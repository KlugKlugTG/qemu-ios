/* SPDX-License-Identifier: GPL-2.0-or-later
 * APPLE_fence portability. A synchronous completion barrier is conservative
 * and preserves ordering on EGL drivers without the Apple extension.
 */
static GHashTable *gles_egl_fences;
static GLuint gles_egl_next_fence;
static void gles_egl_gen_fences(GLsizei n, GLuint *names)
{
    if (!gles_egl_fences) gles_egl_fences=g_hash_table_new(g_direct_hash,g_direct_equal);
    for (GLsizei i=0;i<n;i++) {
        names[i]=++gles_egl_next_fence;
        g_hash_table_add(gles_egl_fences,GUINT_TO_POINTER(names[i]));
    }
}
static void gles_egl_delete_fences(GLsizei n, const GLuint *names)
{
    if (gles_egl_fences) for (GLsizei i=0;i<n;i++)
        g_hash_table_remove(gles_egl_fences,GUINT_TO_POINTER(names[i]));
}
static GLboolean gles_egl_is_fence(GLuint name)
{ return gles_egl_fences && g_hash_table_contains(gles_egl_fences,GUINT_TO_POINTER(name)); }
static void gles_egl_finish_fence(GLuint name) { glFinish(); }
static GLboolean gles_egl_test_fence(GLuint name) { glFinish(); return gles_egl_is_fence(name); }
static GLboolean gles_egl_test_object(GLenum object, GLuint name) { glFinish(); return GL_TRUE; }
static void gles_egl_finish_object(GLenum object, GLint name) { glFinish(); }
#define glGenFencesAPPLE gles_egl_gen_fences
#define glDeleteFencesAPPLE gles_egl_delete_fences
#define glSetFenceAPPLE gles_egl_finish_fence
#define glIsFenceAPPLE gles_egl_is_fence
#define glTestFenceAPPLE gles_egl_test_fence
#define glFinishFenceAPPLE gles_egl_finish_fence
#define glTestObjectAPPLE gles_egl_test_object
#define glFinishObjectAPPLE gles_egl_finish_object
