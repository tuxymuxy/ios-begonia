# Current state — 2026-11-06

## Current phone: v42 UI / atomic touch plus tested CPU correction

v41 capture validated exact payload request and both log checksums. Native G76
VA48 four-level mapped WRITE_VALUE PASS: irq2, slot0, MMU0, faultaddr0/0,
wordbec07601. Identity test also passed. This is DMA/job-manager evidence;
shader execution and Metal acceleration remain unverified.
v41 touch record validates epoch/checksum:9091polls,0bad,0SPIerrors,0events,
maxread740us. No physical finger interaction during that capture.

v42 installed/readback verified and reboot requested:30-minute UI test, atomic SPI,
CPU A55/CCI guarded correction enabled, GPU probes OFF, console released for GUI.
- Image out/begonia-chain-xnu-forced/ios16-ui-v42.img,49774592bytes
- SHA256108927b11ddbe290a23a25dbfed1af559856ec98e2056599ddecabca894a3b2f
- Payload out/native-ui-v42.payload
- Kernel out/kernel.begonia-gpu-mapped-v41
- Install evidence private/device/native-v42-install.txt
- Next: observe UI, then retrieve v42 logs and touch record after automatic recovery.

Next prepared GPU image: v44 own-source compute test (NOT installed yet).
Image out/begonia-chain-xnu-forced/ios16-gpu-shader-v44.img,49776640bytes,
SHA25643b724f9c3c96e1a3be437ee3cd7f836b03c88b5c5aadbd6d709a4f33e920374.
Payload out/native-gpu-shader-v44.payload; kernel out/kernel.begonia-gpu-shader-v44.
Exactly one compute chain,64results i*3+7,5BOs/8private16KiB data pages;
GPU four-level tables map only these allocations. G76 TLS hash/IDVS configuration.
v43 earlier preparation lacked this configuration and was never installed.

Built Mesa for iOS (978targets/825archive members), signed arm64/platformIOS
VivaMTLDriver.bundle; codesign verification passes. Includes G76 Mesa/NIR routing.
Private queue descriptor declaration needed with installed Theos16.5SDK.
Signed iOS16.5 test_vgpu/vmprobe ready in out/gpu-ios-tests. No bundle installed,
no GPU user client integrated and no SpringBoard GPU acceleration yet.
Reproduction: userland/gpu/README.md. Host regression4/4resources/fences,
3/3ownshadercompiles; fixture validation7/7. Shader execution remains pending.

Latest touch evidence v40 FIXED RECORD validates checksum+exact bootepoch:
14674polls,0bad,0SPIerrors,0each rejection category,stage2active,maxread1177.153us.
No finger used=>0events. New atomic SPI crossed earlier~12938poll failure point.
Camera v37 at~152sec shows actual SpringBoard. Physical app interaction with
atomic fix still needs confirmation; do not claim complete/stable GUI touch.

Fixed104byte touch record at1b7fff000, last4KiB of existing2MiB reserved mirror,
outside all mirror text. Sequence+FNV+bootepoch reject torn/stale data. Reader
out/read-touch-diagnostics from tools/read_touch_diagnostics.c, pass exact
payload epoch. v40 record private/device/native-v40-touch.json. Normal text
summaries were overwritten even in v37; do not equate missing text with no polls.

Native GPU v38 power/reset PASS:ID72110000,shaderf,tiler1,L21 after6domains and
owned protection normalization at26MHz. v39 identity WRITE_VALUE PASS:
irq2,slot0,MMU0,wordbec07601. NO shader ALU/rendering/Metal acceleration yet.
Before this v36 safely declined partial inherited protection; v38 fixed it.

Camera authorized via owner's offer; bounded phone-only rotated snapshots work,
noaudio. Stills private. CurrentOStest short; no home screen expected before
90sec recovery. Do not interrupt automated recovery unless actual failure.

Owner wants continuous progress without repeated interruptions and explicitly
authorizes CPU clock correction. Do not claim stable GUI touch or GPU acceleration.

## Verified CPU work

Native v27/v30 showed A55~1.175GHz,A76~1.085GHz,CCI~612MHz. Mux1=ARMPLL,
confirmed in pinned MT6785 configuration. Linux later reports2GHz/2.05GHz,
CCI1.4GHz; these Linux rates are not inherited by XNU.

Read-only MT6785 PWRAP WACS2 works: MT6359 HWCID/SWCID5920, VPROC2 selector64
=>800mV; SRAM_PROC2 selector64=>900mV. No regulator-write commands implemented.
Stock captured OPP permits A551275MHz/800mV and requiredCCI875MHz/781.25mV.

Opt-in begonia_cpu_boost=1/--cpu-boost: exact PLL/divider, PMIC/voltage guards;
raiseCCI first, CLKSQ parking during PCW update,20us settle, restoreARMPLL;
no voltage writes. v31 capture validated request/checksums and readback:
A55 8116989d->011884ec,CCI821789d8->0221a762. Native file-root boot continued;
7500touch polls/0invalid. Longer stability/performance remains pending v32.
Host tests/clock_policy.c passes rate/mask/rail/unknown-state checks.

SMP is NOT working. v28 registers physical0/0x200 but1of2 processors run;
secondary acceptsCPU_ON but never reports reaching kernel startup/scheduler.
v29 lowRAM br-x0 trampoline0x400801c0 stillfails. Keep cpus=1 for UI.
PhysicalCPU1/MPIDR0x100 belongs to recovery supervisor; NEVER expose it to XNU.
Opt-in --cpus2 maps logical0/1 to physical0/2 only. Trampoline experiment remains
opt-in; high reset-address hypothesis is not sufficient to explain failure.

## Touch / GUI evidence

- Native iOS16.5(20F66)/XNU22.5 reached SpringBoard, owner photographed v10,
  again confirmed v18. v24 console-release build allowed real Settings taps.
- v24 was very slow/stalled; clock later advanced. Input tracing showed backlog.
- v25 (quiet/batched reads) failed GUI touch. Capture:22500polls/13684invalid/
  58events, later30000/21184/58. Root cause unproven, batching now opt-in only.
- v27:7500polls/0invalid/387events; v28:0invalid/649events. Controller-level
  failure fromv25 not reproduced. Coordinates are normalized0..65535;30k/40k
  values are valid, not physical-panel coordinates.
- Decoder classifies negative reasons: recovery77,watchdogFD/FE,invalidID,
  duplicates,bounds. Rate-limited prefixes added; no native rejection observed
  in v27/v28. Firmware reload recovery is NOT implemented.
- Verified NT36672A SPI5 at11019000; resetGPIO2 LOW; firmware all8 readbacks
  match; polling8ms. Exact32bit MMIO, CFG1 beforeFIFO, STATUS1 idle (not repeated
  read-clearSTATUS0). Private firmware stays private. Do not regress this path.

Known partially working fallback: out/begonia-chain-xnu-forced/ios16-no-overlay-v24.img
49768448bytes,SHA256a4abe35aca60b440339f1d813a8c7f51fbfd64cc85836a10fce3f6f264669886.
It is retained, NOT currently installed. v24 payload also retained.

## GPU work: compiler verified, native probe untested

Software rendering/framebuffer remains in use. Linux Panfrost measured G76
product7211,shader0xf,AS0xff,jobslots0x7. Native power/reset/identity jobs now verified; mapped addressing under test; shader/Metal unported.

Built Mesa26.2.6 commit8e87a503ebd91408c88596d1b9eeb5f4c0b2088d using pinned
viva-gpu patches. Host LLVM23.1.5,SPIRV-LLVM-Translator23.1.3,SPIRV-Tools1.4.363.0
and pkgconf installed. tools/test_g76_compiler.py passes: architecture7,
Bifrost ISA,176-byte fragment shader. No physical GPU execution implied.

kernel/bsd/dev/begonia_mali_probe.c compiled as opt-in --gpu-probe: actualMT6785
SPM/MFG clock/bus-protection addresses; measured enabled MT6359 rails required;
26MHz crystal; bounded power/SRAM ACK waits; G76 identity/soft-reset only.
No native GPU jobs/MMU/Metal service verified. Power/reset and identity job now verified in v38/v39; not enabled in ordinary UI builds. Separately gated selftest source can submit one private-page
WRITE_VALUE job in identity mode; host range/alignment/mirror-exclusion checks
pass. Reset before freeing, retain allocation on reset timeout. Never enable
selftest until the power/reset capture passes. G76 mapper must use VA48/PA40,
not reference G52's35bit/3-level tables.
Prepared v33 NOT INSTALLED: out/begonia-chain-xnu-forced/ios16-gpu-probe-v33.img,
49770496bytes (different length fromv32),SHA256
9db1d49948c5f9d63832e25bcfd21804091bf2d0146f45dd3137fc2b7fff43c2.
Payload out/native-gpu-v33.payload; kernel out/kernel.begonia-gpu-probe.
90sec deadline, oneCPU, noCPUboost; isolates GPU power/reset. Do NOT install
before v32 capture/review. Verify old/new image lengths separately on install.
DefaultUI remains unchanged. Extended query_linux_gpu.py --all prepared to
capture missing hardware capabilities instead of copying G57 values.

Next: retrieve v32 after recovery (validate exact request/checksums), assess UI
and timing. Preserve UI baseline. Then a short <=180sec opt-in native GPU probe,
plus extended Linux capability/rail baseline. Do not interrupt active v32.

## Storage and recovery

System: /home/user/ios-begonia/system.apfs (read-only file-backed UFS root).
Data: /home/user/ios-begonia/data.hfs, fully allocated16GiB journaledHFS+ with
130 verified disjoint extents. No physical repartitioning. Multiple markers,
caches and app-container metadata persisted. Preserve data; do not reinitialize.
Immutable Linux inode pins extents but does not prevent native block writes.
HFS mountuid/gid/mask VNOVAL fix essential; recurring group deletion removed.
No blanket AMFI flags by default. No synthetic touch in physical-touch support.

PMOS SSH user@192.168.1.124; USB172.16.42.1 with verified host alias can work.
Use normal /Users/tux/.ssh/known_hosts, strict checking. Fastboot target private
in private/device/fastboot-target.txt; always select exact target. Temporary
fastboot boot unreliable: only backed-up boot writes, verify current/staged and
partition readback. No GPT/bootloader/modem/persist changes.

Original PMOS backup private/device/backups/boot.img,67108864bytes,SHA256
115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7.
Cold/early manual reset can retry iOS. To leave PMOS safely, restore exact backup.

Log mirror:0x1b7e00000/2MiB, matching no-map PMOS recoveryDTB, BEGLOG02 format.
Copy out/read-log-mirror to PMOS/tmp after reboot, retrieve binary privately,
parse with tools/parse_log_mirror.py and compare request_id(exactpayload).
Captures private/device/native-v24/v25/v27/v28/v29/v30/v31-mirror.bin and decoded
log/JSON. v26 was prepared but never installed. v32 capture still pending.

No Apple assets, firmware, images, identifiers or private device maps/captures
published. Mu menu handoff remains deferred. Process: docs/PROCESS.md;
CPU/GPU/touch/storage details in docs/. Source repo git.tuxy.dev/tuxy/ios-begonia.

## Additional offline GPU frontend validation during v32

G76 architecture guard recorded in userland/gpu/patches. Queries actual product;
G76 bypasses direct Valhall ISA regardless of VMG_ISA/STRICT flags. Host frontend
builds; tests/gpu_arch_policy.c passes; resource/fence mock4/4; own Metal
vertex/fragment/compute AIR->NIR->G76 compile3/3. Initial mock used synthetic G57 capabilities; current opt-in measured MC4 profile is described below. Do not claim physical shader execution.
Reproduce with tools/test_gpu_frontend.py (Mac Metal compiler needed).
v34/v35 prepared (both NOT installed),49770496bytes:
- v34 power/reset only SHA256fe9ad1b7aed1553179790096c7ba36eda498bd3cfc9f26583f434979d6bb2d37
- v35 adds selftest SHA256f2dcfab070017b55d48b4cab32cf260d4497ecbedabd3736acaa02f0bdc35a4c
Images out/begonia-chain-xnu-forced/ios16-gpu-probe-v34.img and v35.img;
payloads out/native-gpu-v34.payload/v35.payload; kernel out/kernel.begonia-gpu-v34.
90sec,oneCPU,noCPUboost. Only installv34 after capturing v32. Only installv35
if v34 reports matching G76 ID and soft reset complete. Oldv33 uninstalled.

## Measured full GPU profile

Linux Panfrost --all captured45 fields/43supported in
private/device/linux-gpu-parameters-full.json. Thread featuresa046000,
maxthreads768,workgroup/barrier384; texture formats and slot masks differ from
G57. Public model-capability header userland/gpu/begonia_gpu_params.h contains
only generic hardware numeric capabilities (no device identifiers). Added
opt-in VGPU_MOCK_PROFILE=begonia, MC4; unsupported43/44 returnEINVAL, timestamp
simulated13MHz. Rebuilt libdrm transport and relinked frontend so no stale MC2
objects. Host tests now report Mali-G76 MC4,4/4 resources/fences,3/3 own Metal
AIR shader compilations. No native hardware execution implied. Default mock
unchanged. tools/test_gpu_frontend.py reproducible; profile tests pass.

## v40 telemetry artifact/result

Image out/begonia-chain-xnu-forced/ios16-touch-telemetry-v40.img,49772544bytes,
SHA2562523d35889e2e3ca3708c52b19a2d047c671f3675a48946e2ad36c4053754acf.
Payload out/native-touch-telemetry-v40.payload. Log+touch record both validated.
Native touch record last bytesfffdfdfdfdfdfffd (idle slots),IRQLow,allerrors0.
Next afterv41: retrieve mirror and touch record with epochguard, then prepare a
longer UI run of atomic SPI+tested CPU correction, GPU job probes OFF. GPU
mapped result must pass before proceeding toward native shader execution/client.
