#pragma once
#ifdef MELONPRIME_CUSTOM_HUD

#include <QWidget>
#include <QFormLayout>
#include <QScrollArea>
#include <QCheckBox>
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

class MelonPrimeHudEditSidePanel : public QWidget
{
public:
    explicit MelonPrimeHudEditSidePanel(QWidget* parent, EmuInstance* emu);

    void populateForElement(int elemIdx);
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
    QCheckBox*      addCheckBox(const QString& label, const char* key);
    QComboBox*      addComboBox(const QString& label, const char* key, const QStringList& items);
    QSpinBox*       addSpinBox(const QString& label, const char* key, int min, int max);
    QDoubleSpinBox* addDoubleSpinBox(const QString& label, const char* key, double min, double max, double step);
    QLineEdit*      addLineEdit(const QString& label, const char* key);
    QPushButton*    addColorPicker(const QString& label, const char* keyR, const char* keyG, const char* keyB);
    void            addSubColor(const QString& label, const char* overallKey, const char* keyR, const char* keyG, const char* keyB);
    // Add Show/Color/Anchor rows common to all elements; pass nullptr for absent keys
    void            addBuiltins(const char* showKey, const char* colorR, const char* colorG, const char* colorB, const char* anchorKey);

    // Separator
    void addSeparator();

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
    void populateRadar();
};

#endif // MELONPRIME_CUSTOM_HUD
