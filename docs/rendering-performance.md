# Android renderer performance

The Android build enables coherent framebuffer fetch and whole-pass DYN
vertex streaming by default. Fetch is used only when the driver advertises
the exact coherent extension and the shader compiles and links. Unsupported
drivers or failed shaders retain the ordinary shader and dependency-preserving
copy path. Arena allocation failure retains the original per-face uploads.
Other platform builds keep their existing renderer paths.

These paths are built into the normal Android renderer; no extra compiler
flags or user settings are needed.

The changes preserve face order, texture/table boundaries, palette and depth
behavior. They do not change gameplay, physics, effects or device resolution.
Optional `IGN_PROF` renderer counters report copies, draws and uploaded bytes;
without `IGN_PROF`, the counters compile away.

Run the asset-free host tests from the repository root:

```sh
make -f tests/renderer.mk test-renderer
```

They are also part of `make test`. The standalone target needs GNU Make,
a native C compiler and a POSIX shell; on Windows use Git Bash or put Git's
`usr/bin` on the command's PATH, with `CC=gcc`. Windows-specific unwind and
switch-table options apply only to host fixtures. The tests include feature
and platform controls, coherent-extension matching, shader failures, dependency layers,
full uploaded vertex payloads and profiler reporting. They need no ROM,
display server, Android device or SDK.

Faces wholly outside the screen are conservatively removed before upload; intersecting and uncertain faces retain their stable draw order.
