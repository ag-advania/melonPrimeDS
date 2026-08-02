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

// Bottom-screen colors retained by the Custom HUD radar. OpenGL uploads this
// table to its radar shader; software rendering uses the same packed RGB values
// for its CPU-side color key. Keep the Vulkan shader table in sync as well.
constexpr uint32_t kRadarPaletteColors[] = {
    0xC0F868, // yellow-green
    0xF8A8A8, // node red middle
    0xE03030, // node red outer and center
    0xA0A0A0, // octolith gray top
    0xC8C8C8, // octolith gray center
    0x909090, // octolith gray bottom
    0xF88010, // octolith orange top
    0xF8D0A0, // octolith orange center
    0xD86800, // octolith orange bottom
    0x88E008, // octolith green top
    0xC8F880, // octolith green center
    0x68B800, // octolith green bottom
    0x1098C8, // node blue outer and center
    0x28D8F8, // node blue middle
    0xA8A8A8, // node gray
};
constexpr int kRadarPaletteColorCount =
    static_cast<int>(sizeof(kRadarPaletteColors) / sizeof(kRadarPaletteColors[0]));
// The software and Vulkan compositors expand DS 6-bit channels to the full
// 8-bit range, while the established OpenGL radar palette is expressed as
// 5-bit channel values shifted left by three. Ignore those expansion bits so
// all renderers identify the same source colors.
constexpr uint32_t kRadarPaletteQuantizationMask = 0x00F8F8F8u;

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
