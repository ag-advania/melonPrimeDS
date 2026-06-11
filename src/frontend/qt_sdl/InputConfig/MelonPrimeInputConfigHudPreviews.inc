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
        return QPoint((anchor % 3) * 128 + ofsX, (anchor / 3) * 96 + ofsY);
    }

    // Apply DS-space transform (call after drawBackground, before DS-space drawing)
    void setupDsTransform(QPainter& p) const {
        p.scale(width() / 256.0f, height() / 192.0f);
    }

    // Text width in DS-space units (accounts for scale transform)
    int textWidthDS(QPainter& p, const QString& str) const {
        return static_cast<int>(p.fontMetrics().horizontalAdvance(str) * 256.0f / width());
    }

    // Alignment-adjusted X in DS-space: 0=left, 1=center, 2=right
    int alignedX(int anchorX, int align, int textW) const {
        if (align == 1) return anchorX - textW / 2;
        if (align == 2) return anchorX - textW;
        return anchorX;
    }

    // Draw a gauge bar in DS-space at 50% fill
    static void drawGaugeDS(QPainter& p, int x, int y, const QColor& color, int orient, int len, int wid, int align = 0) {
        if (len <= 0) len = 28;
        if (wid <= 0) wid = 3;
        if (orient == 0) x -= len * align / 2;
        else             y -= len * align / 2;
        int fw = (orient == 0) ? len / 2 : wid;
        int fh = (orient == 0) ? wid    : len / 2;
        int fx = x;
        int fy = (orient == 0) ? y : y + len - fh;
        p.fillRect(QRect(x, y, orient == 0 ? len : wid, orient == 0 ? wid : len), QColor(0,0,0,100));
        p.fillRect(QRect(fx, fy, fw, fh), color);
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
        QColor col = readColor("Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB");

        int cx = r.center().x();
        int cy = r.center().y();
        float scale = 2.0f;

        auto drawArms = [&](const char* showKey, const char* opacityKey, const char* lxKey, const char* lyKey,
                            const char* thickKey, const char* ofsKey) {
            if (!c.GetBool(showKey)) return;
            float opacity = c.GetDouble(opacityKey);
            int lx = c.GetInt(lxKey);
            int ly = c.GetInt(lyKey);
            int thick = c.GetInt(thickKey);
            int ofs = c.GetInt(ofsKey);
            QColor ac = col;
            ac.setAlphaF(opacity);
            int t = qMax(1, static_cast<int>(thick * scale));
            int ht = t / 2;
            bool tStyle = c.GetBool("Metroid.Visual.CrosshairTStyle");
            p.fillRect(QRect(cx + static_cast<int>(ofs * scale), cy - ht, static_cast<int>(lx * scale), t), ac);
            p.fillRect(QRect(cx - static_cast<int>(ofs * scale) - static_cast<int>(lx * scale), cy - ht, static_cast<int>(lx * scale), t), ac);
            p.fillRect(QRect(cx - ht, cy + static_cast<int>(ofs * scale), t, static_cast<int>(ly * scale)), ac);
            if (!tStyle)
                p.fillRect(QRect(cx - ht, cy - static_cast<int>(ofs * scale) - static_cast<int>(ly * scale), t, static_cast<int>(ly * scale)), ac);
        };

        drawArms("Metroid.Visual.CrosshairInnerShow", "Metroid.Visual.CrosshairInnerOpacity",
                 "Metroid.Visual.CrosshairInnerLengthX", "Metroid.Visual.CrosshairInnerLengthY",
                 "Metroid.Visual.CrosshairInnerThickness", "Metroid.Visual.CrosshairInnerOffset");
        drawArms("Metroid.Visual.CrosshairOuterShow", "Metroid.Visual.CrosshairOuterOpacity",
                 "Metroid.Visual.CrosshairOuterLengthX", "Metroid.Visual.CrosshairOuterLengthY",
                 "Metroid.Visual.CrosshairOuterThickness", "Metroid.Visual.CrosshairOuterOffset");

        if (c.GetBool("Metroid.Visual.CrosshairCenterDot")) {
            float dotOp = c.GetDouble("Metroid.Visual.CrosshairDotOpacity");
            int dotThick = c.GetInt("Metroid.Visual.CrosshairDotThickness");
            QColor dc = col; dc.setAlphaF(dotOp);
            int ds = qMax(1, static_cast<int>(dotThick * scale));
            p.fillRect(QRect(cx - ds/2, cy - ds/2, ds, ds), dc);
        }
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
        p.setFont(getMphHudFont(c));

        // ── HP text ──
        {
            float op = c.GetDouble("Metroid.Visual.HudHpOpacity");
            QColor col = readColor("Metroid.Visual.HudHpTextColorR", "Metroid.Visual.HudHpTextColorG", "Metroid.Visual.HudHpTextColorB", op);
            QPoint pos = dsPos(c.GetInt("Metroid.Visual.HudHpAnchor"), c.GetInt("Metroid.Visual.HudHpX"), c.GetInt("Metroid.Visual.HudHpY"));
            QString str = QString::fromStdString(c.GetString("Metroid.Visual.HudHpPrefix")) + "100";
            int tw = textWidthDS(p, str);
            int th = static_cast<int>(p.fontMetrics().height() * 192.0f / height());

            bool showGauge = c.GetBool("Metroid.Visual.HudHpGauge");
            int mode = showGauge ? c.GetInt("Metroid.Visual.HudHpGaugePosMode") : 0;
            int ori  = c.GetInt("Metroid.Visual.HudHpGaugeOrientation");
            int glen = c.GetInt("Metroid.Visual.HudHpGaugeLength");
            int gwid = c.GetInt("Metroid.Visual.HudHpGaugeWidth");
            int galign = c.GetInt("Metroid.Visual.HudHpGaugeAlign");

            int tx, textY;
            int gx = 0, gy = 0;

            if (showGauge && mode == 2) {
                // Text → Gauge: gauge at independent pos, text derived from gauge
                QPoint gp = dsPos(c.GetInt("Metroid.Visual.HudHpGaugePosAnchor"), c.GetInt("Metroid.Visual.HudHpGaugePosX"), c.GetInt("Metroid.Visual.HudHpGaugePosY"));
                gx = gp.x(); gy = gp.y();
                int egx = gx - (ori == 0 ? glen * galign / 2 : 0);
                int egy = gy - (ori == 1 ? glen * galign / 2 : 0);
                int gW  = (ori == 0) ? glen : gwid;
                int gH  = (ori == 0) ? gwid : glen;
                int tAnc = c.GetInt("Metroid.Visual.HudHpTextAnchor");
                int ofsX = c.GetInt("Metroid.Visual.HudHpTextOffsetX");
                int ofsY = c.GetInt("Metroid.Visual.HudHpTextOffsetY");
                switch (tAnc) {
                case 1: tx = egx + gW/2 - tw/2 + ofsX; textY = egy                  + ofsY; break; // Above
                case 2: tx = egx + gW       + ofsX;    textY = egy + gH/2 + th/2 + ofsY; break; // Right
                case 3: tx = egx - tw       + ofsX;    textY = egy + gH/2 + th/2 + ofsY; break; // Left
                case 4: tx = egx + gW/2 - tw/2 + ofsX; textY = egy + gH/2 + th/2 + ofsY; break; // Center
                default:tx = egx + gW/2 - tw/2 + ofsX; textY = egy + gH + th + 2 + ofsY; break; // Below
                }
            } else {
                tx    = alignedX(pos.x(), c.GetInt("Metroid.Visual.HudHpAlign"), tw);
                textY = pos.y();
                if (showGauge && mode == 1) {
                    QPoint gp = dsPos(c.GetInt("Metroid.Visual.HudHpGaugePosAnchor"), c.GetInt("Metroid.Visual.HudHpGaugePosX"), c.GetInt("Metroid.Visual.HudHpGaugePosY"));
                    gx = gp.x(); gy = gp.y();
                } else if (showGauge) { // mode 0: gauge relative to text
                    gx = tx    + c.GetInt("Metroid.Visual.HudHpGaugeOffsetX");
                    gy = pos.y() + c.GetInt("Metroid.Visual.HudHpGaugeOffsetY") + 2;
                }
            }

            p.setPen(col);
            p.drawText(QPoint(tx, textY), str);

            if (showGauge) {
                float gop = c.GetDouble("Metroid.Visual.HudHpGaugeOpacity");
                QColor gc = readColor("Metroid.Visual.HudHpGaugeColorR", "Metroid.Visual.HudHpGaugeColorG", "Metroid.Visual.HudHpGaugeColorB", gop);
                drawGaugeDS(p, gx, gy, gc, ori, glen, gwid, galign);
            }
        }

        // ── Weapon icon ──
        if (c.GetInt("Metroid.Visual.HudWeaponIconMode") != 0) {
            static QImage s_magmaul;
            static int s_magmaulH = 0;
            int wantH = c.GetInt("Metroid.Visual.HudWeaponIconHeight");
            if (s_magmaul.isNull() || s_magmaulH != wantH) {
                QImageReader rd(QStringLiteral(":/mph-icon-magmaul"));
                QSize sz = rd.size(); if (sz.isEmpty()) sz = QSize(wantH, wantH);
                rd.setScaledSize(QSize(qMax(1, sz.width()*wantH/qMax(1,sz.height())), wantH));
                s_magmaul = rd.read(); s_magmaulH = wantH;
            }
            if (!s_magmaul.isNull()) {
                QPoint ip = dsPos(c.GetInt("Metroid.Visual.HudWeaponIconPosAnchor"),
                                  c.GetInt("Metroid.Visual.HudWeaponIconPosX"),
                                  c.GetInt("Metroid.Visual.HudWeaponIconPosY"));
                int ancX = c.GetInt("Metroid.Visual.HudWeaponIconAnchorX");
                int ancY = c.GetInt("Metroid.Visual.HudWeaponIconAnchorY");
                int iw = s_magmaul.width(), ih = s_magmaul.height();
                int ix = ip.x() - (ancX == 1 ? iw/2 : ancX == 2 ? iw : 0);
                int iy = ip.y() - (ancY == 1 ? ih/2 : ancY == 2 ? ih : 0);
                p.drawImage(QRectF(ix, iy, iw, ih), s_magmaul);
            }
        }

        // ── Ammo text ──
        {
            float op = c.GetDouble("Metroid.Visual.HudWeaponOpacity");
            QColor col = readColor("Metroid.Visual.HudAmmoTextColorR", "Metroid.Visual.HudAmmoTextColorG", "Metroid.Visual.HudAmmoTextColorB", op);
            QPoint pos = dsPos(c.GetInt("Metroid.Visual.HudWeaponAnchor"), c.GetInt("Metroid.Visual.HudWeaponX"), c.GetInt("Metroid.Visual.HudWeaponY"));
            QString str = QString::fromStdString(c.GetString("Metroid.Visual.HudAmmoPrefix")) + "50";
            int tw = textWidthDS(p, str);
            int th = static_cast<int>(p.fontMetrics().height() * 192.0f / height());

            bool showGauge = c.GetBool("Metroid.Visual.HudAmmoGauge");
            int mode = showGauge ? c.GetInt("Metroid.Visual.HudAmmoGaugePosMode") : 0;
            int ori  = c.GetInt("Metroid.Visual.HudAmmoGaugeOrientation");
            int glen = c.GetInt("Metroid.Visual.HudAmmoGaugeLength");
            int gwid = c.GetInt("Metroid.Visual.HudAmmoGaugeWidth");
            int galign = c.GetInt("Metroid.Visual.HudAmmoGaugeAlign");

            int tx, textY;
            int gx = 0, gy = 0;

            if (showGauge && mode == 2) {
                // Text → Gauge: gauge at independent pos, text derived from gauge
                QPoint gp = dsPos(c.GetInt("Metroid.Visual.HudAmmoGaugePosAnchor"), c.GetInt("Metroid.Visual.HudAmmoGaugePosX"), c.GetInt("Metroid.Visual.HudAmmoGaugePosY"));
                gx = gp.x(); gy = gp.y();
                int egx = gx - (ori == 0 ? glen * galign / 2 : 0);
                int egy = gy - (ori == 1 ? glen * galign / 2 : 0);
                int gW  = (ori == 0) ? glen : gwid;
                int gH  = (ori == 0) ? gwid : glen;
                int tAnc = c.GetInt("Metroid.Visual.HudAmmoTextAnchor");
                int ofsX = c.GetInt("Metroid.Visual.HudAmmoTextOffsetX");
                int ofsY = c.GetInt("Metroid.Visual.HudAmmoTextOffsetY");
                switch (tAnc) {
                case 1: tx = egx + gW/2 - tw/2 + ofsX; textY = egy                  + ofsY; break; // Above
                case 2: tx = egx + gW       + ofsX;    textY = egy + gH/2 + th/2 + ofsY; break; // Right
                case 3: tx = egx - tw       + ofsX;    textY = egy + gH/2 + th/2 + ofsY; break; // Left
                case 4: tx = egx + gW/2 - tw/2 + ofsX; textY = egy + gH/2 + th/2 + ofsY; break; // Center
                default:tx = egx + gW/2 - tw/2 + ofsX; textY = egy + gH + th + 2 + ofsY; break; // Below
                }
            } else {
                tx    = alignedX(pos.x(), c.GetInt("Metroid.Visual.HudAmmoAlign"), tw);
                textY = pos.y();
                if (showGauge && mode == 1) {
                    QPoint gp = dsPos(c.GetInt("Metroid.Visual.HudAmmoGaugePosAnchor"), c.GetInt("Metroid.Visual.HudAmmoGaugePosX"), c.GetInt("Metroid.Visual.HudAmmoGaugePosY"));
                    gx = gp.x(); gy = gp.y();
                } else if (showGauge) { // mode 0: gauge relative to text
                    gx = tx    + c.GetInt("Metroid.Visual.HudAmmoGaugeOffsetX");
                    gy = pos.y() + c.GetInt("Metroid.Visual.HudAmmoGaugeOffsetY") + 2;
                }
            }

            p.setPen(col);
            p.drawText(QPoint(tx, textY), str);

            if (showGauge) {
                float gop = c.GetDouble("Metroid.Visual.HudAmmoGaugeOpacity");
                QColor gc = readColor("Metroid.Visual.HudAmmoGaugeColorR", "Metroid.Visual.HudAmmoGaugeColorG", "Metroid.Visual.HudAmmoGaugeColorB", gop);
                drawGaugeDS(p, gx, gy, gc, ori, glen, gwid, galign);
            }
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
        p.setFont(getMphHudFont(c));

        // ── Score ──
        if (c.GetBool("Metroid.Visual.HudMatchStatusShow")) {
            float op = c.GetDouble("Metroid.Visual.HudMatchStatusOpacity");
            QColor col = readColor("Metroid.Visual.HudMatchStatusColorR", "Metroid.Visual.HudMatchStatusColorG", "Metroid.Visual.HudMatchStatusColorB", op);
            QPoint pos = dsPos(c.GetInt("Metroid.Visual.HudMatchStatusAnchor"), c.GetInt("Metroid.Visual.HudMatchStatusX"), c.GetInt("Metroid.Visual.HudMatchStatusY"));
            p.setPen(col);
            p.drawText(pos, "3 / 7");
        }

        // ── Rank ──
        if (c.GetBool("Metroid.Visual.HudRankShow")) {
            float op = c.GetDouble("Metroid.Visual.HudRankOpacity");
            QColor col = readColor("Metroid.Visual.HudRankColorR", "Metroid.Visual.HudRankColorG", "Metroid.Visual.HudRankColorB", op);
            QPoint pos = dsPos(c.GetInt("Metroid.Visual.HudRankAnchor"), c.GetInt("Metroid.Visual.HudRankX"), c.GetInt("Metroid.Visual.HudRankY"));
            QString prefix = QString::fromStdString(c.GetString("Metroid.Visual.HudRankPrefix"));
            QString suffix = QString::fromStdString(c.GetString("Metroid.Visual.HudRankSuffix"));
            QString str = prefix + "1st" + suffix;
            int tx = alignedX(pos.x(), c.GetInt("Metroid.Visual.HudRankAlign"), textWidthDS(p, str));
            p.setPen(col);
            p.drawText(QPoint(tx, pos.y()), str);
        }

        // ── Time Left ──
        if (c.GetBool("Metroid.Visual.HudTimeLeftShow")) {
            float op = c.GetDouble("Metroid.Visual.HudTimeLeftOpacity");
            QColor col = readColor("Metroid.Visual.HudTimeLeftColorR", "Metroid.Visual.HudTimeLeftColorG", "Metroid.Visual.HudTimeLeftColorB", op);
            QPoint pos = dsPos(c.GetInt("Metroid.Visual.HudTimeLeftAnchor"), c.GetInt("Metroid.Visual.HudTimeLeftX"), c.GetInt("Metroid.Visual.HudTimeLeftY"));
            QString str = QStringLiteral("4:32");
            int tx = alignedX(pos.x(), c.GetInt("Metroid.Visual.HudTimeLeftAlign"), textWidthDS(p, str));
            p.setPen(col);
            p.drawText(QPoint(tx, pos.y()), str);
        }

        // ── Bomb Left ── (text + icon)
        if (c.GetBool("Metroid.Visual.HudBombLeftShow")) {
            float op = c.GetDouble("Metroid.Visual.HudBombLeftOpacity");
            QColor col = readColor("Metroid.Visual.HudBombLeftColorR", "Metroid.Visual.HudBombLeftColorG", "Metroid.Visual.HudBombLeftColorB", op);
            QPoint pos = dsPos(c.GetInt("Metroid.Visual.HudBombLeftAnchor"), c.GetInt("Metroid.Visual.HudBombLeftX"), c.GetInt("Metroid.Visual.HudBombLeftY"));
            QString prefix = QString::fromStdString(c.GetString("Metroid.Visual.HudBombLeftPrefix"));
            QString suffix = QString::fromStdString(c.GetString("Metroid.Visual.HudBombLeftSuffix"));
            QString str = c.GetBool("Metroid.Visual.HudBombLeftTextShow") ? prefix + "3" + suffix : prefix + suffix;
            if (!str.isEmpty()) {
                int tx = alignedX(pos.x(), c.GetInt("Metroid.Visual.HudBombLeftAlign"), textWidthDS(p, str));
                p.setPen(col);
                p.drawText(QPoint(tx, pos.y()), str);
            }

            // Bomb icon
            if (c.GetBool("Metroid.Visual.HudBombLeftIconShow")) {
                static QImage s_bombIcon;
                static int s_bombIconH = 0;
                int wantBH = c.GetInt("Metroid.Visual.HudBombIconHeight");
                if (s_bombIcon.isNull() || s_bombIconH != wantBH) {
                    QImageReader rd(QStringLiteral(":/mph-icon-bombs3"));
                    QSize sz = rd.size(); if (sz.isEmpty()) sz = QSize(wantBH, wantBH);
                    rd.setScaledSize(QSize(qMax(1, sz.width()*wantBH/qMax(1,sz.height())), wantBH));
                    s_bombIcon = rd.read(); s_bombIconH = wantBH;
                }
                if (!s_bombIcon.isNull()) {
                    QPoint ip;
                    if (c.GetInt("Metroid.Visual.HudBombLeftIconMode") == 0) {
                        // relative to text anchor
                        ip = QPoint(pos.x() + c.GetInt("Metroid.Visual.HudBombLeftIconOfsX"),
                                    pos.y() + c.GetInt("Metroid.Visual.HudBombLeftIconOfsY"));
                    } else {
                        ip = dsPos(c.GetInt("Metroid.Visual.HudBombLeftIconPosAnchor"),
                                   c.GetInt("Metroid.Visual.HudBombLeftIconPosX"),
                                   c.GetInt("Metroid.Visual.HudBombLeftIconPosY"));
                    }
                    int ancX = c.GetInt("Metroid.Visual.HudBombLeftIconAnchorX");
                    int ancY = c.GetInt("Metroid.Visual.HudBombLeftIconAnchorY");
                    int iw = s_bombIcon.width(), ih = s_bombIcon.height();
                    int ix = ip.x() - (ancX == 1 ? iw/2 : ancX == 2 ? iw : 0);
                    int iy = ip.y() - (ancY == 1 ? ih/2 : ancY == 2 ? ih : 0);
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
        if (!c.GetBool("Metroid.Visual.BtmOverlayEnable")) return;

        setupDsTransform(p);

        float op  = c.GetDouble("Metroid.Visual.BtmOverlayOpacity");
        int   sz  = c.GetInt("Metroid.Visual.BtmOverlayDstSize");
        QPoint pos = dsPos(c.GetInt("Metroid.Visual.BtmOverlayAnchor"),
                           c.GetInt("Metroid.Visual.BtmOverlayDstX"),
                           c.GetInt("Metroid.Visual.BtmOverlayDstY"));
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
