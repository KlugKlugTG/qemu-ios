# Graphics evidence beyond adapter probes

The frozen production adapter probes exercise real GL commands, pixels and
resource state; they do not execute an emulated guest. Keep their manifests and
results separate from guest boot/presentation results. A mock capability test is
an error/state check only. Screenshots of Android's own boot/home screen are not
guest rendering evidence.

## Remaining guest evidence

For the baseline n72ap-7E18 route, retain the backend commit and exact engine
library hash, frontend APK/worker hash, admitted fixture/guest-tool revisions,
actual host page size, renderer/version and worker UID/PID. The normal app
`:engine` process must execute the real guest graphics protocol and publish
actual visible guest frames through the copying API. The engine's initialization
log identifies the requested guest API with the actual host renderer; this is
context evidence, not proof that a shader workload was rendered.

Observe a lit guest Lock Screen/Home and an interaction that visibly changes
its pixels. Keep a copied guest screenshot and an Android Surface screenshot at
corresponding frame sequences/times and inspect both. A frame counter or black
image is insufficient: retain backlight/frontmost state to distinguish sleep,
failed rendering and actual guest shutdown. Repeat after pause/resume, Surface
replacement/activity recreation, orderly stop and a fresh worker restart.
The real guest's required multi-contact input and concurrent render/audio
behavior remain part of integration validation; gyro is deferred/unqualified.

For a black or transparent image, set `IT_GLES_FRAME_METRICS=1` before starting
the engine in a bounded diagnostic run. The first successfully copied frame
and every sixtieth frame report their actual drawable FBO, guest destination
base, copy path, dimensions/stride, source format, exact nonzero RGB/alpha and
opaque pixel counts, mean RGBA and canonical RGBA hash. RGB565 has implicit
alpha, explicitly identified in the log. Metrics read only the buffer actually
copied, exclude padding and use the accepted dimensions. They make no GL calls,
consume no GL errors and modify no pixels. Existing verbose/profiling runs also
include these bounded metrics.

Match those lines to the real guest's copied frame and Surface frame, retaining
LCD scanout/base, backlight/frontmost state and command errors. Nonzero RGB with
zero alpha is different evidence from black RGB with opaque alpha. These metrics
describe copied bytes; they do not assert successful GL readback, CA composition
or visible presentation. Check the real command/error receipt as well. The old
progress mean sampled the reusable readback as a full RGBA drawable even for
RGB565, cropped or direct IOSurface copies; that sample has been removed.

## Surface lifecycle and black-cache source checks

The Android adapter and the legacy iOS foreground gate are separate paths:

| Operation | Actual source behavior |
| --- | --- |
| Android `clearSurface` | Cancels contacts and removes the JNI `ANativeWindow` under its guard; does not call the graphics foreground setter or pause the VM. |
| Android Surface attachment | Retains a new native window, increments its generation and resets the copied-frame sequence to zero, so an unchanged cached engine frame can be replayed. |
| Portable pause/resume | Cancels input and invokes `qemu_ios_ui_pause`/`qemu_ios_ui_resume`, which schedule QMP stop/continue; the Portable engine does not load the legacy foreground setter. |
| Legacy `qemu_ios_set_foreground` | Writes `gles_host_set_allowed` and schedules a resume on foreground return. The renderer gate rejects guest GL calls while false and does not itself clear buffers or repaint on return. |

The Android service's `foreground()` function posts its notification through
`startForeground`; it is not a GL state transition. Android sources do not
invoke `qemu_ios_set_foreground`. Consequently an Android stale Surface clear
cannot directly leave the renderer's foreground flag false. The inspected
Surface attach/clear interface has no owning view token, so a late old-view
destroy callback could structurally remove a newer window. Actual callback
ordering and a product-side ownership fix need separate evidence. That risk
removes presentation; it does not change the engine's cached frame pixels.

The engine's `qemu_ios_portable_frame_copy` returns the latest completed panel
buffer under the frame lock when its serial differs from the caller's. The
Android presenter requests serial zero on attachment and on failed window
lock/post, allowing retry of the same engine frame. `ios_capture` copies QEMU's
real panel Surface and deduplicates byte-identical frames; it applies no
foreground or backlight mask. A QEMU console resize marks capture pending until
the graphics refresh completes, avoiding publication of a newly allocated
blank Surface before the LCD paints it.

The iPod LCD model applies backlight scaling during conversion. In
`lcd_refresh`, the duplicate-poll skip requires the current brightness to equal
`last_bright`. A changed brightness therefore bypasses that skip and sets full
invalidation; a changed scanout base or QEMU Surface also invalidates. Rotated
and composed paths redraw complete frames. Re-enabling the modeled brightness
can thus reconvert unchanged scanout RAM without requiring a fresh guest GL
draw. If the programmed brightness remains zero, a black output is expected
even when the guest agent is alive and reports SpringBoard/Lock Screen.

These are source checks, not a reproduced lifecycle failure or a target pass.
For an actual black-frame attempt, retain activity/Surface generation and
window-post receipts alongside VM pause state, scanout/base, PMU/backlight
writes, cached-frame serial/hash and the real copied/presented images. An
unchanged black frame needs those observations to distinguish panel power,
black scanout RAM and a detached presentation window.

GLES2 needs an actual admitted guest/workload that compiles and executes shader
commands, not the standalone host GLES2 triangle. The supplied n81 route may be
used where appropriate with its prepared kernel/NAND/NOR and matching package;
do not request an arbitrary ROM or copy the large fixture merely for this test.
Record API2 context and successful shader/link/draw behavior with actual pixels.
A short bounded `IT_GLES_CALL_TRACE`/existing trace event budget can help identify
protocol calls; keep debug traces private and avoid unbounded call logging in
performance runs. Do not infer GLES2 guest compatibility from an ES1 baseline.

Live native EGL state blocks graphics snapshots. Show snapshots as unavailable;
validate cold restart in a new worker instead. A screenshot is not a snapshot.

## Android packaging and presentation requirements

The frontend loads `lighttouch_jni` in a same-app-UID `:engine` service. Its CMake
imports the real Bionic engine dependency closure; the NDK runtime is supplied
through AGP. The engine must select `gles-host.c` with `GLES_HOST_EGL` and
`GLES_HOST_EGL_ES`, link platform EGL/GLESv1_CM/GLESv2, and exclude the graphics
stub. Inspect every ABI's dependency closure rather than copying Linux glibc
libraries into the APK. Native EGL/GLES calls occur on the engine thread; the
frontend's CPU Surface upload does not replace guest GPU execution.

The ABI publishes copied BGRA8888 frames, top-left origin and byte stride. The
JNI presentation/capture paths convert BGRA to Android RGBA and use the actual
source/window stride. Compare red and blue pixels, orientation and letterboxing
against the copied guest frame; a uniformly green probe does not detect channel
swaps or vertical inversion. Surface replacement must not use an old window or
stale pixels, and restart must reset frame/resource generations.

The current frontend pins AGP8.9.2/NDK28.2 and requests uncompressed JNI packaging.
Android's [16 KiB guidance](https://developer.android.com/guide/practices/page-sizes)
requires compatible ELF segments and ZIP alignment, including prebuilt shared
libraries. Verify the final signed APK, both ABI closures and every dependency's
LOAD alignment; compiler flags on the JNI wrapper alone are insufficient. Use
[zipalign's check mode](https://developer.android.com/tools/zipalign):

```sh
zipalign -c -P 16 -v 4 FINAL_SIGNED_APK.apk
llvm-readelf -l -d EXTRACTED_SHARED_LIBRARY.so
```

Retain a package report with APK/library hashes and source/build revisions, then
load and run the actual engine in the packaged app on the physical arm64 phone,
4 KiB x86_64 AVD and 16 KiB x86_64 AVD. The native AVD GLES1 translator failed
required FBO or texture-stage behavior. The pinned ANGLE frontend now passes
full shell probes on both sizes using host/Mesa backing, which requires an
isolated X display; inspected Android UI captures still failed usability.
Actual packaged-worker validation needs an explicit supported GLES3 backing
driver and real guest frames. The scoped probe
runner verifies frozen hashes and final markers on an already allocated AVD;
root still owns allocation, package install, phone access and integration runs.
