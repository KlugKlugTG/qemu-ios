# Physical Pixel native-renderer investigation

Source-only audit of engine259e4ad31702f4927ed8d06a06b64219121bc0a0 and
Android25's be428ddd source. No physical Pixel ADB log, actual GL implementation,
page size or graphics failure is established by this audit. The reported smooth
progress bar, subsequent logo flicker/idle, approximately140% CPU and482MiB worker
PSS do not establish OOM, a host crash or a guest reboot. Retain those reports
separately from x86 virtual-Pixel results.

## Concrete native compatibility boundaries

Android ARM64 selects `provider=platform` before the first context. The provider
table initially uses linked public EGL/GLES entry points; platform selection does
not resolve or verify the197 packaged ANGLE owners. The optional packaged ANGLE
DSOs and JNI opt-in are x86_64 only. A successful x86 ANGLE guest is not a test of
the physical ARM64 platform driver. Obtain the renderer/version from the actual
worker's `[gles] host GL up` line, not a device-profile name or another process's
SurfaceFlinger GL implementation.

| Production source boundary | Established source behavior | Physical evidence that distinguishes it |
| --- | --- | --- |
| `gles-host-egl.c.inc`, `gles_egl_create` | Chooses one RGBA8 pbuffer config requiring **both** ES1 and ES2 renderable bits, even when the current guest context needs only API1. Creates a1x1 offscreen pbuffer. | `initialize/config failed` or `create api=1 failed` with the original EGL error, versus successful `host GL up ... guest-api=1`. A config supporting only the requested API would not satisfy the current combined mask. No such rejection is known on Pixel. |
| `gles-host-native.c.inc`, `gles_native_require_fbo` | GLES1 requires the exact `GL_OES_framebuffer_object` extension; missing support explicitly refuses host setup. ES2 uses core FBO entry points. | `unsupported required GLES1 capability GL_OES_framebuffer_object`, renderer/version, then `REFUSED host:no-context`. The earlier emulator GLES1 FBO gap is a precedent for this check, not evidence about Tensor. |
| `gles-host.c`, `gles_host_init` | After the FBO check, unconditionally allocates `DEPTH24_STENCIL8` and attaches the same renderbuffer to depth/stencil. No separate packed-depth capability test or depth16/stencil fallback is selected. | Generic `FBO incomplete` before host-up is compatible with a rejected attachment/format, but does not identify which operation failed. The actual context extension list and per-stage GL error/status would be needed if this branch occurs. Do not remove depth/stencil to manufacture a pass. |
| `gles_host_call`, `gles_check_draw` | Each guest operation checks make-current; strict error attribution is enabled by default. Draw/slot errors are logged/refusal-counted, and not every logged GL error changes the guest operation's return value. | Preserve `make-current failed`, `REFUSED host:make-current`, `slot N raised GL error`, `DRAWING INTO INCOMPLETE framebuffer` and their order/arguments. A live guest may continue submitting calls while its native rendering fails. |
| `gles_surface_writeback` | Attaches the rendered texture to a scratch FBO and reads pixels, then copies rows/pages to guest memory. A GL error yields `REFUSED surface:writeback`, clears this surface's dirty flag and returns failure; it requires a later draw to become dirty again. | `CA copied`/`CA flush` metrics, their base/format/stride/API and RGB/alpha values, plus refusals. A failed writeback can drop a CA frame without killing the host. This is a possible failure path, not an observed Pixel cause. |

The EGL mask semantics are specified by
[Khronos eglChooseConfig](https://chromium.googlesource.com/external/github.com/KhronosGroup/EGL-Registry/+/refs/heads/upstream/main/sdk/docs/man/xhtml/eglChooseConfig.html).
The packed renderbuffer format is introduced by
[OES_packed_depth_stencil](https://registry.khronos.org/OpenGL/extensions/OES/OES_packed_depth_stencil.txt),
separately from
[OES_framebuffer_object](https://registry.khronos.org/OpenGL/extensions/OES/OES_framebuffer_object.txt).
These establish compatibility requirements, not the physical device's extensions.

## Separate guest rendering from display sleep and Android presentation

The n72 guest's software boot/logo path can publish before its hooked
OpenGLES/CoreAnimation work. Repeated logo pixels or publisher increments do not
prove the native GLES1 host initialized. Normal CA rendering is often offscreen:
absence of an explicit drawable/panel-present call alone is not a fault. Follow
the full boundary from guest context/draw, through CA guest-memory writeback,
to LCD scanout publication and the Android presenter's copied cache/window post.

`Native.capture()` captures the presenter's cached pixels; it is not an
independent fresh read of the guest framebuffer or GPU. Pair every captured PNG
with copied `guestFrameSequence`/timestamp, current producer sequence/timestamp,
native publication age and actual device Surface PNG. A black or logo image from
an aged cache cannot identify the current guest scene. When the same awake frame
is static, coherent positive copied/producer identity is sufficient; perpetual
counter growth is not required.

The current renderer's surface-page arithmetic uses guest `TARGET_PAGE_SIZE`
and QEMU RAM offsets. It does not mmap these GL surfaces with a hardcoded4KiB
host-page assumption. That inspection does not qualify all ARM64/16KiB TCG,
allocator or driver behavior. APK/ELF alignment is only one prerequisite. Keep
the separate upstream97860/backend investigation out of this renderer audit.

## First physical-ADB receipt, without changing the failed session

Bind actual APK/source/engine, device build/ABI, actual `getconf PAGESIZE`, app
UID, worker PID/start ticks, selected record lock/package and its dirty state.
Preserve current engine/serial logs, accessible own-process exit evidence and
the before-action status/observations. Do not wipe, restart or switch provider
before preserving the failed state. Accessible logcat/exit data must distinguish
a new host PID, an explicit guest reboot and a display sleep transition; repeated
logo alone distinguishes none of them.

Retain a short time series of produced/copied/presented identity and timestamps;
backlight/sleep state; windowAttached/generation/epoch; presenter phase, phase
entry time and stable-phase marker; window lock/post/geometry/copy failures and
their first/last errors. Pair current raw-cache and Surface PNGs with those
samples and inspect them. Producer advance with an aged copied cache or stalled
window phase localizes a different boundary from fresh black scanout while the
guest reports sleeping. CPU/PSS samples alone localize neither.

Collect the automatic EGL/strict-error logs first. Fixed copied-pixel metrics
must be armed through the existing closed debug path **before a new unused
worker starts**; do not pretend to arm them retroactively. If initialization
fails with only generic FBO-incomplete output, the next narrow source diagnostic
would need the actual production-context extension/capability and staged FBO
status/error receipt. It is not implemented or justified as a Pixel fix yet.
No arbitrary driver paths, ARM ANGLE rollout, pixel replacement, removed
acceptance gate or performance claim follows from this audit.
