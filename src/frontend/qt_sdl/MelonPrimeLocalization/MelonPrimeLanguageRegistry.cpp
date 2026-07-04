#include "MelonPrimeLanguageRegistry.h"

#include <QLocale>
#include <QString>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace MelonPrime::UiText
{

namespace {

bool LanguageTagMatches(const QString& tag, const char* prefix)
{
    if (tag.isEmpty())
        return false;
    return QString(tag).toLower().replace(QLatin1Char('-'), QLatin1Char('_'))
        .startsWith(QString::fromLatin1(prefix));
}

#ifdef __APPLE__
bool ApplePreferredLanguagesContain(const char* prefix)
{
    CFPropertyListRef value = CFPreferencesCopyValue(
        CFSTR("AppleLanguages"),
        kCFPreferencesAnyApplication,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost);
    if (!value)
        return false;

    bool found = false;
    if (CFGetTypeID(value) == CFArrayGetTypeID()) {
        const CFArrayRef langs = static_cast<CFArrayRef>(value);
        const CFIndex count = CFArrayGetCount(langs);
        for (CFIndex i = 0; i < count; ++i) {
            const CFStringRef lang =
                static_cast<CFStringRef>(CFArrayGetValueAtIndex(langs, i));
            if (!lang)
                continue;

            char buf[32];
            if (!CFStringGetCString(lang, buf, sizeof(buf), kCFStringEncodingUTF8))
                continue;

            if (LanguageTagMatches(QString::fromUtf8(buf), prefix)) {
                found = true;
                break;
            }
        }
    }

    CFRelease(value);
    return found;
}
#endif

MenuLangId LanguageTagToMenuLang(const QString& tag)
{
    const QString lower = QString(tag).toLower().replace(QLatin1Char('-'), QLatin1Char('_'));

    auto regionIs = [&](const char* region) -> bool
    {
        const int idx = lower.indexOf(QLatin1Char('_'));
        if (idx < 0)
            return false;
        return lower.mid(idx + 1).startsWith(QString::fromLatin1(region));
    };

    if (LanguageTagMatches(tag, "ar"))
        return MenuLangId::Arabic;
    if (LanguageTagMatches(tag, "id"))
        return MenuLangId::Indonesian;
    if (LanguageTagMatches(tag, "uk"))
        return MenuLangId::Ukrainian;
    if (LanguageTagMatches(tag, "el"))
        return MenuLangId::Greek;
    if (LanguageTagMatches(tag, "sv"))
        return MenuLangId::Swedish;
    if (LanguageTagMatches(tag, "th"))
        return MenuLangId::Thai;
    if (LanguageTagMatches(tag, "cs"))
        return MenuLangId::Czech;
    if (LanguageTagMatches(tag, "da"))
        return MenuLangId::Danish;
    if (LanguageTagMatches(tag, "tr"))
        return MenuLangId::Turkish;
    if (LanguageTagMatches(tag, "nb") || LanguageTagMatches(tag, "no"))
        return MenuLangId::Norwegian;
    if (LanguageTagMatches(tag, "hu"))
        return MenuLangId::Hungarian;
    if (LanguageTagMatches(tag, "fi"))
        return MenuLangId::Finnish;
    if (LanguageTagMatches(tag, "vi"))
        return MenuLangId::Vietnamese;
    if (LanguageTagMatches(tag, "pl"))
        return MenuLangId::Polish;
    if (LanguageTagMatches(tag, "ro"))
        return MenuLangId::Romanian;

    if (LanguageTagMatches(tag, "en"))
    {
        if (regionIs("gb"))
            return MenuLangId::EnglishGB;
        if (regionIs("us"))
            return MenuLangId::EnglishUS;
        return MenuLangId::English;
    }
    if (LanguageTagMatches(tag, "ja"))
        return MenuLangId::Japanese;
    if (LanguageTagMatches(tag, "de"))
        return MenuLangId::German;
    if (LanguageTagMatches(tag, "es"))
    {
        if (regionIs("419") || regionIs("mx") || regionIs("ar") || regionIs("co")
            || regionIs("cl") || regionIs("pe"))
            return MenuLangId::SpanishLatAm;
        return MenuLangId::Spanish;
    }
    if (LanguageTagMatches(tag, "fr"))
    {
        if (regionIs("ca"))
            return MenuLangId::FrenchCanada;
        return MenuLangId::French;
    }
    if (LanguageTagMatches(tag, "it"))
        return MenuLangId::Italian;
    if (LanguageTagMatches(tag, "nl"))
        return MenuLangId::Dutch;
    if (LanguageTagMatches(tag, "pt"))
    {
        if (regionIs("br"))
            return MenuLangId::PortugueseBrazil;
        return MenuLangId::Portuguese;
    }
    if (LanguageTagMatches(tag, "ru"))
        return MenuLangId::Russian;
    if (LanguageTagMatches(tag, "zh"))
    {
        if (regionIs("tw") || regionIs("hk") || regionIs("hant"))
            return MenuLangId::ChineseTraditional;
        return MenuLangId::ChineseSimplified;
    }
    if (LanguageTagMatches(tag, "ko"))
        return MenuLangId::Korean;
    return MenuLangId::English;
}

MenuLangId QLocaleLanguageToMenuLang(QLocale::Language language, QLocale::Territory territory)
{
    switch (language) {
    case QLocale::Arabic: return MenuLangId::Arabic;
    case QLocale::Indonesian: return MenuLangId::Indonesian;
    case QLocale::Ukrainian: return MenuLangId::Ukrainian;
    case QLocale::Greek: return MenuLangId::Greek;
    case QLocale::Swedish: return MenuLangId::Swedish;
    case QLocale::Thai: return MenuLangId::Thai;
    case QLocale::Czech: return MenuLangId::Czech;
    case QLocale::Danish: return MenuLangId::Danish;
    case QLocale::Turkish: return MenuLangId::Turkish;
    case QLocale::NorwegianBokmal:
    case QLocale::NorwegianNynorsk: return MenuLangId::Norwegian;
    case QLocale::Hungarian: return MenuLangId::Hungarian;
    case QLocale::Finnish: return MenuLangId::Finnish;
    case QLocale::Vietnamese: return MenuLangId::Vietnamese;
    case QLocale::Polish: return MenuLangId::Polish;
    case QLocale::Romanian: return MenuLangId::Romanian;
    case QLocale::Japanese: return MenuLangId::Japanese;
    case QLocale::German: return MenuLangId::German;
    case QLocale::Spanish:
        if (territory == QLocale::Mexico || territory == QLocale::Argentina
            || territory == QLocale::Colombia || territory == QLocale::Chile
            || territory == QLocale::Peru || territory == QLocale::Venezuela)
            return MenuLangId::SpanishLatAm;
        return MenuLangId::Spanish;
    case QLocale::French:
        if (territory == QLocale::Canada)
            return MenuLangId::FrenchCanada;
        return MenuLangId::French;
    case QLocale::Italian: return MenuLangId::Italian;
    case QLocale::Dutch: return MenuLangId::Dutch;
    case QLocale::Portuguese:
        if (territory == QLocale::Brazil)
            return MenuLangId::PortugueseBrazil;
        return MenuLangId::Portuguese;
    case QLocale::Russian: return MenuLangId::Russian;
    case QLocale::Chinese:
        if (territory == QLocale::Taiwan || territory == QLocale::HongKong)
            return MenuLangId::ChineseTraditional;
        return MenuLangId::ChineseSimplified;
    case QLocale::Korean: return MenuLangId::Korean;
    case QLocale::English:
        if (territory == QLocale::UnitedKingdom)
            return MenuLangId::EnglishGB;
        if (territory == QLocale::UnitedStates)
            return MenuLangId::EnglishUS;
        return MenuLangId::English;
    default: break;
    }
    return MenuLangId::English;
}

MenuLangId DetectLanguageFromEnvironment()
{
    const QLocale sys = QLocale::system();
    {
        const MenuLangId lang = QLocaleLanguageToMenuLang(sys.language(), sys.territory());
        if (lang != MenuLangId::English)
            return lang;
    }

    for (const QString& tag : sys.uiLanguages()) {
        const MenuLangId lang = LanguageTagToMenuLang(tag);
        if (lang != MenuLangId::English)
            return lang;
    }

    {
        const MenuLangId lang = LanguageTagToMenuLang(sys.name());
        if (lang != MenuLangId::English)
            return lang;
    }

#ifdef __APPLE__
    const char* applePrefixes[] = {
        "ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "no", "hu", "fi",
        "vi", "pl", "ro", "en", "ja", "de", "es", "fr", "it", "nl", "pt", "ru", "zh", "ko",
    };
    for (const char* prefix : applePrefixes) {
        if (ApplePreferredLanguagesContain(prefix))
            return LanguageTagToMenuLang(QString::fromLatin1(prefix));
    }
#endif

    for (const char* envName : {"LANG", "LC_ALL", "LC_MESSAGES", "LANGUAGE"}) {
        const MenuLangId lang =
            LanguageTagToMenuLang(QString::fromLatin1(qgetenv(envName)));
        if (lang != MenuLangId::English)
            return lang;
    }

    return MenuLangId::English;
}

} // namespace

MenuLangId ResolveTranslationLanguage(MenuLangId lang)
{
    switch (lang) {
    case MenuLangId::EnglishGB:
    case MenuLangId::EnglishUS:
        return MenuLangId::English;
    case MenuLangId::SpanishLatAm:
        return MenuLangId::Spanish;
    case MenuLangId::FrenchCanada:
        return MenuLangId::French;
    case MenuLangId::PortugueseBrazil:
        return MenuLangId::Portuguese;
    case MenuLangId::ChineseTraditional:
        return MenuLangId::ChineseSimplified;
    default:
        return lang;
    }
}

int NormalizeMenuLanguageConfig(int storedValue)
{
    if (storedValue == kMenuLanguageLegacyNative)
        return kMenuLanguageSystemDefault;
    if (storedValue == kMenuLanguageLegacyEnglish)
        return static_cast<int>(MenuLangId::English);
    if (storedValue >= static_cast<int>(MenuLangId::First)
        && storedValue < static_cast<int>(MenuLangId::Count))
        return storedValue;
    if (storedValue == kMenuLanguageSystemDefault)
        return storedValue;
    return storedValue;
}

bool IsEnglishMenuLanguage(MenuLangId lang)
{
    switch (lang) {
    case MenuLangId::English:
    case MenuLangId::EnglishGB:
    case MenuLangId::EnglishUS:
        return true;
    default:
        return false;
    }
}

MenuLangId DetectSystemMenuLanguage()
{
    static const MenuLangId lang = DetectLanguageFromEnvironment();
    return lang;
}

bool IsJapaneseSystemLocale()
{
    return DetectSystemMenuLanguage() == MenuLangId::Japanese;
}

int MenuLanguageSelection()
{
    return MenuLanguageModeStorage();
}

void SetMenuLanguageSelection(int configValue)
{
    MenuLanguageModeStorage() = NormalizeMenuLanguageConfig(configValue);
}

bool IsMenuTranslationActive()
{
    return !IsEnglishMenuLanguage(ActiveMenuLanguage());
}

MenuLangId ActiveMenuLanguage()
{
    const int selection = MenuLanguageSelection();
    if (selection == kMenuLanguageSystemDefault)
        return DetectSystemMenuLanguage();
    if (selection >= static_cast<int>(MenuLangId::First)
        && selection < static_cast<int>(MenuLangId::Count))
        return static_cast<MenuLangId>(selection);
    return MenuLangId::English;
}

QString MenuLanguageDisplayName(MenuLangId lang)
{
    switch (lang) {
    case MenuLangId::Arabic: return QStringLiteral("العربية");
    case MenuLangId::Italian: return QStringLiteral("Italiano");
    case MenuLangId::Indonesian: return QStringLiteral("Bahasa Indonesia");
    case MenuLangId::Ukrainian: return QStringLiteral("Українська");
    case MenuLangId::English: return QStringLiteral("English");
    case MenuLangId::EnglishGB: return QStringLiteral("English (UK)");
    case MenuLangId::EnglishUS: return QStringLiteral("English (US)");
    case MenuLangId::Dutch: return QStringLiteral("Nederlands");
    case MenuLangId::Greek: return QStringLiteral("Ελληνικά");
    case MenuLangId::Korean: return QStringLiteral("한국어");
    case MenuLangId::Swedish: return QStringLiteral("Svenska");
    case MenuLangId::Spanish: return QStringLiteral("Español (España)");
    case MenuLangId::SpanishLatAm: return QStringLiteral("Español (Latinoamérica)");
    case MenuLangId::Thai: return QStringLiteral("ไทย");
    case MenuLangId::Czech: return QStringLiteral("Čeština");
    case MenuLangId::ChineseSimplified: return QStringLiteral("中文（简体）");
    case MenuLangId::ChineseTraditional: return QStringLiteral("中文（繁體，简体fallback）");
    case MenuLangId::Danish: return QStringLiteral("Dansk");
    case MenuLangId::German: return QStringLiteral("Deutsch");
    case MenuLangId::Turkish: return QStringLiteral("Türkçe");
    case MenuLangId::Japanese: return QStringLiteral("日本語");
    case MenuLangId::Norwegian: return QStringLiteral("Norsk");
    case MenuLangId::Hungarian: return QStringLiteral("Magyar");
    case MenuLangId::Finnish: return QStringLiteral("Suomi");
    case MenuLangId::French: return QStringLiteral("Français (France)");
    case MenuLangId::FrenchCanada: return QStringLiteral("Français (Canada)");
    case MenuLangId::Vietnamese: return QStringLiteral("Tiếng Việt");
    case MenuLangId::Polish: return QStringLiteral("Polski");
    case MenuLangId::Portuguese: return QStringLiteral("Português (Portugal)");
    case MenuLangId::PortugueseBrazil: return QStringLiteral("Português (Brasil)");
    case MenuLangId::Romanian: return QStringLiteral("Română");
    case MenuLangId::Russian: return QStringLiteral("Русский");
    default: return QStringLiteral("English");
    }
}

QString MenuLanguageNativeLabel()
{
    return MenuLanguageDisplayName(DetectSystemMenuLanguage());
}

QList<MenuLangId> AllSelectableMenuLanguages()
{
    QList<MenuLangId> langs;
    langs.reserve(static_cast<int>(MenuLangId::Count) - static_cast<int>(MenuLangId::First));
    for (int i = static_cast<int>(MenuLangId::First);
        i < static_cast<int>(MenuLangId::Count);
        ++i)
    {
        const MenuLangId lang = static_cast<MenuLangId>(i);
        if (lang == MenuLangId::ChineseTraditional)
            continue;
        langs.append(lang);
    }
    return langs;
}

} // namespace MelonPrime::UiText
