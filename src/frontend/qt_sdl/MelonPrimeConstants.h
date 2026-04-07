#pragma once

namespace MelonPrime {

enum class HunterId
{
    Samus,
    Kanden,
    Trace,
    Sylux,
    Noxus,
    Spire,
    Weavel,
    Count,
};

constexpr int kCustomHudFontSize = 6;
constexpr int kBtmOverlaySrcCenterX = 128;
constexpr int kHunterCount = static_cast<int>(HunterId::Count);
constexpr int kBtmOverlaySrcCenterYSamus = 112;
constexpr int kBtmOverlaySrcCenterYKanden = 112;
constexpr int kBtmOverlaySrcCenterYTrace = 128;
constexpr int kBtmOverlaySrcCenterYSylux = 112;
constexpr int kBtmOverlaySrcCenterYNoxus = 120;
constexpr int kBtmOverlaySrcCenterYSpire = 120;
constexpr int kBtmOverlaySrcCenterYWeavel = 112;
// Radar source center Y in HunterId order.
constexpr int kBtmOverlaySrcCenterY[kHunterCount] = {
    kBtmOverlaySrcCenterYSamus,
    kBtmOverlaySrcCenterYKanden,
    kBtmOverlaySrcCenterYTrace,
    kBtmOverlaySrcCenterYSylux,
    kBtmOverlaySrcCenterYNoxus,
    kBtmOverlaySrcCenterYSpire,
    kBtmOverlaySrcCenterYWeavel,
};

static_assert(static_cast<int>(HunterId::Weavel) + 1 == kHunterCount,
    "HunterId and kBtmOverlaySrcCenterY must stay in sync");

// Per-hunter radar frame colors (RGB packed as 0xRRGGBB), in HunterId order.
constexpr uint32_t kHunterFrameColor[kHunterCount] = {
    0x68E028, // Samus
    0xF8F858, // Kanden
    0xE01018, // Trace
    0xD0F0A0, // Sylux
    0x5098D0, // Noxus
    0xF87038, // Spire
    0xD09838, // Weavel
};

} // namespace MelonPrime


