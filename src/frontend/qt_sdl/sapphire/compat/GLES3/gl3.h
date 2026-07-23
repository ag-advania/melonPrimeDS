#pragma once
// MELONPRIME-PC-ADAPT: desktop stub so Sapphire FrameQueue.h compiles byte-identical; GL path is never taken on the Vulkan-only desktop build.
//
// Minimal subset of GLES3 types/constants/functions actually named by
// FrameQueue.h/.cpp: GLuint plus the handful of texture-object entry points
// used to (de)allocate Frame::frameTexture. Function bodies live in
// GlStubs.cpp and are no-ops; the desktop Vulkan renderer never takes the
// FrameBackend::OpenGlTexture path so these are never actually called.
//
// Deliberately NOT a full GLES/GL header: do not add symbols beyond what
// FrameQueue.h/.cpp reference, to avoid colliding with any real OpenGL
// loader (e.g. glad) linked elsewhere in the desktop build.

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef void GLvoid;

#define GL_TEXTURE_2D         0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST            0x2600
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_CLAMP_TO_EDGE      0x812F
#define GL_RGBA                0x1908
#define GL_UNSIGNED_BYTE       0x1401

// MELONPRIME-PC-ADAPT: rename the stub entry points via macros so ONLY the
// TUs that include this compat header (FrameQueue.cpp, GlStubs.cpp) bind to
// the no-op stubs. Without this, a global no-op definition of e.g.
// glGenTextures would shadow opengl32's real export at link time and
// silently break the existing desktop OpenGL renderer, which resolves the
// same symbol names through the system GL.
#define glGenTextures    melonprime_sapphire_stub_glGenTextures
#define glDeleteTextures melonprime_sapphire_stub_glDeleteTextures
#define glBindTexture    melonprime_sapphire_stub_glBindTexture
#define glTexParameteri  melonprime_sapphire_stub_glTexParameteri
#define glTexImage2D     melonprime_sapphire_stub_glTexImage2D
// MELONPRIME-PC-ADAPT: added for MelonInstanceVulkan.cpp's verbatim-copied runFrame(), which references these
// on the (dead, on this Vulkan-only desktop build) non-accelerated software-renderer texture-upload path.
#define glTexSubImage2D  melonprime_sapphire_stub_glTexSubImage2D
#define glFlush          melonprime_sapphire_stub_glFlush

#ifdef __cplusplus
extern "C" {
#endif

void glGenTextures(GLsizei n, GLuint* textures);
void glDeleteTextures(GLsizei n, const GLuint* textures);
void glBindTexture(GLenum target, GLuint texture);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glTexImage2D(
    GLenum target,
    GLint level,
    GLint internalformat,
    GLsizei width,
    GLsizei height,
    GLint border,
    GLenum format,
    GLenum type,
    const void* pixels);
void glTexSubImage2D(
    GLenum target,
    GLint level,
    GLint xoffset,
    GLint yoffset,
    GLsizei width,
    GLsizei height,
    GLenum format,
    GLenum type,
    const void* pixels);
void glFlush(void);

#ifdef __cplusplus
}
#endif
