# Shared renderer probes

Build and execute focused host tests, then cross-build Android probes:

```sh
tests/graphics/check.sh /absolute/private/output /absolute/Android/Sdk/ndk/28.2.13676358
```

The probes compile the production EGL lifetime and native pixel adapters. The
render probe draws actual fixed-function and shader triangles, checks private
object names and packed depth/stencil FBOs, shares a native texture across GLES
versions, switches contexts, reads pixels and releases contexts. The pixel
probe checks multirow RGB texture uploads with explicit row length plus alignment,
BGRA upload/readback, invalid pixel-store state, preserved errors, and destination
retention on failed framebuffer readback. These are host graphics tests; guest
boot, guest rendering and frontend process lifecycle remain separate gates.

With exclusive device ownership, copy the matching Android binary to a private
executable location and run it. For example:

```sh
adb -s DEVICE push /absolute/private/output/egl-render-aarch64 /data/local/tmp/lt-egl-render
adb -s DEVICE shell chmod 755 /data/local/tmp/lt-egl-render
adb -s DEVICE shell /data/local/tmp/lt-egl-render
```

Use `x86_64` binaries on the AVD, and repeat for `egl-pixels`. Both are linked
with 16 KiB ELF alignment. Android runs real EGL/GLES1/GLES2; Linux's render
probe uses Mesa compatibility OpenGL through EGL, while its pixel probe uses a
native GLES2 context. Some Mesa installations cannot create native GLES1
contexts; the Android render probe must still pass on all acceptance targets.
