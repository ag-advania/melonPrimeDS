#pragma once
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeScreenVulkan requires the Vulkan build gate"
#endif
// MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1
#include <memory>
#include "MelonPrimeVulkanSurfaceHost.h"
#include "Screen.h"

namespace MelonDSAndroid { class VulkanSurfacePresenter; }

class ScreenPanelVulkan final : public ScreenPanelNative {
public:
    explicit ScreenPanelVulkan(QWidget* parent);
    ~ScreenPanelVulkan() override;
    bool initVulkan();
    bool captureVulkanFrame(const QString& outputPath);
    void drawScreen() override;
    void phase13PresentCompleted() {}
    void phase13NotifyDeviceLoss(const char*, int) {}

private:
    bool ensureNativeSurface();
    bool configureBasicSurface(
        int width, int height, bool managePresenterRegistration = true);

    std::unique_ptr<MelonDSAndroid::VulkanSurfacePresenter> presenter;
    MelonPrimeVulkanSurfaceHost surfaceHost;
    int surfaceId = 0;
    int configuredWidth = 0;
    int configuredHeight = 0;
    bool sessionPresenterRegistered = false;
};
