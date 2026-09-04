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
#ifdef MELONPRIME_DS
#include "MelonPrimeWheelEvent.h"
#include "MelonPrimeDirectAimIngress.h"
#include "MelonPrimeScreenCursorPolicy.h"
#include "MelonPrimeScreenInputPerf.h"
#include "MelonPrimeTopScreenTouch.h"
#include "MelonPrimeNativePaintPerf.h"
#endif
#include "MelonPrimePresentationSnapshot.h"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudScreenVisualTypes.h"
#endif // MELONPRIME_CUSTOM_HUD

class MainWindow;
class EmuInstance;
class EmuThread;
#ifdef MELONPRIME_CUSTOM_HUD
class MelonPrimeHudConfigOnScreenEdit;
#endif

#ifdef MELONPRIME_DS
namespace MelonPrime {
class MelonPrimeCore;
struct MelonPrimeUiSnapshot;
struct RendererTransitionSample;

// Native presentation visibility is an identity, not a boolean latch. A
// renderer/backend transition must hide the retained surface until a complete
// frame from the new epoch has actually been presented.
struct NativeVisibilityState {
    std::uint64_t Epoch = 0;
    std::uint64_t LastAcceptedSerial = 0;
    bool FirstCompleteFrameVisible = false;

    void Reset() noexcept
    {
        Epoch = 0;
        LastAcceptedSerial = 0;
        FirstCompleteFrameVisible = false;
    }

    void Accept(std::uint64_t epoch, std::uint64_t serial) noexcept
    {
        Epoch = epoch;
        LastAcceptedSerial = serial;
        FirstCompleteFrameVisible = true;
    }

    // A CPU frame published by the plain software renderer -- the
    // "3D.ForceSoftwareOutsideMatch" path -- is a complete picture, so it makes
    // the native surface legitimately visible. It carries no renderer frame
    // identity, so the epoch/serial fields keep the last native values instead
    // of being reset to zero.
    void AcceptWithoutIdentity() noexcept
    {
        FirstCompleteFrameVisible = true;
    }
};

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
        melonDS::u64 targetFrameIntervalNs,
        melonDS::u64 logicalFrameId)
    {
        (void)reflexMode;
        (void)antiLag2Enabled;
        (void)normalSpeed;
        (void)targetFrameIntervalNs;
        (void)logicalFrameId;
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

#if defined(MELONPRIME_CUSTOM_HUD) || defined(MELONPRIME_DS)
    std::optional<QRect> getTopScreenWidgetRect() const;
#endif

    void getAimMouseDelta(std::int32_t& outDx, std::int32_t& outDy);
    void resetAimMouseDelta();

    void reloadNoRomSplashLocalization();
    void refreshTopScreenTouchSetting();
    void refreshStylusCursorSettings();
    // Event-driven keyboard equivalent of starting a panel touch. Traditional
    // Stylus Mode publishes the pointer's DS coordinate; direct-aim mode starts
    // relative capture instead. The matching release always tears it down.
    void primeStylusTouchHotkeyAtCursor(int qtKey);
    void releaseStylusTouchHotkeyCapture(int qtKey);
    void containAimCursorIfNeeded();
    void syncMelonPrimeThreadBridge();
    // Explicit settings-dialog save wins over any older debounced hotkey save.
    void cancelMelonPrimeDeferredConfigSave();

    // Narrow accessors for MelonPrimeScreenCursorPolicy (avoid friend coupling).
    [[nodiscard]] bool isClosingForMelonPrime() const noexcept { return closing; }
    [[nodiscard]] bool isActiveVisibleWindowForMelonPrime() const;
    // Input surfaces are deliberately primary-window-owned. Secondary
    // presentation panels never publish or clear the shared input snapshot.
    [[nodiscard]] bool isMelonPrimeInputSurfaceAuthority() const noexcept;
    [[nodiscard]] MelonPrime::MelonPrimeCore* melonPrimeCoreForPolicy() const;
    [[nodiscard]] QRect aimContainmentLocalRectForPolicy() const;
    [[nodiscard]] QPoint aimContainmentCenterGlobalForPolicy() const;
    [[nodiscard]] bool shouldConfineCursorToBottomScreenForPolicy() const;
    // An absolute pen/injected-pointer capture still requests aim ownership
    // exactly like a relative one; only the confinement differs, because its
    // coordinate signal must survive.
    [[nodiscard]] MelonPrime::ScreenCursorPolicy::AimConfinement
    aimConfinementForPolicy() const noexcept
    {
        using MelonPrime::ScreenCursorPolicy::AimConfinement;
        return m_directAim.Active() ? AimConfinement::AimAreaBounds
                                    : AimConfinement::CenterPin;
    }
    // Stylus-mode match cursor options. Both derive from one reconciled edge
    // (stylus mode, focused, out of cursor mode) and never imply a capture
    // request: the pointer stays free so stylus aiming keeps working.
    [[nodiscard]] bool isStylusCursorHiddenForPolicy() const noexcept
    {
        return m_stylusMatchCursorActive && stylusHideCursorInGameEnabled;
    }
    [[nodiscard]] bool shouldConfineCursorToTopScreenForPolicy() const noexcept
    {
        return m_stylusMatchCursorActive && stylusConfineCursorToTopScreenEnabled;
    }
    // The pointer is pinned at the centre for as long as no click is held, so
    // a drag can only ever start from there. Takes precedence over the
    // top-screen confinement, which applies to the held drag itself.
    [[nodiscard]] bool isStylusCursorPinnedAtCenterForPolicy() const noexcept
    {
        return m_stylusMatchCursorActive && stylusHoldCursorAtCenterEnabled
            && !m_stylusClickHeld;
    }
    [[nodiscard]] QPoint stylusCursorCenterLocalForPolicy() const;
    [[nodiscard]] std::optional<QRect> getTopScreenWidgetRectForPolicy() const;
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
#endif

public slots:
    // Publishes the aim-capture request (which is what gives this instance the
    // relative input device) and reconciles the cursor for it.
    void requestAimCapture();
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
#ifdef MELONPRIME_DS
    bool topScreenTouchEnabled = false;
    int topScreenTouchTransform = -1;
    // SCR-PERF-001: the raw Stylus Mode setting, kept rather than discarded as
    // a local, so a high-rate Qt mouse event can decide in one bool load
    // whether it needs a runtime snapshot at all. Refreshed on the same cold
    // path as the derived flags below.
    bool stylusModeEnabled = false;
    bool stylusHideCursorInGameEnabled = false;
    bool stylusConfineCursorToTopScreenEnabled = false;
    bool stylusHoldCursorAtCenterEnabled = false;
    bool stylusDirectAimWhileTouchingEnabled = false;
    // Tablet direct aim is opt-in so the existing Raw Mouse path remains the
    // steady-state fast path for users who do not need pen input.
    bool stylusDirectAimAllowTabletInputEnabled = false;
    MelonPrime::PhysicalWheelStepAccumulator wheelSteps;
    // Cached OR of the match-scoped cursor options, so the per-pass reconcile
    // short-circuits on one predictable bool when they are all off.
    bool stylusMatchCursorOptionsEnabled = false;
    // Developer-only GUI input counters; the release implementation is a
    // constexpr no-op and remains outside the renderer-specific panels.
    MelonPrime::ScreenInputPerf m_screenInputPerf;
#endif

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
#ifdef MELONPRIME_DS
    // SCR-PERF-002: the top-screen touch inverse is layout state, resolved in
    // setupScreenLayout() rather than rebuilt on every hover, move and drag
    // event. Fixed length, no heap.
    MelonPrime::TopScreenTouchTransform
        topScreenTouchTransforms[kMaxScreenTransforms];
#endif

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
    void initializeHudScreenIntegration();
    void updateHudScreenLayoutCache();
    void handleHudMouseWheel(QWheelEvent* event);
    [[nodiscard]] bool handleHudMousePress(QMouseEvent* event);
    [[nodiscard]] bool handleHudMouseRelease(QMouseEvent* event);
    [[nodiscard]] bool handleHudMouseMove(QMouseEvent* event);
    void repositionHudEditPanel(bool resizeEvent);

    QImage Overlay[2];       // [0]=Top HUD, [1]=software radar color-key scratch
    QFont overlayFont;
    MelonPrimeHudConfigOnScreenEdit* m_hudEditPanel = nullptr;
    // GUI-owned latch set on the cold editor lifecycle path. It lets the
    // high-rate mouse handler reject ordinary moves without consulting the
    // HUD config/runtime bridge; the helper retains its own edit-mode guard.
    bool m_hudEditInputActive = false;
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
    bool getTouchCoords(int& x, int& y, bool clamp);

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
    void loadMelonPrimeStylusCursorConfig();
    void reconcileStylusMatchCursor(bool hasState,
                                    const MelonPrime::MelonPrimeUiSnapshot& ui);
    // Parks the pointer at the drag origin while no click is held, so every
    // drag starts centred with its full range available.
    void holdStylusCursorAtCenterIfNotClicking(const QPoint& localPos);
    void beginStylusDirectAimCapture();
    void endStylusDirectAimCapture();
    [[nodiscard]] QPoint stylusCursorCenterLocal() const;
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
    bool m_stylusMatchCursorActive = false;
    // Mouse button accepted as the current touch contact. In a Stylus Mode
    // match this follows HK_MetroidStylusTouch instead of being hard-wired to
    // the left button.
    Qt::MouseButton m_touchMouseButton = Qt::NoButton;
    Qt::MouseButton m_stylusDirectAimMouseButton = Qt::NoButton;
#if defined(__APPLE__)
    // GUI-thread recovery mailbox: normal mouse movement queries global Qt
    // button state only while a mapped press is still potentially held.
    uint8_t m_mouseRecoveryArmedMask = 0;
#endif
    // Tracks the held click itself rather than `touching`: a press whose touch
    // never registers must still free the pointer, or the pin would trap it.
    bool m_stylusClickHeld = false;
    bool m_stylusDirectAimCaptureHeld = false;
    // GUI-thread owned. The embedded ingress is inert while the option is off;
    // its Windows filter is allocated/installed only for a tablet-enabled
    // direct-aim capture.
    MelonPrime::DirectAimIngress m_directAim;
    bool m_hasLastClipFocusedState = false;
    // EmuThread requests a GUI-thread cursor/state reconciliation. The atomic
    // coalesces repeated per-frame requests without touching QWidget off-thread.
    std::atomic_bool m_melonPrimeGuiRefreshQueued{false};
    std::atomic<uint64_t> m_melonPrimeGuiRevisionSeen{0};
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
    MelonPrime::NativePaintPerf m_nativePaintPerf;
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
    bool event(QEvent* event) override;

private:
    void setupScreenLayout() override;
    void handleDX12SurfaceHostLifecycleGuiThread(
        QEvent::Type eventType, bool aboutToDestroy);
    void publishDX12SurfaceSnapshotGuiThread();
    void prepareForRendererTransition(
        MelonPrime::RendererTransitionSample* sample = nullptr);
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
        melonDS::u64 targetFrameIntervalNs,
        melonDS::u64 logicalFrameId) override;
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
    void noteFramePresented(melonDS::u64 epoch, melonDS::u64 serial);
    // Same bookkeeping for a frame that carries no renderer frame identity:
    // the plain software renderer's CPU output, presented through this panel
    // while "3D.ForceSoftwareOutsideMatch" holds the match window open.
    void noteFramePresentedWithoutIdentity();
    void clearPresentationStall();

    // Quiesces renderer-owned work before the outgoing renderer is destroyed.
    // The transition caller supplies the GUI/emulation barrier that keeps this
    // panel alive while the cold registry snapshot is consumed.
    void prepareForRendererTransition(
        MelonPrime::RendererTransitionSample* sample = nullptr);
    void invalidateScreenRetention();
    bool initVulkanPresenter();
    void reportVulkanRuntimeFailure(const char* reason);
    void setupScreenLayout() override;

    // GUI-thread-only refresh of the platform presentation surface behind the
    // panel. setupScreenLayout() cannot do this: initVulkanPresenter() calls it
    // from the emulation thread, and macOS CoreAnimation state must only be
    // touched from the GUI thread.
    void refreshNativeSurfaceGuiThread();
    void publishNativeSurfaceSnapshotGuiThread();
    void handleNativeSurfaceHostLifecycleGuiThread(QEvent::Type eventType, bool aboutToDestroy);
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
    bool renderHudOverlay(
        EmuThread* emuThread,
        QImage* bottomScreen,
        int logicalWidth,
        int logicalHeight,
        QRect& outDirty);
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
    void initializeHudOpenGL();
    void deinitializeHudOpenGL();
    GLuint overlayTextures[2];  // GL_TEXTURE_2D per screen (top/bottom), resized to match hi-res HUD buffer
    int overlayTexW = 0, overlayTexH = 0; // currently allocated texture dimensions
    GLuint btmOverlayShader;
    GLint btmOverlayScreenSizeULoc, btmOverlayOpacityULoc, btmOverlaySrcCenterULoc, btmOverlaySrcRadiusULoc;
    GLuint btmOverlayVertexArray, btmOverlayVertexBuffer;

    // The radar quad is stable between layout/config/resize/hunter edges.
    // Keep the edge signatures with the GL owner so the presentation loop
    // does not rebuild vertices or rewrite uniforms on every draw.
    struct HudRadarGlCache {
        uint32_t configEpoch = ~0u;
        uint64_t layoutGeneration = 0;
        int surfaceWidth = 0;
        int surfaceHeight = 0;
        float surfaceScale = 0.0f;
        float screenWidth = 0.0f;
        float screenHeight = 0.0f;
        float hudScale = 0.0f;
        float hudOriginX = 0.0f;
        float hudOriginY = 0.0f;
        float anchorDsX = 0.0f;
        float anchorDsY = 0.0f;
        int dstX = 0;
        int dstY = 0;
        int dstSize = 0;
        float topMatrix[6] = {};
        float opacity = 0.0f;
        int srcRadius = 0;
        uint8_t hunterId = 0xFFu;
        bool geometryValid = false;
        bool screenSizeValid = false;
        bool opacityValid = false;
        bool sourceValid = false;
    } m_hudRadarGl;
#endif

    GLuint logoTexture;

    void osdRenderItem(OSDItem* item) override;
    void osdDeleteItem(OSDItem* item) override;
};

#endif // SCREEN_H
