// MelonPrimeCustomHudScreenConfig.cpp
// ============================================================
// In-game HUD layout editor — on-screen config mode.
//
// This file is NOT a standalone translation unit.
// It is #include-d near the bottom of MelonPrimeCustomHud.cpp
// (inside namespace MelonPrime, inside #ifdef MELONPRIME_CUSTOM_HUD)
// and therefore shares all statics, types, and draw functions
// defined in that file.  Do NOT add it to CMakeLists.txt.
// ============================================================

// Definitions and element/property tables.
#include "MelonPrimeHudConfigOnScreenDefs.inc"

// ── Edit mode static state ──────────────────────────────────────────────────
static EmuInstance*     s_editEmu           = nullptr;
static int              s_editSelected      = -1;
static int              s_editHovered       = -1;
static bool             s_dragging          = false;
static bool             s_anchorPickerOpen  = false;  // expanded 3×3 anchor grid visible
static QPointF      s_dragStartDS;
static int          s_dragStartOfsX   = 0;
static int          s_dragStartOfsY   = 0;
static bool         s_resizingLength  = false;
static bool         s_resizingWidth   = false;
static int          s_resizeStartVal  = 0;
static QPointF      s_resizeStartDS;
static std::map<std::string, int> s_editSnapshot;
static std::set<std::string>      s_editSnapshotBools;
static std::map<std::string, double>      s_editSnapshotDoubles;
static std::map<std::string, std::string> s_editSnapshotStrings;
static std::function<void(int)>           s_editSelectionCb;

static void NotifySelectionChanged(int idx)
{
    if (s_editSelectionCb) s_editSelectionCb(idx);
}

// Context updated by render pass and mouse handlers
static float        s_editOriginX     = 0.0f;
static float        s_editOriginY     = 0.0f;

// Save/Cancel/Reset button rects in DS-space
static const QRectF kEditSaveRect  (10.0f,  1.0f, 74.0f, 12.0f);
static const QRectF kEditCancelRect(88.0f,  1.0f, 74.0f, 12.0f);
static const QRectF kEditResetRect (166.0f, 1.0f, 74.0f, 12.0f);

// Text Scale control, Auto-Scale Cap, Crosshair button, and Preview toggle (below button bar)
static const QRectF kEditTextScaleRect  (10.0f,  15.0f, 58.0f, 10.0f);
static const QRectF kEditAutoScaleCapRect(70.0f, 15.0f, 58.0f, 10.0f);
static const QRectF kEditCrosshairBtnRect(130.0f, 15.0f, 54.0f, 10.0f);

// Text Scale slider helpers (depend on kEditTextScaleRect)
static constexpr int kTsMin = 100, kTsMax = 300;
static constexpr float kTsLabelW = 14.0f;

static inline QRectF TsTrackRect()
{
    const float tsX = static_cast<float>(kEditTextScaleRect.left()) + 2.0f;
    const float tsY = static_cast<float>(kEditTextScaleRect.top());
    const float tsW = static_cast<float>(kEditTextScaleRect.width()) - 4.0f;
    const float tsH = static_cast<float>(kEditTextScaleRect.height());
    return QRectF(tsX + kTsLabelW, tsY + 2.0f, tsW - kTsLabelW, tsH - 4.0f);
}

static inline int TsValueFromX(float px)
{
    const QRectF tr = TsTrackRect();
    float frac = static_cast<float>((px - tr.left()) / tr.width());
    frac = std::max(0.0f, std::min(1.0f, frac));
    return std::max(kTsMin, std::min(kTsMax,
        kTsMin + static_cast<int>(std::round(frac * (kTsMax - kTsMin) / 5.0f) * 5.0f)));
}

// Auto-Scale Cap slider helpers (depend on kEditAutoScaleCapRect)
static constexpr int kAscMin = 100, kAscMax = 800;
static constexpr float kAscLabelW = 14.0f;

static inline QRectF AscTrackRect()
{
    const float x = static_cast<float>(kEditAutoScaleCapRect.left()) + 2.0f;
    const float y = static_cast<float>(kEditAutoScaleCapRect.top());
    const float w = static_cast<float>(kEditAutoScaleCapRect.width()) - 4.0f;
    const float h = static_cast<float>(kEditAutoScaleCapRect.height());
    return QRectF(x + kAscLabelW, y + 2.0f, w - kAscLabelW, h - 4.0f);
}

static inline int AscValueFromX(float px)
{
    const QRectF tr = AscTrackRect();
    float frac = static_cast<float>((px - tr.left()) / tr.width());
    frac = std::max(0.0f, std::min(1.0f, frac));
    return std::max(kAscMin, std::min(kAscMax,
        kAscMin + static_cast<int>(std::round(frac * (kAscMax - kAscMin) / 25.0f) * 25.0f)));
}
static const QRectF kEditPreviewBtnRect(186.0f, 15.0f, 54.0f, 10.0f);

// Crosshair panel rect (left side, below control bar)
static constexpr float kCrosshairPanelX = 2.0f;
static constexpr float kCrosshairPanelY = 28.0f;
// Max rows the crosshair panel can show — set large enough to fit all rows
// without scrolling (11 currently). At 8px/row + 4px padding, 16 rows = 132px
// which fits comfortably below the button bar (192 - 28 = 164px available).
static constexpr int kCrosshairMaxVisible = 16;

// Properties panel layout constants
static constexpr float kPropRowH    = 8.0f;
static constexpr float kPropLabelW  = 52.0f;
static constexpr float kPropCtrlW   = 36.0f;
static constexpr float kPropPanelW  = kPropLabelW + kPropCtrlW + 4.0f;
static constexpr float kAnchorGridCellW = (kPropPanelW - 4.0f) / 3.0f;
static constexpr float kAnchorGridCellH = 9.0f;
static constexpr float kAnchorGridH = kAnchorGridCellH * 3 + 4.0f;
static int s_editPropScroll = 0;
static constexpr int kPropMaxVisible = 8;
static constexpr bool kShowDsEditPropsPanel = false;

// ── Modern dark theme ────────────────────────────────────────────────────────
static const QColor kPanelBg        (10, 12, 24, 225);
static const QColor kPanelShadow    (0, 0, 0, 100);
static const QColor kPanelBorder    (50, 55, 80, 80);
static const QColor kAccent         (70, 140, 255);
static const QColor kAccentDim      (70, 140, 255, 60);
static const QColor kLabelColor     (120, 130, 165);
static const QColor kValueColor     (200, 210, 235);
static const QColor kCtrlBg         (18, 22, 42, 200);
static const QColor kBtnBg          (28, 32, 58, 220);
static const QColor kBtnOnBg        (25, 115, 55, 220);
static const QColor kBtnOffBg       (105, 32, 32, 210);
static const QColor kArrowBg        (28, 32, 58, 220);
static const QColor kElemSelBg      (70, 140, 255, 35);
static const QColor kElemHovBg      (70, 140, 255, 18);
static const QColor kElemNormBg     (50, 80, 130, 12);
static const QColor kElemHiddenSelBg(90, 90, 90, 45);
static const QColor kElemHiddenHovBg(70, 70, 70, 25);
static const QColor kElemHiddenBg   (50, 50, 50, 12);
static constexpr float kCornerRadius = 2.0f;
static constexpr float kBtnCorner    = 3.0f;

// Crosshair side panel (Inner/Outer) — positioned to the right of the main panel
static constexpr float kCrosshairSidePanelX  = kCrosshairPanelX + kPropPanelW + 2.0f;
static constexpr float kCrosshairPreviewX    = kCrosshairSidePanelX + kPropPanelW + 2.0f;
static constexpr int   kCrosshairPreviewSize = 64;

// ── Coordinate conversion ───────────────────────────────────────────────────
static inline QPointF WidgetToDS(const QPointF& pt)
{
    const float dsX = static_cast<float>((pt.x() - s_editOriginX) / s_editHudScale)
                      - (s_editTopStretchX - 1.0f) * 128.0f;
    const float dsY = static_cast<float>((pt.y() - s_editOriginY) / s_editHudScale);
    return QPointF(dsX, dsY);
}

// Snapshot, restore, and factory reset helpers.
#include "MelonPrimeHudConfigOnScreenSnapshot.inc"

// Bounds calculation and editor overlay drawing.
#include "MelonPrimeHudConfigOnScreenDraw.inc"

// Public edit-mode API and mouse/wheel input handling.
#include "MelonPrimeHudConfigOnScreenInput.inc"
