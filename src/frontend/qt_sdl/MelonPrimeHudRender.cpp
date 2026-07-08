#ifdef MELONPRIME_CUSTOM_HUD

#include "MelonPrimeHudRender.h"
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
//  P-7: HUD Layout Editor unity fragment.
//  The on-screen editor shares the runtime HUD statics and helpers above.
// =========================================================================
#include "MelonPrimeHudConfigOnScreenUnity.inc"

// Developer-only golden hash harness.
#include "MelonPrimeHudGoldenHarness.inc"

} // namespace MelonPrime

#endif // MELONPRIME_CUSTOM_HUD
