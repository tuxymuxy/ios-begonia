# Current handoff — verified GUI restored; physical touch test next

Latest successful native test: **v18**, persistent HFS+ data enabled, touch
explicitly disabled. Owner confirmed SpringBoard home screen. Automatic capture
and PMOS recovery succeeded; checksums valid, request 0x5033318f. Native log
contains `hakios: unlock: asked, answer 1` and SpringBoard surface creation.

Reviewed another agent's v15–v17 changes. Kept the HFS uid/gid/mask VNOVAL fix,
removed automatic deletion of all SystemGroup contents and restored directory
permission-error checking. The mount fix is now recorded in
userland/patches/0001-hfs-native-permission-defaults.patch, applied/built by
tools/prepare_userland.py. make_ui_support.py invokes that builder. Restored
lsd console logging; blanket AMFI/code-signing arguments are now opt-in.
Touch changes from v16 are preserved but were NOT exercised by successful v18.

Current phone: PMOS after v18 automatic recovery. **v20 physical-touch test is
installed and readback verified; reboot is the final action of this review.**
It enables the experimental touch driver, keeps persistent data and verbose
console, removes scripted rwtouch swipes (`--physical-touch`), and uses the
user-requested 7200-second limit. Physical touch success remains UNKNOWN until
owner taps/swipes/releases and retained driver events agree.

v20 image: out/begonia-chain-xnu-forced/ios16-physical-touch-v20.img
size 49766400; SHA256
3beab8f40efade043bdc448b79ae7f72515b9b35921dcbfd02e631907f1f0df1.
v18 GUI baseline: out/begonia-chain-xnu-forced/ios16-preserve-diagnostics-v18.img
size 49768448; SHA256
adadf032275642ce7f694f5f895c63e0634e4eab151f95c51b55dd79f4f3f3cd.
v19 was only prepared and never installed; v20 removes synthetic gestures for
an unambiguous physical test.

Data survived: six boot markers, populated mobile caches and LaunchServices
stores (3555328 and 3915776 bytes) plus SettingsStore SQL (20480 bytes) found
read-only after v17. Original host system.apfs and boot backup hashes unchanged.
No wholesale wipe occurred. Do not repeat the SystemGroup deletion workaround.

The repeating memory line reports MEGABYTES: `programs 13` is 13 MB of program
memory, not a process/failure count. Sparse Wi-Fi/memory console output is not
by itself evidence that SpringBoard stopped. Keep logs visible for diagnosis.

Next: let owner observe v20 and test opening Settings and scrolling, then lifting
all fingers. If physical input fails or GUI regresses, recover PMOS and inspect
BEGONIA TOUCH firmware status and real driver events. Preserve v18 as baseline.
Do not claim GPU acceleration; no native GPU port has been implemented.

---

# Historical handoff (before v15–v20 review)

# Begonia iOS port — current state

Updated 2026-11-05 (Europe/Budapest). This file is the handoff; chronological
history is in docs/PROCESS.md. No credentials or firmware are included here.

## Objective and user preferences

Native iOS 16 on Redmi Note 8 Pro (MT6785/begonia). Prioritize physical touch,
then consider GPU acceleration. Keep verbose output available. Mu boot menu is
deferred. Preserve postmarketOS and original boot backups. User authorized
publishing source/notes to https://git.tuxy.dev/tuxy/ios-begonia (main).

## Verified

- Native XNU and iOS 16.5 (20F66) userland reach SpringBoard home screen on the
  physical phone: owner photos IMG_9416 and IMG_9417, build v10. Physical touch
  and native GPU acceleration were not working in that milestone.
- System APFS image is a read-only file inside postmarketOS storage, not a new
  partition. Native UFS reads it through a verified private extent map.
- A new fully allocated, journaled HFS+ data image reserves 16 GiB inside the
  same filesystem. No GPT change or filesystem shrink was performed.
- v12 mounted that data image at /private/var, wrote a marker, rebooted, and
  Linux read-only hfsfuse inspection found the marker. This proves basic native
  writes and that one file survived reboot. It DOES NOT prove app-registration
  databases are committed, valid or reused successfully.
- Bounds/overlap policy and Novatek report decoder host tests pass. XNU builds
  link successfully; the build wrapper accepts the known kext symbol-set check
  failures only.

## Current phone state

Prepared build v17:
- 49,768,448 bytes, SHA256 `3ef21eef66b70dff3a86d5d40f7596fbfcc7698f928c3c3651e4fa2a02f78f69`.
- Re-enabled `begonia_keep_console=1` to ensure framebuffer remains continuously driven (preventing screen blanking from `kPEReleaseScreen`, matching verified milestone v10).
- Pure software framebuffer rendering retained (no hardware GPU drivers loaded).
- AMFI hard-disabled via boot arguments: `cs_enforcement_disable=1 amfi_get_out_of_my_way=1 amfi_allow_any_signature=1 amfi=0xff`.
- `OS_ACTIVITY_DT_MODE` removed from `lsd` to prevent signature verification console flood.
- Novatek NT36672A touchscreen fixes included (GPIO2 reset pin pulsed low then driven HIGH `0x104`, 2-byte CRC length, dummy-byte check removed).
- Data image on postmarketOS userdata preserved intact. Original postmarketOS backup retained locally.
Neither the system image nor the data image was erased during recovery.

## Latest iOS test and observations

- Installed image v15: out/begonia-chain-xnu-forced/ios16-native-persistent-long-v15.img
  49,768,448 bytes; SHA256
  0d6e2a61b9aacb9e19c9eb68e90107038fa0493561440a063e0af242627b347e.
- Verified on phone: flashed to /dev/disk/by-partlabel/boot and readback matched.
- Root cause of "Permission denied" and empty cache folders resolved:
  In upstream `init.c`, `mount_data_volume()` passed a zeroed `struct hfs_mount_args`.
  In XNU's `hfs_vfsops.c`, if `args->hfs_mask` is not `VNOVAL` (`-1`), XNU overrides
  `hfsmp->hfs_dir_mask = args->hfs_mask & ALLPERMS`. With `args->hfs_mask == 0`,
  directory and file permission masks were clamped to `0000` (`d---------`), and
  `hfs_uid`/`hfs_gid` clamped to 0. Every directory created by `containermanagerd`
  without explicit BSD attributes (such as `systemgroup.com.apple.lsd.iconscache/Library`
  and `Caches`) inherited mode `0000`, blocking any subsequent reads or writes with
  `Permission denied` (`EACCES`).
- Fix applied:
  1. Set `args.uid = (uint32_t)-1`, `args.gid = (uint32_t)-1`, `args.mask = (uint16_t)-1`
     (`VNOVAL`) in `init.c:mount_data_volume()`. XNU defaults `hfs_dir_mask` to `0777`,
     `hfs_file_mask` to `0666`, and `hfs_uid`/`hfs_gid` to `UNKNOWNUID`/`UNKNOWNGID` (99).
  2. `init.c:mount_writable()` clears stale/corrupted `SystemGroup` directories left by v14
     and sets `0777` permissions on `/var/mobile/Library/Caches`, `/var/containers`, and
     `/var/db/lsd`.
  3. `init.c` continues through `var.skel` even if directories exist, avoiding early loop aborts.
  4. Disabled noisy irrelevant Apple Watch daemons (`com.apple.nanoregistryd` and
     `com.apple.nanoregistrylaunchd`) in the UI support image to keep the console clean
     and save CPU cycles.
- Prior reference build v14:
  49,766,400 bytes; SHA256 1f0507a72022743190540e9915e377061b7e97bcbe8fc029e8c233f0aedef2f3.

## Touch and GPU

- NT36672A controller on physical SPI5 at 0x11019000; Linux names it spi1.0.
  GPIO1 IRQ; GPIO2 reset is active-high in captured Linux DT. Working Linux
  driver drives reset low before firmware upload.
- Private firmware copied from owner's Linux installation: 118784 bytes,
  SHA256 0b57dd62863f0f20ea285263a667af3fdd86f48473e23ac638efeba5c8f6ad03.
  It is NOT distributed in Git.
- v11 completed transfers but firmware reset status stayed zero.
- v12 found a section-0 readback mismatch and stopped before hardware-CRC boot.
- v14 uses 1 MHz SPI, word-aligned upload chunks, hardware-CRC boot validation,
  and release handling for empty reports. Its touch result is UNKNOWN until
  logs are recovered. No physical touch success has been confirmed.
- GPU is Mali-G76/Bifrost. Linux measured product 0x7211, four shader cores
  (mask 0xf), job slots 0x7. Native GPU driver is NOT implemented. Viva's G57
  Valhall compiler and Apollo's Adreno register drivers cannot be copied as-is.

## Recovery and artifacts

- Original boot: private/device/backups/boot.img, 67,108,864 bytes; SHA256
  115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7.
- Always select the exact FASTBOOT target recorded privately; never flash
  bootloaders, GPT, modem, persist, or unrelated partitions.
- Known home-screen image v10 is retained in out/begonia-chain-xnu-forced/.
- Native captures v9/v11/v12 and decoded logs are in private/device/.
- System file on phone: /home/user/ios-begonia/system.apfs.
- Data file on phone: /home/user/ios-begonia/data.hfs (immutable Linux inode to
  pin extents; native block writes bypass that flag by design).
- Root/data maps are private/device/system.extents and data.extents; data has
  130 extents. They are device-specific. Do not move/defragment either image
  without rebuilding and verifying mappings.
- Log mirror: 2 MiB reserved at 0x1b7e00000, BEGLOG02 format. Read only with the
  matching reserved-memory PMOS DTB; original PMOS boot has no such reservation.
  Do not trust a mirror that the recovered Linux kernel may have overwritten.

## Latest read-only data inspection

The data image contains three boot-marker lines (87 bytes total), 787 regular
files, and 5,093 directories. Saved content includes many app-container metadata
files and mobile preference plists, including migration and SpringBoard files.
Thus this is NOT a case of every write disappearing at reboot.

However, `mobile/Library/Caches` and `db/lsd` are empty in the read-only view.
The mobile directory and Library have mode 0711/UID 501/GID 501; mobile Caches
has mode 0700/UID 501/GID 501, consistent with the seeded template. Do not
blindly change them to world-writable. Why native processes report Permission
denied despite these numeric ownership/mode values remains unresolved.

The image journal is dirty after the forced reset. hfsfuse inspected it with
`--force -o ro`, without replaying or modifying the journal. Missing files in
this view are not conclusive proof that no pending journal transaction exists.
Raw inspection reports remain private under private/device/.

## Immediate next steps

1. Preserve the inspected image and investigate LaunchServices database creation,
   native mobile-user access checks, HFS+ ACL/flags, and journal recovery. The
   initial read-only inventory is complete; no data was repaired or erased.
2. Distinguish failed writes, mount fallback to RAM, failed database commits,
   failed registrations due to signatures, and permission/access failures.
3. The upstream init seeds directories with modes and owners and accepts
   existing directories on persistent mounts. v12 reported success creating
   the skeleton. It clears tmp/run/vm but should retain registration data.
4. Fix only demonstrated causes; retain v10 as a GUI baseline. Do not claim
   touch, registration persistence, or faster reboot until verified.
5. Update this handoff and PROCESS.md; push source/notes after review. Private
   Apple assets, device dumps, photos, maps and backups remain excluded.

Latest published implementation commit before this handoff: 27c45db.
