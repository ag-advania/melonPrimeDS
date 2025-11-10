#include "MelonPrimeScreen.h"
#include <algorithm>

namespace MelonPrimeScreen
{
    bool Enabled = true;

    //-----------------------------------------------------
    // レイアウト補助
    //-----------------------------------------------------
    void ArrangeOverlayTopRight(
        float screenWidth,
        float screenHeight,
        float scaleTop,
        float& overlayX,
        float& overlayY,
        float& overlayW,
        float& overlayH,
        float shiftRatio,
        float marginX,
        float marginY)
    {
        if (!Enabled) return; // ← 内部で無効時は何もしない

        float topX = (screenWidth - 256.0f * scaleTop) * 0.5f;
        float topY = (screenHeight - 192.0f * scaleTop) * 0.5f;
        float topW = 256.0f * scaleTop;
        float topH = 192.0f * scaleTop;

        float scaleBot = 0.25f * scaleTop;
        overlayW = 256.0f * scaleBot;
        overlayH = 192.0f * scaleBot;

        overlayX = topX + topW - overlayW - marginX;
        overlayY = topY + marginY;

        if (shiftRatio != 0.0f)
        {
            overlayX += overlayW * shiftRatio;
            overlayY -= overlayH * shiftRatio;
        }

        overlayX = std::min(overlayX, screenWidth - overlayW);
        overlayY = std::max(overlayY, 0.0f);
    }

    //-----------------------------------------------------
    // OpenGL Scissor & Alpha
    //-----------------------------------------------------
    void EnableScissor(int x, int y, int width, int height, int windowHeight)
    {
        if (!Enabled) return; // 無効時は無動作
        int glY = windowHeight - (y + height);
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, glY, width, height);
    }

    void DisableScissor()
    {
        if (!Enabled) return;
        glDisable(GL_SCISSOR_TEST);
    }

    void EnableAlpha(float alpha)
    {
        if (!Enabled) return;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1.0f, 1.0f, 1.0f, alpha);
    }

    void DisableAlpha()
    {
        if (!Enabled) return;
        glDisable(GL_BLEND);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    }
}
