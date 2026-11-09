/*
 * viva (MT6781): CONNSYS / WiFi bring-up, step 1 (USB link "wifi" command).
 *
 * The WiFi is the SoC's CONNSYS (CONNAC 1.x, legacy WMT, gen4m chip SOC2_1X1 over AXI at 0x18000000) with an
 * MT6631 or MT6635 RF "A-die" (picks the rails and the firmware flavor). Facts, sequences and citations:
 * ~/Developer/ios-viva/wifi/docs/MT6781-WIFI-HW.md (sources: mt6781-devs/android_kernel_xiaomi_mt6781
 * common_main/platform/mt6781.c, mt6781_pos_gen.c; clk-mt6781-pg.c).
 *
 *   wifi [status]  read only: SPM CONN power, bus protection, EMI remap, CONN reset, PMIC rails, and (only when
 *                  CONN is powered and its bus protection released) chip id / HW version / MCU idle magic.
 *   wifi detect    A-die detection as Linux's consys_adie_chipid_checking_flow (6631 rails: VCN18 only):
 *                  VCN18 on, A-die pinmux, CONN CPU held in reset, MTCMOS on, bus protection off, chip id poll,
 *                  EMI HW mode, AFE, XO (co-clock), A-die SPI read of 0xB024 (6631) and 0xB02C (6635), then
 *                  everything back off (pinmux and VCN18 restored to what they were). Refused if CONN is
 *                  already powered. Nothing here touches 0x18xxxxxx unless CONN power and bus protection were
 *                  verified first (an access with protection set can hang the bus: a soft hang, reboot by hand).
 *
 *   wifi on        step 2 (MT6631 A-die): the full Linux power-on (mtk_wcn_consys_hw_reg_ctrl + mt6781_pos_gen.c):
 *                  5 MiB EMI window (iokit/Kernel/arm/VivaWiFi.cpp) remapped at 0xF0000000 for the MCU, the RAM
 *                  patches mcu/bt/wifi from WIFI_FW_DIR (48-byte header dropped) at +0x0/+0x170000/+0x2A0000 and
 *                  their dates at +0x88300/310/320, rails VCN18 + VCN33 (3.3 V) + VCN28 (co-clock efuse PALDO),
 *                  pinmux, MTCMOS, bus protection, chip id, EMI HW mode, AFE, EMI entries, XO, A-die select, bus
 *                  timeout, MCU mem delay, CPU reset released, then the MCU must reach its idle loop
 *                  (0x18002600 == 0x1D1E). CONNSYS stays on (no WMT/STP yet, no EMI MPU SMC: tried without first).
 *   wifi wmt       step 2b (after "wifi on", before "wifi fw"): what Linux's WMT does over STP/BTIF before the WiFi
 *                  driver may start (wmt_core_stp_init + mtk_wcn_soc_sw_init, MT6781 options): BTIF in PIO (clock
 *                  infracfg_ao bit 31, CONNSYS wake pulse), STP "mand" mode: register read 0x80000008 (proves the
 *                  MCU answers), query/set STP -> full mode (seq/ack, header sum, CRC-16 IBM), set chip id 0x6781,
 *                  query A-die, RF calibration, then VCN28 (efuse PALDO) off. 0127 showed the WiFi RAM code starting
 *                  (ready bits 0x2 -> 0x4) but never finishing its init without this.
 *   wifi fw        step 3 (after "wifi on"), gen4m wlanDownloadFW without WMT: driver own (LPCTL), WFDMA rings
 *                  (TX 0/1/3/15, RX 0/1, 16 entries) in contiguous memory, GLO_CFG with the FW-download bypass,
 *                  WIFI_FW_DIR/WIFI_RAM_CODE_soc2_2_2_1.bin (CONNAC tailer format): DL_TO_EMI regions copied into
 *                  the EMI window at Addr & 0xFFFFFF, the others sent on TX ring 3 (INIT_CMD DOWNLOAD_CONFIG, 2 KiB
 *                  PDA chunks, result event on RX ring 1), then WIFI_START (override, RAM entry) and the ready bits
 *                  0x180C1140 & 0x6. gen4m sources: chips/common/fw_dl.c, cmm_asic_connac.c, hif_pdma.h.
 *   wifi cap       step 4 (firmware ready): CMD_ID_GET_NIC_CAPABILITY_V2 (0x8A) on TX ring 15 (WIFI_CMD: 32-byte TXD +
 *                  32-byte command header, wlan_lib.c wlanQueryNicCapabilityV2), event 0xEC on RX ring 1 (20-byte RXD +
 *                  12-byte event header + {u16 count, rsv, TLVs (u32 tag, u32 len, body)}): HW/SW version, MAC, PHY caps.
 *   wifi init      step 5: basic config, NVRAM (WIFI_FW_DIR/WIFI from nvdata, else the vendor default + a random
 *                  locally administered MAC), domain info, DBDC off, RX filter, BSS 0 activated
 *                  (MT6781-WIFI-CMDS.md §2-§3).
 *   wifi scan [2g] [SSID]  SCAN_REQ_V2, beacons/probe responses into a table until SCAN_DONE, listed by RSSI.
 *   wifi off       CONN CPU back in reset, MTCMOS off, rails and pinmux as before "wifi on".
 *
 * PMIC writes go through VivaPwrap::writeRegulator (VCN18/VCN33/VCN28 enable/mode bits, VCN33 = 3.3 V only).
 */
#if 1
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/vnode.h>
#include <sys/vnode_internal.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/random.h>
#include <kern/locks.h>
#include <kern/thread.h>
#include <sys/kpi_mbuf.h>
#include <sys/proc.h>
#include <pexpert/device_tree.h>

#include "begonia_wpa.h"
#include "begonia_wlan.h"
#include "begonia_wifi_fw.h"
#include "begonia_wifi.h"

extern void *begonia_wifi_emi_alloc(uint32_t size, uint64_t *pa);
extern void begonia_wifi_emi_flush(uint32_t offset, uint32_t len);
extern void *begonia_wifi_dma_alloc(uint32_t size, uint64_t *pa);
extern void begonia_wifi_dma_flush(uint32_t offset, uint32_t len);
extern void *begonia_wifi_dma_uncached(void);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

#define PWRAP_PHYS_BASE 0x1000d000u
static vm_offset_t g_begonia_pwrap_base = 0;

static inline vm_offset_t begonia_pwrap_get(void) {
    if (!g_begonia_pwrap_base) g_begonia_pwrap_base = ml_io_map(PWRAP_PHYS_BASE, 0x1000);
    return g_begonia_pwrap_base;
}

static int begonia_pwrap_read(uint32_t addr, uint32_t *value) {
    vm_offset_t base = begonia_pwrap_get();
    if (!base || (addr & 1) || addr > 0xfffe) return -1;
    for (int i = 0; i < 1000; i++) {
        uint32_t st = *(volatile uint32_t *)(base + 0xc24);
        if ((st & (1u << 22)) && ((st >> 16) & 7) == 0) break;
        delay(10);
    }
    *(volatile uint32_t *)(base + 0xc20) = (addr >> 1) << 16;
    for (int i = 0; i < 1000; i++) {
        uint32_t st = *(volatile uint32_t *)(base + 0xc24);
        if (((st >> 16) & 7) == 6) {
            *value = st & 0xffff;
            *(volatile uint32_t *)(base + 0xc28) = 1;
            return 0;
        }
        delay(10);
    }
    return -1;
}

static int begonia_pwrap_write(uint32_t addr, uint32_t val) {
    vm_offset_t base = begonia_pwrap_get();
    if (!base || (addr & 1) || addr > 0xfffe) return -1;
    for (int i = 0; i < 1000; i++) {
        uint32_t st = *(volatile uint32_t *)(base + 0xc24);
        if ((st & (1u << 22)) && ((st >> 16) & 7) == 0) break;
        delay(10);
    }
    *(volatile uint32_t *)(base + 0xc20) = (1u << 31) | ((addr >> 1) << 16) | (val & 0xffff);
    for (int i = 0; i < 1000; i++) {
        uint32_t st = *(volatile uint32_t *)(base + 0xc24);
        if ((st & (1u << 22)) && ((st >> 16) & 7) == 0) return 0;
        delay(10);
    }
    return -1;
}

static int begonia_pwrap_write_regulator(uint32_t addr, uint32_t mask, uint32_t value) {
    uint32_t cur = 0;
    if (begonia_pwrap_read(addr, &cur) != 0) return -1;
    uint32_t next = (cur & ~mask) | (value & mask);
    return begonia_pwrap_write(addr, next);
}

#define WIFI_FW_DIR             "/usr/share/firmware/wifi/viva/"
#define EMI_SIZE                0x400000
#define EMI_CONN_BASE           0xf0000000u

/* AP side */
#define SPM_POWERON_CONFIG_EN   0x10006000
#define SPM_CONN_PWR_CON        0x10006304
#define SPM_PWR_STATUS          0x1000616c
#define SPM_PWR_STATUS_2ND      0x10006170
#define CONN_PWR_STA            (1u << 1)
#define PWR_RST_B               (1u << 0)
#define PWR_ISO                 (1u << 1)
#define PWR_ON                  (1u << 2)
#define PWR_ON_2ND              (1u << 3)
#define PWR_CLK_DIS             (1u << 4)
#define INFRA_PROT_SET          0x100012a0
#define INFRA_PROT_CLR          0x100012a4
#define INFRA_PROT_STA1         0x10001228
#define INFRA_PROT1_SET         0x100012a8
#define INFRA_PROT1_CLR         0x100012ac
#define INFRA_PROT1_STA1        0x10001258
#define INFRA_CCIF4_CG_CLR      0x100010c4
#define INFRA_CONN_CCU_CONFIG   0x10001c14
#define INFRA_EMI_MAPPING       0x10001380
#define INFRA_PERI_MAPPING      0x10001388
#define TOPRGU_SWSYSRST         0x10007018
#define GPIO_MODE23             0x10005470
#define GPIO_MODE24             0x10005480
#define IOCFG_LT_DRV            0x10002c00
/* CONN side (only with CONN powered and protection released) */
#define CONN_VERSION            0x180b1010
#define CONN_CFG_ID             0x180b101c
#define CONN_HW_VER             0x18002000
#define CONN_FW_VER             0x18002004
#define CONN_MCU_COM_REG0       0x18002600
#define CONN_MCU_EMI_SLPPROT    0x180c1168
#define CONN_AFE_WBG_PLL_04     0x180b3030
#define CONN_AFE_WBG_GL5_02     0x180b3104
#define CONN_XO_1200            0x180c1200
#define CONN_XO_1204            0x180c1204
#define CONN_XO_1240            0x180c1240
#define CONN_ADIE_CTL           0x180c1500
#define CONN_SPI_STA            0x180c6000
#define CONN_SPI_ADDR           0x180c6050
#define CONN_SPI_WDAT           0x180c6054
#define CONN_SPI_RDAT           0x180c6058
#define CONN_VERSION_VALUE      0x100b0000
#define CONN_BUSHANGCR          0x18002440
#define CONN_BT_EMI_ENTRY       0x18002504
#define CONN_WF_EMI_ENTRY       0x18002508
#define CONN_MCU_DBG_216C       0x1800216c
#define CONN_HOST_CSR           0x18007000      /* readability check: write 0x30000, bits 16/17 = HCLK/OSC on */
#define CONN_MCU_PC             0x18007104
#define CONN_ADIE_SEL           0x180c1130
#define CONN_RGU_DLY_CHAIN      0x180c0284
#define CONN_IDLE_MAGIC         0x00001d1e
/* PMIC MT6358 on Begonia */
#define PMIC_VCN18_CON0         0x1c58
#define PMIC_VCN33_CON0_0       0x1d1c
#define PMIC_VCN33_CON0_1       0x1d2a
#define PMIC_VCN33_ANA_CON0     0x1e28
#define PMIC_VCN28_CON0         0x1d88

/* WiFi (WFDMA) side, step 3 */
#define WF_LPCTL                0x18007000      /* BIT0 HOST_SET_OWN (read: 1 = firmware own), BIT1 HOST_CLR_OWN */
#define WF_INT_STA              0x18004200
#define WF_INT_MSK              0x18004204
#define WF_GLO_CFG              0x18004208
#define WF_RST_PTR              0x1800420c
#define WF_PAUSE_TX_Q           0x18004224
#define WF_APSRC_SLPPROT        0x18004160
#define WF_TX_RING(n)           (0x18004300 + 0x10 * (n))      /* +0 BASE +4 CNT +8 CIDX +C DIDX */
#define WF_RX_RING(n)           (0x18004400 + 0x10 * (n))
#define WF_TX_EXT(n)            (0x18004500 + 4 * (n))
#define WF_RX_EXT(n)            (0x18004580 + 4 * (n))
#define WF_READY                0x180c1140      /* (v & 0x6) == 0x6: WiFi firmware ready */

static inline uint32_t rd(uint64_t pa) { return ml_phys_read_word_64(pa); }
static inline void wr(uint64_t pa, uint32_t v) { __builtin_arm_dsb(0xf); ml_phys_write_word_64(pa, v); }   /* WC descriptors first */
static inline void rmw(uint64_t pa, uint32_t mask, uint32_t val) { wr(pa, (rd(pa) & ~mask) | (val & mask)); }

static bool
poll(uint64_t pa, uint32_t mask, uint32_t want, uint32_t us)
{
	for (uint32_t t = 0; t < us; t += 10) {
		if ((rd(pa) & mask) == want) {
			return true;
		}
		delay(10);
	}
	return (rd(pa) & mask) == want;
}

static uint32_t
pmic(uint32_t addr)
{
	uint32_t v = 0xffffffff;
	(void)begonia_pwrap_read(addr, &v);
	return v;
}

#define say(...) do { if (o < len) o += (size_t)snprintf(out + o, len - o, __VA_ARGS__); } while (0)

static bool
conn_powered(void)
{
	return (rd(SPM_PWR_STATUS) & CONN_PWR_STA) && (rd(SPM_PWR_STATUS_2ND) & CONN_PWR_STA);
}

/* Bus protection bits CONNSYS needs released (clk-mt6781-pg.c conn steps 1-4). */
static bool
conn_unprotected(void)
{
	return (rd(INFRA_PROT_STA1) & 0x16000) == 0 && (rd(INFRA_PROT1_STA1) & 0x40000) == 0;
}

static size_t
wifi_status(char *out, size_t len, size_t o)
{
	uint32_t pwr = rd(SPM_CONN_PWR_CON);
	say("conn power: PWR_CON 0x%08x (on %u/%u iso %u rst_b %u clk_dis %u), status %u/%u, bus protect 0x%08x/0x%08x (%s)\n",
	    pwr, !!(pwr & PWR_ON), !!(pwr & PWR_ON_2ND), !!(pwr & PWR_ISO), !!(pwr & PWR_RST_B), !!(pwr & PWR_CLK_DIS),
	    !!(rd(SPM_PWR_STATUS) & CONN_PWR_STA), !!(rd(SPM_PWR_STATUS_2ND) & CONN_PWR_STA), rd(INFRA_PROT_STA1),
	    rd(INFRA_PROT1_STA1), conn_unprotected() ? "conn released" : "conn protected");
	say("emi remap 0x%08x, toprgu swsysrst 0x%08x (conn reset bit12 %u), pinmux mode23 0x%08x mode24 0x%08x drv 0x%08x\n",
	    rd(INFRA_EMI_MAPPING), rd(TOPRGU_SWSYSRST), !!(rd(TOPRGU_SWSYSRST) & (1u << 12)), rd(GPIO_MODE23),
	    rd(GPIO_MODE24), rd(IOCFG_LT_DRV));
	say("pmic: vcn18 %04x; vcn33_0 %04x vcn33_1 %04x vcn28 %04x\n",
	    pmic(PMIC_VCN18_CON0), pmic(PMIC_VCN33_CON0_0), pmic(PMIC_VCN33_CON0_1), pmic(PMIC_VCN28_CON0));
	if (conn_powered() && conn_unprotected()) {
		wr(CONN_HOST_CSR, 0x30000);
		delay(200);
		say("conn: host csr 0x%08x (hclk %u osc %u), mcu pc 0x%08x, dbg 0x%08x\n", rd(CONN_HOST_CSR),
		    !!(rd(CONN_HOST_CSR) & (1u << 16)), !!(rd(CONN_HOST_CSR) & (1u << 17)), rd(CONN_MCU_PC),
		    rd(CONN_MCU_DBG_216C));
		say("wifi: ready bits 0x%08x (0x6 = firmware ready), LPCTL 0x%08x, GLO_CFG 0x%08x, int_sta 0x%08x; "
		    "tx3 cidx %u didx %u, tx15 cidx %u didx %u, rx0 cidx %u didx %u, rx1 cidx %u didx %u\n", rd(WF_READY),
		    rd(WF_LPCTL), rd(WF_GLO_CFG), rd(WF_INT_STA), rd(WF_TX_RING(3) + 8), rd(WF_TX_RING(3) + 12),
		    rd(WF_TX_RING(15) + 8), rd(WF_TX_RING(15) + 12), rd(WF_RX_RING(0) + 8), rd(WF_RX_RING(0) + 12),
		    rd(WF_RX_RING(1) + 8), rd(WF_RX_RING(1) + 12));
		say("conn: version 0x%08x (want 0x%08x), cfg id %u, hw 0x%04x fw 0x%04x, mcu com_reg0 0x%08x (0x1d1e = idle loop)\n",
		    rd(CONN_VERSION), CONN_VERSION_VALUE, rd(CONN_CFG_ID) & 0xf, rd(CONN_HW_VER) & 0xffff,
		    rd(CONN_FW_VER) & 0xffff, rd(CONN_MCU_COM_REG0));
	} else {
		say("conn registers not read (powered off or protected)\n");
	}
	return o;
}

static uint32_t
adie_read(uint32_t addr)
{
	if (!poll(CONN_SPI_STA, 1u << 0, 0, 5000)) {
		return 0xfffffffe;
	}
	wr(CONN_SPI_ADDR, addr);
	wr(CONN_SPI_WDAT, 0);
	if (!poll(CONN_SPI_STA, 1u << 5, 0, 5000)) {
		return 0xfffffffd;
	}
	return rd(CONN_SPI_RDAT);
}

/* Power-off (mtk_wcn_consys_hw.c / clk-mt6781-pg.c conn off): protection on with acks, MTCMOS off. */
static size_t
conn_off(char *out, size_t len, size_t o)
{
	static const struct { uint64_t set, sta; uint32_t bit; } prot[] = {
		{ INFRA_PROT1_SET, INFRA_PROT1_STA1, 0x40000 }, { INFRA_PROT_SET, INFRA_PROT_STA1, 0x4000 },
		{ INFRA_PROT_SET, INFRA_PROT_STA1, 0x2000 }, { INFRA_PROT_SET, INFRA_PROT_STA1, 0x10000 },
	};
	for (unsigned i = 0; i < sizeof(prot) / sizeof(prot[0]); i++) {
		wr(prot[i].set, prot[i].bit);
		if (!poll(prot[i].sta, prot[i].bit, prot[i].bit, 10000)) {
			say("off: bus protection 0x%x not acknowledged\n", prot[i].bit);
		}
	}
	rmw(SPM_CONN_PWR_CON, PWR_ISO, PWR_ISO);
	rmw(SPM_CONN_PWR_CON, PWR_CLK_DIS, PWR_CLK_DIS);
	rmw(SPM_CONN_PWR_CON, PWR_RST_B, 0);
	rmw(SPM_CONN_PWR_CON, PWR_ON, 0);
	rmw(SPM_CONN_PWR_CON, PWR_ON_2ND, 0);
	bool off = poll(SPM_PWR_STATUS, CONN_PWR_STA, 0, 10000) && poll(SPM_PWR_STATUS_2ND, CONN_PWR_STA, 0, 10000);
	say("off: MTCMOS %s (PWR_CON 0x%08x)\n", off ? "off" : "STILL ON", rd(SPM_CONN_PWR_CON));
	return o;
}

static size_t
wifi_detect(char *out, size_t len, size_t o)
{
	if (rd(SPM_PWR_STATUS) & CONN_PWR_STA) {
		say("detect: CONN is already powered: refused (wifi status)\n");
		return o;
	}
	uint32_t old18 = pmic(PMIC_VCN18_CON0);
	uint32_t oldM23 = rd(GPIO_MODE23), oldM24 = rd(GPIO_MODE24), oldDrv = rd(IOCFG_LT_DRV);
	/* 1. VCN18 on */
	int e = begonia_pwrap_write_regulator(PMIC_VCN18_CON0, 1, 1);
	delay(300);
	say("detect: vcn18 %s\n", e ? "WRITE FAILED" : "on");
	if (e) {
		goto rails;
	}
	/* 2. A-die interface pins (set_if_pinmux(1)), 150 us */
	rmw(GPIO_MODE24, 0x77777777, 0x11111111);
	rmw(GPIO_MODE23, 0x70000000, 0x10000000);
	rmw(IOCFG_LT_DRV, 0x07ffffff, 0x1209);
	delay(150);
	/* 3. CONN CPU held in reset (key 0x88, bit 12), SPM register access */
	rmw(TOPRGU_SWSYSRST, 0xff001000, 0x88001000);
	rmw(SPM_POWERON_CONFIG_EN, 0xffff0001, 0x0b160001);
	/* 4. MTCMOS on (consys_hw_power_ctrl_mt6781_gen) */
	wr(INFRA_CCIF4_CG_CLR, 1u << 24);
	rmw(SPM_CONN_PWR_CON, PWR_ON, PWR_ON);
	if (!poll(SPM_PWR_STATUS, CONN_PWR_STA, CONN_PWR_STA, 5000)) {
		say("detect: PWR_STATUS did not come up\n");
		goto off;
	}
	rmw(SPM_CONN_PWR_CON, PWR_ON_2ND, PWR_ON_2ND);
	if (!poll(SPM_PWR_STATUS_2ND, CONN_PWR_STA, CONN_PWR_STA, 5000)) {
		say("detect: PWR_STATUS_2ND did not come up\n");
		goto off;
	}
	rmw(SPM_CONN_PWR_CON, PWR_CLK_DIS, 0);
	delay(1);
	rmw(SPM_CONN_PWR_CON, PWR_ISO, 0);
	rmw(SPM_CONN_PWR_CON, PWR_RST_B, PWR_RST_B);
	delay(5000);
	/* 5. bus protection off, each acknowledged */
	{
		static const struct { uint64_t clr, sta; uint32_t bit; } prot[] = {
			{ INFRA_PROT_CLR, INFRA_PROT_STA1, 0x10000 }, { INFRA_PROT_CLR, INFRA_PROT_STA1, 0x4000 },
			{ INFRA_PROT1_CLR, INFRA_PROT1_STA1, 0x40000 }, { INFRA_PROT_CLR, INFRA_PROT_STA1, 0x2000 },
		};
		for (unsigned i = 0; i < sizeof(prot) / sizeof(prot[0]); i++) {
			wr(prot[i].clr, prot[i].bit);
			if (!poll(prot[i].sta, prot[i].bit, 0, 10000)) {
				say("detect: bus protection 0x%x did not release: CONN registers not touched\n", prot[i].bit);
				goto off;
			}
		}
	}
	wr(INFRA_CONN_CCU_CONFIG, 0);
	delay(10);
	/* 6. chip id (polling_consys_chipid: 10 x 20 ms) */
	{
		bool idOK = false;
		for (int i = 0; i < 10 && !(idOK = rd(CONN_VERSION) == CONN_VERSION_VALUE); i++) {
			delay(20000);
		}
		say("detect: conn version 0x%08x (%s), cfg id %u, hw 0x%04x, fw 0x%04x\n", rd(CONN_VERSION),
		    idOK ? "ok" : "UNEXPECTED", rd(CONN_CFG_ID) & 0xf, rd(CONN_HW_VER) & 0xffff, rd(CONN_FW_VER) & 0xffff);
		if (!idOK) {
			goto off;
		}
	}
	/* 7. EMI HW mode, AFE, XO (legacy co-clock branch: WMT_SOC.cfg co_clock_flag=1) */
	rmw(CONN_MCU_EMI_SLPPROT, 1u << 0, 1u << 0);
	wr(CONN_AFE_WBG_PLL_04, 0x00040088);
	wr(CONN_AFE_WBG_GL5_02, 0xa8808800);
	rmw(CONN_XO_1240, 1u << 7, 0);
	rmw(CONN_XO_1204, 0xff00, 0x0600);
	rmw(CONN_XO_1200, 0xffff, 0x0708);
	rmw(CONN_XO_1204, 1u << 0, 0);
	/* 8. A-die id over the RF SPI (consys_adie_chipid_checking_flow) */
	rmw(CONN_ADIE_CTL, 1u << 0, 1u << 0);
	{
		uint32_t a = adie_read(0xb024), b = adie_read(0xb02c);
		say("detect: a-die spi 0xb024 -> 0x%08x (id 0x%04x), 0xb02c -> 0x%08x (id 0x%04x): %s\n", a, a >> 16, b, b >> 16,
		    (a >> 16) == 0x6631 ? "MT6631 (firmware *_soc2_2_*_2_1*)" : (b >> 16) == 0x6635 ?
		    "MT6635 (firmware *_2a_1*; its 1.3 V rail was off, so this read is lucky)" :
		    "no answer (MT6635 needs VS2 1.4625 V + GPIO12 on to answer)");
	}
	rmw(CONN_ADIE_CTL, 1u << 0, 0);
off:
	o = conn_off(out, len, o);
	wr(GPIO_MODE24, (rd(GPIO_MODE24) & ~0x77777777u) | (oldM24 & 0x77777777u));
	wr(GPIO_MODE23, (rd(GPIO_MODE23) & ~0x70000000u) | (oldM23 & 0x70000000u));
	wr(IOCFG_LT_DRV, (rd(IOCFG_LT_DRV) & ~0x07ffffffu) | (oldDrv & 0x07ffffffu));
rails:
	begonia_pwrap_write(PMIC_VCN18_CON0, old18);
	say("detect: rails and pinmux restored (vcn18 con0 %04x)\n", pmic(PMIC_VCN18_CON0));
	return o;
}

/* ---- step 2: CONNSYS on with the RAM patches (MT6631 A-die) ---- */

struct wmt_rom_patch {                  /* common_main/core/include/wmt_core.h: 48 bytes, stripped */
	char     date[16];
	char     plat[4];
	uint16_t hwVer, swVer;          /* big endian */
	uint32_t patchAddr;             /* byte 0 = sequence flag (0x11), bytes 1..2 = EMI offset >> 8 */
	uint32_t patchType;
	uint32_t crc[4];
};

static struct {
	bool     on, wmt;
	void    *emi;
	uint64_t emiPA;
	uint32_t old18, old33, old33_1, old28;
	uint32_t oldM23, oldM24, oldDrv;
} vw;

/* Load patch from embedded memory array into the EMI window at wantOff; date at +dateOff. */
static int
load_patch_mem(const char *name, const uint8_t *data, size_t size, uint32_t wantOff, uint32_t dateOff, char *out, size_t len, size_t *po)
{
	size_t o = *po;
	if (size <= sizeof(struct wmt_rom_patch)) {
		say("on: %s: truncated\n", name);
		*po = o;
		return EINVAL;
	}
	const struct wmt_rom_patch *h = (const struct wmt_rom_patch *)data;
	uint32_t off = h->patchAddr & 0x00ffff00u;
	uint32_t body = (uint32_t)(size - sizeof(struct wmt_rom_patch));
	if (off != wantOff || (uint64_t)off + body > EMI_SIZE || dateOff + 16 > EMI_SIZE) {
		say("on: %s: unexpected header (patch addr 0x%08x -> offset 0x%x, want 0x%x, %u bytes)\n", name,
		    h->patchAddr, off, wantOff, body);
		*po = o;
		return EINVAL;
	}
	memcpy((char *)vw.emi + off, data + sizeof(struct wmt_rom_patch), body);
	memcpy((char *)vw.emi + dateOff, h->date, 16);
	begonia_wifi_emi_flush(off, body);
	begonia_wifi_emi_flush(dateOff & ~63u, 64);
	say("on: %s: %u bytes at EMI +0x%06x (MCU 0x%08x), date %.16s\n", name, body, off,
	    EMI_CONN_BASE + off, h->date);
	*po = o;
	return 0;
}

static void
rails_restore(void)
{
	begonia_pwrap_write(PMIC_VCN28_CON0, vw.old28);
	begonia_pwrap_write(PMIC_VCN33_CON0_1, vw.old33_1);
	begonia_pwrap_write(PMIC_VCN33_CON0_0, vw.old33);
	begonia_pwrap_write(PMIC_VCN18_CON0, vw.old18);
}

static void
pinmux_restore(void)
{
	wr(GPIO_MODE24, (rd(GPIO_MODE24) & ~0x77777777u) | (vw.oldM24 & 0x77777777u));
	wr(GPIO_MODE23, (rd(GPIO_MODE23) & ~0x70000000u) | (vw.oldM23 & 0x70000000u));
	wr(IOCFG_LT_DRV, (rd(IOCFG_LT_DRV) & ~0x07ffffffu) | (vw.oldDrv & 0x07ffffffu));
}

static void wl_reset(void);      /* station state, defined with it */

static size_t
wifi_off(char *out, size_t len, size_t o)
{
	if (!vw.on) {
		say("off: not on (wifi on)\n");
		return o;
	}
	rmw(TOPRGU_SWSYSRST, 0xff001000, 0x88001000);  /* CPU back in reset first */
	o = conn_off(out, len, o);
	pinmux_restore();
	rails_restore();
	vw.on = false;
	wl_reset();
	say("off: rails and pinmux restored; the EMI window stays allocated for the next \"wifi on\"\n");
	return o;
}

static size_t
wifi_on(char *out, size_t len, size_t o)
{
	if (vw.on) {
		return o;
	}
	if (rd(SPM_PWR_STATUS) & CONN_PWR_STA) {
		say("on: CONN was already powered, power cycling via conn_off\n");
		rmw(TOPRGU_SWSYSRST, 0xff001000, 0x88001000);  /* CPU back in reset first */
		o = conn_off(out, len, o);
		delay(2000);
	}

	/* EMI window + RAM patches */
	vw.emi = begonia_wifi_emi_alloc(EMI_SIZE, &vw.emiPA);
	if (vw.emi == NULL) {
		say("on: no EMI window allocated\n");
		return o;
	}
	bzero(vw.emi, EMI_SIZE);
	begonia_wifi_emi_flush(0, EMI_SIZE);
	say("on: EMI window PA 0x%llx (%s aligned), MCU sees it at 0x%08x\n", vw.emiPA,
	    (vw.emiPA & 0xffffff) == 0 ? "16 MiB" : "64 KiB", EMI_CONN_BASE);
	if (load_patch_mem("soc1_0_ram_mcu", g_begonia_fw_ram_mcu, g_begonia_fw_ram_mcu_len, 0x000000, 0x88320, out, len, &o) ||
	    load_patch_mem("soc1_0_ram_bt", g_begonia_fw_ram_bt, g_begonia_fw_ram_bt_len, 0x080000, 0x88300, out, len, &o) ||
	    load_patch_mem("soc1_0_ram_wifi", g_begonia_fw_ram_wifi, g_begonia_fw_ram_wifi_len, 0x140000, 0x88310, out, len, &o)) {
		return o;
	}
	rmw(INFRA_EMI_MAPPING, 0xfffff, (uint32_t)(vw.emiPA >> 16));
	rmw(INFRA_PERI_MAPPING, 0xfffff, 0x1000);
	/* rails: VCN18 (1.8 V) + VCN33_1 (3.3 V) via MT6359 PMIC */
	vw.old18 = pmic(PMIC_VCN18_CON0);
	vw.old33 = pmic(PMIC_VCN33_CON0_0);
	vw.old33_1 = pmic(PMIC_VCN33_CON0_1);
	vw.old28 = pmic(PMIC_VCN28_CON0);
	vw.oldM23 = rd(GPIO_MODE23);
	vw.oldM24 = rd(GPIO_MODE24);
	vw.oldDrv = rd(IOCFG_LT_DRV);
	int e = 0;
	e |= begonia_pwrap_write_regulator(PMIC_VCN18_CON0, 1, 1);
	e |= begonia_pwrap_write_regulator(PMIC_VCN33_ANA_CON0, 0x300, 0x100); /* 3.3V */
	e |= begonia_pwrap_write_regulator(PMIC_VCN33_CON0_0, 1, 1);
	e |= begonia_pwrap_write_regulator(PMIC_VCN33_CON0_1, 1, 1);
	e |= begonia_pwrap_write_regulator(PMIC_VCN28_CON0, 1, 1);
	delay(500);
	say("on: rails %s: vcn18 %04x, vcn33_0 %04x, vcn33_1 %04x, vcn28 %04x\n", e ? "WRITE FAILED" : "on",
	    pmic(PMIC_VCN18_CON0), pmic(PMIC_VCN33_CON0_0), pmic(PMIC_VCN33_CON0_1), pmic(PMIC_VCN28_CON0));
	vw.on = true;           /* from here "wifi off" undoes everything */
	if (e) {
		goto fail;
	}
	rmw(GPIO_MODE24, 0x77777777, 0x11111111);
	rmw(GPIO_MODE23, 0x70000000, 0x10000000);
	rmw(IOCFG_LT_DRV, 0x07ffffff, 0x1209);
	delay(150);
	rmw(TOPRGU_SWSYSRST, 0xff001000, 0x88001000);
	rmw(SPM_POWERON_CONFIG_EN, 0xffff0001, 0x0b160001);
	wr(INFRA_CCIF4_CG_CLR, 1u << 24);
	rmw(SPM_CONN_PWR_CON, PWR_ON, PWR_ON);
	if (!poll(SPM_PWR_STATUS, CONN_PWR_STA, CONN_PWR_STA, 5000)) {
		say("on: PWR_STATUS did not come up\n");
		goto fail;
	}
	rmw(SPM_CONN_PWR_CON, PWR_ON_2ND, PWR_ON_2ND);
	if (!poll(SPM_PWR_STATUS_2ND, CONN_PWR_STA, CONN_PWR_STA, 5000)) {
		say("on: PWR_STATUS_2ND did not come up\n");
		goto fail;
	}
	rmw(SPM_CONN_PWR_CON, PWR_CLK_DIS, 0);
	delay(1);
	rmw(SPM_CONN_PWR_CON, PWR_ISO, 0);
	rmw(SPM_CONN_PWR_CON, PWR_RST_B, PWR_RST_B);
	delay(5000);
	{
		static const struct { uint64_t clr, sta; uint32_t bit; } prot[] = {
			{ INFRA_PROT_CLR, INFRA_PROT_STA1, 0x10000 }, { INFRA_PROT_CLR, INFRA_PROT_STA1, 0x4000 },
			{ INFRA_PROT1_CLR, INFRA_PROT1_STA1, 0x40000 }, { INFRA_PROT_CLR, INFRA_PROT_STA1, 0x2000 },
		};
		for (unsigned i = 0; i < sizeof(prot) / sizeof(prot[0]); i++) {
			wr(prot[i].clr, prot[i].bit);
			if (!poll(prot[i].sta, prot[i].bit, 0, 10000)) {
				say("on: bus protection 0x%x did not release\n", prot[i].bit);
				goto fail;
			}
		}
	}
	wr(INFRA_CONN_CCU_CONFIG, 0);
	delay(10);
	{
		bool idOK = false;
		for (int i = 0; i < 10 && !(idOK = rd(CONN_VERSION) == CONN_VERSION_VALUE); i++) {
			delay(20000);
		}
		if (!idOK) {
			say("on: conn version 0x%08x, not 0x%08x\n", rd(CONN_VERSION), CONN_VERSION_VALUE);
			goto fail;
		}
	}
	rmw(CONN_MCU_EMI_SLPPROT, 1u << 0, 1u << 0);           /* 11. EMI HW mode */
	wr(CONN_AFE_WBG_PLL_04, 0x00040088);                    /* 13. AFE */
	wr(CONN_AFE_WBG_GL5_02, 0xa8808800);
	wr(CONN_BT_EMI_ENTRY, EMI_CONN_BASE + 0x080000);        /* 14. EMI entries */
	wr(CONN_WF_EMI_ENTRY, EMI_CONN_BASE + 0x140000);
	rmw(CONN_XO_1240, 1u << 7, 0);                          /* 15. XO, legacy co-clock */
	rmw(CONN_XO_1204, 0xff00, 0x0600);
	rmw(CONN_XO_1200, 0xffff, 0x0708);
	rmw(CONN_XO_1204, 1u << 0, 0);
	rmw(CONN_ADIE_SEL, 0x300, 0x100);                       /* 16. A-die = MT6631 */
	wr(CONN_BUSHANGCR, 0x90000002);                         /* 18. bus timeout */
	rmw(CONN_RGU_DLY_CHAIN, 0xf0000, 0x40000);              /* 19. MCU mem PDN delay */
	rmw(TOPRGU_SWSYSRST, 0xff001000, 0x88000000);           /* 20. CPU out of reset: ROM, then the patches */
	{
		bool idle = false;
		uint32_t t;
		for (t = 0; t < 41 && !(idle = rd(CONN_MCU_COM_REG0) == CONN_IDLE_MAGIC); t++) {
			delay(6000);
		}
		say("on: MCU %s after ~%u ms: com_reg0 0x%08x, pc 0x%08x, dbg 0x%08x\n", idle ? "IDLE LOOP" :
		    "NOT IDLE", t * 6, rd(CONN_MCU_COM_REG0), rd(CONN_MCU_PC), rd(CONN_MCU_DBG_216C));
		if (!idle) {
			say("on: left on for inspection (wifi status, peek); wifi off to power down. Next suspects: EMI MPU "
			    "SMC, patch placement\n");
		}
	}
	delay(20000);
	return o;
fail:
	o = wifi_off(out, len, o);
	return o;
}

/* ---- step 2b: WMT over STP/BTIF ---- */

#define BTIF_BASE               0x1100c000
#define BTIF_RBR                (BTIF_BASE + 0x00)
#define BTIF_THR                (BTIF_BASE + 0x00)
#define BTIF_IER                (BTIF_BASE + 0x04)
#define BTIF_FIFOCTRL           (BTIF_BASE + 0x08)
#define BTIF_FAKELCR            (BTIF_BASE + 0x0c)
#define BTIF_LSR                (BTIF_BASE + 0x14)
#define BTIF_DMA_EN             (BTIF_BASE + 0x4c)
#define BTIF_TRI_LVL            (BTIF_BASE + 0x60)
#define BTIF_WAK                (BTIF_BASE + 0x64)
#define BTIF_HANDSHAKE          (BTIF_BASE + 0x6c)
#define LSR_DR                  (1u << 0)
#define LSR_THRE                (1u << 5)
#define INFRA_CG0_CLR           0x10001084
#define INFRA_CG0_STA           0x10001090
#define INFRA_CG0_BTIF          (1u << 31)
#define STP_TYPE_WMT            4

static struct {
	bool    full;
	uint8_t txseq, txack, rxseq;
	uint16_t crc[256];
} st;

static void
crc_init(void)
{
	for (uint32_t i = 0; i < 256; i++) {            /* CRC-16 IBM (reflected 0xA001): osal.c crc16_table */
		uint16_t c = (uint16_t)i;
		for (int k = 0; k < 8; k++) {
			c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xa001) : (uint16_t)(c >> 1);
		}
		st.crc[i] = c;
	}
}

static uint16_t
crc16(const uint8_t *b, uint32_t n)
{
	uint16_t c = 0;
	while (n--) {
		c = (uint16_t)((c >> 8) ^ st.crc[(c ^ *b++) & 0xff]);
	}
	return c;
}

static bool
btif_put(const uint8_t *b, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		if (!poll(BTIF_LSR, LSR_THRE, LSR_THRE, 20000)) {
			return false;
		}
		wr(BTIF_THR, b[i]);
	}
	return true;
}

static int
btif_get(uint32_t us)
{
	for (uint32_t t = 0; t <= us; t += 10) {
		if (rd(BTIF_LSR) & LSR_DR) {
			return (int)(rd(BTIF_RBR) & 0xff);
		}
		delay(10);
	}
	return -1;
}

/* hal_btif_hw_init in PIO, plus the AP -> CONNSYS wake pulse (hal_btif_raise_wak_sig). */
static void
btif_init(void)
{
	if (rd(INFRA_CG0_STA) & INFRA_CG0_BTIF) {
		wr(INFRA_CG0_CLR, INFRA_CG0_BTIF);
	}
	wr(BTIF_FAKELCR, 0);
	rmw(BTIF_HANDSHAKE, 1, 1);
	rmw(BTIF_FIFOCTRL, 1u << 1, 1u << 1);
	rmw(BTIF_FIFOCTRL, 1u << 1, 0);
	rmw(BTIF_FIFOCTRL, 1u << 2, 1u << 2);
	rmw(BTIF_FIFOCTRL, 1u << 2, 0);
	wr(BTIF_TRI_LVL, 8 | (1u << 4));                /* TX 8, RX 1, loopback off */
	wr(BTIF_DMA_EN, 1u << 2);                       /* PIO both ways, auto reset */
	wr(BTIF_IER, 0);                                /* polled */
	rmw(BTIF_WAK, 1, 0);
	delay(160);
	rmw(BTIF_WAK, 1, 1);
	st.full = false;
	st.txseq = 0;
	st.txack = 7;
	st.rxseq = 0;
	crc_init();
}

static bool
stp_ack(void)
{
	uint8_t h[4] = { (uint8_t)(0x80 | st.txack), 0, 0, 0 };
	h[3] = (uint8_t)(h[0] + h[1] + h[2]);
	return btif_put(h, 4);
}

/* One WMT packet in the current STP mode (stp_core.c mtk_wcn_stp_send_data, mand / full). */
static bool
stp_send(const uint8_t *p, uint32_t n)
{
	uint8_t h[4], c[2] = { 0, 0 };
	h[0] = (uint8_t)(0x80 | (st.txseq << 3) | (st.full ? st.txack : 0));
	h[1] = (uint8_t)((STP_TYPE_WMT << 4) | ((n >> 8) & 0xf));
	h[2] = (uint8_t)n;
	h[3] = st.full ? (uint8_t)(h[0] + h[1] + h[2]) : 0;
	if (st.full) {
		uint16_t crc = crc16(p, n);
		c[0] = (uint8_t)crc;
		c[1] = (uint8_t)(crc >> 8);
		st.txseq = (st.txseq + 1) & 7;
	}
	return btif_put(h, 4) && btif_put(p, n) && btif_put(c, 2);
}

/* Next STP data frame (ack-only frames skipped; full mode: acknowledged). Payload into out, length returned, <0 none. */
static int
stp_recv(uint8_t *out, uint32_t max, uint32_t ms, uint8_t *type)
{
	for (uint32_t tries = 0; tries < 16; tries++) {
		int b0;
		do {                                    /* sync: bit 7 */
			b0 = btif_get(ms * 1000);
		} while (b0 >= 0 && !(b0 & 0x80));
		int b1 = b0 < 0 ? -1 : btif_get(20000), b2 = b1 < 0 ? -1 : btif_get(20000), b3 = b2 < 0 ? -1 : btif_get(20000);
		if (b3 < 0) {
			return -1;
		}
		uint32_t n = ((uint32_t)(b1 & 0xf) << 8) | (uint32_t)b2;
		if (n == 0) {
			continue;                       /* an acknowledgement */
		}
		uint32_t got = 0;
		for (uint32_t i = 0; i < n + 2; i++) {
			int v = btif_get(20000);
			if (v < 0) {
				return -2;
			}
			if (i < n && got < max) {
				out[got++] = (uint8_t)v;
			}
		}
		if (type) {
			*type = (uint8_t)(b1 >> 4);
		}
		if (st.full) {
			st.txack = (uint8_t)((b0 >> 3) & 7);
			st.rxseq = (st.txack + 1) & 7;
			stp_ack();
		}
		return (int)got;
	}
	return -3;
}

/* WMT command -> its event (02 opcode ...); the event bytes are compared with `want` (wlen bytes) if given. */
static int
wmt_cmd(const char *what, const uint8_t *cmd, uint32_t clen, const uint8_t *want, uint32_t wlen, uint32_t ms,
    uint8_t *ev, uint32_t evmax, char *out, size_t len, size_t *po)
{
	size_t o = *po;
	uint8_t type = 0;
	int n = -1;
	if (!stp_send(cmd, clen)) {
		say("wmt: %s: BTIF did not take it (LSR 0x%02x)\n", what, rd(BTIF_LSR) & 0xff);
		*po = o;
		return -1;
	}
	for (int k = 0; k < 4; k++) {
		n = stp_recv(ev, evmax, ms, &type);
		if (n < 0 || (type == STP_TYPE_WMT && n >= 2 && ev[0] == 0x02 && ev[1] == cmd[1])) {
			break;
		}
		say("wmt: %s: other frame type %u, %d bytes (%02x %02x ..)\n", what, type, n, n > 0 ? ev[0] : 0,
		    n > 1 ? ev[1] : 0);
	}
	if (n < 0) {
		say("wmt: %s: no answer (%d, LSR 0x%02x)\n", what, n, rd(BTIF_LSR) & 0xff);
	} else {
		say("wmt: %s ->", what);
		for (int i = 0; i < n && i < 24; i++) {
			say(" %02x", ev[i]);
		}
		bool ok = want == NULL || ((uint32_t)n >= wlen && memcmp(ev, want, wlen) == 0);
		say("%s\n", ok ? "" : "  (UNEXPECTED: shown, sequence goes on)");
	}
	*po = o;
	return n;
}

static size_t
wifi_wmt(char *out, size_t len, size_t o)
{
	if (!vw.on || rd(CONN_MCU_COM_REG0) != CONN_IDLE_MAGIC) {
		say("wmt: CONNSYS not on with the MCU idle (wifi on first)\n");
		return o;
	}
	btif_init();
	say("wmt: BTIF clock %s, LSR 0x%02x\n", (rd(INFRA_CG0_STA) & INFRA_CG0_BTIF) ? "STILL GATED" : "on",
	    rd(BTIF_LSR) & 0xff);
	uint8_t ev[64];
	/* wmt_core_hw_check: register read 0x80000008 (GEN_HCR) in mand mode */
	static const uint8_t regrd[] = { 0x01, 0x08, 0x10, 0x00, 0x02, 0x01, 0x00, 0x01, 0x08, 0x00, 0x00, 0x80,
		                         0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff };
	/* MT6781 answers 02 08 0c 00 | status 00, 00 00 01 | address | value (0127+0130 on the phone) */
	static const uint8_t regrdEv[] = { 0x02, 0x08, 0x0c, 0x00, 0x00 };
	int rn = wmt_cmd("read 0x80000008", regrd, sizeof(regrd), regrdEv, sizeof(regrdEv), 1000, ev, sizeof(ev), out, len, &o);
	if (rn < 0) {
		return o;
	}
	if (rn >= 16) {
		say("wmt: GEN_HCR 0x%02x%02x%02x%02x (the MCU answers over STP/BTIF)\n", ev[15], ev[14], ev[13], ev[12]);
	}
	static const uint8_t qstp[] = { 0x01, 0x04, 0x01, 0x00, 0x04 };
	static const uint8_t qstpDef[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x04, 0x11, 0x00, 0x00, 0x00 };
	static const uint8_t qstpFull[] = { 0x02, 0x04, 0x06, 0x00, 0x00, 0x04, 0xdb, 0x0e, 0x68, 0x01 };
	static const uint8_t sstp[] = { 0x01, 0x04, 0x05, 0x00, 0x03, 0xdb, 0x0e, 0x68, 0x01 };
	static const uint8_t sstpEv[] = { 0x02, 0x04, 0x02, 0x00, 0x00, 0x03 };
	(void)wmt_cmd("query stp", qstp, sizeof(qstp), qstpDef, sizeof(qstpDef), 1000, ev, sizeof(ev), out, len, &o);
	if (wmt_cmd("set stp full", sstp, sizeof(sstp), sstpEv, sizeof(sstpEv), 1000, ev, sizeof(ev), out, len, &o) < 0) {
		return o;
	}
	st.full = true;                                 /* host side switch, 10 ms */
	st.txseq = 0;
	st.txack = 7;
	delay(10000);
	if (wmt_cmd("query stp (full)", qstp, sizeof(qstp), qstpFull, sizeof(qstpFull), 1000, ev, sizeof(ev), out, len,
	    &o) < 0) {
		return o;
	}
	static const uint8_t chipid[] = { 0x01, 0x02, 0x05, 0x00, 0x11, 0x85, 0x67, 0x00, 0x00 };
	static const uint8_t chipidEv[] = { 0x02, 0x02, 0x01, 0x00, 0x00 };
	if (wmt_cmd("set chip id 0x6785", chipid, sizeof(chipid), chipidEv, sizeof(chipidEv), 1000, ev, sizeof(ev), out,
	    len, &o) < 0) {
		return o;
	}
	static const uint8_t adie[] = { 0x01, 0x02, 0x01, 0x00, 0x12 };
	static const uint8_t adieEv[] = { 0x02, 0x02, 0x05, 0x00, 0x00 };
	(void)wmt_cmd("query a-die", adie, sizeof(adie), adieEv, sizeof(adieEv), 1000, ev, sizeof(ev), out, len, &o);
	static const uint8_t cal[] = { 0x01, 0x14, 0x01, 0x00, 0x01 };
	/* MT6781: 02 14 <len 0x22a> 00 01 + calibration data (0130 on the phone) */
	static const uint8_t calEv[] = { 0x02, 0x14 };
	int c = wmt_cmd("rf calibration", cal, sizeof(cal), calEv, sizeof(calEv), 5000, ev, sizeof(ev), out, len, &o);
	/* wmt_ic_soc.c: the efuse PALDO (VCN28) goes off after the calibration */
	begonia_pwrap_write_regulator(PMIC_VCN28_CON0, 1, 0);
	vw.wmt = c >= 0;
	say("wmt: %s; VCN28 off. Next: wifi fw\n", c >= 0 ? "done" : "calibration FAILED");
	return o;
}

/* ---- step 3: WiFi RAM code over WFDMA ---- */

#define GLO_TX_EN               (1u << 0)
#define GLO_TX_BUSY             (1u << 1)
#define GLO_RX_EN               (1u << 2)
#define GLO_RX_BUSY             (1u << 3)
#define GLO_ENABLE              (GLO_TX_EN | GLO_RX_EN | (3u << 4) | (1u << 6) | (2u << 10) | (1u << 12) | \
	                         (1u << 26) | (1u << 30))      /* asicPdmaConfig */
#define GLO_FWDL_BYPASS         (1u << 9)       /* bypass_dmashdl_txring3 while downloading */
#define DESC_DMADONE            (1u << 31)
#define DESC_LASTSEC0           (1u << 30)
#define RING_N                  16
#define BUF_SZ                  0x1000
#define RX_BUF_LEN              3840
#define DMA_SIZE                0x48000
#define OFF_TX(n)               ((n) == 3 ? 0x0000u : (n) == 15 ? 0x0100u : (n) == 0 ? 0x41000u : 0x0300u)
#define TX0_N                   32              /* data ring: 32 entries at 0x41000, 2 KiB buffers in ring 3's area */
#define TX0_BUF                 0x800
#define RING_LEN(n)             ((n) == 0 ? TX0_N : RING_N)
#define OFF_RX(n)               ((n) == 0 ? 0x0400u : 0x0500u)
#define OFF_TXBUF               0x01000u        /* ring 3 then ring 15 buffers: 16 x 4 KiB each */
#define OFF_RXBUF(n)            ((n) == 1 ? 0x21000u : 0x31000u)
#define WIFI_RAM_CODE           "WIFI_RAM_CODE_soc2_2_2_1.bin"
#define PDA_CHUNK               2048

static struct {
	uint8_t *va;
	uint8_t *uva;                   /* the same memory mapped write-combined (uncached): the descriptors */
	uint64_t pa;
	uint32_t txIdx[16], rxIdx[2];
	uint32_t rxGot[2], rxSkipped[2], rxSkipD1[2];
	uint8_t  seq;
} wd;

/*
 * Ring descriptors are 16 bytes, four to a cache line: written through a cached mapping, cleaning the line of entry i
 * also wrote back the CPU's stale copy of entries i+1..i+3 over what the DMA had just put there (DMA done + length),
 * and the RX ring stopped for good at that entry (0151: WiFi data stalled after the first packets). Descriptors are
 * only ever touched through the uncached mapping; the flushes of their (never dirtied) cached lines are harmless.
 */
static uint32_t *
desc(uint32_t off, uint32_t i)
{
	return (uint32_t *)(void *)(wd.uva + off + 16 * i);
}

static void
wfdma_setup(void)
{
	bzero(wd.va, DMA_SIZE);
	begonia_wifi_dma_flush(0, DMA_SIZE);               /* before any descriptor goes in through wd.uva */
	static const uint32_t tx[] = { 0, 1, 3, 15 };
	for (unsigned r = 0; r < 2; r++) {
		for (uint32_t i = 0; i < RING_N; i++) {
			uint32_t *d = desc(OFF_RX(r), i);
			d[0] = (uint32_t)(wd.pa + OFF_RXBUF(r) + BUF_SZ * i);
			d[1] = (uint32_t)RX_BUF_LEN << 16;
			d[2] = 0;
			d[3] = (uint32_t)((wd.pa + OFF_RXBUF(r) + BUF_SZ * i) >> 32) & 0xf;
		}
	}
	begonia_wifi_dma_flush(0, DMA_SIZE);
	wr(WF_GLO_CFG, rd(WF_GLO_CFG) & ~(GLO_TX_EN | GLO_RX_EN));
	(void)poll(WF_GLO_CFG, GLO_TX_BUSY | GLO_RX_BUSY, 0, 100000);
	wr(WF_RST_PTR, 0xffffffff);
	for (unsigned k = 0; k < 4; k++) {
		uint32_t n = tx[k];
		wr(WF_TX_RING(n) + 0, (uint32_t)(wd.pa + OFF_TX(n)));
		wr(WF_TX_EXT(n), (uint32_t)(wd.pa >> 32));
		wr(WF_TX_RING(n) + 4, RING_LEN(n));
		wr(WF_TX_RING(n) + 8, 0);
		wd.txIdx[n] = 0;
	}
	for (unsigned r = 0; r < 2; r++) {
		wr(WF_RX_RING(r) + 0, (uint32_t)(wd.pa + OFF_RX(r)));
		wr(WF_RX_EXT(r), (uint32_t)(wd.pa >> 32));
		wr(WF_RX_RING(r) + 4, RING_N);
		wr(WF_RX_RING(r) + 8, RING_N - 1);
		wd.rxIdx[r] = 0;
	}
	wr(WF_INT_MSK, 0);
	wr(WF_INT_STA, 0xffffffff);
	wr(WF_PAUSE_TX_Q, 0);
	rmw(WF_APSRC_SLPPROT, 1u << 4, 1u << 4);
	wr(WF_GLO_CFG, rd(WF_GLO_CFG) | GLO_ENABLE | GLO_FWDL_BYPASS);
	wd.seq = 0;
}

/* One packet on TX ring n (3 or 15): copied into its buffer, CIDX advanced, wait for the DMA to take it. */
static bool
wfdma_tx(uint32_t n, const void *pkt, uint32_t plen)
{
	uint32_t i = wd.txIdx[n], boff = OFF_TXBUF + (n == 15 ? RING_N * BUF_SZ : 0) + BUF_SZ * i;
	if (plen > BUF_SZ) {
		return false;
	}
	memcpy(wd.va + boff, pkt, plen);
	begonia_wifi_dma_flush(boff, plen);
	uint32_t *d = desc(OFF_TX(n), i);
	d[0] = (uint32_t)(wd.pa + boff);
	d[1] = (plen << 16) | DESC_LASTSEC0;
	d[2] = 0;
	d[3] = (uint32_t)((wd.pa + boff) >> 32) & 0xf;
	begonia_wifi_dma_flush(OFF_TX(n) + 16 * i, 16);
	wd.txIdx[n] = (i + 1) % RING_N;
	wr(WF_TX_RING(n) + 8, wd.txIdx[n]);
	return poll(WF_TX_RING(n) + 12, 0xfff, wd.txIdx[n], 200000);
}

/* Next received packet on RX ring r within `us`: copied to out (max), length returned, 0 = nothing. */
static uint32_t
wfdma_rx(uint32_t r, uint8_t *out, uint32_t max, uint32_t us)
{
	uint32_t i = wd.rxIdx[r], *d = desc(OFF_RX(r), i);
	for (uint32_t t = 0; t <= us; t += 50) {
		begonia_wifi_dma_flush(OFF_RX(r) + 16 * i, 16);
		if (d[1] & DESC_DMADONE) {
			uint32_t n = (d[1] >> 16) & 0x3fff, boff = OFF_RXBUF(r) + BUF_SZ * i;
			begonia_wifi_dma_flush(boff, BUF_SZ);
			memcpy(out, wd.va + boff, MIN(n, max));
			d[1] = (uint32_t)RX_BUF_LEN << 16;
			begonia_wifi_dma_flush(OFF_RX(r) + 16 * i, 16);
			wr(WF_RX_RING(r) + 8, i);              /* give the entry back */
			wd.rxIdx[r] = (i + 1) % RING_N;
			wd.rxGot[r]++;
			return n ? n : 1;
		}
		delay(50);
	}
	/*
	 * 0153 on the phone: RX ring 0 stopped at an entry that read empty while the DMA had filled the ten after it.
	 * Not explained yet; resynchronise: if the DMA is past this entry and the next one is done, give this one back
	 * and move on (counted in "wifi ring").
	 */
	uint32_t didx = rd(WF_RX_RING(r) + 12) & 0xfff;
	if (didx != i && (desc(OFF_RX(r), (i + 1) % RING_N)[1] & DESC_DMADONE)) {
		wd.rxSkipped[r]++;
		wd.rxSkipD1[r] = d[1];
		d[1] = (uint32_t)RX_BUF_LEN << 16;
		wr(WF_RX_RING(r) + 8, i);
		wd.rxIdx[r] = (i + 1) % RING_N;
	}
	return 0;
}

/* INIT packet (cmm_asic_connac.c asicFillInitCmdTxdInfo): 32-byte TXD + 32-byte INIT_HIF_TX_HEADER + payload. */
static uint32_t
init_pkt(uint8_t *pkt, uint8_t cid, const void *payload, uint32_t plen, uint8_t *seqOut)
{
	uint32_t total = 64 + plen;
	bzero(pkt, 64);
	pkt[0] = (uint8_t)total;
	pkt[1] = (uint8_t)(total >> 8);
	uint16_t pq = cid ? 0x8000 : 0xf800;            /* INIT_CMD_PQ_ID / INIT_CMD_PDA_PQ_ID */
	pkt[2] = (uint8_t)pq;
	pkt[3] = (uint8_t)(pq >> 8);
	pkt[5] = 0xa0;                                  /* header format */
	pkt[7] = cid ? 2 : 3;                           /* PktFt: INIT_PKT_FT_CMD / INIT_PKT_FT_PDA_FWDL */
	pkt[32] = (uint8_t)(total - 32);
	pkt[33] = (uint8_t)((total - 32) >> 8);
	pkt[36] = cid;
	pkt[37] = 0xa0;
	pkt[39] = ++wd.seq;
	if (seqOut) {
		*seqOut = wd.seq;
	}
	if (payload != pkt + 64) {
		memmove(pkt + 64, payload, plen);
	}
	return total;
}

/* Send an init command, wait for its INIT_EVENT_ID_CMD_RESULT on RX ring 1 (20-byte RXD + 8-byte event head). */
static int
init_cmd(uint8_t cid, const void *payload, uint32_t plen, uint32_t waitMs, char *out, size_t len, size_t *po)
{
	size_t o = *po;
	uint8_t pkt[128], ev[256], seq;
	uint32_t n = init_pkt(pkt, cid, payload, plen, &seq);
	int ret = -1;
	if (!wfdma_tx(3, pkt, n)) {
		say("fw: cmd 0x%02x: TX ring 3 did not take it (didx %u cidx %u)\n", cid, rd(WF_TX_RING(3) + 12),
		    rd(WF_TX_RING(3) + 8));
		goto done;
	}
	for (int tries = 0; tries < 6; tries++) {
		uint32_t got = 0, ring = 1;
		for (uint32_t ms = 0; ms < waitMs && got == 0; ms += 10) {      /* both RX rings */
			if ((got = wfdma_rx(1, ev, sizeof(ev), 5000)) == 0 && (got = wfdma_rx(0, ev, sizeof(ev), 5000)) != 0) {
				ring = 0;
			}
		}
		if (got == 0) {
			say("fw: cmd 0x%02x seq %u: no event in %u ms (rx1 didx %u, rx0 didx %u, int_sta 0x%08x, ready 0x%08x)\n",
			    cid, seq, waitMs, rd(WF_RX_RING(1) + 12), rd(WF_RX_RING(0) + 12), rd(WF_INT_STA), rd(WF_READY));
			goto done;
		}
		if (ring == 0) {
			say("fw: cmd 0x%02x: event on RX ring 0 (%u bytes)\n", cid, got);
		}
		uint8_t *e = ev + 20;
		if (got >= 32 && e[4] == 1 && e[5] == seq) {
			ret = e[8];
			if (ret != 0) {
				say("fw: cmd 0x%02x seq %u: result status %u (1 params, 2 CRC, 3 decrypt)\n", cid, seq, e[8]);
			}
			goto done;
		}
		say("fw: cmd 0x%02x: other event (%u bytes, eid 0x%02x seq %u): %02x %02x %02x %02x %02x %02x %02x %02x\n",
		    cid, got, e[4], e[5], ev[0], ev[1], ev[2], ev[3], e[0], e[1], e[2], e[3]);
	}
done:
	*po = o;
	return ret;
}

static size_t
wifi_fw(char *out, size_t len, size_t o)
{
	if ((rd(WF_READY) & 0x6) == 0x6) {
		say("fw: WiFi firmware already running (ready 0x%08x)\n", rd(WF_READY));
		return o;
	}
	if (!vw.on || rd(CONN_MCU_COM_REG0) != CONN_IDLE_MAGIC) {
		say("fw: CONNSYS MCU not in idle loop (wifi on first)\n");
		return o;
	}
	if (wd.va == NULL) {
		wd.va = (uint8_t *)begonia_wifi_dma_alloc(DMA_SIZE, &wd.pa);
		wd.uva = (uint8_t *)begonia_wifi_dma_uncached();
		if (wd.va == NULL || wd.uva == NULL) {
			say("fw: no contiguous DMA memory for WFDMA\n");
			return o;
		}
	}

	/* driver own */
	wr(WF_LPCTL, 1u << 1);
	if (!poll(WF_LPCTL, 1u << 0, 0, 100000)) {
		say("fw: driver own not granted (LPCTL 0x%08x)\n", rd(WF_LPCTL));
		return o;
	}
	wfdma_setup();
	say("fw: driver own, WFDMA at PA 0x%llx, GLO_CFG 0x%08x, ready bits 0x%08x\n", wd.pa, rd(WF_GLO_CFG), rd(WF_READY));

	const uint8_t *fw = g_begonia_fw_ram_code;
	size_t size = g_begonia_fw_ram_code_len;
	uint8_t tail[36], reg[40], pkt[64 + PDA_CHUNK];
	if (size < 36 + 40) {
		say("fw: bad size\n");
		return o;
	}
	memcpy(tail, fw + size - 36, 36);
	if (tail[2] == 0 || tail[2] > 8) {
		say("fw: bad tailer\n");
		return o;
	}
	uint32_t nreg = tail[2], fileOff = 0, entry = 0;
	say("fw: %u regions, version %.10s built %.15s\n", nreg, (const char *)tail + 7, (const char *)tail + 17);
	for (uint32_t r = 0; r < nreg; r++) {
		memcpy(reg, fw + size - 36 - 40 * (nreg - r), 40);
		uint32_t addr, rlen;
		memcpy(&addr, reg + 16, 4);
		memcpy(&rlen, reg + 20, 4);
		uint8_t feat = reg[24];
		if ((feat & 0x20) && entry == 0) {
			entry = addr;
		}
		if (feat & 0x40) {
			say("fw: region %u not downloaded (feature 0x%02x)\n", r, feat);
		} else if (feat & 0x80) {
			uint32_t eoff = addr & 0xffffff;
			if ((uint64_t)eoff + rlen > EMI_SIZE || fileOff + rlen > size) {
				say("fw: region %u: EMI copy bounds error\n", r);
				return o;
			}
			memcpy((uint8_t *)vw.emi + eoff, fw + fileOff, rlen);
			begonia_wifi_emi_flush(eoff, rlen);
			say("fw: region %u: %u bytes -> EMI +0x%06x\n", r, rlen, eoff);
		} else {
			uint32_t mode = 0;
			if (feat & 0x01) {
				mode = (1u << 3) | (feat & 0x6) | 1u | ((feat & 0x10) ? (1u << 6) : 0);
			}
			mode |= 1u << 31;
			uint32_t cfg[3] = { addr, rlen, mode };
			int res = init_cmd(1, cfg, sizeof(cfg), 500, out, len, &o);
			if (res != 0) {
				say("fw: region %u: DOWNLOAD_CONFIG failed (%d)\n", r, res);
				return o;
			}
			for (uint32_t done = 0; done < rlen; done += PDA_CHUNK) {
				uint32_t n = MIN((uint32_t)PDA_CHUNK, rlen - done);
				memcpy(pkt + 64, fw + fileOff + done, n);
				uint32_t total = init_pkt(pkt, 0, pkt + 64, n, NULL);
				if (!wfdma_tx(3, pkt, total)) {
					say("fw: region %u: chunk at %u not taken by the DMA\n", r, done);
					return o;
				}
			}
			say("fw: region %u: %u bytes -> 0x%08x (mode 0x%08x)\n", r, rlen, addr, mode);
		}
		fileOff += rlen;
	}
	{
		uint32_t start[2] = { entry ? 1u : 0u, entry };
		int res = init_cmd(2, start, sizeof(start), 3000, out, len, &o);
		bool ready = poll(WF_READY, 0x6, 0x6, 1000000);
		if (res != 0) {
			say("fw: WIFI_START 0x%08x: no good result (%d); ready bits 0x%08x, MCU pc 0x%08x\n", entry, res,
			    rd(WF_READY), rd(CONN_MCU_PC));
			return o;
		}
		wr(WF_GLO_CFG, rd(WF_GLO_CFG) & ~GLO_FWDL_BYPASS);
		say("fw: WIFI_START 0x%08x ok; firmware %s (ready bits 0x%08x), MCU pc 0x%08x\n", entry,
		    ready ? "READY" : "NOT READY after 1 s", rd(WF_READY), rd(CONN_MCU_PC));
	}
	return o;
}

/* ---- step 4: normal commands (firmware running) ---- */

/* Station state after "wifi init" (under vw_mtx: the worker thread and the USB link). */
#define WL_MAX_BSS              48
#define WL_IE_MAX               VWL_IE_MAX

struct wl_bss {
	uint8_t  bssid[6], ssid[32], ssid_len, ch;
	int8_t   rssi;
	uint16_t capinfo, bintval;
	uint16_t ie_len;
	uint8_t  ie[WL_IE_MAX];
	uint32_t seen;                  /* scan generation */
	uint64_t when;                  /* mach_absolute_time of the last beacon / probe response */
};

static struct {
	bool     cap, init;
	bool     scanBusy;              /* SCAN_REQ_V2 sent, SCAN_DONE not seen yet */
	int      scanStatus;
	uint8_t  mac[6];
	uint8_t  scanSeq, scanGen;
	uint64_t scanDeadline;
	uint32_t nbss, frames, dataFrames, events;
	struct wl_bss bss[WL_MAX_BSS];
} wl;

/*
 * Received packets (MT6781-WIFI-CMDS.md §5): dispatched on the RXD packet type, never on the ring. 0xE000 = event
 * (12-byte header at +20), 0xE001 = frame forwarded by the MCU (beacons, probe/auth/assoc responses, EAPOL),
 * type 2 = data from the MAC. The frame starts after the RXD (20), the optional groups (G4 16, G1 16, G2 8, G3 24,
 * in that order) and 2 bytes of padding if RXD byte 6 bit 6 is set.
 */
#define PKT_EVENT               1
#define PKT_FRAME               2
#define PKT_DATA                3
#define PKT_MSDU                4               /* MSDU report: TX ring 0 tokens back */

static int
rx_classify(const uint8_t *b, uint32_t n, uint32_t *foff)
{
	if (n >= 8 && (b[3] >> 5) == 6) {
		return PKT_MSDU;                        /* short: header 8 + 4 per token */
	}
	if (n < 20) {
		return 0;
	}
	uint16_t ptype = (uint16_t)(b[2] | b[3] << 8);
	if ((ptype & 0xe00f) == 0xe000) {
		return n >= 32 ? PKT_EVENT : 0;
	}
	uint32_t type = ptype >> 13;
	if ((ptype & 0xe00f) != 0xe001 && type != 2) {
		return 0;                               /* TX status, RX vector, MSDU report: not used */
	}
	uint32_t off = 20 + ((ptype & (1u << 12)) ? 16 : 0) + ((ptype & (1u << 9)) ? 16 : 0) +
	    ((ptype & (1u << 10)) ? 8 : 0) + ((ptype & (1u << 11)) ? 24 : 0) + ((b[6] & 0x40) ? 2 : 0);
	uint32_t total = (uint32_t)(b[0] | b[1] << 8);
	if (total > n) {
		total = n;
	}
	if (off >= total) {
		return 0;
	}
	*foff = off;
	return type == 2 ? PKT_DATA : PKT_FRAME;
}

/* RSSI (dBm) of a received frame from G3 RXV cycle 4 (RCPI0/1, the larger, ignoring > 220), 0 if no G3. */
static int
rx_rssi(const uint8_t *b)
{
	uint16_t ptype = (uint16_t)(b[2] | b[3] << 8);
	if (!(ptype & (1u << 11))) {
		return 0;
	}
	uint32_t g3 = 20 + ((ptype & (1u << 12)) ? 16 : 0) + ((ptype & (1u << 9)) ? 16 : 0) + ((ptype & (1u << 10)) ? 8 : 0);
	int r0 = b[g3 + 12], r1 = b[g3 + 13], r = 0;
	if (r0 <= 220) {
		r = r0;
	}
	if (r1 <= 220 && r1 > r) {
		r = r1;
	}
	return r / 2 - 110;
}

static void wifi_frame(const uint8_t *b, uint32_t n, uint32_t foff, int kind);     /* scan table, join, EAPOL */
static void wifi_event(const uint8_t *ev, uint32_t got);                            /* scan done, beacon loss */
static void tx_report(const uint8_t *b, uint32_t n);                                /* TX ring 0 tokens */

/* Next packet from RX ring 1, then RX ring 0, within us microseconds; frames and data go to wifi_frame(). */
static uint32_t
rx_next(uint8_t *buf, uint32_t max, uint32_t us, int *kind)
{
	uint32_t got = wfdma_rx(1, buf, max, us / 2);
	if (got == 0) {
		got = wfdma_rx(0, buf, max, us / 2);
	}
	*kind = 0;
	if (got) {
		uint32_t foff = 0;
		*kind = rx_classify(buf, MIN(got, max), &foff);
		if (*kind == PKT_FRAME || *kind == PKT_DATA) {
			wifi_frame(buf, MIN(got, max), foff, *kind);
		} else if (*kind == PKT_EVENT) {
			wifi_event(buf, MIN(got, max));
		} else if (*kind == PKT_MSDU) {
			tx_report(buf, MIN(got, max));
		}
	}
	return got;
}

static uint8_t wcmd_pkt[64 + 2048];
static uint8_t wcmd_ev[RX_BUF_LEN];                     /* RX for the worker's polls and the join's waits */

/*
 * WIFI_CMD (nic_cmd_event.h, asicFillCmdTxdInfo) on TX ring 15. eid != 0: wait for that event into ev (returns its
 * length). eid 0 (set commands: the stock driver expects no answer): only drain what comes back for 30 ms, logging
 * events (an unknown CID may get INIT_EVENT_ID_CMD_RESULT 0xFD); returns 0 if the DMA took it.
 */
static int
wifi_cmd(uint8_t cid, bool set, const void *payload, uint32_t plen, uint8_t eid, uint8_t *ev, uint32_t evmax,
    uint32_t ms, char *out, size_t len, size_t *po)
{
	size_t o = *po;
	uint8_t *pkt = wcmd_pkt;
	uint32_t total = 64 + plen;
	static uint8_t scratch[RX_BUF_LEN];
	if (plen > 2048) {
		return -1;
	}
	if (ev == NULL) {
		ev = scratch;
		evmax = sizeof(scratch);
	}
	bzero(pkt, 64);
	pkt[0] = (uint8_t)total;
	pkt[1] = (uint8_t)(total >> 8);
	pkt[2] = 0x00;                                  /* CMD_PQ_ID 0x8000 */
	pkt[3] = 0x80;
	pkt[5] = 0xa0;                                  /* header format */
	pkt[7] = 2;                                     /* TXD_PKT_FT_CMD */
	pkt[32] = (uint8_t)(total - 32);                /* u2Length */
	pkt[33] = (uint8_t)((total - 32) >> 8);
	pkt[36] = cid;
	pkt[37] = 0xa0;                                 /* CMD_PACKET_TYPE_ID */
	pkt[38] = set ? 1 : 0;
	pkt[39] = ++wd.seq;
	pkt[42] = 3;                                    /* S2D_INDEX_CMD_H2N_H2C */
	if (plen) {
		memcpy(pkt + 64, payload, plen);
	}
	uint8_t seq = wd.seq;
	int ret = -1;
	if (!wfdma_tx(15, pkt, total)) {
		say("cmd 0x%02x: TX ring 15 did not take it (cidx %u didx %u)\n", cid, rd(WF_TX_RING(15) + 8),
		    rd(WF_TX_RING(15) + 12));
		goto done;
	}
	if (eid == 0) {
		ret = 0;
		ms = 30;
	}
	for (uint32_t t = 0; t < ms;) {
		int kind;
		uint32_t got = rx_next(ev, evmax, 10000, &kind);
		if (got == 0) {
			t += 10;
			continue;
		}
		if (kind != PKT_EVENT) {
			continue;
		}
		uint8_t *e = ev + 20;
		if (eid != 0 && e[4] == eid) {
			ret = (int)got;
			goto done;
		}
		if (e[4] != 0x07) {                     /* EVENT_ID_SLEEPY_INFO: ignored, we stay in driver own */
			say("cmd 0x%02x: event 0x%02x seq %u (%u bytes): %02x %02x %02x %02x\n", cid, e[4], e[5], got,
			    e[12], e[13], e[14], e[15]);
		}
	}
	if (eid != 0) {
		say("cmd 0x%02x seq %u: no event 0x%02x in %u ms (rx1 didx %u)\n", cid, seq, eid, ms, rd(WF_RX_RING(1) + 12));
	}
done:
	*po = o;
	return ret;
}

static size_t
wifi_cap(char *out, size_t len, size_t o)
{
	if (!vw.on || (rd(WF_READY) & 0x6) != 0x6) {
		say("cap: WiFi firmware not running (wifi on, wifi wmt, wifi fw)\n");
		return o;
	}
	static uint8_t ev[2048];
	int n = wifi_cmd(0x8a, false, NULL, 0, 0xec, ev, sizeof(ev), 3000, out, len, &o);
	if (n > 0) {
		wl.cap = true;
	}
	if (n < 36) {
		say("cap: no NIC_CAPABILITY_V2 event\n");
		return o;
	}
	uint8_t *b = ev + 20 + 12;
	uint32_t count = b[0] | b[1] << 8, off = 4, end = (uint32_t)MIN(n, (int)sizeof(ev)) - 32;
	say("cap: %u elements (%d bytes)\n", count, n);
	for (uint32_t i = 0; i < count && off + 8 <= end; i++) {
		uint32_t tag, blen;
		memcpy(&tag, b + off, 4);
		memcpy(&blen, b + off + 4, 4);
		const uint8_t *d = b + off + 8;
		if (off + 8 + blen > end) {
			break;
		}
		switch (tag) {
		case 0x5:
			say("cap: hw: product 0x%04x eco %u, mac ip 0x%08x, bb ip 0x%08x, top ip 0x%08x, config %u\n",
			    d[0] | d[1] << 8, d[2], d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24,
			    d[8] | d[9] << 8 | d[10] << 16 | (uint32_t)d[11] << 24,
			    d[12] | d[13] << 8 | d[14] << 16 | (uint32_t)d[15] << 24, d[16]);
			break;
		case 0x6:
			say("cap: fw: version %u.%u build %u, branch %.4s, date %.16s\n", d[1], d[0], d[2] | d[3] << 8,
			    (const char *)d + 4, (const char *)d + 8);
			break;
		case 0x7:
			say("cap: MAC %02x:%02x:%02x:%02x:%02x:%02x (efuse)\n", d[0], d[1], d[2], d[3], d[4], d[5]);
			break;
		case 0x8:
			say("cap: phy: HT %u VHT %u 5 GHz %u max bandwidth %u (0=20 1=40 2=80), nss %u, LDPC %u\n", d[0], d[1],
			    d[2], d[3], blen > 4 ? d[4] : 0, blen > 5 ? d[5] : 0);
			break;
		case 0x14:
			say("cap: a-die version: chip 0x%04x eco %u\n", d[0] | d[1] << 8, d[2]);
			break;
		default:
			say("cap: tag 0x%02x, %u bytes\n", tag, blen);
			break;
		}
		off += 8 + blen;
	}
	return o;
}

/* ---- step 5: station bring-up and scan (MT6781-WIFI-CMDS.md §2-§4) ---- */

#define CID_DUMMY               0x00
#define CID_BASIC_CONFIG        0x02
#define CID_SCAN_REQ_V2         0x03
#define CID_SET_RX_FILTER       0x0a
#define CID_SET_DOMAIN_INFO     0x0f
#define CID_BSS_ACTIVATE_CTRL   0x11
#define CID_SET_DBDC_PARMS      0x28
#define CID_SET_NVRAM_SETTINGS  0x48
#define EID_SCAN_DONE           0x0d
#define EID_BSS_BEACON_TIMEOUT  0x13
#define NVRAM_SIZE              2048
#define NVRAM_FILE              "WIFI"                          /* nvdata APCFG/APRDEB/WIFI, copied by hand */
#define NVRAM_DEFAULT           "stWifiCfgDefault_6631.bin"     /* vendor default (MAC zero) */

static int
read_file(const char *name, uint8_t *buf, uint32_t max)
{
	char path[160];
	snprintf(path, sizeof(path), "%s%s", WIFI_FW_DIR, name);
	vfs_context_t ctx = vfs_context_kernel();
	vnode_t vp = NULLVP;
	int resid = 0, err = vnode_open(path, FREAD, 0, 0, &vp, ctx);
	if (err != 0) {
		return -err;
	}
	err = vn_rdwr(UIO_READ, vp, (caddr_t)buf, (int)max, 0, UIO_SYSSPACE, IO_NOAUTH, vfs_context_ucred(ctx), &resid,
	    vfs_context_proc(ctx));
	vnode_close(vp, FREAD, ctx);
	return err ? -err : (int)(max - (uint32_t)resid);
}

static bool
mac_valid(const uint8_t *m)
{
	static const uint8_t zero[6], ff[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	return memcmp(m, zero, 6) != 0 && memcmp(m, ff, 6) != 0 && !(m[0] & 1);
}

/* CMD_SET_DOMAIN_INFO V1 (56 B), country from the NVRAM, the default group's tables (rlm_domain.c:744-806). */
static void
domain_info(uint8_t *d, uint16_t cc, bool passive)
{
	static const uint8_t act[6][5] = { { 81, 1, 1, 1, 13 }, { 115, 2, 4, 36, 4 }, { 118, 2, 4, 52, 4 },
		                           { 121, 2, 4, 100, 12 }, { 125, 2, 4, 149, 5 }, { 0 } };
	static const uint8_t pas[6][5] = { { 81, 1, 1, 1, 0 }, { 82, 1, 1, 14, 0 }, { 115, 2, 4, 36, 0 },
		                           { 118, 2, 4, 52, 4 }, { 121, 2, 4, 100, 12 }, { 125, 2, 4, 149, 0 } };
	bzero(d, 56);
	d[0] = (uint8_t)cc;
	d[1] = (uint8_t)(cc >> 8);
	d[2] = passive ? 1 : 0;
	for (int i = 0; i < 6; i++) {
		memcpy(d + 4 + 8 * i, passive ? pas[i] : act[i], 5);
	}
}

static size_t
wifi_init(char *out, size_t len, size_t o)
{
	if (!vw.on || (rd(WF_READY) & 0x6) != 0x6) {
		say("init: WiFi firmware not running (wifi on, wifi wmt, wifi fw)\n");
		return o;
	}
	static uint8_t nv[NVRAM_SIZE];
	uint8_t p[80];
	memcpy(nv, g_begonia_fw_nvram, MIN((size_t)NVRAM_SIZE, g_begonia_fw_nvram_len));
	begonia_wifi_mac(wl.mac);
	memcpy(nv + 4, wl.mac, 6);
	uint16_t cc = (uint16_t)(nv[10] << 8 | nv[11]);
	say("init: authentic NVRAM (version 0x%04x), MAC %02x:%02x:%02x:%02x:%02x:%02x, country %c%c\n", nv[0] | nv[1] << 8,
	    wl.mac[0], wl.mac[1], wl.mac[2], wl.mac[3], wl.mac[4], wl.mac[5],
	    nv[10] ? nv[10] : '0', nv[11] ? nv[11] : '0');

	int err = 0;
	err |= wifi_cmd(CID_DUMMY, true, NULL, 0, 0, NULL, 0, 0, out, len, &o);
	if (!wl.cap) {
		o = wifi_cap(out, len, o);
	}
	bzero(p, sizeof(p));
	err |= wifi_cmd(CID_BASIC_CONFIG, true, p, 12, 0, NULL, 0, 0, out, len, &o);           /* 802.3 data path */
	err |= wifi_cmd(CID_SET_NVRAM_SETTINGS, true, nv, NVRAM_SIZE, 0, NULL, 0, 0, out, len, &o);
	domain_info(p, cc, true);
	err |= wifi_cmd(CID_SET_DOMAIN_INFO, true, p, 56, 0, NULL, 0, 0, out, len, &o);
	domain_info(p, cc, false);
	err |= wifi_cmd(CID_SET_DOMAIN_INFO, true, p, 56, 0, NULL, 0, 0, out, len, &o);
	bzero(p, 40);
	p[4] = 1;                                       /* ucCmdVer */
	p[6] = 40;                                      /* u2CmdLen; ucDbdcEn 0 */
	err |= wifi_cmd(CID_SET_DBDC_PARMS, true, p, 40, 0, NULL, 0, 0, out, len, &o);
	bzero(p, 68);
	p[0] = 0x0b;                                    /* directed | multicast | broadcast */
	err |= wifi_cmd(CID_SET_RX_FILTER, true, p, 68, 0, NULL, 0, 0, out, len, &o);
	uint8_t act[12] = { 0, 1, 0, 1 };               /* BSS 0 active, AIS, own MAC index 1 */
	memcpy(act + 4, wl.mac, 6);
	act[10] = 4;                                    /* broadcast/multicast WTBL entry */
	err |= wifi_cmd(CID_BSS_ACTIVATE_CTRL, true, act, sizeof(act), 0, NULL, 0, 0, out, len, &o);
	wl.init = (err == 0);
	say("init: %s (basic config, NVRAM, domain x2, DBDC off, RX filter, BSS 0 active). Next: wifi scan\n",
	    wl.init ? "done" : "a command was not taken");
	return o;
}

static const uint8_t *
ie_find(const uint8_t *ie, uint32_t len, uint8_t id, const uint8_t *oui4)
{
	for (uint32_t i = 0; i + 2 <= len && i + 2 + ie[i + 1] <= len; i += 2 + ie[i + 1]) {
		if (ie[i] == id && (oui4 == NULL || (ie[i + 1] >= 4 && memcmp(ie + i + 2, oui4, 4) == 0))) {
			return ie + i;
		}
	}
	return NULL;
}

/* Beacon / probe response into the scan table (newest data wins, keyed by BSSID). */
static void
scan_frame(const uint8_t *b, const uint8_t *f, uint32_t flen)
{
	if (flen < 36) {
		return;
	}
	const uint8_t *bssid = f + 16, *ie = f + 36;
	uint32_t ielen = flen - 36;
	struct wl_bss *e = NULL;
	for (uint32_t i = 0; i < wl.nbss; i++) {
		if (memcmp(wl.bss[i].bssid, bssid, 6) == 0) {
			e = &wl.bss[i];
			break;
		}
	}
	if (e == NULL) {
		if (wl.nbss < WL_MAX_BSS) {
			e = &wl.bss[wl.nbss++];
		} else {
			for (uint32_t i = 0; i < WL_MAX_BSS; i++) {     /* replace the oldest */
				if (e == NULL || wl.bss[i].seen < e->seen) {
					e = &wl.bss[i];
				}
			}
		}
		bzero(e, sizeof(*e));
		memcpy(e->bssid, bssid, 6);
	}
	e->seen = wl.scanGen;
	e->when = mach_absolute_time();
	e->rssi = (int8_t)rx_rssi(b);
	e->ch = b[5] > 241 ? (uint8_t)(b[5] - 241) : b[5];
	e->bintval = (uint16_t)(f[32] | f[33] << 8);
	e->capinfo = (uint16_t)(f[34] | f[35] << 8);
	const uint8_t *ds = ie_find(ie, ielen, 3, NULL), *ssid = ie_find(ie, ielen, 0, NULL);
	if (ds && ds[1] >= 1) {
		e->ch = ds[2];
	}
	if (ssid && ssid[1] <= 32 && (ssid[1] > 0 || e->ssid_len == 0)) {
		e->ssid_len = ssid[1];
		memcpy(e->ssid, ssid + 2, ssid[1]);
	}
	/* whole IEs only (the framework parses them; a cut one would be garbage) */
	uint32_t keep = 0;
	while (keep + 2 <= ielen && keep + 2 + ie[keep + 1] <= ielen && keep + 2 + ie[keep + 1] <= WL_IE_MAX) {
		keep += 2 + ie[keep + 1];
	}
	e->ie_len = (uint16_t)keep;
	memcpy(e->ie, ie, keep);
}

/* ---- step 6: the station (join, WPA2-PSK, data) driven by en0 / wifid (bsd/dev/viva_wlan.c) ---- */

/*
 * Locks: vw_mtx = the hardware, wl and sj (the worker thread, the USB link, SET POWER); vs_mtx = requests, the TX
 * queue and the snapshots the ioctls read (never held while waiting on the hardware). Order: vw_mtx, then vs_mtx.
 * Calls into viva_wlan.c (events, ifnet_input) happen after vw_mtx is dropped (vw_unlock()).
 */
LCK_GRP_DECLARE(vw_lck_grp, "begonia_wifi");
LCK_MTX_DECLARE(vw_mtx, &vw_lck_grp);
LCK_MTX_DECLARE(vs_mtx, &vw_lck_grp);
static int vw_chan;                             /* the worker's wait channel */

#define REQ_POWER               0x1
#define REQ_SCAN                0x2
#define REQ_JOIN                0x4
#define REQ_LEAVE               0x8
#define REQ_POWER_EVENT         0x10            /* SET POWER: POWER_CHANGED even when nothing changes */

static struct {
	uint32_t req;
	bool     want;                  /* radio asked on (GET POWER answers this) */
	bool     worker;
	struct vwl_scan scan;
	struct vwl_join join;
	mbuf_t   txHead, txTail;
	uint32_t txn;
	struct vwl_status st;
	uint32_t nres;
	struct vwl_bss res[WL_MAX_BSS];
	struct vwl_bss cur;
} vs = { .want = true };

/* what to tell viva_wlan.c once vw_mtx is dropped */
#define NOTE_SCAN               0x1
#define NOTE_ASSOC              0x2
#define NOTE_DOWN               0x4
#define NOTE_POWER              0x8
static struct {
	uint32_t bits;
	int      assoc;
	uint32_t reason;
	bool     power;
	mbuf_t   rxHead, rxTail;
} vn;

/* the join / the association */
#define EAPOL_MS                8000
static struct {
	uint8_t  state;                 /* APPLE80211_S_* */
	uint8_t  result;                /* ASSOCIATE_RESULT */
	bool     link, rsn, ht, wmm, chHeld, staRec, sentMsg2;
	struct wl_bss ap;
	uint8_t  pmk[32];
	uint16_t aid, seq;
	uint8_t  rsnie[64], rsnie_len;
	uint8_t  rates[16], nrates;     /* the AP's (supported + extended), 500 kb/s units with the basic bit */
	struct begonia_wpa wpa;
	uint64_t deadline;              /* EAPOL handshake */
	uint8_t  mgmt[1024];            /* last auth / assoc response from the AP */
	uint32_t mgmtLen;
	int8_t   rssi;
	uint32_t tx, rx, txDrops, rxDrops, eapolRx, eapolTx;
} sj;

static inline uint64_t
ms_abs(uint32_t ms)
{
	uint64_t t;
	nanoseconds_to_absolutetime((uint64_t)ms * NSEC_PER_MSEC, &t);
	return t;
}

/* ADT /chosen mac-address-wifi0 (mkadt: locally administered, from the synthetic ECID), else a random one */
void
begonia_wifi_mac(uint8_t mac[6])
{
	static uint8_t m[6];
	static bool have;
	if (!have) {
		DTEntry e;
		void const *p = NULL;
		unsigned int sz = 0;
		if (SecureDTLookupEntry(NULL, "/chosen", &e) == kSuccess &&
		    SecureDTGetProperty(e, "mac-address-wifi0", &p, &sz) == kSuccess && sz >= 6) {
			memcpy(m, p, 6);
			have = true;
		} else if (g_begonia_fw_nvram_len >= 10 && mac_valid(g_begonia_fw_nvram + 4)) {
			memcpy(m, g_begonia_fw_nvram + 4, 6);
			have = true;
		} else {
			read_random(m, 6);
			m[0] = (uint8_t)((m[0] & 0xfc) | 0x02);
			have = true;
		}
	}
	memcpy(mac, m, 6);
}

static void hw_reset(void);                     /* the chip-specific join part, below */

static void
wl_reset(void)
{
	hw_reset();
	wl.cap = wl.init = wl.scanBusy = false;
	vw.wmt = false;
	st.full = false;
	if (sj.link || sj.state != 0) {
		vn.bits |= NOTE_DOWN;
		vn.reason = 0;
	}
	bzero(&sj, sizeof(sj));
}

/* ---- transmit (TX ring 0: data and management) ---- */

static int hw_tx(const uint8_t *frame, uint32_t len, bool mgmt, bool protect);         /* below, chip specific */

static void
mgmt_hdr(uint8_t *f, uint8_t fc)
{
	bzero(f, 24);
	f[0] = fc;
	memcpy(f + 4, sj.ap.bssid, 6);
	memcpy(f + 10, wl.mac, 6);
	memcpy(f + 16, sj.ap.bssid, 6);
	f[22] = (uint8_t)(sj.seq << 4);
	f[23] = (uint8_t)(sj.seq >> 4);
	sj.seq = (sj.seq + 1) & 0xfff;
}

static int
tx_eapol(const uint8_t *body, uint32_t len)
{
	uint8_t eth[14 + 256];
	if (len > 256) {
		return -1;
	}
	memcpy(eth, sj.ap.bssid, 6);
	memcpy(eth + 6, wl.mac, 6);
	eth[12] = 0x88;
	eth[13] = 0x8e;
	memcpy(eth + 14, body, len);
	sj.eapolTx++;
	return hw_tx(eth, 14 + len, false, false);
}

/* ---- the AP's frames ---- */

static void
join_frame(__unused const uint8_t *b, const uint8_t *f, uint32_t flen)
{
	uint8_t fc = f[0];
	if (sj.state == 0 || memcmp(f + 16, sj.ap.bssid, 6) != 0 || memcmp(f + 4, wl.mac, 6) != 0) {
		return;
	}
	if (fc == 0xc0 || fc == 0xa0) {                 /* deauthentication / disassociation */
		uint16_t reason = flen >= 26 ? (uint16_t)(f[24] | f[25] << 8) : 0;
		if (sj.link) {
			vn.bits |= NOTE_DOWN;
			vn.reason = reason;
		}
		printf("BEGONIA WIFI: %s from the AP, reason %u\n", fc == 0xc0 ? "deauthentication" : "disassociation", reason);
		sj.link = false;
		sj.state = 0;                           /* the worker cleans up the firmware side */
		sj.mgmtLen = 0;
		return;
	}
	if ((fc == 0xb0 || fc == 0x10 || fc == 0x30) && flen <= sizeof(sj.mgmt)) {
		memcpy(sj.mgmt, f, flen);
		sj.mgmtLen = flen;
	}
}

static void sta_eapol(const uint8_t *eth, uint32_t len);

/* An 802.3 data frame from the AP: EAPOL to the supplicant, the rest to en0 (after vw_unlock()). */
static void
data_rx(const uint8_t *eth, uint32_t len)
{
	if (len < 14) {
		return;
	}
	if (eth[12] == 0x88 && eth[13] == 0x8e) {
		sta_eapol(eth, len);
		return;
	}
	if (!sj.link || len > 2048) {
		sj.rxDrops++;
		return;
	}
	/* one mbuf with a 2 KiB cluster, lengths set explicitly (as sweet_usb.c; begonia_wlan_input strips 14 bytes) */
	mbuf_t m = NULL;
	if (mbuf_gethdr(MBUF_DONTWAIT, MBUF_TYPE_DATA, &m) != 0) {
		sj.rxDrops++;
		return;
	}
	if (mbuf_mclget(MBUF_DONTWAIT, MBUF_TYPE_DATA, &m) != 0 || !(mbuf_flags(m) & MBUF_EXT) || mbuf_maxlen(m) < len) {
		mbuf_freem(m);
		sj.rxDrops++;
		return;
	}
	memcpy(mbuf_data(m), eth, len);
	mbuf_setlen(m, len);
	mbuf_pkthdr_setlen(m, len);
	if (vn.rxTail) {
		mbuf_setnextpkt(vn.rxTail, m);
	} else {
		vn.rxHead = m;
	}
	vn.rxTail = m;
	sj.rx++;
}

/* RX dispatch for frames and data (called from rx_next). */
static void
wifi_frame(const uint8_t *b, uint32_t n, uint32_t foff, int kind)
{
	const uint8_t *f = b + foff;
	uint32_t total = MIN((uint32_t)(b[0] | b[1] << 8), n), flen = total - foff;
	if (b[6] & 0x80) {                              /* translated to 802.3: DA, SA, type */
		wl.dataFrames++;
		data_rx(f, flen);
		return;
	}
	if (kind != PKT_FRAME && kind != PKT_DATA) {
		return;
	}
	if (flen < 24) {
		return;
	}
	wl.frames++;
	uint8_t fc = f[0];
	if (fc == 0x80 || fc == 0x50) {
		scan_frame(b, f, flen);
		if (sj.state && memcmp(f + 16, sj.ap.bssid, 6) == 0) {
			int r = rx_rssi(b);
			if (r) {
				sj.rssi = (int8_t)r;
			}
		}
	} else if ((fc & 0x0c) == 0) {
		join_frame(b, f, flen);
	} else if ((fc & 0x0c) == 0x08 && flen >= 24 + 8) {
		/* 802.11 data not translated: LLC/SNAP AA AA 03 00 00 00 + type -> 802.3 (from the AP: to-DS 0) */
		uint32_t h = 24 + ((fc & 0x80) ? 2 : 0);
		const uint8_t *l = f + h;
		if (flen >= h + 8 && l[0] == 0xaa && l[1] == 0xaa && l[2] == 0x03) {
			static uint8_t eth[14 + 2048];
			uint32_t plen = MIN(flen - h - 8, (uint32_t)2048);
			memcpy(eth, f + 4, 6);                  /* DA = addr1 */
			memcpy(eth + 6, f + 16, 6);             /* SA = addr3 */
			eth[12] = l[6];
			eth[13] = l[7];
			memcpy(eth + 14, l + 8, plen);
			data_rx(eth, 14 + plen);
		}
	}
}

static const char *
bss_security(const struct wl_bss *e)
{
	static const uint8_t wpa_oui[4] = { 0x00, 0x50, 0xf2, 0x01 };
	const uint8_t *rsn = ie_find(e->ie, e->ie_len, 48, NULL);
	if (rsn && rsn[1] >= 20) {
		/* AKM list after version (2), group (4), pairwise count + suites */
		uint32_t pc = rsn[8] | rsn[9] << 8, a = 10 + 4 * pc;
		bool psk = false, sae = false, eap = false;
		if (a + 2 <= (uint32_t)rsn[1] + 2) {
			uint32_t ac = rsn[a] | rsn[a + 1] << 8;
			for (uint32_t i = 0; i < ac && a + 2 + 4 * i + 4 <= (uint32_t)rsn[1] + 2; i++) {
				uint8_t t = rsn[a + 2 + 4 * i + 3];
				psk |= (t == 2 || t == 6);
				sae |= (t == 8);
				eap |= (t == 1 || t == 5);
			}
		}
		return psk && sae ? "WPA2/WPA3" : psk ? "WPA2-PSK" : sae ? "WPA3-SAE" : eap ? "WPA2-EAP" : "RSN?";
	}
	if (ie_find(e->ie, e->ie_len, 0xdd, wpa_oui)) {
		return "WPA";
	}
	return (e->capinfo & 0x10) ? "WEP" : "open";
}

/* ---- events not awaited by a command ---- */

static void sta_scan_done_locked(void);

static void
wifi_event(const uint8_t *ev, uint32_t got)
{
	const uint8_t *e = ev + 20, *d = ev + 32;
	wl.events++;
	switch (e[4]) {
	case EID_SCAN_DONE:
		if (wl.scanBusy && got >= 38 && d[0] == wl.scanSeq) {
			wl.scanBusy = false;
			wl.scanStatus = 0;
			sta_scan_done_locked();
		}
		break;
	case EID_BSS_BEACON_TIMEOUT:
		if (sj.link || sj.state) {
			printf("BEGONIA WIFI: beacon timeout (bss %u, reason %u)\n", d[0], d[1]);
			if (sj.link) {
				vn.bits |= NOTE_DOWN;
				vn.reason = 4;
			}
			sj.link = false;
			sj.state = 0;
		}
		break;
	default:
		break;
	}
}

/* ---- scan ---- */

/* SCAN_REQ_V2 (1188 B, MT6781-WIFI-CMDS.md §4.1); completion: wifi_event(EID_SCAN_DONE). */
static int
scan_send(const uint8_t *ssid, uint32_t ssid_len, const uint8_t *chans, uint32_t nch, bool passive, char *out,
    size_t len, size_t *po)
{
	static uint8_t req[1188];
	bzero(req, sizeof(req));
	req[0] = ++wl.scanSeq;
	req[1] = 0;                                     /* BSS 0 */
	req[2] = passive ? 0 : 1;
	req[3] = 1;                                     /* wildcard SSID */
	if (ssid_len) {
		uint32_t sl = MIN(ssid_len, 32u);
		req[3] = 4;                             /* specified + wildcard */
		req[4] = 1;
		memcpy(req + 8, &sl, 4);
		memcpy(req + 12, ssid, sl);
	}
	req[6] = 0x02;                                  /* DBDC scan disabled */
	req[7] = 1;                                     /* auVersion */
	if (nch > 0 && nch <= 32) {
		req[158] = 4;                           /* specified channels: {band, channel} */
		req[159] = (uint8_t)nch;
		for (uint32_t i = 0; i < nch; i++) {
			req[160 + 2 * i] = chans[i] <= 14 ? 1 : 2;
			req[161 + 2 * i] = chans[i];
		}
	}
	memset(req + 1112, 0xff, 6);                    /* aucBSSID */
	wl.scanGen++;
	if (wifi_cmd(CID_SCAN_REQ_V2, true, req, sizeof(req), 0, NULL, 0, 0, out, len, po) != 0) {
		return -1;
	}
	wl.scanBusy = true;
	wl.scanDeadline = mach_absolute_time() + ms_abs(12000);
	return 0;
}

/* Pump RX until the scan is done (USB link and joins). */
static int
scan_wait(uint32_t ms)
{
	uint8_t *ev = wcmd_ev;
	uint64_t end = mach_absolute_time() + ms_abs(ms);
	while (wl.scanBusy && mach_absolute_time() < end) {
		int kind;
		(void)rx_next(ev, RX_BUF_LEN, 10000, &kind);
	}
	if (wl.scanBusy) {
		wl.scanBusy = false;
		wl.scanStatus = -1;
		sta_scan_done_locked();
		return -1;
	}
	return 0;
}

static void
bss_to_vwl(const struct wl_bss *e, struct vwl_bss *v, uint64_t now)
{
	memcpy(v->bssid, e->bssid, 6);
	memcpy(v->ssid, e->ssid, 32);
	v->ssid_len = e->ssid_len;
	v->ch = e->ch;
	v->rssi = e->rssi;
	v->capinfo = e->capinfo;
	v->bintval = e->bintval;
	v->ie_len = e->ie_len;
	memcpy(v->ie, e->ie, e->ie_len);
	uint64_t ns;
	absolutetime_to_nanoseconds(now - e->when, &ns);
	v->age_ms = (uint32_t)MIN(ns / NSEC_PER_MSEC, (uint64_t)UINT32_MAX);
}

/* Results for SCAN_RESULT: what was heard in the last 30 s (strongest first). */
static void
sta_scan_done_locked(void)
{
	uint64_t now = mach_absolute_time(), old = ms_abs(30000);
	uint8_t order[WL_MAX_BSS], m = 0;
	for (uint32_t i = 0; i < wl.nbss; i++) {
		if (wl.bss[i].ssid_len > 0 && now - wl.bss[i].when < old) {
			order[m++] = (uint8_t)i;
		}
	}
	for (uint32_t i = 1; i < m; i++) {
		for (uint32_t j = i; j > 0 && wl.bss[order[j]].rssi > wl.bss[order[j - 1]].rssi; j--) {
			uint8_t t = order[j];
			order[j] = order[j - 1];
			order[j - 1] = t;
		}
	}
	lck_mtx_lock(&vs_mtx);
	for (uint32_t i = 0; i < m; i++) {
		bss_to_vwl(&wl.bss[order[i]], &vs.res[i], now);
	}
	vs.nres = m;
	lck_mtx_unlock(&vs_mtx);
	vn.bits |= NOTE_SCAN;
}

/* ---- join ---- */

static int  hw_ch_privilege(bool get);                 /* below, chip specific */
static int  hw_sta_rec(uint8_t state);
static void hw_sta_remove(void);
static int  hw_bss_info(bool connected);
static void hw_pm_connected(bool connected);
static int  hw_key(bool pairwise, uint8_t keyid, const uint8_t *key, uint32_t klen, const uint8_t *rsc);
static void hw_wmm(void);

static const uint8_t rsn_psk_ccmp[] = { 0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
	                                0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00 };

/* The AP's RSN IE allows WPA2-PSK with CCMP pairwise: our IE = PSK, CCMP, the AP's group cipher. */
static bool
rsn_choose(const struct wl_bss *e)
{
	const uint8_t *r = ie_find(e->ie, e->ie_len, 48, NULL);
	if (r == NULL || r[1] < 18) {
		return false;
	}
	uint32_t end = 2 + r[1], pc = r[8] | r[9] << 8, a = 10 + 4 * pc;
	bool ccmp = false, psk = false;
	for (uint32_t i = 0; i < pc && 10 + 4 * i + 4 <= end; i++) {
		ccmp |= memcmp(r + 10 + 4 * i, "\x00\x0f\xac\x04", 4) == 0;
	}
	if (a + 2 > end) {
		return false;
	}
	uint32_t ac = r[a] | r[a + 1] << 8;
	for (uint32_t i = 0; i < ac && a + 2 + 4 * i + 4 <= end; i++) {
		psk |= memcmp(r + a + 2 + 4 * i, "\x00\x0f\xac\x02", 4) == 0;
	}
	if (!ccmp || !psk) {
		return false;
	}
	memcpy(sj.rsnie, rsn_psk_ccmp, sizeof(rsn_psk_ccmp));
	memcpy(sj.rsnie + 4, r + 4, 4);                 /* group cipher as the AP says (CCMP or TKIP) */
	sj.rsnie_len = sizeof(rsn_psk_ccmp);
	return true;
}

static struct wl_bss *
bss_pick(const struct vwl_join *j)
{
	static const uint8_t zero[6];
	struct wl_bss *best = NULL;
	uint64_t now = mach_absolute_time(), old = ms_abs(60000);
	for (uint32_t i = 0; i < wl.nbss; i++) {
		struct wl_bss *e = &wl.bss[i];
		if (now - e->when > old || e->ssid_len != j->ssid_len || memcmp(e->ssid, j->ssid, j->ssid_len) != 0) {
			continue;
		}
		if (memcmp(j->bssid, zero, 6) != 0 && memcmp(j->bssid, e->bssid, 6) != 0) {
			continue;
		}
		if (best == NULL || e->rssi > best->rssi) {
			best = e;
		}
	}
	return best;
}

/* Wait for the AP's management frame fc (pumping RX); its length, 0 on timeout. */
static uint32_t
wait_mgmt(uint8_t fc, uint32_t ms)
{
	uint8_t *ev = wcmd_ev;
	uint64_t end = mach_absolute_time() + ms_abs(ms);
	sj.mgmtLen = 0;
	while (mach_absolute_time() < end && sj.state != 0) {
		int kind;
		(void)rx_next(ev, RX_BUF_LEN, 5000, &kind);
		if (sj.mgmtLen && sj.mgmt[0] == fc) {
			return sj.mgmtLen;
		}
	}
	return 0;
}

static void
sta_teardown(void)
{
	if (sj.staRec) {
		hw_pm_connected(false);
		(void)hw_bss_info(false);
		hw_sta_remove();
		sj.staRec = false;
	}
	if (sj.chHeld) {
		(void)hw_ch_privilege(false);
		sj.chHeld = false;
	}
}

static void
sta_fail(uint8_t result, const char *why, char *out, size_t len, size_t *po)
{
	size_t o = *po;
	say("join: FAILED (%s), result %u\n", why, result);
	sta_teardown();
	sj.state = 0;
	sj.link = false;
	sj.result = result;
	vn.bits |= NOTE_ASSOC;
	vn.assoc = result;
	*po = o;
}

static void
sta_up(char *out, size_t len, size_t *po)
{
	size_t o = *po;
	if (sj.chHeld) {
		(void)hw_ch_privilege(false);           /* gen4m keeps it ~4 s after the grant; done once keyed */
		sj.chHeld = false;
	}
	sj.state = 4;
	sj.link = true;
	sj.result = 1;
	vn.bits |= NOTE_ASSOC;
	vn.assoc = 1;
	say("join: CONNECTED to \"%.*s\" %02x:%02x:%02x:%02x:%02x:%02x ch %u aid %u%s\n", sj.ap.ssid_len, sj.ap.ssid,
	    sj.ap.bssid[0], sj.ap.bssid[1], sj.ap.bssid[2], sj.ap.bssid[3], sj.ap.bssid[4], sj.ap.bssid[5], sj.ap.ch,
	    sj.aid, sj.rsn ? ", WPA2 keys installed" : " (open)");
	*po = o;
}

/* Association request body: capability, listen interval, SSID, rates, RSN, HT capabilities, WMM. */
static uint32_t
assoc_req(uint8_t *f)
{
	mgmt_hdr(f, 0x00);
	uint8_t *p = f + 24;
	uint16_t cap = (uint16_t)(0x0001 | (sj.ap.capinfo & (0x0010 | 0x0020 | 0x0400)));   /* ESS, privacy, short */
	*p++ = (uint8_t)cap;
	*p++ = (uint8_t)(cap >> 8);
	*p++ = 10;                                      /* listen interval */
	*p++ = 0;
	*p++ = 0;
	*p++ = sj.ap.ssid_len;
	memcpy(p, sj.ap.ssid, sj.ap.ssid_len);
	p += sj.ap.ssid_len;
	uint32_t n1 = MIN(sj.nrates, 8u);
	*p++ = 1;
	*p++ = (uint8_t)n1;
	memcpy(p, sj.rates, n1);
	p += n1;
	if (sj.nrates > 8) {
		*p++ = 50;
		*p++ = (uint8_t)(sj.nrates - 8);
		memcpy(p, sj.rates + 8, sj.nrates - 8);
		p += sj.nrates - 8;
	}
	if (sj.rsn) {
		memcpy(p, sj.rsnie, sj.rsnie_len);
		p += sj.rsnie_len;
	}
	if (sj.ht) {
		/* HT capabilities (gen4m rlmFillHtCapIE on 1x1, 20 MHz): SM power save disabled, SGI-20, RX STBC 1,
		 * A-MPDU 64 KiB, MCS 0-7 */
		static const uint8_t htcap[28] = { 45, 26, 0x2c, 0x01, 0x03, 0xff };
		memcpy(p, htcap, sizeof(htcap));
		p += sizeof(htcap);
	}
	if (sj.wmm) {
		static const uint8_t wmm[9] = { 0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00 };
		memcpy(p, wmm, sizeof(wmm));
		p += sizeof(wmm);
	}
	return (uint32_t)(p - f);
}

/*
 * The host MLME (gen4m saa_fsm / ais_fsm, MT6781-WIFI-JOIN.md): channel privilege, STA record, open-system
 * authentication, association, STA record connected + BSS info + PM connected, then (WPA2) the 4-way handshake in
 * sta_eapol() and keys, finished by sta_poll(). Runs on the worker with vw_mtx held.
 */
static void
sta_join(const struct vwl_join *j, char *out, size_t len, size_t *po)
{
	size_t o = *po;
	static uint8_t f[512];
	if (sj.state != 0 || sj.link) {
		sta_teardown();
		sj.link = false;
		vn.bits |= NOTE_DOWN;
		vn.reason = 0;
	}
	bzero(&sj, sizeof(sj));
	hw_reset();
	sj.state = 2;
	struct wl_bss *e = bss_pick(j);
	for (int k = 0; e == NULL && k < 2; k++) {
		say("join: \"%.*s\" not heard recently, scanning for it\n", j->ssid_len, j->ssid);
		if (scan_send(j->ssid, j->ssid_len, NULL, 0, false, out, len, &o) == 0) {
			(void)scan_wait(10000);
		}
		e = bss_pick(j);
	}
	if (e == NULL) {
		sta_fail(2, "network not found", out, len, &o);
		goto done;
	}
	sj.ap = *e;
	sj.state = 2;
	sj.rssi = e->rssi;
	memcpy(sj.pmk, j->pmk, 32);
	bool privacy = (e->capinfo & 0x10) != 0;
	if (j->has_pmk) {
		if (!rsn_choose(e)) {
			sta_fail(2, "the AP does not offer WPA2-PSK with CCMP", out, len, &o);
			goto done;
		}
		sj.rsn = true;
	} else if (privacy) {
		sta_fail(2, "the network needs a key", out, len, &o);
		goto done;
	}
	static const uint8_t wmm_oui[4] = { 0x00, 0x50, 0xf2, 0x02 };
	sj.ht = ie_find(e->ie, e->ie_len, 45, NULL) != NULL;
	sj.wmm = ie_find(e->ie, e->ie_len, 0xdd, wmm_oui) != NULL;
	for (int k = 0; k < 2; k++) {
		const uint8_t *r = ie_find(e->ie, e->ie_len, k ? 50 : 1, NULL);
		for (uint32_t i = 0; r && i < r[1] && sj.nrates < sizeof(sj.rates); i++) {
			sj.rates[sj.nrates++] = r[2 + i];
		}
	}
	say("join: \"%.*s\" %02x:%02x:%02x:%02x:%02x:%02x ch %u %d dBm, %s%s%s\n", e->ssid_len, e->ssid, e->bssid[0],
	    e->bssid[1], e->bssid[2], e->bssid[3], e->bssid[4], e->bssid[5], e->ch, e->rssi, sj.rsn ? "WPA2-PSK" : "open",
	    sj.ht ? ", HT" : "", sj.wmm ? ", WMM" : "");

	if (hw_ch_privilege(true) != 0) {
		sta_fail(2, "no channel privilege", out, len, &o);
		goto done;
	}
	sj.chHeld = true;
	if (hw_sta_rec(1) != 0) {
		sta_fail(2, "STA record not taken", out, len, &o);
		goto done;
	}
	sj.staRec = true;

	/* open-system authentication: 3 tries, 300 ms each */
	uint32_t n = 0;
	for (int k = 0; k < 3 && n == 0; k++) {
		mgmt_hdr(f, 0xb0);
		bzero(f + 24, 6);
		f[26] = 1;                              /* transaction 1 */
		if (hw_tx(f, 30, true, false) != 0) {
			say("join: auth frame not taken by TX ring 0\n");
		}
		n = wait_mgmt(0xb0, 300);
		if (n >= 30 && (sj.mgmt[26] != 2 || sj.mgmt[24] != 0)) {
			n = 0;                          /* not our transaction 2 */
		}
	}
	if (n == 0) {
		sta_fail(9, "no authentication response", out, len, &o);
		goto done;
	}
	uint16_t status = (uint16_t)(sj.mgmt[28] | sj.mgmt[29] << 8);
	if (status != 0) {
		say("join: authentication refused, status %u\n", status);
		sta_fail(2, "authentication refused", out, len, &o);
		goto done;
	}
	sj.state = 3;

	/* association: 3 tries, 500 ms each */
	n = 0;
	for (int k = 0; k < 3 && n == 0; k++) {
		uint32_t flen = assoc_req(f);
		(void)hw_tx(f, flen, true, false);
		n = wait_mgmt(0x10, 500);
	}
	if (n < 30) {
		sta_fail(9, "no association response", out, len, &o);
		goto done;
	}
	status = (uint16_t)(sj.mgmt[26] | sj.mgmt[27] << 8);
	if (status != 0) {
		say("join: association refused, status %u\n", status);
		sta_fail(2, "association refused", out, len, &o);
		goto done;
	}
	sj.aid = (uint16_t)((sj.mgmt[28] | sj.mgmt[29] << 8) & 0x3fff);
	{
		/* what the AP agreed to: HT only if its response carries HT capabilities, WMM if it has the parameter IE */
		static const uint8_t wmm_param[4] = { 0x00, 0x50, 0xf2, 0x02 };
		const uint8_t *ies = sj.mgmt + 30;
		uint32_t il = n - 30;
		sj.ht = sj.ht && ie_find(ies, il, 45, NULL) != NULL;
		sj.wmm = sj.wmm && ie_find(ies, il, 0xdd, wmm_param) != NULL;
	}
	hw_wmm();
	if (hw_bss_info(true) != 0 || hw_sta_rec(3) != 0) {
		sta_fail(2, "firmware refused the connected state", out, len, &o);
		goto done;
	}
	hw_pm_connected(true);
	say("join: associated, aid %u%s%s\n", sj.aid, sj.ht ? ", HT" : "", sj.wmm ? ", WMM" : "");
	if (!sj.rsn) {
		sta_up(out, len, &o);
		goto done;
	}
	begonia_wpa_start(&sj.wpa, sj.pmk, sj.ap.bssid, wl.mac, sj.rsnie, sj.rsnie_len);
	sj.deadline = mach_absolute_time() + ms_abs(EAPOL_MS);
	say("join: waiting for the 4-way handshake\n");
done:
	*po = o;
}

/* An EAPOL frame from the AP (802.3, header included). */
static void
sta_eapol(const uint8_t *eth, uint32_t len)
{
	static bool inside;                     /* hw_key() pumps RX: a retransmission arriving then is dropped */
	sj.eapolRx++;
	if (inside || !sj.rsn || sj.state < 3 || memcmp(eth + 6, sj.ap.bssid, 6) != 0) {
		return;
	}
	inside = true;
	int act = begonia_wpa_rx(&sj.wpa, eth + 14, len - 14);
	if (act & BEGONIA_WPA_SEND) {
		(void)tx_eapol(sj.wpa.out, sj.wpa.out_len);
		sj.sentMsg2 |= sj.wpa.state == 1;
	}
	if (act & BEGONIA_WPA_PTK) {
		delay(30000);                           /* message 4 leaves in the clear before the key is in */
		int e = hw_key(true, 0, sj.wpa.ptk + 32, 16, NULL);
		printf("BEGONIA WIFI: PTK %s\n", e == 0 ? "installed" : "NOT installed");
	}
	if (act & BEGONIA_WPA_GTK) {
		int e = hw_key(false, sj.wpa.gtk_idx, sj.wpa.gtk, sj.wpa.gtk_len, sj.wpa.gtk_rsc);
		printf("BEGONIA WIFI: GTK %u (%u bytes) %s\n", sj.wpa.gtk_idx, sj.wpa.gtk_len, e == 0 ? "installed" : "NOT installed");
	}
	if ((act & BEGONIA_WPA_PTK) && !sj.link) {
		char buf[256];
		size_t o = 0;
		sta_up(buf, sizeof(buf), &o);
		printf("BEGONIA WIFI: %s", buf);
	}
	inside = false;
}

static void
sta_leave(char *out, size_t len, size_t *po)
{
	size_t o = *po;
	if (sj.state == 0 && !sj.link) {
		*po = o;
		return;
	}
	if (sj.link || sj.state >= 3) {
		uint8_t f[26];
		mgmt_hdr(f, 0xc0);
		f[24] = 3;                              /* reason: leaving */
		f[25] = 0;
		(void)hw_tx(f, sizeof(f), true, sj.rsn && sj.link);
	}
	sta_teardown();
	if (sj.link) {
		vn.bits |= NOTE_DOWN;
		vn.reason = 0;
	}
	say("leave: disconnected from \"%.*s\"\n", sj.ap.ssid_len, sj.ap.ssid);
	bzero(&sj, sizeof(sj));
	*po = o;
}

/*
 * ---- chip-specific join commands (gen4m SOC2_1X1, ~/Developer/ios-viva/wifi/docs/MT6781-WIFI-JOIN.md) ----
 * WTBL: 0-3 BIP, 4 the BMC entry of BSS 0 (BSS_ACTIVATE_CTRL), 5 the AP (the host picks it; STA record 0).
 * Management frames: TX ring 15, 32-byte HW_MAC_TX_DESC + the 802.11 frame in one buffer (no WIFI_CMD half).
 * Data: TX ring 0, the ring entry is TXD + 32-byte append (64 bytes); the 802.3 frame sits after it in the same
 * buffer and is fetched later through the append pointer, so the buffer is only free when an MSDU report (RX
 * packet type 6) gives its token back. Token = ring slot.
 */
#define WTBL_AP                 5
#define WTBL_BMC                4
#define WTBL_DEFAULT            31
#define STA_IDX_AP              0
#define CID_POWER_SAVE_MODE     0x05
#define CID_ADD_REMOVE_KEY      0x07
#define CID_UPDATE_BSS_INFO     0x12
#define CID_UPDATE_STA_RECORD   0x13
#define CID_REMOVE_STA_RECORD   0x14
#define CID_PM_BSS_CONNECTED    0x16
#define CID_PM_BSS_ABORT        0x17
#define CID_CH_PRIVILEGE        0x1c
#define CID_UPDATE_WMM_PARMS    0x1d
#define EID_ACTIVATE_STA_REC    0x0c
#define EID_CH_PRIVILEGE        0x10
#define EID_ADD_PKEY_DONE       0x24

static struct {
	uint32_t busy;                  /* TX ring 0 slots whose token is out */
	uint64_t when[TX0_N];
	uint16_t sn;                    /* management frames' sequence numbers */
	uint8_t  chToken;
	bool     ptk;                   /* pairwise key in: data goes out protected */
	uint32_t ringFull, reused;
	uint8_t  firstReport[16];       /* raw bytes of the first MSDU report (wifi sta) */
	uint32_t reports, lost, full;
} tx0;

static void
hw_reset(void)
{
	bzero(&tx0, sizeof(tx0));
}

static inline void
put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static inline void
put32(uint8_t *p, uint32_t v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

/* SW rate index (wlan_def.h): 1, 2, 5.5, 11, 22, 33, 6, 9, 12, 18, 24, 36, 48, 54 Mb/s in 500 kb/s units */
static int
rate_bit(uint8_t r)
{
	static const uint8_t v[14] = { 2, 4, 11, 22, 44, 66, 12, 18, 24, 36, 48, 72, 96, 108 };
	for (int i = 0; i < 14; i++) {
		if ((r & 0x7f) == v[i]) {
			return i;
		}
	}
	return -1;
}

static void
rate_sets(uint16_t *oper, uint16_t *basic)
{
	*oper = *basic = 0;
	for (uint32_t i = 0; i < sj.nrates; i++) {
		int b = rate_bit(sj.rates[i]);
		if (b >= 0) {
			*oper |= (uint16_t)(1u << b);
			if (sj.rates[i] & 0x80) {
				*basic |= (uint16_t)(1u << b);
			}
		}
	}
}

/* Lowest basic rate as a fixed-rate code: CCK 1/2/5.5/11 M = 0..3, otherwise OFDM 6 M (0x4B). */
static uint16_t
rate_code(void)
{
	uint16_t oper, basic;
	rate_sets(&oper, &basic);
	for (int b = 0; b < 4; b++) {
		if (basic & (1u << b)) {
			return (uint16_t)b;
		}
	}
	return 0x004b;
}

static uint8_t
phy_set(void)
{
	uint16_t oper, basic;
	rate_sets(&oper, &basic);
	uint8_t p = 0;
	if (sj.ap.ch > 14) {
		p = 0x08;                               /* OFDM */
	} else {
		if (oper & 0x3fc0) {
			p |= 0x02;                      /* ERP */
		}
		if ((oper & 0x000f) && !(basic & 0x3fc0)) {
			p |= 0x01;                      /* HR/DSSS */
		}
	}
	return p | (sj.ht ? 0x10 : 0);
}

static int
cmd_set(uint8_t cid, const void *p, uint32_t n)
{
	static char junk[256];
	size_t o = 0;
	return wifi_cmd(cid, true, p, n, 0, NULL, 0, 0, junk, sizeof(junk), &o);
}

/* A received MSDU report (RX packet type 6): the tokens of TX ring 0 slots that are done. */
static void
tx_report(const uint8_t *b, uint32_t n)
{
	uint32_t cnt = (uint32_t)(b[2] | b[3] << 8) & 0x7f;
	for (uint32_t i = 0; i < cnt && 8 + 4 * i + 2 <= n; i++) {
		uint32_t tok = (uint32_t)(b[8 + 4 * i] | b[9 + 4 * i] << 8) & 0x7fff;
		if (tok < TX0_N) {
			tx0.busy &= ~(1u << tok);
		}
	}
	if (tx0.reports++ == 0) {
		memcpy(tx0.firstReport, b, MIN(n, (uint32_t)sizeof(tx0.firstReport)));
	}
}

/* 802.3 frame on TX ring 0 (hardware 802.3 -> 802.11 translation and CCMP with the WTBL key). */
static int
tx_data(const uint8_t *eth, uint32_t len, bool protect, bool lowRate)
{
	if (len < 14 || len > TX0_BUF - 64) {
		return -1;
	}
	uint32_t i = wd.txIdx[0];
	/* the DMA has not taken the entry before this one round the ring yet: the ring itself is full */
	for (int k = 0; k < 50 && (i + 1) % TX0_N == (rd(WF_TX_RING(0) + 12) & 0xfff); k++) {
		delay(20);
	}
	if ((i + 1) % TX0_N == (rd(WF_TX_RING(0) + 12) & 0xfff)) {
		tx0.ringFull++;
		return -1;
	}
	if (tx0.busy & (1u << i)) {
		/* give the reports a moment, then reuse a slot whose report never came (100 ms: long past the fetch of
		 * its frame; 0152: with 1 s and 16 slots, missing reports capped TX at ~16 frames/s, TCP stalled) */
		for (int k = 0; k < 4 && (tx0.busy & (1u << i)); k++) {
			int kind;
			(void)rx_next(wcmd_ev, RX_BUF_LEN, 200, &kind);
		}
		if (tx0.busy & (1u << i)) {
			if (mach_absolute_time() - tx0.when[i] < ms_abs(100)) {
				tx0.full++;
				return -1;
			}
			tx0.reused++;
		}
	}
	uint32_t boff = OFF_TXBUF + TX0_BUF * i;
	uint8_t *t = wd.va + boff;
	uint64_t pa = wd.pa + boff, fpa = pa + 64;
	uint16_t type = (uint16_t)(eth[12] << 8 | eth[13]);
	bzero(t, 64);
	put16(t + 0, 32 + len);                         /* TXD + frame, the append not counted */
	t[2] = 0x08;                                    /* ether offset ((14 - 2) + 4) / 2 */
	t[3] = 0x04;                                    /* LMAC, queue 1 = AC_BE (WMM set 0) */
	t[4] = WTBL_AP;
	t[5] = type >= 0x600 ? 0x90 : 0x80;             /* long, non-802.11, Ethernet II */
	t[7] = 0x04;                                    /* PktFt 0, own MAC 1 */
	put16(t + 12, 0xf000 | (protect ? 0x0002 : 0));
	if (lowRate) {
		t[11] = 0x80;                           /* fixed rate */
		put16(t + 26, rate_code());
	}
	put16(t + 32, i | 0x8000);                      /* MSDU id = token, valid */
	put32(t + 40, (uint32_t)fpa);
	put16(t + 44, (len & 0xfff) | (uint32_t)((fpa >> 32) & 0x7) << 12 | 0x8000);
	memcpy(t + 64, eth, len);
	begonia_wifi_dma_flush(boff, 64 + len);
	uint32_t *d = desc(OFF_TX(0), i);
	d[0] = (uint32_t)pa;
	d[1] = (64u << 16) | DESC_LASTSEC0;
	d[2] = 0;
	d[3] = (uint32_t)(pa >> 32) & 0xf;
	begonia_wifi_dma_flush(OFF_TX(0) + 16 * i, 16);
	tx0.busy |= 1u << i;
	tx0.when[i] = mach_absolute_time();
	wd.txIdx[0] = (i + 1) % TX0_N;
	wr(WF_TX_RING(0) + 8, wd.txIdx[0]);
	return 0;
}

/* 802.11 management frame on TX ring 15 behind its own TXD (subtype from the frame control). */
static int
tx_mgmt(const uint8_t *f, uint32_t len)
{
	static uint8_t pkt[32 + 512];
	if (len > 512) {
		return -1;
	}
	uint16_t sn = tx0.sn = (uint16_t)((tx0.sn + 1) & 0xfff);
	bzero(pkt, 32);
	put16(pkt + 0, 32 + len);
	pkt[2] = 0x0e;                                  /* (4 + 24) / 2 */
	pkt[3] = 0x84;                                  /* MCU port, MCU_Q1 */
	pkt[4] = sj.staRec ? WTBL_AP : WTBL_DEFAULT;
	pkt[5] = 0xcc;                                  /* long, 802.11 normal, 12-word header */
	pkt[7] = 0x06;                                  /* PktFt 2, own MAC 1 */
	pkt[8] = (uint8_t)((f[0] >> 4) & 0xf);          /* type 0, subtype */
	pkt[10] = 0x3d;                                 /* 2 s lifetime */
	pkt[11] = 0x80;                                 /* fixed rate */
	put16(pkt + 12, 0xf010);                        /* 30 tries, force TX */
	put16(pkt + 14, 0x8000 | sn);
	put16(pkt + 26, rate_code());
	memcpy(pkt + 32, f, len);
	return wfdma_tx(15, pkt, 32 + len) ? 0 : -1;
}

static int
hw_tx(const uint8_t *frame, uint32_t len, bool mgmt, bool protect)
{
	if (mgmt) {
		return tx_mgmt(frame, len);
	}
	bool eapol = len >= 14 && frame[12] == 0x88 && frame[13] == 0x8e;
	if (eapol && len >= 14 + 7) {
		/* EAPOL-Key with the pairwise bit (4-way M2/M4) never protected; group-key M2 is once the PTK is in */
		uint16_t info = (uint16_t)(frame[14 + 5] << 8 | frame[14 + 6]);
		protect = tx0.ptk && !(frame[14 + 1] == 3 && (info & 0x0008));
	} else {
		protect = protect && tx0.ptk;
	}
	return tx_data(frame, len, protect, eapol);
}

static int
hw_ch_privilege(bool get)
{
	uint8_t p[24], ev[128];
	static char junk[256];
	size_t o = 0;
	bzero(p, sizeof(p));
	if (!get) {
		p[1] = tx0.chToken;
		p[2] = 1;                               /* abort */
		p[10] = 4;
		return cmd_set(CID_CH_PRIVILEGE, p, sizeof(p));
	}
	p[1] = ++tx0.chToken;
	p[3] = sj.ap.ch;
	p[5] = sj.ap.ch > 14 ? 2 : 1;
	p[10] = 4;                                      /* DBDC band auto */
	put32(p + 12, 4000);                            /* max interval, ms */
	int n = wifi_cmd(CID_CH_PRIVILEGE, true, p, sizeof(p), EID_CH_PRIVILEGE, ev, sizeof(ev), 1000, junk, sizeof(junk), &o);
	if (n >= 32 + 3 && ev[33] == tx0.chToken && ev[34] == 0) {
		return 0;
	}
	printf("BEGONIA WIFI: channel %u: no grant (%d bytes, token %u/%u, status %u)\n", sj.ap.ch, n, n > 33 ? ev[33] : 0,
	    tx0.chToken, n > 34 ? ev[34] : 0);
	return -1;
}

static int
hw_sta_rec(uint8_t state)
{
	uint8_t p[168], ev[128];
	static char junk[256];
	size_t o = 0;
	uint16_t oper, basic;
	rate_sets(&oper, &basic);
	bzero(p, sizeof(p));
	p[0] = STA_IDX_AP;
	p[1] = 0x41;                                    /* legacy AP */
	memcpy(p + 2, sj.ap.bssid, 6);
	p[12] = 0;                                      /* BSS 0 */
	p[13] = phy_set();
	put16(p + 14, oper & 0x3fcf);
	put16(p + 16, basic);
	p[54] = WTBL_AP;
	p[55] = 0xff;
	p[68] = 3;                                      /* RTS policy legacy */
	put16(p + 74, rate_code());
	p[76] = 0;                                      /* no TX A-MPDU (0153: the data queue stalled under load) */
	p[77] = 0;                                      /* no RX block ack: we do not reorder */
	put32(p + 88, 0x10);
	p[92] = 0x40;
	p[128] = 1;
	p[129] = 1;
	put32(p + 132, 8192);
	if (state != 3) {
		p[20] = 0;                              /* STA_STATE_1 */
		return cmd_set(CID_UPDATE_STA_RECORD, p, sizeof(p));
	}
	p[20] = 2;                                      /* STA_STATE_3 */
	p[51] = 1;                                      /* answer with EVENT_ACTIVATE_STA_REC */
	put16(p + 8, sj.aid);
	put16(p + 10, 10);                              /* as in our association request */
	p[18] = (sj.wmm || sj.ht) ? 1 : 0;
	if (sj.ht && sj.mgmtLen > 30) {
		const uint8_t *h = ie_find(sj.mgmt + 30, sj.mgmtLen - 30, 45, NULL);
		if (h && h[1] == 26) {
			put16(p + 40, (uint32_t)(h[2] | h[3] << 8));    /* HT cap info */
			p[48] = h[4];                                   /* A-MPDU parameters */
			p[21] = h[5];                                   /* MCS 0-7 */
			p[22] = h[9] & 1;                               /* MCS 32 */
			memcpy(p + 24, h + 5, 10);
			put16(p + 34, (uint32_t)(h[15] | h[16] << 8) & 0x3ff);
			p[36] = h[17];
			put16(p + 42, (uint32_t)(h[21] | h[22] << 8));
			memcpy(p + 44, h + 23, 4);
			p[49] = h[27];
		}
	}
	int n = wifi_cmd(CID_UPDATE_STA_RECORD, true, p, sizeof(p), EID_ACTIVATE_STA_REC, ev, sizeof(ev), 1000, junk,
	    sizeof(junk), &o);
	if (n < 0) {
		printf("BEGONIA WIFI: STA record (state 3): no EVENT_ACTIVATE_STA_REC, going on\n");
	}
	return 0;
}

static void
hw_sta_remove(void)
{
	uint8_t p[4] = { 1, 27, 0, 0 };                 /* every record of BSS 0 */
	(void)cmd_set(CID_REMOVE_STA_RECORD, p, sizeof(p));
}

/* CMD_UPDATE_WMM_PARMS from the AP's WMM parameter element in its association response. */
static void
hw_wmm(void)
{
	static const uint8_t oui[4] = { 0x00, 0x50, 0xf2, 0x02 };
	if (!sj.wmm || sj.mgmtLen <= 30) {
		return;
	}
	const uint8_t *w = ie_find(sj.mgmt + 30, sj.mgmtLen - 30, 0xdd, oui);
	if (w == NULL || w[1] < 24 || w[6] != 1) {
		return;                                 /* not the parameter element */
	}
	uint8_t p[44];
	bzero(p, sizeof(p));
	for (int ac = 0; ac < 4; ac++) {                /* BE, BK, VI, VO as in the element */
		const uint8_t *r = w + 10 + 4 * ac;
		uint8_t *q = p + 10 * ac;
		put16(q + 0, (1u << (r[1] & 0xf)) - 1);
		put16(q + 2, (1u << (r[1] >> 4)) - 1);
		put16(q + 4, (uint32_t)(r[2] | r[3] << 8));
		put16(q + 6, r[0] & 0xf);
		q[9] = (r[0] >> 4) & 1;
	}
	p[41] = 1;
	(void)cmd_set(CID_UPDATE_WMM_PARMS, p, sizeof(p));
}

static int
hw_bss_info(bool connected)
{
	uint8_t p[116];
	uint16_t oper, basic;
	rate_sets(&oper, &basic);
	bzero(p, sizeof(p));
	p[1] = connected ? 0 : 1;
	p[3] = sj.ap.ssid_len;
	memcpy(p + 4, sj.ap.ssid, sj.ap.ssid_len);
	memcpy(p + 36, sj.ap.bssid, 6);
	p[42] = (sj.wmm || sj.ht) ? 1 : 0;
	put16(p + 44, oper);
	put16(p + 46, basic);
	p[48] = STA_IDX_AP;
	put16(p + 50, rate_code());
	p[52] = sj.ap.ch > 14 ? 3 : (oper & 0x3fc0) ? 1 : 0;
	p[53] = sj.rsn ? 7 : 0;                         /* WPA2-PSK / open */
	p[54] = sj.rsn ? 6 : 1;                         /* CCMP enabled / encryption disabled */
	p[55] = phy_set();
	p[58] = WTBL_BMC;
	/* RLM parameters at +68 */
	p[69] = sj.ap.ch > 14 ? 2 : 1;
	p[70] = sj.ap.ch;
	if (connected) {
		const uint8_t *erp = ie_find(sj.ap.ie, sj.ap.ie_len, 42, NULL);
		const uint8_t *op = ie_find(sj.ap.ie, sj.ap.ie_len, 61, NULL);
		p[72] = (erp && erp[1] >= 1 && (erp[2] & 0x02)) ? 1 : 0;
		if (sj.ht && op && op[1] >= 22) {
			uint16_t i2 = (uint16_t)(op[4] | op[5] << 8), i3 = (uint16_t)(op[6] | op[7] << 8);
			p[73] = i2 & 0x3;
			p[74] = (i2 & 0x4) ? 1 : 0;
			p[75] = (op[3] & 0x08) ? 0 : 1;
			put16(p + 76, i3);
			put16(p + 78, i2);
			p[80] = op[3] & (uint8_t)~0x07;  /* 20 MHz only */
		}
		p[81] = (sj.ap.capinfo & 0x0020) ? 1 : 0;
		p[82] = (sj.ap.ch > 14 || (sj.ap.capinfo & 0x0400)) ? 1 : 0;
		put16(p + 86, basic);
	}
	p[88] = 1;
	p[89] = 1;
	p[90] = 4;                                      /* DBDC band auto */
	p[93] = 1;                                      /* NSS */
	return cmd_set(CID_UPDATE_BSS_INFO, p, sizeof(p));
}

static void
hw_pm_connected(bool connected)
{
	uint8_t p[12];
	bzero(p, sizeof(p));
	if (!connected) {
		(void)cmd_set(CID_PM_BSS_ABORT, p, 4);
		return;
	}
	const uint8_t *tim = ie_find(sj.ap.ie, sj.ap.ie_len, 5, NULL);
	p[1] = (tim && tim[1] >= 2 && tim[3]) ? tim[3] : 1;
	put16(p + 2, sj.aid);
	put16(p + 4, sj.ap.bintval ? sj.ap.bintval : 100);
	(void)cmd_set(CID_PM_BSS_CONNECTED, p, sizeof(p));
	uint8_t cam[4] = { 0, 0, 0, 0 };                /* power save: CAM while we poll */
	(void)cmd_set(CID_POWER_SAVE_MODE, cam, sizeof(cam));
}

static int
hw_key(bool pairwise, uint8_t keyid, const uint8_t *key, uint32_t klen, const uint8_t *rsc)
{
	uint8_t p[64], ev[128];
	static char junk[256];
	size_t o = 0;
	(void)rsc;                                      /* gen4m never sends it for CCMP */
	if (klen != 16 && klen != 32) {
		return -1;
	}
	bzero(p, sizeof(p));
	p[0] = 1;
	p[1] = pairwise ? 1 : 0;
	p[2] = pairwise ? 1 : 0;
	memcpy(p + 4, sj.ap.bssid, 6);
	p[11] = klen == 32 ? 2 : 4;                     /* TKIP (group only) / CCMP */
	p[12] = keyid;
	p[13] = (uint8_t)klen;
	p[14] = pairwise ? WTBL_AP : WTBL_BMC;
	memcpy(p + 16, key, klen);
	int n = wifi_cmd(CID_ADD_REMOVE_KEY, true, p, sizeof(p), pairwise ? EID_ADD_PKEY_DONE : 0, ev, sizeof(ev), 500, junk,
	    sizeof(junk), &o);
	bzero(p, sizeof(p));
	if (pairwise) {
		tx0.ptk = true;
		if (n < 0) {
			printf("BEGONIA WIFI: no ADD_PKEY_DONE for the PTK, going on\n");
		}
	}
	return n < 0 && !pairwise ? -1 : 0;
}


/* ---- the worker ---- */

static size_t
wifi_up(char *out, size_t len, size_t o)
{
	if (!vw.on) {
		o = wifi_on(out, len, o);
		if (!vw.on || rd(CONN_MCU_COM_REG0) != CONN_IDLE_MAGIC) {
			return o;
		}
	}
	if (!vw.wmt) {
		o = wifi_wmt(out, len, o);
		if (!vw.wmt) {
			return o;
		}
	}
	if ((rd(WF_READY) & 0x6) != 0x6) {
		o = wifi_fw(out, len, o);
		if ((rd(WF_READY) & 0x6) != 0x6) {
			return o;
		}
	}
	if (!wl.init) {
		o = wifi_init(out, len, o);
	}
	return o;
}

/* Status for the ioctls (vw_mtx held). */
static void
st_publish(void)
{
	struct vwl_status s;
	bzero(&s, sizeof(s));
	s.state = sj.state ? sj.state : (wl.scanBusy ? 1 : 0);
	s.link = sj.link;
	s.result = sj.result;
	s.rsn = sj.rsn;
	s.ch = sj.ap.ch;
	s.rssi = sj.rssi;
	s.rate = sj.ht ? 72 : 54;
	memcpy(s.ssid, sj.ap.ssid, 32);
	s.ssid_len = sj.ap.ssid_len;
	memcpy(s.bssid, sj.ap.bssid, 6);
	memcpy(s.mac, wl.mac, 6);
	s.txFrames = sj.tx;
	s.rxFrames = sj.rx;
	s.txDrops = sj.txDrops;
	lck_mtx_lock(&vs_mtx);
	s.power = vs.want;
	vs.st = s;
	if (sj.state || sj.link) {
		bss_to_vwl(&sj.ap, &vs.cur, mach_absolute_time());
		vs.cur.rssi = sj.rssi;
	}
	lck_mtx_unlock(&vs_mtx);
}

/* Drop vw_mtx, then tell viva_wlan.c what happened (events, received frames). */
static void
vw_unlock(void)
{
	st_publish();
	uint32_t bits = vn.bits;
	int assoc = vn.assoc;
	uint32_t reason = vn.reason;
	bool power = vn.power;
	mbuf_t rx = vn.rxHead;
	bzero(&vn, sizeof(vn));
	lck_mtx_unlock(&vw_mtx);

	if (bits & NOTE_POWER) {
		begonia_wlan_power(power);
	}
	if (bits & NOTE_DOWN) {
		begonia_wlan_link(false, reason);
	}
	if (bits & NOTE_SCAN) {
		begonia_wlan_scan_done(0);
	}
	if (bits & NOTE_ASSOC) {
		begonia_wlan_assoc_done(assoc);
	}
	while (rx != NULL) {
		mbuf_t next = mbuf_nextpkt(rx);
		mbuf_setnextpkt(rx, NULL);
		begonia_wlan_input(rx);
		rx = next;
	}
}

static uint8_t tx_buf[2048];

/* One iteration: requests, then RX, the TX queue and the timers. Returns true when there is more to do soon. */
static bool
sta_step(char *out, size_t len, size_t *po)
{
	size_t o = *po;
	lck_mtx_lock(&vs_mtx);
	uint32_t req = vs.req;
	bool want = vs.want;
	struct vwl_scan sc = vs.scan;
	struct vwl_join jn = vs.join;
	vs.req = 0;
	bzero(&vs.join, sizeof(vs.join));
	lck_mtx_unlock(&vs_mtx);

	if (req & (REQ_POWER | REQ_POWER_EVENT)) {
		bool changed = false;
		if (want && !wl.init) {
			o = wifi_up(out, len, o);
			say("power: radio %s\n", wl.init ? "UP" : "FAILED to come up");
			if (!wl.init && vw.on) {
				o = wifi_off(out, len, o);      /* the next request tries again from scratch */
			}
			changed = wl.init;
		} else if (!want && vw.on) {
			sta_leave(out, len, &o);
			o = wifi_off(out, len, o);
			say("power: radio off\n");
			changed = true;
		}
		if (changed || (req & REQ_POWER_EVENT)) {
			vn.bits |= NOTE_POWER;
			vn.power = want;
		}
	}
	if (wl.init && (req & REQ_LEAVE)) {
		sta_leave(out, len, &o);
	}
	if (wl.init && (req & REQ_JOIN)) {
		sta_join(&jn, out, len, &o);
	} else if (req & REQ_JOIN) {
		sj.result = 2;
		vn.bits |= NOTE_ASSOC;
		vn.assoc = 2;
	}
	bzero(&jn, sizeof(jn));
	if (req & REQ_SCAN) {
		if (!wl.init) {
			vn.bits |= NOTE_SCAN;           /* radio off: an empty answer */
		} else if (!wl.scanBusy && sj.state != 2 && sj.state != 3) {
			if (scan_send(sc.ssid, sc.ssid_len, sc.ch, sc.nch, sc.passive, out, len, &o) != 0) {
				sta_scan_done_locked();
			}
		}
	}
	if (!wl.init) {
		*po = o;
		return false;
	}

	/* RX: up to 64 packets */
	for (int k = 0; k < 64; k++) {
		int kind;
		if (rx_next(wcmd_ev, RX_BUF_LEN, 0, &kind) == 0) {
			break;
		}
	}
	/* TX queue */
	for (int k = 0; k < 64; k++) {
		lck_mtx_lock(&vs_mtx);
		mbuf_t m = vs.txHead;
		if (m != NULL) {
			vs.txHead = mbuf_nextpkt(m);
			if (vs.txHead == NULL) {
				vs.txTail = NULL;
			}
			vs.txn--;
		}
		lck_mtx_unlock(&vs_mtx);
		if (m == NULL) {
			break;
		}
		mbuf_setnextpkt(m, NULL);
		size_t mlen = mbuf_pkthdr_len(m);
		if (sj.link && mlen >= 14 && mlen <= sizeof(tx_buf) && mbuf_copydata(m, 0, mlen, tx_buf) == 0 &&
		    hw_tx(tx_buf, (uint32_t)mlen, false, sj.rsn) == 0) {
			sj.tx++;
		} else {
			sj.txDrops++;
		}
		mbuf_freem(m);
	}
	/* timers */
	uint64_t now = mach_absolute_time();
	if (wl.scanBusy && now > wl.scanDeadline) {
		wl.scanBusy = false;
		wl.scanStatus = -1;
		say("scan: no SCAN_DONE (timeout)\n");
		sta_scan_done_locked();
	}
	if (sj.state == 3 && sj.rsn && !sj.link && sj.deadline && now > sj.deadline) {
		sta_fail(sj.sentMsg2 && sj.wpa.state == 1 ? 21 : 22, sj.sentMsg2 ? "no message 3 (wrong password?)" :
		    "no EAPOL from the AP", out, len, &o);
	}
	if (sj.state == 0 && (sj.staRec || sj.chHeld)) {
		sta_teardown();                         /* the AP left us (deauth, beacon timeout) */
	}
	*po = o;
	return wl.scanBusy || (sj.state != 0 && !sj.link) || vs.txn;
}

static void
vw_worker(__unused void *arg, __unused wait_result_t wr)
{
	static char log[2048];
	for (;;) {
		size_t o = 0;
		log[0] = 0;
		lck_mtx_lock(&vw_mtx);
		bool busy = sta_step(log, sizeof(log), &o);
		bool up = wl.init, link = sj.link;
		vw_unlock();
		if (log[0]) {
			printf("BEGONIA WIFI: %s", log);
		}
		struct timespec ts = { 0, (busy ? 1 : link ? 2 : 20) * NSEC_PER_MSEC };
		lck_mtx_lock(&vs_mtx);
		if (vs.req == 0 && vs.txn == 0) {
			(void)msleep(&vw_chan, &vs_mtx, PZERO | PDROP, "begoniawifi", up ? &ts : NULL);
		} else {
			lck_mtx_unlock(&vs_mtx);
		}
	}
}

/* vs_mtx held */
static void
vw_kick_locked(uint32_t req)
{
	vs.req |= req;
	if (!vs.worker) {
		thread_t th;
		if (kernel_thread_start(vw_worker, NULL, &th) == KERN_SUCCESS) {
			vs.worker = true;
			thread_deallocate(th);
		}
	}
	wakeup(&vw_chan);
}

/* ---- API for viva_wlan.c ---- */

int
begonia_wifi_set_power(bool on)
{
	lck_mtx_lock(&vs_mtx);
	vs.want = on;
	vw_kick_locked(REQ_POWER | REQ_POWER_EVENT);
	lck_mtx_unlock(&vs_mtx);
	return 0;
}

int
begonia_wifi_scan(const struct vwl_scan *s)
{
	lck_mtx_lock(&vs_mtx);
	vs.want = true;
	vs.scan = *s;
	vw_kick_locked(REQ_SCAN | REQ_POWER);
	lck_mtx_unlock(&vs_mtx);
	return 0;
}

int
begonia_wifi_scan_result(uint32_t i, struct vwl_bss *out)
{
	int err = ENOENT;
	lck_mtx_lock(&vs_mtx);
	if (i < vs.nres) {
		*out = vs.res[i];
		err = 0;
	}
	lck_mtx_unlock(&vs_mtx);
	return err;
}

int
begonia_wifi_current_bss(struct vwl_bss *out)
{
	lck_mtx_lock(&vs_mtx);
	*out = vs.cur;
	lck_mtx_unlock(&vs_mtx);
	return 0;
}

int
begonia_wifi_join(const struct vwl_join *j)
{
	lck_mtx_lock(&vs_mtx);
	vs.want = true;
	vs.join = *j;
	vs.st.result = 0;                               /* running, until the worker says otherwise */
	vs.st.state = 2;
	vw_kick_locked(REQ_JOIN | REQ_POWER);
	lck_mtx_unlock(&vs_mtx);
	return 0;
}

void
begonia_wifi_leave(void)
{
	lck_mtx_lock(&vs_mtx);
	vw_kick_locked(REQ_LEAVE);
	lck_mtx_unlock(&vs_mtx);
}

void
begonia_wifi_get_status(struct vwl_status *s)
{
	lck_mtx_lock(&vs_mtx);
	*s = vs.st;
	s->power = vs.want;
	lck_mtx_unlock(&vs_mtx);
}

void
begonia_wifi_output(mbuf_t m)
{
	lck_mtx_lock(&vs_mtx);
	if (vs.txn >= 256) {
		lck_mtx_unlock(&vs_mtx);
		mbuf_freem(m);
		return;
	}
	mbuf_setnextpkt(m, NULL);
	if (vs.txTail) {
		mbuf_setnextpkt(vs.txTail, m);
	} else {
		vs.txHead = m;
	}
	vs.txTail = m;
	vs.txn++;
	wakeup(&vw_chan);
	lck_mtx_unlock(&vs_mtx);
}

/* ---- USB link ---- */

static size_t
wifi_scan(const char *args, char *out, size_t len, size_t o)
{
	if (!wl.init) {
		say("scan: wifi init first (or wifi up)\n");
		return o;
	}
	while (*args == ' ') {
		args++;
	}
	bool only2g = strncmp(args, "2g", 2) == 0;
	if (only2g) {
		args += 2;
		while (*args == ' ') {
			args++;
		}
	}
	static const uint8_t ch2g[13] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
	uint32_t frames0 = wl.frames;
	if (scan_send((const uint8_t *)args, (uint32_t)strlen(args), only2g ? ch2g : NULL, only2g ? 13 : 0, false, out,
	    len, &o) != 0) {
		return o;
	}
	int r = scan_wait(15000);
	say("scan: %s (%u beacons/responses received)\n", r == 0 ? "done" : "no SCAN_DONE in 15 s", wl.frames - frames0);
	/* strongest first, this scan only */
	uint8_t order[WL_MAX_BSS], m = 0;
	for (uint32_t i = 0; i < wl.nbss; i++) {
		if (wl.bss[i].seen == wl.scanGen) {
			order[m++] = (uint8_t)i;
		}
	}
	for (uint32_t i = 1; i < m; i++) {
		for (uint32_t j = i; j > 0 && wl.bss[order[j]].rssi > wl.bss[order[j - 1]].rssi; j--) {
			uint8_t t = order[j];
			order[j] = order[j - 1];
			order[j - 1] = t;
		}
	}
	say("scan: %u networks\n", m);
	for (uint32_t i = 0; i < m; i++) {
		const struct wl_bss *e = &wl.bss[order[i]];
		say("%4d dBm ch %3u %02x:%02x:%02x:%02x:%02x:%02x %-9s %.*s\n", e->rssi, e->ch, e->bssid[0], e->bssid[1],
		    e->bssid[2], e->bssid[3], e->bssid[4], e->bssid[5], bss_security(e), e->ssid_len, e->ssid);
	}
	return o;
}

/* wifi join SSID [PASSPHRASE]: the same join wifid asks for (synchronous here, with the handshake). */
static size_t
wifi_join_link(const char *args, char *out, size_t len, size_t o)
{
	if (!wl.init) {
		say("join: wifi up first\n");
		return o;
	}
	while (*args == ' ') {
		args++;
	}
	struct vwl_join j;
	bzero(&j, sizeof(j));
	const char *sp = strchr(args, ' ');
	size_t sl = sp ? (size_t)(sp - args) : strlen(args);
	if (sl == 0 || sl > 32) {
		say("join: usage wifi join SSID [PASSPHRASE] (SSID without spaces)\n");
		return o;
	}
	memcpy(j.ssid, args, sl);
	j.ssid_len = (uint8_t)sl;
	if (sp) {
		const char *pass = sp + 1;
		size_t pl = strlen(pass);
		if (pl < 8 || pl > 63) {
			say("join: the passphrase must be 8..63 characters\n");
			return o;
		}
		begonia_wpa_pmk(pass, j.ssid, j.ssid_len, j.pmk);
		j.has_pmk = 1;
	}
	sta_join(&j, out, len, &o);
	bzero(&j, sizeof(j));
	/* the handshake: pump RX until connected or failed */
	while (sj.state == 3 && !sj.link) {
		int kind;
		(void)rx_next(wcmd_ev, RX_BUF_LEN, 10000, &kind);
		if (sj.deadline && mach_absolute_time() > sj.deadline) {
			sta_fail(sj.sentMsg2 ? 21 : 22, sj.sentMsg2 ? "no message 3 (wrong password?)" : "no EAPOL from the AP",
			    out, len, &o);
		}
	}
	if (sj.link) {
		say("join: link up (%u EAPOL frames in, %u out); en0 takes it from here\n", sj.eapolRx, sj.eapolTx);
	}
	return o;
}

static size_t
wifi_sta(char *out, size_t len, size_t o)
{
	struct vwl_status s;
	begonia_wifi_get_status(&s);
	say("sta: radio %s (asked %s), state %u, link %u, result %u, \"%.*s\" ch %u %d dBm, aid %u\n",
	    wl.init ? "up" : "down", s.power ? "on" : "off", sj.state, sj.link, sj.result, sj.ap.ssid_len, sj.ap.ssid,
	    sj.ap.ch, sj.rssi, sj.aid);
	say("sta: data tx %u (drops %u) rx %u (drops %u), EAPOL in %u out %u (wpa state %u, drops %u, mic failures %u), "
	    "%u events, %u frames, %u data\n", sj.tx, sj.txDrops, sj.rx, sj.rxDrops, sj.eapolRx, sj.eapolTx, sj.wpa.state,
	    sj.wpa.drops, sj.wpa.mic_fail, wl.events, wl.frames, wl.dataFrames);
	say("sta: tx ring 0: %u MSDU reports, slots in use 0x%08x, %u reused without report, %u waits for a report, %u ring "
	    "full, cidx %u didx %u; first report %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n", tx0.reports,
	    tx0.busy, tx0.reused, tx0.full, tx0.ringFull, rd(WF_TX_RING(0) + 8), rd(WF_TX_RING(0) + 12),
	    tx0.firstReport[0], tx0.firstReport[1], tx0.firstReport[2], tx0.firstReport[3], tx0.firstReport[4],
	    tx0.firstReport[5], tx0.firstReport[6], tx0.firstReport[7], tx0.firstReport[8], tx0.firstReport[9],
	    tx0.firstReport[10], tx0.firstReport[11]);
	if (o < len) {
		o += begonia_wlan_stats(out + o, len - o);
	}
	return o;
}

void
begonia_wifi_link(const char *args, char *out, size_t len)
{
	size_t o = 0;
	if (len == 0) {
		return;
	}
	out[0] = 0;
	while (*args == ' ') {
		args++;
	}
	lck_mtx_lock(&vw_mtx);
	if (strncmp(args, "on", 2) == 0) {
		printf("BEGONIA WIFI: power on\n");
		o = wifi_on(out, len, o);
	} else if (strncmp(args, "wmt", 3) == 0) {
		printf("BEGONIA WIFI: WMT over STP/BTIF\n");
		o = wifi_wmt(out, len, o);
	} else if (strncmp(args, "init", 4) == 0) {
		printf("BEGONIA WIFI: station init\n");
		o = wifi_init(out, len, o);
	} else if (strncmp(args, "up", 2) == 0) {
		printf("BEGONIA WIFI: up (on, wmt, fw, init)\n");
		o = wifi_up(out, len, o);
		say("up: radio %s\n", wl.init ? "UP" : "NOT up");
	} else if (strncmp(args, "scan", 4) == 0) {
		o = wifi_scan(args + 4, out, len, o);
	} else if (strncmp(args, "join", 4) == 0) {
		printf("BEGONIA WIFI: join (link)\n");
		o = wifi_join_link(args + 4, out, len, o);
	} else if (strncmp(args, "leave", 5) == 0) {
		sta_leave(out, len, &o);
	} else if (strncmp(args, "sta", 3) == 0) {
		o = wifi_sta(out, len, o);
	} else if (strncmp(args, "ring", 4) == 0 && wd.uva != NULL) {
		for (uint32_t r = 0; r < 2; r++) {
			say("rx%u: hw cidx %u didx %u, driver next %u, %u taken, %u skipped (last skipped d1 0x%08x); d1:", r,
			    rd(WF_RX_RING(r) + 8), rd(WF_RX_RING(r) + 12), wd.rxIdx[r], wd.rxGot[r], wd.rxSkipped[r], wd.rxSkipD1[r]);
			for (uint32_t i = 0; i < RING_N; i++) {
				say(" %08x", desc(OFF_RX(r), i)[1]);
			}
			say("\n");
		}
		say("tx0: hw cidx %u didx %u, driver next %u; tx15: cidx %u didx %u; GLO_CFG 0x%08x INT_STA 0x%08x\n",
		    rd(WF_TX_RING(0) + 8), rd(WF_TX_RING(0) + 12), wd.txIdx[0], rd(WF_TX_RING(15) + 8), rd(WF_TX_RING(15) + 12),
		    rd(WF_GLO_CFG), rd(WF_INT_STA));
	} else if (strncmp(args, "cap", 3) == 0) {
		o = wifi_cap(out, len, o);
	} else if (strncmp(args, "fw", 2) == 0) {
		printf("BEGONIA WIFI: firmware download\n");
		o = wifi_fw(out, len, o);
	} else if (strncmp(args, "off", 3) == 0) {
		sta_leave(out, len, &o);
		o = wifi_off(out, len, o);
	} else if (strncmp(args, "detect", 6) == 0) {
		if (vw.on) {
			say("detect: CONN is on (wifi off first)\n");
		} else {
			printf("BEGONIA WIFI: a-die detection\n");
			o = wifi_detect(out, len, o);
		}
	} else {
		o = wifi_status(out, len, o);
	}
	bool up = wl.init;
	vw_unlock();
	if (up) {
		lck_mtx_lock(&vs_mtx);                  /* the worker polls RX / TX from now on */
		vw_kick_locked(0);
		lck_mtx_unlock(&vs_mtx);
	}
	printf("%s", out);
	(void)o;
}
#endif
