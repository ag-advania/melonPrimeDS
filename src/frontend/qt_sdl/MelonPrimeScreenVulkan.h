#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeScreenVulkan is owned by the Vulkan build gate"
#endif

#include "Screen.h"

class QPaintEvent;
class QResizeEvent;
class QVulkanWindow;

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

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QVulkanWindow* m_vulkanWindow = nullptr;
    QWidget* m_windowContainer = nullptr;
    QImage m_compositeFrame;
};
