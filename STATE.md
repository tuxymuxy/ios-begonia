# Current state — 2026-11-06

## Active v46 diagnostic

v46 installed and rebooted around21:52UTC Nov6;180sec capture/recovery.
Phone not yet reachable at21:55UTC. Retrieve logs before another test.
Image out/ios16-vulkan-v46.img,61118464bytes SHA256
6e8c07016d5c2e6b2aaa133cb983acf99a353e7746224c6e56d4f31148d82c52.
Payload out/native-vulkan-v46.payload;kernel out/kernel.begonia-vulkan-v46;
support out/begonia-vulkan-support-v46/root.apfs. Updated service validation is
in this kernel. Own Metal-shaped API->metal2vulkan->PanVK compute test20sec/500MiB
runs first in rc.jobs; look for BEGONIA M2V USER. No UI acceleration enabled.
PanVK native iOS build/sign and0strong unresolved symbol audit pass.
Objective-C frontend out/metal-frontend-ios/HakiosMetal builds/signs separately,
retains default process gate; not installed. Camera remains prohibited.

## Phone and owner constraints

Phone returned to postmarketOS after v45 GPU service test. Wi-Fi192.168.1.124
works; USB172.16.42.1 with strict192.168.1.124 host alias also works but may
briefly disconnect during network setup. Camera permission remains REVOKED.
Owner says keep working on UI acceleration, do not stop. No further permission
needed for scoped driver work and backed-up boot diagnostics.

The Qwen “final Metal” claim was reviewed and rejected: identical old v44
payload, print statements without service registration, invalid fastboot syntax,
and cleanup removed prior Android images. Original PMOS backup remains intact.
Review docs/METAL_INTEGRATION_REVIEW.md. Those untracked scripts must not run.

## v45 actual IOKit transport result

Exact request/checksums validate native-v45 mirror. G76 own compute again64/64.
Real HakiosMali IOKit service published. Signed user program opened it, queried
G76 product7211, allocated/mapped16KiB private BO, submitted WRITE_VALUE through
IOConnectCallStructMethod, waited, readbec07601, unmapped/freed/closed. Kernel
job1 slot1 IRQ2/MMU0/result1. This proves real application-to-GPU DMA, not UI
acceleration. Source backend kernel/bsd/dev/begonia_mali_service.c; generated
attributed S9+ IOKit code via tools/prepare_gpu_service.py. Shared VA prototype,
synchronous jobs/reset between jobs; not per-process isolation or async scheduler.
No compositor enabled yet.

- Image out/begonia-gpu-service-loader-v45/ios16-gpu-service-v45.img,49782784bytes
- SHA2568aeebde7feb1f8ea14048fd9e1cc4e27888d1f1c4a0190051cfebc99cbe0d1b8
- Payload out/native-gpu-service-v45.payload;kernel out/kernel.begonia-gpu-service-v45
- Support out/begonia-gpu-service-support-v45/root.apfs reuses tested support tree
- Capture private/device/native-v45-mirror.bin/.log/.json validated
- New loader out/begonia-gpu-service-loader-v45/Image,30512bytes,
  SHA2565e888fff672501053cd3057698829c56ca83a3f7174da74b28aef3c4be48decf
  QEMU relocated handoff test passes; phone v45 auto-recovery works.
- v45 image remains installed; leave data intact.

Source AFTER v45 adds stricter BO/chain validation, explicit submission error
return, oversized executable BO refusal, and VM unmap tests. These later changes
need the next build/test. prepare_kernel idempotence passes after0012 moved
service entries outside generated driver block and old patch header contexts.
Normal userland patch replay fails on overlapping historical input patches; GPU
support test deliberately reused known support tree via --base-root instead of
changing touch/init again. Do not claim that source replay issue is fixed.

Separate pristine upstream/mesa-vulkan at Mesa8e87a503... for native PanVK.
Build out/panvk-ios-build, target src/panfrost/vulkan/libvulkan_panfrost.dylib.
Userland/vulkan/patches currently adapts Darwin libdrm dependency and generic
vk_image DRM modifier metadata. Compat headers explicitly ENOSYS Linux perf
sessions; real pthread scheduling and dev_t macros. Panfrost source unchanged.
Build log out/panvk-ios-build.log; further portability/link/transport gaps remain.

## Verified native GPU result

v44 own-source compute shader executed on actual G76:64/64 values match i*3+7,
IRQ2,slot0,MMU0,fault address0/0,job exception1,first7,lastc4. Identity and VA48
four-level translated WRITE_VALUE jobs also pass. Exact payload request and log
checksums validated. GPU clock26MHz; unchanged rails575/850mV. No shader firmware
needed. No iOS UI hardware acceleration yet; no Metal service/client installed.

- Image out/begonia-chain-xnu-forced/ios16-gpu-shader-v44.img,49776640bytes
- SHA25643b724f9c3c96e1a3be437ee3cd7f836b03c88b5c5aadbd6d709a4f33e920374
- Payload out/native-gpu-shader-v44.payload;kernel out/kernel.begonia-gpu-shader-v44
- Capture private/device/native-v44-mirror.bin/.log/.json
- Touch private/device/native-v44-touch.json:9060polls,0bad,2081events,
  0SPI/rejections,maxread735.307us,checksum+epochvalid.
- v43 was prepared but never installed. v44 adds G76 TLS hash/IDVS settings.

GPU domains MT6785 MFG0..5 at328/32c/330/334/338/350, GPU13040000,
MFGCFG13fbf000. Owned protection normalized before power-up. Managed private
GPU pages only; four-level VA48/PA40,4K GPU pages over16K XNU pages. GPU resets
before free; retain all allocations on reset timeout. No PMIC writes implemented.
Inherited MFGPLL0314c4ec is~270MHz but stock OPP42 needs612.5mV; current575mV
is insufficient for blindly selecting that parent. Keep crystal clock for now.

## Vulkan work

Pinned upstream/ios-s9plus-exynos at2d092398040f98685b0c5a07776df5e9f199b9dc.
Its default installed graphics path is GLES; Metal/Vulkan is WIP, with unresolved
cache maintenance/runtime surface lifetimes. Sources and limitations inspected.
Pinned upstream/metal2vulkan at720ef1ce9ae2a2068999a8675717418b9416283e.
S9 WIP patch dba02debb819bdcc31e739b34ebbc59458681bbb3a70461c01b81d4170499612
applied. Official Rust1.98.1+iOS target installed locally under .tools/rust.

Host release translator built. Our own compute AIR -> SPIR-V PASS including
spirv-val Vulkan1.2; synthetic minimum profile, not measured phone Vulkan caps.
Elapsed0.2014sec/peak group RSS9338880bytes under20sec/500MiB enforcement.
Outputs out/begonia-own-compute.air/.spv and out/metal2vulkan-own-translation*.
Separate runtime built/signed for iOS16.5, -Dwarnings clean, host/iOS SDK linker
isolation fixed. out/metal2vulkan-target-ios/aarch64-apple-ios/release/
libmetal2vulkan_bridge.dylib, install name @rpath/libmetal2vulkan.dylib.
Reproduce tools/prepare_vulkan_userland.py and tools/build_metal2vulkan_ios.py.

Next: native PanVK ICD, kernel job transport, IOSurface sharing/cache ordering,
then isolated Vulkan compute/render before enabling it for SpringBoard. No Vulkan
runtime or Mesa-backed Metal bundle has been installed. Existing Viva/Panfrost
Metal bundle also builds for iOS as a separate fallback experiment.

## Touch / CPU / UI

Native iOS16.5 build20F66 SpringBoard photographed and observed; v24/v18 owner
opened apps/settings. v37+atomic SPI IRQ save/disable/restore fixes XNU delay()
sleeping with CS held; outer8ms poll still sleeps. Firmware is private verified
NT36672A SRAM upload, no touch firmware flash writes. Batching is opt-in only.
v40 idle14674polls/0bad, v44 recorded2081events/0bad; complete GUI stability
with this exact build remains unverified.

A55/CCI correction verified v31:1.275GHz/875MHz at unchanged800/900mV CPU rails,
exact inherited PLL/divider/PMIC guards. No full DVFS/thermal governor. XNU runs
one core. CPU1/MPIDR0x100 is recovery supervisor, never expose it to XNU.
Two-core experiments failed secondary rendezvous; keep cpus=1.
v42 combined CPU correction+atomic touch reached SpringBoard but exceeded30min
automatic recovery. Owner manually used FASTBOOT; both reserved records invalid
after recovery, so no log/stability claim. Recovery failure cause unresolved.

## Storage / access / recovery

Native read-only APFS file root on UFS, writable16GiB HFS+ data file, no GPT
changes. /home/user/ios-begonia/system.apfs and data.hfs. Immutable Linux data
inode pins its extents while native block writes persist. Never recreate data.

SSH user@192.168.1.124, normal strict /Users/tux/.ssh/known_hosts. USB172.16.42.1
with HostKeyAlias=192.168.1.124 sometimes works. Sudo credential provided by owner
in chat; never publish it. Exact fastboot target private/device/fastboot-target.txt.
fastboot boot unreliable; boot writes only, expected starting image and readback
verified. Never GPT, bootloaders, modem, persist or unrelated partitions.

Original PMOS private/device/backups/boot.img,67108864bytes,SHA256
115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7.
Log-preserving full PMOS image out/log-recovery/pmos-log-recovery-full.img.
RAM mirror0x1b7e00000/2MiB; fixed touch record0x1b7fff000. Upload out/read-log-mirror
and out/read-touch-diagnostics after each PMOS reboot. Validate mirror checksums
and request_id(exactpayload), touch checksum and exact payload epoch. Cold reset
can erase reserved evidence. No camera needed or allowed.

## Source / reproducibility

Repo https://git.tuxy.dev/tuxy/ios-begonia, main. Last pushed500c791 before Vulkan
work. Never publish Apple images/SDKs/AIR caches, touch firmware, captures,
identifiers, photos or extent maps; out/private/upstream/.tools ignored.
Normal kernel preparation embeds no shader fixture. Own generated fixture is
selected explicitly with --gpu-fixture. Userland/gpu README records frontend
build/capture steps. Process and historical evidence: docs/PROCESS.md.
