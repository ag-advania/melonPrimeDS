/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>

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

    // Snapshot all programmatic HUD widgets
    for (auto& [key, widget] : m_hudWidgets) {
        QString qk = QString::fromStdString(key);
        if (auto* cb = qobject_cast<QCheckBox*>(widget))
            s[qk] = cb->isChecked();
        else if (auto* sb = qobject_cast<QSpinBox*>(widget))
            s[qk] = sb->value();
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget))
            s[qk] = dsb->value();
        else if (auto* le = qobject_cast<QLineEdit*>(widget))
            s[qk] = le->text();
        else if (auto* combo = qobject_cast<QComboBox*>(widget))
            s[qk] = combo->currentIndex();
    }
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

    // Restore all programmatic HUD widgets
    for (auto& [key, widget] : m_hudWidgets) {
        QString qk = QString::fromStdString(key);
        auto it = s.find(qk);
        if (it == s.end()) continue;

        widget->blockSignals(true);
        if (auto* cb = qobject_cast<QCheckBox*>(widget))
            cb->setChecked(it->toBool());
        else if (auto* sb = qobject_cast<QSpinBox*>(widget))
            sb->setValue(it->toInt());
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget))
            dsb->setValue(it->toDouble());
        else if (auto* le = qobject_cast<QLineEdit*>(widget))
            le->setText(it->toString());
        else if (auto* combo = qobject_cast<QComboBox*>(widget))
            combo->setCurrentIndex(it->toInt());
        widget->blockSignals(false);
    }

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

    // Write all programmatic HUD widget values to config
    for (auto& [key, widget] : m_hudWidgets) {
        if (auto* cb = qobject_cast<QCheckBox*>(widget))
            instcfg.SetBool(key, cb->isChecked());
        else if (auto* sb = qobject_cast<QSpinBox*>(widget))
            instcfg.SetInt(key, sb->value());
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget))
            instcfg.SetDouble(key, dsb->value());
        else if (auto* le = qobject_cast<QLineEdit*>(widget))
            instcfg.SetString(key, le->text().toStdString());
        else if (auto* combo = qobject_cast<QComboBox*>(widget))
            instcfg.SetInt(key, combo->currentIndex());
    }

    MelonPrime::CustomHud_InvalidateConfigCache();

    // Refresh preview widgets
    for (auto* pw : m_hudPreviews)
        pw->update();

    m_applyPreviewActive = false;
#endif
}
