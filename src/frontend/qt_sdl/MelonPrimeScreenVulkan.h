#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeScreenVulkan is owned by the Vulkan build gate"
#endif

#include "Screen.h"

#include <atomic>
#include <cstdint>
#include <memory>

class QPaintEvent;
class QResizeEvent;
class QVulkanWindow;

namespace MelonPrime::Vulkan
{
class Phase13RuntimeState;
}

class ScreenPanelVulkan final : public ScreenPanelNative
{
public:
    explicit ScreenPanelVulkan(QWidget* parent);
    ~ScreenPanelVulkan() override;

    bool initVulkan();
    bool captureVulkanFrame(const QString& outputPath);
    void drawScreen() override;
    const QImage& composeFrameForVulkan();
    bool linearFilteringForVulkan() const { return filter; }
    // MELONPRIME_VULKAN_PHASE13_SCREEN_RUNTIME_V1
    void phase13PresentCompleted();
    void phase13NotifyDeviceLoss(const char* stage, int result);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QVulkanWindow* m_vulkanWindow = nullptr;
    QWidget* m_windowContainer = nullptr;
    QImage m_compositeFrame;
    std::unique_ptr<MelonPrime::Vulkan::Phase13RuntimeState> m_phase13Runtime;
    std::atomic<bool> m_phase13PresentPending{false};
    std::atomic<std::uint64_t> m_phase13QueuedSerial{0};
    std::atomic<std::uint64_t> m_phase13CompletedSerial{0};
};
