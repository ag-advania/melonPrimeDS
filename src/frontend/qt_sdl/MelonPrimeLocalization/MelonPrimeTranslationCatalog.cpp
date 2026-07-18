#include "MelonPrimeTranslationCatalog.h"

#include "MelonPrimeLanguageRegistry.h"

#include <QDebug>
#include <QHash>
#include <QString>

#include <initializer_list>
#include <iterator>

namespace MelonPrime::UiText
{

namespace {

struct TranslationValue
{
    MenuLangId lang;
    const char* text;
};

struct Translation
{
    const char* en;
    std::initializer_list<TranslationValue> values;
};

struct ObjectTextTranslation
{
    const char* objectName;
    std::initializer_list<TranslationValue> values;
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
    auto findValue = [&](MenuLangId wanted) -> const char*
    {
        for (const TranslationValue& value : entry.values)
        {
            if (value.lang == wanted)
                return value.text;
        }
        return nullptr;
    };

    if (lang == MenuLangId::English)
        return entry.en;

    if (const char* exact = findValue(lang))
        return exact;

    const MenuLangId baseLang = ResolveTranslationLanguage(lang);
    if (baseLang != lang && baseLang != MenuLangId::English)
    {
        if (const char* base = findValue(baseLang))
            return base;
    }

    return entry.en;
}

const char* ObjectTranslationFieldForLang(const ObjectTextTranslation& entry, MenuLangId lang)
{
    auto findValue = [&](MenuLangId wanted) -> const char*
    {
        for (const TranslationValue& value : entry.values)
        {
            if (value.lang == wanted)
                return value.text;
        }
        return nullptr;
    };

    if (lang == MenuLangId::English)
        return nullptr;

    if (const char* exact = findValue(lang))
        return exact;

    const MenuLangId baseLang = ResolveTranslationLanguage(lang);
    if (baseLang != lang && baseLang != MenuLangId::English)
    {
        if (const char* base = findValue(baseLang))
            return base;
    }

    return nullptr;
}

#include "inc/MelonPrimeTranslations.inc"

#include "inc/MelonPrimeObjectTranslations.inc"

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
