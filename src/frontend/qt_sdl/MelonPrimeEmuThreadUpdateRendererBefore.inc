// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
bool vsyncFlag = cfg.GetBool("Screen.VSync");
emuInstance->setVSyncGL(vsyncFlag);

// Metal-backend plan Phase 0 (melonprime-metal-backend-plan.md): clamp
// `videoRenderer` (loaded from `3D.Renderer`, possibly stale/imported/
// hand-edited) to a value safe to construct on this platform before it
// reaches the switch below. Also closes the crash-only `default:` case
// downstream, since normalization is exhaustive over the known enum.
videoRenderer = MelonPrime::VideoBackend::NormalizeRendererForPlatform(videoRenderer);

#if defined(MELONPRIME_ENABLE_VULKAN)
// The panel compositor owns commands/descriptors that sample renderer images.
// Quiesce the complete Vulkan presentation path before SetRenderer().
if (lastVideoRenderer == renderer3D_Vulkan
    && videoRenderer != renderer3D_Vulkan)
{
    ScreenPanelVulkan::PrepareForInstanceRendererTransition(emuInstance);
}
#endif

#endif // MELONPRIME_DS
