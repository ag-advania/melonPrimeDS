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
        const float cs = qMax(100, c.GetInt(MP_HUD_PROP_KEY_CrosshairScale)) / 100.0f;
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
        const int radius    = qBound(4, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeRadius), 1024);
        const int gap       = qBound(0, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeGap), 64);
        const int thickness = qBound(1, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeThickness), 12);

        const int cx = r.center().x();
        const int cy = r.center().y();
        if (scopeOpacity <= 0.001) return;

        // Faded, zoom-scaled base crosshair underneath the scope ring. This makes
        // Zoom Base Scale % and Zoom Base Opacity % visibly functional. The default
        // Zoom Base Opacity is 0 (base hidden while fully scoped, as in-game), so
        // nothing shows until the user raises it — then it scales by Zoom Base Scale %.
        {
            const float cs = qMax(100, c.GetInt(MP_HUD_PROP_KEY_CrosshairScale)) / 100.0f;
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

        const int dotSizeDs = qBound(1, c.GetInt(MP_HUD_PROP_KEY_CrosshairZoomScopeDotSize), 32);
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
        p.setFont(getMphHudFont(c));

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
            int glen = c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeLength);
            int gwid = c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeWidth);
            int galign = c.GetInt(MP_HUD_PROP_KEY_HudHpGaugeAlign);

            int tx, textY;
            int gx = 0, gy = 0;

            if (showGauge && mode == 2) {
                // Text → Gauge: gauge at independent pos, text derived from gauge
                QPoint gp = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosAnchor), c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosX), c.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosY));
                gx = gp.x(); gy = gp.y();
                int egx = gx - (ori == 0 ? glen * galign / 2 : 0);
                int egy = gy - (ori == 1 ? glen * galign / 2 : 0);
                int gW  = (ori == 0) ? glen : gwid;
                int gH  = (ori == 0) ? gwid : glen;
                int tAnc = c.GetInt(MP_HUD_PROP_KEY_HudHpTextAnchor);
                int ofsX = c.GetInt(MP_HUD_PROP_KEY_HudHpTextOffsetX);
                int ofsY = c.GetInt(MP_HUD_PROP_KEY_HudHpTextOffsetY);
                switch (tAnc) {
                case 1: tx = egx + gW/2 - tw/2 + ofsX; textY = egy                  + ofsY; break; // Above
                case 2: tx = egx + gW       + ofsX;    textY = egy + gH/2 + th/2 + ofsY; break; // Right
                case 3: tx = egx - tw       + ofsX;    textY = egy + gH/2 + th/2 + ofsY; break; // Left
                case 4: tx = egx + gW/2 - tw/2 + ofsX; textY = egy + gH/2 + th/2 + ofsY; break; // Center
                default:tx = egx + gW/2 - tw/2 + ofsX; textY = egy + gH + th + 2 + ofsY; break; // Below
                }
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
            int wantH = c.GetInt(MP_HUD_PROP_KEY_HudWeaponIconHeight);
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
                int ix = ip.x() - (ancX == 1 ? iw/2 : ancX == 2 ? iw : 0);
                int iy = ip.y() - (ancY == 1 ? ih/2 : ancY == 2 ? ih : 0);
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
            int glen = c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeLength);
            int gwid = c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeWidth);
            int galign = c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeAlign);

            int tx, textY;
            int gx = 0, gy = 0;

            if (showGauge && mode == 2) {
                // Text → Gauge: gauge at independent pos, text derived from gauge
                QPoint gp = dsPos(c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosAnchor), c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosX), c.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosY));
                gx = gp.x(); gy = gp.y();
                int egx = gx - (ori == 0 ? glen * galign / 2 : 0);
                int egy = gy - (ori == 1 ? glen * galign / 2 : 0);
                int gW  = (ori == 0) ? glen : gwid;
                int gH  = (ori == 0) ? gwid : glen;
                int tAnc = c.GetInt(MP_HUD_PROP_KEY_HudAmmoTextAnchor);
                int ofsX = c.GetInt(MP_HUD_PROP_KEY_HudAmmoTextOffsetX);
                int ofsY = c.GetInt(MP_HUD_PROP_KEY_HudAmmoTextOffsetY);
                switch (tAnc) {
                case 1: tx = egx + gW/2 - tw/2 + ofsX; textY = egy                  + ofsY; break; // Above
                case 2: tx = egx + gW       + ofsX;    textY = egy + gH/2 + th/2 + ofsY; break; // Right
                case 3: tx = egx - tw       + ofsX;    textY = egy + gH/2 + th/2 + ofsY; break; // Left
                case 4: tx = egx + gW/2 - tw/2 + ofsX; textY = egy + gH/2 + th/2 + ofsY; break; // Center
                default:tx = egx + gW/2 - tw/2 + ofsX; textY = egy + gH + th + 2 + ofsY; break; // Below
                }
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

            const int iconH   = qBound(4, c.GetInt(MP_HUD_PROP_KEY_HudWeaponInventoryIconHeight), 48);
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
        p.setFont(getMphHudFont(c));

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
                int wantBH = c.GetInt(MP_HUD_PROP_KEY_HudBombIconHeight);
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
