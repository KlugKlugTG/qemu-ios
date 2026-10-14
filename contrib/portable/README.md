# Portable QEMU embedding build

This lane preserves `qemu_ios` API 2.2 and the original macOS sources and
build-support dependency pins. The additive portable ABI is version 1 in
`contrib/ios-app/qemu-ios-ui.h`. A library can initialize QEMU **once**; every
restart requires a fresh worker process. The host must finish worker shutdown
before freeing callbacks, frame buffers, or audio resources.

Use private build outputs outside the checkout. These scripts never read or
stage firmware, ROMs, guest assets, or credentials. Dependencies are downloaded
from their official release sources and SHA-256 checked against
`dependencies.json`. FFmpeg 9.0.1 retains the macOS version/hash and both
existing H.264 patches. Other dependencies add portable build inputs without
changing the macOS pin ledger. Android builds use NDK 28.2.13676358, API 28 or
newer, static PIC third-party dependencies, and 16 KiB ELF maximum page size.
The platform provides zlib; its metadata version is read from the NDK header.

```sh
# MESON is a Meson >=1.5 executable (QEMU's pyvenv supplies one).
python3 contrib/portable/build-deps.py /private/builds/deps-linux --only ffmpeg
python3 contrib/portable/build-engine.py /private/builds/linux \
  --prefix /private/builds/deps-linux/prefix

python3 contrib/portable/build-deps.py /private/builds/deps-arm64 \
  --abi arm64-v8a --ndk "$ANDROID_NDK_ROOT" --meson "$MESON"
python3 contrib/portable/build-engine.py /private/builds/arm64-v8a \
  --abi arm64-v8a --ndk "$ANDROID_NDK_ROOT" \
  --prefix /private/builds/deps-arm64/prefix
# Repeat both commands with --abi x86_64 and distinct output/prefix paths.
```

Each compiler stage uses at most two jobs. The shared library relinker uses
Ninja's real link arguments and compile configuration, retaining its response
file, replacing only `system/main.c` with the existing iOS entry/display bridge
and the macOS controls/audio implementation. The latter is generated privately
with its Mach-O image identity replaced by a compiled Git revision; its pause
handler also cancels live host input. ELF exports only `qemu_ios_*`. Inspect
`readelf -d libqemu-arm.so` before packaging, including any NDK C++ runtime.
`portable-link.json` records the exact final link arguments.

`portable.mak` selects the existing iOS models plus ARM virt for legal TCG
smoke programs. Device prerequisites are explicitly selected in Kconfig, as in
the audited Windows reference; default upstream configurations remain present.
Linux graphics use EGL compatibility OpenGL. Android uses EGL and native GLES1
and GLES2 selected from the compiler's `__ANDROID__` marker. A graphics-stub
build never establishes port acceptance. The backend version and separate
verified guest-asset version must be included in each run's manifest.

The portable frame operation copies tightly packed BGRA while holding the
publisher mutex. Caller storage remains valid across subsequent frames. A
small buffer returns dimensions/required bytes without acknowledging delivery.
Touch batches preserve full 32-bit host contact IDs, validate the whole batch
before any injection, map active IDs to five existing controller slots, emit
coordinates before commits, and close with one sync. Eight is the ABI batch
bound, not the emulated controller contact limit. Poll the newest ID to confirm
actual delivery; `QUEUED` is only queue admission. Cancel releases touch and
buttons even while paused and resets live motion. Held contacts/live samples
are never migrated or retained across reset/worker restart.

Motion samples use guest portrait axes x-right, y-up, z-toward-viewer.
Acceleration is Android-style specific force including gravity in m/s²; it is
negated once at the existing LIS model's gravity-vector boundary, then the
existing board sensor mount/full-scale conversion supplies guest registers.
Gyroscope values are rad/s in the same portrait frame; the existing L3G model
applies its mount and CTRL_REG4 sensitivity. Accel and gyro samples expire
independently after 250 ms of virtual time. Pause cancels them. n72 has an
accelerometer and no gyro; n81 and n90 advertise their existing L3G models.
These capability facts are model support, not proof of an accepted guest run.

Focused host checks (ASan/UBSan, actual production functions):

```sh
tests/slice/run.sh tests/slice/ipod-portable-frame.c
tests/slice/run.sh tests/slice/ipod-portable-touch.c
tests/slice/run.sh tests/slice/ipod-accel-sampling.c
tests/slice/run.sh tests/slice/ipod-attitude.c
tests/slice/run.sh tests/slice/ipod-multitouch-frames.c
```

These checks establish frame ownership, stable contact identity/crossing,
atomic rejection, paused release, sensor units/mounts/expiry/restore behavior,
and controller wire checksums. They do not establish actual guest reception,
physical gyroscope acceptance, ARM64 JIT qualification, or guest graphics.

QEMU and reused glue retain upstream GPL terms. GLib is LGPL-2.1-or-later,
proxy-libintl retains its GNU Library GPL v2 notice, PCRE2 BSD, libffi MIT,
pixman MIT, OpenSSL Apache-2.0, and the restricted FFmpeg configuration LGPL.
Retain upstream source/license notices and provide the corresponding source,
patches, and reproducible build/relink material when distributing engine
binaries; LGPL static dependencies also require their applicable relinking
material. The source manifest is `dependencies.json`, not permission to
redistribute proprietary guest material.
