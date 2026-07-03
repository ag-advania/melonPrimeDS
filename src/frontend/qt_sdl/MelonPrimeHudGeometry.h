#ifndef MELON_PRIME_HUD_GEOMETRY_H
#define MELON_PRIME_HUD_GEOMETRY_H

#include <cmath>

namespace MelonPrime::HudGeometry {

inline void ApplyAnchor(int anchor, int offsetX, int offsetY,
                        int& outX, int& outY, float topStretchX = 1.0f)
{
    static constexpr int H = 192;
    const int col = anchor % 3;
    const float baseX = (col == 0) ? -(topStretchX - 1.0f) * 128.0f
                      : (col == 1) ?  128.0f
                      :               128.0f * (topStretchX + 1.0f);
    const int row = anchor / 3;
    const int baseY = (row == 0) ? 0 : (row == 1) ? H / 2 : H;
    outX = static_cast<int>(baseX) + offsetX;
    outY = baseY + offsetY;
}

inline float AlignedTextX(float anchorX, int align, float textW)
{
    switch (align) {
    case 1: return anchorX - textW * 0.5f;
    case 2: return anchorX - textW;
    default: return anchorX;
    }
}

inline int AlignedTextXInt(int anchorX, int align, int textW)
{
    switch (align) {
    case 1: return anchorX - textW / 2;
    case 2: return anchorX - textW;
    default: return anchorX;
    }
}

inline void CalcGaugePos(float textX, float textY, float textW, float textH,
                         int anchor, int ofsX, int ofsY,
                         float gaugeLen, float gaugeWid, int ori,
                         int& outX, int& outY)
{
    const float gL = gaugeLen;
    const float gW = gaugeWid;
    switch (anchor) {
    case 0:
        outX = static_cast<int>(textX) + ofsX;
        outY = static_cast<int>(textY) + 2 + ofsY;
        break;
    case 1:
        outX = static_cast<int>(textX) + ofsX;
        outY = static_cast<int>(std::round(textY - textH - (ori == 0 ? gW : gL))) + ofsY;
        break;
    case 2:
        outX = static_cast<int>(std::round(textX + textW)) + ofsX;
        outY = static_cast<int>(std::round(textY - textH * 0.5f - (ori == 0 ? gW : gL) * 0.5f)) + ofsY;
        break;
    case 3:
        outX = static_cast<int>(std::round(textX - (ori == 0 ? gL : gW))) + ofsX;
        outY = static_cast<int>(std::round(textY - textH * 0.5f - (ori == 0 ? gW : gL) * 0.5f)) + ofsY;
        break;
    case 4:
        outX = static_cast<int>(std::round(textX + textW * 0.5f - (ori == 0 ? gL : gW) * 0.5f)) + ofsX;
        outY = static_cast<int>(std::round(textY - textH * 0.5f - (ori == 0 ? gW : gL) * 0.5f)) + ofsY;
        break;
    default:
        outX = static_cast<int>(textX) + ofsX;
        outY = static_cast<int>(textY) + 2 + ofsY;
        break;
    }
}

inline void ApplyGaugeAlign(int& x, int& y, int orient, int len, int align)
{
    if (orient == 0)
        x -= len * align / 2;
    else
        y -= len * align / 2;
}

inline int GaugeAlignOffsetTrunc(float len, int align)
{
    return static_cast<int>(len * align / 2);
}

inline void ApplyGaugeAlignTrunc(int& x, int& y, int orient, float len, int align)
{
    const int offset = GaugeAlignOffsetTrunc(len, align);
    if (orient == 0)
        x -= offset;
    else
        y -= offset;
}

inline void ApplyGaugeAlignF(float& x, float& y, int orient, float len, int align)
{
    const float offset = len * static_cast<float>(align) * 0.5f;
    if (orient == 0)
        x -= offset;
    else
        y -= offset;
}

inline void GaugeSize(float gaugeLen, float gaugeWid, int orient, float& outW, float& outH)
{
    outW = (orient == 0) ? gaugeLen : gaugeWid;
    outH = (orient == 0) ? gaugeWid : gaugeLen;
}

inline void CalcTextPosFromGauge(float gx, float gy, float gaugeLen, float gaugeWid, int orient,
                                 int anchor, int ofsX, int ofsY,
                                 float textW, float textH,
                                 float& outTextX, float& outTextY)
{
    float gW, gH;
    GaugeSize(gaugeLen, gaugeWid, orient, gW, gH);
    switch (anchor) {
    case 1:
        outTextX = gx + gW * 0.5f - textW * 0.5f + ofsX;
        outTextY = gy + ofsY;
        break;
    case 2:
        outTextX = gx + gW + ofsX;
        outTextY = gy + gH * 0.5f + textH * 0.5f + ofsY;
        break;
    case 3:
        outTextX = gx - textW + ofsX;
        outTextY = gy + gH * 0.5f + textH * 0.5f + ofsY;
        break;
    case 4:
        outTextX = gx + gW * 0.5f - textW * 0.5f + ofsX;
        outTextY = gy + gH * 0.5f + textH * 0.5f + ofsY;
        break;
    default:
        outTextX = gx + gW * 0.5f - textW * 0.5f + ofsX;
        outTextY = gy + gH + textH + 2 + ofsY;
        break;
    }
}

inline void CalcTextPosFromGaugeInt(int gx, int gy, int gaugeLen, int gaugeWid, int orient,
                                    int anchor, int ofsX, int ofsY,
                                    int textW, int textH,
                                    int& outTextX, int& outTextY)
{
    const int gW = (orient == 0) ? gaugeLen : gaugeWid;
    const int gH = (orient == 0) ? gaugeWid : gaugeLen;
    switch (anchor) {
    case 1:
        outTextX = gx + gW / 2 - textW / 2 + ofsX;
        outTextY = gy + ofsY;
        break;
    case 2:
        outTextX = gx + gW + ofsX;
        outTextY = gy + gH / 2 + textH / 2 + ofsY;
        break;
    case 3:
        outTextX = gx - textW + ofsX;
        outTextY = gy + gH / 2 + textH / 2 + ofsY;
        break;
    case 4:
        outTextX = gx + gW / 2 - textW / 2 + ofsX;
        outTextY = gy + gH / 2 + textH / 2 + ofsY;
        break;
    default:
        outTextX = gx + gW / 2 - textW / 2 + ofsX;
        outTextY = gy + gH + textH + 2 + ofsY;
        break;
    }
}

inline void ApplyRectAnchorF(float& x, float& y, float width, float height, int anchorX, int anchorY)
{
    if (anchorX == 1)
        x -= width * 0.5f;
    else if (anchorX == 2)
        x -= width;

    if (anchorY == 1)
        y -= height * 0.5f;
    else if (anchorY == 2)
        y -= height;
}

inline void ApplyRectAnchor(int& x, int& y, int width, int height, int anchorX, int anchorY)
{
    if (anchorX == 1)
        x -= width / 2;
    else if (anchorX == 2)
        x -= width;

    if (anchorY == 1)
        y -= height / 2;
    else if (anchorY == 2)
        y -= height;
}

} // namespace MelonPrime::HudGeometry

#endif // MELON_PRIME_HUD_GEOMETRY_H
