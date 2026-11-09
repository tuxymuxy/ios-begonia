/*
 * Begonia (MT6785) Wi-Fi IOKit Registry Integration & CONNSYS Memory Manager.
 * Publishes IO80211Controller, local-mac-address, and Infrastructure en0 for wifid / configd.
 * Allocates contiguous DMA and EMI buffers for CONNSYS WFDMA and MCU RAM patches.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSBoolean.h>
#include <pexpert/pexpert.h>

extern "C" void begonia_wlan_mac(uint8_t mac[6]);
extern "C" void begonia_wlan_attach(void);
extern "C" vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
#define VM_WIMG_WCOMB 0x6
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, unsigned int flags);

class IONetworkController : public IOService {
    OSDeclareDefaultStructors(IONetworkController);
};
OSDefineMetaClassAndStructors(IONetworkController, IOService);

class IOEthernetController : public IONetworkController {
    OSDeclareDefaultStructors(IOEthernetController);
};
OSDefineMetaClassAndStructors(IOEthernetController, IONetworkController);

class IO80211Controller : public IOEthernetController {
    OSDeclareDefaultStructors(IO80211Controller);
};
OSDefineMetaClassAndStructors(IO80211Controller, IOEthernetController);

class IONetworkInterface : public IOService {
    OSDeclareDefaultStructors(IONetworkInterface);
};
OSDefineMetaClassAndStructors(IONetworkInterface, IOService);

class BegoniaWLANInterface : public IONetworkInterface {
    OSDeclareDefaultStructors(BegoniaWLANInterface);
public:
    virtual IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties,
        IOUserClient **handler) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(BegoniaWLANInterface, IONetworkInterface);

IOReturn
BegoniaWLANInterface::newUserClient(task_t, void *, UInt32 type, OSDictionary *, IOUserClient **)
{
    static bool logged;
    if (!logged) {
        logged = true;
        IOLog("BEGONIA WLAN: user client refused (type %u): IO80211.framework falls back to IOCTL path\n", (unsigned)type);
    }
    return kIOReturnUnsupported;
}

static BegoniaWLANInterface *g_begonia_wlan_interface = NULL;

extern "C" void begonia_wlan_registry_link(bool up)
{
    if (g_begonia_wlan_interface != NULL) {
        OSNumber *n = OSNumber::withNumber(up ? 3 : 1, 32);
        if (n != NULL) {
            g_begonia_wlan_interface->setProperty("IOLinkStatus", n);
            n->release();
        }
    }
}

extern "C" void BegoniaWLANStart(void)
{
    IOService *platform = IOService::getPlatform();
    IO80211Controller *controller = OSTypeAlloc(IO80211Controller);
    BegoniaWLANInterface *wlan = OSTypeAlloc(BegoniaWLANInterface);
    OSData *mac;
    OSNumber *num;

    if (!platform || !controller || !wlan || !controller->init() || !wlan->init()) {
        OSSafeReleaseNULL(controller);
        OSSafeReleaseNULL(wlan);
        IOLog("BEGONIA WLAN: Failed to allocate IOKit services\n");
        return;
    }

    uint8_t mac_bytes[6];
    begonia_wlan_mac(mac_bytes);

    if ((mac = OSData::withBytes(mac_bytes, 6)) != NULL) {
        controller->setProperty("IOMACAddress", mac);
        platform->setProperty("local-mac-address", mac);
        mac->release();
    }
    controller->setProperty("IOBuiltin", kOSBooleanTrue);

    controller->setName("BegoniaWLANController");
    if (!controller->attach(platform) || !controller->start(platform)) {
        controller->release();
        wlan->release();
        IOLog("BEGONIA WLAN: Controller failed to attach/start\n");
        return;
    }
    controller->registerService();

    wlan->setName("BegoniaWLAN");
    wlan->setProperty("IOInterfaceName", "en0");
    wlan->setProperty("BSD Name", "en0");
    wlan->setProperty("IOInterfaceNamePrefix", "en");
    if ((num = OSNumber::withNumber(6ULL, 32)) != NULL) {
        wlan->setProperty("IOInterfaceType", num);
        num->release();
    }
    if ((num = OSNumber::withNumber(0ULL, 32)) != NULL) {
        wlan->setProperty("IOInterfaceUnit", num);
        num->release();
    }
    wlan->setProperty("IOBuiltin", kOSBooleanTrue);
    wlan->setProperty("IOPrimaryInterface", kOSBooleanTrue);
    wlan->setProperty("IO80211InterfaceRole", "Infrastructure");

    if (!wlan->attach(controller) || !wlan->start(controller)) {
        wlan->release();
        controller->release();
        IOLog("BEGONIA WLAN: Interface failed to attach/start\n");
        return;
    }
    wlan->registerService();
    g_begonia_wlan_interface = wlan;
    controller->release();

    IOLog("BEGONIA WLAN: Published IO80211Controller and BegoniaWLAN (en0) in IORegistry\n");
}

/*
 * CONNSYS Memory Management:
 * 1. EMI window: 4 MiB DRAM at physical 0xab000000 (dedicated hardware reservation).
 * 2. DMA buffer: 288 KiB for WFDMA ring descriptors and packet buffers placed at 0xab300000.
 * Both strictly reside in 32-bit physical memory accessible by MT6785 CONNSYS / WFDMA.
 */
#define BEGONIA_CONN_RESERVED_EMI_PA   0xab000000ULL
#define BEGONIA_CONN_RESERVED_EMI_SZ   0x00400000ULL /* 4 MiB */
#define BEGONIA_CONN_DMA_OFFSET        0x00300000ULL /* 3 MiB into reservation */
#define BEGONIA_CONN_DMA_PA            (BEGONIA_CONN_RESERVED_EMI_PA + BEGONIA_CONN_DMA_OFFSET)
#define BEGONIA_CONN_DMA_SZ            0x00080000ULL /* 512 KiB */

static void *gBegoniaWiFiEMIVA = NULL;
static void *gBegoniaWiFiDMAVA = NULL;

extern "C" void *
begonia_wifi_emi_alloc(uint32_t size, uint64_t *pa)
{
    if (gBegoniaWiFiEMIVA == NULL) {
        gBegoniaWiFiEMIVA = (void *)ml_io_map_unmappable(BEGONIA_CONN_RESERVED_EMI_PA, BEGONIA_CONN_RESERVED_EMI_SZ, VM_WIMG_WCOMB);
        if (gBegoniaWiFiEMIVA != NULL) {
            bzero(gBegoniaWiFiEMIVA, BEGONIA_CONN_RESERVED_EMI_SZ);
            IOLog("BEGONIA WIFI: EMI window mapped directly to hardware reserved PA 0x%llx (size 0x%llx)\n",
                  BEGONIA_CONN_RESERVED_EMI_PA, BEGONIA_CONN_RESERVED_EMI_SZ);
        }
    }
    if (gBegoniaWiFiEMIVA != NULL) {
        *pa = BEGONIA_CONN_RESERVED_EMI_PA;
        return gBegoniaWiFiEMIVA;
    }
    return NULL;
}

extern "C" void
begonia_wifi_emi_flush(uint32_t offset, uint32_t len)
{
    __builtin_arm_dsb(0xf);
}

extern "C" void *
begonia_wifi_dma_alloc(uint32_t size, uint64_t *pa)
{
    if (gBegoniaWiFiDMAVA == NULL) {
        uint64_t emi_pa = 0;
        (void)begonia_wifi_emi_alloc(0, &emi_pa);
        if (gBegoniaWiFiEMIVA != NULL) {
            gBegoniaWiFiDMAVA = (void *)((uintptr_t)gBegoniaWiFiEMIVA + BEGONIA_CONN_DMA_OFFSET);
            bzero(gBegoniaWiFiDMAVA, BEGONIA_CONN_DMA_SZ);
            IOLog("BEGONIA WIFI: DMA buffer mapped directly to reserved PA 0x%llx (size 0x%llx)\n",
                  BEGONIA_CONN_DMA_PA, BEGONIA_CONN_DMA_SZ);
        }
    }
    if (gBegoniaWiFiDMAVA != NULL) {
        *pa = BEGONIA_CONN_DMA_PA;
        return gBegoniaWiFiDMAVA;
    }
    return NULL;
}

extern "C" void *
begonia_wifi_dma_uncached(void)
{
    if (gBegoniaWiFiDMAVA == NULL) {
        uint64_t pa = 0;
        (void)begonia_wifi_dma_alloc(0, &pa);
    }
    return gBegoniaWiFiDMAVA;
}

extern "C" void
begonia_wifi_dma_flush(uint32_t offset, uint32_t len)
{
    __builtin_arm_dsb(0xf);
}
