# Current state — 2026-11-06

## Immediate task

Confirm the physical touch port in the iOS GUI. **Native controller startup and
real finger coordinates are verified in XNU; GUI interaction is not yet verified.**
User rejected two-hour waits. Use short bounded tests. The ten-minute UI
test failed to open apps. Work resumed at the owner's request on 2026-11-06.

## Current phone / next action

Work resumed on 2026-11-06. PMOS was booted from its restored original image.
Built and installed **v23 routing diagnostic**, readback verified. Reboot is the
final action of this turn; UI result is pending owner feedback. Deadline600s.
No scripted gestures. Original PMOS backup and all iOS data remain retained.

v23 keeps the verified kernel touch transport. A recorded opt-in patch to
userland/src/inject.c adds per-event routing logs and `begonia_touch_direct=1`
to bypass conversion of touch DOWN into wake-button events. `touch_lat=1` traces
kernel producer and backboardd consumer timing. This is an experiment, not a
confirmed fix. Defaults are unchanged without those boot arguments.

v23: out/begonia-chain-xnu-forced/ios16-routing-v23.img, 49768448 bytes,
SHA256 5b2911d90b72fbd756c8a5e8cab1f4694fc3b62f418e716d26910c33a7b6ce24.
Support builder now compiles both init and libhakios.dylib from recorded patches.

Previous v22 capture validated down/move/release into the queue and backboardd
reads, but owner could not open apps. Retained mode0xfe is wake-only; not enough
log history to prove it never changed. Compare direct dispatch with normal wake
routing and check whether applications launch while rendering stays frozen.

Owner also asked for live serial to the laptop. USB console is a follow-up:
MTU3 native USB gadget/PHY support is not implemented; hardware UART would need
identified pads, an adapter and console setup. Current working log transport is
the reserved-RAM capture retrieved after PMOS recovery. Do not claim live serial.

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
