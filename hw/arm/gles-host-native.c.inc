/* SPDX-License-Identifier: GPL-2.0-or-later
 * GLES portability: ES1 extension dispatch, guest row strides and packed pixel
 * formats. No guest GL operation is replaced by a frame or canned drawing.
 */
#include <string.h>
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_TEXTURE_MAX_LEVEL
#define GL_TEXTURE_MAX_LEVEL 0x813D
#endif
static void gles_native_clip_plane(GLenum plane, const double *d)
{
    GLfloat f[] = {d[0], d[1], d[2], d[3]};
    glClipPlanef(plane, f);
}
static void gles_native_get_clip_plane(GLenum plane, double *out)
{
    GLfloat f[4];
    glGetClipPlanef(plane,f);
    for (unsigned i=0;i<4;i++) out[i]=f[i];
}
#define glGetClipPlane gles_native_get_clip_plane
#define glClipPlane gles_native_clip_plane

/* GL error flags are per context, including our emulated pixel-store state. */
static void gles_native_error(GLenum error)
{
    if (error >= GL_INVALID_ENUM && error <= GL_INVALID_FRAMEBUFFER_OPERATION)
        gles_egl_current->errors |= 1u << (error - GL_INVALID_ENUM);
}
static void gles_native_save_errors(void)
{
    GLenum error;
    while ((error=glGetError()) != GL_NO_ERROR) gles_native_error(error);
}
static GLenum gles_native_get_error(void)
{
    if (gles_egl_current->errors) {
        for (unsigned bit=0;bit<=GL_INVALID_FRAMEBUFFER_OPERATION-GL_INVALID_ENUM;bit++)
            if (gles_egl_current->errors & (1u<<bit)) {
                gles_egl_current->errors &= ~(1u<<bit);
                return GL_INVALID_ENUM+bit;
            }
    }
    return glGetError();
}
static void gles_native_pixel_store(GLenum pname, GLint value)
{
    if (pname == GL_UNPACK_ROW_LENGTH) {
        if (value < 0) { gles_native_error(GL_INVALID_VALUE); return; }
        gles_egl_current->unpack_row_length = value;
        return;
    }
    if (pname == GL_UNPACK_ALIGNMENT && value != 1 && value != 2 && value != 4 && value != 8) {
        gles_native_error(GL_INVALID_VALUE); return;
    }
    glPixelStorei(pname, value);
    if (pname == GL_UNPACK_ALIGNMENT) gles_egl_current->unpack_alignment = value;
}
static unsigned gles_native_bpp(GLenum format, GLenum type)
{
    if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
        type == GL_UNSIGNED_SHORT_5_5_5_1 || type == GL_UNSIGNED_SHORT_4_4_4_4_REV ||
        type == GL_UNSIGNED_SHORT_1_5_5_5_REV) return 2;
    if (type == GL_UNSIGNED_INT_8_8_8_8 || type == GL_UNSIGNED_INT_8_8_8_8_REV) return 4;
    unsigned components = format == GL_RGBA || format == GL_BGRA ? 4 :
        format == GL_RGB ? 3 : format == GL_LUMINANCE_ALPHA ? 2 : 1;
    return components * (type == GL_FLOAT || type == GL_UNSIGNED_INT ? 4 :
                         type == GL_UNSIGNED_SHORT || type == 0x8D61 ? 2 : 1);
}
static void *gles_native_texels(GLsizei w, GLsizei h, GLenum *format, GLenum *type, const void *pixels)
{
    if (!pixels || w <= 0 || h <= 0) return NULL;
    unsigned bpp = gles_native_bpp(*format, *type);
    unsigned align = gles_egl_current->unpack_alignment;
    size_t rowbytes = (size_t)(gles_egl_current->unpack_row_length ?
        gles_egl_current->unpack_row_length : (unsigned)w) * bpp;
    size_t stride = (rowbytes + align - 1) & ~(size_t)(align - 1);
    bool bgra = *format == GL_BGRA;
    bool packed = *type == GL_UNSIGNED_INT_8_8_8_8 || *type == GL_UNSIGNED_INT_8_8_8_8_REV;
    bool reverse16 = *type == GL_UNSIGNED_SHORT_4_4_4_4_REV || *type == GL_UNSIGNED_SHORT_1_5_5_5_REV;
    unsigned output_bpp = reverse16 || packed ? 4 : bpp;
    if ((size_t)w > (64 * 1024 * 1024u) / output_bpp / (unsigned)h) return NULL;
    unsigned char *out = malloc((size_t)w * h * output_bpp);
    if (!out) return NULL;
    for (unsigned y = 0; y < (unsigned)h; y++) {
        const unsigned char *src = (const unsigned char *)pixels + y * stride;
        unsigned char *dst = out + (size_t)y * w * output_bpp;
        if (reverse16) {
            for (unsigned x = 0; x < (unsigned)w; x++) {
                unsigned v = src[x*2] | src[x*2+1] << 8;
                if (*type == GL_UNSIGNED_SHORT_4_4_4_4_REV) {
                    dst[x*4]=((v>>8)&15)*17; dst[x*4+1]=((v>>4)&15)*17;
                    dst[x*4+2]=(v&15)*17; dst[x*4+3]=(v>>12)*17;
                } else {
                    dst[x*4]=((v>>10)&31)*255/31; dst[x*4+1]=((v>>5)&31)*255/31;
                    dst[x*4+2]=(v&31)*255/31; dst[x*4+3]=(v>>15)*255;
                }
            }
        } else if (bgra && !packed && *type != GL_UNSIGNED_BYTE) {
            unsigned component=bpp/4;
            for (unsigned x=0;x<(unsigned)w;x++) for (unsigned k=0;k<4;k++)
                memcpy(dst+x*bpp+k*component,src+x*bpp+(k==0?2:k==2?0:k)*component,component);
        } else if (bgra || packed) {
            for (unsigned x = 0; x < (unsigned)w; x++) {
                const unsigned char *p = src+x*4;
                unsigned char q[4];
                for (unsigned k=0;k<4;k++) q[k]=p[packed && *type==GL_UNSIGNED_INT_8_8_8_8 ? 3-k : k];
                dst[x*4]=q[bgra?2:0]; dst[x*4+1]=q[1]; dst[x*4+2]=q[bgra?0:2]; dst[x*4+3]=q[3];
            }
        } else memcpy(dst,src,(size_t)w*bpp);
    }
    if (bgra) *format=GL_RGBA;
    if (reverse16 || packed) { *format=GL_RGBA; *type=GL_UNSIGNED_BYTE; }
    return out;
}
static void gles_native_tex_image(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                                  GLint border, GLenum format, GLenum type, const void *pixels)
{
    void *copy = gles_native_texels(w,h,&format,&type,pixels);
    if (pixels && !copy) { gles_native_error(w <= 0 || h <= 0 ? GL_INVALID_VALUE : GL_OUT_OF_MEMORY); return; }
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    if (format == GL_RGBA && (internal == GL_BGRA || internal == GL_RGBA)) internal=GL_RGBA;
    glTexImage2D(target,level,internal,w,h,border,format,type,copy);
    glPixelStorei(GL_UNPACK_ALIGNMENT,gles_egl_current->unpack_alignment);
    free(copy);
}
static void gles_native_tex_sub_image(GLenum target, GLint level, GLint x, GLint y,
                                      GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels)
{
    void *copy=gles_native_texels(w,h,&format,&type,pixels);
    if (pixels && !copy) { gles_native_error(w <= 0 || h <= 0 ? GL_INVALID_VALUE : GL_OUT_OF_MEMORY); return; }
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    glTexSubImage2D(target,level,x,y,w,h,format,type,copy);
    glPixelStorei(GL_UNPACK_ALIGNMENT,gles_egl_current->unpack_alignment);
    free(copy);
}
static void gles_native_read_pixels(GLint x, GLint y, GLsizei w, GLsizei h,
                                    GLenum format, GLenum type, void *pixels)
{
    if (format != GL_BGRA && !(format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5)) {
        glReadPixels(x,y,w,h,format,type,pixels); return;
    }
    if (format == GL_BGRA && type != GL_UNSIGNED_BYTE &&
        type != GL_UNSIGNED_INT_8_8_8_8 && type != GL_UNSIGNED_INT_8_8_8_8_REV) {
        gles_native_error(GL_INVALID_ENUM); return;
    }
    if (w <= 0 || h <= 0 || (size_t)w > 64*1024*1024u/4/(unsigned)h) {
        glReadPixels(x,y,w,h,format,type,pixels); return;
    }
    unsigned char *copy=malloc((size_t)w*h*4);
    if (!copy) { gles_native_error(GL_OUT_OF_MEMORY); return; }
    gles_native_save_errors();
    GLint alignment; glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(x,y,w,h,GL_RGBA,GL_UNSIGNED_BYTE,copy);
    GLenum error=glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT,alignment);
    if (error != GL_NO_ERROR) { gles_native_error(error); free(copy); return; }
    unsigned bpp=format==GL_BGRA?4:2;
    size_t stride=((size_t)w*bpp+alignment-1)&~(size_t)(alignment-1);
    for (unsigned row=0;row<(unsigned)h;row++) for (unsigned col=0;col<(unsigned)w;col++) {
        unsigned char *src=copy+((size_t)row*w+col)*4, *dst=(unsigned char *)pixels+row*stride+col*bpp;
        if (bpp==4) {
            unsigned char q[4]={src[2],src[1],src[0],src[3]};
            for (unsigned k=0;k<4;k++) dst[k]=q[type==GL_UNSIGNED_INT_8_8_8_8?3-k:k];
        }
        else { unsigned v=(src[0]>>3)<<11 | (src[1]>>2)<<5 | src[2]>>3;dst[0]=v;dst[1]=v>>8; }
    }
    free(copy);
}
#define glGetError gles_native_get_error
#define glPixelStorei gles_native_pixel_store
#define glTexImage2D gles_native_tex_image
#define glTexSubImage2D gles_native_tex_sub_image
#define glReadPixels gles_native_read_pixels

/* ES1 FBO extensions are loaded via EGL, ES2 uses core dispatch. */
static GLboolean gles_native_IsFramebuffer(GLuint name)
{
    typedef GLboolean (*Proc)(GLuint name);
    static Proc es1;
    if (gles_egl_current->api == 2) { return glIsFramebuffer(name); }
    if (!es1) es1=(Proc)eglGetProcAddress("glIsFramebufferOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glIsFramebufferOES\n"); gles_native_error(GL_INVALID_OPERATION); return 0; }
    return es1(name);
}
static GLboolean gles_native_IsRenderbuffer(GLuint name)
{
    typedef GLboolean (*Proc)(GLuint name);
    static Proc es1;
    if (gles_egl_current->api == 2) { return glIsRenderbuffer(name); }
    if (!es1) es1=(Proc)eglGetProcAddress("glIsRenderbufferOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glIsRenderbufferOES\n"); gles_native_error(GL_INVALID_OPERATION); return 0; }
    return es1(name);
}
static void gles_native_GenerateMipmap(GLenum target)
{
    typedef void (*Proc)(GLenum target);
    static Proc es1;
    if (gles_egl_current->api == 2) { glGenerateMipmap(target); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glGenerateMipmapOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glGenerateMipmapOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(target);
}
static void gles_native_GenFramebuffers(GLsizei n, GLuint *names)
{
    typedef void (*Proc)(GLsizei n, GLuint *names);
    static Proc es1;
    if (gles_egl_current->api == 2) { glGenFramebuffers(n,names); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glGenFramebuffersOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glGenFramebuffersOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(n,names);
}
static void gles_native_BindFramebuffer(GLenum target, GLuint name)
{
    typedef void (*Proc)(GLenum target, GLuint name);
    static Proc es1;
    if (gles_egl_current->api == 2) { glBindFramebuffer(target,name); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glBindFramebufferOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glBindFramebufferOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(target,name);
}
static void gles_native_FramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
{
    typedef void (*Proc)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
    static Proc es1;
    if (gles_egl_current->api == 2) { glFramebufferTexture2D(target,attachment,textarget,texture,level); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glFramebufferTexture2DOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glFramebufferTexture2DOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(target,attachment,textarget,texture,level);
}
static void gles_native_GenRenderbuffers(GLsizei n, GLuint *names)
{
    typedef void (*Proc)(GLsizei n, GLuint *names);
    static Proc es1;
    if (gles_egl_current->api == 2) { glGenRenderbuffers(n,names); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glGenRenderbuffersOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glGenRenderbuffersOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(n,names);
}
static void gles_native_BindRenderbuffer(GLenum target, GLuint name)
{
    typedef void (*Proc)(GLenum target, GLuint name);
    static Proc es1;
    if (gles_egl_current->api == 2) { glBindRenderbuffer(target,name); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glBindRenderbufferOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glBindRenderbufferOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(target,name);
}
static void gles_native_RenderbufferStorage(GLenum target, GLenum format, GLsizei w, GLsizei h)
{
    typedef void (*Proc)(GLenum target, GLenum format, GLsizei w, GLsizei h);
    static Proc es1;
    if (gles_egl_current->api == 2) { glRenderbufferStorage(target,format,w,h); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glRenderbufferStorageOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glRenderbufferStorageOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(target,format,w,h);
}
static void gles_native_FramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rbtarget, GLuint name)
{
    typedef void (*Proc)(GLenum target, GLenum attachment, GLenum rbtarget, GLuint name);
    static Proc es1;
    if (gles_egl_current->api == 2) { glFramebufferRenderbuffer(target,attachment,rbtarget,name); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glFramebufferRenderbufferOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glFramebufferRenderbufferOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(target,attachment,rbtarget,name);
}
static GLenum gles_native_CheckFramebufferStatus(GLenum target)
{
    typedef GLenum (*Proc)(GLenum target);
    static Proc es1;
    if (gles_egl_current->api == 2) { return glCheckFramebufferStatus(target); }
    if (!es1) es1=(Proc)eglGetProcAddress("glCheckFramebufferStatusOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glCheckFramebufferStatusOES\n"); gles_native_error(GL_INVALID_OPERATION); return 0; }
    return es1(target);
}
static void gles_native_DeleteFramebuffers(GLsizei n, const GLuint *names)
{
    typedef void (*Proc)(GLsizei n, const GLuint *names);
    static Proc es1;
    if (gles_egl_current->api == 2) { glDeleteFramebuffers(n,names); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glDeleteFramebuffersOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glDeleteFramebuffersOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(n,names);
}
static void gles_native_DeleteRenderbuffers(GLsizei n, const GLuint *names)
{
    typedef void (*Proc)(GLsizei n, const GLuint *names);
    static Proc es1;
    if (gles_egl_current->api == 2) { glDeleteRenderbuffers(n,names); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glDeleteRenderbuffersOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glDeleteRenderbuffersOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(n,names);
}
static void gles_native_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *value)
{
    typedef void (*Proc)(GLenum target, GLenum pname, GLint *value);
    static Proc es1;
    if (gles_egl_current->api == 2) { glGetRenderbufferParameteriv(target,pname,value); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glGetRenderbufferParameterivOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glGetRenderbufferParameterivOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(target,pname,value);
}
static void gles_native_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment, GLenum pname, GLint *value)
{
    typedef void (*Proc)(GLenum target, GLenum attachment, GLenum pname, GLint *value);
    static Proc es1;
    if (gles_egl_current->api == 2) { glGetFramebufferAttachmentParameteriv(target,attachment,pname,value); return; }
    if (!es1) es1=(Proc)eglGetProcAddress("glGetFramebufferAttachmentParameterivOES");
    if (!es1) { fprintf(stderr,"[gles-egl] missing required glGetFramebufferAttachmentParameterivOES\n"); gles_native_error(GL_INVALID_OPERATION); return; }
    es1(target,attachment,pname,value);
}
#define glIsFramebufferOES gles_native_IsFramebuffer
#define glIsRenderbufferOES gles_native_IsRenderbuffer
#define glGenerateMipmapOES gles_native_GenerateMipmap
#define glGenFramebuffersOES gles_native_GenFramebuffers
#define glBindFramebufferOES gles_native_BindFramebuffer
#define glFramebufferTexture2DOES gles_native_FramebufferTexture2D
#define glGenRenderbuffersOES gles_native_GenRenderbuffers
#define glBindRenderbufferOES gles_native_BindRenderbuffer
#define glRenderbufferStorageOES gles_native_RenderbufferStorage
#define glFramebufferRenderbufferOES gles_native_FramebufferRenderbuffer
#define glCheckFramebufferStatusOES gles_native_CheckFramebufferStatus
#define glDeleteFramebuffersOES gles_native_DeleteFramebuffers
#define glDeleteRenderbuffersOES gles_native_DeleteRenderbuffers
#define glGetRenderbufferParameterivOES gles_native_GetRenderbufferParameteriv
#define glGetFramebufferAttachmentParameterivOES gles_native_GetFramebufferAttachmentParameteriv
