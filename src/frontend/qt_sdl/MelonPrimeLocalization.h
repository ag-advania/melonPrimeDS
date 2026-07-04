#pragma once

#include <QAction>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QStringList>
#include <QWidget>

namespace MelonPrime::UiText
{

inline constexpr int kMenuLanguageJapanese = 0;
inline constexpr int kMenuLanguageEnglish = 1;

bool IsJapaneseSystemLocale();

inline int& MenuLanguageModeStorage()
{
    static int mode = kMenuLanguageJapanese;
    return mode;
}

inline void SetMenuLanguageMode(int mode)
{
    MenuLanguageModeStorage() =
        (mode == kMenuLanguageEnglish) ? kMenuLanguageEnglish : kMenuLanguageJapanese;
}

inline int MenuLanguageMode()
{
    return MenuLanguageModeStorage();
}

inline bool CanChooseMenuLanguage()
{
    return IsJapaneseSystemLocale();
}

inline bool IsJapaneseLocale()
{
    return IsJapaneseSystemLocale() && MenuLanguageMode() == kMenuLanguageJapanese;
}

QString TranslateExact(const QString& text);
QString TranslateByObjectName(const QWidget* widget, const QString& text);
QString Tr(const QString& text);
QString TrWidgetText(const QWidget* widget, const QString& text);
QString SourcePropertyText(QWidget* widget, const char* propertyName, const QString& current);
QStringList SourcePropertyTextList(QWidget* widget, const char* propertyName, const QStringList& current);
QString SourceObjectPropertyText(QObject* object, const char* propertyName, const QString& current);
QString Tr(const char* text);
QString Tr(const char* text, int size);
QStringList TrList(const QStringList& items);
void LocalizeWidgetTextProperties(QWidget* widget);
void LocalizeWidgetTree(QWidget* root);
// Localize a melonDS-owned settings dialog when Menu Language is Japanese.
void LocalizeMelonDsDialog(QWidget* dialog);

#ifdef MELONPRIME_DS
// Localize on first Show (before paint) so widgets are fully built without a flash of English.
void InstallMelonDsDialogShowLocalizer(QWidget* dialog);

// Construct dialog, install show-time localizer, then show.
template<typename DialogT>
DialogT* OpenLocalizedMelonDsDialog(QWidget* parent)
{
    if (DialogT::currentDlg)
    {
        DialogT::currentDlg->activateWindow();
        return DialogT::currentDlg;
    }

    DialogT::currentDlg = new DialogT(parent);
    InstallMelonDsDialogShowLocalizer(DialogT::currentDlg);
    DialogT::currentDlg->show();
    return DialogT::currentDlg;
}

// One-shot dialogs without a static currentDlg (e.g. LAN host/join).
template<typename DialogT>
DialogT* OpenLocalizedMelonDsDialogOnce(QWidget* parent)
{
    DialogT* dlg = new DialogT(parent);
    InstallMelonDsDialogShowLocalizer(dlg);
    dlg->open();
    return dlg;
}
#endif

void LocalizeActionTextProperties(QAction* action);
void SetLocalizedActionText(QAction* action, const QString& sourceText);
void LocalizeAction(QAction* action);
void LocalizeMenu(QMenu* menu);
void LocalizeMenuBar(QMenuBar* menuBar);

} // namespace MelonPrime::UiText
