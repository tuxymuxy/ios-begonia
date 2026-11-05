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

After the owner reported repeated app processing and a forced reboot, they
entered FASTBOOT. The verified ORIGINAL postmarketOS boot backup was flashed
and reboot requested successfully. postmarketOS SSH is now available. The existing iOS data image was inspected
READ-ONLY and unmounted afterward. Do not recreate/format the data image.
Neither the system image nor the data image was erased during recovery.

## Latest iOS test and observations

- Last installed experimental image was v14, a two-hour verbose run with
  persistent data and experimental touch enabled.
- v14: out/begonia-chain-xnu-forced/ios16-native-persistent-long-v14.img
  49,766,400 bytes; SHA256
  1f0507a72022743190540e9915e377061b7e97bcbe8fc029e8c233f0aedef2f3.
- User forced a reboot before log capture, then reported the app list repeated
  through further attempts. A forced reset can retry iOS because the automatic
  warm fallback depends on a matching completed capture marker.
- IMG_9422 shows background retries, signature errors, and nanoregistryd unable
  to use /var/mobile/Library/Caches (Permission denied).
- IMG_9423.MOV shows lsd advancing through DIFFERENT apps while background
  retries continue. This proves local progress in that clip, not completion or
  persistence. Owner subsequently reports repeated full passes through apps.
- Do not assume the remaining issue is just a slow first boot. Inspect actual
  saved database files, mount status, permissions, and journal recovery first.

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
