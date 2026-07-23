#pragma once
// MELONPRIME-PC-ADAPT: desktop stub so Sapphire FrameQueue.h compiles byte-identical; GL path is never taken on the Vulkan-only desktop build.
//
// FrameQueue.h includes "OpenGLContext.h" but never names the OpenGLContext
// class itself; this mirrors the reference melonDS-android OpenGLContext.h
// class shape (see melonDS-android/app/src/main/cpp/OpenGLContext.h) purely
// so the #include resolves and parses. Method bodies are not required since
// nothing in the desktop Vulkan build instantiates or calls this class.

#include <EGL/egl.h>

class OpenGLContext
{
public:
    bool InitContext(long sharedGlContext);
    bool Use();
    void Release();
    void DeInit();
    [[nodiscard]] EGLContext GetContext() const { return glContext; }

private:
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext glContext = EGL_NO_CONTEXT;
};
