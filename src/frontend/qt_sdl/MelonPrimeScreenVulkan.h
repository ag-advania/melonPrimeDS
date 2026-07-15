#pragma once
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeScreenVulkan requires the Vulkan build gate"
#endif
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <QWidget>

class QResizeEvent;
#include "MelonPrimeVulkanSurfaceHost.h"
#include "MelonPrimeVulkanOverlayRenderer.h"
#include "Screen.h"

namespace MelonDSAndroid {
class VulkanSurfacePresenter;
}

class ScreenPanelVulkan final : public ScreenPanel {
public:
    explicit ScreenPanelVulkan(QWidget* parent);
    ~ScreenPanelVulkan() override;
    bool initVulkan();
    void drawScreen() override;

private:
    class NoRomSplashOverlay;

    bool ensureNativeSurface();
    bool hasValidGameScreenLayout() const noexcept;
    bool configureSurface(
        int width, int height, bool managePresenterRegistration = true);
    void presentOnGuiThread();
    void syncNoRomSplashOverlay();
    void setupScreenLayout() override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    QPaintEngine* paintEngine() const override;

    std::unique_ptr<MelonDSAndroid::VulkanSurfacePresenter> presenter;
    MelonPrimeVulkanOverlayRenderer overlayRenderer;
    MelonPrimeVulkanSurfaceHost surfaceHost;
    int surfaceId = 0;
    int configuredWidth = 0;
    int configuredHeight = 0;
    melonDS::u64 configuredLayoutGeneration = 0;
    bool configuredFilter = false;
    melonDS::u64 layoutGeneration = 1;
    melonDS::u64 lastPresentedFrameId = 0;
    std::atomic_bool repaintQueued{false};
    bool sessionPresenterRegistered = false;
    int presenterTraceBudget = 120;
    NoRomSplashOverlay* noRomSplashOverlay = nullptr;
};
