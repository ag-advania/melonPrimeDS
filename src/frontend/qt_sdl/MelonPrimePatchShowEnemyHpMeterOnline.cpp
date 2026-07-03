#ifdef MELONPRIME_DS

#include "MelonPrimePatchShowEnemyHpMeterOnline.h"
#include "MelonPrimePatchCommon.h"
#include "Config.h"
#include "MelonPrimeDef.h"

namespace MelonPrime {
namespace {

static constexpr const char* kCfgShowEnemyHpMeterOnline = MelonPrime::CfgKey::ShowEnemyHpMeterOnline;

// Enemy HP Meter WiFi Force Display
// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
static constexpr PatchWord kPatchWords[7][3] = {
    { // JP1.0
        { 0x0202DBE4u, 0xE1A00000u, 0x128DD044u },
        { 0x0202DBE8u, 0xE1A00000u, 0x18BD4030u },
        { 0x0202DBECu, 0xE1A00000u, 0x112FFF1Eu },
    },
    { // JP1.1
        { 0x0202DBE4u, 0xE1A00000u, 0x128DD044u },
        { 0x0202DBE8u, 0xE1A00000u, 0x18BD4030u },
        { 0x0202DBECu, 0xE1A00000u, 0x112FFF1Eu },
    },
    { // US1.0
        { 0x0202DBC0u, 0xE1A00000u, 0x128DD044u },
        { 0x0202DBC4u, 0xE1A00000u, 0x18BD4030u },
        { 0x0202DBC8u, 0xE1A00000u, 0x112FFF1Eu },
    },
    { // US1.1
        { 0x0202DBC0u, 0xE1A00000u, 0x128DD044u },
        { 0x0202DBC4u, 0xE1A00000u, 0x18BD4030u },
        { 0x0202DBC8u, 0xE1A00000u, 0x112FFF1Eu },
    },
    { // EU1.0
        { 0x0202DBB8u, 0xE1A00000u, 0x128DD044u },
        { 0x0202DBBCu, 0xE1A00000u, 0x18BD4030u },
        { 0x0202DBC0u, 0xE1A00000u, 0x112FFF1Eu },
    },
    { // EU1.1
        { 0x0202DBC0u, 0xE1A00000u, 0x128DD044u },
        { 0x0202DBC4u, 0xE1A00000u, 0x18BD4030u },
        { 0x0202DBC8u, 0xE1A00000u, 0x112FFF1Eu },
    },
    { // KR1.0
        { 0x02036904u, 0xE1A00000u, 0x128DD044u },
        { 0x02036908u, 0xE1A00000u, 0x18BD8030u },
        { 0u, 0u, 0u },
    },
};

static constexpr RomPatchSpan kPatchSpans[7] = {
    { &kPatchWords[0][0], 3 },
    { &kPatchWords[1][0], 3 },
    { &kPatchWords[2][0], 3 },
    { &kPatchWords[3][0], 3 },
    { &kPatchWords[4][0], 3 },
    { &kPatchWords[5][0], 3 },
    { &kPatchWords[6][0], 2 },
};

static StaticWordPatch s_patch(kPatchSpans);

} // namespace

void ShowEnemyHpMeterOnline_ApplyOnce(melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    if (!cfg.GetBool(kCfgShowEnemyHpMeterOnline))
    {
        s_patch.RestoreOnce(nds, romGroupIndex);
        return;
    }

    s_patch.ApplyOnce(nds, romGroupIndex);
}

void ShowEnemyHpMeterOnline_RestoreOnce(melonDS::NDS* nds, uint8_t romGroupIndex)
{
    s_patch.RestoreOnce(nds, romGroupIndex);
}

void ShowEnemyHpMeterOnline_ResetPatchState()
{
    s_patch.ResetState();
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
