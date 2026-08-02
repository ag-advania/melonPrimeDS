// Custom HUD OpenGL resource initialization for ScreenPanelGL::initOpenGL().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
    // A live Vulkan -> OpenGL transition publishes the new panel before the
    // emulation thread finishes initOpenGL(). Reset the CPU-side upload state
    // together with the real GL objects so no dimensions or dirty tracking
    // from an earlier/pre-initialization presentation attempt can suppress the
    // first valid Custom HUD upload.
    overlayTexW = 0;
    overlayTexH = 0;
    m_hudPrevDirty = QRect();
    m_hudUploadedRect = QRect();
    m_hudUploadedHash = 0;
    m_hudUploadedValid = false;

    // Create 2 overlay textures (GL_TEXTURE_2D, 256x192 each, one per screen)
    glGenTextures(2, overlayTextures);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, overlayTextures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }

    // Bottom screen overlay shader (textured quad with opacity + Y-flip)
    OpenGL::CompileVertexFragmentProgram(btmOverlayShader,
        kBtmOverlayVS, kBtmOverlayFS,
        "BtmOverlayShader",
        { {"vPosition", 0}, {"vTexcoord", 1} },
        { {"oColor", 0} });

    glUseProgram(btmOverlayShader);
    glUniform1i(glGetUniformLocation(btmOverlayShader, "ScreenTex"), 0);
    btmOverlayScreenSizeULoc = glGetUniformLocation(btmOverlayShader, "uScreenSize");
    btmOverlayOpacityULoc = glGetUniformLocation(btmOverlayShader, "uOpacity");
    btmOverlaySrcCenterULoc = glGetUniformLocation(btmOverlayShader, "uSrcCenter");
    btmOverlaySrcRadiusULoc = glGetUniformLocation(btmOverlayShader, "uSrcRadius");

    // OPT-SH1: Upload radar palette colors as uniform array (set once at init).
    // Corresponds to uPalette[PALETTE_SIZE] in main_shaders.h kBtmOverlayFS.
    // These are the active radar palette colors (nodes, octoliths, etc.)
    // Commented-out colors below are hunter-specific radar dots — currently
    // filtered by the radar frame overlay, but kept here for future reference:
    //   68E028 (104,224, 40) - green Samus radar
    //   F8F858 (248,248, 88) - yellow Kanden radar
    //   F87038 (248,112, 56) - orange Spire radar
    //   E01018 (224, 16, 24) - red Trace radar
    //   5098D0 ( 80,152,208) - blue Noxus radar
    //   D0F0A0 (208,240,160) - pale green Sylux radar
    //   D09838 (208,152, 56) - amber Weavel radar
    //   F8F898 (248,248,152) - center of Kanden radar
    //   529CD6, 6582B1       - Noxus radar blue variants
    {
        static_assert(MelonPrime::kRadarPaletteColorCount == 15,
            "OpenGL radar shader palette size must stay in sync");
        float palette[MelonPrime::kRadarPaletteColorCount][3] = {};
        for (int i = 0; i < MelonPrime::kRadarPaletteColorCount; ++i) {
            const uint32_t rgb = MelonPrime::kRadarPaletteColors[i];
            palette[i][0] = static_cast<float>((rgb >> 16) & 0xFF);
            palette[i][1] = static_cast<float>((rgb >> 8) & 0xFF);
            palette[i][2] = static_cast<float>(rgb & 0xFF);
        }
        GLint paletteLoc = glGetUniformLocation(btmOverlayShader, "uPalette");
        glUniform3fv(paletteLoc, MelonPrime::kRadarPaletteColorCount, &palette[0][0]);
    }

    // Quad: 6 vertices, each with position(x,y) + texcoord(u,v)
    // Texcoords map source rect — will be updated dynamically
    const float btmOverlayVerts[6 * 4] =
    {
        0, 0,  0, 0,
        0, 1,  0, 1,
        1, 1,  1, 1,
        0, 0,  0, 0,
        1, 1,  1, 1,
        1, 0,  1, 0,
    };

    glGenBuffers(1, &btmOverlayVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, btmOverlayVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(btmOverlayVerts), btmOverlayVerts, GL_STATIC_DRAW);

    glGenVertexArrays(1, &btmOverlayVertexArray);
    glBindVertexArray(btmOverlayVertexArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); // texcoord
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
#endif
