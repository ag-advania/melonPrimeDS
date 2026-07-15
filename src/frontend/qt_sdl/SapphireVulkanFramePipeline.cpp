#include "SapphireVulkanFramePipeline.h"

#include "MelonPrimeVulkanFrontendSession.h"

SapphireVulkanFramePipeline::SapphireVulkanFramePipeline(
    MelonPrimeVulkanFrontendSession& session)
    : session_(session)
{
}

bool SapphireVulkanFramePipeline::beginProducerTransaction(
    melonDS::VulkanRenderer3D& renderer3D)
{
    return session_.beginProducerFrame(renderer3D);
}

bool SapphireVulkanFramePipeline::completeProducerTransaction(
    melonDS::VulkanRenderer3D& renderer3D)
{
    return session_.completeProducerFrame(renderer3D);
}

void SapphireVulkanFramePipeline::cancelProducerTransaction()
{
    session_.cancelProducerFrame();
}
