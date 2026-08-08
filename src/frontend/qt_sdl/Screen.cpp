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

#include <string.h>

#include <array>
#include <optional>
#include <utility>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <mutex>

#include <QPaintEvent>
#include <QPainter>
#include <QCursor>
#include <QGuiApplication>
#include <QLabel>
#include <QMetaObject>
#include <QThread>

#include <QDateTime>
#include <cstdlib>

#include "OpenGLSupport.h"
#include "duckstation/gl/context.h"

#include "main.h"
#include "EmuInstance.h"

#include "NDS.h"
#include "GPU.h"
#include "GPU3D_Soft.h"
#include "GPU3D_OpenGL.h"
#include "Platform.h"
#include "Config.h"

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
#include "GPU_DX12.h"
#include "MelonPrimeDX12FeatureCheck.h"
#include "MelonPrimeDX12SurfacePresenter.h"
#include <QPointer>
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
#include "GPU_Vulkan.h"
#include "GPU3D_Vulkan.h"
#include "MelonPrimeVulkanFilterMode.h"
#include "MelonPrimeVulkanFrameQueue.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanOutput.h"
#include "MelonPrimeVulkanSurfacePresenter.h"
#if defined(__APPLE__) // scatter-budget-exempt: macOS Vulkan presentation layer, not input dispatch
#include "MelonPrimeVulkanSurfaceMacOS.h"
#endif
#if defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
#include "MelonPrimeVulkanSurfaceHostLinux.h"
#include <QPointer>
#endif
#endif

#include "main_shaders.h"
#include "OSD_shaders.h"
#include "font.h"
#include "version.h"

// MelonPrimeDS Integration
#ifdef MELONPRIME_DS
#include "MelonPrime.h"
#include "MelonPrimeLocalization.h"
#include "MelonPrimePlatformInput.h"
#include "MelonPrimeScreenCursorPolicy.h"
#include "MelonPrimeDef.h"
#include "MelonPrimeWheelEvent.h"
#include "MelonPrimeInstanceDiagnostics.h"
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
#include "MelonPrimeWaylandPointerLock.h" // MELONPRIME_WAYLAND_POINTER_LOCK_V1
#endif
#include "MelonPrimePerfProbe.h"
#include "MelonPrimeHudPropSchema.inc"

#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeConstants.h"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeHudConfigOnScreenEdit.h"
#include "InputConfig/InputConfigDialog.h"
#include <QFontDatabase>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif
#endif // MELONPRIME_DS

using namespace melonDS;

#if !defined(_WIN32) && !defined(__APPLE__)
// Qt < 6.5 uses QPlatformNativeInterface for X11 and Wayland.
// Qt 6.5+ uses the public QNativeInterface API on BSD and X11.
// Only the Linux Wayland surface path still needs the private QPA header.
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0) || \
    (defined(__linux__) && defined(WAYLAND_ENABLED))
#include <qpa/qplatformnativeinterface.h>
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
using namespace QNativeInterface;
#endif
#endif


const u32 kOSDMargin = 6;
const int kLogoWidth = 192;

#ifdef MELONPRIME_DS
#include "MelonPrimeHudScreenCppHelpers.inc"

void ScreenPanel::getAimMouseDelta(std::int32_t& outDx, std::int32_t& outDy)
{
    if (auto* core = melonPrimeCore())
        core->ThreadBridge().getAimMouseDelta(outDx, outDy);
    else
        outDx = outDy = 0;
}

void ScreenPanel::resetAimMouseDelta()
{
    if (auto* core = melonPrimeCore())
        core->ThreadBridge().ResetPanelAimDeltaFromGui();
#if !defined(_WIN32)
    aimLastGlobalValid.store(false, std::memory_order_release);
#endif
}

#if defined(__linux__)
void ScreenPanel::addAimMouseDeltaForMelonPrime(
    std::int32_t dx, std::int32_t dy) noexcept
{
    if (auto* core = melonPrimeCore())
        core->ThreadBridge().AddPanelAimDeltaFromGui(dx, dy);
}
#endif

// MELONPRIME_PHASE5_CONFIG_USAGE_V1
void ScreenPanel::processMelonPrimePersistRequests()
{
    if (closing || !emuInstance || !mainWindow)
        return;

    // Only the primary panel owns persistence for this EmuInstance. Secondary
    // windows must not race to consume the single-consumer mailbox.
    if (emuInstance->getMainWindow() != mainWindow)
        return;

    auto* core = melonPrimeCore();
    if (!core)
        return;

    MelonPrime::MelonPrimePersistRequest request;
    if (!core->ThreadBridge().TakePersistRequestForGui(request))
        return;
    if (request.type !=
            MelonPrime::MelonPrimePersistRequest::Type::AimSensitivity
        || request.generation <= m_melonPrimeLastPersistGeneration)
        return;

    m_melonPrimeLastPersistGeneration = request.generation;
    emuInstance->getLocalConfig().SetInt(
        MelonPrime::CfgKey::AimSens,
        std::max(1, request.value));
    scheduleMelonPrimeConfigSave();
}

void ScreenPanel::scheduleMelonPrimeConfigSave()
{
    m_melonPrimeConfigSavePending = true;
    m_melonPrimeConfigSaveTimer.start(750);
}

void ScreenPanel::flushMelonPrimeConfigSave()
{
    if (!m_melonPrimeConfigSavePending)
        return;

    m_melonPrimeConfigSaveTimer.stop();
    m_melonPrimeConfigSavePending = false;
    MelonPrime::InstanceDiagnostics::CheckGuiThread(
        emuInstance,
        "Config::Save(AimSensitivity debounce)");
    Config::Save();
}

void ScreenPanel::cancelMelonPrimeDeferredConfigSave()
{
    m_melonPrimeConfigSaveTimer.stop();
    m_melonPrimeConfigSavePending = false;
}

void ScreenPanel::syncMelonPrimeThreadBridge()
{
    auto* core = melonPrimeCore();
    if (!core)
        return;
    auto& bridge = core->ThreadBridge();
    bridge.SetPanelAvailableFromGui(!closing && isVisible());
    const QPoint center = mapToGlobal(rect().center());
    bridge.PublishCenterFromGui(center.x(), center.y());
    bridge.PublishWindowHandleFromGui(static_cast<uintptr_t>(winId()));
    processMelonPrimePersistRequests();

    const uint32_t requests = bridge.TakeGuiRequestsFromGui();
    const uint32_t cursorRequests =
        MelonPrime::MelonPrimeThreadBridge::GuiRequestReconcileCursor
        | MelonPrime::MelonPrimeThreadBridge::GuiRequestShowCursor
        | MelonPrime::MelonPrimeThreadBridge::GuiRequestHideCursor
        | MelonPrime::MelonPrimeThreadBridge::GuiRequestRefreshCapture;
    const bool cursorVisible = bridge.CursorVisibleDesiredForGui();

    // MELONPRIME_CURSOR_AUTHORITATIVE_STATE_V1
    // Show and hide may be requested between the same two GUI passes. Never
    // resolve that race by bit priority: apply the latest desired state.
    if (requests & cursorRequests) {
        if (cursorVisible) {
            setCursor(Qt::ArrowCursor);
            MelonPrime::ScreenCursorPolicy::Unclip(*this);
        } else {
            MelonPrime::ScreenCursorPolicy::ClipCenter1px(*this);
        }
    }
#if !defined(_WIN32)
    if (!cursorVisible
        && (requests & MelonPrime::MelonPrimeThreadBridge::GuiRequestRecenter)) {
        resetAimMouseDelta();
        MelonPrime::PlatformInput_WarpCursor(center.x(), center.y());
    }
#endif
}

void ScreenPanel::wheelEvent(QWheelEvent* event)
{
    const int steps = MelonPrime::PhysicalWheelSteps(*event);
    if (steps != 0) {
        if (auto* core = melonPrimeCore())
            core->ThreadBridge().AddWheelFromGui(steps);
        if (emuInstance)
            emuInstance->onMouseWheel(steps);
    }
#include "MelonPrimeHudScreenCppMouseWheel.inc"
    event->accept();
}

void ScreenPanel::refreshClipForGameStateChange()
{
    if (closing || !qApp || qApp->closingDown())
        return;

    // MELONPRIME_CURSOR_GUI_THREAD_DISPATCH_V2
    // drawScreen() is driven by EmuThread. QWidget cursor state, focus/window
    // queries, layout changes and Win32 cursor presentation must be reconciled
    // on the QObject's GUI thread. Coalesce to at most one queued callback.
    if (QThread::currentThread() != thread()) {
        bool expected = false;
        if (m_melonPrimeGuiRefreshQueued.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            const bool queued = QMetaObject::invokeMethod(
                this,
                [this]() {
                    m_melonPrimeGuiRefreshQueued.store(
                        false, std::memory_order_release);
                    refreshClipForGameStateChange();
                },
                Qt::QueuedConnection);
            if (!queued) {
                m_melonPrimeGuiRefreshQueued.store(
                    false, std::memory_order_release);
            }
        }
        return;
    }

    syncMelonPrimeThreadBridge();
    auto* core = melonPrimeCore();
    const bool hasState = (core != nullptr);
    const auto ui = hasState ? core->ThreadBridge().ReadForGui()
                             : MelonPrime::MelonPrimeUiSnapshot{};
    const bool isInGame = hasState && ui.inGame;
    const bool isFocused = hasState && ui.focused;
    const bool wantsInGameTopScreenOnly =
        hasState
        && ui.romDetected
        && isInGame
        && inGameTopScreenOnly;

    const bool clipStateUnchanged =
        m_hasLastClipInGameState == hasState
        && (!hasState || m_lastClipInGameState == isInGame)
        && m_hasLastClipFocusedState == hasState
        && (!hasState || m_lastClipFocusedState == isFocused);
    const bool topScreenOnlyStateUnchanged =
        m_hasLastInGameTopScreenOnlyOverride
        && m_lastInGameTopScreenOnlyOverride == wantsInGameTopScreenOnly;

    if (clipStateUnchanged && topScreenOnlyStateUnchanged)
        return;

    m_hasLastClipInGameState = hasState;
    m_lastClipInGameState = isInGame;
    m_hasLastClipFocusedState = hasState;
    m_lastClipFocusedState = isFocused;
    m_hasLastInGameTopScreenOnlyOverride = true;
    m_lastInGameTopScreenOnlyOverride = wantsInGameTopScreenOnly;

    if (!topScreenOnlyStateUnchanged)
        setupScreenLayout();

#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    if (!clipStateUnchanged)
        updateClipIfNeeded();
#endif
}

#ifdef MELONPRIME_CUSTOM_HUD
void ScreenPanel::setHudEditModeActive(bool active)
{
    if (!active || closing || !qApp || qApp->closingDown())
        return;

    // CustomHud_EnterEditMode asks for cursor mode through the thread bridge,
    // but that command is only consumed by the emulation thread's input pass --
    // and the settings dialog has emulation paused for the whole editor
    // session, so it is never read. Aim capture would therefore stay armed:
    // the focus-in that follows the dialog hiding re-clips the pointer to the
    // panel centre and blanks it, leaving the editor unusable. Release it here,
    // on the GUI thread that owns cursor state.
    MelonPrime::ScreenCursorPolicy::Unclip(*this);
}
#endif

void ScreenPanel::applyInGameTopScreenOnlyOverride(int& layout, int& sizing) const
{
    if (closing || !qApp || qApp->closingDown())
        return;

    auto* core = melonPrimeCore();
    if (!core) return;
    const auto ui = core->ThreadBridge().ReadForGui();
    if (!ui.romDetected) return;
    if (!ui.inGame) return;
    if (!inGameTopScreenOnly) return;

    layout = screenLayout_Natural;
    sizing = screenSizing_TopOnly;
}

bool ScreenPanel::shouldConfineCursorToBottomScreen() const
{
    if (closing || !qApp || qApp->closingDown())
        return false;

    auto* core = melonPrimeCore();
    if (!core) return false;
    const auto ui = core->ThreadBridge().ReadForGui();
    if (!ui.romDetected) return false;
    if (getClipWanted()) return false;
    if (ui.inGame) return false;

    auto* emu = emuInstance;
    if (!emu) return false;
    return emu->getLocalConfig().GetBool(MP_HUD_PROP_KEY_ClipCursorToBottomScreenWhenNotInGame);
}

std::optional<QRect> ScreenPanel::getScreenWidgetRect(int wantedScreenKind) const
{
    QRectF bounds;
    bool found = false;
    const QRectF screenRect(0.0, 0.0, 256.0, 192.0);

    for (int i = 0; i < numScreens; i++) {
        if (screenKind[i] != wantedScreenKind) continue;
        const float* mtx = screenMatrix[i];
        QTransform transform(mtx[0], mtx[1], 0.0,
                             mtx[2], mtx[3], 0.0,
                             mtx[4], mtx[5], 1.0);
        QRectF mapped = transform.mapRect(screenRect);
        bounds = found ? bounds.united(mapped) : mapped;
        found = true;
    }

    if (!found) return std::nullopt;

    QRect rect(static_cast<int>(std::floor(bounds.left())),
               static_cast<int>(std::floor(bounds.top())),
               static_cast<int>(std::ceil(bounds.right())) - static_cast<int>(std::floor(bounds.left())),
               static_cast<int>(std::ceil(bounds.bottom())) - static_cast<int>(std::floor(bounds.top())));
    rect = rect.intersected(this->rect());
    if (rect.isEmpty()) return std::nullopt;
    return rect;
}

std::optional<QRect> ScreenPanel::getBottomScreenWidgetRect() const
{
    return getScreenWidgetRect(1);
}

#ifdef MELONPRIME_CUSTOM_HUD
std::optional<QRect> ScreenPanel::getTopScreenWidgetRect() const
{
    return getScreenWidgetRect(0);
}
#endif

QRect ScreenPanel::aimContainmentLocalRect() const
{
    QRect unionRect;
    bool found = false;
    const QRectF screenRect(0.0, 0.0, 256.0, 192.0);

    for (int i = 0; i < numScreens; ++i) {
        const float* mtx = screenMatrix[i];
        QTransform transform(mtx[0], mtx[1], 0.0,
                             mtx[2], mtx[3], 0.0,
                             mtx[4], mtx[5], 1.0);
        const QRect r = transform.mapRect(screenRect).toAlignedRect().intersected(rect());
        if (r.isEmpty())
            continue;
        unionRect = found ? unionRect.united(r) : r;
        found = true;
    }

    return found ? unionRect : rect();
}

void ScreenPanel::containAimCursorIfNeeded()
{
    MelonPrime::ScreenCursorPolicy::ContainAimCursorIfNeeded(*this);
}

void ScreenPanel::clipCursorToBottomScreen() {
    MelonPrime::ScreenCursorPolicy::ConfineToBottomScreen(*this);
}

void ScreenPanel::clipCursorCenter1px() {
    MelonPrime::ScreenCursorPolicy::ClipCenter1px(*this);
}

void ScreenPanel::unclip() {
    MelonPrime::ScreenCursorPolicy::Unclip(*this);
}

void ScreenPanel::releaseCursorStateForClose()
{
    MelonPrime::ScreenCursorPolicy::ReleaseForClose(*this);
}

void ScreenPanel::beginClose()
{
    if (closing)
        return;
    processMelonPrimePersistRequests();
    flushMelonPrimeConfigSave();
    closing = true;
    if (auto* core = melonPrimeCore()) {
        core->ThreadBridge().SetFocusedFromGui(false);
        core->ThreadBridge().SetPanelAvailableFromGui(false);
        core->ThreadBridge().SetCaptureWantedFromGui(false);
    }
    releaseCursorStateForClose();
}

void ScreenPanel::updateClipIfNeeded() {
    MelonPrime::ScreenCursorPolicy::UpdateClipIfNeeded(*this);
}
#endif // MELONPRIME_DS

#ifdef MELONPRIME_DS
bool ScreenPanel::isActiveVisibleWindowForMelonPrime() const
{
    return isVisible() && window() && window()->isActiveWindow();
}

MelonPrime::MelonPrimeCore* ScreenPanel::melonPrimeCoreForPolicy() const
{
    return melonPrimeCore();
}

QRect ScreenPanel::aimContainmentLocalRectForPolicy() const
{
    return aimContainmentLocalRect();
}

QPoint ScreenPanel::aimContainmentCenterGlobalForPolicy() const
{
    return mapToGlobal(aimContainmentLocalRect().center());
}

bool ScreenPanel::shouldConfineCursorToBottomScreenForPolicy() const
{
    return shouldConfineCursorToBottomScreen();
}

void ScreenPanel::clipCursorToBottomScreenForPolicy()
{
    clipCursorToBottomScreen();
}

std::optional<QRect> ScreenPanel::getBottomScreenWidgetRectForPolicy() const
{
    return getBottomScreenWidgetRect();
}

void ScreenPanel::setClipWantedForMelonPrime(bool value)
{
    setClipWanted(value);
}

bool ScreenPanel::getClipWantedForMelonPrime() const
{
    return getClipWanted();
}
#endif // MELONPRIME_DS

ScreenPanel::ScreenPanel(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_AcceptTouchEvents);

    QWidget* w = parent;
    for (;;)
    {
        mainWindow = qobject_cast<MainWindow*>(w);
        if (mainWindow) break;
        w = w->parentWidget();
        if (!w) break;
    }

    emuInstance = mainWindow->getEmuInstance();

#ifdef MELONPRIME_DS
    m_melonPrimeConfigSaveTimer.setSingleShot(true);
    connect(
        &m_melonPrimeConfigSaveTimer,
        &QTimer::timeout,
        this,
        &ScreenPanel::flushMelonPrimeConfigSave);
#endif

    mouseHide = false;
    mouseHideDelay = 0;

    osdEnabled = false;
    osdID = 1;

#include "MelonPrimeHudScreenCppInit.inc"

    loadConfig();
    setFilter(mainWindow->getWindowConfig().GetBool("ScreenFilter"));

    splashLogo = QPixmap(":/melon-logo");

    strncpy(splashText[0].text, "File->Open ROM...", 256);
    splashText[0].id = 0x80000000;
    splashText[0].color = 0;
    splashText[0].rendered = false;
    splashText[0].rainbowstart = -1;

    strncpy(splashText[1].text, "to get started", 256);
    splashText[1].id = 0x80000001;
    splashText[1].color = 0;
    splashText[1].rendered = false;
    splashText[1].rainbowstart = -1;

#ifdef MELONPRIME_DS
    MelonPrime::UiText::ApplyNoRomSplashLocalization(splashText[0].text, splashText[1].text);
#endif

    std::string url = MELONDS_URL;
    int urlpos = url.find("://");
    urlpos = (urlpos == std::string::npos) ? 0 : urlpos + 3;
    strncpy(splashText[2].text, url.c_str() + urlpos, 256);
    splashText[2].id = 0x80000002;
    splashText[2].color = 0;
    splashText[2].rendered = false;
    splashText[2].rainbowstart = -1;
}

ScreenPanel::~ScreenPanel()
{
#ifdef MELONPRIME_DS
    if (!closing) {
        processMelonPrimePersistRequests();
        flushMelonPrimeConfigSave();
        closing = true;
        releaseCursorStateForClose();
    }
#endif
}

#ifdef MELONPRIME_DS
void ScreenPanel::reloadNoRomSplashLocalization()
{
    MelonPrime::UiText::ApplyNoRomSplashLocalization(splashText[0].text, splashText[1].text);
    osdMutex.lock();
    for (int i = 0; i < 2; ++i)
    {
        splashText[i].rendered = false;
        splashText[i].bitmap = QImage();
    }
    m_splashRendered = false;
    osdMutex.unlock();
    update();
}
#endif

void ScreenPanel::loadConfig()
{
    auto& cfg = mainWindow->getWindowConfig();

    screenRotation = cfg.GetInt("ScreenRotation");
    screenGap = cfg.GetInt("ScreenGap");
    screenLayout = cfg.GetInt("ScreenLayout");
    screenSwap = cfg.GetBool("ScreenSwap");
    screenSizing = cfg.GetInt("ScreenSizing");
    integerScaling = cfg.GetBool("IntegerScaling");
    screenAspectTop = cfg.GetInt("ScreenAspectTop");
    screenAspectBot = cfg.GetInt("ScreenAspectBot");
    inGameTopScreenOnly = emuInstance->getLocalConfig().GetBool(MP_HUD_PROP_KEY_InGameTopScreenOnly);
}

void ScreenPanel::setFilter(bool filter)
{
    this->filter = filter;
}

void ScreenPanel::setMouseHide(bool enable, int delay)
{
    mouseHide = enable;
    mouseHideDelay = delay;
}

void ScreenPanel::setupScreenLayout()
{
    int w = width();
    int h = height();

    int layoutType = screenLayout;
    int sizing = screenSizing;
    applyInGameTopScreenOnlyOverride(layoutType, sizing);
    if (sizing == screenSizing_Auto) sizing = autoScreenSizing;

    float aspectTop, aspectBot;

    for (auto ratio : aspectRatios)
    {
        if (ratio.id == screenAspectTop)
            aspectTop = ratio.ratio;
        if (ratio.id == screenAspectBot)
            aspectBot = ratio.ratio;
    }

    if (aspectTop == 0)
        aspectTop = ((float)w / h) / (4.f / 3.f);

    if (aspectBot == 0)
        aspectBot = ((float)w / h) / (4.f / 3.f);

    layout.Setup(w, h,
        static_cast<ScreenLayoutType>(layoutType),
        static_cast<ScreenRotation>(screenRotation),
        static_cast<ScreenSizing>(sizing),
        screenGap,
        integerScaling != 0,
        screenSwap != 0,
        aspectTop,
        aspectBot);

    numScreens = layout.GetScreenTransforms(screenMatrix[0], screenKind);

    calcSplashLayout();

#include "MelonPrimeHudScreenCppLayout.inc"

#ifdef MELONPRIME_DS
    // Notify layout change
    if (!closing && qApp && !qApp->closingDown()) {
        syncMelonPrimeThreadBridge();
        if (auto* core = melonPrimeCore()) {
            core->NotifyLayoutChange();
        }
    }
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    updateClipIfNeeded();
#endif
#endif
}

QSize ScreenPanel::screenGetMinSize(int factor = 1)
{
    bool isHori = (screenRotation == screenRot_90Deg
        || screenRotation == screenRot_270Deg);
    int gap = screenGap * factor;

    int w = 256 * factor;
    int h = 192 * factor;

    if (screenSizing == screenSizing_TopOnly
        || screenSizing == screenSizing_BotOnly)
    {
        return QSize(w, h);
    }

    if (screenLayout == screenLayout_Natural)
    {
        if (isHori)
            return QSize(h + gap + h, w);
        else
            return QSize(w, h + gap + h);
    }
    else if (screenLayout == screenLayout_Vertical)
    {
        if (isHori)
            return QSize(h, w + gap + w);
        else
            return QSize(w, h + gap + h);
    }
    else if (screenLayout == screenLayout_Horizontal)
    {
        if (isHori)
            return QSize(h + gap + h, w);
        else
            return QSize(w + gap + w, h);
    }
    else // hybrid
    {
        if (isHori)
            return QSize(h + gap + h, 3 * w + (int)ceil((4 * gap) / 3.0));
        else
            return QSize(3 * w + (int)ceil((4 * gap) / 3.0), h + gap + h);
    }
}

void ScreenPanel::onScreenLayoutChanged()
{
    loadConfig();

    setMinimumSize(screenGetMinSize());
    setupScreenLayout();
}

void ScreenPanel::onAutoScreenSizingChanged(int sizing)
{
    autoScreenSizing = sizing;
    if (screenSizing != screenSizing_Auto) return;

    setupScreenLayout();
}

void ScreenPanel::resizeEvent(QResizeEvent* event)
{
    setupScreenLayout();
#ifdef MELONPRIME_DS
#if defined(_WIN32) || defined(__linux__)
    updateClipIfNeeded();
#endif
#endif
#include "MelonPrimeHudScreenCppEditPanelResize.inc"
    QWidget::resizeEvent(event);
}

void ScreenPanel::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    auto* const emu = emuInstance;

#ifdef MELONPRIME_DS
    auto* const thr = emu->getEmuThread();
    auto* const core = thr->GetMelonPrimeCore();
#endif

    if (Q_UNLIKELY(!emu->emuIsActive()))
    {
        touching = false;
        return;
    }

#include "MelonPrimeHudScreenCppMousePress.inc"

#ifdef MELONPRIME_DS
#if defined(__APPLE__)
    emu->syncMouseHotkeysFromQtButtons(QGuiApplication::mouseButtons());
#endif
    // Click sets focus
    if (core) core->ThreadBridge().SetFocusedFromGui(true);

    emu->onMousePress(event);
#endif

    if (event->button() != Qt::LeftButton)
        return;

#ifdef MELONPRIME_DS
    // Mouse aim mode logic
    const auto ui = core ? core->ThreadBridge().ReadForGui()
                         : MelonPrime::MelonPrimeUiSnapshot{};
    if (core && !ui.stylusMode && ui.inGame)
    {
        // If not in cursor mode (aim mode), treat click as returning to aim (clip)
        if (!ui.cursorMode)
        {
            clipCursorCenter1px();
            return;
        }
        // If isCursorMode == true, proceed to standard touch processing
    }
#endif

    const QPoint p = event->pos();
    int x = p.x();
    int y = p.y();

    if (layout.GetTouchCoords(x, y, false))
    {
        touching = true;
        emu->touchScreen(x, y);
    }

#ifdef MELONPRIME_DS
    // If not in cursor mode, re-clip
    if (core && !ui.stylusMode && !ui.cursorMode)
    {
        clipCursorCenter1px();
    }
#endif
}

void ScreenPanel::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();

    auto* const emu = emuInstance;

#ifdef MELONPRIME_DS
    emu->onMouseRelease(event);
#endif

    if (Q_UNLIKELY(!emu->emuIsActive()))
    {
        touching = false;
        return;
    }

#include "MelonPrimeHudScreenCppMouseRelease.inc"

    if (event->button() != Qt::LeftButton)
        return;

    if (!touching)
        return;

    touching = false;
    emu->releaseScreen();
}

void ScreenPanel::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    auto* const emu = emuInstance;

    if (Q_UNLIKELY(!emu->emuIsActive()))
        return;

#if defined(MELONPRIME_DS) && defined(__APPLE__)
    emu->syncMouseHotkeysFromQtButtons(QGuiApplication::mouseButtons());
#endif

#include "MelonPrimeHudScreenCppMouseMove.inc"

#if defined(MELONPRIME_DS) && (defined(__linux__) || defined(__APPLE__))
    auto* const thread = emu->getEmuThread();
    auto* const core = thread ? thread->GetMelonPrimeCore() : nullptr;
    const auto ui = core ? core->ThreadBridge().ReadForGui()
                         : MelonPrime::MelonPrimeUiSnapshot{};
    // MELONPRIME_INPUT_DEBUG=1: 1 Hz gate/event diagnostics for the Qt aim path.
    static const bool s_aimDbg = getenv("MELONPRIME_INPUT_DEBUG") != nullptr;
    if (Q_UNLIKELY(s_aimDbg)) {
        static int s_events = 0, s_blocked = 0;
        static qint64 s_lastLog = 0;
        const bool gateOk = core
            && ui.focused
            && !ui.stylusMode && !ui.cursorMode && ui.inGame;
        gateOk ? ++s_events : ++s_blocked;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - s_lastLog >= 1000) {
            fprintf(stderr,
                "[MelonPrime] linux panel: 1s moveEvents gateOk=%d blocked=%d"
                " (core=%d focused=%d stylus=%d cursor=%d inGame=%d rawActive=%d)\n",
                s_events, s_blocked,
                core ? 1 : 0,
                core ? (ui.focused ? 1 : 0) : -1,
                core ? (ui.stylusMode ? 1 : 0) : -1,
                core ? (ui.cursorMode ? 1 : 0) : -1,
                core ? (ui.inGame ? 1 : 0) : -1,
                core ? (ui.rawAimActive ? 1 : 0) : -1);
            s_events = s_blocked = 0;
            s_lastLog = now;
        }
    }
    if (core
        && ui.focused
        && !ui.stylusMode
        && !ui.cursorMode
        && ui.inGame)
    {
#if defined(__APPLE__)
        if (ui.rawAimActive) {
            aimLastGlobalValid.store(false, std::memory_order_release);
        } else {
#if QT_VERSION_MAJOR == 6
            const QPoint global = event->globalPosition().toPoint();
#else
            const QPoint global = event->globalPos();
#endif
            if (aimLastGlobalValid.load(std::memory_order_acquire)) {
                const int dx = global.x() - aimLastGlobal.x();
                const int dy = global.y() - aimLastGlobal.y();
                if ((dx | dy) != 0)
                    core->ThreadBridge().AddPanelAimDeltaFromGui(dx, dy);
            }
            aimLastGlobal = global;
            aimLastGlobalValid.store(true, std::memory_order_release);
        }
        if (getClipWanted())
            containAimCursorIfNeeded();
        return;
#elif defined(__linux__)
        // A compositor-native pointer lock supplies the PanelDelta accumulator
        // directly. Ignore absolute QMouseEvent positions while it is active,
        // otherwise the same physical motion could be counted twice.
        if (isWaylandPointerLockActiveForMelonPrime()) {
            aimLastGlobalValid.store(false, std::memory_order_release);
            return;
        }

        const QPoint center = mapToGlobal(rect().center());
#if QT_VERSION_MAJOR == 6
        const QPoint global = event->globalPosition().toPoint();
        const QPoint local = event->position().toPoint();
#else
        const QPoint global = event->globalPos();
        const QPoint local = event->pos();
#endif
        // MELONPRIME_LINUX_MOUSE_INPUT_HARDENING_V2:
        // The previous 96px threshold matched half the height of a 256x192
        // single-screen panel, allowing the pointer to leave before recentering.
        const QRect safeRect = rect().adjusted(16, 16, -16, -16);
        const bool strayed = !safeRect.contains(local);

        if (ui.rawAimActive) {
            // MELONPRIME_LINUX_RAW_GRAB_RELEASE_FIX_V1
            // XI_RawMotion is collected on a separate X connection. Keeping
            // QWidget's active X11 pointer grab can starve that collector, so raw
            // mode must release the fallback-only grab before suppressing Qt deltas.
            aimLastGlobalValid.store(false, std::memory_order_release);
            if (MelonPrime::PlatformInput_IsXcb()
                && QWidget::mouseGrabber() == this)
            {
                releaseMouse();
            }
            if (strayed)
                MelonPrime::PlatformInput_WarpCursor(center.x(), center.y());
            return;
        }

        // Qt fallback (XWayland / sessions where raw motion never arrives):
        const auto warpToCenter = [&]() {
            aimLastGlobalValid.store(false, std::memory_order_release);
            MelonPrime::PlatformInput_WarpCursor(center.x(), center.y());
        };
        // previous-position differencing. Unlike the old center-delta +
        // warp-per-event scheme, consecutive positions are pure pointer
        // motion, so VirtualBox host re-syncs and our own containment warps
        // cannot be double-counted (the baseline is re-seeded around warps).
        if (aimLastGlobalValid.load(std::memory_order_acquire)) {
            const int dx = global.x() - aimLastGlobal.x();
            const int dy = global.y() - aimLastGlobal.y();
            if ((dx | dy) != 0) {
                core->ThreadBridge().AddPanelAimDeltaFromGui(dx, dy);
            }
        }
        aimLastGlobal = global;
        aimLastGlobalValid.store(true, std::memory_order_release);
        if (strayed)
            warpToCenter();
        return;
#endif
    }
#endif

    if (!touching)
        return;

    const QPoint p = event->pos();
    int x = p.x();
    int y = p.y();

    if (layout.GetTouchCoords(x, y, true))
    {
        emu->touchScreen(x, y);
    }
}


void ScreenPanel::tabletEvent(QTabletEvent* event)
{
    event->accept();
    if (!emuInstance->emuIsActive()) { touching = false; return; }

    switch (event->type())
    {
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    {
#if QT_VERSION_MAJOR == 6
        const QPointF pos = event->position();
        int x = pos.x();
        int y = pos.y();
#else
        int x = event->x();
        int y = event->y();
#endif

        if (layout.GetTouchCoords(x, y, event->type() == QEvent::TabletMove))
        {
            touching = true;
            emuInstance->touchScreen(x, y);
        }
    }
    break;
    case QEvent::TabletRelease:
        if (touching)
        {
            emuInstance->releaseScreen();
            touching = false;
        }
        break;
    default:
        break;
    }
}

void ScreenPanel::touchEvent(QTouchEvent* event)
{
#if QT_VERSION_MAJOR == 6
    if (event->device()->type() == QInputDevice::DeviceType::TouchPad)
        return;
#endif

    event->accept();
    if (!emuInstance->emuIsActive()) { touching = false; return; }

    switch (event->type())
    {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
#if QT_VERSION_MAJOR == 6
        if (event->points().length() > 0)
        {
            QPointF lastPosition = event->points().first().lastPosition();
#else
        if (event->touchPoints().length() > 0)
        {
            QPointF lastPosition = event->touchPoints().first().lastPos();
#endif
            int x = (int)lastPosition.x();
            int y = (int)lastPosition.y();

            if (layout.GetTouchCoords(x, y, event->type() == QEvent::TouchUpdate))
            {
                touching = true;
                emuInstance->touchScreen(x, y);
            }
        }
        break;
    case QEvent::TouchEnd:
        if (touching)
        {
            emuInstance->releaseScreen();
            touching = false;
        }
        break;
    default:
        break;
        }
    }

bool ScreenPanel::event(QEvent * event)
{
#ifdef MELONPRIME_DS
    // MELONPRIME_CURSOR_CAPTURE_STATE_V3
    // Suspend on every transition that can transfer input to another window.
    // Reacquire on the next event-loop turn after activation/unblocking so Qt
    // and MainWindow have finished updating their focus state.
    switch (event->type())
    {
    case QEvent::WindowBlocked:
    case QEvent::WindowDeactivate:
    case QEvent::Hide:
    case QEvent::ParentChange:
        if (getenv("MELONPRIME_WAYLAND_LOCK_DEBUG"))
            fprintf(stderr, "[MelonPrime] ScreenPanel::event: Suspend-triggering type=%d\n", (int)event->type());
        MelonPrime::ScreenCursorPolicy::Suspend(*this);
        break;
    case QEvent::WindowActivate:
    case QEvent::WindowUnblocked:
    case QEvent::Show:
    case QEvent::WindowStateChange:
        if (getenv("MELONPRIME_WAYLAND_LOCK_DEBUG"))
            fprintf(stderr, "[MelonPrime] ScreenPanel::event: updateClipIfNeeded-triggering type=%d\n", (int)event->type());
        QTimer::singleShot(0, this, [this]() {
            if (!closing && qApp && !qApp->closingDown())
                updateClipIfNeeded();
        });
        break;
    case QEvent::Close:
        beginClose();
        break;
    default:
        break;
    }
#endif

    if (event->type() == QEvent::TouchBegin
        || event->type() == QEvent::TouchEnd
        || event->type() == QEvent::TouchUpdate)
    {
        touchEvent((QTouchEvent*)event);
        return true;
    }
    else if (event->type() == QEvent::FocusIn)
        mainWindow->onFocusIn();
    else if (event->type() == QEvent::FocusOut)
        mainWindow->onFocusOut();

    return QWidget::event(event);
}

int ScreenPanel::osdFindBreakPoint(const char* text, int i)
{
    for (int j = i; j >= 0; j--)
    {
        if (text[j] == ' ')
            return j;
    }

    return i;
}

void ScreenPanel::osdLayoutText(const char* text, int* width, int* height, int* breaks)
{
    int w = 0;
    int h = 14;
    int totalw = 0;
    int maxw = ((QWidget*)this)->width() - (kOSDMargin * 2);
    int lastbreak = -1;
    int numbrk = 0;
    u16* ptr;

    memset(breaks, 0, sizeof(int) * 64);

    for (int i = 0; text[i] != '\0'; )
    {
        int glyphsize;
        if (text[i] == ' ')
        {
            glyphsize = 6;
        }
        else
        {
            u32 ch = text[i];
            if (ch < 0x10 || ch > 0x7E) ch = 0x7F;

            ptr = &::font[(ch - 0x10) << 4];
            glyphsize = ptr[0];
            if (!glyphsize) glyphsize = 6;
            else            glyphsize += 2;
        }

        w += glyphsize;
        if (w > maxw)
        {
            if (text[i] == ' ')
            {
                if (numbrk >= 64) break;
                breaks[numbrk++] = i;
                i++;
            }
            else
            {
                int brk = osdFindBreakPoint(text, i);
                if (brk != lastbreak) i = brk;

                if (numbrk >= 64) break;
                breaks[numbrk++] = i;

                lastbreak = brk;
            }

            w = 0;
            h += 14;
        }
        else
            i++;

        if (w > totalw) totalw = w;
    }

    *width = totalw;
    *height = h;
}

unsigned int ScreenPanel::osdRainbowColor(int inc)
{
    if (inc < 100) return 0xFFFF9B9B + (inc << 8);
    else if (inc < 200) return 0xFFFFFF9B - ((inc - 100) << 16);
    else if (inc < 300) return 0xFF9BFF9B + (inc - 200);
    else if (inc < 400) return 0xFF9BFFFF - ((inc - 300) << 8);
    else if (inc < 500) return 0xFF9B9BFF + ((inc - 400) << 16);
    else                return 0xFFFF9BFF - (inc - 500);
}

void ScreenPanel::osdRenderItem(OSDItem * item)
{
    int w, h;
    int breaks[64];

    char* text = item->text;
    u32 color = item->color;

#ifdef MELONPRIME_DS
    {
        QImage qtBitmap;
        int rainbowEnd = item->rainbowstart;
        const int maxw = ((QWidget*)this)->width() - (kOSDMargin * 2);
        if (MelonPrime::UiText::TryRenderNoRomSplashOsdItem(
                item->id, text, color, item->rainbowstart, rainbowEnd, maxw, &qtBitmap))
        {
            item->bitmap = std::move(qtBitmap);
            item->rainbowend = rainbowEnd;
            return;
        }
    }
#endif

    bool rainbow = (color == 0);
    u32 rainbowinc;
    if (item->rainbowstart == -1)
    {
        u32 ticks = (u32)QDateTime::currentMSecsSinceEpoch();
        rainbowinc = ((text[0] * 17) + (ticks * 13)) % 600;
    }
    else
        rainbowinc = (u32)item->rainbowstart;

    color |= 0xFF000000;
    const u32 shadow = 0xE0000000;

    osdLayoutText(text, &w, &h, breaks);

    item->bitmap = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    u32* bitmap = (u32*)item->bitmap.bits();
    memset(bitmap, 0, w * h * sizeof(u32));

    int x = 0, y = 1;
    int curline = 0;
    u16* ptr;

    for (int i = 0; text[i] != '\0'; )
    {
        int glyphsize;
        if (text[i] == ' ')
        {
            x += 6;
        }
        else
        {
            u32 ch = text[i];
            if (ch < 0x10 || ch > 0x7E) ch = 0x7F;

            ptr = &::font[(ch - 0x10) << 4];
            int glyphsize = ptr[0];
            if (!glyphsize) x += 6;
            else
            {
                x++;

                if (rainbow)
                {
                    color = osdRainbowColor(rainbowinc);
                    rainbowinc = (rainbowinc + 30) % 600;
                }

                for (int cy = 0; cy < 12; cy++)
                {
                    u16 val = ptr[4 + cy];

                    for (int cx = 0; cx < glyphsize; cx++)
                    {
                        if (val & (1 << cx))
                            bitmap[((y + cy) * w) + x + cx] = color;
                    }
                }

                x += glyphsize;
                x++;
            }
        }

        i++;
        if (breaks[curline] && i >= breaks[curline])
        {
            i = breaks[curline++];
            if (text[i] == ' ') i++;

            x = 0;
            y += 14;
        }
    }

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            u32 val;

            val = bitmap[(y * w) + x];
            if ((val >> 24) == 0xFF) continue;

            if (x > 0)   val = bitmap[(y * w) + x - 1];
            if (x < w - 1) val |= bitmap[(y * w) + x + 1];
            if (y > 0)
            {
                if (x > 0)   val |= bitmap[((y - 1) * w) + x - 1];
                val |= bitmap[((y - 1) * w) + x];
                if (x < w - 1) val |= bitmap[((y - 1) * w) + x + 1];
            }
            if (y < h - 1)
            {
                if (x > 0)   val |= bitmap[((y + 1) * w) + x - 1];
                val |= bitmap[((y + 1) * w) + x];
                if (x < w - 1) val |= bitmap[((y + 1) * w) + x + 1];
            }

            if ((val >> 24) == 0xFF)
                bitmap[(y * w) + x] = shadow;
        }
    }

    item->rainbowend = (int)rainbowinc;
}

void ScreenPanel::osdDeleteItem(OSDItem * item)
{
}

void ScreenPanel::osdSetEnabled(bool enabled)
{
    osdMutex.lock();
    osdEnabled = enabled;
    osdMutex.unlock();
}

void ScreenPanel::osdAddMessage(unsigned int color, const char* text)
{
    if (!osdEnabled) return;

    osdMutex.lock();

    OSDItem item;

    item.id = (osdID++) & 0x7FFFFFFF;
    item.timestamp = QDateTime::currentMSecsSinceEpoch();
    strncpy(item.text, text, 255); item.text[255] = '\0';
    item.color = color;
    item.rendered = false;
    item.rainbowstart = -1;

    osdItems.push_back(item);

    osdMutex.unlock();
}

void ScreenPanel::osdUpdate()
{
#ifdef MELONPRIME_DS
    // OPT-OSD1: During normal gameplay, 99%+ of frames have zero OSD items and
    // splash texts are already rendered. Skip the mutex lock + syscall
    // (QDateTime::currentMSecsSinceEpoch) entirely in this case.
    if (m_splashRendered && osdItems.empty()) return;
#endif

    osdMutex.lock();

    qint64 tick_now = QDateTime::currentMSecsSinceEpoch();
    qint64 tick_min = tick_now - 2500;

    for (auto it = osdItems.begin(); it != osdItems.end(); )
    {
        OSDItem& item = *it;

        if ((!osdEnabled) || (item.timestamp < tick_min))
        {
            osdDeleteItem(&item);
            it = osdItems.erase(it);
            continue;
        }

        if (!item.rendered)
        {
            osdRenderItem(&item);
            item.rendered = true;
        }

        it++;
    }

    int rainbowinc = -1;
    bool needrecalc = false;

    for (int i = 0; i < 3; i++)
    {
        if (!splashText[i].rendered)
        {
            splashText[i].rainbowstart = rainbowinc;
            osdRenderItem(&splashText[i]);
            splashText[i].rendered = true;
            rainbowinc = splashText[i].rainbowend;
            needrecalc = true;
        }
    }

#ifdef MELONPRIME_DS
    // OPT-OSD1: Once all 3 splash texts are rendered, enable early-exit fast path.
    if (!m_splashRendered && !needrecalc)
        m_splashRendered = true;
#endif

    osdMutex.unlock();

    if (needrecalc)
        calcSplashLayout();
}

void ScreenPanel::calcSplashLayout()
{
    if (!splashText[0].rendered)
        return;

    osdMutex.lock();

    int w = width();
    int h = height();

    int xlogo = (w - kLogoWidth) / 2;
    int ylogo = (h - kLogoWidth) / 2;

    int totalwidth = splashText[0].bitmap.width() + 6 + splashText[1].bitmap.width();
#ifdef MELONPRIME_DS
    // Localized splash uses a proportional CJK font that can fit on one row even
    // when the upstream bitmap-font English pair stacks; keep line 1 / line 2 layout.
    const bool splashStackVertically =
        (totalwidth >= w) || MelonPrime::UiText::UsesLocalizedSplashLayout();
#else
    const bool splashStackVertically = (totalwidth >= w);
#endif
    if (splashStackVertically)
    {
        splashPos[0].setX((width() - splashText[0].bitmap.width()) / 2);
        splashPos[1].setX((width() - splashText[1].bitmap.width()) / 2);

        int basey = ylogo / 2;
        splashPos[0].setY(basey - splashText[0].bitmap.height() - 1);
        splashPos[1].setY(basey + 1);
    }
    else
    {
        splashPos[0].setX((w - totalwidth) / 2);
        splashPos[1].setX(splashPos[0].x() + splashText[0].bitmap.width() + 6);

        int basey = (ylogo - splashText[0].bitmap.height()) / 2;
        splashPos[0].setY(basey);
        splashPos[1].setY(basey);
    }

    splashPos[2].setX((w - splashText[2].bitmap.width()) / 2);
    splashPos[2].setY(ylogo + kLogoWidth + ((ylogo - splashText[2].bitmap.height()) / 2));

    splashPos[3].setX(xlogo);
    splashPos[3].setY(ylogo);

    osdMutex.unlock();
}



#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
namespace {

// Resolves the native wl_display/wl_surface handles MelonPrime's Wayland
// pointer-lock path needs, given the QWindow that owns the surface.
// ScreenPanelGL, ScreenPanelNative and ScreenPanelVulkan all call this with
// their *top-level* window's handle (never a panel's own
// Qt::WA_NativeWindow subsurface):
// locking a child subsurface directly caused KWin to fire WindowDeactivate
// on the main window in windowed mode, which our own Suspend() path read as
// focus loss and immediately tore the lock back down (see issue #526).
std::optional<std::pair<void*, void*>> ResolveMelonPrimeWaylandHandles(QWindow* handle)
{
    if (!handle || QGuiApplication::platformName() != QStringLiteral("wayland"))
        return std::nullopt;

    QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
    if (!pni)
        return std::nullopt;

    void* display = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const QWaylandApplication* wl = qApp->nativeInterface<QWaylandApplication>();
    if (!wl)
        return std::nullopt;
    display = wl->display();
#else
    display = pni->nativeResourceForWindow("display", handle);
#endif

    void* const surface = pni->nativeResourceForWindow("surface", handle);
    if (!display || !surface)
        return std::nullopt;

    return std::make_pair(display, surface);
}

} // namespace
#endif

ScreenPanelNative::ScreenPanelNative(QWidget * parent) : ScreenPanel(parent)
{
    hasBuffers = false;

    screen[0] = QImage(256, 192, QImage::Format_RGB32);
    screen[1] = QImage(256, 192, QImage::Format_RGB32);

    screenTrans[0].reset();
    screenTrans[1].reset();
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    waylandPointerLock = std::make_unique<MelonPrime::WaylandPointerLock>(
        [this](std::int32_t dx, std::int32_t dy) {
            addAimMouseDeltaForMelonPrime(dx, dy);
        });
#endif
}

ScreenPanelNative::~ScreenPanelNative()
{
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    if (waylandPointerLock)
        waylandPointerLock->setLocked(nullptr, nullptr, false);
#endif
}

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
bool ScreenPanelNative::setWaylandPointerLockForMelonPrime(bool enabled)
{
    if (!waylandPointerLock)
        return false;

    if (!enabled)
        return waylandPointerLock->setLocked(nullptr, nullptr, false);

    // Not a Qt::WA_NativeWindow: lock the top-level window's surface instead
    // of our own (we have none).
    QWindow* const topLevelHandle = window() ? window()->windowHandle() : nullptr;
    const auto handles = ResolveMelonPrimeWaylandHandles(topLevelHandle);
    if (!handles.has_value())
        return false;

    // Hint the panel's own center, expressed in the locked (top-level)
    // surface's local coordinates, so the compositor recenters the cursor
    // away from any edge whenever this lock later releases.
    const QPoint hint = window() ? mapTo(window(), rect().center()) : rect().center();
    return waylandPointerLock->setLocked(
        handles->first, handles->second, true, hint.x(), hint.y());
}

bool ScreenPanelNative::isWaylandPointerLockActiveForMelonPrime() const
{
    return waylandPointerLock && waylandPointerLock->isLockActive();
}
#endif

void ScreenPanelNative::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();

    for (int i = 0; i < numScreens; i++)
    {
        float* mtx = screenMatrix[i];
        screenTrans[i].setMatrix(mtx[0], mtx[1], 0.f,
            mtx[2], mtx[3], 0.f,
            mtx[4], mtx[5], 1.f);
    }
}

#ifdef MELONPRIME_DS
void ScreenPanelNative::requestLatestFrameUpdate()
{
    latestFrameDirty.store(true, std::memory_order_release);
    if (!latestFrameUpdatePosted.exchange(true, std::memory_order_acq_rel))
    {
        QMetaObject::invokeMethod(
            this,
            [this]() { update(); },
            Qt::QueuedConnection);
    }
}

void ScreenPanelNative::finishLatestFramePaint()
{
    latestFrameUpdatePosted.store(false, std::memory_order_release);

    // If the producer published another frame while this paint was running,
    // reserve exactly one follow-up update. A concurrent producer can win the
    // exchange instead; either way there is still only one queued request.
    if (latestFrameDirty.exchange(false, std::memory_order_acq_rel)
        && !latestFrameUpdatePosted.exchange(true, std::memory_order_acq_rel))
    {
        QMetaObject::invokeMethod(
            this,
            [this]() { update(); },
            Qt::QueuedConnection);
    }
}

void ScreenPanelNative::invalidateRendererOutput()
{
    bufferLock.lock();
    hasBuffers = false;
    topBuffer = nullptr;
    bottomBuffer = nullptr;
    bufferWidth = 256;
    bufferHeight = 192;
    bufferLock.unlock();

    requestLatestFrameUpdate();
}
#endif

void ScreenPanelNative::drawScreen()
{
    refreshClipForGameStateChange();

    auto emuThread = emuInstance->getEmuThread();
    if (!emuThread->emuIsActive())
    {
#ifdef MELONPRIME_DS
        invalidateRendererOutput();
#else
        bufferLock.lock();
        hasBuffers = false;
        bufferLock.unlock();
#endif
        return;
    }

    auto nds = emuInstance->getNDS();
    assert(nds != nullptr);

    const RendererOutput output = nds->GPU.GetRendererOutput();
    bufferLock.lock();
    hasBuffers = (output.Kind == RendererOutputKind::CpuBgra);
    topBuffer = hasBuffers ? output.Top : nullptr;
    bottomBuffer = hasBuffers ? output.Bottom : nullptr;
    bufferWidth = hasBuffers ? static_cast<int>(std::max(1u, output.Width)) : 256;
    bufferHeight = hasBuffers ? static_cast<int>(std::max(1u, output.Height)) : 192;
    bufferLock.unlock();
#ifdef MELONPRIME_DS
    requestLatestFrameUpdate();
#endif
}

void ScreenPanelNative::paintEvent(QPaintEvent * event)
{
#ifdef MELONPRIME_DS
    // Everything published before this point is represented by the buffer
    // pointers sampled below. Publications during paint request one follow-up.
    latestFrameDirty.store(false, std::memory_order_release);
#endif

    QPainter painter(this);

    painter.fillRect(event->rect(), QColor::fromRgb(0, 0, 0));

    auto emuThread = emuInstance->getEmuThread();

    if (emuThread->emuIsActive())
    {
        emuInstance->renderLock.lock();

        bufferLock.lock();
        if (hasBuffers)
        {
            const int sourceWidth = bufferWidth;
            const int sourceHeight = bufferHeight;
            if (screen[0].width() != sourceWidth || screen[0].height() != sourceHeight)
            {
                screen[0] = QImage(sourceWidth, sourceHeight, QImage::Format_RGB32);
                screen[1] = QImage(sourceWidth, sourceHeight, QImage::Format_RGB32);
            }
            const std::size_t bytes = static_cast<std::size_t>(sourceWidth)
                * static_cast<std::size_t>(sourceHeight) * sizeof(u32);
            memcpy(screen[0].scanLine(0), topBuffer, bytes);
            memcpy(screen[1].scanLine(0), bottomBuffer, bytes);
        }
        bufferLock.unlock();

        QRect screenrc(0, 0, 256, 192);

        for (int i = 0; i < numScreens; i++)
        {
            painter.setTransform(screenTrans[i]);
            painter.drawImage(screenrc, screen[screenKind[i]]);
        }

#define MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE (&screen[1])
#include "MelonPrimeHudScreenCppOverlayOfSoftware.inc"
#undef MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE

        emuInstance->renderLock.unlock();
    }

    osdUpdate();

    if (!emuThread->emuIsActive())
    {
        osdMutex.lock();

        painter.drawPixmap(QRect(splashPos[3], QSize(kLogoWidth, kLogoWidth)), splashLogo);

        for (int i = 0; i < 3; i++)
            painter.drawImage(splashPos[i], splashText[i].bitmap);

        osdMutex.unlock();
    }

    if (osdEnabled)
    {
        osdMutex.lock();

        u32 y = kOSDMargin;

        painter.resetTransform();

        for (auto it = osdItems.begin(); it != osdItems.end(); )
        {
            OSDItem& item = *it;

            painter.drawImage(kOSDMargin, y, item.bitmap);

            y += item.bitmap.height();
            it++;
        }

        osdMutex.unlock();
    }

#ifdef MELONPRIME_DS
    finishLatestFramePaint();
#endif
}


#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
namespace
{
class DX12SurfaceHost final : public QWidget
{
public:
    explicit DX12SurfaceHost(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NativeWindow, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_PaintOnScreen, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAutoFillBackground(false);
    }

    QPaintEngine* paintEngine() const override { return nullptr; }

protected:
    void paintEvent(QPaintEvent*) override {}
};
} // namespace

struct ScreenPanelDX12::DX12State
{
    MelonPrime::DX12SurfacePresenter presenter;
    QPointer<DX12SurfaceHost> surface;
    QMutex layoutLock;
    QTransform screenTransform[kMaxScreenTransforms];
    QMutex fallbackLock;
    QImage fallbackFrame;
    QImage logicalFrame;
    QImage physicalFrame;
    std::atomic_bool surfaceVisibleRequested{false};
    // Set by the GUI thread while the Custom HUD on-screen editor owns the
    // panel, read by the emulation thread's paused draw pass.
    std::atomic_bool hudEditLivePresentation{false};
    bool initialized = false;
    bool runtimeFailureReported = false;
};

ScreenPanelDX12::ScreenPanelDX12(QWidget* parent)
    : ScreenPanel(parent), dx12(std::make_unique<DX12State>())
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize());

    dx12->surface = new DX12SurfaceHost(this);
    dx12->surface->setGeometry(rect());
    dx12->surface->hide();
}

ScreenPanelDX12::~ScreenPanelDX12()
{
    if (dx12)
        dx12->presenter.Shutdown();
}

bool ScreenPanelDX12::initDX12()
{
    if (!dx12 || !dx12->surface)
        return false;
    const HWND window = reinterpret_cast<HWND>(dx12->surface->winId());
    dx12->initialized = dx12->presenter.Init(window);
    if (!dx12->initialized)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12 native presenter initialization failed reason=%s\n",
            dx12->presenter.LastError().c_str());
    }
    return dx12->initialized;
}

void ScreenPanelDX12::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    if (!dx12)
        return;

    QMutexLocker lock(&dx12->layoutLock);
    for (int index = 0; index < numScreens; ++index)
    {
        const float* matrix = screenMatrix[index];
        dx12->screenTransform[index].setMatrix(
            matrix[0], matrix[1], 0.0f,
            matrix[2], matrix[3], 0.0f,
            matrix[4], matrix[5], 1.0f);
    }
}

void ScreenPanelDX12::resizeEvent(QResizeEvent* event)
{
    if (dx12 && dx12->surface)
        dx12->surface->setGeometry(rect());
    ScreenPanel::resizeEvent(event);
}

void ScreenPanelDX12::requestNativeSurfaceVisible(bool visible)
{
    if (!dx12 || dx12->surfaceVisibleRequested.exchange(visible) == visible)
        return;

    QMetaObject::invokeMethod(
        this,
        [this, visible]() {
            if (!dx12 || !dx12->surface)
                return;
            if (visible)
            {
                dx12->surface->setGeometry(rect());
                dx12->surface->show();
                dx12->surface->raise();
            }
            else
            {
                dx12->surface->hide();
                update();
            }
        },
        Qt::QueuedConnection);
}

void ScreenPanelDX12::reportRuntimeFailure(const char* reason)
{
    if (!dx12 || dx12->runtimeFailureReported)
        return;
    dx12->runtimeFailureReported = true;
    MelonPrime::DX12FeatureCheck::ReportRuntimeFailure(
        reason ? reason : "DX12 native presentation failed");
    if (auto* emuThread = emuInstance->getEmuThread())
    {
        QMetaObject::invokeMethod(
            emuThread,
            [emuThread]() { emit emuThread->rendererRuntimeFallback(); },
            Qt::QueuedConnection);
    }
}

#ifdef MELONPRIME_CUSTOM_HUD
void ScreenPanelDX12::setHudEditModeActive(bool active)
{
    ScreenPanel::setHudEditModeActive(active);
    if (!dx12)
        return;

    dx12->hudEditLivePresentation.store(active, std::memory_order_relaxed);
}
#endif

void ScreenPanelDX12::drawScreen()
{
    refreshClipForGameStateChange();
    if (!dx12 || !dx12->initialized)
        return;

    auto* emuThread = emuInstance->getEmuThread();
    if (!emuThread->emuIsActive())
    {
        requestNativeSurfaceVisible(false);
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        return;
    }

    // Paused normally means "leave the swapchain showing the last presented
    // image": nothing new is emulated, and the native child keeps the frame up
    // even while a modal dialog is exposed.
    //
    // The Custom HUD on-screen editor is the exception. The settings dialog
    // pauses emulation before handing the panel to the editor, so this pass is
    // the only thing that can ever put the editor overlay on screen; without
    // this the DX12 panel just keeps presenting the frozen pre-pause frame and
    // the editor is invisible. Same contract as ScreenPanelVulkan; the
    // software/OpenGL panels gate on emuIsActive() and keep drawing anyway.
#ifdef MELONPRIME_CUSTOM_HUD
    const bool hudEditLivePresentation =
        dx12->hudEditLivePresentation.load(std::memory_order_relaxed);
#else
    constexpr bool hudEditLivePresentation = false;
#endif
    if (!emuThread->emuIsRunning() && !hudEditLivePresentation)
        return;

    auto* nds = emuInstance->getNDS();
    if (!nds)
        return;
    const RendererOutput output = nds->GPU.GetRendererOutput();
    if (output.Kind != RendererOutputKind::CpuBgra || !output.Top || !output.Bottom)
        return;

    const int logicalWidth = std::max(1, width());
    const int logicalHeight = std::max(1, height());
    if (dx12->logicalFrame.width() != logicalWidth
        || dx12->logicalFrame.height() != logicalHeight
        || dx12->logicalFrame.format() != QImage::Format_RGB32)
    {
        dx12->logicalFrame = QImage(logicalWidth, logicalHeight, QImage::Format_RGB32);
    }
    dx12->logicalFrame.fill(Qt::black);

    const int sourceWidth = static_cast<int>(std::max(1u, output.Width));
    const int sourceHeight = static_cast<int>(std::max(1u, output.Height));
    QImage screen[2] = {
        QImage(
            static_cast<uchar*>(output.Top),
            sourceWidth,
            sourceHeight,
            sourceWidth * static_cast<int>(sizeof(u32)),
            QImage::Format_RGB32),
        QImage(
            static_cast<uchar*>(output.Bottom),
            sourceWidth,
            sourceHeight,
            sourceWidth * static_cast<int>(sizeof(u32)),
            QImage::Format_RGB32),
    };

    QTransform transforms[kMaxScreenTransforms];
    {
        QMutexLocker lock(&dx12->layoutLock);
        for (int index = 0; index < numScreens; ++index)
            transforms[index] = dx12->screenTransform[index];
    }

    QPainter painter(&dx12->logicalFrame);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, filter);
    const QRect screenRect(0, 0, 256, 192);
    for (int index = 0; index < numScreens; ++index)
    {
        painter.setTransform(transforms[index]);
        painter.drawImage(screenRect, screen[screenKind[index]]);
    }

#define MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE (&screen[1])
#include "MelonPrimeHudScreenCppOverlayOfSoftware.inc"
#undef MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE

    osdUpdate();
    if (osdEnabled)
    {
        QMutexLocker osdLock(&osdMutex);
        int y = kOSDMargin;
        painter.resetTransform();
        for (const OSDItem& item : osdItems)
        {
            painter.drawImage(kOSDMargin, y, item.bitmap);
            y += item.bitmap.height();
        }
    }
    painter.end();

    const qreal dpr = devicePixelRatioF();
    const int physicalWidth = std::max(1, qRound(logicalWidth * dpr));
    const int physicalHeight = std::max(1, qRound(logicalHeight * dpr));
    const QImage* presentFrame = &dx12->logicalFrame;
    if (physicalWidth != logicalWidth || physicalHeight != logicalHeight)
    {
        dx12->physicalFrame = dx12->logicalFrame.scaled(
            physicalWidth,
            physicalHeight,
            Qt::IgnoreAspectRatio,
            filter ? Qt::SmoothTransformation : Qt::FastTransformation);
        presentFrame = &dx12->physicalFrame;
    }

    const bool vsync = emuInstance->getGlobalConfig().GetBool("Screen.VSync");
    if (!dx12->presenter.UploadFrame(
            presentFrame->constBits(),
            static_cast<std::uint32_t>(presentFrame->width()),
            static_cast<std::uint32_t>(presentFrame->height()),
            static_cast<std::size_t>(presentFrame->bytesPerLine()),
            vsync))
    {
        {
            QMutexLocker fallbackLock(&dx12->fallbackLock);
            dx12->fallbackFrame = dx12->logicalFrame.copy();
        }
        requestNativeSurfaceVisible(false);
        reportRuntimeFailure(dx12->presenter.LastError().c_str());
        return;
    }

    auto* renderer = dynamic_cast<melonDS::DX12Renderer*>(&nds->GPU.GetRenderer());
    if (renderer)
        renderer->BeginReflexPresent();
    const bool presented = dx12->presenter.Present(vsync);
    if (renderer)
        renderer->EndReflexPresent();

    if (!presented)
    {
        {
            QMutexLocker fallbackLock(&dx12->fallbackLock);
            dx12->fallbackFrame = dx12->logicalFrame.copy();
        }
        requestNativeSurfaceVisible(false);
        reportRuntimeFailure(dx12->presenter.LastError().c_str());
        return;
    }

    requestNativeSurfaceVisible(true);
}

void ScreenPanelDX12::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(event->rect(), Qt::black);

    auto* emuThread = emuInstance->getEmuThread();
    if (emuThread->emuIsActive())
    {
        QMutexLocker fallbackLock(&dx12->fallbackLock);
        if (!dx12->fallbackFrame.isNull())
            painter.drawImage(QPoint(0, 0), dx12->fallbackFrame);
        return;
    }

    osdUpdate();
    QMutexLocker osdLock(&osdMutex);
    painter.drawPixmap(QRect(splashPos[3], QSize(kLogoWidth, kLogoWidth)), splashLogo);
    for (int index = 0; index < 3; ++index)
        painter.drawImage(splashPos[index], splashText[index].bitmap);
}
#endif



#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
struct ScreenPanelVulkan::VulkanState
{
    MelonPrime::MelonPrimeVulkanFrameQueue frameQueue;
    MelonPrime::MelonPrimeVulkanFrameQueuePolicy framePolicy;
    MelonPrime::MelonPrimeVulkanOutput output;
    MelonPrime::MelonPrimeVulkanSurfacePresenter presenter;
    // Published by the VBlank compose pass, read by the presentation pass.
    // Both run on the emulation thread; the atomic only documents the handoff.
    std::atomic<bool> nativeMenuHeld{false};
    QMutex layoutLock;
    std::vector<MelonPrime::VulkanPresentRegion> regions;
    std::uint32_t surfaceWidth = 0;
    std::uint32_t surfaceHeight = 0;
    QImage overlayFrame;
    QLabel* modalPauseOverlay = nullptr;
    // Guards nativeWindow/nativeWindowGeneration: the GUI thread publishes the
    // platform presentation handle, the emulation thread builds swapchains from
    // it. presenterWindowGeneration stays emulation-thread-owned and records
    // which generation the live presenter was built against.
    QMutex nativeWindowLock;
    MelonPrime::VulkanNativeWindowInfo nativeWindow;
    std::uint64_t nativeWindowGeneration = 0;
    std::uint64_t presenterWindowGeneration = 0;
#if defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
    // Vulkan draws only here; the panel itself keeps Qt's backing store for its
    // software output. Owned as a Qt child, cleared in releaseNativeSurface().
    QPointer<MelonPrime::VulkanSurfaceHostLinux> linuxSurfaceHost;
#endif
    QMutex softwareBufferLock;
    QImage softwareScreen[2] = {
        QImage(256, 192, QImage::Format_RGB32),
        QImage(256, 192, QImage::Format_RGB32),
    };
    QTransform softwareScreenTransform[kMaxScreenTransforms];
    // Emulation-thread view of whether the Vulkan surface should be on screen,
    // so a show/hide is only posted to the GUI thread on a real transition.
    bool surfaceVisibleRequested = false;
    bool initialized = false;
    bool presenterInitialized = false;
    bool softwareMode = true;
    bool hasSoftwareBuffers = false;
    bool runtimeFailureReported = false;
    bool softwarePaintFailureReported = false;
    unsigned consecutiveFailures = 0;
    // Set when the panel hands presentation back to Qt, cleared by the paint
    // pass that acts on it, so transition debugging logs one line per handoff
    // instead of one per frame.
    std::atomic_bool logSoftwarePaintHandoff{false};
#ifdef MELONPRIME_CUSTOM_HUD
    // Set from the GUI thread when the Custom HUD on-screen editor takes over
    // the panel, read by the emulation thread's draw pass. The editor runs with
    // emulation paused, so the panel must keep composing and presenting.
    std::atomic_bool hudEditLivePresentation{false};
#endif
};

namespace
{

// Opt-in tracing for the Vulkan/software presentation handoff
// (MELONPRIME_VULKAN_TRANSITION_DEBUG=1). Only state transitions log, never
// per-frame work, and nothing is emitted unless the variable is set.
bool VulkanTransitionDebugEnabled()
{
    static const bool enabled = []() {
        const char* const value = std::getenv("MELONPRIME_VULKAN_TRANSITION_DEBUG");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

#if defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
// Resolves the Vulkan WSI handles of one *mapped* native widget. The caller
// passes the dedicated Vulkan child surface, never the panel or the top-level
// window: Qt paints into those, and Vulkan must not share a surface with it.
MelonPrime::VulkanNativeWindowInfo ResolveLinuxVulkanNativeWindow(QWidget* target)
{
    MelonPrime::VulkanNativeWindowInfo info{};
    if (target == nullptr)
        return info;

    const QString platformName = QGuiApplication::platformName();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (platformName == QStringLiteral("xcb"))
    {
        const QX11Application* x11 = qApp->nativeInterface<QX11Application>();
        info.type = MelonPrime::VulkanNativeWindowType::Xlib;
        info.display = x11 != nullptr ? x11->display() : nullptr;
        info.window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(target->winId()));
    }
#if defined(WAYLAND_ENABLED)
    else if (platformName == QStringLiteral("wayland"))
    {
        const QWaylandApplication* wayland = qApp->nativeInterface<QWaylandApplication>();
        QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
        info.type = MelonPrime::VulkanNativeWindowType::Wayland;
        info.display = wayland != nullptr ? wayland->display() : nullptr;
        info.window = pni != nullptr && target->windowHandle() != nullptr
            ? pni->nativeResourceForWindow("surface", target->windowHandle())
            : nullptr;
    }
#endif
#else
    QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
    if (platformName == QStringLiteral("xcb"))
    {
        info.type = MelonPrime::VulkanNativeWindowType::Xlib;
        info.display = pni != nullptr
            ? pni->nativeResourceForWindow("display", target->windowHandle())
            : nullptr;
        info.window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(target->winId()));
    }
    else if (platformName == QStringLiteral("wayland"))
    {
        info.type = MelonPrime::VulkanNativeWindowType::Wayland;
        info.display = pni != nullptr
            ? pni->nativeResourceForWindow("display", target->windowHandle())
            : nullptr;
        info.window = pni != nullptr
            ? pni->nativeResourceForWindow("surface", target->windowHandle())
            : nullptr;
    }
#endif

    // A handle pair that is not complete is not usable; report it as absent so
    // the panel retries instead of building a swapchain on half a surface.
    if (info.display == nullptr || info.window == nullptr)
        return MelonPrime::VulkanNativeWindowInfo{};
    return info;
}
#endif

std::mutex& VulkanPanelRegistryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::vector<ScreenPanelVulkan*>& VulkanPanelRegistry()
{
    static std::vector<ScreenPanelVulkan*> panels;
    return panels;
}

}

ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanel(parent), vulkan(std::make_unique<VulkanState>())
{
    // Constructed before the registry insert so a throwing allocation cannot
    // leave a half-built panel published in VulkanPanelRegistry().
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    waylandPointerLock = std::make_unique<MelonPrime::WaylandPointerLock>(
        [this](std::int32_t dx, std::int32_t dy) {
            addAimMouseDeltaForMelonPrime(dx, dy);
        });
#endif

    {
        std::lock_guard<std::mutex> lock(VulkanPanelRegistryMutex());
        VulkanPanelRegistry().push_back(this);
    }

    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
#if defined(_WIN32) // scatter-budget-exempt: platform Vulkan presentation surface, not input dispatch
    setAttribute(Qt::WA_PaintOnScreen, true);
#endif
    // WA_PaintOnScreen is set only where Vulkan presents straight to this
    // panel's own native window (Windows/HWND). macOS and Linux composite
    // Vulkan on a separate surface above the panel, and there the attribute is
    // actively harmful: it takes the panel off Qt's backing store, and this
    // panel needs QPainter for the splash screen, the software-rendered screens
    // (still reachable through 3D.ForceSoftwareOutsideMatch and while the
    // renderer is switching) and the OSD, all of which go through
    // paintEvent(). Qt's macOS backend has no on-screen paint
    // support at all, and native Wayland has none either -- keeping it set on
    // Linux is what left the last presented Vulkan frame stuck on screen when
    // the post-match recap switched back to the software renderer.
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize());

    // One composed frame per emulated frame, presented immediately. Stealing a
    // frame back off the present queue would re-enter one whose compositor
    // dispatch is still reading the shared structured planes.
    vulkan->framePolicy.MaxBacklogDepth = 1;
    vulkan->framePolicy.AllowStealPending = false;
    vulkan->framePolicy.AllowPreviousFrameReuse = true;
    vulkan->framePolicy.PreferOldestFrame = false;

#if defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
    // GUI thread (MainWindow::createScreenPanel). The child is created hidden
    // and stays unmapped until the first Vulkan frame is about to be presented.
    auto* surfaceHost = new MelonPrime::VulkanSurfaceHostLinux(this);
    surfaceHost->setGeometry(rect());
    surfaceHost->setNativeSurfaceChangedCallback(
        [this]() { refreshNativeSurfaceGuiThread(); });
    vulkan->linuxSurfaceHost = surfaceHost;
#endif
}

ScreenPanelVulkan::~ScreenPanelVulkan()
{
    // Ahead of the `if (!vulkan) return` below: releasing the OS pointer
    // capture must not depend on whether this panel still owns GPU state.
    // Idempotent, so a prior Suspend() having already unlocked is fine.
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    if (waylandPointerLock)
        waylandPointerLock->setLocked(nullptr, nullptr, false);
#endif

    {
        std::lock_guard<std::mutex> lock(VulkanPanelRegistryMutex());
        auto& panels = VulkanPanelRegistry();
        panels.erase(
            std::remove(panels.begin(), panels.end(), this),
            panels.end());
    }

    if (!vulkan)
        return;

    prepareForRendererTransition();
    // Ordered after the complete Vulkan quiesce.
    releaseNativeSurface();
}

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
bool ScreenPanelVulkan::setWaylandPointerLockForMelonPrime(bool enabled)
{
    if (!waylandPointerLock)
        return false;

    if (!enabled)
        return waylandPointerLock->setLocked(nullptr, nullptr, false);

    // Vulkan presents onto this panel's own Qt::WA_NativeWindow subsurface,
    // but pointer constraints must target the top-level window's surface --
    // locking the child subsurface made KWin fire WindowDeactivate on the main
    // window, which Suspend() reads as focus loss and tears the lock straight
    // back down (see issue #526). The presentation surface handles in
    // VulkanState are for display only and are never reused here.
    QWindow* const topLevelHandle = window() ? window()->windowHandle() : nullptr;
    const auto handles = ResolveMelonPrimeWaylandHandles(topLevelHandle);
    if (!handles.has_value())
        return false;

    // Hint the panel's own center, expressed in the locked (top-level)
    // surface's local coordinates, so the compositor recenters the cursor
    // away from any edge whenever this lock later releases.
    const QPoint hint = window() ? mapTo(window(), rect().center()) : rect().center();
    return waylandPointerLock->setLocked(
        handles->first, handles->second, true, hint.x(), hint.y());
}

bool ScreenPanelVulkan::isWaylandPointerLockActiveForMelonPrime() const
{
    return waylandPointerLock && waylandPointerLock->isLockActive();
}
#endif

void ScreenPanelVulkan::prepareForRendererTransition()
{
    if (!vulkan)
        return;

    const bool hadPresenter = vulkan->presenterInitialized;
    const bool hadOutput = vulkan->output.isInitialized();

    // Consumer-to-producer teardown. Presenter commands can sample compositor
    // images, while compositor commands can sample renderer-owned VkImageViews.
    vulkan->presenter.Shutdown();
    vulkan->presenterInitialized = false;

    // shutdown() performs vkDeviceWaitIdle before destroying compositor command
    // buffers, descriptor sets, frame resources and cached image-view bindings.
    // This must finish before NDS::SetRenderer destroys VulkanRenderer3D.
    vulkan->output.shutdown();

    vulkan->frameQueue.clear();
    vulkan->consecutiveFailures = 0;
    vulkan->runtimeFailureReported = false;

    {
        QMutexLocker bufferLock(&vulkan->softwareBufferLock);
        vulkan->softwareMode = true;
        vulkan->hasSoftwareBuffers = false;
    }

    requestNativeSurfaceVisible(false);

    if (hadPresenter || hadOutput)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "Vulkan presentation/output quiesced before renderer transition "
            "(presenter=%d output=%d)",
            hadPresenter ? 1 : 0,
            hadOutput ? 1 : 0);
    }
}

void ScreenPanelVulkan::PrepareForInstanceRendererTransition(EmuInstance* instance)
{
    if (instance == nullptr)
        return;

    // Destructor unregisters under this lock before deleting VulkanState.
    std::lock_guard<std::mutex> lock(VulkanPanelRegistryMutex());
    for (ScreenPanelVulkan* panel : VulkanPanelRegistry())
    {
        if (panel == nullptr || panel->emuInstance != instance)
            continue;
        panel->prepareForRendererTransition();
    }
}

void ScreenPanelVulkan::beginModalPausePresentation()
{
    if (!vulkan || vulkan->modalPauseOverlay)
        return;

#ifdef MELONPRIME_CUSTOM_HUD
    // The on-screen HUD editor owns the panel and keeps it presenting live; a
    // frozen grab on top would hide every edit the user makes.
    if (vulkan->hudEditLivePresentation.load(std::memory_order_relaxed))
        return;
#endif

    QScreen* targetScreen = windowHandle() ? windowHandle()->screen() : nullptr;
    if (!targetScreen)
        targetScreen = QGuiApplication::primaryScreen();
    if (!targetScreen)
        return;

    const QPixmap frozenFrame = targetScreen->grabWindow(winId());
    if (frozenFrame.isNull())
        return;

    auto* overlay = new QLabel(this);
    overlay->setAttribute(Qt::WA_NativeWindow, true);
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    overlay->setAutoFillBackground(false);
    overlay->setScaledContents(true);
    overlay->setPixmap(frozenFrame);
    overlay->setGeometry(rect());
    overlay->show();
    overlay->raise();
    vulkan->modalPauseOverlay = overlay;
}

void ScreenPanelVulkan::endModalPausePresentation()
{
    if (!vulkan || !vulkan->modalPauseOverlay)
        return;

    QLabel* overlay = vulkan->modalPauseOverlay;
    vulkan->modalPauseOverlay = nullptr;
    delete overlay;
}

#ifdef MELONPRIME_CUSTOM_HUD
void ScreenPanelVulkan::setHudEditModeActive(bool active)
{
    ScreenPanel::setHudEditModeActive(active);
    if (!vulkan)
        return;

    vulkan->hudEditLivePresentation.store(active, std::memory_order_relaxed);
    if (!active)
        return;

    // Drop the modal-pause freeze: from here the panel presents every paused
    // draw pass itself, which is both what makes the editor overlay visible and
    // what keeps the native Vulkan child repainted while the dialog is around.
    endModalPausePresentation();
}
#endif

void ScreenPanelVulkan::resizeEvent(QResizeEvent* event)
{
    ScreenPanel::resizeEvent(event);
    refreshNativeSurfaceGuiThread();
    if (vulkan && vulkan->modalPauseOverlay)
        vulkan->modalPauseOverlay->setGeometry(rect());
}

bool ScreenPanelVulkan::event(QEvent* event)
{
    const bool handled = ScreenPanel::event(event);
    switch (event->type())
    {
    case QEvent::WinIdChange:
    case QEvent::Show:
    case QEvent::WindowStateChange:
    case QEvent::ScreenChangeInternal:
        // Qt rebuilds the native window across these transitions. The macOS
        // presentation layer has to be re-hosted on the new native handle;
        // platforms whose surface is derived from the window handle itself
        // recreate their swapchain through the existing dirty/resize path.
        refreshNativeSurfaceGuiThread();
        break;
    default:
        break;
    }
    return handled;
}

void ScreenPanelVulkan::refreshNativeSurfaceGuiThread()
{
    if (!vulkan || !vulkan->initialized)
        return;

#if defined(__APPLE__) // scatter-budget-exempt: macOS Vulkan presentation layer, not input dispatch
    auto& nativeWindow = vulkan->nativeWindow;
    if (nativeWindow.type != MelonPrime::VulkanNativeWindowType::Metal)
        return;

    // winId() is re-read every time: Qt rebuilds the native view across
    // fullscreen and screen transitions, and CreateOrAttachLayer() re-parents
    // the same layer rather than making a new one, so the VkSurfaceKHR created
    // from it stays valid.
    nativeWindow.window = MelonPrime::VulkanMacOS::CreateOrAttachLayer(
        nativeWindow.window,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(winId())));

    MelonPrime::VulkanMacOS::UpdateLayerGeometry(
        nativeWindow.window,
        static_cast<double>(devicePixelRatioF()),
        width(),
        height());
#elif defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
    auto* host = vulkan->linuxSurfaceHost.data();
    if (host == nullptr)
        return;

    host->setGeometry(rect());

    // Only a mapped child has a usable native surface: Qt's Wayland backend
    // destroys the wl_surface of a hidden window. Publishing an empty handle
    // while hidden is what keeps the emulation thread from building a swapchain
    // on a surface that is about to be (or already was) torn down.
    MelonPrime::VulkanNativeWindowInfo info{};
    std::uint64_t generation = 0;
    if (host->isVisible())
    {
        info = ResolveLinuxVulkanNativeWindow(host);
        generation = host->nativeGeneration();
    }

    {
        QMutexLocker lock(&vulkan->nativeWindowLock);
        vulkan->nativeWindow = info;
        vulkan->nativeWindowGeneration = generation;
    }
#endif
}

void ScreenPanelVulkan::setNativeSurfaceVisibleGuiThread(bool visible)
{
    if (!vulkan)
        return;

    if (VulkanTransitionDebugEnabled())
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[VulkanTransition] presentation surface %s",
            visible ? "shown" : "hidden");
    }

#if defined(__APPLE__) // scatter-budget-exempt: macOS Vulkan presentation layer, not input dispatch
    if (vulkan->nativeWindow.type != MelonPrime::VulkanNativeWindowType::Metal)
        return;
    MelonPrime::VulkanMacOS::SetLayerHidden(vulkan->nativeWindow.window, !visible);
    if (!visible)
    {
        // The panel owns the software/splash drawing again.
        update();
    }
#elif defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
    auto* host = vulkan->linuxSurfaceHost.data();
    if (host == nullptr)
        return;

    if (visible)
    {
        host->setGeometry(rect());
        host->show();
        host->raise();
        // Mapping recreates the native surface on Wayland; republish the
        // handle the emulation thread is about to build a swapchain from.
        refreshNativeSurfaceGuiThread();
    }
    else
    {
        // Unmapping hands the screen area back to the panel's backing store.
        // The handle is dropped by the same refresh path, so a late Vulkan
        // frame cannot present into a surface Qt is painting over.
        host->hide();
        refreshNativeSurfaceGuiThread();
        update();
    }
#else
    (void)visible;
#endif
}

void ScreenPanelVulkan::requestNativeSurfaceVisible(bool visible)
{
    if (!vulkan || vulkan->surfaceVisibleRequested == visible)
        return;

    vulkan->surfaceVisibleRequested = visible;
    QMetaObject::invokeMethod(
        this,
        [this, visible]() { setNativeSurfaceVisibleGuiThread(visible); },
        Qt::QueuedConnection);
}

bool ScreenPanelVulkan::nativeSurfaceReady() const
{
    if (!vulkan)
        return false;

    QMutexLocker lock(&vulkan->nativeWindowLock);
    return vulkan->nativeWindow.type != MelonPrime::VulkanNativeWindowType::Unknown
        && vulkan->nativeWindow.window != nullptr;
}

#if defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
bool ScreenPanelVulkan::prepareLinuxPresentationSurface()
{
    // Emulation thread, called once per Vulkan frame before the presenter is
    // used. Unlike Windows (one HWND) and macOS (a layer that is only hidden,
    // never destroyed), the Linux child surface is unmapped between matches and
    // Qt may hand back a different native surface on the next map.
    requestNativeSurfaceVisible(true);

    std::uint64_t nativeGeneration = 0;
    bool published = false;
    {
        QMutexLocker lock(&vulkan->nativeWindowLock);
        nativeGeneration = vulkan->nativeWindowGeneration;
        published = vulkan->nativeWindow.type != MelonPrime::VulkanNativeWindowType::Unknown
            && vulkan->nativeWindow.window != nullptr;
    }

    if (vulkan->presenterInitialized
        && vulkan->presenterWindowGeneration != nativeGeneration)
    {
        // The VkSurfaceKHR belongs to a native surface that no longer exists.
        if (VulkanTransitionDebugEnabled())
        {
            Platform::Log(
                Platform::LogLevel::Info,
                "[VulkanTransition] native surface generation %llu -> %llu; rebuilding presenter",
                static_cast<unsigned long long>(vulkan->presenterWindowGeneration),
                static_cast<unsigned long long>(nativeGeneration));
        }
        vulkan->presenter.Shutdown();
        vulkan->presenterInitialized = false;
        vulkan->frameQueue.clear();
        vulkan->output.releaseFrameReferences();
    }

    if (!published)
    {
        // The GUI thread has not mapped the child and republished its handle
        // yet. This is the normal first frame after a software-to-Vulkan
        // switch, so retry on the next frame instead of reporting a failure.
        QMetaObject::invokeMethod(
            this,
            [this]() { refreshNativeSurfaceGuiThread(); },
            Qt::QueuedConnection);
        return false;
    }
    return true;
}
#endif

void ScreenPanelVulkan::releaseNativeSurface()
{
    if (!vulkan)
        return;

#if defined(__APPLE__) // scatter-budget-exempt: macOS Vulkan presentation layer, not input dispatch
    auto& nativeWindow = vulkan->nativeWindow;
    if (nativeWindow.type == MelonPrime::VulkanNativeWindowType::Metal)
        MelonPrime::VulkanMacOS::DestroyLayer(nativeWindow.window);
    nativeWindow.window = nullptr;
#elif defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
    // Destructor path only, and deliberately after the presenter/output
    // quiesce: the native child surface must outlive every VkSurfaceKHR and
    // swapchain built from it. Deleting it here rather than leaving it to
    // ~QWidget also guarantees no surface-changed callback can run after
    // VulkanState is gone.
    if (auto* host = vulkan->linuxSurfaceHost.data(); host != nullptr)
    {
        host->setNativeSurfaceChangedCallback(nullptr);
        vulkan->linuxSurfaceHost = nullptr;
        delete host;
    }
    {
        QMutexLocker lock(&vulkan->nativeWindowLock);
        vulkan->nativeWindow = MelonPrime::VulkanNativeWindowInfo{};
        vulkan->nativeWindowGeneration = 0;
    }
#endif
}

void ScreenPanelVulkan::paintEvent(QPaintEvent* event)
{
    auto* emuThread = emuInstance->getEmuThread();
    if (emuThread != nullptr && emuThread->emuIsActive())
    {
        QMutexLocker bufferLock(&vulkan->softwareBufferLock);
        if (!vulkan->softwareMode)
            return;

        QPainter painter(this);
        if (!painter.isActive())
        {
            // The panel is not on Qt's backing-store path, so every software
            // frame drawn here is silently discarded and whatever the previous
            // producer left on the surface stays visible. Report it once: this
            // is the failure mode that stranded the post-match recap on Linux.
            if (!vulkan->softwarePaintFailureReported)
            {
                vulkan->softwarePaintFailureReported = true;
                Platform::Log(
                    Platform::LogLevel::Error,
                    "Vulkan panel cannot paint software frames: QPainter is inactive");
            }
            return;
        }
        if (vulkan->logSoftwarePaintHandoff.exchange(false, std::memory_order_relaxed))
        {
            Platform::Log(
                Platform::LogLevel::Info,
                "[VulkanTransition] software paintEvent active=1 buffers=%d",
                vulkan->hasSoftwareBuffers ? 1 : 0);
        }
        painter.fillRect(event->rect(), QColor::fromRgb(0, 0, 0));
        if (vulkan->hasSoftwareBuffers)
        {
            const QRect screenRect(0, 0, 256, 192);
            for (int index = 0; index < numScreens; ++index)
            {
                painter.setTransform(vulkan->softwareScreenTransform[index]);
                painter.drawImage(
                    screenRect,
                    vulkan->softwareScreen[screenKind[index]]);
            }

#define MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE (&vulkan->softwareScreen[1])
#include "MelonPrimeHudScreenCppOverlayOfSoftware.inc"
#undef MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE
        }

        if (osdEnabled)
        {
            QMutexLocker osdLock(&osdMutex);
            int y = kOSDMargin;
            painter.resetTransform();
            for (const OSDItem& item : osdItems)
            {
                painter.drawImage(kOSDMargin, y, item.bitmap);
                y += item.bitmap.height();
            }
        }
        return;
    }

    QPainter painter(this);
    painter.fillRect(event->rect(), QColor::fromRgb(0, 0, 0));
    osdUpdate();
    QMutexLocker osdLock(&osdMutex);
    painter.drawPixmap(QRect(splashPos[3], QSize(kLogoWidth, kLogoWidth)), splashLogo);
    for (int index = 0; index < 3; ++index)
        painter.drawImage(splashPos[index], splashText[index].bitmap);
}

bool ScreenPanelVulkan::initVulkan()
{
    if (!vulkan || !vulkan->output.init())
        return false;

    auto& nativeWindow = vulkan->nativeWindow;
#if defined(_WIN32)
    nativeWindow.type = MelonPrime::VulkanNativeWindowType::Win32;
    nativeWindow.window = reinterpret_cast<void*>(static_cast<std::uintptr_t>(winId()));
#elif defined(__APPLE__)
    // MoltenVK presents only to a CAMetalLayer, so instead of a raw window
    // handle the panel gets a layer composited over its NSView; the layer
    // itself is built on the GUI thread by refreshNativeSurfaceGuiThread().
    // The native Metal renderer builds a separate panel with a separate layer,
    // so the two backends never contend for this view.
    nativeWindow.type = MelonPrime::VulkanNativeWindowType::Metal;
    nativeWindow.window = nullptr;
#elif defined(__linux__)
    // Nothing to publish yet. The handle belongs to the dedicated Vulkan child
    // surface and is only valid while that child is mapped, so
    // refreshNativeSurfaceGuiThread() publishes it on every map and drops it on
    // every unmap. Presenting into the panel's own surface -- which Qt paints
    // the software frames into -- is exactly the sharing this avoids.
    (void)nativeWindow;
#endif

    vulkan->initialized = true;
    // GUI thread here (MainWindow::createScreenPanel): safe to build the
    // platform presentation surface before the emulation thread first asks the
    // presenter to initialize.
    refreshNativeSurfaceGuiThread();
    return true;
}

bool ScreenPanelVulkan::initVulkanPresenter()
{
    if (!vulkan || !vulkan->initialized)
        return false;
    if (vulkan->presenterInitialized)
        return true;

    setupScreenLayout();
    std::uint32_t surfaceWidth = 0;
    std::uint32_t surfaceHeight = 0;
    {
        QMutexLocker lock(&vulkan->layoutLock);
        surfaceWidth = vulkan->surfaceWidth;
        surfaceHeight = vulkan->surfaceHeight;
    }

    MelonPrime::VulkanNativeWindowInfo nativeWindow;
    std::uint64_t nativeGeneration = 0;
    {
        QMutexLocker lock(&vulkan->nativeWindowLock);
        nativeWindow = vulkan->nativeWindow;
        nativeGeneration = vulkan->nativeWindowGeneration;
    }

    if (!vulkan->presenter.Init(
            nativeWindow,
            surfaceWidth,
            surfaceHeight,
            emuInstance->getGlobalConfig().GetBool("Screen.VSync")))
    {
        return false;
    }

    vulkan->presenterInitialized = true;
    // Records which native surface this swapchain belongs to; a later
    // generation means the surface was replaced and the presenter is stale.
    vulkan->presenterWindowGeneration = nativeGeneration;
    Platform::Log(
        Platform::LogLevel::Info,
        "Vulkan presentation initialized requested=Vulkan actual=Vulkan path=Qt-native-swapchain");
    return true;
}

void ScreenPanelVulkan::reportVulkanRuntimeFailure(const char* reason)
{
    if (!vulkan || vulkan->runtimeFailureReported)
        return;

    vulkan->runtimeFailureReported = true;
    MelonPrime::VulkanFeatureCheck::ReportRuntimeFailure(
        reason != nullptr ? reason : "Vulkan presentation failed");
    if (auto* emuThread = emuInstance->getEmuThread(); emuThread != nullptr)
    {
        QMetaObject::invokeMethod(
            emuThread,
            [emuThread]() { emit emuThread->rendererRuntimeFallback(); },
            Qt::QueuedConnection);
    }
}

void ScreenPanelVulkan::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    if (!vulkan)
        return;

    const qreal dpr = devicePixelRatioF();
    std::vector<MelonPrime::VulkanPresentRegion> regions;
    regions.reserve(static_cast<std::size_t>(numScreens));
    for (int index = 0; index < numScreens; ++index)
    {
        const float* matrix = screenMatrix[index];
        vulkan->softwareScreenTransform[index].setMatrix(
            matrix[0], matrix[1], 0.0f,
            matrix[2], matrix[3], 0.0f,
            matrix[4], matrix[5], 1.0f);
        QTransform transform(
            matrix[0], matrix[1],
            matrix[2], matrix[3],
            matrix[4], matrix[5]);
        const QRectF bounds = transform.mapRect(QRectF(0.0, 0.0, 256.0, 192.0));
        MelonPrime::VulkanPresentRegion region;
        region.enabled = bounds.width() > 0.0 && bounds.height() > 0.0;
        region.bottomScreen = screenKind[index] != 0;
        region.x = qRound(bounds.left() * dpr);
        region.y = qRound(bounds.top() * dpr);
        region.width = qRound(bounds.width() * dpr);
        region.height = qRound(bounds.height() * dpr);
        const std::array<QPointF, 4> corners = {
            transform.map(QPointF(0.0, 0.0)),
            transform.map(QPointF(256.0, 0.0)),
            transform.map(QPointF(0.0, 192.0)),
            transform.map(QPointF(256.0, 192.0)),
        };
        for (std::size_t corner = 0; corner < corners.size(); ++corner)
        {
            region.corners[corner * 2u] = static_cast<float>(corners[corner].x() * dpr);
            region.corners[(corner * 2u) + 1u] = static_cast<float>(corners[corner].y() * dpr);
        }
        region.hasTransformedCorners = true;
        regions.push_back(region);
    }

    QMutexLocker lock(&vulkan->layoutLock);
    vulkan->surfaceWidth = static_cast<std::uint32_t>(std::max<qreal>(0.0, width() * dpr));
    vulkan->surfaceHeight = static_cast<std::uint32_t>(std::max<qreal>(0.0, height() * dpr));
    vulkan->regions = std::move(regions);
}

void ScreenPanelVulkan::drawScreen()
{
#if defined(__APPLE__) // scatter-budget-exempt: macOS Vulkan frame boundary, not input dispatch
    MelonPrime::VulkanMacOS::RunInAutoreleasePool([this]() { drawScreenFrame(); });
#else
    drawScreenFrame();
#endif
}

void ScreenPanelVulkan::beginVulkanLowLatencyFrame(int reflexMode, bool antiLag2Enabled)
{
    if (vulkan && vulkan->presenterInitialized)
    {
        vulkan->presenter.BeginAmdAntiLag2Frame(antiLag2Enabled);
        vulkan->presenter.BeginNvidiaReflexFrame(reflexMode);
    }
}

void ScreenPanelVulkan::markVulkanReflexInputSample()
{
    if (vulkan && vulkan->presenterInitialized)
        vulkan->presenter.MarkNvidiaReflexInputSample();
}

void ScreenPanelVulkan::markVulkanReflexRenderSubmitStart()
{
    if (vulkan && vulkan->presenterInitialized)
        vulkan->presenter.MarkNvidiaReflexRenderSubmitStart();
}

void ScreenPanelVulkan::markVulkanReflexRenderSubmitEnd()
{
    if (vulkan && vulkan->presenterInitialized)
        vulkan->presenter.MarkNvidiaReflexRenderSubmitEnd();
}

void ScreenPanelVulkan::finishVulkanLowLatencyFrame()
{
    if (vulkan && vulkan->presenterInitialized)
    {
        vulkan->presenter.FinishNvidiaReflexFrame();
        vulkan->presenter.FinishAmdAntiLag2Frame();
    }
}

void ScreenPanelVulkan::ComposeInstanceFrameAtVBlank(EmuInstance* instance)
{
    if (instance == nullptr)
        return;

    // Go through the registry rather than capturing a panel pointer: a window
    // can be closed while the renderer lives on, and every window of this
    // instance needs its own composed frame from the same emulated frame.
    std::lock_guard<std::mutex> lock(VulkanPanelRegistryMutex());
    for (ScreenPanelVulkan* panel : VulkanPanelRegistry())
    {
        if (panel == nullptr || panel->emuInstance != instance)
            continue;
        panel->composeFrameAtVBlank();
    }
}

void ScreenPanelVulkan::installVulkanComposeHook(melonDS::VulkanRenderer* renderer)
{
    if (renderer == nullptr || renderer->HasVBlankHook())
        return;

    EmuInstance* instance = emuInstance;
    renderer->SetVBlankHook([instance]() { ComposeInstanceFrameAtVBlank(instance); });
}

void ScreenPanelVulkan::composeFrameAtVBlank()
{
    // Emulation thread, at VCount 192. The structured planes were completed at
    // scanline 191 and the 3D color target still holds the render that belongs
    // to them; VCount 215 is about to start the next one into the same image.
    if (!vulkan || !vulkan->initialized || !vulkan->output.isInitialized())
        return;

    auto* nds = emuInstance->getNDS();
    if (!nds)
        return;

    auto* renderer = dynamic_cast<melonDS::VulkanRenderer*>(&nds->GPU.GetRenderer());
    melonDS::VulkanRenderer3D* renderer3D = renderer ? renderer->GetVulkanRenderer3D() : nullptr;
    if (!renderer || !renderer3D)
        return;

    SoftRenderer::StructuredVulkanFrameView structuredView{};
    if (!renderer->GetStructuredVulkanFrame(structuredView) || !structuredView.Valid)
    {
        if (vulkan->consecutiveFailures++ == 0)
            Platform::Log(Platform::LogLevel::Error, "Vulkan presentation lost its Vulkan 2D source");
        return;
    }
    vulkan->nativeMenuHeld.store(structuredView.NativeMenuHeld, std::memory_order_relaxed);

    const int configuredScale = std::clamp(
        emuInstance->getGlobalConfig().GetInt("3D.GL.ScaleFactor"), 1, 16);
    const u32 rendererScale = renderer3D->GetColorTargetWidth() >= 256
        ? std::max<u32>(1, renderer3D->GetColorTargetWidth() / 256u)
        : static_cast<u32>(configuredScale);
    const u32 outputWidth = 256u * rendererScale;
    const u32 outputHeight = 386u * rendererScale;

    MelonPrime::VulkanFrame* renderFrame = vulkan->frameQueue.getRenderFrame(vulkan->framePolicy);
    if (!renderFrame || !vulkan->output.ensureFrameResources(renderFrame, outputWidth, outputHeight))
    {
        if (renderFrame)
            vulkan->frameQueue.discardRenderedFrame(renderFrame);
        ++vulkan->consecutiveFailures;
        return;
    }

    // Everything the compositor sees comes from this one emulated frame. The
    // producer already resolved which engine owns which LCD, so there is no
    // screen-swap value to pass and no frame history to maintain.
    MelonPrime::StructuredCompositionFrame structured{};
    for (std::size_t screen = 0; screen < 2u; ++screen)
    {
        for (std::size_t plane = 0; plane < 3u; ++plane)
            structured.Plane[screen][plane] = structuredView.Plane[screen][plane];
        structured.LineMeta[screen] = structuredView.LineMeta[screen];
    }
    structured.Has3D = !nds->GPU.GPU3D.AbortFrame;
    structured.Generation = structuredView.Generation;

    // Composition is always nearest and deterministic. Screen.Filter is a
    // presentation preference and is applied once, by the presenter, to the
    // finished image -- the same place the software and OpenGL paths apply it.
    MelonPrime::VulkanCompositionInputs inputs{};
    const bool composed = vulkan->output.prepareFrameForPresentation(renderFrame, structured)
        && vulkan->output.buildCompositionInputs(
            renderFrame,
            *renderer3D,
            static_cast<int>(rendererScale),
            structured.Has3D,
            structured.Generation,
            inputs)
        && vulkan->output.composeAndSubmitFrame(renderFrame, inputs);
    if (!composed)
    {
        vulkan->frameQueue.discardRenderedFrame(renderFrame);
        if (vulkan->consecutiveFailures++ == 0)
            Platform::Log(Platform::LogLevel::Error, "Vulkan compositor submission failed");
        return;
    }

    vulkan->frameQueue.pushRenderedFrame(renderFrame, vulkan->framePolicy);
}

void ScreenPanelVulkan::drawScreenFrame()
{
    refreshClipForGameStateChange();
    if (!vulkan || !vulkan->initialized)
        return;

    auto* emuThread = emuInstance->getEmuThread();
    osdUpdate();
    if (!emuThread->emuIsActive())
    {
        if (vulkan->presenterInitialized)
        {
            vulkan->presenter.Shutdown();
            vulkan->presenterInitialized = false;
            vulkan->frameQueue.clear();
            vulkan->output.releaseFrameReferences();
        }
        {
            QMutexLocker bufferLock(&vulkan->softwareBufferLock);
            vulkan->softwareMode = true;
            vulkan->hasSoftwareBuffers = false;
        }
        requestNativeSurfaceVisible(false);
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        return;
    }

    // The native swapchain already owns the last completed image. Rebuilding
    // from DS staging buffers while paused can surface stale boot/menu pixels;
    // leave the swapchain untouched until emulation resumes. The presentation
    // surface is Vulkan's alone (a separate native child on macOS/Linux,
    // WA_PaintOnScreen on Windows), so Qt cannot erase it while a modal dialog
    // is exposed.
    //
    // The Custom HUD on-screen editor is the one paused state that must keep
    // drawing: the settings dialog pauses emulation before handing the panel to
    // the editor, so without this the editor overlay would never reach the
    // screen at all (OpenGL/software gate on emuIsActive() and keep drawing).
#ifdef MELONPRIME_CUSTOM_HUD
    const bool hudEditLivePresentation =
        vulkan->hudEditLivePresentation.load(std::memory_order_relaxed);
#else
    constexpr bool hudEditLivePresentation = false;
#endif
    if (!emuThread->emuIsRunning() && !hudEditLivePresentation)
        return;

    auto* nds = emuInstance->getNDS();
    if (!nds)
        return;

    const RendererOutput rendererOutput = nds->GPU.GetRendererOutput();
    if (rendererOutput.Kind == RendererOutputKind::CpuBgra)
    {
        if (vulkan->presenterInitialized)
        {
            // Post-match handoff: the 3D renderer went back to software, so
            // Vulkan stops producing and Qt takes the screen area back.
            if (VulkanTransitionDebugEnabled())
            {
                Platform::Log(
                    Platform::LogLevel::Info,
                    "[VulkanTransition] output=CpuBgra; shutting the presenter down");
                vulkan->logSoftwarePaintHandoff.store(true, std::memory_order_relaxed);
            }
            vulkan->presenter.Shutdown();
            vulkan->presenterInitialized = false;
            vulkan->frameQueue.clear();
            vulkan->output.releaseFrameReferences();
        }
        {
            QMutexLocker bufferLock(&vulkan->softwareBufferLock);
            std::memcpy(
                vulkan->softwareScreen[0].scanLine(0),
                rendererOutput.Top,
                256u * 192u * sizeof(u32));
            std::memcpy(
                vulkan->softwareScreen[1].scanLine(0),
                rendererOutput.Bottom,
                256u * 192u * sizeof(u32));
            vulkan->softwareMode = true;
            vulkan->hasSoftwareBuffers = true;
        }
        vulkan->consecutiveFailures = 0;
        requestNativeSurfaceVisible(false);
        QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
        return;
    }

    auto* renderer = dynamic_cast<VulkanRenderer*>(&nds->GPU.GetRenderer());
    VulkanRenderer3D* renderer3D = renderer ? renderer->GetVulkanRenderer3D() : nullptr;

    // Fullscreen/native-window changes can schedule one final Vulkan-panel draw
    // after EmuThread has already replaced the renderer with Software/OpenGL.
    // RendererOutput may still describe the preceding GPU frame at that point.
    // Never reacquire VulkanContext from that stale panel pass.
    if (!renderer || !renderer3D)
    {
        if (vulkan->presenterInitialized || vulkan->output.isInitialized())
            prepareForRendererTransition();
        else
            requestNativeSurfaceVisible(false);
        return;
    }

    // The transition hook shuts the complete output down before the old Vulkan
    // renderer dies. Recreate it only after identifying a new Vulkan renderer.
    if (!vulkan->output.isInitialized() && !vulkan->output.init())
    {
        if (vulkan->consecutiveFailures++ == 0)
        {
            Platform::Log(
                Platform::LogLevel::Error,
                "Vulkan output reinitialization failed after renderer transition");
        }
        reportVulkanRuntimeFailure(
            "Vulkan output reinitialization failed after renderer transition");
        return;
    }

    // Composition itself runs from the renderer's VBlank hook; this pass only
    // presents whatever that produced. Installing the hook here rather than at
    // renderer creation keeps it tied to a panel that is actually drawing.
    installVulkanComposeHook(renderer);

#ifdef MELONPRIME_CUSTOM_HUD
    auto* mp = emuThread->GetMelonPrimeCore();
    const bool vulkanNativeMenuHeld =
        vulkan->nativeMenuHeld.load(std::memory_order_relaxed);
#else
    const bool vulkanNativeMenuHeld = false;
#endif

    {
        QMutexLocker bufferLock(&vulkan->softwareBufferLock);
        vulkan->softwareMode = false;
        vulkan->hasSoftwareBuffers = false;
    }
#if defined(__linux__) // scatter-budget-exempt: Linux Vulkan presentation surface, not input dispatch
    if (!prepareLinuxPresentationSurface())
        return;
#endif
    if (!nativeSurfaceReady())
    {
        // The GUI thread has not (re)built the platform presentation surface
        // yet -- on macOS the CAMetalLayer is re-hosted after Qt recreates the
        // native view. This is recoverable, so ask for a refresh and retry on
        // the next frame instead of permanently disabling Vulkan.
        if (vulkan->consecutiveFailures++ == 0)
            Platform::Log(Platform::LogLevel::Warn, "Vulkan presentation surface is not ready yet");
        QMetaObject::invokeMethod(
            this,
            [this]() { refreshNativeSurfaceGuiThread(); },
            Qt::QueuedConnection);
        return;
    }

    if (!initVulkanPresenter())
    {
        // Never leave a mapped-but-empty presentation surface covering the
        // software output the panel falls back to.
        requestNativeSurfaceVisible(false);
        if (vulkan->consecutiveFailures++ == 0)
            Platform::Log(Platform::LogLevel::Error, "Vulkan native presenter initialization failed");
        const std::string& reason = vulkan->presenter.LastError();
        reportVulkanRuntimeFailure(
            reason.empty()
                ? "Vulkan native presenter initialization failed"
                : reason.c_str());
        return;
    }

    const int configuredScale = std::clamp(
        emuInstance->getGlobalConfig().GetInt("3D.GL.ScaleFactor"), 1, 16);
    const u32 rendererScale = renderer3D->GetColorTargetWidth() >= 256
        ? std::max<u32>(1, renderer3D->GetColorTargetWidth() / 256u)
        : static_cast<u32>(configuredScale);

    MelonPrime::VulkanFrame* presentFrame = vulkan->frameQueue.getPresentCandidate(
        vulkan->framePolicy, std::nullopt);
    if (!presentFrame)
        return;

    std::vector<MelonPrime::VulkanPresentRegion> regions;
    std::uint32_t surfaceWidth = 0;
    std::uint32_t surfaceHeight = 0;
    {
        QMutexLocker lock(&vulkan->layoutLock);
        regions = vulkan->regions;
        surfaceWidth = vulkan->surfaceWidth;
        surfaceHeight = vulkan->surfaceHeight;
    }
    vulkan->presenter.Resize(
        surfaceWidth,
        surfaceHeight,
        emuInstance->getGlobalConfig().GetBool("Screen.VSync"));

    bool hasOverlay = false;
    MelonPrime::VulkanRadarFrame radarFrame{};
    const qreal dpr = devicePixelRatioF();
    const int logicalWidth = width();
    const int logicalHeight = height();
    if (surfaceWidth > 0 && surfaceHeight > 0 && logicalWidth > 0 && logicalHeight > 0)
    {
        if (vulkan->overlayFrame.width() != static_cast<int>(surfaceWidth)
            || vulkan->overlayFrame.height() != static_cast<int>(surfaceHeight)
            || vulkan->overlayFrame.format() != QImage::Format_ARGB32_Premultiplied)
        {
            vulkan->overlayFrame = QImage(
                static_cast<int>(surfaceWidth),
                static_cast<int>(surfaceHeight),
                QImage::Format_ARGB32_Premultiplied);
        }
        vulkan->overlayFrame.fill(Qt::transparent);
        QPainter overlayPainter(&vulkan->overlayFrame);
        overlayPainter.scale(dpr, dpr);

#ifdef MELONPRIME_CUSTOM_HUD
        {
            const bool editMode = mp && MelonPrime::CustomHud_IsEditMode(mp->HudConfigState());
            // The held native MPH menu suppresses the gameplay HUD, but never
            // the layout editor: it is opened from the settings dialog and has
            // to stay on screen whatever the paused frame happens to show.
            if ((!vulkanNativeMenuHeld || editMode)
                && MelonPrimeHud_CanRenderForCore(mp, editMode))
            {
                auto& instcfg = emuInstance->getLocalConfig();
                MelonPrimeHud_RefreshHudEnabledIfNeeded(
                    mp->HudConfigState(), instcfg, m_hudCfgEpoch, m_hudEnabled);
                MelonPrimeHud_RefreshOverlayFontIfNeeded(
                    mp->HudConfigState(), instcfg, m_hudFontEpoch, overlayFont);
                MelonPrimeHud_RefreshRadarConfigIfNeeded(
                    mp->HudConfigState(), instcfg, m_radarCfgEpoch,
                    m_radarEnable, m_radarAnchor,
                    m_radarDstX, m_radarDstY, m_radarDstSize,
                    m_radarOpacity, m_radarSrcRadius,
                    m_radarAnchorDsX, m_radarAnchorDsY);
                if (MelonPrimeHud_IsHudVisibleOrRestorePatch(
                        emuInstance, instcfg, mp, m_hudEnabled, editMode))
                {
                    MelonPrimeHud_PrepareTopOverlay(
                        Overlay[0], logicalWidth, logicalHeight, m_hudPrevDirty);
                    const QRect currentDirty = MelonPrimeHud_RenderTopOverlay(
                        emuInstance,
                        instcfg,
                        mp,
                        Overlay[0],
                        overlayFont,
                        m_topStretchX,
                        m_hudScale,
                        m_hudOriginX,
                        m_hudOriginY);
                    overlayPainter.drawImage(QPoint(0, 0), Overlay[0]);
                    m_hudPrevDirty = currentDirty;
                    hasOverlay = true;

                    if (m_radarEnable && m_hudTopMatrixValid
                        && MelonPrime::CustomHud_ShouldDrawRadarOverlay(
                            emuInstance, mp->GetCurrentRom(), mp->GetPlayerPosition()))
                    {
                        const float* topMtx = m_hudTopMatrix;
                        const float anchorX = topMtx[0] * m_radarAnchorDsX
                            + topMtx[1] * m_radarAnchorDsY + topMtx[4];
                        const float anchorY = topMtx[2] * m_radarAnchorDsX
                            + topMtx[3] * m_radarAnchorDsY + topMtx[5];
                        const int overlayBaseX = static_cast<int>(m_hudOriginX);
                        const int overlayBaseY = static_cast<int>(m_hudOriginY);
                        const int destinationX = overlayBaseX + static_cast<int>(
                            (anchorX - m_hudOriginX) + m_radarDstX * m_hudScale);
                        const int destinationY = overlayBaseY + static_cast<int>(
                            (anchorY - m_hudOriginY) + m_radarDstY * m_hudScale);
                        const uint8_t hunterID = std::min<uint8_t>(
                            mp->GetHunterID(), MelonPrime::kHunterCount - 1);
                        radarFrame.enabled = true;
                        radarFrame.x = static_cast<float>(destinationX * dpr);
                        radarFrame.y = static_cast<float>(destinationY * dpr);
                        radarFrame.size = static_cast<float>(m_radarDstSize * m_hudScale * dpr);
                        radarFrame.opacity = m_radarOpacity;
                        radarFrame.sourceCenterY = static_cast<std::uint32_t>(
                            MelonPrime::kBtmOverlaySrcCenterY[hunterID]);
                        radarFrame.sourceRadius = static_cast<std::uint32_t>(
                            std::max(m_radarSrcRadius, 0));
                    }
                }
            }
        }
#endif

        if (osdEnabled && !osdItems.empty())
        {
            QMutexLocker osdLock(&osdMutex);
            int y = kOSDMargin;
            for (const OSDItem& item : osdItems)
            {
                overlayPainter.drawImage(kOSDMargin, y, item.bitmap);
                y += item.bitmap.height();
            }
            hasOverlay = true;
        }
    }

    MelonPrime::VulkanOverlayFrame overlay{};
    overlay.radar = radarFrame;
    if (hasOverlay)
    {
        overlay.pixels = vulkan->overlayFrame.constBits();
        overlay.width = static_cast<std::uint32_t>(vulkan->overlayFrame.width());
        overlay.height = static_cast<std::uint32_t>(vulkan->overlayFrame.height());
        overlay.rowBytes = static_cast<std::size_t>(vulkan->overlayFrame.bytesPerLine());
    }
    const bool hasPresentationOverlay = hasOverlay || radarFrame.IsValid();
    if (vulkan->presenter.Present(
            presentFrame,
            vulkan->output,
            filter ? MelonPrime::VulkanFilterMode::Linear : MelonPrime::VulkanFilterMode::Nearest,
            rendererScale,
            regions,
            hasPresentationOverlay ? &overlay : nullptr))
    {
        vulkan->frameQueue.commitPresentedFrame(presentFrame, vulkan->framePolicy);
        vulkan->consecutiveFailures = 0;
        // Only reveal the platform presentation surface once a frame has
        // actually reached the swapchain, so it never flashes uninitialized
        // content over the software output it replaces.
        requestNativeSurfaceVisible(true);
    }
    else
    {
        vulkan->frameQueue.deferPresentedFrame(presentFrame, vulkan->framePolicy);
        if (vulkan->consecutiveFailures++ == 0)
            Platform::Log(Platform::LogLevel::Warn, "Vulkan swapchain presentation failed; retrying after resync");
    }
}
#endif

ScreenPanelGL::ScreenPanelGL(QWidget * parent) : ScreenPanel(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize());

    glInited = false;
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    waylandPointerLock = std::make_unique<MelonPrime::WaylandPointerLock>(
        [this](std::int32_t dx, std::int32_t dy) {
            addAimMouseDeltaForMelonPrime(dx, dy);
        });
#endif
}

ScreenPanelGL::~ScreenPanelGL()
{
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    if (waylandPointerLock)
        waylandPointerLock->setLocked(nullptr, nullptr, false);
#endif
}

bool ScreenPanelGL::createContext()
{
    std::optional<WindowInfo> windowinfo = getWindowInfo();

    MainWindow* ourwin = (MainWindow*)parentWidget();
    MainWindow* parentwin = (MainWindow*)parentWidget()->parentWidget();

    if (ourwin->getWindowID() != 0)
    {
        if (windowinfo.has_value())
            if ((glContext = parentwin->getOGLContext()->CreateSharedContext(*windowinfo)))
                glContext->DoneCurrent();
    }
    else
    {
        std::array<GL::Context::Version, 2> versionsToTry = {
                GL::Context::Version{GL::Context::Profile::Core, 4, 3},
                GL::Context::Version{GL::Context::Profile::Core, 3, 2} };
        if (windowinfo.has_value())
            if ((glContext = GL::Context::Create(*windowinfo, versionsToTry)))
                glContext->DoneCurrent();
    }

    return glContext != nullptr;
}

void ScreenPanelGL::setSwapInterval(int intv)
{
    if (!glContext) return;

    glContext->SetSwapInterval(intv);
}

void ScreenPanelGL::initOpenGL()
{
    if (!glContext) return;
    if (glInited) return;

    glContext->MakeCurrent();

    OpenGL::CompileVertexFragmentProgram(screenShaderProgram,
        kScreenVS, kScreenFS,
        "ScreenShader",
        { {"vPosition", 0}, {"vTexcoord", 1} },
        { {"oColor", 0} });

    glUseProgram(screenShaderProgram);
    glUniform1i(glGetUniformLocation(screenShaderProgram, "TopScreenTex"), 0);
    glUniform1i(glGetUniformLocation(screenShaderProgram, "BottomScreenTex"), 1);

    screenShaderScreenSizeULoc = glGetUniformLocation(screenShaderProgram, "uScreenSize");
    screenShaderTransformULoc = glGetUniformLocation(screenShaderProgram, "uTransform");

    const float vertices[] =
    {
        0.f,   0.f,    0.f, 0.f, 0.f,
        0.f,   192.f,  0.f, 1.f, 0.f,
        256.f, 192.f,  1.f, 1.f, 0.f,
        0.f,   0.f,    0.f, 0.f, 0.f,
        256.f, 192.f,  1.f, 1.f, 0.f,
        256.f, 0.f,    1.f, 0.f, 0.f,

        0.f,   0.f,    0.f, 0.f, 1.f,
        0.f,   192.f,  0.f, 1.f, 1.f,
        256.f, 192.f,  1.f, 1.f, 1.f,
        0.f,   0.f,    0.f, 0.f, 1.f,
        256.f, 192.f,  1.f, 1.f, 1.f,
        256.f, 0.f,    1.f, 0.f, 1.f
    };

    glGenBuffers(1, &screenVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, screenVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glGenVertexArrays(1, &screenVertexArray);
    glBindVertexArray(screenVertexArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * 4, (void*)(0));
    glEnableVertexAttribArray(1); // texcoord
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * 4, (void*)(2 * 4));

    glGenTextures(1, &screenTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, screenTexture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, 256, 192, 2, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    screenTextureWidth = 256;
    screenTextureHeight = 192;


    OpenGL::CompileVertexFragmentProgram(osdShader,
        kScreenVS_OSD, kScreenFS_OSD,
        "OSDShader",
        { {"vPosition", 0} },
        { {"oColor", 0} });

    glUseProgram(osdShader);
    glUniform1i(glGetUniformLocation(osdShader, "OSDTex"), 0);

    osdScreenSizeULoc = glGetUniformLocation(osdShader, "uScreenSize");
    osdPosULoc = glGetUniformLocation(osdShader, "uOSDPos");
    osdSizeULoc = glGetUniformLocation(osdShader, "uOSDSize");
    osdScaleFactorULoc = glGetUniformLocation(osdShader, "uScaleFactor");
    osdTexScaleULoc = glGetUniformLocation(osdShader, "uTexScale");

    const float osdvertices[6 * 2] =
    {
        0, 0,
        1, 1,
        1, 0,
        0, 0,
        0, 1,
        1, 1
    };

    glGenBuffers(1, &osdVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, osdVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(osdvertices), osdvertices, GL_STATIC_DRAW);

    glGenVertexArrays(1, &osdVertexArray);
    glBindVertexArray(osdVertexArray);
    glEnableVertexAttribArray(0); // position
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)(0));

    // splash logo texture
    QImage logo = splashLogo.scaled(kLogoWidth * 2, kLogoWidth * 2).toImage();
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#ifdef MELONPRIME_DS
    // OPT-TX1: GL_BGRA matches QImage ARGB32_Premultiplied native byte order
    // (BGRA on little-endian), enabling driver fast-path (no R↔B byte swap).
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, logo.width(), logo.height(), 0, GL_BGRA, GL_UNSIGNED_BYTE, logo.bits());
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, logo.width(), logo.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, logo.bits());
#endif
    logoTexture = tex;

#include "MelonPrimeHudScreenCppGlInit.inc"

    transferLayout();
    glInited = true;
}

void ScreenPanelGL::deinitOpenGL()
{
    if (!glContext) return;
    if (!glInited) return;

    glContext->MakeCurrent();

    glDeleteTextures(1, &screenTexture);

    glDeleteVertexArrays(1, &screenVertexArray);
    glDeleteBuffers(1, &screenVertexBuffer);

    glDeleteProgram(screenShaderProgram);


    for (const auto& [key, tex] : osdTextures)
    {
        glDeleteTextures(1, &tex);
    }
    osdTextures.clear();

    glDeleteVertexArrays(1, &osdVertexArray);
    glDeleteBuffers(1, &osdVertexBuffer);

    glDeleteTextures(1, &logoTexture);

#include "MelonPrimeHudScreenCppGlDeinit.inc"

    glDeleteProgram(osdShader);


    glContext->DoneCurrent();

    lastScreenWidth = lastScreenHeight = -1;
    glInited = false;
}

void ScreenPanelGL::makeCurrentGL()
{
    if (!glContext) return;

    glContext->MakeCurrent();
}

void ScreenPanelGL::releaseGL()
{
    if (!glContext) return;

    glContext->DoneCurrent();
}

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
bool ScreenPanelGL::setWaylandPointerLockForMelonPrime(bool enabled)
{
    if (!waylandPointerLock)
        return false;

    if (!enabled)
        return waylandPointerLock->setLocked(nullptr, nullptr, false);

    // Lock the top-level window's surface, not this panel's own
    // Qt::WA_NativeWindow subsurface (used by getWindowInfo() for GL context
    // creation). Locking the child subsurface directly made KWin immediately
    // fire WindowDeactivate on the main window in windowed (non-fullscreen)
    // mode, which our own Suspend() path read as focus loss and tore the lock
    // right back down -- a lock/unlock churn whose brief unlocked gaps let
    // fast mouse motion escape the window (see issue #526).
    QWindow* const topLevelHandle = window() ? window()->windowHandle() : nullptr;
    const auto handles = ResolveMelonPrimeWaylandHandles(topLevelHandle);
    if (!handles.has_value())
        return false;

    // Hint the panel's own center, expressed in the locked (top-level)
    // surface's local coordinates, so the compositor recenters the cursor
    // away from any edge whenever this lock later releases.
    const QPoint hint = window() ? mapTo(window(), rect().center()) : rect().center();
    return waylandPointerLock->setLocked(
        handles->first, handles->second, true, hint.x(), hint.y());
}

bool ScreenPanelGL::isWaylandPointerLockActiveForMelonPrime() const
{
    return waylandPointerLock && waylandPointerLock->isLockActive();
}
#endif

void ScreenPanelGL::osdRenderItem(OSDItem * item)
{
    ScreenPanel::osdRenderItem(item);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
#ifdef MELONPRIME_DS
    // OPT-TX1: GL_BGRA fast-path (see logo upload comment).
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, item->bitmap.width(), item->bitmap.height(), 0, GL_BGRA, GL_UNSIGNED_BYTE, item->bitmap.bits());
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, item->bitmap.width(), item->bitmap.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, item->bitmap.bits());
#endif

    osdTextures[item->id] = tex;
}

void ScreenPanelGL::osdDeleteItem(OSDItem * item)
{
    if (osdTextures.count(item->id))
    {
        GLuint tex = osdTextures[item->id];
        glDeleteTextures(1, &tex);
        osdTextures.erase(item->id);
    }

    ScreenPanel::osdDeleteItem(item);
}

void ScreenPanelGL::drawScreen()
{
    refreshClipForGameStateChange();

    // During a live Vulkan -> OpenGL switch, the paused emulation loop can ask
    // the newly published panel to draw before msg_InitGL has initialized its
    // shaders and textures. Running the HUD path in that window poisons its
    // upload-size cache even though every GL upload failed, leaving Custom HUD
    // permanently blank after the transition.
    if (!glContext || !glInited) return;

    auto emuThread = emuInstance->getEmuThread();

    glContext->MakeCurrent();

    int w = windowInfo.surface_width;
    int h = windowInfo.surface_height;
    float factor = windowInfo.surface_scale;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glViewport(0, 0, w, h);

    if (emuThread->emuIsActive())
    {
        auto nds = emuInstance->getNDS();

        glUseProgram(screenShaderProgram);
        glUniform2f(screenShaderScreenSizeULoc, w / factor, h / factor);

        const RendererOutput output = nds->GPU.GetRendererOutput();
        const bool hasCPUBuffers = (output.Kind == RendererOutputKind::CpuBgra);
        GLuint activeScreenTexture = screenTexture; // track which texture has the screen data
        if (hasCPUBuffers)
        {
            // if we're doing a regular render, use the provided framebuffers
            // otherwise, GetFramebuffers() will set up the required state

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, screenTexture);

            const int outputWidth = static_cast<int>(std::max(1u, output.Width));
            const int outputHeight = static_cast<int>(std::max(1u, output.Height));
            if (screenTextureWidth != outputWidth || screenTextureHeight != outputHeight)
            {
                glTexImage3D(
                    GL_TEXTURE_2D_ARRAY,
                    0,
                    GL_RGBA,
                    outputWidth,
                    outputHeight,
                    2,
                    0,
                    GL_BGRA,
                    GL_UNSIGNED_BYTE,
                    nullptr);
                screenTextureWidth = outputWidth;
                screenTextureHeight = outputHeight;
            }

            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, outputWidth, outputHeight, 1, GL_BGRA,
                GL_UNSIGNED_BYTE, output.Top);
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 1, outputWidth, outputHeight, 1, GL_BGRA,
                GL_UNSIGNED_BYTE, output.Bottom);
        }
        else if (output.Kind == RendererOutputKind::OpenGLTextureArray)
        {
            GLuint texid = *(GLuint*)output.Top;
            activeScreenTexture = texid; // GPU renderer's texture has the screen data

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, texid);
        }
        else
        {
            glContext->SwapBuffers();
            return;
        }

        screenSettingsLock.lock();

        GLint filter = this->filter ? GL_LINEAR : GL_NEAREST;
#ifdef MELONPRIME_DS
        // OPT-GL1: glTexParameteri internally validates texture state even for no-ops
        // (~100-200 cyc per call on NVIDIA/AMD). Filter only changes when user
        // toggles it in settings, so cache and skip redundant calls.
        // Safe: texture parameters are per-texture object, not affected by 3D renderer.
        if (filter != lastFilter) {
            lastFilter = filter;
#endif
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, filter);
#ifdef MELONPRIME_DS
        }
#endif

        glBindBuffer(GL_ARRAY_BUFFER, screenVertexBuffer);
        glBindVertexArray(screenVertexArray);

        for (int i = 0; i < numScreens; i++)
        {
            glUniformMatrix2x3fv(screenShaderTransformULoc, 1, GL_TRUE, screenMatrix[i]);
            glDrawArrays(GL_TRIANGLES, screenKind[i] == 0 ? 0 : 2 * 3, 2 * 3);
        }

#include "MelonPrimeHudScreenCppOverlayOfGl.inc"

        screenSettingsLock.unlock();
    }

    osdUpdate();

    if (!emuThread->emuIsActive())
    {
        // splashscreen
        osdMutex.lock();

        glUseProgram(osdShader);

        glUniform2f(osdScreenSizeULoc, w, h);
        glUniform1f(osdScaleFactorULoc, factor);
#ifdef MELONPRIME_DS
        glUniform2f(osdTexScaleULoc, 2.0f, 2.0f);
#else
        glUniform1f(osdTexScaleULoc, 2.0);
#endif

        glBindBuffer(GL_ARRAY_BUFFER, osdVertexBuffer);
        glBindVertexArray(osdVertexArray);

        glActiveTexture(GL_TEXTURE0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        glBindTexture(GL_TEXTURE_2D, logoTexture);
        glUniform2i(osdPosULoc, splashPos[3].x(), splashPos[3].y());
        glUniform2i(osdSizeULoc, kLogoWidth, kLogoWidth);
        glDrawArrays(GL_TRIANGLES, 0, 2 * 3);

#ifdef MELONPRIME_DS
        glUniform2f(osdTexScaleULoc, 1.0f, 1.0f);
#else
        glUniform1f(osdTexScaleULoc, 1.0);
#endif

        for (int i = 0; i < 3; i++)
        {
            OSDItem& item = splashText[i];

            if (!osdTextures.count(item.id))
                continue;

            glBindTexture(GL_TEXTURE_2D, osdTextures[item.id]);
            glUniform2i(osdPosULoc, splashPos[i].x(), splashPos[i].y());
            glUniform2i(osdSizeULoc, item.bitmap.width(), item.bitmap.height());
            glDrawArrays(GL_TRIANGLES, 0, 2 * 3);
        }

        glDisable(GL_BLEND);
        glUseProgram(0);

        osdMutex.unlock();
    }

#ifdef MELONPRIME_DS
    // OPT-OSD2: Skip entire GL state setup + mutex when no OSD items to draw.
    // During normal gameplay 99%+ of frames have zero items, avoiding:
    //   osdMutex lock/unlock + glUseProgram + 3x glUniform + 2x glBind
    //   + glEnable/Disable BLEND = ~500-900 cyc waste
    if (osdEnabled && !osdItems.empty())
#else
    if (osdEnabled)
#endif
    {
        osdMutex.lock();

        u32 y = kOSDMargin;

        glUseProgram(osdShader);

        glUniform2f(osdScreenSizeULoc, w, h);
        glUniform1f(osdScaleFactorULoc, factor);
#ifdef MELONPRIME_DS
        glUniform2f(osdTexScaleULoc, 1.0f, 1.0f);
#else
        glUniform1f(osdTexScaleULoc, 1.0);
#endif

        glBindBuffer(GL_ARRAY_BUFFER, osdVertexBuffer);
        glBindVertexArray(osdVertexArray);

        glActiveTexture(GL_TEXTURE0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        for (auto it = osdItems.begin(); it != osdItems.end(); )
        {
            OSDItem& item = *it;

            if (!osdTextures.count(item.id))
                continue;

            glBindTexture(GL_TEXTURE_2D, osdTextures[item.id]);
            glUniform2i(osdPosULoc, kOSDMargin, y);
            glUniform2i(osdSizeULoc, item.bitmap.width(), item.bitmap.height());
            glDrawArrays(GL_TRIANGLES, 0, 2 * 3);

            y += item.bitmap.height();
            it++;
        }

        glDisable(GL_BLEND);
        glUseProgram(0);

        osdMutex.unlock();
    }

    glContext->SwapBuffers();

#ifdef MELONPRIME_DS
    // Screen Sync: default Off (0). Effectively free when Off:
    //   - emuThread already local, GetMelonPrimeCore() is inline .get()
    //   - UNLIKELY ensures branch predictor skips the block when Off
    //   - Runs after SwapBuffers (heavy sync point), so no pipeline impact
    //   - Forced off during FastForward/SlowMo (isFastForward set by EmuThread)
    //   - DwmFlush mode is normalized away on non-Windows (Linux/macOS expose
    //     only Off/glFinish in settings)
    if (auto* core = emuThread->GetMelonPrimeCore(); core) {
        const auto ui = core->ThreadBridge().ReadForGui();
        if (UNLIKELY(ui.screenSyncMode != 0) && !ui.fastForward) {
            if (ui.screenSyncMode == 1)
                glFinish();
#ifdef _WIN32
            else if (ui.screenSyncMode == 2)
                DwmFlush();
#endif
        }
    }
#endif
}

qreal ScreenPanelGL::devicePixelRatioFromScreen() const
{
    const QScreen* screen_for_ratio = window()->windowHandle()->screen();
    if (!screen_for_ratio)
        screen_for_ratio = QGuiApplication::primaryScreen();

    return screen_for_ratio ? screen_for_ratio->devicePixelRatio() : static_cast<qreal>(1);
}

int ScreenPanelGL::scaledWindowWidth() const
{
    return std::max(static_cast<int>(std::ceil(static_cast<qreal>(width()) * devicePixelRatioFromScreen())), 1);
}

int ScreenPanelGL::scaledWindowHeight() const
{
    return std::max(static_cast<int>(std::ceil(static_cast<qreal>(height()) * devicePixelRatioFromScreen())), 1);
}

std::optional<WindowInfo> ScreenPanelGL::getWindowInfo()
{
    WindowInfo wi;

    // Windows and Apple are easy here since there's no display connection.
#if defined(_WIN32)
    wi.type = WindowInfo::Type::Win32;
    wi.window_handle = reinterpret_cast<void*>(winId());
#elif defined(__APPLE__)
    wi.type = WindowInfo::Type::MacOS;
    wi.window_handle = reinterpret_cast<void*>(winId());
#else
    const QString platform_name = QGuiApplication::platformName();

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (platform_name == QStringLiteral("xcb"))
    {
        wi.type = WindowInfo::Type::X11;
        const QX11Application* x11 = qApp->nativeInterface<QX11Application>();
        wi.display_connection = x11->display();
        wi.window_handle = reinterpret_cast<void*>(winId());
    }
#if defined(WAYLAND_ENABLED)
    else if (platform_name == QStringLiteral("wayland"))
    {
        wi.type = WindowInfo::Type::Wayland;
        const QWaylandApplication* wl = qApp->nativeInterface<QWaylandApplication>();
        if (!wl)
            return std::nullopt;

        wi.display_connection = wl->display();
#if defined(__linux__)
        // MelonPrime's Linux pointer-lock path needs the native wl_surface.
        // QPlatformNativeInterface is private Qt API and is intentionally not
        // required by BSD builds, where private QPA headers may not be packaged.
        QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
        QWindow* handle = windowHandle();
        if (!pni || !handle)
            return std::nullopt;
        wi.window_handle = pni->nativeResourceForWindow("surface", handle);
#else
        // Match upstream Qt 6.5+ behavior on BSD and other Unix platforms.
        wi.window_handle = reinterpret_cast<void*>(winId());
#endif
        if (!wi.display_connection || !wi.window_handle)
            return std::nullopt;
    }
#endif
#else
    QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
    if (platform_name == QStringLiteral("xcb"))
    {
        wi.type = WindowInfo::Type::X11;
        wi.display_connection = pni->nativeResourceForWindow("display", windowHandle());
        wi.window_handle = reinterpret_cast<void*>(winId());
    }
    else if (platform_name == QStringLiteral("wayland"))
    {
        wi.type = WindowInfo::Type::Wayland;
        QWindow* handle = windowHandle();
        if (handle == nullptr)
            return std::nullopt;

        wi.display_connection = pni->nativeResourceForWindow("display", handle);
        wi.window_handle = pni->nativeResourceForWindow("surface", handle);
    }
#endif
    else
    {
        Platform::Log(Platform::LogLevel::Error, "Unknown PNI platform %s\n", platform_name.toStdString().c_str());
        return std::nullopt;
    }
#endif

    wi.surface_width = static_cast<u32>(scaledWindowWidth());
    wi.surface_height = static_cast<u32>(scaledWindowHeight());
    wi.surface_scale = static_cast<float>(devicePixelRatioFromScreen());

    return wi;
}


QPaintEngine* ScreenPanelGL::paintEngine() const
{
    return nullptr;
}

void ScreenPanelGL::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    transferLayout();
}

void ScreenPanelGL::transferLayout()
{
    std::optional<WindowInfo> windowInfo = getWindowInfo();
    if (windowInfo.has_value())
    {
        screenSettingsLock.lock();

        if (lastScreenWidth != windowInfo->surface_width || lastScreenHeight != windowInfo->surface_height)
        {
            if (glContext)
                glContext->ResizeSurface(windowInfo->surface_width, windowInfo->surface_height);
            lastScreenWidth = windowInfo->surface_width;
            lastScreenHeight = windowInfo->surface_height;
        }

        this->windowInfo = *windowInfo;

        screenSettingsLock.unlock();
    }
}

#ifdef MELONPRIME_DS
/* MelonPrimeDS */
void ScreenPanel::unfocus()
{
    if (closing || !qApp || qApp->closingDown())
        return;

    auto* emu = emuInstance;
    auto* thread = emu ? emu->getEmuThread() : nullptr;
    auto* core = thread ? thread->GetMelonPrimeCore() : nullptr;

    if (core)
        core->ThreadBridge().SetFocusedFromGui(false);

#if defined(MELONPRIME_DS) && defined(__APPLE__)
    if (emu) {
        emu->syncMouseHotkeysFromQtButtons(QGuiApplication::mouseButtons());
        if (touching) {
            emu->releaseScreen();
            touching = false;
        }
    }
#endif

    // Focus loss is temporary. Preserve clipWanted and only release the
    // active platform capture; focus/click activation can reacquire it.
    MelonPrime::ScreenCursorPolicy::Suspend(*this);
}

void ScreenPanel::focusInEvent(QFocusEvent * event)
{
    if (auto* core = melonPrimeCore())
        core->ThreadBridge().SetFocusedFromGui(true);
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    updateClipIfNeeded();
#endif
    QWidget::focusInEvent(event);
}

void ScreenPanel::focusOutEvent(QFocusEvent * event)
{
    if (closing || !qApp || qApp->closingDown())
    {
        QWidget::focusOutEvent(event);
        return;
    }

    unfocus();
    QWidget::focusOutEvent(event);
}

void ScreenPanel::enterEvent(QEnterEvent * event)
{
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    updateClipIfNeeded();
#endif
    QWidget::enterEvent(event);
}

void ScreenPanel::moveEvent(QMoveEvent * e) {
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    updateClipIfNeeded();
#endif
#include "MelonPrimeHudScreenCppEditPanelMove.inc"
    QWidget::moveEvent(e);
}


__attribute__((always_inline)) inline void ScreenPanel::setClipWanted(bool value)
{
    if (auto* core = melonPrimeCore())
        core->ThreadBridge().SetCaptureWantedFromGui(value);
}

__attribute__((always_inline)) inline bool ScreenPanel::getClipWanted() const
{
    if (auto* core = melonPrimeCore())
        return core->ThreadBridge().ReadForGui().captureWanted;
    return false;
}

MelonPrime::MelonPrimeCore* ScreenPanel::melonPrimeCore() const
{
    auto* emu = emuInstance;
    auto* thread = emu ? emu->getEmuThread() : nullptr;
    return thread ? thread->GetMelonPrimeCore() : nullptr;
}
#endif // MELONPRIME_DS
