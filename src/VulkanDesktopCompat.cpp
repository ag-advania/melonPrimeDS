#include "VulkanDesktopCompat.h"
namespace MelonDSAndroid {
bool isFastForwardActive(){return false;} bool areRendererDebugToolsEnabled(){return false;} bool areRendererDebugBgObjLogsEnabled(){return false;}
bool isRenderer3DDebugFeatureEnabled(melonDS::u32){return true;} bool areRenderer3DDebugControlsActive(){return false;} melonDS::u32 getVulkanDiagnosticFlags(){return 0;}
int getRenderer2DDebugForcedMode(melonDS::u32){return -1;} bool isRenderer2DDebugBgLayerEnabled(melonDS::u32,melonDS::u32){return true;}
bool areRenderer2DDebugControlsActive(){return false;}
bool isRenderer2DDebugBgPriorityEnabled(melonDS::u32,melonDS::u32){return true;} bool isRenderer2DDebugBackgroundKindEnabled(melonDS::u32){return true;}
bool areRenderer2DDebugObjectsEnabled(melonDS::u32){return true;} bool isRenderer2DDebugObjectPriorityEnabled(melonDS::u32,melonDS::u32){return true;}
bool isRenderer2DDebugObjectOrderEnabled(melonDS::u32,melonDS::u32){return true;} bool isRenderer2DDebugObjectFeatureEnabled(melonDS::u32){return true;}
}
