#pragma once

namespace MelonPrime::VulkanDebugIsolation
{

enum class ProducerBeginGate
{
    Enabled,
    Disabled,
};

enum class Sapphire2DGate
{
    Enabled,
    Disabled,
};

ProducerBeginGate producerBeginGate();
Sapphire2DGate sapphire2DGate();

} // namespace MelonPrime::VulkanDebugIsolation
