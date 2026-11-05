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
