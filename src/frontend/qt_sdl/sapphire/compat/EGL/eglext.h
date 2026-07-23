#pragma once
// MELONPRIME-PC-ADAPT: desktop stub so Sapphire FrameQueue.h compiles byte-identical; GL path is never taken on the Vulkan-only desktop build.
//
// Minimal subset of the EGL_KHR_fence_sync / EGL_KHR_reusable_sync extension
// surface actually named by FrameQueue.h/.cpp: EGLSyncKHR, EGL_NO_SYNC_KHR,
// and eglDestroySyncKHR(). The body lives in GlStubs.cpp and is a no-op; the
// desktop Vulkan renderer never takes the FrameBackend::OpenGlTexture path so
// this is never actually called.

#include <stdint.h>

#include "egl.h"

typedef void* EGLSyncKHR;
typedef intptr_t EGLAttribKHR;
typedef intptr_t EGLTimeKHR;

#define EGL_NO_SYNC_KHR ((EGLSyncKHR)0)
#define EGL_SYNC_FENCE_KHR 0x30F9
#define EGL_FOREVER_KHR 0xFFFFFFFFFFFFFFFFULL

// MELONPRIME-PC-ADAPT: rename the stub entry points via macros so ONLY the TUs
// including this compat header bind to the no-op stubs (see egl.h note). Extends
// the original eglDestroySyncKHR-only subset with eglCreateSyncKHR/eglWaitSyncKHR,
// which MelonInstanceVulkan.cpp's verbatim-copied runFrame()/prepareRenderFrame()
// reference on the (dead, on this Vulkan-only desktop build) OpenGlTexture frame
// backend path.
#define eglDestroySyncKHR melonprime_sapphire_stub_eglDestroySyncKHR
#define eglCreateSyncKHR melonprime_sapphire_stub_eglCreateSyncKHR
#define eglWaitSyncKHR melonprime_sapphire_stub_eglWaitSyncKHR

#ifdef __cplusplus
extern "C" {
#endif

EGLBoolean eglDestroySyncKHR(EGLDisplay display, EGLSyncKHR sync);
EGLSyncKHR eglCreateSyncKHR(EGLDisplay display, EGLenum type, const EGLAttribKHR* attrib_list);
EGLint eglWaitSyncKHR(EGLDisplay display, EGLSyncKHR sync, EGLint flags);

#ifdef __cplusplus
}
#endif
