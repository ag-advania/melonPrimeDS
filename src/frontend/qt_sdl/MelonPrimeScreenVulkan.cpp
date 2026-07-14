#include "MelonPrimeScreenVulkan.h"

#include <algorithm>

#include "EmuInstance.h"
#include "MelonPrimeVulkanFrontendSession.h"
#include "Platform.h"

#include "VulkanReference/VulkanSurfacePresenter.h"

using namespace MelonDSAndroid;

ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanelNative(parent)
{
}

ScreenPanelVulkan::~ScreenPanelVulkan()
{
    if (!presenter)
        return;
    emuInstance->vulkanFrontendSession().unregisterPresenter(presenter.get());
    if (surfaceId != 0)
        presenter->detachSurface(surfaceId);
    presenter->shutdown();
}

bool ScreenPanelVulkan::initVulkan()
{
#if !defined(_WIN32)
    return false;
#else
    presenter = std::make_unique<VulkanSurfacePresenter>();
    if (!presenter->init())
        return false;

    const int initialWidth = std::max(width(), 1);
    const int initialHeight = std::max(height(), 1);
    surfaceId = presenter->attachSurface(
        reinterpret_cast<void*>(static_cast<quintptr>(winId())),
        static_cast<u32>(initialWidth),
        static_cast<u32>(initialHeight));
    if (surfaceId == 0 || !configureBasicSurface(initialWidth, initialHeight))
    {
        presenter->shutdown();
        presenter.reset();
        return false;
    }

    emuInstance->vulkanFrontendSession().registerPresenter(presenter.get());
    melonDS::Platform::Log(melonDS::Platform::LogLevel::Info,
        "[MelonPrime] Vulkan frontend session attached to Win32 surface\n");
    return true;
#endif
}

bool ScreenPanelVulkan::configureBasicSurface(int newWidth, int newHeight)
{
    if (!presenter || surfaceId == 0 || newWidth <= 0 || newHeight <= 0)
        return false;

    VulkanSurfaceConfig config{};
    const int halfHeight = std::max(newHeight / 2, 1);
    config.topScreen = {true, 0, 0, newWidth, halfHeight};
    config.bottomScreen = {
        true, 0, halfHeight, newWidth, std::max(newHeight - halfHeight, 1)};
    config.filtering = VulkanFilterMode::Nearest;

    if ((configuredWidth != newWidth || configuredHeight != newHeight)
        && !presenter->resizeSurface(
            surfaceId, static_cast<u32>(newWidth), static_cast<u32>(newHeight)))
    {
        return false;
    }
    if (!presenter->configureSurface(surfaceId, config, {}))
        return false;

    configuredWidth = newWidth;
    configuredHeight = newHeight;
    return true;
}

bool ScreenPanelVulkan::captureVulkanFrame(const QString&)
{
    return false;
}

void ScreenPanelVulkan::drawScreen()
{
    refreshClipForGameStateChange();
    if (!presenter || surfaceId == 0 || !emuInstance->getEmuThread()->emuIsActive())
        return;

    const int currentWidth = std::max(width(), 1);
    const int currentHeight = std::max(height(), 1);
    if ((currentWidth != configuredWidth || currentHeight != configuredHeight)
        && !configureBasicSurface(currentWidth, currentHeight))
    {
        return;
    }

    auto& session = emuInstance->vulkanFrontendSession();
    Frame* frame = session.acquirePresentFrame();
    if (frame == nullptr)
        return;

    constexpr u64 kPresentTimeoutNs = 16'666'667ull;
    if (session.presentAcquiredFrame(frame, *presenter, kPresentTimeoutNs))
        session.commitPresentedFrame(frame);
    else
        session.deferPresentedFrame(frame);
}
