/*
    Copyright 2016-2025 melonDS team
    (MelonPrime specific configuration extension)
*/

#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QSlider>
#include <QPointer>
#include <QSignalBlocker>
#include <cmath>

#include "MelonPrimeInputConfig.h"
#include "ui_MelonPrimeInputConfig.h"
#include "Config.h"
#include "../MelonPrimeDef.h"
#include "../MelonPrimeHudPropSchema.inc"
#include "../MelonPrime.h"
#include "../EmuThread.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudConfigState.h"
#endif

using namespace melonDS;

namespace {

template<typename T>
bool widgetAlive(const QPointer<T>& w)
{
    return !w.isNull();
}

bool widgetAlive(const QWidget* w)
{
    return w != nullptr;
}

} // namespace

bool MelonPrimeInputConfig::visualSnapshotTargetsAlive() const
{
    if (!ui)
        return false;
    return widgetAlive(QPointer{ui->cbMetroidEnableCustomHud})
        && widgetAlive(QPointer{ui->cbMetroidInGameAspectRatio})
        && widgetAlive(QPointer{ui->comboMetroidInGameAspectRatioMode})
        && widgetAlive(QPointer{ui->comboMetroidOnScreenEditStyle});
}

void MelonPrimeInputConfig::snapshotVisualConfig()
{
    if (!visualSnapshotTargetsAlive())
        return;

    QVariantMap& s = m_visualSnapshot;
    s.clear();

    auto sB = [&](const char* k, QCheckBox* w) {
        if (!widgetAlive(w)) return;
        s[k] = w->isChecked();
    };
    auto sC = [&](const char* k, QComboBox* w) {
        if (!widgetAlive(w)) return;
        s[k] = w->currentIndex();
    };

    sB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    sB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    sC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);
    if (widgetAlive(ui->comboMetroidOnScreenEditStyle))
        s["cOnScreenEditStyle"] = ui->comboMetroidOnScreenEditStyle->currentData().toInt();

    // Snapshot all programmatic HUD widgets
    for (auto& [key, widget] : m_hudWidgets) {
        if (!widgetAlive(widget))
            continue;
        QString qk = QString::fromStdString(key);
        if (auto* cb = qobject_cast<QCheckBox*>(widget.data()))
            s[qk] = cb->isChecked();
        else if (auto* slider = qobject_cast<QSlider*>(widget.data()))
            // Opacity widgets store percentages in the slider, while the
            // snapshot restore path uses the normalized config representation.
            // Keep the snapshot in that same 0.0..1.0 representation so a
            // 50/75% value cannot be clamped to 100% during restoration.
            s[qk] = slider->value() / 100.0;
        else if (auto* sb = qobject_cast<QSpinBox*>(widget.data()))
            s[qk] = sb->value();
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget.data()))
            s[qk] = dsb->value();
        else if (auto* le = qobject_cast<QLineEdit*>(widget.data()))
            s[qk] = le->text();
        else if (auto* fc = qobject_cast<QFontComboBox*>(widget.data()))   // before QComboBox: family string
            s[qk] = fc->currentFont().family();
        else if (auto* combo = qobject_cast<QComboBox*>(widget.data()))
            s[qk] = combo->currentIndex();
    }
}

void MelonPrimeInputConfig::restoreVisualSnapshot()
{
    if (m_visualSnapshot.isEmpty())
        return;
    if (!visualSnapshotTargetsAlive()) {
        m_visualSnapshot.clear();
        return;
    }

    m_applyPreviewEnabled = false;

    const QVariantMap& s = m_visualSnapshot;
    auto rB = [&](const char* k, QCheckBox* w) {
        if (!widgetAlive(w)) return;
        auto it = s.find(k);
        if (it == s.end()) return;
        w->blockSignals(true);
        w->setChecked(it->toBool());
        w->blockSignals(false);
    };
    auto rC = [&](const char* k, QComboBox* w) {
        if (!widgetAlive(w)) return;
        auto it = s.find(k);
        if (it == s.end()) return;
        w->blockSignals(true);
        w->setCurrentIndex(it->toInt());
        w->blockSignals(false);
    };

    rB("cCustomHud",       ui->cbMetroidEnableCustomHud);
    rB("cAspectRatio",     ui->cbMetroidInGameAspectRatio);
    rC("cAspectRatioMode", ui->comboMetroidInGameAspectRatioMode);
    if (widgetAlive(ui->comboMetroidOnScreenEditStyle)) {
        auto it = s.find("cOnScreenEditStyle");
        if (it != s.end()) {
            const QSignalBlocker blocker(ui->comboMetroidOnScreenEditStyle);
            const int normalized = static_cast<int>(MelonPrime::NormalizeOnScreenEditStyle(it->toInt()));
            const int index = ui->comboMetroidOnScreenEditStyle->findData(normalized);
            ui->comboMetroidOnScreenEditStyle->setCurrentIndex(index >= 0 ? index : 0);
        }
    }

    // Restore all programmatic HUD widgets
    for (auto& [key, widget] : m_hudWidgets) {
        if (!widgetAlive(widget))
            continue;
        QString qk = QString::fromStdString(key);
        auto it = s.find(qk);
        if (it == s.end()) continue;

        widget->blockSignals(true);
        if (auto* cb = qobject_cast<QCheckBox*>(widget.data()))
            cb->setChecked(it->toBool());
        else if (auto* slider = qobject_cast<QSlider*>(widget.data())) {
            slider->setValue(static_cast<int>(std::lround(
                std::clamp(it->toDouble(), 0.0, 1.0) * 100.0)));
            if (auto* valueLabel = slider->parentWidget()->findChild<QLabel*>(
                    slider->objectName() + QStringLiteral("_value")))
                valueLabel->setText(QStringLiteral("%1%").arg(slider->value()));
        }
        else if (auto* sb = qobject_cast<QSpinBox*>(widget.data()))
            sb->setValue(it->toInt());
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget.data()))
            dsb->setValue(it->toDouble());
        else if (auto* le = qobject_cast<QLineEdit*>(widget.data()))
            le->setText(it->toString());
        else if (auto* fc = qobject_cast<QFontComboBox*>(widget.data()))   // before QComboBox: family string
            fc->setCurrentFont(QFont(it->toString()));
        else if (auto* combo = qobject_cast<QComboBox*>(widget.data()))
            combo->setCurrentIndex(it->toInt());
        widget->blockSignals(false);
    }

    m_applyPreviewEnabled = true;
    if (visualSnapshotTargetsAlive())
        applyVisualPreview();
}

void MelonPrimeInputConfig::applyVisualPreview()
{
#ifdef MELONPRIME_CUSTOM_HUD
    if (!m_applyPreviewEnabled || m_applyPreviewActive)
        return;
    if (!visualSnapshotTargetsAlive())
        return;

    const QPointer<QCheckBox> customHud = ui->cbMetroidEnableCustomHud;
    const QPointer<QCheckBox> aspectRatio = ui->cbMetroidInGameAspectRatio;
    const QPointer<QComboBox> aspectRatioMode = ui->comboMetroidInGameAspectRatioMode;
    const QPointer<QCheckBox> clipCursor = ui->cbMetroidClipCursorToBottomScreenWhenNotInGame;
    if (!widgetAlive(customHud) || !widgetAlive(aspectRatio) || !widgetAlive(aspectRatioMode)
        || !widgetAlive(clipCursor))
        return;

    m_applyPreviewActive = true;

    Config::Table& instcfg = emuInstance->getLocalConfig();
    instcfg.SetBool(MP_HUD_PROP_KEY_CustomHUD,              customHud->isChecked());
    instcfg.SetBool(MP_HUD_PROP_KEY_InGameAspectRatio,      aspectRatio->isChecked());
    instcfg.SetInt (MP_HUD_PROP_KEY_InGameAspectRatioMode,  aspectRatioMode->currentIndex());
    instcfg.SetBool(MP_HUD_PROP_KEY_ClipCursorToBottomScreenWhenNotInGame, clipCursor->isChecked());
    instcfg.SetInt(
        MelonPrime::CfgKey::OnScreenEditStyle,
        static_cast<int>(MelonPrime::NormalizeOnScreenEditStyle(
            ui->comboMetroidOnScreenEditStyle->currentData().toInt())));

    // Write all programmatic HUD widget values to config
    for (auto& [key, widget] : m_hudWidgets) {
        if (!widgetAlive(widget))
            continue;
        if (auto* cb = qobject_cast<QCheckBox*>(widget.data()))
            instcfg.SetBool(key, cb->isChecked());
        else if (auto* slider = qobject_cast<QSlider*>(widget.data()))
            instcfg.SetDouble(key, slider->value() / 100.0);
        else if (auto* sb = qobject_cast<QSpinBox*>(widget.data()))
            instcfg.SetInt(key, sb->value());
        else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(widget.data()))
            instcfg.SetDouble(key, dsb->value());
        else if (auto* le = qobject_cast<QLineEdit*>(widget.data()))
            instcfg.SetString(key, le->text().toStdString());
        else if (auto* fc = qobject_cast<QFontComboBox*>(widget.data()))   // before QComboBox: family string
            instcfg.SetString(key, fc->currentFont().family().toStdString());
        else if (auto* combo = qobject_cast<QComboBox*>(widget.data()))
            instcfg.SetInt(key, combo->currentIndex());
    }

    if (auto* thread = emuInstance->getEmuThread()) {
        if (auto* core = thread->GetMelonPrimeCore()) {
            MelonPrime::CustomHud_InvalidateConfigCache(core->HudConfigState());
            // Match the normal save path: preview values must be visible to
            // the runtime reload path as well as to the dialog-side preview.
            core->NotifyConfigChanged();
        }
    }

    // Refresh preview widgets
    for (auto* pw : m_hudPreviews) {
        if (widgetAlive(pw))
            pw->update();
    }

    m_applyPreviewActive = false;
#endif
}
