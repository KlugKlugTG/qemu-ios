/* SPDX-License-Identifier: GPL-2.0-or-later
 * Experimental core-FBO interop within a native ES1 context. A pass requires
 * actual fixed-function pixels. This probe does not select a production
 * backend or bypass its required capability checks.
 */
#include <assert.h>
#define GL_GLEXT_PROTOTYPES 1
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES2/gl2.h>
#include "../../hw/arm/gles-host-egl.c.inc"

int main(void)
{
    CGLContextObj context;
    assert(gles_egl_create(NULL, 1, &context));
    fprintf(stderr, "ES1 CORE-FBO experiment renderer=%s version=%s\n",
            glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint texture = 0, framebuffer = 0, renderbuffer = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &framebuffer);
    fprintf(stderr, "CORE gen framebuffer=%u error=0x%x\n",
            framebuffer, glGetError());
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    fprintf(stderr, "CORE bind framebuffer error=0x%x\n", glGetError());
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, texture, 0);
    fprintf(stderr, "CORE attach color error=0x%x\n", glGetError());
    glGenRenderbuffers(1, &renderbuffer);
    fprintf(stderr, "CORE gen renderbuffer=%u error=0x%x\n",
            renderbuffer, glGetError());
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, 32, 32);
    fprintf(stderr, "CORE storage error=0x%x\n", glGetError());
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, renderbuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, renderbuffer);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    GLenum error = glGetError();
    fprintf(stderr, "CORE FBO status=0x%x error=0x%x\n", status, error);
    assert(framebuffer && renderbuffer &&
           status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR);

    GLfloat vertices[] = {-1, -1, 1, -1, 0, 1};
    glViewport(0, 0, 32, 32);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor4f(1, 0, 0, 1);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4] = {0};
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    error = glGetError();
    fprintf(stderr, "CORE fixed-function pixel=%u,%u,%u,%u error=0x%x\n",
            pixel[0], pixel[1], pixel[2], pixel[3], error);
    assert(error == GL_NO_ERROR && pixel[0] > 240 &&
           pixel[1] < 5 && pixel[2] < 5 && pixel[3] > 240);
    CGLReleaseContext(context);
    return 0;
}
