#pragma once
#include <GL/gl.h>

/**
 * @brief MelonPrime 専用画面制御モジュール
 *
 * Enabled フラグが true の場合のみ内部処理が実行される。
 * 呼び出し側は IsEnabled() チェックを不要とする。
 */
namespace MelonPrimeScreen
{
    //=====================================================
    // 状態制御
    //=====================================================

    /// 現在の有効状態
    extern bool Enabled;

    /// 有効化
    inline void Enable() noexcept { Enabled = true; }

    /// 無効化
    inline void Disable() noexcept { Enabled = false; }

    /// 状態確認
    inline bool IsEnabled() noexcept { return Enabled; }

    //=====================================================
    // レイアウト補助
    //=====================================================

    void ArrangeOverlayTopRight(
        float screenWidth,
        float screenHeight,
        float scaleTop,
        float& overlayX,
        float& overlayY,
        float& overlayW,
        float& overlayH,
        float shiftRatio = 0.0f,
        float marginX = 0.0f,
        float marginY = 0.0f);

    //=====================================================
    // OpenGL 描画制御
    //=====================================================

    void EnableScissor(int x, int y, int width, int height, int windowHeight);
    void DisableScissor();

    void EnableAlpha(float alpha);
    void DisableAlpha();
}
