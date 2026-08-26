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

#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
// The DX12 panel keeps its presentation object across renderer changes. Its
// direct SRV cache therefore needs the same renderer-lifetime boundary as the
// Vulkan panel before the old output resources are destroyed.
if (lastVideoRenderer == renderer3D_DX12
    && videoRenderer != renderer3D_DX12)
{
    ScreenPanelDX12::PrepareForInstanceRendererTransition(emuInstance);
}
#endif

// REAUDIT-P1-001: release the outgoing native backend before the incoming one
// touches the GPU.
//
// Both halves of a native backend switch reach for a device while the other
// backend still owns one. The incoming renderer's factory acquires its device
// inside its *constructor*, which runs before SetRenderer() destroys the old
// renderer, and the DX12 admission probe used to run earlier still, during
// normalization. On Windows the result was not a graceful refusal: the D3D12
// adapter enumeration came back empty, DX12 was recorded as unsupported for
// the rest of the process, and the live VkDevice was lost with it.
//
// Installing the software renderer first destroys the outgoing renderer and
// drops its device references, so only one native device is ever being created
// or destroyed at a time. It costs one software-renderer construction on a cold
// path that only runs when the user changes renderer.
//
// If that install is refused -- SetRenderer() declines while display capture
// cannot be synchronized -- the outgoing renderer is still live, so the
// transition must not continue into the incoming backend.
bool melonPrimeTransitionReleasedOldBackend = true;
if (videoRenderer != lastVideoRenderer
    && MelonPrime::VideoBackend::RendererOwnsNativeGpuDevice(lastVideoRenderer)
    && MelonPrime::VideoBackend::RendererOwnsNativeGpuDevice(videoRenderer))
{
    Platform::Log(
        Platform::LogLevel::Info,
        "Renderer transition releasing previous native backend previous=%d requested=%d\n",
        lastVideoRenderer,
        videoRenderer);
    // Pointer identity, not a type test: VulkanRenderer and DX12Renderer both
    // derive from SoftRenderer, so a dynamic_cast to it cannot tell whether the
    // old renderer is gone. SetRenderer() either replaces the object or leaves
    // it untouched, and the outgoing object stays alive while the replacement
    // is constructed, so the two addresses cannot collide.
    const melonDS::Renderer* const melonPrimeOutgoingRenderer =
        &nds->GetRenderer();
    nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));
    melonPrimeTransitionReleasedOldBackend =
        &nds->GetRenderer() != melonPrimeOutgoingRenderer;
    if (!melonPrimeTransitionReleasedOldBackend)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "Renderer transition aborted: the previous native backend could not be released\n");
        videoRenderer = lastVideoRenderer;
    }
}

#endif // MELONPRIME_DS
