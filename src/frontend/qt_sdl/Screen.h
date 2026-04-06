/*
    Copyright 2016-2026 melonDS team

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

#ifndef SCREEN_H
#define SCREEN_H

#include <optional>
#include <deque>
#include <map>

#include <QWidget>
#include <QImage>
#include <QMutex>
#include <QScreen>
#include <QCloseEvent>
#include <QEnterEvent>
#include <QTimer>
#include <QFont>

#include "glad/glad.h"
#include "ScreenLayout.h"
#include "duckstation/gl/context.h"


class MainWindow;
class EmuInstance;

#ifdef MELONPRIME_CUSTOM_HUD
class HudEditSidePanel;
#endif // MELONPRIME_CUSTOM_HUD


const struct { int id; float ratio; const char* label; } aspectRatios[] =
{
    { 0, 1,                       "4:3 (native)" },
    { 4, (5.f / 3) / (4.f / 3), "5:3 (3DS)"},
    { 1, (16.f / 9) / (4.f / 3),  "16:9" },
#ifdef MELONPRIME_DS
    { 2, (21.f / 9) / (4.f / 3),  "21:9" },
    { 3, 0,                       "window" }
#endif
};
constexpr int AspectRatiosNum = sizeof(aspectRatios) / sizeof(aspectRatios[0]);


class ScreenPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenPanel(QWidget* parent);
    virtual ~ScreenPanel();

    void setFilter(bool filter);

    void setMouseHide(bool enable, int delay);

#ifndef MELONPRIME_DS
    QTimer* setupMouseTimer();
    void updateMouseTimer();
    QTimer* mouseTimer;
#endif

    QSize screenGetMinSize(int factor);

    void osdSetEnabled(bool enabled);
    void osdAddMessage(unsigned int color, const char* msg);

    virtual void drawScreen() {}// = 0;

#ifdef MELONPRIME_DS
    void unfocus();

    int getDelta() {
        // Store and reset in one operation for optimal performance
        int currentDelta = wheelDelta;
        wheelDelta = 0;
        return currentDelta;
    }

public slots:
    void clipCursorCenter1px();
    void unclip();
    void updateClipIfNeeded();
#endif // MELONPRIME_DS

private slots:
    void onScreenLayoutChanged();
    void onAutoScreenSizingChanged(int sizing);

protected:
    MainWindow* mainWindow;
    EmuInstance* emuInstance;

    bool filter;

    int screenRotation;
    int screenGap;
    int screenLayout;
    bool screenSwap;
    int screenSizing;
    bool integerScaling;
    int screenAspectTop, screenAspectBot;
    bool inGameTopScreenOnly = false;

    int autoScreenSizing;

    ScreenLayout layout;

#ifdef MELONPRIME_DS
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
#endif

    float screenMatrix[kMaxScreenTransforms][6];
    int screenKind[kMaxScreenTransforms];
    int numScreens;

    bool touching = false;

    bool mouseHide;
    int mouseHideDelay;

    struct OSDItem
    {
        unsigned int id;
        qint64 timestamp;

        char text[256];
        unsigned int color;

        bool rendered;
        QImage bitmap;

        int rainbowstart;
        int rainbowend;
    };

#ifdef MELONPRIME_DS
    int wheelDelta = 0;
    void wheelEvent(QWheelEvent* event) override;
#endif

    QMutex osdMutex;
    bool osdEnabled;
    unsigned int osdID;
    std::deque<OSDItem> osdItems;

#ifdef MELONPRIME_CUSTOM_HUD
    QImage Overlay[2];       // [0]=Top, [1]=Bottom — ARGB32_Premultiplied, 256x192 (DS-native space)
    QFont overlayFont;
    HudEditSidePanel* m_hudEditPanel = nullptr;
#endif

#ifdef MELONPRIME_DS
    // OPT-OSD1: Skip osdUpdate mutex + syscall when no OSD items and splash rendered.
    bool m_splashRendered = false;
#endif

    QPixmap splashLogo;
    OSDItem splashText[3];
    QPoint splashPos[4];

    void loadConfig();

    virtual void setupScreenLayout();

    void resizeEvent(QResizeEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

    void tabletEvent(QTabletEvent* event) override;
    void touchEvent(QTouchEvent* event);
    bool event(QEvent* event) override;

#ifndef MELONPRIME_DS
    void showCursor();
#endif

    int osdFindBreakPoint(const char* text, int i);
    void osdLayoutText(const char* text, int* width, int* height, int* breaks);
    unsigned int osdRainbowColor(int inc);

    virtual void osdRenderItem(OSDItem* item);
    virtual void osdDeleteItem(OSDItem* item);

    void osdUpdate();

    void calcSplashLayout();

#ifdef MELONPRIME_DS
protected:
    void refreshClipForGameStateChange();

private:
    void applyInGameTopScreenOnlyOverride(int& layout, int& sizing) const;
    bool shouldConfineCursorToBottomScreen() const;
    std::optional<QRect> getBottomScreenWidgetRect() const;
    void clipCursorToBottomScreen();
    void setClipWanted(bool value);
    bool getClipWanted() const;
    bool m_lastInGameTopScreenOnlyOverride = false;
    bool m_hasLastInGameTopScreenOnlyOverride = false;
    bool m_lastClipInGameState = false;
    bool m_hasLastClipInGameState = false;
    bool m_lastClipFocusedState = false;
    bool m_hasLastClipFocusedState = false;
#endif
};


class ScreenPanelNative : public ScreenPanel
{
    Q_OBJECT

public:
    explicit ScreenPanelNative(QWidget* parent);
    virtual ~ScreenPanelNative();

    void drawScreen() override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupScreenLayout() override;

    QMutex bufferLock;
    bool hasBuffers;
    void* topBuffer;
    void* bottomBuffer;

    QImage screen[2];
    QTransform screenTrans[kMaxScreenTransforms];
};


class ScreenPanelGL : public ScreenPanel
{
    Q_OBJECT

public:
    explicit ScreenPanelGL(QWidget* parent);
    virtual ~ScreenPanelGL();

    std::optional<WindowInfo> getWindowInfo();

    bool createContext();

    void setSwapInterval(int intv);

    void initOpenGL();
    void deinitOpenGL();
    void makeCurrentGL();
    void releaseGL();

    void drawScreen() override;

    GL::Context* getContext() { return glContext.get(); }

    void transferLayout();
protected:

    qreal devicePixelRatioFromScreen() const;
    int scaledWindowWidth() const;
    int scaledWindowHeight() const;

    QPaintEngine* paintEngine() const override;

private:
    void setupScreenLayout() override;

    std::unique_ptr<GL::Context> glContext;
    bool glInited;

    GLuint screenVertexBuffer, screenVertexArray;
    GLuint screenTexture;
    GLuint screenShaderProgram;
    GLint screenShaderTransformULoc, screenShaderScreenSizeULoc;

    QMutex screenSettingsLock;
    WindowInfo windowInfo;

    int lastScreenWidth = -1, lastScreenHeight = -1;

#ifdef MELONPRIME_DS
    // OPT-GL1: Cache GL texture filter to skip redundant glTexParameteri calls.
    // Safe: texture parameters are per-texture, not global GL state.
    GLint lastFilter = -1;
#endif

    GLuint osdShader;
    GLint osdScreenSizeULoc, osdPosULoc, osdSizeULoc;
    GLint osdScaleFactorULoc;
    GLint osdTexScaleULoc;
    GLuint osdVertexArray;
    GLuint osdVertexBuffer;
    std::map<unsigned int, GLuint> osdTextures;

#ifdef MELONPRIME_CUSTOM_HUD
    GLuint overlayTextures[2];  // GL_TEXTURE_2D per screen (top/bottom), resized to match hi-res HUD buffer
    int overlayTexW = 0, overlayTexH = 0; // currently allocated texture dimensions
    GLuint btmOverlayShader;
    GLint btmOverlayScreenSizeULoc, btmOverlayOpacityULoc, btmOverlaySrcCenterULoc, btmOverlaySrcRadiusULoc;
    GLuint btmOverlayVertexArray, btmOverlayVertexBuffer;
#endif

    GLuint logoTexture;

    void osdRenderItem(OSDItem* item) override;
    void osdDeleteItem(OSDItem* item) override;
};

#endif // SCREEN_H
