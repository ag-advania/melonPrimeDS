/*
    Sync melonPrime GPU2D register state into Sapphire GPU2D::Unit snapshots.
*/

#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

namespace melonDS
{
class GPU;
class GPU2D;

namespace SapphireGPU2DCore
{
namespace GPU2D
{
class Unit;

void SyncUnitFromGPU2D(Unit& unit, const melonDS::GPU2D& gpu2d, melonDS::GPU& gpu);
}

} // namespace SapphireGPU2DCore

} // namespace melonDS

#endif
