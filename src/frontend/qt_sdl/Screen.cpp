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
#include <vector>
#include <optional>
#include <utility>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>

#include <QPaintEvent>
#include <QPainter>
#include <QCursor>
#include <QGuiApplication>
#include <QLabel>
#include <QMetaObject>
#include <QThread>
#include <QPlatformSurfaceEvent>

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
#include "MelonPrimeMouseButton.h"
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
#include "MelonPrimeHudConfigState.h"
#include "MelonPrimeHudRuntime.h"
#include "MelonPrimeHudRadar.h"
#include "MelonPrimeHudPatchLifecycle.h"
#include "MelonPrimeHudEdit.h"
#include "MelonPrimeHudConfigOnScreenEdit.h"
#include "MelonPrimeHudScreenEditPanel.h"
#include "MelonPrimeHudScreenOverlay.h"
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

#if defined(MELONPRIME_DS) && defined(__APPLE__)
namespace {

uint8_t MelonPrimeMouseRecoveryMask(Qt::MouseButtons buttons) noexcept
{
    uint8_t mask = 0;
    for (int i = 0; i < static_cast<int>(MelonPrime::kSupportedMouseButtonCount); ++i) {
        if (buttons.testFlag(MelonPrime::kSupportedMouseButtons[
                static_cast<std::size_t>(i)]))
            mask |= static_cast<uint8_t>(1u << i);
    }
    return mask;
}

} // namespace
#endif

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

void ScreenPanel::getAimMouseDelta(std::int32_t& outDx, std::int32_t& outDy)
{
    if (auto* core = melonPrimeCore())
        core->ThreadBridge().getAimMouseDelta(outDx, outDy);
    else
        outDx = outDy = 0;
}

void ScreenPanel::resetAimMouseDelta()
{
    if (isMelonPrimeInputSurfaceAuthority()) {
        if (auto* core = melonPrimeCore())
            core->ThreadBridge().ResetPanelAimDeltaFromGui();
    }
    // Recenter, warp and layout-generation boundaries invalidate an absolute
    // baseline exactly as they invalidate the relative accumulator.
    if (m_directAim.Active())
        m_directAim.DropBaseline(MelonPrime::DirectAimBaselineReset::Lifecycle);
#if !defined(_WIN32)
    aimLastGlobalValid.store(false, std::memory_order_release);
#endif
}

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
    if (!isMelonPrimeInputSurfaceAuthority())
        return;
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
            MelonPrime::ScreenCursorPolicy::RequestAimCapture(*this);
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
#ifdef MELONPRIME_DS
    if (!isMelonPrimeInputSurfaceAuthority()) {
        event->accept();
        return;
    }
#endif
    auto* const core = melonPrimeCore();
    const int steps = core
        ? wheelSteps.Consume(
            *event, core->ThreadBridge().InputGenerationForGui())
        : wheelSteps.Consume(*event);
    if (steps != 0) {
        if (core)
            core->ThreadBridge().AddWheelFromGui(steps);
        if (emuInstance)
            emuInstance->onMouseWheel(steps);
    }
#ifdef MELONPRIME_CUSTOM_HUD
    handleHudMouseWheel(event);
#endif
    event->accept();
}

void ScreenPanel::refreshClipForGameStateChange()
{
    if (closing || !qApp || qApp->closingDown())
        return;
    if (!isMelonPrimeInputSurfaceAuthority())
        return;

    auto* const revisionCore = melonPrimeCore();
    const uint64_t workRevision = revisionCore
        ? revisionCore->ThreadBridge().GuiWorkRevisionForGui() : 0;

    // MELONPRIME_CURSOR_GUI_THREAD_DISPATCH_V2
    // drawScreen() is driven by EmuThread. QWidget cursor state, focus/window
    // queries, layout changes and Win32 cursor presentation must be reconciled
    // on the QObject's GUI thread. Coalesce to at most one queued callback.
    if (QThread::currentThread() != thread()) {
        if (m_melonPrimeGuiRevisionSeen.load(std::memory_order_acquire)
            == workRevision)
            return;
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
    auto* core = revisionCore;
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

    // Needs its own edge: the clip-state comparison below tracks inGame, while
    // the stylus match options follow cursorMode, which also covers the
    // Adventure pause. When they are all off -- the default -- this is one
    // predictable, short-circuited bool test.
    reconcileStylusMatchCursor(hasState, ui);

    const bool clipStateUnchanged =
        m_hasLastClipInGameState == hasState
        && (!hasState || m_lastClipInGameState == isInGame)
        && m_hasLastClipFocusedState == hasState
        && (!hasState || m_lastClipFocusedState == isFocused);
    const bool topScreenOnlyStateUnchanged =
        m_hasLastInGameTopScreenOnlyOverride
        && m_lastInGameTopScreenOnlyOverride == wantsInGameTopScreenOnly;

    if (clipStateUnchanged && topScreenOnlyStateUnchanged) {
        m_melonPrimeGuiRevisionSeen.store(
            workRevision, std::memory_order_release);
        return;
    }

    m_hasLastClipInGameState = hasState;
    m_lastClipInGameState = isInGame;
    m_hasLastClipFocusedState = hasState;
    m_lastClipFocusedState = isFocused;
    m_hasLastInGameTopScreenOnlyOverride = true;
    m_lastInGameTopScreenOnlyOverride = wantsInGameTopScreenOnly;

    // ROM stop/reopen and any other in-game transition invalidate an absolute
    // baseline, so returning to a match cannot replay the gap as one jump.
    if (!clipStateUnchanged && m_directAim.Active())
        m_directAim.DropBaseline(MelonPrime::DirectAimBaselineReset::Lifecycle);

    if (!topScreenOnlyStateUnchanged)
        setupScreenLayout();

#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    if (!clipStateUnchanged)
        updateClipIfNeeded();
#endif
    m_melonPrimeGuiRevisionSeen.store(
        workRevision, std::memory_order_release);
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

// Mirrors the non-stylus rule: the match-scoped cursor options apply exactly
// while the core is out of cursor mode (in a match, and not an Adventure
// pause). One reconciled edge drives them all; the decision itself stays
// in updateClipIfNeeded(), which is the single authority for cursor state.
void ScreenPanel::reconcileStylusMatchCursor(
    bool hasState, const MelonPrime::MelonPrimeUiSnapshot& ui)
{
    const bool active = stylusMatchCursorOptionsEnabled
        && hasState
        && ui.stylusMode
        && ui.focused
        && !ui.cursorMode;
    if (active == m_stylusMatchCursorActive)
        return;

    m_stylusMatchCursorActive = active;
    if (!active)
        m_stylusClickHeld = false;
    updateClipIfNeeded();
    // Park once on entry so the first drag of the match is centred too, even
    // if the pointer is never moved before it.
    if (active)
        holdStylusCursorAtCenterIfNotClicking(mapFromGlobal(QCursor::pos()));
}

// Where the pointer is parked. The confined screen when the top-screen
// confinement is on, otherwise the touch screen the stylus drags on.
QPoint ScreenPanel::stylusCursorCenterLocal() const
{
    const std::optional<QRect> target = shouldConfineCursorToTopScreenForPolicy()
        ? getTopScreenWidgetRect()
        : getBottomScreenWidgetRect();
    return target.value_or(rect()).center();
}

// Fallback parking for platforms where the cursor policy cannot pin the
// pointer at the OS level. Where it can (Windows), the pin already stops the
// pointer, so no move event with a stale position ever reaches this.
// Two member-bool tests when the option is off, and a held drag exits on the
// third -- an active drag pays nothing beyond that.
void ScreenPanel::holdStylusCursorAtCenterIfNotClicking(const QPoint& localPos)
{
    if (!stylusHoldCursorAtCenterEnabled || !m_stylusMatchCursorActive
        || m_stylusClickHeld)
        return;

    const QPoint localTarget = stylusCursorCenterLocal();
    // Our own warp lands here and would otherwise re-trigger this handler.
    if (localPos == localTarget)
        return;

    const QPoint global = mapToGlobal(localTarget);
    MelonPrime::PlatformInput_WarpCursor(global.x(), global.y());
}

std::optional<QRect> ScreenPanel::getTopScreenWidgetRectForPolicy() const
{
    return getTopScreenWidgetRect();
}

QPoint ScreenPanel::stylusCursorCenterLocalForPolicy() const
{
    return stylusCursorCenterLocal();
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

#if defined(MELONPRIME_CUSTOM_HUD) || defined(MELONPRIME_DS)
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

void ScreenPanel::requestAimCapture() {
    MelonPrime::ScreenCursorPolicy::RequestAimCapture(*this);
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
    wheelSteps.Reset();
    processMelonPrimePersistRequests();
    flushMelonPrimeConfigSave();
    closing = true;
    m_directAim.EndCapture();
    if (isMelonPrimeInputSurfaceAuthority()) {
        if (auto* core = melonPrimeCore()) {
            core->ThreadBridge().SetFocusedFromGui(false);
            core->ThreadBridge().SetPanelAvailableFromGui(false);
            core->ThreadBridge().SetCaptureWantedFromGui(false);
        }
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

bool ScreenPanel::isMelonPrimeInputSurfaceAuthority() const noexcept
{
    return emuInstance && mainWindow
        && emuInstance->getMainWindow() == mainWindow;
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

#ifdef MELONPRIME_CUSTOM_HUD
    initializeHudScreenIntegration();
#endif

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
        wheelSteps.Reset();
        processMelonPrimePersistRequests();
        flushMelonPrimeConfigSave();
        if (isMelonPrimeInputSurfaceAuthority()) {
            if (auto* core = melonPrimeCore()) {
                core->ThreadBridge().SetFocusedFromGui(false);
                core->ThreadBridge().SetPanelAvailableFromGui(false);
                core->ThreadBridge().SetCaptureWantedFromGui(false);
            }
        }
        closing = true;
        m_directAim.EndCapture();
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
#ifdef MELONPRIME_DS
    auto& metroidCfg = emuInstance->getLocalConfig();
    topScreenTouchEnabled = metroidCfg.GetBool(MelonPrime::CfgKey::StylusMode)
        && metroidCfg.GetBool(MelonPrime::CfgKey::TopScreenTouch);
    loadMelonPrimeStylusCursorConfig();
#endif
}

#ifdef MELONPRIME_DS
void ScreenPanel::refreshTopScreenTouchSetting()
{
    auto& cfg = emuInstance->getLocalConfig();
    const bool enabled = cfg.GetBool(MelonPrime::CfgKey::StylusMode)
        && cfg.GetBool(MelonPrime::CfgKey::TopScreenTouch);
    if (!enabled && topScreenTouchTransform >= 0 && touching)
    {
        emuInstance->releaseScreen();
        touching = false;
    }
    topScreenTouchEnabled = enabled;
    if (!touching)
        topScreenTouchTransform = -1;
}

void ScreenPanel::loadMelonPrimeStylusCursorConfig()
{
    auto& cfg = emuInstance->getLocalConfig();
    stylusModeEnabled = cfg.GetBool(MelonPrime::CfgKey::StylusMode);
    stylusDirectAimWhileTouchingEnabled = stylusModeEnabled
        && cfg.GetBool(MelonPrime::CfgKey::StylusDirectAimWhileTouching);
    // Tablet direct aim is opt-in so the existing Raw Mouse path remains the
    // steady-state fast path for users who do not need pen input.
    stylusDirectAimAllowTabletInputEnabled = stylusDirectAimWhileTouchingEnabled
        && cfg.GetBool(MelonPrime::CfgKey::StylusDirectAimAllowTabletInput);
    stylusHideCursorInGameEnabled = stylusModeEnabled
        && cfg.GetBool(MelonPrime::CfgKey::StylusHideCursorInGame);
    stylusConfineCursorToTopScreenEnabled = stylusModeEnabled
        && cfg.GetBool(MelonPrime::CfgKey::StylusConfineCursorToTopScreen);
    // Direct aim owns a 1px relative-input capture only while its touch action
    // is held. The traditional idle-center policy must not act between presses.
    stylusHoldCursorAtCenterEnabled = stylusModeEnabled
        && !stylusDirectAimWhileTouchingEnabled
        && cfg.GetBool(MelonPrime::CfgKey::StylusHoldCursorAtCenterWhenNotClicking);
    stylusMatchCursorOptionsEnabled = stylusHideCursorInGameEnabled
        || stylusConfineCursorToTopScreenEnabled
        || stylusHoldCursorAtCenterEnabled;
}

void ScreenPanel::refreshStylusCursorSettings()
{
    loadMelonPrimeStylusCursorConfig();

    if (!stylusDirectAimWhileTouchingEnabled
        && m_stylusDirectAimCaptureHeld)
    {
        m_stylusDirectAimMouseButton = Qt::NoButton;
        endStylusDirectAimCapture();
    }
    else if (!stylusDirectAimAllowTabletInputEnabled && m_directAim.Active())
    {
        // The option was turned off with the touch action still held. Retire
        // the tablet ingress and fall back to the mouse-only capture policy.
        m_directAim.EndCapture();
        setTabletTracking(false);
        if (m_stylusDirectAimCaptureHeld)
            requestAimCapture(); // reconciles back to CenterPin
    }

    auto* const core = melonPrimeCore();
    const bool hasState = (core != nullptr);
    const auto ui = hasState ? core->ThreadBridge().ReadForGui()
                             : MelonPrime::MelonPrimeUiSnapshot{};
    // A toggle changes what the current edge means without changing the edge
    // itself, so latch the state and re-decide unconditionally.
    m_stylusMatchCursorActive = stylusMatchCursorOptionsEnabled
        && hasState && ui.stylusMode && ui.focused && !ui.cursorMode;
    updateClipIfNeeded();
    if (m_stylusMatchCursorActive)
        holdStylusCursorAtCenterIfNotClicking(mapFromGlobal(QCursor::pos()));
}

void ScreenPanel::primeStylusTouchHotkeyAtCursor(int qtKey)
{
    if (!isMelonPrimeInputSurfaceAuthority())
        return;
    auto* const emu = emuInstance;
    auto* const core = melonPrimeCore();
    if (!emu || !core
        || !emu->hotkeyUsesKeyboardKey(HK_MetroidStylusTouch, qtKey))
    {
        return;
    }

    const auto ui = core->ThreadBridge().ReadForGui();
    if (!ui.stylusMode || !ui.inGame)
        return;

    if (stylusDirectAimWhileTouchingEnabled && !ui.cursorMode) {
        beginStylusDirectAimCapture();
        return;
    }

    QPoint local = mapFromGlobal(QCursor::pos());
    int x = local.x();
    int y = local.y();
    const bool valid = getTouchCoords(x, y, false);
    core->ThreadBridge().PublishStylusPointerFromGui(x, y, valid);
    // Merely sampling the hover coordinate must not claim a top-screen drag.
    topScreenTouchTransform = -1;
}

void ScreenPanel::releaseStylusTouchHotkeyCapture(int qtKey)
{
    auto* const emu = emuInstance;
    if (!emu || !m_stylusDirectAimCaptureHeld
        || !emu->hotkeyUsesKeyboardKey(HK_MetroidStylusTouch, qtKey))
    {
        return;
    }

    endStylusDirectAimCapture();
}

void ScreenPanel::beginStylusDirectAimCapture()
{
    if (m_stylusDirectAimCaptureHeld)
        return;

    m_stylusDirectAimCaptureHeld = true;
    m_stylusClickHeld = true;

    // Only the instance that owns the input surface may publish into a
    // ThreadBridge mailbox or install a process-wide message filter.
    auto* const core = stylusDirectAimAllowTabletInputEnabled
            && isMelonPrimeInputSurfaceAuthority()
        ? melonPrimeCore() : nullptr;
    if (core) {
        // Started before the cursor policy runs: the policy reads
        // aimConfinementForPolicy() from this panel, and that answer must
        // already be AimAreaBounds for the very first reconcile.
        QWidget* const top = window();
        m_directAim.BeginCapture(
            core->ThreadBridge(),
            top ? reinterpret_cast<void*>(top->winId()) : nullptr,
            internalWinId() ? reinterpret_cast<void*>(internalWinId())
                            : nullptr);
        // Hover movement needs tablet tracking, but only for the held capture:
        // mouse-only users, menus and normal DS touch stay event-free.
        setTabletTracking(true);
    }

    // Always taken, tablet or not: this is what gives the instance its
    // relative input device. Whether the cursor is then pinned or merely
    // bounded is decided by aimConfinementForPolicy().
    requestAimCapture();
}

void ScreenPanel::endStylusDirectAimCapture()
{
    if (!m_stylusDirectAimCaptureHeld)
        return;

    m_stylusDirectAimCaptureHeld = false;
    m_stylusClickHeld = false;
    if (m_directAim.Active()) {
        m_directAim.EndCapture();
        setTabletTracking(false);
    }
    unclip();
    // Restore any Stylus Mode hide/confine/not-clicking policy that was
    // temporarily superseded by relative aim capture.
    updateClipIfNeeded();
}
#endif

bool ScreenPanel::getTouchCoords(int& x, int& y, bool clamp)
{
#ifdef MELONPRIME_DS
    // The determinant, the epsilon test and the screen-kind test are resolved
    // in setupScreenLayout(); this is only the mapping. One unsigned compare
    // covers both index bounds, and out-of-range entries are default-invalid.
    const auto mapTopTransform = [this](int transform, int& px, int& py, bool clampCoords) {
        if (static_cast<unsigned>(transform)
            >= static_cast<unsigned>(kMaxScreenTransforms))
            return false;
        return MelonPrime::MapTopScreenTouch(
            topScreenTouchTransforms[transform], px, py, clampCoords);
    };

    // A drag that began on a top-screen transform remains owned by that same
    // transform. Otherwise the bottom-screen clamping path would steal it as
    // soon as the pointer moved.
    if (clamp && topScreenTouchTransform >= 0)
        return mapTopTransform(topScreenTouchTransform, x, y, true);
#endif

    if (layout.GetTouchCoords(x, y, clamp))
    {
#ifdef MELONPRIME_DS
        if (!clamp)
            topScreenTouchTransform = -1;
#endif
        return true;
    }

#ifdef MELONPRIME_DS
    if (topScreenTouchEnabled && !clamp)
    {
        for (int i = 0; i < numScreens; ++i)
        {
            int tx = x;
            int ty = y;
            if (mapTopTransform(i, tx, ty, false))
            {
                x = tx;
                y = ty;
                topScreenTouchTransform = i;
                return true;
            }
        }
    }
#endif

    return false;
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

#ifdef MELONPRIME_CUSTOM_HUD
    // Layout/DPI/fullscreen changes alter the output transform and invalidate
    // the retained visual frame before any backend-specific overlay path runs.
    m_hudVisualFrameValid = false;
    m_hudVisualFrameWasReused = false;
    ++m_hudVisualRendererGeneration;
#endif

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

#ifdef MELONPRIME_DS
    // SCR-PERF-002: layout generation is the recalculation authority for
    // layout-derived state. Entries past numScreens stay default-invalid, so a
    // stale transform index cannot map anything.
    for (int i = 0; i < kMaxScreenTransforms; ++i) {
        topScreenTouchTransforms[i] = (i < numScreens)
            ? MelonPrime::MakeTopScreenTouchTransform(
                  screenMatrix[i], screenKind[i] == 0)
            : MelonPrime::TopScreenTouchTransform{};
    }
#endif

    calcSplashLayout();

#ifdef MELONPRIME_CUSTOM_HUD
    updateHudScreenLayoutCache();
#endif

#ifdef MELONPRIME_DS
    // Notify layout change
    if (!closing && qApp && !qApp->closingDown()
        && isMelonPrimeInputSurfaceAuthority()) {
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
#ifdef MELONPRIME_CUSTOM_HUD
    repositionHudEditPanel(true);
#endif
    QWidget::resizeEvent(event);
}

void ScreenPanel::mousePressEvent(QMouseEvent* event)
{
    event->accept();
    auto* const emu = emuInstance;

#ifdef MELONPRIME_DS
    auto* const thr = emu->getEmuThread();
    auto* const core = isMelonPrimeInputSurfaceAuthority()
        ? thr->GetMelonPrimeCore() : nullptr;
#endif

    if (Q_UNLIKELY(!emu->emuIsActive()))
    {
        touching = false;
#ifdef MELONPRIME_DS
        topScreenTouchTransform = -1;
#endif
        return;
    }

#ifdef MELONPRIME_CUSTOM_HUD
    if (handleHudMousePress(event))
        return;
#endif

#ifdef MELONPRIME_DS
#if defined(__APPLE__)
    if (core) {
        emu->syncMouseHotkeysFromQtButtons(QGuiApplication::mouseButtons());
        const int buttonIndex = MelonPrime::MouseButtonIndex(event->button());
        if (buttonIndex >= 0
            && (emu->mouseRecoveryEligibleMask()
                & static_cast<uint8_t>(1u << buttonIndex)) != 0)
            m_mouseRecoveryArmedMask |= static_cast<uint8_t>(1u << buttonIndex);
    }
#endif
    // Click sets focus
    if (core) core->ThreadBridge().SetFocusedFromGui(true);

    if (core)
        emu->onMousePress(event);
#endif

#ifdef MELONPRIME_DS
    // Mouse aim mode logic
    const auto ui = core ? core->ThreadBridge().ReadForGui()
                         : MelonPrime::MelonPrimeUiSnapshot{};
    // During a Stylus Mode match the configured touch-contact binding owns
    // mouse touch activation. Outside that narrow case, retain the normal DS
    // UI behavior where the physical left button touches the screen.
    if (core && ui.stylusMode && ui.inGame) {
        if (!emu->hotkeyUsesMouseButton(HK_MetroidStylusTouch, event->button()))
            return;
        if (stylusDirectAimWhileTouchingEnabled && !ui.cursorMode) {
            m_stylusDirectAimMouseButton = event->button();
            beginStylusDirectAimCapture();
            return;
        }
    }
    else if (event->button() != Qt::LeftButton) {
        return;
    }

    if (core && !ui.stylusMode && ui.inGame)
    {
        // If not in cursor mode (aim mode), treat click as returning to aim (clip)
        if (!ui.cursorMode)
        {
            requestAimCapture();
            return;
        }
        // If isCursorMode == true, proceed to standard touch processing
    }
    m_touchMouseButton = event->button();
#else
    if (event->button() != Qt::LeftButton)
        return;
#endif

    const QPoint p = event->pos();
    int x = p.x();
    int y = p.y();

    if (getTouchCoords(x, y, false))
    {
        touching = true;
        emu->touchScreen(x, y);
#ifdef MELONPRIME_DS
        if (core && ui.stylusMode && ui.inGame)
            core->ThreadBridge().PublishStylusPointerFromGui(x, y, true);
#endif
    }
#ifdef MELONPRIME_DS
    else if (core && ui.stylusMode && ui.inGame) {
        core->ThreadBridge().PublishStylusPointerFromGui(0, 0, false);
    }
#endif

#ifdef MELONPRIME_DS
    // If not in cursor mode, re-clip
    if (core && !ui.stylusMode && !ui.cursorMode)
    {
        requestAimCapture();
    }
    // The click is held now, so the not-clicking pin no longer applies:
    // re-decide so the drag is free to move. Latched even when the touch did
    // not register, otherwise a press outside the touch area would leave the
    // pointer pinned with no way to drag.
    if (!m_stylusClickHeld)
    {
        m_stylusClickHeld = true;
        if (stylusHoldCursorAtCenterEnabled && m_stylusMatchCursorActive)
            updateClipIfNeeded();
    }
#endif
}

void ScreenPanel::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();

    auto* const emu = emuInstance;

#ifdef MELONPRIME_DS
#if defined(__APPLE__)
    if (isMelonPrimeInputSurfaceAuthority()) {
        const int buttonIndex = MelonPrime::MouseButtonIndex(event->button());
        if (buttonIndex >= 0)
            m_mouseRecoveryArmedMask &= static_cast<uint8_t>(~(1u << buttonIndex));
    }
#endif
    if (isMelonPrimeInputSurfaceAuthority())
        emu->onMouseRelease(event);

    if (m_stylusDirectAimMouseButton != Qt::NoButton
        && event->button() == m_stylusDirectAimMouseButton)
    {
        m_stylusDirectAimMouseButton = Qt::NoButton;
        endStylusDirectAimCapture();
        return;
    }
#endif

    if (Q_UNLIKELY(!emu->emuIsActive()))
    {
        touching = false;
#ifdef MELONPRIME_DS
        topScreenTouchTransform = -1;
#endif
        return;
    }

#ifdef MELONPRIME_CUSTOM_HUD
    if (handleHudMouseRelease(event))
        return;
#endif

#ifdef MELONPRIME_DS
    const bool releasesAcceptedTouch = (m_touchMouseButton != Qt::NoButton)
        ? event->button() == m_touchMouseButton
        : event->button() == Qt::LeftButton;
    if (!releasesAcceptedTouch)
        return;
    m_touchMouseButton = Qt::NoButton;
#else
    if (event->button() != Qt::LeftButton)
        return;
#endif

#ifdef MELONPRIME_DS
    // Ahead of the `touching` early-out below: a press whose touch never
    // registered still has to end the held-click window. This is the first
    // moment of the not-clicking window, so park the pointer and re-decide to
    // have it pinned there for the rest of it.
    if (m_stylusClickHeld)
    {
        m_stylusClickHeld = false;
        if (stylusHoldCursorAtCenterEnabled && m_stylusMatchCursorActive)
        {
            holdStylusCursorAtCenterIfNotClicking(event->pos());
            updateClipIfNeeded();
        }
    }
#endif

    if (!touching)
        return;

    touching = false;
#ifdef MELONPRIME_DS
    topScreenTouchTransform = -1;
#endif
    emu->releaseScreen();
}

void ScreenPanel::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();

    auto* const emu = emuInstance;

    if (Q_UNLIKELY(!emu->emuIsActive()))
        return;

#if defined(MELONPRIME_DS) && defined(__APPLE__)
    if (isMelonPrimeInputSurfaceAuthority()) {
        m_mouseRecoveryArmedMask &= emu->mouseRecoveryEligibleMask();
        if (m_mouseRecoveryArmedMask != 0) {
            const auto physicalButtons = QGuiApplication::mouseButtons();
            emu->syncMouseHotkeysFromQtButtons(physicalButtons);
            m_mouseRecoveryArmedMask = MelonPrimeMouseRecoveryMask(physicalButtons)
                & emu->mouseRecoveryEligibleMask();
        }
    }
#endif

#ifdef MELONPRIME_CUSTOM_HUD
    if (handleHudMouseMove(event))
        return;
#endif

#ifdef MELONPRIME_DS
    // SCR-PERF-001. Qt mouse events arrive at the device's report rate, so the
    // snapshot is taken only when something below actually reads it.
    //
    // On Windows, relative aim is owned by Raw Input and the platform aim
    // route below compiles to nothing; the only consumers left in this handler
    // are Stylus Mode's hover and drag publication, and every one of them is
    // already gated on ui.stylusMode. With Stylus Mode off -- the default and
    // the common case -- resolving the owner and reading the snapshot cannot
    // change what this handler does, so both are skipped.
    //
    // macOS and Linux always take it: their Qt fallback aim route reads the
    // snapshot on every event, whatever Stylus Mode is set to.
    //
    // The gate is the cold-cached config value, not the runtime snapshot it
    // would otherwise have to read. Turning Stylus Mode off can therefore skip
    // publication for the few frames before the emulation thread applies the
    // same change, which is the direction where publication no longer matters.
#ifdef _WIN32
    const bool melonPrimeWantsUiSnapshot = stylusModeEnabled;
#else
    constexpr bool melonPrimeWantsUiSnapshot = true;
#endif
    auto* const thread = melonPrimeWantsUiSnapshot
        ? emu->getEmuThread() : nullptr;
    auto* const core = (thread && isMelonPrimeInputSurfaceAuthority())
        ? thread->GetMelonPrimeCore() : nullptr;
    const auto ui = core ? core->ThreadBridge().ReadForGui()
                         : MelonPrime::MelonPrimeUiSnapshot{};
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY) \
    && (defined(__linux__) || defined(__APPLE__))
    // MELONPRIME_INPUT_DEBUG=1: 1 Hz gate/event diagnostics for the Qt aim path.
    static const bool s_aimDbg = getenv("MELONPRIME_INPUT_DEBUG") != nullptr;
    if (Q_UNLIKELY(s_aimDbg)) {
        static int s_events = 0, s_blocked = 0;
        static qint64 s_lastLog = 0;
        const bool relativeAimMode = core
            && (!ui.stylusMode
                || (stylusDirectAimWhileTouchingEnabled
                    && ui.captureWanted));
        const bool gateOk = relativeAimMode
            && ui.focused
            && !ui.cursorMode && ui.inGame;
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
        && (!ui.stylusMode
            || (stylusDirectAimWhileTouchingEnabled
                && ui.captureWanted))
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

#ifdef MELONPRIME_DS
    holdStylusCursorAtCenterIfNotClicking(event->pos());

    // Keep a coherent DS coordinate available even when the configured touch
    // action is a keyboard or joystick button rather than a physical click.
    if (!touching && core && ui.stylusMode && ui.inGame
        && !(stylusDirectAimWhileTouchingEnabled && ui.captureWanted))
    {
        const QPoint p = event->pos();
        int x = p.x();
        int y = p.y();
        const bool valid = getTouchCoords(x, y, false);
        core->ThreadBridge().PublishStylusPointerFromGui(x, y, valid);
        // Hover tracking must not claim a drag transform before a contact
        // actually starts.
        topScreenTouchTransform = -1;
        return;
    }
#endif

    if (!touching)
        return;

    const QPoint p = event->pos();
    int x = p.x();
    int y = p.y();

    if (getTouchCoords(x, y, true))
    {
        emu->touchScreen(x, y);
#ifdef MELONPRIME_DS
        if (core && ui.stylusMode && ui.inGame)
            core->ThreadBridge().PublishStylusPointerFromGui(x, y, true);
#endif
    }
}


void ScreenPanel::tabletEvent(QTabletEvent* event)
{
    event->accept();
    if (!emuInstance->emuIsActive())
    {
        touching = false;
#ifdef MELONPRIME_DS
        topScreenTouchTransform = -1;
        if (m_directAim.Active())
            m_directAim.DropBaseline(
                MelonPrime::DirectAimBaselineReset::Lifecycle);
#endif
        return;
    }

#ifdef MELONPRIME_DS
    // One coherent gate per event: the capture latch already encodes stylus
    // mode, direct aim, in-game and non-cursor-mode state from its press edge,
    // and Active() encodes the opt-in plus a live GUI mailbox.
    if (Q_UNLIKELY(m_directAim.Active()))
    {
        switch (event->type())
        {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        {
#if QT_VERSION_MAJOR == 6
            const QPointF global = event->globalPosition();
            const auto* const device = event->pointingDevice();
            const std::uint64_t penId = device
                ? static_cast<std::uint64_t>(device->uniqueId().numericId())
                : 0u;
#else
            const QPointF global = QPointF(event->globalPos());
            const std::uint64_t penId =
                static_cast<std::uint64_t>(event->uniqueId());
#endif
            // Pen contact is not required: the touch action is what gates
            // direct aim, so hover movement aims exactly like a mouse.
            (void)m_directAim.SubmitAbsolute(
                MelonPrime::DirectAimHostSource::QtTablet,
                penId, global.x(), global.y());
            break;
        }
        case QEvent::TabletRelease: {
#if QT_VERSION_MAJOR == 6
            const auto* const device = event->pointingDevice();
            const std::uint64_t penId = device
                ? static_cast<std::uint64_t>(device->uniqueId().numericId())
                : 0u;
#else
            const std::uint64_t penId =
                static_cast<std::uint64_t>(event->uniqueId());
#endif
            // Contact-up ends the coordinate segment but hover remains the
            // same Qt source. An unrelated device release is ignored.
            (void)m_directAim.DropBaselineForSource(
                MelonPrime::DirectAimHostSource::QtTablet,
                penId,
                MelonPrime::DirectAimBaselineReset::PointerLeave);
            break;
        }
        default:
            break;
        }
        // Direct aim owns this stream; DS touch must not also be driven, and
        // no release is synthesized for a contact that was never sent.
        return;
    }
#endif

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

        if (getTouchCoords(x, y, event->type() == QEvent::TabletMove))
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
#ifdef MELONPRIME_DS
            topScreenTouchTransform = -1;
#endif
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
    if (!emuInstance->emuIsActive())
    {
        touching = false;
#ifdef MELONPRIME_DS
        topScreenTouchTransform = -1;
#endif
        return;
    }

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

            if (getTouchCoords(x, y, event->type() == QEvent::TouchUpdate))
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
#ifdef MELONPRIME_DS
            topScreenTouchTransform = -1;
#endif
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
    waylandPointerLock = std::make_unique<MelonPrime::WaylandPointerLock>();
#endif
}

ScreenPanelNative::~ScreenPanelNative()
{
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    if (waylandPointerLock) {
        waylandPointerLock->setLocked(nullptr, nullptr, false);
        waylandPointerLock->setDeltaTarget(nullptr);
    }
#endif
}

#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
bool ScreenPanelNative::setWaylandPointerLockForMelonPrime(bool enabled)
{
    if (!waylandPointerLock)
        return false;

    if (!enabled) {
        const bool result = waylandPointerLock->setLocked(nullptr, nullptr, false);
        waylandPointerLock->setDeltaTarget(nullptr);
        return result;
    }

    auto* const core = isMelonPrimeInputSurfaceAuthority()
        ? melonPrimeCoreForPolicy() : nullptr;
    if (!core) {
        waylandPointerLock->setDeltaTarget(nullptr);
        return false;
    }

    // Not a Qt::WA_NativeWindow: lock the top-level window's surface instead
    // of our own (we have none).
    QWindow* const topLevelHandle = window() ? window()->windowHandle() : nullptr;
    const auto handles = ResolveMelonPrimeWaylandHandles(topLevelHandle);
    if (!handles.has_value()) {
        waylandPointerLock->setDeltaTarget(nullptr);
        return false;
    }

    // Hint the panel's own center, expressed in the locked (top-level)
    // surface's local coordinates, so the compositor recenters the cursor
    // away from any edge whenever this lock later releases.
    const QPoint hint = window() ? mapTo(window(), rect().center()) : rect().center();
    waylandPointerLock->setDeltaTarget(&core->ThreadBridge());
    const bool result = waylandPointerLock->setLocked(
        handles->first, handles->second, true, hint.x(), hint.y());
    if (!result)
        waylandPointerLock->setDeltaTarget(nullptr);
    return result;
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
        const auto renderLockWaitStart = m_nativePaintPerf.Now();
        emuInstance->renderLock.lock();
        const auto renderLockAcquired = m_nativePaintPerf.Now();
        m_nativePaintPerf.Record(
            MelonPrime::NativePaintMetric::RenderLockWait,
            renderLockWaitStart, renderLockAcquired);
        const auto renderLockHoldStart = renderLockAcquired;

        const auto framebufferCopyStart = m_nativePaintPerf.Now();
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
        m_nativePaintPerf.Record(
            MelonPrime::NativePaintMetric::FramebufferCopy,
            framebufferCopyStart, m_nativePaintPerf.Now());

        QRect screenrc(0, 0, 256, 192);

        const auto qpaintGameStart = m_nativePaintPerf.Now();
        for (int i = 0; i < numScreens; i++)
        {
            painter.setTransform(screenTrans[i]);
            painter.drawImage(screenrc, screen[screenKind[i]]);
        }
        m_nativePaintPerf.Record(
            MelonPrime::NativePaintMetric::QPaintGame,
            qpaintGameStart, m_nativePaintPerf.Now());

#define MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE (&screen[1])
        const auto hudSoftwareStart = m_nativePaintPerf.Now();
#include "MelonPrimeHudScreenCppOverlayOfSoftware.inc"
        m_nativePaintPerf.Record(
            MelonPrime::NativePaintMetric::HudSoftware,
            hudSoftwareStart, m_nativePaintPerf.Now());
#undef MELONPRIME_HUD_BOTTOM_SCREEN_IMAGE

        m_nativePaintPerf.Record(
            MelonPrime::NativePaintMetric::RenderLockHold,
            renderLockHoldStart, m_nativePaintPerf.Now());
        emuInstance->renderLock.unlock();
        m_nativePaintPerf.MaybeReport(emuInstance->getInstanceID());
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
    waylandPointerLock = std::make_unique<MelonPrime::WaylandPointerLock>();
#endif
}

ScreenPanelGL::~ScreenPanelGL()
{
#if defined(__linux__) && defined(MELONPRIME_ENABLE_WAYLAND_POINTER_LOCK)
    if (waylandPointerLock) {
        waylandPointerLock->setLocked(nullptr, nullptr, false);
        waylandPointerLock->setDeltaTarget(nullptr);
    }
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

#ifdef MELONPRIME_CUSTOM_HUD
    initializeHudOpenGL();
#endif

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

#ifdef MELONPRIME_CUSTOM_HUD
    deinitializeHudOpenGL();
#endif

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

    if (!enabled) {
        const bool result = waylandPointerLock->setLocked(nullptr, nullptr, false);
        waylandPointerLock->setDeltaTarget(nullptr);
        return result;
    }

    auto* const core = isMelonPrimeInputSurfaceAuthority()
        ? melonPrimeCoreForPolicy() : nullptr;
    if (!core) {
        waylandPointerLock->setDeltaTarget(nullptr);
        return false;
    }

    // Lock the top-level window's surface, not this panel's own
    // Qt::WA_NativeWindow subsurface (used by getWindowInfo() for GL context
    // creation). Locking the child subsurface directly made KWin immediately
    // fire WindowDeactivate on the main window in windowed (non-fullscreen)
    // mode, which our own Suspend() path read as focus loss and tore the lock
    // right back down -- a lock/unlock churn whose brief unlocked gaps let
    // fast mouse motion escape the window (see issue #526).
    QWindow* const topLevelHandle = window() ? window()->windowHandle() : nullptr;
    const auto handles = ResolveMelonPrimeWaylandHandles(topLevelHandle);
    if (!handles.has_value()) {
        waylandPointerLock->setDeltaTarget(nullptr);
        return false;
    }

    // Hint the panel's own center, expressed in the locked (top-level)
    // surface's local coordinates, so the compositor recenters the cursor
    // away from any edge whenever this lock later releases.
    const QPoint hint = window() ? mapTo(window(), rect().center()) : rect().center();
    waylandPointerLock->setDeltaTarget(&core->ThreadBridge());
    const bool result = waylandPointerLock->setLocked(
        handles->first, handles->second, true, hint.x(), hint.y());
    if (!result)
        waylandPointerLock->setDeltaTarget(nullptr);
    return result;
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

    wheelSteps.Reset();
    auto* emu = emuInstance;
    auto* thread = emu ? emu->getEmuThread() : nullptr;
    auto* core = thread ? thread->GetMelonPrimeCore() : nullptr;

    if (core && isMelonPrimeInputSurfaceAuthority())
        core->ThreadBridge().SetFocusedFromGui(false);

    if (m_stylusDirectAimCaptureHeld) {
        m_stylusDirectAimMouseButton = Qt::NoButton;
        endStylusDirectAimCapture();
    }

#if defined(MELONPRIME_DS) && defined(__APPLE__)
    if (emu) {
        const auto physicalButtons = QGuiApplication::mouseButtons();
        emu->syncMouseHotkeysFromQtButtons(physicalButtons);
        if (isMelonPrimeInputSurfaceAuthority())
            m_mouseRecoveryArmedMask = MelonPrimeMouseRecoveryMask(physicalButtons)
                & emu->mouseRecoveryEligibleMask();
        if (touching) {
            emu->releaseScreen();
            touching = false;
            topScreenTouchTransform = -1;
        }
        // The matching release can be delivered to whoever took focus, so the
        // held-click latch is cleared here as well.
        m_stylusClickHeld = false;
    }
#endif

    // Normal mouse aim preserves clipWanted across temporary focus loss.
    // A held-only Stylus direct-aim capture was explicitly cleared above so
    // focus return cannot reacquire it without a fresh touch-action press.
    MelonPrime::ScreenCursorPolicy::Suspend(*this);
}

void ScreenPanel::focusInEvent(QFocusEvent * event)
{
    if (isMelonPrimeInputSurfaceAuthority()) {
        if (auto* core = melonPrimeCore())
            core->ThreadBridge().SetFocusedFromGui(true);
    }
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
#ifdef MELONPRIME_CUSTOM_HUD
    repositionHudEditPanel(false);
#endif
    QWidget::moveEvent(e);
}


__attribute__((always_inline)) inline void ScreenPanel::setClipWanted(bool value)
{
    if (isMelonPrimeInputSurfaceAuthority()) {
        if (auto* core = melonPrimeCore())
            core->ThreadBridge().SetCaptureWantedFromGui(value);
    }
}

__attribute__((always_inline)) inline bool ScreenPanel::getClipWanted() const
{
    if (!isMelonPrimeInputSurfaceAuthority())
        return false;
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
