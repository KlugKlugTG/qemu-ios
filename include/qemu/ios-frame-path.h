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
    IOS_FP_ALIGNMENT_BEGIN,
    IOS_FP_ALIGNMENT_RETURN,
    IOS_FP_ALIGNMENT_ABANDON,
    IOS_FP_ALIGNMENT_ARGS,
    IOS_FP_ALIGNMENT_CALLER,
    IOS_FP_GUEST_PC,
    IOS_FP_GUEST_REGS,
    IOS_FP_GUEST_STACK,
    IOS_FP_SHIM_TALLY,
    IOS_FP_NATIVE_WAIT,
    IOS_FP_AHB_STAGE_BEGIN,
    IOS_FP_AHB_STAGE_END,
    IOS_FP_FMSS_BEGIN,
    IOS_FP_FMSS_END,
};

void qemu_ios_native_wait_init(void) __attribute__((weak));
void qemu_ios_native_wait_finish(void) __attribute__((weak));
void qemu_ios_bql_spin_finish(void) __attribute__((weak));
void qemu_ios_idle_spin_finish(void) __attribute__((weak));

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
