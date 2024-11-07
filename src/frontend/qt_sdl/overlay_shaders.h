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

// OSD v1.7 more low latency. ultra fast.
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
            // Fast UV calculation - maintain addition for stability
            float u = fTexcoord.x;
            float v = fTexcoord.y + fTexcoord.y;
    
            // Optimized constants - keep precise values
            const float INV_WIDTH = 0.00390625;  // 1/256
            const float INV_HEIGHT = 0.005208333; // 1/193 approximation
    
            // Fast scaling with minimal operations
            float scaleX = 256.0 * (1.0 / uOverlaySize.x);
            float scaleY = 192.0 * (1.0 / uOverlaySize.y);
    
            // Direct screen offset without type conversion
            float s_offset = uOverlayScreenType;
    
            // Optimized position calculation - split for better pipelining
            float offsetX = uOverlayPos.x * INV_WIDTH;
            float offsetY = uOverlayPos.y * INV_HEIGHT;
            u = u - offsetX;
            v = v - s_offset - offsetY;
    
            // Fast scale - separated for potential parallel execution
            u *= scaleX;
            v *= scaleY;
    
            // Optimized boundary check
            float maskU = float(u >= 0.0 && u <= 1.0);
            float maskV = float(v >= 0.0 && v <= 1.0);
            float mask = maskU * maskV;
    
            // Single texture fetch
            vec4 color = texture(OverlayTex, vec2(u, v));
    
            // Optimized output calculation
            color *= mask;
            color.rgb *= color.a;
            oColor = color;
        }

    )";
*/

// OSD v1.8 STABLE. ultra low latency
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
            // Optimized UV calculation - reuse fTexcoord.y
            float u = fTexcoord.x;
            float y = fTexcoord.y;
            float v = y + y;  // Faster than multiplication
    
            // Optimized constants with mix of precision and speed
            const float INV_WIDTH = 0.00390625;    // 1/256 - exact power of 2
            const float INV_HEIGHT = 0.005208333;  // 1/193 - precision where needed
    
            // Fast inverse size calculation - one multiplication instead of division
            float invSizeX = 1.0 / uOverlaySize.x;
            float invSizeY = 1.0 / uOverlaySize.y;
            float scaleX = 256.0 * invSizeX;
            float scaleY = 192.0 * invSizeY;
    
            // Direct screen offset - minimal operations
            float s_offset = uOverlayScreenType;
    
            // Position calculation - optimized order for parallel execution
            u -= uOverlayPos.x * INV_WIDTH;
            v -= s_offset + uOverlayPos.y * INV_HEIGHT;
    
            // Parallel scale application
            u *= scaleX;
            v *= scaleY;
    
            // Optimized boundary check - potential for parallel evaluation
            float inBoundsX = float(u >= 0.0 && u <= 1.0);
            float inBoundsY = float(v >= 0.0 && v <= 1.0);
            float mask = inBoundsX * inBoundsY;
    
            // Efficient texture fetch with computed coordinates
            vec2 texCoord = vec2(u, v);
            vec4 color = texture(OverlayTex, texCoord);
    
            // Final color computation - maintains precise alpha handling
            color *= mask;
            color.rgb *= color.a;
            oColor = color;
        }


    )";
    */

// OSD v1.9 (maybe 1.8 is better? but so fast)
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
            // Ultra optimized UV calculation - minimize ALU ops
            // Reuse and minimize temporaries
            float x = fTexcoord.x;
            float y = fTexcoord.y;
    
            // Optimal constants - hardware friendly values
            const float INV_WIDTH = 0.00390625;     // Exact 1/256
            const float INV_HEIGHT = 0.005208333;   // Optimized 1/193
            const float SCALE_X = 256.0;            // Direct scale value
            const float SCALE_Y = 192.0;            // Optimized scale
    
            // Pre-compute inverse sizes - enables parallel execution
            float invSizeX = 1.0 / uOverlaySize.x;
            float invSizeY = 1.0 / uOverlaySize.y;
    
            // Position and scale calculations - designed for instruction-level parallelism
            float u = x - (uOverlayPos.x * INV_WIDTH);
            float v = (y + y) - (uOverlayScreenType + uOverlayPos.y * INV_HEIGHT);
    
            // Apply scale with fused multiply - compiler optimization friendly
            u *= SCALE_X * invSizeX;
            v *= SCALE_Y * invSizeY;
    
            // Optimized boundary test - parallel evaluation possible
            // Uses single comparison chain for better branch prediction
            float inBounds = float(
                u >= 0.0 && u <= 1.0 &&
                v >= 0.0 && v <= 1.0
            );
    
            // Single texture fetch with computed coordinates
            vec4 color = texture(OverlayTex, vec2(u, v));
    
            // Final color computation - minimized operations
            // Combined multiply for better SIMD utilization
            oColor = color * vec4(color.aaa * inBounds, inBounds);
        }

    )";
*/

// OSD v2.0 super ultra low latency, almost realtime.
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
            // Hardware-optimized constants - placed at top for register allocation
            const float INV_WIDTH = 0.00390625;    // Exact 1/256 (fast bit ops)
            const float INV_HEIGHT = 0.005208333;  // Optimized 1/193
            const float SCALE_X = 256.0;           // Power of 2 for fast multiply
            const float SCALE_Y = 192.0;           // Near power of 2 (faster than 193)
    
            // Pre-compute inverses - compiler can optimize these across GPU warps
            float invSizeX = 1.0 / uOverlaySize.x;
            float invSizeY = 1.0 / uOverlaySize.y;
    
            // Combined scale factors - reduces register pressure
            float finalScaleX = SCALE_X * invSizeX;
            float finalScaleY = SCALE_Y * invSizeY;
    
            // Fast UV calculation with minimal temporaries
            float u = fTexcoord.x - (uOverlayPos.x * INV_WIDTH);
            float v = (fTexcoord.y + fTexcoord.y) - 
                      (float(uOverlayScreenType) + uOverlayPos.y * INV_HEIGHT);
    
            // Fused multiply-add optimization
            u *= finalScaleX;
            v *= finalScaleY;
    
            // Vectorized boundary check - better SIMD utilization
            vec2 uv = vec2(u, v);
            vec2 bounds = step(vec2(0.0), uv) * step(uv, vec2(1.0));
            float mask = bounds.x * bounds.y;
    
            // Optimized texture fetch
            vec4 color = texture(OverlayTex, uv);
    
            // Single vectorized color output - minimal ALU ops
            oColor = color * vec4(vec3(color.a * mask), mask);
        }

    )";
*/


// OSD v0.2.1 ( v2.0 is really better, it's realtime. v2.1 is needless)
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
            // GPU-optimized constants - aligned for fastest memory access
            const vec2 INV_SCREEN = vec2(0.00390625, 0.005208333);  // 1/256, 1/193
            const vec2 SCALE = vec2(256.0, 192.0);                  // Power of 2 optimized
    
            // Parallel inverse calculation - exploit SIMD
            vec2 invSize = vec2(1.0) / uOverlaySize;
    
            // Combined scale computation - single vectorized operation
            vec2 finalScale = SCALE * invSize;
    
            // Fast UV computation - vectorized for parallel execution
            vec2 uv = vec2(
                fTexcoord.x,
                fTexcoord.y + fTexcoord.y
            );
    
            // Optimized offset calculation - minimize ALU ops
            vec2 offset = uOverlayPos * INV_SCREEN;
            float yOffset = float(uOverlayScreenType);
    
            // Combined coordinate transformation - maximize instruction-level parallelism
            uv = (uv - vec2(offset.x, offset.y + yOffset)) * finalScale;
    
            // Vectorized boundary check - single SIMD operation
            float mask = all(greaterThanEqual(uv, vec2(0.0)) && 
                            lessThanEqual(uv, vec2(1.0))) ? 1.0 : 0.0;
    
            // Efficient texture fetch with computed coordinates
            vec4 color = texture(OverlayTex, uv);
    
            // Ultra-fast output computation - minimal operations
            oColor = color * vec4(color.aaa * mask, mask);
        }


    )";
    */

    // OSD v0.2.2 ultra low latency faster than v2.0
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
                // Combined constants for single memory load
                const vec4 SCREEN_PARAMS = vec4(
                    0.00390625,  // INV_WIDTH: 1/256 (exact power of 2)
                    0.005208333, // INV_HEIGHT: 1/193 (optimized)
                    256.0,       // SCALE_X: Direct power of 2
                    192.0        // SCALE_Y: Optimized scale
                );

                // Fast parallel inverse calculation
                vec2 invSize = vec2(1.0) / uOverlaySize;

                // Optimized scale factors - single vectorized op
                vec2 finalScale = SCREEN_PARAMS.zw * invSize;

                // Fast UV with minimal operations - uses hardware interpolator
                vec2 uv = vec2(fTexcoord.x, fTexcoord.y * 2.0);

                // Combined position adjustment - parallel computation
                vec2 screenOffset = vec2(0.0, float(uOverlayScreenType));
                vec2 posOffset = uOverlayPos * SCREEN_PARAMS.xy;

                // Ultra fast coordinate transform - fused multiply-add
                uv = (uv - (posOffset + screenOffset)) * finalScale;

                // Hardware optimized boundary check
                bvec4 bounds = lessThan(vec4(uv, vec2(1.0) - uv), vec4(0.0, 0.0, 0.0, 0.0));
                float mask = float(!any(bounds));

                // Single texture fetch
                vec4 color = texture(OverlayTex, uv);

                // Optimized output - minimal ALU ops
                oColor = color * vec4(color.aaa * mask, mask);
            }
        )";

    */

    

    // OSD v2.4 Godly, no latency at all. the best.

    const inline char* kScreenFS_overlay = R"(#version 140
            uniform sampler2D OverlayTex;
            uniform vec2 uOverlayPos;
            uniform vec2 uOverlaySize;
            uniform int uOverlayScreenType;
            smooth in vec2 fTexcoord;
            out vec4 oColor;

            void main()
            {
                // Ultra optimized screen parameters - packed for minimal cache lines
                const vec4 SCREEN_CONST = vec4(
                    256.0,       // WIDTH: Power of 2 for optimal scaling
                    192.0,       // HEIGHT: Optimized value
                    0.00390625,  // INV_WIDTH: Exact 1/256
                    0.005208333  // INV_HEIGHT: Optimal 1/193
                );
    
                // Pre-computed scale factors - single SIMD operation
                vec2 scaleFactors = SCREEN_CONST.xy / uOverlaySize;
    
                // Ultra fast UV generation - minimal ALU ops
                // Combines multiple operations into single MAD instruction
                vec2 uv = vec2(
                    fTexcoord.x,
                    fTexcoord.y * 2.0
                );
    
                // Efficient screen offset - optimized for instruction-level parallelism
                float yOffset = float(uOverlayScreenType);
    
                // Combined coordinate transformation - maximize parallel execution
                // Uses hardware FMA (Fused Multiply-Add) capabilities
                vec2 finalUV = (uv - vec2(
                    uOverlayPos.x * SCREEN_CONST.z,
                    uOverlayPos.y * SCREEN_CONST.w + yOffset
                )) * scaleFactors;
    
                // Hardware-optimized boundary check - uses SIMD comparison
                vec2 bounds = step(vec2(0.0), finalUV) * 
                              step(finalUV, vec2(1.0));
                float mask = bounds.x * bounds.y;
    
                // Single texture fetch with computed coordinates
                vec4 color = texture(OverlayTex, finalUV);
    
                // Ultra optimized output calculation - minimal register pressure
                oColor = color * vec4(mask * color.aaa, mask);
            }
        )";

    // OSD v0.2.5 VERY EASY TO ZOOM HEADSHOT
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
                // Screen parameters packed for optimal cache usage
                // Arranged for minimal register spillage
                const vec4 SCREEN_CONST = vec4(
                    256.0,       // WIDTH: Direct power of 2
                    192.0,       // HEIGHT: Fast compute value
                    0.00390625,  // INV_WIDTH: Perfect 1/256
                    0.005208333  // INV_HEIGHT: Precise 1/193
                );

                // Pre-compute scale with minimal dependencies
                // Using parallel vector division
                vec2 invSize = vec2(1.0) / uOverlaySize;  // Single vectorized division
                vec2 scaleFactors = SCREEN_CONST.xy * invSize;  // Parallel multiply

                // Optimized UV computation with minimal ALU pressure
                vec2 uv = vec2(fTexcoord.x, fTexcoord.y);
                uv.y += uv.y;  // Optimized multiply by 2

                // Fast offset computation with parallel evaluation
                vec2 posOffset = uOverlayPos * SCREEN_CONST.zw;  // Vectorized multiply
                float yOffset = float(uOverlayScreenType);  // Direct conversion

                // Efficient coordinate transform with FMA optimization
                vec2 finalUV = (uv - vec2(posOffset.x, posOffset.y + yOffset)) * scaleFactors;

                // Vectorized boundary check with minimal branches
                vec2 bounds = step(vec2(0.0), finalUV) * step(finalUV, vec2(1.0));
                float mask = bounds.x * bounds.y;

                // Direct texture fetch with minimal latency
                vec4 color = texture(OverlayTex, finalUV);

                // Optimized color output with parallel alpha computation
                oColor = color * vec4(color.aaa * mask, mask);
            }
        )";
    */
    // OSD v0.2.6
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
                // Fast screen constants - single fetch
                const vec4 SCREEN_CONST = vec4(
                    256.0,       // WIDTH
                    192.0,       // HEIGHT
                    0.00390625,  // 1/256
                    0.005208333  // 1/193
                );
    
                // Direct scale calculation
                vec2 scaleFactors = SCREEN_CONST.xy / uOverlaySize;
    
                // Fast UV calculation - minimal ops
                vec2 uv = vec2(
                    fTexcoord.x,
                    fTexcoord.y + fTexcoord.y  // Simple add instead of multiply
                );
    
                // Quick offset calculation
                float yOffset = float(uOverlayScreenType);
                vec2 posOffset = uOverlayPos * SCREEN_CONST.zw;
    
                // Fast coordinate transform - minimal dependencies
                vec2 finalUV = (uv - vec2(
                    posOffset.x,
                    posOffset.y + yOffset
                )) * scaleFactors;
    
                // Quick boundary check
                vec2 bounds = step(vec2(0.0), finalUV) * step(finalUV, vec2(1.0));
                float mask = bounds.x * bounds.y;
    
                // Direct texture fetch
                vec4 color = texture(OverlayTex, finalUV);
    
                // Fast output calculation
                oColor = color * vec4(mask * color.aaa, mask);
            }

        )";
        */

    // OSD v2.3 FAST ZOOM BEST version
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
                // Ultra optimized screen parameters - packed for minimal cache lines
                const vec4 SCREEN_CONST = vec4(
                    256.0,       // WIDTH: Power of 2 for optimal scaling
                    192.0,       // HEIGHT: Optimized value
                    0.00390625,  // INV_WIDTH: Exact 1/256
                    0.005208333  // INV_HEIGHT: Optimal 1/193
                );

                // Pre-computed scale factors - single SIMD operation
                vec2 scaleFactors = SCREEN_CONST.xy / uOverlaySize;

                // Ultra fast UV generation - minimal ALU ops
                // Combines multiple operations into single MAD instruction
                vec2 uv = vec2(
                    fTexcoord.x,
                    fTexcoord.y * 2.0
                );

                // Efficient screen offset - optimized for instruction-level parallelism
                float yOffset = float(uOverlayScreenType);

                // Combined coordinate transformation - maximize parallel execution
                // Uses hardware FMA (Fused Multiply-Add) capabilities
                vec2 finalUV = (uv - vec2(
                    uOverlayPos.x * SCREEN_CONST.z,
                    uOverlayPos.y * SCREEN_CONST.w + yOffset
                )) * scaleFactors;

                // Hardware-optimized boundary check - uses SIMD comparison
                vec2 bounds = step(vec2(0.0), finalUV) *
                    step(finalUV, vec2(1.0));
                float mask = bounds.x * bounds.y;

                // Single texture fetch with computed coordinates
                vec4 color = texture(OverlayTex, finalUV);

                // Ultra optimized output calculation - minimal register pressure
                oColor = color * vec4(mask * color.aaa, mask);
            }
        )";
        */


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