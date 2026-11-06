# Metal to Vulkan experiment

The owner selected metal2vulkan and the S9+ reference. Exact revisions and patch
checksum are in `sources.json`. The reference's installed graphics path is GLES;
its Metal/Vulkan experiments have unfinished cache and surface-lifetime work.
Its Exynos power registers, G72 capabilities and installed images do not apply to
begonia. Preserve the original component license notices.

Verified here: metal2vulkan release translator builds; our authored compute AIR
translates to SPIR-V and passes `spirv-val` against a synthetic minimum Vulkan1.2
profile. A separate runtime/bridge builds and signs for iOS16.5 with warnings
treated as errors. Native G76 shader execution separately passes64/64results.
Native iOS PanVK now builds/signs; its required unresolved-symbol audit is empty.
The v46 diagnostic installs the ICD/runtime for an own-source compute test only.
The Metal compositor remains disabled pending actual hardware results.

Reproduce after checking out the exact sources under `upstream/`:

```
python3 tools/prepare_vulkan_userland.py
RUSTUP_HOME="$PWD/.tools/rust/rustup" CARGO_HOME="$PWD/.tools/rust/cargo" \
  CARGO_TARGET_DIR="$PWD/out/metal2vulkan-target" \
  .tools/rust/cargo/bin/cargo build --manifest-path upstream/metal2vulkan/Cargo.toml \
  -p metal2vulkan --features cli --release
BEGONIA_OWN_AIR_OUTPUT="$PWD/out/begonia-own-compute.air" \
  python3 tools/capture_gpu_compute.py --software-only
python3 tools/test_metal2vulkan_shader.py
python3 tools/build_metal2vulkan_ios.py --sdk /absolute/path/to/iPhoneOS16.5.sdk
```

The AIR exporter refuses to overwrite an existing file. Generated AIR/SPIR-V and
runtime binaries stay in ignored `out/`. The translation check enforces20seconds
and500MiB across its process group. The profile is a synthetic translator test,
not a claim about the phone's Vulkan capabilities. The runtime remains a separate
replaceable dylib, preserving its upstream dual-license integration model.

The v45 kernel submission transport passed a mapped user-client WRITE_VALUE job.
Native Vulkan execution and correct IOSurface page ownership/cache ordering still
need hardware qualification before compositor testing. Do not advertise
cached CPU/GPU memory as coherent to hide missing cache maintenance.

Native build/staging entry points:

```
python3 tools/build_panvk_ios.py --sdk /absolute/path/to/iPhoneOS16.5.sdk
python3 tools/build_metal_frontend_ios.py --sdk /absolute/path/to/iPhoneOS16.5.sdk
python3 tools/make_vulkan_support.py --base-root /absolute/tested/root \
  --output-dir out/fresh-diagnostic --test out/metal-vulkan-compute-test-v46
```

The frontend retains upstream process gating and is built separately, not staged
into the v46 support image. Its deprecated iOS API declarations produce three
compiler warnings; no compile/link errors. PanVK uses Darwin weak imports for
optional entry points and a build gate rejects strong unresolved dynamic lookups.
The support image removes unused diagnostics and reduces spare APFS blocks to128;
the payload builder now rejects overlap with the loader's dummy RAM disk.

Authored native diagnostics can be rebuilt together:

```
python3 tools/build_gpu_tests_ios.py --sdk /absolute/path/to/iPhoneOS16.5.sdk
```

`gpu_surface_test` imports an original16KiB IOSurface and checks three CPU/GPU
write/read cycles, with kernel WAIT_BO cache invalidation. `vulkan_device_test`
requests Vulkan1.0, reports the real API/extensions, and checks a16KiB GPU fill.
The diagnostic builder's `--isolated` option deliberately omits GUI startup jobs
to preserve evidence; it is not a UI image. `--surface-test`/`--vulkan-test` add
those binaries. Nothing here overrides Mesa's reported API version. PanVK v7
currently reports1.0, below metal2vulkan's1.2 floor; native compatibility work is
required before enabling the compositor.
