/* SPDX-License-Identifier: GPL-2.0-or-later
 * Android: the drawable's color attachment in an AHardwareBuffer.
 *
 * The CPU can map an AHardwareBuffer, so a present copies the rows the GPU
 * rendered straight out of it, as the iOS host does with an IOSurface. The
 * readback it replaces (glFinish + glReadPixels into a staging buffer, then a
 * copy into the guest surface) spent ~1 ms per 320x480 frame in glReadPixels on
 * a Snapdragon 865-class phone, where the driver resolves the render target
 * into its own staging memory and copies it out again.
 *
 * Everything is looked up at run time (libnativewindow and the EGL/GL
 * extensions), and anything missing leaves the readback path in place.
 * LT_GLES_AHB=0 keeps the readback, for A/B.
 */
#include <dlfcn.h>
#include <android/hardware_buffer.h>
#include <EGL/eglext.h>

/* Not in the NDK's AHardwareBuffer_Format list, but gralloc's BGRA_8888: the
 * guest surface's byte order, so the rows copy without a swizzle. */
#define GLES_AHB_FORMAT_BGRA 5

typedef struct GLESAhb {
    AHardwareBuffer *buffer;
    EGLImageKHR image;
    size_t stride;      /* row pitch in bytes, asked once at attach */
    bool rgba;          /* R8G8B8A8 storage: rows need a byte swap for BGRA */
} GLESAhb;

/* Optional bounded frame-path markers, separate from user profiling. Off adds
 * no clock reads; enabled producers only append to the existing memory trace.
 * Stages: 1 GPU completion, 2 row pitch, 3 CPU map, 4 CPU unmap. Stage 1
 * reports whether it waited on a fence (1) or drained the pipeline (0). */
static bool gles_ahb_path_on;

static inline void gles_ahb_path(unsigned event, GLESAhb *a, unsigned stage,
                                 int result)
{
    if (gles_ahb_path_on) {
        ios_frame_path(event, (uintptr_t)a->buffer, stage, (uint32_t)result);
    }
}

static struct {
    int state;          /* 0 not probed, 1 usable, -1 not */
    int (*allocate)(const AHardwareBuffer_Desc *, AHardwareBuffer **);
    void (*release)(AHardwareBuffer *);
    void (*describe)(const AHardwareBuffer *, AHardwareBuffer_Desc *);
    int (*lock)(AHardwareBuffer *, uint64_t, int32_t, const ARect *, void **);
    int (*unlock)(AHardwareBuffer *, int32_t *);
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC client_buffer;
    PFNEGLCREATEIMAGEKHRPROC create_image;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target_texture;
} gles_ahb_api;

static bool gles_ahb_usable(void)
{
    if (!gles_ahb_api.state) {
        const char *e = getenv("LT_GLES_AHB");
        const char *egl = eglQueryString(gles_egl_display, EGL_EXTENSIONS);
        void *nw = NULL;

        gles_ahb_api.state = -1;
        if (e && !atoi(e)) {
            return false;
        }
        if (!egl || !gles_native_extension(egl, "EGL_ANDROID_get_native_client_buffer") ||
            !gles_native_extension(egl, "EGL_ANDROID_image_native_buffer") ||
            !gles_native_extension((const char *)glGetString(GL_EXTENSIONS),
                                   "GL_OES_EGL_image") ||
            !(nw = dlopen("libnativewindow.so", RTLD_NOW))) {
            fprintf(stderr, "[gles-ahb] unavailable; presenting by readback\n");
            return false;
        }
        gles_ahb_api.allocate = dlsym(nw, "AHardwareBuffer_allocate");
        gles_ahb_api.release = dlsym(nw, "AHardwareBuffer_release");
        gles_ahb_api.describe = dlsym(nw, "AHardwareBuffer_describe");
        gles_ahb_api.lock = dlsym(nw, "AHardwareBuffer_lock");
        gles_ahb_api.unlock = dlsym(nw, "AHardwareBuffer_unlock");
        gles_ahb_api.client_buffer = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
            eglGetProcAddress("eglGetNativeClientBufferANDROID");
        gles_ahb_api.create_image = (PFNEGLCREATEIMAGEKHRPROC)
            eglGetProcAddress("eglCreateImageKHR");
        gles_ahb_api.destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)
            eglGetProcAddress("eglDestroyImageKHR");
        gles_ahb_api.target_texture = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
            eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (gles_ahb_api.allocate && gles_ahb_api.release && gles_ahb_api.describe &&
            gles_ahb_api.lock && gles_ahb_api.unlock && gles_ahb_api.client_buffer &&
            gles_ahb_api.create_image && gles_ahb_api.destroy_image &&
            gles_ahb_api.target_texture) {
            gles_ahb_api.state = 1;
            const char *mode = getenv("LT_GLES_AHB_PATH");
            const char *path = getenv("LT_FRAME_PATH");
            bool requested = mode && !strcmp(mode, "on");
            gles_ahb_path_on = requested && path && *path &&
                               strcmp(path, "off") && strcmp(path, "0") &&
                               qemu_ios_frame_path_event;
            fprintf(stderr, "[gles-ahb-path] requested=%s active=%s\n",
                    requested ? "on" : "off", gles_ahb_path_on ? "on" : "off");
        }
    }
    return gles_ahb_api.state > 0;
}

static void gles_present_async_settle(void);

static void gles_ahb_free(GLESAhb *a)
{
    gles_present_async_settle();   /* the present worker may be reading it */
    if (a->image != EGL_NO_IMAGE_KHR) {
        gles_ahb_api.destroy_image(gles_egl_display, a->image);
    }
    if (a->buffer) {
        gles_ahb_api.release(a->buffer);
    }
    *a = (GLESAhb){ .image = EGL_NO_IMAGE_KHR };
}

/*
 * Give the bound GL_TEXTURE_2D AHardwareBuffer storage of @width x @height, in
 * place of a glTexImage2D. False leaves @a empty and the texture without
 * storage.
 */
static bool gles_ahb_attach(GLESAhb *a, uint32_t width, uint32_t height)
{
    static const uint32_t formats[] = {
        GLES_AHB_FORMAT_BGRA, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
    };
    static const EGLint attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };

    *a = (GLESAhb){ .image = EGL_NO_IMAGE_KHR };
    if (!gles_ahb_usable()) {
        return false;
    }
    gles_native_save_errors();
    for (unsigned i = 0; i < ARRAY_SIZE(formats); i++) {
        AHardwareBuffer_Desc desc = {
            .width = width, .height = height, .layers = 1, .format = formats[i],
            .usage = AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT |
                     AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                     AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,
        };

        if (gles_ahb_api.allocate(&desc, &a->buffer) != 0) {
            a->buffer = NULL;
            continue;
        }
        /* The surface's description is fixed for its lifetime, and gralloc may pad
         * the rows the allocation was asked for, so ask once here rather than once a
         * present -- on the vCPU in the synchronous path. */
        gles_ahb_api.describe(a->buffer, &desc);
        a->stride = (size_t)desc.stride * 4;
        a->image = gles_ahb_api.create_image(gles_egl_display, EGL_NO_CONTEXT,
                                             EGL_NATIVE_BUFFER_ANDROID,
                                             gles_ahb_api.client_buffer(a->buffer),
                                             attrs);
        if (a->image != EGL_NO_IMAGE_KHR) {
            gles_ahb_api.target_texture(GL_TEXTURE_2D, (GLeglImageOES)a->image);
            if (glGetError() == GL_NO_ERROR) {
                a->rgba = formats[i] != GLES_AHB_FORMAT_BGRA;
                return true;
            }
        }
        gles_ahb_free(a);
    }
    return false;
}

/*
 * A native fence for the frame the GPU is drawing, for the lock below to wait on -- the same sync
 * the asynchronous present hands to AHardwareBuffer_lock. A CPU lock has no implicit sync with GPU
 * writes, and without a fence the only way to wait was glFinish(), which drains every queue in the
 * context; the wait then also happens on the vCPU instead of in the kernel's fence wait. This is
 * the synchronous present's path: a driver with no eglDupNativeFenceFDANDROID, LT_PRESENT_ASYNC=off,
 * a present that has to finish here, and every run with the frame metrics on (they read the frame
 * on the vCPU), so it is also part of what a profile capture costs.
 */
static struct {
    int state;                  /* -1 not asked yet, 0 no fences, 1 fences */
    EGLSyncKHR (*create)(EGLDisplay, EGLenum, const EGLint *);
    int (*dup_fd)(EGLDisplay, EGLSyncKHR);
    EGLBoolean (*destroy)(EGLDisplay, EGLSyncKHR);
} gles_ahb_fence_api = { .state = -1 };

static int gles_ahb_frame_fence(void)
{
    static const EGLint attrs[] = {
        EGL_SYNC_NATIVE_FENCE_FD_ANDROID, EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE,
    };
    const char *mode;
    EGLSyncKHR sync;
    int fd;

    if (gles_ahb_fence_api.state < 0) {
        mode = getenv("LT_AHB_FENCE");
        if (mode && (!strcmp(mode, "off") || !strcmp(mode, "0"))) {
            gles_ahb_fence_api.state = 0;
        } else if (gles_egl_display == EGL_NO_DISPLAY) {
            return -1;          /* no context yet: ask again next time */
        } else {
            gles_ahb_fence_api.create = (EGLSyncKHR (*)(EGLDisplay, EGLenum, const EGLint *))
                eglGetProcAddress("eglCreateSyncKHR");
            gles_ahb_fence_api.dup_fd = (int (*)(EGLDisplay, EGLSyncKHR))
                eglGetProcAddress("eglDupNativeFenceFDANDROID");
            gles_ahb_fence_api.destroy = (EGLBoolean (*)(EGLDisplay, EGLSyncKHR))
                eglGetProcAddress("eglDestroySyncKHR");
            gles_ahb_fence_api.state = gles_ahb_fence_api.create && gles_ahb_fence_api.dup_fd &&
                                       gles_ahb_fence_api.destroy;
            if (!gles_ahb_fence_api.state) {
                fprintf(stderr, "[gles-ahb] no native fence; the present waits with glFinish\n");
            }
        }
    }
    if (gles_ahb_fence_api.state <= 0) {
        return -1;
    }
    sync = gles_ahb_fence_api.create(gles_egl_display, EGL_SYNC_NATIVE_FENCE_ANDROID, attrs);
    if (sync == EGL_NO_SYNC_KHR) {
        return -1;
    }
    glFlush();                  /* the fence is only reached once the frame is submitted */
    fd = gles_ahb_fence_api.dup_fd(gles_egl_display, sync);
    gles_ahb_fence_api.destroy(gles_egl_display, sync);
    return fd;
}

/* The rendered rows, bottom row first as GL draws them; NULL on failure. */
static const uint8_t *gles_ahb_lock(GLESAhb *a, size_t *stride)
{
    size_t pitch = a->stride;
    void *p = NULL;
    int fence;

    /* No implicit sync between GPU writes and a CPU lock: wait for the frame. */
    gles_ahb_path(IOS_FP_AHB_STAGE_BEGIN, a, 1, 0);
    fence = gles_ahb_frame_fence();
    if (fence < 0) {
        glFinish();             /* no fence: wait for the whole pipeline here, as before */
    }
    gles_ahb_path(IOS_FP_AHB_STAGE_END, a, 1, fence >= 0);
    gles_ahb_path(IOS_FP_AHB_STAGE_BEGIN, a, 2, 0);
    if (!pitch) {               /* the description could not be read at attach */
        AHardwareBuffer_Desc desc;

        gles_ahb_api.describe(a->buffer, &desc);
        pitch = (size_t)desc.stride * 4;
        a->stride = pitch;
    }
    gles_ahb_path(IOS_FP_AHB_STAGE_END, a, 2, pitch != 0);
    gles_ahb_path(IOS_FP_AHB_STAGE_BEGIN, a, 3, 0);
    int result = gles_ahb_api.lock(a->buffer,
                                 AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, fence,
                                 NULL, &p);
    gles_ahb_path(IOS_FP_AHB_STAGE_END, a, 3, result);
    if (result != 0 || !pitch) {
        return NULL;
    }
    *stride = pitch;
    return p;
}

static void gles_ahb_unlock(GLESAhb *a)
{
    gles_ahb_path(IOS_FP_AHB_STAGE_BEGIN, a, 4, 0);
    int result = gles_ahb_api.unlock(a->buffer, NULL);
    gles_ahb_path(IOS_FP_AHB_STAGE_END, a, 4, result);
}
