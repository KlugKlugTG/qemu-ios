/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <string.h>
#ifdef GLES_HOST_EGL_ES
#define GL_GLEXT_PROTOTYPES 1
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES2/gl2.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include "../../hw/arm/gles-host-egl.c.inc"

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
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    /* Exercise actual per-context FBO, not the one-pixel EGL dummy drawable. */
    GLuint fb;
    glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
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
    CGLRetainContext(c1); CGLReleaseContext(c1);
    CGLReleaseContext(c2); CGLReleaseContext(c1);
    assert(eglGetCurrentContext()==EGL_NO_CONTEXT);
    puts("PASS context switching, cross-version texture sharing, render/readback, teardown");
    return 0;
}
