// Unity fragment included by MelonPrimeInputConfig.cpp.
// Do not add this file to CMakeLists.

// ── Preview Widgets ─────────────────────────────────────────────────────────

class HudPreviewWidget : public QWidget
{
public:
    // DS top screen 256×192 at ×0.75 → 192×144 (equal X/Y scale, keeps circles round)
    static constexpr int kPrevW = 192;
    static constexpr int kPrevH = 144;

    explicit HudPreviewWidget(EmuInstance* emu, QWidget* parent = nullptr)
        : QWidget(parent), m_emu(emu)
    {
        setFixedSize(kPrevW, kPrevH);
    }
    void refreshPreview() { update(); }
protected:
    EmuInstance* m_emu;
    Config::Table& cfg() { return m_emu->getLocalConfig(); }

    QColor readColor(const char* kR, const char* kG, const char* kB, float opacity = 1.0f) {
        auto& c = cfg();
        return QColor(c.GetInt(kR), c.GetInt(kG), c.GetInt(kB), qBound(0, static_cast<int>(opacity * 255), 255));
    }

    void drawBackground(QPainter& p, const QRect& r) {
        p.fillRect(r, QColor(20, 20, 30));
        p.setPen(QColor(60, 60, 80));
        p.drawRect(r.adjusted(0, 0, -1, -1));
    }

    // 9-point anchor + offset → DS-space QPoint (0-255, 0-191)
    static QPoint dsPos(int anchor, int ofsX, int ofsY) {
        int x = 0, y = 0;
        MelonPrime::HudGeometry::ApplyAnchor(anchor, ofsX, ofsY, x, y);
        return QPoint(x, y);
    }

    // Apply DS-space transform (call after drawBackground, before DS-space drawing)
    void setupDsTransform(QPainter& p) const {
        p.scale(width() / 256.0f, height() / 192.0f);
    }

    // Text width in DS-space units (accounts for scale transform)
    int textWidthDS(QPainter& p, const QString& str) const {
        return static_cast<int>(p.fontMetrics().horizontalAdvance(str) * 256.0f / width());
    }

    // Settings previews do not have a game-output resolution from which to
    // derive the runtime's integer auto-scale. Normalize each cap against its
    // schema default so the default preview stays unchanged while every cap
    // value in the 100-800% range remains visible in the preview. Runtime
    // rendering still derives the actual factor from the output hudScale.
    static float previewAutoScale(Config::Table& c,
                                  const char* categoryCapKey,
                                  const char* additionalCapKey = nullptr,
                                  int categoryDefaultCap = 800,
                                  int additionalDefaultCap = 800)
    {
        if (!c.GetBool(MP_HUD_PROP_KEY_HudAutoScaleEnable))
            return 1.0f;
        constexpr int kDefaultGlobalCap = 800;
        const int globalCap = qBound(100, c.GetInt(MP_HUD_PROP_KEY_HudAutoScaleCap), 800);
        const int categoryCap = qBound(100, c.GetInt(categoryCapKey), 800);
        float scale = qMin(globalCap / static_cast<float>(kDefaultGlobalCap),
                           categoryCap / static_cast<float>(qMax(1, categoryDefaultCap)));
        if (additionalCapKey)
            scale = qMin(scale,
                         qBound(100, c.GetInt(additionalCapKey), 800)
                         / static_cast<float>(qMax(1, additionalDefaultCap)));
        return scale;
    }

    QFont previewHudFont(Config::Table& c, const char* categoryCapKey,
                         const char* additionalCapKey = nullptr,
                         int categoryDefaultCap = 800,
                         int additionalDefaultCap = 800) const
    {
        QFont font = getMphHudFont(c);
        const float autoScale = previewAutoScale(
            c, categoryCapKey, additionalCapKey,
            categoryDefaultCap, additionalDefaultCap);
        font.setPixelSize(qMax(1, qRound(font.pixelSize() * autoScale)));
        return font;
    }

    // Alignment-adjusted X in DS-space: 0=left, 1=center, 2=right
    int alignedX(int anchorX, int align, int textW) const {
        return MelonPrime::HudGeometry::AlignedTextXInt(anchorX, align, textW);
    }

    // Draw a gauge bar in DS-space at 50% fill
    static void drawGaugeDS(QPainter& p, int x, int y, const QColor& color, int orient, int len, int wid, int align = 0) {
        if (len <= 0) len = 28;
        if (wid <= 0) wid = 3;
        MelonPrime::HudGeometry::ApplyGaugeAlign(x, y, orient, len, align);
        int fw = (orient == 0) ? len / 2 : wid;
        int fh = (orient == 0) ? wid    : len / 2;
        int fx = x;
        int fy = (orient == 0) ? y : y + len - fh;
        p.fillRect(QRect(x, y, orient == 0 ? len : wid, orient == 0 ? wid : len), QColor(0,0,0,100));
        p.fillRect(QRect(fx, fy, fw, fh), color);
    }

    // Draw the base crosshair (inner/outer arms + center dot) centred at (cx,cy).
    // `scale` already includes the preview magnification and any user scale
    // factors; `opacityMul` multiplies every element's opacity (the zoomed
    // preview passes Zoom Base Opacity here to show the faded base crosshair).
    void drawBaseCrosshair(QPainter& p, int cx, int cy, float scale, float opacityMul) {
        if (opacityMul <= 0.0f || scale <= 0.0f) return;
        auto& c = cfg();
        const QColor col = readColor(MP_HUD_PROP_KEY_CrosshairColorR, MP_HUD_PROP_KEY_CrosshairColorG, MP_HUD_PROP_KEY_CrosshairColorB);
        const bool tStyle = c.GetBool(MP_HUD_PROP_KEY_CrosshairTStyle);

        auto arms = [&](const char* showKey, const char* opacityKey, const char* lxKey, const char* lyKey,
                        const char* thickKey, const char* ofsKey) {
            if (!c.GetBool(showKey)) return;
            QColor ac = col;
            ac.setAlphaF(qBound(0.0f, static_cast<float>(c.GetDouble(opacityKey)) * opacityMul, 1.0f));
            const int lx  = c.GetInt(lxKey);
            const int ly  = c.GetInt(lyKey);
            const int ofs = c.GetInt(ofsKey);
            const int t   = qMax(1, static_cast<int>(c.GetInt(thickKey) * scale));
            const int ht  = t / 2;
            p.fillRect(QRect(cx + static_cast<int>(ofs * scale), cy - ht, static_cast<int>(lx * scale), t), ac);
            p.fillRect(QRect(cx - static_cast<int>(ofs * scale) - static_cast<int>(lx * scale), cy - ht, static_cast<int>(lx * scale), t), ac);
            p.fillRect(QRect(cx - ht, cy + static_cast<int>(ofs * scale), t, static_cast<int>(ly * scale)), ac);
            if (!tStyle)
                p.fillRect(QRect(cx - ht, cy - static_cast<int>(ofs * scale) - static_cast<int>(ly * scale), t, static_cast<int>(ly * scale)), ac);
        };
        arms(MP_HUD_PROP_KEY_CrosshairInnerShow, MP_HUD_PROP_KEY_CrosshairInnerOpacity,
             MP_HUD_PROP_KEY_CrosshairInnerLengthX, MP_HUD_PROP_KEY_CrosshairInnerLengthY,
             MP_HUD_PROP_KEY_CrosshairInnerThickness, MP_HUD_PROP_KEY_CrosshairInnerOffset);
        arms(MP_HUD_PROP_KEY_CrosshairOuterShow, MP_HUD_PROP_KEY_CrosshairOuterOpacity,
             MP_HUD_PROP_KEY_CrosshairOuterLengthX, MP_HUD_PROP_KEY_CrosshairOuterLengthY,
             MP_HUD_PROP_KEY_CrosshairOuterThickness, MP_HUD_PROP_KEY_CrosshairOuterOffset);

        if (c.GetBool(MP_HUD_PROP_KEY_CrosshairCenterDot)) {
            QColor dc = c.GetBool(MP_HUD_PROP_KEY_CrosshairDotCustomColor)
                ? readColor(MP_HUD_PROP_KEY_CrosshairDotColorR, MP_HUD_PROP_KEY_CrosshairDotColorG, MP_HUD_PROP_KEY_CrosshairDotColorB)
                : col;
            dc.setAlphaF(qBound(0.0f, static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_CrosshairDotOpacity)) * opacityMul, 1.0f));
            const int ds = qMax(1, static_cast<int>(c.GetInt(MP_HUD_PROP_KEY_CrosshairDotThickness) * scale));
            const QRect dotRect(cx - ds/2, cy - ds/2, ds, ds);
            if (c.GetInt(MP_HUD_PROP_KEY_CrosshairDotShape) == 1) {
                p.setPen(Qt::NoPen); p.setBrush(dc); p.drawEllipse(dotRect); p.setBrush(Qt::NoBrush);
            } else {
                p.fillRect(dotRect, dc);
            }
        }
    }
};

class ScoreboardPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;

protected:
    struct PreviewRow {
        const char* name;
        int hunter;
        int rank;
        int stars;
        int points;
        int deaths;
    };

    static QImage loadRasterToHeight(const char* path, int height)
    {
        if (height <= 0)
            return QImage();
        QImageReader reader(QString::fromLatin1(path));
        const QSize sourceSize = reader.size();
        const QSize fallbackSize = sourceSize.isEmpty()
            ? QSize(height, height) : sourceSize;
        const int width = qMax(1, fallbackSize.width() * height
                                   / qMax(1, fallbackSize.height()));
        QImage image = reader.read();
        if (!image.isNull() && image.size() != QSize(width, height))
            image = image.scaled(QSize(width, height), Qt::IgnoreAspectRatio,
                                 Qt::FastTransformation);
        return image.isNull()
            ? image : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    static const QImage& hunterIcon(int index, int height)
    {
        static QImage icons[7];
        static int cachedHeight = -1;
        if (cachedHeight != height) {
            static const char* const paths[7] = {
                ":/mph-scoreboard-hunter-0-p1", ":/mph-scoreboard-hunter-1-p1",
                ":/mph-scoreboard-hunter-2-p1", ":/mph-scoreboard-hunter-3-p1",
                ":/mph-scoreboard-hunter-4-p1", ":/mph-scoreboard-hunter-5-p1",
                ":/mph-scoreboard-hunter-6-p1",
            };
            for (int i = 0; i < 7; ++i)
                icons[i] = loadRasterToHeight(paths[i], height);
            cachedHeight = height;
        }
        return icons[qBound(0, index, 6)];
    }

    static const QImage& starsImage(int index, int height)
    {
        static QImage images[5];
        static int cachedHeight = -1;
        if (cachedHeight != height) {
            static const char* const paths[5] = {
                ":/mph-scoreboard-stars-0", ":/mph-scoreboard-stars-1",
                ":/mph-scoreboard-stars-2", ":/mph-scoreboard-stars-3",
                ":/mph-scoreboard-stars-4",
            };
            for (int i = 0; i < 5; ++i)
                images[i] = loadRasterToHeight(paths[i], height);
            cachedHeight = height;
        }
        return images[qBound(0, index, 4)];
    }

    static int imageWidthDS(const QImage& image)
    {
        return image.isNull() ? 0 : image.width();
    }

    static void drawCellText(QPainter& p, const QRectF& rect,
                             const QString& text, const QColor& color,
                             Qt::Alignment alignment,
                             const QColor& outlineColor, float outlineOpacity,
                             int outlineThickness, float scoreboardScale)
    {
        const QString display = text;
        const auto flags = alignment | Qt::AlignVCenter | Qt::TextSingleLine;
        const float radius = outlineOpacity > 0.0f
            ? qMax(0.0f, outlineThickness * scoreboardScale) : 0.0f;
        if (radius > 0.0f) {
            const QFontMetrics fm(p.font());
            const Qt::Alignment horizontal = alignment & Qt::AlignHorizontal_Mask;
            const qreal advance = fm.horizontalAdvance(display);
            qreal textX = rect.left();
            if (horizontal == Qt::AlignRight)
                textX = rect.right() - advance;
            else if (horizontal == Qt::AlignHCenter)
                textX = rect.center().x() - advance * 0.5;
            const qreal baseline = rect.top()
                + (rect.height() - fm.height()) * 0.5 + fm.ascent();
            QPainterPath path;
            path.addText(QPointF(textX, baseline), p.font(), display);
            QColor oc = outlineColor;
            oc.setAlphaF(qBound(0.0f, outlineOpacity, 1.0f));
            QPen pen(oc, radius * 2.0f);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
        }
        p.setPen(color);
        p.setBrush(Qt::NoBrush);
        p.drawText(rect, flags, display);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        if (!c.GetBool(MP_HUD_PROP_KEY_HudScoreboardShow)) {
            p.setPen(QColor(130, 130, 150));
            p.drawText(r, Qt::AlignCenter, MelonPrime::UiText::Tr("Scoreboard OFF"));
            return;
        }

        setupDsTransform(p);
        const float assetAutoScale = previewAutoScale(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapIcons,
            MP_HUD_PROP_KEY_HudAutoScaleCapScoreboard);
        const float scoreboardScale = qBound(
            0.1f, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardScale) / 100.0f, 8.0f);
        const float scoreboardTextScale = qBound(
            0.1f, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardTextScale) / 100.0f, 8.0f);
        const bool rankShow = c.GetBool(MP_HUD_PROP_KEY_HudScoreboardRankShow);
        QFont boardFont = previewHudFont(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapText,
            MP_HUD_PROP_KEY_HudAutoScaleCapScoreboard, 500, 800);
        int boardFontPx = qMax(1, qRound(
            boardFont.pixelSize() * scoreboardTextScale * scoreboardScale));
        boardFont.setPixelSize(boardFontPx);
        p.setFont(boardFont);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);

        static const PreviewRow rows[4] = {
            {"COM-SPI", 0, 1, 0, 12, 0},
            {"ZECTION_6V", 1, 2, 4, 9, 1},
            {"COM-WEA", 2, 3, 1, 7, 2},
            {"TRACE", 3, 4, 2, 4, 3},
        };

        const auto scaleDs = [scoreboardScale](int value) {
            return qMax(0, qRound(value * scoreboardScale));
        };
        const int rowSpacing = scaleDs(
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardRowSpacing), 16));
        const int columnSpacing = scaleDs(
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardColumnSpacing), 32));
        const int rightPadding = scaleDs(
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardRightPadding), 32));
        const int hunterH = qMax(0, qRound(
            c.GetInt(MP_HUD_PROP_KEY_HudScoreboardHunterIconHeight)
            * assetAutoScale * scoreboardScale));
        const int starsH = qMax(0, qRound(
            c.GetInt(MP_HUD_PROP_KEY_HudScoreboardStarsHeight)
            * assetAutoScale * scoreboardScale));
        const int headerInset = qMax(1, scaleDs(1));
        const int fontHeightDS = qMax(1, qRound(
            p.fontMetrics().height() * 192.0f / qMax(1, height())));
        const int lineHeight = fontHeightDS + 1;
        const int nameSplitHeight = (lineHeight * 100 + 57) / 58;
        const int rankSplitHeight = (lineHeight * 100 + 41) / 42;
        const int headerHeight = fontHeightDS + headerInset * 2 + 1;
        const int autoRowHeight = qMax(
            qMax(nameSplitHeight, rankSplitHeight),
            qMax(headerHeight, qMax(hunterH, starsH)));
        // Keep the established default spacing, but let font/image metrics
        // override it when a small scoreboard scale would clip two lines.
        const int rowH = qMax(autoRowHeight, scaleDs(22));

        const QString playerHeader = MelonPrime::UiText::Tr("Player");
        const QString pointsHeader = MelonPrime::UiText::Tr("Points");
        const QString deathsHeader = MelonPrime::UiText::Tr("Deaths");
        int playerW = textWidthDS(p, playerHeader);
        int pointsW = textWidthDS(p, pointsHeader);
        int deathsW = textWidthDS(p, deathsHeader);
        int portraitW = qMax(scaleDs(20), hunterH + scaleDs(4));
        for (const PreviewRow& row : rows) {
            playerW = qMax(playerW, textWidthDS(p, QString::fromLatin1(row.name)));
            const QString rankLabel = QStringLiteral("%1 %2")
                .arg(MelonPrime::UiText::Tr("Rank")).arg(row.rank);
            int rankLineW = rankShow ? textWidthDS(p, rankLabel) : 0;
            const QImage& stars = starsImage(row.stars, starsH);
            rankLineW += stars.isNull() ? 0
                : (rankShow ? scaleDs(3) : 0) + imageWidthDS(stars);
            playerW = qMax(playerW, rankLineW);
            const QImage& hunter = hunterIcon(row.hunter, hunterH);
            portraitW = qMax(portraitW, hunter.isNull() ? 0 : hunter.width() + scaleDs(2));
            pointsW = qMax(pointsW, textWidthDS(p, QString::number(row.points)));
            deathsW = qMax(deathsW, textWidthDS(p, QString::number(row.deaths)));
        }
        playerW = qMax(1, playerW + scaleDs(2));
        pointsW = qMax(scaleDs(20), pointsW + scaleDs(2));
        deathsW = qMax(scaleDs(20), deathsW + scaleDs(2));

        const int fixedW = portraitW + scaleDs(3) + pointsW + deathsW
                         + columnSpacing * 2 + rightPadding;
        const int naturalW = fixedW + playerW;
        const int panelW = qMax(fixedW + 1, qMin(256, naturalW));
        playerW = qMax(1, panelW - fixedW);
        const int panelH = rowH + 4 * (rowH + rowSpacing) + scaleDs(4);

        int panelX = 0;
        int panelY = 0;
        const QPoint anchor = dsPos(
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardAnchor), 8),
            c.GetInt(MP_HUD_PROP_KEY_HudScoreboardX),
            c.GetInt(MP_HUD_PROP_KEY_HudScoreboardY));
        panelX = anchor.x();
        panelY = anchor.y();
        MelonPrime::HudGeometry::ApplyRectAnchor(
            panelX, panelY, panelW, panelH,
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardAnchor), 8) % 3,
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardAnchor), 8) / 3);

        const int nameX = panelX + portraitW + scaleDs(3);
        const int pointsX = nameX + playerW + columnSpacing;
        const int deathsX = pointsX + pointsW + columnSpacing;

        QColor background = readColor(
            MP_HUD_PROP_KEY_HudScoreboardBackgroundColorR,
            MP_HUD_PROP_KEY_HudScoreboardBackgroundColorG,
            MP_HUD_PROP_KEY_HudScoreboardBackgroundColorB,
            static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_HudScoreboardBackgroundOpacity)));
        const float panelOpacity = qBound(
            0.0f, static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_HudScoreboardOpacity)), 1.0f);
        p.setOpacity(panelOpacity);
        p.fillRect(QRectF(panelX, panelY, panelW, panelH), background);

        const QColor headerColor = readColor(
            MP_HUD_PROP_KEY_HudScoreboardHeaderColorR,
            MP_HUD_PROP_KEY_HudScoreboardHeaderColorG,
            MP_HUD_PROP_KEY_HudScoreboardHeaderColorB);
        const QColor outlineColor = readColor(
            MP_HUD_PROP_KEY_HudScoreboardOutlineColorR,
            MP_HUD_PROP_KEY_HudScoreboardOutlineColorG,
            MP_HUD_PROP_KEY_HudScoreboardOutlineColorB);
        const float outlineOpacity = qBound(
            0.0f, static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_HudScoreboardOutlineOpacity)), 1.0f);
        const int outlineThickness = qMax(
            1, c.GetInt(MP_HUD_PROP_KEY_HudScoreboardOutlineThickness));
        const bool outlineEnabled = c.GetBool(MP_HUD_PROP_KEY_HudScoreboardOutline);
        const QColor effectiveOutlineColor = c.GetBool(MP_HUD_PROP_KEY_HudGlobalOutline)
            ? readColor(MP_HUD_PROP_KEY_HudGlobalOutlineColorR,
                        MP_HUD_PROP_KEY_HudGlobalOutlineColorG,
                        MP_HUD_PROP_KEY_HudGlobalOutlineColorB)
            : outlineColor;
        const float effectiveOutlineOpacity = c.GetBool(MP_HUD_PROP_KEY_HudGlobalOutline)
            ? qBound(0.0f, static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_HudGlobalOutlineOpacity)), 1.0f)
            : outlineOpacity;
        const int effectiveOutlineThickness = c.GetBool(MP_HUD_PROP_KEY_HudGlobalOutline)
            ? qMax(1, c.GetInt(MP_HUD_PROP_KEY_HudGlobalOutlineThickness))
            : outlineThickness;
        const bool effectiveOutlineEnabled = c.GetBool(MP_HUD_PROP_KEY_HudGlobalOutline)
            || outlineEnabled;
        const int headerCellHeight = qMax(1, rowH - headerInset * 2);
        const QRectF playerHeaderRect(nameX, panelY + headerInset, playerW, headerCellHeight);
        const QRectF pointsHeaderRect(pointsX, panelY + headerInset, pointsW, headerCellHeight);
        const QRectF deathsHeaderRect(deathsX, panelY + headerInset, deathsW, headerCellHeight);
        drawCellText(p, playerHeaderRect, playerHeader, headerColor, Qt::AlignLeft,
                     effectiveOutlineColor, effectiveOutlineEnabled ? effectiveOutlineOpacity : 0.0f,
                     effectiveOutlineThickness, scoreboardScale);
        drawCellText(p, pointsHeaderRect, pointsHeader, headerColor, Qt::AlignRight,
                     effectiveOutlineColor, effectiveOutlineEnabled ? effectiveOutlineOpacity : 0.0f,
                     effectiveOutlineThickness, scoreboardScale);
        drawCellText(p, deathsHeaderRect, deathsHeader, headerColor, Qt::AlignRight,
                     effectiveOutlineColor, effectiveOutlineEnabled ? effectiveOutlineOpacity : 0.0f,
                     effectiveOutlineThickness, scoreboardScale);

        const QColor textColor = readColor(
            MP_HUD_PROP_KEY_HudScoreboardTextColorR,
            MP_HUD_PROP_KEY_HudScoreboardTextColorG,
            MP_HUD_PROP_KEY_HudScoreboardTextColorB);
        const QColor localColor = readColor(
            MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorR,
            MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorG,
            MP_HUD_PROP_KEY_HudScoreboardLocalPlayerColorB);
        for (int i = 0; i < 4; ++i) {
            const PreviewRow& row = rows[i];
            const int y = panelY + rowH + i * (rowH + rowSpacing);
            const QRectF nameRect(nameX, y, playerW, rowH);
            const QRectF pointsRect(pointsX, y, pointsW, rowH);
            const QRectF deathsRect(deathsX, y, deathsW, rowH);
            const QColor& rowColor = (i == 0) ? localColor : textColor;

            if (hunterH > 0) {
                const QImage& hunter = hunterIcon(row.hunter, hunterH);
                if (!hunter.isNull())
                    p.drawImage(QRectF(panelX + headerInset, y + (rowH - hunter.height()) * 0.5,
                                       hunter.width(), hunter.height()), hunter);
            }

            drawCellText(p, QRectF(nameRect.x(), nameRect.y(), nameRect.width(), rowH * 0.58),
                         QString::fromLatin1(row.name), rowColor, Qt::AlignLeft,
                         effectiveOutlineColor, effectiveOutlineEnabled ? effectiveOutlineOpacity : 0.0f,
                         effectiveOutlineThickness, scoreboardScale);
            const QString rankLabel = QStringLiteral("%1 %2")
                .arg(MelonPrime::UiText::Tr("Rank")).arg(row.rank);
            const QRectF rankRect(nameRect.x(), y + rowH * 0.58,
                                  nameRect.width(), rowH * 0.42);
            if (rankShow)
                drawCellText(p, rankRect, rankLabel, rowColor, Qt::AlignLeft,
                             effectiveOutlineColor, effectiveOutlineEnabled ? effectiveOutlineOpacity : 0.0f,
                             effectiveOutlineThickness, scoreboardScale);
            if (starsH > 0) {
                const QImage& stars = starsImage(row.stars, starsH);
                if (!stars.isNull()) {
                    const int rankW = rankShow ? textWidthDS(p, rankLabel) : 0;
                    const float starsX = nameX + rankW
                        + (rankW > 0 ? scaleDs(3) : 0);
                    const float available = qMax(0.0f,
                        static_cast<float>(nameRect.right()) - starsX - scaleDs(2));
                    const float starsW = qMin(static_cast<float>(stars.width()), available);
                    if (starsW > 0.0f)
                        p.drawImage(QRectF(starsX,
                                           rankRect.y() + (rankRect.height() - stars.height()) * 0.5,
                                           starsW, stars.height()), stars);
                }
            }
            drawCellText(p, pointsRect, QString::number(row.points), rowColor, Qt::AlignRight,
                         effectiveOutlineColor, effectiveOutlineEnabled ? effectiveOutlineOpacity : 0.0f,
                         effectiveOutlineThickness, scoreboardScale);
            drawCellText(p, deathsRect, QString::number(row.deaths), rowColor, Qt::AlignRight,
                         effectiveOutlineColor, effectiveOutlineEnabled ? effectiveOutlineOpacity : 0.0f,
                         effectiveOutlineThickness, scoreboardScale);
        }
        p.setOpacity(1.0);
    }
};

// Settings-dialog preview for the Enemy Target panel. Derives from the
// scoreboard preview only to reuse its raster loader; the two keep separate
// icon caches so their different heights do not thrash one another.
class EnemyTargetPreviewWidget : public ScoreboardPreviewWidget
{
public:
    using ScoreboardPreviewWidget::ScoreboardPreviewWidget;

protected:
    static const QImage& targetHunterIcon(int index, int height)
    {
        static QImage icons[7];
        static int cachedHeight = -1;
        if (cachedHeight != height) {
            static const char* const paths[7] = {
                ":/mph-scoreboard-hunter-0", ":/mph-scoreboard-hunter-1",
                ":/mph-scoreboard-hunter-2", ":/mph-scoreboard-hunter-3",
                ":/mph-scoreboard-hunter-4", ":/mph-scoreboard-hunter-5",
                ":/mph-scoreboard-hunter-6",
            };
            for (int i = 0; i < 7; ++i)
                icons[i] = loadRasterToHeight(paths[i], height);
            cachedHeight = height;
        }
        return icons[qBound(0, index, 6)];
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        if (!c.GetBool(MP_HUD_PROP_KEY_HudEnemyTargetShow)) {
            p.setPen(QColor(130, 130, 150));
            p.drawText(r, Qt::AlignCenter, MelonPrime::UiText::Tr("Enemy Target OFF"));
            return;
        }

        setupDsTransform(p);
        p.setRenderHint(QPainter::SmoothPixmapTransform, false);

        const float scale = qBound(
            0.1f, c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetScale) / 100.0f, 8.0f);
        const float textScale = qBound(
            0.1f, c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetTextScale) / 100.0f, 8.0f);
        QFont font = previewHudFont(c, MP_HUD_PROP_KEY_HudAutoScaleCapText);
        font.setPixelSize(qMax(1, qRound(font.pixelSize() * textScale * scale)));
        p.setFont(font);
        const QFontMetrics fm(font);

        const int spacing = qRound(
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetRowSpacing), 16) * scale);
        const int barLen = qRound(
            qBound(8, c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetBarLength), 192) * scale);
        const int barH = qRound(
            qBound(1, c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetBarHeight), 20) * scale);
        const int hpMode = qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetHpMode), 3);
        const bool nameShow = c.GetBool(MP_HUD_PROP_KEY_HudEnemyTargetNameShow);
        const bool scoreShow = c.GetBool(MP_HUD_PROP_KEY_HudEnemyTargetScoreShow);
        const bool numberShow =
            c.GetBool(MP_HUD_PROP_KEY_HudEnemyTargetHpNumberShow) || hpMode == 2;
        const bool iconShow = c.GetBool(MP_HUD_PROP_KEY_HudEnemyTargetHunterIconShow);
        const int iconH = qRound(
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetHunterIconHeight), 48) * scale);

        // Same sample values as MakeEnemyTargetPreviewSnapshot() so the dialog
        // preview and the in-game editor show the same panel.
        const QString name = QStringLiteral("Player 2");
        const QString number = QStringLiteral("145/199");
        const QString score = QStringLiteral("4 / 7");

        const int lineH = fm.height();
        int columnH = 0;
        auto addRow = [&](int h) {
            if (columnH > 0) columnH += spacing;
            const int top = columnH;
            columnH += h;
            return top;
        };
        const int nameY   = nameShow ? addRow(lineH) : 0;
        const int bar1Y   = (hpMode == 0 || hpMode == 1) ? addRow(barH) : 0;
        const int bar2Y   = (hpMode == 0) ? addRow(barH) : 0;
        const int numberY = numberShow ? addRow(lineH) : 0;
        const int scoreY  = scoreShow ? addRow(lineH) : 0;
        if (columnH <= 0) columnH = lineH;

        int columnW = (hpMode == 0 || hpMode == 1) ? barLen : 0;
        if (nameShow)   columnW = qMax(columnW, fm.horizontalAdvance(name));
        if (numberShow) columnW = qMax(columnW, fm.horizontalAdvance(number));
        if (scoreShow)  columnW = qMax(columnW, fm.horizontalAdvance(score));
        columnW = qMax(columnW, 1);

        const QImage& icon = iconShow ? targetHunterIcon(2, iconH) : QImage();
        const int iconW = icon.isNull() ? 0 : icon.width();
        const int gap = icon.isNull() ? 0 : qMax(1, spacing);
        const int panelW = iconW + gap + columnW;
        const int panelH = qMax(columnH, icon.isNull() ? 0 : icon.height());

        const QPoint origin = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetAnchor),
                                    c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetX),
                                    c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetY));
        float px = origin.x(), py = origin.y();
        const int anchor = qBound(0, c.GetInt(MP_HUD_PROP_KEY_HudEnemyTargetAnchor), 8);
        MelonPrime::HudGeometry::ApplyRectAnchorF(px, py, panelW, panelH,
                                                  anchor % 3, anchor / 3);
        const int textX = qRound(px) + panelW - columnW;

        p.setOpacity(qBound(0.0, c.GetDouble(MP_HUD_PROP_KEY_HudEnemyTargetOpacity), 1.0));
        if (!icon.isNull()) {
            p.drawImage(QRect(qRound(px), qRound(py + (panelH - icon.height()) * 0.5f),
                              icon.width(), icon.height()), icon);
        }
        const QColor nameColor = readColor(MP_HUD_PROP_KEY_HudEnemyTargetColorR,
                                           MP_HUD_PROP_KEY_HudEnemyTargetColorG,
                                           MP_HUD_PROP_KEY_HudEnemyTargetColorB);
        const QColor hpColor = readColor(MP_HUD_PROP_KEY_HudEnemyTargetHpColorR,
                                         MP_HUD_PROP_KEY_HudEnemyTargetHpColorG,
                                         MP_HUD_PROP_KEY_HudEnemyTargetHpColorB);
        const QColor hpBack = readColor(MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorR,
                                        MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorG,
                                        MP_HUD_PROP_KEY_HudEnemyTargetHpBackColorB);
        const QColor scoreColor = readColor(MP_HUD_PROP_KEY_HudEnemyTargetScoreColorR,
                                            MP_HUD_PROP_KEY_HudEnemyTargetScoreColorG,
                                            MP_HUD_PROP_KEY_HudEnemyTargetScoreColorB);
        const auto drawLine = [&](const QString& text, int y, const QColor& color) {
            p.setPen(color);
            p.drawText(QRect(textX, qRound(py) + y, columnW, lineH),
                       Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, text);
        };
        const auto drawBar = [&](int y, float ratio) {
            p.fillRect(QRect(textX, qRound(py) + y, barLen, barH), hpBack);
            p.fillRect(QRect(textX, qRound(py) + y, qRound(barLen * ratio), barH), hpColor);
        };
        if (nameShow)         drawLine(name, nameY, nameColor);
        if (hpMode == 0)      drawBar(bar1Y, 1.0f);
        else if (hpMode == 1) drawBar(bar1Y, 145.0f / 199.0f);
        if (hpMode == 0)      drawBar(bar2Y, 45.0f / 99.0f);
        if (numberShow)       drawLine(number, numberY, hpColor);
        if (scoreShow)        drawLine(score, scoreY, scoreColor);
        p.setOpacity(1.0);
    }
};

class CrosshairPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        // Preview magnification × user Crosshair Scale % (matches the runtime
        // chScale = CrosshairScale/100 applied in LoadCrosshairConfig).
        const float autoScale = previewAutoScale(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapCrosshair);
        const float cs = qMax(100, c.GetInt(MP_HUD_PROP_KEY_CrosshairScale))
                       / 100.0f * autoScale;
        drawBaseCrosshair(p, r.center().x(), r.center().y(), 2.0f * cs, 1.0f);
    }
};

// Fully-scoped zoom reticle preview. Mirrors DrawZoomScopeReticle() at scopeT=1
// (ring + cross arms clipped to the ring + center dot, optional outline). Drawn
// at the screen-accurate DS scale (the preview box maps the 256-px-wide DS top
// screen), so Scope Radius / Gap / Thickness / Dot Size all visibly react and a
// radius of 128 spans the full width exactly as it does in-game. Large radii
// clip to the box edge (QPainter clips for free), which is faithful.
class ZoomScopePreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        // Match the runtime cachedZoomScopeRenderable gate: stage + scope enabled.
        const bool renderable =
            c.GetBool(MP_HUD_PROP_KEY_CrosshairZoomStageEnable) &&
            c.GetBool(MP_HUD_PROP_KEY_CrosshairZoomScopeEnable);
        if (!renderable) {
            p.setPen(QColor(120, 120, 140));
            p.drawText(r, Qt::AlignCenter,
                       MelonPrime::UiText::Tr(QStringLiteral("Scope reticle off")));
            return;
        }

        const double scopeOpacity = qBound(0, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeOpacity), 100) / 100.0;
        const float autoScale = previewAutoScale(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapCrosshair);
        const int radius = qMax(1, qRound(
            qBound(4, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeRadius), 1024)
            * autoScale));
        const int gap = qMax(0, qRound(
            qBound(0, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeGap), 64)
            * autoScale));
        const int thickness = qMax(1, qRound(
            qBound(1, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeThickness), 12)
            * autoScale));

        const int cx = r.center().x();
        const int cy = r.center().y();
        if (scopeOpacity <= 0.001) return;

        // Faded, zoom-scaled base crosshair underneath the scope ring. This makes
        // Zoom Base Scale % and Zoom Base Opacity % visibly functional. The default
        // Zoom Base Opacity is 0 (base hidden while fully scoped, as in-game), so
        // nothing shows until the user raises it — then it scales by Zoom Base Scale %.
        {
            const float cs = qMax(100, c.GetInt(MP_HUD_PROP_KEY_CrosshairScale))
                           / 100.0f * autoScale;
            const float zoomScale = qBound(10, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScale), 200) / 100.0f;
            const float zoomBaseOpacity = qBound(0, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomOpacity), 100) / 100.0f;
            drawBaseCrosshair(p, cx, cy, 2.0f * cs * zoomScale, zoomBaseOpacity);
        }

        // Screen-accurate scale (preview box maps the 256-wide DS top screen).
        // No auto-fit, so the ring size tracks Scope Radius.
        const double scale = static_cast<double>(r.width()) / 256.0;
        const int dRadius = qMax(1, static_cast<int>(radius * scale + 0.5));
        const int dGap    = qMin(static_cast<int>(gap * scale + 0.5), qMax(0, dRadius - 1));
        const int dArmEnd = qMax(dGap + 1, dRadius);
        const int dThick  = qMax(1, static_cast<int>(thickness * scale + 0.5));

        const QColor baseCol = readColor(MP_HUD_PROP_KEY_CrosshairColorR, MP_HUD_PROP_KEY_CrosshairColorG, MP_HUD_PROP_KEY_CrosshairColorB);

        QPainterPath circleClip;
        circleClip.addEllipse(QRect(cx - dRadius, cy - dRadius, dRadius * 2, dRadius * 2));

        auto drawArms = [&](const QColor& color, int width, double opacity) {
            if (dArmEnd <= dGap) return;
            p.save();
            p.setClipPath(circleClip, Qt::IntersectClip);
            QPen pen(color); pen.setWidth(qMax(1, width)); pen.setCapStyle(Qt::SquareCap);
            p.setPen(pen); p.setBrush(Qt::NoBrush); p.setOpacity(opacity);
            p.drawLine(QPoint(cx - dArmEnd, cy), QPoint(cx - dGap, cy));
            p.drawLine(QPoint(cx + dGap, cy), QPoint(cx + dArmEnd, cy));
            p.drawLine(QPoint(cx, cy - dArmEnd), QPoint(cx, cy - dGap));
            p.drawLine(QPoint(cx, cy + dGap), QPoint(cx, cy + dArmEnd));
            p.restore();
        };
        auto drawCircle = [&](const QColor& color, int width, double opacity) {
            QPen pen(color); pen.setWidth(qMax(1, width)); pen.setCapStyle(Qt::SquareCap);
            p.setPen(pen); p.setBrush(Qt::NoBrush); p.setOpacity(opacity);
            p.drawEllipse(QRect(cx - dRadius, cy - dRadius, dRadius * 2, dRadius * 2));
        };
        const bool hasDot = c.GetBool(MP_HUD_PROP_KEY_CrosshairZoomScopeCenterDot)
                            && c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeDotOpacity) > 0;
        const bool dotEllipse = c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeDotShape) == 1;
        auto drawDot = [&](const QColor& color, int size, double opacity) {
            if (!hasDot) return;
            const int dotSize = qMax(1, size);
            const int dotHalf = dotSize / 2;
            const QRect dotRect(cx - dotHalf, cy - dotHalf, dotSize, dotSize);
            p.setPen(Qt::NoPen); p.setBrush(color); p.setOpacity(opacity);
            if (dotEllipse) p.drawEllipse(dotRect);
            else            p.drawRect(dotRect);
        };

        const int dotSizeDs = qMax(1, qRound(
            qBound(1, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeDotSize), 32)
            * autoScale));
        const int dDotSize  = qMax(1, static_cast<int>(dotSizeDs * scale + 0.5));
        const double dotOpacity = qBound(0, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeDotOpacity), 100) / 100.0;
        QColor dotColor = c.GetBool(MP_HUD_PROP_KEY_CrosshairZoomScopeDotCustomColor)
            ? readColor(MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorR, MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorG, MP_HUD_PROP_KEY_CrosshairZoomScopeDotColorB)
            : baseCol;

        // Outline pass (matches runtime: width = thickness + outlineThickness*2).
        if (c.GetBool(MP_HUD_PROP_KEY_CrosshairOutline)) {
            const double olOpacity = c.GetDouble(MP_HUD_PROP_KEY_CrosshairOutlineOpacity);
            if (olOpacity > 0.0) {
                const int olT = qMax(1, c.GetInt(MP_HUD_PROP_KEY_CrosshairOutlineThickness));
                const QColor black(0, 0, 0, 255);
                const double oa = olOpacity * scopeOpacity;
                drawArms(black, dThick + olT * 2, oa);
                drawCircle(black, dThick + olT * 2, oa);
                drawDot(black, dDotSize + olT * 2, oa);
            }
        }
        drawArms(baseCol, dThick, scopeOpacity);
        drawCircle(baseCol, dThick, scopeOpacity);
        drawDot(dotColor, dDotSize, scopeOpacity * dotOpacity);
    }
};

class HpAmmoPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        setupDsTransform(p);
        p.setFont(previewHudFont(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapText, nullptr, 500));
        const float gaugeAutoScale = previewAutoScale(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapGauges);
        const float iconAutoScale = previewAutoScale(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapIcons);

        // ── HP text ──
        {
            float op = c.GetDouble(MP_HUD_PROP_KEY_HudHpOpacity);
            QColor col = readColor(MP_HUD_PROP_KEY_HudHpTextColorR, MP_HUD_PROP_KEY_HudHpTextColorG, MP_HUD_PROP_KEY_HudHpTextColorB, op);
            QPoint pos = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudHpAnchor), c.GetInt(MP_HUD_PROP_KEY_HudHpX), c.GetInt(MP_HUD_PROP_KEY_HudHpY));
            QString str = QString::fromStdString(c.GetString(MP_HUD_PROP_KEY_HudHpPrefix)) + "100";
            int tw = textWidthDS(p, str);
            int th = static_cast<int>(p.fontMetrics().height() * 192.0f / height());

            bool showGauge = c.GetBool(MP_HUD_PROP_KEY_HudHpGauge);
            int mode = showGauge ? c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosMode) : 0;
            int ori  = c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeOrientation);
            int glen = qMax(1, qRound(
                c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeLength) * gaugeAutoScale));
            int gwid = qMax(1, qRound(
                c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeWidth) * gaugeAutoScale));
            int galign = c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeAlign);

            int tx, textY;
            int gx = 0, gy = 0;

            if (showGauge && mode == 2) {
                // Text → Gauge: gauge at independent pos, text derived from gauge
                QPoint gp = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosAnchor), c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosX), c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosY));
                gx = gp.x(); gy = gp.y();
                int egx = gx, egy = gy;
                MelonPrime::HudGeometry::ApplyGaugeAlign(egx, egy, ori, glen, galign);
                int tAnc = c.GetInt(MP_HUD_PROP_KEY_HudHpTextAnchor);
                int ofsX = c.GetInt(MP_HUD_PROP_KEY_HudHpTextOffsetX);
                int ofsY = c.GetInt(MP_HUD_PROP_KEY_HudHpTextOffsetY);
                MelonPrime::HudGeometry::CalcTextPosFromGaugeInt(egx, egy, glen, gwid, ori,
                                                                  tAnc, ofsX, ofsY,
                                                                  tw, th, tx, textY);
            } else {
                tx    = alignedX(pos.x(), c.GetInt(MP_HUD_PROP_KEY_HudHpAlign), tw);
                textY = pos.y();
                if (showGauge && mode == 1) {
                    QPoint gp = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosAnchor), c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosX), c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosY));
                    gx = gp.x(); gy = gp.y();
                } else if (showGauge) { // mode 0: gauge relative to text
                    gx = tx    + c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeOffsetX);
                    gy = pos.y() + c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeOffsetY) + 2;
                }
            }

            p.setPen(col);
            p.drawText(QPoint(tx, textY), str);

            if (showGauge) {
                float gop = c.GetDouble(MP_HUD_PROP_KEY_HudHpGaugeOpacity);
                QColor gc = readColor(MP_HUD_PROP_KEY_HudHpGaugeColorR, MP_HUD_PROP_KEY_HudHpGaugeColorG, MP_HUD_PROP_KEY_HudHpGaugeColorB, gop);
                drawGaugeDS(p, gx, gy, gc, ori, glen, gwid, galign);
            }
        }

        // ── Weapon icon ──
        if (c.GetInt(MP_HUD_PROP_KEY_HudWeaponIconMode) != 0) {
            static QImage s_magmaul;
            static int s_magmaulH = 0;
            int wantH = qMax(1, qRound(
                c.GetInt(MP_HUD_PROP_KEY_HudWeaponIconHeight) * iconAutoScale));
            if (s_magmaul.isNull() || s_magmaulH != wantH) {
                QImageReader rd(QStringLiteral(":/mph-icon-magmaul"));
                QSize sz = rd.size(); if (sz.isEmpty()) sz = QSize(wantH, wantH);
                rd.setScaledSize(QSize(qMax(1, sz.width()*wantH/qMax(1,sz.height())), wantH));
                s_magmaul = rd.read(); s_magmaulH = wantH;
            }
            if (!s_magmaul.isNull()) {
                QPoint ip = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudWeaponIconPosAnchor),
                                  c.GetInt(MP_HUD_PROP_KEY_HudWeaponIconPosX),
                                  c.GetInt(MP_HUD_PROP_KEY_HudWeaponIconPosY));
                int ancX = c.GetInt(MP_HUD_PROP_KEY_HudWeaponIconAnchorX);
                int ancY = c.GetInt(MP_HUD_PROP_KEY_HudWeaponIconAnchorY);
                int iw = s_magmaul.width(), ih = s_magmaul.height();
                int ix = ip.x(), iy = ip.y();
                MelonPrime::HudGeometry::ApplyRectAnchor(ix, iy, iw, ih, ancX, ancY);
                p.drawImage(QRectF(ix, iy, iw, ih), s_magmaul);
            }
        }

        // ── Ammo text ──
        {
            float op = c.GetDouble(MP_HUD_PROP_KEY_HudWeaponOpacity);
            QColor col = readColor(MP_HUD_PROP_KEY_HudAmmoTextColorR, MP_HUD_PROP_KEY_HudAmmoTextColorG, MP_HUD_PROP_KEY_HudAmmoTextColorB, op);
            QPoint pos = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudWeaponAnchor), c.GetInt(MP_HUD_PROP_KEY_HudWeaponX), c.GetInt(MP_HUD_PROP_KEY_HudWeaponY));
            QString str = QString::fromStdString(c.GetString(MP_HUD_PROP_KEY_HudAmmoPrefix)) + "50";
            int tw = textWidthDS(p, str);
            int th = static_cast<int>(p.fontMetrics().height() * 192.0f / height());

            bool showGauge = c.GetBool(MP_HUD_PROP_KEY_HudAmmoGauge);
            int mode = showGauge ? c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosMode) : 0;
            int ori  = c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeOrientation);
            int glen = qMax(1, qRound(
                c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeLength) * gaugeAutoScale));
            int gwid = qMax(1, qRound(
                c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeWidth) * gaugeAutoScale));
            int galign = c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeAlign);

            int tx, textY;
            int gx = 0, gy = 0;

            if (showGauge && mode == 2) {
                // Text → Gauge: gauge at independent pos, text derived from gauge
                QPoint gp = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosAnchor), c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosX), c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosY));
                gx = gp.x(); gy = gp.y();
                int egx = gx, egy = gy;
                MelonPrime::HudGeometry::ApplyGaugeAlign(egx, egy, ori, glen, galign);
                int tAnc = c.GetInt(MP_HUD_PROP_KEY_HudAmmoTextAnchor);
                int ofsX = c.GetInt(MP_HUD_PROP_KEY_HudAmmoTextOffsetX);
                int ofsY = c.GetInt(MP_HUD_PROP_KEY_HudAmmoTextOffsetY);
                MelonPrime::HudGeometry::CalcTextPosFromGaugeInt(egx, egy, glen, gwid, ori,
                                                                  tAnc, ofsX, ofsY,
                                                                  tw, th, tx, textY);
            } else {
                tx    = alignedX(pos.x(), c.GetInt(MP_HUD_PROP_KEY_HudAmmoAlign), tw);
                textY = pos.y();
                if (showGauge && mode == 1) {
                    QPoint gp = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosAnchor), c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosX), c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosY));
                    gx = gp.x(); gy = gp.y();
                } else if (showGauge) { // mode 0: gauge relative to text
                    gx = tx    + c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeOffsetX);
                    gy = pos.y() + c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeOffsetY) + 2;
                }
            }

            p.setPen(col);
            p.drawText(QPoint(tx, textY), str);

            if (showGauge) {
                float gop = c.GetDouble(MP_HUD_PROP_KEY_HudAmmoGaugeOpacity);
                QColor gc = readColor(MP_HUD_PROP_KEY_HudAmmoGaugeColorR, MP_HUD_PROP_KEY_HudAmmoGaugeColorG, MP_HUD_PROP_KEY_HudAmmoGaugeColorB, gop);
                drawGaugeDS(p, gx, gy, gc, ori, glen, gwid, galign);
            }
        }

        // ── Weapon Inventory ──
        // Representative layout preview: all 9 weapon icons in display order with
        // the configured orientation/align/spacing/icon-height, owned vs not-owned
        // opacity, and the selection highlight box. (Per-icon ammo text is omitted;
        // the separate Ammo element above already previews ammo styling.)
        if (c.GetBool(MP_HUD_PROP_KEY_HudWeaponInventoryShow)) {
            // weaponId per display slot — matches runtime kInventoryOrder.
            static const int kOrder[9] = { 0, 2, 7, 6, 5, 4, 3, 1, 8 };
            // icon resource indexed by weaponId — matches runtime EnsureIconsLoaded.
            static const char* kIconAlias[9] = {
                ":/mph-icon-pb", ":/mph-icon-volt", ":/mph-icon-missile",
                ":/mph-icon-battlehammer", ":/mph-icon-imperialist",
                ":/mph-icon-judicator", ":/mph-icon-magmaul",
                ":/mph-icon-shock", ":/mph-icon-omega"
            };
            // Representative ownership by display slot: last three shown as not-owned
            // so the Not Owned Opacity is visible in the preview.
            static const bool kOwned[9] = { true, true, true, true, true, true, false, false, false };
            const int highlightWeaponId = kOrder[0]; // highlight the first slot

            const int iconH   = qBound(4, qRound(
                c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryIconHeight) * iconAutoScale), 48);
            const int spacing = qMax(0, c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventorySpacing));
            const int orient  = c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryOrientation); // 0=horiz 1=vert
            const int align   = c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryAlign);        // 0/1/2
            const float opOwned    = static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_HudWeaponInventoryOpacity));
            const float opNotOwned = static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_HudWeaponInventoryNotOwnedOpacity));
            const QPoint pos = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryAnchor),
                                     c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryX),
                                     c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryY));

            // Icon cache keyed by requested height (rendered at 2x for crispness).
            static QImage s_invIcons[9];
            static int s_invIconH = 0;
            const int loadH = qMax(1, iconH * 2);
            if (s_invIconH != loadH) {
                for (int i = 0; i < 9; ++i) {
                    QImageReader rd(QString::fromLatin1(kIconAlias[i]));
                    QSize sz = rd.size(); if (sz.isEmpty()) sz = QSize(loadH, loadH);
                    rd.setScaledSize(QSize(qMax(1, sz.width() * loadH / qMax(1, sz.height())), loadH));
                    s_invIcons[i] = rd.read();
                }
                s_invIconH = loadH;
            }

            float maxAspect = 1.0f;
            for (int i = 0; i < 9; ++i)
                if (!s_invIcons[i].isNull() && s_invIcons[i].height() > 0)
                    maxAspect = qMax(maxAspect, float(s_invIcons[i].width()) / s_invIcons[i].height());
            const float maxDrawIW = iconH * maxAspect;

            const float startX = pos.x();
            const float startY = pos.y();
            const float iconCenterX = (orient == 1)
                ? (align == 1 ? startX
                 : align == 2 ? startX - maxDrawIW * 0.5f
                 :              startX + maxDrawIW * 0.5f)
                : 0.0f;

            const bool  hlEnable    = c.GetBool(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightEnable);
            const float hlOpacity   = static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightOpacity));
            const float hlThickness = static_cast<float>(c.GetDouble(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightThickness));
            const int   hlPad       = qMax(0, c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightPadding));
            const int   hlCorner    = qMax(0, c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightCornerRadius));
            const int   ofsL = c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetLeft);
            const int   ofsR = c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetRight);
            const int   ofsT = c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetTop);
            const int   ofsB = c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightSizeOffsetBottom);
            const QColor hlColor = readColor(MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorR,
                                             MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorG,
                                             MP_HUD_PROP_KEY_HudWeaponInventoryHighlightColorB);

            p.setRenderHint(QPainter::Antialiasing, true);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            float curX = startX, curY = startY;
            for (int si = 0; si < 9; ++si) {
                const int wid = kOrder[si];
                const QImage& icon = s_invIcons[wid];
                const float drawIH = iconH;
                const float drawIW = (!icon.isNull() && icon.height() > 0)
                                     ? drawIH * float(icon.width()) / icon.height() : drawIH;
                const float eff = kOwned[si] ? opOwned : opNotOwned;

                float px, py;
                if (orient == 1) {
                    px = iconCenterX - drawIW * 0.5f;
                    py = curY;
                } else {
                    px = curX;
                    py = align == 1 ? startY - drawIH * 0.5f
                       : align == 2 ? startY - drawIH
                       :              startY;
                }

                if (eff > 0.0f) {
                    if (hlEnable && wid == highlightWeaponId && hlOpacity > 0.0f) {
                        const QRectF hlRect(px - hlPad - ofsL, py - hlPad - ofsT,
                                            drawIW + 2 * hlPad + ofsL + ofsR,
                                            drawIH + 2 * hlPad + ofsT + ofsB);
                        p.setOpacity(hlOpacity);
                        QPen hlPen(hlColor); hlPen.setWidthF(hlThickness);
                        p.setPen(hlPen); p.setBrush(Qt::NoBrush);
                        p.drawRoundedRect(hlRect, hlCorner, hlCorner);
                    }
                    if (!icon.isNull()) {
                        p.setOpacity(eff);
                        p.drawImage(QRectF(px, py, drawIW, drawIH), icon);
                    }
                }

                if (orient == 1) curY += drawIH + spacing;
                else             curX += drawIW + spacing;
            }
            p.setOpacity(1.0);
        }
    }
};

class MatchStatusPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        setupDsTransform(p);
        p.setFont(previewHudFont(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapText, nullptr, 500));
        const float iconAutoScale = previewAutoScale(
            c, MP_HUD_PROP_KEY_HudAutoScaleCapIcons);

        // ── Score ──
        if (c.GetBool(MP_HUD_PROP_KEY_HudMatchStatusShow)) {
            float op = c.GetDouble(MP_HUD_PROP_KEY_HudMatchStatusOpacity);
            QColor col = readColor(MP_HUD_PROP_KEY_HudMatchStatusColorR, MP_HUD_PROP_KEY_HudMatchStatusColorG, MP_HUD_PROP_KEY_HudMatchStatusColorB, op);
            QPoint pos = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudMatchStatusAnchor), c.GetInt(MP_HUD_PROP_KEY_HudMatchStatusX), c.GetInt(MP_HUD_PROP_KEY_HudMatchStatusY));
            p.setPen(col);
            p.drawText(pos, "3 / 7");
        }

        // ── Rank ──
        if (c.GetBool(MP_HUD_PROP_KEY_HudRankShow)) {
            float op = c.GetDouble(MP_HUD_PROP_KEY_HudRankOpacity);
            QColor col = readColor(MP_HUD_PROP_KEY_HudRankColorR, MP_HUD_PROP_KEY_HudRankColorG, MP_HUD_PROP_KEY_HudRankColorB, op);
            QPoint pos = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudRankAnchor), c.GetInt(MP_HUD_PROP_KEY_HudRankX), c.GetInt(MP_HUD_PROP_KEY_HudRankY));
            QString prefix = QString::fromStdString(c.GetString(MP_HUD_PROP_KEY_HudRankPrefix));
            QString suffix = QString::fromStdString(c.GetString(MP_HUD_PROP_KEY_HudRankSuffix));
            QString str = prefix + "1st" + suffix;
            int tx = alignedX(pos.x(), c.GetInt(MP_HUD_PROP_KEY_HudRankAlign), textWidthDS(p, str));
            p.setPen(col);
            p.drawText(QPoint(tx, pos.y()), str);
        }

        // ── Time Left ──
        if (c.GetBool(MP_HUD_PROP_KEY_HudTimeLeftShow)) {
            float op = c.GetDouble(MP_HUD_PROP_KEY_HudTimeLeftOpacity);
            QColor col = readColor(MP_HUD_PROP_KEY_HudTimeLeftColorR, MP_HUD_PROP_KEY_HudTimeLeftColorG, MP_HUD_PROP_KEY_HudTimeLeftColorB, op);
            QPoint pos = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudTimeLeftAnchor), c.GetInt(MP_HUD_PROP_KEY_HudTimeLeftX), c.GetInt(MP_HUD_PROP_KEY_HudTimeLeftY));
            QString str = QStringLiteral("4:32");
            int tx = alignedX(pos.x(), c.GetInt(MP_HUD_PROP_KEY_HudTimeLeftAlign), textWidthDS(p, str));
            p.setPen(col);
            p.drawText(QPoint(tx, pos.y()), str);
        }

        // ── Bomb Left ── (text + icon)
        if (c.GetBool(MP_HUD_PROP_KEY_HudBombLeftShow)) {
            float op = c.GetDouble(MP_HUD_PROP_KEY_HudBombLeftOpacity);
            QColor col = readColor(MP_HUD_PROP_KEY_HudBombLeftColorR, MP_HUD_PROP_KEY_HudBombLeftColorG, MP_HUD_PROP_KEY_HudBombLeftColorB, op);
            QPoint pos = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudBombLeftAnchor), c.GetInt(MP_HUD_PROP_KEY_HudBombLeftX), c.GetInt(MP_HUD_PROP_KEY_HudBombLeftY));
            QString prefix = QString::fromStdString(c.GetString(MP_HUD_PROP_KEY_HudBombLeftPrefix));
            QString suffix = QString::fromStdString(c.GetString(MP_HUD_PROP_KEY_HudBombLeftSuffix));
            QString str = c.GetBool(MP_HUD_PROP_KEY_HudBombLeftTextShow) ? prefix + "3" + suffix : prefix + suffix;
            if (!str.isEmpty()) {
                int tx = alignedX(pos.x(), c.GetInt(MP_HUD_PROP_KEY_HudBombLeftAlign), textWidthDS(p, str));
                p.setPen(col);
                p.drawText(QPoint(tx, pos.y()), str);
            }

            // Bomb icon
            if (c.GetBool(MP_HUD_PROP_KEY_HudBombLeftIconShow)) {
                static QImage s_bombIcon;
                static int s_bombIconH = 0;
                int wantBH = qMax(1, qRound(
                    c.GetInt(MP_HUD_PROP_KEY_HudBombIconHeight) * iconAutoScale));
                if (s_bombIcon.isNull() || s_bombIconH != wantBH) {
                    QImageReader rd(QStringLiteral(":/mph-icon-bombs3"));
                    QSize sz = rd.size(); if (sz.isEmpty()) sz = QSize(wantBH, wantBH);
                    rd.setScaledSize(QSize(qMax(1, sz.width()*wantBH/qMax(1,sz.height())), wantBH));
                    s_bombIcon = rd.read(); s_bombIconH = wantBH;
                }
                if (!s_bombIcon.isNull()) {
                    QPoint ip;
                    if (c.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconMode) == 0) {
                        // relative to text anchor
                        ip = QPoint(pos.x() + c.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconOfsX),
                                    pos.y() + c.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconOfsY));
                    } else {
                        ip = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconPosAnchor),
                                   c.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconPosX),
                                   c.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconPosY));
                    }
                    int ancX = c.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconAnchorX);
                    int ancY = c.GetInt(MP_HUD_PROP_KEY_HudBombLeftIconAnchorY);
                    int iw = s_bombIcon.width(), ih = s_bombIcon.height();
                    int ix = ip.x(), iy = ip.y();
                    MelonPrime::HudGeometry::ApplyRectAnchor(ix, iy, iw, ih, ancX, ancY);
                    p.setOpacity(op);
                    p.drawImage(QRectF(ix, iy, iw, ih), s_bombIcon);
                    p.setOpacity(1.0);
                }
            }
        }
    }
};

class RadarPreviewWidget : public HudPreviewWidget
{
public:
    using HudPreviewWidget::HudPreviewWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRect r = rect();
        drawBackground(p, r);

        auto& c = cfg();
        if (!c.GetBool(MP_HUD_PROP_KEY_BtmOverlayEnable)) return;

        setupDsTransform(p);

        float op  = c.GetDouble(MP_HUD_PROP_KEY_BtmOverlayOpacity);
        int   sz  = c.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstSize);
        QPoint pos = dsPos(c.GetInt(MP_HUD_PROP_KEY_BtmOverlayAnchor),
                           c.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstX),
                           c.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstY));
        // Rect top-left = pos, circle centered in rect
        int cx = pos.x() + sz / 2;
        int cy = pos.y() + sz / 2;
        int r2 = sz / 2;

        QColor fill(40, 80, 40, static_cast<int>(op * 180));
        QColor border(80, 160, 80, static_cast<int>(op * 255));

        p.setPen(QPen(border, 2));
        p.setBrush(fill);
        p.drawEllipse(QPoint(cx, cy), r2, r2);

        p.setPen(QPen(border, 1));
        p.drawLine(cx - 4, cy, cx + 4, cy);
        p.drawLine(cx, cy - 4, cx, cy + 4);
    }
};
