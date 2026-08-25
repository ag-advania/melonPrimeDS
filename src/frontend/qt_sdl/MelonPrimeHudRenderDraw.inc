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

struct ScoreboardMetricSpec {
    const char* firstLabel;
    const char* secondLabel;
    bool firstIsTime;
    bool secondIsTime;
    bool secondIsKills;
};

static ScoreboardMetricSpec ResolveScoreboardMetric(uint8_t mode)
{
    switch (static_cast<ScoreboardGameMode>(mode)) {
    case ScoreboardGameMode::Battle:
    case ScoreboardGameMode::BattleTeams:
        return {"Points", "Deaths", false, false, false};
    case ScoreboardGameMode::Survival:
    case ScoreboardGameMode::SurvivalTeams:
        return {"Time", "Deaths", true, false, false};
    case ScoreboardGameMode::Capture:
    case ScoreboardGameMode::Bounty:
    case ScoreboardGameMode::BountyTeams:
        return {"Octoliths", "Kills", false, false, true};
    case ScoreboardGameMode::Nodes:
    case ScoreboardGameMode::NodesTeams:
        return {"Points", "Kills", false, false, true};
    case ScoreboardGameMode::Defender:
    case ScoreboardGameMode::DefenderTeams:
    case ScoreboardGameMode::PrimeHunter:
        return {"Time", "Kills", true, false, true};
    default:
        return {"", "", false, false, false};
    }
}

static bool IsScoreboardSurvivalMode(uint8_t gameMode)
{
    return gameMode == static_cast<uint8_t>(ScoreboardGameMode::Survival)
        || gameMode == static_cast<uint8_t>(ScoreboardGameMode::SurvivalTeams);
}

static ScoreboardVisualValue MakeScoreboardVisualValue(
    int32_t rawValue, bool isTime, uint8_t gameMode)
{
    ScoreboardVisualValue visual;
    visual.valid = true;
    visual.isTime = isTime;
    if (!isTime) {
        visual.value = rawValue;
        return visual;
    }

    // Match runtime time is 60 ticks/sec. Only Survival uses -1 as the
    // game's unlimited/MAX sentinel; other negative values clamp to zero.
    if (IsScoreboardSurvivalMode(gameMode) && rawValue == -1) {
        visual.isMax = true;
        return visual;
    }
    visual.value = std::max<int32_t>(0, rawValue) / 60;
    return visual;
}

static QString FormatScoreboardVisualValue(const ScoreboardVisualValue& visual)
{
    if (!visual.valid)
        return {};
    if (visual.isTime && visual.isMax)
        return UiText::Tr("MAX");
    if (!visual.isTime)
        return QString::number(visual.value);
    const int totalSeconds = std::max(0, visual.value);
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QChar('0'));
}

static QString FormatScoreboardTime(int32_t rawTicks, uint8_t gameMode)
{
    return FormatScoreboardVisualValue(
        MakeScoreboardVisualValue(rawTicks, true, gameMode));
}

static QString FormatScoreboardMetric(int32_t value, bool isTime, uint8_t gameMode)
{
    return FormatScoreboardVisualValue(
        MakeScoreboardVisualValue(value, isTime, gameMode));
}

static const QColor& ResolveScoreboardTeamColor(const CachedHudConfig& c,
                                                uint8_t teamIndex)
{
    // Native scoreboard colors are BGR555: TeamIndex 0 = 0x023F (Orange),
    // TeamIndex 1 = 0x2BEA (Green). The configurable Team Row Color remains
    // a single-color override when it differs from the native Team 1 default.
    static const QColor nativeTeam1(
        (0x023Fu & 0x1Fu) << 3,
        ((0x023Fu >> 5) & 0x1Fu) << 3,
        ((0x023Fu >> 10) & 0x1Fu) << 3);
    static const QColor nativeTeam2(
        (0x2BEAu & 0x1Fu) << 3,
        ((0x2BEAu >> 5) & 0x1Fu) << 3,
        ((0x2BEAu >> 10) & 0x1Fu) << 3);
    if (c.scoreboard.teamRowColor == nativeTeam1)
        return teamIndex == 1 ? nativeTeam2 : nativeTeam1;
    return c.scoreboard.teamRowColor;
}

static QString ScoreboardElide(const QString& text, const QFontMetrics& fm, int width)
{
    return fm.elidedText(text, Qt::ElideRight, std::max(1, width));
}

static QRectF ScoreboardRankLine(const QRectF& rect)
{
    const qreal nameHeight = rect.height() * 0.58;
    return QRectF(rect.x(), rect.y() + nameHeight,
                  rect.width(), rect.height() - nameHeight);
}

static QString FormatScoreboardRankLabel(int rank)
{
    return QStringLiteral("%1 %2").arg(UiText::Tr("Rank")).arg(rank);
}

struct ScoreboardRenderRow {
    bool team = false;
    uint8_t slot = 0xFF;
    uint8_t teamIndex = 0xFF;
};

static int ResolveScoreboardDisplayedRank(const ScoreboardSnapshot& snapshot,
                                          bool teamRow, uint8_t slot)
{
    if (slot >= 4)
        return 1;
    const ScoreboardPlayerSnapshot& player = snapshot.playersBySlot[slot];
    if (teamRow)
        return static_cast<int>(player.standing) + 1;
    return static_cast<int>(snapshot.teams
        ? player.teamStanding : player.standing) + 1;
}

struct ScoreboardLayout {
    int panelW = 1;
    int panelH = 0;
    int rowH = 12;
    int teamRowH = 12;
    int rowSpacing = 0;
    int columnSpacing = 0;
    int rightPadding = 0;
    int portraitW = 20;
    int playerW = 1;
    int firstW = 20;
    int secondW = 20;
};

static int BuildScoreboardRenderRows(const ScoreboardSnapshot& snapshot,
                                     std::array<ScoreboardRenderRow, 8>& rows)
{
    int rowCount = 0;
    int previousTeam = -1;
    const int displayCount = std::min<int>(snapshot.activePlayers, 4);
    for (int displayIndex = 0; displayIndex < displayCount; ++displayIndex) {
        // ResultSlots is authoritative display order. It is deliberately not
        // sorted here; slot and displayIndex are separate concepts.
        const uint8_t slot = snapshot.resultSlots[displayIndex];
        if (slot >= 4 || rowCount >= static_cast<int>(rows.size()))
            continue;
        const ScoreboardPlayerSnapshot& player = snapshot.playersBySlot[slot];
        if (!player.active)
            continue;
        if (snapshot.teams && player.teamIndex <= 1) {
            if (previousTeam != player.teamIndex && rowCount < static_cast<int>(rows.size())) {
                rows[rowCount++] = {true, slot, player.teamIndex};
                previousTeam = player.teamIndex;
            }
        }
        if (rowCount < static_cast<int>(rows.size()))
            rows[rowCount++] = {false, slot, player.teamIndex};
    }
    return rowCount;
}

static float ScoreboardUserScale(const CachedHudConfig& c)
{
    return std::clamp(c.scoreboard.scale, 0.1f, 8.0f);
}

static float ScoreboardTextDrawScale(const CachedHudConfig& c, float hudScale)
{
    // Scoreboard text keeps the shared resolution-aware automatic scale. The
    // scoreboard-specific Text Scale % is an always-available multiplier on
    // top of that result; 100% preserves the existing automatic size.
    const float boardAutoTextScale = std::min(c.scaleText, c.scaleScoreboard);
    const float sharedTextScale = (boardAutoTextScale * 100.0f
                                   + (c.textScalePct - 100)) / 100.0f;
    const float scoreboardTextScale = std::clamp(
        c.scoreboard.textScalePct, 10, 800) / 100.0f;
    const float requestedTextScale = sharedTextScale * scoreboardTextScale;
    const float logicalTextScale = hudScale > 0.0f
        ? requestedTextScale / hudScale : requestedTextScale;
    return logicalTextScale * ScoreboardUserScale(c);
}

static void BuildScoreboardFonts(const CachedHudConfig& c, float hudScale,
                                 QFont& boardFont, QFont& rankFont)
{
    boardFont = s_frameFont;
    int boardFontPx = std::max(1, static_cast<int>(std::lround(
        s_frameFpx * std::max(0.1f, ScoreboardTextDrawScale(c, hudScale)))));
    boardFont.setPixelSize(boardFontPx);
    rankFont = boardFont;
    int rankFontPx = std::max(1, static_cast<int>(std::lround(boardFontPx * 0.72)));
    rankFont.setPixelSize(rankFontPx);
}

static void EnsureScoreboardFontCache(const CachedHudConfig& c, float hudScale)
{
    auto& cache = HudFrameState().scoreboardFont;
    if (cache.valid
        && cache.configEpoch == s_cacheEpoch
        && cache.baseFontGeneration == s_textCacheGen
        && cache.baseFontPx == s_frameFpx
        && cache.hudScale == hudScale) {
        return;
    }

    BuildScoreboardFonts(c, hudScale, cache.boardFont, cache.rankFont);
    cache.boardFm = QFontMetrics(cache.boardFont);
    cache.rankFm = QFontMetrics(cache.rankFont);
    cache.configEpoch = s_cacheEpoch;
    cache.baseFontGeneration = s_textCacheGen;
    cache.baseFontPx = s_frameFpx;
    cache.hudScale = hudScale;
    cache.valid = true;
}

static int ScoreboardImageWidthDS(const QImage& image, float invHudScale)
{
    return image.isNull() ? 0
        : std::max(0, static_cast<int>(std::ceil(image.width() * invHudScale)));
}

static int ScoreboardAutoRowHeight(const QFontMetrics& boardFm,
                                   const QFontMetrics& rankFm,
                                   int headerInset, int assetHeightDS)
{
    // Each player cell contains two lines. Derive the row height from the
    // actual fonts/assets instead of scaling a user-entered fixed height;
    // otherwise a small scoreboard scale clips the name/rank pair.
    const int nameLineHeight = boardFm.height() + 1;
    const int rankLineHeight = rankFm.height() + 1;
    const int nameSplitHeight = static_cast<int>(std::ceil(
        static_cast<float>(nameLineHeight) / 0.58f));
    const int rankSplitHeight = static_cast<int>(std::ceil(
        static_cast<float>(rankLineHeight) / 0.42f));
    const int headerHeight = boardFm.height() + headerInset * 2 + 1;
    int rowHeight = std::max(nameSplitHeight, rankSplitHeight);
    rowHeight = std::max(rowHeight, headerHeight);
    rowHeight = std::max(rowHeight, assetHeightDS);
    return std::max(1, rowHeight);
}

static int ScoreboardTeamRowHeight(const QFontMetrics& boardFm,
                                   const QFontMetrics& rankFm,
                                   int headerInset, bool rankShow)
{
    const int headerHeight = boardFm.height() + headerInset * 2 + 1;
    if (!rankShow)
        return std::max(1, headerHeight);

    const int nameLineHeight = boardFm.height() + 1;
    const int rankLineHeight = rankFm.height() + 1;
    const int nameSplitHeight = static_cast<int>(std::ceil(
        static_cast<float>(nameLineHeight) / 0.58f));
    const int rankSplitHeight = static_cast<int>(std::ceil(
        static_cast<float>(rankLineHeight) / 0.42f));
    int rowHeight = std::max(nameSplitHeight, rankSplitHeight);
    return std::max(rowHeight, headerHeight);
}

static ScoreboardLayout BuildScoreboardLayout(
    const ScoreboardSnapshot& snapshot,
    const std::array<ScoreboardRenderRow, 8>& rows, int rowCount,
    const CachedHudConfig& c, float hudScale,
    const QFontMetrics& boardFm, const QFontMetrics& rankFm)
{
    ScoreboardLayout layout;
    const float boardScale = ScoreboardUserScale(c);
    const auto scaleDs = [boardScale](int value) {
        return std::max(0, static_cast<int>(std::lround(value * boardScale)));
    };
    const float invHudScale = hudScale > 0.0f ? 1.0f / hudScale : 1.0f;
    const int headerInset = std::max(1, scaleDs(1));
    const int assetHeightDS = std::max(0, static_cast<int>(std::ceil(
        std::max(s_scoreboardHunterIconHeight, s_scoreboardStarsHeight)
        * invHudScale)));
    // Preserve the established default breathing room while allowing the
    // font/asset minimum to win whenever the user scales the board down.
    layout.rowH = std::max(ScoreboardAutoRowHeight(
        boardFm, rankFm, headerInset, assetHeightDS), scaleDs(22));
    layout.teamRowH = ScoreboardTeamRowHeight(
        boardFm, rankFm, headerInset, c.scoreboard.rankShow);
    layout.rowSpacing = scaleDs(std::max(0, c.scoreboard.rowSpacing));
    layout.columnSpacing = scaleDs(std::clamp(c.scoreboard.columnSpacing, 0, 32));
    layout.rightPadding = scaleDs(std::clamp(c.scoreboard.rightPadding, 0, 32));
    layout.panelH = layout.rowH + scaleDs(4);
    for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
        layout.panelH += rows[rowIndex].team ? layout.teamRowH : layout.rowH;
        layout.panelH += layout.rowSpacing;
    }

    layout.portraitW = std::max(scaleDs(20),
                                scaleDs(c.scoreboard.hunterIconHeight) + scaleDs(4));
    int playerContentW = boardFm.horizontalAdvance(UiText::Tr("Player"));

    const ScoreboardMetricSpec metric = ResolveScoreboardMetric(snapshot.gameMode);
    int firstContentW = boardFm.horizontalAdvance(UiText::Tr(metric.firstLabel));
    int secondContentW = boardFm.horizontalAdvance(UiText::Tr(metric.secondLabel));

    for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
        const ScoreboardRenderRow& row = rows[rowIndex];
        if (row.team) {
            const ScoreboardTeamSnapshot& team = snapshot.teamsByIndex[row.teamIndex];
            const QString teamName = QStringLiteral("%1 %2").arg(UiText::Tr("Team"))
                .arg(static_cast<int>(row.teamIndex) + 1);
            const int teamRank = ResolveScoreboardDisplayedRank(
                snapshot, true, row.slot);
            playerContentW = std::max(playerContentW, boardFm.horizontalAdvance(teamName));
            if (c.scoreboard.rankShow)
                playerContentW = std::max(playerContentW,
                                           rankFm.horizontalAdvance(FormatScoreboardRankLabel(teamRank)));
            firstContentW = std::max(firstContentW,
                boardFm.horizontalAdvance(FormatScoreboardMetric(
                    metric.firstIsTime ? team.time : team.points,
                    metric.firstIsTime, snapshot.gameMode)));
            secondContentW = std::max(secondContentW,
                boardFm.horizontalAdvance(FormatScoreboardMetric(
                    metric.secondIsTime ? team.time
                        : metric.secondIsKills ? team.kills : team.deaths,
                    metric.secondIsTime, snapshot.gameMode)));
            continue;
        }

        const ScoreboardPlayerSnapshot& player = snapshot.playersBySlot[row.slot];
        const int rank = ResolveScoreboardDisplayedRank(
            snapshot, false, row.slot);
        playerContentW = std::max(playerContentW, boardFm.horizontalAdvance(player.displayName));
        int rankLineW = c.scoreboard.rankShow
            ? rankFm.horizontalAdvance(FormatScoreboardRankLabel(rank)) : 0;
        if (player.stars <= 4) {
            const QImage& stars = s_scoreboardStars[player.stars];
            rankLineW += stars.isNull() ? 0
                : scaleDs(3) + ScoreboardImageWidthDS(stars, invHudScale);
        }
        playerContentW = std::max(playerContentW, rankLineW);
        const QImage* hunter = player.hunterId < 7
            ? &s_scoreboardHunterIcons[player.hunterId] : nullptr;
        if (hunter && !hunter->isNull())
            layout.portraitW = std::max(layout.portraitW,
                                        scaleDs(2) + ScoreboardImageWidthDS(*hunter, invHudScale));

        firstContentW = std::max(firstContentW,
            boardFm.horizontalAdvance(FormatScoreboardMetric(
                metric.firstIsTime ? player.time : player.points,
                metric.firstIsTime, snapshot.gameMode)));
        secondContentW = std::max(secondContentW,
            boardFm.horizontalAdvance(FormatScoreboardMetric(
                metric.secondIsTime ? player.time
                    : metric.secondIsKills ? player.kills : player.deaths,
                metric.secondIsTime, snapshot.gameMode)));
    }

    // The two statistics columns size themselves to their labels/content.
    // This is the scoreboard equivalent of an HTML auto-layout table: the
    // user controls gaps and edge padding, not an arbitrary total width.
    layout.playerW = std::max(1, playerContentW + scaleDs(2));
    layout.firstW = std::max(scaleDs(20), firstContentW + scaleDs(2));
    layout.secondW = std::max(scaleDs(20), secondContentW + scaleDs(2));

    const int fixedW = layout.portraitW + scaleDs(3) + layout.firstW + layout.secondW
                     + layout.columnSpacing * 2 + layout.rightPadding;
    const int naturalW = fixedW + layout.playerW;
    // A DS top screen is 256 units wide. Long localized names still elide in
    // the Player cell rather than allowing the panel to grow without bound.
    layout.panelW = std::max(fixedW + 1, std::min(256, naturalW));
    layout.playerW = std::max(1, layout.panelW - fixedW);
    return layout;
}

static ScoreboardStructureKey MakeScoreboardStructureKey(
    const ScoreboardSnapshot& snapshot)
{
    ScoreboardStructureKey key;
    key.matchSerial = snapshot.matchSerial;
    key.gameMode = snapshot.gameMode;
    key.activePlayers = snapshot.activePlayers;
    key.teamFlags = snapshot.teamFlags;
    key.teams = snapshot.teams;
    key.visible = snapshot.visible;
    key.resultSlots = snapshot.resultSlots;
    for (int slot = 0; slot < 4; ++slot) {
        key.teamIndex[slot] = snapshot.playersBySlot[slot].teamIndex;
        key.active[slot] = snapshot.playersBySlot[slot].active;
    }
    return key;
}

static ScoreboardPlanText MakeScoreboardPlanText(
    const QString& text, const QRectF& rect, const QColor& color,
    const QFontMetrics& fm, bool rankFont,
    Qt::Alignment alignment = Qt::AlignLeft,
    ScoreboardVisualValue visual = {})
{
    ScoreboardPlanText out;
    out.rect = rect;
    out.color = color;
    out.visual = visual;
    out.alignment = alignment;
    out.rankFont = rankFont;
    out.display = ScoreboardElide(text, fm, static_cast<int>(rect.width()));
    out.advance = fm.horizontalAdvance(out.display);
    out.enabled = !out.display.isEmpty();
    return out;
}

static ScoreboardPlanText MakeScoreboardMetricPlanText(
    int32_t rawValue, bool isTime, uint8_t gameMode,
    const QRectF& rect, const QColor& color, const QFontMetrics& fm,
    Qt::Alignment alignment = Qt::AlignRight)
{
    const ScoreboardVisualValue visual = MakeScoreboardVisualValue(
        rawValue, isTime, gameMode);
    return MakeScoreboardPlanText(
        FormatScoreboardVisualValue(visual), rect, color, fm, false,
        alignment, visual);
}

static ScoreboardPlanText MakeScoreboardRankPlanText(
    int rank, const QRectF& rect, const QColor& color,
    const QFontMetrics& fm)
{
    const ScoreboardVisualValue visual{rank, false, false, true};
    return MakeScoreboardPlanText(
        FormatScoreboardRankLabel(rank), rect, color, fm, true,
        Qt::AlignLeft, visual);
}

static bool ScoreboardPlanMatches(const ScoreboardRenderPlan& plan,
                                  const ScoreboardStructureKey& structure,
                                  const CachedHudConfig& c,
                                  float hudScale,
                                  uint8_t localPlayerSlot)
{
    return plan.valid
        && plan.configEpoch == s_cacheEpoch
        && plan.textCacheGeneration == s_textCacheGen
        && plan.frameFontPx == s_frameFpx
        && plan.hudScale == hudScale
        && plan.stretchX == c.lastStretchX
        && plan.localPlayerSlot == localPlayerSlot
        && plan.structure == structure;
}

static ScoreboardRenderPlan BuildScoreboardRenderPlan(
    const ScoreboardSnapshot& snapshot,
    const ScoreboardStructureKey& structure,
    const CachedHudConfig& c,
    float hudScale,
    uint8_t localPlayerSlot,
    const QFontMetrics& boardFm,
    const QFontMetrics& rankFm)
{
    ScoreboardRenderPlan plan;
    std::array<ScoreboardRenderRow, 8> rows{};
    const int rowCount = BuildScoreboardRenderRows(snapshot, rows);
    if (rowCount == 0)
        return plan;

    const ScoreboardLayout layout = BuildScoreboardLayout(
        snapshot, rows, rowCount, c, hudScale, boardFm, rankFm);
    plan.configEpoch = s_cacheEpoch;
    plan.textCacheGeneration = s_textCacheGen;
    plan.frameFontPx = s_frameFpx;
    plan.hudScale = hudScale;
    plan.stretchX = c.lastStretchX;
    plan.localPlayerSlot = localPlayerSlot;
    plan.structure = structure;
    plan.panelW = layout.panelW;
    plan.panelH = layout.panelH;
    plan.rowH = layout.rowH;
    plan.teamRowH = layout.teamRowH;
    plan.rowSpacing = layout.rowSpacing;
    plan.columnSpacing = layout.columnSpacing;
    plan.rightPadding = layout.rightPadding;
    plan.portraitW = layout.portraitW;
    plan.playerW = layout.playerW;
    plan.firstW = layout.firstW;
    plan.secondW = layout.secondW;
    plan.rowCount = rowCount;

    plan.panelX = c.scoreboard.x;
    plan.panelY = c.scoreboard.y;
    const int safeAnchor = std::clamp(c.scoreboard.anchor, 0, 8);
    HudGeometry::ApplyRectAnchor(plan.panelX, plan.panelY,
                                 plan.panelW, plan.panelH,
                                 safeAnchor % 3, safeAnchor / 3);

    const float invHudScale = hudScale > 0.0f ? 1.0f / hudScale : 1.0f;
    const float boardScale = ScoreboardUserScale(c);
    const auto scaleDs = [boardScale](int value) {
        return std::max(0, static_cast<int>(std::lround(value * boardScale)));
    };
    const int nameX = plan.panelX + plan.portraitW + scaleDs(3);
    const int firstX = nameX + plan.playerW + plan.columnSpacing;
    const int secondX = firstX + plan.firstW + plan.columnSpacing;
    const int panelInset = std::max(1, scaleDs(1));
    const int headerInset = panelInset;
    const int headerHeight = std::max(1, plan.rowH - headerInset * 2);
    const ScoreboardMetricSpec metric = ResolveScoreboardMetric(snapshot.gameMode);

    plan.headers[0] = MakeScoreboardPlanText(
        UiText::Tr("Player"),
        QRectF(nameX, plan.panelY + headerInset, plan.playerW, headerHeight),
        c.scoreboard.headerColor, boardFm, false);
    plan.headers[1] = MakeScoreboardPlanText(
        UiText::Tr(metric.firstLabel),
        QRectF(firstX, plan.panelY + headerInset, plan.firstW, headerHeight),
        c.scoreboard.headerColor, boardFm, false, Qt::AlignRight);
    plan.headers[2] = MakeScoreboardPlanText(
        UiText::Tr(metric.secondLabel),
        QRectF(secondX, plan.panelY + headerInset, plan.secondW, headerHeight),
        c.scoreboard.headerColor, boardFm, false, Qt::AlignRight);

    int y = plan.panelY + plan.rowH;
    for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
        const ScoreboardRenderRow& row = rows[rowIndex];
        ScoreboardPlanRow& planned = plan.rows[rowIndex];
        planned.team = row.team;
        planned.slot = row.slot;
        planned.teamIndex = row.teamIndex;
        const int currentRowH = row.team ? plan.teamRowH : plan.rowH;
        const QRectF nameRect(nameX, y, plan.playerW, currentRowH);
        const QRectF firstRect(firstX, y, plan.firstW, currentRowH);
        const QRectF secondRect(secondX, y, plan.secondW, currentRowH);

        if (row.team) {
            const ScoreboardTeamSnapshot& team = snapshot.teamsByIndex[row.teamIndex];
            planned.color = ResolveScoreboardTeamColor(c, row.teamIndex);
            const QString teamName = QStringLiteral("%1 %2").arg(UiText::Tr("Team"))
                .arg(static_cast<int>(row.teamIndex) + 1);
            const int teamRank = ResolveScoreboardDisplayedRank(
                snapshot, true, row.slot);
            if (c.scoreboard.rankShow) {
                planned.name = MakeScoreboardPlanText(
                    teamName, QRectF(nameRect.x(), nameRect.y(), nameRect.width(),
                                     nameRect.height() * 0.58),
                    planned.color, boardFm, false);
                planned.rank = MakeScoreboardRankPlanText(
                    teamRank, ScoreboardRankLine(nameRect), planned.color, rankFm);
            } else {
                planned.name = MakeScoreboardPlanText(
                    teamName, nameRect, planned.color, boardFm, false);
            }
            planned.first = MakeScoreboardMetricPlanText(
                metric.firstIsTime ? team.time : team.points,
                metric.firstIsTime, snapshot.gameMode,
                firstRect, planned.color, boardFm);
            planned.second = MakeScoreboardMetricPlanText(
                metric.secondIsTime ? team.time
                    : metric.secondIsKills ? team.kills : team.deaths,
                metric.secondIsTime, snapshot.gameMode,
                secondRect, planned.color, boardFm);
        } else {
            const ScoreboardPlayerSnapshot& player = snapshot.playersBySlot[row.slot];
            planned.color = (player.slot == localPlayerSlot)
                ? c.scoreboard.localPlayerColor : c.scoreboard.textColor;
            planned.hunterId = player.hunterId;
            planned.stars = player.stars;

            const QImage* hunter = player.hunterId < 7
                ? &s_scoreboardHunterIcons[player.hunterId] : nullptr;
            if (hunter && !hunter->isNull()) {
                const float iconH = static_cast<float>(hunter->height()) * invHudScale;
                const float iconW = static_cast<float>(hunter->width()) * invHudScale;
                planned.hunterIconRect = QRectF(
                    plan.panelX + panelInset,
                    y + (currentRowH - iconH) * 0.5f,
                    iconW, iconH);
            }

            const int rank = ResolveScoreboardDisplayedRank(
                snapshot, false, row.slot);
            planned.name = MakeScoreboardPlanText(
                player.displayName,
                QRectF(nameRect.x(), nameRect.y(), nameRect.width(),
                       nameRect.height() * 0.58),
                planned.color, boardFm, false);
            const QString rankLabel = FormatScoreboardRankLabel(rank);
            if (c.scoreboard.rankShow) {
                planned.rank = MakeScoreboardRankPlanText(
                    rank, ScoreboardRankLine(nameRect), planned.color, rankFm);
            }

            if (player.stars <= 4) {
                const QImage& stars = s_scoreboardStars[player.stars];
                if (!stars.isNull()) {
                    const float starsH = static_cast<float>(stars.height()) * invHudScale;
                    const float starsW = static_cast<float>(stars.width()) * invHudScale;
                    const QRectF rankLine = ScoreboardRankLine(nameRect);
                    const float rankTextW = c.scoreboard.rankShow
                        ? static_cast<float>(rankFm.horizontalAdvance(rankLabel)) : 0.0f;
                    const float starsX = static_cast<float>(nameX) + rankTextW
                        + (rankTextW > 0.0f ? static_cast<float>(scaleDs(3)) : 0.0f);
                    const float availableStarsW = std::max(
                        0.0f, static_cast<float>(nameRect.right() - starsX - scaleDs(2)));
                    const float drawStarsW = std::min(starsW, availableStarsW);
                    planned.starsFullW = starsW;
                    planned.starsFullH = starsH;
                    if (drawStarsW > 0.0f) {
                        planned.starsRect = QRectF(
                            starsX,
                            rankLine.y() + (rankLine.height() - starsH) * 0.5f,
                            drawStarsW, starsH);
                    }
                }
            }
            planned.first = MakeScoreboardMetricPlanText(
                metric.firstIsTime ? player.time : player.points,
                metric.firstIsTime, snapshot.gameMode,
                firstRect, planned.color, boardFm);
            planned.second = MakeScoreboardMetricPlanText(
                metric.secondIsTime ? player.time
                    : metric.secondIsKills ? player.kills : player.deaths,
                metric.secondIsTime, snapshot.gameMode,
                secondRect, planned.color, boardFm);
        }
        y += currentRowH + plan.rowSpacing;
    }

    plan.valid = true;
    return plan;
}

static bool UpdateScoreboardPlanCell(
    ScoreboardPlanText& cell, int32_t rawValue, bool isTime,
    uint8_t gameMode, const QColor& color, const QFontMetrics& fm,
    bool rankFont, Qt::Alignment alignment)
{
    const ScoreboardVisualValue visual = MakeScoreboardVisualValue(
        rawValue, isTime, gameMode);
    if (cell.visual == visual)
        return true;

    MelonPrimePerf::CountScoreboardDynamicCellUpdate(
        cell.visual.valid && cell.visual.isTime && visual.isTime);

    // The normal dynamic case is a value that still fits its existing cell.
    // Format and measure exactly once; elision is reserved for a structural
    // rebuild where the column width is recomputed.
    QString display = FormatScoreboardVisualValue(visual);
    const qreal advance = fm.horizontalAdvance(display);
    if (advance > std::max(1, static_cast<int>(cell.rect.width())))
        return false;

    cell.display = std::move(display);
    cell.outlinePath.clear();
    cell.outlinePathPrepared = false;
    cell.color = color;
    cell.visual = visual;
    cell.advance = advance;
    cell.alignment = alignment;
    cell.rankFont = rankFont;
    cell.enabled = !cell.display.isEmpty();
    return true;
}

static bool UpdateScoreboardRenderPlanDynamicCells(
    ScoreboardRenderPlan& plan,
    const ScoreboardSnapshot& snapshot,
    const CachedHudConfig& c,
    const QFontMetrics& boardFm,
    const QFontMetrics& rankFm)
{
    if (!plan.valid)
        return false;

    const ScoreboardMetricSpec metric = ResolveScoreboardMetric(snapshot.gameMode);

    for (int rowIndex = 0; rowIndex < plan.rowCount; ++rowIndex) {
        ScoreboardPlanRow& current = plan.rows[rowIndex];
        if (current.slot >= 4)
            return false;

        const ScoreboardPlayerSnapshot& player = snapshot.playersBySlot[current.slot];
        if (c.scoreboard.rankShow) {
            const int rank = ResolveScoreboardDisplayedRank(
                snapshot, current.team, current.slot);
            const ScoreboardVisualValue previousRank = current.rank.visual;
            if (!UpdateScoreboardPlanCell(
                    current.rank, rank, false, snapshot.gameMode,
                    current.color, rankFm, true, Qt::AlignLeft))
                return false;
            if (!(previousRank == current.rank.visual) && current.starsFullW > 0.0) {
                const float boardScale = ScoreboardUserScale(c);
                const auto scaleDs = [boardScale](int value) {
                    return std::max(0, static_cast<int>(std::lround(value * boardScale)));
                };
                const QRectF rankLine = ScoreboardRankLine(current.name.rect);
                const float starsX = static_cast<float>(current.name.rect.x())
                    + static_cast<float>(current.rank.advance)
                    + static_cast<float>(scaleDs(3));
                const float availableStarsW = std::max(
                    0.0f, static_cast<float>(current.name.rect.right() - starsX - scaleDs(2)));
                const float drawStarsW = std::min(
                    static_cast<float>(current.starsFullW), availableStarsW);
                current.starsRect = drawStarsW > 0.0f && current.starsFullH > 0.0
                    ? QRectF(starsX,
                             rankLine.y() + (rankLine.height() - current.starsFullH) * 0.5f,
                             drawStarsW, current.starsFullH)
                    : QRectF();
            }
        }

        int firstValue = player.points;
        int secondValue = metric.secondIsKills ? player.kills : player.deaths;
        if (current.team) {
            if (current.teamIndex > 1)
                return false;
            const ScoreboardTeamSnapshot& team = snapshot.teamsByIndex[current.teamIndex];
            firstValue = metric.firstIsTime ? team.time : team.points;
            secondValue = metric.secondIsTime ? team.time
                : metric.secondIsKills ? team.kills : team.deaths;
        } else {
            firstValue = metric.firstIsTime ? player.time : player.points;
            secondValue = metric.secondIsTime ? player.time
                : metric.secondIsKills ? player.kills : player.deaths;
        }

        if (!UpdateScoreboardPlanCell(
                current.first, firstValue, metric.firstIsTime, snapshot.gameMode,
                current.color, boardFm, false, Qt::AlignRight)
            || !UpdateScoreboardPlanCell(
                current.second, secondValue, metric.secondIsTime, snapshot.gameMode,
                current.color, boardFm, false, Qt::AlignRight))
            return false;
    }
    return true;
}

static void DrawScoreboardPlanText(
    QPainter* p, ScoreboardPlanText& text,
    const QFont& font, const QFontMetrics& fm,
    const HudOutlineConfig& outline, float hudScale)
{
    if (!p || !text.enabled)
        return;

    const auto flags = text.alignment | Qt::AlignVCenter | Qt::TextSingleLine;
    const float outlineRadius = outline.enable && outline.opacity > 0.0f
        ? static_cast<float>(std::max(1, outline.thickness))
            / std::max(0.001f, hudScale)
        : 0.0f;
    if (outlineRadius > 0.0f) {
        if (!text.outlinePathPrepared) {
            text.outlinePath.addText(QPointF(0.0f, 0.0f), font, text.display);
            text.outlinePathPrepared = true;
            MelonPrimePerf::CountScoreboardOutlinePathMiss();
        } else {
            MelonPrimePerf::CountScoreboardOutlinePathHit();
        }
        const Qt::Alignment horizontal = text.alignment & Qt::AlignHorizontal_Mask;
        qreal textX = text.rect.left();
        if (horizontal == Qt::AlignRight)
            textX = text.rect.right() - text.advance;
        else if (horizontal == Qt::AlignHCenter)
            textX = text.rect.center().x() - text.advance * 0.5;
        const qreal baseline = text.rect.top()
            + (text.rect.height() - fm.height()) * 0.5 + fm.ascent();
        QColor outlineColor = outline.color;
        outlineColor.setAlphaF(std::clamp(outline.opacity, 0.0f, 1.0f));
        QPen outlinePen(outlineColor, outlineRadius * 2.0);
        outlinePen.setJoinStyle(Qt::RoundJoin);
        p->save();
        p->setPen(outlinePen);
        p->setBrush(Qt::NoBrush);
        p->translate(textX, baseline);
        p->drawPath(text.outlinePath);
        p->restore();
    }
    p->setPen(text.color);
    p->setBrush(Qt::NoBrush);
    p->setFont(font);
    p->drawText(text.rect, flags, text.display);
    AccumDirtyDs(p, outlineRadius > 0.0f ? text.rect.adjusted(
        -outlineRadius, -outlineRadius, outlineRadius, outlineRadius) : text.rect);
}

static void DrawScoreboard(QPainter* p, const ScoreboardSnapshot& snapshot,
                           const CachedHudConfig& c, float hudScale,
                           uint8_t localPlayerSlot)
{
    if (!p || !snapshot.visible || !c.scoreboard.show
        || c.scoreboard.opacity <= 0.0f)
        return;

    EnsureScoreboardFontCache(c, hudScale);
    auto& hudFrame = HudFrameState();
    const auto& scoreboardFont = hudFrame.scoreboardFont;
    const QFont& boardFont = scoreboardFont.boardFont;
    const QFont& rankFont = scoreboardFont.rankFont;
    const QFontMetrics& boardFm = scoreboardFont.boardFm;
    const QFontMetrics& rankFm = scoreboardFont.rankFm;
    const HudOutlineConfig& scoreboardOutline = EffOL(c, c.scoreboard.outline);
    const ScoreboardStructureKey structure = MakeScoreboardStructureKey(snapshot);
    MelonPrimePerf::CountScoreboardStructureCheck();

    if (!ScoreboardPlanMatches(hudFrame.scoreboardPlan, structure, c,
                               hudScale, localPlayerSlot)) {
        MelonPrimePerf::ScopedHudPhase planTimer(
            MelonPrimePerf::HudPhase::ScoreboardPlan);
        hudFrame.scoreboardPlan = BuildScoreboardRenderPlan(
            snapshot, structure, c, hudScale, localPlayerSlot, boardFm, rankFm);
        MelonPrimePerf::CountScoreboardPlanBuild();
        MelonPrimePerf::CountScoreboardFullPlanRebuild();
    } else {
        MelonPrimePerf::ScopedHudPhase planTimer(
            MelonPrimePerf::HudPhase::ScoreboardPlan);
        if (!UpdateScoreboardRenderPlanDynamicCells(
                hudFrame.scoreboardPlan, snapshot, c, boardFm, rankFm))
        {
            hudFrame.scoreboardPlan = BuildScoreboardRenderPlan(
                snapshot, structure, c, hudScale, localPlayerSlot, boardFm, rankFm);
            MelonPrimePerf::CountScoreboardPlanBuild();
            MelonPrimePerf::CountScoreboardFullPlanRebuild();
        }
    }
    auto& plan = hudFrame.scoreboardPlan;
    if (!plan.valid)
        return;

    MelonPrimePerf::ScopedHudPhase rasterTimer(
        MelonPrimePerf::HudPhase::ScoreboardRaster);

    p->save();
    p->setFont(boardFont);
    p->setOpacity(c.scoreboard.opacity);
    p->setPen(Qt::NoPen);
    QColor background = c.scoreboard.backgroundColor;
    background.setAlphaF(c.scoreboard.backgroundOpacity);
    p->setBrush(background);
    const QRectF panelRect(plan.panelX, plan.panelY, plan.panelW, plan.panelH);
    p->drawRect(panelRect);
    AccumDirtyDs(p, panelRect);

    // Hunter portraits and Stars are pixel art. Disable Qt's smooth transform
    // for this scope so both the cached resize and the final draw stay NEAREST.
    p->setRenderHint(QPainter::SmoothPixmapTransform, false);

    for (int headerIndex = 0; headerIndex < 3; ++headerIndex) {
        DrawScoreboardPlanText(p, plan.headers[headerIndex],
                               boardFont, boardFm, scoreboardOutline, hudScale);
    }

    for (int rowIndex = 0; rowIndex < plan.rowCount; ++rowIndex) {
        const ScoreboardPlanRow& row = plan.rows[rowIndex];
        if (row.team) {
            DrawScoreboardPlanText(p, plan.rows[rowIndex].name,
                                   boardFont, boardFm, scoreboardOutline, hudScale);
            DrawScoreboardPlanText(p, plan.rows[rowIndex].rank,
                                   rankFont, rankFm, scoreboardOutline, hudScale);
            DrawScoreboardPlanText(p, plan.rows[rowIndex].first,
                                   boardFont, boardFm, scoreboardOutline, hudScale);
            DrawScoreboardPlanText(p, plan.rows[rowIndex].second,
                                   boardFont, boardFm, scoreboardOutline, hudScale);
            continue;
        }

        const QImage* hunter = row.hunterId < 7
            ? &s_scoreboardHunterIcons[row.hunterId] : nullptr;
        if (hunter && !hunter->isNull() && !row.hunterIconRect.isEmpty()) {
            p->drawImage(row.hunterIconRect, *hunter);
            AccumDirtyDs(p, row.hunterIconRect);
        }
        DrawScoreboardPlanText(p, plan.rows[rowIndex].name,
                               boardFont, boardFm, scoreboardOutline, hudScale);
        DrawScoreboardPlanText(p, plan.rows[rowIndex].rank,
                               rankFont, rankFm, scoreboardOutline, hudScale);
        if (row.stars <= 4 && !row.starsRect.isEmpty()) {
            const QImage& stars = s_scoreboardStars[row.stars];
            if (!stars.isNull()) {
                p->drawImage(row.starsRect, stars);
                AccumDirtyDs(p, row.starsRect);
            }
        }
        DrawScoreboardPlanText(p, plan.rows[rowIndex].first,
                               boardFont, boardFm, scoreboardOutline, hudScale);
        DrawScoreboardPlanText(p, plan.rows[rowIndex].second,
                               boardFont, boardFm, scoreboardOutline, hudScale);
    }
    p->restore();
}

static ScoreboardSnapshot MakeScoreboardPreviewSnapshot()
{
    ScoreboardSnapshot preview{};
    preview.gameMode = static_cast<uint8_t>(ScoreboardGameMode::BattleTeams);
    preview.activePlayers = 4;
    preview.teams = true;
    preview.visible = true;
    preview.resultSlots = {0, 1, 2, 3};
    preview.teamsByIndex[0] = {420, 8, 3, 120};
    preview.teamsByIndex[1] = {390, 6, 4, 96};
    for (int slot = 0; slot < 4; ++slot) {
        auto& player = preview.playersBySlot[slot];
        player.slot = static_cast<uint8_t>(slot);
        player.teamIndex = static_cast<uint8_t>(slot / 2);
        player.hunterId = static_cast<uint8_t>(slot % 7);
        player.stars = static_cast<uint8_t>(slot % 5);
        player.standing = static_cast<uint8_t>(slot / 2);
        player.teamStanding = static_cast<uint8_t>(slot % 2);
        player.active = true;
        player.points = 60 - slot * 7;
        player.deaths = slot + 1;
        player.displayName = QStringLiteral("Player %1").arg(slot + 1);
    }
    return preview;
}

// =========================================================================
//  Enemy Target HUD — the opponent the local player last damaged.
//
//  The panel is laid out top-down as: name, HP meter(s), score. A hunter
//  portrait sits to the left of that column. Layout is a pure function of the
//  snapshot + config + fonts so edit mode can reproduce the exact selection
//  box without drawing, exactly like the scoreboard.
// =========================================================================
static float EnemyTargetUserScale(const CachedHudConfig& c)
{
    return std::clamp(c.enemyTarget.scale, 0.1f, 8.0f);
}

static float EnemyTargetTextDrawScale(const CachedHudConfig& c, float hudScale)
{
    // Same shape as ScoreboardTextDrawScale: shared resolution-aware auto scale
    // plus the global Text Scale % offset, then this element's own multiplier.
    const float sharedTextScale = (c.scaleText * 100.0f + (c.textScalePct - 100)) / 100.0f;
    const float elementTextScale = std::clamp(c.enemyTarget.textScalePct, 10, 800) / 100.0f;
    const float requested = sharedTextScale * elementTextScale;
    const float logical = hudScale > 0.0f ? requested / hudScale : requested;
    return logical * EnemyTargetUserScale(c);
}

static void BuildEnemyTargetFont(const CachedHudConfig& c, float hudScale, QFont& font)
{
    font = s_frameFont;
    const int px = std::max(1, static_cast<int>(std::lround(
        s_frameFpx * std::max(0.1f, EnemyTargetTextDrawScale(c, hudScale)))));
    font.setPixelSize(px);
}

static void EnsureEnemyTargetFontCache(const CachedHudConfig& c, float hudScale)
{
    auto& cache = HudFrameState().enemyTargetFont;
    if (cache.valid
        && cache.configEpoch == s_cacheEpoch
        && cache.baseFontGeneration == s_textCacheGen
        && cache.baseFontPx == s_frameFpx
        && cache.hudScale == hudScale) {
        return;
    }
    BuildEnemyTargetFont(c, hudScale, cache.font);
    cache.fm = QFontMetrics(cache.font);
    cache.configEpoch = s_cacheEpoch;
    cache.baseFontGeneration = s_textCacheGen;
    cache.baseFontPx = s_frameFpx;
    cache.hudScale = hudScale;
    cache.valid = true;
}

// Native Energy Tank granularity: the first meter fills to 100, the second
// shows the remainder. MphRead's DrawOpponent draws the same two 72px bars.
static constexpr int kEnemyTargetEnergyTank = 100;

static QString FormatEnemyTargetHpNumber(const EnemyTargetSnapshot& s)
{
    return QStringLiteral("%1/%2").arg(s.hp).arg(s.maxHp);
}

static QString FormatEnemyTargetScore(const EnemyTargetSnapshot& s)
{
    const ScoreboardVisualValue value =
        MakeScoreboardVisualValue(s.rawValue, s.valueIsTime, s.gameMode);
    const ScoreboardVisualValue goal =
        MakeScoreboardVisualValue(s.rawGoal, s.valueIsTime, s.gameMode);
    const QString valueText = FormatScoreboardVisualValue(value);
    // A zero/absent goal means the mode has no target to count towards; show
    // the bare value instead of an "x / 0" that reads as a bug.
    if (!goal.valid || (!goal.isMax && goal.value <= 0))
        return valueText;
    return QStringLiteral("%1 / %2").arg(valueText, FormatScoreboardVisualValue(goal));
}

static const QColor& ResolveEnemyTargetNameColor(const CachedHudConfig& c,
                                                 const EnemyTargetSnapshot& s)
{
    if (c.enemyTarget.teamColor && s.teams && s.teamIndex <= 1)
        return ResolveScoreboardTeamColor(c, s.teamIndex);
    return c.enemyTarget.nameColor;
}

static EnemyTargetLayout BuildEnemyTargetLayout(const EnemyTargetSnapshot& s,
                                                const CachedHudConfig& c,
                                                float hudScale,
                                                const QFontMetrics& fm)
{
    EnemyTargetLayout layout;
    const auto& et = c.enemyTarget;
    const float userScale = EnemyTargetUserScale(c);
    const float invHudScale = hudScale > 0.0f ? 1.0f / hudScale : 1.0f;
    const float spacing = static_cast<float>(et.rowSpacing) * userScale;

    layout.lineH = static_cast<float>(fm.height());
    layout.hasName = et.nameShow;
    layout.hasScore = et.scoreShow;
    // Mode 3 is "Off"; mode 2 is numeric-only, so it draws no bar. The second
    // bar only appears in segmented mode and only once the first tank is full.
    layout.hasBar1 = et.hpMode == 0 || et.hpMode == 1;
    layout.hasBar2 = et.hpMode == 0;
    layout.hasNumber = et.hpNumberShow || et.hpMode == 2;

    layout.barLen = static_cast<float>(et.barLength) * userScale;
    layout.barH = static_cast<float>(et.barHeight) * userScale;

    const QImage* portrait = (et.hunterIconShow && s.hunterId < 7)
        ? &s_enemyTargetHunterIcons[s.hunterId] : nullptr;
    if (portrait && !portrait->isNull()) {
        layout.hasPortrait = true;
        layout.portraitW = portrait->width() * invHudScale;
        layout.portraitH = portrait->height() * invHudScale;
    }

    // Stack the text column: name, bar 1, bar 2, number, score.
    float columnH = 0.0f;
    auto addRow = [&](float rowHeight) {
        if (columnH > 0.0f)
            columnH += spacing;
        const float top = columnH;
        columnH += rowHeight;
        return top;
    };
    if (layout.hasName)   layout.nameY = addRow(layout.lineH);
    if (layout.hasBar1)   layout.bar1Y = addRow(layout.barH);
    if (layout.hasBar2)   layout.bar2Y = addRow(layout.barH);
    if (layout.hasNumber) layout.numberY = addRow(layout.lineH);
    if (layout.hasScore)  layout.scoreY = addRow(layout.lineH);
    if (columnH <= 0.0f)
        columnH = layout.lineH;

    // Only reserve the bar width when a bar is actually drawn, so Number Only
    // and Off do not leave the panel padded out to the bar length.
    float columnW = layout.hasBar1 ? layout.barLen : 0.0f;
    if (layout.hasName) {
        layout.nameAdvance = static_cast<float>(fm.horizontalAdvance(s.displayName));
        columnW = std::max(columnW, layout.nameAdvance);
    }
    if (layout.hasNumber) {
        layout.numberText = FormatEnemyTargetHpNumber(s);
        layout.numberAdvance = static_cast<float>(fm.horizontalAdvance(layout.numberText));
        columnW = std::max(columnW, layout.numberAdvance);
    }
    if (layout.hasScore) {
        layout.scoreText = FormatEnemyTargetScore(s);
        layout.scoreAdvance = static_cast<float>(fm.horizontalAdvance(layout.scoreText));
        columnW = std::max(columnW, layout.scoreAdvance);
    }
    columnW = std::max(columnW, 1.0f);

    const float gap = layout.hasPortrait ? std::max(1.0f, spacing) : 0.0f;
    layout.panelW = layout.portraitW + gap + columnW;
    layout.panelH = std::max(columnH, layout.portraitH);
    layout.textW = columnW;
    return layout;
}

// Positions the layout at the element's anchored origin. Split from
// BuildEnemyTargetLayout so edit mode can reuse the same placement math.
static void PlaceEnemyTargetLayout(EnemyTargetLayout& layout,
                                   float originX, float originY, int anchor)
{
    const int safeAnchor = std::clamp(anchor, 0, 8);
    float x = originX;
    float y = originY;
    HudGeometry::ApplyRectAnchorF(x, y, layout.panelW, layout.panelH,
                                  safeAnchor % 3, safeAnchor / 3);
    layout.panelX = x;
    layout.panelY = y;
    layout.portraitX = x;
    // Centre the portrait against the text column rather than top-aligning it,
    // so a tall portrait next to a short column does not look detached.
    layout.portraitY = y + (layout.panelH - layout.portraitH) * 0.5f;
    // panelW = portraitW + gap + textW, so this lands just past the portrait.
    layout.textX = x + layout.panelW - layout.textW;
    layout.numberX = layout.textX;
    layout.nameY += y;
    layout.bar1Y += y;
    layout.bar2Y += y;
    layout.numberY += y;
    layout.scoreY += y;
}

static bool EnemyTargetPlanMatches(const EnemyTargetRenderPlan& plan,
                                   const EnemyTargetPlanKey& key,
                                   const CachedHudConfig& c,
                                   float hudScale)
{
    // stretchX is tracked separately from configEpoch because a window resize
    // re-runs RecomputeAnchorPositions() without bumping the epoch.
    return plan.valid
        && plan.configEpoch == s_cacheEpoch
        && plan.textCacheGeneration == s_textCacheGen
        && plan.frameFontPx == s_frameFpx
        && plan.hudScale == hudScale
        && plan.stretchX == c.lastStretchX
        && plan.key == key;
}

// Outline paths are built here, once per plan, and stroked at draw time. The
// path depends only on (text, font), so it survives HP ticks that leave the
// string alone.
static void BuildEnemyTargetPlanText(EnemyTargetPlanText& cell,
                                     const QString& text, float advance,
                                     bool enabled, bool outlineEnabled,
                                     const QFont& font)
{
    cell.text = text;
    cell.advance = advance;
    cell.enabled = enabled && !text.isEmpty();
    cell.outlinePath = QPainterPath();
    cell.outlinePathPrepared = false;
    if (cell.enabled && outlineEnabled) {
        cell.outlinePath.addText(QPointF(0.0f, 0.0f), font, text);
        cell.outlinePathPrepared = true;
    }
}

static EnemyTargetRenderPlan BuildEnemyTargetRenderPlan(
    const EnemyTargetSnapshot& s,
    const EnemyTargetPlanKey& key,
    const CachedHudConfig& c,
    float hudScale,
    const QFont& font,
    const QFontMetrics& fm)
{
    const auto& et = c.enemyTarget;
    EnemyTargetRenderPlan plan;
    plan.valid = true;
    plan.configEpoch = s_cacheEpoch;
    plan.textCacheGeneration = s_textCacheGen;
    plan.frameFontPx = s_frameFpx;
    plan.hudScale = hudScale;
    plan.stretchX = c.lastStretchX;
    plan.key = key;
    plan.layout = BuildEnemyTargetLayout(s, c, hudScale, fm);
    PlaceEnemyTargetLayout(plan.layout, static_cast<float>(et.x),
                           static_cast<float>(et.y), et.anchor);
    plan.nameColor = ResolveEnemyTargetNameColor(c, s);

    const HudOutlineConfig& outline = EffOL(c, et.outline);
    const bool outlineEnabled = outline.enable && outline.opacity > 0.0f;
    const EnemyTargetLayout& layout = plan.layout;
    BuildEnemyTargetPlanText(plan.name, s.displayName, layout.nameAdvance,
                             layout.hasName, outlineEnabled, font);
    BuildEnemyTargetPlanText(plan.number, layout.numberText, layout.numberAdvance,
                             layout.hasNumber, outlineEnabled, font);
    BuildEnemyTargetPlanText(plan.score, layout.scoreText, layout.scoreAdvance,
                             layout.hasScore, outlineEnabled, font);
    return plan;
}

static void DrawEnemyTargetPlanText(QPainter* p, const EnemyTargetPlanText& cell,
                                    float x, float y, float lineH,
                                    const QColor& color, const QFont& font,
                                    const QFontMetrics& fm,
                                    const HudOutlineConfig& outline, float hudScale)
{
    if (!cell.enabled)
        return;
    const QRectF rect(x, y, std::max(1.0f, cell.advance), lineH);
    const float outlineRadius = outline.enable && outline.opacity > 0.0f
        ? static_cast<float>(std::max(1, outline.thickness)) / std::max(0.001f, hudScale)
        : 0.0f;
    if (outlineRadius > 0.0f && cell.outlinePathPrepared) {
        QColor outlineColor = outline.color;
        outlineColor.setAlphaF(std::clamp(outline.opacity, 0.0f, 1.0f));
        QPen outlinePen(outlineColor, outlineRadius * 2.0);
        outlinePen.setJoinStyle(Qt::RoundJoin);
        p->save();
        p->setPen(outlinePen);
        p->setBrush(Qt::NoBrush);
        p->translate(rect.left(), rect.top() + (rect.height() - fm.height()) * 0.5 + fm.ascent());
        p->drawPath(cell.outlinePath);
        p->restore();
    }
    p->setPen(color);
    p->setBrush(Qt::NoBrush);
    p->setFont(font);
    p->drawText(rect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, cell.text);
    AccumDirtyDs(p, outlineRadius > 0.0f
        ? rect.adjusted(-outlineRadius, -outlineRadius, outlineRadius, outlineRadius)
        : rect);
}

static void DrawEnemyTargetBar(QPainter* p, float x, float y, float len, float h,
                               float ratio, const QColor& fill, const QColor& back,
                               const HudOutlineConfig& outline, float hudScale)
{
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    const float expand = outline.enable && outline.opacity > 0.0f
        ? static_cast<float>(std::max(1, outline.thickness)) / std::max(0.001f, hudScale)
        : 0.0f;
    if (expand > 0.0f) {
        QColor outlineColor = outline.color;
        outlineColor.setAlphaF(std::clamp(outline.opacity, 0.0f, 1.0f));
        p->fillRect(QRectF(x - expand, y - expand,
                           len + expand * 2.0f, h + expand * 2.0f), outlineColor);
    }
    p->fillRect(QRectF(x, y, len, h), back);
    if (ratio > 0.0f)
        p->fillRect(QRectF(x, y, len * ratio, h), fill);
    AccumDirtyDs(p, QRectF(x - expand, y - expand,
                           len + expand * 2.0f, h + expand * 2.0f));
}

static void DrawEnemyTarget(QPainter* p, const EnemyTargetSnapshot& s,
                            const CachedHudConfig& c, float hudScale)
{
    const auto& et = c.enemyTarget;
    if (!p || !s.visible || !et.show || et.opacity <= 0.0f)
        return;

    EnsureEnemyTargetFontCache(c, hudScale);
    auto& hudFrame = HudFrameState();
    const auto& fontCache = hudFrame.enemyTargetFont;
    const QFont& font = fontCache.font;
    const QFontMetrics& fm = fontCache.fm;
    const HudOutlineConfig& outline = EffOL(c, et.outline);

    // Reuse the whole plan -- geometry, strings, and glyph outline paths --
    // unless something that changes the drawn result changed.
    const EnemyTargetPlanKey key = MakeEnemyTargetPlanKey(s);
    if (!EnemyTargetPlanMatches(hudFrame.enemyTargetPlan, key, c, hudScale)) {
        hudFrame.enemyTargetPlan =
            BuildEnemyTargetRenderPlan(s, key, c, hudScale, font, fm);
    }
    const EnemyTargetRenderPlan& plan = hudFrame.enemyTargetPlan;
    const EnemyTargetLayout& layout = plan.layout;

    p->save();
    p->setOpacity(et.opacity);
    // Hunter portraits are pixel art; keep the scale NEAREST like the scoreboard.
    p->setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (layout.hasPortrait) {
        const QImage& portrait = s_enemyTargetHunterIcons[s.hunterId];
        const QRectF dst(layout.portraitX, layout.portraitY,
                         layout.portraitW, layout.portraitH);
        p->drawImage(dst, portrait);
        AccumDirtyDs(p, dst);
    }

    DrawEnemyTargetPlanText(p, plan.name, layout.textX, layout.nameY,
                            layout.lineH, plan.nameColor, font, fm, outline, hudScale);

    if (layout.hasBar1) {
        // Segmented: bar 1 is the first Energy Tank, bar 2 the remainder.
        // Single Bar: one meter across the whole of maxHp.
        const float first = et.hpMode == 0
            ? std::min<float>(s.hp, kEnemyTargetEnergyTank) / static_cast<float>(kEnemyTargetEnergyTank)
            : (s.maxHp > 0 ? static_cast<float>(s.hp) / static_cast<float>(s.maxHp) : 0.0f);
        DrawEnemyTargetBar(p, layout.textX, layout.bar1Y, layout.barLen, layout.barH,
                           first, et.hpColor, et.hpBackColor, outline, hudScale);
    }
    if (layout.hasBar2) {
        const int remaining = std::max(0, static_cast<int>(s.hp) - kEnemyTargetEnergyTank);
        const int secondMax = std::max(1, static_cast<int>(s.maxHp) - kEnemyTargetEnergyTank);
        DrawEnemyTargetBar(p, layout.textX, layout.bar2Y, layout.barLen, layout.barH,
                           static_cast<float>(remaining) / static_cast<float>(secondMax),
                           et.hpColor, et.hpBackColor, outline, hudScale);
    }
    DrawEnemyTargetPlanText(p, plan.number, layout.numberX, layout.numberY,
                            layout.lineH, et.hpColor, font, fm, outline, hudScale);
    DrawEnemyTargetPlanText(p, plan.score, layout.textX, layout.scoreY,
                            layout.lineH, et.scoreColor, font, fm, outline, hudScale);
    p->restore();
}

// Deterministic sample used by edit mode and the settings-dialog preview so
// both show the panel without a running match.
static EnemyTargetSnapshot MakeEnemyTargetPreviewSnapshot()
{
    EnemyTargetSnapshot preview{};
    preview.visible = true;
    preview.slot = 1;
    preview.timer = 60;
    preview.hunterId = 2;
    preview.teamIndex = 1;
    preview.gameMode = static_cast<uint8_t>(ScoreboardGameMode::Battle);
    preview.teams = false;
    preview.displayName = QStringLiteral("Player 2");
    preview.hp = 145;
    preview.maxHp = 199;
    preview.rawValue = 4;
    preview.rawGoal = 7;
    preview.valueIsTime = false;
    return preview;
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

    auto& s_hpTextCache = HudElementTextCaches().hpText;
    auto& s_hpBitmapCache = HudElementTextCaches().hpBitmap;
    auto& s_hpOutlineCache = HudElementTextCaches().hpOutline;
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
    auto& s_invTextCache = HudElementTextCaches().invText;
    auto& s_invOutlineCache = HudElementTextCaches().invOutline;

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
    auto& s_ammoTextCache = HudElementTextCaches().ammoText;
    auto& s_ammoBitmapCache = HudElementTextCaches().ammoBitmap;
    auto& s_ammoOutlineCache = HudElementTextCaches().ammoOutline;
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
        DrawFxGlitchArtOverlay(p, pxCx, pxCy, maxR, std::clamp(zoomT, 0.0, 1.0), midFx,
                               ch.chZoomScopeThickness, s_chGlitchFxPhase, 0);
        break;
    }
    case 3: { // Glitch2 — scan-disturb overload + tactical brackets
        p->setRenderHint(QPainter::Antialiasing, false);
        const double lockT = std::clamp(zoomT, 0.0, 1.0);
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
