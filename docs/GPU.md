# Begonia GPU bring-up

Current home-screen output uses the existing software rendering/framebuffer path.
The user has photographed native SpringBoard; this does not establish GPU acceleration.

Begonia has Mali-G76 (Bifrost), on the captured Linux DT at `0x13040000`.
Linux binds Panfrost. A live query returned product `0x7211`, shader mask
`0xf`, job-slot mask `0x7`, and address-space mask `0xff`. `tools/query_linux_gpu.py` reads the hardware feature masks
through Linux's documented Panfrost ioctl rather than accessing powered-down MMIO.

The pinned viva references provide useful XNU service/user-client, memory mapping,
job submission, synchronization, and Metal-interface scaffolding. Their target is
Mali-G57 (Valhall), so their GPU quirks, clock/power sequence and generated shader
machine code cannot be reused as-is. In particular viva-gpu's direct AIR-to-Valhall
ISA compiler is unsuitable for G76; investigate its Mesa/Panfrost path with a
Bifrost compiler before selecting it as an iOS Metal device.

Apollo uses Qualcomm Adreno 650. Its graphics register driver and shader backend
are not compatible with Mali-G76. Its general display/synchronization lessons
are references, not a Begonia GPU driver.

Bring-up order: capture Linux feature/power/clock details; independently verify
GPU identification and reset under XNU; allocate bounded GPU memory and map it;
run a small checked compute/render job; validate cache coherency and fences;
then connect the iOS renderer while retaining software fallback. Never make the
unverified GPU path a dependency of the known working GUI boot.

References: [viva-gpu](https://github.com/Nik300/viva-gpu),
[ios-viva](https://github.com/Nik300/ios-viva/tree/viva),
[Apollo](https://github.com/aTechGeekDev/ios-apollo),
[Linux Panfrost UAPI](https://github.com/torvalds/linux/blob/v6.16/include/uapi/drm/panfrost_drm.h).

## Clock/power dependency investigation (2026-11-06)

Captured DT: Mali register block0x13040000/0x4000; mfgcfg clock controller
0x13fbf000; four named shader power domains plus the clock-controller domain.
Pinned Xiaomi clk-mt6785-pg.c describes SPM status at0x160/0x164 and MFG0..5
power controls at0x328,0x32c,0x330,0x334,0x338,0x350. This differs from the
MT6768 reference's masks and clock-controller address; do not reuse that power
sequence without adaptation.

Added opt-in native begonia_perf.c clock/power observation. It reports MFGPLL
and SPM status along with CPU/CCI PLL/mux/divider values, without writes or Mali
MMIO access. It deliberately does not power on the GPU, submit work, publish a
Metal device or claim acceleration. The next GPU dependency is MT6785-specific
PMIC/SPM/bus-protection/clock ownership, followed by reset and G76 identification.

Native MT6359 PMIC reads work as ofv30 (identity5920). VGPU11 DA selector28
corresponds to575mV (400mV base,6.25mV steps); this alone is not permission to
power or clock the GPU. MFG domains remain uninitialized by this port. No native
GPU jobs or hardware acceleration are verified.

## G76 compiler and native probe preparation

Built Mesa26.2.6 at8e87a503ebd91408c88596d1b9eeb5f4c0b2088d with the pinned
viva-gpu Mesa patches. Host dependencies installed: LLVM23.1.5,
SPIRV-LLVM-Translator23.1.3, SPIRV-Tools1.4.363.0 and pkgconf. The reference's
macos build completed all1099 targets. This is a host compiler build, not an
installed iOS Metal driver.

Run `python3 tools/test_g76_compiler.py` after
`bash mesa/build.sh macos-nosmoke` inside the pinned viva-gpu checkout. The test
uses product0x72110000, explicitly checks architecture7 and Bifrost ISA, and
compiled a176-byte fragment shader. It does not use the direct Valhall ISA
compiler or claim execution on a physical GPU. Explicit checks remain active
even when the Mesa package adds NDEBUG.

kernel/bsd/dev/begonia_mali_probe.c is an optional first native experiment:
check enabled MT6359 GPU/VPU(SRAM_GPU) rails, require initially-off MFG domains
and expected bus protection; select documented26MHz crystal parent; power
MFG0/1 and four shader domains with bounded ACK waits; release only the MT6785
MFG bus-protection masks; ungate13fbf000 BG3D; read G76 ID and soft-reset.
It does not submit jobs, install page tables or publish a Metal device.
The normal UI image does not enable this path. Native execution is pending.
The actual MT6785 gpufreq source specifies VGPU minimum575mV, SRAM850mV and
maximum rail difference350mV; the probe uses stricter bounds within those.
The extended `tools/query_linux_gpu.py --all` can collect remaining capability
parameters after PMOS recovery instead of borrowing G57 values.
