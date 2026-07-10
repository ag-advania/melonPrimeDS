// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  P-3: Cached HUD config — refreshed only when config generation changes.
//       Avoids ~50 hash-map lookups per frame → single generation compare.
// =========================================================================
struct HudOutlineConfig {
    bool   enable;
    QColor color;
    QColor colorWithAlpha; // P-14: pre-computed color with opacity baked in
    float  opacity;
    int    thickness;
};
// Color-by-value ramp: when enabled, picks a color from `count` stops based on
// the element's current percentage (value/max). Each stop is a "minimum percent"
// threshold; eval picks the stop with the largest threshold <= pct. Used for
// HP label/gauge and ammo label/gauge. kHudColorRampMax must match the UI
// descriptor row count and the "Number of Colors" spinbox max in
// MelonPrimeInputConfig.cpp.
static constexpr int kHudColorRampMax = 6;
struct HudColorRamp {
    bool   enabled;
    int    count;                            // active stops, clamped [1, kHudColorRampMax]
    int    thresholdPct[kHudColorRampMax];   // 0..100 (minimum % for this color)
    QColor color[kHudColorRampMax];
};
struct HpHudConfig {
    int hpX, hpY, hpAlign;          // final (computed from anchor + offset + stretch)
    int hpAnchor, hpOfsX, hpOfsY;   // raw config values
    char hpPrefix[48];
    bool hpGauge;
    QColor hpTextColor;
    int hpGaugeOri, hpGaugeLen, hpGaugeWid;
    int hpGaugeAlign;
    int hpGaugeOfsX, hpGaugeOfsY, hpGaugeAnchor;
    int hpGaugePosMode, hpGaugePosX, hpGaugePosY;  // final
    int hpGaugePosAnchor, hpGaugePosOfsX, hpGaugePosOfsY;  // raw
    int hpTextAnchor;            // which side of gauge the text appears (mode 2 only)
    int hpTextOfsX, hpTextOfsY;  // text offset from gauge (mode 2 only)
    QColor hpGaugeColor;
    HudColorRamp hpTextRamp;
    HudColorRamp hpGaugeRamp;
    HudOutlineConfig outline;
    HudOutlineConfig gaugeOutline;
};
struct WeaponHudConfig {
    int wpnX, wpnY, ammoAlign;           // final
    int wpnAnchor, wpnOfsX, wpnOfsY;     // raw
    char ammoPrefix[48];
    QColor ammoTextColor;
    bool iconShow, ammoGauge;
    bool iconOverlayEnable[9];
    QColor iconOverlayColor[9];
    int iconHeight;
    int iconMode, iconOfsX, iconOfsY, iconPosX, iconPosY;  // iconPosX/Y = final
    int iconPosAnchor, iconPosOfsX, iconPosOfsY;            // raw
    int iconAnchorX, iconAnchorY;
    int ammoGaugeOri, ammoGaugeLen, ammoGaugeWid;
    int ammoGaugeAlign;
    int ammoGaugeOfsX, ammoGaugeOfsY, ammoGaugeAnchor;
    int ammoGaugePosMode, ammoGaugePosX, ammoGaugePosY;     // final
    int ammoGaugePosAnchor, ammoGaugePosOfsX, ammoGaugePosOfsY;  // raw
    int ammoTextAnchor;              // which side of gauge the text appears (mode 2 only)
    int ammoTextOfsX, ammoTextOfsY;  // text offset from gauge (mode 2 only)
    QColor ammoGaugeColor;
    HudColorRamp ammoTextRamp;
    HudColorRamp ammoGaugeRamp;
    HudOutlineConfig outline;
    HudOutlineConfig iconOutline;
    HudOutlineConfig gaugeOutline;
};
struct WeaponInventoryHudConfig {
    bool    show;
    int     anchor, ofsX, ofsY;   // raw anchor + offsets
    int     posX, posY;            // computed final DS-space position
    int     align;                 // 0=Left 1=Center 2=Right
    int     orientation;           // 0=Horizontal 1=Vertical
    int     iconHeight;            // DS-space icon height (loaded at iconH * scaleIcons px)
    int     spacing;               // DS-space gap between weapon entries
    float   opacity;               // owned-weapon opacity
    float   notOwnedOpacity;       // unowned-weapon opacity (0 = hide)
    QColor  textColor;
    HudOutlineConfig outline;      // text outline
    HudOutlineConfig iconOutline;  // icon outline
    bool    highlightEnable;       // highlight currently selected weapon
    QColor  highlightColor;
    float   highlightOpacity;
    float   highlightThickness;    // border line width (DS-space)
    int     highlightPadding;      // padding around icon+text (DS-space px)
    int     highlightCornerRadius; // rounded corner radius (DS-space px)
    int     highlightOffsetLeft;   // expand/shrink rect left edge (DS-space px)
    int     highlightOffsetRight;  // expand/shrink rect right edge
    int     highlightOffsetTop;    // expand/shrink rect top edge
    int     highlightOffsetBottom; // expand/shrink rect bottom edge
};
struct CrosshairHudConfig {
    QColor chColor;
    bool chOutline, chCenterDot, chTStyle;
    int chDotShape;          // 0=Square, 1=Circle
    int chZoomScopeDotShape; // 0=Square, 1=Circle
    double chOutlineOpacity, chDotOpacity;
    int chOutlineThickness, chDotThickness;
    bool chInnerShow;
    double chInnerOpacity;
    int chInnerLengthX, chInnerLengthY, chInnerThickness, chInnerOffset;
    bool chOuterShow;
    double chOuterOpacity;
    int chOuterLengthX, chOuterLengthY, chOuterThickness, chOuterOffset;
    double chScale; // visual scale multiplier (1.0 = 100%)
    bool chZoomStageEnable;
    double chZoomScale;   // target multiplier while fully zoomed
    double chZoomOpacity; // target opacity multiplier while fully zoomed
    bool chZoomScopeEnable, chZoomScopeCenterDot;
    int chZoomScopeRadius, chZoomScopeGap, chZoomScopeThickness;
    int chZoomScopeDotSize;
    double chZoomScopeDotOpacity, chZoomScopeOpacity;
    bool chZoomTransitionEnable, chZoomTransitionPulseEnable;
    int chZoomTransitionStyle;
    float chZoomTransitionStep;
    double chZoomTransitionPulseStrength;
    bool chZoomScopeDotCustomColor;
    QColor chZoomScopeDotColor, chZoomScopeDotDrawColor;
    // P-11: Pre-computed arm/dot colors with alpha set (avoids per-frame QColor copy + setAlphaF)
    QColor chInnerColor, chOuterColor, chDotColor;
    // Cached arm rects in output pixels, centred at (0,0).
    // DrawCrosshair translates to the rounded pixel position of (cx,cy) each frame.
    QRect cachedInnerRects[4], cachedOuterRects[4];
    int   cachedInnerCount, cachedOuterCount;
    int   cachedDotHalfPx;   // half-size of the centre dot in output pixels
    int   cachedDotSizePx;   // full size of the centre dot in output pixels
    bool  cachedZoomAffectsVisual;
    bool  cachedZoomScopeRenderable;
    int   cachedZoomScopeLineEndPx;
    int   cachedZoomScopeGapPx;
    QRect cachedZoomScopeBboxPx;
};
struct MatchStatusHudConfig {
    bool matchStatusShow;
    int matchStatusX, matchStatusY;                     // final
    int matchStatusAnchor, matchStatusOfsX, matchStatusOfsY;  // raw
    int matchStatusLabelOfsX, matchStatusLabelOfsY;
    int matchStatusLabelPos;
    char matchStatusLabelPoints[64], matchStatusLabelOctoliths[64], matchStatusLabelLives[64];
    char matchStatusLabelRingTime[64], matchStatusLabelPrimeTime[64];
    QColor matchStatusColor, matchStatusLabelColor, matchStatusValueColor, matchStatusSepColor, matchStatusGoalColor;
    HudOutlineConfig outline;
};
struct BombLeftHudConfig {
    bool bombLeftShow, bombLeftTextShow;
    int bombLeftX, bombLeftY, bombLeftAlign;                  // final
    int bombLeftAnchor, bombLeftOfsX, bombLeftOfsY;            // raw
    QColor bombLeftColor;
    char bombLeftPrefix[48];
    char bombLeftSuffix[48];
    bool bombIconShow, bombIconColorOverlay;
    QColor bombIconColor;
    int bombIconHeight;
    int bombIconMode, bombIconOfsX, bombIconOfsY, bombIconPosX, bombIconPosY;  // iconPosX/Y = final
    int bombIconPosAnchor, bombIconPosOfsX, bombIconPosOfsY;                    // raw
    int bombIconAnchorX, bombIconAnchorY;
    HudOutlineConfig outline;
    HudOutlineConfig iconOutline;
};
struct RankTimeHudConfig {
    bool rankShow;
    int rankX, rankY, rankAlign;                       // final
    int rankAnchor, rankOfsX, rankOfsY;                // raw
    QColor rankColor;
    char rankPrefix[48];
    bool rankShowOrdinal;
    char rankSuffix[48];
    bool timeLeftShow;
    int timeLeftX, timeLeftY, timeLeftAlign;            // final
    int timeLeftAnchor, timeLeftOfsX, timeLeftOfsY;     // raw
    QColor timeLeftColor;
    bool timeLimitShow;
    int timeLimitX, timeLimitY, timeLimitAlign;         // final
    int timeLimitAnchor, timeLimitOfsX, timeLimitOfsY;  // raw
    QColor timeLimitColor;
    HudOutlineConfig rankOutline;
    HudOutlineConfig timeLeftOutline;
    HudOutlineConfig timeLimitOutline;
};
struct RadarOverlayConfig {
    bool radarShow;
    int radarDstX, radarDstY, radarDstSize;             // final
    int radarAnchor, radarOfsX, radarOfsY;               // raw
    int radarSrcRadius;
    double radarOpacity;
    QColor radarFrameColor;  // independent color for the radar frame SVG
    bool radarFrameColorUseHunter;
    QRect radarDstRect;
    QPainterPath radarClipPath;
    // P-15: Pre-computed radar frame geometry (avoids redundant per-draw calculations)
    QRectF frameDstRect;
    float expandDS;   // outline thickness in DS-space
    HudOutlineConfig outline;
    HudOutlineConfig frameOutline;
};
struct CachedHudConfig {
    HpHudConfig hp;
    WeaponHudConfig weapon;
    WeaponInventoryHudConfig weaponInventory;
    CrosshairHudConfig crosshair;
    MatchStatusHudConfig matchStatus;
    BombLeftHudConfig bombLeft;
    RankTimeHudConfig rankTime;
    RadarOverlayConfig radar;
    HudOutlineConfig globalOutline; // when enable=true, overrides all per-element outlines
    int textScalePct; // text visual scale in percent (100 = 1x, bitmap always rendered at 6px)
    float textDrawScale; // P-14: pre-computed combined text scale (auto + user offset) / hudScale
    // Auto-scale: per-category effective scale factors (integer resolution scale capped by user)
    float scaleText;      // effective text auto-scale factor (capped)
    float scaleIcons;     // effective icons auto-scale factor (capped)
    float scaleGauges;    // effective gauges auto-scale factor (capped)
    float scaleCrosshair; // effective crosshair auto-scale factor (capped)
    float lastStretchX; // topStretchX used for last anchor position computation
    float lastHudScale;  // hudScale (scaleY) at last render — needed for outline thickness conversion
    // Per-element opacity values (cached to avoid per-frame config lookups)
    float hpOpacity, weaponOpacity, matchStatusOpacity, rankOpacity, bombLeftOpacity;
    float hpGaugeOpacity, wpnIconOpacity, ammoGaugeOpacity;
    float bombIconOpacity, timeLeftOpacity, timeLimitOpacity;
    // DisableDefaultHud: bitmask of NoHudElement bits the user wants hidden.
    // Cached once per epoch so the hot path doesn't issue per-element GetBool calls every frame.
    uint16_t noHudMask;
    bool hiResText;  // HudFontMode != 0: rasterise glyphs at output resolution (hi-DPI text)
    bool valid;
};
static CachedHudConfig s_cache = { .valid = false };
static uint32_t s_cacheEpoch = 1;

static QRect ScaleRectAroundOrigin(const QRect& r, double scale)
{
    if (std::abs(scale - 1.0) < 0.001)
        return r;

    const double x1 = static_cast<double>(r.left()) * scale;
    const double y1 = static_cast<double>(r.top()) * scale;
    const double x2 = static_cast<double>(r.left() + r.width()) * scale;
    const double y2 = static_cast<double>(r.top() + r.height()) * scale;
    const int left = static_cast<int>(std::floor(std::min(x1, x2)));
    const int top = static_cast<int>(std::floor(std::min(y1, y2)));
    const int right = static_cast<int>(std::ceil(std::max(x1, x2)));
    const int bottom = static_cast<int>(std::ceil(std::max(y1, y2)));
    return QRect(left, top, std::max(1, right - left), std::max(1, bottom - top));
}

static QRect CrosshairScopeBboxPx(int cx, int cy, const CrosshairHudConfig& ch,
                                  double scopeT, const QRect& scopeClipPx = QRect())
{
    if (!ch.cachedZoomScopeRenderable || scopeT <= 0.001)
        return QRect();
    const double geom = std::clamp(scopeT, 0.0, 1.0);
    QRect scopeBbox;
    if (geom >= 0.999) {
        scopeBbox = ch.cachedZoomScopeBboxPx;
    } else {
        const int w = std::max(1, static_cast<int>(ch.cachedZoomScopeBboxPx.width() * geom + 0.5));
        const int h = std::max(1, static_cast<int>(ch.cachedZoomScopeBboxPx.height() * geom + 0.5));
        scopeBbox = QRect(-w / 2, -h / 2, w, h);
    }
    scopeBbox.translate(cx, cy);
    return scopeClipPx.isEmpty() ? scopeBbox : (scopeBbox & scopeClipPx);
}

static QRect CrosshairPulseBboxPx(int cx, int cy, const CrosshairHudConfig& ch,
                                  double zoomT, double pulseT)
{
    if (pulseT <= 0.01 || !ch.cachedZoomScopeRenderable)
        return QRect();
    const int maxR = ch.chZoomScopeRadius;
    const int r0 = std::max(4, maxR / 8);
    const int ringR = r0 + static_cast<int>((maxR - r0) * std::clamp(zoomT, 0.0, 1.0) + 0.5);
    const int pad = std::max(2, ch.chZoomScopeThickness + 1);
    return QRect(cx - ringR - pad, cy - ringR - pad,
                 ringR * 2 + pad * 2 + 1, ringR * 2 + pad * 2 + 1);
}

// OPT-DR1: Returns the pixel-space bounding rect of the crosshair rendered at (cx, cy).
// Uses the pre-computed stamp, arm rects, and dot size from CrosshairHudConfig.
// Result is in overlay pixel space (same as Overlay[0]).
static QRect CrosshairBboxPx(int cx, int cy, const CrosshairHudConfig& ch,
                             double geomScale = 1.0, double zoomT = 0.0,
                             double scopeT = 0.0, double pulseT = 0.0,
                             const QRect& scopeClipPx = QRect())
{
    QRect r;
    const bool scaled = std::abs(geomScale - 1.0) >= 0.001;
    if (!scaled && s_chStampValid && !s_chOutlineStamp.isNull())
        r = QRect(cx + s_chStampOrigin.x(), cy + s_chStampOrigin.y(),
                  s_chOutlineStamp.width(), s_chOutlineStamp.height());
    const int olT = (ch.chOutline && ch.chOutlineOpacity > 0.0)
        ? std::max(1, ch.chOutlineThickness)
        : 0;
    auto addRect = [&](const QRect& base) {
        QRect scaledRect = ScaleRectAroundOrigin(base, geomScale);
        if (scaled && olT > 0)
            r |= scaledRect.adjusted(-olT, -olT, olT, olT).translated(cx, cy);
        r |= scaledRect.translated(cx, cy);
    };
    for (int i = 0; i < ch.cachedInnerCount; ++i) addRect(ch.cachedInnerRects[i]);
    for (int i = 0; i < ch.cachedOuterCount; ++i) addRect(ch.cachedOuterRects[i]);
    if (ch.chCenterDot) {
        const int dh = ch.cachedDotHalfPx, ds = ch.cachedDotSizePx;
        QRect dot = ScaleRectAroundOrigin(QRect(-dh, -dh, ds, ds), geomScale);
        if (scaled && olT > 0)
            r |= dot.adjusted(-olT, -olT, olT, olT).translated(cx, cy);
        r |= dot.translated(cx, cy);
    }
    r |= CrosshairScopeBboxPx(cx, cy, ch, scopeT, scopeClipPx);
    r |= CrosshairPulseBboxPx(cx, cy, ch, zoomT, pulseT);
    // Transition FX overlays (DrawZoomTransitionFx) are drawn out to the full
    // scope radius during mid-transition regardless of scopeT, so the scope-
    // reticle bbox above (which scales with scopeT) does not cover them. Include
    // the full-radius extent so the dirty-rect upload region clears/uploads the
    // whole FX and avoids clipping/trails. Inlined midFx>=0.015 gate mirrors
    // DrawZoomTransitionFx (CrosshairTransitionMidIntensity is defined later in
    // the unity build, so the 4*t*(1-t) form is spelled out here).
    if (ch.chZoomTransitionEnable && ch.cachedZoomScopeRenderable
        && 4.0 * zoomT * (1.0 - zoomT) >= 0.015) {
        const int pad = std::max(4, ch.chZoomScopeThickness + 2);
        QRect fx = ch.cachedZoomScopeBboxPx.adjusted(-pad, -pad, pad, pad)
                       .translated(cx, cy);
        r |= scopeClipPx.isEmpty() ? fx : (fx & scopeClipPx);
    }
    return r;
}

static void RebuildCrosshairZoomScopeCache(CrosshairHudConfig& crosshair)
{
    s_chScopeStampValid = false;
    crosshair.cachedZoomScopeRenderable =
        crosshair.chZoomStageEnable
        && crosshair.chZoomScopeEnable
        && crosshair.chZoomScopeOpacity > 0.0;
    crosshair.cachedZoomAffectsVisual =
        crosshair.chZoomStageEnable
        && (std::abs(crosshair.chZoomScale - 1.0) >= 0.001
            || std::abs(crosshair.chZoomOpacity - 1.0) >= 0.001
            || crosshair.cachedZoomScopeRenderable);
    crosshair.cachedZoomScopeLineEndPx = crosshair.chZoomScopeRadius;
    crosshair.cachedZoomScopeGapPx = crosshair.chZoomScopeGap;
    crosshair.cachedZoomScopeBboxPx = QRect();
    if (!crosshair.cachedZoomScopeRenderable)
        return;

    const int radius = crosshair.chZoomScopeRadius;
    const int armEnd = crosshair.cachedZoomScopeLineEndPx;
    const int gap = crosshair.cachedZoomScopeGapPx;
    const int halfT = (crosshair.chZoomScopeThickness + 1) / 2;
    QRect r(-radius, -radius, radius * 2 + 1, radius * 2 + 1);
    if (armEnd > gap) {
        r |= QRect(-armEnd, -halfT, armEnd - gap, halfT * 2 + 1);
        r |= QRect(gap, -halfT, armEnd - gap, halfT * 2 + 1);
        r |= QRect(-halfT, -armEnd, halfT * 2 + 1, armEnd - gap);
        r |= QRect(-halfT, gap, halfT * 2 + 1, armEnd - gap);
    }
    if (crosshair.chZoomScopeCenterDot && crosshair.chZoomScopeDotOpacity > 0.0) {
        const int dotSize = std::max(1, crosshair.chZoomScopeDotSize);
        const int dotHalf = dotSize / 2;
        r |= QRect(-dotHalf, -dotHalf, dotSize, dotSize);
    }

    int pad = halfT + 1;
    if (crosshair.chOutline && crosshair.chOutlineOpacity > 0.0)
        pad += crosshair.chOutlineThickness;
    crosshair.cachedZoomScopeBboxPx = r.adjusted(-pad, -pad, pad, pad);
}

// P-9: Frame-level font metrics — set once in CustomHud_Render, shared by all draw sub-functions.
// OPT-HUD-6: s_frameFont caches the QFont so draw functions never call p->font() per-frame.
//   p->font() creates a CoW ref-increment even on cache-hit paths (~10–20 cyc × 6 callers).
static QFontMetrics s_frameFm{QFont{}};
static int s_frameFpx = 0;
static QFont s_frameFont;

// =========================================================================
//  Selectable HUD font (default MPH / system font / font file)
//  Single source of truth shared by the runtime render (overlayFont),
//  the settings-dialog previews, and the render cache.
// =========================================================================

// Bundled MPH pixel font (:/mph-font). Loaded once; addApplicationFont is idempotent.
static const QString& MphFontFamily()
{
    static QString s_mphFamily = []() -> QString {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/mph-font"));
        if (id >= 0) {
            const QStringList fams = QFontDatabase::applicationFontFamilies(id);
            if (!fams.isEmpty())
                return fams.at(0);
        }
        return QString();
    }();
    return s_mphFamily;
}

// Register a font file once and return its family. Cached by absolute path so the
// per-frame resolver never re-runs addApplicationFont for an already-loaded file.
// Returns an empty string if the file cannot be loaded.
// Guarded by a mutex: the resolver is called from both the GUI thread (settings
// preview) and the screen render path, so the cache mutation must be serialized.
static QString LoadAppFontFamilyCached(const QString& path)
{
    static QMutex s_mutex;
    static QHash<QString, QString> s_fileFamilies;  // path -> family ("" = known-bad)
    QMutexLocker lock(&s_mutex);

    auto it = s_fileFamilies.find(path);
    if (it != s_fileFamilies.end())
        return it.value();

    QString family;
    const int id = QFontDatabase::addApplicationFont(path);
    if (id >= 0) {
        const QStringList fams = QFontDatabase::applicationFontFamilies(id);
        if (!fams.isEmpty())
            family = fams.at(0);
    }
    s_fileFamilies.insert(path, family);
    return family;
}

// Apply the user's style/effects (weight, italic, underline, strikeout) from the HUD FONT
// picker. Only used for system/file fonts — MPH stays a clean pixel font.
static void ApplyHudFontStyle(QFont& f, Config::Table& cfg)
{
    f.setStyleStrategy(QFont::PreferAntialias);
    // Weight index 0..8 -> QFont::Weight Thin..Black.
    static const int kWeights[9] = { 100, 200, 300, 400, 500, 600, 700, 800, 900 };
    const int wi = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_HudFontWeight), 0, 8);
    f.setWeight(static_cast<QFont::Weight>(kWeights[wi]));
    f.setItalic(cfg.GetBool(MP_HUD_PROP_KEY_HudFontItalic));
    f.setUnderline(cfg.GetBool(MP_HUD_PROP_KEY_HudFontUnderline));
    f.setStrikeOut(cfg.GetBool(MP_HUD_PROP_KEY_HudFontStrikeOut));
}

QFont CustomHud_ResolveBaseFont(Config::Table& cfg)
{
    const int mode = cfg.GetInt(MP_HUD_PROP_KEY_HudFontMode);

    if (mode == 1) {
        // System font family.
        const QString fam = QString::fromStdString(cfg.GetString(MP_HUD_PROP_KEY_HudFontFamily));
        if (!fam.isEmpty()) {
            QFont f(fam);
            ApplyHudFontStyle(f, cfg);
            return f;
        }
    } else if (mode == 2) {
        // Font file on disk.
        const QString path = QString::fromStdString(cfg.GetString(MP_HUD_PROP_KEY_HudFontFile));
        if (!path.isEmpty()) {
            const QString fam = LoadAppFontFamilyCached(path);
            if (!fam.isEmpty()) {
                QFont f(fam);
                ApplyHudFontStyle(f, cfg);
                return f;
            }
        }
    }

    // Mode 0, or any empty/invalid selection: bundled MPH pixel font.
    const QString& mph = MphFontFamily();
    QFont f(mph.isEmpty() ? QStringLiteral("Consolas") : mph);
    f.setStyleStrategy(QFont::NoAntialias);
    f.setHintingPreference(QFont::PreferFullHinting);
    return f;
}

int CustomHud_ResolveFontPixelSize(Config::Table& cfg)
{
    // MPH is tuned for its native 6px; only system/file fonts get a configurable size.
    if (cfg.GetInt(MP_HUD_PROP_KEY_HudFontMode) == 0)
        return kCustomHudFontSize;
    return std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_HudFontSize), 4, 64);
}

static inline void CopyConfigString(char* dst, size_t dstSize, const std::string& value)
{
    std::strncpy(dst, value.c_str(), dstSize - 1);
    dst[dstSize - 1] = '\0';
}
static inline QColor ReadRgbColor(Config::Table& cfg, const char* keyR, const char* keyG, const char* keyB)
{
    return QColor(cfg.GetInt(keyR), cfg.GetInt(keyG), cfg.GetInt(keyB));
}
static inline QColor ReadOptionalSubColor(Config::Table& cfg, const char* keyOverall,
                                          const char* keyR, const char* keyG, const char* keyB)
{
    if (cfg.GetBool(keyOverall)) return QColor();
    return ReadRgbColor(cfg, keyR, keyG, keyB);
}
struct HudColorRampKeys {
    const char* enable;
    const char* count;
    const char* pct[kHudColorRampMax];
    const char* colorR[kHudColorRampMax];
    const char* colorG[kHudColorRampMax];
    const char* colorB[kHudColorRampMax];
};

#define MP_HUD_RAMP_KEYS(prefix) { \
    MP_HUD_PROP_KEY_##prefix##Enable, \
    MP_HUD_PROP_KEY_##prefix##Count, \
    { MP_HUD_PROP_KEY_##prefix##1Pct, MP_HUD_PROP_KEY_##prefix##2Pct, MP_HUD_PROP_KEY_##prefix##3Pct, MP_HUD_PROP_KEY_##prefix##4Pct, MP_HUD_PROP_KEY_##prefix##5Pct, MP_HUD_PROP_KEY_##prefix##6Pct }, \
    { MP_HUD_PROP_KEY_##prefix##1R, MP_HUD_PROP_KEY_##prefix##2R, MP_HUD_PROP_KEY_##prefix##3R, MP_HUD_PROP_KEY_##prefix##4R, MP_HUD_PROP_KEY_##prefix##5R, MP_HUD_PROP_KEY_##prefix##6R }, \
    { MP_HUD_PROP_KEY_##prefix##1G, MP_HUD_PROP_KEY_##prefix##2G, MP_HUD_PROP_KEY_##prefix##3G, MP_HUD_PROP_KEY_##prefix##4G, MP_HUD_PROP_KEY_##prefix##5G, MP_HUD_PROP_KEY_##prefix##6G }, \
    { MP_HUD_PROP_KEY_##prefix##1B, MP_HUD_PROP_KEY_##prefix##2B, MP_HUD_PROP_KEY_##prefix##3B, MP_HUD_PROP_KEY_##prefix##4B, MP_HUD_PROP_KEY_##prefix##5B, MP_HUD_PROP_KEY_##prefix##6B } \
}

// Loads a color-by-value ramp. Cold path (config change only).
static void LoadColorRamp(HudColorRamp& r, Config::Table& cfg, const HudColorRampKeys& keys)
{
    r.enabled = cfg.GetBool(keys.enable);
    r.count = std::clamp(cfg.GetInt(keys.count), 1, kHudColorRampMax);
    for (int i = 0; i < kHudColorRampMax; i++) {
        r.thresholdPct[i] = std::clamp(cfg.GetInt(keys.pct[i]), 0, 100);
        r.color[i] = ReadRgbColor(cfg, keys.colorR[i], keys.colorG[i], keys.colorB[i]);
    }
}

// ApplyAnchor: converts anchor index + offset into final coordinates.
// anchor 0=TL 1=TC 2=TR 3=ML 4=MC 5=MR 6=BL 7=BC 8=BR
// topStretchX = scaleX/scaleY: >1 when wider than 4:3, <1 when narrower.
// Adjusts X anchor bases so left/right edges track the actual visible area.
// With the painter set to scale(hudScale,hudScale) + translate((topStretchX-1)*128, 0),
// the visible X range in DS coords is [(1-sx)*128 .. 128*(sx+1)].
static inline void ApplyAnchor(int anchor, int offsetX, int offsetY,
                                int& outX, int& outY, float topStretchX = 1.0f)
{
    HudGeometry::ApplyAnchor(anchor, offsetX, offsetY, outX, outY, topStretchX);
}

static void LoadHpConfig(HpHudConfig& hp, Config::Table& cfg)
{
    hp.hpAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudHpAnchor);
    hp.hpOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudHpX);
    hp.hpOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudHpY);
    hp.hpAlign = cfg.GetInt(MP_HUD_PROP_KEY_HudHpAlign);
    CopyConfigString(hp.hpPrefix, sizeof(hp.hpPrefix), cfg.GetString(MP_HUD_PROP_KEY_HudHpPrefix));
    hp.hpTextColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudHpTextColorR, MP_HUD_PROP_KEY_HudHpTextColorG, MP_HUD_PROP_KEY_HudHpTextColorB);
    hp.hpGauge = cfg.GetBool(MP_HUD_PROP_KEY_HudHpGauge);
    hp.hpGaugeOri = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeOrientation);
    hp.hpGaugeLen = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeLength);
    hp.hpGaugeWid = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeWidth);
    hp.hpGaugeAlign = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeAlign);
    hp.hpGaugeOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeOffsetX);
    hp.hpGaugeOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeOffsetY);
    hp.hpGaugeAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeAnchor);
    hp.hpGaugePosMode = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosMode);
    hp.hpGaugePosAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosAnchor);
    hp.hpGaugePosOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosX);
    hp.hpGaugePosOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosY);
    hp.hpTextAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudHpTextAnchor);
    hp.hpTextOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudHpTextOffsetX);
    hp.hpTextOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudHpTextOffsetY);
    hp.hpGaugeColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudHpGaugeColorR, MP_HUD_PROP_KEY_HudHpGaugeColorG, MP_HUD_PROP_KEY_HudHpGaugeColorB);
    LoadColorRamp(hp.hpTextRamp,  cfg, MP_HUD_RAMP_KEYS(HudHpTextRamp));
    LoadColorRamp(hp.hpGaugeRamp, cfg, MP_HUD_RAMP_KEYS(HudHpGaugeRamp));
}
static void LoadWeaponConfig(WeaponHudConfig& weapon, Config::Table& cfg)
{
    weapon.wpnAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponAnchor);
    weapon.wpnOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponX);
    weapon.wpnOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponY);
    weapon.ammoAlign = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoAlign);
    CopyConfigString(weapon.ammoPrefix, sizeof(weapon.ammoPrefix), cfg.GetString(MP_HUD_PROP_KEY_HudAmmoPrefix));
    weapon.ammoTextColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudAmmoTextColorR, MP_HUD_PROP_KEY_HudAmmoTextColorG, MP_HUD_PROP_KEY_HudAmmoTextColorB);
    weapon.iconShow = cfg.GetBool(MP_HUD_PROP_KEY_HudWeaponIconShow);
    {
        static const char* kOverlayEnable[9] = {
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayPowerBeam,
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayVoltDriver,
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayMissile,
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayBattleHammer,
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayImperialist,
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayJudicator,
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayMagmaul,
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayShockCoil,
            MP_HUD_PROP_KEY_HudWeaponIconColorOverlayOmegaCannon
        };
        static const char* kOverlayR[9] = {
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRPowerBeam,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRVoltDriver,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRMissile,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRBattleHammer,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRImperialist,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRJudicator,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRMagmaul,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorRShockCoil,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorROmegaCannon
        };
        static const char* kOverlayG[9] = {
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGPowerBeam,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGVoltDriver,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGMissile,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGBattleHammer,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGImperialist,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGJudicator,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGMagmaul,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGShockCoil,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorGOmegaCannon
        };
        static const char* kOverlayB[9] = {
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBPowerBeam,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBVoltDriver,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBMissile,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBBattleHammer,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBImperialist,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBJudicator,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBMagmaul,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBShockCoil,
            MP_HUD_PROP_KEY_HudWeaponIconOverlayColorBOmegaCannon
        };
        for (int i = 0; i < 9; i++) {
            weapon.iconOverlayEnable[i] = cfg.GetBool(kOverlayEnable[i]);
            weapon.iconOverlayColor[i]  = ReadRgbColor(cfg, kOverlayR[i], kOverlayG[i], kOverlayB[i]);
        }
    }
    weapon.iconHeight = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconHeight);
    weapon.iconMode = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconMode);
    weapon.iconOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconOffsetX);
    weapon.iconOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconOffsetY);
    weapon.iconPosAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconPosAnchor);
    weapon.iconPosOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconPosX);
    weapon.iconPosOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconPosY);
    weapon.iconAnchorX = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconAnchorX);
    weapon.iconAnchorY = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponIconAnchorY);
    weapon.ammoGauge = cfg.GetBool(MP_HUD_PROP_KEY_HudAmmoGauge);
    weapon.ammoGaugeOri = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeOrientation);
    weapon.ammoGaugeLen = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeLength);
    weapon.ammoGaugeWid = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeWidth);
    weapon.ammoGaugeAlign = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeAlign);
    weapon.ammoGaugeOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeOffsetX);
    weapon.ammoGaugeOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeOffsetY);
    weapon.ammoGaugeAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeAnchor);
    weapon.ammoGaugePosMode = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosMode);
    weapon.ammoGaugePosAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosAnchor);
    weapon.ammoGaugePosOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosX);
    weapon.ammoGaugePosOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosY);
    weapon.ammoTextAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoTextAnchor);
    weapon.ammoTextOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoTextOffsetX);
    weapon.ammoTextOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoTextOffsetY);
    weapon.ammoGaugeColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudAmmoGaugeColorR, MP_HUD_PROP_KEY_HudAmmoGaugeColorG, MP_HUD_PROP_KEY_HudAmmoGaugeColorB);
    LoadColorRamp(weapon.ammoTextRamp,  cfg, MP_HUD_RAMP_KEYS(HudAmmoTextRamp));
    LoadColorRamp(weapon.ammoGaugeRamp, cfg, MP_HUD_RAMP_KEYS(HudAmmoGaugeRamp));
}
static void LoadWeaponInventoryConfig(WeaponInventoryHudConfig& wi, Config::Table& cfg)
{
    wi.show        = cfg.GetBool(MP_HUD_PROP_KEY_HudWeaponInventoryShow);
    wi.anchor      = cfg.GetInt (MP_HUD_PROP_KEY_HudWeaponInventoryAnchor);
    wi.ofsX        = cfg.GetInt (MP_HUD_PROP_KEY_HudWeaponInventoryX);
    wi.ofsY        = cfg.GetInt (MP_HUD_PROP_KEY_HudWeaponInventoryY);
    wi.align       = cfg.GetInt (MP_HUD_PROP_KEY_HudWeaponInventoryAlign);
    wi.orientation = cfg.GetInt (MP_HUD_PROP_KEY_HudWeaponInventoryOrientation);
    wi.iconHeight  = std::max(4, cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryIconHeight));
    wi.spacing     = std::max(0, cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventorySpacing));
    wi.opacity        = static_cast<float>(std::clamp(cfg.GetDouble(MP_HUD_PROP_KEY_HudWeaponInventoryOpacity), 0.0, 1.0));
    wi.notOwnedOpacity= static_cast<float>(std::clamp(cfg.GetDouble(MP_HUD_PROP_KEY_HudWeaponInventoryNotOwnedOpacity), 0.0, 1.0));
    wi.textColor   = QColor(cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryColorR),
                            cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryColorG),
                            cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryColorB));
    wi.highlightEnable  = cfg.GetBool(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightEnable);
    wi.highlightColor   = QColor(cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorR),
                                 cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorG),
                                 cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorB));
    wi.highlightOpacity       = static_cast<float>(std::clamp(cfg.GetDouble(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightOpacity), 0.0, 1.0));
    wi.highlightThickness     = static_cast<float>(std::clamp(cfg.GetDouble(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightThickness), 0.1, 8.0));
    wi.highlightPadding       = std::max(0, cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightPadding));
    wi.highlightCornerRadius  = std::max(0, cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightCornerRadius));
    wi.highlightOffsetLeft   = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetLeft);
    wi.highlightOffsetRight  = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetRight);
    wi.highlightOffsetTop    = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetTop);
    wi.highlightOffsetBottom = cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetBottom);
    // posX/posY computed in RecomputeAnchorPositions
}

// Forward declaration — full definition follows DrawCrosshair.
static void CollectArmRects(QRect* out, int& n, int cx, int cy,
                            int lengthX, int lengthY, int offset,
                            int thickness, bool tStyle);
// hudScale: crosshair dimensions are specified in actual output pixels.
// Arm rects are cached in pixel units (centred at origin) and drawn directly
// in pixel coordinates, completely bypassing the painter's DS→pixel transform.
static void LoadCrosshairConfig(CrosshairHudConfig& crosshair, Config::Table& cfg, float hudScale = 1.0f)
{
    crosshair.chColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_CrosshairColorR, MP_HUD_PROP_KEY_CrosshairColorG, MP_HUD_PROP_KEY_CrosshairColorB);
    crosshair.chOutline = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairOutline);
    crosshair.chOutlineOpacity = cfg.GetDouble(MP_HUD_PROP_KEY_CrosshairOutlineOpacity);
    crosshair.chOutlineThickness = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairOutlineThickness); if (crosshair.chOutlineThickness <= 0) crosshair.chOutlineThickness = 1;
    crosshair.chCenterDot = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairCenterDot);
    crosshair.chDotShape = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairDotShape), 0, 1);
    crosshair.chDotOpacity = cfg.GetDouble(MP_HUD_PROP_KEY_CrosshairDotOpacity);
    crosshair.chDotThickness = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairDotThickness); if (crosshair.chDotThickness <= 0) crosshair.chDotThickness = 1;
    crosshair.chTStyle = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairTStyle);
    crosshair.chInnerShow = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairInnerShow);
    crosshair.chInnerOpacity = cfg.GetDouble(MP_HUD_PROP_KEY_CrosshairInnerOpacity);
    crosshair.chInnerLengthX = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairInnerLengthX);
    crosshair.chInnerLengthY = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairInnerLengthY);
    crosshair.chInnerThickness = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairInnerThickness); if (crosshair.chInnerThickness <= 0) crosshair.chInnerThickness = 1;
    crosshair.chInnerOffset = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairInnerOffset);
    crosshair.chOuterShow = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairOuterShow);
    crosshair.chOuterOpacity = cfg.GetDouble(MP_HUD_PROP_KEY_CrosshairOuterOpacity);
    crosshair.chOuterLengthX = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairOuterLengthX);
    crosshair.chOuterLengthY = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairOuterLengthY);
    crosshair.chOuterThickness = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairOuterThickness); if (crosshair.chOuterThickness <= 0) crosshair.chOuterThickness = 1;
    crosshair.chOuterOffset = cfg.GetInt(MP_HUD_PROP_KEY_CrosshairOuterOffset);
    crosshair.chScale = std::max(100, cfg.GetInt(MP_HUD_PROP_KEY_CrosshairScale)) / 100.0;
    crosshair.chZoomStageEnable = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairZoomStageEnable);
    crosshair.chZoomScale = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScale), 10, 200) / 100.0;
    crosshair.chZoomOpacity = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomOpacity), 0, 100) / 100.0;
    crosshair.chZoomScopeEnable = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairZoomScopeEnable);
    crosshair.chZoomScopeRadius = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeRadius), 4, 1024);
    crosshair.chZoomScopeGap = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeGap), 0, 64);
    crosshair.chZoomScopeThickness = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeThickness), 1, 12);
    crosshair.chZoomScopeCenterDot = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairZoomScopeCenterDot);
    crosshair.chZoomScopeDotShape = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeDotShape), 0, 1);
    crosshair.chZoomScopeDotSize = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeDotSize), 1, 32);
    crosshair.chZoomScopeDotOpacity = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeDotOpacity), 0, 100) / 100.0;
    crosshair.chZoomScopeOpacity = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeOpacity), 0, 100) / 100.0;
    crosshair.chZoomTransitionEnable = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairZoomTransitionEnable);
    crosshair.chZoomTransitionStyle = ZoomStatus::RemapCrosshairZoomTransitionStyle(
        cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomTransitionStyle));
    crosshair.chZoomTransitionStyle =
        std::clamp(crosshair.chZoomTransitionStyle, 0,
                   ZoomStatus::kCrosshairZoomTransitionStyleCount - 1);
    const int transitionSpeedPct =
        std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomTransitionSpeed), 25, 400);
    crosshair.chZoomTransitionStep =
        0.125f * (static_cast<float>(transitionSpeedPct) / 100.0f);
    crosshair.chZoomTransitionPulseEnable =
        cfg.GetBool(MP_HUD_PROP_KEY_CrosshairZoomTransitionPulseEnable);
    crosshair.chZoomTransitionPulseStrength =
        std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_CrosshairZoomTransitionPulseStrength), 0, 100)
        / 100.0;
    crosshair.chZoomScopeDotCustomColor =
        cfg.GetBool(MP_HUD_PROP_KEY_CrosshairZoomScopeDotCustomColor);
    crosshair.chZoomScopeDotColor =
        ReadRgbColor(cfg, MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorR,
                     MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorG,
                     MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorB);
    crosshair.chZoomScopeDotDrawColor = crosshair.chZoomScopeDotCustomColor
        ? crosshair.chZoomScopeDotColor
        : crosshair.chColor;
    crosshair.chZoomScopeDotDrawColor.setAlphaF(crosshair.chZoomScopeDotOpacity);
    RebuildCrosshairZoomScopeCache(crosshair);
    // P-11: Pre-compute arm/dot colors with alpha
    crosshair.chInnerColor = crosshair.chColor;
    crosshair.chInnerColor.setAlphaF(crosshair.chInnerOpacity);
    crosshair.chOuterColor = crosshair.chColor;
    crosshair.chOuterColor.setAlphaF(crosshair.chOuterOpacity);
    const bool dotCustomColor = cfg.GetBool(MP_HUD_PROP_KEY_CrosshairDotCustomColor);
    crosshair.chDotColor = dotCustomColor
        ? ReadRgbColor(cfg, MP_HUD_PROP_KEY_CrosshairDotColorR,
                       MP_HUD_PROP_KEY_CrosshairDotColorG,
                       MP_HUD_PROP_KEY_CrosshairDotColorB)
        : crosshair.chColor;
    crosshair.chDotColor.setAlphaF(crosshair.chDotOpacity);

    // Pre-compute arm rects in actual output pixels, centred at (0,0).
    // DrawCrosshair maps (cx,cy) to pixel space, rounds to integer, and
    // adds (pxCx,pxCy) to these rects.  Because both the rects and the
    // centre are integers, pixel-grid alignment is always identical
    // regardless of aim position — the crosshair shape never wavers.
    const double cs = crosshair.chScale;
    crosshair.cachedInnerCount = 0;
    crosshair.cachedOuterCount = 0;
    if (crosshair.chInnerShow)
        CollectArmRects(crosshair.cachedInnerRects, crosshair.cachedInnerCount, 0, 0,
                        static_cast<int>(crosshair.chInnerLengthX * cs),
                        static_cast<int>(crosshair.chInnerLengthY * cs),
                        static_cast<int>(crosshair.chInnerOffset  * cs),
                        std::max(1, static_cast<int>(crosshair.chInnerThickness * cs)),
                        crosshair.chTStyle);
    if (crosshair.chOuterShow)
        CollectArmRects(crosshair.cachedOuterRects, crosshair.cachedOuterCount, 0, 0,
                        static_cast<int>(crosshair.chOuterLengthX * cs),
                        static_cast<int>(crosshair.chOuterLengthY * cs),
                        static_cast<int>(crosshair.chOuterOffset  * cs),
                        std::max(1, static_cast<int>(crosshair.chOuterThickness * cs)),
                        crosshair.chTStyle);
    const int dotSz = std::max(1, static_cast<int>(crosshair.chDotThickness * cs));
    crosshair.cachedDotSizePx = dotSz;
    crosshair.cachedDotHalfPx = dotSz / 2;
}
static void LoadMatchStatusConfig(MatchStatusHudConfig& matchStatus, Config::Table& cfg)
{
    matchStatus.matchStatusShow = cfg.GetBool(MP_HUD_PROP_KEY_HudMatchStatusShow);
    matchStatus.matchStatusAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudMatchStatusAnchor);
    matchStatus.matchStatusOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudMatchStatusX);
    matchStatus.matchStatusOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudMatchStatusY);
    matchStatus.matchStatusLabelOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudMatchStatusLabelOfsX);
    matchStatus.matchStatusLabelOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudMatchStatusLabelOfsY);
    matchStatus.matchStatusLabelPos = cfg.GetInt(MP_HUD_PROP_KEY_HudMatchStatusLabelPos);
    CopyConfigString(matchStatus.matchStatusLabelPoints, sizeof(matchStatus.matchStatusLabelPoints), cfg.GetString(MP_HUD_PROP_KEY_HudMatchStatusLabelPoints));
    CopyConfigString(matchStatus.matchStatusLabelOctoliths, sizeof(matchStatus.matchStatusLabelOctoliths), cfg.GetString(MP_HUD_PROP_KEY_HudMatchStatusLabelOctoliths));
    CopyConfigString(matchStatus.matchStatusLabelLives, sizeof(matchStatus.matchStatusLabelLives), cfg.GetString(MP_HUD_PROP_KEY_HudMatchStatusLabelLives));
    CopyConfigString(matchStatus.matchStatusLabelRingTime, sizeof(matchStatus.matchStatusLabelRingTime), cfg.GetString(MP_HUD_PROP_KEY_HudMatchStatusLabelRingTime));
    CopyConfigString(matchStatus.matchStatusLabelPrimeTime, sizeof(matchStatus.matchStatusLabelPrimeTime), cfg.GetString(MP_HUD_PROP_KEY_HudMatchStatusLabelPrimeTime));
    matchStatus.matchStatusColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudMatchStatusColorR, MP_HUD_PROP_KEY_HudMatchStatusColorG, MP_HUD_PROP_KEY_HudMatchStatusColorB);
    matchStatus.matchStatusLabelColor = ReadOptionalSubColor(cfg, MP_HUD_PROP_KEY_HudMatchStatusLabelColorOverall, MP_HUD_PROP_KEY_HudMatchStatusLabelColorR, MP_HUD_PROP_KEY_HudMatchStatusLabelColorG, MP_HUD_PROP_KEY_HudMatchStatusLabelColorB);
    matchStatus.matchStatusValueColor = ReadOptionalSubColor(cfg, MP_HUD_PROP_KEY_HudMatchStatusValueColorOverall, MP_HUD_PROP_KEY_HudMatchStatusValueColorR, MP_HUD_PROP_KEY_HudMatchStatusValueColorG, MP_HUD_PROP_KEY_HudMatchStatusValueColorB);
    matchStatus.matchStatusSepColor = ReadOptionalSubColor(cfg, MP_HUD_PROP_KEY_HudMatchStatusSepColorOverall, MP_HUD_PROP_KEY_HudMatchStatusSepColorR, MP_HUD_PROP_KEY_HudMatchStatusSepColorG, MP_HUD_PROP_KEY_HudMatchStatusSepColorB);
    matchStatus.matchStatusGoalColor = ReadOptionalSubColor(cfg, MP_HUD_PROP_KEY_HudMatchStatusGoalColorOverall, MP_HUD_PROP_KEY_HudMatchStatusGoalColorR, MP_HUD_PROP_KEY_HudMatchStatusGoalColorG, MP_HUD_PROP_KEY_HudMatchStatusGoalColorB);
}
static void LoadBombLeftConfig(BombLeftHudConfig& bombLeft, Config::Table& cfg)
{
    bombLeft.bombLeftShow = cfg.GetBool(MP_HUD_PROP_KEY_HudBombLeftShow);
    bombLeft.bombLeftTextShow = cfg.GetBool(MP_HUD_PROP_KEY_HudBombLeftTextShow);
    bombLeft.bombLeftAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftAnchor);
    bombLeft.bombLeftOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftX);
    bombLeft.bombLeftOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftY);
    bombLeft.bombLeftAlign = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftAlign);
    bombLeft.bombLeftColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudBombLeftColorR, MP_HUD_PROP_KEY_HudBombLeftColorG, MP_HUD_PROP_KEY_HudBombLeftColorB);
    CopyConfigString(bombLeft.bombLeftPrefix, sizeof(bombLeft.bombLeftPrefix), cfg.GetString(MP_HUD_PROP_KEY_HudBombLeftPrefix));
    CopyConfigString(bombLeft.bombLeftSuffix, sizeof(bombLeft.bombLeftSuffix), cfg.GetString(MP_HUD_PROP_KEY_HudBombLeftSuffix));
    bombLeft.bombIconShow = cfg.GetBool(MP_HUD_PROP_KEY_HudBombLeftIconShow);
    bombLeft.bombIconColorOverlay = cfg.GetBool(MP_HUD_PROP_KEY_HudBombLeftIconColorOverlay);
    bombLeft.bombIconHeight = cfg.GetInt(MP_HUD_PROP_KEY_HudBombIconHeight);
    bombLeft.bombIconColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudBombLeftIconColorR, MP_HUD_PROP_KEY_HudBombLeftIconColorG, MP_HUD_PROP_KEY_HudBombLeftIconColorB);
    bombLeft.bombIconMode = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconMode);
    bombLeft.bombIconOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconOfsX);
    bombLeft.bombIconOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconOfsY);
    bombLeft.bombIconPosAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconPosAnchor);
    bombLeft.bombIconPosOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconPosX);
    bombLeft.bombIconPosOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconPosY);
    bombLeft.bombIconAnchorX = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconAnchorX);
    bombLeft.bombIconAnchorY = cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconAnchorY);
}
static void LoadRankTimeConfig(RankTimeHudConfig& rankTime, Config::Table& cfg)
{
    rankTime.rankShow = cfg.GetBool(MP_HUD_PROP_KEY_HudRankShow);
    rankTime.rankAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudRankAnchor);
    rankTime.rankOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudRankX);
    rankTime.rankOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudRankY);
    rankTime.rankAlign = cfg.GetInt(MP_HUD_PROP_KEY_HudRankAlign);
    rankTime.rankColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudRankColorR, MP_HUD_PROP_KEY_HudRankColorG, MP_HUD_PROP_KEY_HudRankColorB);
    CopyConfigString(rankTime.rankPrefix, sizeof(rankTime.rankPrefix), cfg.GetString(MP_HUD_PROP_KEY_HudRankPrefix));
    rankTime.rankShowOrdinal = cfg.GetBool(MP_HUD_PROP_KEY_HudRankShowOrdinal);
    CopyConfigString(rankTime.rankSuffix, sizeof(rankTime.rankSuffix), cfg.GetString(MP_HUD_PROP_KEY_HudRankSuffix));
    rankTime.timeLeftShow = cfg.GetBool(MP_HUD_PROP_KEY_HudTimeLeftShow);
    rankTime.timeLeftAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLeftAnchor);
    rankTime.timeLeftOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLeftX);
    rankTime.timeLeftOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLeftY);
    rankTime.timeLeftAlign = cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLeftAlign);
    rankTime.timeLeftColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudTimeLeftColorR, MP_HUD_PROP_KEY_HudTimeLeftColorG, MP_HUD_PROP_KEY_HudTimeLeftColorB);
    rankTime.timeLimitShow = cfg.GetBool(MP_HUD_PROP_KEY_HudTimeLimitShow);
    rankTime.timeLimitAnchor = cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLimitAnchor);
    rankTime.timeLimitOfsX = cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLimitX);
    rankTime.timeLimitOfsY = cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLimitY);
    rankTime.timeLimitAlign = cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLimitAlign);
    rankTime.timeLimitColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_HudTimeLimitColorR, MP_HUD_PROP_KEY_HudTimeLimitColorG, MP_HUD_PROP_KEY_HudTimeLimitColorB);
}
static void LoadRadarOverlayConfig(RadarOverlayConfig& radar, Config::Table& cfg)
{
    radar.radarShow = cfg.GetBool(MP_HUD_PROP_KEY_BtmOverlayEnable);
    radar.radarAnchor = cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayAnchor);
    radar.radarOfsX = cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstX);
    radar.radarOfsY = cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstY);
    radar.radarDstSize = std::max(cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstSize), 1);
    radar.radarOpacity = std::clamp(cfg.GetDouble(MP_HUD_PROP_KEY_BtmOverlayOpacity), 0.0, 1.0);
    radar.radarSrcRadius = std::max(cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlaySrcRadius), 1);
    radar.radarFrameColor = ReadRgbColor(cfg, MP_HUD_PROP_KEY_BtmOverlayRadarColorR,
                                              MP_HUD_PROP_KEY_BtmOverlayRadarColorG,
                                              MP_HUD_PROP_KEY_BtmOverlayRadarColorB);
    radar.radarFrameColorUseHunter = cfg.GetBool(MP_HUD_PROP_KEY_BtmOverlayRadarColorUseHunter);
    // radarDstRect and radarClipPath are recomputed in RecomputeAnchorPositions()
}
// Recompute all final X/Y positions from stored anchor + offset + topStretchX.
// Called when config is refreshed or when topStretchX changes (window resize).
static void RecomputeAnchorPositions(float topStretchX)
{
    auto& c = s_cache;
    const float sx = topStretchX;
    c.lastStretchX = sx;

    // HP
    ApplyAnchor(c.hp.hpAnchor, c.hp.hpOfsX, c.hp.hpOfsY, c.hp.hpX, c.hp.hpY, sx);
    ApplyAnchor(c.hp.hpGaugePosAnchor, c.hp.hpGaugePosOfsX, c.hp.hpGaugePosOfsY, c.hp.hpGaugePosX, c.hp.hpGaugePosY, sx);

    // Weapon
    ApplyAnchor(c.weapon.wpnAnchor, c.weapon.wpnOfsX, c.weapon.wpnOfsY, c.weapon.wpnX, c.weapon.wpnY, sx);
    ApplyAnchor(c.weapon.iconPosAnchor, c.weapon.iconPosOfsX, c.weapon.iconPosOfsY, c.weapon.iconPosX, c.weapon.iconPosY, sx);
    ApplyAnchor(c.weapon.ammoGaugePosAnchor, c.weapon.ammoGaugePosOfsX, c.weapon.ammoGaugePosOfsY, c.weapon.ammoGaugePosX, c.weapon.ammoGaugePosY, sx);

    // Match status
    ApplyAnchor(c.matchStatus.matchStatusAnchor, c.matchStatus.matchStatusOfsX, c.matchStatus.matchStatusOfsY, c.matchStatus.matchStatusX, c.matchStatus.matchStatusY, sx);

    // Bomb left
    ApplyAnchor(c.bombLeft.bombLeftAnchor, c.bombLeft.bombLeftOfsX, c.bombLeft.bombLeftOfsY, c.bombLeft.bombLeftX, c.bombLeft.bombLeftY, sx);
    ApplyAnchor(c.bombLeft.bombIconPosAnchor, c.bombLeft.bombIconPosOfsX, c.bombLeft.bombIconPosOfsY, c.bombLeft.bombIconPosX, c.bombLeft.bombIconPosY, sx);

    // Rank & Time
    ApplyAnchor(c.rankTime.rankAnchor, c.rankTime.rankOfsX, c.rankTime.rankOfsY, c.rankTime.rankX, c.rankTime.rankY, sx);
    ApplyAnchor(c.rankTime.timeLeftAnchor, c.rankTime.timeLeftOfsX, c.rankTime.timeLeftOfsY, c.rankTime.timeLeftX, c.rankTime.timeLeftY, sx);
    ApplyAnchor(c.rankTime.timeLimitAnchor, c.rankTime.timeLimitOfsX, c.rankTime.timeLimitOfsY, c.rankTime.timeLimitX, c.rankTime.timeLimitY, sx);

    // Weapon Inventory
    ApplyAnchor(c.weaponInventory.anchor, c.weaponInventory.ofsX, c.weaponInventory.ofsY,
                c.weaponInventory.posX, c.weaponInventory.posY, sx);

    // Radar overlay
    ApplyAnchor(c.radar.radarAnchor, c.radar.radarOfsX, c.radar.radarOfsY, c.radar.radarDstX, c.radar.radarDstY, sx);
    c.radar.radarDstRect = QRect(c.radar.radarDstX, c.radar.radarDstY, c.radar.radarDstSize, c.radar.radarDstSize);
    c.radar.radarClipPath = QPainterPath();
    c.radar.radarClipPath.addEllipse(c.radar.radarDstRect);
    // P-15: Pre-compute radar frame geometry
    {
        const int srcDiameter = c.radar.radarSrcRadius * 2;
        const float frameSizeDS = static_cast<float>(kRadarArtSize) * c.radar.radarDstSize
                                  / static_cast<float>(srcDiameter);
        const float cropCenterX = c.radar.radarDstX + c.radar.radarDstSize * 0.5f + 0.75f;
        const float cropCenterY = c.radar.radarDstY + c.radar.radarDstSize * 0.5f;
        c.radar.frameDstRect = QRectF(cropCenterX - frameSizeDS * 0.5f,
                                      cropCenterY - frameSizeDS * 0.5f,
                                      frameSizeDS, frameSizeDS);
    }
}

static void RefreshCachedConfig(Config::Table& cfg, float topStretchX = 1.0f, float hudScale = 1.0f)
{
    auto& c = s_cache;
    LoadHpConfig(c.hp, cfg);
    LoadWeaponConfig(c.weapon, cfg);
    LoadWeaponInventoryConfig(c.weaponInventory, cfg);
    LoadCrosshairConfig(c.crosshair, cfg, hudScale);
    LoadMatchStatusConfig(c.matchStatus, cfg);
    LoadBombLeftConfig(c.bombLeft, cfg);
    LoadRankTimeConfig(c.rankTime, cfg);
    LoadRadarOverlayConfig(c.radar, cfg);
    // Per-element outlines
#define MP_HUD_OUTLINE_KEYS(prefix) \
    MP_HUD_PROP_KEY_##prefix##Outline, \
    MP_HUD_PROP_KEY_##prefix##OutlineColorR, \
    MP_HUD_PROP_KEY_##prefix##OutlineColorG, \
    MP_HUD_PROP_KEY_##prefix##OutlineColorB, \
    MP_HUD_PROP_KEY_##prefix##OutlineOpacity, \
    MP_HUD_PROP_KEY_##prefix##OutlineThickness
    auto loadOL = [&](HudOutlineConfig& ol, const char* enableKey,
                      const char* colorRKey, const char* colorGKey, const char* colorBKey,
                      const char* opacityKey, const char* thicknessKey) {
        ol.enable    = cfg.GetBool(enableKey);
        ol.color     = ReadRgbColor(cfg, colorRKey, colorGKey, colorBKey);
        ol.opacity   = (float)std::clamp(cfg.GetDouble(opacityKey), 0.0, 1.0);
        ol.thickness = std::max(1, cfg.GetInt(thicknessKey));
        ol.colorWithAlpha = ol.color;
        ol.colorWithAlpha.setAlphaF(ol.opacity);
    };
    loadOL(c.globalOutline,        MP_HUD_OUTLINE_KEYS(HudGlobal));
    loadOL(c.hp.outline,           MP_HUD_OUTLINE_KEYS(HudHp));
    loadOL(c.hp.gaugeOutline,      MP_HUD_OUTLINE_KEYS(HudHpGauge));
    loadOL(c.weapon.outline,       MP_HUD_OUTLINE_KEYS(HudWeapon));
    loadOL(c.weapon.iconOutline,   MP_HUD_OUTLINE_KEYS(HudWeaponIcon));
    loadOL(c.weapon.gaugeOutline,  MP_HUD_OUTLINE_KEYS(HudAmmoGauge));
    loadOL(c.matchStatus.outline,  MP_HUD_OUTLINE_KEYS(HudMatchStatus));
    loadOL(c.rankTime.rankOutline,      MP_HUD_OUTLINE_KEYS(HudRank));
    loadOL(c.rankTime.timeLeftOutline,   MP_HUD_OUTLINE_KEYS(HudTimeLeft));
    loadOL(c.rankTime.timeLimitOutline,  MP_HUD_OUTLINE_KEYS(HudTimeLimit));
    loadOL(c.weaponInventory.outline,     MP_HUD_OUTLINE_KEYS(HudWeaponInventory));
    loadOL(c.weaponInventory.iconOutline, MP_HUD_OUTLINE_KEYS(HudWeaponInventoryIcon));
    loadOL(c.bombLeft.outline,      MP_HUD_OUTLINE_KEYS(HudBombLeft));
    loadOL(c.bombLeft.iconOutline,  MP_HUD_OUTLINE_KEYS(HudBombIcon));
    loadOL(c.radar.outline,        MP_HUD_OUTLINE_KEYS(BtmOverlay));
    loadOL(c.radar.frameOutline,   MP_HUD_OUTLINE_KEYS(BtmOverlayFrame));
#undef MP_HUD_OUTLINE_KEYS
    c.textScalePct        = std::max(100, cfg.GetInt(MP_HUD_PROP_KEY_HudTextScale));
    const bool autoScaleEnable = cfg.GetBool(MP_HUD_PROP_KEY_HudAutoScaleEnable);
    if (autoScaleEnable) {
        // Auto-scale: integer resolution multiplier, capped by user preferences.
        // autoScaleInt = floor(hudScale), i.e. at 2.3x DS res -> 2.
        const int autoScaleInt = std::max(1, static_cast<int>(std::floor(hudScale)));
        const int autoScalePct = autoScaleInt * 100;
        const int globalCap    = std::clamp(cfg.GetInt(MP_HUD_PROP_KEY_HudAutoScaleCap),      100, 800);
        auto capScale = [&](const char* catKey) -> float {
            int catCap = std::clamp(cfg.GetInt(catKey), 100, 800);
            return std::min({autoScalePct, globalCap, catCap}) / 100.0f;
        };
        c.scaleText      = capScale(MP_HUD_PROP_KEY_HudAutoScaleCapText);
        c.scaleIcons     = capScale(MP_HUD_PROP_KEY_HudAutoScaleCapIcons);
        c.scaleGauges    = capScale(MP_HUD_PROP_KEY_HudAutoScaleCapGauges);
        c.scaleCrosshair = capScale(MP_HUD_PROP_KEY_HudAutoScaleCapCrosshair);
    } else {
        c.scaleText = 1.0f;
        c.scaleIcons = 1.0f;
        c.scaleGauges = 1.0f;
        c.scaleCrosshair = 1.0f;
    }
    // Text draw scale: auto-scale + user TextScale as additive offset.
    // e.g. auto=200%, TextScale=150% -> effective=250% (200+150-100).
    const float effectiveTextPct = c.scaleText * 100.0f + (c.textScalePct - 100);
    c.textDrawScale       = (hudScale > 0.0f) ? (effectiveTextPct / 100.0f) / hudScale
                                               : effectiveTextPct / 100.0f;
    // Hi-DPI text: only system/file fonts benefit (MPH is a pixel font tuned for 6px upscale).
    c.hiResText           = (cfg.GetInt(MP_HUD_PROP_KEY_HudFontMode) != 0);
    // Build the output-resolution glyph font here — RefreshCachedConfig only runs on a config
    // change or window resize (via EnsureCachedConfigForFrame), so this is event-driven, never
    // per frame. s_textRenderScale = glyph magnification (effectiveTextPct/100), capped.
    {
        const float mag = effectiveTextPct / 100.0f;
        s_textRenderScale = (c.hiResText && mag > 1.0f) ? std::min(mag, 16.0f) : 1.0f;
        if (s_textRenderScale > 1.0f) {
            const int basePx = CustomHud_ResolveFontPixelSize(cfg);
            s_hiResFont = CustomHud_ResolveBaseFont(cfg);
            s_hiResFont.setPixelSize(std::max(1, static_cast<int>(std::lround(basePx * s_textRenderScale))));
            s_hiResFm = QFontMetrics(s_hiResFont);
        }
    }

    // Recompute crosshair arm rects with auto-scale applied.
    // LoadCrosshairConfig computes rects using chScale only; we now
    // multiply in scaleCrosshair and redo the geometry.
    if (c.scaleCrosshair != 1.0f) {
        auto& ch = c.crosshair;
        const double cs = ch.chScale * (double)c.scaleCrosshair;
        ch.cachedInnerCount = 0;
        ch.cachedOuterCount = 0;
        if (ch.chInnerShow)
            CollectArmRects(ch.cachedInnerRects, ch.cachedInnerCount, 0, 0,
                            static_cast<int>(ch.chInnerLengthX * cs),
                            static_cast<int>(ch.chInnerLengthY * cs),
                            static_cast<int>(ch.chInnerOffset  * cs),
                            std::max(1, static_cast<int>(ch.chInnerThickness * cs)),
                            ch.chTStyle);
        if (ch.chOuterShow)
            CollectArmRects(ch.cachedOuterRects, ch.cachedOuterCount, 0, 0,
                            static_cast<int>(ch.chOuterLengthX * cs),
                            static_cast<int>(ch.chOuterLengthY * cs),
                            static_cast<int>(ch.chOuterOffset  * cs),
                            std::max(1, static_cast<int>(ch.chOuterThickness * cs)),
                            ch.chTStyle);
        const int dotSz = std::max(1, static_cast<int>(ch.chDotThickness * cs));
        ch.cachedDotSizePx = dotSz;
        ch.cachedDotHalfPx = dotSz / 2;

        const double scopeScale = static_cast<double>(c.scaleCrosshair);
        ch.chZoomScopeRadius = std::max(
            1, static_cast<int>(std::lround(ch.chZoomScopeRadius * scopeScale)));
        ch.chZoomScopeGap = std::max(
            0, static_cast<int>(std::lround(ch.chZoomScopeGap * scopeScale)));
        ch.chZoomScopeThickness = std::max(
            1, static_cast<int>(std::lround(ch.chZoomScopeThickness * scopeScale)));
        ch.chZoomScopeDotSize = std::max(
            1, static_cast<int>(std::lround(ch.chZoomScopeDotSize * scopeScale)));
        RebuildCrosshairZoomScopeCache(ch);
    }
    c.lastHudScale        = hudScale;
    c.hpOpacity           = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudHpOpacity);
    c.weaponOpacity       = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudWeaponOpacity);
    c.matchStatusOpacity  = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudMatchStatusOpacity);
    c.rankOpacity         = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudRankOpacity);
    c.bombLeftOpacity     = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudBombLeftOpacity);
    c.hpGaugeOpacity      = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudHpGaugeOpacity);
    c.wpnIconOpacity      = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudWpnIconOpacity);
    c.ammoGaugeOpacity    = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudAmmoGaugeOpacity);
    c.bombIconOpacity     = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudBombIconOpacity);
    c.timeLeftOpacity     = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudTimeLeftOpacity);
    c.timeLimitOpacity    = (float)cfg.GetDouble(MP_HUD_PROP_KEY_HudTimeLimitOpacity);
    {
        uint16_t m = 0;
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Helmet))            m |= (1u << NOHUD_HELMET);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Ammo))              m |= (1u << NOHUD_AMMO);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_WeaponIcon))        m |= (1u << NOHUD_WEAPONICON);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_HP))                m |= (1u << NOHUD_HP);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Crosshair))         m |= (1u << NOHUD_CROSSHAIR);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreBattle))       m |= (1u << NOHUD_SCORE_BATTLE);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreSurvival))     m |= (1u << NOHUD_SCORE_SURVIVAL);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScorePrimeHunter))  m |= (1u << NOHUD_SCORE_PRIMEHUNTER);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreBounty))       m |= (1u << NOHUD_SCORE_BOUNTY);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreCapture))      m |= (1u << NOHUD_SCORE_CAPTURE);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreDefender))     m |= (1u << NOHUD_SCORE_DEFENDER);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_ScoreNode))         m |= (1u << NOHUD_SCORE_NODE);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Bomb))              m |= (1u << NOHUD_BOMB);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Boost))             m |= (1u << NOHUD_BOOST);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Cloak))             m |= (1u << NOHUD_CLOAK);
        if (cfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_DoubleDamage))      m |= (1u << NOHUD_DOUBLE_DAMAGE);
        c.noHudMask = m;
    }
    RecomputeAnchorPositions(topStretchX);
    // P-17: Rebuild crosshair outline stamp now that geometry is final.
    RebuildCrosshairOutlineStamp(c.crosshair);
    ++s_cacheEpoch;
    if (s_cacheEpoch == 0) s_cacheEpoch = 1;
}
