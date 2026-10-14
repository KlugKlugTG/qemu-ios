/* SPDX-License-Identifier: GPL-2.0-or-later
 * Offscreen EGL lifetime adapter. Guest GL remains in gles-host.c. This leaf
 * deliberately uses only libc/EGL so the production context can be exercised
 * independently of QEMU, including on Android hardware.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct GLESEGLTexture {
    unsigned name, width, height;
    struct GLESEGLTexture *next;
} GLESEGLTexture;
typedef struct GLESEGLTextures {
    unsigned refs;
    GLESEGLTexture *head;
} GLESEGLTextures;

typedef struct GLESEGLContext {
    EGLContext context;
    EGLSurface surface;
    unsigned refs, api;
    unsigned unpack_alignment, unpack_row_length, errors;
    bool fbo_checked, fbo_supported;
    GLESEGLTextures *textures;
    GLESEGLTexture default_texture;
} *CGLContextObj;
static EGLDisplay gles_egl_display = EGL_NO_DISPLAY;
static EGLConfig gles_egl_config;
static __thread CGLContextObj gles_egl_current;

static int CGLSetCurrentContext(CGLContextObj c)
{
    if (gles_egl_current == c && eglGetCurrentContext() ==
        (c ? c->context : EGL_NO_CONTEXT)) return 0;
    if (!eglMakeCurrent(gles_egl_display, c ? c->surface : EGL_NO_SURFACE,
                        c ? c->surface : EGL_NO_SURFACE,
                        c ? c->context : EGL_NO_CONTEXT)) {
        fprintf(stderr, "[gles-egl] make-current failed: 0x%x\n", eglGetError());
        return -1;
    }
    gles_egl_current = c;
    return 0;
}
static CGLContextObj CGLRetainContext(CGLContextObj c) { c->refs++; return c; }
static void CGLReleaseContext(CGLContextObj c)
{
    if (!c || --c->refs) return;
    if (gles_egl_current == c) CGLSetCurrentContext(NULL);
    eglDestroyContext(gles_egl_display, c->context);
    eglDestroySurface(gles_egl_display, c->surface);
    if (!--c->textures->refs) {
        GLESEGLTexture *texture=c->textures->head;
        while (texture) { GLESEGLTexture *next=texture->next; free(texture); texture=next; }
        free(c->textures);
    }
    free(c);
}
static bool gles_egl_create(CGLContextObj share, unsigned api, CGLContextObj *out)
{
#ifdef GLES_HOST_EGL_ES
    EGLenum client = EGL_OPENGL_ES_API;
    EGLint renderable = EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT;
#else
    EGLenum client = EGL_OPENGL_API;
    EGLint renderable = EGL_OPENGL_BIT;
#endif
    if (gles_egl_display == EGL_NO_DISPLAY) {
#ifndef __ANDROID__
        PFNEGLGETPLATFORMDISPLAYEXTPROC get = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
        if (get) gles_egl_display = get(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
#endif
        if (gles_egl_display == EGL_NO_DISPLAY)
            gles_egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint config[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, renderable, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLint count = 0;
        if (gles_egl_display == EGL_NO_DISPLAY ||
            !eglInitialize(gles_egl_display, NULL, NULL) ||
            !eglChooseConfig(gles_egl_display, config, &gles_egl_config, 1, &count) || !count) {
            fprintf(stderr, "[gles-egl] initialize/config failed: 0x%x\n", eglGetError());
            gles_egl_display = EGL_NO_DISPLAY;
            return false;
        }
    }
    if (!eglBindAPI(client)) return false;
#ifdef GLES_HOST_EGL_ES
    EGLint context[] = {EGL_CONTEXT_CLIENT_VERSION, api == 2 ? 2 : 1, EGL_NONE};
#else
    EGLint context[] = {EGL_CONTEXT_MAJOR_VERSION, 2, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
#endif
    EGLint surface[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    CGLContextObj c = calloc(1, sizeof(*c));
    if (!c) return false;
    c->refs = 1;
    c->unpack_alignment = 4;
    c->textures=share ? share->textures : calloc(1,sizeof(*c->textures));
    if (!c->textures) { free(c); return false; }
    c->textures->refs++;
    c->api = api == 2 ? 2 : 1;
    c->context = eglCreateContext(gles_egl_display, gles_egl_config,
                                 share ? share->context : EGL_NO_CONTEXT, context);
    c->surface = eglCreatePbufferSurface(gles_egl_display, gles_egl_config, surface);
    if (c->context == EGL_NO_CONTEXT || c->surface == EGL_NO_SURFACE || CGLSetCurrentContext(c)) {
        fprintf(stderr, "[gles-egl] create api=%u failed: 0x%x\n", c->api, eglGetError());
        if (c->context != EGL_NO_CONTEXT) eglDestroyContext(gles_egl_display, c->context);
        if (c->surface != EGL_NO_SURFACE) eglDestroySurface(gles_egl_display, c->surface);
        if (!--c->textures->refs) free(c->textures);
        free(c);
        return false;
    }
    *out = c;
    return true;
}
