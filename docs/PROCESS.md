# Process log — 2026-11-05

## Scope and starting state

Target: a real iOS 16/XNU bring-up for Redmi Note 8 Pro, not an Android launcher. The initial workspace contained sparse ios-sweet and Xiaomi vendor-kernel checkouts. The phone was initially disconnected; it later became reachable over SSH running postmarketOS Linux 6.16.4. Device identifiers, raw logs, DTBs and backups stay in ignored `private/`.

## Sources reviewed

- https://hackiosx.dev/ and its compatibility page: general project/porting context.
- https://github.com/dipa-studio/ios-sweet : small shim, Mach-O/ADT loader, kernel patches and software-display/HID service research. Read its AGENTS.md and NOTES.md before reviewing it; no upstream files modified.
- https://github.com/oosexgang/hakios-xnu-kernel : common build/device framework, docs and open-kernel test path. Sparse checkout, not a full kernel build.
- https://github.com/oosexgang/hakios-mt6768 : related MediaTek loader/driver research. Different storage, touchscreen, GPU and boot layout; not a drop-in port. Its documentation says some working graphics userland is not yet in the common repository.
- https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/begonia-r-oss : initial board-register evidence. The live postmarketOS tree supersedes inaccurate vendor placeholders. Exact source revisions are in sources.lock.json.

## Offline implementation and validation

Added a small independently written ARM64 architectural probe, a workspace-local Zig 0.13 linker, Python build tools, QEMU tests, Android read-only collector and DTB inspection tool. No Apple or vendor firmware was downloaded or added.

`tools/test_probe.py` passed: Cortex-A55 QEMU entry at EL1, MIDR/MMFR0/CNTFRQ report, and PROBE_COMPLETE. The first test assertion wrongly assumed a particular CPU revision; corrected it to check implementer/part while allowing revision variation. QEMU-generated DTB decompilation passed. Python tools compiled successfully.

These tests demonstrate the diagnostic code only. No XNU kernel or iOS userland has been built or booted.

## Live phone evidence

SSH key login succeeded, and the owner supplied sudo authentication. Read-only capture established:

- Six A55 cores (MIDR 0x411fd050), two A76 (0x413fd0b0), 13 MHz timer, about 5.3 GiB Linux-visible RAM.
- **Correction:** the earlier source-based eMMC assumption was wrong. The actual phone has SKhynix UFS, MediaTek UFSHCI at 0x11270000, interrupt 136; MSDC0/eMMC driver planning does not apply.
- Active Novatek NT36672A SPI touchscreen; 1080x2340 Linux DRM framebuffer, stride 4320, 32 bpp. Bootloader stride/address still require direct observation.
- Live RAM starts 0x40000000, size 0x180000000. Numerous secure/vendor/modem reservations prohibit treating it as one free allocation. The live framebuffer reservation starts 0x7d9b0000, size 0x2250000.
- Single `boot` partition, 64 MiB. Android header v2, page size 2048, kernel 0x40080000, ramdisk 0x47c80000, DTB 0x41f78000. Existing kernel is gzip compressed; decompressed Image is 30,476,800 bytes.
- Persistent ramoops configured at 0x4d010000, size 0xe0000; console 0x40000, ftrace 0x1000, pmsg 0x10000, record 0x1000, ECC off. Calculated console zone is 0x4d09f000.
- Read-only EL0 diagnostic ran on all eight cores, then removed its temporary executable. Linux returned MMFR0 0x111ff000000 with TGran16=0, but the EL0 CPUID interface masks/sanitizes features, so this is **not** proof of physical 16K capability. Direct EL1 evidence is still required.
- Matching kernel build directory is a dangling path and a compiler/make are unavailable on the phone; no custom kernel module attempted.

Saved full boot, DTBO and vbmeta backups locally with sizes and SHA256 in private/device/backups/manifest.json. Boot SHA256: 115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7. This is not a complete calibration/data backup.

## Temporary chainloader diagnostic

Added an optional probe variant that logs into the verified ramoops console zone and chains to the original postmarketOS kernel. A relocated copy stub moves the appended uncompressed original kernel back to 0x40080000, preserves x0/DTB, then branches. No storage/peripheral initialization is performed.

`tools/test_chain.py` passed in QEMU: PROBE_COMPLETE, CHAINING_ORIGINAL_KERNEL, CHAIN_PASS. This exercises the overlapping copy and relocated handoff with a synthetic kernel entry.

`tools/pack_chain.py` checks the exact backed-up boot checksum/layout, memory placements and partition size. Generated temporary boot image: 62,146,560 bytes, SHA256 ea3043ae55f53a29050e104de28372292bbf71f6a47de8485b4f47d572d193df. Original ramdisk and DTB verified byte-for-byte. The temporary image contains the user's existing postmarketOS files and remains in ignored output; do not redistribute.

Requested a reboot into the bootloader using systemctl with reboot argument `bootloader`. Request returned success. USB fastboot detection and actual diagnostic boot outcome are recorded in the next update; **do not infer successful hardware boot from image generation**.

### Reboot outcome

The bootloader reboot argument was not honored: the phone rebooted normally into postmarketOS, confirmed by reset uptime and the NCM USB gadget returning. No fastboot device appeared during a 20-second poll. No diagnostic boot or flash was attempted. Asked the owner to enter fastboot physically (Volume Down during reboot) for the temporary test. SSH remains functional.

### Hardware attempt 1: uncompressed diagnostic boot

Owner entered fastboot physically. Read-only fastboot queries confirmed product `begonia`, unlocked `yes`, maximum download 0x8000000 (128 MiB). Sent only `fastboot boot` with the explicit detected device target. The image download succeeded (60,690 KiB); `Booting` ended with `usb_read failed with status e00002ed`. No flash operation was run.

Owner reported postmarketOS starting/running; SSH subsequently returned. `/sys/fs/pstore` was empty because systemd-pstore archived `console-ramoops-0` into `/var/lib/systemd/pstore/`. The archive contained no BEGONIA marker. Thus there is no evidence the architectural probe executed. The installed OS remained usable.

The first package replaced the gzip-compressed original with an uncompressed Image. This is a plausible LK compatibility problem, not an established cause. Updated the packer to preserve gzip encoding when present in the source backup, retaining the same original kernel, ramdisk and DTB. Revised diagnostic: 43,563,008 bytes, SHA256 9670ca3bee434f28469e1daa517d10de7237ed78080f50639383a2b44a14ecc1. Decompression and original ramdisk equality verified locally. Requested physical fastboot entry for attempt 2.

### Hardware attempt 2: gzip diagnostic boot

The gzip package also downloaded successfully (42,542 KiB), then fastboot reported `usb_read failed with status e00002ed` during Booting. PostmarketOS returned, confirmed by SSH/uptime. No BEGONIA marker was found in current/archived pstore; systemd-pstore showed no entries for that boot. Do not treat this as a successful probe execution or as proof that the diagnostic code is wrong. The evidence does not yet distinguish bootloader behaviour from image rejection.

Owner agreed to a third, non-flashing baseline using the untouched boot-partition backup. Its outcome is recorded next. No partition has been written during these three temporary-boot attempts.

### Baseline and hardware milestone: stage 0 executed

The untouched 64 MiB boot backup produced the same fastboot temporary-boot USB error. This established that the error was not specific to either diagnostic package. With the owner's explicit go-ahead, used a backed-up `fastboot flash boot` test of the gzip diagnostic (only boot written), followed by reboot.

**Verified on the real phone:** the probe ran, persisted its report and chainloaded the original Linux kernel. Owner reported postmarketOS; SSH returned. `/var/lib/systemd/pstore/console-ramoops-0` contained:

```text
BEGONIA stage0: architectural probe only; no XNU or iOS
EL=0000000000000002
DTB=000000004bc80000
MIDR=00000000411fd050
MPIDR=0000000081000000
MMFR0=0000000000101122
TGran16=0000000000000001
CNTFRQ=0000000000c65d40
SCTLR=0000000030c50830
PROBE_COMPLETE
CHAINING_ORIGINAL_KERNEL
```

This confirms boot-core 16K granule support, EL2 entry with MMU/cache off, live DTB pointer, logging and the original-kernel handoff. It does not prove SMP, XNU or iOS boot. XNU's handoff will need deliberate EL2-to-EL1 setup; the sweet loader's EL1 entry assumption is wrong here.

**Restoration completed:** uploaded the original 64 MiB backup into a private temporary directory, checked its SHA256, restored only the boot partition, then read/checksummed the entire partition. SHA256 exactly matched 115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7. Temporary restoration files were removed. Current installed boot is original postmarketOS; no diagnostic remains installed.

## XNU build preparation

Fetched the shared XNU/build/platform sources, applied the pinned HakiOS patch to Apple's xnu-8796.121.2 and prepared build stage 1. Selected `make defconfig`: open configuration, one core, no board-specific drivers. Kernel configuration was regenerated from it to remove the common tree's default phone drivers. Building support libraries next; no XNU binary has yet been produced or tested.

## XNU built and emulated

Support-library setup completed. Initial `make defconfig` still enabled 25 default drivers even with no phone boot arguments. Corrected this by adding `devices/begonia/configs/begonia_defconfig`, based on the minimal defconfig with all 25 optional drivers explicitly excluded. The configuration checker confirmed **0 of 25** drivers built. An initial restricted build could not read host sysctl details; the permitted retry succeeded.

Built `out/kernel.begonia-common`, SHA256 d20989e905ad1d2e4f2aa9f22cde9433bbea38aaca95949fe3afb6fd283e2fce. Upstream's make-kernel checker accepted the linked kernel and classified the final failures as its documented kext symbol/export checks. Built a placeholder-identity UEFI loader and the source-only raw-syscall `hello` program. QEMU reached “Hello from user space on Darwin!” before the expected PID-1-exit panic (hello exits 0). No iOS system image is involved.

Added an opt-in begonia_log=1 persistent-console patch to the generated XNU source; patch is `patches/0001-begonia-ramoops-console.patch` (XNU derivative, APSL 2.0). It maps the measured reserved console area after early VM setup and logs there without storage access. It is inactive when the boot argument is absent.

Implemented `boot/xnu_loader.c`, an independent minimal loader consuming ios-sweet's SWEETXNU payload format, and a single-core ADT generator. Upper-bank XNU RAM is 0x100000000..0x1b7f00000, with 1 MiB panic RAM immediately above; all fixed live firmware carve-outs are elsewhere. No touch, storage, GPU or power-controller driver is enabled. XNU's device-tree pointer is a **kernel virtual address**, while the entry argument is a physical boot_args pointer. Reviewed this against both upstream loaders before testing.

Raw-loader QEMU tests found and corrected two real ADT omissions: `/options`, then the empty CHRP NVRAM proxy/metadata. The synthetic NVRAM bank follows HakiOS's open loader; MIT attribution retained in licenses/HakiOS-MIT.txt. This is an in-memory stand-in, not access to the phone's NVRAM partition.

The corrected raw-loader test passed: loader -> XNU -> GIC/timer initialization -> root RAM disk -> user-space hello, with a readable persistent RAM log. Test artifacts: out/qemu-chain-xnu/. The hello program's process-1 exit intentionally produces a final panic; it is not a complete userland.

Prepared a phone test package with the enlarged payload and a DTB staging address moved to 0x47000000 to prevent decompression from overwriting it. Existing postmarketOS ramdisk/DTB contents remain included unchanged; source DTB bytes are not redistributed. The XNU wrapper has a reserved-RAM prior-attempt marker to fall back to Linux on a subsequent warm reset. Retention through every reset type is not guaranteed; fastboot restoration remains the recovery method.

### Raw-loader EL2 test and first XNU phone attempt

QEMU's direct-boot test passed both at EL1 and with `virtualization=on` starting at EL2. The latter exercised the actual begonia-style EL2-to-EL1 transition, including HCR, timer access, SRE and exception return. Both reached “Hello from user space on Darwin!” and captured the intentional PID-1-exit panic in reserved console RAM. The final ADT includes a synthetic valid CHRP proxy bank and `/options`.

Phone package: out/begonia-chain-xnu/xnu-open-boot-v2.img, 49,215,488 bytes, SHA256 fa033fe04b9c914b538033679ac1049966c3cfe3252cd9370a8cc4bbe2ba69ec. This is an **open-kernel test, no iOS userland and no framebuffer setup**. It contains the original postmarketOS kernel for warm-reset fallback. The actual display can stay on the boot logo even if XNU runs.

Uploaded into a private temporary directory, verified the upload, wrote only `boot`, read back/checksummed the exact image-length prefix and confirmed the same SHA256. Removed temporary upload, requested normal reboot. Test outcome and original-boot restoration remain pending in the next entry; do not assume restoration until a matching full-partition checksum is recorded.

### VERIFIED: XNU and user space on the real Redmi Note 8 Pro

The owner reported that postmarketOS returned. SSH recovered. Captured 50,460 bytes of console history from pstore/systemd-pstore into ignored private/device/xnu-first-phone.log. It contains:

```text
BEGONIA XNU: validated payload; copying kernel to upper DDR
BEGONIA XNU: entering open XNU at EL1; no iOS userland
BEGONIA XNU: persistent console mapped
Hello from user space on Darwin!
[    8.191593]: gic: v3, distributor 0xfffffff0cc900000, cpu 0's redistributor 0xfffffff0cc940000, virtual timer on PPI 27
[    8.191645]: Darwin Kernel Version 22.5.0
panic(cpu 0 caller 0xfffffff007a30664): initproc failed to start ... (signal 0, exit status 0)
BEGONIA XNU: prior attempt seen; chaining postmarketOS
```

The last panic is expected from the short hello program exiting as PID 1. This demonstrates: real hardware XNU execution, MMU/VM, interrupts/timer, single-core scheduling, RAM disk root, process execution and raw BSD system calls. It does **not** demonstrate iOS userland, screen, touch, storage, graphics, networking, battery/charging or phone functions. No claim of a working iOS ROM is warranted.

The second-stage loader's warm-reset fallback was also observed in the log. The precise reset cause (watchdog versus owner's button action) is not established from this capture. Keep the fastboot recovery path and original boot backup.

Requested restoration of the original boot partition immediately after retrieving the log; final verification is recorded next.

### Restoration after successful XNU hardware boot

Restored the full original boot partition over SSH and read/checksummed all 67,108,864 bytes. SHA256 again exactly equals 115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7. Temporary upload removed. The installed boot is original postmarketOS; XNU is not left installed. The source, test image, kernel, logs and complete local boot backup remain available for subsequent work.

## iOS userland preparation

Selected iPhone X iOS 16.5 build 20F66 to match HakiOS's kernel/userland tooling. Queried firmware metadata, verified the download hostname is updates.cdn-apple.com, and started a local download into ignored private/firmware/. Expected archive size 6,165,698,198 bytes; expected SHA1 6ee6e5b447902dbb36a3991ca20d032d6b4cc759. No Apple userland has yet been extracted or run. The local 7-Zip was too old for APFS, so obtaining the official current macOS console tool locally, without a system-wide installation.

The firmware download completed and its SHA1 matched 6ee6e5b447902dbb36a3991ca20d032d6b4cc759 exactly. Official 7-Zip 26.05 was installed only into .tools/7zip/ and exposed through a workspace-local wrapper; firmware extraction is next. These are local userland preparation artifacts, not a distributable ROM.

## Storage and display inheritance

The read-only register inventory returned to postmarketOS normally. UFSHCI 2.0 is enabled/idle: CAP 0x3587071f, HCS 0x10f, HCE 1, request list 0x4c687400, no outstanding doorbells. Linux confirms the main device is LUN2 with 4096-byte logical/physical blocks; loop0 is backed directly by /dev/sdc46 and loop0p2 is the postmarketOS root.

A second probe adapted ios-sweet's MIT bare-metal UFS transport to MediaTek base 0x11270000, LUN2 and 13 MHz timing. It issued only TEST UNIT READY, READ CAPACITY(10), and READ(10) for the first two blocks. All completed successfully; last LBA 0xee57ff, block size 4096. The first 8192 bytes' FNV-1a was dadb021aa776a925, exactly matching a reference read through Linux. GPT signature matched. The inherited request-list base/run state was restored. No SCSI write command exists in this probe.

The boot logo uses OVL0 **layer 1**, not layer 0. Measured layer-1 CON 0x21ff (ARGB8888 per upstream Linux format definitions), size 0x09240438 (1080x2340), pitch register 0x21ff10e0 (stride 4320), address 0x7e384000. These lie within the live framebuffer reservation. Restored the original full boot partition after the two probes; full SHA256 matched the original again.

## Private iOS filesystem and emulator validation

Firmware extraction finished: about 2.8 GiB selected system files, 2.5 GiB dyld cache, plus cryptex files. The common userland build completed, producing a 4 MiB support volume and a 2,476,720,128-byte APFS system volume. Real firmware-derived identity was generated locally; not committed/distributed.

The first emulator launch failed because a restricted local socket could not bind. The permitted retry then exposed QEMU's ordinary loader's large-file read limit. Changed the test runner to the upstream-style sparse file-backed guest RAM, with the system image at the resident offset. The emulator ran real iOS launchd, backboardd and SpringBoard; a captured frame contains the home screen, **with significant rendering corruption**. This confirms execution and drawing, not polished graphics or phone operation. Artifacts remain in ignored out/ios16-qemu/.

Verified the phone's USB SSH interface at [phone USB address] against the already-known SSH host key using HostKeyAlias, then staged the APFS volume as /home/user/ios-begonia/system.apfs, a regular read-only file. Upload checksum matched a1b4857610f8c4772604380ac9010ea27f34bd40e76b51ac1b1d91064c75887d. No formatting or repartitioning. Read-only FIEMAP capture returned 24 extents. Converted through the verified loop/partition offsets to LUN2 LBAs, then compared every extent's first block with the normal file read: all matched.

## Native read-only root path (prepared; hardware test pending)

Adapted ios-sweet's APSL block root driver to MediaTek/LUN2 and the staged file's verified extents. It exposes only that file as a BSD root device, refuses writes, validates the APFS NXSB header and extent bounds, and permits only TUR/READ(10) commands. It does not mount or modify postmarketOS's filesystem. Root hook is gated by begonia_root=1; rd=md0 is retained for iOS's boot semantics. The small support APFS volume is md1, with a reserved dummy md0 page.

Added display handoff using the measured layer-1 buffer only if its size, format, stride and reserved-memory bounds still match. Removed a redundant manually-created display node: the common framebuffer service publishes that node itself. Changed the one-shot marker to a per-payload fingerprint so a previous test cannot accidentally skip a new image.

Kernel builds with the new native driver/root hook passed the upstream build checker. A new EL2 raw-loader regression again reached the open hello program in QEMU. Native UFS operation and iOS on the phone are not yet verified at this point.

Prepared an opt-in bounded test callback: begonia_test=240 maps the known watchdog, services it only when already enabled, and requests a warm reset after four minutes (PSCI reset with the MediaTek software-reset fallback). It does not change clocks, voltages or watchdog timeout. A reset should lead the one-shot wrapper back to postmarketOS; fastboot restoration remains the fallback if reset retention or early boot fails.

## Visible QEMU and verbose test (owner request)

The owner asked to see QEMU and requested verbose boot. Added --window and --verbose to tools/run_ios_qemu.py and launched a Cocoa window for a 30-minute session. Serial log explicitly confirms `-v`, Darwin and iOS launchd. The emulator window is a local validation environment, not evidence that the phone has reached SpringBoard.

## First native iOS phone test (in progress)

Verified the installed boot was still the exact original backup, then uploaded/wrote/readback-verified the native verbose test image: 49,700,864 bytes, SHA256 776a9deeffe12a6648687b38f743a7da8a1e8a3b6cad92c3654906c844320452. Root system file and all 24 extent starts were independently checked before this test. The image enables verbose display, read-only file root, native UFS and the four-minute bounded callback; no system/data partition is formatted or replaced.

The owner reported **scrolling boot text on the phone**, confirming the inherited display handoff. Neither USB nor Wi-Fi SSH had returned after the nominal bounded interval, so automatic return was **not verified**. Requested a manual normal reboot; the per-payload marker should select postmarketOS on a warm reset, with original-image fastboot restoration as fallback. Native root/iOS boot result remains unverified until the log is captured. Boot restoration after this attempt is still pending.

## 2026-11-05 — Mu-Silicium begonia bring-up

User requested Mu-Silicium with volume-button selection. Pinned the upstream
revision f971025b5f4478425aa9ef30658102a2506e7f59 and downloaded its MediaTek
reference platforms and Mu dependencies. Upstream supports lancelot/MT6769T and
stone/MT6789, but has no begonia/MT6785 target.

The phone is reachable in postmarketOS after the cache-synchronized one-shot
request test. The archived console still contains the earlier Hello/panic plus
“no explicit request”; it does not prove that the latest full iOS attempt ran.
The currently installed opt-in boot loader defaults to postmarketOS. No Mu image
has been installed.

Created a canonical overlay under firmware/mu, installed reproducibly with
`python3 tools/prepare_mu.py`. The new platform uses only a conservative low RAM
arena, measured GIC addresses, and the measured OVL0 layer-1 framebuffer. It does
not inherit the Redmi 9's eMMC, regulator, clock, or PMIC drivers. Storage and
OS handoff are not implemented in this initial firmware configuration.

The captured live DT describes KPD at 0x10010000, row 0/column 0 as Volume Up
and column 1 as Volume Down; both are active-low bits at offset 4. The upstream
Linux mt6779 keypad driver confirms the register interpretation. This differs
from lancelot's PMIC/GPIO buttons. Captured live input events include KEY_VOLUMEDOWN
(114) presses/releases; the capture started too late to verify Volume Up events.

Implemented a Mu KeypadDeviceLib: Up/Down navigation, both held together for Enter,
30 ms debounce, 500 ms initial repeat delay and 150 ms repeat interval. The actual
C driver compiles for ARM64 PE/COFF against Mu headers. Host simulation tests pass
for navigation, chord Enter, bounce rejection, repeat and release suppression.
Full firmware build and phone validation remain pending; this is not a finished
Mu port or evidence of a physical iOS userland boot.

### Mu firmware build and memory-carried menu

The stock dependency updater has no Mu-Basetools package for MacOs/ARM64 and
requires Mono for NuGet. Built BaseTools from its pinned C sources instead;
`tools/build_mu.py` invokes the source build frontend with project-local tools.
The full begonia firmware volume now builds successfully. Resolved duplicate
firmware-volume entries inherited from the reference DXE include. The first
build used Apple clang with incompatible LLVM bitcode, so the final build uses
one consistent local Zig compiler/archiver/linker toolchain with normalized
AArch64 target spelling.

Added a Mu DeviceBootManagerLib with two choices, navigation/Enter through the
begonia keypad, and a 10-second postmarketOS default. Added a direct handoff:
FD at 0x40080000, appended existing chain at 0x40280000, DXE heap at
0x180000000, copy stub at 0x44000000. After ExitBootServices and cache/MMU shutdown,
the stub moves the chain down over Mu and branches with the preserved LK DTB.
The iOS choice writes the exact payload-specific opt-in token immediately before
entry. This does not depend on a reboot retaining a RAM marker. The default
explicitly clears that marker.

`tools/test_mu_handoff.py` runs the actual handoff assembly under QEMU, checking
an overlapping 4 MiB copy, final payload bytes, preserved DTB, cleared argument
registers, and both the zero/positive request values. It passed. Keypad state
checks passed again. These tests do not prove full MT6785 UEFI initialization.

Packaged out/mu-begonia/volume-picker-v1.img (50,153,472 bytes), SHA256
484a75f7e43eb2affb06f98434088e20a71682d56c6ae11d532c5ad984bcc8f7.
The original ramdisk, DTB bytes, and command line are retained. The original
boot backup hash was checked again before the user put the phone in FASTBOOT.
The first physical test is the default return to postmarketOS; install transcript
is private/device/mu-picker-v1-install.txt. Physical results remain pending.

### First physical Mu results

User confirmed the actual Mu menu appears on begonia and Volume Up/Down move the
selection marker. Both choices hang with the screen unchanged. The second build
adds an updating numerical countdown, pre-handoff text and a cache-cleaned
reserved pmsg progress word (0x4d0e0200, 0x4d550000 | step).

Second build SHA256:
68c882ce12eabc08e9c4aa9b9ef160871f05a53aee964181dd0b1f113961b030.
User confirmed its countdown advances and the automatic postmarketOS choice
prints both “Starting postmarketOS” and “Boot image validated. Preparing handoff”.
Thus display, keypad navigation, timer/countdown and input image validation work
on physical hardware. The failure is later in the shared handoff; neither OS
choice is yet functional through Mu. Requested FASTBOOT to restore the verified
original boot and retrieve the progress marker. Added a read-only native helper
at tools/read_mu_progress.c for this diagnostic.

### User deferred Mu; resumed direct iOS work

User asked to set Mu aside and prioritize iOS. Restored the exact original boot
image through FASTBOOT (private/device/mu-original-restore.txt); phone returned
to postmarketOS and SSH. The read helper initially used DC IVAC, which is not
available at EL0 and terminated with illegal instruction. Corrected it to the
previously tested DC CIVAC path. The read progress value was 0xffffffff, so the
marker did not survive this recovery and cannot identify the failed Mu step.
No further Mu tests are planned unless requested.

Created an explicit `--force-xnu-test` build option, allowed only for the hardware
XNU diagnostic. It bypasses the opt-in request gate, then follows the same
validated XNU path. This is a temporary diagnostic that retries on every boot
until the original boot is restored; the user was told this before installation.
Default builds retain positive opt-in behavior. Updated stage0 and supervisor
messages to accurately identify forced retries. Direct native image v2 SHA256:
ae47b12c0df7612dadb2c83da6b8488b592678fb4c17e595c033886593421701.
The image contains the supervised native-UFS iOS payload, not the Hello-only
payload. Staged it as a regular phone file before any boot-partition write.

Direct native v2 was installed from postmarketOS only after checking the full
original boot SHA256, staged image SHA256 and device compatible string. The
written boot prefix was read back and matched. The normal reboot request
succeeded. Updated QEMU XNU/Hello + independent-supervisor test passed; this
validates loader mechanics only. At 2026-11-05 16:50:50 UTC, SSH was unavailable
and the physical screen result was still pending. Current boot is the forced
native diagnostic, not Mu or the original postmarketOS image.

## Native iOS userland observed; diagnostic log capture v3

The user reported scrolling native boot text, an Apple logo, repeated “denied
attempt to escape CPU monitor (missing required entitlement)” and “locale has
not changed”. This is evidence of native iOS userland progress, not proof that
SpringBoard has drawn. The identical CPU-monitor message also repeats in the
running QEMU userland, so it does not by itself identify the failure. The source
is osfmk/kern/task_policy.c; it clamps CPU-monitor policy and prints a warning,
not a kernel boot-stop.

Added patch 0005: optional `begonia_cpumon_log_limit=16` caps repeated warning
printing while leaving the entitlement decision and policy enforcement unchanged.
Rebuilt kernel successfully through the upstream checker (only documented kext
symbol checks remain); the linked binary contains the new suppression message.

Low-RAM progress readback was inconclusive. FASTBOOT download-buffer overlap is
a possible explanation for losing low reserved data during image recovery, not
yet a measured fact. Added a 1 MiB mirror at 0x1b7f00000, outside the XNU-managed
RAM window, copied by the independent supervisor at its deadline. It stores the
whole console ring and up to 160 KiB of early kernel output, checksums, and the
payload-specific request ID. Metadata magic is published last. The matching
captured request makes the diagnostic chain postmarketOS on the next warm boot.
A paired DTB reserves this range with both memreserve and a no-map reserved-memory
node so Linux will preserve it and allow diagnostic reads. All original PMOS
kernel/ramdisk contents are retained.

QEMU tests passed XNU/Hello, the independent supervisor, mirror metadata, exact
request, and both data checksums. Host parser also validates/decodes the capture.
Native v3 SHA256:
89612f8f0f680f01ab8f15283a74d5c2a61f43600c9080ce4a4db506043d0a26.
User returned to FASTBOOT and the diagnostic was installed for a four-minute
capture followed by intended warm postmarketOS recovery. This behavior is not
yet phone-verified. Cold resets may clear the guard and retry the diagnostic.

### Capture v3 recovered; native iOS services verified

The four-minute test returned to postmarketOS automatically. The matching captured
request guard and reserved log DTB therefore work on the phone. Retrieved
426,112 bytes from the reserved upper-RAM mirror; both checksums passed. Files:
private/device/native-v3-mirror.bin, native-v3-decoded.log and its JSON metadata.
The console ring is full (262,132 characters), early data 163,840 bytes; counter
frequency 13 MHz. The captured environment identifies Darwin 22.5.0 / iPhone10,3
and 2,943 MiB usable memory. The raw-syscall/init checks report 36/36 passed.

Native logs confirm SpringBoard PID 8, backboardd PID 13, framebuffer enumeration,
window-server initialization, the system-shell connection to SpringBoard, and
backboardd's “startup complete” after about 19.5 seconds. No graphical lock/home
screen was confirmed. “Software renderer! Simulating crash!” also occurs in QEMU
before its framebuffer swaps, so that message alone is not a diagnosis. The
Wi-Fi locale messages are routine service logging, not a panic.

User supplied [local Downloads]/IMG_9411.heic and IMG_9410.MOV (113 seconds).
Converted the photo and sampled video frames into ignored private/device/.
Visual inspection confirms boot console text and an Apple-shaped block pattern,
with routine service/locale and memory-stat messages at the end. No panic is
visible in the supplied final frames. The supervisor deadline is in the captured
log and explains the bounded restart; the interface/display handoff remains
unresolved.

The native capture lacked MiniIOMobileFramebuffer kernel diagnostics that appear
in QEMU. Added patches 0006/0007 to mirror serial and console_write paths into the
board's active RAM logger, preserving normal output. The helper is gated by the
existing begonia console mapping. Kept the helper away from earlier patch context
so prepare_kernel.py remains idempotent. Kernel rebuilt through the checker.
Increased captured early log to the complete 256 KiB preserved head (still fits
inside the same reserved 1 MiB). Updated reader/parser bounds. The preliminary
v4 serial-only image was staged but never installed; v5 includes the buffered
console path as well. v5 SHA256:
322bf7293b818ead4738aa5298dcd791497b46e946eaa96011781c4797801649.

### Display trace v5 and text-console ownership defect

v5 returned to postmarketOS and its 524,416-byte mirror passed both checksums.
private/device/native-v5-decoded.log now includes the kernel display trace:
1125×2436 virtual display on the actual 1080×2340 panel, stride 4320; 60 Hz timing
at 13 MHz; the read-only staged APFS root on UFS LUN2; and backboardd submitting
BGRA surfaces through actual framebuffer swaps. First-frame processing is about
90 ms in the driver. The full log confirms frame submission even while the
photos show boot text continuing to cover the display.

Source inspection identified a concrete ownership defect: initialize_screen's
kPEGraphicsMode case changes gc_graphics_boot/gc_desire_text but does not clear
gc_enabled for an already acquired text console. MiniIOMobileFramebuffer only
called GraphicsMode on its first picture. Added patch 0008 to follow that with
kPEReleaseScreen, which disables console drawing and releases progress display
ownership. The fix preserves verbose startup until the first frame. Applied to
both generated XNU source and xnu/files canonical driver so rebuilds retain it;
prepare_kernel.py passed an idempotence check. Kernel checker accepted the build.

User supplied IMG_9413.heic and IMG_9414.heic; converted and inspected privately.
They show the same console overlay, not a visible panic, with about 1.4 GiB free
and roughly 2.5 GiB available. Do not interpret the last Wi-Fi log as a reset cause.

v6 (console-release fix) SHA256:
304486549e37360bf921556a975af61d281837d65db9d68a7feb70715fbbae45.
Verified v5's exact installed prefix before writing v6 over SSH, verified v6's
staged checksum, wrote/synced boot and read it back. Normal reboot requested at
2026-11-05 17:37:28 UTC (18:37 local). Four-minute mirror/recovery is retained;
physical graphical result is pending. Mu remains deferred.

### Console release verified; focused SpringBoard diagnostic

User confirmed v6 replaces scrolling text with a clean Apple logo. Thus the
first-frame console ownership fix works on hardware; no lock/home screen was
yet visible. v6 returned to postmarketOS. Its 524,416-byte mirror passed checksums
and shows the same valid first BGRA swaps, with no visible panic in the searched
capture. The remaining logo is a separate UI startup/input issue.

Created tools/make_ui_support.py and a source-only begonia diagnostic rc.jobs.
The private output copies the existing small support volume, enables
OS_ACTIVITY_DT_MODE for SpringBoard, and sends one normalized synthetic swipe
through the upstream rwtouch helper after 45 seconds. Physical touchscreen
support is still unported; this does not implement it. The staged system.apfs
and its UFS extent map remain unchanged. make_native_payload.py now accepts an
explicit --support-root override, retaining its original default.

v7 SHA256:
dcce96635e31b6e554ef516485c4dc157a19dd2f2318b54485f41ba7413a804a.
Verified the exact v6 boot prefix, staged hash, and written v7 bytes over SSH.
Reboot requested at 2026-11-05 17:49:27 UTC (18:49 local); same four-minute
log capture and warm recovery remain enabled. SpringBoard details and gesture
result are pending. Do not report a graphical iOS boot as complete yet.


### Two-hour visible-console test (v10) — 2026-11-05T18:47:32.383234+00:00

User requested a 7200-second limit and no further work after boot starts. Raised payload/loader validation ceiling from 1800 to 7200 seconds; watcher uses 64-bit counter arithmetic. Built current kernel with optional `begonia_keep_console=1`, preserving console overlay after framebuffer handoff. Payload sets `begonia_watch=7200`, disables the separate short kernel deadline, and uses the existing UI diagnostic support volume. Installed image SHA256 `a05d5fac44cfe67b4da6a957a467639341f4955810325cf5737815743c9da321`; existing v9 prefix verified before write, new image readback verified afterward. Original boot backup retained. At the two-hour deadline the supervisor captures logs and requests recovery reboot; earlier faults may still reboot sooner. GUI outcome awaits user observation.


### Writable data investigation

User reported signature verification messages during v10 and requested writable app/data storage. Existing v9 captured logs confirm MIInstallerErrorDomain Code=13 / 0xe8008001 for Home.app and Preferences.app; subsequent log messages explicitly fall back to allocating data containers. This is not evidence of EROFS causing signature verification failure. `/private/var` is already writable ramfs, but is volatile. The Begonia block driver explicitly refuses writes, and the upstream APFS implementation is read-only, so removing the file mode restriction cannot provide writable APFS. Upstream init already has an HFS+ persistent-data-volume path and falls back to ramfs. Upstream var_store alternatively persists archives but assumes a dedicated darwin partition at fixed offsets, which is not applicable to our existing postmarketOS layout. Never copy those offsets onto Begonia userdata. A future implementation should use a separately allocated, fully backed data image, validated extent boundaries, isolated write permissions, and writable HFS+ support; it must preserve the existing system image and avoid changes while the current native boot owns storage. No phone storage changes made during this investigation; v10 remains undisturbed.


### Native home screen confirmed by user photographs

User supplied IMG_9416.jpg and IMG_9417.jpg showing the physical Redmi Note 8 Pro displaying the iOS SpringBoard home screen, with stock app icons and dock; second photograph includes the retained verbose console overlay and SpringBoard messages. This confirms native graphical startup on v10, not emulator-only success. Physical touch and GPU acceleration remain unverified/unimplemented. User requested touch first and possible GPU work. Preserve `ios16-native-verbose-twohour-v10.img` as the known GUI-booting reference.


### Touch, GPU, and persistent-data follow-up

Restored the SHA-verified original postmarketOS boot backup through explicit-target FASTBOOT after the user reported a forced reset retried iOS. Linux 6.16.4 is reachable again. Actual touch device is Linux spi1.0, physically SPI5 at 0x11019000, compatible novatek,nt36672a-ts, driver novatek-nvt-ts-spi. Copied the existing device firmware privately (118784 bytes; SHA256 0b57dd62863f0f20ea285263a667af3fdd86f48473e23ac638efeba5c8f6ad03). Firmware is not redistributed. Independent six-byte contact decoder passes host tests for multitouch, normalized coordinates, motion, release, and invalid/recovery reports. First optional XNU SPI5 polling driver compiles successfully; hardware functionality is not tested. Firmware upload and hardware validation remain pending.

The user supplied ios-apollo and authorized publication to git.tuxy.dev/tuxy/ios-begonia. Apollo's Novatek firmware protocol is relevant; its Qualcomm GENI bus, pin mux and GPU are not Begonia hardware. Its UFS writable-HFS+ design is a reference, not a flashable Begonia build. Linux shows about 41 GB available in the existing ext4 root, enough for a fully allocated 16 GB data image. No GPT or filesystem resize has been performed. Real writable storage requires an extent-scoped block device, write-command/cache handling, and verification before mounting HFS+ persistently.


### Native touch and 16 GiB data test preparation

Published reviewed source and documentation to the user-provided repository (`33a7447`); private/generated/upstream trees are ignored. The first native touch test v11 used a 90-second recovery deadline. Capture checksums validated (592183 loader-log bytes). SPI transfers completed and eight firmware sections were sent, but controller reset status stayed zero; physical input was not established. Mainline Linux reference `novatek-nvt-ts-core.c` shows GPIO2 reset is driven low before firmware loading (DT active-high), so the next driver explicitly releases reset and checks section readbacks. Empty all-0xff contact slots must release existing fingers; added a host regression test for this case.

Created `/home/user/ios-begonia/data.hfs` as a new fully zero-written 17179869184-byte regular file, formatted journaled HFS+ with hfsprogs. No repartition, shrink, or existing data overwrite. The immutable flag pins its allocation. FIEMAP returned 130 extents; every first and last extent block matched a direct read from LUN2. Host checks confirm physical disjointness from the system image. Native optional minor-1 data device adds WRITE(10) with FUA, read/modify/write for partial blocks, and SYNCHRONIZE CACHE. The low-level command path rejects writes outside the explicitly mapped data extents; duplicate/overlapping extents are rejected. Extent boundary, gap, overlap, and overflow host tests pass. Data path and touch driver compile; HFS mount is linked.

Installed and readback-verified v12 (`9a178d9be21aee96ac4f53b8a9248e3192946bf5925b208c052343d415289bb3`, 49766400 bytes), a three-minute test with GPIO reset correction, firmware readback diagnostics and the optional data volume. Support jobs report the filesystem and append a persistence-test marker. Waiting for the retained native log before claiming writable HFS+ works. User requests a long run to finish migrations once persistent mounting is verified.

Linux Panfrost ioctl query confirms product 0x7211, revision 0, shader mask 0xf, job slots 0x7, address spaces 0xff; full hardware parameters remain in the private capture. Native GPU acceleration remains unported.


### Writable iOS data verified; next long migration run

The retained v12 capture validated (647718 loader-log bytes). Native init mounted `/private/var` from `/dev/begonia-data` as persistent HFS+, reported success, and var_store appended 29 bytes to `/private/var/begonia-persistence-test`. After automatic reboot, Linux hfsfuse mounted the new image read-only and read back `written by run of 1793907542`. Thus native writes and persistence across reboot are confirmed. The Linux kernel lacks hfsplus, so hfsfuse was installed for read-only verification. The journal was dirty after the bounded hardware reset; inspection did not replay or alter it. Next Darwin mount handles journal recovery.

Touch v12 got as far as section-0 SRAM readback but differed in at least one byte (the first byte matched). The diagnostic stopped before the hardware-CRC boot step. The next build uses 1 MHz instead of 4.33 MHz, word-aligned 28-byte upload chunks, and treats readback differences as diagnostics while the documented controller hardware CRC decides whether firmware can boot. Firmware bounds, private source, reset status and parameter checks remain. No physical input success claimed.

The user requested a long run so migrations can finish on persistent storage. Prepared the next image with a 7200-second supervisor limit, touch enabled, writable data enabled and visible verbose output. The job reads the existing marker before appending a new line. Source updates will be pushed before reboot, then work stops while the user observes startup.

Installed and verified long-run v14: 49766400 bytes, SHA256 `1f0507a72022743190540e9915e377061b7e97bcbe8fc029e8c233f0aedef2f3`. Existing v12 prefix checked before installation; new image readback matched. Original postmarketOS backup and GUI-booting v10 retained locally. Reboot into the two-hour persistent run is the final action; outcome awaits owner observation.


### Long-run photograph IMG_9422

Owner reports continued visible activity about an hour into v14. Photo shows launchd repeatedly restarting analyticsd, nanoregistryd and the daemon named `blonded` in the visible text. nanoregistryd reports it cannot set its working directory to `/var/mobile/Library/Caches`: `Permission denied`, followed by initialization failure and a throttled respawn. App-registration messages also show signature validation errors for ShortcutsViewService and GameCenterUIService. This is evidence of service retry loops, not proof of continuing migration progress. The photo does not show a kernel panic or establish the main SpringBoard blocker. Inspect data-volume directory ownership/modes and relevant access checks after recovery; do not infer that writable storage alone fixed app initialization. Phone left running for the configured capture deadline.


### Video clarifies concurrent app-registration progress

Owner supplied IMG_9423.MOV (18.94 seconds). Comparing early and late frames shows lsd processing different app bundles, including ScreenshotServicesService and later SIMSetupUIService, while analyticsd/nanoregistryd/blonded retry messages continue in parallel. Correction to the photograph-only interpretation: background retry loops are present, but they do not establish that app registration is stuck; the video supports ongoing progression through the app list. Completion/success of registration is still unverified. Native run left undisturbed.


### Forced-reboot persistence inspection and handoff

Owner reported repeated app passes after a forced reboot. Restored the SHA-verified original postmarketOS boot through explicit-target FASTBOOT, then inspected the existing HFS+ data file using `hfsfuse --force -o ro` and unmounted it. Three boot-marker entries survive (87 bytes). Inventory found 787 regular files and 5093 directories, including app-container metadata and mobile migration/SpringBoard preference files. Writes demonstrably survive beyond the single original test marker. However mobile/Library/Caches and db/lsd are empty in this read-only view. Caches is UID/GID 501, mode0700; mobile and mobile/Library are 501:501 mode0711, matching the template. Native Permission denied remains unexplained; do not assume blanket chmod is appropriate. The journal is dirty and was not replayed by the inspector, so absence in this view is not conclusive evidence about pending journal records. No formatting, repair, or data modifications were performed. Created root STATE.md as the current handoff. Phone left in postmarketOS for subsequent diagnosis.


### Root cause analysis and v15 build

Live phone inspection revealed container directories such as `/containers/Shared/SystemGroup/*/Library` and `Caches` had mode `0000` (`d---------`) and owner `4294967294:4294967294`.
Root cause: in `upstream/hakios-xnu-kernel/userland/src/init.c` (`mount_data_volume()`), `args` was zero-initialized without setting `uid`, `gid`, and `mask` to `VNOVAL` (`-1`). In XNU (`bsd/hfs/hfs_vfsops.c` line 1361), `args->hfs_mask != VNOVAL` causes `hfsmp->hfs_dir_mask` to be clamped to `args->hfs_mask & ALLPERMS` (which is `0`). Consequently, newly created container directories without explicit BSD mode fell back to mode `0000`, causing all subsequent file writes and chdir operations by mobile processes to fail with `Permission denied` (`EACCES`).

Fixed `init.c` to pass `args.uid = -1`, `args.gid = -1`, `args.mask = -1` (`VNOVAL`), defaulting directory and file masks to `0777`/`0666` and owner to `UNKNOWNUID` (99). Added cleanup for stale `SystemGroup` directories, permissions on `/var/mobile/Library/Caches`, `/var/containers`, and `/var/db/lsd`, and disabled noisy `com.apple.nanoregistryd` and `com.apple.nanoregistrylaunchd`.
Recompiled `init`, generated UI support image, built native payload `out/native-persistent-long-v15.payload`, and packaged `out/begonia-chain-xnu-forced/ios16-native-persistent-long-v15.img` (49,768,448 bytes, SHA256 `0d6e2a61b9aacb9e19c9eb68e90107038fa0493561440a063e0af242627b347e`).
Uploaded, flashed to `/dev/disk/by-partlabel/boot`, and verified via readback SHA256 match. Ready for boot execution.


### AMFI disable, touchscreen driver fixes, and v16 build — 2026-11-05T23:28:00+01:00

During v15, owner reported verbose console loop displaying kernel memory reports (`programs 25, wired 177, free 1035 MB`) and wifid timers without visible SpringBoard GUI. Traced to `--keep-console` (`begonia_keep_console=1`) retaining the text console overlay and blocking `PE_initialize_console(NULL, kPEReleaseScreen)` from yielding to graphics mode. Also observed `0xe8008001` signature verification spam caused by `OS_ACTIVITY_DT_MODE` enabled on `com.apple.lsd`.

Actions taken:
1. Disabled AMFI and code signing enforcement in `tools/make_native_payload.py`: passed `cs_enforcement_disable=1 amfi_get_out_of_my_way=1 amfi_allow_any_signature=1 amfi=0xff` in kernel boot arguments.
2. Removed `OS_ACTIVITY_DT_MODE` on `com.apple.lsd` in `tools/make_ui_support.py` to silence registration console noise.
3. Removed `--keep-console` to allow the text console to cleanly release the display framebuffer upon SpringBoard first frame render.
4. Resolved three Novatek NT36672A touchscreen bring-up bugs in `kernel/bsd/dev/begonia_touch.c`:
   - Reset polarity: Novatek TP_RESX is active-low (`ts_reset_active` is `output-high` in device tree). Driver previously held GPIO2 permanently low (`0x108` = DOUT_CLR). Updated to pulse low for 10ms and drive high (`0x104` = DOUT_SET) to release hardware reset.
   - CRC length register write: on NT36672A (`hw_crc == 1`), `ILM_LENGTH_ADDR` (`0x3F118`) and `DLM_LENGTH_ADDR` (`0x3F130`) are 2 bytes, not 3 bytes. Writing 3 bytes to `0x3F130` overflowed into `0x3F132` (`DMA_CRC_EN_ADDR`), corrupting the hardware CRC enable register. Corrected length writes to 2 bytes.
   - Spurious dummy-byte check: removed `if (rx[1] != 0xff)` from `read_address()`. In SPI transactions, `rx[1]` is a turnaround dummy cycle byte and not a status indicator.
5. Recompiled XNU kernel (`hakios-kernel`), updated `out/kernel.begonia-native-ufs`, rebuilt support volume, and packaged `out/begonia-chain-xnu-forced/ios16-native-persistent-long-v16.img` (49,768,448 bytes, SHA256 `05628179cae0b9e40dfdf1edc9993ff3fac78baff720e52c7ffcf3ca6367d334`).
6. Flashed to `boot` partition over fastboot (`fastboot -s YOUR_DEVICE flash boot out/begonia-chain-xnu-forced/ios16-native-persistent-long-v16.img`). Ready for reboot.


### Retaining keep_console and basic framebuffer (v17) — 2026-11-05T23:34:00+01:00

In v16, omitting `--keep-console` caused `PE_initialize_console(NULL, kPEReleaseScreen)` to unbind the simplefb console upon the first graphics frame, leaving the panel blank with only the backlight illuminated. Confirmed that milestone v10 retained the live framebuffer by keeping `begonia_keep_console=1`. User requested basic framebuffer and keeping console output.

Built v17 payload and image:
- Restored `begonia_keep_console=1` to keep the display scanout live and continuous.
- Pure basic software framebuffer rasterizer maintained (no GPU driver).
- Kept AMFI disabled (`cs_enforcement_disable=1 amfi_get_out_of_my_way=1 amfi_allow_any_signature=1 amfi=0xff`).
- Kept `lsd` console logging silenced.
- Kept Novatek NT36672A touchscreen bring-up fixes in kernel.
- Packaged `out/begonia-chain-xnu-forced/ios16-native-persistent-long-v17.img` (49,768,448 bytes, SHA256 `3ef21eef66b70dff3a86d5d40f7596fbfcc7698f928c3c3651e4fa2a02f78f69`). Ready for fastboot flash.


### Review of v15–v17 after owner reported only Wi-Fi/memory output

Reviewed commits 8ca06e3, e6dbfbb, 2d39fa9 and the additional untracked upstream init changes. These builds disabled lsd activity logging, so absence of app names in the console does not establish loss of registration progress. Prior claims that retaining the text console necessarily blocked the GUI conflict with the photographed v10/v14-style console-overlay home-screen evidence; treat that explanation as unproven. No retained capture was available from v17 after manual FASTBOOT entry (reader rejected the header).

Confirmed original postmarketOS backup and host system.apfs hashes unchanged; GUI milestone image v10 is retained. Recovered PMOS with the original kernel/ramdisk plus only the upper-log memory reservation. Read-only HFS inspection found six boot-marker lines, populated mobile caches, a 3555328-byte LaunchServices store, a 3915776-byte SystemDataOnly store and a 20480-byte LaunchServices SettingsStore SQL file. This supports improvement after the mount-default fix and disproves a wholesale wipe; it does not establish healthy GUI startup.

The upstream init modifications also deleted all contents of /private/var/containers/Shared/SystemGroup on every boot and ignored chmod/chown failures. Preserved that source privately under out/review-v17/init.c, then restored the pinned init baseline with only uid/gid/mask VNOVAL defaults. Captured the fix as userland/patches/0001-hfs-native-permission-defaults.patch and added tools/prepare_userland.py so support images build from a recorded source patch. Restored lsd activity logging and original daemon selection. Blanket code-signing boot arguments are now an explicit --disable-cs diagnostic option rather than an unconditional default. Touch changes are preserved but opt-in; the review test leaves touch disabled to isolate GUI startup.

Built and installed v18, a 300-second capture test: 49768448 bytes, SHA256 adadf032275642ce7f694f5f895c63e0634e4eab151f95c51b55dd79f4f3f3cd, request ID 0x5033318f. Verified recovery-image hash before writing and the new prefix after writing. It preserves the data image, keeps visible console, samples ps at approximately 60/180/240 seconds after the demo job, and restores PMOS automatically on capture. Owner reports abundant console activity again. Awaiting capture before attributing the GUI stall.


v18 result: owner confirmed SpringBoard home screen appeared before the five-minute deadline. Capture was retrieved after automatic PMOS recovery; both checksums valid, request ID matched 0x5033318f, 1310719 loader bytes retained. The log contains ongoing SpringBoard activity and IOSurface creation. This re-establishes native GUI with persistent data under the corrected support init; touch was disabled. Prepared v19 as an isolated touch test: identical bytes after the payload command-line header (kernel, device tree and support volume), with only touch enabled, a new epoch and the requested two-hour deadline. Driver functionality remains unverified until physical taps/swipes and logs agree.


Physical-test refinement: v19 was prepared but not installed. Built v20 with the same kernel, ADT and corrected init, plus --physical-touch support that removes scripted rwtouch swipes. This prevents synthetic gestures being mistaken for real driver success. v20 keeps verbose output and persistent data, enables the preserved v16 touch code, and has a 7200-second deadline. Installed/readback verified: 49766400 bytes, SHA256 3beab8f40efade043bdc448b79ae7f72515b9b35921dcbfd02e631907f1f0df1. Existing v18 prefix verified first. Reboot will be the final action; touch result awaits owner observation.


### 2026-11-06: physical touch transport validated under Linux

After owner reported v20 ignored physical taps, restored SHA-verified original postmarketOS boot via FASTBOOT. Live Linux GPIO/debugfs confirmed GPIO2 reset output LOW in the working configuration; the v16/v20 HIGH setting did not match the working board driver. Read-only hardware inspection must skip SPI registers while runtime-suspended to avoid bus faults.

Built a private-firmware spidev control harness. On the actual phone, Linux SPI_IOC_MESSAGE loaded all eight firmware sections with exact 16-byte readbacks and controller status A0. The same control passed with 28-byte writes and two-byte CRC length fields. Direct FIFO comparisons initially used Python byte-copy MMIO, which is invalid for FIFO registers; discarded those results and added exact-width C MMIO accessors. With proper 32-bit accesses, repeated reads of read-clear STATUS0 still lost completion events during long uploads. Programming packet length BEFORE filling TX FIFO, waiting through wire time, and polling non-destructive STATUS1 idle yielded exact readbacks for all eight sections, firmware boot A0 and valid 1080x2340 parameters. The 65-byte report path then captured real finger positions, multiple fingers and releases for 60 seconds. No controller flash was programmed.

Temporary spidev binding exposed an existing Linux IRQ-domain reattachment mismatch (MT_SYSIRQ GPIO1), so the original touch driver could not rebind in that session. Normal PMOS reboot restored novatek-nvt-ts-spi attachment, verified afterward. The raw test tools state this limitation; --leave-unbound is for controlled test series ending in reboot.

Ported the validated FIFO ordering/completion method into begonia_touch.c, restored measured reset-low configuration, added a quiet CS interval and require all eight section readback samples to match before enabling firmware execution. Decoder host tests pass; XNU build links. v21 is a 90-second native capture test with real touch enabled and no scripted gestures, not a long UI run: 49766400 bytes, SHA256 81b3eeb50686f6124856917cbd0e27dc571d367aff6a5f7e8adc5c18d12f0912. Verified original boot hash before install and v21 prefix after. Native result pending capture.


v21 native result: capture validates both checksums and exact payload request ID. Native XNU reports zero of eight section readback differences, firmware boot A0, subsequent reset A1, version1a/e5, bounds1080x2340, and polling active. Owner's physical strokes appear as real type1/type2 finger0 events with changing normalized coordinates. This verifies the native controller/input-queue path. Prepared and installed v22 for end-to-end GUI confirmation, 600 seconds (not two hours), touch enabled and synthetic swipes removed. Same validated kernel/support assets; SHA256 0105f617fa90110732a554be712dcf67f04edfc78787052e3402df0c4a7800af, 49766400 bytes. v21 installed prefix verified before write, v22 readback verified. Reboot will be the final action so owner can test Settings, scrolling and releases.


### v22 UI failure and overnight stop — 2026-11-06

Owner reports apps still do not open. Allowed the ten-minute test to capture and recover automatically; captured both checksums and request ID match v22. Kernel logged physical down/move/release and backboardd read real samples. Retained mode changes are0xff then0xfe; no0x1 is visible, but circular-log loss prevents claiming it never occurred. inject.c explicitly converts DOWN in mode0xfe to a power-button wake and suppresses that finger sequence, so HID/wake/display state is the next specific investigation. Also check whether applications launch but frames stop updating. Added optional --touch-debug to enable existing per-event touch_lat instrumentation for a future diagnostic; no further image was installed.

Owner asked to finish for the night. Saved capture locally and restored original PMOS boot via SSH, checking current v22 prefix first and full original SHA256 after write. No reboot after restoration; phone remains in PMOS, next normal boot is PMOS, no overnight experiment pending. iOS data/system images untouched. Updated STATE.md and touch status: physical controller/queue path works, GUI taps still fail, port not complete.


### 2026-11-06 resumed: opt-in HID routing experiment

Owner requested continuing after overnight stop. PMOS SSH reachable on original restored boot. Reviewed inject.c: in mode0xfe, DOWN is converted into a lock-button wake and its entire finger sequence suppressed. Added recorded userland patch0002 with opt-in begonia_touch_direct=1 to bypass that conversion and per-event route/mode/waking diagnostics when touch_lat=1 is enabled. Existing default behavior remains unchanged. Extended prepare_userland.py to compile/copy libhakios.dylib as well as init, and make_ui_support.py to stage the newly compiled library. Builds and Python syntax checks pass.

Built/installed v23, 600-second test, physical touch enabled, no scripted gestures, direct routing and per-event traces enabled. Size49768448, SHA256 5b2911d90b72fbd756c8a5e8cab1f4694fc3b62f418e716d26910c33a7b6ce24. Original boot hash verified before installation; new prefix verified after. UI outcome pending. Owner also requested laptop-readable serial: native MTU3 USB console is not available yet; no hardware UART pinout/adapter has been established. Saved RAM capture remains the current log channel.

### 2026-11-06: isolate text-console overlay

Owner reports v23 still cannot open apps. Prepared v24 with the same support
image, physical touch, direct routing and tracing, removing keep-console so the
existing framebuffer release patch stops console drawing at first graphics.
A visible kernel console is not itself a UIKit touch-receiving window, but
rendering interference remains worth isolating. SSH timed out; installation
awaits FASTBOOT. No boot partition changes during preparation.

Owner confirmed FASTBOOT. Verified v24 SHA256 and original recovery backup, selected the exact private device target, and flashed boot successfully. Install output retained privately in native-v24-install.txt. Reboot follows; GUI touch result remains pending.

### v24 result and pause

Owner confirmed real Settings interaction after disabling the console overlay,
then very low frame rate and navigation stalls. Clock later advanced. After
automatic PMOS recovery, retrieved native-v24-mirror.bin and decoded log/JSON;
checksums and exact payload request ID match. Producer/consumer touch traces
persist, with one post-to-send interval around11.37 seconds. Preferences is
running in retained evidence. No retained panic(cpu string. This establishes
partial GUI touch success with severe latency, not a completed stable driver.
Saved state and stopped at owner's request. No new flash or reboot.

### Offline latency reduction, v25 prepared

Deduplicated116 complete input timing records from v24. Median queue delay is
8.521sec, median read-to-sent0.733ms and maximum1.468sec (includes logging).
Added analyzer and recorded userland batch/MOVE-coalescing patch. Event boundaries
are preserved; actual selection block compiled and passed ordering checks.
Built init, library, quiet support image and ten-minute v25 with no text overlay
or per-event tracing. No kernel or touch firmware changes. Image SHA256:
6b95d055ea57e08400f67dbea8fa710ab0a0b187f280af939c4aafe06229387a.
Phone unreachable on Wi-Fi/USB SSH and absent from FASTBOOT; no installation or
reboot performed. Performance improvement remains unmeasured.

SSH retry succeeded at confirmed IP. Installed v25 via verified v24-prefix precondition and v25 checksum; partition readback matched. Reboot requested for ten-minute test. No performance result yet.

### v25 regression and CPU/GPU observation build

Owner reports no touch in v25. Default batch optimization disabled behind new
opt-in boot argument; restored non-quiet support. Exact v24 image preserved.
Connection unavailable; no new test installed. Added MT6785-only read-only clock
observation driver and native hook; kernel compile/link passes the standard build
judge. Preserved original kernel path, built separate 90sec v26 observation
candidate. Native execution awaits phone access. No clock/voltage/power writes,
Mali MMIO access or Metal acceleration. Documented supervisor-core exclusion and
incompatible Qualcomm/MT6781 power drivers. CPU/GPU work remains incomplete.

### v25 capture retrieved; exact v24 restored

Capture verified against payload request and checksums. Native touch firmware
readbacks/startup pass, but22500 polls include13684 invalid decoder reports and
58 events; later30000/21184/58 implies7500 consecutive invalid reports. The
failure cannot be attributed solely to batching. Need invalid-packet reason and
prefix logging. Read-only Linux baseline collected: A55 2GHz/A76 2.05GHz,
GPU270MHz/CCI1.4GHz under Linux. Exact v24 boot restored with precondition v25
checksum and verified readback. PMOS left running, no reboot.

### v27 diagnostic running

Specific decoder rejection reasons and rate-limited raw prefixes added; tests
pass. Built kernel, installed with prior-v24 checksum guard and readback verify,
booted120sec automatic capture. Includes read-only CPU/CCI/MFG PLL/SPM probe.
Owner authorizes CPU clock correction; waiting native measurements before writes.
No DVFS/GPU power changes yet. prepare_kernel repeatability fixed by moving
perf build entry outside prior patch EOF context and using existing touch hook.

### v27 evidence; v28 two-core test

v27 capture validated:7500 polls/0invalid/387events, so persistent bad packets
fromv25 not reproduced. CPU PLL paths~1.175GHz A55,1.085GHz A76,612MHz CCI;
no rate/rail writes. Added explicit two-core payload topology0/0x200 preserving
supervisor0x100 and default single-core behavior. Generated payload parsed and
assertions pass. Installed/readback verifiedv28 and booted120sec capture. Pending
scheduler evidence before claiming working SMP or performance gain.

### v28 secondary did not join; v29 entry diagnostic

Firmware accepted CPU2 but1of2 processors running. Added opt-in lowRAM br-x0
trampoline carrying high reset target in PSCI context. ELF symbol/alignment and
branch opcode verified at0x400801c0; loader QEMU entry regression passes.
Separate loader/kernel outputs; installed/readback verified120sec v29. Pending
capture, no SMP success claimed. PMIC readback groundwork targets actual MT6359,
not Viva MT6366. No CPU clock or regulator writes made.

### v30 voltage readback / v31 conservative clock correction test

v29 low-entry still1of2, leavingSMPdisabled. v30 validates MT6359 identity5920,
VPROC2 800mV and SRAM900mV. Captured stock table supports1275MHz CPU and875MHz
CCI at those existing rails. Implemented guarded opt-in PCW changes, CCI first,
CLKSQ parking,20us settle, unchanged voltage. Policy tests and kernelbuild pass.
Installed/readback verified v31 and booted90sec capture; result pending.

### v31 clock readback validated; v32 longer UI boot

v31 capture checksum/request validated. A55 readback011884ec and CCI0221a762
confirm1275MHz/875MHz; rails800/900mV unchanged. iOS file-root boot continues,
7500touch polls/0invalid. This is a short stability observation, not a sustained
performance or temperature result. Built and installed/readback verifiedv32,
1800sec UI run with singlecore, direct physical touch, original nonbatched reads,
console release and tested stock-clock correction. SHA256
2808eb48475edd05143cc63a9f028b007aeb393b89568281060f93ff29dcb2d5.

### G76 compiler and native probe groundwork during v32

Installed host compiler dependencies, built pinned Mesa26.2.6 with XNU patches.
G76 product72110000 compiles176-byte Bifrost fragment shader; architecture/ISA
checks explicitly run despite NDEBUG. Added optional MT6785-specific GPU
power/reset/ID probe with rail/domain/protection preconditions and bounded waits.
Compiled kernel; no hardware GPU execution, jobs or acceleration verified.
Left v32 UI run untouched. Extended Linux GPU capability-query option prepared.

Prepared v33 GPU probe (not installed):49770496bytes,SHA256
9db1d49948c5f9d63832e25bcfd21804091bf2d0146f45dd3137fc2b7fff43c2,
90sec,oneCPU,noCPUboost. Final kernelbuild and repeated compiler check pass.
New payload builder verifies probe symbols and limits GPU probes to<=180sec.
No interruption to activev32. Native GPU probe remains untested.

### G76 frontend guard and isolated GPU job source

Found direct Valhall ISA selection guarded only by panfrost. Added recorded
G76 Mesa/NIR guard and real product query; host frontend builds. G76 mock
resource/fence4/4 passes; own MSL vertex/fragment/compute AIR translation and
compilation3/3 passes with direct ISA requested+strict (G76 correctly bypasses).
Mock capabilities are synthetic; no phone GPU execution implied.

Added optional native WRITE_VALUE test confined to one kernel page; managedRAM
bounds and mirror exclusion checks pass. It remains disabled for first power/
reset probe. GPU reset required before freeing page; retain on reset failure.
Caught/fixed missing cache-maintenance declaration during build. Active v32 UI
run left untouched throughout. Native GPU work still awaits first execution.

### v32 capture and v36 persistent rejection summary

v32 completed and capturevalidated. Retained last summary180000polls/
167062invalid/4events; no retained reject-prefix lines (early detail overwritten).
Added persistent reason counters and latest6reportbytes to periodic summaries.
Builtv36, installed over verifiedv32, readback verified, rebooted180sec. GPU power/
reset probe enabled; GPU job selftest disabled; noCPUboost. Image49772544bytes,
SHA256f2f90253033e47131d54e96a0cdaf8c78a4009e1461908acc9a1652bce4600df.
Owner offers camera; permitted single AVFoundation snapshot works (noaudio).
Observed upside-down Applelogo/boot text during shorttest. Stills stay private.

### v36 evidence / v37 transaction atomicity

v36 capture validates: GPU575/850mV enabled but inherited bus protection partial;
probe safely refused power-up. No touch poll summary retained. XNU delay()
may block/yield when CPU idle latency unknown, including FIFO microdelays while
CS remains held. Wrapped bounded SPI transaction in saved/restored IRQ disable;
outer8ms poll still sleeps. Added maxread timing and persistent reject counts.
v37 installed/readback verified300sec; camera at~152sec shows actual SpringBoard.
Capture pending. v38 GPU source normalizes owned protection bits when domains
off, bounded ACK checks; native power/reset remains unverified.

### Full measured G76 MC4 mock profile

Linux45params/43supported captured privately. Added generic capability header
and opt-in begonia mockprofile; no G57 thread/texture/slot limits borrowed.
Rebuilt transport/relinked tests: Mali-G76 MC4 resources/fence4/4, own AIR shader
stages3/3 compile. Hardware GPU execution still untested.

### Native G76 identity job and reliable touch record

v38 validated native G76 power/reset; v39 validated WRITE_VALUE:irq2,slot0,MMU0,
wordbec07601. No shader execution/Metal acceleration claimed. v40 fixed touch
record validates epoch/checksum:14674polls,0bad/transporterrors,stage2active,
maxread1177.153us and no finger events. Atomic SPI appears stable through earlier
failure point; further physical interaction required. Preparing separately gated
VA48 four-level mapped job; host MMU format checks pass, native pending.

### v40 fixed record verified; v41 mapped job running

v40 log+record validated.14674polls/0bad/0SPIerrors/0every reject,stage2active,
maxread1177.153us,0events without finger. Record104bytes outside text ring,
sequence/checksum/epoch guards. v41 installed over verifiedv40/readbackverified,
90sec mapped job (VA48/PA40 fourlevels). Image49774592bytes SHA256
cd4275451510ab00fdc2746592e2c421217efbb3953da4a0fb6b931d854c2d2f.
Capture pending. No native shader/Metal acceleration claimed.

### v41 native translated job verified (2026-11-06)

Exact payload request and reserved log checksums validated. Both identity and
VA48 four-level mapped WRITE_VALUE completed: IRQ2,slot0,MMU0; mapped fault
address0/0 and canarybec07601. No shader ALU execution or Metal acceleration
claimed. v41 touch record:9091polls,0bad,0SPIerrors,maxread740us,0finger events.
v42 longer UI test combines atomic SPI with previously tested guarded A55/CCI
clock correction; GPU probes disabled. Physical interaction remains to verify.

### Own-source native shader test prepared

Host generated a single G76 compute chain from our Metal sample:64uint outputs,
expected i*3+7. Mock output remains deadbeef, proving no fake execution claim.
Capture has5BOs/8private16KiB pages. Fixture validator rejects fragment/noncompute,
chained jobs, altered output, unaligned/overlapping maps;7tests pass. Native test
maps only these private pages through four-level tables, reads back all64words,
resets before free and retains allocations on reset timeout. Uses G76 TLS hash
bit17 and IDVS group15 per pinned Linux Panfrost features, not G57 VAR_ALGORITHM.
Reference: https://github.com/torvalds/linux/blob/v6.16/drivers/gpu/drm/panfrost/panfrost_gpu.c

v43 prepared but not installed; v44 includes G76 shader configuration. v44image
49776640bytes,SHA25643b724f9c3c96e1a3be437ee3cd7f836b03c88b5c5aadbd6d709a4f33e920374,
out/begonia-chain-xnu-forced/ios16-gpu-shader-v44.img. Kernel
out/kernel.begonia-gpu-shader-v44;payload out/native-gpu-shader-v44.payload.
Native shader execution remains pending. v42 camera confirms SpringBoard.
Existing Theos16.5SDK found; explicit-SDK build override added and iOS Mesa
cross-build started. No system Xcode selection changed.

### iOS rendering libraries and Metal bundle build verified

Explicit installed Theos16.5SDK builds Mesa978targets/825archive members and
VivaMTLDriver.bundle. Fixed missing SDK declaration for MTLCommandQueueDescriptor,
which16.5Metal.tbd exports. arm64/platformIOS/min16.0; ad-hoc signature verifies.
No bundle installed and no accelerator published to SpringBoard. Signed16.5
test_vgpu and vmprobe tools also built for eventual service/Metal discovery tests.
Host frontend regression passes4/4resources/fences and3/3ownshader compilation.

### Service groundwork while v42 runs

Added reusable owned-table mapper (begonia_gpu_vm.h): disjoint VA48 regions,
PA40 checks, duplicate-map refusal, bounded allocation and failure paths tested.
It is not integrated into a user client yet. Own AIR->NIR compute translation
executes correctly on llvmpipe,64/64words; this is software validation only.
GPU clock remains26MHz. Inherited MFGPLL0314c4ec is~270MHz, but stock lowest
OPP42 specifies612.5mV while current readback is575mV. Do not select that PLL
parent just because it is already running; no GPU voltage writes implemented.

### v44 native shader execution verified; Vulkan path selected

v42 exceeded1800sec recovery; owner still saw SpringBoard, manually entered
FASTBOOT. Restored log-preserving PMOS boot. Neither mirror nor touch record was
valid after recovery, so no v42 diagnostic result is claimed. Cause unresolved.
v44 short90sec test installed/readback verified and auto-recovered. Exact payload
request/checksums valid: identity and mapped jobs pass, own compute matches64/64,
IRQ2,slot0,MMU0,faultaddr0/0,exception1,first7,lastc4. This proves G76 shader ALUs
execute native work at26MHz. It does not establish SpringBoard acceleration.
Touchrecord exactepoch validates9060polls,0bad,2081events,0SPI/rejections,
maxread735.307us; no physical app interaction claim from this short boot.

Owner revoked camera access; no further camera use. Requested S9+ reference
danstronics/ios-s9plus-exynos and metal2vulkan. Inspected pinned S9 revision
2d092398040f98685b0c5a07776df5e9f199b9dc. Its installed path is GLES; Metal/Vulkan
is experimental with unfinished cache/lifetime integration. Pinned metal2vulkan
720ef1ce9ae2a2068999a8675717418b9416283e; published WIP patch SHA256
dba02debb819bdcc31e739b34ebbc59458681bbb3a70461c01b81d4170499612 applies.
Installed official Rust1.98.1+iOS target locally under .tools/rust. Host release
translator built. Own compute AIR translates to SPIR-V and passes spirv-val for
synthetic Vulkan1.2 minimum profile:0.2014sec,peak process-group RSS9338880bytes,
20sec/500MiB limits enforced. Not a measured G76 Vulkan capability profile.
Separate iOS16.5 Metal-to-Vulkan runtime built/signed with -Dwarnings; fixed host
linker SDK isolation rather than suppressing warnings. No runtime installed.
Native PanVK ICD/kernel transport/IOSurface cache ordering remain to integrate.

### v45 user-to-GPU submission verified

Implemented MT6785/G76 backend plus pinned S9 HakiosMali IOKit transport adapted
to existing MiniIOSurface (no Exynos power code). Native v45 capture validates
exact request/checksums: own shader64/64, real service publication, user client
opened, GET_PARAM7211, CREATE_BO16KiB, IOKit map, SUBMIT WRITE_VALUE, WAIT,
CPU readbec07601, unmap/free/close. Kerneljob1 IRQ2/MMU0,0unfinished onclose.
Image49782784bytes SHA2568aeebde7feb1f8ea14048fd9e1cc4e27888d1f1c4a0190051cfebc99cbe0d1b8.
New loader QEMU handoff check and phone180sec auto-recovery pass. USB network
briefly disconnected during retrieval; Wi-Fi reconnected and validated capture.
Backend is a shared-VA/synchronous prototype with GPU reset between jobs, not
per-process isolation or asynchronous scheduling. No UI acceleration claim.
Later source adds BO/chain preflight, submission failure propagation and executable
buffer16MiB guard; those later changes require the next kernel build/test.

Native Vulkan DRM/POSIX/sync/surface transport stages from pinned S9 reference,
modern48byte submit packed to kernel40byte ABI with reserved/context validation.
Strict iOS16.5 build passes; ASan/UBSan host map/sync/surface-FD tests pass, including
160exports/1000earlycloses and invalid/recycleddescriptor rejection. PanVK source
compiles after Darwin runtime image metadata and explicit unsupported Linux perf
session compatibility. Cross-link initially picked macOSlibdrm; now selecting
separately built native iOS transport. Native Vulkan execution remains pending.

### v46 native Vulkan diagnostic prepared and started

Native iOS PanVK links/signs; undefined-symbol audit has0strong unresolved dynamic
lookups,7436optional weak imports. Darwin weak-import null behavior tested on host.
Metal2Vulkan runtime rebuilt after Qwen cleanup. Own AIR C bridge diagnostic limits
wall time20sec/RSS500MiB and checks64 i*3+7 outputs through actual native Vulkan.
Built updated kernel with BO/chain validation; only known kext-export checks fail
after successful kernel link. Staged separate runtime/ICD; no compositor enabled.
Support APFS3153blocks,128spare; removed unused diagnostic executables. New payload
builder explicitly checks kernel/support/ADT/bootargs against48MiB dummy RAM disk.
v46 Android boot61118464bytes, SHA256
6e8c07016d5c2e6b2aaa133cb983acf99a353e7746224c6e56d4f31148d82c52.
Installed via verifiedv45prefix, stagedv46hash, fullwrite/fsync/readback; data intact.
180sec independent capture/recovery requested. Actual Vulkan result pending logs.
Separate pinned Objective-C Metal frontend builds/signs with default process gate;
not installed. Three deprecated SDK API warnings, no compile/link errors.

Correction: v46 initially packaged with factory PMOS DTB, so /dev/mem refused
reading the unreserved capture range. Repacked SAME payload with log-preserving
DTB. Since request ID is unchanged and mirror already existed, recovery can skip
re-execution. Retrieved capture still validates exact request/checksums; it shows
M2V device opening thenSIGALRM20sec, no native Vulkan success. New packer guard
rejects capture payloads without the reserved mirror DTB node.

v47 uses fresh epoch/request, known log-preserving base, prior verified CPU boost,
service test FIRST with stage logging, ICD dlopen markers,60sec device-init limit
and separate20sec translation limit,500MiB RSS ceiling. Installed/readback verified:
61118464bytes SHA256631fb1ab2bea9ca0a4e1804705af41c5226e8f43eaace025b99c06802d08d7be.
No iOS data images changed. Own IOSurface cache/ownership test compiled separately;
not staged or hardware-tested yet.

v47 exact request/checksum capture validates. Updated strict service passes native
WRITE_VALUE again: job1slot1IRQ2/MMU0/result1, userbec07601,0unfinished. Native ICD
dlopen completes; bridge device creation remains unresolved. Full UI startup fills
early log buffer and circular console hides later outcome; do not infer success
from absent errors. Source inspection finds PanVK v7 advertises Vulkan1.0 while
metal2vulkan requires1.2. No API version override applied.

v48 isolates qualification from GUI jobs (only demo/notifyd/cfprefsd staged),
retaining existing stored data. Runs private service check, authored original-page
IOSurface/cache3cycle test, authored Vulkan1.0 capability/actualfill check, then
M2V. Each bounded; no compositor enabled. Image60956672bytes SHA256
6930ca1424555aac6841607381aa52f7f68f6b3f4cee5cfd4c5a7f00834bcbd2.
Test build -Wall/-Wextra/-Werror passes. Recovery DTB and payload bounds checked.
Owner revoked optional Claude remote-agent use (machine down); none launched.

v48 capture exact request/checksums validate; quiet isolated output fits capture.
Original IOSurface GPU writes/CPU visibility pass3/3cycles, private user job passes.
Raw Vulkan instance creation fails-3; M2V reports the same-3. Identified shared
Mesa build-id guard: Darwin LC_UUID16bytes rejected by ELF SHA1minimum20. Common
util header patch0005 accepts16onDarwin,20ELF; actualUUIDhosttestpassespaddingcheck.
Native ICD rebuilt/signed/audited. v49 same isolated tests, updated ICD and raw
Vulkan pNext debugcallback. Image60956672bytes SHA256
5209cf47a5e87e58252a67fe6756b43adcbb8670f0f7a8c2fc2922ce2d016e45;
install/readback/reboot requested. No UI compositor enabled.

### Nov7 v59 audit and v60 one-variable display test

Reparsed private v59 mirror checksums and exact payload identity. Hardware blit
4096/4096 and64x64IOSurface render clear valid; backboardd explicitly lacks Metal.
Original report's size corrected to61401088bytes; hash unchanged. MPS stubs can
return success and discard compute, so no assertion match in bounded capture is
not full correctness. Builder now requires --experimental-mps-stubs to reproduce
that behavior; strict default build/sign verified separately, no device install.
Removed unsupported PanVK v7 conformance claim and restored explicit opt-in;
experimental compatibility remains for qualified calls, not general Vulkan1.2.

Eleven unique retained v59 windows: best5.90FPS, median67.705ms driver time,
zero recorded GPU wait. Layer0 surfaces1125x2436, panel1080x2340. Driver time
includes scaling/presentation, cannot attribute solely to memcpy. v60 uses
retune_panel_payload.py to change only canvas boot args and epoch, keeping all
other payload and boot sections equal. Image61401088bytes SHA256
f45476e60ecfcaae90185b7735ab96df5e22e8d92f997630d582dab7c58a5b38.
Written after exact v59prefix verification, full v60 staged hash and readback.
Data intact;180sec log-preserving recovery. Await measured result.

v60 recovered and capture validates. Canvas and backboardd original surfaces
1080x2340, physical panel1080x2340; swaps1–3 exact geometry (no scaling). Capture
has no usable FPS samples; do not infer speed. Broad metal=2 also exposed missing
MTL selector to mediaserverd, causing uncaught exception. Known v59 preserved.

Parallel hardware GLES route: verified Mesa25.0.7 archive, applied pinned S9
OSMesa/ES/Darwin source patches, separate40byte DRM ABI and checked binary fences.
Cross-link excludes laptop pkg-config libraries. OSMesa/EAGL builds sign cleanly,
409 EAGL entries (356 forwarded,30 handwritten,23 unavailable). v61 isolated
hardware test rejects softpipe; clear/triangle exact pixels needed before GUI.
Image53510144bytes SHA256
7fd72d64f659d131a7321350f596dd57b6703e720386e1d1443307e5ece9bccd.
Installed/readback,120sec recovery. Prepared v62 GUI candidate with gl=1, native
canvas and per-frame timing; still uninstalled pending isolated v61.

v61 exact capture PASS: renderer Mali-G76 (Panfrost), RGBA64/128/191/255 clear
and authored triangle0/255/0/255, GLerror0. Jobs1–13 all success/MMU0,0unfinished.
GLES is genuine hardware rendering, not software fallback. Installed v62 GUI
variant only after this pass;53983232bytes SHA256
da459beac114d6c0141398c40d9ff5e08041543899cb6f28dc0243c9e03b93be.
Verified v61 current prefix and full v62 staged/readback hashes. Existing data
kept. gl=1, Metal disabled, matched canvas and per-frame trace;180sec recovery.

v62 recovered to PMOS and exact capture validates. CoreAnimation renderer
Mali-G76(Panfrost), OpenGL ES3.1. 432 captured GPU jobs succeed/MMU0; native
1080x2340 swaps. 30 per-swap draws median2.813ms; active window21frames/5016ms
(4.19FPS),2.852ms driver time/frame and0.677ms GPUwait/frame. This establishes
actual hardware GLES compositing and much lower display cost, not smooth or
complete iOS use. Metal remains off for v62. No data or clock changes in this
test; installed v62 and original recovery/known v59 backups retained.

### Nov 7 QuartzCore Metal library and submission costs

Scoped the strict iOS Metal frontend to backboardd with the native canvas
and normal wallpaper path. v63 selected Mali-G76 and completed 826 GPU
jobs, but backboardd repeatedly exited and no display swaps were captured.
v64 swapped in an assembled QuartzCore library and removed the support bin
directory to fit RAM. That also removed hello_dyn. Without its early dynamic
link step, launchd could not load libSystem.B.dylib. Recovered to PMOS without
altering the iOS data volume.

v66 restored the v63 support tree and used the HakiOS pipeline-cache technique.
The adapter provides an iOS 16 target descriptor and lets Metal generate
an empty cache for the Mali target. It then reopens the original descriptions.
v66 image 61419520 bytes, SHA256
`57efe73cb1e1ba743253801dfe6f90e4a0b5bc0464874047434b9524bed876e0`.
The exact capture verified that QuartzCore opened the library and that
996 GPU job sequences completed. It also identified backboardd's uncaught
`newFunctionWithName:constantValues:functionCache:error:` selector on NBLibrary.
There were still no compositor frame timings.

v67 retains the early helpers and removes unused diagnostics to fit the
assembled QuartzCore library. It again opened the pipeline library and
reproduced the shader specialization selector failure. v69 adds the missing method and routes
specialization through the existing Metal-to-Vulkan compiler. The frontend
does not consume Apple's device-specific compiled shader cache. Unsupported
operations still report errors. Both images use a 150-second recovery deadline.
The installed v69 image is 62101504 bytes, SHA256
`bda40e08b3c53693877480549ac741a59560ebce49fb3fde52b2b1093edb7b75`.

Prepared v68 with the v62 GLES support volume and the revised GPU submission
kernel. The mapper tracks dirty tables and flushes their 4 KiB of entries
before GPU use. Startup and the end of each job retain the reset, with failure
disabling the service. Host mapping and dirty tracking regression checks pass.
Hardware benchmarking is separate from the Metal compositor tests.

### Nov 7 recovery after the reported v80 display failure

The owner's clip showed verbose console output. The text was too small
to identify a specific crash. The local v80 payload contains
`begonia_keep_console=1`, which keeps the console drawing after graphics
start. That setting can obscure UI output. The clip alone cannot establish
whether SpringBoard failed to start.

Confirmed the attached FASTBOOT device was an unlocked begonia with a 64 MiB
boot partition. Flashed the verified log-preserving postmarketOS boot image
and confirmed USB SSH returned. The reserved log reader found no valid capture
from the broken boot. The iOS data volume was preserved.

Prepared a v78 replay by changing only the epoch in the verified original
payload and regenerating the Android boot image header digest. This retained
the exact kernel, loader, support volume, 180-second recovery, and display
settings. Installed and verified the boot partition readback over SSH.
Replay image `out/ios16-metal-v78-recovery.img`, 62107648 bytes, SHA256
`03b94d0039fa7d4c89ff4d7901a12539eee172839e800f4680d2b998886d8383`.
Expected capture request `0x2b27351f`.

The replay returned to postmarketOS and the capture checksums and request
matched. SpringBoard and backboardd ran and the framebuffer driver accepted
swaps, with a median of 4.73 FPS across 23 timing windows. The owner saw
only backlight. These logs establish software and GPU activity but cannot
establish panel output. The planned longer v78 replay was canceled.
