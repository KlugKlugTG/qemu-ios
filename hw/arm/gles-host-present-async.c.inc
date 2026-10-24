/*
 * The present's copy on a worker (Android, the AHardwareBuffer path; LT_PRESENT_ASYNC=off for A/B).
 *
 * A present waited for the GPU to finish the frame and copied it, 2.4 MB at 960x640, into the
 * guest's IOSurface, all on the vCPU: ~1 ms a frame on the Pixel 10 and the Poco. Here the vCPU
 * takes a native fence after the frame's commands, resolves the surface's pages to host pointers
 * (as a guest store would: plain RAM in the caller's TLB, else the synchronous path) and returns;
 * the worker waits on the fence in AHardwareBuffer_lock, copies and unlocks. Like the synchronous
 * path's guest_rw_ram, only pages with no TLB flags (so no dirty tracking to feed) are taken.
 *
 * Who reads the frame waits for it (gles_present_async_settle): the next present, the game
 * context's next GL call (it would draw into the buffer being read), any surface bind, the scaler
 * before it reads a source and the display before it latches (gles_present_wait_hook). CoreAnimation
 * hands out three surfaces in turn and the scaler turns the one presented before this one, so in
 * practice nothing waits. A guest CPU read of the surface within ~1 ms of its present could see the
 * previous frame there; nothing in iOS's display path reads it that way.
 */
#include "hw/arm/gles-present.h"
#include "accel/tcg/probe.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/target_page.h"
#include "system/runstate.h"

typedef EGLSyncKHR (*GlesCreateSync)(EGLDisplay, EGLenum, const EGLint *);
typedef EGLBoolean (*GlesDestroySync)(EGLDisplay, EGLSyncKHR);
typedef EGLint (*GlesDupFence)(EGLDisplay, EGLSyncKHR);

static struct {
    int state;                  /* -1 unknown, 0 unavailable or off, 1 ready */
    GlesCreateSync create_sync;
    GlesDestroySync destroy_sync;
    GlesDupFence dup_fence;
    QemuThread thread;
    QemuMutex lock;
    QemuCond cond;
    bool pending;               /* a job is queued or running (read without the lock as a hint) */
    bool queued;                /* the worker has not taken it yet */
    /* the job */
    uint32_t ctx;
    int fence;
    AHardwareBuffer *buffer;
    bool rgba;
    size_t src_stride;
    uint32_t rw, rh, stride, offset;
    uint8_t **pages;            /* host address of each destination page */
    size_t npages, cap;
    uint8_t *row;               /* the RGBA host's swizzled row */
    size_t row_len;
} gpa = { .state = -1 };

static void gles_present_async_copy(void)
{
    const uint8_t *frame = NULL;
    size_t row_bytes = (size_t)gpa.rw * 4;

    if (gles_ahb_api.lock(gpa.buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, gpa.fence, NULL,
                          (void **)&frame) != 0) {
        frame = NULL;   /* the lock closes the fence either way */
    }
    gpa.fence = -1;
    if (frame) {
        for (uint32_t y = 0; y < gpa.rh; y++) {
            const uint8_t *src = frame + (size_t)(gpa.rh - 1 - y) * gpa.src_stride;
            uint64_t off = gpa.offset + (uint64_t)y * gpa.stride;

            if (gpa.rgba) {
                const uint32_t *s = (const uint32_t *)src;
                uint32_t *d = (uint32_t *)gpa.row;

                for (uint32_t i = 0; i < gpa.rw; i++) {
                    uint32_t v = s[i];
                    d[i] = (v & 0xff00ff00u) | (v >> 16 & 0xffu) | (v & 0xffu) << 16;
                }
                src = gpa.row;
            }
            for (size_t done = 0; done < row_bytes;) {
                uint64_t at = off + done;
                size_t in = at & ~TARGET_PAGE_MASK, n = MIN(row_bytes - done, TARGET_PAGE_SIZE - in);

                memcpy(gpa.pages[at >> TARGET_PAGE_BITS] + in, src + done, n);
                done += n;
            }
        }
        gles_ahb_api.unlock(gpa.buffer, NULL);
    }
}

static void *gles_present_async_worker(void *opaque)
{
    qemu_mutex_lock(&gpa.lock);
    for (;;) {
        while (!gpa.queued) {
            qemu_cond_wait(&gpa.cond, &gpa.lock);
        }
        gpa.queued = false;
        qemu_mutex_unlock(&gpa.lock);
        gles_present_async_copy();
        qemu_mutex_lock(&gpa.lock);
        qatomic_set(&gpa.pending, false);
        qemu_cond_broadcast(&gpa.cond);
    }
    return NULL;
}

/* Until the last present's copy is in guest memory. Any thread. */
static void gles_present_async_settle(void)
{
    if (!qatomic_read(&gpa.pending)) {
        return;
    }
    qemu_mutex_lock(&gpa.lock);
    while (gpa.pending) {
        qemu_cond_wait(&gpa.cond, &gpa.lock);
    }
    qemu_mutex_unlock(&gpa.lock);
}

static void gles_present_async_hook(void)
{
    gles_present_async_settle();
}

/* A stopped machine's RAM is about to be saved or read. */
static void gles_present_async_vm_state(void *opaque, bool running, RunState state)
{
    if (!running) {
        gles_present_async_settle();
    }
}

static bool gles_present_async_ready(void)
{
    if (gpa.state < 0) {
        const char *v = getenv("LT_PRESENT_ASYNC");

        gpa.state = 0;
        if ((v && (!strcmp(v, "off") || !strcmp(v, "0"))) || getenv("IT_GLES_FRAME_METRICS") ||
            getenv("IT_GLES_VERBOSE") || getenv("IT_GLES_DUMP_DIR") || gles_prof) {
            return false;   /* the frame-pixel reports and dumps read the frame on the vCPU */
        }
        gpa.create_sync = (GlesCreateSync)eglGetProcAddress("eglCreateSyncKHR");
        gpa.destroy_sync = (GlesDestroySync)eglGetProcAddress("eglDestroySyncKHR");
        gpa.dup_fence = (GlesDupFence)eglGetProcAddress("eglDupNativeFenceFDANDROID");
        if (!gpa.create_sync || !gpa.destroy_sync || !gpa.dup_fence) {
            return false;
        }
        qemu_mutex_init(&gpa.lock);
        qemu_cond_init(&gpa.cond);
        gpa.fence = -1;
        qemu_thread_create(&gpa.thread, "gles-present", gles_present_async_worker, NULL,
                           QEMU_THREAD_DETACHED);
        qatomic_set(&gles_present_wait_hook, gles_present_async_hook);
        qemu_add_vm_change_state_handler(gles_present_async_vm_state, NULL);
        gpa.state = 1;
    }
    return gpa.state > 0;
}

/*
 * Queue the copy of the drawable's frame into the surface at guest `base` (the calling process's
 * address). False, having changed nothing, when it must be done here.
 */
static bool gles_present_async_submit(CPUState *cpu, uint32_t base, uint32_t stride, uint32_t rw,
                                      uint32_t rh)
{
    static const EGLint attrs[] = {
        EGL_SYNC_NATIVE_FENCE_FD_ANDROID, EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE,
    };
    CPUArchState *env = cpu_env(cpu);
    int idx = cpu_mmu_index(cpu, false);
    uint64_t first = base & TARGET_PAGE_MASK, end = (uint64_t)base + (uint64_t)(rh - 1) * stride + rw * 4;
    size_t npages = (end - first + TARGET_PAGE_SIZE - 1) >> TARGET_PAGE_BITS;
    AHardwareBuffer_Desc desc;
    EGLSyncKHR sync;
    int fence;

    if (!gles_present_async_ready() || !gh.ahb.buffer || !rh || end > UINT32_MAX) {
        return false;
    }
    gles_present_async_settle();
    if (gpa.cap < npages) {
        gpa.pages = g_renew(uint8_t *, gpa.pages, npages);
        gpa.cap = npages;
    }
    for (size_t i = 0; i < npages; i++) {
        void *host;

        if (probe_access_flags(env, first + i * TARGET_PAGE_SIZE, 0, MMU_DATA_STORE, idx, true,
                               &host, 0) || !host) {
            return false;
        }
        gpa.pages[i] = host;
    }
    sync = gpa.create_sync(gles_egl_display, EGL_SYNC_NATIVE_FENCE_ANDROID, attrs);
    if (sync == EGL_NO_SYNC_KHR) {
        return false;
    }
    glFlush();
    fence = gpa.dup_fence(gles_egl_display, sync);
    gpa.destroy_sync(gles_egl_display, sync);
    if (fence < 0) {
        return false;
    }
    gles_ahb_api.describe(gh.ahb.buffer, &desc);
    if (gh.ahb.rgba && gpa.row_len < (size_t)rw * 4) {
        g_free(gpa.row);
        gpa.row = g_malloc((size_t)rw * 4);
        gpa.row_len = (size_t)rw * 4;
    }
    qemu_mutex_lock(&gpa.lock);
    gpa.ctx = gles_cur_ctx;
    gpa.fence = fence;
    gpa.buffer = gh.ahb.buffer;
    gpa.rgba = gh.ahb.rgba;
    gpa.src_stride = (size_t)desc.stride * 4;
    gpa.rw = rw;
    gpa.rh = rh;
    gpa.stride = stride;
    gpa.offset = base & ~TARGET_PAGE_MASK;
    gpa.npages = npages;
    qatomic_set(&gpa.pending, true);
    gpa.queued = true;
    qemu_cond_broadcast(&gpa.cond);
    qemu_mutex_unlock(&gpa.lock);
    return true;
}

/* A GL call from the context whose frame is still being read would draw into that buffer. */
static inline void gles_present_async_before_call(uint32_t ctx)
{
    if (qatomic_read(&gpa.pending) && ctx == gpa.ctx) {
        gles_present_async_settle();
    }
}
