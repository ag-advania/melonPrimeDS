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
struct HpHudConfig {
    int hpX, hpY, hpAlign;          // final (computed from anchor + offset + stretch)
    int hpAnchor, hpOfsX, hpOfsY;   // raw config values
    char hpPrefix[48];
    bool hpTextAutoColor, hpGauge, hpAutoColor;
    QColor hpTextColor;
    int hpGaugeOri, hpGaugeLen, hpGaugeWid;
    int hpGaugeAlign;
    int hpGaugeOfsX, hpGaugeOfsY, hpGaugeAnchor;
    int hpGaugePosMode, hpGaugePosX, hpGaugePosY;  // final
    int hpGaugePosAnchor, hpGaugePosOfsX, hpGaugePosOfsY;  // raw
    int hpTextAnchor;            // which side of gauge the text appears (mode 2 only)
    int hpTextOfsX, hpTextOfsY;  // text offset from gauge (mode 2 only)
    QColor hpGaugeColor;
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
    double chOutlineOpacity, chDotOpacity;
    int chOutlineThickness, chDotThickness;
    bool chInnerShow;
    double chInnerOpacity;
    int chInnerLengthX, chInnerLengthY, chInnerThickness, chInnerOffset;
    bool chOuterShow;
    double chOuterOpacity;
    int chOuterLengthX, chOuterLengthY, chOuterThickness, chOuterOffset;
    double chScale; // visual scale multiplier (1.0 = 100%)
    // P-11: Pre-computed arm/dot colors with alpha set (avoids per-frame QColor copy + setAlphaF)
    QColor chInnerColor, chOuterColor, chDotColor;
    // Cached arm rects in output pixels, centred at (0,0).
    // DrawCrosshair translates to the rounded pixel position of (cx,cy) each frame.
    QRect cachedInnerRects[4], cachedOuterRects[4];
    int   cachedInnerCount, cachedOuterCount;
    int   cachedDotHalfPx;   // half-size of the centre dot in output pixels
    int   cachedDotSizePx;   // full size of the centre dot in output pixels
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
    bool valid;
};
static CachedHudConfig s_cache = { .valid = false };
static uint32_t s_cacheEpoch = 1;

// OPT-DR1: Returns the pixel-space bounding rect of the crosshair rendered at (cx, cy).
// Uses the pre-computed stamp, arm rects, and dot size from CrosshairHudConfig.
// Result is in overlay pixel space (same as Overlay[0]).
static QRect CrosshairBboxPx(int cx, int cy, const CrosshairHudConfig& ch)
{
    QRect r;
    if (s_chStampValid && !s_chOutlineStamp.isNull())
        r = QRect(cx + s_chStampOrigin.x(), cy + s_chStampOrigin.y(),
                  s_chOutlineStamp.width(), s_chOutlineStamp.height());
    for (int i = 0; i < ch.cachedInnerCount; ++i) r |= ch.cachedInnerRects[i].translated(cx, cy);
    for (int i = 0; i < ch.cachedOuterCount; ++i) r |= ch.cachedOuterRects[i].translated(cx, cy);
    if (ch.chCenterDot) {
        const int dh = ch.cachedDotHalfPx, ds = ch.cachedDotSizePx;
        r |= QRect(cx - dh, cy - dh, ds, ds);
    }
    return r;
}

// P-9: Frame-level font metrics — set once in CustomHud_Render, shared by all draw sub-functions.
// OPT-HUD-6: s_frameFont caches the QFont so draw functions never call p->font() per-frame.
//   p->font() creates a CoW ref-increment even on cache-hit paths (~10–20 cyc × 6 callers).
static QFontMetrics s_frameFm{QFont{}};
static int s_frameFpx = 0;
static QFont s_frameFont;

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

// ApplyAnchor: converts anchor index + offset into final coordinates.
// anchor 0=TL 1=TC 2=TR 3=ML 4=MC 5=MR 6=BL 7=BC 8=BR
// topStretchX = scaleX/scaleY: >1 when wider than 4:3, <1 when narrower.
// Adjusts X anchor bases so left/right edges track the actual visible area.
// With the painter set to scale(hudScale,hudScale) + translate((topStretchX-1)*128, 0),
// the visible X range in DS coords is [(1-sx)*128 .. 128*(sx+1)].
static inline void ApplyAnchor(int anchor, int offsetX, int offsetY,
                                int& outX, int& outY, float topStretchX = 1.0f)
{
    static constexpr int H = 192;
    const int col = anchor % 3;
    const float baseX = (col == 0) ? -(topStretchX - 1.0f) * 128.0f
                      : (col == 1) ?  128.0f
                      :               128.0f * (topStretchX + 1.0f);
    const int baseY = (anchor / 3 == 0) ? 0 : (anchor / 3 == 1) ? H / 2 : H;
    outX = static_cast<int>(baseX) + offsetX;
    outY = baseY + offsetY;
}

static void LoadHpConfig(HpHudConfig& hp, Config::Table& cfg)
{
    hp.hpAnchor = cfg.GetInt("Metroid.Visual.HudHpAnchor");
    hp.hpOfsX = cfg.GetInt("Metroid.Visual.HudHpX");
    hp.hpOfsY = cfg.GetInt("Metroid.Visual.HudHpY");
    hp.hpAlign = cfg.GetInt("Metroid.Visual.HudHpAlign");
    CopyConfigString(hp.hpPrefix, sizeof(hp.hpPrefix), cfg.GetString("Metroid.Visual.HudHpPrefix"));
    hp.hpTextAutoColor = cfg.GetBool("Metroid.Visual.HudHpTextAutoColor");
    hp.hpTextColor = ReadRgbColor(cfg, "Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB");
    hp.hpGauge = cfg.GetBool("Metroid.Visual.HudHpGauge");
    hp.hpGaugeOri = cfg.GetInt("Metroid.Visual.HudHpGaugeOrientation");
    hp.hpGaugeLen = cfg.GetInt("Metroid.Visual.HudHpGaugeLength");
    hp.hpGaugeWid = cfg.GetInt("Metroid.Visual.HudHpGaugeWidth");
    hp.hpGaugeAlign = cfg.GetInt("Metroid.Visual.HudHpGaugeAlign");
    hp.hpGaugeOfsX = cfg.GetInt("Metroid.Visual.HudHpGaugeOffsetX");
    hp.hpGaugeOfsY = cfg.GetInt("Metroid.Visual.HudHpGaugeOffsetY");
    hp.hpGaugeAnchor = cfg.GetInt("Metroid.Visual.HudHpGaugeAnchor");
    hp.hpGaugePosMode = cfg.GetInt("Metroid.Visual.HudHpGaugePosMode");
    hp.hpGaugePosAnchor = cfg.GetInt("Metroid.Visual.HudHpGaugePosAnchor");
    hp.hpGaugePosOfsX = cfg.GetInt("Metroid.Visual.HudHpGaugePosX");
    hp.hpGaugePosOfsY = cfg.GetInt("Metroid.Visual.HudHpGaugePosY");
    hp.hpTextAnchor = cfg.GetInt("Metroid.Visual.HudHpTextAnchor");
    hp.hpTextOfsX = cfg.GetInt("Metroid.Visual.HudHpTextOffsetX");
    hp.hpTextOfsY = cfg.GetInt("Metroid.Visual.HudHpTextOffsetY");
    hp.hpAutoColor = cfg.GetBool("Metroid.Visual.HudHpGaugeAutoColor");
    hp.hpGaugeColor = ReadRgbColor(cfg, "Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB");
}
static void LoadWeaponConfig(WeaponHudConfig& weapon, Config::Table& cfg)
{
    weapon.wpnAnchor = cfg.GetInt("Metroid.Visual.HudWeaponAnchor");
    weapon.wpnOfsX = cfg.GetInt("Metroid.Visual.HudWeaponX");
    weapon.wpnOfsY = cfg.GetInt("Metroid.Visual.HudWeaponY");
    weapon.ammoAlign = cfg.GetInt("Metroid.Visual.HudAmmoAlign");
    CopyConfigString(weapon.ammoPrefix, sizeof(weapon.ammoPrefix), cfg.GetString("Metroid.Visual.HudAmmoPrefix"));
    weapon.ammoTextColor = ReadRgbColor(cfg, "Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB");
    weapon.iconShow = cfg.GetBool("Metroid.Visual.HudWeaponIconShow");
    {
        static const char* kWpnNames[9] = {
            "PowerBeam","VoltDriver","Missile","BattleHammer",
            "Imperialist","Judicator","Magmaul","ShockCoil","OmegaCannon"
        };
        for (int i = 0; i < 9; i++) {
            char kE[80], kR[80], kG[80], kB[80];
            std::snprintf(kE, sizeof(kE), "Metroid.Visual.HudWeaponIconColorOverlay%s", kWpnNames[i]);
            std::snprintf(kR, sizeof(kR), "Metroid.Visual.HudWeaponIconOverlayColorR%s", kWpnNames[i]);
            std::snprintf(kG, sizeof(kG), "Metroid.Visual.HudWeaponIconOverlayColorG%s", kWpnNames[i]);
            std::snprintf(kB, sizeof(kB), "Metroid.Visual.HudWeaponIconOverlayColorB%s", kWpnNames[i]);
            weapon.iconOverlayEnable[i] = cfg.GetBool(kE);
            weapon.iconOverlayColor[i]  = ReadRgbColor(cfg, kR, kG, kB);
        }
    }
    weapon.iconHeight = cfg.GetInt("Metroid.Visual.HudWeaponIconHeight");
    weapon.iconMode = cfg.GetInt("Metroid.Visual.HudWeaponIconMode");
    weapon.iconOfsX = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX");
    weapon.iconOfsY = cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY");
    weapon.iconPosAnchor = cfg.GetInt("Metroid.Visual.HudWeaponIconPosAnchor");
    weapon.iconPosOfsX = cfg.GetInt("Metroid.Visual.HudWeaponIconPosX");
    weapon.iconPosOfsY = cfg.GetInt("Metroid.Visual.HudWeaponIconPosY");
    weapon.iconAnchorX = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorX");
    weapon.iconAnchorY = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorY");
    weapon.ammoGauge = cfg.GetBool("Metroid.Visual.HudAmmoGauge");
    weapon.ammoGaugeOri = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOrientation");
    weapon.ammoGaugeLen = cfg.GetInt("Metroid.Visual.HudAmmoGaugeLength");
    weapon.ammoGaugeWid = cfg.GetInt("Metroid.Visual.HudAmmoGaugeWidth");
    weapon.ammoGaugeAlign = cfg.GetInt("Metroid.Visual.HudAmmoGaugeAlign");
    weapon.ammoGaugeOfsX = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX");
    weapon.ammoGaugeOfsY = cfg.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY");
    weapon.ammoGaugeAnchor = cfg.GetInt("Metroid.Visual.HudAmmoGaugeAnchor");
    weapon.ammoGaugePosMode = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosMode");
    weapon.ammoGaugePosAnchor = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosAnchor");
    weapon.ammoGaugePosOfsX = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosX");
    weapon.ammoGaugePosOfsY = cfg.GetInt("Metroid.Visual.HudAmmoGaugePosY");
    weapon.ammoTextAnchor = cfg.GetInt("Metroid.Visual.HudAmmoTextAnchor");
    weapon.ammoTextOfsX = cfg.GetInt("Metroid.Visual.HudAmmoTextOffsetX");
    weapon.ammoTextOfsY = cfg.GetInt("Metroid.Visual.HudAmmoTextOffsetY");
    weapon.ammoGaugeColor = ReadRgbColor(cfg, "Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB");
}
static void LoadWeaponInventoryConfig(WeaponInventoryHudConfig& wi, Config::Table& cfg)
{
    wi.show        = cfg.GetBool("Metroid.Visual.HudWeaponInventoryShow");
    wi.anchor      = cfg.GetInt ("Metroid.Visual.HudWeaponInventoryAnchor");
    wi.ofsX        = cfg.GetInt ("Metroid.Visual.HudWeaponInventoryX");
    wi.ofsY        = cfg.GetInt ("Metroid.Visual.HudWeaponInventoryY");
    wi.align       = cfg.GetInt ("Metroid.Visual.HudWeaponInventoryAlign");
    wi.orientation = cfg.GetInt ("Metroid.Visual.HudWeaponInventoryOrientation");
    wi.iconHeight  = std::max(4, cfg.GetInt("Metroid.Visual.HudWeaponInventoryIconHeight"));
    wi.spacing     = std::max(0, cfg.GetInt("Metroid.Visual.HudWeaponInventorySpacing"));
    wi.opacity        = static_cast<float>(std::clamp(cfg.GetDouble("Metroid.Visual.HudWeaponInventoryOpacity"), 0.0, 1.0));
    wi.notOwnedOpacity= static_cast<float>(std::clamp(cfg.GetDouble("Metroid.Visual.HudWeaponInventoryNotOwnedOpacity"), 0.0, 1.0));
    wi.textColor   = QColor(cfg.GetInt("Metroid.Visual.HudWeaponInventoryColorR"),
                            cfg.GetInt("Metroid.Visual.HudWeaponInventoryColorG"),
                            cfg.GetInt("Metroid.Visual.HudWeaponInventoryColorB"));
    wi.highlightEnable  = cfg.GetBool("Metroid.Visual.HudWeaponInventoryHighlightEnable");
    wi.highlightColor   = QColor(cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightColorR"),
                                 cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightColorG"),
                                 cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightColorB"));
    wi.highlightOpacity       = static_cast<float>(std::clamp(cfg.GetDouble("Metroid.Visual.HudWeaponInventoryHighlightOpacity"), 0.0, 1.0));
    wi.highlightThickness     = static_cast<float>(std::clamp(cfg.GetDouble("Metroid.Visual.HudWeaponInventoryHighlightThickness"), 0.1, 8.0));
    wi.highlightPadding       = std::max(0, cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightPadding"));
    wi.highlightCornerRadius  = std::max(0, cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightCornerRadius"));
    wi.highlightOffsetLeft   = cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetLeft");
    wi.highlightOffsetRight  = cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetRight");
    wi.highlightOffsetTop    = cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetTop");
    wi.highlightOffsetBottom = cfg.GetInt("Metroid.Visual.HudWeaponInventoryHighlightSizeOffsetBottom");
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
    crosshair.chColor = ReadRgbColor(cfg, "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB");
    crosshair.chOutline = cfg.GetBool("Metroid.Visual.CrosshairOutline");
    crosshair.chOutlineOpacity = cfg.GetDouble("Metroid.Visual.CrosshairOutlineOpacity");
    crosshair.chOutlineThickness = cfg.GetInt("Metroid.Visual.CrosshairOutlineThickness"); if (crosshair.chOutlineThickness <= 0) crosshair.chOutlineThickness = 1;
    crosshair.chCenterDot = cfg.GetBool("Metroid.Visual.CrosshairCenterDot");
    crosshair.chDotOpacity = cfg.GetDouble("Metroid.Visual.CrosshairDotOpacity");
    crosshair.chDotThickness = cfg.GetInt("Metroid.Visual.CrosshairDotThickness"); if (crosshair.chDotThickness <= 0) crosshair.chDotThickness = 1;
    crosshair.chTStyle = cfg.GetBool("Metroid.Visual.CrosshairTStyle");
    crosshair.chInnerShow = cfg.GetBool("Metroid.Visual.CrosshairInnerShow");
    crosshair.chInnerOpacity = cfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity");
    crosshair.chInnerLengthX = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthX");
    crosshair.chInnerLengthY = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthY");
    crosshair.chInnerThickness = cfg.GetInt("Metroid.Visual.CrosshairInnerThickness"); if (crosshair.chInnerThickness <= 0) crosshair.chInnerThickness = 1;
    crosshair.chInnerOffset = cfg.GetInt("Metroid.Visual.CrosshairInnerOffset");
    crosshair.chOuterShow = cfg.GetBool("Metroid.Visual.CrosshairOuterShow");
    crosshair.chOuterOpacity = cfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity");
    crosshair.chOuterLengthX = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthX");
    crosshair.chOuterLengthY = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthY");
    crosshair.chOuterThickness = cfg.GetInt("Metroid.Visual.CrosshairOuterThickness"); if (crosshair.chOuterThickness <= 0) crosshair.chOuterThickness = 1;
    crosshair.chOuterOffset = cfg.GetInt("Metroid.Visual.CrosshairOuterOffset");
    crosshair.chScale = std::max(100, cfg.GetInt("Metroid.Visual.CrosshairScale")) / 100.0;
    // P-11: Pre-compute arm/dot colors with alpha
    crosshair.chInnerColor = crosshair.chColor;
    crosshair.chInnerColor.setAlphaF(crosshair.chInnerOpacity);
    crosshair.chOuterColor = crosshair.chColor;
    crosshair.chOuterColor.setAlphaF(crosshair.chOuterOpacity);
    crosshair.chDotColor = crosshair.chColor;
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
    matchStatus.matchStatusShow = cfg.GetBool("Metroid.Visual.HudMatchStatusShow");
    matchStatus.matchStatusAnchor = cfg.GetInt("Metroid.Visual.HudMatchStatusAnchor");
    matchStatus.matchStatusOfsX = cfg.GetInt("Metroid.Visual.HudMatchStatusX");
    matchStatus.matchStatusOfsY = cfg.GetInt("Metroid.Visual.HudMatchStatusY");
    matchStatus.matchStatusLabelOfsX = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsX");
    matchStatus.matchStatusLabelOfsY = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelOfsY");
    matchStatus.matchStatusLabelPos = cfg.GetInt("Metroid.Visual.HudMatchStatusLabelPos");
    CopyConfigString(matchStatus.matchStatusLabelPoints, sizeof(matchStatus.matchStatusLabelPoints), cfg.GetString("Metroid.Visual.HudMatchStatusLabelPoints"));
    CopyConfigString(matchStatus.matchStatusLabelOctoliths, sizeof(matchStatus.matchStatusLabelOctoliths), cfg.GetString("Metroid.Visual.HudMatchStatusLabelOctoliths"));
    CopyConfigString(matchStatus.matchStatusLabelLives, sizeof(matchStatus.matchStatusLabelLives), cfg.GetString("Metroid.Visual.HudMatchStatusLabelLives"));
    CopyConfigString(matchStatus.matchStatusLabelRingTime, sizeof(matchStatus.matchStatusLabelRingTime), cfg.GetString("Metroid.Visual.HudMatchStatusLabelRingTime"));
    CopyConfigString(matchStatus.matchStatusLabelPrimeTime, sizeof(matchStatus.matchStatusLabelPrimeTime), cfg.GetString("Metroid.Visual.HudMatchStatusLabelPrimeTime"));
    matchStatus.matchStatusColor = ReadRgbColor(cfg, "Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB");
    matchStatus.matchStatusLabelColor = ReadOptionalSubColor(cfg, "Metroid.Visual.HudMatchStatusLabelColorOverall", "Metroid.Visual.HudMatchStatusLabelColorR", "Metroid.Visual.HudMatchStatusLabelColorG", "Metroid.Visual.HudMatchStatusLabelColorB");
    matchStatus.matchStatusValueColor = ReadOptionalSubColor(cfg, "Metroid.Visual.HudMatchStatusValueColorOverall", "Metroid.Visual.HudMatchStatusValueColorR", "Metroid.Visual.HudMatchStatusValueColorG", "Metroid.Visual.HudMatchStatusValueColorB");
    matchStatus.matchStatusSepColor = ReadOptionalSubColor(cfg, "Metroid.Visual.HudMatchStatusSepColorOverall", "Metroid.Visual.HudMatchStatusSepColorR", "Metroid.Visual.HudMatchStatusSepColorG", "Metroid.Visual.HudMatchStatusSepColorB");
    matchStatus.matchStatusGoalColor = ReadOptionalSubColor(cfg, "Metroid.Visual.HudMatchStatusGoalColorOverall", "Metroid.Visual.HudMatchStatusGoalColorR", "Metroid.Visual.HudMatchStatusGoalColorG", "Metroid.Visual.HudMatchStatusGoalColorB");
}
static void LoadBombLeftConfig(BombLeftHudConfig& bombLeft, Config::Table& cfg)
{
    bombLeft.bombLeftShow = cfg.GetBool("Metroid.Visual.HudBombLeftShow");
    bombLeft.bombLeftTextShow = cfg.GetBool("Metroid.Visual.HudBombLeftTextShow");
    bombLeft.bombLeftAnchor = cfg.GetInt("Metroid.Visual.HudBombLeftAnchor");
    bombLeft.bombLeftOfsX = cfg.GetInt("Metroid.Visual.HudBombLeftX");
    bombLeft.bombLeftOfsY = cfg.GetInt("Metroid.Visual.HudBombLeftY");
    bombLeft.bombLeftAlign = cfg.GetInt("Metroid.Visual.HudBombLeftAlign");
    bombLeft.bombLeftColor = ReadRgbColor(cfg, "Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB");
    CopyConfigString(bombLeft.bombLeftPrefix, sizeof(bombLeft.bombLeftPrefix), cfg.GetString("Metroid.Visual.HudBombLeftPrefix"));
    CopyConfigString(bombLeft.bombLeftSuffix, sizeof(bombLeft.bombLeftSuffix), cfg.GetString("Metroid.Visual.HudBombLeftSuffix"));
    bombLeft.bombIconShow = cfg.GetBool("Metroid.Visual.HudBombLeftIconShow");
    bombLeft.bombIconColorOverlay = cfg.GetBool("Metroid.Visual.HudBombLeftIconColorOverlay");
    bombLeft.bombIconHeight = cfg.GetInt("Metroid.Visual.HudBombIconHeight");
    bombLeft.bombIconColor = ReadRgbColor(cfg, "Metroid.Visual.HudBombLeftIconColorR", "Metroid.Visual.HudBombLeftIconColorG", "Metroid.Visual.HudBombLeftIconColorB");
    bombLeft.bombIconMode = cfg.GetInt("Metroid.Visual.HudBombLeftIconMode");
    bombLeft.bombIconOfsX = cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsX");
    bombLeft.bombIconOfsY = cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsY");
    bombLeft.bombIconPosAnchor = cfg.GetInt("Metroid.Visual.HudBombLeftIconPosAnchor");
    bombLeft.bombIconPosOfsX = cfg.GetInt("Metroid.Visual.HudBombLeftIconPosX");
    bombLeft.bombIconPosOfsY = cfg.GetInt("Metroid.Visual.HudBombLeftIconPosY");
    bombLeft.bombIconAnchorX = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorX");
    bombLeft.bombIconAnchorY = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorY");
}
static void LoadRankTimeConfig(RankTimeHudConfig& rankTime, Config::Table& cfg)
{
    rankTime.rankShow = cfg.GetBool("Metroid.Visual.HudRankShow");
    rankTime.rankAnchor = cfg.GetInt("Metroid.Visual.HudRankAnchor");
    rankTime.rankOfsX = cfg.GetInt("Metroid.Visual.HudRankX");
    rankTime.rankOfsY = cfg.GetInt("Metroid.Visual.HudRankY");
    rankTime.rankAlign = cfg.GetInt("Metroid.Visual.HudRankAlign");
    rankTime.rankColor = ReadRgbColor(cfg, "Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB");
    CopyConfigString(rankTime.rankPrefix, sizeof(rankTime.rankPrefix), cfg.GetString("Metroid.Visual.HudRankPrefix"));
    rankTime.rankShowOrdinal = cfg.GetBool("Metroid.Visual.HudRankShowOrdinal");
    CopyConfigString(rankTime.rankSuffix, sizeof(rankTime.rankSuffix), cfg.GetString("Metroid.Visual.HudRankSuffix"));
    rankTime.timeLeftShow = cfg.GetBool("Metroid.Visual.HudTimeLeftShow");
    rankTime.timeLeftAnchor = cfg.GetInt("Metroid.Visual.HudTimeLeftAnchor");
    rankTime.timeLeftOfsX = cfg.GetInt("Metroid.Visual.HudTimeLeftX");
    rankTime.timeLeftOfsY = cfg.GetInt("Metroid.Visual.HudTimeLeftY");
    rankTime.timeLeftAlign = cfg.GetInt("Metroid.Visual.HudTimeLeftAlign");
    rankTime.timeLeftColor = ReadRgbColor(cfg, "Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB");
    rankTime.timeLimitShow = cfg.GetBool("Metroid.Visual.HudTimeLimitShow");
    rankTime.timeLimitAnchor = cfg.GetInt("Metroid.Visual.HudTimeLimitAnchor");
    rankTime.timeLimitOfsX = cfg.GetInt("Metroid.Visual.HudTimeLimitX");
    rankTime.timeLimitOfsY = cfg.GetInt("Metroid.Visual.HudTimeLimitY");
    rankTime.timeLimitAlign = cfg.GetInt("Metroid.Visual.HudTimeLimitAlign");
    rankTime.timeLimitColor = ReadRgbColor(cfg, "Metroid.Visual.HudTimeLimitColorR", "Metroid.Visual.HudTimeLimitColorG", "Metroid.Visual.HudTimeLimitColorB");
}
static void LoadRadarOverlayConfig(RadarOverlayConfig& radar, Config::Table& cfg)
{
    radar.radarShow = cfg.GetBool("Metroid.Visual.BtmOverlayEnable");
    radar.radarAnchor = cfg.GetInt("Metroid.Visual.BtmOverlayAnchor");
    radar.radarOfsX = cfg.GetInt("Metroid.Visual.BtmOverlayDstX");
    radar.radarOfsY = cfg.GetInt("Metroid.Visual.BtmOverlayDstY");
    radar.radarDstSize = std::max(cfg.GetInt("Metroid.Visual.BtmOverlayDstSize"), 1);
    radar.radarOpacity = std::clamp(cfg.GetDouble("Metroid.Visual.BtmOverlayOpacity"), 0.0, 1.0);
    radar.radarSrcRadius = std::max(cfg.GetInt("Metroid.Visual.BtmOverlaySrcRadius"), 1);
    radar.radarFrameColor = ReadRgbColor(cfg, "Metroid.Visual.BtmOverlayRadarColorR",
                                              "Metroid.Visual.BtmOverlayRadarColorG",
                                              "Metroid.Visual.BtmOverlayRadarColorB");
    radar.radarFrameColorUseHunter = cfg.GetBool("Metroid.Visual.BtmOverlayRadarColorUseHunter");
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
    s_staticDirtyStale = true; // OPT-DR1: element positions changed
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
    auto loadOL = [&](HudOutlineConfig& ol, const char* pfx) {
        char k[80];
        auto key = [&](const char* s) { std::snprintf(k, sizeof(k), "Metroid.Visual.%s%s", pfx, s); return k; };
        ol.enable    = cfg.GetBool(key("Outline"));
        ol.color     = ReadRgbColor(cfg, key("OutlineColorR"), key("OutlineColorG"), key("OutlineColorB"));
        ol.opacity   = (float)std::clamp(cfg.GetDouble(key("OutlineOpacity")), 0.0, 1.0);
        ol.thickness = std::max(1, cfg.GetInt(key("OutlineThickness")));
        ol.colorWithAlpha = ol.color;
        ol.colorWithAlpha.setAlphaF(ol.opacity);
    };
    loadOL(c.globalOutline,        "HudGlobal");
    loadOL(c.hp.outline,           "HudHp");
    loadOL(c.hp.gaugeOutline,      "HudHpGauge");
    loadOL(c.weapon.outline,       "HudWeapon");
    loadOL(c.weapon.iconOutline,   "HudWeaponIcon");
    loadOL(c.weapon.gaugeOutline,  "HudAmmoGauge");
    loadOL(c.matchStatus.outline,  "HudMatchStatus");
    loadOL(c.rankTime.rankOutline,      "HudRank");
    loadOL(c.rankTime.timeLeftOutline,   "HudTimeLeft");
    loadOL(c.rankTime.timeLimitOutline,  "HudTimeLimit");
    loadOL(c.weaponInventory.outline,     "HudWeaponInventory");
    loadOL(c.weaponInventory.iconOutline, "HudWeaponInventoryIcon");
    loadOL(c.bombLeft.outline,      "HudBombLeft");
    loadOL(c.bombLeft.iconOutline,  "HudBombIcon");
    loadOL(c.radar.outline,        "BtmOverlay");
    loadOL(c.radar.frameOutline,   "BtmOverlayFrame");
    c.textScalePct        = std::max(100, cfg.GetInt("Metroid.Visual.HudTextScale"));
    const bool autoScaleEnable = cfg.GetBool("Metroid.Visual.HudAutoScaleEnable");
    if (autoScaleEnable) {
        // Auto-scale: integer resolution multiplier, capped by user preferences.
        // autoScaleInt = floor(hudScale), i.e. at 2.3x DS res -> 2.
        const int autoScaleInt = std::max(1, static_cast<int>(std::floor(hudScale)));
        const int autoScalePct = autoScaleInt * 100;
        const int globalCap    = std::clamp(cfg.GetInt("Metroid.Visual.HudAutoScaleCap"),      100, 800);
        auto capScale = [&](const char* catKey) -> float {
            int catCap = std::clamp(cfg.GetInt(catKey), 100, 800);
            return std::min({autoScalePct, globalCap, catCap}) / 100.0f;
        };
        c.scaleText      = capScale("Metroid.Visual.HudAutoScaleCapText");
        c.scaleIcons     = capScale("Metroid.Visual.HudAutoScaleCapIcons");
        c.scaleGauges    = capScale("Metroid.Visual.HudAutoScaleCapGauges");
        c.scaleCrosshair = capScale("Metroid.Visual.HudAutoScaleCapCrosshair");
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
    }
    c.lastHudScale        = hudScale;
    c.hpOpacity           = (float)cfg.GetDouble("Metroid.Visual.HudHpOpacity");
    c.weaponOpacity       = (float)cfg.GetDouble("Metroid.Visual.HudWeaponOpacity");
    c.matchStatusOpacity  = (float)cfg.GetDouble("Metroid.Visual.HudMatchStatusOpacity");
    c.rankOpacity         = (float)cfg.GetDouble("Metroid.Visual.HudRankOpacity");
    c.bombLeftOpacity     = (float)cfg.GetDouble("Metroid.Visual.HudBombLeftOpacity");
    c.hpGaugeOpacity      = (float)cfg.GetDouble("Metroid.Visual.HudHpGaugeOpacity");
    c.wpnIconOpacity      = (float)cfg.GetDouble("Metroid.Visual.HudWpnIconOpacity");
    c.ammoGaugeOpacity    = (float)cfg.GetDouble("Metroid.Visual.HudAmmoGaugeOpacity");
    c.bombIconOpacity     = (float)cfg.GetDouble("Metroid.Visual.HudBombIconOpacity");
    c.timeLeftOpacity     = (float)cfg.GetDouble("Metroid.Visual.HudTimeLeftOpacity");
    c.timeLimitOpacity    = (float)cfg.GetDouble("Metroid.Visual.HudTimeLimitOpacity");
    RecomputeAnchorPositions(topStretchX);
    // P-17: Rebuild crosshair outline stamp now that geometry is final.
    RebuildCrosshairOutlineStamp(c.crosshair);
    ++s_cacheEpoch;
    if (s_cacheEpoch == 0) s_cacheEpoch = 1;
}

