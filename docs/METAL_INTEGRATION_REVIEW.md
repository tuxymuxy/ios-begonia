# Review of the proposed final Metal boot

Result: not ready; no boot or flash performed during this review.

The owner's request was conditional: boot only if this integration is ready.
The following checks failed that condition.

## No integration in the source change

The change to `kernel/bsd/dev/begonia_mali_probe.c` at lines243–255 adds
`printf` messages saying a Metal service is published and acceleration is ready.
It adds no IOKit service registration, user client, job submission transport,
Metal plugin loading, native Vulkan ICD, or compositor integration. The shader
test still resets the GPU before freeing its private allocations. Moreover,
`shader_compute_test()` returns void, so the new success message cannot certify
even that test's result. These messages are not evidence of acceleration.

## The final payload is an unchanged diagnostic

These three files are exactly identical,20377172bytes each:

- `final_metal_acceleration_boot.payload`
- `fastboot_payload.bin`
- `out/native-gpu-shader-v44.payload`

SHA256: `e0d4ef3e485001d9f2195e36cefaf4248ec65516b7966afe422658143e8fe6b6`.

They contain the old `no Metal service published` message, not the newly added
acceleration messages. Their embedded command line still requests the90second
v44 shader diagnostic. The proposed source change was not incorporated into
these artifacts. The build scripts copy the old payload instead of compiling
and linking a new kernel.

## The proposed boot command is invalid for this artifact

The payload starts with `SWEETXNU`, the private XNU loader payload format. It is
neither an Android boot image nor an ARM64 Linux Image. Our earlier successful
phone tests packed this payload with the begonia loader and original PMOS
kernel/ramdisk/DTB into a verified Android boot image.

Installed fastboot help specifies `boot KERNEL [RAMDISK [SECOND]]`; extra bare
arguments are not boot flags. `--cmdline` is a separate fastboot option and would
not implement missing XNU features. No code under kernel/boot/tools/patches/devices
consumes `begonia_gpu_metal` or `begonia_ui_acceleration`. The command also
misspells the existing probe flag as `begona_gpu_probe`.

## Saved boot artifacts were removed

`simple_build_metal.sh` line13 and other generated build scripts contain
`rm -rf out/begonia-chain-xnu-forced/*`. The previously verified
`out/begonia-chain-xnu-forced/ios16-gpu-shader-v44.img` is missing at review time.
The original PMOS backup is intact:67108864bytes, SHA256
`115e3e00624de812b99e83703768bf90c44d5e9196415381c036629c226e7ee7`.
Do not run these cleanup scripts. Preserve the raw diagnostic payload and
remaining backups for reconstruction.

## Actual remaining work

v44 verified native G76 compute64/64, not UI acceleration. The Metal-to-Vulkan
runtime builds separately. Native Vulkan transport, the PanVK ICD and correct
IOSurface sharing/cache synchronization still need integration and isolated
rendering tests before selecting that path for SpringBoard. Changing log text
or adding unimplemented boot flags cannot perform those steps.

Review used source diffs, exact artifact byte/hash comparisons, embedded command
line/message inspection, local fastboot help and backup checksums. No camera
access, device boot or partition write was used.
