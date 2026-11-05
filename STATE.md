# Current state — 2026-11-06

## Immediate task

Confirm the physical touch port in the iOS GUI. **Native controller startup and
real finger coordinates are verified in XNU; GUI interaction is not yet verified.**
User rejected two-hour waits. Use short bounded tests. The ten-minute UI
test failed to open apps; no further tests tonight.

## Current phone / next action

**Stopped for the night at the owner's request. Phone is running postmarketOS.**
The original 64 MiB PMOS boot image was restored over SSH and its full checksum
verified, without another reboot. Next normal restart boots PMOS, not the test.
No scheduled or background test is pending. iOS system/data images remain intact.

v22 completed its ten-minute automatic capture and returned to PMOS. Capture
checksums and exact request ID validated. Owner reports the home screen appeared
but tapping apps still does not open them. This is NOT a fully working touch UI.

Captured evidence: kernel posts type1/type2/type3 (down/move/release), backboardd
reads real samples, and the digitizer reports modes0xff then0xfe (wake-only).
No mode0x1 appears in the retained capture; the circular log may omit intermediate
history. In inject.c, wake-only DOWN becomes a power-button wake event and its
finger sequence is swallowed. This is a concrete next investigation path, not a
proven complete root cause. Also distinguish app launch with frozen rendering
from taps dropped before dispatch.

Next session: inspect HID/wake/display-state handling and collect per-event
producer/consumer traces. Optional `--touch-debug` now adds existing touch_lat=1
instrumentation to a future payload; it has NOT been boot-tested yet. Do not
re-run long tests or claim the hardware driver alone completes UI interaction.

v22 retained: out/begonia-chain-xnu-forced/ios16-touch-ui-v22.img,
49766400 bytes; SHA256
0105f617fa90110732a554be712dcf67f04edfc78787052e3402df0c4a7800af.
Native capture: private/device/native-v22-mirror.bin and decoded log/JSON.
Original-boot restoration report: private/device/pmos-overnight-restore.txt.

## Verified touch work

- Actual Novatek NT36672A / physical SPI5 `0x11019000`; Linux device spi1.0.
- Measured working reset GPIO2 is LOW; GPIO1 is input IRQ. Previous v16/v20
  reset-high implementation did not match Linux. Touch firmware stays private.
- Linux SPI control loads all eight SRAM sections with exact sampled readbacks
  and firmware boot. Small writes and two-byte CRC length fields also pass.
- Corrected polled FIFO passes the same checks and reads 65-byte touch reports.
  Physical Linux capture: 334 changed samples, 42 releases, multiple contacts.
- Native **v21** capture: validated checksums/request, zero of eight readback
  mismatches, firmware A0/A1, bounds1080x2340, polling enabled, real down/move
  events posted to hakios-touch. GUI was not required in this 90-second test.
- Essential fixes: set packet length before filling FIFO; avoid polling
  read-clear STATUS0; delay through wire time and poll STATUS1 idle, acknowledge
  once afterward; exact 32-bit MMIO accesses; reset low and CS quiet interval.
- Linux spidev testing temporarily unbound its original touch driver. IRQ mapping
  prevented immediate rebind; normal PMOS reboot restored it and binding was
  verified before v21. The Linux testing session is finished.

See docs/TOUCH.md. Decoder tests and XNU build pass. The existing backboardd HID
injection consumes the kernel queue. Physical touch in apps still awaits v22.

## Verified GUI and persistent storage

- Owner photographed native SpringBoard on v10; owner again confirmed the home
  screen on **v18**, with persistent data and touch disabled. v18 capture matched
  request/checksums, logged unlock success and SpringBoard surface creation.
- v18 baseline: out/begonia-chain-xnu-forced/ios16-preserve-diagnostics-v18.img
  49768448 bytes, SHA256
  adadf032275642ce7f694f5f895c63e0634e4eab151f95c51b55dd79f4f3f3cd.
- System file on phone: /home/user/ios-begonia/system.apfs, read-only.
- Data file: /home/user/ios-begonia/data.hfs, fully allocated 16 GiB journaled
  HFS+, Linux immutable inode pins its 130 extents. No GPT change or shrink.
- Multiple boot markers, mobile caches and LaunchServices stores (3.5/3.9 MB)
  survive reboot. The data image was not recreated during this work.
- The HFS uid/gid/mask VNOVAL fix is recorded in userland/patches and built by
  tools/prepare_userland.py, invoked by make_ui_support.py. Removed another
  agent's recurring SystemGroup deletion and hidden permission failures.
- lsd logging restored; blanket code-signing boot flags are opt-in. Keeping the
  console does not prevent GUI (proved by owner observation). Memory report
  fields are MB; `programs 13` is not a process count.

## Recovery and private artifacts

Original PMOS boot backup: private/device/backups/boot.img, 67108864 bytes,
SHA256 115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7.
Always select the exact FASTBOOT target stored privately. Never flash GPT,
bootloaders, modem, persist, or unrelated partitions. Keep known GUI images.

Diagnostic images contain original PMOS kernel/ramdisk with a 2 MiB no-map
reservation at 0x1b7e00000. The independent CPU1 supervisor captures BEGLOG02;
out/read-log-mirror (built from tools/read_log_mirror.c) plus tools/parse_log_mirror.py verify it. Check capture request ID
against the exact payload; manual early resets may leave no valid capture and
may retry iOS. Original PMOS backup has no reserved mirror area.

Native v21 capture: private/device/native-v21-mirror.bin and decoded log/JSON.
Linux evidence: private/device/touch-* files. The initial Python byte-copy MMIO
experiments were invalid; only results using exact-width C accessors count.
Root/data extent blobs are device-specific and private. Do not relocate either
image without regenerating and verifying maps. No Apple assets, firmware,
photos, identifiers, backups or generated boot images are published.

GPU acceleration remains unported. Linux G76 product0x7211/shader mask0xf is
measured; Viva's Valhall and Apollo's Adreno code are references, not compatible
binaries. Mu menu handoff remains deferred. Full chronology: docs/PROCESS.md.
Repository: https://git.tuxy.dev/tuxy/ios-begonia (main).
