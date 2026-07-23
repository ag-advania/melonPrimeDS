// MELONPRIME-PC-ADAPT: desktop stub so Sapphire FrameQueue.h/.cpp compile and link byte-identical; GL path is
// never taken on the Vulkan-only desktop build (FrameQueue only touches these entry points when
// Frame::backend == FrameBackend::OpenGlTexture, which never happens here). All bodies are no-ops.
//
// eglGetCurrentDisplay()/eglGetCurrentContext() always report "no current GL context" (EGL_NO_DISPLAY /
// EGL_NO_CONTEXT), so every hasCurrentOpenGlContext guard in FrameQueue.cpp evaluates false and the
// glGenTextures/glDeleteTextures/glTexImage2D/eglDestroySyncKHR stubs below are consequently unreachable
// at runtime; they exist only to satisfy the linker for the dead OpenGlTexture code path.

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

EGLDisplay eglGetCurrentDisplay(void)
{
    return EGL_NO_DISPLAY;
}

EGLContext eglGetCurrentContext(void)
{
    return EGL_NO_CONTEXT;
}

EGLBoolean eglDestroySyncKHR(EGLDisplay /*display*/, EGLSyncKHR /*sync*/)
{
    return EGL_FALSE;
}

// MELONPRIME-PC-ADAPT: added for MelonInstanceVulkan.cpp's verbatim-copied runFrame()/prepareRenderFrame(),
// which reference these on the dead OpenGlTexture frame-backend path (see EGL/eglext.h note).
EGLSyncKHR eglCreateSyncKHR(EGLDisplay /*display*/, EGLenum /*type*/, const EGLAttribKHR* /*attrib_list*/)
{
    return EGL_NO_SYNC_KHR;
}

EGLint eglWaitSyncKHR(EGLDisplay /*display*/, EGLSyncKHR /*sync*/, EGLint /*flags*/)
{
    return EGL_FALSE;
}

void glGenTextures(GLsizei /*n*/, GLuint* textures)
{
    if (textures != nullptr)
        *textures = 0;
}

void glDeleteTextures(GLsizei /*n*/, const GLuint* /*textures*/)
{
}

void glBindTexture(GLenum /*target*/, GLuint /*texture*/)
{
}

void glTexParameteri(GLenum /*target*/, GLenum /*pname*/, GLint /*param*/)
{
}

void glTexImage2D(
    GLenum /*target*/,
    GLint /*level*/,
    GLint /*internalformat*/,
    GLsizei /*width*/,
    GLsizei /*height*/,
    GLint /*border*/,
    GLenum /*format*/,
    GLenum /*type*/,
    const void* /*pixels*/)
{
}

void glTexSubImage2D(
    GLenum /*target*/,
    GLint /*level*/,
    GLint /*xoffset*/,
    GLint /*yoffset*/,
    GLsizei /*width*/,
    GLsizei /*height*/,
    GLenum /*format*/,
    GLenum /*type*/,
    const void* /*pixels*/)
{
}

void glFlush(void)
{
}
