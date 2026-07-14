#pragma once
#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeScreenVulkan requires the Vulkan build gate"
#endif
// MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "MelonPrimeVulkanSurfaceHost.h"
#include "Screen.h"

namespace MelonDSAndroid {
class VulkanSurfacePresenter;
struct VulkanSurfaceOverlay;
}

class ScreenPanelVulkan final : public ScreenPanel {
public:
    explicit ScreenPanelVulkan(QWidget* parent);
    ~ScreenPanelVulkan() override;
    bool initVulkan();
    bool captureVulkanFrame(const QString& outputPath);
    void drawScreen() override;
    void phase13PresentCompleted() {}
    void phase13NotifyDeviceLoss(const char*, int) {}

private:
    struct HudTextCommand
    {
        std::string text;
        int anchor = 0;
        int offsetX = 0;
        int offsetY = 0;
        int align = 0;
        float scale = 0.5f;
        melonDS::u32 color = 0xFFFFFFFFu;
    };

    struct HudRectCommand
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        melonDS::u32 color = 0xFFFFFFFFu;
        int anchor = -1;
    };

    struct HudRadarCommand
    {
        bool enabled = false;
        int anchor = 2;
        int offsetX = 0;
        int offsetY = 0;
        int size = 64;
        float sourceCenterY = 96.0f;
        float sourceRadius = 46.0f;
        float opacity = 0.85f;
        melonDS::u32 frameColor = 0x5098D0FFu;
    };

    struct HudSnapshot
    {
        melonDS::u64 generation = 0;
        std::vector<HudTextCommand> texts;
        std::vector<HudRectCommand> rects;
        HudRadarCommand radar;
    };

    bool ensureNativeSurface();
    bool configureSurface(
        int width, int height, bool managePresenterRegistration = true);
    void presentOnGuiThread();
    void captureHudSnapshotOnEmuThread();
    MelonDSAndroid::VulkanSurfaceOverlay buildOverlayOnGuiThread();
    void setupScreenLayout() override;
    void paintEvent(QPaintEvent* event) override;
    QPaintEngine* paintEngine() const override;

    std::unique_ptr<MelonDSAndroid::VulkanSurfacePresenter> presenter;
    MelonPrimeVulkanSurfaceHost surfaceHost;
    int surfaceId = 0;
    int configuredWidth = 0;
    int configuredHeight = 0;
    melonDS::u64 configuredLayoutGeneration = 0;
    bool configuredFilter = false;
    melonDS::u64 layoutGeneration = 1;
    melonDS::u64 lastPresentedFrameId = 0;
    melonDS::u64 overlayGeneration = 0;
    std::mutex hudSnapshotMutex;
    HudSnapshot hudSnapshot;
    std::atomic_bool repaintQueued{false};
    bool sessionPresenterRegistered = false;
};
