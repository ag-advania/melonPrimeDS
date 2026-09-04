/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Custom HUD OpenGL resource lifecycle. The draw fragment remains at its
// call-site until a measured extraction can prove that a function boundary
// does not regress the renderer hot path; init/deinit are cold and have no
// such coupling.

#include "Screen.h"

#ifdef MELONPRIME_CUSTOM_HUD

#include <cstdint>

#include "MelonPrimeConstants.h"
#include "MelonPrimeHudRadar.h"
#include "OpenGLSupport.h"

extern const char* kBtmOverlayVS;
extern const char* kBtmOverlayFS;

void ScreenPanelGL::initializeHudOpenGL()
{
    // Reset CPU upload state with the GL objects. During a live backend
    // switch, pre-initialization draw attempts must not suppress the first
    // valid HUD upload.
    overlayTexW = 0;
    overlayTexH = 0;
    m_hudVisualFrameValid = false;
    ++m_hudVisualRendererGeneration;
    m_hudPrevDirty = QRect();
    m_hudUploadedRect = QRect();
    m_hudUploadedHash = 0;
    m_hudUploadedValid = false;
    m_hudRadarGl = {};

    glGenSamplers(1, &m_hudRadarSampler);
    glSamplerParameteri(m_hudRadarSampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_hudRadarSampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(m_hudRadarSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(m_hudRadarSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenTextures(2, overlayTextures);
    for (int i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, overlayTextures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    melonDS::OpenGL::CompileVertexFragmentProgram(
        btmOverlayShader, kBtmOverlayVS, kBtmOverlayFS,
        "BtmOverlayShader",
        {{"vPosition", 0}, {"vTexcoord", 1}},
        {{"oColor", 0}});

    glUseProgram(btmOverlayShader);
    glUniform1i(glGetUniformLocation(btmOverlayShader, "ScreenTex"), 0);
    btmOverlayScreenSizeULoc =
        glGetUniformLocation(btmOverlayShader, "uScreenSize");
    btmOverlayOpacityULoc =
        glGetUniformLocation(btmOverlayShader, "uOpacity");
    btmOverlaySrcCenterULoc =
        glGetUniformLocation(btmOverlayShader, "uSrcCenter");
    btmOverlaySrcRadiusULoc =
        glGetUniformLocation(btmOverlayShader, "uSrcRadius");

    static_assert(MelonPrime::kRadarPaletteColorCount == 15,
        "OpenGL radar shader palette size must stay in sync");
    float palette[MelonPrime::kRadarPaletteColorCount][3]{};
    for (int i = 0; i < MelonPrime::kRadarPaletteColorCount; ++i) {
        const std::uint32_t rgb = MelonPrime::kRadarPaletteColors[i];
        palette[i][0] = static_cast<float>((rgb >> 16) & 0xFF);
        palette[i][1] = static_cast<float>((rgb >> 8) & 0xFF);
        palette[i][2] = static_cast<float>(rgb & 0xFF);
    }
    const GLint paletteLocation =
        glGetUniformLocation(btmOverlayShader, "uPalette");
    glUniform3fv(
        paletteLocation, MelonPrime::kRadarPaletteColorCount, &palette[0][0]);

    const float vertices[6 * 4] = {
        0, 0,  0, 0,
        0, 1,  0, 1,
        1, 1,  1, 1,
        0, 0,  0, 0,
        1, 1,  1, 1,
        1, 0,  1, 0,
    };

    glGenBuffers(1, &btmOverlayVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, btmOverlayVertexBuffer);
    glBufferData(
        GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenVertexArrays(1, &btmOverlayVertexArray);
    glBindVertexArray(btmOverlayVertexArray);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
        reinterpret_cast<void*>(2 * sizeof(float)));
}

void ScreenPanelGL::deinitializeHudOpenGL()
{
    glBindSampler(0, 0);
    if (m_hudRadarSampler != 0) {
        glDeleteSamplers(1, &m_hudRadarSampler);
        m_hudRadarSampler = 0;
    }
    glDeleteTextures(2, overlayTextures);
    glDeleteProgram(btmOverlayShader);
    glDeleteBuffers(1, &btmOverlayVertexBuffer);
    glDeleteVertexArrays(1, &btmOverlayVertexArray);
    m_hudVisualFrameValid = false;
    m_hudRadarGl = {};
    ++m_hudVisualRendererGeneration;
}

#endif // MELONPRIME_CUSTOM_HUD
