#include "MelonPrimeVulkanInstanceHost.h"

#include <QCoreApplication>
#include <QThread>

#include <cstdlib>

#include "Platform.h"

namespace MelonPrime::Vulkan
{

namespace
{

bool ValidationRequested()
{
    if (const char* env = std::getenv("MELONPRIME_VULKAN_VALIDATION"))
        return env[0] == '1';
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    return true;
#else
    return false;
#endif
}

std::string VersionText(const QVersionNumber& version)
{
    return version.toString().toStdString();
}

} // namespace

InstanceHost::InstanceHost() = default;

InstanceHost::~InstanceHost()
{
    if (m_instance.isValid())
        m_instance.destroy();
}

bool InstanceHost::ensureCreated()
{
    if (m_instance.isValid())
        return true;
    if (m_attempted)
        return false;
    m_attempted = true;

    if (!QCoreApplication::instance() ||
        QThread::currentThread() != QCoreApplication::instance()->thread())
    {
        m_unavailableReason = "QVulkanInstance creation must run on the GUI thread";
        return false;
    }

    const QVersionNumber loaderVersion = m_instance.supportedApiVersion();
    const QVersionNumber requiredVersion(1, 1, 0);
    if (QVersionNumber::compare(loaderVersion, requiredVersion) < 0)
    {
        m_unavailableReason = "Vulkan loader does not support Vulkan 1.1";
        melonDS::Platform::Log(melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan unavailable: loader=%s required=1.1\n",
            VersionText(loaderVersion).c_str());
        return false;
    }

    m_instance.setApiVersion(requiredVersion);
    if (ValidationRequested() &&
        m_instance.supportedLayers().contains("VK_LAYER_KHRONOS_validation"))
    {
        m_instance.setLayers({"VK_LAYER_KHRONOS_validation"});
        m_validationEnabled = true;
    }

    if (!m_instance.create())
    {
        m_unavailableReason = "QVulkanInstance::create failed with VkResult " +
            std::to_string(static_cast<int>(m_instance.errorCode()));
        melonDS::Platform::Log(melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan instance creation failed: VkResult=%d\n",
            static_cast<int>(m_instance.errorCode()));
        return false;
    }

    std::string extensionList;
    for (const QByteArray& extension : m_instance.extensions())
    {
        if (!extensionList.empty())
            extensionList += ',';
        extensionList += extension.constData();
    }
    melonDS::Platform::Log(melonDS::Platform::LogLevel::Info,
        "[MelonPrime] Vulkan instance: loader=%s requested=1.1 created=%s "
        "validation=%s extensions=%s\n",
        VersionText(loaderVersion).c_str(),
        VersionText(m_instance.apiVersion()).c_str(),
        m_validationEnabled ? "on" : "off",
        extensionList.c_str());
    return true;
}

} // namespace MelonPrime::Vulkan
