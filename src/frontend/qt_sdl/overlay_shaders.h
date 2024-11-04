/*
    Copyright 2016-2023 melonDS team
    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef OVERLAY_SHADERS_H
#define OVERLAY_SHADERS_H

/*
* OSD v1.0
const inline char* kScreenFS_overlay = R"(#version 140

    uniform sampler2D OverlayTex;

    smooth in vec2 fTexcoord;

    uniform vec2 uOverlayPos;
    uniform vec2 uOverlaySize;
    uniform int uOverlayScreenType;

    out vec4 oColor;

    void main()
    {
        const vec2 dsSize = vec2(256.0, 193.0); // +1 on y for pixel gap

        vec2 uv = fTexcoord * vec2(1.0, 2.0);

        if (uOverlayScreenType < 1) {
            // top screen
            uv -= uOverlayPos / dsSize;
            uv *= dsSize / uOverlaySize;
        } else {
            // bottom screen
            uv -= vec2(0.0, 1.0);
            uv -= (uOverlayPos + vec2(0.0, 1.0)) / dsSize;
            uv *= dsSize / uOverlaySize;
        }

        vec4 pixel = texture(OverlayTex, uv);
        pixel.rgb *= pixel.a;

        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
            oColor = vec4(0.0, 0.0, 0.0, 0.0);
        } else {
            oColor = pixel.bgra;
        }
    }
)";
*/

// Improved process for less latency
/*
* * OSD v1.1
const inline char* kScreenFS_overlay = R"(#version 140
        uniform sampler2D OverlayTex;
        smooth in vec2 fTexcoord;
        uniform vec2 uOverlayPos;
        uniform vec2 uOverlaySize;
        uniform int uOverlayScreenType;
        out vec4 oColor;
        void main()
        {
            const vec2 dsSize = vec2(256.0, 193.0); // +1 on y for pixel gap
            vec2 uv = fTexcoord * vec2(1.0, 2.0);

            // Precompute the scale factor
            vec2 scaleFactor = dsSize / uOverlaySize;

            // Determine if it's the bottom screen (1.0 if true, 0.0 if false)
            float isBottomScreen = float(uOverlayScreenType >= 1);

            // Adjust 'uv' without branching
            uv -= isBottomScreen * vec2(0.0, 1.0);
            uv -= (uOverlayPos + vec2(0.0, isBottomScreen)) / dsSize;

            // Apply the scale factor
            uv *= scaleFactor;

            // UV coordinate boundary check mask
            vec2 uvMask = step(vec2(0.0), uv) * step(uv, vec2(1.0));
            float mask = uvMask.x * uvMask.y;

            // Sample the texture
            vec4 pixel = texture(OverlayTex, uv);

            // Premultiply alpha
            pixel.rgb *= pixel.a;

            // Output color with mask applied
            oColor = pixel * mask;
        }
    )";
    */

// OSD v1.2
/*
const inline char* kScreenFS_overlay = R"(#version 140

        uniform sampler2D OverlayTex;
        uniform vec2 uOverlayPos;
        uniform vec2 uOverlaySize;
        uniform int uOverlayScreenType;

        smooth in vec2 fTexcoord;
        out vec4 oColor;

        void main()
        {
            const vec2 dsSize = vec2(256.0, 193.0);
            vec2 uv = fTexcoord * vec2(1.0, 2.0);
            vec2 scaleFactor = dsSize / uOverlaySize;
    
            // Screen adjustment for top/bottom screen handling
            float isBottomScreen = float(uOverlayScreenType >= 1);
            uv -= vec2(0.0, isBottomScreen);
            uv -= (uOverlayPos + vec2(0.0, isBottomScreen)) / dsSize;
            uv *= scaleFactor;
    
            // UV coordinate boundary check using step function
            vec2 uvMask = step(vec2(0.0), uv) * step(uv, vec2(1.0));
            float mask = uvMask.x * uvMask.y;
    
            // Sample texture and apply alpha premultiplication
            vec4 pixel = texture(OverlayTex, uv);
            pixel.rgb *= pixel.a;
            oColor = pixel * mask;
        }
    )";
*/


// OSD v1.3
/*

主な低レベル最適化ポイント：

スカラー演算の活用


ベクトル演算を個別のスカラー演算に分解
GPU のSIMDユニットの効率的な使用


除算の最適化


定数除算を乗算に変換（inv_width/inv_height の使用）
除算命令のレイテンシを削減


メモリアクセスの最適化


テクスチャフェッチを1回に集約
依存テクスチャ読み込みの最小化


命令レベルの並列性（ILP）向上


演算の依存関係を減らす
パイプライン・ストールの削減


条件分岐の最適化


分岐命令を算術演算に変換
実行パスの予測性向上

このバージョンは：

より少ないGPU命令で実行可能
メモリバンド幅の使用を最適化
より効率的なGPUパイプラインの使用
キャッシュヒット率の向上

ただし、このような低レベルな最適化は：

GPUアーキテクチャに依存する可能性がある
コンパイラの最適化との相性を考慮する必要がある
実際のパフォーマンス向上は使用環境でテストが必要

更なる最適化の可能性：

uniform変数のパッキング
テクスチャサンプリングの最適化
シェーダーバリアントの生成
*/
/*
const inline char* kScreenFS_overlay = R"(#version 140
        uniform sampler2D OverlayTex;
        uniform vec2 uOverlayPos;
        uniform vec2 uOverlaySize;
        uniform int uOverlayScreenType;

        smooth in vec2 fTexcoord;
        out vec4 oColor;

        void main()
        {
            // Minimize vector operations by using scalar math where possible
            float u = fTexcoord.x;
            float v = fTexcoord.y * 2.0;

            // Precalculate inverse of screen size for division optimization
            const float inv_width = 1.0 / 256.0;
            const float inv_height = 1.0 / 193.0;

            // Scale factors precomputed as individual components
            float scaleX = 256.0 / uOverlaySize.x;
            float scaleY = 193.0 / uOverlaySize.y;

            // Screen type check with minimal branching
            float screenOffset = float(uOverlayScreenType >= 1);

            // Component-wise UV calculation for better instruction pipelining
            u = (u - (uOverlayPos.x * inv_width)) * scaleX;
            v = (v - screenOffset - (uOverlayPos.y + screenOffset) * inv_height) * scaleY;

            // Optimized boundary check using MAD operations
            float maskU = step(0.0, u) * step(u, 1.0);
            float maskV = step(0.0, v) * step(v, 1.0);
            float mask = maskU * maskV;

            // Single texture fetch with minimal dependent texture reads
            vec4 pixel = texture(OverlayTex, vec2(u, v));

            // Vectorized multiplication for better SIMD utilization
            pixel = pixel * vec4(pixel.aaa, 1.0) * mask;
            oColor = pixel;
        }
    )";
*/

//OSD 1.4
/*
超低レベルな最適化のポイント：

メモリレイアウトの最適化


uniform変数のパッキング（vec4にまとめる）
メモリアクセスパターンの最適化
キャッシュライン効率の向上


定数の最適化


除算を乗算に変換
定数を直接的な数値に展開
コンパイル時の定数畳み込み最適化


演算の最適化


MAD（Multiply-Add）命令の活用
SIMDユニットの効率的な利用
依存関係のある演算の最小化


テクスチャアクセスの最適化


テクスチャキャッシュの効率的な使用
UV計算の単純化
メモリバンド幅の最適化


パイプライン効率の向上


分岐命令の完全な排除
命令レベル並列性の最大化
レジスタ使用の最適化


ハードウェア補間器の活用


step関数の効率的な実装
ハードウェアの特性を活かした補間

コンパイラへのヒント：

マクロ定義による定数の明示
演算順序の明示的な指定
レジスタ圧力の低減

これらの最適化により：

メモリバンド幅の使用が最小化
命令数が削減
パイプラインストールが最小化
キャッシュ効率が向上
レイテンシが低減

注意点：

この最適化はGPUアーキテクチャに強く依存
ドライバーの最適化との相互作用を考慮
実際のパフォーマンスは環境依存

これ以上の最適化を行う場合：

アセンブリレベルでの最適化
シェーダーバリアントの生成
プラットフォーム固有の最適化
を検討する必要があります。

rev
主な修正点：

uniform変数のパッキングを維持しつつ、わかりやすく展開
dsSize定数を復活
UV計算のロジックを安定した形に戻す
過度な最適化を取り除き、可読性と安定性を向上

最適化のポイント（維持）：

uniform変数のパッキング
最小限の分岐処理
効率的な境界チェック
テクスチャフェッチの最適化
*/
const inline char* kScreenFS_overlay = R"(#version 140

        // Pack related uniforms into vec4 for better memory alignment
        uniform vec4 uOverlayPosSize;   // xy: pos, zw: size
        uniform sampler2D OverlayTex;
        uniform int uOverlayScreenType;

        smooth in vec2 fTexcoord;
        out vec4 oColor;

        void main()
        {
            // Constant screen dimensions
            const vec2 dsSize = vec2(256.0, 193.0);
    
            // Unpack overlay position and size
            vec2 overlayPos = uOverlayPosSize.xy;
            vec2 overlaySize = uOverlayPosSize.zw;
    
            // Precalculated scale factors
            vec2 scale = dsSize / overlaySize;
    
            // Screen type handling with minimal branching
            float screenOffset = float(uOverlayScreenType >= 1);
    
            // UV calculation optimized for minimal operations
            vec2 uv = fTexcoord * vec2(1.0, 2.0);
            uv.y -= screenOffset;
            uv -= (overlayPos + vec2(0.0, screenOffset)) / dsSize;
            uv *= scale;
    
            // Efficient boundary check
            float mask = step(0.0, uv.x) * step(uv.x, 1.0) * 
                         step(0.0, uv.y) * step(uv.y, 1.0);
    
            // Single texture fetch with premultiplied alpha
            vec4 color = texture(OverlayTex, uv);
            color.rgb *= color.a;
    
            // Final output with mask
            oColor = color * mask;
        }

    )";



/*
const inline int virtualCursorSize = 11;
const inline bool virtualCursorPixels[] = {
    0,0,0,1,1,1,1,1,0,0,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,1,0,0,0,0,0,0,0,1,0,
    1,0,0,0,0,0,0,0,0,0,1,
    1,0,0,0,0,1,0,0,0,0,1,
    1,0,0,0,1,1,1,0,0,0,1,
    1,0,0,0,0,1,0,0,0,0,1,
    1,0,0,0,0,0,0,0,0,0,1,
    0,1,0,0,0,0,0,0,0,1,0,
    0,0,1,0,0,0,0,0,1,0,0,
    0,0,0,1,1,1,1,1,0,0,0,
};
*/



#endif // OVERLAY_SHADERS_H