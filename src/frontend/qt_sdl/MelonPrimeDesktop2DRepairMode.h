#pragma once

namespace MelonDSAndroid
{

enum class Desktop2DRepairMode
{
    CurrentFrameOnly,
    ExactProvenanceRepair,
    LegacyHeuristicRepair,
};

inline Desktop2DRepairMode desktop2DRepairMode() noexcept
{
    return Desktop2DRepairMode::CurrentFrameOnly;
}

inline bool desktop2DTemporalRepairEnabled() noexcept
{
    return desktop2DRepairMode() != Desktop2DRepairMode::CurrentFrameOnly;
}

inline bool desktop2DExactProvenanceRepairEnabled() noexcept
{
    return desktop2DRepairMode() == Desktop2DRepairMode::ExactProvenanceRepair
        || desktop2DRepairMode() == Desktop2DRepairMode::LegacyHeuristicRepair;
}

inline bool desktop2DLegacyHeuristicRepairEnabled() noexcept
{
    return desktop2DRepairMode() == Desktop2DRepairMode::LegacyHeuristicRepair;
}

} // namespace MelonDSAndroid
