# iOS 16 bring-up for Redmi Note 8 Pro (begonia)

**This is an experimental bring-up workspace, not a working iOS ROM.** XNU and a source-only hello program have run on the actual Redmi. Native UFS reads and inherited verbose display output are verified separately. A verified native log now shows iOS launchd, SpringBoard and backboardd running; the graphical interface is still unverified. iOS userland reaches a visibly corrupted home screen in QEMU.

Mu-Silicium now builds for begonia: the physical phone shows its menu, volume navigation works, and its countdown runs. Its shared OS handoff hangs. The user deferred Mu while direct native iOS debugging continues. See [Mu source and status](firmware/mu/README.md).

The intended target is iOS 16's plain arm64 iPhone userland on open-source XNU, following ios-sweet and HakiOS. Begonia's MediaTek MT6785 needs a separate platform layer. Renaming a Snapdragon configuration will not work.

## Available now

- A minimal ARM64 entry probe that records CPU features, entry EL, DTB pointer and counter frequency in RAM. The default build performs no peripheral MMIO and no storage writes. It halts after the report; there is no Android chainload or automatic reboot.
- QEMU loader/supervisor tests, a visible iOS emulator, and separate hardware diagnostic images. Emulation does not establish phone compatibility.
- Source-derived board facts with explicit unknowns, pinned upstream revisions, read-only device collection, and a staged integration plan.

## Reproduce the offline diagnostic

Requires macOS ARM64 clang, Python 3, Zig 0.13 and QEMU with ARM64 support. The workspace-local linker can be installed with:

```sh
python3 -m pip install --target .tools/python ziglang==0.13.0
python3 tools/build_probe.py
python3 tools/test_probe.py
```

Outputs: `out/begonia/Image`, `out/begonia/probe.elf`, manifests, and `out/qemu/serial.log`. **Never load `out/qemu/Image` on the phone:** it writes QEMU's PL011 address. The default image halts without a visible phone console. An optional chainloader variant now logs to the captured ramoops console and starts the original postmarketOS kernel; see the process log. The temporary boot image has passed local layout checks and an emulated handoff test, and has executed on the phone using a backed-up boot-partition test. The original boot partition was restored and its full checksum verified. No flashing command is supplied.

## Connected phone (2026-11-05)

SSH and sudo access on postmarketOS now work. The live device tree and boot, DTBO and vbmeta backups are captured in ignored `private/`. Live evidence corrects the initial eMMC assumption: this phone has UFS. Boot tests use a verified original backup. A regular iOS filesystem image is staged on userdata; no formatting or repartitioning was performed. The current direct native diagnostic retries until the original boot is restored. See the process log for the latest installation and recovery state.

## Android collection alternative

Enable USB debugging and authorize this computer, then run the collector using the exact serial displayed by `adb devices`:

```sh
python3 tools/collect_device.py --serial YOUR_DEVICE
```

If Android already has working root, `--root` additionally reads the live DTB and current boot partition. It does not root or unlock the phone. Captures stay in ignored `private/`. Keep backups outside this workspace too. Do not share captures publicly: they can contain identifiers.

```sh
python3 tools/inspect_dtb.py private/device/live.dtb
```

See [process log](docs/PROCESS.md), [integration plan](docs/PORTING.md), [hardware facts](devices/begonia/board.json), and [source revisions](docs/sources.lock.json). Upstream directories are independent checkouts; some are sparse and do not contain all build inputs.

Native GUI milestone: user photographs from the v10 two-hour test confirm the SpringBoard home screen on the physical phone. Physical touch and hardware GPU acceleration are not yet working.
