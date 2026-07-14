#include "MelonPrimeScreenVulkan.h"

#include <algorithm>

#include "EmuInstance.h"
#include "MelonPrimeVulkanFrontendSession.h"
#include "Platform.h"
#include "VulkanContext.h"

#include "VulkanReference/VulkanSurfacePresenter.h"

using namespace MelonDSAndroid;

ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanelNative(parent)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
}

ScreenPanelVulkan::~ScreenPanelVulkan()
{
    if (presenter)
    {
        if (sessionPresenterRegistered)
            emuInstance->vulkanFrontendSession().unregisterPresenter(presenter.get());
        sessionPresenterRegistered = false;
        if (surfaceId != 0)
            presenter->detachSurface(surfaceId);
        surfaceId = 0;
    }
    surfaceHost.destroy(melonDS::VulkanContext::Get().GetInstance());
    if (presenter)
        presenter->shutdown();
}

bool ScreenPanelVulkan::initVulkan()
{
    presenter = std::make_unique<VulkanSurfacePresenter>();
    if (!presenter->init())
        return false;

    if (!ensureNativeSurface())
    {
        if (surfaceId != 0)
            presenter->detachSurface(surfaceId);
        surfaceId = 0;
        surfaceHost.destroy(melonDS::VulkanContext::Get().GetInstance());
        presenter->shutdown();
        presenter.reset();
        return false;
    }

    emuInstance->vulkanFrontendSession().registerPresenter(presenter.get());
    sessionPresenterRegistered = true;
    melonDS::Platform::Log(melonDS::Platform::LogLevel::Info,
        "[MelonPrime] Vulkan frontend session attached to desktop surface generation %llu\n",
        static_cast<unsigned long long>(surfaceHost.generation()));
    return true;
}

bool ScreenPanelVulkan::ensureNativeSurface()
{
    if (!presenter)
        return false;
    if (surfaceId != 0 && surfaceHost.matchesWidget(*this))
        return true;

    auto& context = melonDS::VulkanContext::Get();
    auto& session = emuInstance->vulkanFrontendSession();
    const bool presenterWasRegistered = sessionPresenterRegistered;
    if (presenterWasRegistered)
        session.unregisterPresenter(presenter.get());
    if (surfaceId != 0)
    {
        presenter->detachSurface(surfaceId);
        surfaceId = 0;
    }
    surfaceHost.destroy(context.GetInstance());
    configuredWidth = 0;
    configuredHeight = 0;

    if (!surfaceHost.createForWidget(*this, context.GetInstance()))
        return false;
    const QSize size = surfaceHost.pixelSize();
    surfaceId = presenter->attachSurface(
        surfaceHost.surface(),
        static_cast<u32>(size.width()),
        static_cast<u32>(size.height()));
    if (surfaceId == 0)
    {
        surfaceHost.destroy(context.GetInstance());
        return false;
    }

    session.beginSurfaceGeneration(surfaceHost.generation());
    if (configureBasicSurface(size.width(), size.height(), false))
    {
        if (presenterWasRegistered)
            session.registerPresenter(presenter.get());
        return true;
    }

    presenter->detachSurface(surfaceId);
    surfaceId = 0;
    surfaceHost.destroy(context.GetInstance());
    return false;
}

bool ScreenPanelVulkan::configureBasicSurface(
    int newWidth, int newHeight, bool managePresenterRegistration)
{
    if (!presenter || surfaceId == 0 || newWidth <= 0 || newHeight <= 0)
        return false;

    auto& session = emuInstance->vulkanFrontendSession();
    const bool presenterWasRegistered =
        managePresenterRegistration && sessionPresenterRegistered;
    if (presenterWasRegistered)
        session.unregisterPresenter(presenter.get());

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
        if (presenterWasRegistered)
            session.registerPresenter(presenter.get());
        return false;
    }
    if (!presenter->configureSurface(surfaceId, config, {}))
    {
        if (presenterWasRegistered)
            session.registerPresenter(presenter.get());
        return false;
    }

    configuredWidth = newWidth;
    configuredHeight = newHeight;
    if (presenterWasRegistered)
        session.registerPresenter(presenter.get());
    return true;
}

bool ScreenPanelVulkan::captureVulkanFrame(const QString&)
{
    return false;
}

void ScreenPanelVulkan::drawScreen()
{
    refreshClipForGameStateChange();
    if (!presenter || !emuInstance->getEmuThread()->emuIsActive()
        || !ensureNativeSurface())
        return;

    const QSize pixelSize = surfaceHost.pixelSize();
    const int currentWidth = pixelSize.width();
    const int currentHeight = pixelSize.height();
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
