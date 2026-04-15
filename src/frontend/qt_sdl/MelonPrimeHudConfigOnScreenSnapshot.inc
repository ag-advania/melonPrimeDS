// This file is a unity-build fragment included by MelonPrimeHudConfigOnScreen.cpp.
// Do not add it to CMakeLists.txt.

// ── Snapshot / restore ──────────────────────────────────────────────────────
static void SnapshotEditConfig(Config::Table& cfg)
{
    s_editSnapshot.clear();
    s_editSnapshotBools.clear();
    for (int i = 0; i < kEditElemCount; ++i) {
        const HudEditElemDesc& d = kEditElems[i];
        if (d.anchorKey) s_editSnapshot[d.anchorKey] = cfg.GetInt(d.anchorKey);
        if (d.ofsXKey)   s_editSnapshot[d.ofsXKey]   = cfg.GetInt(d.ofsXKey);
        if (d.ofsYKey)   s_editSnapshot[d.ofsYKey]   = cfg.GetInt(d.ofsYKey);
        if (d.orientKey)  s_editSnapshot[d.orientKey]  = cfg.GetInt(d.orientKey);
        if (d.lengthKey)  s_editSnapshot[d.lengthKey]  = cfg.GetInt(d.lengthKey);
        if (d.widthKey && d.widthKey != d.lengthKey)
            s_editSnapshot[d.widthKey] = cfg.GetInt(d.widthKey);
        if (d.posModeKey) s_editSnapshot[d.posModeKey] = cfg.GetInt(d.posModeKey);
        if (d.showKey) {
            s_editSnapshot[d.showKey] = cfg.GetBool(d.showKey) ? 1 : 0;
            s_editSnapshotBools.insert(d.showKey);
        }
        if (d.colorRKey) {
            s_editSnapshot[d.colorRKey] = cfg.GetInt(d.colorRKey);
            s_editSnapshot[d.colorGKey] = cfg.GetInt(d.colorGKey);
            s_editSnapshot[d.colorBKey] = cfg.GetInt(d.colorBKey);
        }
    }
    s_editSnapshotDoubles.clear();
    s_editSnapshotStrings.clear();

    auto snapshotPropArray = [&](const HudEditPropDesc* arr, int count) {
        for (int p = 0; p < count; ++p) {
            const HudEditPropDesc& pr = arr[p];
            switch (pr.type) {
            case EditPropType::Bool:
                s_editSnapshot[pr.cfgKey] = cfg.GetBool(pr.cfgKey) ? 1 : 0;
                s_editSnapshotBools.insert(pr.cfgKey);
                break;
            case EditPropType::Enum:
            case EditPropType::Int:
                s_editSnapshot[pr.cfgKey] = cfg.GetInt(pr.cfgKey);
                break;
            case EditPropType::Float:
                s_editSnapshotDoubles[pr.cfgKey] = cfg.GetDouble(pr.cfgKey);
                break;
            case EditPropType::String:
                s_editSnapshotStrings[pr.cfgKey] = cfg.GetString(pr.cfgKey);
                break;
            case EditPropType::SubColor:
                s_editSnapshot[pr.cfgKey] = cfg.GetBool(pr.cfgKey) ? 1 : 0;
                s_editSnapshotBools.insert(pr.cfgKey);
                s_editSnapshot[pr.extra1] = cfg.GetInt(pr.extra1);
                s_editSnapshot[pr.extra2] = cfg.GetInt(pr.extra2);
                s_editSnapshot[pr.extra3] = cfg.GetInt(pr.extra3);
                break;
            default: break;
            }
        }
    };

    for (int i = 0; i < kEditElemCount; ++i)
        snapshotPropArray(kEditElems[i].props, kEditElems[i].propCount);

    s_editSnapshot["Metroid.Visual.CrosshairColorR"] = cfg.GetInt("Metroid.Visual.CrosshairColorR");
    s_editSnapshot["Metroid.Visual.CrosshairColorG"] = cfg.GetInt("Metroid.Visual.CrosshairColorG");
    s_editSnapshot["Metroid.Visual.CrosshairColorB"] = cfg.GetInt("Metroid.Visual.CrosshairColorB");
    snapshotPropArray(kPropsCrosshairMain,  kCrosshairMainCount);
    snapshotPropArray(kPropsCrosshairInner, kCrosshairInnerCount);
    snapshotPropArray(kPropsCrosshairOuter, kCrosshairOuterCount);
    s_editSnapshot["Metroid.Visual.HudTextScale"] = cfg.GetInt("Metroid.Visual.HudTextScale");
    s_editSnapshot["Metroid.Visual.HudAutoScaleEnable"] = cfg.GetBool("Metroid.Visual.HudAutoScaleEnable") ? 1 : 0;
    s_editSnapshotBools.insert("Metroid.Visual.HudAutoScaleEnable");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCap"]       = cfg.GetInt("Metroid.Visual.HudAutoScaleCap");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCapText"]   = cfg.GetInt("Metroid.Visual.HudAutoScaleCapText");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCapIcons"]  = cfg.GetInt("Metroid.Visual.HudAutoScaleCapIcons");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCapGauges"]    = cfg.GetInt("Metroid.Visual.HudAutoScaleCapGauges");
    s_editSnapshot["Metroid.Visual.HudAutoScaleCapCrosshair"] = cfg.GetInt("Metroid.Visual.HudAutoScaleCapCrosshair");
}

static void RestoreEditSnapshot(Config::Table& cfg)
{
    for (const auto& kv : s_editSnapshot) {
        if (s_editSnapshotBools.count(kv.first))
            cfg.SetBool(kv.first, kv.second != 0);
        else
            cfg.SetInt(kv.first, kv.second);
    }
    for (const auto& kv : s_editSnapshotDoubles)
        cfg.SetDouble(kv.first, kv.second);
    for (const auto& kv : s_editSnapshotStrings)
        cfg.SetString(kv.first, kv.second);
}

static void ResetEditToDefaults(Config::Table& cfg)
{
    toml::value defData(toml::table{});
    Config::Table defaults(defData, "Instance0");
    for (int i = 0; i < kEditElemCount; ++i) {
        const HudEditElemDesc& d = kEditElems[i];
        if (d.anchorKey) cfg.SetInt(d.anchorKey, defaults.GetInt(d.anchorKey));
        if (d.ofsXKey)   cfg.SetInt(d.ofsXKey,   defaults.GetInt(d.ofsXKey));
        if (d.ofsYKey)   cfg.SetInt(d.ofsYKey,   defaults.GetInt(d.ofsYKey));
        if (d.orientKey)  cfg.SetInt(d.orientKey,  defaults.GetInt(d.orientKey));
        if (d.lengthKey)  cfg.SetInt(d.lengthKey,  defaults.GetInt(d.lengthKey));
        if (d.widthKey && d.widthKey != d.lengthKey)
            cfg.SetInt(d.widthKey, defaults.GetInt(d.widthKey));
        if (d.posModeKey) cfg.SetInt(d.posModeKey, defaults.GetInt(d.posModeKey));
        if (d.showKey)    cfg.SetBool(d.showKey,   defaults.GetBool(d.showKey));
        if (d.colorRKey) {
            cfg.SetInt(d.colorRKey, defaults.GetInt(d.colorRKey));
            cfg.SetInt(d.colorGKey, defaults.GetInt(d.colorGKey));
            cfg.SetInt(d.colorBKey, defaults.GetInt(d.colorBKey));
        }
        for (int p = 0; p < d.propCount; ++p) {
            const HudEditPropDesc& pr = d.props[p];
            switch (pr.type) {
            case EditPropType::Bool:   cfg.SetBool(pr.cfgKey, defaults.GetBool(pr.cfgKey)); break;
            case EditPropType::Enum:
            case EditPropType::Int:    cfg.SetInt(pr.cfgKey,  defaults.GetInt(pr.cfgKey));  break;
            case EditPropType::Float:  cfg.SetDouble(pr.cfgKey, defaults.GetDouble(pr.cfgKey)); break;
            case EditPropType::String: cfg.SetString(pr.cfgKey, defaults.GetString(pr.cfgKey)); break;
            case EditPropType::SubColor:
                cfg.SetBool(pr.cfgKey, defaults.GetBool(pr.cfgKey));
                cfg.SetInt(pr.extra1, defaults.GetInt(pr.extra1));
                cfg.SetInt(pr.extra2, defaults.GetInt(pr.extra2));
                cfg.SetInt(pr.extra3, defaults.GetInt(pr.extra3));
                break;
            }
        }
    }

    auto resetPropArray = [&](const HudEditPropDesc* arr, int count) {
        for (int p = 0; p < count; ++p) {
            const HudEditPropDesc& pr = arr[p];
            switch (pr.type) {
            case EditPropType::Bool:   cfg.SetBool(pr.cfgKey, defaults.GetBool(pr.cfgKey));     break;
            case EditPropType::Enum:
            case EditPropType::Int:    cfg.SetInt(pr.cfgKey,  defaults.GetInt(pr.cfgKey));       break;
            case EditPropType::Float:  cfg.SetDouble(pr.cfgKey, defaults.GetDouble(pr.cfgKey)); break;
            default: break;
            }
        }
    };
    cfg.SetInt("Metroid.Visual.CrosshairColorR", defaults.GetInt("Metroid.Visual.CrosshairColorR"));
    cfg.SetInt("Metroid.Visual.CrosshairColorG", defaults.GetInt("Metroid.Visual.CrosshairColorG"));
    cfg.SetInt("Metroid.Visual.CrosshairColorB", defaults.GetInt("Metroid.Visual.CrosshairColorB"));
    resetPropArray(kPropsCrosshairMain,  kCrosshairMainCount);
    resetPropArray(kPropsCrosshairInner, kCrosshairInnerCount);
    resetPropArray(kPropsCrosshairOuter, kCrosshairOuterCount);
    cfg.SetInt("Metroid.Visual.HudTextScale", defaults.GetInt("Metroid.Visual.HudTextScale"));
    cfg.SetBool("Metroid.Visual.HudAutoScaleEnable", defaults.GetBool("Metroid.Visual.HudAutoScaleEnable"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCap",       defaults.GetInt("Metroid.Visual.HudAutoScaleCap"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCapText",   defaults.GetInt("Metroid.Visual.HudAutoScaleCapText"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCapIcons",  defaults.GetInt("Metroid.Visual.HudAutoScaleCapIcons"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCapGauges",    defaults.GetInt("Metroid.Visual.HudAutoScaleCapGauges"));
    cfg.SetInt("Metroid.Visual.HudAutoScaleCapCrosshair", defaults.GetInt("Metroid.Visual.HudAutoScaleCapCrosshair"));
}

