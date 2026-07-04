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
void LocalizeActionTextProperties(QAction* action);
void SetLocalizedActionText(QAction* action, const QString& sourceText);
void LocalizeAction(QAction* action);
void LocalizeMenu(QMenu* menu);
void LocalizeMenuBar(QMenuBar* menuBar);

} // namespace MelonPrime::UiText
