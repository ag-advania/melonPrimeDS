// Custom HUD OpenGL resource cleanup for ScreenPanelGL::deinitOpenGL().
// This is a unity-build fragment included by Screen.cpp; do not add it to CMakeLists.txt.

#ifdef MELONPRIME_CUSTOM_HUD
glDeleteTextures(2, overlayTextures);
glDeleteProgram(btmOverlayShader);
glDeleteBuffers(1, &btmOverlayVertexBuffer);
glDeleteVertexArrays(1, &btmOverlayVertexArray);
#endif
