# Native GLES compositor experiment

This separate route uses the S9 reference's Mesa 25.0.7 Panfrost/OSMesa and the
existing iOS EAGL compositor adapter. It shares the verified Begonia IOKit GPU
service with PanVK, but keeps the older 40-byte DRM submit ABI and the reference's
IOSurface-ID import interface separate. There is no change to the Metal path.
The adapter is opt-in, and a hardware-only test rejects software renderers.

```sh
curl --fail --location https://archive.mesa3d.org/mesa-25.0.7.tar.xz -o out/mesa-25.0.7.tar.xz
python3 tools/build_gles_ios.py --sdk /absolute/path/to/iPhoneOS16.5.sdk --frontend
python3 tools/make_gles_support.py --base-root out/verified-support/root --output-dir out/fresh-gles-test
```

The preparation script verifies the archive SHA-256 and pinned S9 revision
before extracting/applying the three source patches. The build does not search
laptop libraries when linking the iOS target. Native binary fences use the
reference's checked implementation; unsupported PRIME export fails explicitly.
Build output stays under ignored `out/` and `upstream/`, with upstream notices
retained. No private runtime assets are committed.

Use the isolated test first. It checks the actual Mali-G76 renderer, clear
readback, and an authored GLSL ES triangle. `--ui` stages the optional compositor
adapter and full daemon set for a separate test. Enable it via
`make_native_payload.py --gles-compositor --gpu-service` plus the required GPU
selftest flags. Neither staging nor building writes the phone. The log-preserving
recovery base is required for every diagnostic image.

Hardware results: v61 exact capture verified Mali-G76 clear and triangle
readback. v62 verified CoreAnimation itself uses Mali-G76 GLES3.1, with432
successful captured GPU jobs and native1080x2340 presentation. One active frame
window measured4.19FPS and2.852ms/frame in the display driver. See `STATE.md`
for the tested images and limits.
