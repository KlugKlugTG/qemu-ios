/*
 * Focused tests for the host-camera conversion helpers used by s5l8930_isp.c.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <glib.h>
#include "hw/arm/s5l8930-isp-camera.h"

static void test_nv12_passthrough(void)
{
    const uint8_t input[] = { 16, 32, 48, 64, 128, 192 };
    uint8_t output[sizeof(input)] = { 0 };

    s5l8930_isp_camera_convert(input, 2, 2, output, 2, 2, true);
    g_assert_cmpmem(output, sizeof(output), input, sizeof(input));
}

static void test_nv12_to_yuyv(void)
{
    const uint8_t input[] = { 16, 32, 48, 64, 128, 192 };
    const uint8_t expected[] = { 16, 128, 32, 192, 48, 128, 64, 192 };
    uint8_t output[sizeof(expected)] = { 0 };

    s5l8930_isp_camera_convert(input, 2, 2, output, 2, 2, false);
    g_assert_cmpmem(output, sizeof(output), expected, sizeof(expected));
}

static void test_scale_nearest(void)
{
    const uint8_t input[] = { 10, 20, 30, 40, 50, 60 };
    const uint8_t expected[] = {
        10, 10, 20, 20,
        10, 10, 20, 20,
        30, 30, 40, 40,
        30, 30, 40, 40,
        50, 60, 50, 60,
        50, 60, 50, 60,
    };
    uint8_t output[sizeof(expected)] = { 0 };

    s5l8930_isp_camera_convert(input, 2, 2, output, 4, 4, true);
    g_assert_cmpmem(output, sizeof(output), expected, sizeof(expected));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/s5l8930-isp-camera/nv12-passthrough", test_nv12_passthrough);
    g_test_add_func("/s5l8930-isp-camera/nv12-to-yuyv", test_nv12_to_yuyv);
    g_test_add_func("/s5l8930-isp-camera/scale-nearest", test_scale_nearest);
    return g_test_run();
}
