#include "MelonPrimeTranslationCatalog.h"

#include "MelonPrimeLanguageRegistry.h"

#include <QString>

namespace MelonPrime::UiText
{

namespace {

struct Translation
{
    const char* en;
    const char* ja;
    const char* de;
    const char* es;
    const char* fr;
    const char* it;
    const char* nl;
    const char* pt;
    const char* ru;
    const char* zh;
    const char* ko;
    const char* ar;
    const char* id;
    const char* uk;
    const char* el;
    const char* sv;
    const char* th;
    const char* cs;
    const char* da;
    const char* tr;
    const char* nb;
    const char* hu;
    const char* fi;
    const char* vi;
    const char* pl;
    const char* ro;
};

struct ObjectTextTranslation
{
    const char* objectName;
    const char* ja;
    const char* de;
    const char* es;
    const char* fr;
    const char* it;
    const char* nl;
    const char* pt;
    const char* ru;
    const char* zh;
    const char* ko;
    const char* ar;
    const char* id;
    const char* uk;
    const char* el;
    const char* sv;
    const char* th;
    const char* cs;
    const char* da;
    const char* tr;
    const char* nb;
    const char* hu;
    const char* fi;
    const char* vi;
    const char* pl;
    const char* ro;
};

const char* TranslationFieldForLang(const Translation& entry, MenuLangId lang)
{
    switch (ResolveTranslationLanguage(lang)) {
    case MenuLangId::Japanese: return entry.ja;
    case MenuLangId::German: return entry.de;
    case MenuLangId::Spanish: return entry.es;
    case MenuLangId::French: return entry.fr;
    case MenuLangId::Italian: return entry.it;
    case MenuLangId::Dutch: return entry.nl;
    case MenuLangId::Portuguese: return entry.pt;
    case MenuLangId::Russian: return entry.ru;
    case MenuLangId::ChineseSimplified: return entry.zh;
    case MenuLangId::ChineseTraditional: return entry.zh;
    case MenuLangId::Korean: return entry.ko;
    case MenuLangId::Arabic: return entry.ar;
    case MenuLangId::Indonesian: return entry.id;
    case MenuLangId::Ukrainian: return entry.uk;
    case MenuLangId::Greek: return entry.el;
    case MenuLangId::Swedish: return entry.sv;
    case MenuLangId::Thai: return entry.th;
    case MenuLangId::Czech: return entry.cs;
    case MenuLangId::Danish: return entry.da;
    case MenuLangId::Turkish: return entry.tr;
    case MenuLangId::Norwegian: return entry.nb;
    case MenuLangId::Hungarian: return entry.hu;
    case MenuLangId::Finnish: return entry.fi;
    case MenuLangId::Vietnamese: return entry.vi;
    case MenuLangId::Polish: return entry.pl;
    case MenuLangId::Romanian: return entry.ro;
    default: return entry.en;
    }
}

const char* ObjectTranslationFieldForLang(const ObjectTextTranslation& entry, MenuLangId lang)
{
    switch (ResolveTranslationLanguage(lang)) {
    case MenuLangId::Japanese: return entry.ja;
    case MenuLangId::German: return entry.de;
    case MenuLangId::Spanish: return entry.es;
    case MenuLangId::French: return entry.fr;
    case MenuLangId::Italian: return entry.it;
    case MenuLangId::Dutch: return entry.nl;
    case MenuLangId::Portuguese: return entry.pt;
    case MenuLangId::Russian: return entry.ru;
    case MenuLangId::ChineseSimplified: return entry.zh;
    case MenuLangId::ChineseTraditional: return entry.zh;
    case MenuLangId::Korean: return entry.ko;
    case MenuLangId::Arabic: return entry.ar;
    case MenuLangId::Indonesian: return entry.id;
    case MenuLangId::Ukrainian: return entry.uk;
    case MenuLangId::Greek: return entry.el;
    case MenuLangId::Swedish: return entry.sv;
    case MenuLangId::Thai: return entry.th;
    case MenuLangId::Czech: return entry.cs;
    case MenuLangId::Danish: return entry.da;
    case MenuLangId::Turkish: return entry.tr;
    case MenuLangId::Norwegian: return entry.nb;
    case MenuLangId::Hungarian: return entry.hu;
    case MenuLangId::Finnish: return entry.fi;
    case MenuLangId::Vietnamese: return entry.vi;
    case MenuLangId::Polish: return entry.pl;
    case MenuLangId::Romanian: return entry.ro;
    default: return nullptr;
    }
}

#include "MelonPrimeTranslations.inc"

#include "MelonPrimeObjectTranslations.inc"

} // namespace

QString TranslateExact(const QString& text)
{
    const MenuLangId lang = ActiveMenuLanguage();
    if (IsEnglishMenuLanguage(lang))
        return text;

    for (const Translation& entry : kTranslations)
    {
        if (text == QString::fromUtf8(entry.en))
        {
            const char* localized = TranslationFieldForLang(entry, lang);
            return QString::fromUtf8(localized ? localized : entry.en);
        }
    }
    return text;
}

QString TranslateByObjectName(const QWidget* widget, const QString& text)
{
    if (!widget || text.isEmpty())
        return text;

    const MenuLangId lang = ActiveMenuLanguage();
    if (IsEnglishMenuLanguage(lang))
        return text;

    const QString objectName = widget->objectName();
    if (objectName.isEmpty())
        return text;

    for (const ObjectTextTranslation& entry : kObjectTextTranslations)
    {
        if (objectName == QString::fromUtf8(entry.objectName))
        {
            const char* localized = ObjectTranslationFieldForLang(entry, lang);
            return localized ? QString::fromUtf8(localized) : text;
        }
    }
    return text;
}

} // namespace MelonPrime::UiText
