/*
 * The phone's camera, as the emulated device's camera sensor.
 *
 * The A4's camera ISP decides when a frame happens and where it goes; what it
 * has never had is a picture. This is that picture: one mailbox the app fills
 * with the phone's own frames and the ISP empties on its next frame.
 *
 * It is deliberately not a queue and not a sensor model. The guest drives one
 * camera at a time and asks the ISP for frames whether or not anyone is
 * looking, so what it needs is the newest picture, at whatever size the phone
 * produced, and the ISP is the one that scales it to the mode the guest asked
 * for. An old frame is worth nothing: a push over a frame nobody drew replaces
 * it, which is why a slow guest costs a drop and never a delay.
 *
 * Nothing in here knows about QEMU or the ISP: it is a hand-off between threads
 * (the app's camera thread writes, the emulator's vCPU thread reads), and the
 * lock is held only for the copy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_ARM_HOST_CAMERA_H
#define HW_ARM_HOST_CAMERA_H

#include "qemu/osdep.h"

/* Which of the phone's cameras a frame came from: the two the A4 boards carry. */
#define HOST_CAMERA_BACK    0       /* MIPI, the one on the back */
#define HOST_CAMERA_FRONT   1       /* SMIA, the one above the screen */

/* One frame, as the ISP sees it: tightly packed NV12, valid until the next snapshot. */
typedef struct HostCameraFrame {
    const uint8_t *data;            /* NULL when there is nothing to draw */
    size_t size;
    unsigned width, height;
    int sensor;                     /* which of the phone's cameras it came from */
} HostCameraFrame;

/*
 * The ISP exists now. Until this is called a pushed frame is dropped: there is
 * nothing that could ever draw it, and a host without a camera (a desktop, a
 * check boot) must not appear to have one.
 */
void host_camera_attach(void);

/*
 * The app hands over one frame, NV12 (luma, then interleaved chroma, no
 * padding), of width x height. The buffer is copied, so the caller keeps its
 * own. `sensor` is only recorded, so the app can say which camera the picture
 * came from. Returns 1 when accepted, 0 when no ISP is there to draw it, and -1
 * for something that is not a frame: the app treats that as its own mistake,
 * not as something to retry.
 */
int host_camera_write(int sensor, const void *nv12, unsigned width, unsigned height);

/*
 * Once per ISP frame, before drawing: copy the newest mailbox frame if the app
 * has written one since the last call. `*out` describes it, or has a NULL data
 * pointer when there is nothing to draw -- before the first write and after a
 * flush, which is when the ISP's own test pattern shows.
 */
void host_camera_snapshot(HostCameraFrame *out);

/* Which camera the guest's driver last named, for the app's own diagnosis. */
void host_camera_select(int sensor);
int host_camera_active(void);

/*
 * Counters for the app's diagnosis, cheap enough to read every half second:
 * frames written, frames the ISP copied up to draw, frames replaced before
 * anything drew them, and the size of the picture being held.
 */
void host_camera_counts(uint64_t *pushed, uint64_t *taken, uint64_t *replaced,
                        unsigned *width, unsigned *height);

/* Forget the held frame: the pattern resumes until the next write. */
void host_camera_flush(void);

#endif
