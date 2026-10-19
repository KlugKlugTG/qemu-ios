/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QEMU_IOS_FRAME_PATH_H
#define QEMU_IOS_FRAME_PATH_H

/* Optional embedding diagnostic; frame boundaries and selected focused paths. */
enum IOSFramePathEvent {
    IOS_FP_PRESENT_BEGIN = 1,
    IOS_FP_PRESENT_END,
    IOS_FP_LCD_FLIP,
    IOS_FP_VSYNC_BEGIN,
    IOS_FP_VSYNC_END,
    IOS_FP_SCAN_BEGIN,
    IOS_FP_SCAN_END,
    IOS_FP_CAPTURE_BEGIN,
    IOS_FP_CAPTURE_SAME,
    IOS_FP_PUBLISH,
    IOS_FP_CONTEXT_SWITCH,
    IOS_FP_TTBR0,
    IOS_FP_VFP_SAMPLE,
    IOS_FP_VFP_RESULT,
    IOS_FP_ALIGNMENT,
};

void qemu_ios_frame_path_event(unsigned event, uint64_t a, uint64_t b,
                               uint64_t c) __attribute__((weak));

static inline void ios_frame_path(unsigned event, uint64_t a, uint64_t b,
                                   uint64_t c)
{
    if (qemu_ios_frame_path_event) {
        qemu_ios_frame_path_event(event, a, b, c);
    }
}

#endif
