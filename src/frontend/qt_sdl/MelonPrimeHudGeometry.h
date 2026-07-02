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

} // namespace MelonPrime::HudGeometry

#endif // MELON_PRIME_HUD_GEOMETRY_H
