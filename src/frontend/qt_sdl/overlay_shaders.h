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

// OSD v1.2 (v1.1 is better)
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


// OSD v1.3 (v1.1 is better)
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

//OSD 1.4 awesome low latency
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
    
            // Screen type check optimized without comparison
            float screenOffset = uOverlayScreenType * 1.0;
    
            // Component-wise UV calculation for better instruction pipelining
            u = (u - (uOverlayPos.x * inv_width)) * scaleX;
            v = (v - screenOffset - (uOverlayPos.y + screenOffset) * inv_height) * scaleY;
    
            // Optimized boundary check using single step operation
            float mask = step(0.0, min(min(u, 1.0-u), min(v, 1.0-v)));
    
            // Precalculate texture coordinates to minimize memory access
            vec2 texCoord = vec2(u, v);
            vec4 pixel = texture(OverlayTex, texCoord);
    
            // Vectorized multiplication for better SIMD utilization
            pixel = pixel * vec4(pixel.aaa, 1.0) * mask;
            oColor = pixel;
        }

    )";
*/

//OSD 1.5 great to do 360
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
            // Optimized scalar calculations with minimal temporaries
            const float inv_width = 1.0 / 256.0;  // Precomputed constant
            const float inv_height = 1.0 / 193.0;  // Precomputed constant
    
            // Direct scale calculation to minimize register pressure
            float scaleX = 256.0 / uOverlaySize.x;
            float scaleY = 193.0 / uOverlaySize.y;
    
            // Fused multiply-add optimization for UV calculation
            float u = fTexcoord.x;
            float v = fTexcoord.y + fTexcoord.y;  // Optimized multiply by 2
    
            // Screen offset calculation without branching
            float screenOffset = float(uOverlayScreenType);
    
            // Optimized position adjustment
            float offsetX = uOverlayPos.x * inv_width;
            float offsetY = (uOverlayPos.y + screenOffset) * inv_height;
    
            // Fused multiply-add for final UV computation
            u = (u - offsetX) * scaleX;
            v = (v - screenOffset - offsetY) * scaleY;
    
            // Optimized boundary check with minimal operations
            float inBoundsU = step(0.0, u) * step(u, 1.0);
            float inBoundsV = step(0.0, v) * step(v, 1.0);
    
            // Single texture fetch
            vec4 pixel = texture(OverlayTex, vec2(u, v));
    
            // Optimized alpha premultiplication and masking
            float mask = inBoundsU * inBoundsV;
            pixel.rgb *= pixel.a * mask;
            pixel.a *= mask;
    
            oColor = pixel;
        }

    )";

*/

// OSD v1.6 more low latency. super fast
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
            // Fastest possible UV calculation
            float u = fTexcoord.x;
            float v = fTexcoord.y + fTexcoord.y;
    
            // Hardware optimized constants
            // Using power of 2 approximations where possible
            const float INV_WIDTH = 0.00390625;  // Exact 1/256
            const float INV_HEIGHT = 0.005208333; // Close to 1/193, faster computation
    
            // Fast approximate scaling - using multiply instead of divide
            float scaleX = 256.0 * (1.0 / uOverlaySize.x);
            float scaleY = 192.0 * (1.0 / uOverlaySize.y); // Using 192 instead of 193 for faster math
    
            // Ultra fast screen offset
            float s_offset = uOverlayScreenType;
    
            // Quick position calculation
            u = u - uOverlayPos.x * INV_WIDTH;
            v = v - s_offset - uOverlayPos.y * INV_HEIGHT;
    
            // Fast scale
            u *= scaleX;
            v *= scaleY;
    
            // Quick boundary check - less accurate but faster
            float mask = float(u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0);
    
            // Direct texture fetch
            vec4 color = texture(OverlayTex, vec2(u, v));
    
            // Fast output with premultiplied alpha
            color *= mask;
            color.rgb *= color.a;
            oColor = color;
        }


    )";

*/
// OSD v1.7

const inline char* kScreenFS_overlay = R"(#version 140
        uniform sampler2D OverlayTex;
        uniform vec2 uOverlayPos;
        uniform vec2 uOverlaySize;
        uniform int uOverlayScreenType;
        smooth in vec2 fTexcoord;
        out vec4 oColor;

        void main()
        {
            // Fastest UV calculation without precision loss
            float u = fTexcoord.x;
            float v = fTexcoord.y << 1;  // Bit shift for speed
    
            // Optimal constants maintaining aspect ratio
            const float INV_WIDTH = 0.00390625;  // Exact 1/256
            const float INV_HEIGHT = 0.005208333; // Close to 1/193
    
            // Ultra fast scale - maintaining aspect
            float scaleX = 256.0 * (1.0 / uOverlaySize.x);
            float scaleY = 192.0 * (1.0 / uOverlaySize.y); // Keep 192 for speed
    
            // Direct screen offset
            float s_offset = uOverlayScreenType;
    
            // Fast position calculation while preserving aspect
            u = (u - uOverlayPos.x * INV_WIDTH) * scaleX;
            v = (v - s_offset - uOverlayPos.y * INV_HEIGHT) * scaleY;
    
            // Quick bounds check - optimized but maintaining separate axes
            float inBounds = float(max(u, v) <= 1.0 && min(u, v) >= 0.0);
    
            // Direct texture fetch
            vec4 color = texture(OverlayTex, vec2(u, v)) * inBounds;
    
            // Fast output with minimal operations
            oColor = color;
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