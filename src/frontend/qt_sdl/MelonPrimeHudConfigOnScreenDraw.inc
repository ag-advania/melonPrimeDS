// This file is a unity-build fragment included by MelonPrimeHudConfigOnScreen.cpp.
// Do not add it to CMakeLists.txt.

// ── Bounding rect computation ───────────────────────────────────────────────
static QRectF ComputeEditBounds(int idx, Config::Table& cfg, float topStretchX)
{
    // Crosshair: fixed preview box at DS screen center
    if (idx == 13) {
        return QRectF(108.0f, 76.0f, 40.0f, 40.0f); // centered at DS (128, 96)
    }

    const HudEditElemDesc& d = kEditElems[idx];
    const int anchor = cfg.GetInt(d.anchorKey);
    const int ofsX   = cfg.GetInt(d.ofsXKey);
    const int ofsY   = cfg.GetInt(d.ofsYKey);
    int fx, fy;
    ApplyAnchor(anchor, ofsX, ofsY, fx, fy, topStretchX);

    const float hs  = (s_editHudScale > 0.0f) ? s_editHudScale : 1.0f;
    // Use exact textDrawScale from cache (includes auto-scale + user TextScale, divided by hudScale).
    // Falls back to bare formula only before first cache fill.
    const float tds = s_cache.valid
        ? s_cache.textDrawScale
        : std::max(0.3f, cfg.GetInt("Metroid.Visual.HudTextScale") / 100.0f) / hs;

    // ── Radar (idx=11) — must come before gauge branch (radar reuses lengthKey for square resize) ──
    if (idx == 11) {
        if (s_cache.valid)
            return QRectF(s_cache.radar.radarDstRect).united(s_cache.radar.frameDstRect);

        const int dstSize = std::max(cfg.GetInt("Metroid.Visual.BtmOverlayDstSize"), 1);
        const int srcRadius = std::max(cfg.GetInt("Metroid.Visual.BtmOverlaySrcRadius"), 1);
        const int srcDiameter = srcRadius * 2;
        const float frameSizeDS = static_cast<float>(kRadarArtSize) * dstSize
                                  / static_cast<float>(srcDiameter);
        const QRectF radarRect(fx, fy, dstSize, dstSize);
        const float cropCenterX = fx + dstSize * 0.5f + 0.75f;
        const float cropCenterY = fy + dstSize * 0.5f;
        const QRectF frameRect(cropCenterX - frameSizeDS * 0.5f,
                               cropCenterY - frameSizeDS * 0.5f,
                               frameSizeDS, frameSizeDS);
        return radarRect.united(frameRect);
    }

    // ── Weapon Inventory (idx=12) ───────────────────────────────────────────
    if (idx == 12) {
        const float hs = (s_editHudScale > 0.0f) ? s_editHudScale : 1.0f;
        const float iconScale = s_cache.valid ? s_cache.scaleIcons : 1.0f;
        const int iconH = std::max(4, cfg.GetInt("Metroid.Visual.HudWeaponInventoryIconHeight"));
        const float iH = static_cast<float>(iconH) * iconScale / hs;
        const float spacing = static_cast<float>(std::max(0, cfg.GetInt("Metroid.Visual.HudWeaponInventorySpacing")));
        const float rowH = iH + spacing;
        const int ori = cfg.GetInt("Metroid.Visual.HudWeaponInventoryOrientation");
        // Assume worst-case: all 9 weapons visible
        if (ori == 1) // Vertical
            return QRectF(static_cast<float>(fx), static_cast<float>(fy),
                          iH * 2.0f, rowH * 9.0f);
        else // Horizontal
            return QRectF(static_cast<float>(fx), static_cast<float>(fy),
                          (iH * 2.0f + spacing) * 9.0f, rowH);
    }

    // ── HP Gauge (idx=1) / Ammo Gauge (idx=4) ──────────────────────────────
    if (d.lengthKey != nullptr) {
        const float gaugeScale = s_cache.valid ? s_cache.scaleGauges : 1.0f;
        const float len = std::max(4.0f, cfg.GetInt(d.lengthKey) * gaugeScale / hs);
        const float wid = std::max(1.0f, cfg.GetInt(d.widthKey) * gaugeScale / hs);
        const int ori     = (d.orientKey != nullptr) ? cfg.GetInt(d.orientKey) : 0;
        const int posMode = d.posModeKey ? cfg.GetInt(d.posModeKey) : 1;
        const int align   = cfg.GetInt((idx == 1) ? "Metroid.Visual.HudHpGaugeAlign"
                                                   : "Metroid.Visual.HudAmmoGaugeAlign");

        if (posMode == 0) {
            // Relative to parent text element
            const int parentIdx = (idx == 1) ? 0 : 2; // HP→0, AmmoGauge→Weapon(2)
            const HudEditElemDesc& td = kEditElems[parentIdx];
            int tx, ty;
            ApplyAnchor(cfg.GetInt(td.anchorKey), cfg.GetInt(td.ofsXKey), cfg.GetInt(td.ofsYKey),
                        tx, ty, topStretchX);
            const int approxW = static_cast<int>(30.0f * tds);
            const int approxH = static_cast<int>(12.0f * tds);
            const char* gaugeAnchorKey = (idx == 1) ? "Metroid.Visual.HudHpGaugeAnchor"
                                                     : "Metroid.Visual.HudAmmoGaugeAnchor";
            const char* gaugeOfsXKey   = (idx == 1) ? "Metroid.Visual.HudHpGaugeOffsetX"
                                                     : "Metroid.Visual.HudAmmoGaugeOffsetX";
            const char* gaugeOfsYKey   = (idx == 1) ? "Metroid.Visual.HudHpGaugeOffsetY"
                                                     : "Metroid.Visual.HudAmmoGaugeOffsetY";
            int gx, gy;
            CalcGaugePos(tx, ty, approxW, approxH,
                         cfg.GetInt(gaugeAnchorKey), cfg.GetInt(gaugeOfsXKey), cfg.GetInt(gaugeOfsYKey),
                         len, wid, ori, gx, gy);
            if (ori == 0) gx -= static_cast<int>(len * align / 2);
            else          gy -= static_cast<int>(len * align / 2);
            return (ori == 1) ? QRectF(gx, gy, wid, len) : QRectF(gx, gy, len, wid);
        }
        // Independent: fx/fy come from PosAnchor + PosX/Y
        float bx = static_cast<float>(fx), by = static_cast<float>(fy);
        if (ori == 0) bx -= len * align / 2;
        else          by -= len * align / 2;
        return (ori == 1) ? QRectF(bx, by, wid, len) : QRectF(bx, by, len, wid);
    }

    // ── Weapon icon (idx=3) ─────────────────────────────────────────────────
    if (idx == 3) {
        const int iconH = std::max(4, cfg.GetInt("Metroid.Visual.HudWeaponIconHeight"));
        const float iconScale = s_cache.valid ? s_cache.scaleIcons : 1.0f;
        const float dh  = static_cast<float>(iconH) * iconScale / hs;
        // Use cached icon aspect ratio (don't reload); fall back to square
        const QImage& icon = s_weaponIcons[0];
        const float dw = (!icon.isNull() && icon.height() > 0)
                         ? dh * static_cast<float>(icon.width()) / static_cast<float>(icon.height())
                         : dh;
        float ix, iy;
        if (cfg.GetInt("Metroid.Visual.HudWeaponIconMode") == 0) {
            // Relative to Weapon/Ammo text (idx=2)
            const HudEditElemDesc& td = kEditElems[2];
            int tx, ty;
            ApplyAnchor(cfg.GetInt(td.anchorKey), cfg.GetInt(td.ofsXKey), cfg.GetInt(td.ofsYKey),
                        tx, ty, topStretchX);
            ix = static_cast<float>(tx) + cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetX");
            iy = static_cast<float>(ty) + cfg.GetInt("Metroid.Visual.HudWeaponIconOffsetY");
        } else {
            ix = static_cast<float>(fx);
            iy = static_cast<float>(fy);
        }
        const int ancX = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorX");
        const int ancY = cfg.GetInt("Metroid.Visual.HudWeaponIconAnchorY");
        if (ancX == 1) ix -= dw * 0.5f; else if (ancX == 2) ix -= dw;
        if (ancY == 1) iy -= dh * 0.5f; else if (ancY == 2) iy -= dh;
        return QRectF(ix, iy, dw, dh);
    }

    // ── Bomb icon (idx=10) ──────────────────────────────────────────────────
    if (idx == 10) {
        const int iconH = std::max(4, cfg.GetInt("Metroid.Visual.HudBombIconHeight"));
        const float iconScale = s_cache.valid ? s_cache.scaleIcons : 1.0f;
        const float dh  = static_cast<float>(iconH) * iconScale / hs;
        const QImage& icon = s_bombIcons[3];
        const float dw = (!icon.isNull() && icon.height() > 0)
                         ? dh * static_cast<float>(icon.width()) / static_cast<float>(icon.height())
                         : dh;
        float ix, iy;
        if (cfg.GetInt("Metroid.Visual.HudBombLeftIconMode") == 0) {
            // Relative to BombLeft text (idx=9)
            const HudEditElemDesc& td = kEditElems[9];
            int tx, ty;
            ApplyAnchor(cfg.GetInt(td.anchorKey), cfg.GetInt(td.ofsXKey), cfg.GetInt(td.ofsYKey),
                        tx, ty, topStretchX);
            ix = static_cast<float>(tx) + cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsX");
            iy = static_cast<float>(ty) + cfg.GetInt("Metroid.Visual.HudBombLeftIconOfsY");
        } else {
            ix = static_cast<float>(fx);
            iy = static_cast<float>(fy);
        }
        const int ancX = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorX");
        const int ancY = cfg.GetInt("Metroid.Visual.HudBombLeftIconAnchorY");
        if (ancX == 1) ix -= dw * 0.5f; else if (ancX == 2) ix -= dw;
        if (ancY == 1) iy -= dh * 0.5f; else if (ancY == 2) iy -= dh;
        return QRectF(ix, iy, dw, dh);
    }

    // ── Text elements ───────────────────────────────────────────────────────
    // Use actual font metrics (s_frameFm = mph.ttf 6px) scaled by tds for accuracy.
    // fy is the text baseline — rendered glyphs sit above it (by ascent) with a small
    // descender margin below. Position the box to match the actual drawn region.
    float bw, bh, boxTop;
    const char* sampleTxt = (idx >= 0 && idx < kEditElemCount) ? kEditElemSampleText[idx] : nullptr;
    if (sampleTxt && s_frameFpx == kCustomHudFontSize) {
        bw = std::max(14.0f, s_frameFm.horizontalAdvance(QString::fromUtf8(sampleTxt)) * tds + 4.0f);
        bh = std::max(5.0f,  s_frameFm.height() * tds + 2.0f);
        // Align box so text ascent sits just inside the top edge.
        boxTop = static_cast<float>(fy) - static_cast<float>(s_frameFm.ascent()) * tds - 1.0f;
    } else {
        bw = std::max(14.0f, 50.0f * tds);
        bh = std::max(5.0f,  12.0f * tds);
        // Fallback: treat fy as the bottom of the text area.
        boxTop = static_cast<float>(fy) - bh + 2.0f;
    }

    // Determine horizontal alignment to position the box correctly relative to the anchor.
    // Each text element uses CalcAlignedTextX: 0=left (text starts at fx),
    // 1=center (text centered on fx), 2=right (text ends at fx).
    // Match Status (idx=5) is always left-anchored (no alignment setting).
    static const struct { int idx; const char* key; } kAlignMap[] = {
        {0, "Metroid.Visual.HudHpAlign"},
        {2, "Metroid.Visual.HudAmmoAlign"},
        {6, "Metroid.Visual.HudRankAlign"},
        {7, "Metroid.Visual.HudTimeLeftAlign"},
        {8, "Metroid.Visual.HudTimeLimitAlign"},
        {9, "Metroid.Visual.HudBombLeftAlign"},
    };
    int textAlign = 1; // default: center
    if (idx == 5) { textAlign = 0; } // Match Status: always left
    else {
        for (auto& ka : kAlignMap)
            if (ka.idx == idx) { textAlign = cfg.GetInt(ka.key); break; }
    }
    float boxLeft;
    if      (textAlign == 0) boxLeft = static_cast<float>(fx);               // left
    else if (textAlign == 2) boxLeft = static_cast<float>(fx) - bw;          // right
    else                     boxLeft = static_cast<float>(fx) - bw * 0.5f;   // center
    return QRectF(boxLeft, boxTop, bw, bh);
}

static int CountBuiltinRows(const HudEditElemDesc& d) {
    int n = d.anchorKey ? 1 : 0; // anchor row only if key exists
    if (d.showKey)   ++n;
    if (d.colorRKey) ++n;
    return n;
}

static QRectF ComputePropsPanelRect(const QRectF& elemRect, int totalRowCount)
{
    int visCount = std::min(totalRowCount, kPropMaxVisible);
    float h = visCount * kPropRowH + 4.0f + (s_anchorPickerOpen ? kAnchorGridH : 0.0f);
    float px = static_cast<float>(elemRect.right()) + 4.0f;
    float py = static_cast<float>(elemRect.top());
    if (px + kPropPanelW > 256.0f) px = static_cast<float>(elemRect.left()) - kPropPanelW - 2.0f;
    if (py + h > 192.0f) py = 192.0f - h;
    if (px < 0.0f) px = 0.0f;
    if (py < 26.0f) py = 26.0f;
    return QRectF(px, py, kPropPanelW, h);
}

static QRectF GetOrientToggleRect(int idx)
{
    const QRectF& r = s_editRects[idx];
    QRectF rect(r.right() - 8.0f, r.top() - 11.0f, 10.0f, 10.0f);
    if (rect.top() < 0.0f) rect.moveTop(r.top());
    return rect;
}

static void GetResizeHandles(int idx, Config::Table& cfg,
                              QRectF& lenHandle, QRectF& widHandle)
{
    const QRectF& r = s_editRects[idx];
    const int ori = kEditElems[idx].orientKey ? cfg.GetInt(kEditElems[idx].orientKey) : 0;
    const bool squareResize = kEditElems[idx].lengthKey && kEditElems[idx].widthKey
                              && strcmp(kEditElems[idx].lengthKey, kEditElems[idx].widthKey) == 0;
    if (ori == 1) {
        lenHandle = QRectF(r.center().x() - 3.0f, r.bottom() - 3.0f, 6.0f, 6.0f);
        widHandle = squareResize ? QRectF() : QRectF(r.right() - 3.0f, r.center().y() - 3.0f, 6.0f, 6.0f);
    } else {
        lenHandle = QRectF(r.right() - 3.0f, r.center().y() - 3.0f, 6.0f, 6.0f);
        widHandle = squareResize ? QRectF() : QRectF(r.center().x() - 3.0f, r.bottom() - 3.0f, 6.0f, 6.0f);
    }
}

static void DrawEditCrosshairActual(QPainter* p, const QRectF& r, const CachedHudConfig& c)
{
    if (!p) return;

    const QRectF pxR = p->transform().mapRect(r).adjusted(1.0f, 1.0f, -1.0f, -1.0f);
    if (pxR.isEmpty()) return;

    const QRect* innerRects = c.crosshair.cachedInnerRects;
    const QRect* outerRects = c.crosshair.cachedOuterRects;
    const int nInner = c.crosshair.cachedInnerCount;
    const int nOuter = c.crosshair.cachedOuterCount;
    const int pxCx = static_cast<int>(std::round(pxR.center().x()));
    const int pxCy = static_cast<int>(std::round(pxR.center().y()));

    p->save();
    p->resetTransform();
    p->setClipRect(pxR);

    if (c.crosshair.chOutline && c.crosshair.chOutlineOpacity > 0.0) {
        static const QColor kOutlineBlack(0, 0, 0, 255);
        const int olT = c.crosshair.chOutlineThickness;
        p->setOpacity(c.crosshair.chOutlineOpacity);
        for (int i = 0; i < nInner; ++i)
            p->fillRect(innerRects[i].translated(pxCx, pxCy).adjusted(-olT, -olT, olT, olT), kOutlineBlack);
        for (int i = 0; i < nOuter; ++i)
            p->fillRect(outerRects[i].translated(pxCx, pxCy).adjusted(-olT, -olT, olT, olT), kOutlineBlack);
        if (c.crosshair.chCenterDot) {
            const int dh = c.crosshair.cachedDotHalfPx + olT;
            const int ds = c.crosshair.cachedDotSizePx + olT * 2;
            p->fillRect(QRect(pxCx - dh, pxCy - dh, ds, ds), kOutlineBlack);
        }
    }

    if (nInner > 0) {
        p->setOpacity(1.0);
        for (int i = 0; i < nInner; ++i)
            p->fillRect(innerRects[i].translated(pxCx, pxCy), c.crosshair.chInnerColor);
    }

    if (nOuter > 0) {
        p->setOpacity(1.0);
        for (int i = 0; i < nOuter; ++i)
            p->fillRect(outerRects[i].translated(pxCx, pxCy), c.crosshair.chOuterColor);
    }

    if (c.crosshair.chCenterDot) {
        p->setOpacity(1.0);
        const int dh = c.crosshair.cachedDotHalfPx;
        const int ds = c.crosshair.cachedDotSizePx;
        p->fillRect(QRect(pxCx - dh, pxCy - dh, ds, ds), c.crosshair.chDotColor);
    }

    p->restore();
}

// ── DrawEditHudPreview ──────────────────────────────────────────────────────
static void DrawEditHudPreview(QPainter* p, Config::Table& cfg, float tds, float hudScale,
                               float topStretchX, QImage* btmBuffer)
{
    if (!s_editEmu || s_editRomCopy.playerHP == 0) return;
    melonDS::NDS* nds = s_editEmu->getNDS();
    if (!nds) return;
    melonDS::u8* ram = nds->MainRAM;
    if (!ram) return;

    const CachedHudConfig& c = s_cache;
    const RomAddresses&     rom     = s_editRomCopy;
    const GameAddressesHot& addrHot = s_editAddrHotCopy;
    const uint32_t offP = static_cast<uint32_t>(s_editPlayerPosCopy) * Consts::PLAYER_ADDR_INC;

    const uint16_t currentHP = Read16(ram, rom.playerHP + offP);
    const uint16_t maxHP     = Read16(ram, rom.maxHP     + offP);
    DrawHP(p, currentHP, maxHP, c, tds);

    const uint8_t hunterID = Read8(ram, addrHot.chosenHunter);
    const bool    isAlt    = Read8(ram, addrHot.isAltForm) == 0x02;
    const bool    isBomber = (hunterID == static_cast<uint8_t>(HunterId::Samus) ||
                              hunterID == static_cast<uint8_t>(HunterId::Sylux));
    if (isBomber && isAlt)
        DrawBombLeft(p, ram, rom, offP, c, tds, hudScale);

    const bool isAdventure = Read8(ram, rom.isInAdventure) == 0x02;
    DrawMatchStatusHud(p, ram, rom, s_editPlayerPosCopy, isAdventure, c);
    DrawRankAndTime(p, ram, rom, s_editPlayerPosCopy, isAdventure, c, tds);

    // Mirror the normal HUD path so preview mode includes the live radar frame/crop.
    DrawBottomScreenOverlay(cfg, p, btmBuffer, (hunterID <= 6) ? hunterID : 0);

    const uint8_t viewMode    = Read8(ram, rom.baseViewMode + offP);
    const bool    isFirstPerson = (viewMode == 0x00);
    if (isFirstPerson) {
        const uint8_t  weapon          = Read8(ram, addrHot.currentWeapon);
        const uint32_t addrAmmoSpecial = rom.currentAmmoSpecial + offP;
        const uint32_t addrAmmoMissile = rom.currentAmmoMissile + offP;
        const uint16_t maxAmmoSpecial  = Read16(ram, rom.maxAmmoSpecial + offP);
        const uint16_t maxAmmoMissile  = Read16(ram, rom.maxAmmoMissile + offP);
        DrawWeaponAmmo(p, ram, weapon,
                       Read16(ram, addrAmmoSpecial), addrAmmoMissile,
                       maxAmmoSpecial, maxAmmoMissile, c, tds, hudScale);

        {
            const uint16_t havingWeapons = static_cast<uint16_t>(
                Read16(ram, addrHot.havingWeapons));
            DrawWeaponInventory(p, ram, rom, s_editPlayerPosCopy,
                                havingWeapons, weapon, c, tds, hudScale);
        }

        const bool isTrans = (Read8(ram, addrHot.jumpFlag) & 0x10) != 0;
        if (!isTrans && !isAlt)
            DrawCrosshair(p, ram, rom, c, hudScale, topStretchX);
    }
}

// ── Guard to prevent re-entrant QColorDialog ────────────────────────────────
static bool s_colorDialogOpen = false;

// ── DrawElemPropsPanel ──────────────────────────────────────────────────────
// Draws the unified properties panel for the currently selected element.
// Called from DrawEditOverlay in both normal mode and preview mode.
static void DrawElemPropsPanel(QPainter* p, Config::Table& cfg,
                               const QFont& smallFont, const QFont& normalFont)
{
    if (s_editSelected < 0 || s_editSelected >= kEditElemCount) return;
    const HudEditElemDesc& d = kEditElems[s_editSelected];
    const QRectF& selRect = s_editRects[s_editSelected];

    const int builtinRows = CountBuiltinRows(d);
    const int totalRows = builtinRows + d.propCount;
    if (totalRows <= 0) return;

    QRectF panelRect = ComputePropsPanelRect(selRect, totalRows);
    // Shadow
    p->setPen(Qt::NoPen); p->setBrush(kPanelShadow);
    p->drawRoundedRect(panelRect.translated(1.0, 1.0), kBtnCorner, kBtnCorner);
    // Panel bg
    p->setBrush(kPanelBg);
    p->setPen(QPen(kPanelBorder, 0.3));
    p->drawRoundedRect(panelRect, kBtnCorner, kBtnCorner);
    // Accent line at top
    p->setPen(QPen(kAccent, 0.6));
    p->drawLine(QPointF(panelRect.left() + 3, panelRect.top()),
                QPointF(panelRect.right() - 3, panelRect.top()));
    p->setBrush(Qt::NoBrush);

    int visCount = std::min(totalRows, kPropMaxVisible);
    int scrollOfs = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
    for (int vi = 0; vi < visCount; ++vi) {
        int rowIdx = vi + scrollOfs;
        if (rowIdx >= totalRows) break;
        float rowY = static_cast<float>(panelRect.top()) + 2.0f + vi * kPropRowH;
        float rowX = static_cast<float>(panelRect.left()) + 2.0f;
        float ctrlX = rowX + kPropLabelW;
        float ctrlW = kPropCtrlW;

        int builtinIdx = rowIdx;
        bool isShowRow = false, isColorRow = false, isAnchorRow = false;
        if (d.showKey && builtinIdx == 0) {
            isShowRow = true;
        } else {
            if (d.showKey) --builtinIdx;
            if (d.colorRKey && builtinIdx == 0) {
                isColorRow = true;
            } else {
                if (d.colorRKey) --builtinIdx;
                if (builtinIdx == 0 && rowIdx < builtinRows) {
                    isAnchorRow = true;
                }
            }
        }

        if (isShowRow) {
            p->setFont(smallFont);
            p->setPen(kLabelColor);
            p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Show"));
            const bool visible = cfg.GetBool(d.showKey);
            QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
            p->setBrush(visible ? kBtnOnBg : kBtnOffBg);
            p->setPen(Qt::NoPen);
            p->drawRoundedRect(btnR, 1.5, 1.5);
            p->setBrush(Qt::NoBrush);
            p->setPen(kValueColor);
            p->drawText(btnR, Qt::AlignCenter, visible ? QStringLiteral("ON") : QStringLiteral("OFF"));
        } else if (isColorRow) {
            p->setFont(smallFont);
            p->setPen(kLabelColor);
            p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                         Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Color"));
            QColor curColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
            QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
            p->setBrush(curColor); p->setPen(Qt::NoPen);
            p->drawRoundedRect(swR, 1.5, 1.5);
            p->setBrush(Qt::NoBrush);
        } else if (isAnchorRow) {
            static const char* const kAnchorArrow[9] = {
                "\xe2\x86\x96", "\xe2\x86\x91", "\xe2\x86\x97",
                "\xe2\x86\x90", "\xc2\xb7",      "\xe2\x86\x92",
                "\xe2\x86\x99", "\xe2\x86\x93", "\xe2\x86\x98",
            };
            int anchor = cfg.GetInt(d.anchorKey);
            float halfW = ctrlW / 2.0f;
            QRectF valR(ctrlX, rowY + 1.0f, halfW - 1.0f, kPropRowH - 2.0f);
            QRectF btnR(ctrlX + halfW, rowY + 1.0f, halfW, kPropRowH - 2.0f);
            p->setFont(smallFont);
            p->setPen(kLabelColor);
            p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                        Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Anchor"));
            p->setBrush(kCtrlBg); p->setPen(Qt::NoPen);
            p->drawRoundedRect(valR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
            p->setBrush(s_anchorPickerOpen ? QColor(45, 100, 50, 230) : kBtnBg);
            p->setPen(Qt::NoPen);
            p->drawRoundedRect(btnR, 1.5, 1.5);
            p->setBrush(Qt::NoBrush);
            p->setPen(kValueColor);
            p->drawText(valR, Qt::AlignCenter,
                QString::fromUtf8(anchor >= 0 && anchor < 9 ? kAnchorArrow[anchor] : "?"));
            p->drawText(btnR, Qt::AlignCenter, QStringLiteral("Chng"));
        } else {
            int propIdx = rowIdx - builtinRows;
            if (propIdx < 0 || propIdx >= d.propCount) continue;
            const HudEditPropDesc& pr = d.props[propIdx];

            p->setFont(smallFont);
            p->setPen(kLabelColor);
            p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                         Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(pr.label));

            switch (pr.type) {
            case EditPropType::Bool: {
                bool val = cfg.GetBool(pr.cfgKey);
                QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                p->setBrush(val ? kBtnOnBg : kBtnOffBg);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(btnR, 1.5, 1.5);
                p->setBrush(Qt::NoBrush);
                p->setPen(kValueColor);
                p->drawText(btnR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                break;
            }
            case EditPropType::Int: {
                int val = cfg.GetInt(pr.cfgKey);
                float arrowW = 8.0f;
                QRectF leftArr(ctrlX, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF rightArr(ctrlX + ctrlW - arrowW, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF valR(ctrlX + arrowW, rowY + 1.0f, ctrlW - 2.0f * arrowW, kPropRowH - 2.0f);
                p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                p->drawRoundedRect(leftArr, 1.5, 1.5);
                p->drawRoundedRect(rightArr, 1.5, 1.5);
                p->setBrush(kCtrlBg);
                p->drawRoundedRect(valR, 0, 0);
                p->setBrush(Qt::NoBrush);
                p->setPen(kLabelColor);
                p->drawText(leftArr, Qt::AlignCenter, QStringLiteral("\u25C0"));
                p->drawText(rightArr, Qt::AlignCenter, QStringLiteral("\u25B6"));
                p->setPen(kValueColor);
                p->drawText(valR, Qt::AlignCenter, QString::number(val));
                break;
            }
            case EditPropType::Enum: {
                int val = cfg.GetInt(pr.cfgKey);
                float arrowW = 8.0f;
                QRectF leftArr(ctrlX, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF rightArr(ctrlX + ctrlW - arrowW, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF valR(ctrlX + arrowW, rowY + 1.0f, ctrlW - 2.0f * arrowW, kPropRowH - 2.0f);
                p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                p->drawRoundedRect(leftArr, 1.5, 1.5);
                p->drawRoundedRect(rightArr, 1.5, 1.5);
                p->setBrush(kCtrlBg);
                p->drawRoundedRect(valR, 0, 0);
                p->setBrush(Qt::NoBrush);
                p->setPen(kLabelColor);
                p->drawText(leftArr, Qt::AlignCenter, QStringLiteral("\u25C0"));
                p->drawText(rightArr, Qt::AlignCenter, QStringLiteral("\u25B6"));
                p->setPen(kValueColor);
                p->drawText(valR, Qt::AlignCenter, EditEnumLabel(pr.extra1, val));
                break;
            }
            case EditPropType::Float: {
                double val = cfg.GetDouble(pr.cfgKey);
                float arrowW = 8.0f;
                QRectF leftArr(ctrlX, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF rightArr(ctrlX + ctrlW - arrowW, rowY + 1.0f, arrowW, kPropRowH - 2.0f);
                QRectF valR(ctrlX + arrowW, rowY + 1.0f, ctrlW - 2.0f * arrowW, kPropRowH - 2.0f);
                p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                p->drawRoundedRect(leftArr, 1.5, 1.5);
                p->drawRoundedRect(rightArr, 1.5, 1.5);
                p->setBrush(kCtrlBg);
                p->drawRoundedRect(valR, 0, 0);
                p->setBrush(Qt::NoBrush);
                p->setPen(kLabelColor);
                p->drawText(leftArr, Qt::AlignCenter, QStringLiteral("\u25C0"));
                p->drawText(rightArr, Qt::AlignCenter, QStringLiteral("\u25B6"));
                p->setPen(kValueColor);
                p->drawText(valR, Qt::AlignCenter, QString::number(val, 'f', 2));
                break;
            }
            case EditPropType::String: {
                std::string val = cfg.GetString(pr.cfgKey);
                QRectF btnR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                p->setBrush(kCtrlBg); p->setPen(Qt::NoPen);
                p->drawRoundedRect(btnR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
                p->setPen(kValueColor);
                QString display = QString::fromStdString(val);
                if (display.length() > 6) display = display.left(5) + QStringLiteral("\u2026");
                p->drawText(btnR, Qt::AlignCenter, display.isEmpty() ? QStringLiteral("...") : display);
                break;
            }
            case EditPropType::SubColor: {
                bool overall = cfg.GetBool(pr.cfgKey);
                float ovrW = 14.0f;
                QRectF ovrR(ctrlX, rowY + 1.0f, ovrW, kPropRowH - 2.0f);
                p->setBrush(overall ? QColor(70, 70, 35, 200) : kCtrlBg);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(ovrR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
                p->setPen(overall ? QColor(220, 220, 130) : QColor(100, 100, 140));
                p->drawText(ovrR, Qt::AlignCenter, QStringLiteral("OVR"));
                QColor clr(cfg.GetInt(pr.extra1), cfg.GetInt(pr.extra2), cfg.GetInt(pr.extra3));
                QRectF swR(ctrlX + ovrW + 2.0f, rowY + 1.0f, ctrlW - ovrW - 2.0f, kPropRowH - 2.0f);
                p->setBrush(overall ? QColor(80, 80, 80) : clr);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(swR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
                break;
            }
            case EditPropType::Color: break;
            }
        }
    }
    // Scroll indicator
    if (totalRows > kPropMaxVisible) {
        int scrollOfsI = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
        float rowAreaH = visCount * kPropRowH;
        float indH = rowAreaH * kPropMaxVisible / totalRows;
        float indY = static_cast<float>(panelRect.top()) + 2.0f +
            (rowAreaH - indH) * scrollOfsI / (totalRows - kPropMaxVisible);
        QRectF indR(panelRect.right() - 2.0f, indY, 1.5f, indH);
        p->setPen(Qt::NoPen); p->setBrush(kPanelBorder);
        p->drawRoundedRect(indR, 0.75f, 0.75f);
        p->setBrush(Qt::NoBrush);
    }
    // Anchor picker 3×3 grid
    if (s_anchorPickerOpen) {
        static const char* const kAnchorArrowGrid[9] = {
            "\xe2\x86\x96", "\xe2\x86\x91", "\xe2\x86\x97",
            "\xe2\x86\x90", "\xc2\xb7",      "\xe2\x86\x92",
            "\xe2\x86\x99", "\xe2\x86\x93", "\xe2\x86\x98",
        };
        int curAnchor = cfg.GetInt(d.anchorKey);
        float gridLeft = static_cast<float>(panelRect.left()) + 2.0f;
        float gridTop  = static_cast<float>(panelRect.top()) + 2.0f + visCount * kPropRowH + 2.0f;
        float cellW = (kPropPanelW - 4.0f) / 3.0f;
        float cellH = kAnchorGridCellH;
        p->setFont(normalFont);
        for (int a = 0; a < 9; ++a) {
            int col = a % 3, row = a / 3;
            QRectF cell(gridLeft + col * cellW, gridTop + row * cellH, cellW - 1.0f, cellH - 1.0f);
            bool sel = (a == curAnchor);
            p->setPen(Qt::NoPen);
            p->setBrush(sel ? kBtnOnBg : kCtrlBg);
            p->drawRoundedRect(cell, kCornerRadius, kCornerRadius);
            p->setBrush(Qt::NoBrush);
            p->setPen(sel ? QPen(QColor(140, 255, 160), 0.5) : QPen(kPanelBorder, 0.3));
            p->drawRoundedRect(cell, kCornerRadius, kCornerRadius);
            p->setPen(sel ? QColor(200, 255, 210) : kLabelColor);
            p->drawText(cell, Qt::AlignCenter, QString::fromUtf8(kAnchorArrowGrid[a]));
        }
    }
    p->setFont(normalFont);
}

// ── DrawEditOverlay ─────────────────────────────────────────────────────────
static void DrawEditOverlay(QPainter* p, Config::Table& cfg, float topStretchX, QImage* btmBuffer)
{
    if (!p) return;

    const float leftX = -(topStretchX - 1.0f) * 128.0f;
    const float hs  = (s_editHudScale > 0.0f) ? s_editHudScale : 1.0f;
    // Use exact textDrawScale from cache (includes auto-scale + user TextScale, divided by hudScale).
    const float tds = s_cache.valid
        ? s_cache.textDrawScale
        : std::max(0.3f, cfg.GetInt("Metroid.Visual.HudTextScale") / 100.0f) / hs;

    QFont smallFont = p->font();
    smallFont.setPixelSize(4);
    QFont elemFont = p->font();
    elemFont.setPixelSize(std::max(3, static_cast<int>(std::round(6.0f * tds))));
    QFont normalFont = p->font();
    normalFont.setPixelSize(5);

    // ── Preview mode: actual HUD rendering ─────────────────────────────────
    if (s_editPreviewMode) {
        DrawEditHudPreview(p, cfg, tds, s_editHudScale, topStretchX, btmBuffer);

        // Selection highlight over the live preview
        if (s_editSelected >= 0 && s_editSelected < kEditElemCount) {
            const QRectF& selR = s_editRects[s_editSelected];
            if (!selR.isEmpty()) {
                p->setBrush(Qt::NoBrush);
                p->setPen(QPen(QColor(255, 200, 80, 200), 0.8));
                p->drawRect(selR);
            }
            if (kShowDsEditPropsPanel)
                DrawElemPropsPanel(p, cfg, smallFont, normalFont);
        }

    } else {
    // ── Normal mode: element boxes ─────────────────────────────────────────

    p->fillRect(QRectF(leftX, 0.0f, 256.0f * topStretchX, 192.0f), QColor(0, 0, 0, 80));
    p->setPen(Qt::NoPen);

    for (int i = 0; i < kEditElemCount; ++i) {
        const QRectF& r = s_editRects[i];
        if (r.isEmpty()) continue;
        const bool sel = (i == s_editSelected);
        const bool hov = (i == s_editHovered);
        const HudEditElemDesc& d = kEditElems[i];

        const bool hidden = d.showKey && !cfg.GetBool(d.showKey);
        const QColor& bgc = hidden ? (sel ? kElemHiddenSelBg : hov ? kElemHiddenHovBg : kElemHiddenBg)
                                   : (sel ? kElemSelBg       : hov ? kElemHovBg       : kElemNormBg);
        p->setBrush(bgc);
        p->setPen(sel ? QPen(kAccent, 0.8)
                      : QPen(QColor(90, 120, 170, hov ? 60 : 30), 0.3));
        p->drawRoundedRect(r, kCornerRadius, kCornerRadius);
        p->setBrush(Qt::NoBrush);

        p->setFont(elemFont);
        const bool isRadar   = (i == 11);
        const bool isGauge   = (d.lengthKey != nullptr) && !isRadar; // radar reuses lengthKey for square resize
        const bool isWpnIcon = (i == 3);
        const bool isBmbIcon = (i == 10);

        if (isGauge && !hidden) {
            QColor gc(255, 255, 255);
            if (d.colorRKey)
                gc = QColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
            gc.setAlpha(180);
            const int ori = (d.orientKey != nullptr) ? cfg.GetInt(d.orientKey) : 0;
            QRectF fillR = r;
            if (ori == 1)
                fillR.setHeight(r.height() * 0.5);
            else
                fillR.setWidth(r.width() * 0.5);
            p->fillRect(fillR, gc);
            p->setPen(QPen(Qt::white, 0.3));
            if (ori == 1)
                p->drawLine(QPointF(r.left(), fillR.bottom()), QPointF(r.right(), fillR.bottom()));
            else
                p->drawLine(QPointF(fillR.right(), r.top()), QPointF(fillR.right(), r.bottom()));
        } else if (isWpnIcon && !hidden) {
            EnsureIconsLoaded();
            const QImage& icon = s_weaponIcons[0];
            if (!icon.isNull()) {
                p->drawImage(r, icon);
            } else {
                p->setPen(Qt::white);
                p->drawText(r, Qt::AlignCenter, QStringLiteral("WPN"));
            }
        } else if (isBmbIcon && !hidden) {
            EnsureBombIconsLoaded();
            const QImage& icon = s_bombIcons[3];
            if (!icon.isNull()) {
                p->drawImage(r, icon);
            } else {
                p->setPen(Qt::white);
                p->drawText(r, Qt::AlignCenter, QStringLiteral("BMB"));
            }
        } else if (isRadar) {
            // Ensure radar SVG frame is loaded and draw it behind the crop circle.
            // Always draw even when hidden (BtmOverlayEnable=false) so preview is visible.
            // s_cache is always valid in edit mode (refreshed at top of CustomHud_Render).
            p->setPen(QPen(QColor(0x66, 0xDD, 0x66, 200), 0.5));
            p->setBrush(QColor(0x22, 0x88, 0x22, 80));
            if (s_cache.valid) {
                const CachedHudConfig& c = s_cache;
                p->drawEllipse(QRectF(c.radar.radarDstRect));
                p->setBrush(Qt::NoBrush);
                const HudOutlineConfig& fol = EffOL(c, c.radar.frameOutline);
                const int expandR = (fol.enable && fol.opacity > 0.0f)
                                    ? std::max(1, fol.thickness) : 0;
                // Use radarFrameColor directly (s_effectiveRadarColor is only updated
                // in DrawBottomScreenOverlay which is not called in edit mode).
                const QColor& tint = c.radar.radarFrameColor;
                EnsureRadarFrameLoaded(c.radar.radarDstSize, c.radar.radarSrcRadius,
                                       s_editHudScale, tint, fol.color, expandR);
                if (!s_radarFrameTinted.isNull())
                    p->drawImage(c.radar.frameDstRect, s_radarFrameTinted);
            } else {
                // Fallback before the first cache refresh.
                float sz = std::min(static_cast<float>(r.width()), static_cast<float>(r.height()));
                QRectF circR(r.center().x() - sz * 0.5f, r.center().y() - sz * 0.5f, sz, sz);
                p->drawEllipse(circR);
                p->setBrush(Qt::NoBrush);
            }
        } else if (i == 12) {
            // Weapon Inventory: draw mini icon grid preview
            EnsureIconsLoaded();
            const int ori = cfg.GetInt("Metroid.Visual.HudWeaponInventoryOrientation");
            const float gap = 1.0f;
            const int numIcons = 9;
            float iconSz;
            if (ori == 1) // Vertical
                iconSz = std::max(2.0f, (static_cast<float>(r.height()) - gap * (numIcons - 1)) / numIcons);
            else
                iconSz = std::max(2.0f, (static_cast<float>(r.width()) - gap * (numIcons - 1)) / numIcons);
            float ix = static_cast<float>(r.left()), iy = static_cast<float>(r.top());
            for (int wi = 0; wi < numIcons; ++wi) {
                const QImage& icon = s_weaponIcons[wi];
                const float iw = (!icon.isNull() && icon.height() > 0)
                                 ? iconSz * icon.width() / icon.height() : iconSz;
                const QRectF ir(ix, iy, iw, iconSz);
                if (!icon.isNull())
                    p->drawImage(ir, icon);
                else {
                    p->setPen(QColor(200,200,200,120));
                    p->drawRect(ir);
                }
                if (ori == 1) iy += iconSz + gap;
                else           ix += iw + gap;
            }
        } else if (i == 13) {
            if (s_cache.valid) {
                DrawEditCrosshairActual(p, r, s_cache);
            } else {
                const QColor cc(cfg.GetInt("Metroid.Visual.CrosshairColorR"),
                                cfg.GetInt("Metroid.Visual.CrosshairColorG"),
                                cfg.GetInt("Metroid.Visual.CrosshairColorB"));
                const float cx = static_cast<float>(r.center().x());
                const float cy = static_cast<float>(r.center().y());
                const float arm = static_cast<float>(r.width()) * 0.38f;
                QPen chPen(cc, 1.5f);
                chPen.setCapStyle(Qt::RoundCap);
                p->setPen(chPen);
                p->drawLine(QPointF(cx - arm, cy), QPointF(cx + arm, cy));
                p->drawLine(QPointF(cx, cy - arm), QPointF(cx, cy + arm));
                if (cfg.GetBool("Metroid.Visual.CrosshairCenterDot")) {
                    p->setPen(Qt::NoPen);
                    p->setBrush(cc);
                    p->drawEllipse(QPointF(cx, cy), 1.8f, 1.8f);
                    p->setBrush(Qt::NoBrush);
                }
            }
        } else {
            QColor tc(255, 255, 255);
            if (d.colorRKey)
                tc = QColor(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
            p->setPen(tc);

            const char* sampleText = (i >= 0 && i < kEditElemCount && kEditElemSampleText[i])
                                      ? kEditElemSampleText[i] : d.name;

            if (r.height() > r.width() * 1.3) {
                p->save();
                p->translate(r.center());
                p->rotate(-90);
                QRectF textRect(-r.height() / 2.0, -r.width() / 2.0, r.height(), r.width());
                p->drawText(textRect, Qt::AlignCenter, QString::fromUtf8(sampleText));
                p->restore();
            } else {
                p->drawText(r, Qt::AlignCenter, QString::fromUtf8(sampleText));
            }
        }
        p->setFont(normalFont);
    }

    // Selected element extras
    if (s_editSelected >= 0 && s_editSelected < kEditElemCount) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];

        // Orientation toggle (gauges only)
        if (d.orientKey != nullptr) {
            QRectF orientRect = GetOrientToggleRect(s_editSelected);
            p->setPen(Qt::NoPen); p->setBrush(kBtnBg);
            p->drawRoundedRect(orientRect, kBtnCorner, kBtnCorner);
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(kPanelBorder, 0.5));
            p->drawRoundedRect(orientRect, kBtnCorner, kBtnCorner);
            float lx = static_cast<float>(orientRect.left())  + 1.5f;
            float rx = static_cast<float>(orientRect.right()) - 2.0f;
            float ty = static_cast<float>(orientRect.top())   + 1.5f;
            float by = static_cast<float>(orientRect.bottom())- 2.0f;
            QPen ap(kLabelColor, 0.8f);
            ap.setCapStyle(Qt::RoundCap);
            p->setPen(ap);
            p->drawLine(QPointF(lx, by), QPointF(rx, by));
            p->drawLine(QPointF(rx, by), QPointF(rx, ty));
            p->drawLine(QPointF(lx, by), QPointF(lx + 1.8f, by - 1.5f));
            p->drawLine(QPointF(lx, by), QPointF(lx + 1.8f, by + 1.5f));
            p->drawLine(QPointF(rx, ty), QPointF(rx - 1.5f, ty + 1.8f));
            p->drawLine(QPointF(rx, ty), QPointF(rx + 1.5f, ty + 1.8f));
        }

        // Resize handles
        if (d.lengthKey != nullptr) {
            QRectF lenH, widH;
            GetResizeHandles(s_editSelected, cfg, lenH, widH);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(100, 140, 255, 200));
            p->drawRoundedRect(lenH, 1.0f, 1.0f);
            if (!widH.isEmpty())
                p->drawRoundedRect(widH, 1.0f, 1.0f);
            p->setBrush(Qt::NoBrush);
        }

        if (kShowDsEditPropsPanel)
            DrawElemPropsPanel(p, cfg, smallFont, normalFont);
    }

    } // end of normal (non-preview) element rendering

    // ── Button bar (always drawn in both modes) ─────────────────────────────

    p->setFont(normalFont);
    // Save
    p->setBrush(QColor(25, 105, 50, 220));
    p->setPen(Qt::NoPen);
    p->drawRoundedRect(kEditSaveRect, kBtnCorner, kBtnCorner);
    // Cancel
    p->setBrush(QColor(110, 32, 32, 220));
    p->drawRoundedRect(kEditCancelRect, kBtnCorner, kBtnCorner);
    // Reset
    p->setBrush(QColor(30, 35, 100, 220));
    p->drawRoundedRect(kEditResetRect, kBtnCorner, kBtnCorner);
    p->setBrush(Qt::NoBrush);
    p->setPen(kValueColor);
    p->drawText(kEditSaveRect,   Qt::AlignCenter, QStringLiteral("\u2713 Save"));
    p->drawText(kEditCancelRect, Qt::AlignCenter, QStringLiteral("\u2717 Cancel"));
    p->drawText(kEditResetRect,  Qt::AlignCenter, QStringLiteral("\u21ba Reset"));

    // ── Text Scale slider ───────────────────────────────────────────────────
    {
        int txSc = std::max(kTsMin, std::min(kTsMax, cfg.GetInt("Metroid.Visual.HudTextScale")));
        p->setBrush(kPanelBg);
        p->setPen(QPen(kPanelBorder, 0.3));
        p->drawRoundedRect(kEditTextScaleRect, kBtnCorner, kBtnCorner);
        p->setBrush(Qt::NoBrush);
        p->setFont(smallFont);

        const float tsX = static_cast<float>(kEditTextScaleRect.left()) + 2.0f;
        const float tsY = static_cast<float>(kEditTextScaleRect.top());
        const float tsH = static_cast<float>(kEditTextScaleRect.height());

        // Label
        p->setPen(kLabelColor);
        p->drawText(QRectF(tsX, tsY, kTsLabelW, tsH), Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("Text"));

        // Track
        const QRectF tr = TsTrackRect();
        const float frac = static_cast<float>(txSc - kTsMin) / (kTsMax - kTsMin);
        p->setBrush(kCtrlBg);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(tr, 1.0, 1.0);
        p->setBrush(kAccentDim);
        p->drawRoundedRect(QRectF(tr.left(), tr.top(), tr.width() * frac, tr.height()), 1.0, 1.0);
        p->setBrush(Qt::NoBrush);
        // Thumb
        const float thumbX = tr.left() + tr.width() * frac - 1.0f;
        p->fillRect(QRectF(thumbX, tsY + 0.5f, 2.0f, tsH - 1.0f), QColor(200, 220, 255, 240));
        // Track border
        p->setPen(QPen(kPanelBorder, 0.4));
        p->drawRoundedRect(tr, 1.0, 1.0);
        // Value text
        p->setPen(kValueColor);
        p->setFont(smallFont);
        p->drawText(tr, Qt::AlignCenter, QString::number(txSc) + QStringLiteral("%"));
    }

    // ── Auto-Scale Global Cap slider ────────────────────────────────────────
    {
        int ascVal = std::clamp(cfg.GetInt("Metroid.Visual.HudAutoScaleCap"), kAscMin, kAscMax);
        p->setBrush(kPanelBg);
        p->setPen(QPen(kPanelBorder, 0.3));
        p->drawRoundedRect(kEditAutoScaleCapRect, kBtnCorner, kBtnCorner);
        p->setBrush(Qt::NoBrush);
        p->setFont(smallFont);

        const float ascX = static_cast<float>(kEditAutoScaleCapRect.left()) + 2.0f;
        const float ascY = static_cast<float>(kEditAutoScaleCapRect.top());
        const float ascH = static_cast<float>(kEditAutoScaleCapRect.height());

        // Label
        p->setPen(kLabelColor);
        p->drawText(QRectF(ascX, ascY, kAscLabelW, ascH), Qt::AlignLeft | Qt::AlignVCenter,
                    QStringLiteral("Auto"));

        // Track
        const QRectF atr = AscTrackRect();
        const float ascFrac = static_cast<float>(ascVal - kAscMin) / (kAscMax - kAscMin);
        p->setBrush(kCtrlBg);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(atr, 1.0, 1.0);
        p->setBrush(QColor(60, 110, 60, 180));
        p->drawRoundedRect(QRectF(atr.left(), atr.top(), atr.width() * ascFrac, atr.height()), 1.0, 1.0);
        p->setBrush(Qt::NoBrush);
        // Thumb
        const float ascThumbX = atr.left() + atr.width() * ascFrac - 1.0f;
        p->fillRect(QRectF(ascThumbX, ascY + 0.5f, 2.0f, ascH - 1.0f), QColor(180, 240, 180, 240));
        // Track border
        p->setPen(QPen(kPanelBorder, 0.4));
        p->drawRoundedRect(atr, 1.0, 1.0);
        // Value text
        p->setPen(kValueColor);
        p->setFont(smallFont);
        p->drawText(atr, Qt::AlignCenter, QString::number(ascVal) + QStringLiteral("%"));
    }

    // ── Crosshair toggle button ─────────────────────────────────────────────
    {
        p->setBrush(s_crosshairPanelOpen ? QColor(35, 90, 45, 220) : kPanelBg);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(kEditCrosshairBtnRect, kBtnCorner, kBtnCorner);
        p->setBrush(Qt::NoBrush);
        p->setFont(normalFont);
        p->setPen(s_crosshairPanelOpen ? kValueColor : kLabelColor);
        p->drawText(kEditCrosshairBtnRect, Qt::AlignCenter,
                    s_crosshairPanelOpen ? QStringLiteral("Crosshair \u25bc")
                                         : QStringLiteral("Crosshair \u25b6"));
    }

    // ── Preview toggle button ───────────────────────────────────────────────
    {
        p->setBrush(s_editPreviewMode ? QColor(80, 65, 18, 220) : kPanelBg);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(kEditPreviewBtnRect, kBtnCorner, kBtnCorner);
        p->setBrush(Qt::NoBrush);
        p->setFont(normalFont);
        p->setPen(s_editPreviewMode ? QColor(255, 210, 100) : kLabelColor);
        p->drawText(kEditPreviewBtnRect, Qt::AlignCenter,
                    s_editPreviewMode ? QStringLiteral("\u25a0 Preview ON")
                                      : QStringLiteral("\u25b6 Preview"));
    }

    // ── Crosshair panel (visible in both modes when open) ───────────────────
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows();
        const int visCount  = std::min(totalRows, kCrosshairMaxVisible);
        const float panelH  = visCount * kPropRowH + 4.0f;
        const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);

        const int scrollOfs = std::min(s_crosshairPanelScroll,
                                       std::max(0, totalRows - kCrosshairMaxVisible));

        p->setBrush(kPanelShadow);
        p->setPen(Qt::NoPen);
        p->drawRoundedRect(panelRect.translated(1.0, 1.0), kBtnCorner, kBtnCorner);
        p->setBrush(kPanelBg);
        p->setPen(QPen(kPanelBorder, 0.3));
        p->drawRoundedRect(panelRect, kBtnCorner, kBtnCorner);
        p->setPen(QPen(kAccent, 0.5));
        p->drawLine(panelRect.left()+kBtnCorner, panelRect.top(), panelRect.right()-kBtnCorner, panelRect.top());
        p->setBrush(Qt::NoBrush);
        if (totalRows > visCount) {
            const float sbW    = 2.5f;
            const float sbX    = panelRect.right() - sbW - 0.5f;
            const float sbY    = panelRect.top()   + 2.0f;
            const float sbH    = panelH - 4.0f;
            const float thumbH = sbH * (float)visCount / totalRows;
            const float thumbY = sbY + sbH * (float)scrollOfs / totalRows;
            p->fillRect(QRectF(sbX, sbY, sbW, sbH),       kCtrlBg);
            p->fillRect(QRectF(sbX, thumbY, sbW, thumbH), kAccentDim);
        }

        const float rowX  = kCrosshairPanelX + 2.0f;
        const float ctrlX = rowX + kPropLabelW;
        const float ctrlW = kPropCtrlW;

        p->setFont(smallFont);

        for (int vi = 0; vi < visCount; ++vi) {
            const int rowIdx = vi + scrollOfs;
            const float rowY = kCrosshairPanelY + 2.0f + vi * kPropRowH;
            bool isColorRow, isInnerHdr, isOuterHdr;
            const HudEditPropDesc* pr = GetCrosshairPropForRow(rowIdx, isColorRow, isInnerHdr, isOuterHdr);

            if (isColorRow) {
                int r = cfg.GetInt("Metroid.Visual.CrosshairColorR");
                int g = cfg.GetInt("Metroid.Visual.CrosshairColorG");
                int b = cfg.GetInt("Metroid.Visual.CrosshairColorB");
                p->setPen(kLabelColor);
                p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                            Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Color"));
                QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                p->setBrush(QColor(r, g, b));
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(swR, 1.5, 1.5); p->setBrush(Qt::NoBrush);
            } else if (isInnerHdr) {
                QRectF hdrR(rowX, rowY + 0.5f, kPropLabelW + ctrlW, kPropRowH - 1.0f);
                p->setBrush(s_innerSectionOpen ? QColor(35, 90, 45, 210) : kBtnBg);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(hdrR, 1.5, 1.5);
                p->setBrush(Qt::NoBrush);
                p->setPen(s_innerSectionOpen ? kValueColor : kLabelColor);
                p->drawText(hdrR, Qt::AlignCenter,
                    s_innerSectionOpen ? QStringLiteral("Inner \u25c4") : QStringLiteral("Inner \u25ba"));
            } else if (isOuterHdr) {
                QRectF hdrR(rowX, rowY + 0.5f, kPropLabelW + ctrlW, kPropRowH - 1.0f);
                p->setBrush(s_outerSectionOpen ? QColor(35, 90, 45, 210) : kBtnBg);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(hdrR, 1.5, 1.5);
                p->setBrush(Qt::NoBrush);
                p->setPen(s_outerSectionOpen ? kValueColor : kLabelColor);
                p->drawText(hdrR, Qt::AlignCenter,
                    s_outerSectionOpen ? QStringLiteral("Outer \u25c4") : QStringLiteral("Outer \u25ba"));
            } else if (pr) {
                p->setPen(kLabelColor);
                p->drawText(QRectF(rowX, rowY, kPropLabelW, kPropRowH),
                            Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1(pr->label));
                switch (pr->type) {
                case EditPropType::Bool: {
                    bool val = cfg.GetBool(pr->cfgKey);
                    QRectF swR(ctrlX, rowY + 1.0f, ctrlW, kPropRowH - 2.0f);
                    p->setBrush(val ? kBtnOnBg : kBtnOffBg);
                    p->setPen(Qt::NoPen);
                    p->drawRoundedRect(swR, 1.5, 1.5);
                    p->setBrush(Qt::NoBrush);
                    p->setPen(kValueColor);
                    p->drawText(swR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                    break;
                }
                case EditPropType::Int: {
                    int val = cfg.GetInt(pr->cfgKey);
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF valR2(ctrlX + btnW2, rowY + 1.0f, ctrlW - btnW2 * 2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                    p->drawRoundedRect(decR, 1.5, 1.5);
                    p->drawRoundedRect(incR, 1.5, 1.5);
                    p->setBrush(kCtrlBg);
                    p->drawRoundedRect(valR2, 0, 0);
                    p->setBrush(Qt::NoBrush);
                    p->setPen(kLabelColor);
                    p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                    p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                    p->setPen(kValueColor);
                    p->drawText(valR2, Qt::AlignCenter, QString::number(val));
                    break;
                }
                case EditPropType::Enum: {
                    int val = cfg.GetInt(pr->cfgKey);
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF valR2(ctrlX + btnW2, rowY + 1.0f, ctrlW - btnW2 * 2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                    p->drawRoundedRect(decR, 1.5, 1.5);
                    p->drawRoundedRect(incR, 1.5, 1.5);
                    p->setBrush(kCtrlBg);
                    p->drawRoundedRect(valR2, 0, 0);
                    p->setBrush(Qt::NoBrush);
                    p->setPen(kLabelColor);
                    p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                    p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                    p->setPen(kValueColor);
                    p->drawText(valR2, Qt::AlignCenter, EditEnumLabel(pr->extra1, val));
                    break;
                }
                case EditPropType::Float: {
                    double val = cfg.GetDouble(pr->cfgKey);
                    int pct = static_cast<int>(val * 100.0 + 0.5);
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF valR2(ctrlX + btnW2, rowY + 1.0f, ctrlW - btnW2 * 2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                    p->drawRoundedRect(decR, 1.5, 1.5);
                    p->drawRoundedRect(incR, 1.5, 1.5);
                    p->setBrush(kCtrlBg);
                    p->drawRoundedRect(valR2, 0, 0);
                    p->setBrush(Qt::NoBrush);
                    p->setPen(kLabelColor);
                    p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                    p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                    p->setPen(kValueColor);
                    p->drawText(valR2, Qt::AlignCenter, QString::number(pct) + QStringLiteral("%"));
                    break;
                }
                default: break;
                }
            }
        }

        // Inner / Outer side panel
        {
            const HudEditPropDesc* sideProps = nullptr;
            int sidePropCount = 0;
            if (s_innerSectionOpen) {
                sideProps = kPropsCrosshairInner; sidePropCount = kCrosshairInnerCount;
            } else if (s_outerSectionOpen) {
                sideProps = kPropsCrosshairOuter; sidePropCount = kCrosshairOuterCount;
            }
            if (sideProps) {
                const float sidePanelH = sidePropCount * kPropRowH + 4.0f;
                const QRectF sideRect(kCrosshairSidePanelX, kCrosshairPanelY, kPropPanelW, sidePanelH);
                p->setBrush(kPanelShadow);
                p->setPen(Qt::NoPen);
                p->drawRoundedRect(sideRect.translated(1.0, 1.0), kBtnCorner, kBtnCorner);
                p->setBrush(kPanelBg);
                p->setPen(QPen(kPanelBorder, 0.3));
                p->drawRoundedRect(sideRect, kBtnCorner, kBtnCorner);
                p->setPen(QPen(kAccent, 0.5));
                p->drawLine(sideRect.left()+kBtnCorner, sideRect.top(), sideRect.right()-kBtnCorner, sideRect.top());
                p->setBrush(Qt::NoBrush);

                const float sRowX  = kCrosshairSidePanelX + 2.0f;
                const float sCtrlX = sRowX + kPropLabelW;

                for (int i = 0; i < sidePropCount; ++i) {
                    const HudEditPropDesc& pr2 = sideProps[i];
                    const float rowY = kCrosshairPanelY + 2.0f + i * kPropRowH;
                    p->setPen(kLabelColor);
                    p->drawText(QRectF(sRowX, rowY, kPropLabelW, kPropRowH),
                                Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1(pr2.label));
                    switch (pr2.type) {
                    case EditPropType::Bool: {
                        bool val = cfg.GetBool(pr2.cfgKey);
                        QRectF swR(sCtrlX, rowY + 1.0f, kPropCtrlW, kPropRowH - 2.0f);
                        p->setBrush(val ? kBtnOnBg : kBtnOffBg);
                        p->setPen(Qt::NoPen);
                        p->drawRoundedRect(swR, 1.5, 1.5);
                        p->setBrush(Qt::NoBrush);
                        p->setPen(kValueColor);
                        p->drawText(swR, Qt::AlignCenter, val ? QStringLiteral("ON") : QStringLiteral("OFF"));
                        break;
                    }
                    case EditPropType::Int: {
                        int val = cfg.GetInt(pr2.cfgKey);
                        float btnW2 = 8.0f;
                        QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        QRectF valR2(sCtrlX + btnW2, rowY + 1.0f, kPropCtrlW - btnW2 * 2, kPropRowH - 2.0f);
                        QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                        p->drawRoundedRect(decR, 1.5, 1.5);
                        p->drawRoundedRect(incR, 1.5, 1.5);
                        p->setBrush(kCtrlBg);
                        p->drawRoundedRect(valR2, 0, 0);
                        p->setBrush(Qt::NoBrush);
                        p->setPen(kLabelColor);
                        p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                        p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                        p->setPen(kValueColor);
                        p->drawText(valR2, Qt::AlignCenter, QString::number(val));
                        break;
                    }
                    case EditPropType::Float: {
                        double val = cfg.GetDouble(pr2.cfgKey);
                        int pct = static_cast<int>(val * 100.0 + 0.5);
                        float btnW2 = 8.0f;
                        QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        QRectF valR2(sCtrlX + btnW2, rowY + 1.0f, kPropCtrlW - btnW2 * 2, kPropRowH - 2.0f);
                        QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                        p->setBrush(kArrowBg); p->setPen(Qt::NoPen);
                        p->drawRoundedRect(decR, 1.5, 1.5);
                        p->drawRoundedRect(incR, 1.5, 1.5);
                        p->setBrush(kCtrlBg);
                        p->drawRoundedRect(valR2, 0, 0);
                        p->setBrush(Qt::NoBrush);
                        p->setPen(kLabelColor);
                        p->drawText(decR, Qt::AlignCenter, QStringLiteral("\u25c0"));
                        p->drawText(incR, Qt::AlignCenter, QStringLiteral("\u25b6"));
                        p->setPen(kValueColor);
                        p->drawText(valR2, Qt::AlignCenter, QString::number(pct) + QStringLiteral("%"));
                        break;
                    }
                    default: break;
                    }
                }
            }
        }

        // Crosshair preview
        {
            const float pvX = kCrosshairPreviewX;
            const float pvY = kCrosshairPanelY;
            const float pvS = static_cast<float>(kCrosshairPreviewSize);
            const float fcx = pvX + pvS * 0.5f;
            const float fcy = pvY + pvS * 0.5f;

            p->setBrush(QColor(8, 10, 22, 220));
            p->setPen(QPen(kPanelBorder, 0.3));
            p->drawRoundedRect(QRectF(pvX, pvY, pvS, pvS), kBtnCorner, kBtnCorner);
            p->setBrush(Qt::NoBrush);

            const int cr  = cfg.GetInt("Metroid.Visual.CrosshairColorR");
            const int cg  = cfg.GetInt("Metroid.Visual.CrosshairColorG");
            const int cb2 = cfg.GetInt("Metroid.Visual.CrosshairColorB");
            const bool tStyle    = cfg.GetBool("Metroid.Visual.CrosshairTStyle");
            const bool innerShow = cfg.GetBool("Metroid.Visual.CrosshairInnerShow");
            const bool outerShow = cfg.GetBool("Metroid.Visual.CrosshairOuterShow");
            const bool centerDot = cfg.GetBool("Metroid.Visual.CrosshairCenterDot");
            constexpr float PVS = 2.0f;

            // Helper lambda: draw arms using float rects with properly scaled center gap
            auto drawArms = [&](float lenX, float lenY, float offset, float thick,
                                bool tSt, const QColor& clr) {
                const float ht = thick * 0.5f;
                if (lenX > 0.0f) {
                    p->fillRect(QRectF(fcx - offset - lenX, fcy - ht, lenX, thick), clr);
                    p->fillRect(QRectF(fcx + offset + PVS,  fcy - ht, lenX, thick), clr);
                }
                if (lenY > 0.0f) {
                    p->fillRect(QRectF(fcx - ht, fcy + offset + PVS,  thick, lenY), clr);
                    if (!tSt)
                        p->fillRect(QRectF(fcx - ht, fcy - offset - lenY, thick, lenY), clr);
                }
            };

            p->setClipRect(QRectF(pvX + 1.0f, pvY + 1.0f, pvS - 2.0f, pvS - 2.0f));
            if (outerShow) {
                const float oLenX  = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthX") * PVS;
                const float oLenY  = cfg.GetInt("Metroid.Visual.CrosshairOuterLengthY") * PVS;
                const float oThick = std::max(1.0f, cfg.GetInt("Metroid.Visual.CrosshairOuterThickness") * PVS);
                const float oOfs   = cfg.GetInt("Metroid.Visual.CrosshairOuterOffset") * PVS;
                const float oOpac  = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairOuterOpacity"));
                drawArms(oLenX, oLenY, oOfs, oThick, tStyle,
                         QColor(cr, cg, cb2, static_cast<int>(oOpac * 255.0f)));
            }
            if (innerShow) {
                const float iLenX  = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthX") * PVS;
                const float iLenY  = cfg.GetInt("Metroid.Visual.CrosshairInnerLengthY") * PVS;
                const float iThick = std::max(1.0f, cfg.GetInt("Metroid.Visual.CrosshairInnerThickness") * PVS);
                const float iOfs   = cfg.GetInt("Metroid.Visual.CrosshairInnerOffset") * PVS;
                const float iOpac  = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairInnerOpacity"));
                drawArms(iLenX, iLenY, iOfs, iThick, tStyle,
                         QColor(cr, cg, cb2, static_cast<int>(iOpac * 255.0f)));
            }
            if (centerDot) {
                const float dotThick = std::max(1.0f, cfg.GetInt("Metroid.Visual.CrosshairDotThickness") * PVS);
                const float dotOpac = static_cast<float>(cfg.GetDouble("Metroid.Visual.CrosshairDotOpacity"));
                const float dh = dotThick * 0.5f;
                QColor dotColor(cr, cg, cb2, static_cast<int>(dotOpac * 255.0f));
                p->fillRect(QRectF(fcx - dh, fcy - dh, dotThick, dotThick), dotColor);
            }
            p->setClipping(false);

            p->setFont(smallFont);
            p->setPen(QColor(100, 100, 140));
            p->drawText(QRectF(pvX, pvY + pvS - kPropRowH, pvS, kPropRowH),
                        Qt::AlignCenter, QStringLiteral("preview"));
        }

        p->setFont(normalFont);
    }
}

