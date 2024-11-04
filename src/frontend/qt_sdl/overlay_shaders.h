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

��Ȓ჌�x���œK���|�C���g�F

�X�J���[���Z�̊��p


�x�N�g�����Z���ʂ̃X�J���[���Z�ɕ���
GPU ��SIMD���j�b�g�̌����I�Ȏg�p


���Z�̍œK��


�萔���Z����Z�ɕϊ��iinv_width/inv_height �̎g�p�j
���Z���߂̃��C�e���V���팸


�������A�N�Z�X�̍œK��


�e�N�X�`���t�F�b�`��1��ɏW��
�ˑ��e�N�X�`���ǂݍ��݂̍ŏ���


���߃��x���̕��񐫁iILP�j����


���Z�̈ˑ��֌W�����炷
�p�C�v���C���E�X�g�[���̍팸


��������̍œK��


���򖽗߂��Z�p���Z�ɕϊ�
���s�p�X�̗\��������

���̃o�[�W�����́F

��菭�Ȃ�GPU���߂Ŏ��s�\
�������o���h���̎g�p���œK��
�������I��GPU�p�C�v���C���̎g�p
�L���b�V���q�b�g���̌���

�������A���̂悤�Ȓ჌�x���ȍœK���́F

GPU�A�[�L�e�N�`���Ɉˑ�����\��������
�R���p�C���̍œK���Ƃ̑������l������K�v������
���ۂ̃p�t�H�[�}���X����͎g�p���Ńe�X�g���K�v

�X�Ȃ�œK���̉\���F

uniform�ϐ��̃p�b�L���O
�e�N�X�`���T���v�����O�̍œK��
�V�F�[�_�[�o���A���g�̐���
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
���჌�x���ȍœK���̃|�C���g�F

���������C�A�E�g�̍œK��


uniform�ϐ��̃p�b�L���O�ivec4�ɂ܂Ƃ߂�j
�������A�N�Z�X�p�^�[���̍œK��
�L���b�V�����C�������̌���


�萔�̍œK��


���Z����Z�ɕϊ�
�萔�𒼐ړI�Ȑ��l�ɓW�J
�R���p�C�����̒萔��ݍ��ݍœK��


���Z�̍œK��


MAD�iMultiply-Add�j���߂̊��p
SIMD���j�b�g�̌����I�ȗ��p
�ˑ��֌W�̂��鉉�Z�̍ŏ���


�e�N�X�`���A�N�Z�X�̍œK��


�e�N�X�`���L���b�V���̌����I�Ȏg�p
UV�v�Z�̒P����
�������o���h���̍œK��


�p�C�v���C�������̌���


���򖽗߂̊��S�Ȕr��
���߃��x�����񐫂̍ő剻
���W�X�^�g�p�̍œK��


�n�[�h�E�F�A��Ԋ�̊��p


step�֐��̌����I�Ȏ���
�n�[�h�E�F�A�̓����������������

�R���p�C���ւ̃q���g�F

�}�N����`�ɂ��萔�̖���
���Z�����̖����I�Ȏw��
���W�X�^���͂̒ጸ

�����̍œK���ɂ��F

�������o���h���̎g�p���ŏ���
���ߐ����팸
�p�C�v���C���X�g�[�����ŏ���
�L���b�V������������
���C�e���V���ጸ

���ӓ_�F

���̍œK����GPU�A�[�L�e�N�`���ɋ����ˑ�
�h���C�o�[�̍œK���Ƃ̑��ݍ�p���l��
���ۂ̃p�t�H�[�}���X�͊��ˑ�

����ȏ�̍œK�����s���ꍇ�F

�A�Z���u�����x���ł̍œK��
�V�F�[�_�[�o���A���g�̐���
�v���b�g�t�H�[���ŗL�̍œK��
����������K�v������܂��B

rev
��ȏC���_�F

uniform�ϐ��̃p�b�L���O���ێ����A�킩��₷���W�J
dsSize�萔�𕜊�
UV�v�Z�̃��W�b�N�����肵���`�ɖ߂�
�ߓx�ȍœK������菜���A�ǐ��ƈ��萫������

�œK���̃|�C���g�i�ێ��j�F

uniform�ϐ��̃p�b�L���O
�ŏ����̕��򏈗�
�����I�ȋ��E�`�F�b�N
�e�N�X�`���t�F�b�`�̍œK��
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