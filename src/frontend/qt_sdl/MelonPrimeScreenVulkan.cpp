#include "MelonPrimeScreenVulkan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <QDateTime>
#include <QMetaObject>
#include <QPaintEvent>
#include <QThread>

#include "EmuInstance.h"
#include "MelonPrime.h"
#include "MelonPrimeConstants.h"
#include "MelonPrimeGameRomAddrTable.h"
#include "MelonPrimeHudGeometry.h"
#include "MelonPrimeHudPropSchema.inc"
#include "MelonPrimeHudRender.h"
#include "MelonPrimeInternal.h"
#include "MelonPrimeVulkanFrontendSession.h"
#include "NDS.h"
#include "Platform.h"
#include "VulkanContext.h"

#include "VulkanReference/VulkanSurfacePresenter.h"

using namespace MelonDSAndroid;

extern unsigned short font[];

namespace
{
u32 PackRgba(int red, int green, int blue, int alpha = 255)
{
    return (static_cast<u32>(std::clamp(red, 0, 255)) << 24)
        | (static_cast<u32>(std::clamp(green, 0, 255)) << 16)
        | (static_cast<u32>(std::clamp(blue, 0, 255)) << 8)
        | static_cast<u32>(std::clamp(alpha, 0, 255));
}
}

ScreenPanelVulkan::ScreenPanelVulkan(QWidget* parent)
    : ScreenPanel(parent, false)
{
    setAutoFillBackground(false);
    setAttribute(Qt::WA_NativeWindow, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_PaintOnScreen, true);
    setAttribute(Qt::WA_KeyCompression, false);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(screenGetMinSize(1));
}

ScreenPanelVulkan::~ScreenPanelVulkan()
{
    if (presenter)
    {
        if (sessionPresenterRegistered)
            emuInstance->vulkanFrontendSession().unregisterPresenter(presenter.get());
        sessionPresenterRegistered = false;
        if (surfaceId != 0)
            presenter->detachSurface(surfaceId);
        surfaceId = 0;
    }
    surfaceHost.destroy(melonDS::VulkanContext::Get().GetInstance());
    if (presenter)
        presenter->shutdown();
}

bool ScreenPanelVulkan::initVulkan()
{
    if (QThread::currentThread() != thread())
        return false;

    presenter = std::make_unique<VulkanSurfacePresenter>();
    if (!presenter->init())
        return false;

    if (!ensureNativeSurface())
    {
        if (surfaceId != 0)
            presenter->detachSurface(surfaceId);
        surfaceId = 0;
        surfaceHost.destroy(melonDS::VulkanContext::Get().GetInstance());
        presenter->shutdown();
        presenter.reset();
        return false;
    }

    emuInstance->vulkanFrontendSession().registerPresenter(presenter.get());
    sessionPresenterRegistered = true;
    melonDS::Platform::Log(melonDS::Platform::LogLevel::Info,
        "[MelonPrime] Vulkan frontend session attached to desktop surface generation %llu\n",
        static_cast<unsigned long long>(surfaceHost.generation()));
    return true;
}

bool ScreenPanelVulkan::ensureNativeSurface()
{
    if (!presenter)
        return false;
    if (surfaceId != 0 && surfaceHost.matchesWidget(*this))
        return true;

    auto& context = melonDS::VulkanContext::Get();
    auto& session = emuInstance->vulkanFrontendSession();
    const bool presenterWasRegistered = sessionPresenterRegistered;
    if (presenterWasRegistered)
        session.unregisterPresenter(presenter.get());
    if (surfaceId != 0)
    {
        presenter->detachSurface(surfaceId);
        surfaceId = 0;
    }
    surfaceHost.destroy(context.GetInstance());
    configuredWidth = 0;
    configuredHeight = 0;

    if (!surfaceHost.createForWidget(*this, context.GetInstance()))
        return false;
    const QSize size = surfaceHost.pixelSize();
    surfaceId = presenter->attachSurface(
        surfaceHost.surface(),
        static_cast<u32>(size.width()),
        static_cast<u32>(size.height()));
    if (surfaceId == 0)
    {
        surfaceHost.destroy(context.GetInstance());
        return false;
    }

    session.beginSurfaceGeneration(surfaceHost.generation());
    if (configureSurface(size.width(), size.height(), false))
    {
        if (presenterWasRegistered)
            session.registerPresenter(presenter.get());
        return true;
    }

    presenter->detachSurface(surfaceId);
    surfaceId = 0;
    surfaceHost.destroy(context.GetInstance());
    return false;
}

bool ScreenPanelVulkan::configureSurface(
    int newWidth, int newHeight, bool managePresenterRegistration)
{
    if (!presenter || surfaceId == 0 || newWidth <= 0 || newHeight <= 0)
        return false;

    auto& session = emuInstance->vulkanFrontendSession();
    const bool presenterWasRegistered =
        managePresenterRegistration && sessionPresenterRegistered;
    if (presenterWasRegistered)
        session.unregisterPresenter(presenter.get());

    VulkanSurfaceConfig config{};
    const float dpr = static_cast<float>(devicePixelRatioF());
    config.screenTransformCount = static_cast<u32>(
        std::clamp(numScreens, 0, static_cast<int>(config.screenTransforms.size())));
    for (u32 index = 0; index < config.screenTransformCount; ++index)
    {
        auto& destination = config.screenTransforms[index];
        destination.enabled = true;
        destination.topScreen = screenKind[index] == 0;
        for (size_t coefficient = 0; coefficient < destination.matrix.size(); ++coefficient)
            destination.matrix[coefficient] = screenMatrix[index][coefficient] * dpr;
    }
    config.filtering = filter ? VulkanFilterMode::Linear : VulkanFilterMode::Nearest;

    if ((configuredWidth != newWidth || configuredHeight != newHeight)
        && !presenter->resizeSurface(
            surfaceId, static_cast<u32>(newWidth), static_cast<u32>(newHeight)))
    {
        if (presenterWasRegistered)
            session.registerPresenter(presenter.get());
        return false;
    }
    if (!presenter->configureSurface(surfaceId, config, {}))
    {
        if (presenterWasRegistered)
            session.registerPresenter(presenter.get());
        return false;
    }

    configuredWidth = newWidth;
    configuredHeight = newHeight;
    configuredLayoutGeneration = layoutGeneration;
    configuredFilter = filter;
    if (presenterWasRegistered)
        session.registerPresenter(presenter.get());
    return true;
}

void ScreenPanelVulkan::captureHudSnapshotOnEmuThread()
{
    // Never let a future GUI-side repaint call site turn this into a live RAM
    // read. drawScreen() is the producer-thread publication boundary.
    if (QThread::currentThread() == thread())
        return;

    HudSnapshot next{};
    {
        std::scoped_lock lock(hudSnapshotMutex);
        next.generation = hudSnapshot.generation + 1;
    }

#ifdef MELONPRIME_CUSTOM_HUD
    auto* core = melonPrimeCoreForPolicy();
    auto* nds = emuInstance ? emuInstance->getNDS() : nullptr;
    auto& cfg = emuInstance->getLocalConfig();
    if (core && nds && nds->MainRAM)
    {
        const auto& rom = core->GetCurrentRom();
        const auto& hot = core->GetAddrHot();
        const u8 playerPosition = core->GetPlayerPosition();
        u8* ram = nds->MainRAM;
        const u32 playerOffset = static_cast<u32>(playerPosition)
            * MelonPrime::Consts::PLAYER_ADDR_INC;
        const bool isInGame = core->ThreadBridge().ReadForGui().inGame;

        if (MelonPrime::CustomHud_SyncVulkanPatch(
                core->HudConfigState(), emuInstance, cfg, rom,
                playerPosition, isInGame))
        {
            const auto color = [&](const char* r, const char* g, const char* b,
                                   int alpha = 255) {
                return PackRgba(cfg.GetInt(r), cfg.GetInt(g), cfg.GetInt(b), alpha);
            };
            const auto addText = [&](std::string text, int anchor, int x, int y,
                                     int align, float scale, u32 rgba) {
                next.texts.push_back(HudTextCommand{
                    std::move(text), anchor, x, y, align, scale, rgba});
            };
            const auto addGauge = [&](int anchor, int x, int y, int orientation,
                                      int align, int length, int width,
                                      float ratio, u32 fillColor) {
                length = std::max(length, 1);
                width = std::max(width, 1);
                ratio = std::clamp(ratio, 0.0f, 1.0f);
                if (orientation == 0)
                {
                    x -= length * align / 2;
                    next.rects.push_back({static_cast<float>(x),
                        static_cast<float>(y), static_cast<float>(length),
                        static_cast<float>(width), 0x00000080u, anchor});
                    next.rects.push_back({static_cast<float>(x),
                        static_cast<float>(y), length * ratio,
                        static_cast<float>(width), fillColor, anchor});
                }
                else
                {
                    y -= length * align / 2;
                    const float fillHeight = length * ratio;
                    next.rects.push_back({static_cast<float>(x),
                        static_cast<float>(y), static_cast<float>(width),
                        static_cast<float>(length), 0x00000080u, anchor});
                    next.rects.push_back({static_cast<float>(x),
                        y + length - fillHeight, static_cast<float>(width),
                        fillHeight, fillColor, anchor});
                }
            };

            const u16 hp = MelonPrime::Read16(ram, rom.playerHP + playerOffset);
            const u16 maxHp = MelonPrime::Read16(ram, rom.maxHP + playerOffset);
            addText(
                cfg.GetString(MP_HUD_PROP_KEY_HudHpPrefix) + std::to_string(hp),
                cfg.GetInt(MP_HUD_PROP_KEY_HudHpAnchor),
                cfg.GetInt(MP_HUD_PROP_KEY_HudHpX),
                cfg.GetInt(MP_HUD_PROP_KEY_HudHpY),
                cfg.GetInt(MP_HUD_PROP_KEY_HudHpAlign), 0.5f,
                color(MP_HUD_PROP_KEY_HudHpTextColorR,
                      MP_HUD_PROP_KEY_HudHpTextColorG,
                      MP_HUD_PROP_KEY_HudHpTextColorB));
            if (cfg.GetBool(MP_HUD_PROP_KEY_HudHpGauge) && maxHp > 0)
            {
                addGauge(
                    cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosAnchor),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosX),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugePosY),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeOrientation),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeAlign),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeLength),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudHpGaugeWidth),
                    static_cast<float>(hp) / static_cast<float>(maxHp),
                    color(MP_HUD_PROP_KEY_HudHpGaugeColorR,
                          MP_HUD_PROP_KEY_HudHpGaugeColorG,
                          MP_HUD_PROP_KEY_HudHpGaugeColorB,
                          static_cast<int>(std::clamp(
                              cfg.GetDouble(MP_HUD_PROP_KEY_HudHpGaugeOpacity),
                              0.0, 1.0) * 255.0)));
            }

            const u8 weapon = MelonPrime::Read8(ram, hot.currentWeapon);
            static constexpr u16 kAmmoDivisor[9] = {0, 5, 10, 4, 20, 5, 10, 10, 1};
            static constexpr bool kMissileWeapon[9] = {
                false, false, true, false, false, false, false, false, false};
            if (weapon < 9 && kAmmoDivisor[weapon] != 0)
            {
                const u16 rawAmmo = MelonPrime::Read16(
                    ram,
                    (kMissileWeapon[weapon] ? rom.currentAmmoMissile
                                            : rom.currentAmmoSpecial)
                        + playerOffset);
                const u16 ammo = rawAmmo / kAmmoDivisor[weapon];
                const u16 rawMaxAmmo = MelonPrime::Read16(
                    ram,
                    (kMissileWeapon[weapon] ? rom.maxAmmoMissile
                                            : rom.maxAmmoSpecial)
                        + playerOffset);
                const u16 maxAmmo = rawMaxAmmo / kAmmoDivisor[weapon];
                addText(
                    cfg.GetString(MP_HUD_PROP_KEY_HudAmmoPrefix)
                        + std::to_string(ammo),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponAnchor),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponX),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudWeaponY),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoAlign), 0.5f,
                    color(MP_HUD_PROP_KEY_HudAmmoTextColorR,
                          MP_HUD_PROP_KEY_HudAmmoTextColorG,
                          MP_HUD_PROP_KEY_HudAmmoTextColorB));
                if (cfg.GetBool(MP_HUD_PROP_KEY_HudAmmoGauge) && maxAmmo > 0)
                {
                    addGauge(
                        cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosAnchor),
                        cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosX),
                        cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugePosY),
                        cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeOrientation),
                        cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeAlign),
                        cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeLength),
                        cfg.GetInt(MP_HUD_PROP_KEY_HudAmmoGaugeWidth),
                        static_cast<float>(ammo) / static_cast<float>(maxAmmo),
                        color(MP_HUD_PROP_KEY_HudAmmoGaugeColorR,
                              MP_HUD_PROP_KEY_HudAmmoGaugeColorG,
                              MP_HUD_PROP_KEY_HudAmmoGaugeColorB,
                              static_cast<int>(std::clamp(
                                  cfg.GetDouble(MP_HUD_PROP_KEY_HudAmmoGaugeOpacity),
                                  0.0, 1.0) * 255.0)));
                }
            }

            if (cfg.GetBool(MP_HUD_PROP_KEY_HudRankShow))
            {
                const u32 rankWord = MelonPrime::Read32(ram, rom.matchRank);
                const u32 rank = ((rankWord >> (playerPosition * 8)) & 0xFFu) + 1u;
                addText(
                    cfg.GetString(MP_HUD_PROP_KEY_HudRankPrefix)
                        + std::to_string(rank)
                        + cfg.GetString(MP_HUD_PROP_KEY_HudRankSuffix),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudRankAnchor),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudRankX),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudRankY),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudRankAlign), 0.5f,
                    color(MP_HUD_PROP_KEY_HudRankColorR,
                          MP_HUD_PROP_KEY_HudRankColorG,
                          MP_HUD_PROP_KEY_HudRankColorB));
            }

            if (cfg.GetBool(MP_HUD_PROP_KEY_HudTimeLeftShow))
            {
                const u32 seconds = MelonPrime::Read32(ram, rom.timeLeft) / 60u;
                char timeText[16]{};
                std::snprintf(timeText, sizeof(timeText), "%u:%02u",
                    seconds / 60u, seconds % 60u);
                addText(
                    timeText,
                    cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLeftAnchor),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLeftX),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLeftY),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudTimeLeftAlign), 0.5f,
                    color(MP_HUD_PROP_KEY_HudTimeLeftColorR,
                          MP_HUD_PROP_KEY_HudTimeLeftColorG,
                          MP_HUD_PROP_KEY_HudTimeLeftColorB));
            }

            const u8 hunter = MelonPrime::Read8(ram, hot.chosenHunter);
            const u8 viewMode = MelonPrime::Read8(
                ram, rom.baseViewMode + playerOffset);
            const bool altForm = MelonPrime::Read8(ram, hot.isAltForm) == 0x02;
            if (cfg.GetBool(MP_HUD_PROP_KEY_HudBombLeftShow)
                && altForm
                && (hunter == static_cast<u8>(MelonPrime::HunterId::Samus)
                    || hunter == static_cast<u8>(MelonPrime::HunterId::Sylux)))
            {
                const u32 bombs = (MelonPrime::Read32(
                    ram, rom.baseBomb + playerOffset) >> 8) & 0xFu;
                addText(
                    cfg.GetString(MP_HUD_PROP_KEY_HudBombLeftPrefix)
                        + std::to_string(bombs)
                        + cfg.GetString(MP_HUD_PROP_KEY_HudBombLeftSuffix),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftAnchor),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftX),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftY),
                    cfg.GetInt(MP_HUD_PROP_KEY_HudBombLeftAlign), 0.5f,
                    color(MP_HUD_PROP_KEY_HudBombLeftColorR,
                          MP_HUD_PROP_KEY_HudBombLeftColorG,
                          MP_HUD_PROP_KEY_HudBombLeftColorB));
            }

            if (viewMode == 0 && !altForm)
            {
                const float centerX = static_cast<float>(
                    MelonPrime::Read8(ram, rom.crosshairPosX));
                const float centerY = static_cast<float>(
                    MelonPrime::Read8(ram, rom.crosshairPosY));
                const float scale = std::max(
                    0.1, cfg.GetDouble(MP_HUD_PROP_KEY_CrosshairScale));
                const float thickness = std::max(
                    1.0f, static_cast<float>(cfg.GetInt(
                        MP_HUD_PROP_KEY_CrosshairThickness)) * scale);
                const u32 crosshairColor = color(
                    MP_HUD_PROP_KEY_CrosshairColorR,
                    MP_HUD_PROP_KEY_CrosshairColorG,
                    MP_HUD_PROP_KEY_CrosshairColorB);
                const auto addArms = [&](bool enabled, const char* lengthXKey,
                                         const char* lengthYKey, const char* offsetKey) {
                    if (!enabled)
                        return;
                    const float lengthX = cfg.GetInt(lengthXKey) * scale;
                    const float lengthY = cfg.GetInt(lengthYKey) * scale;
                    const float offset = cfg.GetInt(offsetKey) * scale;
                    const float half = thickness * 0.5f;
                    if (lengthX > 0.0f)
                    {
                        next.rects.push_back({centerX - offset - lengthX,
                            centerY - half, lengthX, thickness, crosshairColor});
                        next.rects.push_back({centerX + offset,
                            centerY - half, lengthX, thickness, crosshairColor});
                    }
                    if (lengthY > 0.0f)
                    {
                        next.rects.push_back({centerX - half,
                            centerY + offset, thickness, lengthY, crosshairColor});
                        if (!cfg.GetBool(MP_HUD_PROP_KEY_CrosshairTStyle))
                            next.rects.push_back({centerX - half,
                                centerY - offset - lengthY,
                                thickness, lengthY, crosshairColor});
                    }
                };
                addArms(cfg.GetBool(MP_HUD_PROP_KEY_CrosshairInnerShow),
                    MP_HUD_PROP_KEY_CrosshairInnerLengthX,
                    MP_HUD_PROP_KEY_CrosshairInnerLengthY,
                    MP_HUD_PROP_KEY_CrosshairInnerOffset);
                addArms(cfg.GetBool(MP_HUD_PROP_KEY_CrosshairOuterShow),
                    MP_HUD_PROP_KEY_CrosshairOuterLengthX,
                    MP_HUD_PROP_KEY_CrosshairOuterLengthY,
                    MP_HUD_PROP_KEY_CrosshairOuterOffset);
                if (cfg.GetBool(MP_HUD_PROP_KEY_CrosshairCenterDot))
                {
                    const float dot = std::max(1.0f,
                        static_cast<float>(cfg.GetInt(
                            MP_HUD_PROP_KEY_CrosshairDotThickness)) * scale);
                    next.rects.push_back({centerX - dot * 0.5f,
                        centerY - dot * 0.5f, dot, dot, crosshairColor});
                }
            }

            if (cfg.GetBool(MP_HUD_PROP_KEY_BtmOverlayEnable)
                && MelonPrime::CustomHud_ShouldDrawRadarOverlay(
                    emuInstance, rom, playerPosition))
            {
                next.radar.enabled = true;
                next.radar.anchor = cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayAnchor);
                next.radar.offsetX = cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstX);
                next.radar.offsetY = cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstY);
                next.radar.size = std::max(
                    1, cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlayDstSize));
                next.radar.sourceCenterY = hunter < MelonPrime::kHunterCount
                    ? static_cast<float>(MelonPrime::kBtmOverlaySrcCenterY[hunter])
                    : 96.0f;
                next.radar.sourceRadius = static_cast<float>(std::max(
                    1, cfg.GetInt(MP_HUD_PROP_KEY_BtmOverlaySrcRadius)));
                next.radar.opacity = static_cast<float>(std::clamp(
                    cfg.GetDouble(MP_HUD_PROP_KEY_BtmOverlayOpacity), 0.0, 1.0));
                u32 radarRgb = 0;
                if (cfg.GetBool(MP_HUD_PROP_KEY_BtmOverlayRadarColorUseHunter)
                    && hunter < MelonPrime::kHunterCount)
                {
                    radarRgb = MelonPrime::kHunterFrameColor[hunter];
                }
                else
                {
                    radarRgb = (static_cast<u32>(cfg.GetInt(
                        MP_HUD_PROP_KEY_BtmOverlayRadarColorR)) << 16)
                        | (static_cast<u32>(cfg.GetInt(
                            MP_HUD_PROP_KEY_BtmOverlayRadarColorG)) << 8)
                        | static_cast<u32>(cfg.GetInt(
                            MP_HUD_PROP_KEY_BtmOverlayRadarColorB));
                }
                next.radar.frameColor = (radarRgb << 8) | 0xFFu;
            }
        }
    }
#endif

    std::scoped_lock lock(hudSnapshotMutex);
    hudSnapshot = std::move(next);
}

void ScreenPanelVulkan::drawScreen()
{
    refreshClipForGameStateChange();
    captureHudSnapshotOnEmuThread();
    if (!presenter)
        return;

    bool expected = false;
    if (!repaintQueued.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;

    const bool queued = QMetaObject::invokeMethod(
        this,
        [this]() {
            repaintQueued.store(false, std::memory_order_release);
            update();
        },
        Qt::QueuedConnection);
    if (!queued)
        repaintQueued.store(false, std::memory_order_release);
}

VulkanSurfaceOverlay ScreenPanelVulkan::buildOverlayOnGuiThread()
{
    HudSnapshot snapshot;
    {
        std::scoped_lock lock(hudSnapshotMutex);
        snapshot = hudSnapshot;
    }

    VulkanSurfaceOverlay overlay{};
    overlay.generation = ++overlayGeneration;
    constexpr size_t kMaxSolidQuads = 10'000;
    const float dpr = static_cast<float>(devicePixelRatioF());
#ifdef MELONPRIME_CUSTOM_HUD
    const float hudTopStretchX = m_topStretchX;
#else
    constexpr float hudTopStretchX = 1.0f;
#endif

    const auto unpackColor = [](u32 rgba) {
        constexpr float kColorScale = 1.0f / 255.0f;
        return std::array<float, 4>{
            static_cast<float>((rgba >> 24) & 0xFFu) * kColorScale,
            static_cast<float>((rgba >> 16) & 0xFFu) * kColorScale,
            static_cast<float>((rgba >> 8) & 0xFFu) * kColorScale,
            static_cast<float>(rgba & 0xFFu) * kColorScale};
    };
    const auto appendQuad = [&](const std::array<float, 8>& points, u32 color) {
        if (overlay.solidQuads.size() >= kMaxSolidQuads)
            return;
        overlay.solidQuads.push_back(VulkanOverlayQuad{points, unpackColor(color)});
    };
    const auto mapDsRect = [&](int transformIndex, float x, float y,
                               float width, float height) {
        const float* matrix = screenMatrix[transformIndex];
        const auto map = [&](float px, float py) {
            return std::array<float, 2>{
                (matrix[0] * px + matrix[2] * py + matrix[4]) * dpr,
                (matrix[1] * px + matrix[3] * py + matrix[5]) * dpr};
        };
        const auto tl = map(x, y);
        const auto tr = map(x + width, y);
        const auto bl = map(x, y + height);
        const auto br = map(x + width, y + height);
        return std::array<float, 8>{
            tl[0], tl[1], tr[0], tr[1],
            bl[0], bl[1], br[0], br[1]};
    };
    const auto directRect = [&](float x, float y, float width, float height) {
        x *= dpr;
        y *= dpr;
        width *= dpr;
        height *= dpr;
        return std::array<float, 8>{
            x, y, x + width, y, x, y + height, x + width, y + height};
    };
    const auto measureText = [](const std::string& text, float scale) {
        float width = 0.0f;
        for (unsigned char raw : text)
        {
            const u32 character = (raw < 0x10 || raw > 0x7E) ? 0x7Fu : raw;
            if (character == ' ')
            {
                width += 6.0f * scale;
                continue;
            }
            const unsigned short* glyph = &::font[(character - 0x10u) << 4u];
            width += static_cast<float>(glyph[0] == 0 ? 6 : glyph[0] + 2) * scale;
        }
        return width;
    };
    const auto appendTextPixels = [&](const std::string& text, float originX,
                                      float originY, float scale, u32 color,
                                      const auto& mapPixel) {
        float cursorX = originX;
        for (unsigned char raw : text)
        {
            const u32 character = (raw < 0x10 || raw > 0x7E) ? 0x7Fu : raw;
            if (character == ' ')
            {
                cursorX += 6.0f * scale;
                continue;
            }
            const unsigned short* glyph = &::font[(character - 0x10u) << 4u];
            const int glyphWidth = glyph[0] == 0 ? 0 : glyph[0];
            if (glyphWidth == 0)
            {
                cursorX += 6.0f * scale;
                continue;
            }
            cursorX += scale;
            for (int row = 0; row < 12; ++row)
            {
                const u16 bits = glyph[4 + row];
                for (int column = 0; column < glyphWidth; ++column)
                {
                    if ((bits & (1u << column)) != 0)
                        appendQuad(mapPixel(
                            cursorX + column * scale,
                            originY + row * scale,
                            scale, scale), color);
                }
            }
            cursorX += (glyphWidth + 1) * scale;
        }
    };

    for (int transformIndex = 0; transformIndex < numScreens; ++transformIndex)
    {
        if (screenKind[transformIndex] != 0)
            continue;

        for (const HudRectCommand& rect : snapshot.rects)
        {
            float rectX = rect.x;
            float rectY = rect.y;
            if (rect.anchor >= 0)
            {
                int anchoredX = 0;
                int anchoredY = 0;
                MelonPrime::HudGeometry::ApplyAnchor(
                    rect.anchor,
                    static_cast<int>(std::lround(rect.x)),
                    static_cast<int>(std::lround(rect.y)),
                    anchoredX, anchoredY, hudTopStretchX);
                rectX = static_cast<float>(anchoredX);
                rectY = static_cast<float>(anchoredY);
            }
            appendQuad(mapDsRect(transformIndex, rectX, rectY,
                rect.width, rect.height), rect.color);
        }

        for (const HudTextCommand& text : snapshot.texts)
        {
            int anchorX = 0;
            int anchorY = 0;
            MelonPrime::HudGeometry::ApplyAnchor(
                text.anchor, text.offsetX, text.offsetY,
                anchorX, anchorY, hudTopStretchX);
            const float textWidth = measureText(text.text, text.scale);
            const float x = MelonPrime::HudGeometry::AlignedTextX(
                static_cast<float>(anchorX), text.align, textWidth);
            const float y = static_cast<float>(anchorY) - 12.0f * text.scale;
            appendTextPixels(text.text, x, y, text.scale, text.color,
                [&](float px, float py, float width, float height) {
                    return mapDsRect(transformIndex, px, py, width, height);
                });
        }

        if (snapshot.radar.enabled && !overlay.radar.enabled)
        {
            int radarX = 0;
            int radarY = 0;
            MelonPrime::HudGeometry::ApplyAnchor(
                snapshot.radar.anchor,
                snapshot.radar.offsetX,
                snapshot.radar.offsetY,
                radarX, radarY, hudTopStretchX);
            overlay.radar.enabled = true;
            overlay.radar.points = mapDsRect(
                transformIndex, static_cast<float>(radarX),
                static_cast<float>(radarY),
                static_cast<float>(snapshot.radar.size),
                static_cast<float>(snapshot.radar.size));
            overlay.radar.sourceCenterY = snapshot.radar.sourceCenterY;
            overlay.radar.sourceRadius = snapshot.radar.sourceRadius;
            overlay.radar.opacity = snapshot.radar.opacity;
            overlay.radar.frameColor = unpackColor(snapshot.radar.frameColor);
        }
    }

    // Vulkan OSD keeps only text metadata. It never calls osdRenderItem(), so
    // no QImage is allocated or uploaded for this presenter.
    osdMutex.lock();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 oldest = now - 2500;
    for (auto iterator = osdItems.begin(); iterator != osdItems.end(); )
    {
        if (!osdEnabled || iterator->timestamp < oldest)
            iterator = osdItems.erase(iterator);
        else
            ++iterator;
    }
    float osdY = 6.0f;
    for (const OSDItem& item : osdItems)
    {
        const u32 rgb = item.color == 0 ? 0xFFFFFFu : item.color & 0xFFFFFFu;
        const u32 rgba = (rgb << 8) | 0xFFu;
        appendTextPixels(item.text, 6.0f, osdY, 1.0f, rgba,
            [&](float x, float y, float width, float height) {
                return directRect(x, y, width, height);
            });
        osdY += 14.0f;
    }
    osdMutex.unlock();

    return overlay;
}

void ScreenPanelVulkan::presentOnGuiThread()
{
    if (QThread::currentThread() != thread() || !presenter || !isVisible()
        || !ensureNativeSurface())
        return;

    const QSize pixelSize = surfaceHost.pixelSize();
    const int currentWidth = pixelSize.width();
    const int currentHeight = pixelSize.height();
    if ((currentWidth != configuredWidth || currentHeight != configuredHeight
            || configuredLayoutGeneration != layoutGeneration
            || configuredFilter != filter)
        && !configureSurface(currentWidth, currentHeight))
    {
        return;
    }

    auto& session = emuInstance->vulkanFrontendSession();
    if (!session.updatePresenterOverlay(
            *presenter, surfaceId, buildOverlayOnGuiThread()))
        return;
    Frame* frame = session.acquirePresentFrame();
    if (frame == nullptr)
        return;

    constexpr u64 kPresentTimeoutNs = 16'666'667ull;
    if (frame->surfaceGeneration != surfaceHost.generation())
    {
        session.deferPresentedFrame(frame);
        return;
    }

    if (session.presentAcquiredFrame(frame, *presenter, kPresentTimeoutNs))
    {
        lastPresentedFrameId = frame->frameId;
        session.commitPresentedFrame(frame);
    }
    else
        session.deferPresentedFrame(frame);
}

void ScreenPanelVulkan::setupScreenLayout()
{
    ScreenPanel::setupScreenLayout();
    ++layoutGeneration;
    update();
}

void ScreenPanelVulkan::paintEvent(QPaintEvent* event)
{
    event->accept();
    refreshClipForGameStateChange();
    presentOnGuiThread();
}

QPaintEngine* ScreenPanelVulkan::paintEngine() const
{
    return nullptr;
}
