#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeScreenVulkan is owned by the Vulkan build gate"
#endif

#include "Screen.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

class QEvent;
class QPaintEvent;
class QResizeEvent;
class QVulkanWindow;

namespace MelonPrime::Vulkan
{
class Phase13RuntimeState;
}

// MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P3_V1
struct VulkanDirectFrameSnapshot
{
    std::array<QImage, 2> Screens;

    // MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P6_FRAME_IDENTITY_V1
    // Exact producer identity. Zero disables transfer reuse conservatively.
    std::uint64_t FrameSerial = 0;
    std::uint64_t Generation = 0;

    QSize LogicalSize;
    int NumScreens = 0;
    std::array<std::array<float, 6>, kMaxScreenTransforms> ScreenMatrices{};
    std::array<int, kMaxScreenTransforms> ScreenKinds{};

    QImage HudOverlay;
    QRect HudSource;
    QRect HudDestination;

    bool RadarVisible = false;
    QRectF RadarDestination;
    QPointF RadarSourceCenter;
    float RadarSourceRadius = 0.0f;
    float RadarOpacity = 0.0f;
};

class ScreenPanelVulkan final : public ScreenPanelNative
{
public:
    explicit ScreenPanelVulkan(QWidget* parent);
    ~ScreenPanelVulkan() override;

    bool initVulkan();
    bool captureVulkanFrame(const QString& outputPath);
    void drawScreen() override;
    bool prepareDirectFrameForVulkan(VulkanDirectFrameSnapshot& snapshot);
    const QImage& composeFrameForVulkan();
    bool linearFilteringForVulkan() const { return filter; }
    // MELONPRIME_VULKAN_PHASE13_SCREEN_RUNTIME_V1
    void phase13PresentCompleted();
    void phase13NotifyDeviceLoss(const char* stage, int result);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool copyHighResolutionScreens(QImage& top, QImage& bottom) const override;

private:
    void syncVulkanCursor();

    QVulkanWindow* m_vulkanWindow = nullptr;
    QWidget* m_windowContainer = nullptr;
    QImage m_compositeFrame;

    // MELONPRIME_VULKAN_DIRECT_COMPOSITOR_P4_V1
    // Reuse native snapshots instead of allocating two QImages every present.
    std::array<QImage, 2> m_directNativeScreens;
    std::unique_ptr<MelonPrime::Vulkan::Phase13RuntimeState> m_phase13Runtime;
    std::atomic<bool> m_phase13PresentPending{false};
    std::atomic<std::uint64_t> m_phase13QueuedSerial{0};
    std::atomic<std::uint64_t> m_phase13CompletedSerial{0};

    // MELONPRIME_VULKAN_CURSOR_CHILD_SYNC_V2
    // drawScreen() runs on EmuThread, while QWindow/QWidget cursor mutations
    // belong to the GUI thread. Coalesce cross-thread synchronization requests.
    std::atomic<bool> m_vulkanCursorSyncQueued{false};
};
