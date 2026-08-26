// This file is a unity-build fragment included by MelonPrimeHudRender.cpp.
// Do not add it to CMakeLists.txt.

// =========================================================================
//  Custom HUD runtime-sourced drawing and lifecycle boundaries
//
//  What is left once sampling (MelonPrimeHudRuntimeSample.inc) and render
//  planning (MelonPrimeHudRenderPlan.inc) have been separated out: the draw
//  helpers for runtime-sourced elements (match status, bomb count, rank and
//  time), the shared HUD visibility rules, radar colour keying, the native
//  HUD patch lifecycle, and the config/visual generation counters.
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
static void DrawMatchStatusHud(QPainter* p, melonDS::u8* ram, const RomAddresses& rom,
                               uint8_t playerPos, bool isAdventure,
                               const CachedHudConfig& c,
                               const HudRuntimeState* st = nullptr)
{
    if (!c.matchStatus.matchStatusShow || isAdventure)
        return;

    MatchStatusResolvedState state = {};
    const bool hasState = st
        ? ReadHudRuntimeMatchStatusState(rom, playerPos, *st, state)
        : ComputeMatchStatusState(ram, rom, playerPos, state);
    if (!hasState)
        return;

    DrawMatchStatusText(p, s_frameFm, s_frameFpx, c.textDrawScale,
                        state, c.matchStatus, c.matchStatusOpacity,
                        EffOL(c, c.matchStatus.outline), c.lastHudScale);
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
static void DrawRankAndTime(QPainter* p, melonDS::u8* ram, const RomAddresses& rom,
                            uint8_t playerPos, bool isAdventure,
                            const CachedHudConfig& c, float tds,
                            const HudRuntimeState* st = nullptr)
{
    if (isAdventure)
        return;

    const auto& hud = c.rankTime;

    if (hud.rankShow) {
        auto& s_rankStringCache = HudFrameState().rankString;
        auto& s_rankCache = HudFrameState().rankBitmap;
        auto& s_rankOutlineCache = HudFrameState().rankOutline;
        auto& s_rankMeasure = HudFrameState().rankMeasure;

        const uint8_t rankByte = st
            ? ReadHudRuntimeRankByte(rom, playerPos, *st)
            : static_cast<uint8_t>((Read32(ram, rom.matchRank) >> (playerPos * 8)) & 0xFF);
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

        const int seconds = st
            ? ReadHudRuntimeTimeLeftSeconds(rom, *st)
            : static_cast<int>(Read32(ram, rom.timeLeft)) / 60;
        DrawCachedAlignedText(p, s_frameFm, s_frameFpx, tds,
                              s_timeLeftMeasure, s_timeLeftCache, s_timeLeftOutlineCache,
                              UpdateTimeString(s_timeLeftStringCache, seconds, false),
                              hud.timeLeftColor, hud.timeLeftX, hud.timeLeftAlign, hud.timeLeftY,
                              c.timeLeftOpacity, EffOL(c, c.rankTime.timeLeftOutline), c.lastHudScale);
    }

    if (hud.timeLimitShow) {
        auto& s_timeLimitStringCache = HudFrameState().timeLimitString;
        auto& s_timeLimitCache = HudFrameState().timeLimitBitmap;
        auto& s_timeLimitOutlineCache = HudFrameState().timeLimitOutline;
        auto& s_timeLimitMeasure = HudFrameState().timeLimitMeasure;

        const int goalMinutes = st
            ? ReadHudRuntimeTimeLimitMinutes(rom, *st)
            : (s_battleState.valid
                ? s_battleState.timeLimitMinutes
                : LookupTimeLimitMin((Read32(ram, rom.battleSettings + 4) >> 8) & 0xFF));
        DrawCachedAlignedText(p, s_frameFm, s_frameFpx, tds,
                              s_timeLimitMeasure, s_timeLimitCache, s_timeLimitOutlineCache,
                              UpdateTimeString(s_timeLimitStringCache, goalMinutes, true),
                              hud.timeLimitColor, hud.timeLimitX, hud.timeLimitAlign, hud.timeLimitY,
                              c.timeLimitOpacity, EffOL(c, c.rankTime.timeLimitOutline), c.lastHudScale);
    }
}
// =========================================================================
//  Config key (only for IsEnabled — hot path uses s_cache)
// =========================================================================
static constexpr const char* kCfgCustomHud = MP_HUD_PROP_KEY_CustomHUD;
bool CustomHud_IsEnabled(Config::Table& localCfg)
{
    // Dormant diagnostic override used by the real-ROM raster differential.
    // Cache the process environment once so the normal per-frame HUD query
    // remains a single predictable branch plus the existing config lookup.
    static const bool diagnosticForcedOff = [] {
        const char* value = std::getenv("MELONPRIME_TEST_CUSTOM_HUD_OFF");
        return value && value[0] != '\0' && std::strcmp(value, "0") != 0;
    }();
    if (diagnosticForcedOff)
        return false;
    return localCfg.GetBool(kCfgCustomHud);
}

// States in which the game shows its own UI over the gameplay view. Custom HUD
// hides, and the native helmet layers are handed back (see the NOHUD_HELMET
// strip at the call sites).
static inline bool ShouldHideForGameplayState(bool isStartPressed,
                                             uint16_t currentHP,
                                             bool isGameOver,
                                             bool isAdventure,
                                             bool isMapOrUserActionPaused)
{
    return isStartPressed ||
           (isAdventure && isMapOrUserActionPaused) ||
           currentHP == 0 ||
           isGameOver;
}

// Single decision point for the Custom HUD crosshair. The whole-overlay hide
// rules (native UI, adventure camera scenes) return before this is consulted;
// what remains are the states where the rest of the HUD keeps drawing but the
// game has no reticle of its own: third person, alt form, morph transition, and
// an active Scan Visor.
static inline bool ShouldDrawCustomHudCrosshair(const HudRuntimeState& st)
{
    return st.isFirstPerson && !st.isTrans && !st.isAlt && !st.isAdventureScanVisor;
}

bool CustomHud_ShouldHideForGameplayState(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition)
{
    if (!emu) return true;

    melonDS::NDS* nds = emu->getNDS();
    melonDS::u8* ram = nds ? nds->MainRAM : nullptr;
    if (!ram) return true;

    const uint32_t offP = static_cast<uint32_t>(playerPosition) * Consts::PLAYER_ADDR_INC;
    const bool isStartPressed = Read8(ram, rom.startPressed) == 0x01;
    const uint16_t currentHP = Read16(ram, rom.playerHP + offP);
    const bool isGameOver = Read8(ram, rom.gameOver) != 0x00;
    if (isStartPressed || currentHP == 0 || isGameOver)
        return true;

    const bool isAdventure = Read8(ram, rom.isInAdventure) == 0x02;
    if (!isAdventure)
        return false;

    if (Read8(ram, rom.isMapOrUserActionPaused) == 0x01)
        return true;

    return IsCameraSequenceBlockingInput(ram, rom);
}

bool CustomHud_ShouldDrawRadarOverlay(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition)
{
    return !CustomHud_ShouldHideForGameplayState(emu, rom, playerPosition);
}

static bool CustomHud_IsRadarPaletteColor(uint32_t rgb)
{
    const uint32_t quantizedRgb = rgb & kRadarPaletteQuantizationMask;
    for (const uint32_t paletteColor : kRadarPaletteColors) {
        if (quantizedRgb == paletteColor)
            return true;
    }
    return false;
}

int CustomHud_ResolveRadarColorKeyRadius(Config::Table& localCfg)
{
    if (!localCfg.GetBool(MP_HUD_PROP_KEY_BtmOverlayEnable))
        return 0;
    return localCfg.GetInt(MP_HUD_PROP_KEY_BtmOverlaySrcRadius);
}

QImage* CustomHud_PrepareRadarColorKeySource(
    const QImage* bottomScreen,
    QImage* scratch,
    uint8_t hunterID,
    int sourceRadius)
{
    if (!bottomScreen || bottomScreen->isNull() || bottomScreen->depth() != 32
        || !scratch || sourceRadius <= 0)
        return nullptr;

    const int width = bottomScreen->width();
    const int height = bottomScreen->height();
    if (scratch->width() != width
        || scratch->height() != height
        || scratch->format() != QImage::Format_ARGB32_Premultiplied)
    {
        *scratch = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
        scratch->fill(Qt::transparent);
    }

    const int safeHunterID = std::min<int>(hunterID, kHunterCount - 1);
    const float scaleX = static_cast<float>(width) / 256.0f;
    const float scaleY = static_cast<float>(height) / 192.0f;
    const QRect sourceRect(
        static_cast<int>((kBtmOverlaySrcCenterX - sourceRadius) * scaleX),
        static_cast<int>((kBtmOverlaySrcCenterY[safeHunterID] - sourceRadius) * scaleY),
        static_cast<int>(sourceRadius * 2 * scaleX),
        static_cast<int>(sourceRadius * 2 * scaleY));
    const QRect clippedRect = sourceRect.intersected(bottomScreen->rect());
    if (clippedRect.isEmpty())
        return nullptr;

    for (int y = clippedRect.top(); y <= clippedRect.bottom(); ++y) {
        const auto* sourceLine = reinterpret_cast<const QRgb*>(bottomScreen->constScanLine(y));
        auto* destinationLine = reinterpret_cast<QRgb*>(scratch->scanLine(y));
        for (int x = clippedRect.left(); x <= clippedRect.right(); ++x) {
            const uint32_t rgb = sourceLine[x] & 0x00FFFFFFu;
            destinationLine[x] = CustomHud_IsRadarPaletteColor(rgb)
                ? static_cast<QRgb>(0xFF000000u | rgb)
                : static_cast<QRgb>(0u);
        }
    }
    return scratch;
}

bool CustomHud_IsHelmetLayerHideConfigured(Config::Table& localCfg)
{
    return localCfg.GetBool(kCfgCustomHud)
        && localCfg.GetBool(MP_HUD_PROP_KEY_DisableDefaultHud_Helmet);
}
// One-shot guard: when CustomHUD transitions to disabled, perform a full
// ForceRestore on the next in-game frame regardless of s_hudPatchApplied,
// so any out-of-sync RAM state (savestate / persistent RAM / JIT corner
// cases) is normalized exactly once. Reset to true when CustomHUD becomes
// enabled or on emu start/stop, so a future disable triggers it again.

static void CustomHud_MarkNoHudPatchDesired()
{
    s_hudPatchForceRestorePending = true;
}

static void CustomHud_RestoreNativeHudState(const RomAddresses& /*rom*/,
                                            const HudRuntimeState& st)
{
    const bool needForceRestore = s_hudPatchForceRestorePending;
    auto& patch = ActiveHudConfigState().noHudPatch;
    if (NoHudPatch_GetAppliedMask(patch) == 0 && !needForceRestore)
        return;

    if (needForceRestore) {
        NoHudPatch_RestoreAll(patch, st.nds, st.romGroup);
        s_hudPatchForceRestorePending = false;
    } else {
        NoHudPatch_Sync(patch, st.nds, st.romGroup, 0);
    }
}

void CustomHud_ClampHelmetLayersPreFrame(
    CustomHudConfigState& hudConfig,
    EmuInstance* emu,
    const RomAddresses& rom,
    uint8_t playerPosition)
{
    const ScopedHudConfigState active(hudConfig);
    // Gate on the tracker, not the config: the helmet bit is only ever applied
    // by Render() while CustomHUD is enabled, and this avoids a per-frame toml
    // lookup on the hot path.
    auto& patch = ActiveHudConfigState().noHudPatch;
    const uint16_t appliedMask = NoHudPatch_GetAppliedMask(patch);
    if (!(appliedMask & (1u << NOHUD_HELMET)))
        return;

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return;

    // Restore the native helmet instruction before native UI is rendered.
    // Vulkan applies its stronger guarantee presentation-only in the scanline
    // renderer, without changing the emulated register state.
    if (st.isStartPressed || st.isGameOver || st.currentHP == 0)
    {
        NoHudPatch_Sync(
            patch,
            st.nds,
            st.romGroup,
            static_cast<uint16_t>(appliedMask & ~(1u << NOHUD_HELMET)));
        return;
    }
    ReadHudRuntimeAdventurePauseState(rom, st);
    if (st.isAdventure && st.isMapOrUserActionPaused)
    {
        NoHudPatch_Sync(
            patch,
            st.nds,
            st.romGroup,
            static_cast<uint16_t>(appliedMask & ~(1u << NOHUD_HELMET)));
        return;
    }

    NoHudPatch_ClampHelmetLayers(st.nds, st.romGroup);
}

void CustomHud_EnsurePatchRestored(
    CustomHudConfigState& hudConfig,
    EmuInstance* emu,
    Config::Table& localCfg,
    const RomAddresses& rom,
    uint8_t playerPosition,
    bool isInGame)
{
    const ScopedHudConfigState active(hudConfig);
    if (isInGame && CustomHud_IsEnabled(localCfg)) {
        // HUD is enabled — Render() handles patches. Re-arm the force-restore
        // for the next disable event.
        CustomHud_MarkNoHudPatchDesired();
        return;
    }

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return;

    CustomHud_RestoreNativeHudState(rom, st);
}

void CustomHud_ResetPatchState(CustomHudConfigState& hudConfig)
{
    const ScopedHudConfigState active(hudConfig);
    BumpCustomHudVisualGeneration();
    NoHudPatch_ResetState(ActiveHudConfigState().noHudPatch);
    s_cache.valid = false;
    s_battleState.valid = false;
    s_scoreboardMatchCache = {};
    s_hudRuntimeFrameCache.scoreboardValid = false;
    s_hudRuntimeFrameCache.scoreboardMatchSerial = 0;
    s_hudRuntimeFrameCache.scoreboard = {};
    s_hudRuntimeFrameCache.enemyTargetValid = false;
    s_hudRuntimeFrameCache.enemyTarget = {};
    s_chPrevPxCx = INT_MIN;
    s_chPrevPxCy = INT_MIN;
    ResetCrosshairZoomDisplay(0.0f);
    s_hudPatchForceRestorePending = true;
}

void CustomHud_ReconcilePatchAfterSavestateLoad(
    CustomHudConfigState& hudConfig,
    EmuInstance* emu,
    Config::Table& localCfg,
    const RomAddresses& rom,
    uint8_t playerPosition)
{
    const ScopedHudConfigState active(hudConfig);
    BumpCustomHudVisualGeneration();
    auto& patch = ActiveHudConfigState().noHudPatch;

    // Do not trust the normal applied-mask tracker here. A loaded state can
    // contain either original or patched ARM9 HUD instructions, regardless of
    // the CustomHUD setting that is active now.
    NoHudPatch_InvalidateState(patch);
    s_hudPatchForceRestorePending = true;

    HudRuntimeState st = {};
    if (!ReadHudRuntimeBaseState(emu, rom, playerPosition, st))
        return;

    if (!CustomHud_IsEnabled(localCfg)) {
        CustomHud_RestoreNativeHudState(rom, st);
        return;
    }

    if (!st.isStartPressed && st.currentHP != 0 && !st.isGameOver)
        ReadHudRuntimeAdventurePauseState(rom, st);

    uint16_t desiredMask = LoadNoHudMaskFromConfig(localCfg);
    if (ShouldHideForGameplayState(
            st.isStartPressed,
            st.currentHP,
            st.isGameOver,
            st.isAdventure,
            st.isMapOrUserActionPaused))
    {
        desiredMask &= static_cast<uint16_t>(~(1u << NOHUD_HELMET));
    }

    // Perform the full reconciliation before msg_LoadState returns. Waiting
    // for the next presentation callback would fix the persistent mismatch,
    // but could still expose one native-HUD frame immediately after F8.
    NoHudPatch_Sync(patch, st.nds, st.romGroup, desiredMask);
    CustomHud_MarkNoHudPatchDesired();
}

// P-3: Called from settings dialog save to trigger config re-read next frame
static void CustomHud_InvalidateConfigCache()
{
    s_cache.valid = false;
    s_bombTintCacheValid = false;
    for (int i = 0; i < 9; i++) s_weaponTintValid[i] = false;
    s_effectiveRadarHunterID = 0xFF; // P-13: force re-evaluation of effective radar color
    // Force EnsureHudFont() to re-read the painter font next frame; Screen.cpp rebuilds
    // overlayFont from CustomHud_ResolveBaseFont when the HUD-font setting changes, and the
    // rebuild bumps s_textCacheGen so stale glyph bitmaps are re-rasterised.
    s_frameFpx = 0;
    // Bump epoch immediately so Screen.cpp's m_hudEnabled cache refreshes on the
    // next frame. Without this, when CustomHUD is toggled off, the stale cache
    // keeps Render() running for one extra frame and re-applies NoHudPatch.
    if (++s_cacheEpoch == 0) s_cacheEpoch = 1;
}

void CustomHud_InvalidateConfigCache(CustomHudConfigState& hudConfig)
{
    const ScopedHudConfigState active(hudConfig);
    CustomHud_InvalidateConfigCache();
}

uint32_t CustomHud_GetCacheEpoch(const CustomHudConfigState& hudConfig)
{
    return hudConfig.cacheEpoch;
}

uint32_t CustomHud_GetVisualGeneration(const CustomHudConfigState& hudConfig)
{
    return hudConfig.visualGeneration;
}

uint32_t CustomHud_GetVisualGameFrame(EmuInstance* emu,
                                      const void** ndsIdentity)
{
    melonDS::NDS* nds = emu ? emu->getNDS() : nullptr;
    if (ndsIdentity)
        *ndsIdentity = nds;
    return nds ? nds->NumFrames : 0;
}
