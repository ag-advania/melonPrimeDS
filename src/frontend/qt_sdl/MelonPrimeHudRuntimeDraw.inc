// This file is a unity-build fragment included by
// MelonPrimeHudRenderRuntime.inc.  Do not add it to CMakeLists.txt.

// =========================================================================
//  Runtime-sourced HUD drawing
//
//  These helpers consume resolved runtime snapshots and feed the existing
//  cached painter/image helpers.  They do not own visibility policy, radar
//  preprocessing, patch lifecycle, or generation state.
// =========================================================================

static void DrawMatchStatusText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, float tds,
                                const MatchStatusResolvedState& state, const MatchStatusHudConfig& c,
                                float overallOpacity = 1.0f, const HudOutlineConfig& ol = {false,QColor(),0.0f,1},
                                float hudScale = 1.0f)
{
    auto& s_curTextCache = HudFrameState().curText;
    auto& s_sepTextCache = HudFrameState().sepText;
    auto& s_goalTextCache = HudFrameState().goalText;
    auto& s_curBitmapCache = HudFrameState().curBitmap;
    auto& s_sepBitmapCache = HudFrameState().sepBitmap;
    auto& s_goalBitmapCache = HudFrameState().goalBitmap;
    auto& s_labelBitmapCache = HudFrameState().labelBitmap;
    auto& s_curOlCache = HudFrameState().curOutline;
    auto& s_sepOlCache = HudFrameState().sepOutline;
    auto& s_goalOlCache = HudFrameState().goalOutline;
    auto& s_labelOlCache = HudFrameState().labelOutline;
    auto& s_matchStringCache = HudFrameState().matchString;
    UpdateMatchStatusStrings(s_matchStringCache, state);
    // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.
    auto eff = [&](const QColor& sub) -> const QColor& { return sub.isValid() ? sub : c.matchStatusColor; };
    const bool useOutline = ol.enable && ol.opacity > 0.0f;
    const float renderScale = tds * hudScale;
    auto drawText = [&](TextBitmapCache& bmp, TextBitmapCache& olBmp, float x, float y) {
        if (useOutline) {
            PrepareOutlineBitmapCached(olBmp, bmp, ol.color, ol.thickness, renderScale);
            DrawCachedTextOutlined(p, bmp, olBmp, x, y, tds, overallOpacity, ol.opacity);
        } else {
            if (overallOpacity < 1.0f) p->setOpacity(overallOpacity);
            DrawCachedText(p, bmp, x, y, tds);
            p->setOpacity(1.0f);
        }
    };
    float curW = 0.0f, curH = 0.0f;
    MeasureTextCached(fm, fontPixelSize, s_curTextCache, s_matchStringCache.curBuf, curW, curH, tds);
    PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_curBitmapCache, s_matchStringCache.curBuf, eff(c.matchStatusValueColor));
    drawText(s_curBitmapCache, s_curOlCache, static_cast<float>(c.matchStatusX), static_cast<float>(c.matchStatusY));
    if (s_matchStringCache.hasGoal) {
        float sepW = 0.0f, sepH = 0.0f, goalW = 0.0f, goalH = 0.0f;
        MeasureTextCached(fm, fontPixelSize, s_sepTextCache, s_matchStringCache.sepBuf, sepW, sepH, tds);
        MeasureTextCached(fm, fontPixelSize, s_goalTextCache, s_matchStringCache.goalBuf, goalW, goalH, tds);
        float x = c.matchStatusX + curW;
        PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_sepBitmapCache, s_matchStringCache.sepBuf, eff(c.matchStatusSepColor));
        drawText(s_sepBitmapCache, s_sepOlCache, x, static_cast<float>(c.matchStatusY));
        x += sepW;
        PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_goalBitmapCache, s_matchStringCache.goalBuf, eff(c.matchStatusGoalColor));
        drawText(s_goalBitmapCache, s_goalOlCache, x, static_cast<float>(c.matchStatusY));
    }
    const char* label = ResolveMatchStatusLabel(state.mode, c);
    if (label[0] == '\0') return;
    float lx = static_cast<float>(c.matchStatusX), ly = static_cast<float>(c.matchStatusY);
    switch (c.matchStatusLabelPos) { default: case 0: ly -= 10; break; case 1: ly += 10; break; case 2: lx -= 50; break; case 3: lx += 50; break; case 4: break; }
    lx += c.matchStatusLabelOfsX; ly += c.matchStatusLabelOfsY;
    PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_labelBitmapCache, label, eff(c.matchStatusLabelColor));
    drawText(s_labelBitmapCache, s_labelOlCache, lx, ly);
}
// The resolved state is sampled once per emulated frame by the caller, which
// also derives this element's visual stamp from it. Drawing therefore reads no
// guest RAM, which is what lets the retained compositor re-run a draw purely to
// restore z-order without paying for another sample.
static void DrawMatchStatusResolved(QPainter* p,
                                    const MatchStatusResolvedState& state,
                                    const CachedHudConfig& c)
{
    DrawMatchStatusText(p, s_frameFm, s_frameFpx, c.textDrawScale,
                        state, c.matchStatus, c.matchStatusOpacity,
                        EffOL(c, c.matchStatus.outline), c.lastHudScale);
}
// Sample-and-draw wrapper for the callers that are not the per-frame HUD path:
// the on-screen editor preview and the developer golden harness. Both draw one
// isolated element outside the retained compositor, so paying for the read here
// is the simpler contract. The frame path resolves these values once during
// sampling and calls the *Resolved variants directly.
static void DrawMatchStatusHud(QPainter* p, const RomAddresses& rom,
                               uint8_t playerPos, bool isAdventure,
                               const CachedHudConfig& c,
                               const HudRuntimeState& st)
{
    if (!c.matchStatus.matchStatusShow || isAdventure)
        return;

    MatchStatusResolvedState state = {};
    if (!ReadHudRuntimeMatchStatusState(rom, playerPos, st, state))
        return;

    DrawMatchStatusResolved(p, state, c);
}

static float CalcAlignedTextX(int anchorX, int align, float textW);
static void DrawCachedAlignedText(QPainter* p, const QFontMetrics& fm, int fontPixelSize, float tds,
                                   TextMeasureCache& measureCache, TextBitmapCache& bitmapCache,
                                   TextBitmapCache& outlineCache,
                                   const char* text, const QColor& color,
                                   int anchorX, int align, int y,
                                   float opacity, const HudOutlineConfig& ol, float hudScale = 1.0f)
{
    float textW = 0.0f, textH = 0.0f;
    MeasureTextCached(fm, fontPixelSize, measureCache, text, textW, textH, tds);
    const float textX = CalcAlignedTextX(anchorX, align, textW);
    // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.
    PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, bitmapCache, text, color);
    if (ol.enable && ol.opacity > 0.0f) {
        const float renderScale = tds * hudScale;
        PrepareOutlineBitmapCached(outlineCache, bitmapCache, ol.color, ol.thickness, renderScale);
        DrawCachedTextOutlined(p, bitmapCache, outlineCache, textX, y, tds, opacity, ol.opacity);
    } else {
        if (opacity < 1.0f) p->setOpacity(opacity);
        DrawCachedText(p, bitmapCache, textX, y, tds);
        p->setOpacity(1.0f);
    }
}
// =========================================================================
//  Bomb Left HUD
// =========================================================================
static void DrawBombLeft(QPainter* p, uint8_t bombs,
                         const CachedHudConfig& c, float tds, float hudScale)
{
    if (!c.bombLeft.bombLeftShow) return;
    {
        auto& s_bombBitmapCache = HudFrameState().bombBitmap;
        auto& s_bombOutlineCache = HudFrameState().bombOutline;
        auto& s_bombMeasureCache = HudFrameState().bombMeasure;
        const QFontMetrics& fm = s_frameFm; const int fontPixelSize = s_frameFpx; // P-9
        // OPT-HUD-6: use s_frameFont (cached by EnsureHudFont) — no p->font() copy per-frame.
        char buf[64];
        if (c.bombLeft.bombLeftTextShow) std::snprintf(buf, sizeof(buf), "%s%u%s", c.bombLeft.bombLeftPrefix, bombs, c.bombLeft.bombLeftSuffix);
        else std::snprintf(buf, sizeof(buf), "%s%s", c.bombLeft.bombLeftPrefix, c.bombLeft.bombLeftSuffix);
        if (buf[0] != '\0') {
            float bombTextW = 0.0f, bombTextH = 0.0f;
            MeasureTextCached(fm, fontPixelSize, s_bombMeasureCache, buf, bombTextW, bombTextH, tds);
            PrepareTextBitmapCached(fm, s_frameFont, fontPixelSize, s_bombBitmapCache, buf, c.bombLeft.bombLeftColor);
            const float bombTextX = CalcAlignedTextX(c.bombLeft.bombLeftX, c.bombLeft.bombLeftAlign, bombTextW);
            { const HudOutlineConfig& _ol = EffOL(c, c.bombLeft.outline);
            if (_ol.enable && _ol.opacity > 0.0f) {
                const float renderScale = tds * hudScale;
                PrepareOutlineBitmapCached(s_bombOutlineCache, s_bombBitmapCache, _ol.color, _ol.thickness, renderScale);
                DrawCachedTextOutlined(p, s_bombBitmapCache, s_bombOutlineCache, bombTextX, c.bombLeft.bombLeftY, tds,
                                       c.bombLeftOpacity, _ol.opacity);
            } else {
                if (c.bombLeftOpacity < 1.0f) p->setOpacity(c.bombLeftOpacity);
                DrawCachedText(p, s_bombBitmapCache, bombTextX, c.bombLeft.bombLeftY, tds);
                p->setOpacity(1.0f);
            } }
        }
    }
    if (c.bombLeft.bombIconShow) {
        EnsureBombIconsLoaded(static_cast<int>(c.bombLeft.bombIconHeight * c.scaleIcons));
        const QImage& icon = GetBombIconForDraw(bombs, c.bombLeft.bombIconColorOverlay, c.bombLeft.bombIconColor);
        if (!icon.isNull()) {
            const float dw = icon.width() / hudScale; const float dh = icon.height() / hudScale;
            float ix = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftX + c.bombLeft.bombIconOfsX : c.bombLeft.bombIconPosX;
            float iy = (c.bombLeft.bombIconMode == 0) ? c.bombLeft.bombLeftY + c.bombLeft.bombIconOfsY : c.bombLeft.bombIconPosY;
            const int iconAlignX = c.bombLeft.bombIconAnchorX; const int iconAlignY = c.bombLeft.bombIconAnchorY;
            if (iconAlignX == 1) ix -= dw * 0.5f; else if (iconAlignX == 2) ix -= dw;
            if (iconAlignY == 1) iy -= dh * 0.5f; else if (iconAlignY == 2) iy -= dh;
            { const HudOutlineConfig& _ol = EffOL(c, c.bombLeft.iconOutline);
            if (_ol.enable && _ol.opacity > 0.0f) {
                const int expandR = std::max(1, _ol.thickness);
                EnsureBombOutlineIconsUpdated(_ol.color, expandR);
                const int bombIdx = (bombs >= 0 && bombs <= 3) ? bombs : 0;
                DrawImageOutlined(p, icon, s_bombIconsOutline[bombIdx],
                                  QRectF(ix, iy, dw, dh), (float)expandR / hudScale,
                                  c.bombIconOpacity, _ol.opacity);
            } else {
                const QRectF bombIconRect(ix, iy, dw, dh);
                if (c.bombIconOpacity < 1.0f) p->setOpacity(c.bombIconOpacity);
                p->drawImage(bombIconRect, icon);
                AccumDirtyDs(p, bombIconRect); // OPT-DR2
                p->setOpacity(1.0f);
            } }
        }
    }
}
//  Rank & Time HUD
// =========================================================================
// Same contract as DrawMatchStatusResolved: rank byte, remaining seconds and
// the time limit are sampled by the caller. `rankByte` above 3 means the ROM
// has no rank to show.
static void DrawRankAndTimeResolved(QPainter* p,
                                    const CachedHudConfig& c, float tds,
                                    uint8_t rankByte,
                                    int timeLeftSeconds,
                                    int timeLimitMinutes)
{
    const auto& hud = c.rankTime;

    if (hud.rankShow) {
        auto& s_rankStringCache = HudFrameState().rankString;
        auto& s_rankCache = HudFrameState().rankBitmap;
        auto& s_rankOutlineCache = HudFrameState().rankOutline;
        auto& s_rankMeasure = HudFrameState().rankMeasure;

        if (rankByte <= 3) {
            DrawCachedAlignedText(p, s_frameFm, s_frameFpx, tds,
                                  s_rankMeasure, s_rankCache, s_rankOutlineCache,
                                  UpdateRankString(s_rankStringCache, rankByte, hud.rankShowOrdinal, hud),
                                  hud.rankColor, hud.rankX, hud.rankAlign, hud.rankY,
                                  c.rankOpacity, EffOL(c, c.rankTime.rankOutline), c.lastHudScale);
        }
    }

    if (hud.timeLeftShow) {
        auto& s_timeLeftStringCache = HudFrameState().timeLeftString;
        auto& s_timeLeftCache = HudFrameState().timeLeftBitmap;
        auto& s_timeLeftOutlineCache = HudFrameState().timeLeftOutline;
        auto& s_timeLeftMeasure = HudFrameState().timeLeftMeasure;

        DrawCachedAlignedText(p, s_frameFm, s_frameFpx, tds,
                              s_timeLeftMeasure, s_timeLeftCache, s_timeLeftOutlineCache,
                              UpdateTimeString(s_timeLeftStringCache, timeLeftSeconds, false),
                              hud.timeLeftColor, hud.timeLeftX, hud.timeLeftAlign, hud.timeLeftY,
                              c.timeLeftOpacity, EffOL(c, c.rankTime.timeLeftOutline), c.lastHudScale);
    }

    if (hud.timeLimitShow) {
        auto& s_timeLimitStringCache = HudFrameState().timeLimitString;
        auto& s_timeLimitCache = HudFrameState().timeLimitBitmap;
        auto& s_timeLimitOutlineCache = HudFrameState().timeLimitOutline;
        auto& s_timeLimitMeasure = HudFrameState().timeLimitMeasure;

        DrawCachedAlignedText(p, s_frameFm, s_frameFpx, tds,
                              s_timeLimitMeasure, s_timeLimitCache, s_timeLimitOutlineCache,
                              UpdateTimeString(s_timeLimitStringCache, timeLimitMinutes, true),
                              hud.timeLimitColor, hud.timeLimitX, hud.timeLimitAlign, hud.timeLimitY,
                              c.timeLimitOpacity, EffOL(c, c.rankTime.timeLimitOutline), c.lastHudScale);
    }
}

// Companion to DrawMatchStatusHud above: same non-frame callers, same reason.
static void DrawRankAndTime(QPainter* p, const RomAddresses& rom,
                            uint8_t playerPos, bool isAdventure,
                            const CachedHudConfig& c, float tds,
                            const HudRuntimeState& st)
{
    if (isAdventure)
        return;

    const auto& hud = c.rankTime;
    const uint8_t rankByte = hud.rankShow
        ? ReadHudRuntimeRankByte(rom, playerPos, st) : uint8_t{0xFF};
    const int timeLeftSeconds = hud.timeLeftShow
        ? ReadHudRuntimeTimeLeftSeconds(rom, st) : 0;
    const int timeLimitMinutes = hud.timeLimitShow
        ? ReadHudRuntimeTimeLimitMinutes(rom, st) : 0;
    DrawRankAndTimeResolved(p, c, tds, rankByte, timeLeftSeconds, timeLimitMinutes);
}
