#pragma once
#ifdef MELONPRIME_CUSTOM_HUD

#include <QWidget>
#include <QFormLayout>
#include <QScrollArea>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <functional>
#include "Config.h"

class EmuInstance;

namespace MelonPrime { namespace HudEditorSidePanel { struct Row; } }

class MelonPrimeHudConfigOnScreenEdit : public QWidget
{
public:
    explicit MelonPrimeHudConfigOnScreenEdit(QWidget* parent, EmuInstance* emu);

    void populateForElement(int elemIdx);
    void populateForCrosshair();
    void clear();
    void reloadValues();

private:
    EmuInstance* m_emu;
    QScrollArea* m_scroll;
    QWidget* m_inner;
    QFormLayout* m_form;
    QLabel* m_title;
    int m_currentElem = -1;

    // Flag to suppress config writes while populating
    bool m_populating = false;

    // Track created widgets for cleanup
    QList<QWidget*> m_rows;

    void clearForm();
    Config::Table& cfg();

    // Factory methods — each creates a widget, connects it, adds to form
    QWidget*        addCheckBox(const QString& label, const char* key);
    QComboBox*      addComboBox(const QString& label, const char* key, const QStringList& items);
    QSpinBox*       addSpinBox(const QString& label, const char* key, int min, int max);
    QDoubleSpinBox* addDoubleSpinBox(const QString& label, const char* key, double min, double max, double step);
    QSlider*        addOpacitySlider(const QString& label, const char* key);
    QLineEdit*      addLineEdit(const QString& label, const char* key);
    QPushButton*    addColorPicker(const QString& label, const char* keyR, const char* keyG, const char* keyB);
    void            addSubColor(const QString& label, const char* overallKey, const char* keyR, const char* keyG, const char* keyB);
    // ON/OFF radios + color button in one row (for per-weapon tint etc.)
    void            addColorOverlayRow(const QString& label, const char* enableKey, const char* keyR, const char* keyG, const char* keyB);
    // Add Show/Color/Anchor rows common to all elements; pass nullptr for absent keys
    void            addBuiltins(const char* showKey, const char* colorR, const char* colorG, const char* colorB, const char* anchorKey);
    void            addOffsetRows(const char* keyX, const char* keyY, int min, int max,
                                   const QString& labelX, const QString& labelY);
    QComboBox*      addAlign3Combo(const QString& label, const char* key);
    void            addGaugePositionRows(const char* posModeKey,
                                         const char* gaugeAnchorKey, const char* gaugeOffsetXKey, const char* gaugeOffsetYKey,
                                         const char* gaugePosXKey, const char* gaugePosYKey,
                                         const char* textAnchorKey, const char* textOffsetXKey, const char* textOffsetYKey);
    void            addSectionHeader(const QString& label);

    // Separator
    void addSeparator();

    // Outline group: Enable, Color, Opacity, Thickness (4 widgets behind a separator).
    // Call with MP_OUTLINE_KEYS(Prefix) so the six keys are checked at compile time.
    void addOutlineGroup(const char* enableKey, const char* colorR, const char* colorG, const char* colorB,
                         const char* opacityKey, const char* thicknessKey);
    void addOutlineGroupSection(const QString& sectionLabel,
                                const char* enableKey, const char* colorR, const char* colorG, const char* colorB,
                                const char* opacityKey, const char* thicknessKey);

    // Generic dispatcher for the data-table row representation of a
    // populate*() body (V7 Phase 2). Walks `rows` in order and calls the
    // matching add*() factory method above per row. See
    // MelonPrimeHudEditorSidePanelRows.inc for the row table definitions.
    void populateFromRowTable(const MelonPrime::HudEditorSidePanel::Row* rows, int count);

    // Per-element populate functions
    void populateHP();
    void populateHPGauge();
    void populateWeaponAmmo();
    void populateWpnIcon();
    void populateAmmoGauge();
    void populateMatchStatus();
    void populateRank();
    void populateTimeLeft();
    void populateTimeLimit();
    void populateBombLeft();
    void populateBombIcon();
    void populateWeaponInventory();
    void populateRadar();
};

#endif // MELONPRIME_CUSTOM_HUD
