/*
 * The phone's camera into the emulated device: the mailbox the app fills and the
 * ISP empties. See include/hw/arm/host-camera.h for the contract.
 *
 * The size limits live here because the two sides disagree about what a frame
 * is: the app knows the phone's preview sizes (640x480 up to 1280x960), the
 * guest asks the ISP for whatever mode its driver picked. A frame outside these
 * bounds is a bug in the caller, and taking it would mean the ISP scaling from a
 * buffer that is not what it claims -- so it is refused, not truncated.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "hw/arm/host-camera.h"
#include <pthread.h>

#define HOST_CAMERA_MIN_SIDE    16
#define HOST_CAMERA_MAX_SIDE    4096
#define HOST_CAMERA_MAX_FRAME   (HOST_CAMERA_MAX_SIDE * HOST_CAMERA_MAX_SIDE * 3 / 2)

/* A statically initialised lock, because the app may write before any device
 * exists: there is no point at which it would be safe to initialise one. */
static pthread_mutex_t camera_lock = PTHREAD_MUTEX_INITIALIZER;

static bool attached;               /* an ISP has been created: frames have a destination */
static int source = HOST_CAMERA_BACK;   /* which camera the picture came from */
static int active = HOST_CAMERA_BACK;   /* which camera the guest last named */

static uint8_t *mailbox;            /* the newest frame the app wrote */
static size_t mailbox_size;
static unsigned mailbox_width, mailbox_height;
static uint64_t mailbox_generation;
static bool unread;                 /* written since the ISP last copied it up */

static uint8_t *snapshot;           /* the ISP's own copy, drawn while it scales */
static size_t snapshot_size;
static unsigned snapshot_width, snapshot_height;
static int snapshot_sensor = HOST_CAMERA_BACK;
static bool snapshot_valid;
static uint64_t snapshot_generation;

static uint64_t pushed, taken, replaced;

void host_camera_attach(void)
{
    pthread_mutex_lock(&camera_lock);
    attached = true;
    g_free(mailbox);
    mailbox = NULL;
    mailbox_size = 0;
    mailbox_width = mailbox_height = 0;
    mailbox_generation++;
    unread = false;
    source = HOST_CAMERA_BACK;
    active = HOST_CAMERA_BACK;
    pushed = taken = replaced = 0;
    pthread_mutex_unlock(&camera_lock);
}

int host_camera_write(int sensor, const void *nv12, unsigned width, unsigned height)
{
    size_t need;
    uint8_t *copy;

    if (!nv12 || width < HOST_CAMERA_MIN_SIDE || height < HOST_CAMERA_MIN_SIDE ||
        width > HOST_CAMERA_MAX_SIDE || height > HOST_CAMERA_MAX_SIDE ||
        (width & 1) || (height & 1)) {
        return -1;
    }
    need = (size_t)width * height * 3 / 2;
    if (need > HOST_CAMERA_MAX_FRAME) {
        return -1;
    }
    /* Copied outside the lock: the caller's buffer is half a megabyte and the
     * ISP's own thread must not wait for a copy it is not making. */
    copy = g_malloc(need);
    memcpy(copy, nv12, need);

    pthread_mutex_lock(&camera_lock);
    if (!attached) {
        pthread_mutex_unlock(&camera_lock);
        g_free(copy);
        return 0;
    }
    if (unread) {
        /* An unread frame is replaced, not queued: this is a viewfinder. */
        replaced++;
    }
    g_free(mailbox);
    mailbox = copy;
    mailbox_size = need;
    mailbox_width = width;
    mailbox_height = height;
    mailbox_generation++;
    unread = true;
    source = sensor == HOST_CAMERA_FRONT ? HOST_CAMERA_FRONT : HOST_CAMERA_BACK;
    pushed++;
    pthread_mutex_unlock(&camera_lock);
    return 1;
}

void host_camera_snapshot(HostCameraFrame *out)
{
    if (!out) {
        return;
    }
    out->data = NULL;
    out->size = 0;
    out->width = out->height = 0;
    out->sensor = HOST_CAMERA_BACK;

    pthread_mutex_lock(&camera_lock);
    if (snapshot_generation != mailbox_generation) {
        snapshot_generation = mailbox_generation;
        if (!mailbox) {
            snapshot_valid = false;     /* flushed: back to the pattern */
        } else {
            if (snapshot_size < mailbox_size) {
                snapshot = g_realloc(snapshot, mailbox_size);
                snapshot_size = mailbox_size;
            }
            memcpy(snapshot, mailbox, mailbox_size);
            snapshot_width = mailbox_width;
            snapshot_height = mailbox_height;
            snapshot_sensor = source;
            snapshot_valid = true;
            unread = false;
            taken++;
        }
    }
    if (snapshot_valid) {
        out->data = snapshot;
        out->size = (size_t)snapshot_width * snapshot_height * 3 / 2;
        out->width = snapshot_width;
        out->height = snapshot_height;
        out->sensor = snapshot_sensor;
    }
    pthread_mutex_unlock(&camera_lock);
}

void host_camera_select(int sensor)
{
    if (sensor != HOST_CAMERA_BACK && sensor != HOST_CAMERA_FRONT) {
        return;
    }
    pthread_mutex_lock(&camera_lock);
    active = sensor;
    pthread_mutex_unlock(&camera_lock);
}

int host_camera_active(void)
{
    int sensor;

    pthread_mutex_lock(&camera_lock);
    sensor = active;
    pthread_mutex_unlock(&camera_lock);
    return sensor;
}

void host_camera_counts(uint64_t *pushed_out, uint64_t *taken_out, uint64_t *replaced_out,
                        unsigned *width, unsigned *height)
{
    pthread_mutex_lock(&camera_lock);
    if (pushed_out) {
        *pushed_out = pushed;
    }
    if (taken_out) {
        *taken_out = taken;
    }
    if (replaced_out) {
        *replaced_out = replaced;
    }
    if (width) {
        *width = snapshot_valid ? snapshot_width : 0;
    }
    if (height) {
        *height = snapshot_valid ? snapshot_height : 0;
    }
    pthread_mutex_unlock(&camera_lock);
}

void host_camera_flush(void)
{
    pthread_mutex_lock(&camera_lock);
    g_free(mailbox);
    mailbox = NULL;
    mailbox_size = 0;
    mailbox_width = mailbox_height = 0;
    mailbox_generation++;
    unread = false;
    pushed = taken = replaced = 0;
    pthread_mutex_unlock(&camera_lock);
}
