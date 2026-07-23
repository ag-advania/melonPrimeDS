// SapphireSupport.cpp
//
// Desktop (PC) definitions for the small set of MelonDSAndroid free functions that the
// verbatim-copied SapphireRhodonite Vulkan renderer sources reference but that live outside
// the copied renderer/ set (they are defined in the Android app layer, MelonDS.cpp).
//
// The copied renderer sources (VulkanOutput.cpp, VulkanSurfacePresenter.cpp) forward-declare
// these inside `namespace MelonDSAndroid` themselves, so no header is required here; this file
// only provides the link-time definitions for the desktop build.
//
// This file is MelonPrime-owned support glue, NOT a copied Sapphire file. Keep the Sapphire
// renderer sources verbatim and add any further desktop shims here instead of editing them.

#include <cstdlib>
#include <cstring>
#include <memory>

#include "types.h"
#include "MelonEventMessenger.h"

namespace MelonDSAndroid
{

namespace
{
// Reads a boolean-ish environment variable ("1"/"true"/"on" => true). Cheap; these helpers are
// only called on cold/debug paths, never per-pixel.
bool envFlag(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
        return false;
    return std::strcmp(value, "0") != 0
        && std::strcmp(value, "") != 0
        && std::strcmp(value, "false") != 0
        && std::strcmp(value, "off") != 0;
}
}

// Android reads a UI toggle; on desktop we gate the renderer debug tooling behind an env var so
// it stays off by default but remains reachable for developers.
bool areRendererDebugToolsEnabled()
{
    return envFlag("MELONPRIME_VULKAN_DEBUG_TOOLS");
}

bool areRendererDebugBgObjLogsEnabled()
{
    return envFlag("MELONPRIME_VULKAN_DEBUG_BGOBJ_LOGS");
}

// Android exposes an in-app fast-forward state. The desktop frontend has no equivalent wired into
// this renderer path yet, so report inactive. Wire this to the real fast-forward state if/when the
// desktop frontend exposes one.
bool isFastForwardActive()
{
    return false;
}

// MELONPRIME-PC-ADAPT: added for MelonInstanceVulkan.cpp (src/frontend/qt_sdl/sapphire/MelonInstanceVulkan.cpp),
// which references these MelonDSAndroid free functions (declared in reference MelonDS.h, defined in reference
// MelonDS.cpp — the Android app-layer orchestrator, which is out of scope to port). Same env-gated pattern as
// areRendererDebugToolsEnabled/areRendererDebugBgObjLogsEnabled above.
bool areRendererDebugLatchTraceLogsEnabled()
{
    return envFlag("MELONPRIME_VULKAN_DEBUG_LATCH_TRACE_LOGS");
}

// Android surfaces a per-screen forced-CompMode debug override from a developer UI toggle. Desktop has no
// equivalent UI wired up; report "no override" (-1, matching the reference's "unset" sentinel convention).
int getRenderer2DDebugForcedCompMode(bool topScreen)
{
    (void)topScreen;
    return -1;
}

// Android gates a 2D debug-controls UI surface. No desktop equivalent; report inactive.
bool areRenderer2DDebugControlsActive()
{
    return false;
}

// Referenced by the already-ported src/GPU3D_Vulkan.cpp (renderer diagnostic pass-index bits) and by
// MelonInstanceVulkan.cpp's verbatim-copied updateRenderer() logging. Android reads this from a JNI config
// value; desktop has no equivalent, so report "no diagnostic flags active".
melonDS::u32 getVulkanDiagnosticFlags()
{
    return 0;
}

// MelonInstance's Vulkan pipeline functions (precompileVulkanPipelines, handleVulkanRuntimeFailure,
// updateRenderer) report progress/failures through this optional Android event-messenger callback interface.
// All call sites are null-guarded (`if (eventMessenger) ...`); the desktop frontend has no equivalent wired
// up yet, so this stays null and those call sites become no-ops.
std::shared_ptr<MelonEventMessenger> eventMessenger = nullptr;

// Renderer-3D/2D debug gates referenced by the verbatim-copied core (GPU3D_Vulkan.cpp) and
// frontend (VulkanOutput.cpp) sources. The Sapphire release path enables every rendering
// feature when its debug-controls UI is unavailable; these desktop adapters must preserve
// that behavior or the copied renderer filters every polygon/background out.
bool areRenderer3DDebugControlsActive()
{
    return envFlag("MELONPRIME_VULKAN_DEBUG_3D_CONTROLS");
}

bool isRenderer3DDebugFeatureEnabled(melonDS::u32 /*featureFlag*/)
{
    return true;
}

bool isRenderer2DDebugBackgroundKindEnabled(melonDS::u32 /*featureFlag*/)
{
    return true;
}

}

// MELONPRIME-PC-ADAPT: link-time stubs for Android-app-layer classes whose headers the
// verbatim-extracted MelonInstanceVulkan keeps but whose implementations are Android-only.
#include "SaveManager.h"
#include "renderer/ScreenshotRenderer.h"

namespace MelonDSAndroid
{

// The extracted desktop MelonInstance omits loadRom()/bootFirmware() (desktop owns ROM/save
// lifecycle), so ndsSave/gbaSave/firmwareSave stay null and these members are never invoked at
// runtime; definitions exist purely so the verbatim-kept call sites link.
SaveManager::~SaveManager()
{
}

void SaveManager::CheckFlush()
{
}

// The reference ScreenshotRenderer renders via GLES (Android GL context); the desktop build has
// no GL context on the Vulkan path and MelonPrime has its own screenshot facility. Stubs keep the
// verbatim-kept construction/usage sites linking; getScreenshot() returns the caller-owned buffer
// unchanged and isScreenshotPending() reports none.
ScreenshotRenderer::ScreenshotRenderer(u32* screenshotBuffer)
    : screenshotBuffer(screenshotBuffer),
      frameBuffer(0),
      bufferTexture(0),
      vao(0),
      vbo(0),
      screenshotRenderVertexShader(0),
      screenshotRenderFragmentShader(0),
      screenshotRenderShader(0),
      textureUniformLocation(0),
      posAttribLocation(0),
      texCoordAttribLocation(0),
      screenshotRequested(false),
      stopped(false)
{
}

void ScreenshotRenderer::renderScreenshot(GPU* /*gpu*/, Renderer /*renderer*/, Frame* /*renderFrame*/)
{
}

u32* ScreenshotRenderer::getScreenshot()
{
    return screenshotBuffer;
}

bool ScreenshotRenderer::isScreenshotPending()
{
    return false;
}

}
