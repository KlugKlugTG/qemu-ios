/*
 * Pure conversion helpers for the A4 ISP's host-camera input path.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_ARM_S5L8930_ISP_CAMERA_H
#define HW_ARM_S5L8930_ISP_CAMERA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline unsigned s5l8930_isp_scale_nearest(unsigned source, unsigned destination,
                                                  unsigned position)
{
    unsigned value = (unsigned)(((uint64_t)(2 * position + 1) * source) /
                                (2 * destination));

    return value < source ? value : source - 1;
}

/* Source and destination are tightly packed NV12 and packed Y0 Cb Y1 Cr
 * ('yuvs') respectively. All dimensions are positive, even and <= 4096. */
static inline void s5l8930_isp_camera_convert(const uint8_t *source,
                                              unsigned source_width,
                                              unsigned source_height,
                                              uint8_t *destination,
                                              unsigned width,
                                              unsigned height,
                                              bool nv12)
{
    const uint8_t *source_uv = source + (size_t)source_width * source_height;

    if (nv12) {
        for (unsigned y = 0; y < height; y++) {
            unsigned sy = s5l8930_isp_scale_nearest(source_height, height, y);

            for (unsigned x = 0; x < width; x++) {
                unsigned sx = s5l8930_isp_scale_nearest(source_width, width, x);

                destination[(size_t)y * width + x] = source[(size_t)sy * source_width + sx];
            }
        }
        uint8_t *destination_uv = destination + (size_t)width * height;

        for (unsigned y = 0; y < height / 2; y++) {
            unsigned sy = s5l8930_isp_scale_nearest(source_height / 2, height / 2, y);

            for (unsigned x = 0; x < width; x += 2) {
                unsigned sx = 2 * s5l8930_isp_scale_nearest(source_width / 2, width / 2, x / 2);
                size_t src = (size_t)sy * source_width + sx;
                size_t dst = (size_t)y * width + x;

                destination_uv[dst] = source_uv[src];
                destination_uv[dst + 1] = source_uv[src + 1];
            }
        }
    } else {
        for (unsigned y = 0; y < height; y++) {
            unsigned sy = s5l8930_isp_scale_nearest(source_height, height, y);
            unsigned cy = s5l8930_isp_scale_nearest(source_height / 2, height, y);

            for (unsigned x = 0; x < width; x += 2) {
                unsigned sx0 = s5l8930_isp_scale_nearest(source_width, width, x);
                unsigned sx1 = s5l8930_isp_scale_nearest(source_width, width, x + 1);
                unsigned cx = 2 * s5l8930_isp_scale_nearest(source_width / 2, width / 2, x / 2);
                size_t src_uv = (size_t)cy * source_width + cx;
                size_t src_y = (size_t)sy * source_width;
                size_t dst = ((size_t)y * width + x) * 2;

                destination[dst] = source[src_y + sx0];
                destination[dst + 1] = source_uv[src_uv];
                destination[dst + 2] = source[src_y + sx1];
                destination[dst + 3] = source_uv[src_uv + 1];
            }
        }
    }
}

#endif
