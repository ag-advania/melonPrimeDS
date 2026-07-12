#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanInstanceHost is owned by the Vulkan build gate"
#endif

#include <QVulkanInstance>

#include <string>

namespace MelonPrime::Vulkan
{

class InstanceHost
{
public:
    InstanceHost();
    ~InstanceHost();

    InstanceHost(const InstanceHost&) = delete;
    InstanceHost& operator=(const InstanceHost&) = delete;

    bool ensureCreated();
    QVulkanInstance& instance() { return m_instance; }
    const QVulkanInstance& instance() const { return m_instance; }
    const std::string& unavailableReason() const { return m_unavailableReason; }
    bool validationEnabled() const { return m_validationEnabled; }

private:
    QVulkanInstance m_instance;
    std::string m_unavailableReason;
    bool m_attempted = false;
    bool m_validationEnabled = false;
};

} // namespace MelonPrime::Vulkan
