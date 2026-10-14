#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
cd "$(dirname "$0")/../.."
output=${1:?Usage: tests/graphics/check.sh OUTPUT_DIRECTORY [NDK_DIRECTORY]}
mkdir -p "$output"
cc -Wall -Wextra tests/graphics/egl-render.c -o "$output/egl-desktop" $(pkg-config --cflags --libs egl gl)
LIBGL_ALWAYS_SOFTWARE=1 "$output/egl-desktop"
cc -Wall -Wextra -Wno-unused-function -DGLES_HOST_EGL_ES tests/graphics/egl-pixels.c -o "$output/egl-pixels" $(pkg-config --cflags --libs egl glesv2 glesv1_cm)
LIBGL_ALWAYS_SOFTWARE=1 "$output/egl-pixels"
if [ "$#" -gt 1 ]; then
    ndk=$2
    for arch in aarch64 x86_64; do
        for probe in render pixels; do
            "$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/$arch-linux-android26-clang" \
                -Wall -Wextra -Wno-unused-function -DGLES_HOST_EGL_ES \
                "tests/graphics/egl-$probe.c" -o "$output/egl-$probe-$arch" \
                -lEGL -lGLESv2 -lGLESv1_CM -Wl,-z,max-page-size=16384
        done
    done
fi
