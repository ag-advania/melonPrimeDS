#pragma once
// MELONPRIME-PC-ADAPT: desktop stub so Sapphire FrameQueue.h compiles byte-identical; GL path is never taken on the Vulkan-only desktop build.
//
// Minimal subset of EGL 1.x types/constants/functions actually named by
// FrameQueue.h/.cpp: EGLDisplay/EGLContext/EGLSurface types, EGL_NO_* sentinels,
// and eglGetCurrentDisplay()/eglGetCurrentContext(). Function bodies live in
// GlStubs.cpp and are no-ops; the desktop Vulkan renderer never takes the
// FrameBackend::OpenGlTexture path so these are never actually called.

typedef void* EGLDisplay;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef unsigned int EGLBoolean;
// MELONPRIME-PC-ADAPT: added for eglext.h's eglCreateSyncKHR/eglWaitSyncKHR stub declarations
// (MelonInstanceVulkan.cpp's verbatim-copied runFrame()/prepareRenderFrame() dead OpenGlTexture path).
typedef unsigned int EGLenum;
typedef int EGLint;

#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)

#define EGL_TRUE  1
#define EGL_FALSE 0

// MELONPRIME-PC-ADAPT: rename the stub entry points via macros so ONLY the
// TUs including this compat header bind to the no-op stubs; prevents the
// stub definitions from shadowing any real EGL library (e.g. ANGLE via Qt)
// at link time.
#define eglGetCurrentDisplay melonprime_sapphire_stub_eglGetCurrentDisplay
#define eglGetCurrentContext melonprime_sapphire_stub_eglGetCurrentContext

#ifdef __cplusplus
extern "C" {
#endif

EGLDisplay eglGetCurrentDisplay(void);
EGLContext eglGetCurrentContext(void);

#ifdef __cplusplus
}
#endif
