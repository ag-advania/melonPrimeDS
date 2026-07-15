#pragma once

namespace melonDS
{
class VulkanRenderer3D;
}

class MelonPrimeVulkanFrontendSession;

// Sapphire 0.7.0.rc4 runFrame producer transaction adapter.
class SapphireVulkanFramePipeline
{
public:
    explicit SapphireVulkanFramePipeline(MelonPrimeVulkanFrontendSession& session);

    bool beginProducerTransaction(melonDS::VulkanRenderer3D& renderer3D);
    bool completeProducerTransaction(melonDS::VulkanRenderer3D& renderer3D);
    void cancelProducerTransaction();

private:
    MelonPrimeVulkanFrontendSession& session_;
};
