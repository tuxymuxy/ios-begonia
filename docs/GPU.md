# Begonia GPU bring-up

Current home-screen output uses the existing software rendering/framebuffer path.
The user has photographed native SpringBoard; this does not establish GPU acceleration.

Current verified milestones: native power/reset, identity memory-write job, and
VA48 four-level mapped write job. The own-source compute shader test is prepared
as v44, awaiting hardware execution. The Mesa/Panfrost libraries and Metal bundle
cross-build for iOS; bundle is arm64/platformIOS with verified ad-hoc signature.
The kernel user-client and IOSurface synchronization still need integration;
SpringBoard currently uses software rendering. Sections below record bring-up
history, with newer results at the end.

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

## G76 userland architecture guard and first-job preparation

Recorded userland/gpu patch routes G76 through Mesa/NIR even if VMG_ISA=1 or
VMG_ISA_STRICT=1. The reference previously tried its G57-specific direct ISA
compiler on any Panfrost device. Product ID is now queried rather than inferred
from the G57 screen name; only known G57 products9091/9093 retain direct ISA.

`python3 tools/prepare_gpu_userland.py` applies this patch idempotently.
`python3 tools/test_gpu_frontend.py` builds the host frontend and verifies:
resource/fence mock4/4; own vertex/fragment/compute Metal samples translate and
compile3/3 through the G76 route. This needs the Mac Metal compiler. The reference
mock still uses synthetic non-product capabilities (and reports MC2), so this
is not a faithful MC4 hardware profile or proof of physical GPU execution.
The standalone Bifrost binary test and ISA policy tests also pass.

Native probe now has a separately gated begonia_gpu_selftest=1. Only after
power/reset is verified, it powers internal blocks and submits one WRITE_VALUE
job in AS0 identity mode to a newly allocated kernel page. Descriptor helper
restricts addresses to managed highRAM and excludes reserved log memory; host
bounds/alignment/64bit tests pass. Reset must complete before the page is freed;
on reset timeout it is retained until SoC reset to prevent DMA into freed memory.
No translated page-table mapper or user client is implemented. G76 MMU_FEATURES
0x2830 means VA48/PA40: do not copy the G52 reference's35bit/3-level mapper.

## Native G76 results (v38/v39)

v38 capture checksum/request validated: normalized owned protection requests,
powered all6domains, GPU_ID72110000/shader0xf/tiler1/L2=1, soft reset completed.
Measured GPU/SRAM rails575/850mV remain unchanged; clock26MHz for bring-up.
v39 capture validates the isolated WRITE_VALUE job: irq2,slot0,MMUfault0,
CPU observedbec07601 at the owned target. This proves job-manager memory access
in identity mode, not shader ALU execution or accelerated rendering.

Full measured MC4 profile now opt-in VGPU_MOCK_PROFILE=begonia:43supportedLinux
capabilities, unsupported43/44 returnEINVAL, dynamic simulated13MHz timestamp.
Host tests rebuild transport and relink binaries (to avoid staleMC2 objects):
Mali-G76 MC4 resources/fences4/4 and own AIR vertex/fragment/compute3/3 compile.
The older synthetic MC2 profile remains default only without this option.

Four-level VA48/PA40 helper and tests added; native mapped-page job is separately
gated and still unverified. All pages retained if GPU reset does not finish.

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
