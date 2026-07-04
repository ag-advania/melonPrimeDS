#include "MelonPrimeTranslationDynamic.h"

#include "MelonPrimeLanguageRegistry.h"

#include <QString>
#include <QStringList>

namespace MelonPrime::UiText
{

namespace {

QString TrExact(const QString& text)
{
    const QString exact = TranslateExact(text);
    return exact != text ? exact : QString();
}

QString TrDecorated(const QString& text)
{
    const QStringList prefixes = {
        QStringLiteral("\u25B6 "),
        QStringLiteral("\u25BC "),
        QStringLiteral("\u2713 "),
        QStringLiteral("\u2717 "),
        QStringLiteral("\u21BA "),
        QStringLiteral("\u25A0 "),
    };
    for (const QString& prefix : prefixes)
    {
        if (text.startsWith(prefix))
            return prefix + Tr(text.mid(prefix.size()));
    }

    const QStringList suffixes = {
        QStringLiteral(" \u25B6"),
        QStringLiteral(" \u25BC"),
        QStringLiteral(" \u25BA"),
        QStringLiteral(" \u25C4"),
    };
    for (const QString& suffix : suffixes)
    {
        if (text.endsWith(suffix))
            return Tr(text.left(text.size() - suffix.size())) + suffix;
    }

    if (text.endsWith(QLatin1Char(':')))
    {
        const QString base = text.left(text.size() - 1);
        const QString translated = Tr(base);
        if (translated != base)
            return translated + QLatin1Char(':');
    }

    return QString();
}

QString TrInstanceDialogText(const QString& text)
{
    if (text.startsWith(QStringLiteral("Configuring settings for instance ")))
    {
        const QString arg = text.mid(35);
        switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
        case MenuLangId::German:
            return QStringLiteral("Einstellungen für Instanz %1").arg(arg);
        case MenuLangId::Spanish:
            return QStringLiteral("Configuración de la instancia %1").arg(arg);
        case MenuLangId::French:
            return QStringLiteral("Paramètres de l'instance %1").arg(arg);
        case MenuLangId::Italian:
            return QStringLiteral("Impostazioni per l'istanza %1").arg(arg);
        case MenuLangId::Dutch:
            return QStringLiteral("Instellingen voor instantie %1").arg(arg);
        case MenuLangId::Portuguese:
            return QStringLiteral("Configurações da instância %1").arg(arg);
        case MenuLangId::Russian:
            return QStringLiteral("Настройки экземпляра %1").arg(arg);
        case MenuLangId::ChineseSimplified:
            return QStringLiteral("实例 %1 的设置").arg(arg);
        case MenuLangId::Korean:
            return QStringLiteral("인스턴스 %1 설정").arg(arg);
        case MenuLangId::Japanese:
            return QStringLiteral("インスタンス %1 の設定").arg(arg);
        default:
            return text;
        }
    }

    if (text.startsWith(QStringLiteral("Configuring mappings for instance ")))
    {
        const QString arg = text.mid(35);
        switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
        case MenuLangId::German:
            return QStringLiteral("Belegungen für Instanz %1").arg(arg);
        case MenuLangId::Spanish:
            return QStringLiteral("Asignaciones de la instancia %1").arg(arg);
        case MenuLangId::French:
            return QStringLiteral("Assignations de l'instance %1").arg(arg);
        case MenuLangId::Italian:
            return QStringLiteral("Assegnazioni per l'istanza %1").arg(arg);
        case MenuLangId::Dutch:
            return QStringLiteral("Toewijzingen voor instantie %1").arg(arg);
        case MenuLangId::Portuguese:
            return QStringLiteral("Atribuições da instância %1").arg(arg);
        case MenuLangId::Russian:
            return QStringLiteral("Назначения экземпляра %1").arg(arg);
        case MenuLangId::ChineseSimplified:
            return QStringLiteral("实例 %1 的映射").arg(arg);
        case MenuLangId::Korean:
            return QStringLiteral("인스턴스 %1 매핑").arg(arg);
        case MenuLangId::Japanese:
            return QStringLiteral("インスタンス %1 の割り当て").arg(arg);
        default:
            return text;
        }
    }

    if (text.startsWith(QStringLiteral("Configuring paths for instance ")))
    {
        const QString arg = text.mid(32);
        switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
        case MenuLangId::German:
            return QStringLiteral("Pfade für Instanz %1").arg(arg);
        case MenuLangId::Spanish:
            return QStringLiteral("Rutas de la instancia %1").arg(arg);
        case MenuLangId::French:
            return QStringLiteral("Chemins de l'instance %1").arg(arg);
        case MenuLangId::Italian:
            return QStringLiteral("Percorsi per l'istanza %1").arg(arg);
        case MenuLangId::Dutch:
            return QStringLiteral("Paden voor instantie %1").arg(arg);
        case MenuLangId::Portuguese:
            return QStringLiteral("Caminhos da instância %1").arg(arg);
        case MenuLangId::Russian:
            return QStringLiteral("Пути экземпляра %1").arg(arg);
        case MenuLangId::ChineseSimplified:
            return QStringLiteral("实例 %1 的路径").arg(arg);
        case MenuLangId::Korean:
            return QStringLiteral("인스턴스 %1 경로").arg(arg);
        case MenuLangId::Japanese:
            return QStringLiteral("インスタンス %1 のパス").arg(arg);
        default:
            return text;
        }
    }

    if (text.startsWith(QStringLiteral("Setting battery levels for instance ")))
    {
        const QString arg = text.mid(36);
        switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
        case MenuLangId::German:
            return QStringLiteral("Akkustand für Instanz %1").arg(arg);
        case MenuLangId::Spanish:
            return QStringLiteral("Nivel de batería de la instancia %1").arg(arg);
        case MenuLangId::French:
            return QStringLiteral("Niveau de batterie de l'instance %1").arg(arg);
        case MenuLangId::Italian:
            return QStringLiteral("Livello batteria per l'istanza %1").arg(arg);
        case MenuLangId::Dutch:
            return QStringLiteral("Batterijniveau voor instantie %1").arg(arg);
        case MenuLangId::Portuguese:
            return QStringLiteral("Nível de bateria da instância %1").arg(arg);
        case MenuLangId::Russian:
            return QStringLiteral("Уровень заряда экземпляра %1").arg(arg);
        case MenuLangId::ChineseSimplified:
            return QStringLiteral("实例 %1 的电池电量").arg(arg);
        case MenuLangId::Korean:
            return QStringLiteral("인스턴스 %1 배터리 잔량").arg(arg);
        case MenuLangId::Japanese:
            return QStringLiteral("インスタンス %1 のバッテリー残量").arg(arg);
        default:
            return text;
        }
    }

    return QString();
}

QString TrSpecialDynamicText(const QString& text)
{
    if (text == QStringLiteral("(none)"))
    {
        // Chinese Traditional has its own wording; every other language,
        // including region variants without a dedicated case below (es-419,
        // fr-CA, pt-BR, en-GB, en-US), resolves through its fallback/base
        // language before defaulting to the English source text.
        if (ActiveMenuLanguage() == MenuLangId::ChineseTraditional)
            return QStringLiteral("（無）");

        switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
        case MenuLangId::German: return QStringLiteral("(keine)");
        case MenuLangId::Spanish: return QStringLiteral("(ninguno)");
        case MenuLangId::French: return QStringLiteral("(aucun)");
        case MenuLangId::Italian: return QStringLiteral("(nessuno)");
        case MenuLangId::Dutch: return QStringLiteral("(geen)");
        case MenuLangId::Portuguese: return QStringLiteral("(nenhum)");
        case MenuLangId::Russian: return QStringLiteral("(нет)");
        case MenuLangId::ChineseSimplified: return QStringLiteral("（无）");
        case MenuLangId::Korean: return QStringLiteral("(없음)");
        case MenuLangId::Japanese: return QStringLiteral("(なし)");
        default: return text;
        }
    }

    if (text.startsWith(QStringLiteral("Direct mode (requires "))
        && text.endsWith(QStringLiteral(" and ethernet connection)")))
    {
        const QString middle = text.mid(22, text.size() - 22 - 25);
        switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
        case MenuLangId::German:
            return QStringLiteral("Direktmodus (erfordert %1 und Ethernet-Verbindung)").arg(middle);
        case MenuLangId::Spanish:
            return QStringLiteral("Modo directo (requiere %1 y conexión Ethernet)").arg(middle);
        case MenuLangId::French:
            return QStringLiteral("Mode direct (nécessite %1 et une connexion Ethernet)").arg(middle);
        case MenuLangId::Italian:
            return QStringLiteral("Modalità diretta (richiede %1 e connessione Ethernet)").arg(middle);
        case MenuLangId::Dutch:
            return QStringLiteral("Directe modus (vereist %1 en Ethernet-verbinding)").arg(middle);
        case MenuLangId::Portuguese:
            return QStringLiteral("Modo direto (requer %1 e ligação Ethernet)").arg(middle);
        case MenuLangId::Russian:
            return QStringLiteral("Прямой режим (требуется %1 и Ethernet-подключение)").arg(middle);
        case MenuLangId::ChineseSimplified:
            return QStringLiteral("直连模式（需要 %1 和以太网连接）").arg(middle);
        case MenuLangId::Korean:
            return QStringLiteral("직접 모드(%1 및 이더넷 연결 필요)").arg(middle);
        case MenuLangId::Japanese:
            return QStringLiteral("ダイレクトモード (要 %1・イーサネット接続)").arg(middle);
        default:
            return text;
        }
    }

    const int nativeIdx = text.indexOf(QStringLiteral(" native ("));
    if (nativeIdx > 0)
    {
        switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
        case MenuLangId::German:
            return text.left(nativeIdx) + QStringLiteral(" nativ (") + text.mid(nativeIdx + 9);
        case MenuLangId::Spanish:
            return text.left(nativeIdx) + QStringLiteral(" nativo (") + text.mid(nativeIdx + 9);
        case MenuLangId::French:
            return text.left(nativeIdx) + QStringLiteral(" natif (") + text.mid(nativeIdx + 9);
        case MenuLangId::Italian:
            return text.left(nativeIdx) + QStringLiteral(" nativo (") + text.mid(nativeIdx + 9);
        case MenuLangId::Dutch:
            return text.left(nativeIdx) + QStringLiteral(" native (") + text.mid(nativeIdx + 9);
        case MenuLangId::Portuguese:
            return text.left(nativeIdx) + QStringLiteral(" nativo (") + text.mid(nativeIdx + 9);
        case MenuLangId::Russian:
            return text.left(nativeIdx) + QStringLiteral(" нативный (") + text.mid(nativeIdx + 9);
        case MenuLangId::ChineseSimplified:
            return text.left(nativeIdx) + QStringLiteral(" 原生 (") + text.mid(nativeIdx + 9);
        case MenuLangId::Korean:
            return text.left(nativeIdx) + QStringLiteral(" 네이티브 (") + text.mid(nativeIdx + 9);
        case MenuLangId::Japanese:
            return text.left(nativeIdx) + QStringLiteral(" ネイティブ (") + text.mid(nativeIdx + 9);
        default:
            return text;
        }
    }

    {
        QString cameraText = text;
        switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
        case MenuLangId::German:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (Innenkamera)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (Außenkamera)"));
            break;
        case MenuLangId::Spanish:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (cámara interior)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (cámara exterior)"));
            break;
        case MenuLangId::French:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (caméra interne)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (caméra externe)"));
            break;
        case MenuLangId::Italian:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (fotocamera interna)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (fotocamera esterna)"));
            break;
        case MenuLangId::Dutch:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (binnencamera)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (buitencamera)"));
            break;
        case MenuLangId::Portuguese:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (câmara interior)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (câmara exterior)"));
            break;
        case MenuLangId::Russian:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (внутренняя камера)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (внешняя камера)"));
            break;
        case MenuLangId::ChineseSimplified:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (内侧摄像头)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (外侧摄像头)"));
            break;
        case MenuLangId::Korean:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (내부 카메라)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (외부 카메라)"));
            break;
        case MenuLangId::Japanese:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (内側カメラ)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (外側カメラ)"));
            break;
        default:
            break;
        }
        if (cameraText != text)
            return cameraText;
    }

    return QString();
}

QString TrSlotAndScreenPrefixText(const QString& text)
{
    struct DynamicPrefix {
        const char* enPrefix;
        const char* sourceKey;
        bool colon;
    };
    const DynamicPrefix dynamicPrefixes[] = {
        {"DS slot: ", "DS slot", true},
        {"GBA slot: ", "GBA slot", true},
        {"Top ", "Top", false},
        {"Bottom ", "Bottom", false},
    };
    for (const DynamicPrefix& prefix : dynamicPrefixes)
    {
        const QString enPrefix = QString::fromUtf8(prefix.enPrefix);
        if (!text.startsWith(enPrefix))
            continue;

        const QString localizedKey = Tr(QString::fromUtf8(prefix.sourceKey));
        QString localizedPrefix;
        if (prefix.colon)
        {
            switch (ResolveTranslationLanguage(ActiveMenuLanguage())) {
            case MenuLangId::Japanese:
            case MenuLangId::ChineseSimplified:
            case MenuLangId::ChineseTraditional:
                localizedPrefix = localizedKey + QStringLiteral("：");
                break;
            default:
                localizedPrefix = localizedKey + QStringLiteral(": ");
                break;
            }
        }
        else
        {
            localizedPrefix = localizedKey + QLatin1Char(' ');
        }
        return localizedPrefix + Tr(text.mid(enPrefix.size()));
    }

    return QString();
}

QString TrDynamic(const QString& text)
{
    const QString instanceDialog = TrInstanceDialogText(text);
    if (!instanceDialog.isNull())
        return instanceDialog;

    const QString special = TrSpecialDynamicText(text);
    if (!special.isNull())
        return special;

    const QString slotAndScreenPrefix = TrSlotAndScreenPrefixText(text);
    if (!slotAndScreenPrefix.isNull())
        return slotAndScreenPrefix;

    return QString();
}

} // namespace

QString Tr(const QString& text)
{
    if (!IsMenuTranslationActive() || text.isEmpty())
        return text;

    const QString exact = TrExact(text);
    if (!exact.isNull())
        return exact;

    const QString decorated = TrDecorated(text);
    if (!decorated.isNull())
        return decorated;

    const QString dynamic = TrDynamic(text);
    if (!dynamic.isNull())
        return dynamic;

    return text;
}

QString Tr(const char* text)
{
    return Tr(QString::fromUtf8(text));
}

QString Tr(const char* text, int size)
{
    return Tr(QString::fromUtf8(text, size));
}

QStringList TrList(const QStringList& items)
{
    if (!IsMenuTranslationActive())
        return items;

    QStringList translated;
    translated.reserve(items.size());
    for (const QString& item : items)
        translated.append(Tr(item));
    return translated;
}

} // namespace MelonPrime::UiText
