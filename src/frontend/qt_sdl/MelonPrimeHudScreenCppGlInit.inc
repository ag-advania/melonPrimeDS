// Custom HUD OpenGL resource initialization for ScreenPanelGL::initOpenGL().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
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
        const float palette[15][3] = {
            {192, 248, 104},  // C0F868 - yellow-green
            {248, 168, 168},  // F8A8A8 - pink, node red middle
            {224,  48,  48},  // E03030 - node red outer and center
            {160, 160, 160},  // A0A0A0 - octolith gray top
            {200, 200, 200},  // C8C8C8 - octolith gray center
            {144, 144, 144},  // 909090 - octolith gray bottom
            {248, 128,  16},  // F88010 - octolith orange top
            {248, 208, 160},  // F8D0A0 - octolith orange center
            {216, 104,   0},  // D86800 - octolith orange bottom
            {136, 224,   8},  // 88E008 - octolith green top
            {200, 248, 128},  // C8F880 - octolith green center
            {104, 184,   0},  // 68B800 - octolith green bottom
            { 16, 152, 200},  // 1098C8 - node blue outer and center
            { 40, 216, 248},  // 28D8F8 - node blue middle
            {168, 168, 168},  // A8A8A8 - node gray
        };
        GLint paletteLoc = glGetUniformLocation(btmOverlayShader, "uPalette");
        glUniform3fv(paletteLoc, 15, &palette[0][0]);
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
