// This file is a developer-only unity-build fragment included by MelonPrimeHudRender.cpp.
// It renders deterministic HUD cases into QImage buffers and writes FNV-1a hashes.

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES) && defined(MELONPRIME_CUSTOM_HUD)

struct HudGoldenCase {
    const char* name;
    int fontMode;
    float hudScale;
    bool outline;
    bool zoomed;
};

static uint64_t HudGoldenHashImage(const QImage& img)
{
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&](uint8_t v) {
        h ^= static_cast<uint64_t>(v);
        h *= 1099511628211ull;
    };
    const uint32_t w = static_cast<uint32_t>(img.width());
    const uint32_t height = static_cast<uint32_t>(img.height());
    for (int i = 0; i < 4; ++i) mix(static_cast<uint8_t>((w >> (i * 8)) & 0xFFu));
    for (int i = 0; i < 4; ++i) mix(static_cast<uint8_t>((height >> (i * 8)) & 0xFFu));
    for (int y = 0; y < img.height(); ++y) {
        const uchar* row = img.constScanLine(y);
        for (int x = 0; x < img.bytesPerLine(); ++x)
            mix(row[x]);
    }
    return h;
}

static void HudGoldenResetConfig(Config::Table& cfg)
{
#define MP_HUD_GOLDEN_SET_INT(name, key, type, defValue, minValue, maxValue, stepValue, label, widget, surfaces, flags) \
    cfg.SetInt(key, static_cast<int>(defValue));
#define MP_HUD_GOLDEN_SET_BOOL(name, key, type, defValue, minValue, maxValue, stepValue, label, widget, surfaces, flags) \
    cfg.SetBool(key, static_cast<bool>(defValue));
#define MP_HUD_GOLDEN_SET_STRING(name, key, type, defValue, minValue, maxValue, stepValue, label, widget, surfaces, flags) \
    cfg.SetString(key, std::string(defValue));
#define MP_HUD_GOLDEN_SET_DOUBLE(name, key, type, defValue, minValue, maxValue, stepValue, label, widget, surfaces, flags) \
    cfg.SetDouble(key, static_cast<double>(defValue));
    MP_HUD_PROP_SCHEMA_INT(MP_HUD_GOLDEN_SET_INT)
    MP_HUD_PROP_SCHEMA_BOOL(MP_HUD_GOLDEN_SET_BOOL)
    MP_HUD_PROP_SCHEMA_STRING(MP_HUD_GOLDEN_SET_STRING)
    MP_HUD_PROP_SCHEMA_DOUBLE(MP_HUD_GOLDEN_SET_DOUBLE)
#undef MP_HUD_GOLDEN_SET_INT
#undef MP_HUD_GOLDEN_SET_BOOL
#undef MP_HUD_GOLDEN_SET_STRING
#undef MP_HUD_GOLDEN_SET_DOUBLE
}

static void HudGoldenApplyCaseConfig(Config::Table& cfg, const HudGoldenCase& tc)
{
    HudGoldenResetConfig(cfg);
    cfg.SetBool(MP_HUD_PROP_KEY_CustomHUD, true);
    cfg.SetInt(MP_HUD_PROP_KEY_HudFontMode, tc.fontMode);
    cfg.SetString(MP_HUD_PROP_KEY_HudFontFamily, "Arial");
    cfg.SetString(MP_HUD_PROP_KEY_HudFontFile, "");
    cfg.SetBool(MP_HUD_PROP_KEY_HudAutoScaleEnable, false);
    cfg.SetInt(MP_HUD_PROP_KEY_HudTextScale, 100);

    cfg.SetBool(MP_HUD_PROP_KEY_HudGlobalOutline, tc.outline);
    cfg.SetDouble(MP_HUD_PROP_KEY_HudGlobalOutlineOpacity, tc.outline ? 0.75 : 0.0);
    cfg.SetInt(MP_HUD_PROP_KEY_HudGlobalOutlineThickness, 1);

    cfg.SetBool(MP_HUD_PROP_KEY_HudWeaponInventoryShow, true);
    cfg.SetBool(MP_HUD_PROP_KEY_HudMatchStatusShow, true);
    cfg.SetBool(MP_HUD_PROP_KEY_HudRankShow, true);
    cfg.SetBool(MP_HUD_PROP_KEY_HudTimeLeftShow, true);
    cfg.SetBool(MP_HUD_PROP_KEY_HudTimeLimitShow, true);
    cfg.SetBool(MP_HUD_PROP_KEY_HudBombLeftShow, true);
    cfg.SetBool(MP_HUD_PROP_KEY_BtmOverlayEnable, true);

    cfg.SetBool(MP_HUD_PROP_KEY_CrosshairZoomStageEnable, true);
    cfg.SetBool(MP_HUD_PROP_KEY_CrosshairZoomScopeEnable, true);
    cfg.SetBool(MP_HUD_PROP_KEY_CrosshairZoomTransitionEnable, true);
    cfg.SetInt(MP_HUD_PROP_KEY_CrosshairZoomTransitionStyle, 8);
}

static void HudGoldenWrite16(std::vector<melonDS::u8>& ram, uint32_t addr, uint16_t value)
{
    Write16(ram.data(), addr, value);
}

static void HudGoldenWrite32(std::vector<melonDS::u8>& ram, uint32_t addr, uint32_t value)
{
    Write32(ram.data(), addr, value);
}

static RomAddresses HudGoldenRom()
{
    RomAddresses rom = {};
    rom.romGroupIndex = 0;
    rom.playerHP = 0x100;
    rom.maxHP = 0x102;
    rom.currentAmmoSpecial = 0x104;
    rom.currentAmmoMissile = 0x106;
    rom.maxAmmoSpecial = 0x108;
    rom.maxAmmoMissile = 0x10A;
    rom.baseBomb = 0x10C;
    rom.baseViewMode = 0x110;
    rom.crosshairPosX = 0x120;
    rom.crosshairPosY = 0x121;
    rom.matchRank = 0x130;
    rom.timeLeft = 0x134;
    rom.battleMode = 0x140;
    rom.battleSettings = 0x144;
    rom.basePoint = 0x150;
    rom.isInAdventure = 0x160;
    return rom;
}

static GameAddressesHot HudGoldenHot()
{
    GameAddressesHot hot = {};
    hot.isAltForm = 0x200;
    hot.jumpFlag = 0x201;
    hot.currentWeapon = 0x202;
    hot.havingWeapons = 0x204;
    hot.chosenHunter = 0x206;
    return hot;
}

static void HudGoldenFillRam(std::vector<melonDS::u8>& ram, const RomAddresses& rom,
                             const GameAddressesHot& hot)
{
    ram.assign(0x1000, 0);
    HudGoldenWrite16(ram, rom.playerHP, 173);
    HudGoldenWrite16(ram, rom.maxHP, 199);
    HudGoldenWrite16(ram, rom.currentAmmoSpecial, 64);
    HudGoldenWrite16(ram, rom.currentAmmoMissile, 38);
    HudGoldenWrite16(ram, rom.maxAmmoSpecial, 100);
    HudGoldenWrite16(ram, rom.maxAmmoMissile, 60);
    HudGoldenWrite32(ram, rom.baseBomb, 0x00000300u);
    ram[rom.baseViewMode] = 0x00;
    ram[rom.crosshairPosX] = 128;
    ram[rom.crosshairPosY] = 96;
    HudGoldenWrite32(ram, rom.matchRank, 0x00000000u);
    HudGoldenWrite32(ram, rom.timeLeft, 179u * 60u);
    ram[rom.battleMode] = MODE_BATTLE;
    HudGoldenWrite32(ram, rom.battleSettings, 0x00C00000u);
    HudGoldenWrite32(ram, rom.battleSettings + 4, 0x00000A00u);
    HudGoldenWrite16(ram, rom.basePoint, 7);
    ram[rom.isInAdventure] = 0x00;

    ram[hot.isAltForm] = 0x00;
    ram[hot.jumpFlag] = 0x00;
    ram[hot.currentWeapon] = WeaponId::Missile;
    HudGoldenWrite16(ram, hot.havingWeapons, 0x01FFu);
    ram[hot.chosenHunter] = static_cast<uint8_t>(HunterId::Samus);
}

static uint64_t HudGoldenRenderCase(
    CustomHudConfigState& hudConfig,
    Config::Table& cfg,
    const HudGoldenCase& tc)
{
    const ScopedHudConfigState active(hudConfig);
    HudGoldenApplyCaseConfig(cfg, tc);
    CustomHud_InvalidateConfigCache();

    QImage top(256 * 2, 192 * 2, QImage::Format_ARGB32_Premultiplied);
    QImage bottom(256 * 2, 192 * 2, QImage::Format_ARGB32_Premultiplied);
    top.fill(Qt::transparent);
    bottom.fill(QColor(10, 22, 30, 255));

    QPainter bottomPainter(&bottom);
    bottomPainter.fillRect(QRect(80, 80, 180, 120), QColor(20, 95, 120, 255));
    bottomPainter.fillRect(QRect(176, 144, 24, 24), QColor(230, 230, 80, 255));
    bottomPainter.end();

    QPainter p(&top);
    p.setRenderHint(QPainter::Antialiasing, false);
    QFont font = CustomHud_ResolveBaseFont(cfg);
    font.setPixelSize(CustomHud_ResolveFontPixelSize(cfg));
    p.setFont(font);

    const RomAddresses rom = HudGoldenRom();
    const GameAddressesHot hot = HudGoldenHot();
    std::vector<melonDS::u8> ram;
    HudGoldenFillRam(ram, rom, hot);

    const HudFrameTransform xf = MakeHudFrameTransform(&top, 1.0f, tc.hudScale, 0.0f, 0.0f);
    s_chDirtyThisFrame = QRect();
    s_drawnDirtyPx = QRect();
    EnsureCachedConfigForFrame(cfg, xf);
    ApplyHudPainterTransform(&p, xf);
    EnsureHudFont(&p);

    const CachedHudConfig& c = s_cache;
    const float tds = c.textDrawScale;
    const float zoomAmount = tc.zoomed ? 1.0f : 0.0f;
    DrawCrosshair(&p, ram.data(), rom, c, tc.hudScale, 1.0f, tc.zoomed ? 120u : 60u, zoomAmount);
    DrawHP(&p, 173, 199, c, tds);
    DrawWeaponAmmo(&p, WeaponId::Missile, 64, 38, 100, 60, c, tds, tc.hudScale);
    DrawWeaponInventory(&p, 0x01FFu, WeaponId::Missile, 64, 38, c, tds, tc.hudScale);
    DrawBombLeft(&p, 3, c, tds, tc.hudScale);
    DrawMatchStatusHud(&p, ram.data(), rom, 0, false, c);
    DrawRankAndTime(&p, ram.data(), rom, 0, false, c, tds);
    DrawBottomScreenOverlay(cfg, &p, &bottom, static_cast<uint8_t>(HunterId::Samus));
    p.end();

    return HudGoldenHashImage(top);
}

int CustomHud_RunGoldenHarness(const QString& outputPath)
{
    Config::Table cfg = Config::GetGlobalTable();
    const HudGoldenCase cases[] = {
        { "mph-100-outline-off", 0, 2.0f, false, false },
        { "mph-200-outline-on", 0, 2.0f, true, true },
        { "system-100-outline-on", 1, 2.0f, true, false },
        { "file-fallback-200-outline-off", 2, 2.0f, false, true },
    };

    QString outText;
    CustomHudConfigState hudConfig;
    QTextStream stream(&outText);
    stream << "# MelonPrime HUD golden hashes\n";
    stream << "# format: case fnv1a64\n";
    for (const HudGoldenCase& tc : cases) {
        const uint64_t hash = HudGoldenRenderCase(hudConfig, cfg, tc);
        stream << tc.name << " 0x" << Qt::hex << hash << Qt::dec << "\n";
    }

    if (outputPath == QStringLiteral("-")) {
        QTextStream stdoutStream(stdout);
        stdoutStream << outText;
        return 0;
    }

    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QTextStream stderrStream(stderr);
        stderrStream << "Unable to write HUD golden output: " << outputPath << "\n";
        return 1;
    }
    out.write(outText.toUtf8());
    return 0;
}

#endif // MELONPRIME_ENABLE_DEVELOPER_FEATURES && MELONPRIME_CUSTOM_HUD
