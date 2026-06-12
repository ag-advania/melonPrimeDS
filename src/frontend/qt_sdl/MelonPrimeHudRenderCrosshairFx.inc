// Crosshair zoom transition overlay FX helpers (included from MelonPrimeHudRenderDraw.inc).
// Overlay only — must never modify scope reticle geometry (scopeT).

static void DrawFxConvergingRings(QPainter* p, int cx, int cy, int maxR, double lockT,
                                  double midFx, const QColor& color, int penW)
{
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    for (int k = 0; k < 3; ++k) {
        const double spread = (3 - k) * 0.14 * (1.0 - lockT);
        const int r = std::max(8, static_cast<int>(maxR * std::clamp(lockT + spread, 0.0, 1.0) + 0.5));
        p->setOpacity((0.14 + k * 0.06) * midFx);
        p->drawEllipse(QRect(cx - r, cy - r, r * 2, r * 2));
    }
}

static void DrawFxCornerBrackets(QPainter* p, int cx, int cy, int maxR, double lockT,
                                 double midFx, const QColor& color, int penW,
                                 float breathAmp = 0.06f)
{
    const float breath = 1.0f + breathAmp * std::sin(static_cast<float>(lockT * 6.2831853 * 2.0));
    const int bracket = std::max(8, static_cast<int>(maxR * 0.38 * lockT * breath + 0.5));
    const int arm = std::max(4, static_cast<int>(maxR * 0.14 * lockT + 0.5));
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    p->setOpacity(0.42 * midFx);
    p->drawLine(QPoint(cx - bracket, cy - arm), QPoint(cx - bracket, cy + arm));
    p->drawLine(QPoint(cx - bracket, cy - arm), QPoint(cx - bracket + arm, cy - arm));
    p->drawLine(QPoint(cx - bracket, cy + arm), QPoint(cx - bracket + arm, cy + arm));
    p->drawLine(QPoint(cx + bracket, cy - arm), QPoint(cx + bracket, cy + arm));
    p->drawLine(QPoint(cx + bracket, cy - arm), QPoint(cx + bracket - arm, cy - arm));
    p->drawLine(QPoint(cx + bracket, cy + arm), QPoint(cx + bracket - arm, cy + arm));
}

static void DrawFxRangefinderTicks(QPainter* p, int cx, int cy, int maxR, double lockT,
                                   double midFx, const QColor& color)
{
    QPen pen(color);
    pen.setWidth(1);
    p->setPen(pen);
    const int rfSpan = std::max(8, static_cast<int>(maxR * 0.55 * lockT));
    for (int t = -4; t <= 4; ++t) {
        if (t == 0)
            continue;
        const int pos = t * rfSpan / 4;
        const int tick = (t & 1) == 0 ? 4 : 2;
        p->setOpacity(0.24 * midFx);
        p->drawLine(QPoint(cx + pos, cy - tick), QPoint(cx + pos, cy + tick));
        p->drawLine(QPoint(cx - tick, cy + pos), QPoint(cx + tick, cy + pos));
    }
}

static void DrawFxTargetBoxAcquisition(QPainter* p, int cx, int cy, int maxR, double lockT,
                                       double midFx, const QColor& color, int penW,
                                       uint32_t& phase)
{
    phase = AdvanceCrosshairFxPhase(phase);
    const int jitterX = static_cast<int>(((phase >> 4) & 7u) - 3) * std::max(1, maxR / 40);
    const int jitterY = static_cast<int>(((phase >> 8) & 7u) - 3) * std::max(1, maxR / 40);
    const int half = std::max(10, static_cast<int>(maxR * 0.32 * lockT + 0.5));
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    for (int ghost = 0; ghost < 2; ++ghost) {
        const int ox = (ghost == 0) ? jitterX : 0;
        const int oy = (ghost == 0) ? jitterY : 0;
        p->setOpacity((ghost == 0 ? 0.18 : 0.38) * midFx);
        p->drawRect(QRect(cx - half + ox, cy - half + oy, half * 2, half * 2));
    }
}

static void DrawFxIrisAperture(QPainter* p, int cx, int cy, int maxR, double lockT,
                               double midFx, const QColor& color, int penW)
{
    const int blades = 8;
    const int outerR = std::max(10, static_cast<int>(maxR * (0.35 + 0.65 * (1.0 - lockT)) + 0.5));
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    for (int i = 0; i < blades; ++i) {
        const double ang = (i * 360.0 / blades + lockT * 24.0) * 3.14159265 / 180.0;
        const int x1 = cx + static_cast<int>(std::cos(ang) * outerR * 0.35);
        const int y1 = cy + static_cast<int>(std::sin(ang) * outerR * 0.35);
        const int x2 = cx + static_cast<int>(std::cos(ang) * outerR);
        const int y2 = cy + static_cast<int>(std::sin(ang) * outerR);
        p->setOpacity(0.30 * midFx);
        p->drawLine(QPoint(x1, y1), QPoint(x2, y2));
    }
    p->setOpacity(0.22 * midFx);
    p->drawEllipse(QRect(cx - outerR, cy - outerR, outerR * 2, outerR * 2));
}

static void DrawFxLidarCloud(QPainter* p, int cx, int cy, int maxR, double lockT,
                            double midFx, const QColor& color, uint32_t& phase)
{
    p->setPen(Qt::NoPen);
    const int points = 28 + static_cast<int>(lockT * 20.0);
    for (int i = 0; i < points; ++i) {
        phase = AdvanceCrosshairFxPhase(phase);
        const double ang = ((phase & 1023u) / 1024.0) * 6.2831853;
        const int r = std::max(4, static_cast<int>(maxR * lockT * (0.25 + ((phase >> 10) & 255u) / 255.0 * 0.75)));
        const int px = cx + static_cast<int>(std::cos(ang) * r);
        const int py = cy + static_cast<int>(std::sin(ang) * r);
        p->setBrush(QColor(color.red(), color.green(), color.blue(),
                           static_cast<int>(120 + 100 * lockT * midFx)));
        p->setOpacity(1.0);
        p->drawRect(QRect(px, py, 2, 2));
    }
}

static void DrawFxWireframeBox(QPainter* p, int cx, int cy, int maxR, double lockT,
                               double midFx, const QColor& color, int penW)
{
    const int w = std::max(12, static_cast<int>(maxR * 0.45 * lockT));
    const int h = std::max(10, static_cast<int>(maxR * 0.38 * lockT));
    const int depth = std::max(4, w / 4);
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->setOpacity(0.36 * midFx);
    const QRect front(cx - w, cy - h, w * 2, h * 2);
    const QRect back(front.translated(depth, -depth));
    p->drawRect(front);
    p->drawRect(back);
    p->drawLine(front.topLeft(), back.topLeft());
    p->drawLine(front.topRight(), back.topRight());
    p->drawLine(front.bottomLeft(), back.bottomLeft());
    p->drawLine(front.bottomRight(), back.bottomRight());
}

static void DrawFxMaterialGrid(QPainter* p, int cx, int cy, int maxR, double lockT,
                               double midFx, const QColor& color)
{
    const int half = std::max(8, static_cast<int>(maxR * 0.34 * lockT));
    QPen pen(color);
    pen.setWidth(1);
    p->setPen(pen);
    p->setOpacity(0.20 * midFx);
    const int step = std::max(4, half / 5);
    for (int x = -half; x <= half; x += step)
        p->drawLine(QPoint(cx + x, cy - half), QPoint(cx + x, cy + half));
    for (int y = -half; y <= half; y += step)
        p->drawLine(QPoint(cx - half, cy + y), QPoint(cx + half, cy + y));
}

static void DrawFxScanDisturb(QPainter* p, int cx, int cy, int maxR, double lockT,
                              double midFx, const QColor& color, uint32_t& phase)
{
    QPen pen(color);
    pen.setWidth(1);
    p->setPen(pen);
    const int passes = 4 + static_cast<int>(lockT * 4.0);
    for (int i = 0; i < passes; ++i) {
        phase = AdvanceCrosshairFxPhase(phase);
        const int y = cy + static_cast<int>(((phase >> 3) & 31u) - 15) * std::max(1, maxR / 24);
        const int xOff = static_cast<int>(((phase >> 9) & 15u) - 7) * std::max(1, static_cast<int>(midFx * 2.0));
        p->setOpacity(0.22 * midFx);
        p->drawLine(QPoint(cx - maxR + xOff, y), QPoint(cx + maxR + xOff, y));
        if (((phase >> 13) & 3u) == 1u) {
            pen.setWidth(2);
            p->setPen(pen);
            p->setOpacity(0.14 * midFx);
            p->drawLine(QPoint(cx - maxR + xOff * 2, y + 1), QPoint(cx + maxR + xOff * 2, y + 1));
            pen.setWidth(1);
            p->setPen(pen);
        }
    }
}

// AniGlitchArtFX-inspired neon palette (#42e8ff / #ff4fd8 / #ffe66d).
static const QColor kGlitchCyan(66, 232, 255);
static const QColor kGlitchMagenta(255, 79, 216);
static const QColor kGlitchYellow(255, 230, 109);

static void DrawFxGlitchRgbGhost(QPainter* p, int cx, int cy, int maxR, double lockT,
                                 double midFx, int penW, uint32_t phase)
{
    if (midFx < 0.03)
        return;

    const int ringR = std::max(8, static_cast<int>(maxR * std::clamp(lockT, 0.0, 1.0) + 0.5));
    const int mag = std::max(2, static_cast<int>(std::lround(2.0 + midFx * 8.0)));
    const int dir = ((phase >> 5) & 1u) ? 1 : -1;

    const int cyanOx = -dir * mag;
    const int magentaOx = dir * std::max(2, mag + 1);
    const int yellowOy = ((phase >> 6) & 3u) == 2u ? dir : 0;

    DrawCrosshairGlowRing(p, cx + cyanOx, cy + yellowOy, ringR, kGlitchCyan, 1, 0.55 * midFx);
    DrawCrosshairGlowRing(p, cx + magentaOx, cy - yellowOy, ringR, kGlitchMagenta, 1, 0.48 * midFx);
    DrawCrosshairGlowRing(p, cx, cy, ringR, kGlitchYellow, 1, 0.22 * midFx);

    QPen penC(kGlitchCyan);
    penC.setWidth(std::max(1, penW));
    p->setPen(penC);
    p->setOpacity(0.38 * midFx);
    p->drawLine(QPoint(cx + cyanOx - maxR / 3, cy + yellowOy),
                QPoint(cx + cyanOx + maxR / 3, cy + yellowOy));
    p->drawLine(QPoint(cx + cyanOx, cy + yellowOy - maxR / 3),
                QPoint(cx + cyanOx, cy + yellowOy + maxR / 3));

    QPen penM(kGlitchMagenta);
    penM.setWidth(std::max(1, penW));
    p->setPen(penM);
    p->setOpacity(0.34 * midFx);
    p->drawLine(QPoint(cx + magentaOx - maxR / 3, cy - yellowOy),
                QPoint(cx + magentaOx + maxR / 3, cy - yellowOy));
    p->drawLine(QPoint(cx + magentaOx, cy - yellowOy - maxR / 3),
                QPoint(cx + magentaOx, cy - yellowOy + maxR / 3));
}

static void DrawFxGlitchSlices(QPainter* p, int cx, int cy, int maxR, double midFx,
                               uint32_t& phase, int count)
{
    const int span = std::max(maxR, 8);
    const int shiftMag = std::max(2, static_cast<int>(std::lround(2.0 + midFx * 14.0)));

    for (int i = 0; i < count; ++i) {
        phase = AdvanceCrosshairFxPhase(phase);
        const int y = cy + static_cast<int>(((phase >> 4) & 31u) - 15) * std::max(1, maxR / 22);
        const int xOff = static_cast<int>(((phase >> 10) & 31u) - 15) * std::max(1, shiftMag / 4);
        const int thick = 1 + static_cast<int>((phase >> 15) & 1u);
        const QColor& sliceColor = ((phase >> 16) & 1u) ? kGlitchMagenta : kGlitchCyan;

        QPen pen(sliceColor);
        pen.setWidth(thick);
        p->setPen(pen);
        p->setOpacity(0.62 * midFx);
        p->drawLine(QPoint(cx - span + xOff, y), QPoint(cx + span + xOff, y));

        if (((phase >> 17) & 3u) >= 2u) {
            pen.setColor(kGlitchYellow);
            p->setPen(pen);
            p->setOpacity(0.28 * midFx);
            p->drawLine(QPoint(cx - span + xOff * 2, y + thick),
                        QPoint(cx + span + xOff * 2, y + thick));
        }
    }
}

static void DrawFxGlitchBlocks(QPainter* p, int cx, int cy, int maxR, double midFx,
                               uint32_t& phase, int count)
{
    const int area = std::max(12, maxR / 2);
    const int shiftMag = std::max(3, static_cast<int>(std::lround(3.0 + midFx * 12.0)));

    p->setBrush(Qt::NoBrush);
    for (int i = 0; i < count; ++i) {
        phase = AdvanceCrosshairFxPhase(phase);
        const int bw = 6 + static_cast<int>((phase >> 3) & 15u);
        const int bh = 4 + static_cast<int>((phase >> 8) & 11u);
        const int bx = cx + static_cast<int>(((phase >> 12) & 31u) - 15) * std::max(1, area / 14);
        const int by = cy + static_cast<int>(((phase >> 18) & 31u) - 15) * std::max(1, area / 14);
        const int ox = static_cast<int>(((phase >> 24) & 15u) - 7) * std::max(1, shiftMag / 5);
        const int oy = static_cast<int>(((phase >> 28) & 7u) - 3);

        QRect base(bx - bw / 2, by - bh / 2, bw, bh);
        QRect ghost(base.translated(ox, oy));

        QPen pen(kGlitchCyan);
        pen.setWidth(1);
        p->setPen(pen);
        p->setOpacity(0.44 * midFx);
        p->drawRect(base);

        pen.setColor(kGlitchMagenta);
        p->setPen(pen);
        p->setOpacity(0.36 * midFx);
        p->drawRect(ghost);
    }
}

static void DrawFxGlitchScanBars(QPainter* p, int cx, int cy, int maxR, double lockT,
                                 double midFx, uint32_t& phase, int count)
{
    const int barW = maxR * 2 + 16;
    for (int i = 0; i < count; ++i) {
        phase = AdvanceCrosshairFxPhase(phase);
        const int barH = 2 + static_cast<int>((phase >> 5) & 5u);
        const int y = cy + static_cast<int>(((phase >> 9) & 31u) - 15) * std::max(1, maxR / 20);
        const double sweep = lockT * 1.6 + static_cast<double>((phase >> 14) & 255u) / 255.0;
        const int xOff = static_cast<int>((std::fmod(sweep, 1.0) - 0.5) * maxR * 1.4);

        QLinearGradient grad(cx - barW / 2 + xOff, y, cx + barW / 2 + xOff, y);
        grad.setColorAt(0.0, QColor(66, 232, 255, 0));
        grad.setColorAt(0.28, QColor(66, 232, 255, static_cast<int>(180 * midFx)));
        grad.setColorAt(0.50, QColor(255, 255, 255, static_cast<int>(220 * midFx)));
        grad.setColorAt(0.72, QColor(255, 79, 216, static_cast<int>(180 * midFx)));
        grad.setColorAt(1.0, QColor(255, 79, 216, 0));

        p->setPen(Qt::NoPen);
        p->setBrush(grad);
        p->setOpacity(0.85);
        p->drawRect(QRect(cx - barW / 2 + xOff, y - barH / 2, barW, barH));
    }
}

static void DrawFxGlitchScanlines(QPainter* p, int cx, int cy, int maxR, double midFx)
{
    QPen pen(kGlitchCyan);
    pen.setWidth(1);
    p->setPen(pen);
    const int top = cy - maxR;
    const int bottom = cy + maxR;
    for (int y = top; y <= bottom; y += 3) {
        const bool accent = ((y - top) / 3) % 4 == 0;
        p->setOpacity((accent ? 0.14 : 0.06) * midFx);
        p->drawLine(QPoint(cx - maxR, y), QPoint(cx + maxR, y));
    }
}

static void DrawFxGlitchNoise(QPainter* p, int cx, int cy, int maxR, double midFx,
                              uint32_t& phase, int count)
{
    p->setPen(Qt::NoPen);
    for (int i = 0; i < count; ++i) {
        phase = AdvanceCrosshairFxPhase(phase);
        const int px = cx + static_cast<int>(((phase >> 2) & 63u) - 31) * std::max(1, maxR / 32);
        const int py = cy + static_cast<int>(((phase >> 9) & 63u) - 31) * std::max(1, maxR / 32);
        const int sz = 1 + static_cast<int>((phase >> 15) & 2u);
        const QColor dot = ((phase >> 17) & 1u) ? kGlitchMagenta : kGlitchCyan;
        p->setBrush(QColor(dot.red(), dot.green(), dot.blue(),
                           static_cast<int>(120 + 100 * midFx)));
        p->setOpacity(0.55 * midFx);
        p->drawRect(QRect(px, py, sz, sz));
    }
}

static void DrawFxGlitchPixelGrid(QPainter* p, int cx, int cy, int maxR, double midFx,
                                  uint32_t phase)
{
    const int half = maxR;
    const int step = 8;
    QPen pen(kGlitchCyan);
    pen.setWidth(1);
    p->setPen(pen);
    const int xOff = static_cast<int>(((phase >> 3) & 7u) - 3);
    const int yOff = static_cast<int>(((phase >> 6) & 7u) - 3);
    for (int x = cx - half + xOff; x <= cx + half; x += step) {
        p->setOpacity(0.08 * midFx);
        p->drawLine(QPoint(x, cy - half), QPoint(x, cy + half));
    }
    pen.setColor(kGlitchMagenta);
    p->setPen(pen);
    for (int y = cy - half + yOff; y <= cy + half; y += step) {
        p->setOpacity(0.07 * midFx);
        p->drawLine(QPoint(cx - half, y), QPoint(cx + half, y));
    }
}

static void DrawFxGlitchFlash(QPainter* p, int cx, int cy, int maxR, double midFx,
                              uint32_t phase)
{
    if (((phase >> 4) & 0xFu) != 0x8u)
        return;

    QRadialGradient grad(cx, cy, std::max(8, maxR));
    grad.setColorAt(0.0, QColor(255, 255, 255, static_cast<int>(90 * midFx)));
    grad.setColorAt(0.24, QColor(66, 232, 255, static_cast<int>(50 * midFx)));
    grad.setColorAt(0.54, QColor(255, 79, 216, static_cast<int>(20 * midFx)));
    grad.setColorAt(1.0, QColor(0, 0, 0, 0));
    p->setPen(Qt::NoPen);
    p->setBrush(grad);
    p->setOpacity(0.75);
    p->drawEllipse(QRect(cx - maxR, cy - maxR, maxR * 2, maxR * 2));
}

// variant 0 = Glitch (balanced), 1 = Cyber Jam (scan-heavy overload).
static void DrawFxGlitchArtOverlay(QPainter* p, int cx, int cy, int maxR, double lockT,
                                   double midFx, int penW, uint32_t& phase, int variant)
{
    if (midFx < 0.02)
        return;

    const double intensity = midFx * (variant == 1 ? 1.12 : 1.0);
    const int sliceCount = 4 + static_cast<int>(intensity * 5.0);
    const int blockCount = variant == 1 ? 3 + static_cast<int>(intensity * 3.0)
                                      : 4 + static_cast<int>(intensity * 4.0);
    const int barCount = variant == 1 ? 3 + static_cast<int>(intensity * 2.0)
                                      : 2 + static_cast<int>(intensity * 1.5);
    const int noiseCount = 12 + static_cast<int>(intensity * 16.0);

    phase = AdvanceCrosshairFxPhase(phase);

    p->save();
    p->setRenderHint(QPainter::Antialiasing, variant == 0);

    DrawFxGlitchRgbGhost(p, cx, cy, maxR, lockT, intensity, penW, phase);
    DrawFxGlitchScanlines(p, cx, cy, maxR, intensity * 0.85);
    DrawFxGlitchSlices(p, cx, cy, maxR, intensity, phase, sliceCount);
    DrawFxGlitchScanBars(p, cx, cy, maxR, lockT, intensity, phase, barCount);
    if (variant == 0 || ((phase >> 8) & 3u) >= 1u)
        DrawFxGlitchBlocks(p, cx, cy, maxR, intensity, phase, blockCount);
    DrawFxGlitchNoise(p, cx, cy, maxR, intensity, phase, noiseCount);
    DrawFxGlitchPixelGrid(p, cx, cy, maxR, intensity * 0.7, phase);
    DrawFxGlitchFlash(p, cx, cy, maxR, intensity, phase);

    p->restore();
}

static void DrawFxChargeRingFill(QPainter* p, int cx, int cy, int maxR, double lockT,
                                 double midFx, const QColor& color, int penW)
{
    const int r = std::max(10, static_cast<int>(maxR * lockT + 0.5));
    QPen pen(color);
    pen.setWidth(std::max(2, penW + 1));
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->setOpacity(0.48 * midFx);
    const int span = static_cast<int>(lockT * 360.0 * 16.0);
    p->drawArc(QRect(cx - r, cy - r, r * 2, r * 2), 90 * 16, span);
}

static void DrawFxRailgunLines(QPainter* p, int cx, int cy, int maxR, double lockT,
                               double midFx, const QColor& color, int penW)
{
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    const int span = std::max(8, static_cast<int>(maxR * 0.55 * lockT));
    p->setOpacity(0.34 * midFx);
    p->drawLine(QPoint(cx - span, cy - span / 2), QPoint(cx - 4, cy));
    p->drawLine(QPoint(cx + span, cy - span / 2), QPoint(cx + 4, cy));
    p->drawLine(QPoint(cx - span, cy + span / 2), QPoint(cx - 4, cy));
    p->drawLine(QPoint(cx + span, cy + span / 2), QPoint(cx + 4, cy));
    p->drawLine(QPoint(cx, cy - span), QPoint(cx, cy - 4));
    p->drawLine(QPoint(cx, cy + span), QPoint(cx, cy + 4));
}

static void DrawFxEnergySurge(QPainter* p, int cx, int cy, int maxR, double lockT,
                              double midFx, const QColor& color, int penW)
{
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    for (int wave = 0; wave < 2; ++wave) {
        const double phase = lockT * 3.0 + wave * 0.4;
        const int r = std::max(6, static_cast<int>(maxR * std::fmod(phase, 1.0) * lockT + 0.5));
        p->setOpacity((0.20 - wave * 0.06) * midFx);
        p->drawEllipse(QRect(cx - r, cy - r, r * 2, r * 2));
    }
}

static void DrawFxDataHudStrip(QPainter* p, int cx, int cy, int maxR, double lockT,
                               double midFx, const QColor& color)
{
    QPen pen(color);
    pen.setWidth(1);
    p->setPen(pen);
    const int x0 = cx + std::max(12, maxR / 3);
    const int y0 = cy - std::max(8, maxR / 4);
    const int lines = 3 + static_cast<int>(lockT * 2.0);
    for (int i = 0; i < lines; ++i) {
        const int len = 8 + i * 6 + static_cast<int>(lockT * 12);
        p->setOpacity(0.28 * midFx);
        p->drawLine(QPoint(x0, y0 + i * 5), QPoint(x0 + len, y0 + i * 5));
    }
}

static void DrawFxHitProbabilityArc(QPainter* p, int cx, int cy, int maxR, double lockT,
                                    double midFx, const QColor& color, int penW)
{
    const int r = std::max(8, static_cast<int>(maxR * 0.72 * lockT + 0.5));
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);
    p->setOpacity(0.30 * midFx);
    p->drawArc(QRect(cx - r, cy - r, r * 2, r * 2), 200 * 16,
               static_cast<int>(lockT * 140.0 * 16.0));
}

static void DrawFxSoftLockHalo(QPainter* p, int cx, int cy, int maxR, double lockT,
                               double midFx, const QColor& color)
{
    const int r = std::max(8, static_cast<int>(maxR * 0.55 * lockT + 0.5));
    DrawCrosshairGlowRing(p, cx, cy, r, color, 1, 0.20 * midFx);
}

static void DrawFxRadarSweep(QPainter* p, int cx, int cy, int maxR, double lockT,
                             double midFx, const QColor& color, int penW)
{
    const int sweepR = std::max(8, static_cast<int>(maxR * lockT + 0.5));
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    p->setOpacity(0.22 * midFx);
    const int sweepStart = static_cast<int>((lockT * 720.0) * 16.0);
    p->drawArc(QRect(cx - sweepR, cy - sweepR, sweepR * 2, sweepR * 2),
               sweepStart, static_cast<int>(52.0 * 16.0));
}

static void DrawFxVerticalScan(QPainter* p, int cx, int cy, int maxR, double lockT,
                               double midFx, const QColor& color, int penW)
{
    QPen pen(color);
    pen.setWidth(std::max(1, penW));
    p->setPen(pen);
    const double phase = lockT * 1.4;
    const int x = cx + static_cast<int>((phase - std::floor(phase) - 0.5) * maxR * 1.6);
    p->setOpacity(0.28 * midFx);
    p->drawLine(QPoint(x, cy - maxR), QPoint(x, cy + maxR));
}
