#include "MelonPrimeScreenVulkan.h"
#include "Platform.h"
ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent):ScreenPanelNative(parent){}
ScreenPanelVulkan::~ScreenPanelVulkan()=default;
bool ScreenPanelVulkan::initVulkan(){
    // V0 safety presentation: CPU composition remains available while the
    // reference VulkanOutput/VulkanSurfacePresenter are built into the binary.
    melonDS::Platform::Log(melonDS::Platform::LogLevel::Info,
        "[MelonPrime] Vulkan reference modules loaded; CPU safety presentation active, structured 2D metadata disabled\n");
    return true;
}
bool ScreenPanelVulkan::captureVulkanFrame(const QString&){return false;}
void ScreenPanelVulkan::drawScreen(){ScreenPanelNative::drawScreen();}
