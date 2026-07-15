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

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <cstdarg>

#include <optional>
#include <vector>
#include <string>
#include <algorithm>

#include <SDL2/SDL.h>

#include "main.h"
#include "types.h"
#include "version.h"
#include "MelonPrimeBuildInfo.h"
#include "ScreenLayout.h"
#include "Args.h"
#include "NDS.h"
#include "NDSCart.h"
#include "GBACart.h"
#include "GPU.h"
#include "SPU.h"
#include "Wifi.h"
#include "Platform.h"
#include "LocalMP.h"
#include "Config.h"
#include "RTC.h"
#include "DSi.h"
#include "DSi_I2C.h"
#include "Savestate.h"
#include "EmuInstance.h"

#include "MelonPrimeEmuThreadIncludes.inc"

#ifdef MELONPRIME_DS
static void RomBootTrace(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}
#else
#define RomBootTrace(...) ((void)0)
#endif

using namespace melonDS;

EmuThread::EmuThread(EmuInstance* inst, QObject* parent) : QThread(parent)
{
    emuInstance = inst;
    emuStatus = emuStatus_Paused;
    emuPauseStack = emuPauseStackRunning;
    emuActive = false;

#include "MelonPrimeEmuThreadConstructor.inc"
}

EmuThread::~EmuThread()
{

}

void EmuThread::attachWindow(MainWindow* window)
{
    connect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
    connect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
    connect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
    connect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
    connect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
    connect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
    connect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
    connect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

    if (window->winHasMenu())
    {
        connect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
        connect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
    }
}

void EmuThread::detachWindow(MainWindow* window)
{
    disconnect(this, SIGNAL(windowTitleChange(QString)), window, SLOT(onTitleUpdate(QString)));
    disconnect(this, SIGNAL(windowEmuStart()), window, SLOT(onEmuStart()));
    disconnect(this, SIGNAL(windowEmuStop()), window, SLOT(onEmuStop()));
    disconnect(this, SIGNAL(windowEmuPause(bool)), window, SLOT(onEmuPause(bool)));
    disconnect(this, SIGNAL(windowEmuReset()), window, SLOT(onEmuReset()));
    disconnect(this, SIGNAL(autoScreenSizingChange(int)), window->panel, SLOT(onAutoScreenSizingChanged(int)));
    disconnect(this, SIGNAL(windowFullscreenToggle()), window, SLOT(onFullscreenToggled()));
    disconnect(this, SIGNAL(screenEmphasisToggle()), window, SLOT(onScreenEmphasisToggled()));

    if (window->winHasMenu())
    {
        disconnect(this, SIGNAL(windowLimitFPSChange()), window->actLimitFramerate, SLOT(trigger()));
        disconnect(this, SIGNAL(swapScreensToggle()), window->actScreenSwap, SLOT(trigger()));
    }
}


void EmuThread::run()
{
    Config::Table& globalCfg = emuInstance->getGlobalConfig();
    u32 mainScreenPos[3];

#include "MelonPrimeEmuThreadRunSetup.inc"

    mainScreenPos[0] = 0;
    mainScreenPos[1] = 0;
    mainScreenPos[2] = 0;
    autoScreenSizing = 0;

#ifdef MELONPRIME_DS
    // Metal-plan Phase 8 fix: `videoRenderer` must reflect the *requested*
    // renderer (normalized for the current platform/backend), not be forced
    // to Software whenever `useOpenGL` is false. Before this fix, a renderer
    // that does not need an OpenGL context (Metal, or Software itself) was
    // silently discarded here and replaced with 0 (Software) -- harmless for
    // Software, but it meant a real `renderer3D_Metal` selection could never
    // reach EmuThread::updateRenderer() through the normal (non-env-forced)
    // path once Phase 9 UI exists. `useOpenGL` stays purely "does the current
    // presentation backend need a GL context" -- it must not gate which 3D
    // renderer identity is requested.
    {
        const int requestedRenderer = globalCfg.GetInt("3D.Renderer");
        videoRenderer = MelonPrime::VideoBackend::NormalizeRendererForPlatform(requestedRenderer);
        videoBackend = MelonPrime::VideoBackend::ResolvePresentationBackend(
            globalCfg.GetBool("Screen.UseGL"), requestedRenderer);
        useOpenGL = MelonPrime::VideoBackend::IsOpenGLPresentation(videoBackend);
    }
    if (useOpenGL)
        emuInstance->initOpenGL(0);
#else
    useOpenGL = emuInstance->usesOpenGL();
    if (useOpenGL)
    {
        emuInstance->initOpenGL(0);
        videoRenderer = globalCfg.GetInt("3D.Renderer");
    }
    else
    {
        videoRenderer = 0;
    }
#endif

    videoSettingsDirty = true;

    u32 nframes = 0;
    double perfCountsSec = 1.0 / SDL_GetPerformanceFrequency();
    // P-40: Store frequency directly for tick conversion (multiplication vs division).
    // targetTime / perfCountsSec = targetTime * perfCountsFreq.
    // DIVSD (~20-35 cyc) → MULSD (~3-5 cyc) in the frame limiter spin setup.
    double perfCountsFreq = static_cast<double>(SDL_GetPerformanceFrequency());
    double lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
    double frameLimitError = 0.0;
    double lastMeasureTime = lastTime;

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    // Developer-only: per-frame interval statistics (jitter visualization).
    // Reflected into the window title alongside FPS so P-12's hybrid
    // Sleep+Spin behavior can be eyeballed without any extra UI.
    const Uint64 perfFreqDev = SDL_GetPerformanceFrequency();
    Uint64 prevFrameTickDev = SDL_GetPerformanceCounter();
    double frameMsMinDev = 1.0e9;
    double frameMsMaxDev = 0.0;
    double frameMsSumDev = 0.0;
    u32    frameMsCountDev = 0;
    bool   frameMsPrimedDev = false; // skip first interval (start-of-run outlier)
#endif

    u32 winUpdateCount = 0, winUpdateFreq = 1;
    u8 dsiVolumeLevel = 0x1F;

    char melontitle[160];

    bool fastforward = false;
    bool slowmo = false;
    emuInstance->fastForwardToggled = false;
    emuInstance->slowmoToggled = false;

#include "MelonPrimeEmuThreadFrameState.inc"

    // --- Frame Advance (lambda so MelonPrime can call it externally) ---
    auto frameAdvanceOnce = [&]() {
#ifdef MELONPRIME_DS
        MelonPrimePerf::FrameBegin();
#endif

#ifdef MELONPRIME_DS
        // =================================================================
        // P-13: Late-Poll Frame Limiter — sleep BEFORE input, not after.
        //
        // Original order: Poll → RunFrame → Render → Sleep
        // New order:      Sleep → Poll → RunFrame → Render
        //
        // Why: raw input (mouse/kb) is polled in RunFrameHook, which now
        // runs immediately after the sleep. This means input is the freshest
        // possible before RunFrame consumes it. With the old order, input
        // polled before sleep would sit for 0-11ms during the sleep.
        //
        // With VSync OFF: Render happens near the end of the frame period,
        // aligning with the next display refresh — reducing display latency
        // by up to 11ms compared to rendering early and waiting.
        //
        // P-12: Precision Hybrid Sleep+Spin Limiter
        // SDL_Delay has ±1-15ms jitter (depending on timer resolution).
        // Hybrid: SDL_Delay for bulk wait, then QPC spin for sub-ms precision.
        // Combined with P-11 (NtSetTimerResolution 0.5ms): jitter drops from
        // ±15ms to ±0.03ms.
        // =================================================================
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (ConsumeVulkanLimiterPhaseResetRequest())
        {
            isFirstLimiterFrame = true;
            frameLimitError = 0.0;
            storedFrametimeStep = 1.0 / std::max(emuInstance->targetFPS, 1.0);
            lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
        }
#endif
        if (emuInstance->doLimitFPS && !isFirstLimiterFrame)
        {
            double curtime = SDL_GetPerformanceCounter() * perfCountsSec;
            double elapsed = curtime - lastTime;

            frameLimitError += storedFrametimeStep - elapsed;
            if (frameLimitError < -storedFrametimeStep)
                frameLimitError = -storedFrametimeStep;
            if (frameLimitError > storedFrametimeStep)
                frameLimitError = storedFrametimeStep;

            if (frameLimitError > 0.0001)   // > 0.1ms
            {
                double targetTime = curtime + frameLimitError;

                // Coarse sleep: SDL_Delay with a platform-specific safety margin
                // before the QPC spin phase (P-12). Windows keeps 1.0ms for
                // NtSetTimerResolution(0.5ms). Non-Windows uses 0.5ms (V5 Phase 3)
                // until Phase 0 ROM baselines validate a tighter value.
#ifdef _WIN32
                constexpr double kCoarseSleepMarginMs = 1.0;
#else
                constexpr double kCoarseSleepMarginMs = 0.5;
#endif
                double coarseMs = frameLimitError * 1000.0 - kCoarseSleepMarginMs;
                MelonPrimePerf::SectionBegin(MelonPrimePerf::Section::LimiterSleep);
                if (coarseMs > 0.5)
                    SDL_Delay(static_cast<Uint32>(coarseMs));
                MelonPrimePerf::SectionEnd(MelonPrimePerf::Section::LimiterSleep);

                // P-27: Integer spin comparison.
                // Old: SDL_GetPerformanceCounter() * perfCountsSec < targetTime
                //   → float multiply per iteration (~5ns × 33k iterations = ~165μs)
                // New: SDL_GetPerformanceCounter() < targetTick
                //   → pure integer compare (0ns overhead)
                //
                // P-40: targetTime / perfCountsSec → targetTime * perfCountsFreq.
                // DIVSD (~20-35 cyc) → MULSD (~3-5 cyc).
                //
                // P-45: Capture final tick from spin loop condition.
                // The loop's last evaluation already holds a tick >= targetTick.
                // Reuse it instead of calling SDL_GetPerformanceCounter() again.
                // Saves 1 QPC call (~20-40 cyc) per frame when limiting is active.
                const Uint64 targetTick = static_cast<Uint64>(targetTime * perfCountsFreq);
                Uint64 curTick;
                MelonPrimePerf::SectionBegin(MelonPrimePerf::Section::LimiterSpin);
                while ((curTick = SDL_GetPerformanceCounter()) < targetTick) {
#ifdef _WIN32
                    YieldProcessor();
#elif defined(__x86_64__) || defined(__i386__)
                    __builtin_ia32_pause();
#elif defined(__aarch64__)
                    asm volatile("yield");
#endif
                }
                MelonPrimePerf::SectionEnd(MelonPrimePerf::Section::LimiterSpin);

                curtime = static_cast<double>(curTick) * perfCountsSec;
                frameLimitError = targetTime - curtime;  // residual overshoot
            }

            lastTime = curtime;
        }
        else
        {
            lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
            isFirstLimiterFrame = false;
        }
#endif // MELONPRIME_DS

#ifdef MELONPRIME_DS
        // =================================================================
        // P-15: Late-Poll Joystick — refresh SDL state after Sleep.
        //
        // inputProcess() already ran at the main loop top (for edge
        // detection: hotkeyPress/Release). This lightweight refresh
        // re-polls joystick axes/buttons so RunFrameHook sees fresh
        // joyHotkeyMask and inputMask. Edge detection is untouched.
        //
        // P-33: PrePollRawInput removed (P-19 HiddenWndProc captures WM_INPUT at dispatch).
        MelonPrimePerf::SectionBegin(MelonPrimePerf::Section::Input);
        emuInstance->inputRefreshJoystickState();
#endif

        // RTC sync
        emuInstance->syncRTC();

        // emulate
        u32 nlines;

#ifdef MELONPRIME_DS
        // P-39: Skip NeedsShaderCompile virtual dispatch once shaders are ready.
        // GetRenderer().NeedsShaderCompile() is a vtable lookup + indirect call
        // (~15-25 cyc) that returns false 100% of the time after initial compile.
        bool needsCompile = UNLIKELY(!shadersReady)
            && emuInstance->nds->GPU.GetRenderer().NeedsShaderCompile();
#else
        // NeedsShaderCompile reads a GPU flag — no GL context needed.
        bool needsCompile = emuInstance->nds->GPU.GetRenderer().NeedsShaderCompile();
#endif

#ifdef MELONPRIME_DS
        // =================================================================
        // P-28: RunFrameHook BEFORE makeCurrentGL.
        //
        // RunFrameHook only writes to DS memory (aimX/Y, inputMask, etc.)
        // and reads raw input — zero GL dependency. Moving it before
        // wglMakeCurrent eliminates 50-200μs of kernel transition from
        // the input→game latency path.
        //
        // Old: PrePoll → SDL → makeCurrentGL(50-200μs) → RunFrameHook → RunFrame
        // New: PrePoll → SDL → RunFrameHook → makeCurrentGL → RunFrame
        // =================================================================
        if (LIKELY(!needsCompile)) {
            melonPrime->RunFrameHook();
            emuInstance->nds->SetKeyMask(melonPrime->GetInputMaskFast());
        }
        MelonPrimePerf::SectionEnd(MelonPrimePerf::Section::Input);
#endif

        if (useOpenGL)
            emuInstance->makeCurrentGL();

        // update render settings if needed
        if (videoSettingsDirty)
        {
#ifdef MELONPRIME_DS
            if (!useOpenGL)
            {
                videoBackend = MelonPrime::VideoBackend::ResolvePresentationBackend(
                    globalCfg.GetBool("Screen.UseGL"), globalCfg.GetInt("3D.Renderer"));
                if (MelonPrime::VideoBackend::IsOpenGLPresentation(videoBackend))
                    videoBackend = MelonPrime::VideoBackend::FromLegacyOpenGLFlag(false);
            }
#endif
            emuInstance->renderLock.lock();
            if (useOpenGL)
            {
#ifdef MELONPRIME_DS
                emuInstance->setVSyncGL(globalCfg.GetBool("Screen.VSync"));
#else
                emuInstance->setVSyncGL(true);
                videoRenderer = globalCfg.GetInt("3D.Renderer");
#endif
            }
#ifdef OGLRENDERER_ENABLED
            else
            {
#ifdef MELONPRIME_DS
                // `videoRenderer` is the runtime-actual backend. Keep it
                // untouched until applyRendererCreation()/capability refresh
                // has evaluated the complete presentation pipeline.
#else
                videoRenderer = 0;
#endif
            }
#else
            {
                videoRenderer = 0;
            }
#endif

            updateRenderer();

#ifdef MELONPRIME_DS
            // P-39 fix: Reset shadersReady so the new renderer's
            // NeedsShaderCompile() is actually checked.
            shadersReady = false;
#endif

            videoSettingsDirty = false;
            emuInstance->renderLock.unlock();
        }

#ifndef MELONPRIME_DS
        // process input and hotkeys
        emuInstance->nds->SetKeyMask(emuInstance->inputMask);

        if (emuInstance->isTouching)
            emuInstance->nds->TouchScreen(emuInstance->touchX, emuInstance->touchY);
        else
            emuInstance->nds->ReleaseScreen();

        if (emuInstance->hotkeyPressed(HK_Lid))
        {
            bool lid = !emuInstance->nds->IsLidClosed();
            emuInstance->nds->SetLidClosed(lid);
            emuInstance->osdAddMessage(0, lid ? "Lid closed" : "Lid opened");
        }
#endif // MELONPRIME_DS

#ifdef MELONPRIME_DS
        MelonPrimePerf::SectionBegin(MelonPrimePerf::Section::RunFrame);
#endif
        if (UNLIKELY(needsCompile))
        {
            compileShaders();
            nlines = 1;
#ifdef MELONPRIME_DS
            // P-39: Once shaders finish, set flag to skip future virtual calls.
            if (!emuInstance->nds->GPU.GetRenderer().NeedsShaderCompile())
                shadersReady = true;
#endif
        }
        else {
#ifndef MELONPRIME_DS
            // Non-MelonPrime: RunFrameHook not used, just RunFrame.
#endif

#ifdef MELONPRIME_DS
            // RunFrameHook + SetKeyMask already done above (P-28).
#else
            // Original melonDS path (no hook).
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
            bool vulkanProducerBegun = false;
            if (videoBackend
                == MelonPrime::VideoBackend::PresentationBackend::Vulkan)
            {
                vulkanProducerBegun = emuInstance->beginVulkanProducerFrame();
            }
#endif
#ifdef MELONPRIME_DS
            if (romBootTraceFirstRunFrame)
                RomBootTrace("[RomBootTrace] first RunFrame begin\n");
#endif
            nlines = emuInstance->nds->RunFrame();
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
            if (videoBackend
                == MelonPrime::VideoBackend::PresentationBackend::Vulkan)
            {
                if (vulkanProducerBegun)
                {
                    const bool submitted = emuInstance->completeVulkanProducerFrame();
                    Platform::Log(Platform::LogLevel::Info,
                        "[VulkanSubmitTrace] completeProducerTransaction result=%d\n",
                        submitted ? 1 : 0);
                }
                else
                {
                    emuInstance->cancelVulkanProducerFrame();
                }
            }
#endif
#ifdef MELONPRIME_DS
            if (romBootTraceFirstRunFrame)
            {
                RomBootTrace("[RomBootTrace] first RunFrame complete lines=%u\n", nlines);
                romBootTraceFirstRunFrame = false;
            }
#endif
        }

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        if (videoBackend
            == MelonPrime::VideoBackend::PresentationBackend::Vulkan)
        {
#ifdef MELONPRIME_DS
            if (romBootTraceFirstVulkanSubmit)
                RomBootTrace("[RomBootTrace] first Vulkan producer transaction complete\n");
#endif
            if (romBootTraceFirstVulkanSubmit)
                romBootTraceFirstVulkanSubmit = false;
        }

        Platform::Log(Platform::LogLevel::Info,
            "[VulkanSubmitTrace] refreshActualRenderer begin\n");
        fflush(stdout);
        refreshActualRenderer();
        Platform::Log(Platform::LogLevel::Info,
            "[VulkanSubmitTrace] refreshActualRenderer complete\n");
        fflush(stdout);
#endif

#ifdef MELONPRIME_DS
        // P-25: Save flush throttle — check once per 30 frames (~0.5s).
        {
            static uint8_t s_flushCounter = 0;
            if (UNLIKELY(++s_flushCounter >= 30)) {
                s_flushCounter = 0;
                if (emuInstance->ndsSave) emuInstance->ndsSave->CheckFlush();
                if (emuInstance->gbaSave) emuInstance->gbaSave->CheckFlush();
                if (emuInstance->firmwareSave) emuInstance->firmwareSave->CheckFlush();
            }
        }
#else
        if (emuInstance->ndsSave) emuInstance->ndsSave->CheckFlush();
        if (emuInstance->gbaSave) emuInstance->gbaSave->CheckFlush();
        if (emuInstance->firmwareSave) emuInstance->firmwareSave->CheckFlush();
#endif

#ifdef MELONPRIME_DS
        MelonPrimePerf::SectionEnd(MelonPrimePerf::Section::RunFrame);
        MelonPrimePerf::SectionBegin(MelonPrimePerf::Section::Draw);
#endif
#ifdef MELONPRIME_DS
        if (romBootTraceFirstDrawScreen)
            RomBootTrace("[RomBootTrace] first drawScreen begin\n");
#endif
        emuInstance->drawScreen();
#ifdef MELONPRIME_DS
        if (romBootTraceFirstDrawScreen)
        {
            RomBootTrace("[RomBootTrace] first drawScreen complete\n");
            romBootTraceFirstDrawScreen = false;
        }
#endif

#ifdef MELONPRIME_DS
        MelonPrimePerf::SectionEnd(MelonPrimePerf::Section::Draw);
        MelonPrimePerf::SectionBegin(MelonPrimePerf::Section::DeferredDrain);
#endif

#ifdef MELONPRIME_DS
        // P-32: DeferredDrain AFTER drawScreen.
        // During drawScreen, new WM_INPUT accumulates. Draining here means
        // next frame's PrePoll starts with the cleanest possible queue.
        // Completely removed from the input→RunFrame latency path.
        melonPrime->DeferredDrainInput();
        MelonPrimePerf::SectionEnd(MelonPrimePerf::Section::DeferredDrain);
#endif

#ifdef MELONCAP
        MelonCap::Update();
#endif // MELONCAP

        winUpdateCount++;
#ifdef MELONPRIME_DS
        if (winUpdateCount >= winUpdateFreq &&
            videoBackend == MelonPrime::VideoBackend::PresentationBackend::NativeQt)
#else
        if (winUpdateCount >= winUpdateFreq && !useOpenGL)
#endif
        {
            emit windowUpdate();
            winUpdateCount = 0;
        }

        // P-38: Batch early-exit for inner-loop hotkeys.
        // Same pattern as P-24 for the outer loop. hotkeyPress is 0 on 99.9%+
        // of frames, so the 3 individual hotkeyPressed checks are skipped.
#ifdef MELONPRIME_DS
        const uint64_t innerHotkeyPress = emuInstance->hotkeyPress;
        if (UNLIKELY(innerHotkeyPress)) {
            if (innerHotkeyPress & (1ULL << HK_FastForwardToggle)) emuInstance->fastForwardToggled = !emuInstance->fastForwardToggled;
            if (innerHotkeyPress & (1ULL << HK_SlowMoToggle)) emuInstance->slowmoToggled = !emuInstance->slowmoToggled;

            if (innerHotkeyPress & (1ULL << HK_AudioMuteToggle)) emuInstance->toggleAudioMute();
        }

        const uint64_t innerHotkeyDown = emuInstance->hotkeyMask;
        bool enablefastforward = ((innerHotkeyDown & (1ULL << HK_FastForward)) != 0) | emuInstance->fastForwardToggled;
        bool enableslowmo = ((innerHotkeyDown & (1ULL << HK_SlowMo)) != 0) | emuInstance->slowmoToggled;
#else
        if (UNLIKELY(emuInstance->hotkeyPress)) {
            if (emuInstance->hotkeyPressed(HK_FastForwardToggle)) emuInstance->fastForwardToggled = !emuInstance->fastForwardToggled;
            if (emuInstance->hotkeyPressed(HK_SlowMoToggle)) emuInstance->slowmoToggled = !emuInstance->slowmoToggled;

            if (emuInstance->hotkeyPressed(HK_AudioMuteToggle)) emuInstance->toggleAudioMute();
        }

        bool enablefastforward = emuInstance->hotkeyDown(HK_FastForward) | emuInstance->fastForwardToggled;
        bool enableslowmo = emuInstance->hotkeyDown(HK_SlowMo) | emuInstance->slowmoToggled;
#endif

        if (useOpenGL)
        {
            if ((enablefastforward || enableslowmo) && !(fastforward || slowmo))
                emuInstance->setVSyncGL(false);
            else if (!(enablefastforward || enableslowmo) && (fastforward || slowmo))
            {
#ifdef MELONPRIME_DS
                // P-16: Restore VSync to user's configured setting, not hardcoded true.
                // MelonPrime disables VSync for minimum latency; the original code
                // unconditionally re-enabled it here, undoing the user's preference.
                bool vsyncSetting = emuInstance->getGlobalConfig().GetBool("Screen.VSync");
                emuInstance->setVSyncGL(vsyncSetting);
#else
                emuInstance->setVSyncGL(true);
#endif
            }
        }

        fastforward = enablefastforward;
        slowmo = enableslowmo;
        melonPrime->isFastForward = fastforward | slowmo;
        emuInstance->updateFastForwardMute(fastforward);

        if (slowmo) emuInstance->curFPS = emuInstance->slowmoFPS;
        else if (fastforward) emuInstance->curFPS = emuInstance->fastForwardFPS;
        else if (!emuInstance->doLimitFPS && !emuInstance->doAudioSync) emuInstance->curFPS = 1000.0;
        else emuInstance->curFPS = emuInstance->targetFPS;

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
        PublishVulkanRuntimePacingState(
            fastforward || slowmo,
            !emuInstance->doLimitFPS && !emuInstance->doAudioSync);
#endif

#ifndef MELONPRIME_DS
        // P-41: MelonPrime targets NDS (ConsoleType == 0) exclusively.
        // DSi volume sync is unreachable — skip the branch, pointer chase,
        // and I2C read entirely. Saves ~10-20 cyc/frame.
        if (emuInstance->audioDSiVolumeSync && emuInstance->nds->ConsoleType == 1)
        {
            DSi* dsi = static_cast<DSi*>(emuInstance->nds);
            u8 volumeLevel = dsi->I2C.GetBPTWL()->GetVolumeLevel();
            if (volumeLevel != dsiVolumeLevel)
            {
                dsiVolumeLevel = volumeLevel;
                emit syncVolumeLevel();
            }

            emuInstance->audioVolume = volumeLevel * (256.0 / 31.0);
        }
#endif

        if (emuInstance->doAudioSync && !(fastforward || slowmo))
            emuInstance->audioSync();

        double frametimeStep = nlines / (emuInstance->curFPS * 263.0);

        if (frametimeStep < 0.001) frametimeStep = 0.001;

#ifdef MELONPRIME_DS
        // P-13: Store frametimeStep for next frame's early limiter.
        // The limiter at the top of this lambda uses this value.
        storedFrametimeStep = frametimeStep;
#else
        if (emuInstance->doLimitFPS)
        {
            double curtime = SDL_GetPerformanceCounter() * perfCountsSec;

            frameLimitError += frametimeStep - (curtime - lastTime);
            if (frameLimitError < -frametimeStep)
                frameLimitError = -frametimeStep;
            if (frameLimitError > frametimeStep)
                frameLimitError = frametimeStep;

            if (round(frameLimitError * 1000.0) > 0.0)
            {
                SDL_Delay(round(frameLimitError * 1000.0));
                double timeBeforeSleep = curtime;
                curtime = SDL_GetPerformanceCounter() * perfCountsSec;
                frameLimitError -= curtime - timeBeforeSleep;
            }

            lastTime = curtime;
        }
#endif // MELONPRIME_DS

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
        // Developer-only: capture per-frame wall-clock interval.
        // Cheap (one QPC + a few FP ops); never executed in release builds.
        {
            const Uint64 nowTickDev = SDL_GetPerformanceCounter();
            if (frameMsPrimedDev) {
                const double frameMs = static_cast<double>(nowTickDev - prevFrameTickDev)
                                       * 1000.0 / static_cast<double>(perfFreqDev);
                if (frameMs < frameMsMinDev) frameMsMinDev = frameMs;
                if (frameMs > frameMsMaxDev) frameMsMaxDev = frameMs;
                frameMsSumDev += frameMs;
                ++frameMsCountDev;
            }
            prevFrameTickDev = nowTickDev;
            frameMsPrimedDev = true;
        }
#endif

#ifdef MELONPRIME_DS
        MelonPrimePerf::FrameEnd();
#endif

        nframes++;
        if (nframes >= 30)
        {
            double time = SDL_GetPerformanceCounter() * perfCountsSec;
            double dt = time - lastMeasureTime;
            lastMeasureTime = time;

            u32 fps = round(nframes / dt);
            nframes = 0;

            float fpstarget = 1.0 / frametimeStep;

            winUpdateFreq = fps / (u32)round(fpstarget);
            if (winUpdateFreq < 1)
                winUpdateFreq = 1;

            double actualfps = (59.8261 * 263.0) / nlines;
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
            const double avgMsDev = frameMsCountDev > 0
                ? frameMsSumDev / static_cast<double>(frameMsCountDev) : 0.0;
            const double jitterMsDev = (frameMsCountDev > 0)
                ? (frameMsMaxDev - frameMsMinDev) : 0.0;
            const double showMinDev = (frameMsCountDev > 0) ? frameMsMinDev : 0.0;
            const double showMaxDev = (frameMsCountDev > 0) ? frameMsMaxDev : 0.0;
            snprintf(melontitle, sizeof(melontitle),
                "[%d/%.0f min:%.2f avg:%.2f max:%.2fms jit:%.2f] " MELONPRIMEDS_TITLE_PREFIX "%s" MELONPRIMEDS_TITLE_SUFFIX,
                fps, actualfps,
                showMinDev, avgMsDev, showMaxDev, jitterMsDev, MelonPrime::kBuildStamp);
            frameMsMinDev = 1.0e9;
            frameMsMaxDev = 0.0;
            frameMsSumDev = 0.0;
            frameMsCountDev = 0;
#elif defined(MELONPRIME_DS)
            snprintf(melontitle, sizeof(melontitle), "[%d/%.0f] " MELONPRIMEDS_TITLE_PREFIX "%s" MELONPRIMEDS_TITLE_SUFFIX, fps, actualfps, MelonPrime::kBuildStamp);
#else
            snprintf(melontitle, sizeof(melontitle), "[%d/%.0f] melonDS " MELONDS_VERSION, fps, actualfps);
#endif
            changeWindowTitle(melontitle);
        }
        };
    // --- End of frameAdvanceOnce ---

#ifdef MELONPRIME_DS
    melonPrime->SetFrameAdvanceFunc(frameAdvanceOnce);
#endif // MELONPRIME_DS

    while (emuStatus != emuStatus_Exit)
    {
        if (emuInstance->instanceID == 0)
            MPInterface::Get().Process();

        // P-33: PrePollRawInput removed (P-19 HiddenWndProc captures WM_INPUT at dispatch).
        emuInstance->inputProcess();

#ifdef MELONPRIME_DS
        // P-24: Batch early-exit for outer loop hotkeys.
        // hotkeyPress is 0 on 99.9%+ of frames. Single mask test skips
        // all 7 individual hotkeyPressed checks and their branch prediction.
        const uint64_t outerHotkeyPress = emuInstance->hotkeyPress;
        if (UNLIKELY(outerHotkeyPress))
        {
            if (outerHotkeyPress & (1ULL << HK_FrameLimitToggle)) emit windowLimitFPSChange();

            if (outerHotkeyPress & (1ULL << HK_Pause)) emuTogglePause();
            if (outerHotkeyPress & (1ULL << HK_Reset)) emuReset();
            if (outerHotkeyPress & (1ULL << HK_FrameStep)) emuFrameStep();

            if (outerHotkeyPress & (1ULL << HK_FullscreenToggle)) emit windowFullscreenToggle();

            if (outerHotkeyPress & (1ULL << HK_SwapScreens)) emit swapScreensToggle();
            if (outerHotkeyPress & (1ULL << HK_SwapScreenEmphasis)) emit screenEmphasisToggle();
        }
#else
        if (emuInstance->hotkeyPressed(HK_FrameLimitToggle)) emit windowLimitFPSChange();

        if (emuInstance->hotkeyPressed(HK_Pause)) emuTogglePause();
        if (emuInstance->hotkeyPressed(HK_Reset)) emuReset();
        if (emuInstance->hotkeyPressed(HK_FrameStep)) emuFrameStep();

        if (emuInstance->hotkeyPressed(HK_FullscreenToggle)) emit windowFullscreenToggle();

        if (emuInstance->hotkeyPressed(HK_SwapScreens)) emit swapScreensToggle();
        if (emuInstance->hotkeyPressed(HK_SwapScreenEmphasis)) emit screenEmphasisToggle();
#endif

        if (emuStatus == emuStatus_Running || emuStatus == emuStatus_FrameStep)
        {
            if (emuStatus == emuStatus_FrameStep) emuStatus = emuStatus_Paused;

#ifndef MELONPRIME_DS
            if (emuInstance->hotkeyPressed(HK_SolarSensorDecrease))
            {
                int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorDown, true);
                if (level != -1)
                {
                    emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }
            if (emuInstance->hotkeyPressed(HK_SolarSensorIncrease))
            {
                int level = emuInstance->nds->GBACartSlot.SetInput(GBACart::Input_SolarSensorUp, true);
                if (level != -1)
                {
                    emuInstance->osdAddMessage(0, "Solar sensor level: %d", level);
                }
            }

            if (emuInstance->nds->ConsoleType == 1)
            {
                DSi* dsi = static_cast<DSi*>(emuInstance->nds);
                double currentTime = SDL_GetPerformanceCounter() * perfCountsSec;

                // Handle power button
                if (emuInstance->hotkeyDown(HK_PowerButton))
                {
                    dsi->I2C.GetBPTWL()->SetPowerButtonHeld(currentTime);
                }
                else if (emuInstance->hotkeyReleased(HK_PowerButton))
                {
                    dsi->I2C.GetBPTWL()->SetPowerButtonReleased(currentTime);
                }

                // Handle volume buttons
                if (emuInstance->hotkeyDown(HK_VolumeUp))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Up);
                }
                else if (emuInstance->hotkeyReleased(HK_VolumeUp))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Up);
                }

                if (emuInstance->hotkeyDown(HK_VolumeDown))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchHeld(DSi_BPTWL::volumeKey_Down);
                }
                else if (emuInstance->hotkeyReleased(HK_VolumeDown))
                {
                    dsi->I2C.GetBPTWL()->SetVolumeSwitchReleased(DSi_BPTWL::volumeKey_Down);
                }

                dsi->I2C.GetBPTWL()->ProcessVolumeSwitchInput(currentTime);
            }
#endif // MELONPRIME_DS

            // auto screen layout
#ifndef MELONPRIME_DS
            // P-26: MelonPrime does not use auto screen sizing.
            // Skips: 3 array writes + PowerControl9 read + comparison per frame.
            {
                mainScreenPos[2] = mainScreenPos[1];
                mainScreenPos[1] = mainScreenPos[0];
                mainScreenPos[0] = emuInstance->nds->PowerControl9 >> 15;

                int guess;
                if (mainScreenPos[0] == mainScreenPos[2] &&
                    mainScreenPos[0] != mainScreenPos[1])
                {
                    // constant flickering, likely displaying 3D on both screens
                    // TODO: when both screens are used for 2D only...???
                    guess = screenSizing_Even;
                }
                else
                {
                    if (mainScreenPos[0] == 1)
                        guess = screenSizing_EmphTop;
                    else
                        guess = screenSizing_EmphBot;
                }

                if (guess != autoScreenSizing)
                {
                    autoScreenSizing = guess;
                    emit autoScreenSizingChange(autoScreenSizing);
                }
            }
#endif

            frameAdvanceOnce();
        }
        else
        {
            // paused
            nframes = 0;
            lastTime = SDL_GetPerformanceCounter() * perfCountsSec;
            lastMeasureTime = lastTime;
#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
            // Drop accumulated stats so the post-resume stats aren't polluted
            // by the pause gap; re-prime on first frame after resume.
            frameMsMinDev = 1.0e9;
            frameMsMaxDev = 0.0;
            frameMsSumDev = 0.0;
            frameMsCountDev = 0;
            frameMsPrimedDev = false;
#endif

            emit windowUpdate();

#ifdef MELONPRIME_DS
            snprintf(melontitle, sizeof(melontitle), MELONPRIMEDS_TITLE_PREFIX "%s" MELONPRIMEDS_TITLE_SUFFIX, MelonPrime::kBuildStamp);
#else
            snprintf(melontitle, sizeof(melontitle), "melonDS " MELONDS_VERSION);
#endif
            changeWindowTitle(melontitle);

            SDL_Delay(75);

            emuInstance->drawScreen();
        }

        handleMessages();
    }

#include "MelonPrimeEmuThreadPerfShutdown.inc"
}

void EmuThread::sendMessage(Message msg)
{
    msgMutex.lock();
    msgQueue.enqueue(msg);
    msgMutex.unlock();
#include "MelonPrimeEmuThreadSendMessage.inc"
}

void EmuThread::waitMessage(int num)
{
    if (QThread::currentThread() == this) return;
    msgSemaphore.acquire(num);
}

void EmuThread::waitAllMessages()
{
    if (QThread::currentThread() == this) return;
    while (!msgQueue.empty())
        msgSemaphore.acquire();
}

void EmuThread::handleMessages()
{
#include "MelonPrimeEmuThreadHandleMessagesFastPath.inc"

    bool glborrow = false;

    msgMutex.lock();
    while (!msgQueue.empty())
    {
        Message msg = msgQueue.dequeue();
        switch (msg.type)
        {
        case msg_Exit:
            emuStatus = emuStatus_Exit;
            emuPauseStack = emuPauseStackRunning;
            emuInstance->audioDisable();
            MPInterface::Get().End(emuInstance->instanceID);

#ifdef MELONPRIME_DS
            melonPrime->OnEmuStop();
#endif // MELONPRIME_DS
            break;

        case msg_EmuRun:
            RomBootTrace("[RomBootTrace] msg_EmuRun begin\n");
            emuStatus = emuStatus_Running;
            emuPauseStack = emuPauseStackRunning;
            emuActive = true;
            emuInstance->audioEnable();
            emit windowEmuStart();
            RomBootTrace("[RomBootTrace] windowEmuStart emitted\n");

#ifdef MELONPRIME_DS
            RomBootTrace("[RomBootTrace] MelonPrime OnEmuStart begin\n");
            melonPrime->OnEmuStart();
            RomBootTrace("[RomBootTrace] MelonPrime OnEmuStart complete\n");
#endif // MELONPRIME_DS
            RomBootTrace("[RomBootTrace] msg_EmuRun complete\n");
            break;

        case msg_EmuPause:
            emuPauseStack++;
            if (emuPauseStack > emuPauseStackPauseThreshold) break;
            prevEmuStatus = emuStatus;
            emuStatus = emuStatus_Paused;
            if (prevEmuStatus != emuStatus_Paused) {
                emuInstance->audioDisable();
                emit windowEmuPause(true);
                emuInstance->osdAddMessage(0, "Paused");

#ifdef MELONPRIME_DS
                melonPrime->OnEmuPause();
#endif // MELONPRIME_DS
            }
            break;

        case msg_EmuUnpause:
            if (emuPauseStack < emuPauseStackPauseThreshold) break;
            emuPauseStack--;
            if (emuPauseStack >= emuPauseStackPauseThreshold) break;
            emuStatus = prevEmuStatus;
            if (emuStatus != emuStatus_Paused) {
                emuInstance->audioEnable();
                emit windowEmuPause(false);
                emuInstance->osdAddMessage(0, "Resumed");

#ifdef MELONPRIME_DS
                melonPrime->OnEmuUnpause();
#endif // MELONPRIME_DS
            }
            break;

        case msg_EmuStop:
            if (msg.param.value<bool>())
                emuInstance->nds->Stop();
            emuStatus = emuStatus_Paused;
            emuActive = false;
            emuInstance->audioDisable();
            emit windowEmuStop();
            break;

        case msg_EmuFrameStep:
            emuStatus = emuStatus_FrameStep;
            break;

        case msg_EmuReset:
            emuInstance->reset();
            emuStatus = emuStatus_Running;
            emuPauseStack = emuPauseStackRunning;
            emuActive = true;
            emuInstance->audioEnable();
            emit windowEmuReset();
            emuInstance->osdAddMessage(0, "Reset");

#ifdef MELONPRIME_DS
            melonPrime->OnReset();
#endif // MELONPRIME_DS
            break;

        case msg_InitGL:
            emuInstance->initOpenGL(msg.param.value<int>());
            useOpenGL = true;
#ifdef MELONPRIME_DS
            videoBackend = MelonPrime::VideoBackend::FromLegacyOpenGLFlag(useOpenGL);
#endif
            break;

        case msg_DeInitGL:
            emuInstance->deinitOpenGL(msg.param.value<int>());
            if (msg.param.value<int>() == 0)
            {
                useOpenGL = false;
#ifdef MELONPRIME_DS
                auto& cfg = emuInstance->getGlobalConfig();
                videoBackend = MelonPrime::VideoBackend::ResolvePresentationBackend(
                    cfg.GetBool("Screen.UseGL"), cfg.GetInt("3D.Renderer"));
                if (MelonPrime::VideoBackend::IsOpenGLPresentation(videoBackend))
                    videoBackend = MelonPrime::VideoBackend::FromLegacyOpenGLFlag(false);
#endif
            }
            break;

        case msg_BorrowGL:
            emuInstance->releaseGL();
            glborrow = true;
            break;

        case msg_BootROM:
            RomBootTrace("[RomBootTrace] msg_BootROM begin\n");
#ifdef MELONPRIME_DS
            romBootTraceFirstRunFrame = true;
            romBootTraceFirstVulkanSubmit = true;
            romBootTraceFirstDrawScreen = true;
#endif
            msgResult = 0;
            RomBootTrace("[RomBootTrace] loadROM begin\n");
            if (!emuInstance->loadROM(msg.param.value<QStringList>(), true, msgError)) break;
            RomBootTrace("[RomBootTrace] loadROM complete nds=%p\n",
                static_cast<void*>(emuInstance->nds));
            assert(emuInstance->nds != nullptr);
#ifdef MELONPRIME_DS
            // bootROM() queues msg_EmuRun only after this message completes. If
            // we boot over an active match, clear stale in-game pointers/hooks
            // now so the running loop cannot enter one more frame with old ROM
            // state before msg_EmuRun's normal OnEmuStart().
            RomBootTrace("[RomBootTrace] ResetRuntimeStateForBoot begin\n");
            melonPrime->ResetRuntimeStateForBoot();
            RomBootTrace("[RomBootTrace] ResetRuntimeStateForBoot complete\n");
#endif // MELONPRIME_DS
            RomBootTrace("[RomBootTrace] nds->Start begin\n");
            emuInstance->nds->Start();
            RomBootTrace("[RomBootTrace] nds->Start complete\n");
            msgResult = 1;
            RomBootTrace("[RomBootTrace] msg_BootROM complete\n");
            break;

        case msg_BootFirmware:
            msgResult = 0;
            if (!emuInstance->bootToMenu(msgError)) break;
            assert(emuInstance->nds != nullptr);
#ifdef MELONPRIME_DS
            melonPrime->ResetRuntimeStateForBoot();
#endif // MELONPRIME_DS
            emuInstance->nds->Start();
            msgResult = 1;
            break;

        case msg_InsertCart:
            msgResult = 0;
            if (!emuInstance->loadROM(msg.param.value<QStringList>(), false, msgError)) break;
            msgResult = 1;
            break;

        case msg_EjectCart:
            emuInstance->ejectCart();
            break;

        case msg_InsertGBACart:
            msgResult = 0;
            if (!emuInstance->loadGBAROM(msg.param.value<QStringList>(), msgError)) break;
            msgResult = 1;
            break;

        case msg_InsertGBAAddon:
            msgResult = 0;
            emuInstance->loadGBAAddon(msg.param.value<int>(), msgError);
            msgResult = 1;
            break;

        case msg_EjectGBACart:
            emuInstance->ejectGBACart();
            break;

        case msg_SaveState:
            msgResult = emuInstance->saveState(msg.param.value<QString>().toStdString());
            break;

        case msg_LoadState:
            msgResult = emuInstance->loadState(msg.param.value<QString>().toStdString());
            break;

        case msg_UndoStateLoad:
            emuInstance->undoStateLoad();
            msgResult = 1;
            break;

        case msg_ImportSavefile:
        {
            msgResult = 0;
            auto f = Platform::OpenFile(msg.param.value<QString>().toStdString(), Platform::FileMode::Read);
            if (!f) break;
            u32 len = FileLength(f);
            std::unique_ptr<u8[]> data = std::make_unique<u8[]>(len);
            Platform::FileRewind(f);
            Platform::FileRead(data.get(), len, 1, f);
            assert(emuInstance->nds != nullptr);
            emuInstance->nds->SetNDSSave(data.get(), len);
            CloseFile(f);
            msgResult = 1;
        }
        break;

        case msg_EnableCheats:
            emuInstance->enableCheats(msg.param.value<bool>());
            break;

#ifdef MELONPRIME_DS
        case msg_ApplyVideoBackendSwitch:
        {
            const auto stagedPresentation =
                static_cast<MelonPrime::VideoBackend::PresentationBackend>(
                    msg.param.value<int>());
            auto& cfg = emuInstance->getGlobalConfig();

            if (emuInstance->nds == nullptr)
            {
                // No console exists yet. Commit only the staged presentation state.
                // The actual renderer transaction runs after NDS creation because
                // updateVideoRenderer() forces lastVideoRenderer = -1.
                videoBackend = stagedPresentation;
                videoSettingsDirty = true;
                lastVideoRenderer = -1;
#if defined(MELONPRIME_ENABLE_VULKAN)
                auto& session = emuInstance->vulkanFrontendSession();
                session.completeBackendSwitch(
                    videoBackend
                        == MelonPrime::VideoBackend::PresentationBackend::Vulkan);
                if (videoBackend
                    != MelonPrime::VideoBackend::PresentationBackend::Vulkan)
                {
                    session.shutdown();
                }
#endif
                msgResult = static_cast<int>(videoBackend);
                break;
            }

            auto result = MelonPrime::VideoBackend::CreateRendererForSelection(
                *emuInstance->nds,
                cfg.GetInt("3D.Renderer"),
                cfg.GetBool("Screen.UseGL"));
            if (result.Presentation != stagedPresentation)
            {
                MelonPrime::VideoBackend::RegenerateSoftwareFallback(
                    *emuInstance->nds,
                    result,
                    "presentation backend staging",
                    "staged panel does not match the constructed renderer transaction");
            }

            emuInstance->renderLock.lock();
            auto committedPresentation = applyRendererCreation(std::move(result));
            if (committedPresentation != stagedPresentation)
            {
                auto fallback = MelonPrime::VideoBackend::CreateRendererForSelection(
                    *emuInstance->nds,
                    cfg.GetInt("3D.Renderer"),
                    cfg.GetBool("Screen.UseGL"));
                MelonPrime::VideoBackend::RegenerateSoftwareFallback(
                    *emuInstance->nds,
                    fallback,
                    "presentation backend commit",
                    "constructed backend could not commit to the staged panel");
                committedPresentation = applyRendererCreation(std::move(fallback));
            }
            videoBackend = committedPresentation;
            applyRendererSettings();
            videoSettingsDirty = true;
            lastVideoRenderer = MelonPrime::VideoBackend::NormalizeRendererForPlatform(
                cfg.GetInt("3D.Renderer"));
#if defined(MELONPRIME_ENABLE_VULKAN)
            emuInstance->vulkanFrontendSession().completeBackendSwitch(
                videoBackend == MelonPrime::VideoBackend::PresentationBackend::Vulkan);
            if (videoBackend != MelonPrime::VideoBackend::PresentationBackend::Vulkan)
                emuInstance->vulkanFrontendSession().shutdown();
#endif
            emuInstance->renderLock.unlock();
            refreshActualRenderer();
            msgResult = static_cast<int>(videoBackend);
            break;
        }
#endif
        }

        msgSemaphore.release();
    }
#include "MelonPrimeEmuThreadHandleMessagesDrained.inc"
    msgMutex.unlock();

    if (glborrow)
    {
        glBorrowMutex.lock();
        glBorrowCond.wait(&glBorrowMutex);
        glBorrowMutex.unlock();
    }
}

void EmuThread::changeWindowTitle(char* title)
{
    emit windowTitleChange(QString(title));
}

void EmuThread::initContext(int win)
{
    sendMessage({ .type = msg_InitGL, .param = win });
    waitMessage();
}

void EmuThread::deinitContext(int win)
{
    sendMessage({ .type = msg_DeInitGL, .param = win });
    waitMessage();
}

void EmuThread::borrowGL()
{
    sendMessage(msg_BorrowGL);
    waitMessage();
}

void EmuThread::returnGL()
{
    glBorrowMutex.lock();
    glBorrowCond.wakeAll();
    glBorrowMutex.unlock();
}

void EmuThread::emuRun()
{
    sendMessage(msg_EmuRun);
    waitMessage();
}

void EmuThread::emuPause(bool broadcast)
{
    sendMessage(msg_EmuPause);
    waitMessage();

    if (broadcast)
        emuInstance->broadcastCommand(InstCmd_Pause);
}

void EmuThread::emuUnpause(bool broadcast)
{
    sendMessage(msg_EmuUnpause);
    waitMessage();

    if (broadcast)
        emuInstance->broadcastCommand(InstCmd_Unpause);
}

void EmuThread::emuTogglePause(bool broadcast)
{
    if (emuStatus == emuStatus_Paused)
        emuUnpause(broadcast);
    else
        emuPause(broadcast);
}

void EmuThread::emuStop(bool external)
{
    sendMessage({ .type = msg_EmuStop, .param = external });
    waitMessage();
}

void EmuThread::emuExit()
{
    sendMessage(msg_Exit);
    waitAllMessages();
}

void EmuThread::emuFrameStep()
{
    if (emuPauseStack < emuPauseStackPauseThreshold)
        sendMessage(msg_EmuPause);
    sendMessage(msg_EmuFrameStep);
    waitAllMessages();
}

void EmuThread::emuReset()
{
    sendMessage(msg_EmuReset);
    waitMessage();
}

bool EmuThread::emuIsRunning()
{
    return emuStatus == emuStatus_Running;
}

bool EmuThread::emuIsActive()
{
    return emuActive;
}

int EmuThread::bootROM(const QStringList& filename, QString& errorstr)
{
    sendMessage({ .type = msg_BootROM, .param = filename });
    waitMessage();
    if (!msgResult) {
        errorstr = msgError;
        return msgResult;
    }
    sendMessage(msg_EmuRun);
    waitMessage();
    errorstr = "";
    return msgResult;
}

int EmuThread::bootFirmware(QString& errorstr)
{
    sendMessage(msg_BootFirmware);
    waitMessage();
    if (!msgResult) {
        errorstr = msgError;
        return msgResult;
    }
    sendMessage(msg_EmuRun);
    waitMessage();
    errorstr = "";
    return msgResult;
}

int EmuThread::insertCart(const QStringList& filename, bool gba, QString& errorstr)
{
    MessageType msgtype = gba ? msg_InsertGBACart : msg_InsertCart;
    sendMessage({ .type = msgtype, .param = filename });
    waitMessage();
    errorstr = msgResult ? "" : msgError;
    return msgResult;
}

void EmuThread::ejectCart(bool gba)
{
    sendMessage(gba ? msg_EjectGBACart : msg_EjectCart);
    waitMessage();
}

int EmuThread::insertGBAAddon(int type, QString& errorstr)
{
    sendMessage({ .type = msg_InsertGBAAddon, .param = type });
    waitMessage();
    errorstr = msgResult ? "" : msgError;
    return msgResult;
}

int EmuThread::saveState(const QString& filename)
{
    sendMessage({ .type = msg_SaveState, .param = filename });
    waitMessage();
    return msgResult;
}

int EmuThread::loadState(const QString& filename)
{
    sendMessage({ .type = msg_LoadState, .param = filename });
    waitMessage();
    return msgResult;
}

int EmuThread::undoStateLoad()
{
    sendMessage(msg_UndoStateLoad);
    waitMessage();
    return msgResult;
}

int EmuThread::importSavefile(const QString& filename)
{
    sendMessage(msg_EmuReset);
    sendMessage({ .type = msg_ImportSavefile, .param = filename });
    waitMessage(2);
    return msgResult;
}

void EmuThread::enableCheats(bool enable)
{
    sendMessage({ .type = msg_EnableCheats, .param = enable });
    waitMessage();
}

#ifdef MELONPRIME_DS
MelonPrime::VideoBackend::PresentationBackend EmuThread::applyVideoBackendSwitch(
    MelonPrime::VideoBackend::PresentationBackend stagedPresentation)
{
    sendMessage({
        .type = msg_ApplyVideoBackendSwitch,
        .param = static_cast<int>(stagedPresentation),
    });
    waitMessage();
    return static_cast<MelonPrime::VideoBackend::PresentationBackend>(msgResult);
}

MelonPrime::VideoBackend::PresentationBackend EmuThread::applyRendererCreation(
    MelonPrime::VideoBackend::RendererCreationResult&& result)
{
    auto nds = emuInstance->nds;
    const int requestedRenderer = result.RequestedRenderer;
    const int normalizedRenderer = result.NormalizedRenderer;
    // R22 renderer transaction: old CurrentRenderer is stopped by
    // GPU::SetRenderer(), then the outer owner and the explicit Vulkan
    // Renderer3D override are installed before producer generation changes.
    RomBootTrace("[RomBootTrace] outer SetRenderer begin\n");
    if (result.OuterAction == MelonPrime::VideoBackend::OuterRendererAction::Replace)
        nds->SetRenderer(std::move(result.OuterRenderer));
    else
        result.OuterRenderer.reset();
    RomBootTrace("[RomBootTrace] outer SetRenderer complete\n");
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    if (result.OuterAction == MelonPrime::VideoBackend::OuterRendererAction::Replace
        && !nds->GPU.LastRendererInitializationSucceeded())
    {
        if (normalizedRenderer == renderer3D_Vulkan)
        {
            MelonPrime::VideoBackend::ActivateVulkanRuntimeFallback(
                "outer renderer initialization", -1);
        }
        MelonPrime::VideoBackend::RegenerateSoftwareFallback(
            *nds,
            result,
            "outer renderer initialization",
            "selected outer renderer failed Init(); Software pair regenerated");
        nds->SetRenderer(std::move(result.OuterRenderer));
    }
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    const bool activateVulkanFrontend =
        normalizedRenderer == renderer3D_Vulkan
        && result.Presentation == MelonPrime::VideoBackend::PresentationBackend::Vulkan
        && result.Renderer3D != nullptr;
    RomBootTrace("[RomBootTrace] Renderer3D install begin\n");
    nds->GPU.SetRenderer3D(std::move(result.Renderer3D));
    RomBootTrace("[RomBootTrace] Renderer3D install complete\n");
    if (activateVulkanFrontend)
    {
        auto& session = emuInstance->vulkanFrontendSession();
        RomBootTrace("[RomBootTrace] frontend session initialize begin\n");
        if (!session.initialize(*nds))
        {
            MelonPrime::VideoBackend::ActivateVulkanRuntimeFallback(
                "Vulkan frontend session initialization", -1);
            MelonPrime::VideoBackend::RegenerateSoftwareFallback(
                *nds,
                result,
                "Vulkan frontend session initialization",
                "VulkanOutput or FrameQueue initialization failed");
            nds->SetRenderer(std::move(result.OuterRenderer));
            nds->GPU.SetRenderer3D(nullptr);
        }
        else
        {
            session.beginGeneration(nds->GPU.GPU3D.GetCurrentRendererGeneration());
        }
        RomBootTrace("[RomBootTrace] frontend session initialize complete\n");
    }
#endif

    videoBackend = result.Presentation;
    videoRenderer = MelonPrime::VideoBackend::EvaluateActualRenderer(
        *nds, normalizedRenderer, videoBackend);
    result.ActualRenderer = videoRenderer;
    lastVideoRenderer = normalizedRenderer;

    Platform::Log(Platform::LogLevel::Info,
        "[MelonPrime] video transaction: requested=%s(%d) normalized=%s(%d) "
        "actual=%s(%d) presenter=%s generation=%llu failed_stage=%s reason=%s config_changed=no\n",
        MelonPrime::VideoBackend::RendererName(requestedRenderer), requestedRenderer,
        MelonPrime::VideoBackend::RendererName(normalizedRenderer), normalizedRenderer,
        MelonPrime::VideoBackend::RendererName(result.ActualRenderer), result.ActualRenderer,
        MelonPrime::VideoBackend::PresentationBackendName(videoBackend),
#if defined(MELONPRIME_ENABLE_VULKAN)
        static_cast<unsigned long long>(nds->GPU.GPU3D.GetCurrentRendererGeneration()),
#else
        0ull,
#endif
        result.FailedStage.empty() ? "none" : result.FailedStage.c_str(),
        result.FallbackReason.empty() ? "none" : result.FallbackReason.c_str());

    emit windowVideoRendererChanged(requestedRenderer, result.ActualRenderer);
    return videoBackend;
}

void EmuThread::applyRendererSettings()
{
    auto nds = emuInstance->nds;
    auto& cfg = emuInstance->getGlobalConfig();
    const int selectedRenderer = MelonPrime::VideoBackend::NormalizeRendererForPlatform(
        cfg.GetInt("3D.Renderer"));
    const bool sharedHardwareSettings =
#if defined(MELONPRIME_ENABLE_METAL)
        selectedRenderer == renderer3D_Metal ||
        selectedRenderer == renderer3D_MetalCompute ||
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
        selectedRenderer == renderer3D_Vulkan ||
#endif
        false;
    const int scaleFactor = sharedHardwareSettings && cfg.HasKey("3D.Hardware.ScaleFactor")
        ? cfg.GetInt("3D.Hardware.ScaleFactor")
        : cfg.GetInt("3D.GL.ScaleFactor");
    const bool hiresCoordinates = sharedHardwareSettings && cfg.HasKey("3D.Hardware.HiresCoordinates")
        ? cfg.GetBool("3D.Hardware.HiresCoordinates")
        : cfg.GetBool("3D.GL.HiresCoordinates");
    const bool betterPolygons = sharedHardwareSettings && cfg.HasKey("3D.Hardware.BetterPolygons")
        ? cfg.GetBool("3D.Hardware.BetterPolygons")
        : cfg.GetBool("3D.GL.BetterPolygons");
    melonDS::RendererSettings settings = {
        .ScaleFactor = scaleFactor,
        .Threaded = cfg.GetBool("3D.Soft.Threaded"),
        .HiresCoordinates = hiresCoordinates,
        .BetterPolygons = betterPolygons
    };
    nds->GetRenderer().SetRenderSettings(settings);
}

void EmuThread::updateRenderer()
{
    auto nds = emuInstance->nds;
    auto& cfg = emuInstance->getGlobalConfig();

#include "MelonPrimeEmuThreadUpdateRendererBefore.inc"

    const int configuredRenderer = cfg.GetInt("3D.Renderer");
    const int normalizedRenderer =
        MelonPrime::VideoBackend::NormalizeRendererForPlatform(configuredRenderer);
    RomBootTrace(
        "[RomBootTrace] updateRenderer begin configured=%d last=%d nds=%p\n",
        configuredRenderer,
        lastVideoRenderer,
        static_cast<void*>(nds));
    if (normalizedRenderer != lastVideoRenderer)
    {
        RomBootTrace("[RomBootTrace] CreateRendererForSelection begin\n");
        auto result = MelonPrime::VideoBackend::CreateRendererForSelection(
            *nds, configuredRenderer, cfg.GetBool("Screen.UseGL"));
        RomBootTrace("[RomBootTrace] CreateRendererForSelection complete\n");
        RomBootTrace("[RomBootTrace] applyRendererCreation begin\n");
        applyRendererCreation(std::move(result));
        RomBootTrace("[RomBootTrace] applyRendererCreation complete\n");
    }

    RomBootTrace("[RomBootTrace] applyRendererSettings begin\n");
    applyRendererSettings();
    RomBootTrace("[RomBootTrace] applyRendererSettings complete\n");

#include "MelonPrimeEmuThreadUpdateRendererAfter.inc"
}

void EmuThread::refreshActualRenderer()
{
    if (emuInstance->nds == nullptr)
        return;
    auto& cfg = emuInstance->getGlobalConfig();
    const int normalized = MelonPrime::VideoBackend::NormalizeRendererForPlatform(
        cfg.GetInt("3D.Renderer"));
    const int actual = MelonPrime::VideoBackend::EvaluateActualRenderer(
        *emuInstance->nds, normalized, videoBackend);
    if (actual == videoRenderer)
        return;
    videoRenderer = actual;
    Platform::Log(Platform::LogLevel::Info,
        "[MelonPrime] video actual renderer changed: requested=%s(%d) actual=%s(%d) presenter=%s\n",
        MelonPrime::VideoBackend::RendererName(normalized), normalized,
        MelonPrime::VideoBackend::RendererName(actual), actual,
        MelonPrime::VideoBackend::PresentationBackendName(videoBackend));
    emit windowVideoRendererChanged(normalized, actual);
}

#endif

void EmuThread::compileShaders()
{
    auto& renderer = emuInstance->nds->GPU.GetRenderer();
    int currentShader, shadersCount;
    u64 startTime = SDL_GetPerformanceCounter();
    // kind of hacky to look at the wallclock, though it is easier than
    // than disabling vsync
    do
    {
        renderer.ShaderCompileStep(currentShader, shadersCount);
    } while (renderer.NeedsShaderCompile() &&
        (SDL_GetPerformanceCounter() - startTime) * perfCountsSec < 1.0 / 6.0);
    emuInstance->osdAddMessage(0, "Compiling shader %d/%d", currentShader + 1, shadersCount);
}
