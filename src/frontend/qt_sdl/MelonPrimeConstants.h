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

} // namespace MelonPrime


