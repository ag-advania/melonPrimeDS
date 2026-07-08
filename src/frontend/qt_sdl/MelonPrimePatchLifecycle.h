#pragma once

namespace melonDS {
class NDS;
}

class EmuInstance;

namespace Config {
class Table;
}

namespace MelonPrime {

struct RomAddresses;

namespace PatchLifecycle {

void ResetForEmuStart(melonDS::NDS* nds,
                      EmuInstance* emu,
                      Config::Table& cfg,
                      const RomAddresses& rom);

void ResetForBoot(melonDS::NDS* nds,
                  EmuInstance* emu);

void RestoreForEmuStop(melonDS::NDS* nds,
                       EmuInstance* emu,
                       Config::Table& cfg,
                       const RomAddresses& rom,
                       bool romDetected);

} // namespace PatchLifecycle
} // namespace MelonPrime
