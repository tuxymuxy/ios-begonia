# Mu-Silicium for begonia (experimental)

This overlay targets **Redmi Note 8 Pro / MT6785**, using the pinned Mu revision
in `docs/sources.lock.json`. Firmware builds successfully. The actual phone displays the menu and volume
navigation works. Both OS handoffs currently hang after validation; diagnosis
is in progress. It is not a finished dual-boot or iOS port.

- Volume Up / Down: move between postmarketOS and experimental verbose iOS.
- Both volume buttons: select.
- Hold one volume button: repeat navigation.
- Default: postmarketOS after 10 seconds; any key stops the countdown.

The menu runs inside actual Mu firmware. Both OS choices use a memory-carried
copy of the existing chain loader. No UEFI storage driver or EFI system partition
is required. The iOS choice writes the matching one-use request immediately
before entering the existing XNU loader, avoiding the unreliable reboot marker.
Full iOS userland boot on the phone remains unverified.

## Build and checks

Run from the repository root:

```
python3 tools/prepare_mu.py
make -C upstream/Mu-Silicium/Mu_Basecore/BaseTools/Source/C -j8
python3 tools/build_mu.py
python3 tools/test_mu_keypad.py
python3 tools/test_mu_handoff.py
python3 tools/pack_mu.py --output out/mu-begonia/volume-picker.img
```

The project-local `.tools/mu-venv` holds Python requirements. Mu submodules must
be initialized first. The Mac build uses source-built BaseTools and the local
Zig toolchain, rather than Mu's unavailable Apple Silicon prebuilt tools.
`pack_mu.py` requires the verified private original boot backup and the exact
previously packaged opt-in chain image; it never writes to a phone.

## Hardware and memory

Display uses the measured 1080×2340, 4320-byte-stride inherited OVL0 layer-1
scanout. Keypad polling uses active-low KPD_MEM bits 0 and 1 at 0x10010004 and
currently relies on inherited clock/pin setup. Both need physical validation.

The FD occupies 0x40080000–0x40280000. The appended chain stays reserved below
0x43e80000; the DXE heap uses the previously identified upper DDR window at
0x180000000–0x184000000. A small reserved handoff stub at 0x44000000 copies the
chain downward only after ExitBootServices and cache/MMU shutdown, preserving
the LK device tree and writing the one-use choice. QEMU tests cover this actual
assembly with an overlapping source and destination, both request values, and
argument-register checks. They do not validate MT6785 firmware initialization.

UFS storage, USB, ACPI, Windows support, and cold-start peripheral setup are not
provided by this overlay. The reference eMMC and regulator drivers are excluded.
Captured phone data and Apple firmware are not part of the overlay. Source
additions use BSD-2-Clause-Patent; copied files retain upstream notices.
