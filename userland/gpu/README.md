# G76 adaptation

The pinned `upstream/viva-gpu` checkout is adapted with the numbered patches.
Run `python3 tools/prepare_gpu_userland.py` to reproduce them. G76 uses the
Mesa Bifrost compiler; the reference's direct Valhall compiler is bypassed.
The opt-in `VGPU_MOCK_PROFILE=begonia` uses measured MC4 hardware capabilities.

`python3 tools/capture_gpu_compute.py` builds our own Metal compute shader,
translates AIR to NIR, and captures one Panfrost submission in
`out/g76-compute-capture-data`. The mock does not execute it. The capture path
refuses imported memory and limits the number and total size of buffers.
Capture directories must be new to avoid mixing submissions.

Generate a private kernel fixture using the output VA printed by that tool:

```
python3 tools/make_gpu_fixture.py out/g76-compute-capture-data \
  --output-va 0xa014000 --output out/begonia_gpu_fixture_data.h
python3 tools/prepare_kernel.py --gpu-fixture out/begonia_gpu_fixture_data.h
```

The output VA above is an example from the recorded run; use the actual value.
Normal kernel preparation embeds no fixture. The native test needs all four
GPU flags: `--gpu-probe --gpu-selftest --gpu-mapped-selftest
--gpu-shader-selftest`, plus single-core `--perf-probe` and a capture deadline
at most180seconds. It tests64results (`i*3+7`) and resets before freeing memory;
on reset failure allocations remain until the phone resets. No live framebuffer
or user memory is mapped by this test.

The iOS build can use the already installed16.5SDK without changing the selected
Xcode developer directory:

```
VIVA_IOS_SDK=/absolute/path/to/iPhoneOS16.5.sdk bash upstream/viva-gpu/ios/build.sh mesa
VIVA_IOS_SDK=/absolute/path/to/iPhoneOS16.5.sdk bash upstream/viva-gpu/ios/build.sh driver
```

These are build artifacts, not an installed acceleration stack. The native
memory-write tests pass; shader execution and iOS Metal acceleration are separate
milestones. No Apple assets, SDKs, firmware, or device captures belong in Git.
