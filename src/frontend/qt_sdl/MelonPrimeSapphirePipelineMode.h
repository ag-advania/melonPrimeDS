#pragma once

namespace MelonDSAndroid
{

enum class SapphirePipelineMode
{
    SapphireExact,
#if !defined(NDEBUG)
    AllTemporalDisabled,
#endif
};

inline SapphirePipelineMode sapphirePipelineMode() noexcept
{
    return SapphirePipelineMode::SapphireExact;
}

inline bool sapphireTemporalEnabled() noexcept
{
    return sapphirePipelineMode() == SapphirePipelineMode::SapphireExact;
}

} // namespace MelonDSAndroid
