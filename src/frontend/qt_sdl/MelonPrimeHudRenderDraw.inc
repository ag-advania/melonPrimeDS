// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  Gauge drawing — barLengthPx / barWidthPx are in actual output pixels.
//  Divided by hudScale to get DS-space; painter's ×hudScale restores exact pixels.
// =========================================================================
static void DrawGauge(QPainter* p, int x, int y, float ratio,
                      const QColor& fillColor, int orientation,
                      int barLengthPx, int barWidthPx,
                      const HudOutlineConfig* outline = nullptr,
                      float hudScale = 1.0f)
{
    ratio = (ratio < 0.0f) ? 0.0f : (ratio > 1.0f) ? 1.0f : ratio;
    if (barLengthPx <= 0) barLengthPx = 28;
    if (barWidthPx  <= 0) barWidthPx  = 3;

    const float barLength = barLengthPx / hudScale;
    const float barWidth  = barWidthPx  / hudScale;

    static const QColor bgColor(0, 0, 0, 128); // P-4: construct once

    // OPT-DR2: accumulate the outline-expanded full-bar bbox.
    {
        const float eDS = (outline && outline->enable && outline->opacity > 0.0f)
                          ? static_cast<float>(outline->thickness) / hudScale : 0.0f;
        const float w = (orientation == 0 ? barLength : barWidth)  + eDS * 2.0f;
        const float h = (orientation == 0 ? barWidth  : barLength) + eDS * 2.0f;
        AccumDirtyDs(p, QRectF(x - eDS, y - eDS, w, h));
    }

    if (outline && outline->enable && outline->opacity > 0.0f) {
        const float expand = static_cast<float>(outline->thickness) / hudScale;
        const QColor& outlineColor = outline->colorWithAlpha; // P-14
        if (orientation == 0) {
            p->fillRect(QRectF(x - expand, y - expand, barLength + expand * 2.0f, barWidth + expand * 2.0f), outlineColor);
        } else {
            p->fillRect(QRectF(x - expand, y - expand, barWidth + expand * 2.0f, barLength + expand * 2.0f), outlineColor);
        }
    }

    if (orientation == 0) {
        p->fillRect(QRectF(x, y, barLength, barWidth), bgColor);
        float fillW = barLength * ratio;
        if (fillW > 0.0f) p->fillRect(QRectF(x, y, fillW, barWidth), fillColor);
    } else {
        p->fillRect(QRectF(x, y, barWidth, barLength), bgColor);
        float fillH = barLength * ratio;
        if (fillH > 0.0f) p->fillRect(QRectF(x, y + barLength - fillH, barWidth, fillH), fillColor);
    }
}

// Color-by-value ramp evaluation: picks the stop whose threshold (minimum percent)
// is the largest value still <= the element's current percentage (value/max).
// Order-independent; falls back to `fallback` when max is 0 or no stop matches.
// Division-free: compares cross-multiplied `value*100 >= threshold*max` instead of
// computing pct (avoids the per-call IDIV); the cheap `t > bestThr` test short-circuits
// the multiply, so a descending-sorted ramp does only 1-2 multiplies. Only runs when
// the ramp is enabled (gated at the call site), so disabled HUDs pay nothing.
static inline const QColor& EvalColorRamp(const HudColorRamp& r, uint16_t value,
                                          uint16_t maxValue, const QColor& fallback)
{
    if (maxValue == 0) return fallback;
    const uint32_t scaled = static_cast<uint32_t>(value) * 100u;  // == pct * maxValue
    int best = -1, bestThr = -1;
    const int n = (r.count < kHudColorRampMax) ? r.count : kHudColorRampMax;
    for (int i = 0; i < n; i++) {
        const int t = r.thresholdPct[i];
        if (t > bestThr && scaled >= static_cast<uint32_t>(t) * maxValue) {
            bestThr = t; best = i;
        }
    }
    return (best >= 0) ? r.color[best] : fallback;
}

static float CalcAlignedTextX(int anchorX, int align, float textW)
{
    return HudGeometry::AlignedTextX(static_cast<float>(anchorX), align, textW);
}

static void CalcGaugePos(float textX, float textY, float textW, float textH, int anchor,
                         int ofsX, int ofsY, float gaugeLen, float gaugeWid, int ori,
                         int& outX, int& outY)
{
    HudGeometry::CalcGaugePos(textX, textY, textW, textH, anchor, ofsX, ofsY,
                              gaugeLen, gaugeWid, ori, outX, outY);
}

// Inverse of CalcGaugePos: given the gauge's effective top-left and dimensions,
// compute where the text should be placed (anchor = which side of the gauge).
// anchor: 0=Below, 1=Above, 2=Right, 3=Left, 4=Center  (same enum as GaugeAnchor5)
// textY convention: bottom edge of text (same as CalcGaugePos's textY).
// anchor: 0=Below, 1=Above, 2=Right, 3=Left, 4=Center
static void CalcTextPosFromGauge(int gx, int gy, int gaugeLen, int gaugeWid, int ori,
                                  int anchor, int ofsX, int ofsY,
                                  float textW, float textH,
                                  float& outTextX, float& outTextY)
{
    HudGeometry::CalcTextPosFromGauge(static_cast<float>(gx), static_cast<float>(gy),
                                      static_cast<float>(gaugeLen), static_cast<float>(gaugeWid),
                                      ori, anchor, ofsX, ofsY, textW, textH,
                                      outTextX, outTextY);
}

// =========================================================================
//  P-5: DrawHP — stack-buffer text, reads CachedHudConfig directly
// =========================================================================
static inline void DrawHP(QPainter* p, uint16_t hp, uint16_t maxHP,
                           const CachedHudConfig& c, float tds)
{
    // Color-by-value ramp (on by default) overrides the static color when enabled.
    // const-ref binding: both branches are lvalues, so no QColor copy.
    const QColor& hpTextColor = c.hp.hpTextRamp.enabled
        ? EvalColorRamp(c.hp.hpTextRamp, hp, maxHP, c.hp.hpTextColor)
        : c.hp.hpTextColor;

    static TextMeasureCache s_hpTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_hpBitmapCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
    static TextBitmapCache s_hpOutlineCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
    const QFontMetrics& fm = s_frameFm;        // P-9
    const int fontPixelSize = s_frameFpx;       // P-9
    // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.

    char buf[24];
    std::snprintf(buf, sizeof(buf), "%s%u", c.hp.hpPrefix, hp);
    float textW = 0.0f, textH = 0.0f;
    MeasureTextCached(fm, fontPixelSize, s_hpTextCache, buf, textW, textH, tds);
    float textX, hpTextY;
    if (c.hp.hpGaugePosMode == 2) {
        // mode 2: text side relative to gauge — compute gauge effective top-left first
        const float hs = c.lastHudScale;
        const int gl = (int)(c.hp.hpGaugeLen / hs * c.scaleGauges);
        const int gw = (int)(c.hp.hpGaugeWid / hs * c.scaleGauges);
        int gxEff = c.hp.hpGaugePosX, gyEff = c.hp.hpGaugePosY;
        if (c.hp.hpGaugeOri == 0) gxEff -= gl * c.hp.hpGaugeAlign / 2;
        else                       gyEff -= gl * c.hp.hpGaugeAlign / 2;
        CalcTextPosFromGauge(gxEff, gyEff, gl, gw, c.hp.hpGaugeOri,
                             c.hp.hpTextAnchor, c.hp.hpTextOfsX, c.hp.hpTextOfsY,
                             textW, textH, textX, hpTextY);
    } else {
        textX  = CalcAlignedTextX(c.hp.hpX, c.hp.hpAlign, textW);
        hpTextY = static_cast<float>(c.hp.hpY);
    }
    PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_hpBitmapCache, buf, hpTextColor);
    { const HudOutlineConfig& _ol = EffOL(c, c.hp.outline);
    if (_ol.enable && _ol.opacity > 0.0f) {
        const float renderScale = tds * c.lastHudScale;
        PrepareOutlineBitmapCached(s_hpOutlineCache, s_hpBitmapCache, _ol.color, _ol.thickness, renderScale);
        DrawCachedTextOutlined(p, s_hpBitmapCache, s_hpOutlineCache, textX, hpTextY, tds,
                               c.hpOpacity, _ol.opacity);
    } else {
        if (c.hpOpacity < 1.0f) p->setOpacity(c.hpOpacity);
        DrawCachedText(p, s_hpBitmapCache, textX, hpTextY, tds);
        if (c.hpOpacity < 1.0f) p->setOpacity(1.0f);
    } }

    if (c.hp.hpGauge && maxHP > 0) {
        float ratio = static_cast<float>(hp) / static_cast<float>(maxHP);
        const QColor& gc = c.hp.hpGaugeRamp.enabled
            ? EvalColorRamp(c.hp.hpGaugeRamp, hp, maxHP, c.hp.hpGaugeColor)
            : c.hp.hpGaugeColor;
        const float hs = c.lastHudScale;
        int gx, gy;
        if (c.hp.hpGaugePosMode == 0) {
            // gauge relative to text
            CalcGaugePos(textX, hpTextY, textW, textH, c.hp.hpGaugeAnchor, c.hp.hpGaugeOfsX, c.hp.hpGaugeOfsY,
                         c.hp.hpGaugeLen / hs * c.scaleGauges, c.hp.hpGaugeWid / hs * c.scaleGauges, c.hp.hpGaugeOri, gx, gy);
        } else {
            // modes 1 (independent) and 2 (text follows gauge): gauge at absolute position
            gx = c.hp.hpGaugePosX;
            gy = c.hp.hpGaugePosY;
        }
        { const int gl_ds = (int)(c.hp.hpGaugeLen / hs * c.scaleGauges);
          if (c.hp.hpGaugeOri == 0) gx -= gl_ds * c.hp.hpGaugeAlign / 2;
          else                      gy -= gl_ds * c.hp.hpGaugeAlign / 2; }
        if (c.hpGaugeOpacity < 1.0f) p->setOpacity(c.hpGaugeOpacity);
        DrawGauge(p, gx, gy, ratio, gc, c.hp.hpGaugeOri, (int)(c.hp.hpGaugeLen * c.scaleGauges), (int)(c.hp.hpGaugeWid * c.scaleGauges),
                  &EffOL(c, c.hp.gaugeOutline), hs);
        if (c.hpGaugeOpacity < 1.0f) p->setOpacity(1.0);
    }
}

// =========================================================================
//  P-6: Weapon divisor table — replaces two switch statements
// =========================================================================
struct WeaponInfo { uint16_t divisor; bool isMissile; };

static constexpr WeaponInfo kWeaponTable[9] = {
    {0,    false}, {0x5,  false}, {0xA,  true },
    {0x4,  false}, {0x14, false}, {0x5,  false},
    {0xA,  false}, {0xA,  false}, {1,    false},
};

// =========================================================================
//  DrawWeaponInventory — show all 9 weapons with ammo counts
static void DrawWeaponInventory(QPainter* p,
                                 uint16_t havingWeapons, uint8_t currentWeapon,
                                 uint16_t ammoSpecial, uint16_t ammoMissile,
                                 const CachedHudConfig& c, float tds, float hudScale)
{
    const WeaponInventoryHudConfig& wi = c.weaponInventory;
    if (!wi.show) return;

    // scaleIcons is derived from HudAutoScaleEnable + HudAutoScaleCapIcons.
    const float iconScale = (c.scaleIcons > 0.0f) ? c.scaleIcons : 1.0f;

    // Load icons at the same pixel size as DrawWeaponAmmo (c.weapon.iconHeight * scaleIcons)
    // to avoid per-frame reload contention. The drawn DS-space size uses wi.iconHeight.
    EnsureIconsLoaded(std::max(4, static_cast<int>(std::round(
        static_cast<float>(c.weapon.iconHeight) * iconScale))));

    // DS-space icon height: wi.iconHeight * scaleIcons / hudScale
    // mirrors the main weapon icon formula (iconHeight * scaleIcons px, drawn at px/hudScale DS-units).
    const float iH = (hudScale > 0.0f)
        ? static_cast<float>(wi.iconHeight) * iconScale / hudScale
        : static_cast<float>(wi.iconHeight);
    const float rowH = iH + static_cast<float>(wi.spacing);

    // Display order: PB, Missile, ShockCoil, Magmaul, Judicator, Imperialist, BattleHammer, VoltDriver, OmegaCannon
    static constexpr int kInventoryOrder[9] = { 0, 2, 7, 6, 5, 4, 3, 1, 8 };

    // Count drawable weapons to skip entirely if none
    int drawCount = 0;
    for (int si = 0; si < 9; ++si) {
        const int i = kInventoryOrder[si];
        const bool owned = (havingWeapons & (1u << i)) != 0;
        const float eff = owned ? wi.opacity : wi.notOwnedOpacity;
        if (eff > 0.0f) ++drawCount;
    }
    if (drawCount == 0) return;

    // Max icon width for vertical center-axis alignment. The aspect ratio is
    // cached when the SVGs are rasterized, so this avoids a per-frame icon scan.
    const float maxDrawIW = iH * s_weaponIconMaxAspect;

    // OPT-WI1: QPainter::save()/restore() copies the full painter state.
    // DrawWeaponInventory only mutates opacity, SmoothPixmapTransform, and
    // highlight pen/brush, so track just those values.
    const qreal savedOpacity = p->opacity();
    const bool savedSmoothPixmap = p->testRenderHint(QPainter::SmoothPixmapTransform);
    QPen savedPen;
    QBrush savedBrush;
    bool penBrushChanged = false;
    bool smoothPixmapChanged = false;
    bool smoothPixmapActive = savedSmoothPixmap;

    const float startX = static_cast<float>(wi.posX);
    const float startY = static_cast<float>(wi.posY);

    // For vertical mode: fixed icon center X axis and text start X so all icons share one axis.
    // iconCenterX is determined by alignment relative to startX.
    const float iconCenterX = (wi.orientation == 1)
        ? (wi.align == 1 ? startX
         : wi.align == 2 ? startX - maxDrawIW * 0.5f
         :                 startX + maxDrawIW * 0.5f)   // Left: column left edge at startX
        : 0.0f; // unused for horizontal
    const float vertTextX = iconCenterX + maxDrawIW * 0.5f + 2.0f;

    float curX = startX;
    float curY = startY;

    // Per-slot text bitmap caches — persist across frames, invalidated by PrepareTextBitmapCached.
    static TextBitmapCache s_invTextCache[9]    = {};
    static TextBitmapCache s_invOutlineCache[9] = {};

    const QFontMetrics& fm      = s_frameFm;
    const int           fontPx  = s_frameFpx;
    // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.
    const HudOutlineConfig& invIconOutline = EffOL(c, c.weaponInventory.iconOutline);
    const HudOutlineConfig& invTextOutline = EffOL(c, c.weaponInventory.outline);
    const bool useIconOutline = invIconOutline.enable && invIconOutline.opacity > 0.0f;
    const bool useTextOutline = invTextOutline.enable && invTextOutline.opacity > 0.0f;
    const int iconExpandR = useIconOutline ? std::max(1, invIconOutline.thickness) : 0;
    if (useIconOutline)
        EnsureInvOutlineIconsUpdated(invIconOutline.color, iconExpandR);

    for (int si = 0; si < 9; ++si) {
        const int i = kInventoryOrder[si];
        const bool owned = (havingWeapons & (1u << i)) != 0;
        const float eff = owned ? wi.opacity : wi.notOwnedOpacity;
        if (eff <= 0.0f) continue;

        p->setOpacity(eff);

        // Get icon image
        const QImage& icon = s_weaponIcons[i];
        const float drawIH = iH;
        const float drawIW = (!icon.isNull() && icon.height() > 0)
                             ? drawIH * static_cast<float>(icon.width()) / static_cast<float>(icon.height())
                             : drawIH;

        // Compute ammo string — mirrors DrawWeaponAmmo logic; unowned weapons show no ammo
        char buf[16] = "";
        const WeaponInfo& winfo = kWeaponTable[i];
        if (owned && winfo.divisor > 0) {
            uint16_t ammo;
            if (winfo.isMissile) {
                ammo = ammoMissile / winfo.divisor;
                std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(ammo));
            } else if (i == WeaponId::OmegaCannon) {
                // Omega Cannon: always 1 shot — only show when possession flag is set
                if (havingWeapons & WeaponMask::OmegaCannon)
                    std::snprintf(buf, sizeof(buf), "1");
            } else {
                ammo = ammoSpecial / winfo.divisor;
                std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(ammo));
            }
        }
        // Power Beam (divisor=0): no ammo shown

        // Prepare text bitmap (respects tds / TextScale / CapText via DrawCachedText scaling).
        if (buf[0] && fontPx > 0)
            PrepareTextBitmapCached(fm, s_frameFont, fontPx, s_invTextCache[si], buf, wi.textColor);
        else
            s_invTextCache[si].valid = false;

        // Text dimensions in DS-space via bitmap * tds. The bitmap may be hi-DPI
        // (width = native * renderScale), so divide it back out to native px first.
        const TextBitmapCache& tc = s_invTextCache[si];
        const float textW = (tc.valid && !tc.bitmap.isNull())
                            ? static_cast<float>(tc.bitmap.width()) / tc.renderScale * tds : 0.0f;

        float px, py;
        float textX;
        float textBaseY;  // baselineY arg for DrawCachedText (kept as float for sub-pixel precision)

        if (wi.orientation == 1) {
            // Vertical: all icons share iconCenterX axis
            px = iconCenterX - drawIW * 0.5f;
            py = curY;
            textX = vertTextX;
        } else {
            // Horizontal: align shifts Y
            px = curX;
            py = wi.align == 1 ? startY - drawIH * 0.5f
               : wi.align == 2 ? startY - drawIH
               :                 startY;
            textX = px + drawIW + 2.0f;
        }
        // Center glyph pixels on the icon center without integer rounding.
        // DrawCachedText places bitmap top at (textBaseY + originY*tds).
        // Digit glyphs span from bitmap-top to the baseline (-originY pixels, no descenders),
        // so visual center = bitmap-top + (-originY*tds)/2.
        // Setting that equal to iconCenterY = py + drawIH/2:
        //   textBaseY = py + drawIH/2 - originY*tds/2
        {
            const float originYds = tc.valid ? static_cast<float>(tc.originY) * tds : 0.0f;
            textBaseY = py + drawIH * 0.5f - originYds * 0.5f;
        }

        // Draw highlight rounded-rect outline around icon + ammo text for currently selected weapon
        if (wi.highlightEnable && i == static_cast<int>(currentWeapon) && wi.highlightOpacity > 0.0f) {
            if (!penBrushChanged) {
                savedPen = p->pen();
                savedBrush = p->brush();
                penBrushChanged = true;
            }
            const float pad    = static_cast<float>(wi.highlightPadding);
            const float ofsL   = static_cast<float>(wi.highlightOffsetLeft);
            const float ofsR   = static_cast<float>(wi.highlightOffsetRight);
            const float ofsT   = static_cast<float>(wi.highlightOffsetTop);
            const float ofsB   = static_cast<float>(wi.highlightOffsetBottom);
            const float right  = (textW > 0.0f) ? (textX + textW + pad) : (px + drawIW + pad);
            const QRectF hlRect(px  - pad - ofsL,
                                py  - pad - ofsT,
                                right - (px - pad) + ofsL + ofsR,
                                drawIH + 2.0f * pad + ofsT + ofsB);
            const double radius = static_cast<double>(wi.highlightCornerRadius);
            p->setOpacity(wi.highlightOpacity);
            QPen hlPen(wi.highlightColor);
            hlPen.setWidthF(static_cast<double>(wi.highlightThickness));
            p->setPen(hlPen);
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(hlRect, radius, radius);
            { const float t = static_cast<float>(wi.highlightThickness);
              AccumDirtyDs(p, hlRect.adjusted(-t, -t, t, t)); } // OPT-DR2
            p->setPen(Qt::NoPen);
            p->setOpacity(eff);  // restore per-slot opacity for icon + text
        }

        // Draw icon (with optional outline)
        if (!icon.isNull()) {
            if (!smoothPixmapActive) {
                p->setRenderHint(QPainter::SmoothPixmapTransform, true);
                smoothPixmapActive = true;
                smoothPixmapChanged = true;
            }
            if (useIconOutline) {
                DrawImageOutlined(p, icon, s_invIconsOutline[i],
                                  QRectF(px, py, drawIW, drawIH),
                                  static_cast<float>(iconExpandR) / hudScale,
                                  eff, invIconOutline.opacity);
                p->setOpacity(eff); // DrawImageOutlined resets opacity to 1.0; restore for text
            } else {
                const QRectF iconRect(px, py, drawIW, drawIH);
                p->drawImage(iconRect, icon);
                AccumDirtyDs(p, iconRect); // OPT-DR2
            }
        }

        // Draw ammo text via bitmap cache (tds scaling → TextScale + CapText applied)
        if (s_invTextCache[si].valid) {
            if (smoothPixmapActive) {
                p->setRenderHint(QPainter::SmoothPixmapTransform, false);
                smoothPixmapActive = false;
                smoothPixmapChanged = true;
            }
            if (useTextOutline) {
                PrepareOutlineBitmapCached(s_invOutlineCache[si], s_invTextCache[si],
                                           invTextOutline.color, invTextOutline.thickness, tds * hudScale);
                DrawCachedTextOutlined(p, s_invTextCache[si], s_invOutlineCache[si],
                                       textX, textBaseY, tds, eff, invTextOutline.opacity);
            } else {
                DrawCachedText(p, s_invTextCache[si], textX, textBaseY, tds);
            }
        }

        // Advance position
        if (wi.orientation == 1) {
            curY += rowH;
        } else {
            curX += drawIW + (buf[0] ? (2.0f + textW) : 0.0f) + static_cast<float>(wi.spacing);
        }
    }

    // Manual restore for the attributes changed above.
    if (penBrushChanged) {
        p->setPen(savedPen);
        p->setBrush(savedBrush);
    }
    if (smoothPixmapChanged && smoothPixmapActive != savedSmoothPixmap)
        p->setRenderHint(QPainter::SmoothPixmapTransform, savedSmoothPixmap);
    p->setOpacity(savedOpacity);
}

// =========================================================================
//  P-7: DrawWeaponAmmo — cached icon, table lookup, stack text
static void DrawWeaponAmmo(QPainter* p,
                           uint8_t weapon, uint16_t ammoSpecial, uint16_t ammoMissile,
                           uint16_t maxAmmoSpecial, uint16_t maxAmmoMissile,
                           const CachedHudConfig& c, float tds, float hudScale)
{
    static TextMeasureCache s_ammoTextCache = { 0, "", 0, 0, false };
    static TextBitmapCache s_ammoBitmapCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
    static TextBitmapCache s_ammoOutlineCache = { 0, QColor(), "", 0, 0, 0, 1.0f, false, QImage() };
    const QFontMetrics& fm = s_frameFm;        // P-9
    const int fontPixelSize = s_frameFpx;       // P-9
    // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.

    const uint8_t weaponIdx = (weapon < 9) ? weapon : 0;
    const WeaponInfo& wi = kWeaponTable[weaponIdx];
    EnsureIconsLoaded(static_cast<int>(c.weapon.iconHeight * c.scaleIcons));
    const bool iconOvEnable = c.weapon.iconOverlayEnable[weaponIdx];
    const QColor& iconOvColor = c.weapon.iconOverlayColor[weaponIdx];
    const QImage& icon = GetWeaponIconForDraw(weaponIdx, iconOvEnable, iconOvColor); // P-1

    uint16_t ammo = 0, maxAmmo = 0;
    bool hasAmmo = (wi.divisor > 0);

    if (hasAmmo) {
        if (wi.isMissile) {
            ammo    = ammoMissile / wi.divisor;
            maxAmmo = maxAmmoMissile / wi.divisor;
        } else if (weaponIdx == 8) {
            ammo = 1; maxAmmo = 1;
        } else {
            ammo    = ammoSpecial / wi.divisor;
            maxAmmo = maxAmmoSpecial / wi.divisor;
        }
    }

    float textX = static_cast<float>(c.weapon.wpnX), textY = static_cast<float>(c.weapon.wpnY);
    float textW = 0.0f, textH = static_cast<float>(fm.height());
    if (hasAmmo) {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%s%02u", c.weapon.ammoPrefix, ammo);
        MeasureTextCached(fm, fontPixelSize, s_ammoTextCache, buf, textW, textH, tds);
        if (c.weapon.ammoGaugePosMode == 2) {
            // mode 2: text side relative to gauge — compute gauge effective top-left first
            const float hs = hudScale;
            const int gl = (int)(c.weapon.ammoGaugeLen / hs * c.scaleGauges);
            const int gw = (int)(c.weapon.ammoGaugeWid / hs * c.scaleGauges);
            int gxEff = c.weapon.ammoGaugePosX, gyEff = c.weapon.ammoGaugePosY;
            if (c.weapon.ammoGaugeOri == 0) gxEff -= gl * c.weapon.ammoGaugeAlign / 2;
            else                             gyEff -= gl * c.weapon.ammoGaugeAlign / 2;
            CalcTextPosFromGauge(gxEff, gyEff, gl, gw, c.weapon.ammoGaugeOri,
                                 c.weapon.ammoTextAnchor, c.weapon.ammoTextOfsX, c.weapon.ammoTextOfsY,
                                 textW, textH, textX, textY);
        } else {
            textX = CalcAlignedTextX(c.weapon.wpnX, c.weapon.ammoAlign, textW);
        }
        const QColor& ammoTextColorEff = c.weapon.ammoTextRamp.enabled
            ? EvalColorRamp(c.weapon.ammoTextRamp, ammo, maxAmmo, c.weapon.ammoTextColor)
            : c.weapon.ammoTextColor;
        PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_ammoBitmapCache, buf, ammoTextColorEff);
        { const HudOutlineConfig& _ol = EffOL(c, c.weapon.outline);
        if (_ol.enable && _ol.opacity > 0.0f) {
            const float renderScale = tds * hudScale;
            PrepareOutlineBitmapCached(s_ammoOutlineCache, s_ammoBitmapCache, _ol.color, _ol.thickness, renderScale);
            DrawCachedTextOutlined(p, s_ammoBitmapCache, s_ammoOutlineCache, textX, textY, tds,
                                   c.weaponOpacity, _ol.opacity);
        } else {
            if (c.weaponOpacity < 1.0f) p->setOpacity(c.weaponOpacity);
            DrawCachedText(p, s_ammoBitmapCache, textX, textY, tds);
            p->setOpacity(1.0f);
        } }
    }

    if (c.weapon.iconShow && !icon.isNull()) {
        const float dw = icon.width()  / hudScale;
        const float dh = icon.height() / hudScale;
        float ix = (c.weapon.iconMode == 0) ? c.weapon.wpnX + c.weapon.iconOfsX : c.weapon.iconPosX;
        float iy = (c.weapon.iconMode == 0) ? c.weapon.wpnY + c.weapon.iconOfsY : c.weapon.iconPosY;
        if (c.weapon.iconAnchorX == 1) ix -= dw * 0.5f;
        else if (c.weapon.iconAnchorX == 2) ix -= dw;
        if (c.weapon.iconAnchorY == 1) iy -= dh * 0.5f;
        else if (c.weapon.iconAnchorY == 2) iy -= dh;
        { const HudOutlineConfig& _ol = EffOL(c, c.weapon.iconOutline);
        if (_ol.enable && _ol.opacity > 0.0f) {
            const int expandR = std::max(1, _ol.thickness);
            EnsureWeaponOutlineIconsUpdated(_ol.color, expandR);
            DrawImageOutlined(p, icon, s_weaponIconsOutline[weaponIdx],
                              QRectF(ix, iy, dw, dh), (float)expandR / hudScale,
                              c.wpnIconOpacity, _ol.opacity);
        } else {
            const QRectF iconRect(ix, iy, dw, dh);
            if (c.wpnIconOpacity < 1.0f) p->setOpacity(c.wpnIconOpacity);
            p->drawImage(iconRect, icon);
            AccumDirtyDs(p, iconRect); // OPT-DR2
            p->setOpacity(1.0f);
        } }
    }

    if (c.weapon.ammoGauge && hasAmmo && maxAmmo > 0) {
        float ratio = static_cast<float>(ammo) / static_cast<float>(maxAmmo);
        int gx, gy;
        if (c.weapon.ammoGaugePosMode == 0) {
            // gauge relative to text
            CalcGaugePos(textX, textY, textW, textH, c.weapon.ammoGaugeAnchor, c.weapon.ammoGaugeOfsX, c.weapon.ammoGaugeOfsY,
                         c.weapon.ammoGaugeLen / hudScale * c.scaleGauges, c.weapon.ammoGaugeWid / hudScale * c.scaleGauges, c.weapon.ammoGaugeOri, gx, gy);
        } else {
            // modes 1 (independent) and 2 (text follows gauge): gauge at absolute position
            gx = c.weapon.ammoGaugePosX;
            gy = c.weapon.ammoGaugePosY;
        }
        { const int gl_ds = (int)(c.weapon.ammoGaugeLen / hudScale * c.scaleGauges);
          if (c.weapon.ammoGaugeOri == 0) gx -= gl_ds * c.weapon.ammoGaugeAlign / 2;
          else                            gy -= gl_ds * c.weapon.ammoGaugeAlign / 2; }
        if (c.ammoGaugeOpacity < 1.0f) p->setOpacity(c.ammoGaugeOpacity);
        const QColor& ammoGaugeColorEff = c.weapon.ammoGaugeRamp.enabled
            ? EvalColorRamp(c.weapon.ammoGaugeRamp, ammo, maxAmmo, c.weapon.ammoGaugeColor)
            : c.weapon.ammoGaugeColor;
        DrawGauge(p, gx, gy, ratio, ammoGaugeColorEff, c.weapon.ammoGaugeOri, (int)(c.weapon.ammoGaugeLen * c.scaleGauges), (int)(c.weapon.ammoGaugeWid * c.scaleGauges),
                  &EffOL(c, c.weapon.gaugeOutline), hudScale);
        if (c.ammoGaugeOpacity < 1.0f) p->setOpacity(1.0);
    }
}

// =========================================================================
//  Crosshair
// =========================================================================

// Arms stored as rects now (not line endpoints) to avoid pen-centering artifacts.
static void CollectArmRects(QRect* out, int& n, int cx, int cy,
                            int lengthX, int lengthY, int offset,
                            int thickness, bool tStyle)
{
    const int halfT = thickness / 2;
    n = 0;
    if (lengthX > 0) {
        // Left arm
        out[n++] = QRect(cx - offset - lengthX, cy - halfT, lengthX, thickness);
        // Right arm
        out[n++] = QRect(cx + offset + 1, cy - halfT, lengthX, thickness);
    }
    if (lengthY > 0) {
        // Down arm
        out[n++] = QRect(cx - halfT, cy + offset + 1, thickness, lengthY);
        // Up arm (skip if T-style)
        if (!tStyle)
            out[n++] = QRect(cx - halfT, cy - offset - lengthY, thickness, lengthY);
    }
}

// Float version of CollectArmRects — uses QRectF for sub-pixel precision and
// applies chScale to all geometry (arm lengths, offset, thickness).
static void CollectArmRectsF(QRectF* out, int& n, float cx, float cy,
                              float lengthX, float lengthY, float offset,
                              float thickness, bool tStyle)
{
    const float halfT = thickness * 0.5f;
    n = 0;
    if (lengthX > 0.0f) {
        out[n++] = QRectF(cx - offset - lengthX, cy - halfT, lengthX, thickness);
        out[n++] = QRectF(cx + offset,           cy - halfT, lengthX, thickness);
    }
    if (lengthY > 0.0f) {
        out[n++] = QRectF(cx - halfT, cy + offset,           thickness, lengthY);
        if (!tStyle)
            out[n++] = QRectF(cx - halfT, cy - offset - lengthY, thickness, lengthY);
    }
}

// Fills a center-dot rect as a square (legacy fillRect) or circle (anti-aliased
// brush ellipse). Respects the painter's current opacity. The circle path
// temporarily enables antialiasing and restores the prior hint/brush.
static inline void DrawCenterDotShape(QPainter* p, const QRect& rc,
                                      const QColor& color, bool circle)
{
    if (!circle) {
        p->fillRect(rc, color);
        return;
    }
    const bool prevAA = p->testRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawEllipse(rc);
    p->setBrush(Qt::NoBrush);
    p->setRenderHint(QPainter::Antialiasing, prevAA);
}

// P-17: Rebuild the crosshair outline stamp from the cached arm geometry.
// Called once from RefreshCachedConfig whenever crosshair config changes.
static void RebuildCrosshairOutlineStamp(const CrosshairHudConfig& ch)
{
    s_chStampValid = false;
    if (!ch.chOutline) return;

    const int olT = ch.chOutlineThickness;
    const int dotHalf = ch.chCenterDot
                      ? (ch.cachedDotSizePx + olT * 2) / 2 : 0;

    // Bounding box of all outline-expanded rects, centred at origin.
    int minX = -dotHalf, maxX = dotHalf;
    int minY = -dotHalf, maxY = dotHalf;
    auto expand = [&](const QRect& r) {
        int l  = r.left()                   - olT;
        int ri = r.left()  + r.width()  + olT;
        int t  = r.top()                    - olT;
        int b  = r.top()   + r.height() + olT;
        if (l  < minX) minX = l;
        if (ri > maxX) maxX = ri;
        if (t  < minY) minY = t;
        if (b  > maxY) maxY = b;
    };
    for (int i = 0; i < ch.cachedInnerCount; i++) expand(ch.cachedInnerRects[i]);
    for (int i = 0; i < ch.cachedOuterCount; i++) expand(ch.cachedOuterRects[i]);

    if (maxX <= minX || maxY <= minY) return;

    // +2 border so drawImage never clips edge pixels.
    const int w = maxX - minX + 2;
    const int h = maxY - minY + 2;
    // stampOrigin is added to (pxCx,pxCy) to get the drawImage top-left pixel.
    s_chStampOrigin = QPoint(minX - 1, minY - 1);

    s_chOutlineStamp = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    s_chOutlineStamp.fill(Qt::transparent);

    QPainter sp(&s_chOutlineStamp);
    sp.setRenderHint(QPainter::Antialiasing, false);
    static const QColor solidBlack(0, 0, 0, 255);

    // Local origin: shift (0,0) → (ox,oy) inside the stamp image.
    const int ox = -minX + 1;
    const int oy = -minY + 1;

    for (int i = 0; i < ch.cachedInnerCount; i++)
        sp.fillRect(ch.cachedInnerRects[i].adjusted(-olT,-olT,olT,olT).translated(ox,oy), solidBlack);
    for (int i = 0; i < ch.cachedOuterCount; i++)
        sp.fillRect(ch.cachedOuterRects[i].adjusted(-olT,-olT,olT,olT).translated(ox,oy), solidBlack);
    if (ch.chCenterDot) {
        const int dh = dotHalf;
        DrawCenterDotShape(&sp, QRect(ox - dh, oy - dh, dh * 2, dh * 2), solidBlack,
                           ch.chDotShape == 1);
    }
    sp.end();
    s_chStampValid = true;
}

static void UpdateCrosshairZoomAmountForGameFrame(melonDS::u8* ram,
                                                  const RomAddresses& rom,
                                                  uint32_t gameFrame)
{
#ifdef MELONPRIME_DS
    if (s_chZoomReticleState.lastGameFrame == gameFrame)
        return;
    s_chZoomReticleState.lastGameFrame = gameFrame;

    const ZoomStatus::ScopeState scope =
        ZoomStatus::ReadScopeState(
            ram, rom.hookLocalPlayerPtrGlobal, s_chZoomReticleState.canZoomCache);
    if (!scope.valid) {
        s_chZoomReticleState = {};
        return;
    }

    ZoomStatus::UpdateReticleVisibilityState(
        s_chZoomReticleState, scope.rawVisible);
#else
    (void)ram;
    (void)rom;
    (void)gameFrame;
#endif
}

static float ReadCrosshairZoomAmount(melonDS::u8* ram, const RomAddresses& rom)
{
    (void)ram;
    (void)rom;
    return s_chZoomReticleState.amount;
}

static QRect TopScreenContentClipPx(const QTransform& xf, float topStretchX)
{
    const double sx = std::max(0.001, static_cast<double>(topStretchX));
    const QRectF dsRect(-(sx - 1.0) * 128.0, 0.0, 256.0 * sx, 192.0);
    return xf.mapRect(dsRect).toAlignedRect();
}

static double CrosshairTransitionMidIntensity(double zoomT)
{
    zoomT = std::clamp(zoomT, 0.0, 1.0);
    return 4.0 * zoomT * (1.0 - zoomT);
}

static uint32_t AdvanceCrosshairFxPhase(uint32_t phase) noexcept
{
    return phase * 1664525u + 1013904223u;
}

struct CrosshairGlitchJitter {
    int baseOx = 0;
    int baseOy = 0;
    int scopeOx = 0;
    int scopeOy = 0;
};

static CrosshairGlitchJitter ComputeCrosshairGlitchJitter(int fxStyle, double zoomT,
                                                          uint32_t& phase)
{
    CrosshairGlitchJitter j{};
    if (fxStyle != 2 && fxStyle != 3) // Glitch (2) / Glitch2 (3)
        return j;

    const double fx = CrosshairTransitionMidIntensity(zoomT);
    if (fx < 0.03)
        return j;

    phase = AdvanceCrosshairFxPhase(phase);
    const int bucket = static_cast<int>((phase >> 4) & 7u);
    if (bucket == 0)
        return j;

    const int mag = std::max(1, static_cast<int>(std::lround(1.0 + fx * (fxStyle == 3 ? 6.0 : 5.0))));
    const int dir = ((phase >> 7) & 1u) ? 1 : -1;
    const int rgbMode = static_cast<int>((phase >> 11) & 3u);

    switch (rgbMode) {
    case 0:
        j.scopeOx = -dir * mag;
        j.baseOx = -dir * std::max(1, mag / 2);
        break;
    case 1:
        j.scopeOx = dir * mag;
        j.baseOx = dir * std::max(1, mag / 2);
        break;
    case 2:
        j.scopeOx = dir * mag;
        j.scopeOy = -dir * std::max(1, mag / 2);
        j.baseOx = j.scopeOx / 2;
        j.baseOy = j.scopeOy / 2;
        break;
    default:
        j.scopeOx = -dir * std::max(1, mag / 2);
        j.scopeOy = dir * std::max(1, mag / 3);
        j.baseOx = j.scopeOx;
        j.baseOy = j.scopeOy / 2;
        break;
    }
    return j;
}

static void DrawCrosshairGlowRing(QPainter* p, int cx, int cy, int radius,
                                  const QColor& color, int baseWidth, double alpha)
{
    if (radius <= 0 || alpha <= 0.001)
        return;
    p->setBrush(Qt::NoBrush);
    for (int pass = 3; pass >= 0; --pass) {
        QPen pen(color);
        pen.setWidth(std::max(1, baseWidth + pass * 2));
        p->setPen(pen);
        p->setOpacity(alpha * (0.18 + pass * 0.12));
        p->drawEllipse(QRect(cx - radius, cy - radius, radius * 2, radius * 2));
    }
}

#include "MelonPrimeHudRenderCrosshairFx.inc"

static void DrawZoomTransitionFx(QPainter* p, int pxCx, int pxCy,
                                 const CrosshairHudConfig& ch,
                                 double zoomT, double pulseT, int fxStyle,
                                 const QRect& topScreenClipPx)
{
    const double midFx = CrosshairTransitionMidIntensity(zoomT);
    const int maxR = ch.chZoomScopeRadius;

    if (midFx >= 0.015 && ch.cachedZoomScopeRenderable) {
        p->save();
        if (!topScreenClipPx.isEmpty())
            p->setClipRect(topScreenClipPx, Qt::IntersectClip);

        switch (fxStyle) {
    case 1: // Staged — opacity-only; pulse ring (if enabled) handled below
        break;
    case 2: { // Glitch — AniGlitchArtFX-style RGB break / slices / scan bars
        static uint32_t s_chGlitchFxPhase = 0xC0FFEE01u;
        DrawFxGlitchArtOverlay(p, pxCx, pxCy, maxR, std::clamp(zoomT, 0.0, 1.0), midFx,
                               ch.chZoomScopeThickness, s_chGlitchFxPhase, 0);
        break;
    }
    case 3: { // Glitch2 — scan-disturb overload + tactical brackets
        p->setRenderHint(QPainter::Antialiasing, false);
        const double lockT = std::clamp(zoomT, 0.0, 1.0);
        static uint32_t s_chCyberPhase = 0xC088E001u;
        DrawFxGlitchArtOverlay(p, pxCx, pxCy, maxR, lockT, midFx,
                               ch.chZoomScopeThickness, s_chCyberPhase, 1);
        DrawFxScanDisturb(p, pxCx, pxCy, maxR, lockT, midFx * 0.9, ch.chColor, s_chCyberPhase);
        DrawFxCornerBrackets(p, pxCx + 3, pxCy - 1, maxR, lockT, midFx * 0.65, ch.chColor, 1);
        break;
    }
    case 4: { // Snap
        p->setRenderHint(QPainter::Antialiasing, true);
        const float qt = ZoomStatus::QuantizeSteps(static_cast<float>(zoomT), 5);
        const int ghostR = std::max(4, static_cast<int>(maxR * qt + 0.5));
        QPen pen(ch.chColor);
        pen.setWidth(std::max(1, ch.chZoomScopeThickness));
        pen.setStyle(Qt::DashLine);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->setOpacity(0.45 * midFx);
        p->drawEllipse(QRect(pxCx - ghostR, pxCy - ghostR, ghostR * 2, ghostR * 2));
        break;
    }
    case 5: { // Digital — pixel crush + block noise
        p->setRenderHint(QPainter::Antialiasing, false);
        static uint32_t s_chDigitalPhase = 0x12345678u;
        s_chDigitalPhase = AdvanceCrosshairFxPhase(s_chDigitalPhase);
        const int blocks = 10 + static_cast<int>((s_chDigitalPhase >> 6) & 11u);
        p->setPen(Qt::NoPen);
        for (int i = 0; i < blocks; ++i) {
            s_chDigitalPhase = AdvanceCrosshairFxPhase(s_chDigitalPhase);
            const int bx = pxCx + static_cast<int>(((s_chDigitalPhase >> 3) & 31u) - 16)
                         * std::max(1, maxR / 14);
            const int by = pxCy + static_cast<int>(((s_chDigitalPhase >> 9) & 31u) - 16)
                         * std::max(1, maxR / 14);
            const int bw = 2 + static_cast<int>((s_chDigitalPhase >> 14) & 9u);
            const int bh = 2 + static_cast<int>((s_chDigitalPhase >> 18) & 9u);
            const QColor pxColor = ((s_chDigitalPhase >> 22) & 1u)
                ? kGlitchMagenta
                : kGlitchCyan;
            p->setBrush(QColor(pxColor.red(), pxColor.green(), pxColor.blue(),
                               static_cast<int>(150 + 80 * midFx)));
            p->setOpacity(0.85 * midFx);
            p->drawRect(QRect(bx, by, bw, bh));
        }
        DrawFxGlitchPixelGrid(p, pxCx, pxCy, maxR, midFx * 0.65, s_chDigitalPhase);
        DrawFxGlitchNoise(p, pxCx, pxCy, maxR, midFx * 0.45, s_chDigitalPhase, 8);
        break;
    }
    case 6: { // Pulse Wave
        p->setRenderHint(QPainter::Antialiasing, true);
        QPen pen(ch.chColor);
        pen.setWidth(std::max(1, ch.chZoomScopeThickness));
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        for (int wave = 0; wave < 3; ++wave) {
            const double phase = zoomT * 2.0 + wave * 0.33;
            const float w = 0.5f + 0.5f * std::sin(static_cast<float>(phase * 6.2831853));
            const int r = std::max(4, static_cast<int>((maxR / 6)
                + (maxR - maxR / 6) * w * std::clamp(zoomT, 0.0, 1.0) + 0.5));
            p->setOpacity((0.12 + 0.28 * midFx) * (1.0 - wave * 0.22));
            p->drawEllipse(QRect(pxCx - r, pxCy - r, r * 2, r * 2));
        }
        break;
    }
    case 7: { // Magic Circle
        p->setRenderHint(QPainter::Antialiasing, true);
        const double rot = zoomT * 540.0;
        const int outerR = std::max(10, static_cast<int>(maxR * std::clamp(zoomT, 0.0, 1.0) + 0.5));
        const int innerR = std::max(6, outerR * 3 / 5);
        QPen pen(ch.chColor);
        pen.setWidth(std::max(1, ch.chZoomScopeThickness));
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);

        for (int ring = 0; ring < 2; ++ring) {
            const int r = (ring == 0) ? outerR : innerR;
            const double ringRot = rot * (ring == 0 ? 1.0 : -1.35);
            for (int i = 0; i < 6; ++i) {
                const int start = static_cast<int>((ringRot + i * 60.0 + 8.0) * 16.0);
                const int span = static_cast<int>(34.0 * 16.0);
                p->setOpacity((0.34 - ring * 0.08) * midFx);
                p->drawArc(QRect(pxCx - r, pxCy - r, r * 2, r * 2), start, span);
            }
        }

        p->save();
        p->translate(pxCx, pxCy);
        p->rotate(rot * 0.5);
        pen.setWidth(std::max(1, ch.chZoomScopeThickness - 1));
        p->setPen(pen);
        p->setOpacity(0.42 * midFx);
        const int sq = std::max(6, innerR * 2 / 3);
        p->drawRect(QRect(-sq, -sq, sq * 2, sq * 2));
        p->restore();

        pen.setWidth(1);
        p->setPen(pen);
        for (int i = 0; i < 8; ++i) {
            const double ang = (rot * 0.35 + i * 45.0) * 3.14159265 / 180.0;
            const int x0 = pxCx + static_cast<int>(std::cos(ang) * (innerR - 4));
            const int y0 = pxCy + static_cast<int>(std::sin(ang) * (innerR - 4));
            const int x1 = pxCx + static_cast<int>(std::cos(ang) * (innerR + 2));
            const int y1 = pxCy + static_cast<int>(std::sin(ang) * (innerR + 2));
            p->setOpacity(0.38 * midFx);
            p->drawLine(QPoint(x0, y0), QPoint(x1, y1));
        }
        break;
    }
    case 8: { // SF Movie
        p->setRenderHint(QPainter::Antialiasing, true);
        const double lockT = std::clamp(zoomT, 0.0, 1.0);
        const QColor glowColor(ch.chColor.red(), ch.chColor.green(), ch.chColor.blue(), 255);
        const QColor cyan(72, 220, 255);
        const QColor warm(255, 148, 96);
        DrawFxConvergingRings(p, pxCx, pxCy, maxR, lockT, midFx, glowColor, ch.chZoomScopeThickness);
        DrawFxRadarSweep(p, pxCx, pxCy, maxR, lockT, midFx, glowColor, ch.chZoomScopeThickness);
        DrawFxRangefinderTicks(p, pxCx, pxCy, maxR, lockT, midFx, glowColor);
        const int glowR = std::max(8, static_cast<int>(maxR * lockT + 0.5));
        DrawCrosshairGlowRing(p, pxCx - 1, pxCy, glowR, cyan, 1, 0.22 * midFx);
        DrawCrosshairGlowRing(p, pxCx + 1, pxCy, glowR, warm, 1, 0.18 * midFx);
        DrawFxCornerBrackets(p, pxCx, pxCy, maxR, lockT, midFx, glowColor, ch.chZoomScopeThickness);
        break;
    }
    case 9: { // Tactical Lock
        p->setRenderHint(QPainter::Antialiasing, true);
        const double lockT = std::clamp(zoomT, 0.0, 1.0);
        static uint32_t s_chTacticalPhase = 0xCA471CA1u;
        DrawFxTargetBoxAcquisition(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor,
                                   ch.chZoomScopeThickness, s_chTacticalPhase);
        DrawFxConvergingRings(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, ch.chZoomScopeThickness);
        DrawFxCornerBrackets(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, ch.chZoomScopeThickness, 0.04f);
        if (lockT > 0.82) {
            p->setPen(Qt::NoPen);
            p->setBrush(ch.chColor);
            p->setOpacity(0.55 * midFx);
            p->drawEllipse(QRect(pxCx - 2, pxCy - 2, 5, 5));
        }
        break;
    }
    case 10: { // Sniper Optics
        p->setRenderHint(QPainter::Antialiasing, true);
        const double lockT = std::clamp(zoomT, 0.0, 1.0);
        DrawFxIrisAperture(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, ch.chZoomScopeThickness);
        DrawFxCornerBrackets(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, ch.chZoomScopeThickness);
        DrawFxRangefinderTicks(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor);
        QPen pen(ch.chColor);
        pen.setWidth(1);
        p->setPen(pen);
        const int ladderX = pxCx + std::max(14, maxR / 3);
        const int ladderH = std::max(12, static_cast<int>(maxR * 0.5 * lockT));
        for (int i = -3; i <= 3; ++i) {
            p->setOpacity(0.22 * midFx);
            p->drawLine(QPoint(ladderX, pxCy + i * ladderH / 3),
                        QPoint(ladderX + (i == 0 ? 8 : 4), pxCy + i * ladderH / 3));
        }
        break;
    }
    case 11: { // Drone LIDAR
        p->setRenderHint(QPainter::Antialiasing, false);
        const double lockT = std::clamp(zoomT, 0.0, 1.0);
        static uint32_t s_chLidarPhase = 0x11DA0001u;
        DrawFxLidarCloud(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, s_chLidarPhase);
        DrawFxWireframeBox(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, ch.chZoomScopeThickness);
        DrawFxMaterialGrid(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor);
        break;
    }
    case 12: { // Beam Charge
        p->setRenderHint(QPainter::Antialiasing, true);
        const double lockT = std::clamp(zoomT, 0.0, 1.0);
        DrawFxChargeRingFill(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, ch.chZoomScopeThickness);
        DrawFxEnergySurge(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, ch.chZoomScopeThickness);
        DrawFxRailgunLines(p, pxCx, pxCy, maxR, lockT, midFx, ch.chColor, ch.chZoomScopeThickness);
        break;
    }
        default:
            break;
        }

        p->restore();
    }

    const bool drawPulseRing = ch.chZoomTransitionPulseEnable
        && pulseT > 0.01
        && (fxStyle == 1 || fxStyle == 6 || fxStyle == 8 || fxStyle == 9
            || fxStyle == 12);
    if (!drawPulseRing)
        return;

    const int r0 = std::max(4, maxR / 8);
    const int ringR = r0 + static_cast<int>((maxR - r0) * std::clamp(zoomT, 0.0, 1.0) + 0.5);
    const double outerAlpha = pulseT * 0.38;
    const double innerAlpha = pulseT * 0.16;

    p->save();
    if (!topScreenClipPx.isEmpty())
        p->setClipRect(topScreenClipPx, Qt::IntersectClip);

    p->setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ch.chColor);
    pen.setWidth(std::max(1, ch.chZoomScopeThickness));
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->setOpacity(outerAlpha);
    p->drawEllipse(QRect(pxCx - ringR, pxCy - ringR, ringR * 2, ringR * 2));

    if (pulseT > 0.35 * ch.chZoomTransitionPulseStrength) {
        const int innerR = std::max(2, ringR / 2);
        p->setOpacity(innerAlpha);
        p->drawEllipse(QRect(pxCx - innerR, pxCy - innerR, innerR * 2, innerR * 2));
    }

    p->restore();
}

static void RebuildZoomScopeStamp(const CrosshairHudConfig& ch)
{
    s_chScopeStampValid = false;
    if (ch.chZoomScopeRadius > 512)
        return;

    const QRect bbox = ch.cachedZoomScopeBboxPx;
    if (bbox.isEmpty())
        return;

    s_chScopeStampOrigin = bbox.topLeft() - QPoint(1, 1);
    const QSize imageSize = bbox.size() + QSize(2, 2);
    s_chScopeStamp = QImage(imageSize, QImage::Format_ARGB32_Premultiplied);
    s_chScopeStamp.fill(Qt::transparent);

    QPainter sp(&s_chScopeStamp);
    sp.setRenderHint(QPainter::Antialiasing, true);

    const int pxCx = -s_chScopeStampOrigin.x();
    const int pxCy = -s_chScopeStampOrigin.y();
    const int radius = ch.chZoomScopeRadius;
    const int gap = std::min(ch.cachedZoomScopeGapPx, std::max(0, radius - 1));
    const int armEnd = std::max(gap + 1, radius);
    const int thickness = ch.chZoomScopeThickness;
    QPainterPath circleClip;
    circleClip.addEllipse(QRect(pxCx - radius, pxCy - radius, radius * 2, radius * 2));

    auto drawArms = [&](const QColor& color, int width, double opacity) {
        if (armEnd <= gap)
            return;
        sp.save();
        sp.setClipPath(circleClip, Qt::IntersectClip);
        QPen pen(color);
        pen.setWidth(std::max(1, width));
        pen.setCapStyle(Qt::SquareCap);
        sp.setPen(pen);
        sp.setBrush(Qt::NoBrush);
        sp.setOpacity(opacity);
        sp.drawLine(QPoint(pxCx - armEnd, pxCy), QPoint(pxCx - gap, pxCy));
        sp.drawLine(QPoint(pxCx + gap, pxCy), QPoint(pxCx + armEnd, pxCy));
        sp.drawLine(QPoint(pxCx, pxCy - armEnd), QPoint(pxCx, pxCy - gap));
        sp.drawLine(QPoint(pxCx, pxCy + gap), QPoint(pxCx, pxCy + armEnd));
        sp.restore();
    };
    auto drawCirclePass = [&](const QColor& color, int width, double opacity) {
        QPen pen(color);
        pen.setWidth(std::max(1, width));
        pen.setCapStyle(Qt::SquareCap);
        sp.setPen(pen);
        sp.setBrush(Qt::NoBrush);
        sp.setOpacity(opacity);
        sp.drawEllipse(QRect(pxCx - radius, pxCy - radius, radius * 2, radius * 2));
    };
    auto drawDotPass = [&](const QColor& color, int size, double opacity) {
        if (!ch.chZoomScopeCenterDot || ch.chZoomScopeDotOpacity <= 0.0)
            return;
        const int dotSize = std::max(1, size);
        const int dotHalf = dotSize / 2;
        const QRect dotRect(pxCx - dotHalf, pxCy - dotHalf, dotSize, dotSize);
        sp.setPen(Qt::NoPen);
        sp.setBrush(color);
        sp.setOpacity(opacity);
        if (ch.chZoomScopeDotShape == 1)
            sp.drawEllipse(dotRect);
        else
            sp.drawRect(dotRect);
    };

    if (ch.chOutline && ch.chOutlineOpacity > 0.0) {
        static const QColor solidBlack(0, 0, 0, 255);
        drawArms(solidBlack,
                 thickness + std::max(1, ch.chOutlineThickness) * 2,
                 ch.chOutlineOpacity);
        drawCirclePass(solidBlack,
                       thickness + std::max(1, ch.chOutlineThickness) * 2,
                       ch.chOutlineOpacity);
        drawDotPass(solidBlack,
                    ch.chZoomScopeDotSize + std::max(1, ch.chOutlineThickness) * 2,
                    ch.chOutlineOpacity);
    }
    drawArms(ch.chColor, thickness, 1.0);
    drawCirclePass(ch.chColor, thickness, 1.0);
    drawDotPass(ch.chZoomScopeDotDrawColor, ch.chZoomScopeDotSize, 1.0);

    sp.end();
    s_chScopeStampValid = true;
}

static void DrawZoomScopeReticle(QPainter* p, int pxCx, int pxCy,
                                 const CrosshairHudConfig& ch, double scopeT,
                                 const QRect& topScreenClipPx,
                                 int glitchOx = 0, int glitchOy = 0)
{
    pxCx += glitchOx;
    pxCy += glitchOy;
    const double scopeGeom = std::clamp(scopeT, 0.0, 1.0);
    const double scopeOpacity = scopeGeom * ch.chZoomScopeOpacity;
    if (!ch.cachedZoomScopeRenderable || scopeOpacity <= 0.001)
        return;

    const int radius = std::max(1, static_cast<int>(ch.chZoomScopeRadius * scopeGeom + 0.5));
    const int gap = std::min(ch.cachedZoomScopeGapPx, std::max(0, radius - 1));
    const int armEnd = std::max(gap + 1, radius);
    const int thickness = ch.chZoomScopeThickness;

    // The stamp pre-composites overlapping reticle primitives. Use it only when
    // final opacity is effectively full; semi-transparent scope opacity falls
    // back to the procedural passes so source-over overlaps stay pixel-matched.
    if (scopeGeom >= 0.999 && radius == ch.chZoomScopeRadius && scopeOpacity >= 0.999999) {
        if (!s_chScopeStampValid)
            RebuildZoomScopeStamp(ch);
        if (s_chScopeStampValid && !s_chScopeStamp.isNull()) {
            p->save();
            if (!topScreenClipPx.isEmpty())
                p->setClipRect(topScreenClipPx, Qt::IntersectClip);
            p->setOpacity(scopeOpacity);
            p->drawImage(QPoint(pxCx + s_chScopeStampOrigin.x(),
                                pxCy + s_chScopeStampOrigin.y()), s_chScopeStamp);
            p->restore();
            return;
        }
        // Stamp unavailable (radius > 512); fall through to procedural path.
    }

    p->save();
    if (!topScreenClipPx.isEmpty())
        p->setClipRect(topScreenClipPx, Qt::IntersectClip);

    QPainterPath circleClip;
    circleClip.addEllipse(QRect(pxCx - radius, pxCy - radius, radius * 2, radius * 2));
    auto drawArms = [&](const QColor& color, int width, double opacity) {
        if (armEnd <= gap)
            return;
        p->save();
        p->setClipPath(circleClip, Qt::IntersectClip);
        QPen pen(color);
        pen.setWidth(std::max(1, width));
        pen.setCapStyle(Qt::SquareCap);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->setOpacity(opacity);
        p->drawLine(QPoint(pxCx - armEnd, pxCy), QPoint(pxCx - gap, pxCy));
        p->drawLine(QPoint(pxCx + gap, pxCy), QPoint(pxCx + armEnd, pxCy));
        p->drawLine(QPoint(pxCx, pxCy - armEnd), QPoint(pxCx, pxCy - gap));
        p->drawLine(QPoint(pxCx, pxCy + gap), QPoint(pxCx, pxCy + armEnd));
        p->restore();
    };
    auto drawCirclePass = [&](const QColor& color, int width, double opacity) {
        QPen pen(color);
        pen.setWidth(std::max(1, width));
        pen.setCapStyle(Qt::SquareCap);
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        p->setOpacity(opacity);
        p->drawEllipse(QRect(pxCx - radius, pxCy - radius, radius * 2, radius * 2));
    };
    auto drawDotPass = [&](const QColor& color, int size, double opacity) {
        if (!ch.chZoomScopeCenterDot || ch.chZoomScopeDotOpacity <= 0.0)
            return;
        const int dotSize = std::max(1, size);
        const int dotHalf = dotSize / 2;
        const QRect dotRect(pxCx - dotHalf, pxCy - dotHalf, dotSize, dotSize);
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->setOpacity(opacity);
        if (ch.chZoomScopeDotShape == 1)
            p->drawEllipse(dotRect);
        else
            p->drawRect(dotRect);
    };

    p->setRenderHint(QPainter::Antialiasing, true);
    if (ch.chOutline && ch.chOutlineOpacity > 0.0) {
        static const QColor solidBlack(0, 0, 0, 255);
        drawArms(solidBlack,
                 thickness + std::max(1, ch.chOutlineThickness) * 2,
                 ch.chOutlineOpacity * scopeOpacity);
        drawCirclePass(solidBlack,
                       thickness + std::max(1, ch.chOutlineThickness) * 2,
                       ch.chOutlineOpacity * scopeOpacity);
        drawDotPass(solidBlack,
                    ch.chZoomScopeDotSize + std::max(1, ch.chOutlineThickness) * 2,
                    ch.chOutlineOpacity * scopeOpacity);
    }
    drawArms(ch.chColor, thickness, scopeOpacity);
    drawCirclePass(ch.chColor, thickness, scopeOpacity);
    drawDotPass(ch.chZoomScopeDotDrawColor, ch.chZoomScopeDotSize, scopeOpacity);

    p->restore();
}

// P-8: Draws the crosshair entirely in integer pixel coordinates.
// Cached arm rects (from LoadCrosshairConfig) are in output pixels centred at
// (0,0).  The DS-space crosshair position (cx,cy) is mapped to pixel space via
// the painter's current transform, **rounded to integer**, then added to each
// rect.  Because everything is integer, QPainter never has sub-pixel alignment
// differences, and the crosshair shape is perfectly stable when the aim moves.
static void DrawCrosshair(QPainter* p, melonDS::u8* ram,
                          const RomAddresses& rom,
                          const CachedHudConfig& c,
                          float hudScale, float topStretchX,
                          uint32_t gameFrame,
                          float zoomAmount = 0.0f)
{
    (void)hudScale;
    if (s_chAimFrame != gameFrame) {
        s_chAimFrame = gameFrame;
        s_chAimDsCx = static_cast<float>(Read8(ram, rom.crosshairPosX));
        s_chAimDsCy = static_cast<float>(Read8(ram, rom.crosshairPosY));
    }
    const float dsCx = s_chAimDsCx;
    const float dsCy = s_chAimDsCy;
    double zoomT = 0.0;
    double geomScale = 1.0;
    double opacityMul = 1.0;
    double scopeT = 0.0;
    double pulseT = 0.0;
    int transitionFxStyle = 0;
    if (c.crosshair.cachedZoomAffectsVisual) {
        const float targetZoom =
            std::clamp(static_cast<float>(zoomAmount), 0.0f, 1.0f);
        if (s_chDisplayZoomFrame != gameFrame) {
            s_chDisplayZoomFrame = gameFrame;
            if (targetZoom <= 0.001f) {
                // Fully unzoomed — snap display so zoom scale never sticks on the base reticle.
                s_chDisplayZoom = 0.0f;
            } else if (!s_chCrosshairDrawnLastFrame) {
                s_chDisplayZoom = targetZoom;
            } else if (c.crosshair.chZoomTransitionEnable) {
                s_chDisplayZoom = ZoomStatus::AdvanceDisplayZoom(
                    s_chDisplayZoom, targetZoom, c.crosshair.chZoomTransitionStep);
            } else {
                s_chDisplayZoom = targetZoom;
            }
        }

        const float t = s_chDisplayZoom;
        // Fast path: fully unzoomed (the common in-game state when the zoom stage
        // is enabled but the player is not scoped). The neutral defaults above
        // already describe an unmodified base crosshair, so skip the per-frame
        // blend math, trig, and FX/scope setup entirely.
        if (t > 0.001f) {
            if (c.crosshair.chZoomTransitionEnable) {
                const ZoomStatus::CrosshairZoomBlend blend =
                    ZoomStatus::ComputeCrosshairZoomBlend(
                        t,
                        static_cast<float>(c.crosshair.chZoomOpacity),
                        static_cast<float>(c.crosshair.chZoomTransitionPulseStrength),
                        c.crosshair.chZoomTransitionStyle);
                zoomT = t;
                geomScale = 1.0 + (c.crosshair.chZoomScale - 1.0) * blend.geom;
                opacityMul = blend.baseAlpha;
                scopeT = blend.scope;
                pulseT = c.crosshair.chZoomTransitionPulseEnable ? blend.pulse : 0.0;
                transitionFxStyle = blend.fxStyle;
            } else {
                zoomT = t;
                geomScale = 1.0 + (c.crosshair.chZoomScale - 1.0) * t;
                opacityMul = std::clamp(
                    1.0 + (c.crosshair.chZoomOpacity - 1.0) * t, 0.0, 1.0);
                scopeT = t;
                pulseT = 0.0;
            }
        }
    }
    const bool scaledGeometry = std::abs(geomScale - 1.0) >= 0.001;

    // Map DS-space centre → output pixel centre (integer).
    const QTransform& xf = p->transform();
    // Only the scope reticle / transition FX need the top-screen content clip,
    // and only while the zoom visual is active. Skip the rect-map when unzoomed.
    const QRect scopeClipPx = (zoomT > 0.001 || scopeT > 0.001)
        ? TopScreenContentClipPx(xf, topStretchX)
        : QRect();
    const int pxCx = static_cast<int>(std::round(xf.m11() * dsCx + xf.m21() * dsCy + xf.dx()));
    const int pxCy = static_cast<int>(std::round(xf.m12() * dsCx + xf.m22() * dsCy + xf.dy()));
    int glitchOx = 0;
    int glitchOy = 0;
    int scopeGlitchOx = 0;
    int scopeGlitchOy = 0;
    static uint32_t s_chGlitchPhase = 0xA341316Cu;
    if (c.crosshair.chZoomTransitionEnable
        && (transitionFxStyle == 2 || transitionFxStyle == 3)) {
        const CrosshairGlitchJitter jitter =
            ComputeCrosshairGlitchJitter(transitionFxStyle, zoomT, s_chGlitchPhase);
        glitchOx = jitter.baseOx;
        glitchOy = jitter.baseOy;
        scopeGlitchOx = jitter.scopeOx;
        scopeGlitchOy = jitter.scopeOy;
    }
    const int drawCx = pxCx + glitchOx;
    const int drawCy = pxCy + glitchOy;

    const QRect* innerRects = c.crosshair.cachedInnerRects;
    const QRect* outerRects = c.crosshair.cachedOuterRects;
    const int nInner = c.crosshair.cachedInnerCount;
    const int nOuter = c.crosshair.cachedOuterCount;

    // OPT-DR1: Update crosshair dirty rect (prev ∪ curr position).
    // This is accumulated into s_chDirtyThisFrame so CustomHud_Render can return it.
    {
        const QRect currBbox = CrosshairBboxPx(pxCx, pxCy, c.crosshair, geomScale,
                                               zoomT, scopeT, pulseT, scopeClipPx);
        s_chDirtyThisFrame = currBbox;
        if (glitchOx != 0 || glitchOy != 0 || scopeGlitchOx != 0 || scopeGlitchOy != 0)
            s_chDirtyThisFrame |= CrosshairBboxPx(drawCx, drawCy, c.crosshair, geomScale,
                                                  zoomT, 0.0, 0.0, scopeClipPx);
        if (s_chPrevPxCx != INT_MIN)
            s_chDirtyThisFrame |= CrosshairBboxPx(s_chPrevPxCx, s_chPrevPxCy, c.crosshair,
                                                  s_chPrevGeomScale, s_chPrevZoomT,
                                                  s_chPrevScopeT, 0.0, scopeClipPx);
        s_chPrevPxCx = pxCx;
        s_chPrevPxCy = pxCy;
        s_chPrevGeomScale = geomScale;
        s_chPrevZoomT = zoomT;
        s_chPrevScopeT = scopeT;
    }

    // Save/restore only what we change (transform + opacity).
    const QTransform savedXform = p->transform();
    const qreal savedOpacity = p->opacity();
    p->resetTransform();   // draw in pixel coordinates from here on

    // P-17: Outline pass — composite the pre-rendered stamp at the current pixel centre.
    // Stamp is built once in RebuildCrosshairOutlineStamp (called from RefreshCachedConfig);
    // each frame is a single drawImage, no QPainter creation or per-rect fillRect.
    if (scaledGeometry) {
        auto fillScaled = [&](const QRect& base, const QColor& color) {
            p->fillRect(ScaleRectAroundOrigin(base, geomScale).translated(drawCx, drawCy), color);
        };
        if (c.crosshair.chOutline && c.crosshair.chOutlineOpacity > 0.0) {
            p->setOpacity(c.crosshair.chOutlineOpacity * opacityMul);
            static const QColor solidBlack(0, 0, 0, 255);
            const int olT = std::max(1, c.crosshair.chOutlineThickness);
            for (int i = 0; i < nInner; i++)
                p->fillRect(ScaleRectAroundOrigin(innerRects[i], geomScale).adjusted(-olT, -olT, olT, olT).translated(drawCx, drawCy), solidBlack);
            for (int i = 0; i < nOuter; i++)
                p->fillRect(ScaleRectAroundOrigin(outerRects[i], geomScale).adjusted(-olT, -olT, olT, olT).translated(drawCx, drawCy), solidBlack);
            if (c.crosshair.chCenterDot) {
                const int dh = c.crosshair.cachedDotHalfPx;
                const int ds = c.crosshair.cachedDotSizePx;
                DrawCenterDotShape(p, ScaleRectAroundOrigin(QRect(-dh, -dh, ds, ds), geomScale).adjusted(-olT, -olT, olT, olT).translated(drawCx, drawCy), solidBlack, c.crosshair.chDotShape == 1);
            }
        }
        p->setOpacity(opacityMul);
        for (int i = 0; i < nInner; i++)
            fillScaled(innerRects[i], c.crosshair.chInnerColor);
        for (int i = 0; i < nOuter; i++)
            fillScaled(outerRects[i], c.crosshair.chOuterColor);
        if (c.crosshair.chCenterDot) {
            const int dh = c.crosshair.cachedDotHalfPx;
            const int ds = c.crosshair.cachedDotSizePx;
            DrawCenterDotShape(p, ScaleRectAroundOrigin(QRect(-dh, -dh, ds, ds), geomScale).translated(drawCx, drawCy), c.crosshair.chDotColor, c.crosshair.chDotShape == 1);
        }
        DrawZoomTransitionFx(p, pxCx, pxCy, c.crosshair, zoomT, pulseT, transitionFxStyle, scopeClipPx);
        DrawZoomScopeReticle(p, pxCx, pxCy, c.crosshair, scopeT, scopeClipPx,
                             scopeGlitchOx, scopeGlitchOy);
        p->setTransform(savedXform);
        p->setOpacity(savedOpacity);
        s_chCrosshairDrawnLastFrame = true;
        return;
    }

    if (c.crosshair.chOutline && c.crosshair.chOutlineOpacity > 0.0 &&
        s_chStampValid && !s_chOutlineStamp.isNull()) {
        p->setOpacity(c.crosshair.chOutlineOpacity * opacityMul);
        p->drawImage(QPoint(drawCx + s_chStampOrigin.x(), drawCy + s_chStampOrigin.y()), s_chOutlineStamp);
    }

    // Inner arms — chInnerColor already has alpha baked in (P-11)
    if (nInner > 0) {
        p->setOpacity(opacityMul);
        for (int i = 0; i < nInner; i++)
            p->fillRect(innerRects[i].translated(drawCx, drawCy), c.crosshair.chInnerColor);
    }

    // Outer arms
    if (nOuter > 0) {
        p->setOpacity(opacityMul);
        for (int i = 0; i < nOuter; i++)
            p->fillRect(outerRects[i].translated(drawCx, drawCy), c.crosshair.chOuterColor);
    }

    // Center dot
    if (c.crosshair.chCenterDot) {
        p->setOpacity(opacityMul);
        const int dh = c.crosshair.cachedDotHalfPx;
        const int ds = c.crosshair.cachedDotSizePx;
        DrawCenterDotShape(p, QRect(drawCx - dh, drawCy - dh, ds, ds), c.crosshair.chDotColor, c.crosshair.chDotShape == 1);
    }

    DrawZoomTransitionFx(p, pxCx, pxCy, c.crosshair, zoomT, pulseT, transitionFxStyle, scopeClipPx);
    DrawZoomScopeReticle(p, pxCx, pxCy, c.crosshair, scopeT, scopeClipPx,
                         scopeGlitchOx, scopeGlitchOy);

    // Restore painter state.
    p->setTransform(savedXform);
    p->setOpacity(savedOpacity);
    s_chCrosshairDrawnLastFrame = true;
}
