#pragma once

#include <QAction>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QStringList>
#include <QWidget>

class QImage;

namespace MelonPrime::UiText
{

enum class MenuLangId : int
{
    English = 0,
    Japanese,
    German,
    Spanish,
    French,
};

// Config: 0 = OS native menu language, 1 = force English.
inline constexpr int kMenuLanguageNative = 0;
inline constexpr int kMenuLanguageEnglish = 1;

// Legacy aliases (config value 0 always meant "native", not "Japanese only").
inline constexpr int kMenuLanguageJapanese = kMenuLanguageNative;

MenuLangId DetectSystemMenuLanguage();
bool IsMenuTranslationActive();
MenuLangId ActiveMenuLanguage();

// Legacy helpers kept for call sites that still name Japanese explicitly.
bool IsJapaneseSystemLocale();
inline bool IsJapaneseLocale()
{
    return IsMenuTranslationActive() && ActiveMenuLanguage() == MenuLangId::Japanese;
}
inline bool CanChooseMenuLanguage()
{
    return DetectSystemMenuLanguage() != MenuLangId::English;
}

inline int& MenuLanguageModeStorage()
{
    static int mode = kMenuLanguageNative;
    return mode;
}

inline void SetMenuLanguageMode(int mode)
{
    MenuLanguageModeStorage() =
        (mode == kMenuLanguageEnglish) ? kMenuLanguageEnglish : kMenuLanguageNative;
}

inline int MenuLanguageMode()
{
    return MenuLanguageModeStorage();
}

QString MenuLanguageDisplayName(MenuLangId lang);
QString MenuLanguageNativeLabel();

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
// Localize a melonDS-owned settings dialog when a non-English menu language is active.
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
// No-ROM splash lines (ScreenPanel::splashText[0/1]); English source keys stay upstream-owned.
void ApplyNoRomSplashLocalization(char line0[256], char line1[256]);
// Bitmap OSD font is ASCII-only; render localized splash lines with a UI font when needed.
bool TryRenderNoRomSplashOsdItem(unsigned int id, const char* text, unsigned int color,
    int rainbowstart, int& rainbowend, int maxWidth, QImage* outBitmap);
// CJK splash lines or proportional Latin fonts that may need vertical stacking.
bool UsesLocalizedSplashLayout();

} // namespace MelonPrime::UiText
