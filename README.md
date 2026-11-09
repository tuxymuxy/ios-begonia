# iOS 16 on Redmi Note 8 Pro (begonia)

Experimental iOS 16.5 / XNU port for Xiaomi Redmi Note 8 Pro, MediaTek MT6785 (MT6359 PMIC).

**Status**: This is research code demonstrating that Apple's iOS kernel can boot on this hardware. The physical phone reaches the SpringBoard home screen (confirmed by owner photographs), but the UI is extremely slow and most features are non-functional. **This is not a usable daily driver.**

---

## What Works

### Boot & Basic Functionality
- **Native XNU kernel** boots on MT6785 hardware
- **UFS storage**: Read-only iOS system image mounted, 16 GiB HFS+ data partition prepared for writes
- **launchd and SpringBoard** load successfully
- **Physical phone displays home screen** (4–5 FPS in earliest captures)

### GPU (Mali-G76 MC4)
- **GLES 3.1 hardware compositing** via Mesa Panfrost works
- Compute shader tests: PASSED (64/64 matches)
- Metal blit operations: PASSED
- IOSurface clear/render operations: PASSED
- Driver timing: ~3 ms/frame in display pipeline

### CPU (ARM Cortex-A55 / A72)
- 7-core detection works (MT6785 is octa-core)
- CPU frequency readback functional at 1.275 GHz A55 / 875 MHz CCI

### Touchscreen
- Novatek NT36672A touch controller: firmware loads, physical finger events registered in XNU

### Wi-Fi & Bluetooth (Emulated)
- WiFi driver compiles and registers `en0` interface
- Bluetooth stack initializes with synthetic HCI responses

---

## What Does Not Work

### ❌ **Jailbreak / Cydia / Sileo**
**Does NOT work.** Attempts to run rootless jailbreak tools (Procursus, Sileo) fail:
- Missing shader specialization selectors in QuartzCore trigger SIGSEGV crashes
- MPS (Metal Performance Shaders) kernel assertions cause SpringBoard to exit
- The codebase has stubs for these features, but they are incomplete and unstable

### ❌ **Smooth UI / Real-Time Performance**
- Current best capture: **4.19 FPS** (extremely choppy)
- Full interactive 60 FPS operation: not achieved
- CPU bottlenecks in CoreAnimation shader compilation stall the pipeline
- GPU blit/compositor acceleration remains experimental

### ❌ **Metal Compositor (Full Acceleration)**
- Basic Metal tests pass, but the full compositor is unfinished
- QuartzCore pipeline has missing shader selectors that cause crashes
- Sustained smooth UI requires further kernel/userland work

### ❌ **Wi-Fi/Bluetooth Real Networking**
- Wi-Fi and Bluetooth interfaces register but do not associate with real networks
- DHCP and ARP are synthesized in software for testing purposes only
- No actual network connectivity

### ❌ **Camera**
- Camera support is not implemented (permission remains revoked per owner request)

### ❌ **Complete App Compatibility**
- Only basic system apps can launch before instability
- Background daemons cause crashes due to incomplete kernel/userland hooks

---

## Build Requirements

Host prerequisites:
```sh
# Basic compiler checks (host GCC/Clang must be available)
cc -std=c11 -Wall -Wextra -Werror tests/touch_report.c -o /tmp/test-touch
/tmp/test-touch
cc -std=c11 -Wall -Wextra -Werror tests/extent_policy.c -o /tmp/test-extents
/tmp/test-extents
```

Build steps are documented in [docs/PROCESS.md](docs/PROCESS.md).

---

## Source Layout

| Directory | Contents |
|-----------|----------|
| `boot/` | LK-to-XNU diagnostic loader, early boot code |
| `kernel/` | Begonia-specific kernel drivers (WiFi, BT, touch, GPU service) |
| `patches/` | XNU patches and additions |
| `tools/` | Host build utilities and Linux helper scripts |
| `firmware/mu/` | Menu/Vol experiment (deferred) |
| `out/` | Build outputs (excluded from Git, generated per build) |
| `private/` | Device-specific Apple firmware, captures (excluded from Git) |
| `upstream/` | Pinned upstream checkouts (HakiOS, Mesa, Panfrost) |

---

## Device Identity & References

- **Device**: Xiaomi Redmi Note 8 Pro (codename: `begonia`)
- **SoC**: MediaTek MT6785 Helio P70
- **GPU**: ARM Mali-G76 MC4
- **Touch Controller**: Novatek NT36672A
- **PMIC**: MediaTek MT6359

---

## Documentation

- [Process Log](docs/PROCESS.md) - Full development history and timeline
- [Storage](docs/STORAGE.md) - UFS/APFS integration details
- [GPU](docs/GPU.md) - Mali-G76 acceleration notes
- [Touch](docs/TOUCH.md) - Touchscreen driver integration
- [Porting Notes](docs/PORTING.md) - General porting challenges

---

## Licensing & Attribution

This project combines multiple components:
- **HakiOS XNU fork** (Mali-G76 support, iOS 16 compatibility patches)
- **Mesa Panfrost / PanVK** (GPU drivers for Bifrost architecture)
- **postmarketOS** concepts and boot infrastructure

See [NOTICE](NOTICE) for licensing details and attribution requirements.

**Never redistribute**: Apple firmware, private touch firmware, boot images, photos, or device dumps. These are excluded from Git intentionally.

---

## Important Notes

1. This is research/academic work only - do not flash to your device unless you understand the risks
2. Original postmarketOS boot images and partition backups should be retained separately for recovery
3. The repository excludes all private Apple files, device captures, and firmware by design
4. Build outputs in `out/` are regenerated per build and not tracked in Git

---

*This port demonstrates that iOS kernel fundamentals can run on non-Apple hardware, but achieving full compatibility requires extensive additional work on shader compilation, app compatibility layers, and performance optimizations.*
