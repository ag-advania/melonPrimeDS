#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeHudRender.h"
#include "MelonPrimeHudConfigState.h"
#include "MelonPrimeHudRuntime.h"
#include "MelonPrimeHudRadar.h"
#include "MelonPrimeHudPatchLifecycle.h"
#include "MelonPrimeHudPresentationState.h"
#include "MelonPrimeHudEdit.h"
#include "MelonPrimeHudGoldenHarness.h"
#include "MelonPrimePatchNoHud.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeCompilerHints.h"
#include "MelonPrimeConstants.h"
#include "MelonPrimeZoomStatus.h"
#include "EmuInstance.h"
#include "NDS.h"
#include "Config.h"
#include "toml/toml.hpp"
#include "MelonPrime.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeHudGeometry.h"
#include "MelonPrimePerfProbe.h"
#include "MelonPrimeLocalization.h"
#include "MelonPrimeColorDialogPrefs.h"
#include "MelonPrimeHudPropSchema.inc"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QColor>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QInputDialog>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QString>
#include <QFile>
#include <QTextStream>
#include <array>
#include <algorithm>
#include <string>
#include <map>
#include <set>
#include <vector>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace MelonPrime {

// Bottom-screen radar art size in pixels (= SVG viewBox width/height).
static constexpr int kRadarArtSize = 76;

// Cached HUD config structs, loaders, scaling, and anchor recomputation.
#include "MelonPrimeHudRenderConfig.inc"

// Asset, icon, radar-frame, text, and outline caches/helpers.
#include "MelonPrimeHudRenderAssets.inc"

// Render-plan types, painter transform, and text/layout caches.  This remains
// before sampling because the neutral frame aggregate includes plan types.
#include "MelonPrimeHudRenderPlan.inc"

// Game-mode semantics, match cache, and NDS RAM -> snapshot sampling.  This
// fragment includes the presentation-text and state-ownership children at the
// points where their dependent types are complete.
#include "MelonPrimeHudRuntimeSample.inc"

// Ordered wrapper for runtime drawing, policy, radar preprocessing, patch
// lifecycle, and generation state child fragments.
#include "MelonPrimeHudRenderRuntime.inc"

// Primitive and element drawing: gauges, HP, weapons, inventory, crosshair.
#include "MelonPrimeHudRenderDraw.inc"

// CustomHud_Render and radar overlay entry points.
#include "MelonPrimeHudRenderMain.inc"

// =========================================================================
//  P-7: HUD Layout Editor unity fragment.
//  The on-screen editor shares the runtime HUD statics and helpers above.
// =========================================================================
#include "MelonPrimeHudConfigOnScreenUnity.inc"

// Developer-only golden hash harness.
#include "MelonPrimeHudGoldenHarness.inc"

#undef s_cacheEpoch
#undef s_cache

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
