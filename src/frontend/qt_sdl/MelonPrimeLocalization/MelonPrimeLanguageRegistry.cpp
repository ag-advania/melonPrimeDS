#include "MelonPrimeLanguageRegistry.h"

#include <QLocale>
#include <QString>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace MelonPrime::UiText
{

namespace {

constexpr LanguageInfo kLanguageInfos[] = {
    {MenuLangId::Arabic, "ar", "العربية", MenuLangId::Arabic, true, TextDirection::RightToLeft, true, SplashFontGroup::Arabic},
    {MenuLangId::Italian, "it", "Italiano", MenuLangId::Italian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Indonesian, "id", "Bahasa Indonesia", MenuLangId::Indonesian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Ukrainian, "uk", "Українська", MenuLangId::Ukrainian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::English, "en", "English", MenuLangId::English, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::EnglishGB, "en-GB", "English (UK)", MenuLangId::English, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::EnglishUS, "en-US", "English (US)", MenuLangId::English, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Dutch, "nl", "Nederlands", MenuLangId::Dutch, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Greek, "el", "Ελληνικά", MenuLangId::Greek, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Korean, "ko", "한국어", MenuLangId::Korean, true, TextDirection::LeftToRight, false, SplashFontGroup::Korean},
    {MenuLangId::Swedish, "sv", "Svenska", MenuLangId::Swedish, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Spanish, "es", "Español (España)", MenuLangId::Spanish, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::SpanishLatAm, "es-419", "Español (Latinoamérica)", MenuLangId::Spanish, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Thai, "th", "ไทย", MenuLangId::Thai, true, TextDirection::LeftToRight, true, SplashFontGroup::Thai},
    {MenuLangId::Czech, "cs", "Čeština", MenuLangId::Czech, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::ChineseSimplified, "zh-Hans", "中文（简体）", MenuLangId::ChineseSimplified, true, TextDirection::LeftToRight, false, SplashFontGroup::ChineseSimplified},
    {MenuLangId::ChineseTraditional, "zh-Hant", "中文（繁體）", MenuLangId::ChineseSimplified, true, TextDirection::LeftToRight, false, SplashFontGroup::ChineseTraditional},
    {MenuLangId::Danish, "da", "Dansk", MenuLangId::Danish, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::German, "de", "Deutsch", MenuLangId::German, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Turkish, "tr", "Türkçe", MenuLangId::Turkish, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Japanese, "ja", "日本語", MenuLangId::Japanese, true, TextDirection::LeftToRight, false, SplashFontGroup::Japanese},
    {MenuLangId::Norwegian, "nb", "Norsk", MenuLangId::Norwegian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Hungarian, "hu", "Magyar", MenuLangId::Hungarian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Finnish, "fi", "Suomi", MenuLangId::Finnish, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::French, "fr", "Français (France)", MenuLangId::French, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::FrenchCanada, "fr-CA", "Français (Canada)", MenuLangId::French, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Vietnamese, "vi", "Tiếng Việt", MenuLangId::Vietnamese, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Polish, "pl", "Polski", MenuLangId::Polish, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Portuguese, "pt", "Português (Portugal)", MenuLangId::Portuguese, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::PortugueseBrazil, "pt-BR", "Português (Brasil)", MenuLangId::Portuguese, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Romanian, "ro", "Română", MenuLangId::Romanian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Russian, "ru", "Русский", MenuLangId::Russian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},

    // 50-language expansion (see tools/melonprime_all_new_language_metadata.json).
    // ChineseHongKong falls back to ChineseTraditional (same pattern as
    // EnglishGB/EnglishUS -> English); all others fall back to English via
    // the catalog lookup until real translations are added.
    {MenuLangId::Afrikaans, "af", "Afrikaans", MenuLangId::Afrikaans, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Irish, "ga", "Gaeilge", MenuLangId::Irish, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Icelandic, "is", "Íslenska", MenuLangId::Icelandic, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Azerbaijani, "az", "Azərbaycanca", MenuLangId::Azerbaijani, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Assamese, "as", "অসমীয়া", MenuLangId::Assamese, true, TextDirection::LeftToRight, true, SplashFontGroup::Bengali},
    {MenuLangId::Amharic, "am", "አማርኛ", MenuLangId::Amharic, true, TextDirection::LeftToRight, false, SplashFontGroup::Ethiopic},
    {MenuLangId::Albanian, "sq", "Shqip", MenuLangId::Albanian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Armenian, "hy", "Հայերեն", MenuLangId::Armenian, true, TextDirection::LeftToRight, false, SplashFontGroup::Armenian},
    {MenuLangId::Uzbek, "uz", "Oʻzbekcha", MenuLangId::Uzbek, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Urdu, "ur", "اردو", MenuLangId::Urdu, true, TextDirection::RightToLeft, true, SplashFontGroup::Arabic},
    {MenuLangId::Estonian, "et", "Eesti", MenuLangId::Estonian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Odia, "or", "ଓଡ଼ିଆ", MenuLangId::Odia, true, TextDirection::LeftToRight, true, SplashFontGroup::Odia},
    {MenuLangId::Kazakh, "kk", "Қазақша", MenuLangId::Kazakh, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Catalan, "ca", "Català", MenuLangId::Catalan, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Kannada, "kn", "ಕನ್ನಡ", MenuLangId::Kannada, true, TextDirection::LeftToRight, true, SplashFontGroup::Kannada},
    {MenuLangId::Kyrgyz, "ky", "Кыргызча", MenuLangId::Kyrgyz, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Gujarati, "gu", "ગુજરાતી", MenuLangId::Gujarati, true, TextDirection::LeftToRight, true, SplashFontGroup::Gujarati},
    {MenuLangId::Khmer, "km", "ខ្មែរ", MenuLangId::Khmer, true, TextDirection::LeftToRight, true, SplashFontGroup::Khmer},
    {MenuLangId::Croatian, "hr", "Hrvatski", MenuLangId::Croatian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Georgian, "ka", "ქართული", MenuLangId::Georgian, true, TextDirection::LeftToRight, false, SplashFontGroup::Georgian},
    {MenuLangId::Sinhala, "si", "සිංහල", MenuLangId::Sinhala, true, TextDirection::LeftToRight, true, SplashFontGroup::Sinhala},
    {MenuLangId::Swahili, "sw", "Kiswahili", MenuLangId::Swahili, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Slovak, "sk", "Slovenčina", MenuLangId::Slovak, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Slovenian, "sl", "Slovenščina", MenuLangId::Slovenian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Zulu, "zu", "isiZulu", MenuLangId::Zulu, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Serbian, "sr", "Српски", MenuLangId::Serbian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Tamil, "ta", "தமிழ்", MenuLangId::Tamil, true, TextDirection::LeftToRight, true, SplashFontGroup::Tamil},
    {MenuLangId::ChineseHongKong, "zh-HK", "中文（香港）", MenuLangId::ChineseTraditional, true, TextDirection::LeftToRight, false, SplashFontGroup::ChineseTraditional},
    {MenuLangId::Telugu, "te", "తెలుగు", MenuLangId::Telugu, true, TextDirection::LeftToRight, true, SplashFontGroup::Telugu},
    {MenuLangId::Nepali, "ne", "नेपाली", MenuLangId::Nepali, true, TextDirection::LeftToRight, true, SplashFontGroup::Devanagari},
    {MenuLangId::Basque, "eu", "Euskara", MenuLangId::Basque, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Punjabi, "pa", "ਪੰਜਾਬੀ", MenuLangId::Punjabi, true, TextDirection::LeftToRight, true, SplashFontGroup::Gurmukhi},
    {MenuLangId::Hindi, "hi", "हिन्दी", MenuLangId::Hindi, true, TextDirection::LeftToRight, true, SplashFontGroup::Devanagari},
    {MenuLangId::Filipino, "fil", "Filipino", MenuLangId::Filipino, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Bulgarian, "bg", "Български", MenuLangId::Bulgarian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Hebrew, "he", "עברית", MenuLangId::Hebrew, true, TextDirection::RightToLeft, true, SplashFontGroup::Hebrew},
    {MenuLangId::Belarusian, "be", "Беларуская", MenuLangId::Belarusian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Bengali, "bn", "বাংলা", MenuLangId::Bengali, true, TextDirection::LeftToRight, true, SplashFontGroup::Bengali},
    {MenuLangId::Persian, "fa", "فارسی", MenuLangId::Persian, true, TextDirection::RightToLeft, true, SplashFontGroup::Arabic},
    {MenuLangId::Bosnian, "bs", "Bosanski", MenuLangId::Bosnian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Macedonian, "mk", "Македонски", MenuLangId::Macedonian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Marathi, "mr", "मराठी", MenuLangId::Marathi, true, TextDirection::LeftToRight, true, SplashFontGroup::Devanagari},
    {MenuLangId::Malayalam, "ml", "മലയാളം", MenuLangId::Malayalam, true, TextDirection::LeftToRight, true, SplashFontGroup::Malayalam},
    {MenuLangId::Maltese, "mt", "Malti", MenuLangId::Maltese, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Malay, "ms", "Bahasa Melayu", MenuLangId::Malay, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Burmese, "my", "မြန်မာ", MenuLangId::Burmese, true, TextDirection::LeftToRight, true, SplashFontGroup::Myanmar},
    {MenuLangId::Mongolian, "mn", "Монгол", MenuLangId::Mongolian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Lao, "lo", "ລາວ", MenuLangId::Lao, true, TextDirection::LeftToRight, true, SplashFontGroup::Lao},
    {MenuLangId::Latvian, "lv", "Latviešu", MenuLangId::Latvian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
    {MenuLangId::Lithuanian, "lt", "Lietuvių", MenuLangId::Lithuanian, true, TextDirection::LeftToRight, false, SplashFontGroup::Latin},
};

} // namespace

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

    // 50-language expansion. Checked before the original language set below:
    // "fil" (Filipino) must be tested ahead of the existing "fi" (Finnish)
    // check, since "fi" is a strict prefix of "fil" and LanguageTagMatches
    // only tests startsWith(). All other new codes are unambiguous two-letter
    // codes with no prefix relationship to any existing or new code.
    if (LanguageTagMatches(tag, "af"))
        return MenuLangId::Afrikaans;
    if (LanguageTagMatches(tag, "ga"))
        return MenuLangId::Irish;
    if (LanguageTagMatches(tag, "is"))
        return MenuLangId::Icelandic;
    if (LanguageTagMatches(tag, "az"))
        return MenuLangId::Azerbaijani;
    if (LanguageTagMatches(tag, "as"))
        return MenuLangId::Assamese;
    if (LanguageTagMatches(tag, "am"))
        return MenuLangId::Amharic;
    if (LanguageTagMatches(tag, "sq"))
        return MenuLangId::Albanian;
    if (LanguageTagMatches(tag, "hy"))
        return MenuLangId::Armenian;
    if (LanguageTagMatches(tag, "uz"))
        return MenuLangId::Uzbek;
    if (LanguageTagMatches(tag, "ur"))
        return MenuLangId::Urdu;
    if (LanguageTagMatches(tag, "et"))
        return MenuLangId::Estonian;
    if (LanguageTagMatches(tag, "or"))
        return MenuLangId::Odia;
    if (LanguageTagMatches(tag, "kk"))
        return MenuLangId::Kazakh;
    if (LanguageTagMatches(tag, "ca"))
        return MenuLangId::Catalan;
    if (LanguageTagMatches(tag, "kn"))
        return MenuLangId::Kannada;
    if (LanguageTagMatches(tag, "ky"))
        return MenuLangId::Kyrgyz;
    if (LanguageTagMatches(tag, "gu"))
        return MenuLangId::Gujarati;
    if (LanguageTagMatches(tag, "km"))
        return MenuLangId::Khmer;
    if (LanguageTagMatches(tag, "hr"))
        return MenuLangId::Croatian;
    if (LanguageTagMatches(tag, "ka"))
        return MenuLangId::Georgian;
    if (LanguageTagMatches(tag, "si"))
        return MenuLangId::Sinhala;
    if (LanguageTagMatches(tag, "sw"))
        return MenuLangId::Swahili;
    if (LanguageTagMatches(tag, "sk"))
        return MenuLangId::Slovak;
    if (LanguageTagMatches(tag, "sl"))
        return MenuLangId::Slovenian;
    if (LanguageTagMatches(tag, "zu"))
        return MenuLangId::Zulu;
    if (LanguageTagMatches(tag, "sr"))
        return MenuLangId::Serbian;
    if (LanguageTagMatches(tag, "ta"))
        return MenuLangId::Tamil;
    if (LanguageTagMatches(tag, "te"))
        return MenuLangId::Telugu;
    if (LanguageTagMatches(tag, "ne"))
        return MenuLangId::Nepali;
    if (LanguageTagMatches(tag, "eu"))
        return MenuLangId::Basque;
    if (LanguageTagMatches(tag, "pa"))
        return MenuLangId::Punjabi;
    if (LanguageTagMatches(tag, "hi"))
        return MenuLangId::Hindi;
    if (LanguageTagMatches(tag, "fil"))
        return MenuLangId::Filipino;
    if (LanguageTagMatches(tag, "bg"))
        return MenuLangId::Bulgarian;
    if (LanguageTagMatches(tag, "he"))
        return MenuLangId::Hebrew;
    if (LanguageTagMatches(tag, "be"))
        return MenuLangId::Belarusian;
    if (LanguageTagMatches(tag, "bn"))
        return MenuLangId::Bengali;
    if (LanguageTagMatches(tag, "fa"))
        return MenuLangId::Persian;
    if (LanguageTagMatches(tag, "bs"))
        return MenuLangId::Bosnian;
    if (LanguageTagMatches(tag, "mk"))
        return MenuLangId::Macedonian;
    if (LanguageTagMatches(tag, "mr"))
        return MenuLangId::Marathi;
    if (LanguageTagMatches(tag, "ml"))
        return MenuLangId::Malayalam;
    if (LanguageTagMatches(tag, "mt"))
        return MenuLangId::Maltese;
    if (LanguageTagMatches(tag, "ms"))
        return MenuLangId::Malay;
    if (LanguageTagMatches(tag, "my"))
        return MenuLangId::Burmese;
    if (LanguageTagMatches(tag, "mn"))
        return MenuLangId::Mongolian;
    if (LanguageTagMatches(tag, "lo"))
        return MenuLangId::Lao;
    if (LanguageTagMatches(tag, "lv"))
        return MenuLangId::Latvian;
    if (LanguageTagMatches(tag, "lt"))
        return MenuLangId::Lithuanian;

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
        if (regionIs("hk"))
            return MenuLangId::ChineseHongKong;
        if (regionIs("tw") || regionIs("hant"))
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
        if (territory == QLocale::HongKong)
            return MenuLangId::ChineseHongKong;
        if (territory == QLocale::Taiwan)
            return MenuLangId::ChineseTraditional;
        return MenuLangId::ChineseSimplified;
    case QLocale::Korean: return MenuLangId::Korean;
    case QLocale::English:
        if (territory == QLocale::UnitedKingdom)
            return MenuLangId::EnglishGB;
        if (territory == QLocale::UnitedStates)
            return MenuLangId::EnglishUS;
        return MenuLangId::English;

    // 50-language expansion.
    case QLocale::Afrikaans: return MenuLangId::Afrikaans;
    case QLocale::Irish: return MenuLangId::Irish;
    case QLocale::Icelandic: return MenuLangId::Icelandic;
    case QLocale::Azerbaijani: return MenuLangId::Azerbaijani;
    case QLocale::Assamese: return MenuLangId::Assamese;
    case QLocale::Amharic: return MenuLangId::Amharic;
    case QLocale::Albanian: return MenuLangId::Albanian;
    case QLocale::Armenian: return MenuLangId::Armenian;
    case QLocale::Uzbek: return MenuLangId::Uzbek;
    case QLocale::Urdu: return MenuLangId::Urdu;
    case QLocale::Estonian: return MenuLangId::Estonian;
    case QLocale::Odia: return MenuLangId::Odia;
    case QLocale::Kazakh: return MenuLangId::Kazakh;
    case QLocale::Catalan: return MenuLangId::Catalan;
    case QLocale::Kannada: return MenuLangId::Kannada;
    case QLocale::Kyrgyz: return MenuLangId::Kyrgyz;
    case QLocale::Gujarati: return MenuLangId::Gujarati;
    case QLocale::Khmer: return MenuLangId::Khmer;
    case QLocale::Croatian: return MenuLangId::Croatian;
    case QLocale::Georgian: return MenuLangId::Georgian;
    case QLocale::Sinhala: return MenuLangId::Sinhala;
    case QLocale::Swahili: return MenuLangId::Swahili;
    case QLocale::Slovak: return MenuLangId::Slovak;
    case QLocale::Slovenian: return MenuLangId::Slovenian;
    case QLocale::Zulu: return MenuLangId::Zulu;
    case QLocale::Serbian: return MenuLangId::Serbian;
    case QLocale::Tamil: return MenuLangId::Tamil;
    case QLocale::Telugu: return MenuLangId::Telugu;
    case QLocale::Nepali: return MenuLangId::Nepali;
    case QLocale::Basque: return MenuLangId::Basque;
    case QLocale::Punjabi: return MenuLangId::Punjabi;
    case QLocale::Hindi: return MenuLangId::Hindi;
    case QLocale::Filipino: return MenuLangId::Filipino;
    case QLocale::Bulgarian: return MenuLangId::Bulgarian;
    case QLocale::Hebrew: return MenuLangId::Hebrew;
    case QLocale::Belarusian: return MenuLangId::Belarusian;
    case QLocale::Bengali: return MenuLangId::Bengali;
    case QLocale::Persian: return MenuLangId::Persian;
    case QLocale::Bosnian: return MenuLangId::Bosnian;
    case QLocale::Macedonian: return MenuLangId::Macedonian;
    case QLocale::Marathi: return MenuLangId::Marathi;
    case QLocale::Malayalam: return MenuLangId::Malayalam;
    case QLocale::Maltese: return MenuLangId::Maltese;
    case QLocale::Malay: return MenuLangId::Malay;
    case QLocale::Burmese: return MenuLangId::Burmese;
    case QLocale::Mongolian: return MenuLangId::Mongolian;
    case QLocale::Lao: return MenuLangId::Lao;
    case QLocale::Latvian: return MenuLangId::Latvian;
    case QLocale::Lithuanian: return MenuLangId::Lithuanian;

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
        // "fil" must precede "fi": "fi" is a strict prefix of "fil" and
        // LanguageTagMatches only tests startsWith().
        "af", "ga", "is", "az", "as", "am", "sq", "hy", "uz", "ur", "et", "or", "kk",
        "ca", "kn", "ky", "gu", "km", "hr", "ka", "si", "sw", "sk", "sl", "zu", "sr",
        "ta", "te", "ne", "eu", "pa", "hi", "fil", "bg", "he", "be", "bn", "fa", "bs",
        "mk", "mr", "ml", "mt", "ms", "my", "mn", "lo", "lv", "lt",
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

const LanguageInfo* FindLanguageInfo(MenuLangId id)
{
    for (const LanguageInfo& info : kLanguageInfos)
    {
        if (info.id == id)
            return &info;
    }
    return nullptr;
}

const LanguageInfo& LanguageInfoOrEnglish(MenuLangId id)
{
    if (const LanguageInfo* info = FindLanguageInfo(id))
        return *info;
    return *FindLanguageInfo(MenuLangId::English);
}

MenuLangId ResolveTranslationLanguage(MenuLangId lang)
{
    return LanguageInfoOrEnglish(lang).translationBase;
}

bool IsRightToLeftLanguage(MenuLangId lang)
{
    return LanguageInfoOrEnglish(lang).direction == TextDirection::RightToLeft;
}

bool RequiresShapedSplashText(MenuLangId lang)
{
    return LanguageInfoOrEnglish(lang).requiresShapedSplash;
}

SplashFontGroup SplashFontGroupForLanguage(MenuLangId lang)
{
    return LanguageInfoOrEnglish(lang).splashFontGroup;
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
    return ResolveTranslationLanguage(lang) == MenuLangId::English;
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
    return QString::fromUtf8(LanguageInfoOrEnglish(lang).displayName);
}

QString MenuLanguageNativeLabel()
{
    return MenuLanguageDisplayName(DetectSystemMenuLanguage());
}

QList<MenuLangId> AllSelectableMenuLanguages()
{
    QList<MenuLangId> langs;
    langs.reserve(static_cast<int>(MenuLangId::Count) - static_cast<int>(MenuLangId::First));
    for (const LanguageInfo& info : kLanguageInfos)
    {
        if (info.selectable)
            langs.append(info.id);
    }
    return langs;
}

} // namespace MelonPrime::UiText
