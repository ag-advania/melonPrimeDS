#pragma once
// MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1
#include "types.h"
namespace MelonDSAndroid {
bool isFastForwardActive(); bool areRendererDebugToolsEnabled(); bool areRendererDebugBgObjLogsEnabled();
bool isRenderer3DDebugFeatureEnabled(melonDS::u32); bool areRenderer3DDebugControlsActive(); melonDS::u32 getVulkanDiagnosticFlags();
int getRenderer2DDebugForcedMode(melonDS::u32); bool isRenderer2DDebugBgLayerEnabled(melonDS::u32,melonDS::u32);
bool areRenderer2DDebugControlsActive();
bool isRenderer2DDebugBgPriorityEnabled(melonDS::u32,melonDS::u32); bool isRenderer2DDebugBackgroundKindEnabled(melonDS::u32);
bool areRenderer2DDebugObjectsEnabled(melonDS::u32); bool isRenderer2DDebugObjectPriorityEnabled(melonDS::u32,melonDS::u32);
bool isRenderer2DDebugObjectOrderEnabled(melonDS::u32,melonDS::u32); bool isRenderer2DDebugObjectFeatureEnabled(melonDS::u32);
}
