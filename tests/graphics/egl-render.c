/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#ifdef GLES_HOST_EGL_ES
#define GL_GLEXT_PROTOTYPES 1
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES3/gl3.h>
#define GL_UNSIGNED_SHORT_4_4_4_4_REV 0x8365
#define GL_UNSIGNED_SHORT_1_5_5_5_REV 0x8366
#define GL_UNSIGNED_INT_8_8_8_8 0x8035
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_BGRA GL_BGRA_EXT
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include "../../hw/arm/gles-host-egl.c.inc"
#ifdef GLES_HOST_EGL_ES
#include "../../hw/arm/gles-host-native.c.inc"
#define glGenFramebuffers glGenFramebuffersOES
#define glBindFramebuffer glBindFramebufferOES
#define glFramebufferTexture2D glFramebufferTexture2DOES
#define glCheckFramebufferStatus glCheckFramebufferStatusOES
#define glBindRenderbuffer glBindRenderbufferOES
#define glRenderbufferStorage glRenderbufferStorageOES
#define glFramebufferRenderbuffer glFramebufferRenderbufferOES
#endif

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048]; glGetShaderInfoLog(s, sizeof(log), NULL, log);
        fprintf(stderr, "%s\n", log); abort();
    }
    return s;
}
int main(void)
{
    CGLContextObj c1, c2;
    assert(gles_egl_create(NULL, 1, &c1));
    GLuint texture=0x40000000u;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    /* Exercise actual per-context FBO, not the one-pixel EGL dummy drawable. */
    GLuint fb=0x40000000u;
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    GLuint depth=0x40000000u;
    glBindRenderbuffer(GL_RENDERBUFFER,depth);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,32,32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,depth);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    glViewport(0, 0, 32, 32);
    float vertices[] = {-1,-1, 1,-1, 0,1};
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor4f(1,0,0,1); glVertexPointer(2,GL_FLOAT,0,vertices);
    glEnableClientState(GL_VERTEX_ARRAY); glDrawArrays(GL_TRIANGLES,0,3);
    unsigned char px[4];
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    assert(glGetError() == GL_NO_ERROR && px[0] > 240 && px[1] < 5 && px[2] < 5 && px[3] > 240);
    printf("GLES1 triangle rgba=%u,%u,%u,%u renderer=%s\n",px[0],px[1],px[2],px[3],glGetString(GL_RENDERER));
    /* ES1 texture units and client coordinate arrays exercise the CA path. */
    const GLfloat uv[]={0,0, 1,0, .5f,1};
    const unsigned char blue[]={0,0,255,255},red[]={255,0,0,255};
    glBindTexture(GL_TEXTURE_2D,0x40000001u);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,blue);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glEnable(GL_TEXTURE_2D); glTexCoordPointer(2,GL_FLOAT,0,uv); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDrawArrays(GL_TRIANGLES,0,3); glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    assert(glGetError()==GL_NO_ERROR && px[0]<5 && px[1]<5 && px[2]>240);
    glActiveTexture(GL_TEXTURE1); glClientActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,0x40000002u);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,red);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_ADD);
    glEnable(GL_TEXTURE_2D); glTexCoordPointer(2,GL_FLOAT,0,uv); glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDrawArrays(GL_TRIANGLES,0,3); glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    assert(glGetError()==GL_NO_ERROR && px[0]>240 && px[1]<5 && px[2]>240);
#ifdef GLES_HOST_EGL_ES
    typedef void (*DrawTex)(GLfloat,GLfloat,GLfloat,GLfloat,GLfloat);
    DrawTex draw=(DrawTex)eglGetProcAddress("glDrawTexfOES");
    assert(draw);
    GLint crop[]={0,0,1,1};
    glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_CROP_RECT_OES,crop);
    glActiveTexture(GL_TEXTURE0);
    glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_CROP_RECT_OES,crop);
    glClear(GL_COLOR_BUFFER_BIT); draw(0,0,0,32,32);
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    assert(glGetError()==GL_NO_ERROR && px[0]>240 && px[1]<5 && px[2]>240);
#endif
    printf("GLES1 texture/multitexture rgba=%u,%u,%u,%u\n",px[0],px[1],px[2],px[3]);
    assert(gles_egl_create(c1, 2, &c2));
    assert(glIsTexture(texture));
    GLuint fb2; glGenFramebuffers(1,&fb2); glBindFramebuffer(GL_FRAMEBUFFER,fb2);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
#ifdef GLES_HOST_EGL_ES
    const char *vsrc = "attribute vec2 position; void main(){gl_Position=vec4(position,0.,1.);}";
    const char *fsrc = "precision mediump float; void main(){gl_FragColor=vec4(0.,1.,0.,1.);}";
#else
    const char *vsrc = "#version 120\nattribute vec2 position; void main(){gl_Position=vec4(position,0.,1.);}";
    const char *fsrc = "#version 120\nvoid main(){gl_FragColor=vec4(0.,1.,0.,1.);}";
#endif
    GLuint vs=shader(GL_VERTEX_SHADER,vsrc), fs=shader(GL_FRAGMENT_SHADER,fsrc), program=glCreateProgram();
    glAttachShader(program,vs); glAttachShader(program,fs); glBindAttribLocation(program,0,"position"); glLinkProgram(program);
    GLint ok; glGetProgramiv(program,GL_LINK_STATUS,&ok); assert(ok);
    glUseProgram(program); glViewport(0,0,32,32); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,vertices); glEnableVertexAttribArray(0); glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    assert(glGetError()==GL_NO_ERROR && px[0]<5 && px[1]>240 && px[2]<5 && px[3]>240);
    printf("GLES2 triangle rgba=%u,%u,%u,%u renderer=%s\n",px[0],px[1],px[2],px[3],glGetString(GL_RENDERER));
    assert(CGLSetCurrentContext(c1)==0); glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px); assert(px[1]>240);
#ifdef GLES_HOST_EGL_ES
    glReadPixels(16,16,1,1,GL_BGRA,GL_UNSIGNED_BYTE,px);
    assert(px[0]<5 && px[1]>240 && px[2]<5 && px[3]>240);
    unsigned short rgb565;
    glReadPixels(16,16,1,1,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,&rgb565);
    assert(rgb565==0x07e0);
#endif
    CGLRetainContext(c1); CGLReleaseContext(c1);
    CGLReleaseContext(c2); CGLReleaseContext(c1);
    assert(eglGetCurrentContext()==EGL_NO_CONTEXT);
    puts("PASS private object names, packed depth/stencil, GLES1 textures/multitexture, context switching, cross-version texture sharing, render/readback, teardown");
    return 0;
}
