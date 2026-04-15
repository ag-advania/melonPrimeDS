#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeHudRender.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeConstants.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "Config.h"
#include "toml/toml.hpp"
#include "MelonPrime.h"
#include "MelonPrimeDef.h"

#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QColorDialog>
#include <QInputDialog>
#include <algorithm>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <cmath>
#include <climits>
#include <cstdio>
#include <cstring>

namespace MelonPrime {

// Asset, icon, radar-frame, text, and outline caches/helpers.
#include "MelonPrimeHudRenderAssets.inc"

// Cached HUD config structs, loaders, scaling, and anchor recomputation.
#include "MelonPrimeHudRenderConfig.inc"

// Battle/match state, frame runtime helpers, hide rules, and NoHUD patching.
#include "MelonPrimeHudRenderRuntime.inc"

// Primitive and element drawing: gauges, HP, weapons, inventory, crosshair.
#include "MelonPrimeHudRenderDraw.inc"

// CustomHud_Render and radar overlay entry points.
#include "MelonPrimeHudRenderMain.inc"

// =========================================================================
//  P-7: HUD Layout Editor — implementation lives in a separate file.
//  This is a unity-build include: HudConfigScreen shares all statics above.
// =========================================================================
#include "MelonPrimeHudConfigOnScreen.cpp"

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD





