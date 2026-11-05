# Hardware bring-up and XNU integration

## Current gate: physical evidence

Update 2026-11-05: SSH and sudo on postmarketOS work. Live DTB and boot/DTBO/vbmeta backups are captured. Boot is single-slot header v2. UFS storage is confirmed; earlier eMMC planning is superseded. A bootloader-time console and XNU integration are still unavailable. A build cannot prove that unknown hardware will boot. Do not publish this as an iOS port or flash a sweet/Infinix image.

1. Capture the exact device/variant, current build, slot suffix, bootloader unlock status, boot header, DTB/DTBO, partition layout and backups. The collector reads only; root-dependent reads may fail. Back up calibration, NVRAM/NVDATA, persist, boot, recovery, DTBO, vbmeta and the partition table using the actual names from this device. The collector's boot capture is not a complete backup.
2. Inspect the boot image and live DTB, including appended DTBs, vendor MTK wrappers, load addresses, all RAM reservations and framebuffer layout. Do not use the static DTS framebuffer: its comment describes an FPGA fallback. Confirm whether this bootloader actually supports temporary boot. Do not assume A/B slots from the Infinix port.
3. Implement an observable stage-0 transport from verified live data: preferably inherited framebuffer or a known-working UART. Add exception vectors and bounded probing before any risky register access. Package against the actual stock boot header only after this review. The current diagnostic has no exception handler, no watchdog servicing and no visible phone console.
4. Boot stage 0 on hardware, record EL/MIDR/MMFR0/CNTFRQ and live memory map. Verify TGran16 on every participating CPU; single-core first. The vendor DTS's big-core compatible strings and MPIDRs are evidence to check, not a substitute for runtime results. QEMU's timer frequency is not the phone's.
5. Adapt the ios-sweet Mach-O loader, 16K MMU setup, Apple device tree and boot arguments, with a vetted non-overlapping memory placement plan. Replace Qualcomm GIC addresses and timer INTIDs using verified MT6785 facts. Test XNU's early console and panic path on one core before SMP. Do not enable the sweet UFS or other Qualcomm drivers.
6. Bring up XNU with a minimal open-source RAM disk first. Adapt a read-only UFSHCI path for MediaTek at 0x11270000, reviewing MediaTek PHY, clock/reset and power requirements, DMA/cache behaviour and timeouts. The MT6768 eMMC driver does not match this phone. Start with a RAM root to avoid partition changes if the measured RAM budget permits it.
7. Establish framebuffer IOSurface/IOMobileFramebuffer services using the live panel buffer, stride and format. Reuse only portable ios-sweet service/ABI work. Add the actual Novatek SPI touchscreen, power/keypad and battery drivers. The MT6768 port's TD4160 touch and Mali-G52 setup are not begonia drivers.
8. Integrate the user's own Apple iPhone arm64 iOS 16 firmware locally. Do not redistribute Apple system binaries or vendor firmware. Match the userland ABI and kernel patches; successful open-kernel boot alone does not establish iOS compatibility. Validate launchd, backboardd and SpringBoard with captured logs before calling it an iOS boot.
9. Treat Mali-G76 graphics, Wi-Fi, Bluetooth, audio, cameras, cellular, charging, thermal management and suspend as separate driver projects. A boot screen provides no evidence these work. Retain boot logs, image hashes and a tested recovery route for every hardware iteration.

## Reuse assessment

| Reference | Useful material | Requires new work on begonia |
| --- | --- | --- |
| ios-sweet | Minimal entry, XNU patch series, Mach-O/ADT loader, software compositor and HID ABI research | Memory map, boot envelope, GIC/timer config, MediaTek UFS, GPIO/PMIC, Novatek SPI, USB |
| HakiOS common main | Build/configuration framework, open-kernel QEMU path, common driver interfaces and porting docs | MT6785 SoC/device integration; common main does not register the MT6768 port |
| hakios-mt6768 | MediaTek loader and debug transport design | Different storage (begonia has UFS), SoC, touch, GPU, panel, memory/partition layout; some userland integration is not in the common tree |
| hackiosx.dev | Project overview and compatibility/bring-up guidance | No begonia source or downloadable working ROM |

No defconfig is provided that falsely enables another phone's drivers. A buildable XNU target requires the platform and loader work above; a name-only configuration would hide those gaps.


## Latest verified milestones

The architectural probe and a real XNU/user-space hello boot have run on this phone. Read-only bare-metal UFS reads on LUN2 match Linux, and bootloader layer-1 framebuffer values are measured. The iOS 16.5 filesystem is prepared and staged as a regular postmarketOS file. An extent-backed native root driver is compiled; its first hardware iOS test is pending log retrieval. See PROCESS.md for the exact current recovery/install state; earlier numbered steps are an integration outline, not a statement that no work has been done.
