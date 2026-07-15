#include "MelonPrimeScreenVulkan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <QDateTime>
#include <QMetaObject>
#include <QPaintEngine>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QThread>
#include <QWidget>

#include "EmuInstance.h"
#include "MelonPrime.h"
#include "MelonPrimeConstants.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeHudGeometry.h"
#include "MelonPrimeHudPropSchema.inc"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeVulkanFrontendSession.h"
#include "NDS.h"
#include "Platform.h"
#include "VulkanContext.h"

#include "VulkanReference/VulkanSurfacePresenter.h"

using namespace MelonDSAndroid;


namespace
{
u32 PackRgba(int red, int green, int blue, int alpha = 255)
{
    return (static_cast<u32>(std::clamp(red, 0, 255)) << 24)
        | (static_cast<u32>(std::clamp(green, 0, 255)) << 16)
        | (static_cast<u32>(std::clamp(blue, 0, 255)) << 8)
        | static_cast<u32>(std::clamp(alpha, 0, 255));
}
}

class ScreenPanelVulkan::NoRomSplashOverlay final : public QWidget
{
public:
    explicit NoRomSplashOverlay(ScreenPanelVulkan& owner)
        : QWidget(&owner), screen(owner)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFocusPolicy(Qt::NoFocus);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        event->accept();
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);

        screen.osdMutex.lock();
        painter.drawPixmap(
            QRect(screen.splashPos[3], QSize(192, 192)),
            screen.splashLogo);
        for (int i = 0; i < 3; i++)
            painter.drawImage(screen.splashPos[i], screen.splashText[i].bitmap);
        screen.osdMutex.unlock();
    }

private:
    ScreenPanelVulkan& screen;
};

ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanel(parent, false)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize(1));

    if (splashLogo.isNull()
        && !splashLogo.load(QStringLiteral(":/melon-logo")))
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan no-ROM splash logo resource failed to load\n");
    }

    noRomSplashOverlay = new NoRomSplashOverlay(*this);
    noRomSplashOverlay->hide();
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
    if (QThread::currentThread() != thread())
        return false;

    presenter = std::make_unique<VulkanSurfacePresenter>();
    if (!presenter->init())
        return false;

    setupScreenLayout();
    if (!hasValidGameScreenLayout())
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Error,
            "[VulkanSurfaceConfig] init rejected: invalid game screen layout "
            "(numScreens=%d layoutGeneration=%llu)\n",
            numScreens,
            static_cast<unsigned long long>(layoutGeneration));
        presenter->shutdown();
        presenter.reset();
        return false;
    }

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
    if (configureSurface(size.width(), size.height(), false))
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

bool ScreenPanelVulkan::hasValidGameScreenLayout() const noexcept
{
    if (numScreens <= 0 || numScreens > kMaxScreenTransforms)
        return false;

    bool hasTopOrBottom = false;
    for (int i = 0; i < numScreens; ++i)
    {
        const float* matrix = screenMatrix[i];
        for (int coefficient = 0; coefficient < 6; ++coefficient)
        {
            if (!std::isfinite(matrix[coefficient]))
                return false;
        }

        const float transformedWidth =
            std::hypot(matrix[0] * 256.0f, matrix[1] * 256.0f);
        const float transformedHeight =
            std::hypot(matrix[2] * 192.0f, matrix[3] * 192.0f);
        const float determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];

        if (transformedWidth <= 0.5f
            || transformedHeight <= 0.5f
            || std::fabs(determinant) <= 1.0e-8f)
        {
            return false;
        }

        if (screenKind[i] == 0 || screenKind[i] == 1)
            hasTopOrBottom = true;
    }

    return hasTopOrBottom;
}

bool ScreenPanelVulkan::configureSurface(
    int newWidth, int newHeight, bool managePresenterRegistration)
{
    (void)managePresenterRegistration;
    if (!presenter || surfaceId == 0 || newWidth <= 0 || newHeight <= 0)
        return false;

    if (!hasValidGameScreenLayout())
        return false;

    VulkanSurfaceConfig config{};
    const float dpr = static_cast<float>(devicePixelRatioF());
    config.screenTransformCount = static_cast<u32>(
        std::clamp(numScreens, 0, static_cast<int>(config.screenTransforms.size())));
    for (u32 index = 0; index < config.screenTransformCount; ++index)
    {
        auto& destination = config.screenTransforms[index];
        destination.enabled = true;
        destination.topScreen = screenKind[index] == 0;
        for (size_t coefficient = 0; coefficient < destination.matrix.size(); ++coefficient)
            destination.matrix[coefficient] = screenMatrix[index][coefficient] * dpr;
    }
    config.filtering = filter ? VulkanFilterMode::Linear : VulkanFilterMode::Nearest;

    u32 validGameScreens = 0;
    for (u32 index = 0; index < config.screenTransformCount; ++index)
    {
        const auto& transform = config.screenTransforms[index];
        if (!transform.enabled)
            continue;

        const float transformedWidth =
            std::hypot(transform.matrix[0] * 256.0f, transform.matrix[1] * 256.0f);
        const float transformedHeight =
            std::hypot(transform.matrix[2] * 192.0f, transform.matrix[3] * 192.0f);
        const float determinant =
            transform.matrix[0] * transform.matrix[3] - transform.matrix[1] * transform.matrix[2];
        const bool finite = std::all_of(
            transform.matrix.begin(),
            transform.matrix.end(),
            [](float value) { return std::isfinite(value); });
        const bool nonDegenerate =
            transformedWidth > 0.5f
            && transformedHeight > 0.5f
            && std::fabs(determinant) > 1.0e-8f;
        if (finite && nonDegenerate)
            ++validGameScreens;

        auto mapPoint = [&](float x, float y) {
            return std::array<float, 2>{
                transform.matrix[0] * x + transform.matrix[2] * y + transform.matrix[4],
                transform.matrix[1] * x + transform.matrix[3] * y + transform.matrix[5]};
        };
        const auto topLeft = mapPoint(0.0f, 0.0f);
        const auto topRight = mapPoint(256.0f, 0.0f);
        const auto bottomLeft = mapPoint(0.0f, 192.0f);
        const auto bottomRight = mapPoint(256.0f, 192.0f);
        float left = topLeft[0];
        float right = topLeft[0];
        float top = topLeft[1];
        float bottom = topLeft[1];
        for (const auto& point :
            {topRight, bottomLeft, bottomRight})
        {
            left = std::min(left, point[0]);
            right = std::max(right, point[0]);
            top = std::min(top, point[1]);
            bottom = std::max(bottom, point[1]);
        }

        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "[VulkanSurfaceConfig] transform index=%u enabled=%d topScreen=%d "
            "matrix=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f] mappedBounds=[%.1f,%.1f,%.1f,%.1f] "
            "determinant=%.6f finite=%d nonDegenerate=%d\n",
            index,
            transform.enabled ? 1 : 0,
            transform.topScreen ? 1 : 0,
            transform.matrix[0],
            transform.matrix[1],
            transform.matrix[2],
            transform.matrix[3],
            transform.matrix[4],
            transform.matrix[5],
            left,
            top,
            right,
            bottom,
            determinant,
            finite ? 1 : 0,
            nonDegenerate ? 1 : 0);
    }

    melonDS::Platform::Log(
        melonDS::Platform::LogLevel::Info,
        "[VulkanSurfaceConfig] surfaceId=%d surfaceWidth=%d surfaceHeight=%d dpr=%.3f "
        "layoutGeneration=%llu numScreens=%d screenTransformCount=%u validGameScreens=%u\n",
        surfaceId,
        newWidth,
        newHeight,
        dpr,
        static_cast<unsigned long long>(layoutGeneration),
        numScreens,
        config.screenTransformCount,
        validGameScreens);

    if ((configuredWidth != newWidth || configuredHeight != newHeight)
        && !presenter->resizeSurface(
            surfaceId, static_cast<u32>(newWidth), static_cast<u32>(newHeight)))
    {
        return false;
    }
    if (!presenter->configureSurface(surfaceId, config, {}))
    {
        return false;
    }

    configuredWidth = newWidth;
    configuredHeight = newHeight;
    configuredLayoutGeneration = layoutGeneration;
    configuredFilter = filter;
    return true;
}

void ScreenPanelVulkan::drawScreen()
{
    refreshClipForGameStateChange();
    if (!presenter)
        return;

    bool expected = false;
    if (!repaintQueued.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;

    const bool queued = QMetaObject::invokeMethod(
        this,
        [this]() {
            repaintQueued.store(false, std::memory_order_release);
            presentOnGuiThread();
        },
        Qt::QueuedConnection);
    if (!queued)
        repaintQueued.store(false, std::memory_order_release);
}

void ScreenPanelVulkan::presentOnGuiThread()
{
    if (QThread::currentThread() != thread() || !presenter || !isVisible()
        || !ensureNativeSurface())
        return;

    const QSize pixelSize = surfaceHost.pixelSize();
    const int currentWidth = pixelSize.width();
    const int currentHeight = pixelSize.height();
    if ((currentWidth != configuredWidth || currentHeight != configuredHeight
            || configuredLayoutGeneration != layoutGeneration
            || configuredFilter != filter)
        && !configureSurface(currentWidth, currentHeight))
    {
        return;
    }

    auto& session = emuInstance->vulkanFrontendSession();
    const bool tracePresenter = (presenterTraceBudget > 0);
    if (tracePresenter)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "[VulkanPresenterTrace] tick panel=%p presenter=%p surface=%d visible=%d\n",
            static_cast<void*>(this),
            static_cast<void*>(presenter.get()),
            surfaceId,
            isVisible() ? 1 : 0);
    }

    Frame* frame = session.acquirePresentFrame();
    if (tracePresenter)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "[VulkanPresenterTrace] acquire frame=%p id=%llu serial=%llu "
            "rendererGen=%llu surfaceGen=%llu expectedSurface=%llu\n",
            static_cast<void*>(frame),
            frame ? static_cast<unsigned long long>(frame->frameId) : 0ull,
            frame ? static_cast<unsigned long long>(frame->frameSerial) : 0ull,
            frame ? static_cast<unsigned long long>(frame->rendererGeneration) : 0ull,
            frame ? static_cast<unsigned long long>(frame->surfaceGeneration) : 0ull,
            static_cast<unsigned long long>(surfaceHost.generation()));
    }
    if (frame == nullptr)
        return;

    // Keep the GUI responsive if the driver or swapchain stops progressing.
    // Teardown may wait indefinitely, but the normal presentation path must
    // always have a bounded budget.
    constexpr u64 kPresentTimeoutNs = 50'000'000ull;
    if (frame->surfaceGeneration != surfaceHost.generation())
    {
        if (tracePresenter)
        {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Info,
                "[VulkanPresenterTrace] defer id=%llu reason=surfaceGenMismatch\n",
                static_cast<unsigned long long>(frame->frameId));
        }
        session.deferPresentedFrame(frame);
        if (tracePresenter)
            --presenterTraceBudget;
        return;
    }

    const VulkanPresentResult presentResult =
        session.presentAcquiredFrame(frame, *presenter, kPresentTimeoutNs);
    if (tracePresenter)
    {
        melonDS::Platform::Log(
            melonDS::Platform::LogLevel::Info,
            "[VulkanPresenterTrace] present id=%llu result=%u\n",
            static_cast<unsigned long long>(frame->frameId),
            static_cast<unsigned>(presentResult));
    }

    if (presentResult == VulkanPresentResult::PresentedGameFrame)
    {
        lastPresentedFrameId = frame->frameId;
        session.commitPresentedFrame(frame);
        if (tracePresenter)
        {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Info,
                "[VulkanPresenterTrace] commit id=%llu\n",
                static_cast<unsigned long long>(frame->frameId));
        }
    }
    else
    {
        session.deferPresentedFrame(frame);
        if (tracePresenter)
        {
            melonDS::Platform::Log(
                melonDS::Platform::LogLevel::Info,
                "[VulkanPresenterTrace] defer id=%llu\n",
                static_cast<unsigned long long>(frame->frameId));
        }
    }

    if (tracePresenter)
        --presenterTraceBudget;

    syncNoRomSplashOverlay();
}

void ScreenPanelVulkan::syncNoRomSplashOverlay()
{
    if (!noRomSplashOverlay)
        return;

    osdUpdate();
    calcSplashLayout();
    noRomSplashOverlay->setGeometry(rect());

    auto* emuThread = emuInstance->getEmuThread();
    const bool hasPresentedGameFrame =
        emuInstance->vulkanFrontendSession().hasPresentedFrame();
    const bool showSplash =
        !emuThread
        || !emuThread->emuIsActive()
        || !hasPresentedGameFrame;

    if (showSplash)
    {
        noRomSplashOverlay->show();
        noRomSplashOverlay->raise();
        noRomSplashOverlay->update();
    }
    else
        noRomSplashOverlay->hide();
}

void ScreenPanelVulkan::resizeEvent(QResizeEvent* event)
{
    ScreenPanel::resizeEvent(event);
    syncNoRomSplashOverlay();
}

void ScreenPanelVulkan::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    ++layoutGeneration;
    update();
}

void ScreenPanelVulkan::paintEvent(QPaintEvent* event)
{
    event->accept();
    refreshClipForGameStateChange();
    presentOnGuiThread();
    syncNoRomSplashOverlay();
}

QPaintEngine* ScreenPanelVulkan::paintEngine() const
{
    return nullptr;
}
