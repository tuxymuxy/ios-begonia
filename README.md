# iOS on Redmi Note 8 Pro (begonia)

Experimental iOS 16 / XNU port for Xiaomi Redmi Note 8 Pro, MediaTek MT6785.
**The physical phone has reached the SpringBoard home screen**, confirmed by
owner photographs on 2026-11-05. This repository contains the port source,
build tools and process notes; it is not a ready-to-flash ROM.

Current status:

- Native XNU, UFS-backed read-only iOS system image, launchd, backboardd and
  SpringBoard run on the phone. Software-rendered frames reach the real panel.
- First boot required a long attempt; the earlier 4/15-minute limits were too short.
- Novatek NT36672A touch: firmware boot and physical finger events are verified
  in XNU. Owner opened Settings with touch on v24 after console release;
  interaction remains slow/unreliable and v25 regressed. See
  [touch](docs/TOUCH.md).
- Persistent data: a separate 16 GiB HFS+ image is prepared; the isolated write
  path mounted successfully, and an iOS-written marker survived reboot.
- CPU: native clock/rail readback and guarded 1.275 GHz A55 / 875 MHz CCI
  correction passed a short boot test at unchanged voltages. SMP is unverified.
- GPU: native G76 power/reset and a private-page write jobs pass, including
  four-level address translation. Native compute shaders produce64/64
  expected results. Mesa-backed Metal and metal2vulkan iOS libraries build;
  native Vulkan transport and iOS UI acceleration are still under development.
- Mu-Silicium menu and volume navigation worked, but OS handoff stalled; deferred.

See [process log](docs/PROCESS.md), [storage](docs/STORAGE.md),
[GPU work](docs/GPU.md), and [porting notes](docs/PORTING.md).
Pinned references are in [sources.lock.json](docs/sources.lock.json).

Source layout: `boot/` holds the LK-to-XNU diagnostic loader, `kernel/` the
Begonia drivers, `patches/` the XNU additions, `tools/` host and Linux utilities,
and `firmware/mu/` the deferred menu experiment. `tools/prepare_kernel.py` applies
the additions to the prepared, pinned HakiOS tree. External checkouts under
`upstream/`, build outputs under `out/`, and private device/Apple files under
`private/` are intentionally excluded from Git. Read the process log before
attempting a device build; storage mappings are specific to the owner's device.

Host checks:

```sh
cc -std=c11 -Wall -Wextra -Werror tests/touch_report.c -o /tmp/test-touch
/tmp/test-touch
cc -std=c11 -Wall -Wextra -Werror tests/extent_policy.c -o /tmp/test-extents
/tmp/test-extents
```

Original postmarketOS boot and partition backups are retained locally. Native
tests use verified boot-image installation and a separate recovery supervisor.
No Apple firmware, private touch firmware, boot images, photos or device dumps
are redistributed. See [NOTICE](NOTICE) for licensing and attribution.
