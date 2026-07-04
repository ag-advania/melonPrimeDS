#include "MelonPrimeTranslationCatalog.h"

#include "MelonPrimeLanguageRegistry.h"

#include <QDebug>
#include <QHash>
#include <QString>

#include <iterator>

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

class TranslationCatalog
{
public:
    static const TranslationCatalog& Instance();

    QString translateExact(const QString& text, MenuLangId lang) const;
    QString translateObjectText(const QString& objectName, const QString& fallbackText, MenuLangId lang) const;

private:
    TranslationCatalog();

    QHash<QString, const Translation*> m_exact;
    QHash<QString, const ObjectTextTranslation*> m_object;
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

void ReportCatalogIssue(const char* context, const QString& key, const char* issue)
{
    qWarning() << "MelonPrime localization catalog" << context << issue << key;
    Q_ASSERT_X(false, context, issue);
}

const TranslationCatalog& TranslationCatalog::Instance()
{
    static const TranslationCatalog catalog;
    return catalog;
}

TranslationCatalog::TranslationCatalog()
{
    m_exact.reserve(std::size(kTranslations));
    for (const Translation& entry : kTranslations)
    {
        const QString key = entry.en ? QString::fromUtf8(entry.en) : QString();
        if (key.isEmpty())
        {
            ReportCatalogIssue("exact", key, "has empty English key");
            continue;
        }
        if (m_exact.contains(key))
        {
            ReportCatalogIssue("exact", key, "has duplicate key");
            continue;
        }
        m_exact.insert(key, &entry);
    }

    m_object.reserve(std::size(kObjectTextTranslations));
    for (const ObjectTextTranslation& entry : kObjectTextTranslations)
    {
        const QString key = entry.objectName ? QString::fromUtf8(entry.objectName) : QString();
        if (key.isEmpty())
        {
            ReportCatalogIssue("object", key, "has empty objectName");
            continue;
        }
        if (m_object.contains(key))
        {
            ReportCatalogIssue("object", key, "has duplicate objectName");
            continue;
        }
        m_object.insert(key, &entry);
    }
}

QString TranslationCatalog::translateExact(const QString& text, MenuLangId lang) const
{
    const auto it = m_exact.constFind(text);
    if (it == m_exact.cend())
        return text;

    const Translation& entry = **it;
    const char* localized = TranslationFieldForLang(entry, lang);
    return QString::fromUtf8(localized ? localized : entry.en);
}

QString TranslationCatalog::translateObjectText(
    const QString& objectName,
    const QString& fallbackText,
    MenuLangId lang) const
{
    const auto it = m_object.constFind(objectName);
    if (it == m_object.cend())
        return fallbackText;

    const char* localized = ObjectTranslationFieldForLang(**it, lang);
    return localized ? QString::fromUtf8(localized) : fallbackText;
}

} // namespace

QString TranslateExact(const QString& text)
{
    const MenuLangId lang = ActiveMenuLanguage();
    if (IsEnglishMenuLanguage(lang))
        return text;

    return TranslationCatalog::Instance().translateExact(text, lang);
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

    return TranslationCatalog::Instance().translateObjectText(objectName, text, lang);
}

} // namespace MelonPrime::UiText
