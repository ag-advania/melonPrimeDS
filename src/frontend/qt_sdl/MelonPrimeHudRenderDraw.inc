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

// P-10: Static const threshold colors — avoid per-call QColor construction.
static inline const QColor& HpGaugeColor(uint16_t hp, const QColor& safeColor)
{
    static const QColor kCritical(255, 0, 0);
    static const QColor kWarning(255, 165, 0);
    if (hp <= 25)      return kCritical;
    else if (hp <= 50) return kWarning;
    else               return safeColor;
}

static float CalcAlignedTextX(int anchorX, int align, float textW)
{
    switch (align) {
    case 1: return anchorX - textW * 0.5f;
    case 2: return anchorX - textW;
    default: return static_cast<float>(anchorX);
    }
}

static void CalcGaugePos(float textX, float textY, float textW, float textH, int anchor,
                         int ofsX, int ofsY, float gaugeLen, float gaugeWid, int ori,
                         int& outX, int& outY)
{
    const float gL = gaugeLen, gW = gaugeWid;
    switch (anchor) {
    case 0: outX = static_cast<int>(textX) + ofsX;                       outY = static_cast<int>(textY) + 2 + ofsY; break;
    case 1: outX = static_cast<int>(textX) + ofsX;                       outY = static_cast<int>(std::round(textY - textH - (ori==0?gW:gL))) + ofsY; break;
    case 2: outX = static_cast<int>(std::round(textX + textW)) + ofsX;   outY = static_cast<int>(std::round(textY - textH*0.5f - (ori==0?gW:gL)*0.5f)) + ofsY; break;
    case 3: outX = static_cast<int>(std::round(textX - (ori==0?gL:gW))) + ofsX; outY = static_cast<int>(std::round(textY - textH*0.5f - (ori==0?gW:gL)*0.5f)) + ofsY; break;
    case 4: outX = static_cast<int>(std::round(textX + textW*0.5f - (ori==0?gL:gW)*0.5f)) + ofsX; outY = static_cast<int>(std::round(textY - textH*0.5f - (ori==0?gW:gL)*0.5f)) + ofsY; break;
    default: outX = static_cast<int>(textX) + ofsX;                      outY = static_cast<int>(textY) + 2 + ofsY; break;
    }
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
    const float gW = static_cast<float>((ori == 0) ? gaugeLen : gaugeWid);
    const float gH = static_cast<float>((ori == 0) ? gaugeWid : gaugeLen);
    switch (anchor) {
    case 1: // Above — text bottom at gauge top
        outTextX = gx + gW * 0.5f - textW * 0.5f + ofsX;
        outTextY = gy + ofsY;
        break;
    case 2: // Right — text vertically centered on gauge
        outTextX = gx + gW + ofsX;
        outTextY = gy + gH * 0.5f + textH * 0.5f + ofsY;
        break;
    case 3: // Left — text vertically centered on gauge
        outTextX = gx - textW + ofsX;
        outTextY = gy + gH * 0.5f + textH * 0.5f + ofsY;
        break;
    case 4: // Center — text centered on gauge
        outTextX = gx + gW * 0.5f - textW * 0.5f + ofsX;
        outTextY = gy + gH * 0.5f + textH * 0.5f + ofsY;
        break;
    default: // 0 = Below — text top at gauge bottom + 2px gap
        outTextX = gx + gW * 0.5f - textW * 0.5f + ofsX;
        outTextY = gy + gH + textH + 2 + ofsY;
        break;
    }
}

// =========================================================================
//  P-5: DrawHP — stack-buffer text, reads CachedHudConfig directly
// =========================================================================
static inline void DrawHP(QPainter* p, uint16_t hp, uint16_t maxHP,
                           const CachedHudConfig& c, float tds)
{
    const QColor hpTextColor = c.hp.hpTextAutoColor ? HpGaugeColor(hp, c.hp.hpTextColor) : c.hp.hpTextColor;

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
        QColor gc = c.hp.hpAutoColor ? HpGaugeColor(hp, c.hp.hpGaugeColor) : c.hp.hpGaugeColor;
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
        if (buf[0] && fontPx == kCustomHudFontSize)
            PrepareTextBitmapCached(fm, s_frameFont, fontPx, s_invTextCache[si], buf, wi.textColor);
        else
            s_invTextCache[si].valid = false;

        // Text dimensions in DS-space via bitmap * tds
        const TextBitmapCache& tc = s_invTextCache[si];
        const float textW = (tc.valid && !tc.bitmap.isNull())
                            ? static_cast<float>(tc.bitmap.width())  * tds : 0.0f;

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
                p->drawImage(QRectF(px, py, drawIW, drawIH), icon);
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
        PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_ammoBitmapCache, buf, c.weapon.ammoTextColor);
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
            if (c.wpnIconOpacity < 1.0f) p->setOpacity(c.wpnIconOpacity);
            p->drawImage(QRectF(ix, iy, dw, dh), icon);
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
        DrawGauge(p, gx, gy, ratio, c.weapon.ammoGaugeColor, c.weapon.ammoGaugeOri, (int)(c.weapon.ammoGaugeLen * c.scaleGauges), (int)(c.weapon.ammoGaugeWid * c.scaleGauges),
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
        sp.fillRect(QRect(ox - dh, oy - dh, dh * 2, dh * 2), solidBlack);
    }
    sp.end();
    s_chStampValid = true;
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
                          float hudScale, float topStretchX)
{
    const float dsCx = static_cast<float>(Read8(ram, rom.crosshairPosX));
    const float dsCy = static_cast<float>(Read8(ram, rom.crosshairPosY));

    // Map DS-space centre → output pixel centre (integer).
    const QTransform& xf = p->transform();
    const int pxCx = static_cast<int>(std::round(xf.m11() * dsCx + xf.m21() * dsCy + xf.dx()));
    const int pxCy = static_cast<int>(std::round(xf.m12() * dsCx + xf.m22() * dsCy + xf.dy()));

    const QRect* innerRects = c.crosshair.cachedInnerRects;
    const QRect* outerRects = c.crosshair.cachedOuterRects;
    const int nInner = c.crosshair.cachedInnerCount;
    const int nOuter = c.crosshair.cachedOuterCount;

    // OPT-DR1: Update crosshair dirty rect (prev ∪ curr position).
    // This is accumulated into s_chDirtyThisFrame so CustomHud_Render can return it.
    {
        const QRect currBbox = CrosshairBboxPx(pxCx, pxCy, c.crosshair);
        s_chDirtyThisFrame = currBbox;
        if (s_chPrevPxCx != INT_MIN)
            s_chDirtyThisFrame |= CrosshairBboxPx(s_chPrevPxCx, s_chPrevPxCy, c.crosshair);
        s_chPrevPxCx = pxCx;
        s_chPrevPxCy = pxCy;
    }

    // Save/restore only what we change (transform + opacity).
    const QTransform savedXform = p->transform();
    const qreal savedOpacity = p->opacity();
    p->resetTransform();   // draw in pixel coordinates from here on

    // P-17: Outline pass — composite the pre-rendered stamp at the current pixel centre.
    // Stamp is built once in RebuildCrosshairOutlineStamp (called from RefreshCachedConfig);
    // each frame is a single drawImage, no QPainter creation or per-rect fillRect.
    if (c.crosshair.chOutline && c.crosshair.chOutlineOpacity > 0.0 &&
        s_chStampValid && !s_chOutlineStamp.isNull()) {
        p->setOpacity(c.crosshair.chOutlineOpacity);
        p->drawImage(QPoint(pxCx + s_chStampOrigin.x(), pxCy + s_chStampOrigin.y()), s_chOutlineStamp);
    }

    // Inner arms — chInnerColor already has alpha baked in (P-11)
    if (nInner > 0) {
        p->setOpacity(1.0);
        for (int i = 0; i < nInner; i++)
            p->fillRect(innerRects[i].translated(pxCx, pxCy), c.crosshair.chInnerColor);
    }

    // Outer arms
    if (nOuter > 0) {
        p->setOpacity(1.0);
        for (int i = 0; i < nOuter; i++)
            p->fillRect(outerRects[i].translated(pxCx, pxCy), c.crosshair.chOuterColor);
    }

    // Center dot
    if (c.crosshair.chCenterDot) {
        p->setOpacity(1.0);
        const int dh = c.crosshair.cachedDotHalfPx;
        const int ds = c.crosshair.cachedDotSizePx;
        p->fillRect(QRect(pxCx - dh, pxCy - dh, ds, ds), c.crosshair.chDotColor);
    }

    // Restore painter state.
    p->setTransform(savedXform);
    p->setOpacity(savedOpacity);
}

