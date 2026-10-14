# Portable QEMU embedding build

This lane preserves `qemu_ios` API 2.2 and the original macOS sources and
build-support dependency pins. The additive portable ABI is version 1 in
`contrib/ios-app/qemu-ios-ui.h`. A library can initialize QEMU **once**; every
restart requires a fresh worker process. The host must finish worker shutdown
before freeing callbacks, frame buffers, or audio resources.

Use fresh private build outputs outside the checkout. Input stamps record ABI,
compiler, dependency/archive and patch hashes, recipe hashes, tool versions,
and options. A changed or unstamped compiled cache is refused; choose a fresh
output instead of deleting the stamp. Engine builds require a clean committed
source tree. The shared library manifest hashes every tracked source and
selected object/archive/shared-library input, including renderer/device code. These scripts never read or
stage firmware, ROMs, guest assets, or credentials. Dependencies are downloaded
from their official release sources and SHA-256 checked against
`dependencies.json`. FFmpeg 9.0.1 retains the macOS version/hash and both
existing H.264 patches. Other dependencies add portable build inputs without
changing the macOS pin ledger. Android builds use NDK 28.2.13676358, API 28 or
newer, static PIC third-party dependencies, and 16 KiB ELF maximum page size.
The platform provides zlib; its metadata version is read from the NDK header.

```sh
# MESON is Meson 1.10.0; its Python needs packaging 24.0.
# Install host tools in a private venv when unavailable, never change system Python.
python3 contrib/portable/build-deps.py /private/builds/deps-linux --only ffmpeg
python3 contrib/portable/build-crypto-deps.py /private/builds/crypto-linux
python3 contrib/portable/build-engine.py /private/builds/linux \
  --prefix /private/builds/deps-linux/prefix \
  --crypto-prefix /private/builds/crypto-linux/prefix

python3 contrib/portable/build-deps.py /private/builds/deps-arm64 \
  --abi arm64-v8a --ndk "$ANDROID_NDK_ROOT" --meson "$MESON"
python3 contrib/portable/build-crypto-deps.py /private/builds/crypto-arm64 \
  --abi arm64-v8a --ndk "$ANDROID_NDK_ROOT"
python3 contrib/portable/build-engine.py /private/builds/arm64-v8a \
  --abi arm64-v8a --ndk "$ANDROID_NDK_ROOT" \
  --prefix /private/builds/deps-arm64/prefix \
  --crypto-prefix /private/builds/crypto-arm64/prefix
# Repeat all three commands with --abi x86_64 and distinct output/prefix paths.
```

Each compiler stage uses at most two jobs. The shared library relinker uses
Ninja's real link arguments and compile configuration, retaining its response
file, replacing only `system/main.c` with the existing iOS entry/display bridge
and the macOS controls/audio implementation. The latter is generated privately
with its Mach-O image identity replaced by a compiled Git revision; its pause
handler also cancels live host input. ELF exports only `qemu_ios_*`. Inspect
`readelf -d libqemu-arm.so` before packaging, including any NDK C++ runtime.
`portable-link.json` records the exact final link arguments.
`audit-selected-sources.py BUILD` inventories linked source notices and Ninja's
actual included headers against the immutable manifest revision. It reports
SPDX terms and text review candidates, including missing or ambiguous notices;
it does not give a legal verdict. The shared manifest also resolves and hashes
libraries named through `-l` flags.

The portable recipe selects QEMU's existing public-domain AES implementation
for the Apple AES model and pinned libgcrypt MPI for its signed Montgomery PKE
operations. The original macOS OpenSSL implementation remains selected without
`QEMU_IOS_PORTABLE_CRYPTO`. Separate crypto prefixes avoid changing OpenSSL
inputs used by the firmware tool. `crypto-dependencies.json` pins release
archives, and the builder retains their COPYING/COPYING.LIB/LICENSES notices.
The libgpg-error patch generates mutex storage from the target Bionic compiler
instead of borrowing a glibc layout. QEMU's existing libgcrypt initialization
also supplies its shared cipher/hash backend. Unused virtio IOThread mapping is
selected only when virtio block or SCSI is present; neither is selected by the
portable device config.

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
tests/slice/run.sh tests/slice/ipod-pke.c
tests/slice/run.sh tests/slice/ipod-portable-pke.c
python3 tests/portable-crypto.py /private/builds/linux
python3 tests/portable-models.py /private/builds/linux/engine-run \
  /private/builds/linux/libqemu-arm.so
```

These checks establish frame ownership, stable contact identity/crossing,
atomic rejection, paused release, sensor units/mounts/expiry/restore behavior,
and controller wire checksums. They do not establish actual guest reception,
physical gyroscope acceptance, ARM64 JIT qualification, or guest graphics.
The whole n72 model check uses only a legal ARM loop, erased NOR and an empty
NAND directory. It verifies actual board initialization and required registered
peripherals before packaging, including the UNIMP dependency missed by a
virt-only CPU smoke. It is separate from actual guest boot acceptance.

QEMU and reused glue retain upstream GPL terms. GLib is LGPL-2.1-or-later,
proxy-libintl retains its GNU Library GPL v2 notice, PCRE2 BSD, libffi MIT,
pixman MIT, libgcrypt/libgpg-error LGPL-2.1-or-later library terms, OpenSSL
Apache-2.0 for the separate firmware dependency, and the restricted FFmpeg
configuration LGPL. Distribution eligibility remains unresolved pending the
actual selected-source and complete application dependency audit. Vector tests
and source changes alone do not qualify a rebuilt engine or resolve licensing.
Retain upstream source/license notices and provide the corresponding source,
patches, and reproducible build/relink material when distributing engine
binaries; LGPL static dependencies also require their applicable relinking
material. The source manifest is `dependencies.json`, not permission to
redistribute proprietary guest material.

For an existing completed engine, add `--source-update` to the same engine
command after committing a source change. This explicitly permits a source
revision change while keeping ABI, configure arguments/macros, compiler/tools,
environment, NDK and dependency inputs unchanged. Configure and Ninja then
rebuild changed sources and included headers. A reviewed migration accepts the
exact earlier portable recipe once, checking its completed compiler/Ninja and
Meson baseline; subsequent unknown recipe changes still fail. The update
history records the prior library/manifest and requested/compiled revisions.
The new shared manifest records each reused object's earlier recorded engine
revision/hash and each changed object's current engine revision/hash. Earlier
manifests are retained under `BUILD/manifests` by content hash. Changes to configuration or pins
still require a separate output directory. An interrupted update's requested
revision is distinct from the last completed library manifest.
