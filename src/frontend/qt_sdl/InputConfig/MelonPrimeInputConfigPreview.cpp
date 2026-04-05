/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QCheckBox>
#include <QComboBox>

#include "MelonPrimeInputConfig.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeCustomHud.h"
#endif

using namespace melonDS;

void MelonPrimeInputConfig::snapshotVisualConfig()
{
    QVariantMap& s = m_visualSnapshot;
    s.clear();

    auto sB  = [&](const char* k, QCheckBox* w)       { s[k] = w->isChecked(); };
    auto sC  = [&](const char* k, QComboBox* w)       { s[k] = w->currentIndex(); };

    sB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    sB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    sC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);
}

void MelonPrimeInputConfig::restoreVisualSnapshot()
{
    if (m_visualSnapshot.isEmpty()) return;
    m_applyPreviewEnabled = false;

    const QVariantMap& s = m_visualSnapshot;
    auto rB = [&](const char* k, QCheckBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setChecked(it->toBool()); w->blockSignals(false);
    };
    auto rC = [&](const char* k, QComboBox* w) {
        auto it = s.find(k); if (it == s.end()) return;
        w->blockSignals(true); w->setCurrentIndex(it->toInt()); w->blockSignals(false);
    };

    rB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    rB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    rC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);

    m_applyPreviewEnabled = true;
    applyVisualPreview();
}

void MelonPrimeInputConfig::applyVisualPreview()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled || m_applyPreviewActive) return;
    m_applyPreviewActive = true;

    Config::Table& instcfg = emuInstance->getLocalConfig();
    instcfg.SetBool("Metroid.Visual.CustomHUD",              ui->cbMetroidEnableCustomHud->isChecked());
    instcfg.SetBool("Metroid.Visual.InGameAspectRatio",      ui->cbMetroidInGameAspectRatio->isChecked());
    instcfg.SetInt ("Metroid.Visual.InGameAspectRatioMode",  ui->comboMetroidInGameAspectRatioMode->currentIndex());
    instcfg.SetBool("Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame", ui->cbMetroidClipCursorToBottomScreenWhenNotInGame->isChecked());

    MelonPrime::CustomHud_InvalidateConfigCache();
    m_applyPreviewActive = false;
#endif
}
