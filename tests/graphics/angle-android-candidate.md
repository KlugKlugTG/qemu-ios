# Pinned Android ANGLE candidate

This is a proposed, unbuilt fallback. The physical Poco native path remains the
default. Do not qualify ANGLE until its actual Bionic libraries execute the full
adapter probes and required guest workloads in the packaged worker on both AVD
page sizes.

## Why a separate frontend is needed

Host units05 validated four usable GLES1 units and a red triangle, then the
complete production probe returned black for blue-plus-red two-unit ADD, with
no GL error. Upstream gfxstream
[core-profile state](https://github.com/google/gfxstream/blob/07ee40efb0e7037a9a9b7fe59071e7c4997e7cbe/host/gl/glestranslator/gles_cm/core_profile_engine.cpp)
selects one sampler using the current active texture unit. Its
[fragment shader](https://github.com/google/gfxstream/blob/07ee40efb0e7037a9a9b7fe59071e7c4997e7cbe/host/gl/glestranslator/gles_cm/core_profile_engine_shaders.h)
handles replace/modulate and treats combine/default as modulate. This suggests
missing multi-stage semantics; the raw `egl-combine` diagnostic isolates that
hypothesis without the portability wrappers or private object names.

The emulator's `-gpu swangle` changes the host backing renderer while retaining
that GLES1 translator. A fallback needs ANGLE's GLES1 frontend inside the
Android worker, not merely a different host backing driver.

## Official support and chosen pin

On 2026-10-14, official ANGLE main was
`b07d4a63246a825d1f87a25501773bc8e346ff87`. Its
[ES1 status](https://chromium.googlesource.com/angle/angle/+/b07d4a63246a825d1f87a25501773bc8e346ff87/doc/ES1Status.md)
and the existing evaluated pin
`8cd050f07ebd65cce269fc92e36699fc03b20ac3` both describe GLES1 implemented
through GLES3 features and passing the GLES1 MustPass suite. Known missing
features include smooth lines, two-sided lighting and optional matrix palette;
paletted texture emulation depends on the Vulkan backend. These caveats need
actual workload assessment, not a claim of universal parity.

The pinned [GLES1 shader implementation](https://chromium.googlesource.com/angle/angle/+/8cd050f07ebd65cce269fc92e36699fc03b20ac3/src/libANGLE/GLES1Shaders.inc)
processes texture stages and implements ADD/COMBINE. This is a source rationale
for testing the candidate, not a substitute for real output.

## Reproducible build candidate

`build-angle-android.sh` uses the existing ANGLE pin and depot_tools pin
`9b264039190fa270f3fc44779b4e26b43008ba41`, checks their identities, syncs
Android dependencies, limits compilation to two jobs and builds `libEGL`,
`libGLESv1_CM` and `libGLESv2`. Start with x64 and the GLES backing renderer:

```sh
bash tests/graphics/build-angle-android.sh NEW_PRIVATE_DIRECTORY x64 gl
```

A separate Vulkan build is supported by the candidate script if the GLES
backend proves insufficient. Source acquisition can run independently without
hooks or compilation, then resume from the verified checkout after the heavy
slot is allocated:

```sh
bash tests/graphics/build-angle-android.sh --fetch-only NEW_PRIVATE_DIRECTORY x64 gl
bash tests/graphics/build-angle-android.sh --build-only SAME_PRIVATE_DIRECTORY x64 gl
```

The fetch phase records actual dependency revisions in `source-receipt.txt`.
Use `angle-license-inventory.py --checkout SAME_PRIVATE_DIRECTORY --output
NEW_PRIVATE_INVENTORY.json` to record retained license files, notices and
Chromium dependency metadata with hashes. Preserve this checkout as the
corresponding-source input and inspect the libraries' actual linked dependency
closure before distribution. The inventory does not decide license
compatibility. Source acquisition is underway; GN generation, dependency hooks
and linking remain unverified. Public Android dependencies and build outputs
require substantial disk space; keep them outside Git.

The [official Android setup](https://chromium.googlesource.com/angle/angle/+/8cd050f07ebd65cce269fc92e36699fc03b20ac3/doc/DevSetupAndroid.md)
requires Linux and gclient Android dependencies. The selected pin's build
revision `e60c669a94848d832d714025154f2ba1c37450df`
[Android link config](https://chromium.googlesource.com/chromium/src/build/+/e60c669a94848d832d714025154f2ba1c37450df/config/android/BUILD.gn)
sets 16 KiB max-page-size for 64-bit CPUs. Inspect actual ELF LOAD alignment and
the complete dependency closure anyway; the build receipt records revisions,
GN arguments and library hashes.

## Bionic worker integration and validation

Use suffixed `libEGL_lighttouch_angle.so`, `libGLESv1_CM_lighttouch_angle.so`
and `libGLESv2_lighttouch_angle.so` in the app's ABI directory. Packaging a suffix
changes filenames, not public GL symbol names. Route EGL and GL entry points
through handles/function pointers belonging to the selected runtime; avoid
mixing platform EGL contexts with ANGLE GL or relying on linker ordering after
Java preloads system graphics libraries. All guest contexts in one share group
must use the same runtime. Select before creating live guest resources; preserve
engine-thread ownership and fresh-process restart.

Initially build only the x86_64 candidate so arm64/Poco continues to use its
proven platform renderer. Use public EGL/GLES APIs; do not rely on ANGLE's private
prefixed entry-point ABI. The existing shared guest executor, pixel/rectangle
adaptation and fences remain in use. The native Android worker loads packaged
libraries through JNI; do not execute a downloaded tool in writable app storage.

The preprocessed native executor currently calls 12 public EGL functions and
169 public GL functions; 15 GLES1 OES functions are resolved dynamically.
`native-entrypoints.json` records this source inventory, and
`inventory-native-entrypoints.py` regenerates it using an existing full-executor
compile command. It excludes header declarations and desktop-only branches.
It proves the call boundary, not that a runtime implements the calls:

```sh
python3 tests/graphics/inventory-native-entrypoints.py \
  --compile-commands EXISTING_BUILD/compile_commands.json \
  --renderer hw/arm/gles-host.c \
  --check tests/graphics/native-entrypoints.json
```

The loader must maintain separate API1/API2 function tables: shared names such
as `glGetString`, `glDrawArrays` and `glBindTexture` must resolve through the
selected API's library, while fixed-function calls use GLESv1_CM and shader
calls use GLESv2. Resolve table addresses before redefining raw calls, then
include dispatch beneath the existing portability wrappers so wrapper-internal
calls also use the selected provider. Core framebuffer calls remain API2-only;
API1 OES addresses come from that provider's `eglGetProcAddress` and retain the
existing advertised-extension checks. A missing required address must fail
renderer setup explicitly. Keep all three library handles alive until the last
context is released, and require a fresh worker to change providers.

Public-table dispatch prevents a direct call from accidentally reaching system
EGL/GLES, but transitive symbol binding inside the candidate libraries still
needs inspection. Record each object's `DT_NEEDED`, exported symbols and the
actual library owner of resolved addresses; do not rely on a filename suffix or
assume `RTLD_LOCAL` alone prevents interposition. Validate in the app worker after
system graphics libraries have loaded, including cross-version texture sharing
and teardown. This loader is a plan; no production ANGLE dispatch has been added.

Choose the backing renderer explicitly through ANGLE's public
`eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, ...)`, with
`EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE` for the first candidate or
`EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE` for a separately built alternate.
These calls and attribute constants are defined in the pin's
[public EGL extension header](https://chromium.googlesource.com/angle/angle/+/8cd050f07ebd65cce269fc92e36699fc03b20ac3/include/EGL/eglext_angle.h).
The Android GLES backing path loads `/system/lib64/libEGL.so` in
[DisplayEGL](https://chromium.googlesource.com/angle/angle/+/8cd050f07ebd65cce269fc92e36699fc03b20ac3/src/libANGLE/renderer/gl/egl/DisplayEGL.cpp),
so the suffixed frontend must remain distinct from that platform runtime.
Log the selected frontend and backing renderer and fail on unsupported
selection rather than silently changing providers. The current inventory
describes the existing native path; this planned display-selection entry point
would extend it.

First run raw combine and complete production probes using the candidate's own
EGL/GLES dispatch, including API1/API2 context sharing, object lifetime, DrawTex,
fog, rectangle matrix order, packed formats, shader limits and readback. Then
execute the real baseline guest and required GLES2 workload in the app UID,
compare copied/presented frames, and test touch/audio/lifecycle concurrently.
Repeat on 4 KiB and 16 KiB AVDs with source/fixture/package receipts. Keep snapshots
unavailable and require fresh-worker cold restart. Any required feature gap
must be fixed or recorded as a failed gate; do not weaken the workload or replace
its pixels.
