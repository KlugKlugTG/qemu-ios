/* SPDX-License-Identifier: GPL-2.0-or-later
 * A present that does not wait for the GPU (LT_GL_ASYNC_PRESENT=off: the
 * synchronous copy, for A/B).
 *
 * The synchronous present waited for the frame (glFinish, or Android's
 * AHardwareBuffer lock), read it and copied the rows into the guest's
 * CoreAnimation surface, all on the vCPU. Here the present only queues a GPU
 * copy of the drawable into a staging buffer the CPU can read without a GL
 * context, behind an EGL fence, and resolves the surface's pages as the
 * presenting process could store to them now. The rows are copied later, on
 * whichever thread first needs them (gles_host_complete_presents):
 *
 *   - the display's VBL latch and the scaler, before they read RAM: the
 *     game's layer is scanned out from that memory, not composited by GL;
 *   - on the vCPU, before anything in the bridge reads or checks guest
 *     surface memory (binds, draw-time refreshes, write-backs, readbacks, the
 *     guest's finish and fences), the next present into a busy staging
 *     buffer, a resize and a context's deletion.
 *
 * A guest CPU read of that surface before one of those would see the
 * previous frame; CoreAnimation's own path never does one (the scanned-out
 * layer is read by the display, and a GL composite binds it first).
 *
 * Desktop: glReadPixels into a persistently mapped pixel-pack buffer.
 * Android: glCopyTexSubImage2D into an AHardwareBuffer texture, locked by the
 * completing thread. Anything missing at run time keeps the synchronous path.
 */
#ifdef GLES_HOST_EGL

typedef struct GLESPresentJob {
    bool queued;
    uint32_t width, height;             /* staging size */
#ifdef GLES_HOST_AHB
    GLuint texture;
    GLESAhb ahb;
#else
    GLuint pbo;
    const uint8_t *map;
#endif
    EGLSyncKHR sync;
    /* The surface: rows of row_bytes, stride apart, from byte offset into page 0. */
    uint32_t rows, row_bytes, stride, offset;
    unsigned npages, cap;
    uint8_t **host;
    ram_addr_t *ram;
} GLESPresentJob;

static void gles_texture_begin(void);

static int gles_async_on = -1;
static QemuMutex gles_async_lock;
static GLESPresentJob *gles_async_queue[16];    /* oldest first */
static unsigned gles_async_n;
static int gles_async_pending;                  /* gles_async_n, for a lockless check */
static bool gles_async_busy;                    /* the oldest is being copied */
static QemuCond gles_async_cond;                /* queued, or copied */
static QemuThread gles_async_thread;
static PFNEGLCREATESYNCKHRPROC gles_egl_create_sync;
static PFNEGLCLIENTWAITSYNCKHRPROC gles_egl_client_wait_sync;
static PFNEGLDESTROYSYNCKHRPROC gles_egl_destroy_sync;
#ifndef GLES_HOST_AHB
static void (*gles_gl_buffer_storage)(GLenum, GLsizeiptr, const void *, GLbitfield);
static void *(*gles_gl_map_buffer_range)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
#endif

/* Completion time by thread, for LT_GLWAIT_PROF; under gles_async_lock. */
static struct {
    uint64_t n[2], ns[2], wait_ns[2], max[2];
} gles_async_prof;

static void *gles_async_worker(void *opaque);

static bool gles_async_has(const char *list, const char *name)
{
    size_t n = strlen(name);

    for (const char *p = list; p && (p = strstr(p, name)); p += n) {
        if ((p == list || p[-1] == ' ') && (p[n] == ' ' || !p[n])) {
            return true;
        }
    }
    return false;
}

static bool gles_async_enabled(void)
{
    if (gles_async_on >= 0) {
        return gles_async_on;
    }
    const char *v = getenv("LT_GL_ASYNC_PRESENT");
    const char *why = NULL;
    const char *egl = eglQueryString(gles_egl_display, EGL_EXTENSIONS);

    gles_async_on = 0;
    qemu_mutex_init(&gles_async_lock);
    if (v && (!strcmp(v, "off") || !strcmp(v, "0"))) {
        why = "off";
    } else if (gles_prof || getenv("IT_GLES_FRAME_METRICS") || getenv("IT_GLES_VERBOSE") ||
               getenv("IT_GLES_DUMP_DIR") || getenv("IT_GLES_PRESENT_TEST") ||
               gles_present_prof) {
        why = "a frame diagnostic reads the present";
    } else if (!egl || !gles_async_has(egl, "EGL_KHR_fence_sync")) {
        why = "no EGL_KHR_fence_sync";
    } else {
        gles_egl_create_sync = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
        gles_egl_client_wait_sync =
            (PFNEGLCLIENTWAITSYNCKHRPROC)eglGetProcAddress("eglClientWaitSyncKHR");
        gles_egl_destroy_sync = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
#ifdef GLES_HOST_AHB
        if (!gles_ahb_usable()) {
            why = "no AHardwareBuffer";
        }
#else
        gles_gl_buffer_storage = (void *)eglGetProcAddress("glBufferStorage");
        gles_gl_map_buffer_range = (void *)eglGetProcAddress("glMapBufferRange");
        if (!gles_async_has((const char *)glGetString(GL_EXTENSIONS), "GL_ARB_buffer_storage") ||
            !gles_gl_buffer_storage || !gles_gl_map_buffer_range) {
            why = "no GL_ARB_buffer_storage";
        }
#endif
        if (!why && (!gles_egl_create_sync || !gles_egl_client_wait_sync ||
                     !gles_egl_destroy_sync)) {
            why = "no EGL sync entry points";
        }
    }
    gles_async_on = !why;
    if (gles_async_on) {
        qemu_cond_init(&gles_async_cond);
        qemu_thread_create(&gles_async_thread, "gles-present", gles_async_worker, NULL,
                           QEMU_THREAD_DETACHED);
    }
    fprintf(stderr, "[gles] async-present=%s%s%s\n", why ? "off" : "on",
            why ? ": " : "", why ? why : "");
    return gles_async_on;
}

/* Wait for @j's frame and copy it into the guest's surface; the fence wait in *@wait. */
static void gles_async_copy(GLESPresentJob *j, uint64_t *wait)
{
    uint64_t t0 = gw_enabled() ? get_clock() : 0;
    const uint8_t *frame;
    size_t fstride;

    gles_egl_client_wait_sync(gles_egl_display, j->sync, 0, EGL_FOREVER_KHR);
    gles_egl_destroy_sync(gles_egl_display, j->sync);
    j->sync = EGL_NO_SYNC_KHR;
    *wait = t0 ? get_clock() - t0 : 0;
#ifdef GLES_HOST_AHB
    AHardwareBuffer_Desc desc;
    void *p = NULL;

    gles_ahb_api.describe(j->ahb.buffer, &desc);
    if (gles_ahb_api.lock(j->ahb.buffer, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, NULL, &p)) {
        gles_refuse("present:async-lock");
        return;
    }
    frame = p;
    fstride = (size_t)desc.stride * 4;
#else
    frame = j->map;
    fstride = j->row_bytes;
#endif
    g_autofree uint8_t *swapped = NULL;
    for (uint32_t y = 0; y < j->rows; y++) {
        /* GL's origin is bottom-left, the surface's is top-left. */
        const uint8_t *src = frame + (size_t)(j->rows - 1 - y) * fstride;
        uint64_t off = j->offset + (uint64_t)y * j->stride;

#ifdef GLES_HOST_AHB
        if (j->ahb.rgba) {
            const uint32_t *s = (const uint32_t *)src;
            uint32_t *d;

            if (!swapped) {
                swapped = g_malloc(j->row_bytes);
            }
            d = (uint32_t *)swapped;
            for (uint32_t i = 0; i < j->row_bytes / 4; i++) {
                d[i] = (s[i] & 0xff00ff00u) | (s[i] >> 16 & 0xffu) | (s[i] & 0xffu) << 16;
            }
            src = swapped;
        }
#endif
        for (uint32_t done = 0; done < j->row_bytes;) {
            uint64_t at = off + done;
            uint32_t in = at & ~TARGET_PAGE_MASK;
            uint32_t n = MIN(j->row_bytes - done, TARGET_PAGE_SIZE - in);

            memcpy(j->host[at >> TARGET_PAGE_BITS] + in, src + done, n);
            done += n;
        }
    }
#ifdef GLES_HOST_AHB
    gles_ahb_api.unlock(j->ahb.buffer, NULL);
#endif
    /* As a guest store would: the bind cache and the display models read these. */
    for (unsigned i = 0, k; i < j->npages; i = k) {
        for (k = i + 1; k < j->npages && j->ram[k] == j->ram[k - 1] + TARGET_PAGE_SIZE; k++) {
        }
        physical_memory_set_dirty_range(j->ram[i], (ram_addr_t)(k - i) * TARGET_PAGE_SIZE,
                                        DIRTY_CLIENTS_NOCODE);
    }
}

/*
 * Copy the oldest queued present, as the only copier at a time, so frames
 * into one surface land in order. Called and returns with gles_async_lock.
 */
static void gles_async_copy_head_locked(void)
{
    GLESPresentJob *j = gles_async_queue[0];
    uint64_t t0 = gw_enabled() ? get_clock() : 0, wait = 0;

    gles_async_busy = true;
    qemu_mutex_unlock(&gles_async_lock);
    gles_async_copy(j, &wait);
    qemu_mutex_lock(&gles_async_lock);
    gles_async_busy = false;
    j->queued = false;
    memmove(gles_async_queue, gles_async_queue + 1, --gles_async_n * sizeof(*gles_async_queue));
    qatomic_set(&gles_async_pending, gles_async_n);
    qemu_cond_broadcast(&gles_async_cond);
    if (t0) {
        unsigned v = current_cpu != NULL;
        uint64_t d = get_clock() - t0;

        gles_async_prof.n[v]++;
        gles_async_prof.ns[v] += d;
        gles_async_prof.wait_ns[v] += wait;
        gles_async_prof.max[v] = MAX(gles_async_prof.max[v], d);
    }
}

/* Off the vCPU: the copy runs here as soon as the frame is queued. */
static void *gles_async_worker(void *opaque)
{
    rcu_register_thread();
    qemu_mutex_lock(&gles_async_lock);
    for (;;) {
        while (!gles_async_n || gles_async_busy) {
            qemu_cond_wait(&gles_async_cond, &gles_async_lock);
        }
        gles_async_copy_head_locked();
    }
    return NULL;
}

void gles_host_complete_presents(void)
{
    if (!qatomic_read(&gles_async_pending)) {
        return;
    }
    QEMU_LOCK_GUARD(&gles_async_lock);
    while (gles_async_n) {
        if (gles_async_busy) {
            qemu_cond_wait(&gles_async_cond, &gles_async_lock);    /* the worker has it */
        } else {
            gles_async_copy_head_locked();
        }
    }
}

/* gles_host_complete_presents from the bridge, counted by @why for LT_GLWAIT_PROF. */
static void gles_complete_presents(const char *why)
{
    if (!qatomic_read(&gles_async_pending)) {
        return;
    }
    if (gw_enabled()) {
        unsigned i;

        for (i = 0; i < ARRAY_SIZE(gw.site) - 1 && gw.site[i].why && gw.site[i].why != why; i++) {
        }
        gw.site[i].why = why;
        gw.site[i].n++;
    }
    gles_host_complete_presents();
}

/* The staging buffer for @w x @h, created or resized in the current context. */
static bool gles_async_staging(GLESPresentJob *j, uint32_t w, uint32_t h)
{
    if (j->width == w && j->height == h) {
        return true;
    }
#ifdef GLES_HOST_AHB
    /* A driver texture the guest's names never reach: the native binding is
     * restored exactly, and the texture-name layer (rectangles) is untouched. */
    GLint texture;
    GLESAhb ahb;

    if (j->texture) {
        GLES_DRIVER_RAW(glDeleteTextures)(1, &j->texture);
        gles_ahb_free(&j->ahb);
        j->texture = 0;
    }
    j->width = j->height = 0;
    gles_texture_begin();
    GLES_DRIVER_RAW(glGetIntegerv)(GL_TEXTURE_BINDING_2D, &texture);
    GLES_DRIVER_RAW(glGenTextures)(1, &j->texture);
    GLES_DRIVER_RAW(glBindTexture)(GL_TEXTURE_2D, j->texture);
    bool ok = j->texture && gles_ahb_attach(&ahb, w, h);
    GLES_DRIVER_RAW(glBindTexture)(GL_TEXTURE_2D, texture);
    if (!ok) {
        GLES_DRIVER_RAW(glDeleteTextures)(1, &j->texture);
        j->texture = 0;
        return false;
    }
    j->ahb = ahb;
#else
    const GLbitfield flags = GL_MAP_READ_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
    size_t size = (size_t)w * h * 4;

    if (j->pbo) {
        glDeleteBuffers(1, &j->pbo);
        j->pbo = 0;
        j->map = NULL;
    }
    j->width = j->height = 0;
    gles_texture_begin();
    glGenBuffers(1, &j->pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, j->pbo);
    gles_gl_buffer_storage(GL_PIXEL_PACK_BUFFER, size, NULL, flags | GL_CLIENT_STORAGE_BIT);
    j->map = gles_gl_map_buffer_range(GL_PIXEL_PACK_BUFFER, 0, size, flags);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!j->map || glGetError() != GL_NO_ERROR) {
        glDeleteBuffers(1, &j->pbo);
        j->pbo = 0;
        j->map = NULL;
        return false;
    }
#endif
    j->width = w;
    j->height = h;
    return true;
}

/* Free the current context's staging, its copies done. */
static void gles_async_free(GLESHost *state)
{
    gles_complete_presents("context-free");
    for (unsigned i = 0; i < ARRAY_SIZE(state->async); i++) {
        GLESPresentJob *j = state->async[i];

        if (!j) {
            continue;
        }
#ifdef GLES_HOST_AHB
        if (j->texture) {
            GLES_DRIVER_RAW(glDeleteTextures)(1, &j->texture);
        }
        gles_ahb_free(&j->ahb);
#else
        if (j->pbo) {
            glDeleteBuffers(1, &j->pbo);
        }
#endif
        g_free(j->host);
        g_free(j->ram);
        g_free(j);
        state->async[i] = NULL;
    }
}

/*
 * Queue the drawable's @rw x @rh BGRA frame for the surface at @base. False,
 * having changed nothing the guest sees, when this frame takes the
 * synchronous path.
 */
static bool gles_async_present(CPUState *cpu, uint32_t base, uint32_t stride,
                               uint32_t rw, uint32_t rh)
{
    uint32_t row_bytes = rw * 4;
    uint64_t last = (uint64_t)base + (uint64_t)(rh - 1) * stride + row_bytes - 1;
    unsigned npages = (last >> TARGET_PAGE_BITS) - (base >> TARGET_PAGE_BITS) + 1;
    GLESPresentJob *j;

    if (!gles_async_enabled() || last > UINT32_MAX) {
        return false;
    }
    if (!gh.async[gh.async_next]) {
        gh.async[gh.async_next] = g_new0(GLESPresentJob, 1);
    }
    j = gh.async[gh.async_next];
    if (j->queued || gles_async_n == ARRAY_SIZE(gles_async_queue)) {
        gles_complete_presents("staging-busy");
    }
    if (npages > j->cap) {
        j->cap = npages;
        j->host = g_renew(uint8_t *, j->host, npages);
        j->ram = g_renew(ram_addr_t, j->ram, npages);
    }
    uint64_t w0 = gw_t();
    if (!guest_ram_pages(cpu, base, last - base + 1, j->host, j->ram) ||
        !gles_async_staging(j, rw, rh)) {
        return false;
    }
    gw_add(GW_PRESENT_PAGES, w0);
    w0 = gw_t();
    j->rows = rh;
    j->row_bytes = row_bytes;
    j->stride = stride;
    j->offset = base & ~TARGET_PAGE_MASK;
    j->npages = npages;

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gh.fbo);
#ifdef GLES_HOST_AHB
    GLint texture;

    gles_texture_begin();
    GLES_DRIVER_RAW(glGetIntegerv)(GL_TEXTURE_BINDING_2D, &texture);
    GLES_DRIVER_RAW(glBindTexture)(GL_TEXTURE_2D, j->texture);
    GLES_DRIVER_RAW(glCopyTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, 0, 0, rw, rh);
    GLES_DRIVER_RAW(glBindTexture)(GL_TEXTURE_2D, texture);
    bool failed = GLES_DRIVER_RAW(glGetError)() != GL_NO_ERROR;
#else
    GLint pack;

    gles_texture_begin();
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, j->pbo);
    glReadPixels(0, 0, rw, rh, GL_BGRA, GLES_BGRA_READ_TYPE, NULL);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    bool failed = glGetError() != GL_NO_ERROR;
#endif
    j->sync = failed ? EGL_NO_SYNC_KHR :
              gles_egl_create_sync(gles_egl_display, EGL_SYNC_FENCE_KHR, NULL);
    /* Submitted now: the completing thread has no context to flush it from. */
    glFlush();
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, gles_host_fbo(gh.bound_framebuffer));
    gw_add(GW_PRESENT_QUEUE, w0);
    if (j->sync == EGL_NO_SYNC_KHR) {
        /* This host cannot fence the copy: present synchronously from now on. */
        fprintf(stderr, "[gles] async-present: %s; off\n",
                failed ? "staging copy failed" : "no EGL fence");
        gles_async_on = 0;
        return false;
    }
    gh.async_next = (gh.async_next + 1) % ARRAY_SIZE(gh.async);
    QEMU_LOCK_GUARD(&gles_async_lock);
    j->queued = true;
    gles_async_queue[gles_async_n++] = j;
    qatomic_set(&gles_async_pending, gles_async_n);
    qemu_cond_broadcast(&gles_async_cond);
    return true;
}

static void gw_report_async(double secs, double frames)
{
    static const char *const who[2] = { "other", "vcpu" };

    if (gles_async_on < 0) {
        return;                             /* no present yet: the lock is not set up */
    }
    QEMU_LOCK_GUARD(&gles_async_lock);
    for (unsigned v = 0; v < 2; v++) {
        if (gles_async_prof.n[v]) {
            fprintf(stderr, "[glwait]   deferred-%-5s %8.1f/s mean %7.1f us (fence %.1f) max %7.1f us  %6.3f ms/frame\n",
                    who[v], gles_async_prof.n[v] / secs, gles_async_prof.ns[v] / 1e3 / gles_async_prof.n[v],
                    gles_async_prof.wait_ns[v] / 1e3 / gles_async_prof.n[v], gles_async_prof.max[v] / 1e3,
                    gles_async_prof.ns[v] / 1e6 / frames);
        }
    }
    memset(&gles_async_prof, 0, sizeof(gles_async_prof));
    for (unsigned i = 0; i < ARRAY_SIZE(gw.site) && gw.site[i].why; i++) {
        fprintf(stderr, "[glwait]     completed at %s %.1f/s\n", gw.site[i].why, gw.site[i].n / secs);
    }
}

#else /* !GLES_HOST_EGL */

void gles_host_complete_presents(void)
{
}

static void gles_complete_presents(const char *why)
{
}

static void gw_report_async(double secs, double frames)
{
}

#endif
