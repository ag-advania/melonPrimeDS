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
#include <atomic>
#include <cstdint>
#include <memory>

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

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudConfigOnScreenEdit.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeLocalization.h"

// The emulation identity is probed separately from the extended stamp.  New
// game frames can therefore render immediately without constructing a full
// key; repeated presentation of one frame still validates the complete stamp.
struct HudVisualFrameIdentity {
    const void* nds = nullptr;
    uint32_t gameFrame = 0;

    bool operator==(const HudVisualFrameIdentity& other) const noexcept
    {
        return nds == other.nds && gameFrame == other.gameFrame;
    }
};

struct HudVisualFrameStamp {
    uint32_t configEpoch = 0;
    uint32_t fontEpoch = 0;
    uint32_t stateGeneration = 0;
    int menuLanguage = 0;
    int overlayWidth = 0;
    int overlayHeight = 0;
    float topStretchX = 0.0f;
    float hudScale = 0.0f;
    float originX = 0.0f;
    float originY = 0.0f;
    uint64_t rendererGeneration = 0;
    bool hudEnabled = false;
    bool editMode = false;

    bool operator==(const HudVisualFrameStamp& other) const noexcept
    {
        return configEpoch == other.configEpoch
            && fontEpoch == other.fontEpoch
            && stateGeneration == other.stateGeneration
            && menuLanguage == other.menuLanguage
            && overlayWidth == other.overlayWidth
            && overlayHeight == other.overlayHeight
            && topStretchX == other.topStretchX
            && hudScale == other.hudScale
            && originX == other.originX
            && originY == other.originY
            && rendererGeneration == other.rendererGeneration
            && hudEnabled == other.hudEnabled
            && editMode == other.editMode;
    }
};

struct HudVisualFrameKey {
    HudVisualFrameIdentity identity{};
    HudVisualFrameStamp stamp{};

    bool operator==(const HudVisualFrameKey& other) const noexcept
    {
        return identity == other.identity && stamp == other.stamp;
    }
};
#endif // MELONPRIME_CUSTOM_HUD

class MainWindow;
class EmuInstance;
class EmuThread;

#ifdef MELONPRIME_CUSTOM_HUD
static inline HudVisualFrameIdentity MelonPrimeHud_ProbeVisualFrameIdentity(
    EmuInstance* emu)
{
    HudVisualFrameIdentity identity;
    identity.gameFrame = MelonPrime::CustomHud_GetVisualGameFrame(
        emu, &identity.nds);
    return identity;
}

static inline bool MelonPrimeHud_IsSameVisualGameFrame(
    const HudVisualFrameIdentity& identity,
    const HudVisualFrameKey& previous)
{
    return previous.identity == identity;
}

static inline HudVisualFrameKey MelonPrimeHud_MakeVisualFrameKey(
    const HudVisualFrameIdentity& identity,
    const MelonPrime::CustomHudConfigState& hudConfig,
    uint32_t configEpoch,
    uint32_t fontEpoch,
    int overlayWidth,
    int overlayHeight,
    float topStretchX,
    float hudScale,
    float originX,
    float originY,
    uint64_t rendererGeneration,
    bool hudEnabled,
    bool editMode)
{
    HudVisualFrameKey key;
    key.identity = identity;
    key.stamp.configEpoch = configEpoch;
    key.stamp.fontEpoch = fontEpoch;
    key.stamp.stateGeneration = MelonPrime::CustomHud_GetVisualGeneration(hudConfig);
    key.stamp.menuLanguage = static_cast<int>(MelonPrime::UiText::ActiveMenuLanguage());
    key.stamp.overlayWidth = overlayWidth;
    key.stamp.overlayHeight = overlayHeight;
    key.stamp.topStretchX = topStretchX;
    key.stamp.hudScale = hudScale;
    key.stamp.originX = originX;
    key.stamp.originY = originY;
    key.stamp.rendererGeneration = rendererGeneration;
    key.stamp.hudEnabled = hudEnabled;
    key.stamp.editMode = editMode;
    return key;
}
#endif

#ifdef MELONPRIME_DS
namespace MelonPrime {
class MelonPrimeCore;
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
class WaylandPointerLock; // MELONPRIME_WAYLAND_POINTER_LOCK_V1
#endif
}
#endif


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
    // Drop any borrowed RendererOutput pointers before the renderer that owns
    // them is replaced. GPU-owned presenters override this only if needed.
    virtual void invalidateRendererOutput() {}
    virtual void beginModalPausePresentation() {}
    virtual void endModalPausePresentation() {}
#if defined(MELONPRIME_ENABLE_VULKAN)
    // NVIDIA Reflex / AMD Anti-Lag 2 frame hooks, driven by the emulation
    // thread once per emulated frame. Only ScreenPanelVulkan implements them;
    // every other panel inherits these no-ops.
    //
    // They cover the part of the frame the emulation thread can see: the
    // latency sleep, input sampling, and the simulation interval around
    // NDS::RunFrame(). The RENDERSUBMIT_* and PRESENT_* markers are
    // deliberately absent, because the emulation thread is not where the real
    // vkQueueSubmit and vkQueuePresentKHR happen -- VulkanPresenter::EndFrame()
    // emits those directly around the actual calls, which is the only place
    // they can be accurate.
    //
    // `reflexMode` is the raw config value (0 off, 1 on, 2 on+boost) and is
    // pushed every frame so a settings change applies on the next one.
    //
    // `targetFrameIntervalNs` is the emulator's frame interval for this frame,
    // or 0 when it is not running at a fixed rate. The Vulkan present pacer
    // needs the emulator's own cadence -- never the display refresh rate -- to
    // compute a presentation target time.
    virtual void beginVulkanLowLatencyFrame(
        int reflexMode,
        bool antiLag2Enabled,
        bool normalSpeed,
        melonDS::u64 targetFrameIntervalNs)
    {
        (void)reflexMode;
        (void)antiLag2Enabled;
        (void)normalSpeed;
        (void)targetFrameIntervalNs;
    }
    virtual void markVulkanReflexInputSample() {}
    virtual void markVulkanReflexSimulationStart() {}
    virtual void markVulkanReflexSimulationEnd() {}
    virtual void finishVulkanLowLatencyFrame() {}
#endif
#ifdef MELONPRIME_CUSTOM_HUD
    // Hand-off for the Custom HUD on-screen editor, which runs while the
    // settings dialog keeps emulation paused. The emulation thread is stopped
    // for the whole session, so anything it would normally reconcile (cursor
    // mode) and anything that stops on pause (Vulkan presentation) has to be
    // driven from here instead. The native-surface backends (Vulkan, DX12)
    // additionally have to keep presenting while paused, or the editor overlay
    // never reaches the screen at all.
    virtual void setHudEditModeActive(bool active);
    // The regular Custom HUD settings page also uses the paused emulation
    // thread to repaint the real top-screen HUD while its controls change.
    // This is separate from edit mode so it does not expose editor hit targets
    // or change cursor ownership.
    virtual void setHudLivePreviewActive(bool active) { (void)active; }
#endif

    void unfocus();
    void beginClose();

#ifdef MELONPRIME_CUSTOM_HUD
    std::optional<QRect> getTopScreenWidgetRect() const;
#endif

    void getAimMouseDelta(std::int32_t& outDx, std::int32_t& outDy);
    void resetAimMouseDelta();

    void reloadNoRomSplashLocalization();
    void containAimCursorIfNeeded();
    void syncMelonPrimeThreadBridge();
    // Explicit settings-dialog save wins over any older debounced hotkey save.
    void cancelMelonPrimeDeferredConfigSave();

    // Narrow accessors for MelonPrimeScreenCursorPolicy (avoid friend coupling).
    [[nodiscard]] bool isClosingForMelonPrime() const noexcept { return closing; }
    [[nodiscard]] bool isActiveVisibleWindowForMelonPrime() const;
    [[nodiscard]] MelonPrime::MelonPrimeCore* melonPrimeCoreForPolicy() const;
    [[nodiscard]] QRect aimContainmentLocalRectForPolicy() const;
    [[nodiscard]] QPoint aimContainmentCenterGlobalForPolicy() const;
    [[nodiscard]] bool shouldConfineCursorToBottomScreenForPolicy() const;
    void clipCursorToBottomScreenForPolicy();
    [[nodiscard]] std::optional<QRect> getBottomScreenWidgetRectForPolicy() const;
    [[nodiscard]] EmuInstance* emuInstanceForPolicy() const { return emuInstance; }
    void setClipWantedForMelonPrime(bool value);
    [[nodiscard]] bool getClipWantedForMelonPrime() const;
#if defined(__linux__)
    // Native Wayland implementations override these hooks. Other Linux
    // backends retain the existing XInput2 / Qt fallback behavior.
    virtual bool setWaylandPointerLockForMelonPrime(bool enabled)
    {
        (void)enabled;
        return false;
    }
    [[nodiscard]] virtual bool isWaylandPointerLockActiveForMelonPrime() const
    {
        return false;
    }
    void addAimMouseDeltaForMelonPrime(std::int32_t dx, std::int32_t dy) noexcept;
#endif

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
#if !defined(_WIN32)
    // Previous-position differencing baseline for the Qt fallback aim path.
    // aimLastGlobal is GUI-thread-only; the validity flag is atomic because
    // the emu thread invalidates it via resetAimMouseDelta().
    QPoint aimLastGlobal;
    std::atomic<bool> aimLastGlobalValid{ false };
#endif
    void wheelEvent(QWheelEvent* event) override;
#endif

    QMutex osdMutex;
    bool osdEnabled;
    unsigned int osdID;
    std::deque<OSDItem> osdItems;

#ifdef MELONPRIME_CUSTOM_HUD
    QImage Overlay[2];       // [0]=Top HUD, [1]=software radar color-key scratch
    QFont overlayFont;
    MelonPrimeHudConfigOnScreenEdit* m_hudEditPanel = nullptr;
    // Layout values cached in setupScreenLayout() — avoids sqrt per-frame.
    float m_hudScale      = 1.0f;
    float m_topStretchX   = 1.0f;
    float m_hudOriginX    = 0.0f;
    float m_hudOriginY    = 0.0f;
    bool  m_hudTopMatrixValid = false;
    float m_hudTopMatrix[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    // Config values cached per epoch — avoids hash-map lookups per-frame.
    uint32_t m_hudCfgEpoch   = ~0u;
    bool     m_hudEnabled    = false;
    // overlayFont rebuilt from CustomHud_ResolveBaseFont when this epoch changes.
    uint32_t m_hudFontEpoch  = ~0u;
    // BtmOverlay config cache (all renderer paths):
    uint32_t m_radarCfgEpoch = ~0u;
    bool     m_radarEnable    = false;
    int      m_radarAnchor    = 2;
    int      m_radarDstX      = 0;
    int      m_radarDstY      = 0;
    int      m_radarDstSize   = 64;
    float    m_radarOpacity   = 0.85f;
    int      m_radarSrcRadius = 46;
    float    m_radarAnchorDsX = 256.0f;
    float    m_radarAnchorDsY = 0.0f;
    QRect    m_hudPrevDirty;
    QRect    m_hudUploadedRect;
    uint64_t m_hudUploadedHash = 0;
    bool     m_hudUploadedValid = false;
    HudVisualFrameKey m_hudVisualFrameKey;
    bool     m_hudVisualFrameValid = false;
    bool     m_hudVisualFrameWasReused = false;
    uint64_t m_hudVisualRendererGeneration = 1;
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
    MelonPrime::MelonPrimeCore* melonPrimeCore() const;
    void applyInGameTopScreenOnlyOverride(int& layout, int& sizing) const;
    bool shouldConfineCursorToBottomScreen() const;
    std::optional<QRect> getScreenWidgetRect(int wantedScreenKind) const;
    std::optional<QRect> getBottomScreenWidgetRect() const;
    void clipCursorToBottomScreen();
    void releaseCursorStateForClose();
    QRect aimContainmentLocalRect() const;
    void processMelonPrimePersistRequests();
    void scheduleMelonPrimeConfigSave();
    void flushMelonPrimeConfigSave();
    void setClipWanted(bool value);
    bool getClipWanted() const;
    bool m_lastInGameTopScreenOnlyOverride = false;
    bool m_hasLastInGameTopScreenOnlyOverride = false;
    bool m_lastClipInGameState = false;
    bool m_hasLastClipInGameState = false;
    bool m_lastClipFocusedState = false;
    bool m_hasLastClipFocusedState = false;
    // EmuThread requests a GUI-thread cursor/state reconciliation. The atomic
    // coalesces repeated per-frame requests without touching QWidget off-thread.
    std::atomic_bool m_melonPrimeGuiRefreshQueued{false};
    QTimer m_melonPrimeConfigSaveTimer;
    bool m_melonPrimeConfigSavePending = false;
    uint64_t m_melonPrimeLastPersistGeneration = 0;
    bool closing = false;
#endif
};


class ScreenPanelNative : public ScreenPanel
{
    Q_OBJECT

public:
    explicit ScreenPanelNative(QWidget* parent);
    virtual ~ScreenPanelNative();

    void drawScreen() override;
#ifdef MELONPRIME_DS
    void invalidateRendererOutput() override;
#endif

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    // ScreenPanelNative has no wl_surface of its own (unlike ScreenPanelGL, it
    // is not a Qt::WA_NativeWindow); these lock the top-level window's surface
    // instead. Without this override the Software renderer silently has no
    // Wayland cursor confinement at all -- see melonprime-aim-input.md and
    // issue #526.
    bool setWaylandPointerLockForMelonPrime(bool enabled) override;
    [[nodiscard]] bool isWaylandPointerLockActiveForMelonPrime() const override;
#endif

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupScreenLayout() override;
#ifdef MELONPRIME_DS
    void requestLatestFrameUpdate();
    void finishLatestFramePaint();
#endif

    QMutex bufferLock;
    bool hasBuffers;
    void* topBuffer;
    void* bottomBuffer;
    int bufferWidth = 256;
    int bufferHeight = 192;

    QImage screen[2];
    QTransform screenTrans[kMaxScreenTransforms];
#ifdef MELONPRIME_DS
    // Single-slot latest-frame mailbox. The emulation thread only posts a Qt
    // update when no earlier request is awaiting paint; newer frames replace
    // the published buffer pointers and set dirty for one follow-up paint.
    std::atomic_bool latestFrameUpdatePosted{false};
    std::atomic_bool latestFrameDirty{false};
#endif
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    std::unique_ptr<MelonPrime::WaylandPointerLock> waylandPointerLock;
#endif
};

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
class ScreenPanelDX12 final : public ScreenPanel
{
public:
    explicit ScreenPanelDX12(QWidget* parent);
    ~ScreenPanelDX12() override;

    bool initDX12();
    void drawScreen() override;
#ifdef MELONPRIME_CUSTOM_HUD
    void setHudEditModeActive(bool active) override;
    void setHudLivePreviewActive(bool active) override;
#endif

    // Quiesce all DX12 panels belonging to one EmuInstance before its
    // outgoing DX12 renderer is destroyed.
    static void PrepareForInstanceRendererTransition(EmuInstance* instance);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupScreenLayout() override;
    void prepareForRendererTransition();
    void requestNativeSurfaceVisible(bool visible);
    void reportRuntimeFailure(const char* reason);

    struct DX12State;
    std::unique_ptr<DX12State> dx12;
};
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
namespace melonDS
{
class VulkanRenderer;
}

class ScreenPanelVulkan final : public ScreenPanel
{
public:
    explicit ScreenPanelVulkan(QWidget* parent);
    ~ScreenPanelVulkan() override;

    bool initVulkan();
    void drawScreen() override;
    void beginModalPausePresentation() override;
    void endModalPausePresentation() override;
    void beginVulkanLowLatencyFrame(
        int reflexMode,
        bool antiLag2Enabled,
        bool normalSpeed,
        melonDS::u64 targetFrameIntervalNs) override;
    void markVulkanReflexInputSample() override;
    void markVulkanReflexSimulationStart() override;
    void markVulkanReflexSimulationEnd() override;
    void finishVulkanLowLatencyFrame() override;
#ifdef MELONPRIME_CUSTOM_HUD
    void setHudEditModeActive(bool active) override;
    void setHudLivePreviewActive(bool active) override;
#endif

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    // Same contract as ScreenPanelNative/ScreenPanelGL: without these the base
    // implementation always returns false and the Vulkan renderer has no
    // Wayland cursor confinement at all (Native Wayland has no QCursor warp or
    // X11 grab fallback to fall through to).
    bool setWaylandPointerLockForMelonPrime(bool enabled) override;
    [[nodiscard]] bool isWaylandPointerLockActiveForMelonPrime() const override;
#endif

    // Quiesce all Vulkan panels belonging to one EmuInstance before
    // its outgoing Vulkan renderer is destroyed.
    static void PrepareForInstanceRendererTransition(EmuInstance* instance);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool event(QEvent* event) override;

private:
    // The per-frame body of drawScreen(). Kept separate so the platform frame
    // boundary (macOS: an autorelease pool for MoltenVK's temporaries) can wrap
    // it without indenting the whole function.
    void drawScreenFrame();

    // Presentation stall watchdog, called from every exit of drawScreenFrame()
    // so a presenter that came up and then stopped is distinguishable in a log
    // from one that is deliberately idle. noteFrameIdle() covers the skips the
    // panel is supposed to perform (no ROM, paused, modal dialog);
    // noteFrameStalled() names a reason and logs it once after
    // kPresentationStallFrames consecutive frames. Emulation thread.
    void noteFrameIdle();
    void noteFrameStalled(const char* reason);
    void noteFramePresented();

    // Composes one emulated frame. Driven from VulkanRenderer's VBlank hook,
    // on the emulation thread, because that is the only point where this
    // frame's structured 2D metadata and this frame's 3D image coexist.
    void composeFrameAtVBlank();
    static void ComposeInstanceFrameAtVBlank(EmuInstance* instance);
    void installVulkanComposeHook(melonDS::VulkanRenderer* renderer);
    void prepareForRendererTransition(bool detachRendererObserver = true);
    bool initVulkanPresenter();
    void reportVulkanRuntimeFailure(const char* reason);
    void setupScreenLayout() override;

    // GUI-thread-only refresh of the platform presentation surface behind the
    // panel. setupScreenLayout() cannot do this: initVulkanPresenter() calls it
    // from the emulation thread, and macOS CoreAnimation state must only be
    // touched from the GUI thread.
    void refreshNativeSurfaceGuiThread();
    void setNativeSurfaceVisibleGuiThread(bool visible);
#if defined(__linux__)
    // A short lifecycle state/lease handshake surrounds only the decision to
    // enter a presentation frame. The Vulkan frame itself does not hold a
    // native-lifecycle lock.
    bool beginLinuxPresentationFrame();
    void finishLinuxPresentationFrame();
    void serviceLinuxSurfaceRetire();

    // Emulation thread. Binds the presenter only to the GUI-published native
    // snapshot whose generation is still current. Wayland WId reuse is not an
    // identity check.
    bool prepareLinuxPresentationSurface();
    void retireLinuxPresentationSurface(const char* reason);
    // Called only after the panel has been unpublished from the emulation
    // thread. This is the exclusive-owner teardown path for the native
    // presenter, before the QWidget child is hidden/deleted.
    void retireLinuxPresenterForPanelDestruction();
#endif
    // Callable from the emulation thread; posts to the GUI thread only when the
    // requested state actually changes.
    void requestNativeSurfaceVisible(bool visible);
    void releaseNativeSurface();
    [[nodiscard]] bool nativeSurfaceReady() const;

    // Stacks the OSD message bitmaps into one premultiplied strip so the whole
    // OSD costs a single upload and a single draw. False when there is nothing
    // to show. Emulation thread.
    bool buildOsdStrip(QSize& outSize);
#ifdef MELONPRIME_CUSTOM_HUD
    // Renders the Custom HUD into Overlay[0] and reports its dirty rect.
    // False when the HUD is not visible this frame, in which case the stale
    // overlay texture must not be drawn. Emulation thread.
    bool renderHudOverlay(EmuThread* emuThread, QImage* bottomScreen, QRect& outDirty);
#endif

    struct VulkanState;
    std::unique_ptr<VulkanState> vulkan;

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    // Deliberately outside VulkanState: OS pointer capture is panel input
    // state, not a Vulkan GPU resource, and must survive renderer transitions.
    std::unique_ptr<MelonPrime::WaylandPointerLock> waylandPointerLock;
#endif
};
#endif


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
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    bool setWaylandPointerLockForMelonPrime(bool enabled) override;
    [[nodiscard]] bool isWaylandPointerLockActiveForMelonPrime() const override;
#endif
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
    int screenTextureWidth = 256;
    int screenTextureHeight = 192;
    GLuint screenShaderProgram;
    GLint screenShaderTransformULoc, screenShaderScreenSizeULoc;

    QMutex screenSettingsLock;
    WindowInfo windowInfo;
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    std::unique_ptr<MelonPrime::WaylandPointerLock> waylandPointerLock;
#endif

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
