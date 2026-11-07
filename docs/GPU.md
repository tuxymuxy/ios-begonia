# Begonia GPU bring-up

Hardware qualification now includes native GPU shader execution, IOKit user
submissions, original IOSurface visibility, Metal-to-Vulkan compute and small
Metal blit/render tests, and an authored native GLES triangle (v61). The v59
SpringBoard boot still excluded its compositor from Metal. Its captured driver
median was67.705ms/frame with a 1125x2436 canvas scaled to the1080x2340 panel.
v60 proved matching canvas dimensions, without usable FPS windows. v62 tests
the native GLES compositor with frame timings. GPU test success alone does not
establish that the UI uses that GPU.

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

### v48 shared-surface verification and Vulkan startup failure

Exact capture validates request/checksums. GPU user job passes again. Authored
16KiB IOSurface import test passes3/3cycles: original pages receive GPU words
bec07610/11/12, CPU guards remaincafe0000/1/2. WAIT_BO performs real kernel cache
maintenance; no EL0 cache trap or false coherence declaration used. This proves
small surface visibility, not sustained full-frame rendering or lifetime safety
under a compositor.

Native ICD loads, but vkCreateInstance returnsVK_ERROR_INITIALIZATION_FAILED(-3),
before GPU discovery. Mesa's common build-id code already parses actual LC_UUID
and zero-pads it into the32byte cache identity; its shared minimum erroneously
required20bytes though Mach-O UUID is16. Patch0005 fixes expected length for
Darwin only; host test looks up its actual LC_UUID and checks minimum/padding.
Native driver rebuilt/signed,0strong unresolved dynamic lookups. v49 tests it.
PanVK v7 source advertises1.0; metal2vulkan requires1.2. No API override applied.

### v59–v62: distinguish Metal tests from compositor qualification

v59 Metal frontend blit4096/4096 and64x64 IOSurface render clear passed.
backboardd excluded by metal=2, so no Metal compositing. MPS stubs skipped work;
new builds require explicit diagnostic flag for these stubs, and stage only
standalone Metal test clients unless named. PanVK v7 compatibility remains
experimental and does not claim conformance. v60 matched panel dimensions with
only boot-arg changes. v61 verified native Mali-G76 GLES clear/triangle,13/13 jobs
and zero faults. v62 tests existing EAGL compositor adapter on that driver.
