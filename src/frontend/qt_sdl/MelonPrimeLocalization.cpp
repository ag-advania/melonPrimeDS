#include "MelonPrimeLocalization.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QFontComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QItemSelectionModel>
#include <QDialogButtonBox>
#include <QEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <QVariant>

#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTextOption>

#include <utility>

#include <cstring>
#include <algorithm>

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

} // namespace

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

constexpr Translation kTranslations[] = {
    // Common controls
    {"ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON", "ON"},
    {"OFF", "OFF", "OFF", "APAGADO", "ARRÊT", "OFF", "UIT", "DESLIGADO", "ВЫКЛ.", "关闭", "OFF", "إيقاف", "MATI", "ВИМК.", "ΑΠΕΝ.", "AV", "ปิด", "VYP.", "FRA", "KAPALI", "AV", "KI", "POIS", "TẮT", "WYŁ.", "OPRIT"},
    {"Off", "オフ", "Aus", "Off", "Off", "Off", "Off", "Off", "Выкл.", "关闭", "꺼짐", "إيقاف", "Mati", "Вимк.", "Ανεν.", "Av", "ปิด", "Vyp.", "Fra", "Kapalı", "Av", "Ki", "Pois", "Tắt", "Wył.", "Opr."},
    {"Save", "保存", "Speichern", "Guardar", "Enregistrer", "Salva", "Opslaan", "Guardar", "Сохранить", "保存", "저장", "حفظ", "Simpan", "Зберегти", "Αποθήκευση", "Spara", "บันทึก", "Uložit", "Gem", "Kaydet", "Lagre", "Mentés", "Tallenna", "Lưu", "Zapisz", "Salvare"},
    {"Cancel", "キャンセル", "Abbrechen", "Cancelar", "Annuler", "Annulla", "Annuleren", "Cancelar", "Отмена", "取消", "취소", "إلغاء", "Batal", "Скасувати", "Ακύρωση", "Avbryt", "ยกเลิก", "Zrušit", "Annuller", "İptal", "Avbryt", "Mégse", "Peruuta", "Hủy", "Anuluj", "Anulare"},
    {"OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK", "OK"},
    {"Reset", "リセット", "Zurücksetzen", "Restablecer", "Réinitialiser", "Reimposta", "Resetten", "Redefinir", "Сброс", "重置", "초기화", "إعادة تعيين", "Reset", "Скинути", "Επαναφορά", "Återställ", "รีเซ็ต", "Obnovit", "Nulstil", "Sıfırla", "Tilbakestill", "Visszaállítás", "Palauta", "Đặt lại", "Resetuj", "Resetează"},
    {"Generate", "生成", "Generieren", "Generar", "Générer", "Genera", "Genereren", "Gerar", "Создать", "生成", "생성", "إنشاء", "Buat", "Створити", "Δημιουργία", "Generera", "สร้าง", "Generovat", "Generer", "Oluştur", "Generer", "Generálás", "Luo", "Tạo", "Generuj", "Generează"},
    {"Copy Output", "出力をコピー", "Ausgabe kopieren", "Copiar salida", "Copier la sortie", "Copia output", "Uitvoer kopiëren", "Copiar saída", "Копировать вывод", "复制输出", "출력 복사", "نسخ المخرجات", "Salin Output", "Копіювати вивід", "Αντιγραφή εξόδου", "Kopiera utdata", "คัดลอกผลลัพธ์", "Kopírovat výstup", "Kopiér output", "Çıktıyı kopyala", "Kopier utdata", "Kimenet másolása", "Kopioi tuloste", "Sao chép đầu ra", "Kopiuj dane wyjściowe", "Copiază ieșirea"},
    {"Copy Output to Input", "出力を入力へコピー", "Ausgabe in Eingabe kopieren", "Copiar salida a entrada", "Copier la sortie vers l'entrée", "Copia uscita in ingresso", "Uitvoer naar invoer kopiëren", "Copiar saída para entrada", "Копировать выход во вход", "将输出复制到输入", "출력을 입력으로 복사", "نسخ الإخراج إلى الإدخال", "Salin output ke input", "Копіювати вихід у вхід", "Αντιγραφή εξόδου σε είσοδο", "Kopiera utdata till indata", "คัดลอกเอาต์พุตไปยังอินพุต", "Kopírovat výstup do vstupu", "Kopiér output til input", "Çıktıyı girişe kopyala", "Kopier utdata til inndata", "Kimenet másolása bemenetbe", "Kopioi tuloste syötteeseen", "Sao chép đầu ra sang đầu vào", "Kopiuj wyjście do wejścia", "Copiază ieșirea în intrare"},
    {"Apply", "適用", "Anwenden", "Aplicar", "Appliquer", "Applica", "Toepassen", "Aplicar", "Применить", "应用", "적용", "تطبيق", "Terapkan", "Застосувати", "Εφαρμογή", "Verkställ", "นำไปใช้", "Použít", "Anvend", "Uygula", "Bruk", "Alkalmaz", "Käytä", "Áp dụng", "Zastosuj", "Aplică"},
    {"Browse...", "参照...", "Durchsuchen...", "Examinar...", "Parcourir...", "Sfoglia...", "Bladeren...", "Procurar...", "Обзор...", "浏览...", "찾아보기...", "استعراض...", "Telusuri...", "Огляд...", "Περιήγηση...", "Bläddra...", "เรียกดู...", "Procházet...", "Gennemse...", "Gözat...", "Bla gjennom...", "Tallózás...", "Selaa...", "Duyệt...", "Przeglądaj...", "Răsfoiește..."},
    {"Select HUD Font", "HUDフォントを選択", "HUD-Schriftart auswählen", "Seleccionar fuente HUD", "Sélectionner la police HUD", "Seleziona carattere HUD", "HUD-lettertype selecteren", "Selecionar fonte HUD", "Выбрать шрифт HUD", "选择 HUD 字体", "HUD 글꼴 선택", "اختيار خط HUD", "Pilih Font HUD", "Вибрати шрифт HUD", "Επιλογή γραμματοσειράς HUD", "Välj HUD-teckensnitt", "เลือกฟอนต์ HUD", "Vybrat písmo HUD", "Vælg HUD-skrifttype", "HUD Yazı Tipi Seç", "Velg HUD-skrift", "HUD betűtípus kiválasztása", "Valitse HUD-fontti", "Chọn phông HUD", "Wybierz czcionkę HUD", "Selectează font HUD"},
    {"Select HUD Font File", "HUDフォントファイルを選択", "HUD-Schriftdatei auswählen", "Seleccionar archivo de fuente HUD", "Sélectionner le fichier de police HUD", "Seleziona file font HUD", "HUD-lettertypebestand selecteren", "Selecionar arquivo de fonte HUD", "Выбрать файл шрифта HUD", "选择 HUD 字体文件", "HUD 글꼴 파일 선택", "اختيار ملف خط HUD", "Pilih File Font HUD", "Вибрати файл шрифту HUD", "Επιλογή αρχείου γραμματοσειράς HUD", "Välj HUD-typsnittsfil", "เลือกไฟล์ฟอนต์ HUD", "Vybrat soubor písma HUD", "Vælg HUD-skriftfil", "HUD yazı tipi dosyasını seç", "Velg HUD-skriftfil", "HUD betűfájl kiválasztása", "Valitse HUD-fonttitiedosto", "Chọn tệp phông HUD", "Wybierz plik czcionki HUD", "Selectează fișier font HUD"},
    {"Pick Color", "色を選択", "Farbe wählen", "Elegir color", "Choisir une couleur", "Scegli colore", "Kleur kiezen", "Escolher cor", "Выбрать цвет", "选取颜色", "색상 선택", "اختيار لون", "Pilih warna", "Вибрати колір", "Επιλογή χρώματος", "Välj färg", "เลือกสี", "Vybrat barvu", "Vælg farve", "Renk seç", "Velg farge", "Szín választása", "Valitse väri", "Chọn màu", "Wybierz kolor", "Alege culoarea"},
    {"Pick a system font…", "システムフォントを選択…", "Systemschriftart wählen…", "Elegir una fuente del sistema…", "Choisir une police système…", "Scegli un font di sistema…", "Systeemlettertype kiezen…", "Escolher uma fonte do sistema…", "Выбрать системный шрифт…", "选择系统字体…", "시스템 글꼴 선택…", "اختر خط نظام…", "Pilih font sistem…", "Вибрати системний шрифт…", "Επιλογή γραμματοσειράς συστήματος…", "Välj ett systemtypsnitt…", "เลือกฟอนต์ระบบ…", "Vybrat systémové písmo…", "Vælg et systemskrifttype…", "Bir sistem yazı tipi seç…", "Velg et systemskrifttype…", "Rendszerbetűtípus kiválasztása…", "Valitse järjestelmän fontti…", "Chọn phông hệ thống…", "Wybierz czcionkę systemową…", "Alege un font de sistem…"},
    {"Crosshair Color", "照準の色", "Fadenkreuzfarbe", "Color del punto de mira", "Couleur du réticule", "Colore mirino", "Kleur van richtkruis", "Cor da mira", "Цвет прицела", "准星颜色", "조준선 색상", "لون التقاطع", "Warna bidik", "Колір прицілу", "Χρώμα στόχου", "Siktfärg", "สีเล็ง", "Barva zaměřovače", "Sigtekrydsfarve", "Nişangah rengi", "Trådkorsfarge", "Irányzék színe", "Tähtäimen väri", "Màu tâm ngắm", "Kolor celownika", "Culoare țintă"},
    {"Edit", "編集", "Bearbeiten", "Editar", "Modifier", "Modifica", "Bewerken", "Editar", "Изменить", "编辑", "편집", "تحرير", "Edit", "Редагувати", "Επεξεργασία", "Redigera", "แก้ไข", "Upravit", "Rediger", "Düzenle", "Rediger", "Szerkesztés", "Muokkaa", "Chỉnh sửa", "Edytuj", "Editează"},
    {"Chng", "変更", "Änd.", "Camb.", "Mod.", "Mod.", "Wijz.", "Alt.", "Изм.", "更改", "변경", "تغ.", "Ubh.", "Зм.", "Αλλ.", "Änd.", "เปล.", "Zm.", "Ænd.", "Değ.", "End.", "Módos.", "Muut.", "Đổi", "Zm.", "Mod."},
    {"OVR", "全体", "Ges.", "Gral.", "Gén.", "Gen.", "Alg.", "Geral", "Общ.", "总体", "전체", "إجمالي", "Umum", "Заг.", "Γεν.", "Allm.", "รวม", "Celk.", "Samlet", "Gen.", "Tot.", "Össz.", "Kok.", "Tổng", "Ogół.", "Gen."},
    {"Preview", "プレビュー", "Vorschau", "Vista previa", "Aperçu", "Anteprima", "Voorbeeld", "Pré-visualização", "Предпросмотр", "预览", "미리보기", "معاينة", "Pratinjau", "Попередній перегляд", "Προεπισκόπηση", "Förhandsgranska", "ดูตัวอย่าง", "Náhled", "Forhåndsvisning", "Önizleme", "Forhåndsvisning", "Előnézet", "Esikatselu", "Xem trước", "Podgląd", "Previzualizare"},
    {"Preview ON", "プレビューON", "Vorschau ON", "Vista previa ON", "Aperçu ON", "Anteprima ON", "Voorbeeld ON", "Pré-visualização ON", "Предпросмотр ON", "预览 ON", "미리보기 ON", "معاينة ON", "Pratinjau ON", "Попередній перегляд ON", "Προεπισκόπηση ON", "Förhandsvisning ON", "ตัวอย่าง ON", "Náhled ON", "Forhåndsvisning ON", "Önizleme ON", "Forhåndsvisning ON", "Előnézet ON", "Esikatselu ON", "Xem trước ON", "Podgląd ON", "Previzualizare ON"},
    {"preview", "プレビュー", "Vorschau", "vista previa", "aperçu", "anteprima", "voorbeeld", "visualização", "предпросмотр", "预览", "미리보기", "معاينة", "pratinjau", "попередній перегляд", "προεπισκόπηση", "förhandsgranskning", "ตัวอย่าง", "náhled", "forhåndsvisning", "önizleme", "forhåndsvisning", "előnézet", "esikatselu", "xem trước", "podgląd", "previzualizare"},
    {"Normal", "標準", "Standard", "Estándar", "Standard", "Standard", "Standaard", "Padrão", "Стандартный", "标准", "표준", "عادي", "Normal", "Стандартний", "Κανονικό", "Normal", "ปกติ", "Normální", "Normal", "Normal", "Normal", "Normál", "Normaali", "Bình thường", "Normalny", "Normal"},
    {"Zoomed (Scope)", "ズーム時 (スコープ)", "Gezoomt (Visier)", "Con zoom (mira)", "Zoom (viseur)", "Zoom (mirino)", "Ingezoomd (vizier)", "Com zoom (mira)", "С увеличением (прицел)", "缩放时（准星）", "확대 시 (스코프)", "مكبّر (منظار)", "Diperbesar (Scope)", "Збільшено (приціл)", "Με ζουμ (σκόπευτήρας)", "Inzoomad (sikte)", "ซูม (สโคป)", "Přiblíženo (zaměřovač)", "Zoom (sigte)", "Yakınlaştırılmış (nişangah)", "Innzoomet (sikte)", "Nagyítva (távcső)", "Zoomattu (tähtäin)", "Phóng to (Scope)", "Powiększenie (celownik)", "Zoom (lunetă)"},
    {"Scope reticle off", "スコープレティクル無効", "Visierpunkt deaktiviert", "Retícula del visor desactivada", "Réticule du viseur désactivé", "Reticolo mirino disattivato", "Richtmiddelpunt uit", "Retículo da mira desativado", "Прицел отключён", "瞄准镜十字线关闭", "스코프 조준선 끄기", "إيقاف شبكة التصويب", "Retikel scope mati", "Приціл вимкнено", "Σταυρονόμιο σκοπευτικού ανενεργό", "Sikte av", "ปิดเรติicle ของกล้อง", "Zaměřovač vypnut", "Sigtekorn fra", "Nişangah kapalı", "Sikte av", "Irányzék kikapcsolva", "Tähtäin pois", "Tắt reticle scope", "Celownik wyłączony", "Reticul dezactivat"},
    {"Text", "文字", "Text", "Texto", "Texte", "Testo", "Tekst", "Texto", "Текст", "文字", "텍스트", "نص", "Teks", "Текст", "Κείμενο", "Text", "ข้อความ", "Text", "Tekst", "Metin", "Tekst", "Szöveg", "Teksti", "Văn bản", "Tekst", "Text"},
    {"Auto", "自動", "Automatisch", "Automático", "Auto", "Auto", "Auto", "Auto", "Авто", "自动", "자동", "تلقائي", "Auto", "Авто", "Αυτόματο", "Auto", "อัตโนมัติ", "Auto", "Auto", "Otomatik", "Auto", "Auto", "Auto", "Tự động", "Auto", "Auto"},
    {"Overall", "全体", "Gesamt", "General", "Global", "Generale", "Algemeen", "Geral", "Общий", "整体", "전체", "إجمالي", "Keseluruhan", "Загальний", "Συνολικό", "Övergripande", "โดยรวม", "Celkové", "Samlet", "Genel", "Samlet", "Összesített", "Kokonais", "Tổng thể", "Ogólny", "General"},
    {"Custom", "カスタム", "Benutzerdefiniert", "Personalizado", "Personnalisé", "Personalizzato", "Aangepast", "Personalizado", "Пользовательский", "自定义", "사용자 지정", "مخصص", "Kustom", "Користувацький", "Προσαρμοσμένο", "Anpassad", "กำหนดเอง", "Vlastní", "Brugerdefineret", "Özel", "Tilpasset", "Egyéni", "Mukautettu", "Tùy chỉnh", "Niestandardowy", "Personalizat"},
    {"Default (MPH)", "標準 (MPH)", "Standard (MPH)", "Predeterminado (MPH)", "Par défaut (MPH)", "Predefinito (MPH)", "Standaard (MPH)", "Padrão (MPH)", "По умолчанию (MPH)", "默认 (MPH)", "기본 (MPH)", "افتراضي (MPH)", "Default (MPH)", "За замовчуванням (MPH)", "Προεπιλογή (MPH)", "Standard (MPH)", "ค่าเริ่มต้น (MPH)", "Výchozí (MPH)", "Standard (MPH)", "Varsayılan (MPH)", "Standard (MPH)", "Alapértelmezett (MPH)", "Oletus (MPH)", "Mặc định (MPH)", "Domyślny (MPH)", "Implicit (MPH)"},
    {"System Font", "システムフォント", "Systemschriftart", "Fuente del sistema", "Police système", "Carattere di sistema", "Systeemlettertype", "Fonte do sistema", "Системный шрифт", "系统字体", "시스템 글꼴", "خط النظام", "Font sistem", "Системний шрифт", "Γραμματοσειρά συστήματος", "Systemtypsnitt", "แบบอักษรระบบ", "Systémové písmo", "Systemschrift", "Sistem yazı tipi", "Systemskrift", "Rendszerbetűtípus", "Järjestelmäfontti", "Phông hệ thống", "Czcionka systemowa", "Font de sistem"},
    {"Font File", "フォントファイル", "Schriftdatei", "Archivo de fuente", "Fichier de police", "File del carattere", "Lettertypebestand", "Arquivo de fonte", "Файл шрифта", "字体文件", "글꼴 파일", "ملف الخط", "File Font", "Файл шрифту", "Αρχείο γραμματοσειράς", "Teckensnittsfil", "ไฟล์ฟอนต์", "Soubor písma", "Skrifttypefil", "Yazı Tipi Dosyası", "Skriftfil", "Betűfájl", "Fonttitiedosto", "Tệp phông", "Plik czcionki", "Fișier font"},
    {"Font files (*.ttf *.otf *.ttc);;All files (*)", "フォントファイル (*.ttf *.otf *.ttc);;すべてのファイル (*)", "Schriftdateien (*.ttf *.otf *.ttc);;Alle Dateien (*)", "Archivos de fuente (*.ttf *.otf *.ttc);;Todos los archivos (*)", "Fichiers de polices (*.ttf *.otf *.ttc);;Tous les fichiers (*)", "File di font (*.ttf *.otf *.ttc);;Tutti i file (*)", "Lettertypebestanden (*.ttf *.otf *.ttc);;Alle bestanden (*)", "Arquivos de fonte (*.ttf *.otf *.ttc);;Todos os arquivos (*)", "Файлы шрифтов (*.ttf *.otf *.ttc);;Все файлы (*)", "字体文件 (*.ttf *.otf *.ttc);;所有文件 (*)", "글꼴 파일 (*.ttf *.otf *.ttc);;모든 파일 (*)", "ملفات الخطوط (*.ttf *.otf *.ttc);;All files (*)", "File font (*.ttf *.otf *.ttc);;Semua file (*)", "Файли шрифтів (*.ttf *.otf *.ttc);;Усі файли (*)", "Αρχεία γραμματοσειράς (*.ttf *.otf *.ttc);;Όλα τα αρχεία (*)", "Typsnittsfiler (*.ttf *.otf *.ttc);;Alla filer (*)", "ไฟล์ฟอนต์ (*.ttf *.otf *.ttc);;ไฟล์ทั้งหมด (*)", "Soubory písem (*.ttf *.otf *.ttc);;Všechny soubory (*)", "Skrifttypefiler (*.ttf *.otf *.ttc);;Alle filer (*)", "Yazı tipi dosyaları (*.ttf *.otf *.ttc);;Tüm dosyalar (*)", "Skriftfiler (*.ttf *.otf *.ttc);;Alle filer (*)", "Betűfájlok (*.ttf *.otf *.ttc);;Minden fájl (*)", "Fonttitiedostot (*.ttf *.otf *.ttc);;Kaikki tiedostot (*)", "Tệp phông (*.ttf *.otf *.ttc);;Tất cả tệp (*)", "Pliki czcionek (*.ttf *.otf *.ttc);;Wszystkie pliki (*)", "Fișiere font (*.ttf *.otf *.ttc);;Toate fișierele (*)"},
    {"Menu Language", "メニュー言語", "Menüsprache", "Idioma del menú", "Langue du menu", "Lingua del menu", "Menutaal", "Idioma do menu", "Язык меню", "菜单语言", "메뉴 언어", "لغة القائمة", "Bahasa menu", "Мова меню", "Γλώσσα μενού", "Menyspråk", "ภาษาเมนู", "Jazyk menu", "Menusprog", "Menü dili", "Menyspråk", "Menü nyelve", "Valikon kieli", "Ngôn ngữ menu", "Język menu", "Limba meniului"},
    {"Language", "言語", "Sprache", "Idioma", "Langue", "Lingua", "Taal", "Idioma", "Язык", "语言", "언어", "اللغة", "Bahasa", "Мова", "Γλώσσα", "Språk", "ภาษา", "Jazyk", "Sprog", "Dil", "Språk", "Nyelv", "Kieli", "Ngôn ngữ", "Język", "Limbă"},
    {"System default (%1)", "システム既定 (%1)", "Systemstandard (%1)", "Predeterminado del sistema (%1)", "Par défaut du système (%1)", "Predefinito di sistema (%1)", "Systeemstandaard (%1)", "Predefinição do sistema (%1)", "По умолчанию (%1)", "系统默认 (%1)", "시스템 기본 (%1)", "الافتراضي للنظام (%1)", "Default sistem (%1)", "За замовчуванням (%1)", "Προεπιλογή συστήματος (%1)", "Systemstandard (%1)", "ค่าเริ่มต้นของระบบ (%1)", "Výchozí systémové (%1)", "Systemstandard (%1)", "Sistem varsayılanı (%1)", "Systemstandard (%1)", "Rendszer alapértelmezett (%1)", "Järjestelmän oletus (%1)", "Mặc định hệ thống (%1)", "Domyślne systemowe (%1)", "Implicit sistem (%1)"},
    {"Japanese", "日本語", "Japanisch", "Japonés", "Japonais", "Giapponese", "Japans", "Japonês", "Японский", "日语", "일본어", "اليابانية", "Jepang", "Японська", "Ιαπωνικά", "Japanska", "ภาษาญี่ปุ่น", "Japonština", "Japansk", "Japonca", "Japansk", "Japán", "Japani", "Tiếng Nhật", "Japoński", "Japoneză"},
    {"English", "英語", "Englisch", "Inglés", "Anglais", "Inglese", "Engels", "Inglês", "Английский", "英语", "영어", "الإنجليزية", "Inggris", "Англійська", "Αγγλικά", "Engelska", "อังกฤษ", "Angličtina", "Engelsk", "İngilizce", "Engelsk", "Angol", "Englanti", "Tiếng Anh", "Angielski", "Engleză"},
    {"German", "ドイツ語", "Deutsch", "Alemán", "Allemand", "Tedesco", "Duits", "Alemão", "Немецкий", "德语", "독일어", "الألمانية", "Jerman", "Німецька", "Γερμανικά", "Tyska", "เยอรมัน", "Němčina", "Tysk", "Almanca", "Tysk", "Német", "Saksa", "Tiếng Đức", "Niemiecki", "Germană"},
    {"Spanish", "スペイン語", "Spanisch", "Español", "Espagnol", "Spagnolo", "Spaans", "Espanhol", "Испанский", "西班牙语", "스페인어", "الإسبانية", "Spanyol", "Іспанська", "Ισπανικά", "Spanska", "สเปน", "Španělština", "Spansk", "İspanyolca", "Spansk", "Spanyol", "Espanja", "Tiếng Tây Ban Nha", "Hiszpański", "Spaniolă"},
    {"French", "フランス語", "Französisch", "Francés", "Français", "Francese", "Frans", "Francês", "Французский", "法语", "프랑스어", "Français", "Prancis", "Французька", "Γαλλικά", "Franska", "ฝรั่งเศส", "Francouzština", "Fransk", "Fransızca", "Fransk", "Francia", "Ranska", "Pháp", "Francuski", "Franceză"},
    {"Italian", "イタリア語", "Italienisch", "Italiano", "Italien", "Italiano", "Italiaans", "Italiano", "Итальянский", "意大利语", "이탈리아어", "الإيطالية", "Italia", "Італійська", "Ιταλικά", "Italienska", "ภาษาอิตาลี", "Italština", "Italiensk", "İtalyanca", "Italiensk", "Olasz", "Italia", "Tiếng Ý", "Włoski", "Italiană"},
    {"Dutch", "オランダ語", "Niederländisch", "Neerlandés", "Néerlandais", "Olandese", "Nederlands", "Neerlandês", "Нидерландский", "荷兰语", "네덜란드어", "الهولندية", "Belanda", "Нідерландська", "Ολλανδικά", "Nederländska", "ดัตช์", "Nizozemština", "Hollandsk", "Felemenkçe", "Nederlandsk", "Holland", "Hollanti", "Tiếng Hà Lan", "Niderlandzki", "Neerlandeză"},
    {"Portuguese", "ポルトガル語", "Portugiesisch", "Portugués", "Portugais", "Portoghese", "Portugees", "Português", "Португальский", "葡萄牙语", "포르투갈어", "البرتغالية", "Portugis", "Португальська", "Πορτογαλικά", "Portugisiska", "โปรตุเกส", "Portugalština", "Portugisisk", "Portekizce", "Portugisisk", "Portugál", "Portugali", "Tiếng Bồ Đào Nha", "Portugalski", "Portugheză"},
    {"Russian", "ロシア語", "Russisch", "Ruso", "Russe", "Russo", "Russisch", "Russo", "Русский", "俄语", "러시아어", "الروسية", "Rusia", "Російська", "Ρωσικά", "Ryska", "รัสเซีย", "Ruština", "Russisk", "Rusça", "Russisk", "Orosz", "Venäjä", "Tiếng Nga", "Rosyjski", "Rusă"},
    {"Chinese", "中国語", "Chinesisch", "Chino", "Chinois", "Cinese", "Chinees", "Chinês", "Китайский", "中文", "중국어", "الصينية", "Tionghoa", "Китайська", "Κινεζικά", "Kinesiska", "จีน", "Čínština", "Kinesisk", "Çince", "Kinesisk", "Kínai", "Kiina", "Tiếng Trung", "Chiński", "Chineză"},
    {"Korean", "韓国語", "Koreanisch", "Coreano", "Coréen", "Coreano", "Koreaans", "Coreano", "Корейский", "韩语", "한국어", "الكورية", "Korea", "Корейська", "Κορεατικά", "Koreanska", "เกาหลี", "Korejština", "Koreansk", "Korece", "Koreansk", "Koreai", "Korea", "Tiếng Hàn", "Koreański", "Coreeană"},
    {"Arabic", "アラビア語", "Arabisch", "Árabe", "Arabe", "Arabo", "Arabisch", "Árabe", "Арабский", "阿拉伯语", "아랍어", "العربية", "Arab", "Арабська", "Αραβικά", "Arabiska", "อาหรับ", "Arabština", "Arabisk", "Arapça", "Arabisk", "Arab", "Arabia", "Tiếng Ả Rập", "Arabski", "Arabă"},
    {"Indonesian", "インドネシア語", "Indonesisch", "Indonesio", "Indonésien", "Indonesiano", "Indonesisch", "Indonésio", "Индонезийский", "印度尼西亚语", "인도네시아어", "Indonesia", "Bahasa Indonesia", "Індонезійська", "Ινδονησιακά", "Indonesiska", "อินโดนีเซีย", "Indonéština", "Indonesisk", "Endonezce", "Indonesisk", "Indonéz", "Indonesia", "Tiếng Indonesia", "Indonezyjski", "Indoneziană"},
    {"Ukrainian", "ウクライナ語", "Ukrainisch", "Ucraniano", "Ukrainien", "Ucraino", "Oekraïens", "Ucraniano", "Украинский", "乌克兰语", "우크라이나어", "الأوكرانية", "Ukraina", "Українська", "Ουκρανικά", "Ukrainska", "ยูเครน", "Ukrajinština", "Ukrainsk", "Ukraynaca", "Ukrainsk", "Ukrán", "Ukraina", "Tiếng Ukraina", "Ukraiński", "Ucraineană"},
    {"Greek", "ギリシャ語", "Griechisch", "Griego", "Grec", "Greco", "Grieks", "Grego", "Греческий", "希腊语", "그리스어", "اليونانية", "Yunani", "Грецька", "Ελληνικά", "Grekiska", "กรีก", "Řečtina", "Græsk", "Yunanca", "Gresk", "Görög", "Kreikka", "Tiếng Hy Lạp", "Grecki", "Greacă"},
    {"Swedish", "スウェーデン語", "Schwedisch", "Sueco", "Suédois", "Svedese", "Zweeds", "Sueco", "Шведский", "瑞典语", "스웨덴어", "السويدية", "Swedia", "Шведська", "Σουηδικά", "Svenska", "สวีเดน", "Švédština", "Svensk", "İsveççe", "Svensk", "Svéd", "Ruotsi", "Tiếng Thụy Điển", "Szwedzki", "Suedeză"},
    {"Thai", "タイ語", "Thailändisch", "Tailandés", "Thaï", "Tailandese", "Thais", "Tailandês", "Тайский", "泰语", "태국어", "التايلاندية", "Thailand", "Тайська", "Ταϊλανδικά", "Thailändska", "ไทย", "Thajština", "Thai", "Tayca", "Thai", "Thai", "Thai", "Tiếng Thái", "Tajski", "Thailandeză"},
    {"Czech", "チェコ語", "Tschechisch", "Checo", "Tchèque", "Ceco", "Tsjechisch", "Checo", "Чешский", "捷克语", "체코어", "التشيكية", "Ceko", "Чеська", "Τσεχικά", "Tjeckiska", "เช็ก", "Čeština", "Tjekkisk", "Çekçe", "Tsjekkisk", "Cseh", "Tšekki", "Tiếng Séc", "Czeski", "Cehă"},
    {"Danish", "デンマーク語", "Dänisch", "Danés", "Danois", "Danese", "Deens", "Dinamarquês", "Датский", "丹麦语", "덴마크어", "الدنماركية", "Denmark", "Данська", "Δανικά", "Danska", "เดนมาร์ก", "Dánština", "Dansk", "Danca", "Dansk", "Dán", "Tanska", "Tiếng Đan Mạch", "Duński", "Daneză"},
    {"Turkish", "トルコ語", "Türkisch", "Turco", "Turc", "Turco", "Turks", "Turco", "Турецкий", "土耳其语", "터키어", "التركية", "Turki", "Турецька", "Τουρκικά", "Turkiska", "ตุรกี", "Turečtina", "Tyrkisk", "Türkçe", "Tyrkisk", "Török", "Turkki", "Tiếng Thổ Nhĩ Kỳ", "Turecki", "Turcă"},
    {"Norwegian", "ノルウェー語", "Norwegisch", "Noruego", "Norvégien", "Norvegese", "Noors", "Norueguês", "Норвежский", "挪威语", "노르웨이어", "النرويجية", "Norwegia", "Норвезька", "Νορβηγικά", "Norska", "นอร์เวย์", "Norština", "Norsk", "Norveççe", "Norsk", "Norvég", "Norja", "Tiếng Na Uy", "Norweski", "Norvegiană"},
    {"Hungarian", "ハンガリー語", "Ungarisch", "Húngaro", "Hongrois", "Ungherese", "Hongaars", "Húngaro", "Венгерский", "匈牙利语", "헝가리어", "المجرية", "Hungaria", "Угорська", "Ουγγρικά", "Ungerska", "ฮังการี", "Maďarština", "Ungarsk", "Macarca", "Ungarsk", "Magyar", "Unkari", "Tiếng Hungary", "Węgierski", "Maghiară"},
    {"Finnish", "フィンランド語", "Finnisch", "Finlandés", "Finnois", "Finlandese", "Fins", "Finlandês", "Финский", "芬兰语", "핀란드어", "الفنلندية", "Finlandia", "Фінська", "Φινλανδικά", "Finska", "ฟินแลนด์", "Finština", "Finsk", "Fince", "Finsk", "Finn", "Suomi", "Tiếng Phần Lan", "Fiński", "Finlandeză"},
    {"Vietnamese", "ベトナム語", "Vietnamesisch", "Vietnamita", "Vietnamien", "Vietnamita", "Vietnamees", "Vietnamita", "Вьетнамский", "越南语", "베트남어", "الفيتنامية", "Vietnam", "Вʼєтнамська", "Βιετναμικά", "Vietnamesiska", "เวียดนาม", "Vietnamština", "Vietnamesisk", "Vietnamca", "Vietnamesisk", "Vietnámi", "Vietnam", "Tiếng Việt", "Wietnamski", "Vietnameză"},
    {"Polish", "ポーランド語", "Polnisch", "Polaco", "Polonais", "Polacco", "Pools", "Polaco", "Польский", "波兰语", "폴란드어", "البولندية", "Polandia", "Польська", "Πολωνικά", "Polska", "โปแลนด์", "Polština", "Polsk", "Lehçe", "Polsk", "Lengyel", "Puola", "Tiếng Ba Lan", "Polski", "Poloneză"},
    {"Romanian", "ルーマニア語", "Rumänisch", "Rumano", "Roumain", "Rumeno", "Roemeens", "Romeno", "Румынский", "罗马尼亚语", "루마니아어", "الرومانية", "Rumania", "Румунська", "Ρουμανικά", "Rumänska", "โรมาเนีย", "Rumunština", "Rumænsk", "Romence", "Rumensk", "Román", "Romania", "Tiếng Romania", "Rumuński", "Română"},

    // Main menu bar
    {"File", "ファイル", "Datei", "Archivo", "Fichier", "File", "Bestand", "Arquivo", "Файл", "文件", "파일", "ملف", "File", "Файл", "Αρχείο", "Fil", "ไฟล์", "Soubor", "Fil", "Dosya", "Fil", "Fájl", "Tiedosto", "Tệp", "Plik", "Fișier"},
    {"Open ROM...", "ROMを開く...", "ROM öffnen...", "Abrir ROM...", "Ouvrir une ROM...", "Apri ROM...", "ROM openen...", "Abrir ROM...", "Открыть ROM...", "打开 ROM...", "ROM 열기...", "فتح ROM...", "Buka ROM...", "Відкрити ROM...", "Άνοιγμα ROM...", "Öppna ROM...", "เปิด ROM...", "Otevřít ROM...", "Åbn ROM...", "ROM aç...", "Åpne ROM...", "ROM megnyitása...", "Avaa ROM...", "Mở ROM...", "Otwórz ROM...", "Deschide ROM..."},
    {"File->Open ROM...", "ファイル → ROMを開く...", "Datei → ROM öffnen...", "Archivo → Abrir ROM...", "Fichier → Ouvrir une ROM...", "File → Apri ROM...", "Bestand → ROM openen...", "Arquivo → Abrir ROM...", "Файл → Открыть ROM...", "文件 → 打开 ROM...", "파일 → ROM 열기...", "ملف → فتح ROM...", "File → Buka ROM...", "Файл → Відкрити ROM...", "Αρχείο → Άνοιγμα ROM...", "Arkiv → Öppna ROM...", "ไฟล์ → เปิด ROM...", "Soubor → Otevřít ROM...", "Fil → Åbn ROM...", "Dosya → ROM aç...", "Fil → Åpne ROM...", "Fájl → ROM megnyitása...", "Tiedosto → Avaa ROM...", "Tệp → Mở ROM...", "Plik → Otwórz ROM...", "Fișier → Deschide ROM..."},
    {"to get started", "で始めよう", "um loszulegen", "para empezar", "pour commencer", "per iniziare", "om te beginnen", "para começar", "чтобы начать", "开始使用", "시작하기", "للبدء", "untuk memulai", "щоб почати", "για να ξεκινήσετε", "för att komma igång", "เพื่อเริ่มต้น", "pro začátek", "for at komme i gang", "başlamak için", "for å komme i gang", "a kezdéshez", "aloittaaksesi", "để bắt đầu", "aby rozpocząć", "pentru a începe"},
    {"Open recent", "最近使ったROM", "Zuletzt geöffnet", "Abrir reciente", "Ouvrir récent", "Apri recente", "Recent openen", "Abrir recente", "Открыть недавнее", "打开最近", "최근 열기", "فتح الأخيرة", "Buka terbaru", "Відкрити нещодавнє", "Άνοιγμα πρόσφατου", "Öppna senaste", "เปิดล่าสุด", "Otevřít nedávné", "Åbn seneste", "Son kullanılanı aç", "Åpne nylig", "Legutóbbi megnyitása", "Avaa viimeisin", "Mở gần đây", "Otwórz ostatnie", "Deschide recent"},
    {"Boot firmware", "ファームウェアを起動", "Firmware starten", "Iniciar firmware", "Démarrer le firmware", "Avvia firmware", "Firmware opstarten", "Iniciar firmware", "Загрузить прошивку", "启动固件", "펌웨어 부팅", "تشغيل البرنامج الثابت", "Boot firmware", "Завантажити прошивку", "Εκκίνηση firmware", "Starta firmware", "บูตเฟิร์มแวร์", "Spustit firmware", "Start firmware", "Firmware'ü başlat", "Start firmware", "Firmware indítása", "Käynnistä laiteohjelmisto", "Khởi động firmware", "Uruchom firmware", "Pornește firmware"},
    {"DS slot", "DSスロット", "DS-Steckplatz", "Ranura DS", "Emplacement DS", "Slot DS", "DS-sleuf", "Slot DS", "Слот DS", "DS 卡槽", "DS 슬롯", "فتحة DS", "Slot DS", "Слот DS", "Θύρα DS", "DS-plats", "ช่อง DS", "Slot DS", "DS-slot", "DS yuvası", "DS-spor", "DS foglalat", "DS-paikka", "Khe DS", "Gniazdo DS", "Slot DS"},
    {"GBA slot", "GBAスロット", "GBA-Steckplatz", "Ranura GBA", "Emplacement GBA", "Slot GBA", "GBA-slot", "Slot GBA", "Слот GBA", "GBA 插槽", "GBA 슬롯", "فتحة GBA", "Slot GBA", "Слот GBA", "Θύρα GBA", "GBA-plats", "ช่อง GBA", "Slot GBA", "GBA-slot", "GBA yuvası", "GBA-spor", "GBA foglalat", "GBA-paikka", "Khe GBA", "Gniazdo GBA", "Slot GBA"},
    {"Insert cart...", "カートリッジを挿入...", "Modul einlegen...", "Insertar cartucho...", "Insérer une cartouche...", "Inserisci cartuccia...", "Modul invoegen...", "Inserir cartucho...", "Вставить картридж...", "插入卡带...", "카트리지 삽입...", "أدخل الخرطوشة...", "Masukkan kartu...", "Вставте картридж...", "Εισαγωγή κασέτας...", "Sätt i kassett...", "ใส่การ์ท...", "Vložit kazetu...", "Indsæt kassette...", "Kartuş tak...", "Sett inn kassett...", "Kazetta behelyezése...", "Aseta kasetti...", "Lắp thẻ game...", "Włóż kartridż...", "Introdu cartușul..."},
    {"Eject cart", "カートリッジを取り出す", "Modul auswerfen", "Expulsar cartucho", "Éjecter la cartouche", "Espelli cartuccia", "Cartridge uitwerpen", "Ejetar cartucho", "Извлечь картридж", "弹出卡带", "카트리지 꺼내기", "إخراج الخرطوشة", "Keluarkan kartu", "Витягнути картридж", "Εξαγωγή κασέτας", "Mata ut kassett", "ถอดตลับ", "Vysunout kazetu", "Skub kassette ud", "Kartuşu çıkar", "Løs ut kassett", "Kazetta kiürítése", "Poista kasetti", "Rút thẻ game", "Wysuń kartridż", "Scoate cartușul"},
    {"Insert ROM cart...", "ROMカートリッジを挿入...", "ROM-Karte einlegen…", "Insertar cartucho ROM…", "Insérer cartouche ROM…", "Inserisci cartuccia ROM…", "ROM-cartridge invoegen…", "Inserir cartucho ROM…", "Вставить картридж ROM…", "插入 ROM 卡带…", "ROM 카트리지 삽입…", "إدراج خرطوشة ROM…", "Masukkan kartu ROM…", "Вставити картридж ROM…", "Εισαγωγή κασέτας ROM…", "Sätt i ROM-kassett…", "ใส่ตลับ ROM…", "Vložit ROM kartu…", "Indsæt ROM-kassette…", "ROM kartuşu tak…", "Sett inn ROM-kassett…", "ROM kazetta behelyezése…", "Aseta ROM-kasetti…", "Gắn hộp ROM…", "Włóż kartridż ROM…", "Introdu cartuș ROM…"},
    {"Insert add-on cart", "アドオンカートリッジを挿入", "Add-on-Karte einlegen", "Insertar cartucho add-on", "Insérer la cartouche add-on", "Inserisci cartuccia add-on", "Add-on-cartridge invoeren", "Inserir cartucho add-on", "Вставить картридж add-on", "插入扩展卡带", "애드온 카트리지 삽입", "إدخال بطاقة add-on", "Masukkan kartu add-on", "Вставити картридж add-on", "Εισαγωγή κάρτας add-on", "Sätt i add-on-kort", "ใส่การ์ด add-on", "Vložit add-on kartu", "Indsæt add-on-kort", "Add-on kartuşu tak", "Sett inn add-on-kort", "Add-on kazetta behelyezése", "Aseta add-on-kortti", "Lắp thẻ add-on", "Włóż kartę add-on", "Introdu cartuș add-on"},
    {"Import savefile", "セーブファイルをインポート", "Speicherdatei importieren", "Importar partida guardada", "Importer la sauvegarde", "Importa salvataggio", "Savegame importeren", "Importar save", "Импортировать сохранение", "导入存档", "세이브 파일 가져오기", "استيراد ملف الحفظ", "Impor file save", "Імпортувати збереження", "Εισαγωγή αρχείου αποθήκευσης", "Importera sparfil", "นำเข้าไฟล์เซฟ", "Importovat uloženou hru", "Importer savefil", "Kayıt dosyası içe aktar", "Importer lagringsfil", "Mentés importálása", "Tuo tallennustiedosto", "Nhập file save", "Importuj zapis", "Importă fișier de salvare"},
    {"Save state", "ステートを保存", "Zustand speichern", "Guardar estado", "Sauvegarder l'état", "Salva stato", "Status opslaan", "Guardar estado", "Сохранить состояние", "保存状态", "상태 저장", "حفظ الحالة", "Simpan state", "Зберегти стан", "Αποθήκευση κατάστασης", "Spara tillstånd", "บันทึกสถานะ", "Uložit stav", "Gem tilstand", "Durumu kaydet", "Lagre tilstand", "Állapot mentése", "Tallenna tila", "Lưu trạng thái", "Zapisz stan", "Salvează starea"},
    {"Load state", "ステートを読み込み", "Stand laden", "Cargar estado", "Charger l'état", "Carica stato", "Status laden", "Carregar estado", "Загрузить состояние", "读取即时存档", "상태 불러오기", "تحميل الحالة", "Muat status", "Завантажити стан", "Φόρτωση κατάστασης", "Ladda tillstånd", "โหลดสถานะ", "Načíst stav", "Indlæs tilstand", "Durum yükle", "Last tilstand", "Állapot betöltése", "Lataa tila", "Tải trạng thái", "Wczytaj stan", "Încarcă starea"},
    {"File...", "ファイル...", "Datei…", "Archivo…", "Fichier…", "File…", "Bestand…", "Arquivo…", "Файл…", "文件…", "파일…", "ملف…", "File…", "Файл…", "Αρχείο…", "Fil…", "ไฟล์…", "Soubor…", "Fil…", "Dosya…", "Fil…", "Fájl…", "Tiedosto…", "Tệp…", "Plik…", "Fișier…"},
    {"Undo state load", "ステート読み込みを取り消す", "Zustandsladen rückgängig", "Deshacer carga de estado", "Annuler le chargement d'état", "Annulla caricamento stato", "Statusladen ongedaan maken", "Desfazer carregamento de estado", "Отменить загрузку состояния", "撤销状态加载", "상태 불러오기 취소", "تراجع عن تحميل الحالة", "Batalkan muat state", "Скасувати завантаження стану", "Αναίρεση φόρτωσης κατάστασης", "Ångra tillståndsladdning", "เลิกทำการโหลดสถานะ", "Vrátit načtení stavu", "Fortryd indlæsning af tilstand", "Durum yüklemesini geri al", "Angre tilstandslasting", "Állapot betöltésének visszavonása", "Kumoa tilan lataus", "Hoàn tác tải trạng thái", "Cofnij wczytanie stanu", "Anulează încărcarea stării"},
    {"Open melonDS directory", "melonDSフォルダを開く", "melonDS-Ordner öffnen", "Abrir carpeta de melonDS", "Ouvrir le dossier melonDS", "Apri cartella melonDS", "melonDS-map openen", "Abrir pasta do melonDS", "Открыть папку melonDS", "打开 melonDS 文件夹", "melonDS 폴더 열기", "فتح مجلد melonDS", "Buka direktori melonDS", "Відкрити папку melonDS", "Άνοιγμα φακέλου melonDS", "Öppna melonDS-mapp", "เปิดโฟลเดอร์ melonDS", "Otevřít složku melonDS", "Åbn melonDS-mappe", "melonDS klasörünü aç", "Åpne melonDS-mappe", "melonDS mappa megnyitása", "Avaa melonDS-kansio", "Mở thư mục melonDS", "Otwórz folder melonDS", "Deschide directorul melonDS"},
    {"Quit", "終了", "Beenden", "Salir", "Quitter", "Esci", "Afsluiten", "Sair", "Выйти", "退出", "종료", "خروج", "Keluar", "Вийти", "Έξοδος", "Avsluta", "ออก", "Ukončit", "Afslut", "Çık", "Avslutt", "Kilépés", "Lopeta", "Thoát", "Zakończ", "Ieșire"},
    {"System", "システム", "System", "Sistema", "Système", "Sistema", "Systeem", "Sistema", "Система", "系统", "시스템", "النظام", "Sistem", "Система", "Σύστημα", "System", "ระบบ", "Systém", "System", "Sistem", "System", "Rendszer", "Järjestelmä", "Hệ thống", "System", "Sistem"},
    {"Pause", "一時停止", "Pause", "Pausa", "Pause", "Pausa", "Pauze", "Pausa", "Пауза", "暂停", "일시정지", "إيقاف مؤقت", "Jeda", "Пауза", "Παύση", "Paus", "หยุดชั่วคราว", "Pauza", "Pause", "Duraklat", "Pause", "Szünet", "Tauko", "Tạm dừng", "Pauza", "Pauză"},
    {"Stop", "停止", "Stopp", "Detener", "Arrêter", "Stop", "Stop", "Parar", "Стоп", "停止", "정지", "إيقاف", "Berhenti", "Стоп", "Διακοπή", "Stopp", "หยุด", "Stop", "Stop", "Durdur", "Stopp", "Leállítás", "Pysäytä", "Dừng", "Stop", "Stop"},
    {"Frame step", "フレーム送り", "Frame-Schritt", "Avance de fotograma", "Avance image par image", "Avanzamento fotogramma", "Frame-stap", "Avanço de quadro", "Пошаговый кадр", "逐帧前进", "프레임 단위 진행", "تقدم إطار", "Langkah frame", "Крок кадру", "Βήμα καρέ", "Bildsteg", "ขั้นเฟรม", "Krok snímku", "Billedtrin", "Kare adımı", "Bildesteg", "Képkocka léptetés", "Ruudun askel", "Bước khung hình", "Krok klatki", "Pas cadru"},
    {"Power management", "電源管理", "Energieverwaltung", "Gestión de energía", "Gestion de l'alimentation", "Gestione alimentazione", "Energiebeheer", "Gestão de energia", "Управление питанием", "电源管理", "전원 관리", "إدارة الطاقة", "Manajemen daya", "Керування живленням", "Διαχείριση ενέργειας", "Strömhantering", "การจัดการพลังงาน", "Správa napájení", "Strømstyring", "Güç yönetimi", "Strømstyring", "Energiakezelés", "Virranhallinta", "Quản lý nguồn", "Zarządzanie energią", "Gestionare energie"},
    {"Date and time", "日付と時刻", "Datum und Uhrzeit", "Fecha y hora", "Date et heure", "Data e ora", "Datum en tijd", "Data e hora", "Дата и время", "日期和时间", "날짜 및 시간", "التاريخ والوقت", "Tanggal dan waktu", "Дата і час", "Ημερομηνία και ώρα", "Datum och tid", "วันที่และเวลา", "Datum a čas", "Dato og tid", "Tarih ve saat", "Dato og tid", "Dátum és idő", "Päivämäärä ja aika", "Ngày và giờ", "Data i godzina", "Dată și oră"},
    {"Enable cheats", "チートを有効化", "Cheats aktivieren", "Activar trucos", "Activer les triches", "Attiva cheat", "Cheats inschakelen", "Ativar trapaças", "Включить читы", "启用作弊码", "치트 활성화", "تفعيل الغش", "Aktifkan cheat", "Увімкнути чити", "Ενεργοποίηση cheat", "Aktivera fusk", "เปิดใช้ cheat", "Povolit cheaty", "Aktivér snydekoder", "Hileleri etkinleştir", "Aktiver juks", "Cheat engedélyezése", "Ota huijaukset käyttöön", "Bật cheat", "Włącz kody", "Activează cheat-uri"},
    {"Setup cheat codes", "チートコード設定", "Cheat-Codes einrichten", "Configurar códigos de trucos", "Configurer les codes triche", "Configura codici trucchi", "Cheatcodes instellen", "Configurar códigos de trapaça", "Настроить чит-коды", "设置作弊码", "치트 코드 설정", "إعداد أكواد الغش", "Atur kode cheat", "Налаштувати чит-коди", "Ρύθμιση κωδικών cheat", "Konfigurera fuskoder", "ตั้งค่ารหัส cheat", "Nastavit cheat kódy", "Konfigurer snydekoder", "Hile kodlarını ayarla", "Sett opp juksekoder", "Csalókódok beállítása", "Määritä huijauskoodit", "Thiết lập mã cheat", "Skonfiguruj kody oszustw", "Configurează coduri cheat"},
    {"ROM info", "ROM情報", "ROM-Info", "Info de ROM", "Infos ROM", "Info ROM", "ROM-info", "Info da ROM", "Сведения о ROM", "ROM 信息", "ROM 정보", "معلومات ROM", "Info ROM", "Інфо ROM", "Πληροφορίες ROM", "ROM-info", "ข้อมูล ROM", "Info ROM", "ROM-info", "ROM bilgisi", "ROM-info", "ROM infó", "ROM-tiedot", "Thông tin ROM", "Info ROM", "Info ROM"},
    {"RAM search", "RAM検索", "RAM-Suche", "Búsqueda RAM", "Recherche RAM", "Ricerca RAM", "RAM-zoeken", "Busca RAM", "Поиск RAM", "RAM 搜索", "RAM 검색", "بحث RAM", "Pencarian RAM", "Пошук RAM", "Αναζήτηση RAM", "RAM-sökning", "ค้นหา RAM", "Hledání RAM", "RAM-søgning", "RAM arama", "RAM-søk", "RAM-keresés", "RAM-haku", "Tìm kiếm RAM", "Wyszukiwanie RAM", "Căutare RAM"},
    {"Manage DSi titles", "DSiタイトル管理", "DSi-Titel verwalten", "Administrar títulos DSi", "Gérer les titres DSi", "Gestisci titoli DSi", "DSi-titels beheren", "Gerir títulos DSi", "Управление заголовками DSi", "管理 DSi 标题", "DSi 타이틀 관리", "إدارة عناوين DSi", "Kelola judul DSi", "Керувати заголовками DSi", "Διαχείριση τίτλων DSi", "Hantera DSi-titlar", "จัดการชื่อ DSi", "Spravovat tituly DSi", "Administrer DSi-titler", "DSi başlıklarını yönet", "Administrer DSi-titler", "DSi címek kezelése", "Hallitse DSi-otsikoita", "Quản lý tiêu đề DSi", "Zarządzaj tytułami DSi", "Gestionează titlurile DSi"},
    {"Multiplayer", "マルチプレイ", "Mehrspieler", "Multijugador", "Multijoueur", "Multigiocatore", "Multiplayer", "Multijogador", "Мультиплеер", "多人游戏", "멀티플레이", "متعدد اللاعبين", "Multipemain", "Багатокористувацький", "Πολλαπλών παικτών", "Flerspelarläge", "ผู้เล่นหลายคน", "Multiplayer", "Multiplayer", "Çok oyunculu", "Flerspiller", "Többjátékos", "Moninpeli", "Nhiều người chơi", "Multiplayer", "Multiplayer"},
    {"Launch new instance", "新しいインスタンスを起動", "Neue Instanz starten", "Iniciar nueva instancia", "Lancer une nouvelle instance", "Avvia nuova istanza", "Nieuwe instantie starten", "Iniciar nova instância", "Запустить новый экземпляр", "启动新实例", "새 인스턴스 실행", "تشغيل نسخة جديدة", "Luncurkan instance baru", "Запустити новий екземпляр", "Εκκίνηση νέας εμφάνισης", "Starta ny instans", "เปิดอินสแตนซ์ใหม่", "Spustit novou instanci", "Start ny instans", "Yeni örnek başlat", "Start ny instans", "Új példány indítása", "Käynnistä uusi instanssi", "Khởi chạy phiên bản mới", "Uruchom nową instancję", "Lansează instanță nouă"},
    {"Host LAN game", "LANゲームをホスト", "LAN-Spiel hosten", "Alojar partida LAN", "Héberger une partie LAN", "Ospita partita LAN", "LAN-spel hosten", "Hospedar jogo LAN", "Создать LAN-игру", "托管 LAN 游戏", "LAN 게임 호스트", "استضافة لعبة LAN", "Host game LAN", "Хостити LAN-гру", "Φιλοξενία παιχνιδιού LAN", "Värd LAN-spel", "โฮสต์เกม LAN", "Hostovat LAN hru", "Vært LAN-spil", "LAN oyunu barındır", "Vert LAN-spill", "LAN játék hosztolása", "Isännöi LAN-peliä", "Chủ trì game LAN", "Hostuj grę LAN", "Găzduiește joc LAN"},
    {"Join LAN game", "LANゲームに参加", "LAN-Spiel beitreten", "Unirse a partida LAN", "Rejoindre une partie LAN", "Unisciti a partita LAN", "Deelnemen aan LAN-spel", "Entrar em jogo LAN", "Присоединиться к LAN-игре", "加入 LAN 游戏", "LAN 게임 참가", "الانضمام إلى لعبة LAN", "Gabung permainan LAN", "Приєднатися до LAN-гри", "Συμμετοχή σε παιχνίδι LAN", "Gå med i LAN-spel", "เข้าร่วมเกม LAN", "Připojit se k LAN hře", "Deltag i LAN-spil", "LAN oyununa katıl", "Bli med i LAN-spill", "Csatlakozás LAN-játékhoz", "Liity LAN-peliin", "Tham gia game LAN", "Dołącz do gry LAN", "Alătură-te jocului LAN"},
    {"Warning: LAN requires low network latency to work.", "警告: LANは低レイテンシのネットワーク接続が必要です。", "Warnung: LAN erfordert eine latenzarme Netzwerkverbindung.", "Advertencia: LAN requiere una conexión de red de baja latencia.", "Avertissement : le LAN nécessite une connexion réseau à faible latence.", "Avviso: LAN richiede una connessione di rete a bassa latenza.", "Waarschuwing: LAN vereist een netwerkverbinding met lage latentie.", "Aviso: LAN requer uma ligação de rede de baixa latência.", "Предупреждение: для LAN требуется сеть с низкой задержкой.", "警告：LAN 需要低延迟网络连接。", "경고: LAN은 낮은 지연 시간의 네트워크 연결이 필요합니다.", "تحذير: يتطلب LAN اتصال شبكة بزمن انتقال منخفض.", "Peringatan: LAN memerlukan koneksi jaringan latensi rendah.", "Попередження: для LAN потрібне мережеве з'єднання з малою затримкою.", "Προειδοποίηση: το LAN απαιτεί σύνδεση δικτύου χαμηλής καθυστέρησης.", "Varning: LAN kräver en nätverksanslutning med låg latens.", "คำเตือน: LAN ต้องใช้การเชื่อมต่อเครือข่ายที่มีความหน่วงต่ำ", "Varování: LAN vyžaduje síťové připojení s nízkou latencí.", "Advarsel: LAN kræver en netværksforbindelse med lav latency.", "Uyarı: LAN düşük gecikmeli bir ağ bağlantısı gerektirir.", "Advarsel: LAN krever en nettverkstilkobling med lav latency.", "Figyelmeztetés: a LAN alacsony késleltetésű hálózati kapcsolatot igényel.", "Varoitus: LAN vaatii matalan viiveen verkkoyhteyden.", "Cảnh báo: LAN cần kết nối mạng có độ trễ thấp.", "Ostrzeżenie: LAN wymaga połączenia sieciowego o niskim opóźnieniu.", "Avertisment: LAN necesită o conexiune de rețea cu latență redusă."},
    {"Do not expect it to work through a VPN or any sort of tunnel.", "VPNやトンネル経由では動作しない可能性があります。", "Über VPN oder Tunnel funktioniert es möglicherweise nicht.", "Puede no funcionar a través de VPN o túneles.", "Peut ne pas fonctionner via VPN ou tunnel.", "Potrebbe non funzionare tramite VPN o tunnel.", "Werkt mogelijk niet via VPN of tunnels.", "Pode não funcionar através de VPN ou túneis.", "Может не работать через VPN или туннели.", "通过 VPN 或隧道可能无法正常工作。", "VPN 또는 터널을 통해서는 작동하지 않을 수 있습니다.", "لا تتوقع أن يعمل عبر VPN أو أي نوع من الأنفاق.", "Jangan harapkan ini berfungsi melalui VPN atau tunnel apa pun.", "Не очікуйте роботи через VPN або будь-який тунель.", "Μην περιμένετε να λειτουργήσει μέσω VPN ή οποιουδήποτε tunnel.", "Räkna inte med att det fungerar via VPN eller någon tunnel.", "อย่าคาดหวังว่าจะทำงานผ่าน VPN หรือ tunnel ใด ๆ", "Neočekávejte, že bude fungovat přes VPN nebo jakýkoli tunel.", "Forvent ikke, at det virker via VPN eller nogen tunnel.", "VPN veya herhangi bir tünel üzerinden çalışmasını beklemeyin.", "Ikke forvent at det fungerer via VPN eller noen tunnel.", "Ne számítson rá, hogy VPN-en vagy bármilyen alagúton keresztül működik.", "Älä odota sen toimivan VPN:n tai minkään tunnelin kautta.", "Đừng kỳ vọng nó hoạt động qua VPN hoặc bất kỳ đường hầm nào.", "Nie oczekuj działania przez VPN ani żaden tunel.", "Nu vă așteptați să funcționeze prin VPN sau orice fel de tunel."},
    {"View", "表示", "Ansicht", "Ver", "Affichage", "Visualizza", "Beeld", "Exibir", "Вид", "视图", "보기", "عرض", "Tampilan", "Вигляд", "Προβολή", "Visa", "มุมมอง", "Zobrazení", "Visning", "Görünüm", "Visning", "Nézet", "Näkymä", "Xem", "Widok", "Afișare"},
    {"Screen size", "画面サイズ", "Bildschirmgröße", "Tamaño de pantalla", "Taille de l'écran", "Dimensione schermo", "Schermgrootte", "Tamanho da tela", "Размер экрана", "画面大小", "화면 크기", "حجم الشاشة", "Ukuran layar", "Розмір екрана", "Μέγεθος οθόνης", "Skärmstorlek", "ขนาดหน้าจอ", "Velikost obrazovky", "Skærmstørrelse", "Ekran boyutu", "Skjermstørrelse", "Képernyőméret", "Näytön koko", "Kích thước màn hình", "Rozmiar ekranu", "Dimensiune ecran"},
    {"Screen rotation", "画面回転", "Bildschirmdrehung", "Rotación de pantalla", "Rotation de l'écran", "Rotazione schermo", "Schermrotatie", "Rotação de tela", "Поворот экрана", "屏幕旋转", "화면 회전", "دوران الشاشة", "Rotasi layar", "Обертання екрана", "Περιστροφή οθόνης", "Skärmrotation", "หมุนหน้าจอ", "Otočení obrazovky", "Skærmrotation", "Ekran döndürme", "Skjermrotasjon", "Képernyő forgatás", "Näytön kierto", "Xoay màn hình", "Obrót ekranu", "Rotație ecran"},
    {"Screen gap", "画面間隔", "Bildschirmabstand", "Separación de pantallas", "Espacement des écrans", "Spazio tra schermi", "Schermafstand", "Espaço entre telas", "Зазор между экранами", "屏幕间距", "화면 간격", "فجوة الشاشة", "Jarak layar", "Зазор між екранами", "Κενό οθόνης", "Skärmavstånd", "ช่องว่างหน้าจอ", "Mezera obrazovek", "Skærmafstand", "Ekran aralığı", "Skjermavstand", "Képernyőrés", "Näyttöväli", "Khoảng cách màn hình", "Odstęp ekranów", "Spațiu ecran"},
    {"Screen layout", "画面レイアウト", "Bildschirmlayout", "Diseño de pantalla", "Disposition de l'écran", "Layout schermo", "Schermindeling", "Layout da tela", "Расположение экранов", "屏幕布局", "화면 레이아웃", "تخطيط الشاشة", "Tata letak layar", "Розташування екранів", "Διάταξη οθόνης", "Skärmlayout", "รูปแบบหน้าจอ", "Rozložení obrazovek", "Skærmlayout", "Ekran düzeni", "Skjermoppsett", "Képernyő-elrendezés", "Näyttöasettelu", "Bố cục màn hình", "Układ ekranów", "Aspect ecran"},
    {"Natural", "自然", "Natürlich", "Natural", "Naturel", "Naturale", "Natuurlijk", "Natural", "Естественный", "自然", "자연", "طبيعي", "Alami", "Природний", "Φυσικό", "Naturlig", "ธรรมชาติ", "Přirozené", "Naturlig", "Doğal", "Naturlig", "Természetes", "Luonnollinen", "Tự nhiên", "Naturalny", "Natural"},
    {"Vertical", "縦", "Vertikal", "Vertical", "Vertical", "Verticale", "Verticaal", "Vertical", "Вертикально", "纵向", "세로", "عمودي", "Vertikal", "Вертикальний", "Κάθετο", "Vertikal", "แนวตั้ง", "Vertikální", "Vertikal", "Dikey", "Vertikal", "Függőleges", "Pysty", "Dọc", "Pionowy", "Vertical"},
    {"Horizontal", "横", "Horizontal", "Horizontal", "Horizontal", "Orizzontale", "Horizontaal", "Horizontal", "Горизонтально", "横向", "가로", "أفقي", "Horizontal", "Горизонтально", "Οριζόντια", "Horisontell", "แนวนอน", "Horizontální", "Horisontal", "Yatay", "Horisontal", "Vízszintes", "Vaaka", "Ngang", "Poziomo", "Orizontal"},
    {"Hybrid", "ハイブリッド", "Hybrid", "Híbrido", "Hybride", "Ibrido", "Hybride", "Híbrido", "Гибридный", "混合", "하이브리드", "هجين", "Hibrida", "Гібридний", "Υβριδικό", "Hybrid", "ไฮบริด", "Hybridní", "Hybrid", "Hibrit", "Hybrid", "Hibrid", "Hybridi", "Lai ghép", "Hybrydowy", "Hibrid"},
    {"Swap screens", "上下画面を入れ替え", "Bildschirme tauschen", "Intercambiar pantallas", "Inverser les écrans", "Scambia schermi", "Schermen omwisselen", "Trocar telas", "Поменять экраны", "交换上下屏", "화면 교환", "تبديل الشاشات", "Tukar layar", "Поміняти екрани", "Εναλλαγή οθονών", "Byt skärmar", "สลับหน้าจอ", "Prohodit obrazovky", "Byt skærme", "Ekranları değiştir", "Bytt skjermer", "Képernyők felcserélése", "Vaihda näytöt", "Đổi màn hình", "Zamień ekrany", "Schimbă ecranele"},
    {"Screen sizing", "画面の拡大方式", "Bildschirmgröße", "Tamaño de pantalla", "Dimensionnement d'écran", "Dimensione schermo", "Schermgrootte", "Tamanho da tela", "Размер экрана", "画面缩放", "화면 크기", "حجم الشاشة", "Ukuran layar", "Розмір екрана", "Μέγεθος οθόνης", "Skärmstorlek", "ขนาดหน้าจอ", "Velikost obrazovky", "Skærmstørrelse", "Ekran boyutu", "Skjermstørrelse", "Képernyőméret", "Näytön koko", "Kích thước màn hình", "Rozmiar ekranu", "Dimensiune ecran"},
    {"Even", "均等", "Gleichmäßig", "Uniforme", "Uniforme", "Uniforme", "Gelijkmatig", "Uniforme", "Равномерно", "均等", "균등", "متساوٍ", "Merata", "Рівномірно", "Ομοιόμορφο", "Jämn", "สม่ำเสมอ", "Rovnoměrné", "Jævn", "Düzgün", "Jevn", "Egyenletes", "Tasainen", "Đều", "Równomierny", "Uniform"},
    {"Emphasize top", "上画面を重視", "Oberen Bildschirm betonen", "Enfatizar pantalla superior", "Mettre en avant l'écran du haut", "Enfatizza schermo superiore", "Bovenste scherm benadrukken", "Enfatizar tela superior", "Выделить верхний экран", "强调上屏", "상단 화면 강조", "إبراز الشاشة العلوية", "Tekankan layar atas", "Виділити верхній екран", "Έμφαση στην επάνω οθόνη", "Framhäv övre skärm", "เน้นหน้าจอบน", "Zvýraznit horní obrazovku", "Fremhæv top-skærm", "Üst ekranı vurgula", "Fremhev øverste skjerm", "Felső képernyő kiemelése", "Korosta ylänäyttöä", "Nhấn mạnh màn hình trên", "Podkreśl górny ekran", "Evidențiază ecranul superior"},
    {"Emphasize bottom", "下画面を重視", "Unteren Bildschirm betonen", "Enfatizar pantalla inferior", "Mettre en avant l'écran du bas", "Enfatizza schermo inferiore", "Onderste scherm benadrukken", "Enfatizar tela inferior", "Выделить нижний экран", "强调下屏", "하단 화면 강조", "إبراز الشاشة السفلية", "Tekankan layar bawah", "Виділити нижній екран", "Έμφαση κάτω οθόνης", "Betona nedre skärm", "เน้นหน้าจอล่าง", "Zvýraznit spodní obrazovku", "Fremhæv nederste skærm", "Alt ekranı vurgula", "Fremhev nedre skjerm", "Alsó képernyő kiemelése", "Korosta alanäyttöä", "Nhấn màn hình dưới", "Podkreśl dolny ekran", "Evidențiază ecranul inferior"},
    {"Top only", "上画面のみ", "Nur oberer Bildschirm", "Solo pantalla superior", "Écran supérieur uniquement", "Solo schermo superiore", "Alleen bovenste scherm", "Somente tela superior", "Только верхний экран", "仅上屏", "상단 화면만", "الشاشة العلوية فقط", "Hanya layar atas", "Лише верхній екран", "Μόνο η επάνω οθόνη", "Endast övre skärm", "เฉพาะหน้าจอบน", "Pouze horní obrazovka", "Kun øverste skærm", "Yalnızca üst ekran", "Kun øverste skjerm", "Csak felső képernyő", "Vain ylänäyttö", "Chỉ màn hình trên", "Tylko górny ekran", "Doar ecranul superior"},
    {"Bottom only", "下画面のみ", "Nur unten", "Solo inferior", "Bas seulement", "Solo inferiore", "Alleen onder", "Somente inferior", "Только нижний", "仅下屏", "하단만", "الأسفل فقط", "Hanya bawah", "Лише низ", "Μόνο κάτω", "Endast nedre", "เฉพาะล่าง", "Pouze spodní", "Kun nederst", "Yalnızca alt", "Kun nederst", "Csak alsó", "Vain ala", "Chỉ dưới", "Tylko dolny", "Doar jos"},
    {"Force integer scaling", "整数スケーリングを強制", "Ganzzahl-Skalierung erzwingen", "Forzar escala entera", "Forcer mise à l'échelle entière", "Forza scaling a numeri interi", "Gehele schaalforcing", "Forçar escala inteira", "Принудительное целочисленное масштабирование", "强制整数缩放", "정수 배율 강제", "فرض التحجيم بأعداد صحيحة", "Paksa penskalaan bilangan bulat", "Примусове цілочислове масштабування", "Επιβολή ακέραιας κλίμακας", "Tvinga heltalsskalning", "บังคับการปรับขนาดเป็นจำนวนเต็ม", "Vynutit celočíselné škálování", "Tving heltalsskalering", "Tam sayı ölçeklemeyi zorla", "Tving heltallsskalering", "Egész számú skálázás kényszerítése", "Pakota kokonaislukuskaalaus", "Buộc tỷ lệ số nguyên", "Wymuś skalowanie całkowite", "Forțează scalare întreagă"},
    {"Aspect ratio", "アスペクト比", "Seitenverhältnis", "Relación de aspecto", "Format d'image", "Proporzioni", "Beeldverhouding", "Proporção de tela", "Соотношение сторон", "宽高比", "화면 비율", "نسبة العرض إلى الارتفاع", "Rasio aspek", "Співвідношення сторін", "Αναλογία διαστάσεων", "Bildförhållande", "อัตราส่วนภาพ", "Poměr stran", "Billedformat", "En-boy oranı", "Sideforhold", "Képarány", "Kuvasuhde", "Tỷ lệ khung hình", "Proporcje obrazu", "Raport de aspect"},
    {"Open new window", "新しいウィンドウを開く", "Neues Fenster öffnen", "Abrir ventana nueva", "Ouvrir une nouvelle fenêtre", "Apri nuova finestra", "Nieuw venster openen", "Abrir nova janela", "Открыть новое окно", "打开新窗口", "새 창 열기", "فتح نافذة جديدة", "Buka jendela baru", "Відкрити нове вікно", "Άνοιγμα νέου παραθύρου", "Öppna nytt fönster", "เปิดหน้าต่างใหม่", "Otevřít nové okno", "Åbn nyt vindue", "Yeni pencere aç", "Åpne nytt vindu", "Új ablak megnyitása", "Avaa uusi ikkuna", "Mở cửa sổ mới", "Otwórz nowe okno", "Deschide fereastră nouă"},
    {"Screen filtering", "画面フィルタリング", "Bildschirmfilterung", "Filtrado de pantalla", "Filtrage de l'écran", "Filtraggio schermo", "Schermfiltering", "Filtragem de tela", "Фильтрация экрана", "屏幕滤镜", "화면 필터링", "تصفية الشاشة", "Filter layar", "Фільтрація екрана", "Φιλτράρισμα οθόνης", "Skärmfiltrering", "การกรองหน้าจอ", "Filtrování obrazovky", "Skærmfiltrering", "Ekran filtreleme", "Skjermfiltrering", "Képernyőszűrés", "Näytön suodatus", "Lọc màn hình", "Filtrowanie ekranu", "Filtrare ecran"},
    {"Show OSD", "OSDを表示", "OSD anzeigen", "Mostrar OSD", "Afficher l'OSD", "Mostra OSD", "OSD weergeven", "Mostrar OSD", "Показывать OSD", "显示 OSD", "OSD 표시", "إظهار OSD", "Tampilkan OSD", "Показати OSD", "Εμφάνιση OSD", "Visa OSD", "แสดง OSD", "Zobrazit OSD", "Vis OSD", "OSD göster", "Vis OSD", "OSD megjelenítése", "Näytä OSD", "Hiện OSD", "Pokaż OSD", "Afișează OSD"},
    {"Config", "設定", "Konfiguration", "Configuración", "Configuration", "Configurazione", "Configuratie", "Configuração", "Настройки", "设定", "설정", "إعدادات", "Konfigurasi", "Конфігурація", "Ρυθμίσεις", "Konfiguration", "การตั้งค่า", "Konfigurace", "Konfiguration", "Yapılandırma", "Konfigurasjon", "Beállítások", "Asetukset", "Cấu hình", "Konfiguracja", "Configurare"},
    {"Input Config", "入力設定", "Eingabeeinstellungen", "Configuración de entrada", "Configuration des entrées", "Configurazione input", "Invoerinstellingen", "Configuração de entrada", "Настройки ввода", "输入设置", "입력 설정", "إعدادات الإدخال", "Konfigurasi input", "Налаштування вводу", "Ρυθμίσεις εισόδου", "Inmatningsinställningar", "การตั้งค่าอินพุต", "Nastavení vstupu", "Inputindstillinger", "Girdi ayarları", "Inndatainnstillinger", "Beviteli beállítások", "Syöteasetukset", "Cài đặt nhập liệu", "Ustawienia wejścia", "Setări intrare"},
    {"Emu settings", "エミュレーター設定", "Emulator-Einstellungen", "Ajustes del emulador", "Paramètres de l'émulateur", "Impostazioni emulatore", "Emulatorinstellingen", "Configurações do emulador", "Настройки эмулятора", "模拟器设置", "에뮬레이터 설정", "إعدادات المحاكي", "Pengaturan emu", "Налаштування емулятора", "Ρυθμίσεις εξομοιωτή", "Emulatorinställningar", "การตั้งค่า emu", "Nastavení emulátoru", "Emulatorindstillinger", "Emülatör ayarları", "Emulatorinnstillinger", "Emulátor beállítások", "Emulaattorin asetukset", "Cài đặt emu", "Ustawienia emulatora", "Setări emulator"},
    {"Preferences...", "環境設定...", "Einstellungen...", "Preferencias...", "Préférences...", "Preferenze...", "Voorkeuren...", "Preferências...", "Настройки...", "偏好设置...", "환경 설정...", "التفضيلات...", "Preferensi...", "Налаштування...", "Προτιμήσεις...", "Inställningar...", "การตั้งค่า...", "Předvolby...", "Indstillinger...", "Tercihler...", "Innstillinger...", "Beállítások...", "Asetukset...", "Tùy chọn...", "Preferencje...", "Preferințe..."},
    {"Input and hotkeys", "入力とホットキー", "Eingabe und Hotkeys", "Entrada y atajos", "Entrées et raccourcis", "Input e scorciatoie", "Invoer en sneltoetsen", "Entrada e atalhos", "Ввод и горячие клавиши", "输入与快捷键", "입력 및 단축키", "الإدخال والاختصارات", "Input dan hotkey", "Ввід і гарячі клавіші", "Είσοδος και συντομεύσεις", "Inmatning och snabbtangenter", "การป้อนข้อมูลและปุ่มลัด", "Vstup a klávesové zkratky", "Input og genvejstaster", "Girdi ve kısayol tuşları", "Inndata og hurtigtaster", "Bevitel és gyorsbillentyűk", "Syöte ja pikanäppäimet", "Nhập liệu và phím tắt", "Wejście i skróty klawiszowe", "Intrare și taste rapide"},
    {"Video settings", "映像設定", "Videoeinstellungen", "Ajustes de vídeo", "Paramètres vidéo", "Impostazioni video", "Video-instellingen", "Configurações de vídeo", "Настройки видео", "视频设置", "비디오 설정", "إعدادات الفيديو", "Pengaturan video", "Налаштування відео", "Ρυθμίσεις βίντεο", "Videoinställningar", "การตั้งค่าวิดีโอ", "Nastavení videa", "Videoindstillinger", "Video ayarları", "Videoinnstillinger", "Videobeállítások", "Videoasetukset", "Cài đặt video", "Ustawienia wideo", "Setări video"},
    {"Camera settings", "カメラ設定", "Kameraeinstellungen", "Ajustes de cámara", "Paramètres caméra", "Impostazioni fotocamera", "Camera-instellingen", "Configurações da câmera", "Настройки камеры", "摄像头设置", "카메라 설정", "إعدادات الكاميرا", "Pengaturan kamera", "Налаштування камери", "Ρυθμίσεις κάμερας", "Kamerainställningar", "การตั้งค่ากล้อง", "Nastavení kamery", "Kameraindstillinger", "Kamera ayarları", "Kamerainnstillinger", "Kamera beállítások", "Kamera-asetukset", "Cài đặt camera", "Ustawienia kamery", "Setări cameră"},
    {"Audio settings", "音声設定", "Audio-Einstellungen", "Ajustes de audio", "Paramètres audio", "Impostazioni audio", "Audio-instellingen", "Configurações de áudio", "Настройки звука", "音频设置", "오디오 설정", "إعدادات الصوت", "Pengaturan audio", "Налаштування звуку", "Ρυθμίσεις ήχου", "Ljudinställningar", "การตั้งค่าเสียง", "Nastavení zvuku", "Lydindstillinger", "Ses ayarları", "Lydinnstillinger", "Hangbeállítások", "Ääniasetukset", "Cài đặt âm thanh", "Ustawienia audio", "Setări audio"},
    {"Multiplayer settings", "マルチプレイ設定", "Mehrspieler-Einstellungen", "Ajustes multijugador", "Paramètres multijoueur", "Impostazioni multigiocatore", "Multiplayer-instellingen", "Configurações multijogador", "Настройки мультиплеера", "多人游戏设置", "멀티플레이 설정", "إعدادات اللعب الجماعي", "Pengaturan multipemain", "Налаштування мультиплеєра", "Ρυθμίσεις multiplayer", "Flerspelarinställningar", "การตั้งค่าเล่นหลายคน", "Nastavení multiplayeru", "Multiplayer-indstillinger", "Çok oyunculu ayarları", "Flerspillerinnstillinger", "Többjátékos beállítások", "Moninpeliasetukset", "Cài đặt nhiều người chơi", "Ustawienia wieloosobowe", "Setări multiplayer"},
    {"Wifi settings", "Wi-Fi設定", "WLAN-Einstellungen", "Ajustes Wi-Fi", "Paramètres Wi-Fi", "Impostazioni Wi-Fi", "Wi-Fi-instellingen", "Configurações Wi-Fi", "Настройки Wi-Fi", "Wi-Fi 设置", "Wi-Fi 설정", "إعدادات Wi-Fi", "Pengaturan Wi-Fi", "Налаштування Wi-Fi", "Ρυθμίσεις Wi-Fi", "Wi-Fi-inställningar", "การตั้งค่า Wi-Fi", "Nastavení Wi-Fi", "Wi-Fi-indstillinger", "Wi-Fi ayarları", "Wi-Fi-innstillinger", "Wi-Fi beállítások", "Wi-Fi-asetukset", "Cài đặt Wi-Fi", "Ustawienia Wi-Fi", "Setări Wi-Fi"},
    {"Firmware settings", "ファームウェア設定", "Firmware-Einstellungen", "Ajustes de firmware", "Paramètres firmware", "Impostazioni firmware", "Firmware-instellingen", "Configurações de firmware", "Настройки прошивки", "固件设置", "펌웨어 설정", "إعدادات البرنامج الثابت", "Pengaturan firmware", "Налаштування прошивки", "Ρυθμίσεις υλικολογισμικού", "Firmwareinställningar", "การตั้งค่าเฟิร์มแวร์", "Nastavení firmwaru", "Firmwareindstillinger", "Firmware ayarları", "Fastvareinnstillinger", "Firmware-beállítások", "Laiteohjelma-asetukset", "Cài đặt firmware", "Ustawienia firmware", "Setări firmware"},
    {"Interface settings", "インターフェース設定", "Oberflächeneinstellungen", "Ajustes de interfaz", "Paramètres interface", "Impostazioni interfaccia", "Interface-instellingen", "Configurações da interface", "Настройки интерфейса", "界面设置", "인터페이스 설정", "إعدادات الواجهة", "Pengaturan antarmuka", "Налаштування інтерфейсу", "Ρυθμίσεις διεπαφής", "Gränssnittsinställningar", "การตั้งค่าอินเทอร์เฟซ", "Nastavení rozhraní", "Grænsefladeindstillinger", "Arayüz ayarları", "Grensesnittinnstillinger", "Felület beállítások", "Käyttöliittymän asetukset", "Cài đặt giao diện", "Ustawienia interfejsu", "Setări interfață"},
    {"Path settings", "パス設定", "Pfad-Einstellungen", "Ajustes de rutas", "Paramètres des chemins", "Impostazioni percorsi", "Padinstellingen", "Configurações de caminhos", "Настройки путей", "路径设置", "경로 설정", "إعدادات المسارات", "Pengaturan path", "Налаштування шляхів", "Ρυθμίσεις διαδρομών", "Sökvägsinställningar", "การตั้งค่า path", "Nastavení cest", "Sti-indstillinger", "Yol ayarları", "Baneinnstillinger", "Útvonal beállítások", "Polkuasetukset", "Cài đặt đường dẫn", "Ustawienia ścieżek", "Setări căi"},
    {"Limit framerate", "フレームレート制限", "Bildrate begrenzen", "Limitar FPS", "Limiter le taux d'images", "Limita framerate", "Framerate beperken", "Limitar FPS", "Ограничить частоту кадров", "限制帧率", "프레임레이트 제한", "تحديد معدل الإطارات", "Batasi framerate", "Обмежити частоту кадрів", "Περιορισμός ρυθμού καρέ", "Begränsa bildfrekvens", "จำกัดอัตราเฟรม", "Omezit snímkovou frekvenci", "Begræns billedhastighed", "Kare hızını sınırla", "Begrens bildefrekvens", "Képkockasebesség korlátozása", "Rajoita ruudunpäivitysnopeutta", "Giới hạn tốc độ khung hình", "Ogranicz liczbę klatek", "Limitează rata de cadre"},
    {"Audio sync", "音声同期", "Audiosynchronisation", "Sincronización de audio", "Synchronisation audio", "Sincronizzazione audio", "Audiosynchronisatie", "Sincronização de áudio", "Синхронизация звука", "音频同步", "오디오 동기화", "مزامنة الصوت", "Sinkronisasi audio", "Синхронізація звуку", "Συγχρονισμός ήχου", "Ljudsynkronisering", "ซิงค์เสียง", "Synchronizace zvuku", "Lyd synkronisering", "Ses senkronizasyonu", "Lyd synkronisering", "Hangszinkronizálás", "Äänen synkronointi", "Đồng bộ âm thanh", "Synchronizacja dźwięku", "Sincronizare audio"},
    {"MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime"},
    {"Input settings", "操作設定", "Eingabeeinstellungen", "Ajustes de entrada", "Paramètres entrée", "Impostazioni input", "Invoerinstellingen", "Configurações de entrada", "Настройки ввода", "操作设置", "입력 설정", "إعدادات الإدخال", "Pengaturan input", "Налаштування введення", "Ρυθμίσεις εισόδου", "Inmatningsinställningar", "การตั้งค่าอินพุต", "Nastavení vstupu", "Inputindstillinger", "Giriş ayarları", "Inndata-innstillinger", "Beviteli beállítások", "Syöttöasetukset", "Cài đặt đầu vào", "Ustawienia wejścia", "Setări intrare"},
    {"Other settings", "MelonPrime設定", "MelonPrime-Einstellungen", "Ajustes de MelonPrime", "Paramètres MelonPrime", "Impostazioni MelonPrime", "MelonPrime-instellingen", "Configurações do MelonPrime", "Настройки MelonPrime", "MelonPrime 设置", "MelonPrime 설정", "إعدادات MelonPrime", "Pengaturan MelonPrime", "Налаштування MelonPrime", "Ρυθμίσεις MelonPrime", "MelonPrime-inställningar", "การตั้งค่า MelonPrime", "Nastavení MelonPrime", "MelonPrime-indstillinger", "MelonPrime ayarları", "MelonPrime-innstillinger", "MelonPrime beállítások", "MelonPrime-asetukset", "Cài đặt MelonPrime", "Ustawienia MelonPrime", "Setări MelonPrime"},
    {"MelonPrime settings", "MelonPrime設定", "MelonPrime-Einstellungen", "Ajustes de MelonPrime", "Paramètres MelonPrime", "Impostazioni MelonPrime", "MelonPrime-instellingen", "Configurações do MelonPrime", "Настройки MelonPrime", "MelonPrime 设置", "MelonPrime 설정", "إعدادات MelonPrime", "Pengaturan MelonPrime", "Налаштування MelonPrime", "Ρυθμίσεις MelonPrime", "MelonPrime-inställningar", "การตั้งค่า MelonPrime", "Nastavení MelonPrime", "MelonPrime-indstillinger", "MelonPrime ayarları", "MelonPrime-innstillinger", "MelonPrime beállítások", "MelonPrime-asetukset", "Cài đặt MelonPrime", "Ustawienia MelonPrime", "Setări MelonPrime"},
    {"Custom HUD settings", "カスタムHUD設定", "Benutzerdefinierte HUD-Einstellungen", "Ajustes del HUD personalizado", "Paramètres HUD personnalisés", "Impostazioni HUD personalizzato", "Aangepaste HUD-instellingen", "Configurações do HUD personalizado", "Настройки пользовательского HUD", "自定义 HUD 设置", "커스텀 HUD 설정", "إعدادات HUD المخصص", "Pengaturan HUD kustom", "Налаштування користувацького HUD", "Ρυθμίσεις προσαρμοσμένου HUD", "Anpassade HUD-inställningar", "การตั้งค่า HUD แบบกำหนดเอง", "Nastavení vlastního HUD", "Brugerdefinerede HUD-indstillinger", "Özel HUD ayarları", "Tilpassede HUD-innstillinger", "Egyéni HUD beállítások", "Mukautetut HUD-asetukset", "Cài đặt HUD tùy chỉnh", "Ustawienia niestandardowego HUD", "Setări HUD personalizat"},
    {"Disable SF (Shadow Freeze)", "SF (シャドウフリーズ) を無効化", "SF (Shadow Freeze) deaktivieren", "Desactivar SF (Shadow Freeze)", "Désactiver SF (Shadow Freeze)", "Disabilita SF (Shadow Freeze)", "SF (Shadow Freeze) uitschakelen", "Desativar SF (Shadow Freeze)", "Отключить SF (Shadow Freeze)", "禁用 SF (Shadow Freeze)", "SF (Shadow Freeze) 비활성화", "تعطيل SF (Shadow Freeze)", "Nonaktifkan SF (Shadow Freeze)", "Вимкнути SF (Shadow Freeze)", "Απενεργοποίηση SF (Shadow Freeze)", "Inaktivera SF (Shadow Freeze)", "ปิด SF (Shadow Freeze)", "Zakázat SF (Shadow Freeze)", "Deaktiver SF (Shadow Freeze)", "SF'yi devre dışı bırak (Shadow Freeze)", "Deaktiver SF (Shadow Freeze)", "SF (Shadow Freeze) letiltása", "Poista SF (Shadow Freeze) käytöstä", "Tắt SF (Shadow Freeze)", "Wyłącz SF (Shadow Freeze)", "Dezactivează SF (Shadow Freeze)"},
    {"In-Game Top Screen Only", "ゲーム中は上画面のみ", "Nur oberer Bildschirm im Spiel", "Solo pantalla superior en juego", "Écran supérieur uniquement en jeu", "Solo schermo superiore in gioco", "Alleen bovenste scherm in spel", "Somente tela superior no jogo", "Только верхний экран в игре", "游戏中仅上屏", "게임 중 상단 화면만", "الشاشة العلوية في اللعبة فقط", "Hanya layar atas dalam game", "Лише верхній екран у грі", "Μόνο επάνω οθόνη στο παιχνίδι", "Endast övre skärm i spel", "เฉพาะหน้าจอบนในเกม", "Pouze horní obrazovka ve hře", "Kun øverste skærm i spil", "Oyunda yalnızca üst ekran", "Kun øvre skjerm i spill", "Csak felső képernyő játékban", "Vain ylänäyttö pelissä", "Chỉ màn hình trên trong game", "Tylko górny ekran w grze", "Doar ecran superior în joc"},
    {"Help", "ヘルプ", "Hilfe", "Ayuda", "Aide", "Aiuto", "Help", "Ajuda", "Справка", "帮助", "도움말", "مساعدة", "Bantuan", "Довідка", "Βοήθεια", "Hjälp", "ความช่วยเหลือ", "Nápověda", "Hjælp", "Yardım", "Hjelp", "Súgó", "Ohje", "Trợ giúp", "Pomoc", "Ajutor"},
    {"About...", "このアプリについて...", "Über...", "Acerca de...", "À propos...", "Informazioni...", "Over...", "Sobre...", "О программе...", "关于...", "정보...", "حول...", "Tentang...", "Про програму...", "Σχετικά...", "Om...", "เกี่ยวกับ...", "O programu...", "Om...", "Hakkında...", "Om...", "Névjegy...", "Tietoja...", "Giới thiệu...", "O programie...", "Despre..."},
    {"Clear", "履歴を消去", "Verlauf löschen", "Borrar historial", "Effacer l'historique", "Cancella cronologia", "Geschiedenis wissen", "Limpar histórico", "Очистить историю", "清除历史", "기록 지우기", "مسح السجل", "Hapus riwayat", "Очистити історію", "Εκκαθάριση ιστορικού", "Rensa historik", "ล้างประวัติ", "Vymazat historii", "Ryd historik", "Geçmişi temizle", "Tøm historikk", "Előzmények törlése", "Tyhjennä historia", "Xóa lịch sử", "Wyczyść historię", "Șterge istoricul"},

    // Tabs and major sections
    {"MelonPrime Settings", "MelonPrime設定", "MelonPrime-Einstellungen", "Ajustes de MelonPrime", "Paramètres MelonPrime", "Impostazioni MelonPrime", "MelonPrime-instellingen", "Configurações do MelonPrime", "Настройки MelonPrime", "MelonPrime 设置", "MelonPrime 설정", "إعدادات MelonPrime", "Pengaturan MelonPrime", "Налаштування MelonPrime", "Ρυθμίσεις MelonPrime", "MelonPrime-inställningar", "การตั้งค่า MelonPrime", "Nastavení MelonPrime", "MelonPrime-indstillinger", "MelonPrime ayarları", "MelonPrime-innstillinger", "MelonPrime-beállítások", "MelonPrime-asetukset", "Cài đặt MelonPrime", "Ustawienia MelonPrime", "Setări MelonPrime"},
    {"Controls", "操作", "Steuerung", "Controles", "Commandes", "Comandi", "Besturing", "Controles", "Управление", "操作", "조작", "عناصر التحكم", "Kontrol", "Керування", "Χειρισμοί", "Kontroller", "การควบคุม", "Ovládání", "Kontroller", "Kontroller", "Kontroller", "Irányítás", "Ohjaimet", "Điều khiển", "Sterowanie", "Controale"},
    {"Controls 2", "操作 2", "Steuerung 2", "Controles 2", "Commandes 2", "Controlli 2", "Besturing 2", "Controles 2", "Управление 2", "操作 2", "조작 2", "عناصر التحكم 2", "Kontrol 2", "Керування 2", "Χειρισμός 2", "Kontroller 2", "การควบคุม 2", "Ovládání 2", "Styring 2", "Kontroller 2", "Kontroller 2", "Irányítás 2", "Ohjaimet 2", "Điều khiển 2", "Sterowanie 2", "Controale 2"},
    {"Custom HUD", "カスタムHUD", "Benutzerdefiniertes HUD", "HUD personalizado", "HUD personnalisé", "HUD personalizzato", "Aangepaste HUD", "HUD personalizado", "Пользовательский HUD", "自定义 HUD", "사용자 지정 HUD", "HUD مخصص", "HUD kustom", "Користувацький HUD", "Προσαρμοσμένο HUD", "Anpassad HUD", "HUD กำหนดเอง", "Vlastní HUD", "Brugerdefineret HUD", "Özel HUD", "Tilpasset HUD", "Egyéni HUD", "Mukautettu HUD", "HUD tùy chỉnh", "Niestandardowy HUD", "HUD personalizat"},
    {"Custom HUD Input/Output", "カスタムHUD 入出力", "Benutzerdefiniertes HUD Ein-/Ausgabe", "HUD personalizado Entrada/Salida", "HUD personnalisé Entrée/Sortie", "HUD personalizzato Input/Output", "Aangepaste HUD Invoer/Uitvoer", "HUD personalizado Entrada/Saída", "Пользовательский HUD Ввод/Вывод", "自定义 HUD 输入/输出", "커스텀 HUD 입출력", "HUD مخصص إدخال/إخراج", "Input/Output HUD kustom", "Користувацький HUD ввід/вивід", "Προσαρμοσμένο HUD Είσοδος/Έξοδος", "Anpassad HUD In/Ut", "HUD แบบกำหนดเอง อินพุต/เอาต์พุต", "Vlastní HUD Vstup/Výstup", "Brugerdefineret HUD Input/Output", "Özel HUD Giriş/Çıkış", "Tilpasset HUD Inndata/Utdata", "Egyéni HUD bemenet/kimenet", "Mukautettu HUD tulo/lähtö", "HUD tùy chỉnh Nhập/Xuất", "Niestandardowy HUD Wejście/Wyjście", "HUD personalizat Intrare/Ieșire"},
    {"Input", "入力", "Eingabe", "Entrada", "Entrées", "Input", "Invoer", "Entrada", "Ввод", "输入", "입력", "إدخال", "Input", "Введення", "Είσοδος", "Inmatning", "อินพุต", "Vstup", "Input", "Giriş", "Inndata", "Bemenet", "Syöte", "Nhập", "Wejście", "Intrare"},
    {"Output", "出力", "Ausgabe", "Salida", "Sortie", "Uscita", "Uitvoer", "Saída", "Вывод", "输出", "출력", "الإخراج", "Keluaran", "Вихід", "Έξοδος", "Utdata", "เอาต์พุต", "Výstup", "Output", "Çıktı", "Utdata", "Kimenet", "Ulostulo", "Đầu ra", "Wyjście", "Ieșire"},
    {"INPUT SETTINGS", "入力設定", "EINGABE-EINSTELLUNGEN", "AJUSTES DE ENTRADA", "PARAMÈTRES D'ENTRÉE", "IMPOSTAZIONI INPUT", "INVOERINSTELLINGEN", "CONFIGURAÇÕES DE ENTRADA", "НАСТРОЙКИ ВВОДА", "输入设置", "입력 설정", "إعدادات الإدخال", "PENGATURAN INPUT", "НАЛАШТУВАННЯ ВВОДУ", "ΡΥΘΜΙΣΕΙΣ ΕΙΣΟΔΟΥ", "INMATNINGSINSTÄLLNINGAR", "การตั้งค่าอินพุต", "NASTAVENÍ VSTUPU", "INPUTINDSTILLINGER", "GİRİŞ AYARLARI", "INNDATASINNSTILLINGER", "BEVITELI BEÁLLÍTÁSOK", "SYÖTEASETUKSET", "CÀI ĐẶT ĐẦU VÀO", "USTAWIENIA WEJŚCIA", "SETĂRI INTRARE"},
    {"INPUT METHOD", "入力方式", "EINGABEMETHODE", "MÉTODO DE ENTRADA", "MÉTHODE DE SAISIE", "METODO DI INPUT", "INVOERMETHODE", "MÉTODO DE ENTRADA", "МЕТОД ВВОДА", "输入方式", "입력 방식", "طريقة الإدخال", "METODE INPUT", "МЕТОД ВВОДУ", "ΜΕΘΟΔΟΣ ΕΙΣΟΔΟΥ", "INMATNINGSMETOD", "วิธีป้อนข้อมูล", "METODA VSTUPU", "INPUTMETODE", "GİRİŞ YÖNTEMİ", "INNDATAMETODE", "BEVITELI MÓDSZER", "SYÖTTÖMENETELMÄ", "PHƯƠNG THỨC NHẬP", "METODA WEJŚCIA", "METODĂ DE INTRODUCERE"},
    {"SCREEN SYNC", "画面同期", "BILDSYNCHRONISATION", "SINCRONIZACIÓN DE PANTALLA", "SYNCHRONISATION ÉCRAN", "SINCRONIZZAZIONE SCHERMO", "BEELDSYNCHRONISATIE", "SINCRONIZAÇÃO DE TELA", "СИНХРОНИЗАЦИЯ ЭКРАНА", "屏幕同步", "화면 동기화", "مزامنة الشاشة", "SINKRONISASI LAYAR", "СИНХРОНІЗАЦІЯ ЕКРАНА", "ΣΥΓΧΡΟΝΙΣΜΟΣ ΟΘΟΝΗΣ", "SKÄRMSYNK", "ซิงค์หน้าจอ", "SYNCHRONIZACE OBRAZOVKY", "SKÆRMSYNK", "EKRAN SENKRONİZASYONU", "SKJERMSYNK", "KÉPERNYŐ SZINKRON", "NÄYTÖN SYNKRONOINTI", "ĐỒNG BỘ MÀN HÌNH", "SYNCHRONIZACJA EKRANU", "SINCRONIZARE ECRAN"},
    {"CURSOR CLIP SETTINGS", "カーソル制限", "CURSOREINGRENZUNG", "LÍMITES DEL CURSOR", "LIMITES DU CURSEUR", "IMPOSTAZIONI LIMITE CURSORE", "CURSORBEPERKING", "LIMITES DO CURSOR", "ОГРАНИЧЕНИЕ КУРСОРА", "光标限制设置", "커서 제한 설정", "إعدادات قص المؤشر", "PENGATURAN BATAS KURSOR", "ОБМЕЖЕННЯ КУРСОРА", "ΡΥΘΜΙΣΕΙΣ ΟΡΙΟΥ ΔΕΙΚΤΗ", "MARKÖRBEGRÄNSNING", "การตั้งค่าขอบเขตเคอร์เซอร์", "OMEZENÍ KURZORU", "MARKØRBEGRÆNSNING", "İMLEÇ SINIRI AYARLARI", "MARKØRBEGRENSNING", "KURZORKORLÁTOZÁS", "OSOITTIMEN RAJOITUS", "CÀI ĐẶT GIỚI HẠN CON TRỎ", "USTAWIENIA OGRANICZENIA KURSORA", "SETĂRI LIMITĂ CURSOR"},
    {"IN-GAME APPLY", "ゲーム内反映", "IM SPIEL ANWENDEN", "APLICAR EN JUEGO", "APPLIQUER EN JEU", "APPLICA IN GIOCO", "IN SPEL TOEPASSEN", "APLICAR NO JOGO", "ПРИМЕНИТЬ В ИГРЕ", "游戏内应用", "게임 내 적용", "تطبيق في اللعبة", "TERAPKAN DI GAME", "ЗАСТОСУВАТИ В ГРІ", "ΕΦΑΡΜΟΓΗ ΣΤΟ ΠΑΙΧΝΙΔΙ", "TILLÄMPA I SPEL", "ใช้ในเกม", "POUŽÍT VE HŘE", "ANVEND I SPIL", "OYUNDA UYGULA", "BRUK I SPILL", "ALKALMAZÁS JÁTÉKBAN", "KÄYTÄ PELISSÄ", "ÁP DỤNG TRONG GAME", "ZASTOSUJ W GRZE", "APLICĂ ÎN JOC"},
    {"IN-GAME ASPECT RATIO", "ゲーム内アスペクト比", "SEITENVERHÄLTNIS IM SPIEL", "RELACIÓN DE ASPECTO EN EL JUEGO", "FORMAT D'IMAGE EN JEU", "PROPORZIONI IN-GIOCO", "BEELDVERHOUDING IN SPEL", "PROPORÇÃO DE TELA NO JOGO", "СООТНОШЕНИЕ СТОРОН В ИГРЕ", "游戏内宽高比", "게임 내 화면 비율", "نسبة العرض إلى الارتفاع داخل اللعبة", "RASIO ASPEK DALAM GAME", "СПІВВІДНОШЕННЯ СТОРІН У ГРІ", "ΑΝΑΛΟΓΙΑ ΔΙΑΣΤΑΣΕΩΝ ΣΤΟ ΠΑΙΧΝΙΔΙ", "BILDFÖRHÅLLANDE I SPEL", "อัตราส่วนภาพในเกม", "POMĚR STRAN VE HŘE", "BILLEDFORMAT I SPIL", "OYUN İÇİ EN-BOY ORANI", "SIDEFORHOLD I SPILL", "JÁTÉK BELI KÉPARÁNY", "PELIN KUVASUHDE", "TỶ LỆ KHUNG HÌNH TRONG GAME", "PROPORCJE OBRAZU W GRZE", "RAPORT DE ASPECT ÎN JOC"},
    {"LOW HP WARNING", "低HP警告", "NIEDRIG-HP-WARNUNG", "AVISO DE HP BAJO", "ALERTE HP FAIBLE", "AVVISO HP BASSO", "WAARSCHUWING LAGE HP", "AVISO DE HP BAIXO", "ПРЕДУПРЕЖДЕНИЕ О НИЗКОМ HP", "低 HP 警告", "낮은 HP 경고", "تحذير HP منخفض", "PERINGATAN HP RENDAH", "ПОПЕРЕДЖЕННЯ ПРО НИЗЬКИЙ HP", "ΠΡΟΕΙΔΟΠΟΙΗΣΗ ΧΑΜΗΛΟΥ HP", "VARNING LÅGT HP", "คำเตือน HP ต่ำ", "VAROVÁNÍ NÍZKÉHO HP", "ADVARSEL LAVT HP", "DÜŞÜK HP UYARISI", "ADVARSEL LAV HP", "ALACSONY HP FIGYELMEZTETÉS", "VAROITUS ALHAISESTA HP:STÄ", "CẢNH BÁO HP THẤP", "OSTRZEŻENIE NISKIEGO HP", "AVERTISMENT HP SCĂZUT"},
    {"SENSITIVITY", "感度", "EMPFINDLICHKEIT", "SENSIBILIDAD", "SENSIBILITÉ", "SENSIBILITÀ", "GEVOELIGHEID", "SENSIBILIDADE", "ЧУВСТВИТЕЛЬНОСТЬ", "灵敏度", "감도", "الحساسية", "SENSITIVITAS", "ЧУТЛИВІСТЬ", "ΕΥΑΙΣΘΗΣΙΑ", "KÄNSLIGHET", "ความไว", "CITLIVOST", "FØLSOMHED", "HASSASİYET", "FØLSOMHET", "ÉRZÉKENYSÉG", "HERKKYYS", "ĐỘ NHẠY", "CZUŁOŚĆ", "SENSIBILITATE"},
    {"BUG FIXES", "不具合修正", "FEHLERBEHEBUNGEN", "CORRECCIONES DE ERRORES", "CORRECTIONS DE BUGS", "CORREZIONI BUG", "BUGFIXES", "CORREÇÕES DE ERROS", "ИСПРАВЛЕНИЯ ОШИБОК", "错误修复", "버그 수정", "إصلاحات الأخطاء", "PERBAIKAN BUG", "ВИПРАВЛЕННЯ ПОМИЛОК", "ΔΙΟΡΘΩΣΕΙΣ ΣΦΑΛΜΑΤΩΝ", "BUGGFIXAR", "แก้ไขบั๊ก", "OPRAVY CHYB", "FEJLRETTELSER", "HATA DÜZELTMELERİ", "FEILRETTINGER", "HIBAJAVÍTÁSOK", "VIRHEKORJAUKSET", "SỬA LỖI", "POPRAWKI BŁĘDÓW", "CORECȚII BUG"},
    {"GAME FEATURE IMPROVEMENTS", "ゲーム機能改善", "SPIELFUNKTIONS-VERBESSERUNGEN", "MEJORAS DE FUNCIONES DEL JUEGO", "AMÉLIORATIONS DES FONCTIONS DE JEU", "MIGLIORAMENTI FUNZIONI DI GIOCO", "SPELFUNCTIE-VERBETERINGEN", "MELHORIAS DE RECURSOS DO JOGO", "УЛУЧШЕНИЯ ИГРОВЫХ ФУНКЦИЙ", "游戏功能改进", "게임 기능 개선", "تحسينات ميزات اللعبة", "PENINGKATAN FITUR GAME", "ПОКРАЩЕННЯ ІГРОВИХ ФУНКЦІЙ", "ΒΕΛΤΙΩΣΕΙΣ ΛΕΙΤΟΥΡΓΙΩΝ ΠΑΙΧΝΙΔΙΟΥ", "FÖRBÄTTRINGAR AV SPELFUNKTIONER", "ปรับปรุงฟีเจอร์เกม", "VYLEPŠENÍ HERníCH FUNKCÍ", "FORBEDRINGER AF SPILFUNKTIONER", "OYUN ÖZELLİK İYİLEŞTİRMELERİ", "FORBEDRINGER AV SPILLFUNKSJONER", "JÁTÉKFUNKCIÓ-JAVÍTÁSOK", "PELIN OMINAISUUSPARANNUKSET", "CẢI TIẾN TÍNH NĂNG GAME", "ULEPSZENIA FUNKCJI GRY", "ÎMBUNĂTĂȚIRI FUNCȚII DE JOC"},
    {"DISABLE FEATURES", "機能無効化", "FUNKTIONEN DEAKTIVIEREN", "DESACTIVAR FUNCIONES", "DÉSACTIVER DES FONCTIONS", "DISABILITA FUNZIONI", "FUNCTIES UITSCHAKELEN", "DESATIVAR RECURSOS", "ОТКЛЮЧИТЬ ФУНКЦИИ", "禁用功能", "기능 비활성화", "تعطيل الميزات", "NONAKTIFKAN FITUR", "ВИМКНУТИ ФУНКЦІЇ", "ΑΠΕΝΕΡΓΟΠΟΙΗΣΗ ΛΕΙΤΟΥΡΓΙΩΝ", "INAKTIVERA FUNKTIONER", "ปิดใช้งานฟีเจอร์", "ZAKÁZAT FUNKCE", "DEAKTIVER FUNKTIONER", "ÖZELLİKLERİ DEVRE DIŞI BIRAK", "DEAKTIVER FUNKSJONER", "FUNKCIÓK LETILTÁSA", "POISTA OMINAISUUDET KÄYTÖSTÄ", "TẮT TÍNH NĂNG", "WYŁĄCZ FUNKCJE", "DEZACTIVEAZĂ FUNCȚII"},
    {"Power-Up Pickup Effects", "パワーアップ取得効果", "Effekte beim Aufsammeln von Power-Ups", "Efectos al recoger potenciadores", "Effets de ramassage des bonus", "Effetti raccolta potenziamenti", "Effecten bij oppakken power-ups", "Efeitos ao recolher power-ups", "Эффекты подбора усилений", "强化道具拾取效果", "파워업 획득 효과", "تأثيرات التقاط Power-Up", "Efek pengambilan Power-Up", "Ефекти підбору Power-Up", "Εφέ μαζέματος Power-Up", "Effekter vid upplockning av power-ups", "เอฟเฟกต์เก็บ Power-Up", "Efekty sebrání power-upů", "Effekter ved opsamling af power-ups", "Power-Up toplama efektleri", "Effekter ved oppsamling av power-ups", "Power-Up felvételi effektek", "Power-Up-keräysefektit", "Hiệu ứng nhặt Power-Up", "Efekty podnoszenia power-upów", "Efecte la ridicarea power-up-urilor"},
    {"GAMEPLAY TOGGLES", "ゲームプレイ切替", "GAMEPLAY-OPTIONEN", "OPCIONES DE JUEGO", "OPTIONS DE GAMEPLAY", "OPZIONI DI GIOCO", "GAMEPLAY-OPTIES", "OPÇÕES DE JOGO", "ИГРОВЫЕ ПЕРЕКЛЮЧАТЕЛИ", "游戏选项", "게임플레이 토글", "خيارات اللعب", "TOGGLE GAMEPLAY", "ІГРОВІ ПЕРЕМИКАЧІ", "ΕΠΙΛΟΓΕΣ ΠΑΙΧΝΙΔΙΟΥ", "SPELALTERNATIV", "สลับการเล่น", "HERNÍ PŘEPÍNAČE", "SPILINDSTILLINGER", "OYUN SEÇENEKLERİ", "SPILLALTERNATIVER", "JÁTÉKBEÁLLÍTÁSOK", "PELIASETUKSET", "TÙY CHỌN GAMEPLAY", "OPCJE ROZGRYWKI", "OPȚIUNI DE JOC"},
    {"VIDEO QUALITY", "映像品質", "VIDEOQUALITÄT", "CALIDAD DE VÍDEO", "QUALITÉ VIDÉO", "QUALITÀ VIDEO", "VIDEOKWALITEIT", "QUALIDADE DE VÍDEO", "КАЧЕСТВО ВИДЕО", "视频质量", "비디오 품질", "جودة الفيديو", "KUALITAS VIDEO", "ЯКІСТЬ ВІДЕО", "ΠΟΙΟΤΗΤΑ ΒΙΝΤΕΟ", "VIDEOKVALITET", "คุณภาพวิดีโอ", "KVALITA VIDEA", "VIDEOKVALITET", "VİDEO KALİTESİ", "VIDEOKVALITET", "VIDEÓMINŐSÉG", "VIDEON LAATU", "CHẤT LƯỢNG VIDEO", "JAKOŚĆ WIDEO", "CALITATE VIDEO"},
    {"VOLUME", "音量", "LAUTSTÄRKE", "VOLUMEN", "VOLUME", "VOLUME", "VOLUME", "VOLUME", "ГРОМКОСТЬ", "音量", "볼륨", "مستوى الصوت", "VOLUME", "ГУЧНІСТЬ", "ΕΝΤΑΣΗ", "VOLYM", "ระดับเสียง", "HLASITOST", "LYDSTYRKE", "SES", "VOLUM", "HANGERŐ", "ÄÄNENVOIMAKKUUS", "ÂM LƯỢNG", "GŁOŚNOŚĆ", "VOLUM"},
    {"LICENSE APPLY", "ライセンス反映", "LIZENZ ANWENDEN", "APLICAR LICENCIA", "APPLIQUER LA LICENCE", "APPLICA LICENZA", "LICENTIE TOEPASSEN", "APLICAR LICENÇA", "ПРИМЕНИТЬ ЛИЦЕНЗИЮ", "应用许可证", "라이선스 적용", "تطبيق الترخيص", "TERAPKAN LISENSI", "ЗАСТОСУВАТИ ЛІЦЕНЗІЮ", "ΕΦΑΡΜΟΓΗ ΑΔΕΙΑΣ", "TILLÄMPA LICENS", "ใช้ใบอนุญาต", "POUŽÍT LICENCI", "ANVEND LICENS", "LİSANSI UYGULA", "BRUK LISENS", "LICENC ALKALMAZÁSA", "KÄYTÄ LISENSSIÄ", "ÁP DỤNG GIẤY PHÉP", "ZASTOSUJ LICENCJĘ", "APLICĂ LICENȚA"},
    {"DEVELOPER ONLY", "開発者向け", "NUR FÜR ENTWICKLER", "SOLO DESARROLLADORES", "RÉSERVÉ AUX DÉVELOPPEURS", "SOLO SVILUPPATORI", "ALLEEN ONTWIKKELAARS", "SOMENTE DESENVOLVEDORES", "ТОЛЬКО ДЛЯ РАЗРАБОТЧИКОВ", "仅限开发者", "개발자 전용", "للمطورين فقط", "KHUSUS PENGEMBANG", "ЛИШЕ ДЛЯ РОЗРОБНИКІВ", "ΜΟΝΟ ΓΙΑ ΠΡΟΓΡΑΜΜΑΤΙΣΤΕΣ", "ENDAST UTVECKLARE", "สำหรับนักพัฒนาเท่านั้น", "POUZE PRO VÝVOJÁŘE", "KUN UDVIKLERE", "YALNIZCA GELİŞTİRİCİLER", "KUN UTVIKLERE", "CSAK FEJLESZTŐKNEK", "VAIN KEHITTÄJILLE", "CHỈ DÀNH CHO NHÀ PHÁT TRIỂN", "TYLKO DLA DEWELOPERÓW", "DOAR PENTRU DEZVOLTATORI"},
    {"DISABLE DEFAULT HUD", "標準HUDを非表示", "STANDARD-HUD DEAKTIVIEREN", "DESACTIVAR HUD PREDETERMINADO", "DÉSACTIVER LE HUD PAR DÉFAUT", "DISATTIVA HUD PREDEFINITO", "STANDAARD-HUD UITSCHAKELEN", "DESATIVAR HUD PADRÃO", "ОТКЛЮЧИТЬ СТАНДАРТНЫЙ HUD", "禁用默认 HUD", "기본 HUD 비활성화", "تعطيل HUD الافتراضي", "NONAKTIFKAN HUD DEFAULT", "ВИМКНУТИ СТАНДАРТНИЙ HUD", "ΑΠΕΝΕΡΓΟΠΟΙΗΣΗ ΠΡΟΕΠΙΛΕΓΜΕΝΟΥ HUD", "INAKTIVERA STANDARD-HUD", "ปิด HUD เริ่มต้น", "ZAKÁZAT VÝCHOZÍ HUD", "DEAKTIVER STANDARD-HUD", "VARSAYILAN HUD'YI DEVRE DIŞI BIRAK", "DEAKTIVER STANDARD-HUD", "ALAPÉRTELMEZETT HUD KIKAPCSOLÁSA", "POISTA OLETUS-HUD KÄYTÖSTÄ", "TẮT HUD MẶC ĐỊNH", "WYŁĄCZ DOMYŚLNY HUD", "DEZACTIVEAZĂ HUD IMPLICIT"},
    {"OUTLINE OVERRIDE", "アウトライン一括設定", "KONTUR-ÜBERSCHREIBUNG", "ANULACIÓN DE CONTORNO", "REMPLACEMENT DE CONTOUR", "SOSTITUZIONE CONTORNO", "OORLIJNOVERSCHRIJVING", "SUBSTITUIÇÃO DE CONTORNO", "ПЕРЕОПРЕДЕЛЕНИЕ КОНТУРА", "轮廓覆盖", "외곽선 재정의", "تجاوز الحدود", "PENGGANTIAN GARIS LUAR", "ЗАМІНА КОНТУРУ", "ΑΝΤΙΚΑΤΑΣΤΑΣΗ ΠΕΡΙΓΡΑΜΜΑΤΟΣ", "KONTURÖVERSKRIVNING", "แทนที่เส้นขอบ", "NAHRADENÍ OBRYSU", "KONTURTILSIDESÆTTELSE", "DIŞ ÇİZGİ GEÇERSİZ KILMA", "KONTUROVERSKRIVING", "KONTÚR FELÜLÍRÁSA", "ÄÄRIVIIVAN KORVAUS", "GHI ĐÈ VIỀN", "ZASTĄPIENIE OBRYSU", "ÎNLOCUIRE CONTUR"},
    {"HUD SCALE", "HUDスケール", "HUD-SKALIERUNG", "ESCALA HUD", "ÉCHELLE HUD", "SCALA HUD", "HUD-SCHAAL", "ESCALA HUD", "МАСШТАБ HUD", "HUD 缩放", "HUD 배율", "مقياس HUD", "SKALA HUD", "МАСШТАБ HUD", "ΚΛΙΜΑΚΑ HUD", "HUD-SKALNING", "สเกล HUD", "MĚŘÍTKO HUD", "HUD-SKALERING", "HUD ÖLÇEĞİ", "HUD-SKALERING", "HUD SKÁLA", "HUD-SKAALAUS", "TỶ LỆ HUD", "SKALA HUD", "SCALĂ HUD"},
    {"HUD FONT", "HUDフォント", "HUD-SCHRIFT", "FUENTE HUD", "POLICE HUD", "FONT HUD", "HUD-LETTERTYPE", "FONTE HUD", "ШРИФТ HUD", "HUD 字体", "HUD 글꼴", "خط HUD", "FONT HUD", "ШРИФТ HUD", "ΓΡΑΜΜΑΤΟΣΕΙΡΑ HUD", "HUD-TYPSNITT", "ฟอนต์ HUD", "PÍSMO HUD", "HUD-SKRIFT", "HUD YAZI TİPİ", "HUD-SKRIFT", "HUD BETŰ", "HUD-FONTTI", "PHÔNG HUD", "CZCIONKA HUD", "FONT HUD"},
    {"CROSSHAIR", "照準", "VISIER", "PUNTO DE MIRA", "RÉTICULE", "RETICOLO", "RICHTKRUIS", "MIRA", "ПРИЦЕЛ", "准星", "조준선", "التقاطع", "BIDIK", "ПРИЦІЛ", "ΣΤΟΧΕΥΤΗΡΑΣ", "SIKTE", "เล็ง", "ZAMĚŘOVAČ", "SIGTEKORS", "NİŞANGAH", "TRÅDKORS", "IRÁNYZÉK", "TÄHTÄIN", "TÂM NGẮM", "CELOWNIK", "ȚINTĂ"},
    {"HP / AMMO", "HP / 弾薬", "HP / MUNITION", "HP / MUNICIÓN", "PV / MUNITIONS", "HP / MUNIZIONI", "HP / MUNITIE", "HP / MUNIÇÃO", "HP / БОЕПРИПАСЫ", "HP / 弹药", "HP / 탄약", "HP / ذخيرة", "HP / AMUNISI", "HP / БОЄПРИПАСИ", "HP / ΠΥΡΟΜΑΧΙΚΑ", "HP / AMMUNITION", "HP / กระสุน", "HP / MUNICE", "HP / AMMUNITION", "HP / MÜHİMMAT", "HP / AMMUNISJON", "HP / LŐSZER", "HP / AMMUS", "HP / ĐẠN", "HP / AMUNICJA", "HP / MUNIȚIE"},
    {"MATCH STATUS HUD", "試合情報HUD", "SPIELSTAND-HUD", "HUD DE ESTADO DE PARTIDA", "HUD D'ÉTAT DE MATCH", "HUD STATO PARTITA", "WEDSTRIJDSTATUS-HUD", "HUD DE ESTADO DA PARTIDA", "HUD СОСТОЯНИЯ МАТЧА", "比赛状态 HUD", "매치 상태 HUD", "HUD حالة المباراة", "HUD STATUS PERTANDINGAN", "HUD СТАНУ МАТЧУ", "HUD ΚΑΤΑΣΤΑΣΗΣ ΑΓΩΝΑ", "MATCHSTATUS-HUD", "HUD สถานะแมตช์", "HUD STAVU ZÁPASU", "KAMPSTATUS-HUD", "MAÇ DURUMU HUD", "KAMPSTATUS-HUD", "MÉRKŐZÉS-ÁLLAPOT HUD", "OTTELUN TILA-HUD", "HUD TRẠNG THÁI TRẬN", "HUD STANU MECZU", "HUD STARE MECI"},
    {"HUD RADAR", "HUDレーダー", "HUD-RADAR", "RADAR HUD", "RADAR HUD", "RADAR HUD", "HUD-RADAR", "RADAR HUD", "РАДАР HUD", "HUD 雷达", "HUD 레이더", "رادار HUD", "RADAR HUD", "РАДАР HUD", "ΡΑΝΤΑΡ HUD", "HUD-RADAR", "เรดาร์ HUD", "RADAR HUD", "HUD-RADAR", "HUD RADARI", "HUD-RADAR", "HUD RADAR", "HUD-TUTKA", "RADAR HUD", "RADAR HUD", "RADAR HUD"},
    {"IN-GAME OSD COLOR", "ゲーム内OSD色", "OSD-FARBE IM SPIEL", "COLOR OSD EN EL JUEGO", "COULEUR OSD EN JEU", "COLORE OSD IN-GIOCO", "OSD-KLEUR IN SPEL", "COR OSD NO JOGO", "ЦВЕТ OSD В ИГРЕ", "游戏内 OSD 颜色", "게임 내 OSD 색상", "لون OSD داخل اللعبة", "WARNA OSD DALAM GAME", "КОЛІР OSD У ГРІ", "ΧΡΩΜΑ OSD ΣΤΟ ΠΑΙΧΝΙΔΙ", "OSD-FÄRG I SPEL", "สี OSD ในเกม", "BARVA OSD VE HŘE", "OSD-FARVE I SPIL", "OYUN İÇİ OSD RENGİ", "OSD-FARGE I SPILL", "OSD SZÍN A JÁTÉKBAN", "OSD-VÄRI PELISSÄ", "MÀU OSD TRONG GAME", "KOLOR OSD W GRZE", "CULOARE OSD ÎN JOC"},

    // Hotkey page
    {"Keyboard mappings", "キーボード割り当て", "Tastaturbelegung", "Asignación de teclado", "Assignation clavier", "Mappatura tastiera", "Toetsenbordtoewijzingen", "Mapeamento do teclado", "Назначения клавиатуры", "键盘映射", "키보드 매핑", "تعيينات لوحة المفاتيح", "Pemetaan keyboard", "Призначення клавіатури", "Αντιστοιχίσεις πληκτρολογίου", "Tangentbordsmappningar", "การแมปคีย์บอร์ด", "Mapování klávesnice", "Tastaturtilknytninger", "Klavye eşlemeleri", "Tastaturtilordninger", "Billentyűzet-hozzárendelések", "Näppäimistökartoitukset", "Ánh xạ bàn phím", "Mapowanie klawiatury", "Mapări tastatură"},
    {"Keyboard && mouse mappings", "キーボード・マウス割り当て", "Tastatur- und Mausbelegung", "Asignación de teclado y ratón", "Mappage clavier et souris", "Mappature tastiera e mouse", "Toetsenbord- en muisindeling", "Mapeamento de teclado e mouse", "Привязки клавиатуры и мыши", "键盘与鼠标映射", "키보드 및 마우스 매핑", "تعيينات لوحة المفاتيح والفأرة", "Pemetaan keyboard && mouse", "Прив'язки клавіатури && миші", "Αντιστοιχίσεις πληκτρολογίου && ποντικιού", "Tangentbords- && musmappningar", "การแมปคีย์บอร์ด && เมาส์", "Mapování klávesnice && myši", "Tastatur- && musmapping", "Klavye && fare eşlemeleri", "Tastatur- && musmapping", "Billentyűzet && egér hozzárendelések", "Näppäimistön && hiiren määritykset", "Ánh xạ bàn phím && chuột", "Mapowanie klawiatury && myszy", "Mapări tastatură && mouse"},
    {"Joystick mappings", "ジョイスティック割り当て", "Joystick-Zuweisungen", "Asignaciones de joystick", "Assignations manette", "Assegnazioni joystick", "Joystick-toewijzingen", "Mapeamentos de joystick", "Назначения джойстика", "摇杆映射", "조이스틱 매핑", "تعيينات عصا التحكم", "Pemetaan joystick", "Призначення джойстика", "Αντιστοιχίσεις joystick", "Styrkortsinställningar", "การแมปจอยสติก", "Mapování joysticku", "Joystick-tilknytninger", "Joystick eşlemeleri", "Joystick-tilordninger", "Joystick-hozzárendelések", "Ohjaussauvan määritykset", "Ánh xạ joystick", "Mapowania joysticka", "Mapări joystick"},
    {"[Metroid] (W) Move Forward", "[Metroid] (W) 前進", "[Metroid] (W) Vorwärts", "[Metroid] (W) Avanzar", "[Metroid] (W) Avancer", "[Metroid] (W) Avanti", "[Metroid] (W) Vooruit", "[Metroid] (W) Avançar", "[Metroid] (W) Вперёд", "[Metroid] (W) 前进", "[Metroid] (W) 전진", "[Metroid] (W) التقدم للأمام", "[Metroid] (W) Maju", "[Metroid] (W) Вперед", "[Metroid] (W) Μπροστά", "[Metroid] (W) Framåt", "[Metroid] (W) เดินหน้า", "[Metroid] (W) Vpřed", "[Metroid] (W) Fremad", "[Metroid] (W) İleri", "[Metroid] (W) Fremover", "[Metroid] (W) Előre", "[Metroid] (W) Eteenpäin", "[Metroid] (W) Tiến", "[Metroid] (W) Do przodu", "[Metroid] (W) Înainte"},
    {"[Metroid] (S) Move Back", "[Metroid] (S) 後退", "[Metroid] (S) Zurück", "[Metroid] (S) Retroceder", "[Metroid] (S) Reculer", "[Metroid] (S) Indietro", "[Metroid] (S) Achteruit", "[Metroid] (S) Recuar", "[Metroid] (S) Назад", "[Metroid] (S) 后退", "[Metroid] (S) 후진", "[Metroid] (S) الرجوع للخلف", "[Metroid] (S) Mundur", "[Metroid] (S) Назад", "[Metroid] (S) Πίσω", "[Metroid] (S) Backa", "[Metroid] (S) ถอยหลัง", "[Metroid] (S) Zpět", "[Metroid] (S) Tilbage", "[Metroid] (S) Geri git", "[Metroid] (S) Gå tilbake", "[Metroid] (S) Hátrafelé", "[Metroid] (S) Peruuta", "[Metroid] (S) Lùi lại", "[Metroid] (S) Cofnij", "[Metroid] (S) Înapoi"},
    {"[Metroid] (A) Move Left", "[Metroid] (A) 左移動", "[Metroid] (A) Nach links", "[Metroid] (A) Mover a la izquierda", "[Metroid] (A) Aller à gauche", "[Metroid] (A) Muovi a sinistra", "[Metroid] (A) Naar links", "[Metroid] (A) Mover para a esquerda", "[Metroid] (A) Влево", "[Metroid] (A) 向左移动", "[Metroid] (A) 왼쪽 이동", "[Metroid] (A) تحريك لليسار", "[Metroid] (A) Gerak kiri", "[Metroid] (A) Вліво", "[Metroid] (A) Κίνηση αριστερά", "[Metroid] (A) Flytta vänster", "[Metroid] (A) เลื่อนซ้าย", "[Metroid] (A) Pohyb vlevo", "[Metroid] (A) Flyt til venstre", "[Metroid] (A) Sola git", "[Metroid] (A) Flytt til venstre", "[Metroid] (A) Balra mozgatás", "[Metroid] (A) Siirry vasemmalle", "[Metroid] (A) Di chuyển trái", "[Metroid] (A) Ruch w lewo", "[Metroid] (A) Mută stânga"},
    {"[Metroid] (D) Move Right", "[Metroid] (D) 右移動", "[Metroid] (D) Nach rechts", "[Metroid] (D) Mover a la derecha", "[Metroid] (D) Déplacer à droite", "[Metroid] (D) Muovi a destra", "[Metroid] (D) Naar rechts", "[Metroid] (D) Mover para a direita", "[Metroid] (D) Движение вправо", "[Metroid] (D) 向右移动", "[Metroid] (D) 오른쪽 이동", "[Metroid] (D) تحريك لليمين", "[Metroid] (D) Gerak kanan", "[Metroid] (D) Рух вправо", "[Metroid] (D) Μετακίνηση δεξιά", "[Metroid] (D) Flytta höger", "[Metroid] (D) เลื่อนขวา", "[Metroid] (D) Pohyb doprava", "[Metroid] (D) Flyt til højre", "[Metroid] (D) Sağa git", "[Metroid] (D) Flytt til høyre", "[Metroid] (D) Mozgás jobbra", "[Metroid] (D) Liiku oikealle", "[Metroid] (D) Di chuyển sang phải", "[Metroid] (D) Ruch w prawo", "[Metroid] (D) Mișcare la dreapta"},
    {"[Metroid] (Mouse Left) Shoot/Scan", "[Metroid] (マウス左) 射撃/スキャン", "[Metroid] (Linke Maustaste) Schießen/Scannen", "[Metroid] (Clic izquierdo) Disparar/Escanear", "[Metroid] (Clic gauche) Tirer/Scanner", "[Metroid] (Tasto sinistro) Sparare/Scansionare", "[Metroid] (Linkermuisknop) Schieten/Scannen", "[Metroid] (Botão esquerdo) Atirar/Escanear", "[Metroid] (ЛКМ) Стрельба/Сканирование", "[Metroid] (鼠标左键) 射击/扫描", "[Metroid] (마우스 왼쪽) 사격/스캔", "[Metroid] (زر الفأرة الأيسر) إطلاق/مسح", "[Metroid] (Klik kiri) Tembak/Pindai", "[Metroid] (Ліва кнопка миші) Стріляти/Сканувати", "[Metroid] (Αριστερό κλικ) Πυροβολία/Σάρωση", "[Metroid] (Vänster musknapp) Skjut/Skanna", "[Metroid] (คลิกซ้าย) ยิง/สแกน", "[Metroid] (Levé tlačítko myši) Střílet/Skenovat", "[Metroid] (Venstre museknap) Skyd/Scan", "[Metroid] (Sol tık) Ateş et/Tara", "[Metroid] (Venstre museknapp) Skyt/Skann", "[Metroid] (Bal egérgomb) Lövés/Szkennelés", "[Metroid] (Vasen hiiren painike) Ammu/Skannaa", "[Metroid] (Chuột trái) Bắn/Quét", "[Metroid] (Lewy przycisk myszy) Strzelaj/Skanuj", "[Metroid] (Clic stânga) Trage/Scanează"},
    {"[Metroid] (V) Scan/Shoot, Map Zoom In", "[Metroid] (V) スキャン/射撃、マップ拡大", "[Metroid] (V) Scannen/Schießen, Karte vergrößern", "[Metroid] (V) Escanear/Disparar, zoom del mapa", "[Metroid] (V) Scanner/Tirer, zoom carte", "[Metroid] (V) Scansiona/Spara, zoom mappa", "[Metroid] (V) Scannen/Schieten, kaartzoom", "[Metroid] (V) Escanear/Atirar, zoom do mapa", "[Metroid] (V) Сканирование/Стрельба, увеличение карты", "[Metroid] (V) 扫描/射击、地图放大", "[Metroid] (V) 스캔/사격, 맵 확대", "[Metroid] (V) مسح/إطلاق، تكبير الخريطة", "[Metroid] (V) Pindai/Tembak, zoom peta", "[Metroid] (V) Скан/Стріляти, збільшення карти", "[Metroid] (V) Σάρωση/Πυροβολισμός, ζουμ χάρτη", "[Metroid] (V) Skanna/Skjut, kartzoom in", "[Metroid] (V) สแกน/ยิง, ซูมแผนที่", "[Metroid] (V) Sken/Střílet, zoom mapy", "[Metroid] (V) Scan/Skyd, kortzoom ind", "[Metroid] (V) Tara/Ateş et, harita yakınlaştır", "[Metroid] (V) Skann/Skyt, kartzoom inn", "[Metroid] (V) Szkennelés/Lövés, térkép nagyítás", "[Metroid] (V) Skannaa/Ammu, kartan zoomaus", "[Metroid] (V) Quét/Bắn, phóng to bản đồ", "[Metroid] (V) Skanuj/Strzelaj, powiększ mapę", "[Metroid] (V) Scanează/Trage, zoom hartă"},
    {"[Metroid] (Mouse Right) Imperialist Zoom, Map Zoom Out, Morph Ball Boost", "[Metroid] (マウス右) インペリアリストズーム、マップ縮小、モーフボールブースト", "[Metroid] (Rechte Maustaste) Imperialist-Zoom, Karte verkleinern, Morph Ball Boost", "[Metroid] (Clic derecho) Zoom Imperialist, alejar mapa, impulso Morph Ball", "[Metroid] (Clic droit) Zoom Imperialist, dézoom carte, boost Morph Ball", "[Metroid] (Mouse destro) Zoom Imperialist, zoom mappa indietro, Morph Ball Boost", "[Metroid] (Rechtermuisknop) Imperialist-zoom, kaart uitzoomen, Morph Ball Boost", "[Metroid] (Botão direito) Zoom Imperialist, diminuir mapa, impulso Morph Ball", "[Metroid] (ПКМ) Зум Imperialist, отдаление карты, Morph Ball Boost", "[Metroid] (鼠标右键) Imperialist 缩放、地图缩小、变形球加速", "[Metroid] (마우스 오른쪽) Imperialist 줌, 맵 축소, Morph Ball Boost", "[Metroid] (زر الفأرة الأيمن) تكبير Imperialist، تصغير الخريطة، Morph Ball Boost", "[Metroid] (Mouse Kanan) Zoom Imperialist, Perkecil Peta, Morph Ball Boost", "[Metroid] (ПКМ) Зум Imperialist, віддалення карти, Morph Ball Boost", "[Metroid] (Δεξί κλικ) Ζουμ Imperialist, Σμίκρυνση χάρτη, Morph Ball Boost", "[Metroid] (Höger musknapp) Imperialist-zoom, Karta zooma ut, Morph Ball Boost", "[Metroid] (คลิกขวา) ซูม Imperialist, ซูมแผนที่ออก, Morph Ball Boost", "[Metroid] (Pravé tlačítko) Zoom Imperialist, Oddálení mapy, Morph Ball Boost", "[Metroid] (Højre musknap) Imperialist-zoom, Kort zoom ud, Morph Ball Boost", "[Metroid] (Sağ tık) Imperialist zoom, Harita uzaklaştır, Morph Ball Boost", "[Metroid] (Høyre musknapp) Imperialist-zoom, Kart zoom ut, Morph Ball Boost", "[Metroid] (Jobb egérgomb) Imperialist zoom, Térkép kicsinyítés, Morph Ball Boost", "[Metroid] (Hiiren oikea) Imperialist-zoom, Kartan loitontaminen, Morph Ball Boost", "[Metroid] (Chuột phải) Zoom Imperialist, Thu nhỏ bản đồ, Morph Ball Boost", "[Metroid] (PPM) Zoom Imperialist, Pomniejsz mapę, Morph Ball Boost", "[Metroid] (Click dreapta) Zoom Imperialist, Zoom out hartă, Morph Ball Boost"},
    {"[Metroid] (Space) Jump", "[Metroid] (Space) ジャンプ", "[Metroid] (Leertaste) Springen", "[Metroid] (Espacio) Saltar", "[Metroid] (Espace) Sauter", "[Metroid] (Spazio) Salta", "[Metroid] (Spatie) Springen", "[Metroid] (Espaço) Pular", "[Metroid] (Пробел) Прыжок", "[Metroid] (空格) 跳跃", "[Metroid] (스페이스) 점프", "[Metroid] (Space) قفز", "[Metroid] (Space) Lompat", "[Metroid] (Space) Стрибок", "[Metroid] (Space) Άλμα", "[Metroid] (Space) Hoppa", "[Metroid] (Space) กระโดด", "[Metroid] (Space) Skok", "[Metroid] (Space) Hop", "[Metroid] (Space) Zıpla", "[Metroid] (Space) Hopp", "[Metroid] (Space) Ugrás", "[Metroid] (Space) Hyppää", "[Metroid] (Space) Nhảy", "[Metroid] (Space) Skok", "[Metroid] (Space) Săritură"},
    {"[Metroid] (L. Ctrl) Transform", "[Metroid] (左Ctrl) 変形", "[Metroid] (L. Strg) Transformation", "[Metroid] (Ctrl izq.) Transformarse", "[Metroid] (Ctrl G) Transformer", "[Metroid] (Ctrl sin.) Trasformazione", "[Metroid] (L. Ctrl) Transformeren", "[Metroid] (Ctrl esq.) Transformar", "[Metroid] (Л. Ctrl) Трансформация", "[Metroid] (左 Ctrl) 变形", "[Metroid] (왼쪽 Ctrl) 변신", "[Metroid] (Ctrl يس.) تحويل", "[Metroid] (Ctrl Kiri) Transformasi", "[Metroid] (Л. Ctrl) Трансформація", "[Metroid] (Αρ. Ctrl) Μεταμόρφωση", "[Metroid] (V. Ctrl) Transformera", "[Metroid] (Ctrl ซ.) แปลงร่าง", "[Metroid] (Levý Ctrl) Transformace", "[Metroid] (V. Ctrl) Transformér", "[Metroid] (Sol Ctrl) Dönüşüm", "[Metroid] (V. Ctrl) Transformer", "[Metroid] (Bal Ctrl) Átalakulás", "[Metroid] (Vas. Ctrl) Muunnos", "[Metroid] (Ctrl trái) Biến hình", "[Metroid] (L. Ctrl) Transformacja", "[Metroid] (Ctrl st.) Transformare"},
    {"[Metroid] (Shift) Hold to Fast Morph Ball Boost", "[Metroid] (Shift) 長押しで高速モーフボールブースト", "[Metroid] (Shift) Gedrückt halten für schnellen Morph Ball Boost", "[Metroid] (Mayús) Mantener para impulso Morph Ball rápido", "[Metroid] (Maj) Maintenir pour boost Morph Ball rapide", "[Metroid] (Shift) Tieni premuto per Morph Ball Boost veloce", "[Metroid] (Shift) Ingedrukt houden voor snelle Morph Ball Boost", "[Metroid] (Shift) Manter pressionado para impulso Morph Ball rápido", "[Metroid] (Shift) Удерживать для быстрого Morph Ball Boost", "[Metroid] (Shift) 按住以快速变形球加速", "[Metroid] (Shift) 길게 눌러 빠른 Morph Ball 부스트", "[Metroid] (Shift) اضغط مطولاً لتعزيز Morph Ball السريع", "[Metroid] (Shift) Tahan untuk Morph Ball Boost cepat", "[Metroid] (Shift) Утримуйте для швидкого Morph Ball Boost", "[Metroid] (Shift) Κράτησε για γρήγορο Morph Ball Boost", "[Metroid] (Shift) Håll för snabb Morph Ball Boost", "[Metroid] (Shift) กดค้างเพื่อ Morph Ball Boost เร็ว", "[Metroid] (Shift) Podržte pro rychlý Morph Ball Boost", "[Metroid] (Shift) Hold for hurtig Morph Ball Boost", "[Metroid] (Shift) Hızlı Morph Ball Boost için basılı tut", "[Metroid] (Shift) Hold for rask Morph Ball Boost", "[Metroid] (Shift) Tartsd nyomva a gyors Morph Ball Boosthoz", "[Metroid] (Shift) Pidä pohjassa nopeaa Morph Ball Boostia varten", "[Metroid] (Shift) Giữ để Morph Ball Boost nhanh", "[Metroid] (Shift) Przytrzymaj dla szybkiego Morph Ball Boost", "[Metroid] (Shift) Ține apăsat pentru Morph Ball Boost rapid"},
    {"[Metroid] (Mouse 5, Side Top) Weapon Beam", "[Metroid] (Mouse 5/サイド上) ビーム武器", "[Metroid] (Maus 5, Seitentaste oben) Strahlenwaffe", "[Metroid] (Ratón 5, lateral superior) Rayo", "[Metroid] (Souris 5, côté haut) Rayon", "[Metroid] (Mouse 5, lato superiore) Raggio", "[Metroid] (Muis 5, bovenste zijkant) Straal", "[Metroid] (Mouse 5, lateral superior) Raio", "[Metroid] (Мышь 5, верхняя боковая) Луч", "[Metroid] (Mouse 5/侧键上) 光束武器", "[Metroid] (마우스 5, 측면 상단) 빔", "[Metroid] (زر الماوس 5، الجانب العلوي) شعاع السلاح", "[Metroid] (Mouse 5, sisi atas) Senjata Beam", "[Metroid] (Миша 5, верхня бічна) Промінь", "[Metroid] (Ποντίκι 5, πάνω πλευρά) Ακτίνα", "[Metroid] (Mus 5, övre sidoknapp) Strålvapen", "[Metroid] (เมาส์ 5, ด้านบน) อาวุธ Beam", "[Metroid] (Myš 5, horní boční) Paprsek", "[Metroid] (Mus 5, øverste side) Strålevåben", "[Metroid] (Fare 5, üst yan) Işın silahı", "[Metroid] (Mus 5, øvre side) Strålevåpen", "[Metroid] (Egér 5, felső oldal) Sugár fegyver", "[Metroid] (Hiiri 5, yläpuoli) Sädease", "[Metroid] (Chuột 5, cạnh trên) Vũ khí Beam", "[Metroid] (Mysz 5, górny bok) Promień", "[Metroid] (Mouse 5, lateral sus) Rază"},
    {"[Metroid] (Mouse 4, Side Bottom) Weapon Missile", "[Metroid] (Mouse 4/サイド下) ミサイル", "[Metroid] (Maustaste 4, Seitentaste unten) Waffe Rakete", "[Metroid] (Ratón 4, lateral inferior) Arma misil", "[Metroid] (Souris 4, latéral bas) Arme missile", "[Metroid] (Mouse 4, laterale inferiore) Arma missile", "[Metroid] (Muis 4, onderste zijknop) Wapen raket", "[Metroid] (Mouse 4, lateral inferior) Arma míssil", "[Metroid] (Кнопка 4, нижняя боковая) Оружие ракета", "[Metroid] (鼠标 4/侧键下) 导弹武器", "[Metroid] (마우스 4/측면 하단) 미사일 무기", "[Metroid] (Mouse 4، الجانب السفلي) سلاح Missile", "[Metroid] (Mouse 4, Sisi Bawah) Senjata Missile", "[Metroid] (Кнопка 4, нижня бокова) Зброя Missile", "[Metroid] (Mouse 4, Κάτω πλευρά) Όπλο Missile", "[Metroid] (Mus 4, Undersida) Vapen Missile", "[Metroid] (Mouse 4, ด้านล่าง) อาวุธ Missile", "[Metroid] (Myš 4, Spodní strana) Zbraň Missile", "[Metroid] (Mus 4, Side nederst) Våben Missile", "[Metroid] (Mouse 4, Alt yan) Silah Missile", "[Metroid] (Mus 4, Side nederst) Våpen Missile", "[Metroid] (Egér 4, Alsó oldal) Fegyver Missile", "[Metroid] (Hiiri 4, Alareuna) Ase Missile", "[Metroid] (Mouse 4, Cạnh dưới) Vũ khí Missile", "[Metroid] (Mysz 4, Dolny bok) Broń Missile", "[Metroid] (Mouse 4, Lateral jos) Armă Missile"},
    {"[Metroid] (1) Weapon 1. ShockCoil", "[Metroid] (1) 武器1: ショックコイル", "[Metroid] (1) Waffe 1: ShockCoil", "[Metroid] (1) Arma 1: ShockCoil", "[Metroid] (1) Arme 1 : ShockCoil", "[Metroid] (1) Arma 1: ShockCoil", "[Metroid] (1) Wapen 1: ShockCoil", "[Metroid] (1) Arma 1: ShockCoil", "[Metroid] (1) Оружие 1: ShockCoil", "[Metroid] (1) 武器 1：ShockCoil", "[Metroid] (1) 무기 1: ShockCoil", "[Metroid] (1) سلاح 1: ShockCoil", "[Metroid] (1) Senjata 1: ShockCoil", "[Metroid] (1) Зброя 1: ShockCoil", "[Metroid] (1) Όπλο 1: ShockCoil", "[Metroid] (1) Vapen 1: ShockCoil", "[Metroid] (1) อาวุธ 1: ShockCoil", "[Metroid] (1) Zbraň 1: ShockCoil", "[Metroid] (1) Våben 1: ShockCoil", "[Metroid] (1) Silah 1: ShockCoil", "[Metroid] (1) Våpen 1: ShockCoil", "[Metroid] (1) Fegyver 1: ShockCoil", "[Metroid] (1) Ase 1: ShockCoil", "[Metroid] (1) Vũ khí 1: ShockCoil", "[Metroid] (1) Broń 1: ShockCoil", "[Metroid] (1) Armă 1: ShockCoil"},
    {"[Metroid] (2) Weapon 2. Magmaul", "[Metroid] (2) 武器2: マグモール", "[Metroid] (2) Waffe 2: Magmaul", "[Metroid] (2) Arma 2: Magmaul", "[Metroid] (2) Arme 2: Magmaul", "[Metroid] (2) Arma 2: Magmaul", "[Metroid] (2) Wapen 2: Magmaul", "[Metroid] (2) Arma 2: Magmaul", "[Metroid] (2) Оружие 2: Magmaul", "[Metroid] (2) 武器 2：Magmaul", "[Metroid] (2) 무기 2: Magmaul", "[Metroid] (2) سلاح 2: Magmaul", "[Metroid] (2) Senjata 2: Magmaul", "[Metroid] (2) Зброя 2: Magmaul", "[Metroid] (2) Όπλο 2: Magmaul", "[Metroid] (2) Vapen 2: Magmaul", "[Metroid] (2) อาวุธ 2: Magmaul", "[Metroid] (2) Zbraň 2: Magmaul", "[Metroid] (2) Våben 2: Magmaul", "[Metroid] (2) Silah 2: Magmaul", "[Metroid] (2) Våpen 2: Magmaul", "[Metroid] (2) Fegyver 2: Magmaul", "[Metroid] (2) Ase 2: Magmaul", "[Metroid] (2) Vũ khí 2: Magmaul", "[Metroid] (2) Broń 2: Magmaul", "[Metroid] (2) Armă 2: Magmaul"},
    {"[Metroid] (3) Weapon 3. Judicator", "[Metroid] (3) 武器3: ジュディケイター", "[Metroid] (3) Waffe 3: Judicator", "[Metroid] (3) Arma 3: Judicator", "[Metroid] (3) Arme 3 : Judicator", "[Metroid] (3) Arma 3: Judicator", "[Metroid] (3) Wapen 3: Judicator", "[Metroid] (3) Arma 3: Judicator", "[Metroid] (3) Оружие 3: Judicator", "[Metroid] (3) 武器 3：Judicator", "[Metroid] (3) 무기 3: Judicator", "[Metroid] (3) السلاح 3: Judicator", "[Metroid] (3) Senjata 3: Judicator", "[Metroid] (3) Зброя 3: Judicator", "[Metroid] (3) Όπλο 3: Judicator", "[Metroid] (3) Vapen 3: Judicator", "[Metroid] (3) อาวุธ 3: Judicator", "[Metroid] (3) Zbraň 3: Judicator", "[Metroid] (3) Våben 3: Judicator", "[Metroid] (3) Silah 3: Judicator", "[Metroid] (3) Våpen 3: Judicator", "[Metroid] (3) Fegyver 3: Judicator", "[Metroid] (3) Ase 3: Judicator", "[Metroid] (3) Vũ khí 3: Judicator", "[Metroid] (3) Broń 3: Judicator", "[Metroid] (3) Armă 3: Judicator"},
    {"[Metroid] (4) Weapon 4. Imperialist", "[Metroid] (4) 武器4: インペリアリスト", "[Metroid] (4) Waffe 4: Imperialist", "[Metroid] (4) Arma 4: Imperialist", "[Metroid] (4) Arme 4 : Imperialist", "[Metroid] (4) Arma 4: Imperialist", "[Metroid] (4) Wapen 4: Imperialist", "[Metroid] (4) Arma 4: Imperialist", "[Metroid] (4) Оружие 4: Imperialist", "[Metroid] (4) 武器4: Imperialist", "[Metroid] (4) 무기 4: Imperialist", "[Metroid] (4) السلاح 4: Imperialist", "[Metroid] (4) Senjata 4: Imperialist", "[Metroid] (4) Зброя 4: Imperialist", "[Metroid] (4) Όπλο 4: Imperialist", "[Metroid] (4) Vapen 4: Imperialist", "[Metroid] (4) อาวุธ 4: Imperialist", "[Metroid] (4) Zbraň 4: Imperialist", "[Metroid] (4) Våben 4: Imperialist", "[Metroid] (4) Silah 4: Imperialist", "[Metroid] (4) Våpen 4: Imperialist", "[Metroid] (4) Fegyver 4: Imperialist", "[Metroid] (4) Ase 4: Imperialist", "[Metroid] (4) Vũ khí 4: Imperialist", "[Metroid] (4) Broń 4: Imperialist", "[Metroid] (4) Armă 4: Imperialist"},
    {"[Metroid] (5) Weapon 5. Battlehammer", "[Metroid] (5) 武器5: バトルハンマー", "[Metroid] (5) Waffe 5: Battlehammer", "[Metroid] (5) Arma 5: Battlehammer", "[Metroid] (5) Arme 5 : Battlehammer", "[Metroid] (5) Arma 5: Battlehammer", "[Metroid] (5) Wapen 5: Battlehammer", "[Metroid] (5) Arma 5: Battlehammer", "[Metroid] (5) Оружие 5: Battlehammer", "[Metroid] (5) 武器 5：Battlehammer", "[Metroid] (5) 무기 5: Battlehammer", "[Metroid] (5) سلاح 5. Battlehammer", "[Metroid] (5) Senjata 5. Battlehammer", "[Metroid] (5) Зброя 5. Battlehammer", "[Metroid] (5) Όπλο 5. Battlehammer", "[Metroid] (5) Vapen 5. Battlehammer", "[Metroid] (5) อาวุธ 5. Battlehammer", "[Metroid] (5) Zbraň 5. Battlehammer", "[Metroid] (5) Våben 5. Battlehammer", "[Metroid] (5) Silah 5. Battlehammer", "[Metroid] (5) Våpen 5. Battlehammer", "[Metroid] (5) Fegyver 5. Battlehammer", "[Metroid] (5) Ase 5. Battlehammer", "[Metroid] (5) Vũ khí 5. Battlehammer", "[Metroid] (5) Broń 5. Battlehammer", "[Metroid] (5) Armă 5. Battlehammer"},
    {"[Metroid] (6) Weapon 6. VoltDriver", "[Metroid] (6) 武器6: ボルトドライバー", "[Metroid] (6) Waffe 6: VoltDriver", "[Metroid] (6) Arma 6: VoltDriver", "[Metroid] (6) Arme 6 : VoltDriver", "[Metroid] (6) Arma 6: VoltDriver", "[Metroid] (6) Wapen 6: VoltDriver", "[Metroid] (6) Arma 6: VoltDriver", "[Metroid] (6) Оружие 6: VoltDriver", "[Metroid] (6) 武器 6：VoltDriver", "[Metroid] (6) 무기 6: VoltDriver", "[Metroid] (6) سلاح 6: VoltDriver", "[Metroid] (6) Senjata 6: VoltDriver", "[Metroid] (6) Зброя 6: VoltDriver", "[Metroid] (6) Όπλο 6: VoltDriver", "[Metroid] (6) Vapen 6: VoltDriver", "[Metroid] (6) อาวุธ 6: VoltDriver", "[Metroid] (6) Zbraň 6: VoltDriver", "[Metroid] (6) Våben 6: VoltDriver", "[Metroid] (6) Silah 6: VoltDriver", "[Metroid] (6) Våpen 6: VoltDriver", "[Metroid] (6) Fegyver 6: VoltDriver", "[Metroid] (6) Ase 6: VoltDriver", "[Metroid] (6) Vũ khí 6: VoltDriver", "[Metroid] (6) Broń 6: VoltDriver", "[Metroid] (6) Armă 6: VoltDriver"},
    {"[Metroid] (R) Affinity Weapon (Last used Weapon/Omega cannon)", "[Metroid] (R) 得意武器 (最後の武器/オメガキャノン)", "[Metroid] (R) Lieblingswaffe (Letzte Waffe/Omega-Kanone)", "[Metroid] (R) Arma preferida (Última arma/Cañón Omega)", "[Metroid] (R) Arme favorite (Dernière arme/Canon Omega)", "[Metroid] (R) Arma preferita (Ultima arma/Canon Omega)", "[Metroid] (R) Favoriet wapen (Laatst gebruikt/Omega-kanon)", "[Metroid] (R) Arma preferida (Última arma/Canhão Omega)", "[Metroid] (R) Любимое оружие (Последнее оружие/Пушка Омега)", "[Metroid] (R) 擅长武器（上次使用的武器/欧米加炮）", "[Metroid] (R) 특화 무기 (마지막 무기/오메가 캐논)", "[Metroid] (R) سلاح التخصص (آخر سلاح/Omega cannon)", "[Metroid] (R) Senjata Affinity (Senjata terakhir/Omega cannon)", "[Metroid] (R) Улюблена зброя (Остання зброя/Omega cannon)", "[Metroid] (R) Όπλο Affinity (Τελευταίο όπλο/Omega cannon)", "[Metroid] (R) Affinity-vapen (Senast använda/Omega cannon)", "[Metroid] (R) อาวุธ Affinity (อาวุธล่าสุด/Omega cannon)", "[Metroid] (R) Oblíbená zbraň (Poslední zbraň/Omega cannon)", "[Metroid] (R) Affinity-våben (Sidst brugte/Omega cannon)", "[Metroid] (R) Affinity Silahı (Son silah/Omega cannon)", "[Metroid] (R) Affinity-våpen (Sist brukte/Omega cannon)", "[Metroid] (R) Affinity fegyver (Utolsó fegyver/Omega cannon)", "[Metroid] (R) Affinity-ase (Viimeisin ase/Omega cannon)", "[Metroid] (R) Vũ khí Affinity (Vũ khí cuối/Omega cannon)", "[Metroid] (R) Broń Affinity (Ostatnia broń/Omega cannon)", "[Metroid] (R) Armă Affinity (Ultima armă/Omega cannon)"},
    {"[Metroid] (Tab) Menu/Map", "[Metroid] (Tab) メニュー/マップ", "[Metroid] (Tab) Menü/Karte", "[Metroid] (Tab) Menú/Mapa", "[Metroid] (Tab) Menu/Carte", "[Metroid] (Tab) Menu/Mappa", "[Metroid] (Tab) Menu/Kaart", "[Metroid] (Tab) Menu/Mapa", "[Metroid] (Tab) Меню/Карта", "[Metroid] (Tab) 菜单/地图", "[Metroid] (Tab) 메뉴/맵", "[Metroid] (Tab) القائمة/الخريطة", "[Metroid] (Tab) Menu/Peta", "[Metroid] (Tab) Меню/Карта", "[Metroid] (Tab) Μενού/Χάρτης", "[Metroid] (Tab) Meny/Karta", "[Metroid] (Tab) เมนู/แผนที่", "[Metroid] (Tab) Menu/Mapa", "[Metroid] (Tab) Menu/Kort", "[Metroid] (Tab) Menü/Harita", "[Metroid] (Tab) Meny/Kart", "[Metroid] (Tab) Menü/Térkép", "[Metroid] (Tab) Valikko/Kartta", "[Metroid] (Tab) Menu/Bản đồ", "[Metroid] (Tab) Menu/Mapa", "[Metroid] (Tab) Meniu/Hartă"},
    {"[Metroid] (PgUp) AimSensitivity Up", "[Metroid] (PgUp) エイム感度を上げる", "[Metroid] (Bild auf) Ziel-Empfindlichkeit erhöhen", "[Metroid] (RePág) Subir sensibilidad de puntería", "[Metroid] (PgPréc) Augmenter sensibilité visée", "[Metroid] (PgSu) Aumenta sensibilità mira", "[Metroid] (PgUp) Richtingsgevoeligheid verhogen", "[Metroid] (PgUp) Aumentar sensibilidade de mira", "[Metroid] (PgUp) Повысить чувствительность прицеливания", "[Metroid] (PgUp) 提高瞄准灵敏度", "[Metroid] (PgUp) 에임 감도 올리기", "[Metroid] (PgUp) زيادة حساسية التصويب", "[Metroid] (PgUp) Naikkan sensitivitas bidik", "[Metroid] (PgUp) Збільшити чутливість прицілу", "[Metroid] (PgUp) Αύξηση ευαισθησίας σκόπευσης", "[Metroid] (PgUp) Öka siktkänslighet", "[Metroid] (PgUp) เพิ่มความไวการเล็ง", "[Metroid] (PgUp) Zvýšit citlivost míření", "[Metroid] (PgUp) Øg sigtekänslighed", "[Metroid] (PgUp) Nişan hassasiyetini artır", "[Metroid] (PgUp) Øk siktekänslighet", "[Metroid] (PgUp) Célzás érzékenység növelése", "[Metroid] (PgUp) Lisää tähtäyksen herkkyyttä", "[Metroid] (PgUp) Tăng độ nhạy ngắm", "[Metroid] (PgUp) Zwiększ czułość celowania", "[Metroid] (PgUp) Crește sensibilitatea țintirii"},
    {"[Metroid] (PgDown) AimSensitivity Down", "[Metroid] (PgDown) エイム感度を下げる", "[Metroid] (PgDown) Ziel-Empfindlichkeit verringern", "[Metroid] (RePág) Reducir sensibilidad de puntería", "[Metroid] (PgBas) Diminuer la sensibilité de visée", "[Metroid] (PgDown) Riduci sensibilità mira", "[Metroid] (PgDown) Richtgevoeligheid verlagen", "[Metroid] (PgDown) Diminuir sensibilidade de mira", "[Metroid] (PgDown) Уменьшить чувствительность прицела", "[Metroid] (PgDown) 降低瞄准灵敏度", "[Metroid] (PgDown) 조준 감도 낮추기", "[Metroid] (PgDown) خفض حساسية التصويب", "[Metroid] (PgDown) Turunkan Sensitivitas Bidik", "[Metroid] (PgDown) Зменшити чутливість прицілу", "[Metroid] (PgDown) Μείωση ευαισθησίας σκόπευσης", "[Metroid] (PgDown) Minska siktkänslighet", "[Metroid] (PgDown) ลดความไวการเล็ง", "[Metroid] (PgDown) Snížit citlivost míření", "[Metroid] (PgDown) Sænk sigtekänslighed", "[Metroid] (PgDown) Nişan hassasiyetini azalt", "[Metroid] (PgDown) Senk siktekänslighet", "[Metroid] (PgDown) Célzási érzékenység csökkentése", "[Metroid] (PgDown) Pienennä tähtäyksen herkkyyttä", "[Metroid] (PgDown) Giảm độ nhạy ngắm", "[Metroid] (PgDown) Zmniejsz czułość celowania", "[Metroid] (PgDown) Scade sensibilitatea țintirii"},
    {"[Metroid] (J) Next Weapon in the sorted order", "[Metroid] (J) 次の武器", "[Metroid] (J) Nächste Waffe", "[Metroid] (J) Siguiente arma", "[Metroid] (J) Arme suivante", "[Metroid] (J) Arma successiva", "[Metroid] (J) Volgend wapen", "[Metroid] (J) Próxima arma", "[Metroid] (J) Следующее оружие", "[Metroid] (J) 下一武器", "[Metroid] (J) 다음 무기", "[Metroid] (J) السلاح التالي", "[Metroid] (J) Senjata berikutnya", "[Metroid] (J) Наступна зброя", "[Metroid] (J) Επόμενο όπλο", "[Metroid] (J) Nästa vapen", "[Metroid] (J) อาวุธถัดไป", "[Metroid] (J) Další zbraň", "[Metroid] (J) Næste våben", "[Metroid] (J) Sonraki silah", "[Metroid] (J) Neste våpen", "[Metroid] (J) Következő fegyver", "[Metroid] (J) Seuraava ase", "[Metroid] (J) Vũ khí tiếp theo", "[Metroid] (J) Następna broń", "[Metroid] (J) Următoarea armă"},
    {"[Metroid] (K) Previous Weapon in the sorted order", "[Metroid] (K) 前の武器", "[Metroid] (K) Vorherige Waffe (Sortierreihenfolge)", "[Metroid] (K) Arma anterior (orden)", "[Metroid] (K) Arme précédente (ordre trié)", "[Metroid] (K) Arma precedente (ordine)", "[Metroid] (K) Vorig wapen (sorteervolgorde)", "[Metroid] (K) Arma anterior (ordem)", "[Metroid] (K) Предыдущее оружие (порядок сортировки)", "[Metroid] (K) 上一把武器（排序顺序）", "[Metroid] (K) 이전 무기 (정렬 순서)", "[Metroid] (K) السلاح السابق (ترتيب مرتب)", "[Metroid] (K) Senjata sebelumnya (urutan terurut)", "[Metroid] (K) Попередня зброя (відсортований порядок)", "[Metroid] (K) Προηγούμενο όπλο (ταξινομημένη σειρά)", "[Metroid] (K) Föregående vapen (sorterad ordning)", "[Metroid] (K) อาวุธก่อนหน้า (ลำดับที่เรียง)", "[Metroid] (K) Předchozí zbraň (seřazené pořadí)", "[Metroid] (K) Forrige våben (sorteret rækkefølge)", "[Metroid] (K) Önceki silah (sıralı düzen)", "[Metroid] (K) Forrige våpen (sortert rekkefølge)", "[Metroid] (K) Előző fegyver (rendezett sorrend)", "[Metroid] (K) Edellinen ase (lajiteltu järjestys)", "[Metroid] (K) Vũ khí trước (thứ tự đã sắp)", "[Metroid] (K) Poprzednia broń (kolejność sortowania)", "[Metroid] (K) Arma anterioară (ordine sortată)"},
    {"[Metroid] (C) Scan Visor", "[Metroid] (C) スキャンバイザー", "[Metroid] (C) Scan-Visor", "[Metroid] (C) Visor de escaneo", "[Metroid] (C) Visor de scan", "[Metroid] (C) Visore di scansione", "[Metroid] (C) Scanvisor", "[Metroid] (C) Visor de escaneamento", "[Metroid] (C) Сканирующий визор", "[Metroid] (C) 扫描面罩", "[Metroid] (C) 스캔 바이저", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor", "[Metroid] (C) Scan Visor"},
    {"[Metroid] (Z) UI Left (Adventure Left Arrow / Hunter License L)", "[Metroid] (Z) UI左 (アドベンチャー左/ライセンスL)", "[Metroid] (Z) UI links (Abenteuer-Pfeil links / Hunter-Lizenz L)", "[Metroid] (Z) UI izquierda (flecha izquierda Aventura / Licencia Cazador L)", "[Metroid] (Z) UI gauche (flèche gauche Aventure / Licence Chasseur L)", "[Metroid] (Z) UI sinistra (freccia sinistra Avventura / Licenza Cacciatore L)", "[Metroid] (Z) UI links (Avontuur pijl links / Hunter-licentie L)", "[Metroid] (Z) UI esquerda (seta esquerda Aventura / Licença Caçador L)", "[Metroid] (Z) UI влево (стрелка влево Приключение / Лицензия Охотника L)", "[Metroid] (Z) UI左 (冒险左箭头/猎人执照L)", "[Metroid] (Z) UI 왼쪽 (어드벤처 왼쪽 화살표 / 헌터 라이선스 L)", "[Metroid] (Z) واجهة يسار (سهم يسار Adventure / Hunter License L)", "[Metroid] (Z) UI kiri (Panah kiri Adventure / Hunter License L)", "[Metroid] (Z) UI ліворуч (Стрілка ліворуч Adventure / Hunter License L)", "[Metroid] (Z) UI αριστερά (Βέλος αριστερά Adventure / Hunter License L)", "[Metroid] (Z) UI vänster (Adventure vänsterpil / Hunter License L)", "[Metroid] (Z) UI ซ้าย (ลูกศรซ้าย Adventure / Hunter License L)", "[Metroid] (Z) UI vlevo (Adventure šipka vlevo / Hunter License L)", "[Metroid] (Z) UI venstre (Adventure venstre pil / Hunter License L)", "[Metroid] (Z) UI sol (Adventure sol ok / Hunter License L)", "[Metroid] (Z) UI venstre (Adventure venstrepil / Hunter License L)", "[Metroid] (Z) UI bal (Adventure bal nyíl / Hunter License L)", "[Metroid] (Z) UI vasen (Adventure vasen nuoli / Hunter License L)", "[Metroid] (Z) UI trái (Mũi tên trái Adventure / Hunter License L)", "[Metroid] (Z) UI lewo (Strzałka w lewo Adventure / Hunter License L)", "[Metroid] (Z) UI stânga (Săgeată stânga Adventure / Hunter License L)"},
    {"[Metroid] (X) UI Right (Adventure Right Arrow / Hunter License R)", "[Metroid] (X) UI右 (アドベンチャー右/ライセンスR)", "[Metroid] (X) UI rechts (Abenteuer-Pfeil rechts / Hunter-Lizenz R)", "[Metroid] (X) UI derecha (Flecha derecha aventura / Licencia Hunter R)", "[Metroid] (X) UI droite (Flèche droite aventure / Licence Hunter R)", "[Metroid] (X) UI destra (Freccia destra avventura / Licenza Hunter R)", "[Metroid] (X) UI rechts (Avontuur pijl rechts / Hunter-licentie R)", "[Metroid] (X) UI direita (Seta direita aventura / Licença Hunter R)", "[Metroid] (X) UI вправо (Стрелка вправо приключения / Лицензия Hunter R)", "[Metroid] (X) UI 右 (冒险右/猎人许可证 R)", "[Metroid] (X) UI 오른쪽 (어드벤처 오른쪽/헌터 라이선스 R)", "[Metroid] (X) واجهة يمين (سهم المغامرة يمين / ترخيص Hunter R)", "[Metroid] (X) UI Kanan (Panah Kanan Adventure / Lisensi Hunter R)", "[Metroid] (X) UI праворуч (Стрілка вправо пригоди / Ліцензія Hunter R)", "[Metroid] (X) UI Δεξιά (Βέλος δεξιά περιπέτειας / Άδεια Hunter R)", "[Metroid] (X) UI höger (Äventyr pil höger / Hunter-licens R)", "[Metroid] (X) UI ขวา (ลูกศรขวา Adventure / ใบอนุญาต Hunter R)", "[Metroid] (X) UI vpravo (Šipka vpravo dobrodružství / Licence Hunter R)", "[Metroid] (X) UI højre (Eventyr pil højre / Hunter-licens R)", "[Metroid] (X) UI Sağ (Macera sağ ok / Hunter lisansı R)", "[Metroid] (X) UI høyre (Eventyr pil høyre / Hunter-lisens R)", "[Metroid] (X) UI jobbra (Kaland jobb nyíl / Hunter licenc R)", "[Metroid] (X) UI oikea (Seikkailu oikea nuoli / Hunter-lisenssi R)", "[Metroid] (X) UI phải (Mũi tên phải Adventure / Giấy phép Hunter R)", "[Metroid] (X) UI w prawo (Strzałka w prawo przygody / Licencja Hunter R)", "[Metroid] (X) UI dreapta (Săgeată dreapta aventură / Licență Hunter R)"},
    {"[Metroid] (F) UI Ok", "[Metroid] (F) UI決定", "[Metroid] (F) UI Bestätigen", "[Metroid] (F) Confirmar UI", "[Metroid] (F) Valider UI", "[Metroid] (F) Conferma UI", "[Metroid] (F) UI bevestigen", "[Metroid] (F) Confirmar UI", "[Metroid] (F) Подтвердить UI", "[Metroid] (F) UI 确定", "[Metroid] (F) UI 확인", "[Metroid] (F) تأكيد UI", "[Metroid] (F) UI OK", "[Metroid] (F) Підтвердити UI", "[Metroid] (F) Επιβεβαίωση UI", "[Metroid] (F) UI OK", "[Metroid] (F) UI ตกลง", "[Metroid] (F) Potvrdit UI", "[Metroid] (F) UI OK", "[Metroid] (F) UI Tamam", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK"},
    {"[Metroid] (G) UI Yes (Enter Starship)", "[Metroid] (G) UIはい (スターシップに入る)", "[Metroid] (G) UI Ja (Raumschiff betreten)", "[Metroid] (G) UI Sí (Entrar en nave)", "[Metroid] (G) UI Oui (Entrer dans le vaisseau)", "[Metroid] (G) UI Sì (Entra nella nave)", "[Metroid] (G) UI Ja (Ruimteschip betreden)", "[Metroid] (G) UI Sim (Entrar na nave)", "[Metroid] (G) UI Да (Войти в корабль)", "[Metroid] (G) UI 是（进入星舰）", "[Metroid] (G) UI 예 (우주선 진입)", "[Metroid] (G) واجهة نعم (دخول Starship)", "[Metroid] (G) UI Ya (Masuk Starship)", "[Metroid] (G) UI Так (Увійти в Starship)", "[Metroid] (G) UI Ναι (Είσοδος Starship)", "[Metroid] (G) UI Ja (Gå in i Starship)", "[Metroid] (G) UI ใช่ (เข้า Starship)", "[Metroid] (G) UI Ano (Vstoupit do Starship)", "[Metroid] (G) UI Ja (Gå ind i Starship)", "[Metroid] (G) UI Evet (Starship'e gir)", "[Metroid] (G) UI Ja (Gå inn i Starship)", "[Metroid] (G) UI Igen (Starship belépés)", "[Metroid] (G) UI Kyllä (Astu Starshipiin)", "[Metroid] (G) UI Có (Vào Starship)", "[Metroid] (G) UI Tak (Wejdź do Starship)", "[Metroid] (G) UI Da (Intră în Starship)"},
    {"[Metroid] (H) UI No (Enter Starship)", "[Metroid] (H) UIいいえ (スターシップに入る)", "[Metroid] (H) UI Nein (Raumschiff betreten)", "[Metroid] (H) UI No (Entrar a la nave)", "[Metroid] (H) UI Non (Entrer dans le vaisseau)", "[Metroid] (H) UI No (Entra nella nave)", "[Metroid] (H) UI Nee (Ruimteschip betreden)", "[Metroid] (H) UI Não (Entrar na nave)", "[Metroid] (H) UI Нет (Войти в корабль)", "[Metroid] (H) UI 否 (进入星舰)", "[Metroid] (H) UI 아니오 (우주선 진입)", "[Metroid] (H) UI لا (دخول السفينة)", "[Metroid] (H) UI Tidak (Masuk kapal)", "[Metroid] (H) UI Ні (Увійти в корабель)", "[Metroid] (H) UI Όχι (Είσοδος στο σκάφος)", "[Metroid] (H) UI Nej (Gå ombord)", "[Metroid] (H) UI ไม่ (เข้ายาน)", "[Metroid] (H) UI Ne (Vstoupit do lodi)", "[Metroid] (H) UI Nej (Gå ombord)", "[Metroid] (H) UI Hayır (Gemiye gir)", "[Metroid] (H) UI Nei (Gå om bord)", "[Metroid] (H) UI Nem (Belépés a hajóba)", "[Metroid] (H) UI Ei (Mene alukseen)", "[Metroid] (H) UI Không (Vào tàu)", "[Metroid] (H) UI Nie (Wejdź do statku)", "[Metroid] (H) UI Nu (Intră în navă)"},
    {"[Metroid] (Y) Weapon Check", "[Metroid] (Y) 武器確認", "[Metroid] (Y) Waffe prüfen", "[Metroid] (Y) Verificar arma", "[Metroid] (Y) Vérifier arme", "[Metroid] (Y) Verifica arma", "[Metroid] (Y) Wapen controleren", "[Metroid] (Y) Verificar arma", "[Metroid] (Y) Проверить оружие", "[Metroid] (Y) 武器确认", "[Metroid] (Y) 무기 확인", "[Metroid] (Y) فحص السلاح", "[Metroid] (Y) Periksa senjata", "[Metroid] (Y) Перевірка зброї", "[Metroid] (Y) Έλεγχος όπλου", "[Metroid] (Y) Vapenkontroll", "[Metroid] (Y) ตรวจอาวุธ", "[Metroid] (Y) Kontrola zbraně", "[Metroid] (Y) Våbentjek", "[Metroid] (Y) Silah kontrolü", "[Metroid] (Y) Våpensjekk", "[Metroid] (Y) Fegyver ellenőrzés", "[Metroid] (Y) Aseen tarkistus", "[Metroid] (Y) Kiểm tra vũ khí", "[Metroid] (Y) Sprawdź broń", "[Metroid] (Y) Verifică arma"},

    // General Metroid settings
    {"MPH Sensitivity (default: -3)", "MPH感度 (既定: -3)", "MPH-Empfindlichkeit (Standard: -3)", "Sensibilidad MPH (predeterminado: -3)", "Sensibilité MPH (par défaut : -3)", "Sensibilità MPH (predefinito: -3)", "MPH-gevoeligheid (standaard: -3)", "Sensibilidade MPH (padrão: -3)", "Чувствительность MPH (по умолчанию: -3)", "MPH 灵敏度 (默认: -3)", "MPH 감도 (기본값: -3)", "حساسية MPH (افتراضي: -3)", "Sensitivitas MPH (default: -3)", "Чутливість MPH (за замовч.: -3)", "Ευαισθησία MPH (προεπιλογή: -3)", "MPH-känslighet (standard: -3)", "ความไว MPH (ค่าเริ่มต้น: -3)", "Citlivost MPH (výchozí: -3)", "MPH-følsomhed (standard: -3)", "MPH hassasiyeti (varsayılan: -3)", "MPH-følsomhet (standard: -3)", "MPH érzékenység (alapértelmezett: -3)", "MPH-herkkyys (oletus: -3)", "Độ nhạy MPH (mặc định: -3)", "Czułość MPH (domyślnie: -3)", "Sensibilitate MPH (implicit: -3)"},
    {"Aim sensitivity (default: 63)", "エイム感度 (既定: 63)", "Ziel-Empfindlichkeit (Standard: 63)", "Sensibilidad de puntería (predeterminado: 63)", "Sensibilité de visée (par défaut : 63)", "Sensibilità mira (predefinito: 63)", "Richtgevoeligheid (standaard: 63)", "Sensibilidade de mira (padrão: 63)", "Чувствительность прицеливания (по умолчанию: 63)", "瞄准灵敏度（默认：63）", "조준 감도 (기본값: 63)", "حساسية التصويب (افتراضي: 63)", "Sensitivitas bidik (default: 63)", "Чутливість прицілу (за замовч.: 63)", "Ευαισθησία στόχευσης (προεπιλογή: 63)", "Siktkänslighet (standard: 63)", "ความไวการเล็ง (ค่าเริ่มต้น: 63)", "Citlivost míření (výchozí: 63)", "Sigtekänslighed (standard: 63)", "Nişan hassasiyeti (varsayılan: 63)", "Siktesensitivitet (standard: 63)", "Célzás érzékenysége (alapértelmezett: 63)", "Tähtäysherkkyys (oletus: 63)", "Độ nhạy ngắm (mặc định: 63)", "Czułość celowania (domyślnie: 63)", "Sensibilitate țintire (implicit: 63)"},
    {"Aim Y-Axis Scale (default: 1.5147)", "エイムY軸スケール (既定: 1.5147)", "Ziel-Y-Achsen-Skalierung (Standard: 1.5147)", "Escala del eje Y de puntería (predeterminado: 1.5147)", "Échelle de l'axe Y de visée (par défaut : 1.5147)", "Scala asse Y mira (predefinito: 1.5147)", "Doel-Y-as schaal (standaard: 1.5147)", "Escala do eixo Y da mira (padrão: 1.5147)", "Масштаб оси Y прицеливания (по умолчанию: 1.5147)", "瞄准 Y 轴比例（默认：1.5147）", "조준 Y축 배율 (기본값: 1.5147)", "مقياس محور Y للتصويب (افتراضي: 1.5147)", "Skala sumbu Y aim (default: 1.5147)", "Масштаб осі Y прицілювання (за замовч.: 1.5147)", "Κλίμακα άξονα Y σκόπευσης (προεπιλογή: 1.5147)", "Sikte Y-axelskala (standard: 1.5147)", "สเกลแกน Y การเล็ง (ค่าเริ่มต้น: 1.5147)", "Měřítko osy Y míření (výchozí: 1.5147)", "Sigte Y-akse skala (standard: 1.5147)", "Nişan Y ekseni ölçeği (varsayılan: 1.5147)", "Sikte Y-akse skala (standard: 1.5147)", "Célzás Y-tengely skála (alapért.: 1.5147)", "Tähtäyksen Y-akselin skaala (oletus: 1.5147)", "Tỷ lệ trục Y ngắm (mặc định: 1.5147)", "Skala osi Y celowania (domyślnie: 1.5147)", "Scală axă Y țintire (implicit: 1.5147)"},
    {"Mode", "モード", "Modus", "Modo", "Mode", "Modalità", "Modus", "Modo", "Режим", "模式", "모드", "الوضع", "Mode", "Режим", "Λειτουργία", "Läge", "โหมด", "Režim", "Tilstand", "Mod", "Modus", "Mód", "Tila", "Chế độ", "Tryb", "Mod"},
    {"Low-Latency Aim Mode", "低遅延エイム方式", "Zielmodus mit niedriger Latenz", "Modo de puntería de baja latencia", "Mode visée faible latence", "Modalità mira a bassa latenza", "Richtmodus met lage latentie", "Modo de mira de baixa latência", "Режим прицеливания с низкой задержкой", "低延迟瞄准模式", "저지연 에임 모드", "وضع التصويب منخفض التأخير", "Mode bidik latensi rendah", "Режим прицілювання з низькою затримкою", "Λειτουργία σκόπευσης χαμηλής καθυστέρησης", "Siktläge med låg latens", "โหมดเล็งแบบหน่วงต่ำ", "Režim míření s nízkou latencí", "Sigtetilstand med lav latency", "Düşük gecikmeli nişan modu", "Sikte-modus med lav latency", "Alacsony késleltetésű célzás mód", "Matalan viiveen tähtäystila", "Chế độ ngắm độ trễ thấp", "Tryb celowania o niskim opóźnieniu", "Mod țintire cu latență redusă"},
    {"Instant Aim Follow", "即時エイム追従", "Sofortige Zielverfolgung", "Seguimiento de puntería instantáneo", "Suivi de visée instantané", "Mira istantanea", "Directe richtvolging", "Seguimento de mira instantâneo", "Мгновенное слежение прицела", "即时瞄准跟随", "즉시 조준 추적", "متابعة التصويب الفورية", "Ikuti Bidik Instan", "Миттєве слідування прицілу", "Άμεση παρακολούθηση σκόπευσης", "Omedelbar siktning", "ติดตามการเล็งทันที", "Okamžité sledování míření", "Øjeblikkelig sigtefølgning", "Anında nişan takibi", "Umiddelbar siktefølging", "Azonnali célzéskövetés", "Välitön tähtäyksen seuranta", "Theo dõi ngắm tức thì", "Natychmiastowe śledzenie celowania", "Urmărire instantanee a țintirii"},
    {"Instant Aim Follow (Developer Only)", "即時エイム追従（開発者専用）", "Sofortige Zielverfolgung (nur Entwickler)", "Seguimiento de puntería instantáneo (solo desarrolladores)", "Suivi de visée instantané (développeurs uniquement)", "Seguito mira istantaneo (solo sviluppatori)", "Directe richtvolging (alleen ontwikkelaars)", "Seguimento de mira instantâneo (somente desenvolvedores)", "Мгновенное следование прицела (только для разработчиков)", "即时瞄准跟随（仅限开发者）", "즉시 조준 추적 (개발자 전용)", "متابعة التصويب الفورية (للمطورين فقط)", "Ikuti bidik instan (Khusus pengembang)", "Миттєве слідування прицілу (лише для розробників)", "Άμεση παρακολούθηση στόχευσης (μόνο προγραμματιστές)", "Omedelbar siktning (endast utvecklare)", "ติดตามการเล็งทันที (เฉพาะนักพัฒนา)", "Okamžité sledování míření (pouze pro vývojáře)", "Øjeblikkelig sigtefølgning (kun udviklere)", "Anlık nişan takibi (Yalnızca geliştiriciler)", "Umiddelbar siktefølging (kun utviklere)", "Azonnali célzéskövetés (csak fejlesztőknek)", "Välitön tähtäysseuranta (vain kehittäjille)", "Theo dõi ngắm tức thì (Chỉ dành cho nhà phát triển)", "Natychmiastowe śledzenie celu (tylko dla deweloperów)", "Urmărire instantanee a țintirii (doar dezvoltatori)"},
    {"Immediate Sync", "即時同期", "Sofortige Synchronisation", "Sincronización inmediata", "Synchronisation immédiate", "Sincronizzazione immediata", "Directe synchronisatie", "Sincronização imediata", "Мгновенная синхронизация", "即时同步", "즉시 동기화", "مزامنة فورية", "Sinkronisasi langsung", "Миттєва синхронізація", "Άμεσος συγχρονισμός", "Omedelbar synk", "ซิงค์ทันที", "Okamžitá synchronizace", "Øjeblikkelig synk", "Anında senkronizasyon", "Umiddelbar synk", "Azonnali szinkron", "Välitön synkronointi", "Đồng bộ tức thì", "Natychmiastowa synchronizacja", "Sincronizare imediată"},
    {"MoonLike Aim", "MoonLikeエイム", "MoonLike-Ziel", "Puntería MoonLike", "Visée MoonLike", "Mira MoonLike", "MoonLike-richting", "Mira MoonLike", "Прицел MoonLike", "MoonLike 瞄准", "MoonLike 조준", "تصويب MoonLike", "Bidik MoonLike", "Приціл MoonLike", "Στόχευση MoonLike", "MoonLike-sikte", "เล็ง MoonLike", "Míření MoonLike", "MoonLike-sigte", "MoonLike nişan", "MoonLike-sikte", "MoonLike célzás", "MoonLike-tähtäys", "Ngắm MoonLike", "Celownik MoonLike", "Țintire MoonLike"},
    {"Enable SnapTap (Faster directional switching for smooth strafing — may slightly increase input delay)", "SnapTapを有効化 (ストレイフの方向切替を高速化。入力遅延が少し増える場合あり)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "Activar SnapTap (cambio de dirección más rápido al strafe — puede aumentar ligeramente el retardo de entrada)", "Activer SnapTap (changement de direction plus rapide en strafe — peut légèrement augmenter le délai d'entrée)", "Attiva SnapTap (cambio direzione più rapido per strafe — può aumentare leggermente il ritardo di input)", "SnapTap inschakelen (snellere richtingswissel bij strafen — kan de invoervertraging licht verhogen)", "Ativar SnapTap (troca de direção mais rápida ao strafe — pode aumentar ligeiramente o atraso de entrada)", "Включить SnapTap (более быстрая смена направления при стрейфе — может немного увеличить задержку ввода)", "启用 SnapTap（加快方向切换以实现流畅侧移——可能略微增加输入延迟）", "SnapTap 활성화 (부드러운 스트레이프를 위한 빠른 방향 전환 — 입력 지연이 약간 증가할 수 있음)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)"},
    {"Unlock All Hunters/Maps/SoundTest/Gallery (Change a setting in MPH and save to update the save data. Uncheck this option after saving)", "全ハンター/マップ/サウンドテスト/ギャラリーを解放 (MPH側で設定を変更して保存するとセーブに反映。保存後はオフ推奨)", "Alle Hunter/Karten/Soundtest/Galerie freischalten (Einstellung in MPH ändern und speichern, um Speicherdaten zu aktualisieren. Option nach dem Speichern deaktivieren)", "Desbloquear todos los cazadores/mapas/prueba de sonido/galería (Cambia un ajuste en MPH y guarda para actualizar los datos. Desmarca esta opción tras guardar)", "Débloquer tous les chasseurs/cartes/test audio/galerie (Modifiez un paramètre dans MPH et sauvegardez pour mettre à jour les données. Décochez cette option après la sauvegarde)", "Sblocca tutti i cacciatori/mappe/sound test/galleria (Modifica un'impostazione in MPH e salva per aggiornare i dati. Deseleziona questa opzione dopo il salvataggio)", "Alle jagers/kaarten/soundtest/galerij ontgrendelen (Wijzig een instelling in MPH en sla op om gegevens bij te werken. Schakel deze optie uit na het opslaan)", "Desbloquear todos os caçadores/mapas/teste de som/galeria (Altere uma configuração no MPH e salve para atualizar os dados. Desmarque esta opção após salvar)", "Разблокировать всех охотников/карты/звуковой тест/галерею (Измените настройку в MPH и сохраните для обновления данных. Снимите эту опцию после сохранения)", "解锁全部猎人/地图/音效测试/画廊 (在 MPH 中更改设置并保存以更新存档。保存后请取消勾选此选项)", "모든 헌터/맵/사운드 테스트/갤러리 잠금 해제 (MPH에서 설정을 변경하고 저장하면 데이터가 반영됩니다. 저장 후 이 옵션을 해제하세요)", "فتح جميع Hunters/Maps/SoundTest/Gallery (غيّر إعدادًا في MPH واحفظ لتحديث بيانات الحفظ. ألغِ تحديد هذا الخيار بعد الحفظ)", "Buka Kunci Semua Hunters/Maps/SoundTest/Gallery (Ubah pengaturan di MPH dan simpan untuk memperbarui data save. Hapus centang opsi ini setelah menyimpan)", "Розблокувати всіх Hunters/Maps/SoundTest/Gallery (Змініть налаштування в MPH і збережіть для оновлення даних збереження. Зніміть цей прапорець після збереження)", "Ξεκλείδωμα όλων των Hunters/Maps/SoundTest/Gallery (Αλλάξτε μια ρύθμιση στο MPH και αποθηκεύστε για ενημέρωση των δεδομένων. Αποεπιλέξτε αυτήν την επιλογή μετά την αποθήκευση)", "Lås upp alla Hunters/Maps/SoundTest/Gallery (Ändra en inställning i MPH och spara för att uppdatera spardata. Avmarkera detta alternativ efter sparande)", "ปลดล็อก Hunters/Maps/SoundTest/Gallery ทั้งหมด (เปลี่ยนการตั้งค่าใน MPH แล้วบันทึกเพื่ออัปเดตข้อมูลเซฟ ยกเลิกตัวเลือกนี้หลังบันทึก)", "Odemknout všechny Hunters/Maps/SoundTest/Gallery (Změňte nastavení v MPH a uložte pro aktualizaci dat. Po uložení zrušte zaškrtnutí)", "Lås alle Hunters/Maps/SoundTest/Gallery op (Ændr en indstilling i MPH og gem for at opdatere gemte data. Fjern markering efter gemning)", "Tüm Hunters/Maps/SoundTest/Gallery kilidini aç (Kayıt verilerini güncellemek için MPH'de bir ayarı değiştir ve kaydet. Kaydettikten sonra bu seçeneğin işaretini kaldır)", "Lås opp alle Hunters/Maps/SoundTest/Gallery (Endre en innstilling i MPH og lagre for å oppdatere lagrede data. Fjern avkryssing etter lagring)", "Összes Hunters/Maps/SoundTest/Gallery feloldása (Változtass egy beállítást az MPH-ben és ments a mentés frissítéséhez. Mentés után vedd ki a pipát)", "Avaa kaikki Hunters/Maps/SoundTest/Gallery (Muuta asetusta MPH:ssä ja tallenna päivittääksesi tiedot. Poista valinta tallennuksen jälkeen)", "Mở khóa tất cả Hunters/Maps/SoundTest/Gallery (Thay đổi cài đặt trong MPH và lưu để cập nhật dữ liệu save. Bỏ chọn tùy chọn này sau khi lưu)", "Odblokuj wszystkich Hunters/Maps/SoundTest/Gallery (Zmień ustawienie w MPH i zapisz, aby zaktualizować dane zapisu. Odznacz tę opcję po zapisaniu)", "Deblochează toți Hunters/Maps/SoundTest/Gallery (Schimbă o setare în MPH și salvează pentru a actualiza datele. Debifează această opțiune după salvare)"},
    {"Set MPH audio settings to headphones.(recommended) (Change a setting in MPH and save to update the save data. Uncheck this option after saving)", "MPHの音声設定をヘッドホンにする (推奨。MPH側で設定を変更して保存するとセーブに反映。保存後はオフ推奨)", "MPH-Audioeinstellung auf Kopfhörer setzen (empfohlen). (Einstellung in MPH ändern und speichern, um die Speicherdaten zu aktualisieren. Nach dem Speichern deaktivieren)", "Configurar el audio de MPH en auriculares (recomendado). (Cambia un ajuste en MPH y guarda para actualizar la partida. Desmarca esta opción después de guardar)", "Régler l'audio MPH sur casque (recommandé). (Modifiez un réglage dans MPH et sauvegardez pour mettre à jour la sauvegarde. Décochez cette option après la sauvegarde)", "Imposta l'audio MPH su cuffie (consigliato). (Modifica un'impostazione in MPH e salva per aggiornare i dati di salvataggio. Deseleziona dopo il salvataggio)", "MPH-audio instellen op koptelefoon (aanbevolen). (Wijzig een instelling in MPH en sla op om de save bij te werken. Vink uit na het opslaan)", "Definir áudio do MPH para fones de ouvido (recomendado). (Altere uma configuração no MPH e salve para atualizar os dados. Desmarque após salvar)", "Установить аудио MPH на наушники (рекомендуется). (Измените настройку в MPH и сохраните, чтобы обновить данные. Снимите флажок после сохранения)", "将 MPH 音频设置为耳机（推荐）。（在 MPH 中更改设置并保存以更新存档。保存后取消勾选此选项）", "MPH 오디오 설정을 헤드폰으로 설정 (권장). (MPH에서 설정을 변경하고 저장하여 세이브 데이터를 업데이트하세요. 저장 후 이 옵션을 해제하세요)", "تعيين إعدادات صوت MPH إلى سماعات الرأس (موصى به). (غيّر إعدادًا في MPH واحفظ لتحديث بيانات الحفظ. ألغِ تحديد هذا الخيار بعد الحفظ)", "Atur audio MPH ke headphone (disarankan). (Ubah pengaturan di MPH dan simpan untuk memperbarui data save. Hapus centang opsi ini setelah menyimpan)", "Установити аудіо MPH на навушники (рекомендовано). (Змініть налаштування в MPH і збережіть, щоб оновити дані. Зніміть прапорець після збереження)", "Ορισμός ήχου MPH σε ακουστικά (συνιστάται). (Αλλάξτε ρύθμιση στο MPH και αποθηκεύστε για ενημέρωση. Αποεπιλέξτε μετά την αποθήκευση)", "Ställ in MPH-ljud på hörlurar (rekommenderas). (Ändra en inställning i MPH och spara för att uppdatera. Avmarkera efter sparning)", "ตั้งค่าเสียง MPH เป็นหูฟัง (แนะนำ) (เปลี่ยนการตั้งค่าใน MPH แล้วบันทึกเพื่ออัปเดตข้อมูลเซฟ ยกเลิกตัวเลือกนี้หลังบันทึก)", "Nastavit audio MPH na sluchátka (doporučeno). (Změňte nastavení v MPH a uložte pro aktualizaci. Po uložení zrušte zaškrtnutí)", "Indstil MPH-lyd til hovedtelefoner (anbefales). (Ændr en indstilling i MPH og gem for at opdatere. Fjern markering efter gem)", "MPH ses ayarlarını kulaklığa ayarla (önerilir). (MPH'de bir ayarı değiştirip kaydederek güncelleyin. Kaydettikten sonra işareti kaldırın)", "Sett MPH-lyd til hodetelefoner (anbefalt). (Endre en innstilling i MPH og lagre for å oppdatere. Fjern avkrysning etter lagring)", "MPH hang beállítása fejhallgatóra (ajánlott). (Módosíts egy beállítást az MPH-ben és mentsd a mentés frissítéséhez. Mentés után vedd ki a pipát)", "Aseta MPH-ääniasetukset kuulokkeisiin (suositeltu). (Muuta asetusta MPH:ssä ja tallenna päivittääksesi. Poista valinta tallennuksen jälkeen)", "Đặt âm thanh MPH sang tai nghe (khuyến nghị). (Thay đổi cài đặt trong MPH và lưu để cập nhật. Bỏ chọn sau khi lưu)", "Ustaw audio MPH na słuchawki (zalecane). (Zmień ustawienie w MPH i zapisz, aby zaktualizować. Odznacz po zapisaniu)", "Setează audio MPH pe căști (recomandat). (Schimbă o setare în MPH și salvează pentru actualizare. Debifează după salvare)"},
    {"Use DS Name (Resets in-game name and HL color):You can change the DS name from [File -> Boot firmware] or [Config -> Firmware Settings].", "DS本体名を使う (ゲーム内名とHL色をリセット): DS名は [File -> Boot firmware] または [Config -> Firmware Settings] で変更できます。", "DS-Namen verwenden (setzt Spielname und HL-Farbe zurück): Den DS-Namen kannst du unter [Datei -> Firmware starten] oder [Konfiguration -> Firmware-Einstellungen] ändern.", "Usar nombre del DS (restablece el nombre en el juego y el color HL): Puedes cambiar el nombre del DS en [Archivo -> Iniciar firmware] o [Configuración -> Ajustes de firmware].", "Utiliser le nom du DS (réinitialise le nom en jeu et la couleur HL) : vous pouvez modifier le nom du DS via [Fichier -> Démarrer le firmware] ou [Config -> Paramètres firmware].", "Usa nome DS (reimposta nome in gioco e colore HL): puoi cambiare il nome DS da [File -> Avvia firmware] o [Config -> Impostazioni firmware].", "DS-naam gebruiken (reset spelnaam en HL-kleur): je kunt de DS-naam wijzigen via [Bestand -> Firmware opstarten] of [Config -> Firmware-instellingen].", "Usar nome do DS (redefine o nome no jogo e a cor HL): você pode alterar o nome do DS em [Arquivo -> Iniciar firmware] ou [Config -> Configurações de firmware].", "Использовать имя DS (сбрасывает имя в игре и цвет HL): имя DS можно изменить в [Файл -> Загрузить прошивку] или [Настройки -> Параметры прошивки].", "使用 DS 名称（重置游戏内名称和 HL 颜色）：可在 [文件 -> 启动固件] 或 [配置 -> 固件设置] 中更改 DS 名称。", "DS 이름 사용 (게임 내 이름 및 HL 색상 초기화): DS 이름은 [파일 -> 펌웨어 부팅] 또는 [설정 -> 펌웨어 설정]에서 변경할 수 있습니다.", "استخدام اسم DS (يعيد تعيين الاسم داخل اللعبة ولون HL): يمكنك تغيير اسم DS من [File -> Boot firmware] أو [Config -> Firmware Settings].", "Gunakan nama DS (Reset nama in-game dan warna HL): Anda dapat mengubah nama DS dari [File -> Boot firmware] atau [Config -> Firmware Settings].", "Використовувати ім'я DS (скидає ім'я в грі та колір HL): ім'я DS можна змінити в [File -> Boot firmware] або [Config -> Firmware Settings].", "Χρήση ονόματος DS (Επαναφέρει όνομα στο παιχνίδι και χρώμα HL): Μπορείτε να αλλάξετε το όνομα DS από [File -> Boot firmware] ή [Config -> Firmware Settings].", "Använd DS-namn (Återställer spelnamn och HL-färg): Du kan ändra DS-namnet via [File -> Boot firmware] eller [Config -> Firmware Settings].", "ใช้ชื่อ DS (รีเซ็ตชื่อในเกมและสี HL): เปลี่ยนชื่อ DS ได้จาก [File -> Boot firmware] หรือ [Config -> Firmware Settings]", "Použít název DS (resetuje jméno ve hře a barvu HL): Název DS lze změnit v [File -> Boot firmware] nebo [Config -> Firmware Settings].", "Brug DS-navn (Nulstiller spilnavn og HL-farve): Du kan ændre DS-navnet via [File -> Boot firmware] eller [Config -> Firmware Settings].", "DS adını kullan (Oyun içi adı ve HL rengini sıfırlar): DS adını [File -> Boot firmware] veya [Config -> Firmware Settings] üzerinden değiştirebilirsiniz.", "Bruk DS-navn (Tilbakestiller spillnavn og HL-farge): Du kan endre DS-navnet via [File -> Boot firmware] eller [Config -> Firmware Settings].", "DS név használata (Játékon belüli név és HL szín visszaállítása): A DS nevet a [File -> Boot firmware] vagy [Config -> Firmware Settings] menüben módosíthatod.", "Käytä DS-nimeä (Nollaa pelin nimen ja HL-värin): DS-nimen voi muuttaa kohdista [File -> Boot firmware] tai [Config -> Firmware Settings].", "Dùng tên DS (Đặt lại tên trong game và màu HL): Bạn có thể đổi tên DS từ [File -> Boot firmware] hoặc [Config -> Firmware Settings].", "Użyj nazwy DS (Resetuje nazwę w grze i kolor HL): Nazwę DS można zmienić w [File -> Boot firmware] lub [Config -> Firmware Settings].", "Folosește numele DS (Resetează numele din joc și culoarea HL): Poți schimba numele DS din [File -> Boot firmware] sau [Config -> Firmware Settings]."},
    {"Enable Joy2KeySupport (enable this if keys sometimes get stuck; slightly increases input delay)", "Joy2Keyサポートを有効化 (キーが押しっぱなしになる場合に使用。入力遅延が少し増えます)", "Joy2KeySupport aktivieren (aktivieren, wenn Tasten manchmal hängen bleiben; leicht erhöhte Eingabeverzögerung)", "Activar Joy2KeySupport (actívalo si las teclas a veces se quedan pulsadas; aumenta ligeramente el retardo de entrada)", "Activer Joy2KeySupport (à activer si les touches restent parfois bloquées ; augmente légèrement le délai d'entrée)", "Abilita Joy2KeySupport (attivalo se i tasti a volte restano bloccati; aumenta leggermente il ritardo di input)", "Joy2KeySupport inschakelen (inschakelen als toetsen soms blijven hangen; verhoogt licht de invoervertraging)", "Ativar Joy2KeySupport (ative se as teclas às vezes ficarem presas; aumenta ligeiramente o atraso de entrada)", "Включить Joy2KeySupport (включите, если клавиши иногда залипают; немного увеличивает задержку ввода)", "启用 Joy2KeySupport（若按键偶尔卡住请启用；会略微增加输入延迟）", "Joy2KeySupport 활성화 (키가 가끔 눌린 채로 남을 때 사용; 입력 지연이 약간 증가함)", "تفعيل Joy2KeySupport (فعّله إذا علقت المفاتيح أحياناً؛ يزيد تأخير الإدخال قليلاً)", "Aktifkan Joy2KeySupport (aktifkan jika tombol kadang macet; sedikit menambah penundaan input)", "Увімкнути Joy2KeySupport (увімкніть, якщо клавіші інколи залипають; трохи збільшує затримку вводу)", "Ενεργοποίηση Joy2KeySupport (ενεργοποιήστε αν τα πλήκτρα κολλάνε· αυξάνει ελαφρώς την καθυστέρηση εισόδου)", "Aktivera Joy2KeySupport (aktivera om tangenter ibland fastnar; ökar lätt indatalatensen)", "เปิด Joy2KeySupport (เปิดหากปุ่มติดค้างเป็นครั้งคราว จะเพิ่มความล่าช้าของอินพุตเล็กน้อย)", "Povolit Joy2KeySupport (povolte, pokud se klávesy občas zaseknou; mírně zvyšuje zpoždění vstupu)", "Aktiver Joy2KeySupport (aktiver hvis taster nogle gange hænger; øger inputforsinkelsen lidt)", "Joy2KeySupport'u etkinleştir (tuşlar bazen takılırsa etkinleştirin; giriş gecikmesini biraz artırır)", "Aktiver Joy2KeySupport (aktiver hvis taster noen ganger henger; øker inndataforsinkelsen litt)", "Joy2KeySupport engedélyezése (kapcsold be, ha a billentyűk néha beragadnak; kissé növeli a bemeneti késleltetést)", "Ota Joy2KeySupport käyttöön (ota käyttöön, jos näppäimet joskus jumittuvat; lisää hieman syötteen viivettä)", "Bật Joy2KeySupport (bật nếu phím đôi khi bị kẹt; tăng nhẹ độ trễ nhập)", "Włącz Joy2KeySupport (włącz, jeśli klawisze czasem się zacinają; nieznacznie zwiększa opóźnienie wejścia)", "Activează Joy2KeySupport (activează dacă tastele se blochează uneori; crește ușor întârzierea de intrare)"},
    {"Enable Stylus Mode (Leave this unchecked unless you want to play with the stylus)", "スタイラスモードを有効化 (スタイラス操作で遊ぶ場合以外はオフ推奨)", "Stylus-Modus aktivieren (Nur aktivieren, wenn du mit dem Stylus spielen möchtest)", "Activar modo Stylus (Déjalo desmarcado salvo que quieras jugar con el stylus)", "Activer le mode Stylet (Laissez décoché sauf si vous voulez jouer au stylet)", "Attiva modalità Stylus (Lascia deselezionato a meno che tu voglia giocare con lo stylus)", "Stylus-modus inschakelen (Laat uitgevinkt tenzij je met de stylus wilt spelen)", "Ativar modo Stylus (Deixe desmarcado, a menos que queira jogar com o stylus)", "Включить режим стилуса (Не включайте, если не хотите играть стилусом)", "启用触控笔模式（除非要用触控笔游玩，否则请勿勾选）", "스타일러스 모드 활성화 (스타일러스로 플레이하지 않을 경우 체크 해제 권장)", "تفعيل وضع Stylus (اتركه غير محدد ما لم ترغب في اللعب باستخدام Stylus)", "Aktifkan Mode Stylus (Biarkan tidak dicentang kecuali Anda ingin bermain dengan stylus)", "Увімкнути режим Stylus (Залиште знятим, якщо не хочете грати стилусом)", "Ενεργοποίηση λειτουργίας Stylus (Αφήστε το αποεπιλεγμένο εκτός αν θέλετε να παίξετε με stylus)", "Aktivera Stylus-läge (Låt avmarkerad om du inte vill spela med stylus)", "เปิดใช้โหมด Stylus (ปล่อยไม่เลือก เว้นแต่ต้องการเล่นด้วย stylus)", "Povolit režim Stylus (Nechte nezaškrtnuté, pokud nechcete hrát se stylus)", "Aktivér Stylus-tilstand (Lad være umarkeret, medmindre du vil spille med stylus)", "Stylus Modunu etkinleştir (Stylus ile oynamak istemedikçe işaretlemeyin)", "Aktiver Stylus-modus (La stå umerket med mindre du vil spille med stylus)", "Stylus mód engedélyezése (Hagyd kijelölve, hacsak nem stylus-szal akarsz játszani)", "Ota Stylus-tila käyttöön (Jätä valitsematta, ellet halua pelata stylusilla)", "Bật Chế độ Stylus (Để không chọn trừ khi bạn muốn chơi bằng stylus)", "Włącz tryb Stylus (Pozostaw odznaczone, chyba że chcesz grać stylusem)", "Activează modul Stylus (Lasă debifat dacă nu vrei să joci cu stylus)"},
    {"Disable MPH Aim Smoothing (Disables the in-game aim smoothing. Note: Sensitivity will be reduced to 25% in Stylus Mode)", "MPHのエイム補間を無効化 (ゲーム内のエイム補間を無効化。スタイラスモードでは感度が25%になります)", "MPH-Zielglättung deaktivieren (Deaktiviert die Zielglättung im Spiel. Hinweis: Empfindlichkeit wird im Stylus-Modus auf 25 % reduziert)", "Desactivar suavizado de puntería MPH (Desactiva el suavizado de puntería en el juego. Nota: la sensibilidad se reduce al 25 % en modo stylus)", "Désactiver le lissage de visée MPH (Désactive le lissage de visée en jeu. Remarque : la sensibilité passe à 25 % en mode stylet)", "Disabilita smoothing mira MPH (Disabilita lo smoothing della mira in gioco. Nota: la sensibilità sarà ridotta al 25% in modalità stylus)", "MPH-richtgladmaking uitschakelen (Schakelt richtgladmaking in het spel uit. Opmerking: gevoeligheid wordt verlaagd naar 25% in stylusmodus)", "Desativar suavização de mira MPH (Desativa a suavização de mira no jogo. Nota: a sensibilidade será reduzida a 25% no modo stylus)", "Отключить сглаживание прицела MPH (Отключает сглаживание прицела в игре. Примечание: чувствительность снижается до 25% в режиме стилуса)", "禁用 MPH 瞄准平滑 (禁用游戏内瞄准平滑。注意：触控笔模式下灵敏度将降至 25%)", "MPH 조준 스무딩 비활성화 (게임 내 조준 스무딩을 비활성화합니다. 참고: 스타일러스 모드에서 감도가 25%로 줄어듭니다)", "تعطيل تنعيم التصويب MPH (يعطّل تنعيم التصويب داخل اللعبة. ملاحظة: ستُخفض الحساسية إلى 25% في وضع Stylus)", "Nonaktifkan Smoothing Bidik MPH (Menonaktifkan smoothing bidik dalam game. Catatan: Sensitivitas akan dikurangi menjadi 25% di Mode Stylus)", "Вимкнути згладжування прицілу MPH (Вимикає згладжування прицілу в грі. Примітка: чутливість зменшиться до 25% у режимі Stylus)", "Απενεργοποίηση εξομάλυνσης σκόπευσης MPH (Απενεργοποιεί την εξομάλυνση σκόπευσης στο παιχνίδι. Σημ.: Η ευαισθησία μειώνεται στο 25% σε λειτουργία Stylus)", "Inaktivera MPH-siktningsutjämning (Inaktiverar siktningsutjämning i spelet. Obs: Känsligheten minskas till 25 % i Stylus-läge)", "ปิดการทำให้การเล็ง MPH เรียบ (ปิดการทำให้การเล็งเรียบในเกม หมายเหตุ: ความไวจะลดเหลือ 25% ในโหมด Stylus)", "Zakázat vyhlazování míření MPH (Zakáže vyhlazování míření ve hře. Pozn.: Citlivost bude snížena na 25 % v režimu Stylus)", "Deaktiver MPH-sigteudjævning (Deaktiverer sigteudjævning i spillet. Bemærk: Følsomhed reduceres til 25 % i Stylus-tilstand)", "MPH nişan yumuşatmayı devre dışı bırak (Oyun içi nişan yumuşatmayı devre dışı bırakır. Not: Stylus Modunda hassasiyet %25'e düşer)", "Deaktiver MPH-sikteutjevning (Deaktiverer sikteutjevning i spillet. Merk: Følsomhet reduseres til 25 % i Stylus-modus)", "MPH célzás simítás kikapcsolása (Kikapcsolja a játékbeli célzás simítását. Megj.: Stylus módban az érzékenység 25%-ra csökken)", "Poista MPH-tähtäyksen pehmennys käytöstä (Poistaa pelin tähtäyksen pehmennyksen käytöstä. Huom: Herkkyys pienenee 25 %:iin Stylus-tilassa)", "Tắt làm mượt ngắm MPH (Tắt làm mượt ngắm trong game. Lưu ý: Độ nhạy sẽ giảm xuống 25% ở Chế độ Stylus)", "Wyłącz wygładzanie celowania MPH (Wyłącza wygładzanie celowania w grze. Uwaga: Czułość zostanie zmniejszona do 25% w trybie Stylus)", "Dezactivează netezirea țintirii MPH (Dezactivează netezirea țintirii în joc. Notă: Sensibilitatea va fi redusă la 25% în modul Stylus)"},
    {"Enable Aim Sub-pixel Accumulator (Carry fractional mouse movement across frames. Enable for smoother low-sensitivity aiming)", "エイムのサブピクセル蓄積を有効化 (小数のマウス移動を次フレームへ持ち越し。低感度エイムが滑らかになります)", "Subpixel-Zielakkumulator aktivieren (Überträgt Bruchteile der Mausbewegung auf den nächsten Frame. Für flüssigeres Zielen bei niedriger Empfindlichkeit)", "Activar acumulador subpíxel de puntería (Conserva el movimiento fraccional del ratón entre fotogramas. Actívalo para una puntería más suave con baja sensibilidad)", "Activer l'accumulateur sub-pixel de visée (Reporte les mouvements fractionnels de la souris d'une image à l'autre. Pour une visée plus fluide à faible sensibilité)", "Abilita accumulatore sub-pixel mira (Trasferisce il movimento frazionale del mouse tra i fotogrammi. Per una mira più fluida a bassa sensibilità)", "Subpixel-richtaccumulator inschakelen (Draagt fractionele muisbeweging over naar volgende frames. Voor vloeiender richten bij lage gevoeligheid)", "Ativar acumulador subpixel de mira (Mantém movimento fracionário do mouse entre quadros. Ative para mira mais suave com baixa sensibilidade)", "Включить субпиксельный аккумулятор прицела (Переносит дробное движение мыши между кадрами. Для более плавного прицеливания при низкой чувствительности)", "启用瞄准亚像素累加器（将小数鼠标移动量传递到下一帧。低灵敏度瞄准时更流畅）", "조준 서브픽셀 누적기 활성화 (프레임 간 마우스 이동의 소수 부분을 이월합니다. 낮은 감도 조준을 더 부드럽게 합니다)", "تفعيل مجمع التصويب دون البكسل (نقل حركة الفأرة الكسرية عبر الإطارات. فعّله لتصويب أنعم بحساسية منخفضة)", "Aktifkan Akumulator Sub-pixel Bidik (Bawa gerakan mouse fraksional antar frame. Aktifkan untuk bidik halus sensitivitas rendah)", "Увімкнути субпіксельний акумулятор прицілу (Переносить дробовий рух миші між кадрами. Для плавнішого прицілювання при низькій чутливості)", "Ενεργοποίηση συσσωρευτή υπο-pixel στόχευσης (Μεταφέρει κλασματική κίνηση ποντικιού μεταξύ καρέ. Για ομαλότερη στόχευση χαμηλής ευαισθησίας)", "Aktivera subpixel-siktackumulator (Överför bråkdel av musrörelse mellan bilder. För mjukare siktning vid låg känslighet)", "เปิดใช้ตัวสะสม Sub-pixel การเล็ง (ถ่ายโอนการเคลื่อนไหวเมาส์เศษส่วนข้ามเฟรม เปิดเพื่อเล็งลื่นไหลเมื่อความไวต่ำ)", "Povolit subpixelový akumulátor míření (Přenáší zlomkový pohyb myši mezi snímky. Pro plynulejší míření při nízké citlivosti)", "Aktivér subpixel sigteakkumulator (Overfører brøkdele af musebevægelse mellem billeder. For jævnere sigtning ved lav følsomhed)", "Nişan alt piksel biriktiriciyi etkinleştir (Kesirli fare hareketini kareler arasında taşır. Düşük hassasiyette daha akıcı nişan için)", "Aktiver subpiksel sikteakkumulator (Overfører brøkdel av musebevegelse mellom bilder. For jevnere sikting ved lav sensitivitet)", "Célzás subpixel akkumulátor engedélyezése (Tört egérmozgást visz át képkockák között. Alacsony érzékenységű simább célzáshoz)", "Ota käyttöön tähtäyksen subpikselikertymä (Siirtää murto-osa hiiren liikettä ruudulta toiselle. Tasaisempaan tähtäykseen alhaisella herkkyydellä)", "Bật Bộ tích lũy Sub-pixel ngắm (Chuyển chuyển động chuột phân số qua khung hình. Bật để ngắm mượt hơn ở độ nhạy thấp)", "Włącz akumulator subpikselowy celowania (Przenosi ułamkowy ruch myszy między klatkami. Dla płynniejszego celowania przy niskiej czułości)", "Activează acumulatorul sub-pixel de țintire (Transferă mișcarea fracțională a mouse-ului între cadre. Pentru țintire mai lină la sensibilitate scăzută)"},
    {"Scale aim sensitivity while zoomed", "ズーム中のエイム感度を倍率変更", "Zielempfindlichkeit beim Zoomen skalieren", "Escalar sensibilidad de puntería al hacer zoom", "Mettre à l'échelle la sensibilité de visée en zoom", "Scala sensibilità mira durante lo zoom", "Doelgevoeligheid schalen bij zoomen", "Escalar sensibilidade da mira ao dar zoom", "Масштабировать чувствительность прицеливания при зуме", "缩放时调整瞄准灵敏度", "줌 중 조준 감도 조절", "تعديل حساسية التصويب أثناء التكبير", "Skala sensitivitas aim saat zoom", "Масштабувати чутливість прицілювання під час зуму", "Κλιμάκωση ευαισθησίας σκόπευσης κατά το zoom", "Skala siktekänslighet vid zoom", "ปรับความไวการเล็งขณะซูม", "Škálovat citlivost míření při zoomu", "Skaler sigtefølsomhed ved zoom", "Yakınlaştırırken nişan hassasiyetini ölçekle", "Skaler siktefølsomhet ved zoom", "Célzás érzékenység skálázása nagyításkor", "Skaalaa tähtäyksen herkkyys zoomatessa", "Điều chỉnh độ nhạy ngắm khi zoom", "Skaluj czułość celowania podczas zoomu", "Scalează sensibilitatea țintirii la zoom"},
    {"Zoom Aim Scale %", "ズーム時エイム倍率 %", "Zoom-Ziel-Skalierung %", "Escala de puntería con zoom %", "Échelle de visée au zoom %", "Scala mira zoom %", "Zoom-richtschaal %", "Escala de mira com zoom %", "Масштаб прицела при зуме %", "缩放瞄准比例 %", "줌 조준 배율 %", "مقياس تصويب التكبير %", "Skala bidik zoom %", "Масштаб прицілу зуму %", "Κλίμακα στόχευσης ζουμ %", "Zoomsiktskala %", "สเกลเล็งซูม %", "Měřítko míření zoomu %", "Zoom-sigteskala %", "Yakınlaştırma nişan ölçeği %", "Zoom-sikteskala %", "Zoom célzási skála %", "Zoom-tähtäysaste %", "Tỷ lệ ngắm zoom %", "Skala celowania zoomu %", "Scară țintire zoom %"},
    {"Enable Direct Alt-Form Transform", "直接トランスフォーム変形を有効化", "Direkte Alt-Form-Transformation aktivieren", "Activar transformación Alt-Form directa", "Activer transformation Alt-Form directe", "Attiva trasformazione Alt-Form diretta", "Directe Alt-Form-transformatie inschakelen", "Ativar transformação Alt-Form direta", "Включить прямое преобразование Alt-Form", "启用直接 Alt-Form 变形", "직접 Alt-Form 변형 활성화", "تفعيل تحويل Alt-Form المباشر", "Aktifkan Transformasi Alt-Form Langsung", "Увімкнути пряме перетворення Alt-Form", "Ενεργοποίηση άμεσου μετασχηματισμού Alt-Form", "Aktivera direkt Alt-Form-transform", "เปิดใช้การแปลง Alt-Form โดยตรง", "Povolit přímou transformaci Alt-Form", "Aktivér direkte Alt-Form-transform", "Doğrudan Alt-Form Dönüşümünü etkinleştir", "Aktiver direkte Alt-Form-transform", "Közvetlen Alt-Form átalakítás engedélyezése", "Ota suora Alt-Form-muunnos käyttöön", "Bật Biến đổi Alt-Form trực tiếp", "Włącz bezpośrednią transformację Alt-Form", "Activează transformarea Alt-Form directă"},
    {"Enable Immediate Input Edge Overlay", "即時入力エッジ合成を有効化", "Sofortige Eingabe-Kantenüberlagerung aktivieren", "Activar superposición de bordes de entrada inmediata", "Activer la superposition immédiate des bords d'entrée", "Abilita overlay bordi input immediato", "Directe invoerrand-overlay inschakelen", "Ativar sobreposição de bordas de entrada imediata", "Включить наложение мгновенных границ ввода", "启用即时输入边缘叠加", "즉시 입력 엣지 오버레이 활성화", "تفعيل تراكب حافة الإدخال الفوري", "Aktifkan Overlay Tepi Input Langsung", "Увімкнути миттєве накладання країв вводу", "Ενεργοποίηση άμεσης επικάλυψης άκρων εισόδου", "Aktivera omedelbar inmatningskantöverlagring", "เปิดใช้งาน Overlay ขอบอินพุตทันที", "Povolit okamžité překrytí hran vstupu", "Aktivér øjeblikkelig inputkant-overlejring", "Anında giriş kenarı kaplamasını etkinleştir", "Aktiver umiddelbar inndatakant-overlegg", "Azonnali beviteli szél átfedés engedélyezése", "Ota välitön syötteen reunapeitto käyttöön", "Bật lớp phủ cạnh đầu vào tức thì", "Włącz natychmiastowe nakładanie krawędzi wejścia", "Activează suprapunerea imediată a marginilor de intrare"},
    {"Enable Native Aim Delta Hook (PostFold Write)", "ネイティブエイムデルタHookを有効化 (PostFold書き込み)", "Nativen Aim-Delta-Hook aktivieren (PostFold-Schreibzugriff)", "Activar hook nativo de delta de puntería (escritura PostFold)", "Activer le hook natif de delta de visée (écriture PostFold)", "Abilita hook delta mira nativo (scrittura PostFold)", "Native aim-delta-hook inschakelen (PostFold-schrijven)", "Ativar hook nativo de delta de mira (escrita PostFold)", "Включить нативный хук дельты прицела (запись PostFold)", "启用原生瞄准增量 Hook（PostFold 写入）", "네이티브 조준 델타 Hook 활성화 (PostFold 쓰기)", "تفعيل Native Aim Delta Hook (كتابة PostFold)", "Aktifkan Native Aim Delta Hook (PostFold Write)", "Увімкнути нативний Aim Delta Hook (запис PostFold)", "Ενεργοποίηση Native Aim Delta Hook (εγγραφή PostFold)", "Aktivera Native Aim Delta Hook (PostFold-skrivning)", "เปิดใช้ Native Aim Delta Hook (PostFold Write)", "Povolit nativní Aim Delta Hook (zápis PostFold)", "Aktivér Native Aim Delta Hook (PostFold-skrivning)", "Native Aim Delta Hook'u etkinleştir (PostFold Write)", "Aktiver Native Aim Delta Hook (PostFold-skriving)", "Native Aim Delta Hook engedélyezése (PostFold írás)", "Ota käyttöön Native Aim Delta Hook (PostFold Write)", "Bật Native Aim Delta Hook (PostFold Write)", "Włącz natywny Aim Delta Hook (zapis PostFold)", "Activează Native Aim Delta Hook (scriere PostFold)"},
    {"Enable Native Aim Register Injection", "ネイティブエイムのレジスタ注入を有効化", "Native Zielregister-Injektion aktivieren", "Activar inyección nativa de registro de puntería", "Activer l'injection native du registre de visée", "Abilita iniezione registro mira nativo", "Native doelregister-injectie inschakelen", "Ativar injeção nativa de registro de mira", "Включить нативную инъекцию регистра прицеливания", "启用原生瞄准寄存器注入", "네이티브 조준 레지스터 주입 활성화", "تمكين حقن سجل التصويب الأصلي", "Aktifkan injeksi register aim native", "Увімкнути нативну ін'єкцію регістра прицілювання", "Ενεργοποίηση εγχύσεως καταχωρητή σκόπευσης native", "Aktivera native registerinjektion för sikte", "เปิดใช้การฉีด register aim แบบ native", "Povolit nativní injekci registru míření", "Aktivér native sigteregister-injektion", "Yerel nişan register enjeksiyonunu etkinleştir", "Aktiver native sikteregister-injeksjon", "Natív célzás regiszter injektálás engedélyezése", "Ota käyttöön natiivi tähtäysrekisterin injektio", "Bật inject register aim native", "Włącz natywną injekcję rejestru celowania", "Activează injecția nativă a registrului de țintire"},
    {"Apply Input to Custom HUD", "入力をカスタムHUDに反映", "Eingabe auf Custom HUD anwenden", "Aplicar entrada al HUD personalizado", "Appliquer les entrées au HUD personnalisé", "Applica input all'HUD personalizzato", "Invoer toepassen op aangepaste HUD", "Aplicar entrada ao HUD personalizado", "Применить ввод к пользовательскому HUD", "将输入应用到自定义 HUD", "입력을 사용자 HUD에 적용", "تطبيق الإدخال على Custom HUD", "Terapkan input ke Custom HUD", "Застосувати введення до Custom HUD", "Εφαρμογή εισόδου στο Custom HUD", "Tillämpa inmatning på Custom HUD", "ใช้อินพุตกับ Custom HUD", "Použít vstup na Custom HUD", "Anvend input på Custom HUD", "Girişi Custom HUD'a uygula", "Bruk inndata på Custom HUD", "Bemenet alkalmazása a Custom HUD-ra", "Käytä syötettä Custom HUD:iin", "Áp dụng nhập vào Custom HUD", "Zastosuj wejście do Custom HUD", "Aplică intrarea la Custom HUD"},
    {"Screen Sync Mode — Default: Off", "画面同期方式 - 既定: オフ", "Bildschirm-Sync-Modus — Standard: Aus", "Modo de sincronización de pantalla — Predeterminado: Desactivado", "Mode de synchronisation d'écran — Par défaut : Désactivé", "Modalità sincronizzazione schermo — Predefinito: Off", "Scherm-synchronisatiemodus — Standaard: Uit", "Modo de sincronização de tela — Padrão: Desligado", "Режим синхронизации экрана — По умолчанию: Выкл.", "画面同步模式 — 默认：关闭", "화면 동기화 모드 — 기본값: 끔", "وضع مزامنة الشاشة — الافتراضي: إيقاف", "Mode Sinkronisasi Layar — Default: Mati", "Режим синхронізації екрана — За замовч.: Вимк.", "Λειτουργία συγχρονισμού οθόνης — Προεπιλογή: Ανενεργό", "Skärmsynkroniseringsläge — Standard: Av", "โหมดซิงค์หน้าจอ — ค่าเริ่มต้น: ปิด", "Režim synchronizace obrazovky — Výchozí: Vyp.", "Skærmsynkroniseringstilstand — Standard: Fra", "Ekran Senkron Modu — Varsayılan: Kapalı", "Skjermsynkroniseringsmodus — Standard: Av", "Képernyő szinkron mód — Alapértelmezett: Ki", "Näytön synkronointitila — Oletus: Pois", "Chế độ đồng bộ màn hình — Mặc định: Tắt", "Tryb synchronizacji ekranu — Domyślnie: Wył.", "Mod sincronizare ecran — Implicit: Oprit"},
    {"Screen Sync Mode", "画面同期方式", "Bildschirm-Synchronisationsmodus", "Modo de sincronización de pantalla", "Mode de synchronisation d'écran", "Modalità sincronizzazione schermo", "Schermsynchronisatiemodus", "Modo de sincronização de tela", "Режим синхронизации экрана", "屏幕同步模式", "화면 동기화 모드", "وضع مزامنة الشاشة", "Mode Sinkronisasi Layar", "Режим синхронізації екрана", "Λειτουργία συγχρονισμού οθόνης", "Skärmsynkroniseringsläge", "โหมดซิงค์หน้าจอ", "Režim synchronizace obrazovky", "Skærmsynkroniseringstilstand", "Ekran senkronizasyon modu", "Skjermsynkroniseringsmodus", "Képernyő szinkronizációs mód", "Näytön synkronointitila", "Chế độ đồng bộ màn hình", "Tryb synchronizacji ekranu", "Mod sincronizare ecran"},
    {"Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete", "画面同期方式: オフ = 同期呼び出しなし、glFinish = GLコマンド完了まで待機", "Bildschirm-Sync-Modus: Aus = kein Sync-Aufruf, glFinish = warten bis GL-Befehle abgeschlossen sind", "Modo de sincronización de pantalla: Desactivado = sin llamada de sync, glFinish = esperar a que terminen los comandos GL", "Mode de synchronisation d'écran : Désactivé = pas d'appel sync, glFinish = attendre la fin des commandes GL", "Modalità sync schermo: Off = nessuna chiamata sync, glFinish = attende il completamento dei comandi GL", "Scherm-syncmodus: Uit = geen sync-aanroep, glFinish = wacht tot GL-opdrachten voltooid zijn", "Modo de sincronização de tela: Desligado = sem chamada sync, glFinish = aguardar conclusão dos comandos GL", "Режим синхронизации экрана: Выкл = без вызова sync, glFinish = ожидание завершения GL-команд", "屏幕同步模式：关 = 无 sync 调用，glFinish = 等待 GL 命令完成", "화면 동기화 모드: OFF = sync 호출 없음, glFinish = GL 명령 완료 대기", "وضع مزامنة الشاشة: Off = بدون استدعاء sync، glFinish = انتظار اكتمال أوامر GL", "Mode sinkronisasi layar: Off = tanpa panggilan sync, glFinish = tunggu perintah GL selesai", "Режим синхронізації екрана: Off = без виклику sync, glFinish = очікування завершення GL-команд", "Λειτουργία συγχρονισμού οθόνης: Off = χωρίς κλήση sync, glFinish = αναμονή ολοκλήρωσης εντολών GL", "Skärmsynkroniseringsläge: Off = inget sync-anrop, glFinish = vänta på GL-kommandon", "โหมดซิงค์หน้าจอ: Off = ไม่เรียก sync, glFinish = รอคำสั่ง GL เสร็จ", "Režim synchronizace obrazovky: Off = bez volání sync, glFinish = čekání na dokončení GL příkazů", "Skærmsynkroniseringstilstand: Off = intet sync-kald, glFinish = vent på GL-kommandoer", "Ekran senkronizasyon modu: Off = sync çağrısı yok, glFinish = GL komutlarının tamamlanmasını bekle", "Skjermsynkroniseringsmodus: Off = ingen sync-kall, glFinish = vent på GL-kommandoer", "Képernyőszinkron mód: Off = nincs sync hívás, glFinish = GL parancsok befejezésének várása", "Näytön synkronointitila: Off = ei sync-kutsua, glFinish = odota GL-komentojen valmistumista", "Chế độ đồng bộ màn hình: Off = không gọi sync, glFinish = chờ lệnh GL hoàn tất", "Tryb synchronizacji ekranu: Off = brak wywołania sync, glFinish = oczekiwanie na zakończenie poleceń GL", "Mod sincronizare ecran: Off = fără apel sync, glFinish = așteaptă finalizarea comenzilor GL"},
    {"When not in-game and the bottom screen is visible, confine the mouse cursor to the bottom screen area. Press ESC to release the cursor.", "ゲーム外で下画面が表示されているとき、マウスカーソルを下画面内に制限します。ESCで解除します。", "Außerhalb des Spiels, wenn der untere Bildschirm sichtbar ist, Mauszeiger auf den unteren Bildschirmbereich beschränken. ESC drücken, um den Zeiger freizugeben.", "Fuera del juego, con la pantalla inferior visible, confina el cursor al área inferior. Pulsa ESC para liberarlo.", "Hors jeu, lorsque l'écran inférieur est visible, confiner le curseur à cette zone. Appuyez sur Échap pour le libérer.", "Fuori dal gioco, con lo schermo inferiore visibile, limita il cursore all'area dello schermo inferiore. Premi ESC per liberarlo.", "Buiten het spel, wanneer het onderste scherm zichtbaar is, beperk de muisaanwijzer tot het onderste scherm. Druk op ESC om vrij te geven.", "Fora do jogo, com a tela inferior visível, confine o cursor à área inferior. Pressione ESC para liberar.", "Вне игры, когда нижний экран виден, ограничьте курсор областью нижнего экрана. Нажмите ESC для освобождения.", "非游戏时若下屏可见，将鼠标光标限制在下屏区域。按 ESC 释放光标。", "게임 중이 아니고 하단 화면이 보일 때 마우스 커서를 하단 화면 영역으로 제한합니다. ESC로 해제합니다.", "عندما لا تكون داخل اللعبة وتكون الشاشة السفلية مرئية، قيد مؤشر الفأرة بمنطقة الشاشة السفلية. اضغط ESC لتحرير المؤشر.", "Saat tidak dalam game dan layar bawah terlihat, batasi kursor mouse ke area layar bawah. Tekan ESC untuk melepaskan kursor.", "Поза грою, коли нижній екран видимий, обмежте курсор областю нижнього екрана. Натисніть ESC, щоб звільнити курсор.", "Όταν δεν είστε στο παιχνίδι και η κάτω οθόνη είναι ορατή, περιορίστε τον κέρσορα στην περιοχή της κάτω οθόνης. Πατήστε ESC για αποδέσμευση.", "När du inte är i spelet och den nedre skärmen syns, begränsa muspekaren till det nedre skärmområdet. Tryck ESC för att frigöra pekaren.", "เมื่อไม่ได้อยู่ในเกมและหน้าจอล่างมองเห็นได้ จำกัดเคอร์เซอร์เมาส์ให้อยู่ในพื้นที่หน้าจอล่าง กด ESC เพื่อปลดล็อก", "Mimo hru, když je spodní obrazovka viditelná, omezte kurzor myši na oblast spodní obrazovky. Stiskněte ESC pro uvolnění.", "Når du ikke er i spillet og den nederste skærm er synlig, begræns musemarkøren til det nederste skærmområde. Tryk ESC for at frigive markøren.", "Oyunda değilken ve alt ekran görünürken fare imlecini alt ekran alanına sınırlayın. İmleci serbest bırakmak için ESC'ye basın.", "Når du ikke er i spillet og den nedre skjermen er synlig, begrens musepekeren til det nedre skjermområdet. Trykk ESC for å frigjøre pekeren.", "Játékon kívül, ha az alsó képernyő látható, korlátozd az egérkurzort az alsó képernyő területére. ESC a kurzor felszabadításához.", "Kun et ole pelissä ja alanäyttö on näkyvissä, rajoita hiiren osoitin alanäytön alueelle. Paina ESC vapauttaaksesi osoittimen.", "Khi không trong game và màn hình dưới hiển thị, giới hạn con trỏ chuột trong vùng màn hình dưới. Nhấn ESC để giải phóng con trỏ.", "Poza grą, gdy dolny ekran jest widoczny, ogranicz kursor myszy do obszaru dolnego ekranu. Naciśnij ESC, aby zwolnić kursor.", "Când nu ești în joc și ecranul inferior este vizibil, limitează cursorul mouse-ului la zona ecranului inferior. Apasă ESC pentru a elibera cursorul."},
    {"In-game only, temporarily force Screen Sizing to Top Only and Screen Layout to Natural. Outside of gameplay, restore the normal window settings.", "ゲーム中だけ一時的に Screen Sizing を Top Only、Screen Layout を Natural に強制します。ゲーム外では通常のウィンドウ設定に戻します。", "Nur im Spiel vorübergehend Screen Sizing auf Top Only und Screen Layout auf Natural erzwingen. Außerhalb des Spiels werden die normalen Fenstereinstellungen wiederhergestellt.", "Solo en partida, fuerza temporalmente Screen Sizing a Top Only y Screen Layout a Natural. Fuera del juego, restaura los ajustes normales de ventana.", "En jeu uniquement, force temporairement Screen Sizing sur Top Only et Screen Layout sur Natural. Hors jeu, restaure les paramètres de fenêtre normaux.", "Solo in gioco, forza temporaneamente Screen Sizing su Top Only e Screen Layout su Natural. Fuori dal gioco, ripristina le impostazioni normali della finestra.", "Alleen in het spel, dwing tijdelijk Screen Sizing naar Top Only en Screen Layout naar Natural. Buiten het spel worden de normale vensterinstellingen hersteld.", "Somente no jogo, força temporariamente Screen Sizing para Top Only e Screen Layout para Natural. Fora do jogo, restaura as configurações normais da janela.", "Только в игре временно принудительно устанавливает Screen Sizing на Top Only и Screen Layout на Natural. Вне игры восстанавливает обычные настройки окна.", "仅在游戏中临时强制 Screen Sizing 为 Top Only、Screen Layout 为 Natural。游戏外恢复正常的窗口设置。", "게임 중에만 Screen Sizing을 Top Only, Screen Layout을 Natural로 일시 강제합니다. 게임 외에는 일반 창 설정으로 복원합니다.", "في اللعبة فقط، فرض Screen Sizing على Top Only وScreen Layout على Natural مؤقتاً. خارج اللعب، استعادة إعدادات النافذة العادية.", "Hanya dalam game, paksa Screen Sizing ke Top Only dan Screen Layout ke Natural sementara. Di luar gameplay, pulihkan pengaturan jendela normal.", "Лише в грі тимчасово примусово встановлює Screen Sizing на Top Only і Screen Layout на Natural. Поза грою відновлює звичайні налаштування вікна.", "Μόνο στο παιχνίδι, επιβάλλει προσωρινά Screen Sizing σε Top Only και Screen Layout σε Natural. Εκτός παιχνιδιού, επαναφέρει τις κανονικές ρυθμίσεις παραθύρου.", "Endast i spel, tvingar tillfälligt Screen Sizing till Top Only och Screen Layout till Natural. Utanför spelet återställs normala fönsterinställningar.", "เฉพาะในเกม บังคับ Screen Sizing เป็น Top Only และ Screen Layout เป็น Natural ชั่วคราว นอกเกมจะคืนค่าการตั้งค่าหน้าต่างปกติ", "Pouze ve hře dočasně vynutí Screen Sizing na Top Only a Screen Layout na Natural. Mimo hru obnoví běžná nastavení okna.", "Kun i spil, tvinger midlertidigt Screen Sizing til Top Only og Screen Layout til Natural. Uden for spil gendannes normale vinduesindstillinger.", "Yalnızca oyunda, geçici olarak Screen Sizing'i Top Only ve Screen Layout'u Natural yapar. Oyun dışında normal pencere ayarlarını geri yükler.", "Kun i spill, tvinger midlertidig Screen Sizing til Top Only og Screen Layout til Natural. Utenfor spill gjenopprettes normale vindusinnstillinger.", "Csak játék közben kényszeríti ideiglenesen a Screen Sizinget Top Only-ra és a Screen Layoutot Natural-re. Játékon kívül visszaállítja a normál ablakbeállításokat.", "Vain pelissä pakottaa tilapäisesti Screen Sizingin Top Only -tilaan ja Screen Layoutin Natural-tilaan. Pelin ulkopuolella palauttaa normaalit ikkuna-asetukset.", "Chỉ trong game, tạm thời ép Screen Sizing thành Top Only và Screen Layout thành Natural. Ngoài game, khôi phục cài đặt cửa sổ bình thường.", "Tylko w grze tymczasowo wymusza Screen Sizing na Top Only i Screen Layout na Natural. Poza grą przywraca normalne ustawienia okna.", "Doar în joc, forțează temporar Screen Sizing la Top Only și Screen Layout la Natural. În afara jocului, restaurează setările normale ale ferestrei."},
    {"Enable In-Game Aspect Ratio", "ゲーム内アスペクト比を有効化", "Seitenverhältnis im Spiel aktivieren", "Activar relación de aspecto en juego", "Activer le ratio d'aspect en jeu", "Attiva rapporto d'aspetto in gioco", "Beeldverhouding in spel inschakelen", "Ativar proporção de tela no jogo", "Включить соотношение сторон в игре", "启用游戏内宽高比", "게임 내 화면 비율 활성화", "تفعيل نسبة العرض إلى الارتفاع في اللعبة", "Aktifkan Rasio Aspek dalam Game", "Увімкнути співвідношення сторін у грі", "Ενεργοποίηση αναλογίας διαστάσεων στο παιχνίδι", "Aktivera bildförhållande i spel", "เปิดใช้อัตราส่วนภาพในเกม", "Povolit poměr stran ve hře", "Aktivér billedformat i spil", "Oyunda en-boy oranını etkinleştir", "Aktiver sideforhold i spill", "Játékbeli képarány engedélyezése", "Ota pelin kuvasuhde käyttöön", "Bật tỷ lệ khung hình trong game", "Włącz proporcje obrazu w grze", "Activează raportul de aspect în joc"},
    {"Aspect Ratio Mode", "アスペクト比モード", "Seitenverhältnis-Modus", "Modo de relación de aspecto", "Mode de format d'image", "Modalità proporzioni", "Beeldverhoudingsmodus", "Modo de proporção de tela", "Режим соотношения сторон", "宽高比模式", "화면 비율 모드", "وضع نسبة العرض إلى الارتفاع", "Mode Rasio Aspek", "Режим співвідношення сторін", "Λειτουργία αναλογίας διαστάσεων", "Bildförhållningsläge", "โหมดอัตราส่วนภาพ", "Režim poměru stran", "Billedformat-tilstand", "En-boy oranı modu", "Sideforholdsmodus", "Képarány mód", "Kuvasuhdetila", "Chế độ tỷ lệ khung hình", "Tryb proporcji obrazu", "Mod raport de aspect"},
    {"Auto (match Aspect Ratio)", "自動 (画面比率に合わせる)", "Automatisch (Seitenverhältnis anpassen)", "Automático (ajustar relación de aspecto)", "Auto (adapter au ratio d'aspect)", "Auto (adatta al rapporto d'aspetto)", "Auto (beeldverhouding aanpassen)", "Automático (ajustar proporção)", "Авто (по соотношению сторон)", "自动（匹配宽高比）", "자동 (화면 비율 맞춤)", "تلقائي (مطابقة نسبة العرض إلى الارتفاع)", "Otomatis (sesuaikan rasio aspek)", "Авто (за співвідношенням сторін)", "Αυτόματο (ταιριάζει με αναλογία διαστάσεων)", "Auto (matcha bildförhållande)", "อัตโนมัติ (ตรงอัตราส่วนภาพ)", "Auto (podle poměru stran)", "Auto (match billedformat)", "Otomatik (en boy oranını eşle)", "Auto (match sideforhold)", "Auto (képarány illesztése)", "Auto (sovita kuvasuhteeseen)", "Tự động (khớp tỷ lệ khung hình)", "Auto (dopasuj proporcje)", "Auto (potrivește raportul de aspect)"},
    {"5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)"},
    {"16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)"},
    {"16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9", "16:9"},
    {"21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9", "21:9"},
    {"Use DS Firmware Language (EU-style Auto Patch)", "DSファームウェア言語を使う (EU版風の自動パッチ)", "DS-Firmware-Sprache verwenden (EU-Auto-Patch)", "Usar idioma del firmware DS (parche automático estilo UE)", "Utiliser la langue du firmware DS (patch auto style UE)", "Usa lingua firmware DS (patch auto stile UE)", "DS-firmwaretaal gebruiken (EU auto-patch)", "Usar idioma do firmware DS (patch automático estilo UE)", "Использовать язык прошивки DS (автопатч в стиле EU)", "使用 DS 固件语言（欧版风格自动补丁）", "DS 펌웨어 언어 사용 (EU 스타일 자동 패치)", "استخدام لغة firmware DS (تصحيح تلقائي بأسلوب EU)", "Gunakan bahasa firmware DS (Auto Patch gaya EU)", "Використовувати мову прошивки DS (автопатч у стилі EU)", "Χρήση γλώσσας firmware DS (αυτόματο patch στυλ EU)", "Använd DS-firmwarespråk (EU-stil auto patch)", "ใช้ภาษา firmware DS (Auto Patch แบบ EU)", "Použít jazyk firmware DS (auto patch ve stylu EU)", "Brug DS-firmwaresprog (EU-stil auto patch)", "DS firmware dilini kullan (EU tarzı otomatik yama)", "Bruk DS-firmwarespråk (EU-stil auto patch)", "DS firmware nyelv használata (EU-stílusú auto patch)", "Käytä DS-firmwaren kieltä (EU-tyylinen auto patch)", "Dùng ngôn ngữ firmware DS (Auto Patch kiểu EU)", "Użyj języka firmware DS (auto patch w stylu EU)", "Folosește limba firmware DS (auto patch stil EU)"},
    {"Show Headshot Notification Online", "オンラインでヘッドショット通知を表示", "Kopfschuss-Benachrichtigung online anzeigen", "Mostrar notificación de headshot en línea", "Afficher la notification de headshot en ligne", "Mostra notifica headshot online", "Headshot-melding online tonen", "Mostrar notificação de headshot online", "Показывать уведомление о headshot онлайн", "在线显示爆头通知", "온라인 헤드샷 알림 표시", "إظهار إشعار headshot عبر الإنترنت", "Tampilkan notifikasi headshot online", "Показувати сповіщення про headshot онлайн", "Εμφάνιση ειδοποίησης headshot online", "Visa headshot-avisering online", "แสดงการแจ้งเตือน headshot ออนไลน์", "Zobrazit online upozornění na headshot", "Vis headshot-besked online", "Çevrimiçi headshot bildirimini göster", "Vis headshot-varsel online", "Headshot értesítés megjelenítése online", "Näytä headshot-ilmoitus verkossa", "Hiển thị thông báo headshot trực tuyến", "Pokaż powiadomienie o headshot online", "Afișează notificarea headshot online"},
    {"Show Enemy HP Meter Online", "オンラインで敵HPメーターを表示", "Feind-HP-Anzeige online anzeigen", "Mostrar medidor de PV del enemigo en línea", "Afficher la jauge PV ennemi en ligne", "Mostra barra HP nemico online", "Vijand-HP-balk online weergeven", "Mostrar medidor de HP do inimigo online", "Показывать шкалу HP врага онлайн", "在线显示敌人 HP 条", "온라인에서 적 HP 미터 표시", "إظهار مقياس HP العدو أونلاين", "Tampilkan meter HP musuh online", "Показувати шкалу HP ворога онлайн", "Εμφάνιση μετρητή HP εχθρού online", "Visa fiende-HP-mätare online", "แสดงมิเตอร์ HP ศัตรูออนไลน์", "Zobrazit ukazatel HP nepřítele online", "Vis fjende-HP-måler online", "Çevrimiçi düşman HP göstergesini göster", "Vis fiende-HP-måler på nett", "Ellenség HP mérő megjelenítése online", "Näytä vihollisen HP-mittari verkossa", "Hiện thanh HP địch trực tuyến", "Pokaż wskaźnik HP wroga online", "Afișează contorul HP inamic online"},
    {"Fix Noxus Blade Persistence on Death", "ノクサスのブレード残留を修正", "Noxus-Klinge bei Tod beibehalten beheben", "Corregir persistencia de espada Noxus al morir", "Corriger la persistance de la lame Noxus à la mort", "Correggi persistenza lama Noxus alla morte", "Noxus-kling persistentie bij dood corrigeren", "Corrigir persistência da lâmina Noxus ao morrer", "Исправить сохранение клинка Noxus при смерти", "修复死亡时 Noxus 之刃残留", "사망 시 Noxus 블레이드 잔존 수정", "إصلاح استمرار Noxus Blade عند الموت", "Perbaiki Persistensi Noxus Blade saat Mati", "Виправити збереження Noxus Blade після смерті", "Διόρθωση διατήρησης Noxus Blade στο θάνατο", "Åtgärda Noxus Blade-beständighet vid död", "แก้ไข Noxus Blade คงอยู่เมื่อตาย", "Opravit trvalost Noxus Blade při smrti", "Ret Noxus Blade-vedholdenhed ved død", "Ölümde Noxus Blade kalıcılığını düzelt", "Fiks Noxus Blade-vedholdenhet ved død", "Noxus Blade megmaradás javítása halálkor", "Korjaa Noxus Blade -pysyvyys kuolemassa", "Sửa Noxus Blade tồn tại khi chết", "Napraw utrzymywanie Noxus Blade po śmierci", "Corectează persistența Noxus Blade la moarte"},
    {"Friend/Rival Wi-Fi Active Bitset Fix (v2)", "フレンド/ライバルWi-Fi有効ビット修正 (v2)", "Freund/Rivale Wi-Fi-Aktiv-Bitset-Fix (v2)", "Corrección de bitset activo Wi-Fi amigo/rival (v2)", "Correctif bitset actif Wi-Fi ami/rival (v2)", "Fix bitset attivo Wi-Fi amico/rivale (v2)", "Fix actieve Wi-Fi-bitset vriend/rivaal (v2)", "Correção de bitset ativo Wi-Fi amigo/rival (v2)", "Исправление битсета активного Wi-Fi друга/соперника (v2)", "好友/对手 Wi-Fi 激活位集修复 (v2)", "친구/라이벌 Wi-Fi 활성 비트셋 수정 (v2)", "إصلاح Bitset نشط Wi-Fi للصديق/الخصم (v2)", "Perbaikan Bitset Aktif Wi-Fi Teman/Lawan (v2)", "Виправлення активного бітсету Wi-Fi друга/суперника (v2)", "Διόρθωση ενεργού bitset Wi-Fi φίλου/αντιπάλου (v2)", "Fix för aktiv Wi-Fi-bitset vän/rival (v2)", "แก้ไข Bitset ใช้งาน Wi-Fi เพื่อน/คู่แข่ง (v2)", "Oprava aktivního Wi-Fi bitsetu přítel/soupeř (v2)", "Fix for aktiv Wi-Fi-bitset ven/rival (v2)", "Arkadaş/Rakip Wi-Fi aktif bitset düzeltmesi (v2)", "Fix for aktiv Wi-Fi-bitset venn/rival (v2)", "Barát/Rivális Wi-Fi aktív bitset javítás (v2)", "Ystävä/Vastustaja Wi-Fi aktiivinen bitset-korjaus (v2)", "Sửa Bitset Wi-Fi hoạt động Bạn bè/Đối thủ (v2)", "Poprawka aktywnego bitsetu Wi-Fi znajomy/rywal (v2)", "Corecție bitset activ Wi-Fi prieten/rival (v2)"},
    {"Shadow Freeze Fix (Ice Wave full 3D angle check)", "シャドウフリーズ修正 (アイスウェーブの3D角度判定)", "Shadow-Freeze-Fix (Ice Wave: vollständige 3D-Winkelprüfung)", "Corrección de congelación de sombras (Ice Wave: comprobación 3D completa de ángulos)", "Correctif gel d'ombre (Ice Wave : vérification d'angle 3D complète)", "Fix congelamento ombre (Ice Wave: controllo angolo 3D completo)", "Schaduwbevriezingsfix (Ice Wave: volledige 3D-hoekcontrole)", "Correção de congelamento de sombras (Ice Wave: verificação 3D completa de ângulos)", "Исправление заморозки теней (Ice Wave: полная 3D-проверка угла)", "阴影冻结修复（Ice Wave 完整 3D 角度检测）", "그림자 정지 수정 (Ice Wave 전체 3D 각도 검사)", "إصلاح Shadow Freeze (فحص زاوية Ice Wave ثلاثي الأبعاد كامل)", "Perbaikan Shadow Freeze (pemeriksaan sudut Ice Wave 3D penuh)", "Виправлення Shadow Freeze (Ice Wave: повна 3D-перевірка кута)", "Διόρθωση Shadow Freeze (Ice Wave: πλήρης έλεγχος γωνίας 3D)", "Shadow Freeze-fix (Ice Wave: fullständig 3D-vinkelkontroll)", "แก้ Shadow Freeze (ตรวจมุม Ice Wave 3D เต็มรูปแบบ)", "Oprava Shadow Freeze (Ice Wave: úplná 3D kontrola úhlu)", "Shadow Freeze-rettelse (Ice Wave: fuld 3D-vinkelkontrol)", "Shadow Freeze düzeltmesi (Ice Wave tam 3D açı kontrolü)", "Shadow Freeze-fiks (Ice Wave: full 3D-vinkelkontroll)", "Shadow Freeze javítás (Ice Wave teljes 3D szögellenőrzés)", "Shadow Freeze -korjaus (Ice Wave: täysi 3D-kulmatarkistus)", "Sửa Shadow Freeze (kiểm tra góc Ice Wave 3D đầy đủ)", "Poprawka Shadow Freeze (Ice Wave: pełna kontrola kąta 3D)", "Remediere Shadow Freeze (Ice Wave: verificare completă unghi 3D)"},
    {"Expand Stage Selection (Unlock Extra Stages)", "ステージ選択を拡張 (追加ステージ解放)", "Stagewahl erweitern (Zusatzstages freischalten)", "Ampliar selección de escenarios (desbloquear extras)", "Étendre la sélection de stages (débloquer des stages supplémentaires)", "Espandi selezione stage (sblocca stage extra)", "Stagekeuze uitbreiden (extra stages ontgrendelen)", "Expandir seleção de fases (desbloquear fases extras)", "Расширить выбор арен (разблокировать дополнительные)", "扩展关卡选择（解锁额外关卡）", "스테이지 선택 확장 (추가 스테이지 잠금 해제)", "توسيع اختيار المراحل (فتح مراحل إضافية)", "Perluas pilihan stage (Buka stage ekstra)", "Розширити вибір арен (Розблокувати додаткові)", "Επέκταση επιλογής stage (Ξεκλείδωμα επιπλέον)", "Utöka stageval (Lås upp extra stages)", "ขยายการเลือกสเตจ (ปลดล็อกสเตจเพิ่ม)", "Rozšířit výběr stage (Odemknout extra)", "Udvid stagevalg (Lås ekstra op)", "Stage seçimini genişlet (Ekstra stage'leri aç)", "Utvid stagevalg (Lås opp ekstra)", "Stage választék bővítése (Extra stage-ek feloldása)", "Laajenna stage-valintaa (Avaa lisästaget)", "Mở rộng chọn stage (Mở khóa stage thêm)", "Rozszerz wybór stage (Odblokuj dodatkowe)", "Extinde selecția de stage (Deblochează extra)"},
    {"Also unlock additional stages", "追加ステージも解放", "Zusätzliche Stufen freischalten", "También desbloquear etapas adicionales", "Débloquer aussi des stages supplémentaires", "Sblocca anche fasi aggiuntive", "Ook extra levels ontgrendelen", "Também desbloquear fases adicionais", "Также разблокировать дополнительные этапы", "同时解锁额外关卡", "추가 스테이지도 잠금 해제", "فتح مراحل إضافية أيضاً", "Buka kunci tahap tambahan juga", "Також розблокувати додаткові етапи", "Ξεκλείδωση επιπλέον σταδίων", "Lås också upp extra banor", "ปลดล็อกด่านเพิ่มเติมด้วย", "Také odemknout další úrovně", "Lås også yderligere baner op", "Ek aşamaları da kilidini aç", "Lås også opp ekstra baner", "További pályák feloldása is", "Avaa myös lisäkentät", "Cũng mở khóa các màn bổ sung", "Odblokuj także dodatkowe etapy", "Deblochează și etape suplimentare"},
    {"Disable Double Damage Multiplier", "ダブルダメージ倍率を無効化", "Doppelten Schadensmultiplikator deaktivieren", "Desactivar multiplicador de daño doble", "Désactiver le multiplicateur de dégâts double", "Disattiva moltiplicatore danni doppi", "Dubbele schademultiplier uitschakelen", "Desativar multiplicador de dano duplo", "Отключить множитель двойного урона", "禁用双倍伤害倍率", "이중 피해 배율 비활성화", "تعطيل مضاعف الضرر المزدوج", "Nonaktifkan Pengganda Kerusakan Ganda", "Вимкнути подвоєний множник шкоди", "Απενεργοποίηση διπλού πολλαπλασιαστή ζημιάς", "Inaktivera dubbel skademultiplikator", "ปิดใช้ตัวคูณความเสียหายสองเท่า", "Zakázat dvojnásobný násobič poškození", "Deaktiver dobbelt skademultiplikator", "Çift hasar çarpanını devre dışı bırak", "Deaktiver dobbel skademultiplikator", "Dupla sebzésszorzó letiltása", "Poista kaksinkertainen vahinkokerroin käytöstä", "Tắt hệ số sát thương gấp đôi", "Wyłącz podwójny mnożnik obrażeń", "Dezactivează multiplicatorul dublu de daune"},
    {"Damage Notify Purple", "ダメージ通知を紫にする", "Schadensmeldung violett", "Notificación de daño en púrpura", "Notification de dégâts en violet", "Notifica danni viola", "Schademelding paars", "Notificação de dano roxa", "Уведомление об уроне фиолетовое", "伤害通知紫色", "피해 알림 보라색", "إشعار الضرر بنفسجي", "Notifikasi Kerusakan Ungu", "Сповіщення про урон фіолетове", "Ειδοποίηση ζημιάς μωβ", "Skadenotifiering lila", "แจ้งเตือนความเสียหายสีม่วง", "Upozornění na poškození fialové", "Skadesnotifikation lilla", "Hasar bildirimi mor", "Skadenotifikasjon lilla", "Sebzés értesítés lila", "Vahinkoilmoitus violetti", "Thông báo sát thương tím", "Powiadomienie o obrażeniach fioletowe", "Notificare daune mov"},
    {"Power-Ups: Pick Up With No Effect", "パワーアップ: 取得しても効果なし", "Power-Ups: Aufsammeln ohne Effekt", "Potenciadores: recoger sin efecto", "Bonus : ramassage sans effet", "Potenziamenti: raccogli senza effetto", "Power-ups: oppakken zonder effect", "Power-ups: recolher sem efeito", "Усиления: подбор без эффекта", "强化道具：拾取无效果", "파워업: 효과 없이 획득", "Power-Ups: التقاط بدون تأثير", "Power-Ups: Ambil tanpa efek", "Power-Ups: підбір без ефекту", "Power-Ups: μάζεμα χωρίς εφέ", "Power-ups: plocka upp utan effekt", "Power-Ups: เก็บโดยไม่มีเอฟเฟกต์", "Power-Ups: sebrání bez efektu", "Power-ups: saml op uden effekt", "Power-Ups: Etkisiz toplama", "Power-ups: plukk opp uten effekt", "Power-Ups: Felvétel effekt nélkül", "Power-Ups: Kerää ilman efektiä", "Power-Ups: Nhặt không hiệu ứng", "Power-Ups: Podnieś bez efektu", "Power-Ups: Ridicare fără efect"},
    {"Double Damage", "ダブルダメージ", "Doppelter Schaden", "Doble daño", "Dégâts doubles", "Danno doppio", "Dubbele schade", "Dano duplo", "Двойной урон", "双倍伤害", "이중 피해", "ضرر مضاعف", "Kerusakan ganda", "Подвійна шкода", "Διπλή ζημιά", "Dubbel skada", "ความเสียหายสองเท่า", "Dvojnásobné poškození", "Dobbel skade", "Çift hasar", "Dobbel skade", "Dupla sebzés", "Tupla vahinko", "Sát thương gấp đôi", "Podwójne obrażenia", "Daune duble"},
    {"Cloak", "クローク", "Tarnung", "Capa", "Camouflage", "Mimetizzazione", "Camouflage", "Camuflagem", "Маскировка", "隐身", "클로킹", "التخفي", "Kamuflase", "Маскування", "Καμουφλάζ", "Kamouflage", "พรางตัว", "Maskování", "Camouflage", "Kamuflaj", "Kamuflasje", "Álcázás", "Naamiointi", "Tàng hình", "Kamuflaż", "Camuflaj"},
    {"DEATH ALT", "デスオルト", "TOD-ALT", "ALT MUERTE", "ALT MORT", "ALT MORTE", "DOOD-ALT", "ALT MORTE", "ALT СМЕРТЬ", "死亡变形", "데스 변신", "ALT الموت", "ALT KEMATIAN", "ALT СМЕРТІ", "ALT ΘΑΝΑΤΟΥ", "DÖDS-ALT", "ALT ตาย", "ALT SMRTI", "DØDS-ALT", "ÖLÜM ALT", "DØDS-ALT", "HALÁL ALT", "KUOLEMA-ALT", "ALT CHẾT", "ALT ŚMIERCI", "ALT MOARTE"},
    {"Reset sensitivity values", "感度を既定値に戻す", "Empfindlichkeitswerte zurücksetzen", "Restablecer valores de sensibilidad", "Réinitialiser les valeurs de sensibilité", "Reimposta valori sensibilità", "Gevoeligheidswaarden resetten", "Redefinir valores de sensibilidade", "Сбросить значения чувствительности", "重置灵敏度值", "감도 값 초기화", "إعادة تعيين قيم الحساسية", "Atur ulang nilai sensitivitas", "Скинути значення чутливості", "Επαναφορά τιμών ευαισθησίας", "Återställ känslighetsvärden", "รีเซ็ตค่าความไว", "Obnovit hodnoty citlivosti", "Nulstil følsomhedsværdier", "Hassasiyet değerlerini sıfırla", "Tilbakestill følsomhetsverdier", "Érzékenységi értékek visszaállítása", "Palauta herkkyysarvot", "Đặt lại giá trị độ nhạy", "Resetuj wartości czułości", "Resetează valorile de sensibilitate"},
    {"Video quality: Low (High Performance)", "映像品質: 低 (高パフォーマンス)", "Videoqualität: Niedrig (Hohe Leistung)", "Calidad de vídeo: Baja (alto rendimiento)", "Qualité vidéo : Basse (hautes performances)", "Qualità video: Bassa (alte prestazioni)", "Videokwaliteit: Laag (hoge prestaties)", "Qualidade de vídeo: Baixa (alto desempenho)", "Качество видео: Низкое (высокая производительность)", "视频质量：低（高性能）", "비디오 품질: 낮음 (고성능)", "جودة الفيديو: منخفضة (أداء عالٍ)", "Kualitas video: Rendah (Performa tinggi)", "Якість відео: Низька (висока продуктивність)", "Ποιότητα βίντεο: Χαμηλή (υψηλή απόδοση)", "Videokvalitet: Låg (hög prestanda)", "คุณภาพวิดีโอ: ต่ำ (ประสิทธิภาพสูง)", "Kvalita videa: Nízká (vysoký výkon)", "Videokvalitet: Lav (høj ydeevne)", "Video kalitesi: Düşük (Yüksek performans)", "Videokvalitet: Lav (høy ytelse)", "Videó minőség: Alacsony (nagy teljesítmény)", "Videon laatu: Matala (korkea suorituskyky)", "Chất lượng video: Thấp (Hiệu năng cao)", "Jakość wideo: Niska (wysoka wydajność)", "Calitate video: Scăzută (performanță ridicată)"},
    {"Video quality: High (Lower Performance)", "映像品質: 高 (低パフォーマンス)", "Grafikqualität: Hoch (geringere Leistung)", "Calidad de vídeo: Alta (menor rendimiento)", "Qualité vidéo : Élevée (performances réduites)", "Qualità video: Alta (prestazioni inferiori)", "Videokwaliteit: Hoog (lagere prestaties)", "Qualidade de vídeo: Alta (menor desempenho)", "Качество видео: Высокое (ниже производительность)", "视频质量：高（性能较低）", "영상 품질: 높음 (성능 저하)", "جودة الفيديو: عالية (أداء أقل)", "Kualitas video: Tinggi (Performa lebih rendah)", "Якість відео: Висока (нижча продуктивність)", "Ποιότητα βίντεο: Υψηλή (Χαμηλότερη απόδοση)", "Videokvalitet: Hög (lägre prestanda)", "คุณภาพวิดีโอ: สูง (ประสิทธิภาพต่ำลง)", "Kvalita videa: Vysoká (nižší výkon)", "Videokvalitet: Høj (lavere ydeevne)", "Video kalitesi: Yüksek (Daha düşük performans)", "Videokvalitet: Høy (lavere ytelse)", "Videó minőség: Magas (Alacsonyabb teljesítmény)", "Videon laatu: Korkea (heikompi suorituskyky)", "Chất lượng video: Cao (Hiệu năng thấp hơn)", "Jakość wideo: Wysoka (niższa wydajność)", "Calitate video: Ridicată (performanță mai mică)"},
    {"Video quality: High2 (Recommended. Best Performance)", "映像品質: 高2 (推奨。最高パフォーマンス)", "Videoqualität: Hoch2 (Empfohlen. Beste Leistung)", "Calidad de vídeo: Alta2 (Recomendada. Mejor rendimiento)", "Qualité vidéo : Élevée2 (Recommandée. Meilleures performances)", "Qualità video: High2 (Consigliata. Migliori prestazioni)", "Videokwaliteit: High2 (Aanbevolen. Beste prestaties)", "Qualidade de vídeo: High2 (Recomendada. Melhor desempenho)", "Качество видео: High2 (Рекомендуется. Лучшая производительность)", "视频质量：High2（推荐。最佳性能）", "비디오 품질: High2 (권장. 최고 성능)", "جودة الفيديو: High2 (موصى به. أفضل أداء)", "Kualitas video: High2 (Disarankan. Performa terbaik)", "Якість відео: High2 (Рекомендовано. Найкраща продуктивність)", "Ποιότητα βίντεο: High2 (Συνιστάται. Καλύτερη απόδοση)", "Videokvalitet: High2 (Rekommenderas. Bästa prestanda)", "คุณภาพวิดีโอ: High2 (แนะนำ ประสิทธิภาพดีที่สุด)", "Kvalita videa: High2 (Doporučeno. Nejlepší výkon)", "Videokvalitet: High2 (Anbefalet. Bedste ydeevne)", "Video kalitesi: High2 (Önerilir. En iyi performans)", "Videokvalitet: High2 (Anbefalt. Beste ytelse)", "Videóminőség: High2 (Ajánlott. Legjobb teljesítmény)", "Videon laatu: High2 (Suositeltu. Paras suorituskyky)", "Chất lượng video: High2 (Khuyến nghị. Hiệu năng tốt nhất)", "Jakość wideo: High2 (Zalecane. Najlepsza wydajność)", "Calitate video: High2 (Recomandat. Cele mai bune performanțe)"},
    {"Apply SFX volume", "効果音音量を反映", "SFX-Lautstärke anwenden", "Aplicar volumen de efectos", "Appliquer le volume des effets", "Applica volume effetti", "SFX-volume toepassen", "Aplicar volume de efeitos", "Применить громкость эффектов", "应用音效音量", "효과음 볼륨 적용", "تطبيق مستوى صوت SFX", "Terapkan volume SFX", "Застосувати гучність SFX", "Εφαρμογή έντασης SFX", "Tillämpa SFX-volym", "ใช้ระดับเสียง SFX", "Použít hlasitost SFX", "Anvend SFX-lydstyrke", "SFX ses seviyesini uygula", "Bruk SFX-volum", "SFX hangerő alkalmazása", "Käytä SFX-äänenvoimakkuutta", "Áp dụng âm lượng SFX", "Zastosuj głośność SFX", "Aplică volumul SFX"},
    {"Apply music volume", "BGM音量を反映", "Musiklautstärke anwenden", "Aplicar volumen de música", "Appliquer le volume de la musique", "Applica volume musica", "Muziekvolume toepassen", "Aplicar volume da música", "Применить громкость музыки", "应用音乐音量", "음악 볼륨 적용", "تطبيق مستوى صوت الموسيقى", "Terapkan volume musik", "Застосувати гучність музики", "Εφαρμογή έντασης μουσικής", "Tillämpa musikvolym", "ใช้ระดับเสียงเพลง", "Použít hlasitost hudby", "Anvend musiklydstyrke", "Müzik ses seviyesini uygula", "Bruk musikkvolum", "Zene hangerő alkalmazása", "Käytä musiikin äänenvoimakkuutta", "Áp dụng âm lượng nhạc", "Zastosuj głośność muzyki", "Aplică volumul muzicii"},
    {"Apply the selected hunter to your license. (Renaming will update the save data)", "選択したハンターをライセンスに反映 (名前変更でセーブデータ更新)", "Ausgewählten Hunter auf die Lizenz anwenden. (Umbenennen aktualisiert die Speicherdaten)", "Aplicar el cazador seleccionado a tu licencia. (Cambiar el nombre actualizará la partida guardada)", "Appliquer le chasseur sélectionné à votre licence. (Renommer mettra à jour la sauvegarde)", "Applica il cacciatore selezionato alla licenza. (Rinominare aggiornerà i dati di salvataggio)", "Geselecteerde hunter op licentie toepassen. (Hernoemen werkt save bij)", "Aplicar o caçador selecionado à licença. (Renomear atualizará os dados de save)", "Применить выбранного охотника к лицензии. (Переименование обновит сохранение)", "将所选猎人应用到许可证。（重命名将更新存档数据）", "선택한 헌터를 라이선스에 적용합니다. (이름 변경 시 세이브 데이터가 업데이트됩니다)", "تطبيق الصياد المحدد على الترخيص. (إعادة التسمية ستحدّث بيانات الحفظ)", "Terapkan hunter yang dipilih ke lisensi. (Mengganti nama akan memperbarui data save)", "Застосувати вибраного мисливця до ліцензії. (Перейменування оновить збереження)", "Εφαρμογή του επιλεγμένου κυνηγού στην άδεια. (Η μετονομασία θα ενημερώσει τα δεδομένα αποθήκευσης)", "Tillämpa vald jägare på licensen. (Omdöpning uppdaterar spardata)", "ใช้ hunter ที่เลือกกับใบอนุญาต (การเปลี่ยนชื่อจะอัปเดตข้อมูลเซฟ)", "Použít vybraného lovce na licenci. (Přejmenování aktualizuje uložená data)", "Anvend valgt hunter på licensen. (Omdøbning opdaterer gemte data)", "Seçilen avcıyı lisansa uygula. (Yeniden adlandırma kayıt verilerini günceller)", "Bruk valgt hunter på lisensen. (Omdøping oppdaterer lagrede data)", "A kiválasztott vadász alkalmazása a licencre. (Az átnevezés frissíti a mentést)", "Käytä valittua metsästäjää lisenssiin. (Uudelleennimeäminen päivittää tallennuksen)", "Áp dụng hunter đã chọn vào giấy phép. (Đổi tên sẽ cập nhật dữ liệu save)", "Zastosuj wybranego myśliwego do licencji. (Zmiana nazwy zaktualizuje zapis)", "Aplică vânătorul selectat la licență. (Redenumirea va actualiza salvarea)"},
    {"Apply the selected color to your license. (Renaming will update the save data)", "選択した色をライセンスに反映 (名前変更でセーブデータ更新)", "Ausgewählte Farbe auf die Lizenz anwenden. (Umbenennen aktualisiert die Speicherdaten)", "Aplicar el color seleccionado a tu licencia. (Renombrar actualizará los datos de guardado)", "Appliquer la couleur sélectionnée à votre licence. (Renommer mettra à jour les données de sauvegarde)", "Applica il colore selezionato alla licenza. (Rinominare aggiornerà i dati di salvataggio)", "Pas de geselecteerde kleur toe op je licentie. (Hernoemen werkt de savegegevens bij)", "Aplicar a cor selecionada à licença. (Renomear atualizará os dados salvos)", "Применить выбранный цвет к лицензии. (Переименование обновит данные сохранения)", "将所选颜色应用到许可证。（重命名将更新存档数据）", "선택한 색상을 라이선스에 적용합니다. (이름 변경 시 저장 데이터가 업데이트됩니다)", "تطبيق اللون المحدد على الترخيص. (إعادة التسمية ستحدّث بيانات الحفظ)", "Terapkan warna yang dipilih ke lisensi. (Mengganti nama akan memperbarui data simpan)", "Застосувати вибраний колір до ліцензії. (Перейменування оновить дані збереження)", "Εφαρμογή του επιλεγμένου χρώματος στην άδεια. (Η μετονομασία θα ενημερώσει τα δεδομένα αποθήκευσης)", "Tillämpa den valda färgen på din licens. (Namnbyte uppdaterar spardata)", "ใช้สีที่เลือกกับใบอนุญาต (การเปลี่ยนชื่อจะอัปเดตข้อมูลเซฟ)", "Použít vybranou barvu na licenci. (Přejmenování aktualizuje uložená data)", "Anvend den valgte farve på din licens. (Omdøbning opdaterer gemte data)", "Seçilen rengi lisansına uygula. (Yeniden adlandırma kayıt verilerini günceller)", "Bruk den valgte fargen på lisensen. (Omdøping oppdaterer lagrede data)", "A kiválasztott szín alkalmazása a licencre. (Az átnevezés frissíti a mentési adatokat)", "Käytä valittua väriä lisenssiisi. (Uudelleennimeäminen päivittää tallennusdatat)", "Áp dụng màu đã chọn cho giấy phép. (Đổi tên sẽ cập nhật dữ liệu lưu)", "Zastosuj wybrany kolor do licencji. (Zmiana nazwy zaktualizuje dane zapisu)", "Aplică culoarea selectată licenței. (Redenumirea va actualiza datele salvate)"},
    {"Select the hunter to apply.", "反映するハンターを選択します。", "Jäger zum Anwenden auswählen.", "Selecciona el cazador a aplicar.", "Sélectionnez le chasseur à appliquer.", "Seleziona il cacciatore da applicare.", "Selecteer de jager om toe te passen.", "Selecione o caçador a aplicar.", "Выберите охотника для применения.", "选择要应用的猎人。", "적용할 헌터를 선택하세요.", "اختر الصياد لتطبيقه.", "Pilih pemburu yang akan diterapkan.", "Виберіть мисливця для застосування.", "Επιλέξτε τον κυνηγό για εφαρμογή.", "Välj jägare att tillämpa.", "เลือกนักล่าที่จะใช้", "Vyberte lovce k použití.", "Vælg jæger at anvende.", "Uygulanacak avcıyı seçin.", "Velg jeger som skal brukes.", "Válassza ki az alkalmazandó vadászt.", "Valitse sovellettava metsästäjä.", "Chọn thợ săn để áp dụng.", "Wybierz myśliwego do zastosowania.", "Selectează vânătorul de aplicat."},
    {"Select the color to apply.", "反映する色を選択します。", "Wähle die anzuwendende Farbe.", "Selecciona el color a aplicar.", "Sélectionnez la couleur à appliquer.", "Seleziona il colore da applicare.", "Selecteer de toe te passen kleur.", "Selecione a cor a aplicar.", "Выберите цвет для применения.", "选择要应用的颜色。", "적용할 색상을 선택하세요.", "اختر اللون المراد تطبيقه.", "Pilih warna yang akan diterapkan.", "Виберіть колір для застосування.", "Επιλέξτε το χρώμα προς εφαρμογή.", "Välj färg att tillämpa.", "เลือกสีที่จะใช้", "Vyberte barvu k použití.", "Vælg farve, der skal anvendes.", "Uygulanacak rengi seçin.", "Velg farge som skal brukes.", "Válassza ki az alkalmazandó színt.", "Valitse käytettävä väri.", "Chọn màu để áp dụng.", "Wybierz kolor do zastosowania.", "Selectează culoarea de aplicat."},
    {"Red(JP,KR)", "赤 (JP,KR)", "Rot (JP,KR)", "Rojo (JP,KR)", "Rouge (JP,KR)", "Rosso (JP,KR)", "Rood (JP,KR)", "Vermelho (JP,KR)", "Красный (JP,KR)", "红 (JP,KR)", "빨강 (JP,KR)", "أحمر (JP,KR)", "Merah (JP,KR)", "Червоний (JP,KR)", "Κόκκινο (JP,KR)", "Röd (JP,KR)", "แดง (JP,KR)", "Červená (JP,KR)", "Rød (JP,KR)", "Kırmızı (JP,KR)", "Rød (JP,KR)", "Piros (JP,KR)", "Punainen (JP,KR)", "Đỏ (JP,KR)", "Czerwony (JP,KR)", "Roșu (JP,KR)"},
    {"Blue(US)", "青 (US)", "Blau (US)", "Azul (US)", "Bleu (US)", "Blu (US)", "Blauw (US)", "Azul (US)", "Синий (US)", "蓝 (US)", "파랑 (US)", "أزرق (US)", "Biru (US)", "Синій (US)", "Μπλε (US)", "Blå (US)", "น้ำเงิน (US)", "Modrá (US)", "Blå (US)", "Mavi (US)", "Blå (US)", "Kék (US)", "Sininen (US)", "Xanh dương (US)", "Niebieski (US)", "Albastru (US)"},
    {"Green(EU)", "緑 (EU)", "Grün (EU)", "Verde (EU)", "Vert (EU)", "Verde (EU)", "Groen (EU)", "Verde (EU)", "Зелёный (EU)", "绿色 (EU)", "녹색 (EU)", "أخضر (EU)", "Hijau (EU)", "Зелений (EU)", "Πράσινο (EU)", "Grön (EU)", "เขียว (EU)", "Zelená (EU)", "Grøn (EU)", "Yeşil (EU)", "Grønn (EU)", "Zöld (EU)", "Vihreä (EU)", "Xanh lá (EU)", "Zielony (EU)", "Verde (EU)"},
    {"Auto Scale — Base", "自動スケール - 基準", "Automatische Skalierung — Basis", "Escala automática — Base", "Échelle automatique — Base", "Scala automatica — Base", "Automatische schaling — Basis", "Escala automática — Base", "Автомасштаб — База", "自动缩放 — 基准", "자동 크기 조절 — 기본", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis", "Automatische Skalierung — Basis"},
    {"Auto Scale (by Damage)", "自動スケール (ダメージ別)", "Auto-Skalierung (nach Schaden)", "Escala automática (por daño)", "Échelle auto (selon dégâts)", "Scala automatica (per danno)", "Auto-schaal (op schade)", "Escala automática (por dano)", "Автомасштаб (по урону)", "自动缩放（按伤害）", "자동 배율 (피해별)", "تحجيم تلقائي (حسب الضرر)", "Skala Otomatis (berdasarkan Kerusakan)", "Автомасштаб (за шкодою)", "Αυτόματη κλίμακα (κατά ζημιά)", "Autoskalning (efter skada)", "ปรับขนาดอัตโนมัติ (ตามความเสียหาย)", "Automatické škálování (podle poškození)", "Autoskalering (efter skade)", "Otomatik ölçek (hasara göre)", "Autoskalering (etter skade)", "Automatikus skálázás (sebzés szerint)", "Automaattinen skaalaus (vaurion mukaan)", "Tỷ lệ tự động (theo sát thương)", "Skala automatyczna (wg obrażeń)", "Scalare automată (după daune)"},
    {"Disabled (vanilla 25)", "無効 (バニラ25)", "Deaktiviert (Vanilla 25)", "Desactivado (vanilla 25)", "Désactivé (vanilla 25)", "Disabilitato (vanilla 25)", "Uitgeschakeld (vanilla 25)", "Desativado (vanilla 25)", "Отключено (vanilla 25)", "禁用 (原版 25)", "비활성화 (바닐라 25)", "معطّل (vanilla 25)", "Nonaktif (vanilla 25)", "Вимкнено (vanilla 25)", "Απενεργοποιημένο (vanilla 25)", "Inaktiverad (vanilla 25)", "ปิดใช้งาน (vanilla 25)", "Zakázáno (vanilla 25)", "Deaktiveret (vanilla 25)", "Devre dışı (vanilla 25)", "Deaktivert (vanilla 25)", "Letiltva (vanilla 25)", "Poissa käytöstä (vanilla 25)", "Đã tắt (vanilla 25)", "Wyłączone (vanilla 25)", "Dezactivat (vanilla 25)"},
    {"Fixed", "固定", "Fest", "Fijo", "Fixe", "Fisso", "Vast", "Fixo", "Фиксированный", "固定", "고정", "ثابت", "Tetap", "Фіксований", "Σταθερό", "Fast", "คงที่", "Pevné", "Fast", "Sabit", "Fast", "Rögzített", "Kiinteä", "Cố định", "Stały", "Fix"},
    {"Fixed Threshold", "固定しきい値", "Fester Schwellenwert", "Umbral fijo", "Seuil fixe", "Soglia fissa", "Vaste drempel", "Limite fixo", "Фиксированный порог", "固定阈值", "고정 임계값", "عتبة ثابتة", "Ambang tetap", "Фіксований поріг", "Σταθερό κατώφλι", "Fast tröskel", "เกณฑ์คงที่", "Pevný práh", "Fast tærskel", "Sabit eşik", "Fast terskel", "Rögzített küszöb", "Kiinteä kynnys", "Ngưỡng cố định", "Stały próg", "Prag fix"},
    {"Per Damage (Low / Medium / High)", "ダメージ別 (低/中/高)", "Pro Schaden (Niedrig / Mittel / Hoch)", "Por daño (Bajo / Medio / Alto)", "Par dégâts (Faible / Moyen / Élevé)", "Per danno (Basso / Medio / Alto)", "Per schade (Laag / Gemiddeld / Hoog)", "Por dano (Baixo / Médio / Alto)", "По урону (Низкий / Средний / Высокий)", "按伤害（低 / 中 / 高）", "피해별 (낮음 / 보통 / 높음)", "حسب الضرر (منخفض / متوسط / مرتفع)", "Per kerusakan (Rendah / Sedang / Tinggi)", "За шкодою (Низький / Середній / Високий)", "Ανά ζημιά (Χαμηλή / Μέτρια / Υψηλή)", "Per skada (Låg / Medel / Hög)", "ตามความเสียหาย (ต่ำ / กลาง / สูง)", "Podle poškození (Nízké / Střední / Vysoké)", "Pr. skade (Lav / Mellem / Høj)", "Hasara göre (Düşük / Orta / Yüksek)", "Per skade (Lav / Middels / Høy)", "Sebzés szerint (Alacsony / Közepes / Magas)", "Vaurion mukaan (Matala / Keskitaso / Korkea)", "Theo sát thương (Thấp / Trung bình / Cao)", "Według obrażeń (Niskie / Średnie / Wysokie)", "Per daune (Scăzut / Mediu / Ridicat)"},
    {"Per Damage — Low", "ダメージ別 - 低", "Pro Schaden — Niedrig", "Por daño — Bajo", "Par dégâts — Faible", "Per danno — Basso", "Per schade — Laag", "Por dano — Baixo", "По урону — Низкий", "按伤害 — 低", "피해별 — 낮음", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig", "Pro Schaden — Niedrig"},
    {"Per Damage — Medium", "ダメージ別 - 中", "Pro Schaden — Mittel", "Por daño — Medio", "Par dégâts — Moyen", "Per danno — Medio", "Per schade — Gemiddeld", "Por dano — Médio", "По урону — Средний", "按伤害 — 中", "피해별 — 보통", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel", "Pro Schaden — Mittel"},
    {"Per Damage — High", "ダメージ別 - 高", "Pro Schaden — Hoch", "Por daño — Alto", "Par dégâts — Élevé", "Per danno — Alto", "Per schade — Hoog", "Por dano — Alto", "По урону — Высокий", "按伤害 — 高", "피해별 — 높음", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch", "Pro Schaden — Hoch"},
    {"Developer-only option. Currently disabled.", "開発者向けオプションです。現在は無効です。", "Nur für Entwickler. Derzeit deaktiviert.", "Opción solo para desarrolladores. Actualmente desactivada.", "Option réservée aux développeurs. Actuellement désactivée.", "Opzione solo per sviluppatori. Attualmente disabilitata.", "Alleen voor ontwikkelaars. Momenteel uitgeschakeld.", "Opção apenas para desenvolvedores. Atualmente desativada.", "Опция только для разработчиков. Сейчас отключена.", "仅限开发者的选项。当前已禁用。", "개발자 전용 옵션입니다. 현재 비활성화되어 있습니다.", "خيار للمطورين فقط. معطل حالياً.", "Opsi khusus pengembang. Saat ini dinonaktifkan.", "Опція лише для розробників. Зараз вимкнено.", "Επιλογή μόνο για προγραμματιστές. Προσωρινά απενεργοποιημένη.", "Endast för utvecklare. För närvarande inaktiverad.", "ตัวเลือกสำหรับนักพัฒนาเท่านั้น ปิดใช้งานอยู่", "Pouze pro vývojáře. Momentálně zakázáno.", "Kun for udviklere. I øjeblikket deaktiveret.", "Yalnızca geliştiricilere özel seçenek. Şu anda devre dışı.", "Kun for utviklere. For øyeblikket deaktivert.", "Csak fejlesztőknek. Jelenleg letiltva.", "Vain kehittäjille. Tällä hetkellä poissa käytöstä.", "Tùy chọn chỉ dành cho nhà phát triển. Hiện đang tắt.", "Opcja tylko dla deweloperów. Obecnie wyłączona.", "Opțiune doar pentru dezvoltatori. Momentan dezactivată."},
    {"Developer-only option enabled in this build.", "このビルドでは開発者向けオプションが有効です。", "Entwickleroption in diesem Build aktiviert.", "Opción solo para desarrolladores activada en esta compilación.", "Option réservée aux développeurs activée dans cette version.", "Opzione solo per sviluppatori abilitata in questa build.", "Ontwikkelaarsoptie ingeschakeld in deze build.", "Opção exclusiva para desenvolvedores ativada nesta compilação.", "Опция только для разработчиков включена в этой сборке.", "此版本中已启用仅限开发者的选项。", "이 빌드에서 개발자 전용 옵션이 활성화되어 있습니다.", "خيار للمطورين فقط مفعّل في هذا الإصدار.", "Opsi khusus pengembang diaktifkan di build ini.", "Опція лише для розробників увімкнена в цій збірці.", "Επιλογή μόνο για προγραμματιστές ενεργή σε αυτό το build.", "Utvecklaralternativ aktiverat i denna build.", "เปิดใช้ตัวเลือกสำหรับนักพัฒนาใน build นี้", "Možnost pouze pro vývojáře povolena v tomto sestavení.", "Udviklerindstilling aktiveret i denne build.", "Bu derlemede yalnızca geliştiricilere özel seçenek etkin.", "Utvikleralternativ aktivert i denne builden.", "Fejlesztői opció engedélyezve ebben a buildben.", "Vain kehittäjille tarkoitettu vaihtoehto käytössä tässä buildissä.", "Tùy chọn chỉ dành cho nhà phát triển được bật trong bản build này.", "Opcja tylko dla deweloperów włączona w tej kompilacji.", "Opțiune doar pentru dezvoltatori activată în această compilare."},
    {"Developer-only option. Build with MELONPRIME_ENABLE_DEVELOPER_FEATURES to enable it.", "開発者向けオプションです。有効にするには MELONPRIME_ENABLE_DEVELOPER_FEATURES 付きでビルドしてください。", "Nur für Entwickler. Mit MELONPRIME_ENABLE_DEVELOPER_FEATURES kompilieren, um es zu aktivieren.", "Opción solo para desarrolladores. Compila con MELONPRIME_ENABLE_DEVELOPER_FEATURES para activarla.", "Option réservée aux développeurs. Compilez avec MELONPRIME_ENABLE_DEVELOPER_FEATURES pour l'activer.", "Opzione solo per sviluppatori. Compila con MELONPRIME_ENABLE_DEVELOPER_FEATURES per abilitarla.", "Alleen voor ontwikkelaars. Bouw met MELONPRIME_ENABLE_DEVELOPER_FEATURES om het in te schakelen.", "Opção apenas para desenvolvedores. Compile com MELONPRIME_ENABLE_DEVELOPER_FEATURES para ativá-la.", "Только для разработчиков. Соберите с MELONPRIME_ENABLE_DEVELOPER_FEATURES для включения.", "仅限开发者选项。需使用 MELONPRIME_ENABLE_DEVELOPER_FEATURES 构建才能启用。", "개발자 전용 옵션입니다. 활성화하려면 MELONPRIME_ENABLE_DEVELOPER_FEATURES로 빌드하세요.", "خيار للمطورين فقط. قم بالبناء باستخدام MELONPRIME_ENABLE_DEVELOPER_FEATURES لتفعيله.", "Opsi khusus pengembang. Build dengan MELONPRIME_ENABLE_DEVELOPER_FEATURES untuk mengaktifkannya.", "Опція лише для розробників. Зберіть з MELONPRIME_ENABLE_DEVELOPER_FEATURES, щоб увімкнути.", "Επιλογή μόνο για προγραμματιστές. Κάντε build με MELONPRIME_ENABLE_DEVELOPER_FEATURES για ενεργοποίηση.", "Endast för utvecklare. Bygg med MELONPRIME_ENABLE_DEVELOPER_FEATURES för att aktivera.", "ตัวเลือกสำหรับนักพัฒนาเท่านั้น สร้างด้วย MELONPRIME_ENABLE_DEVELOPER_FEATURES เพื่อเปิดใช้", "Pouze pro vývojáře. Sestavte s MELONPRIME_ENABLE_DEVELOPER_FEATURES pro povolení.", "Kun for udviklere. Byg med MELONPRIME_ENABLE_DEVELOPER_FEATURES for at aktivere.", "Yalnızca geliştiriciler için. Etkinleştirmek için MELONPRIME_ENABLE_DEVELOPER_FEATURES ile derleyin.", "Kun for utviklere. Bygg med MELONPRIME_ENABLE_DEVELOPER_FEATURES for å aktivere.", "Csak fejlesztőknek. Fordítsd MELONPRIME_ENABLE_DEVELOPER_FEATURES flaggel az engedélyezéshez.", "Vain kehittäjille. Ota käyttöön kääntämällä MELONPRIME_ENABLE_DEVELOPER_FEATURES -lipulla.", "Tùy chọn chỉ dành cho nhà phát triển. Build với MELONPRIME_ENABLE_DEVELOPER_FEATURES để bật.", "Opcja tylko dla deweloperów. Zbuduj z MELONPRIME_ENABLE_DEVELOPER_FEATURES, aby włączyć.", "Opțiune doar pentru dezvoltatori. Compilează cu MELONPRIME_ENABLE_DEVELOPER_FEATURES pentru activare."},
    {"Features in this section are still in development and are not ready for public release. They may or may not be released.", "このセクションの機能は開発中で、公開リリースの準備はまだできていません。今後公開されるとは限りません。", "Funktionen in diesem Abschnitt befinden sich noch in der Entwicklung und sind nicht für die Veröffentlichung bereit. Eine Veröffentlichung ist nicht garantiert.", "Las funciones de esta sección aún están en desarrollo y no están listas para su lanzamiento público. Puede que se publiquen o no.", "Les fonctionnalités de cette section sont encore en développement et ne sont pas prêtes pour une diffusion publique. Elles pourront être publiées ou non.", "Le funzioni in questa sezione sono ancora in sviluppo e non sono pronte per il rilascio pubblico. Potrebbero essere rilasciate o meno.", "Functies in dit gedeelte zijn nog in ontwikkeling en niet klaar voor openbare release. Ze worden mogelijk wel of niet uitgebracht.", "Os recursos desta seção ainda estão em desenvolvimento e não estão prontos para lançamento público. Podem ou não ser lançados.", "Функции в этом разделе ещё в разработке и не готовы к публичному выпуску. Они могут быть выпущены или нет.", "此部分功能仍在开发中，尚未准备好公开发布。将来可能会发布，也可能不会。", "이 섹션의 기능은 아직 개발 중이며 공개 출시 준비가 되지 않았습니다. 출시 여부는 미정입니다.", "الميزات في هذا القسم لا تزال قيد التطوير وليست جاهزة للإصدار العام. قد تُصدر أو لا تُصدر.", "Fitur di bagian ini masih dalam pengembangan dan belum siap untuk rilis publik. Mungkin akan atau tidak akan dirilis.", "Функції в цьому розділі ще в розробці та не готові до публічного випуску. Вони можуть бути випущені або ні.", "Οι λειτουργίες σε αυτήν την ενότητα βρίσκονται ακόμη σε ανάπτυξη και δεν είναι έτοιμες για δημόσια κυκλοφορία. Μπορεί να κυκλοφορήσουν ή όχι.", "Funktioner i detta avsnitt är fortfarande under utveckling och är inte redo för offentlig release. De kan komma att släppas eller inte.", "ฟีเจอร์ในส่วนนี้ยังอยู่ระหว่างพัฒนาและยังไม่พร้อมสำหรับการเผยแพร่สาธารณะ อาจหรืออาจไม่ถูกปล่อยออกมา", "Funkce v této sekci jsou stále ve vývoji a nejsou připraveny k veřejnému vydání. Mohou být vydány, ale nemusí.", "Funktioner i dette afsnit er stadig under udvikling og er ikke klar til offentlig udgivelse. De kan blive udgivet eller ej.", "Bu bölümdeki özellikler hâlâ geliştirilme aşamasındadır ve genel sürüme hazır değildir. Yayınlanabilir veya yayınlanmayabilir.", "Funksjoner i denne seksjonen er fortsatt under utvikling og er ikke klare for offentlig utgivelse. De kan bli utgitt eller ikke.", "Az ebben a szekcióban lévő funkciók még fejlesztés alatt állnak, és nem készenek nyilvános kiadásra. Lehet, hogy megjelennek, de az is lehet, hogy nem.", "Tämän osion ominaisuudet ovat vielä kehityksessä eivätkä ole valmiita julkiseen julkaisuun. Ne voidaan julkaista tai olla julkaisematta.", "Các tính năng trong phần này vẫn đang phát triển và chưa sẵn sàng phát hành công khai. Chúng có thể được phát hành hoặc không.", "Funkcje w tej sekcji są nadal w trakcie rozwoju i nie są gotowe do publicznego wydania. Mogą zostać wydane lub nie.", "Funcțiile din această secțiune sunt încă în dezvoltare și nu sunt pregătite pentru lansare publică. Pot fi lansate sau nu."},
    {"Controls how the game's current aim direction follows the target aim direction.", "ゲーム内の現在の照準方向を、目標の照準方向へどう追従させるかを設定します。", "Steuert, wie die aktuelle Zielrichtung im Spiel der Zielrichtung folgt.", "Controla cómo la dirección de puntería actual del juego sigue la dirección objetivo.", "Contrôle la façon dont la direction de visée actuelle du jeu suit la direction cible.", "Controlla come la direzione di mira attuale del gioco segue la direzione di mira target.", "Bepaalt hoe de huidige richtrichting in het spel de doelrichting volgt.", "Controla como a direção de mira atual do jogo segue a direção alvo.", "Определяет, как текущее направление прицела в игре следует за целевым.", "控制游戏中当前瞄准方向如何跟随目标瞄准方向。", "게임 내 현재 조준 방향이 목표 조준 방향을 따르는 방식을 제어합니다.", "يتحكم في كيفية متابعة اتجاه التصويب الحالي في اللعبة لاتجاه التصويب المستهدف.", "Mengontrol bagaimana arah bidik saat ini di game mengikuti arah bidik target.", "Керує тим, як поточний напрям прицілу в грі слідує за цільовим.", "Ελέγχει πώς η τρέχουσα κατεύθυνση στόχευσης στο παιχνίδι ακολουθεί την κατεύθυνση-στόχο.", "Styr hur spelets nuvarande siktriktning följer målsiktriktningen.", "ควบคุมว่าทิศทางเล็งปัจจุบันในเกมติดตามทิศทางเป้าหมายอย่างไร", "Řídí, jak aktuální směr míření ve hře sleduje cílový směr.", "Styrer hvordan spillets nuværende sigteretning følger målsigteretningen.", "Oyunun mevcut nişan yönünün hedef nişan yönünü nasıl takip ettiğini kontrol eder.", "Styrer hvordan spillets nåværende sikteretning følger målsikteretningen.", "Szabályozza, hogy a játék aktuális célzási iránya hogyan követi a célcélzási irányt.", "Ohjaa, miten pelin nykyinen tähtäyssuunta seuraa kohdetta.", "Điều khiển cách hướng ngắm hiện tại trong game theo hướng ngắm mục tiêu.", "Kontroluje, jak aktualny kierunek celowania w grze podąża za celem.", "Controlează modul în care direcția curentă de țintire din joc urmează direcția țintă."},
    {"Checked: use the native ARM9 game function hook. Unchecked: use the older touch/menu simulation path.", "オン: ゲーム本来のARM9関数をフックして使います。オフ: 従来のタッチ/メニュー模擬処理を使います。", "Aktiviert: nativen ARM9-Spielfunktions-Hook verwenden. Deaktiviert: älteren Touch-/Menü-Simulationspfad verwenden.", "Marcado: usar el hook nativo de función ARM9 del juego. Desmarcado: usar la ruta antigua de simulación táctil/menú.", "Coché : utiliser le hook natif de fonction ARM9 du jeu. Décoché : utiliser l'ancien chemin de simulation tactile/menu.", "Selezionato: usa l'hook nativo della funzione ARM9 del gioco. Deselezionato: usa il percorso di simulazione touch/menu precedente.", "Aangevinkt: native ARM9-spelfunctiehook gebruiken. Uitgevinkt: oudere touch/menu-simulatie gebruiken.", "Marcado: usar hook nativo da função ARM9 do jogo. Desmarcado: usar caminho antigo de simulação touch/menu.", "Отмечено: использовать нативный хук функции ARM9 игры. Снято: использовать старый путь симуляции касания/меню.", "勾选：使用原生 ARM9 游戏函数钩子。取消勾选：使用旧的触摸/菜单模拟路径。", "선택: 네이티브 ARM9 게임 함수 후크 사용. 해제: 이전 터치/메뉴 시뮬레이션 경로 사용.", "محدد: استخدام خطاف دالة ARM9 الأصلية للعبة. غير محدد: استخدام مسار محاكاة اللمس/القائمة القديم.", "Dicentang: gunakan hook fungsi ARM9 native game. Tidak dicentang: gunakan jalur simulasi sentuh/menu lama.", "Увімкнено: використовувати нативний хук функції ARM9 гри. Вимкнено: використовувати старий шлях симуляції дотику/меню.", "Επιλεγμένο: χρήση native ARM9 hook. Μη επιλεγμένο: χρήση παλαιότερης προσομοίωσης αφής/μενού.", "Ikryssad: använd native ARM9-hook. Avmarkerad: använd äldre touch/meny-simulering.", "เลือก: ใช้ native ARM9 hook ไม่เลือก: ใช้เส้นทางจำลองสัมผัส/เมนูแบบเก่า", "Zaškrtnuto: použít nativní ARM9 hook. Nezaškrtnuto: použít starší simulaci dotyku/menu.", "Markeret: brug native ARM9-hook. Ikke markeret: brug ældre touch/menu-simulering.", "İşaretli: native ARM9 oyun fonksiyonu hook'u kullan. İşaretsiz: eski dokunma/menü simülasyon yolunu kullan.", "Avkrysset: bruk native ARM9-hook. Ikke avkrysset: bruk eldre touch/meny-simulering.", "Bejelölve: natív ARM9 játékfüggvény hook. Nincs bejelölve: régebbi érintés/menü szimuláció.", "Valittu: käytä natiivia ARM9-hookia. Ei valittu: käytä vanhempaa kosketus/valikko-simulaatiota.", "Đã chọn: dùng hook hàm ARM9 native. Bỏ chọn: dùng đường mô phỏng chạm/menu cũ.", "Zaznaczone: użyj natywnego hooka ARM9. Odznaczone: użyj starszej symulacji dotyku/menu.", "Bifat: folosește hook-ul nativ ARM9. Nebifat: folosește calea veche de simulare touch/meniu."},
    {"Checked: inject a native fire edge inside the game's Biped fire update. Unchecked: use the older fixed input/overlay path.", "オン: ゲーム本来の二足射撃更新内へ射撃入力エッジを注入します。オフ: 従来の固定入力/オーバーレイ経路を使います。", "Aktiviert: Feuer-Edge nativ in das Biped-Feuer-Update des Spiels einschleusen. Deaktiviert: älteren festen Eingabe-/Overlay-Pfad verwenden.", "Activado: inyecta un borde de disparo nativo en la actualización de fuego Biped del juego. Desactivado: usa la ruta antigua de entrada/superposición fija.", "Coché : injecte un signal de tir natif dans la mise à jour de tir Biped du jeu. Décoché : utilise l'ancien chemin d'entrée/superposition fixe.", "Selezionato: inietta un segnale di fuoco nativo nell'aggiornamento fuoco Biped del gioco. Deselezionato: usa il vecchio percorso fisso input/overlay.", "Aangevinkt: injecteert een native vuur-edge in de Biped-vuurupdate van het spel. Uitgevinkt: gebruikt het oudere vaste invoer/overlay-pad.", "Marcado: injeta um sinal de disparo nativo na atualização de fogo Biped do jogo. Desmarcado: usa o caminho antigo fixo de entrada/sobreposição.", "Включено: внедряет нативный сигнал выстрела в обновление огня Biped игры. Выключено: использует старый фиксированный путь ввода/оверлея.", "勾选：在游戏 Biped 射击更新中注入原生射击信号。取消勾选：使用旧的固定输入/叠加路径。", "선택: 게임 Biped 사격 업데이트에 네이티브 발사 신호를 주입합니다. 해제: 이전 고정 입력/오버레이 경로를 사용합니다.", "محدد: حقن إشارة إطلاق أصلية في تحديث إطلاق Biped باللعبة. غير محدد: استخدام مسار الإدخال/التراكب الثابت الأقدم.", "Dicentang: menyuntikkan sinyal tembak asli ke pembaruan tembak Biped game. Tidak dicentang: gunakan jalur input/overlay tetap lama.", "Увімкнено: впроваджує нативний сигнал пострілу в оновлення вогню Biped гри. Вимкнено: використовує старий фіксований шлях вводу/накладання.", "Επιλεγμένο: εισάγει εγγενές σήμα πυροβολισμού στην ενημέρωση πυροβολισμού Biped. Μη επιλεγμένο: χρησιμοποιεί την παλιά σταθερή διαδρομή εισόδου/επικάλυψης.", "Ikryssat: injicerar en inbyggd eldkant i spelets Biped-elduppdatering. Avmarkerat: använder den äldre fasta inmatnings-/overlay-vägen.", "เลือก: ฉีดสัญญาณยิงแบบเนทีฟในการอัปเดตยิง Biped ของเกม ไม่เลือก: ใช้เส้นทางอินพุต/โอเวอร์เลย์แบบคงที่เดิม", "Zaškrtnuto: vloží nativní signál střelby do aktualizace střelby Biped hry. Nezaškrtnuto: použije starší pevnou cestu vstupu/překryvu.", "Markeret: injicerer et native skudsignal i spillets Biped-skydopdatering. Ikke markeret: bruger den ældre faste input/overlay-sti.", "İşaretli: oyunun Biped ateş güncellemesine yerel ateş sinyali enjekte eder. İşaretsiz: eski sabit giriş/overlay yolunu kullanır.", "Avkrysset: injiserer et native skuddsignal i spillets Biped-skuddoppdatering. Ikke avkrysset: bruker den eldre faste inndata/overlay-stien.", "Bejelölve: natív tüzelési jelet injektál a játék Biped tüzelési frissítésébe. Nincs bejelölve: a régi fix bemenet/overlay útvonalat használja.", "Valittu: injektoi natiivin tulisignaalin pelin Biped-tulituksen päivitykseen. Ei valittu: käyttää vanhaa kiinteää syöte/peittokuvapolkua.", "Đã chọn: chèn tín hiệu bắn gốc vào cập nhật bắn Biped của game. Bỏ chọn: dùng đường dẫn nhập/overlay cố định cũ.", "Zaznaczone: wstrzykuje natywny sygnał strzału do aktualizacji ognia Biped gry. Odznaczone: używa starszej stałej ścieżki wejścia/nakładki.", "Bifat: injectează un semnal de foc nativ în actualizarea focului Biped a jocului. Nebifat: folosește calea veche fixă de intrare/suprapunere."},
    {"Checked: use the native ARM9 TransformRequest hook. Unchecked: use the older touch/menu simulation path.", "オン: ゲーム本来のARM9変形要求フックを使います。オフ: 従来のタッチ/メニュー模擬処理を使います。", "Aktiviert: nativen ARM9-TransformRequest-Hook verwenden. Deaktiviert: älteren Touch-/Menü-Simulationspfad verwenden.", "Marcado: usar el hook nativo ARM9 TransformRequest. Desmarcado: usar la ruta antigua de simulación táctil/menú.", "Coché : utiliser le hook natif ARM9 TransformRequest. Décoché : utiliser l'ancien chemin de simulation tactile/menu.", "Selezionato: usa l'hook nativo ARM9 TransformRequest. Deselezionato: usa il percorso di simulazione touch/menu precedente.", "Aangevinkt: native ARM9 TransformRequest-hook gebruiken. Uitgevinkt: oudere touch/menu-simulatiepad gebruiken.", "Marcado: usar o hook nativo ARM9 TransformRequest. Desmarcado: usar o caminho antigo de simulação touch/menu.", "Включено: использовать нативный хук ARM9 TransformRequest. Выключено: использовать старый путь симуляции касания/меню.", "勾选：使用原生 ARM9 变形请求钩子。取消勾选：使用旧的触摸/菜单模拟路径。", "체크: 네이티브 ARM9 TransformRequest 후크 사용. 해제: 이전 터치/메뉴 시뮬레이션 경로 사용.", "محدد: استخدم خطاف ARM9 TransformRequest الأصلي. غير محدد: استخدم مسار محاكاة اللمس/القائمة القديم.", "Dicentang: gunakan hook ARM9 TransformRequest asli. Tidak dicentang: gunakan jalur simulasi sentuh/menu lama.", "Увімкнено: використовувати нативний хук ARM9 TransformRequest. Вимкнено: використовувати старий шлях симуляції дотику/меню.", "Επιλεγμένο: χρήση native ARM9 TransformRequest hook. Μη επιλεγμένο: χρήση παλαιότερης διαδρομής προσομοίωσης αφής/μενού.", "Ikryssad: använd native ARM9 TransformRequest-hook. Ej ikryssad: använd äldre touch/meny-simuleringsväg.", "เลือก: ใช้ ARM9 TransformRequest hook แบบ native ไม่เลือก: ใช้เส้นทางจำลองสัมผัส/เมนูแบบเก่า", "Zaškrtnuto: použít nativní ARM9 TransformRequest hook. Nezaškrtnuto: použít starší cestu simulace dotyku/menu.", "Markeret: brug native ARM9 TransformRequest-hook. Ikke markeret: brug ældre touch/menu-simuleringssti.", "İşaretli: yerel ARM9 TransformRequest kancasını kullan. İşaretsiz: eski dokunma/menü simülasyon yolunu kullan.", "Avkrysset: bruk native ARM9 TransformRequest-hook. Ikke avkrysset: bruk eldre touch/meny-simuleringssti.", "Bejelölve: natív ARM9 TransformRequest hook használata. Nincs bejelölve: régebbi érintés/menü szimulációs útvonal.", "Valittu: käytä natiivia ARM9 TransformRequest -koukkua. Ei valittu: käytä vanhempaa kosketus/valikkosimulaatiopolkua.", "Đã chọn: dùng hook ARM9 TransformRequest gốc. Bỏ chọn: dùng đường mô phỏng chạm/menu cũ.", "Zaznaczone: użyj natywnego hooka ARM9 TransformRequest. Odznaczone: użyj starszej ścieżki symulacji dotyku/menu.", "Bifat: folosește hook-ul nativ ARM9 TransformRequest. Debifat: folosește calea veche de simulare atingere/meniu."},
    {"Checked: use the current in-game zoom binding from the player's control preset. Unchecked: use the older fixed R-button path.", "オン: 現在の操作プリセットにあるゲーム内ズーム割り当てを使います。オフ: 従来の固定Rボタン経路を使います。", "Aktiviert: aktuelle Zoom-Belegung aus dem Steuerungs-Preset verwenden. Deaktiviert: älteren festen R-Tasten-Pfad verwenden.", "Marcado: usa la asignación de zoom en el juego del preset de controles del jugador. Desmarcado: usa la ruta fija antigua del botón R.", "Coché : utilise la touche de zoom en jeu du preset de contrôles du joueur. Décoché : utilise l'ancien chemin fixe du bouton R.", "Selezionato: usa l'assegnazione zoom in gioco del preset controlli del giocatore. Deselezionato: usa il percorso fisso del pulsante R precedente.", "Aangevinkt: gebruik de huidige in-game zoomtoets uit het besturingspreset van de speler. Uitgevinkt: gebruik het oudere vaste R-knoppad.", "Marcado: usa a atribuição de zoom no jogo do preset de controles do jogador. Desmarcado: usa o caminho fixo antigo do botão R.", "Включено: используется текущая привязка зума в игре из пресета управления игрока. Выключено: используется старый фиксированный путь кнопки R.", "勾选：使用玩家控制预设中的当前游戏内缩放绑定。取消勾选：使用旧的固定 R 键路径。", "선택: 플레이어 조작 프리셋의 현재 게임 내 줌 바인딩 사용. 해제: 이전 고정 R 버튼 경로 사용.", "محدد: استخدم ربط التكبير الحالي داخل اللعبة من preset التحكم للاعب. غير محدد: استخدم مسار زر R الثابت القديم.", "Dicentang: gunakan binding zoom dalam game saat ini dari preset kontrol pemain. Tidak dicentang: gunakan jalur tombol R tetap yang lama.", "Увімкнено: використовується поточне призначення зуму в грі з пресету керування гравця. Вимкнено: використовується старий фіксований шлях кнопки R.", "Επιλεγμένο: χρησιμοποιεί την τρέχουσα δέσμευση ζουμ στο παιχνίδι από το preset ελέγχου του παίκτη. Μη επιλεγμένο: χρησιμοποιεί την παλιά σταθερή διαδρομή κουμπιού R.", "Ikryssad: använd nuvarande zoombindning i spelet från spelarens kontrollpreset. Ej ikryssad: använd den äldre fasta R-knappvägen.", "เลือก: ใช้การผูกซูมในเกมปัจจุบันจาก preset การควบคุมของผู้เล่น ไม่เลือก: ใช้เส้นทางปุ่ม R คงที่แบบเก่า", "Zaškrtnuto: použije aktuální herní přiřazení zoomu z presetu ovládání hráče. Nezaškrtnuto: použije starší pevnou cestu tlačítka R.", "Markeret: brug den aktuelle in-game zoom-tilknytning fra spillerens kontrolpreset. Ikke markeret: brug den ældre faste R-knap-sti.", "İşaretli: oyuncunun kontrol ön ayarından mevcut oyun içi zoom atamasını kullan. İşaretsiz: eski sabit R düğmesi yolunu kullan.", "Avkrysset: bruk gjeldende in-game zoom-binding fra spillerens kontrollpreset. Ikke avkrysset: bruk den eldre faste R-knapp-stien.", "Bejelölve: a játékos irányítási presetjének aktuális játékbeli zoom hozzárendelését használja. Nincs bejelölve: a régi fix R gomb útvonalat használja.", "Valittu: käytä pelaajan ohjausesiasetuksen nykyistä pelin zoom-sidontaa. Ei valittu: käytä vanhaa kiinteää R-painikepolkua.", "Đã chọn: dùng phím zoom trong game hiện tại từ preset điều khiển của người chơi. Bỏ chọn: dùng đường dẫn nút R cố định cũ.", "Zaznaczone: używa bieżącego przypisania zoomu w grze z presetu sterowania gracza. Odznaczone: używa starszej stałej ścieżki przycisku R.", "Bifat: folosește legarea zoomului în joc curentă din presetul de control al jucătorului. Debifat: folosește vechiul traseu fix al butonului R."},
    {"Checked: toggle native weapon zoom by calling the game's SetPlayerScopeZoom setter. Unchecked with New Method also off: use Legacy fixed R-button input.", "オン: ゲーム本来のズーム切替処理を呼んで武器ズームを切り替えます。新方式もオフの場合は、旧方式の固定Rボタン入力を使います。", "Aktiviert: Waffen-Zoom per SetPlayerScopeZoom des Spiels umschalten. Deaktiviert und Neue Methode aus: Legacy-Eingabe mit fester R-Taste.", "Marcado: alternar el zoom nativo del arma llamando a SetPlayerScopeZoom del juego. Desmarcado y Método nuevo también desactivado: usar entrada fija del botón R heredada.", "Coché : basculer le zoom d'arme natif via SetPlayerScopeZoom du jeu. Décoché et Nouvelle méthode aussi désactivée : utiliser l'entrée héritée du bouton R fixe.", "Selezionato: attiva/disattiva lo zoom nativo dell'arma chiamando SetPlayerScopeZoom del gioco. Deselezionato e Nuovo metodo disattivato: usa l'input legacy del pulsante R fisso.", "Aangevinkt: native wapenzoom schakelen via SetPlayerScopeZoom van het spel. Uitgevinkt en Nieuwe methode ook uit: legacy vaste R-knopinvoer gebruiken.", "Marcado: alternar zoom nativo da arma chamando SetPlayerScopeZoom do jogo. Desmarcado e Novo método também desativado: usar entrada fixa do botão R legado.", "Включено: переключение нативного зума оружия через SetPlayerScopeZoom игры. Выключено и Новый метод тоже выкл.: использовать устаревший ввод кнопки R.", "勾选：调用游戏的 SetPlayerScopeZoom 切换原生武器缩放。未勾选且新方法也关闭：使用旧版固定 R 键输入。", "체크: 게임의 SetPlayerScopeZoom을 호출하여 네이티브 무기 줌 전환. 체크 해제 및 새 방식도 OFF: 레거시 고정 R 버튼 입력 사용.", "محدد: تبديل تكبير السلاح الأصلي باستدعاء SetPlayerScopeZoom للعبة. غير محدد والطريقة الجديدة أيضًا معطلة: استخدم إدخال زر R الثابت القديم.", "Dicentang: alihkan zoom senjata native dengan memanggil SetPlayerScopeZoom game. Tidak dicentang dan Metode Baru juga off: gunakan input tombol R tetap Legacy.", "Увімкнено: перемикання нативного зуму зброї через SetPlayerScopeZoom гри. Вимкнено і Новий метод теж вимк.: використовувати застарілий фіксований ввід кнопки R.", "Επιλεγμένο: εναλλαγή εγγενούς ζουμ όπλου καλώντας SetPlayerScopeZoom του παιχνιδιού. Μη επιλεγμένο και Νέα μέθοδος επίσης off: χρήση παλαιού σταθερού εισόδου κουμπιού R.", "Ikryssad: växla native vapenzoom via spelets SetPlayerScopeZoom. Avmarkerad och Ny metod också av: använd legacy fast R-knappinmatning.", "เลือก: สลับซูมอาวุธ native โดยเรียก SetPlayerScopeZoom ของเกม ไม่เลือกและวิธีใหม่ปิดด้วย: ใช้ปุ่ม R แบบ Legacy", "Zaškrtnuto: přepínání nativního zoomu zbraně voláním SetPlayerScopeZoom hry. Nezaškrtnuto a Nová metoda také vyp.: použít legacy pevný vstup tlačítka R.", "Markeret: skift native våbenzoom via spillets SetPlayerScopeZoom. Afmarkeret og Ny metode også fra: brug legacy fast R-knapinput.", "İşaretli: oyunun SetPlayerScopeZoom'unu çağırarak native silah zoom'unu değiştir. İşaretsiz ve Yeni Yöntem de kapalı: Legacy sabit R düğmesi girişi kullan.", "Avkrysset: bytt native våpenzoom via spillets SetPlayerScopeZoom. Avkrysset og Ny metode også av: bruk legacy fast R-knappinndata.", "Bejelölve: natív fegyverzoom váltása a játék SetPlayerScopeZoom hívásával. Nincs bejelölve és Új módszer is ki: legacy fix R gomb bemenet.", "Valittu: vaihda natiivi asezoomia kutsumalla pelin SetPlayerScopeZoom. Ei valittu ja Uusi menetelmä myös pois: käytä legacy kiinteää R-painiketta.", "Đã chọn: bật/tắt zoom vũ khí native bằng SetPlayerScopeZoom của game. Bỏ chọn và Phương pháp mới cũng tắt: dùng nút R cố định Legacy.", "Zaznaczone: przełącz natywny zoom broni przez SetPlayerScopeZoom gry. Odznaczone i Nowa metoda też wył.: użyj legacy stałego przycisku R.", "Bifat: comută zoom-ul nativ al armei apelând SetPlayerScopeZoom al jocului. Nebifat și Metoda nouă dezactivată: folosește intrarea legacy buton R fix."},
    {"Setting Key: Metroid.Apply.SfxVolume (check to apply)", "設定キー: Metroid.Apply.SfxVolume (オンで反映)", "Einstellungsschlüssel: Metroid.Apply.SfxVolume (zum Anwenden aktivieren)", "Clave de ajuste: Metroid.Apply.SfxVolume (marcar para aplicar)", "Clé de réglage : Metroid.Apply.SfxVolume (cocher pour appliquer)", "Chiave impostazione: Metroid.Apply.SfxVolume (seleziona per applicare)", "Instellingssleutel: Metroid.Apply.SfxVolume (aanvinken om toe te passen)", "Chave de configuração: Metroid.Apply.SfxVolume (marcar para aplicar)", "Ключ настройки: Metroid.Apply.SfxVolume (отметить для применения)", "设置键：Metroid.Apply.SfxVolume（勾选以应用）", "설정 키: Metroid.Apply.SfxVolume (선택하여 적용)", "مفتاح الإعداد: Metroid.Apply.SfxVolume (حدد للتطبيق)", "Kunci pengaturan: Metroid.Apply.SfxVolume (centang untuk menerapkan)", "Ключ налаштування: Metroid.Apply.SfxVolume (позначте для застосування)", "Κλειδί ρύθμισης: Metroid.Apply.SfxVolume (επιλέξτε για εφαρμογή)", "Inställningsnyckel: Metroid.Apply.SfxVolume (kryssa i för att tillämpa)", "คีย์การตั้งค่า: Metroid.Apply.SfxVolume (เลือกเพื่อนำไปใช้)", "Klíč nastavení: Metroid.Apply.SfxVolume (zaškrtněte pro použití)", "Indstillingsnøgle: Metroid.Apply.SfxVolume (marker for at anvende)", "Ayar anahtarı: Metroid.Apply.SfxVolume (uygulamak için işaretle)", "Innstillingsnøkkel: Metroid.Apply.SfxVolume (kryss av for å bruke)", "Beállításkulcs: Metroid.Apply.SfxVolume (jelöld be az alkalmazáshoz)", "Asetusavain: Metroid.Apply.SfxVolume (valitse käyttääksesi)", "Khóa cài đặt: Metroid.Apply.SfxVolume (chọn để áp dụng)", "Klucz ustawienia: Metroid.Apply.SfxVolume (zaznacz, aby zastosować)", "Cheie setare: Metroid.Apply.SfxVolume (bifează pentru a aplica)"},
    {"Setting Key: Metroid.Volume.SFX (0–9)", "設定キー: Metroid.Volume.SFX (0-9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Clave de ajuste: Metroid.Volume.SFX (0–9)", "Clé de réglage : Metroid.Volume.SFX (0–9)", "Chiave impostazione: Metroid.Volume.SFX (0–9)", "Instellingssleutel: Metroid.Volume.SFX (0–9)", "Chave de configuração: Metroid.Volume.SFX (0–9)", "Ключ настройки: Metroid.Volume.SFX (0–9)", "设置键：Metroid.Volume.SFX (0–9)", "설정 키: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)"},
    {"Setting Key: Metroid.Apply.MusicVolume (check to apply)", "設定キー: Metroid.Apply.MusicVolume (オンで反映)", "Einstellungsschlüssel: Metroid.Apply.MusicVolume (zum Anwenden aktivieren)", "Clave de ajuste: Metroid.Apply.MusicVolume (marcar para aplicar)", "Clé de réglage : Metroid.Apply.MusicVolume (cocher pour appliquer)", "Chiave impostazione: Metroid.Apply.MusicVolume (seleziona per applicare)", "Instellingssleutel: Metroid.Apply.MusicVolume (aanvinken om toe te passen)", "Chave de configuração: Metroid.Apply.MusicVolume (marcar para aplicar)", "Ключ настройки: Metroid.Apply.MusicVolume (отметьте для применения)", "设定键：Metroid.Apply.MusicVolume（勾选以应用）", "설정 키: Metroid.Apply.MusicVolume (체크하여 적용)", "مفتاح الإعداد: Metroid.Apply.MusicVolume (حدد للتطبيق)", "Kunci Pengaturan: Metroid.Apply.MusicVolume (centang untuk menerapkan)", "Ключ налаштування: Metroid.Apply.MusicVolume (позначте для застосування)", "Κλειδί ρύθμισης: Metroid.Apply.MusicVolume (επιλέξτε για εφαρμογή)", "Inställningsnyckel: Metroid.Apply.MusicVolume (kryssa i för att tillämpa)", "คีย์การตั้งค่า: Metroid.Apply.MusicVolume (เลือกเพื่อใช้)", "Klíč nastavení: Metroid.Apply.MusicVolume (zaškrtněte pro použití)", "Indstillingsnøgle: Metroid.Apply.MusicVolume (marker for at anvende)", "Ayar Anahtarı: Metroid.Apply.MusicVolume (uygulamak için işaretle)", "Innstillingsnøkkel: Metroid.Apply.MusicVolume (kryss av for å bruke)", "Beállításkulcs: Metroid.Apply.MusicVolume (jelöld be az alkalmazáshoz)", "Asetusavain: Metroid.Apply.MusicVolume (valitse käyttöön)", "Khóa cài đặt: Metroid.Apply.MusicVolume (chọn để áp dụng)", "Klucz ustawienia: Metroid.Apply.MusicVolume (zaznacz, aby zastosować)", "Cheie setare: Metroid.Apply.MusicVolume (bifează pentru aplicare)"},
    {"Setting Key: Metroid.Volume.Music (0–9)", "設定キー: Metroid.Volume.Music (0-9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Clave de ajuste: Metroid.Volume.Music (0–9)", "Clé de réglage : Metroid.Volume.Music (0–9)", "Chiave impostazione: Metroid.Volume.Music (0–9)", "Instellingssleutel: Metroid.Volume.Music (0–9)", "Chave de configuração: Metroid.Volume.Music (0–9)", "Ключ настройки: Metroid.Volume.Music (0–9)", "设置键：Metroid.Volume.Music (0–9)", "설정 키: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)"},
    {"Enables applying the selected hunter to the license.", "選択したハンターをライセンスに反映できるようにします。", "Ermöglicht, den ausgewählten Hunter auf die Lizenz anzuwenden.", "Permite aplicar el cazador seleccionado a la licencia.", "Permet d'appliquer le chasseur sélectionné à la licence.", "Consente di applicare il cacciatore selezionato alla licenza.", "Maakt het mogelijk de geselecteerde hunter op de licentie toe te passen.", "Permite aplicar o caçador selecionado à licença.", "Позволяет применить выбранного охотника к лицензии.", "允许将所选猎人应用到许可证。", "선택한 헌터를 라이선스에 적용할 수 있게 합니다.", "يتيح تطبيق الصياد المحدد على الترخيص.", "Memungkinkan menerapkan hunter yang dipilih ke lisensi.", "Дозволяє застосувати вибраного мисливця до ліцензії.", "Επιτρέπει την εφαρμογή του επιλεγμένου κυνηγού στην άδεια.", "Möjliggör att tillämpa vald jägare på licensen.", "เปิดใช้การนำ hunter ที่เลือกไปใช้กับใบอนุญาต", "Umožňuje použít vybraného lovce na licenci.", "Gør det muligt at anvende valgt hunter på licensen.", "Seçilen avcıyı lisansa uygulamayı etkinleştirir.", "Gjør det mulig å bruke valgt hunter på lisensen.", "Lehetővé teszi a kiválasztott vadász licencre való alkalmazását.", "Mahdollistaa valitun metsästäjän soveltamisen lisenssiin.", "Cho phép áp dụng hunter đã chọn vào giấy phép.", "Umożliwia zastosowanie wybranego myśliwego do licencji.", "Permite aplicarea vânătorului selectat la licență."},
    {"Enables applying the selected color to the license.", "選択した色をライセンスに反映できるようにします。", "Ermöglicht das Anwenden der ausgewählten Farbe auf die Lizenz.", "Permite aplicar el color seleccionado a la licencia.", "Permet d'appliquer la couleur sélectionnée à la licence.", "Consente di applicare il colore selezionato alla licenza.", "Maakt het toepassen van de geselecteerde kleur op de licentie mogelijk.", "Permite aplicar a cor selecionada à licença.", "Позволяет применить выбранный цвет к лицензии.", "允许将所选颜色应用到许可证。", "선택한 색상을 라이선스에 적용할 수 있게 합니다.", "يتيح تطبيق اللون المحدد على الترخيص.", "Mengaktifkan penerapan warna yang dipilih ke lisensi.", "Дозволяє застосувати вибраний колір до ліцензії.", "Επιτρέπει την εφαρμογή του επιλεγμένου χρώματος στην άδεια.", "Gör det möjligt att tillämpa den valda färgen på licensen.", "เปิดใช้การใช้สีที่เลือกกับใบอนุญาต", "Umožňuje použít vybranou barvu na licenci.", "Gør det muligt at anvende den valgte farve på licensen.", "Seçilen rengin lisansa uygulanmasını etkinleştirir.", "Gjør det mulig å bruke den valgte fargen på lisensen.", "Lehetővé teszi a kiválasztott szín licencre való alkalmazását.", "Mahdollistaa valitun värin käyttämisen lisenssissä.", "Cho phép áp dụng màu đã chọn cho giấy phép.", "Umożliwia zastosowanie wybranego koloru do licencji.", "Permite aplicarea culorii selectate licenței."},
    {"Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete, DwmFlush = wait for DWM compositor (Windows only)", "画面同期方式: オフ = 同期呼び出しなし、glFinish = GLコマンド完了まで待機、DwmFlush = DWMコンポジター待機 (Windowsのみ)", "Bildschirm-Sync-Modus: OFF = kein Sync-Aufruf, glFinish = auf GL-Befehle warten, DwmFlush = auf DWM-Compositor warten (nur Windows)", "Modo de sincronización de pantalla: Desactivado = sin llamada de sync, glFinish = esperar a que terminen los comandos GL, DwmFlush = esperar al compositor DWM (solo Windows)", "Mode de sync écran : Désactivé = pas d'appel de sync, glFinish = attendre la fin des commandes GL, DwmFlush = attendre le compositeur DWM (Windows uniquement)", "Modalità sync schermo: Off = nessuna chiamata sync, glFinish = attendi completamento comandi GL, DwmFlush = attendi compositor DWM (solo Windows)", "Scherm-syncmodus: Uit = geen sync-aanroep, glFinish = wacht tot GL-opdrachten voltooid zijn, DwmFlush = wacht op DWM-compositor (alleen Windows)", "Modo de sincronização de tela: Desligado = sem chamada de sync, glFinish = aguardar conclusão dos comandos GL, DwmFlush = aguardar compositor DWM (somente Windows)", "Режим синхронизации экрана: Выкл = без вызова sync, glFinish = ожидание завершения GL-команд, DwmFlush = ожидание композитора DWM (только Windows)", "画面同步模式：关 = 不调用 sync，glFinish = 等待 GL 命令完成，DwmFlush = 等待 DWM 合成器（仅 Windows）", "화면 동기화 모드: 끔 = sync 호출 없음, glFinish = GL 명령 완료 대기, DwmFlush = DWM compositor 대기 (Windows만)", "وضع مزامنة الشاشة: Off = بلا استدعاء sync، glFinish = انتظار أوامر GL، DwmFlush = انتظار مركب DWM (Windows فقط)", "Mode sinkronisasi layar: Off = tanpa panggilan sync, glFinish = tunggu perintah GL selesai, DwmFlush = tunggu kompositor DWM (hanya Windows)", "Режим синхронізації екрана: Off = без виклику sync, glFinish = очікування завершення GL-команд, DwmFlush = очікування композитора DWM (лише Windows)", "Λειτουργία συγχρονισμού οθόνης: Off = χωρίς κλήση sync, glFinish = αναμονή ολοκλήρωσης εντολών GL, DwmFlush = αναμονή συνθέτη DWM (μόνο Windows)", "Skärmsynkroniseringsläge: Av = inget sync-anrop, glFinish = vänta på GL-kommandon, DwmFlush = vänta på DWM-kompositör (endast Windows)", "โหมดซิงก์หน้าจอ: Off = ไม่เรียก sync, glFinish = รอคำสั่ง GL เสร็จ, DwmFlush = รอ DWM compositor (เฉพาะ Windows)", "Režim synchronizace obrazovky: Vyp = žádné volání sync, glFinish = čekání na dokončení GL příkazů, DwmFlush = čekání na DWM compositor (pouze Windows)", "Skærmsynkroniseringstilstand: Fra = intet sync-kald, glFinish = vent på GL-kommandoer, DwmFlush = vent på DWM-compositor (kun Windows)", "Ekran senkronizasyon modu: Kapalı = sync çağrısı yok, glFinish = GL komutlarının bitmesini bekle, DwmFlush = DWM compositor'ı bekle (yalnızca Windows)", "Skjermsynkroniseringsmodus: Av = ingen sync-kall, glFinish = vent på GL-kommandoer, DwmFlush = vent på DWM-compositor (kun Windows)", "Képernyőszinkronizálási mód: Ki = nincs sync hívás, glFinish = GL parancsok befejezésére vár, DwmFlush = DWM compositorra vár (csak Windows)", "Näytön synkronointitila: Pois = ei sync-kutsua, glFinish = odota GL-komentojen valmistumista, DwmFlush = odota DWM-compositoria (vain Windows)", "Chế độ đồng bộ màn hình: Tắt = không gọi sync, glFinish = chờ lệnh GL hoàn tất, DwmFlush = chờ DWM compositor (chỉ Windows)", "Tryb synchronizacji ekranu: Wył. = brak wywołania sync, glFinish = czekaj na zakończenie poleceń GL, DwmFlush = czekaj na kompozytor DWM (tylko Windows)", "Mod sincronizare ecran: Off = fără apel sync, glFinish = așteaptă finalizarea comenzilor GL, DwmFlush = așteaptă compozitorul DWM (doar Windows)"},
    {"Off: No sync (lowest latency, but the display may look choppy). glFinish: Smoother display by waiting for rendering to fully complete each frame. Automatically disabled during FastForward/SlowMo.", "オフ: 同期なし (最小遅延ですが、表示がカクつくことがあります)。glFinish: 各フレームの描画完了を待つことで表示を滑らかにします。早送り/スローモーション中は自動的に無効になります。", "Aus: Keine Synchronisation (niedrigste Latenz, Anzeige kann ruckeln). glFinish: Glattere Anzeige durch Warten auf vollständiges Rendering pro Frame. Wird bei Schnellvorlauf/SlowMo automatisch deaktiviert.", "Desactivado: Sin sincronización (menor latencia, pero la pantalla puede verse entrecortada). glFinish: Pantalla más fluida esperando a que el renderizado termine cada fotograma. Se desactiva automáticamente durante avance rápido/cámara lenta.", "Désactivé : Pas de synchro (latence minimale, mais l'affichage peut saccader). glFinish : Affichage plus fluide en attendant la fin du rendu de chaque image. Désactivé automatiquement pendant avance rapide/ralenti.", "Off: Nessuna sincronizzazione (latenza minima, ma il display può sembrare a scatti). glFinish: Display più fluido attendendo il completamento del rendering di ogni frame. Disabilitato automaticamente durante avanzamento rapido/ralenti.", "Uit: Geen synchronisatie (laagste latentie, maar het beeld kan haperen). glFinish: Vloeiender beeld door te wachten tot rendering per frame volledig is. Automatisch uitgeschakeld tijdens snel vooruit/langzaam.", "Desligado: Sem sincronização (menor latência, mas a tela pode parecer travada). glFinish: Tela mais suave aguardando a conclusão da renderização de cada quadro. Desativado automaticamente durante avanço rápido/câmera lenta.", "Выкл.: Без синхронизации (минимальная задержка, но изображение может дёргаться). glFinish: Более плавное изображение за счёт ожидания полного рендеринга каждого кадра. Автоматически отключается при ускорении/замедлении.", "关闭：不同步（延迟最低，但画面可能卡顿）。glFinish：等待每帧渲染完成以获得更流畅的画面。快进/慢动作时自动禁用。", "끔: 동기화 없음 (지연 최소, 화면이 끊길 수 있음). glFinish: 각 프레임 렌더링 완료를 기다려 더 부드러운 화면. 고속/슬로모 중 자동 비활성화.", "إيقاف: بدون مزامنة (أقل تأخير، لكن العرض قد يبدو متقطعاً). glFinish: عرض أكثر سلاسة بالانتظار حتى يكتمل العرض لكل إطار. يُعطّل تلقائياً أثناء FastForward/SlowMo.", "Mati: Tanpa sinkronisasi (latensi terendah, tampilan mungkin patah-patah). glFinish: Tampilan lebih halus dengan menunggu rendering selesai tiap frame. Dinonaktifkan otomatis saat FastForward/SlowMo.", "Вимк.: Без синхронізації (найменша затримка, але зображення може бути ривковим). glFinish: Плавніше зображення очікуванням завершення рендерингу кожного кадру. Автоматично вимикається під час FastForward/SlowMo.", "Ανενεργό: Χωρίς συγχρονισμό (χαμηλότερη καθυστέρηση, αλλά η οθόνη μπορεί να φαίνεται τραγανή). glFinish: Ομαλότερη οθόνη περιμένοντας ολοκλήρωση απόδοσης κάθε καρέ. Απενεργοποιείται αυτόματα κατά FastForward/SlowMo.", "Av: Ingen synk (lägst latens, men skärmen kan se hackig ut). glFinish: Mjukare skärm genom att vänta på att rendering slutförs varje bildruta. Inaktiveras automatiskt under FastForward/SlowMo.", "ปิด: ไม่ซิงค์ (หน่วงต่ำสุด แต่หน้าจออาจกระตุก) glFinish: หน้าจอลื่นไหลขึ้นโดยรอการเรนเดอร์แต่ละเฟรมเสร็จ ปิดอัตโนมัติระหว่าง FastForward/SlowMo", "Vyp.: Bez synchronizace (nejnižší latence, ale obraz může trhat). glFinish: Plynulejší obraz čekáním na dokončení vykreslení každého snímku. Automaticky vypnuto během FastForward/SlowMo.", "Fra: Ingen synk (lavest latency, men skærmen kan se hakke ud). glFinish: Glattere skærm ved at vente på fuld rendering hver frame. Deaktiveres automatisk under FastForward/SlowMo.", "Kapalı: Senkron yok (en düşük gecikme, ancak görüntü takılabilir). glFinish: Her karede render tamamlanana kadar bekleyerek daha akıcı görüntü. FastForward/SlowMo sırasında otomatik devre dışı.", "Av: Ingen synk (lavest latency, men skjermen kan se hakkete ut). glFinish: Jevnere skjerm ved å vente på full rendering hver frame. Deaktiveres automatisk under FastForward/SlowMo.", "Ki: Nincs szinkron (legalacsonyabb késleltetés, de a kép szaggathat). glFinish: Simább kép a renderelés frame-enkénti befejezésének várakozásával. FastForward/SlowMo alatt automatikusan kikapcsol.", "Pois: Ei synkronointia (alin viive, mutta näyttö voi näyttää pätkivältä). glFinish: Sulavampi näyttö odottamalla renderöinnin valmistumista jokaisella ruudulla. Poistuu automaattisesti käytöstä FastForward/SlowMo-tilassa.", "Tắt: Không đồng bộ (độ trễ thấp nhất, nhưng màn hình có thể giật). glFinish: Màn hình mượt hơn bằng cách chờ render hoàn tất mỗi khung hình. Tự tắt khi FastForward/SlowMo.", "Wył.: Bez synchronizacji (najniższe opóźnienie, ale obraz może być poszarpany). glFinish: Płynniejszy obraz przez oczekiwanie na pełne renderowanie każdej klatki. Automatycznie wyłączane podczas FastForward/SlowMo.", "Oprit: Fără sincronizare (cea mai mică latență, dar afișajul poate părea sacadat). glFinish: Afișaj mai fluid așteptând finalizarea randării fiecărui cadru. Dezactivat automat în timpul FastForward/SlowMo."},
    {"Windows only. Applies only while the bottom screen is actually being drawn. Press ESC to release the cursor.", "Windowsのみ。下画面が実際に描画されている間だけ適用されます。ESCでカーソル制限を解除します。", "Nur Windows. Gilt nur, solange der untere Bildschirm tatsächlich gezeichnet wird. ESC drücken, um den Cursor freizugeben.", "Solo Windows. Se aplica solo mientras se dibuja la pantalla inferior. Pulsa ESC para liberar el cursor.", "Windows uniquement. S'applique uniquement tant que l'écran du bas est réellement affiché. Appuyez sur Échap pour libérer le curseur.", "Solo Windows. Si applica solo mentre lo schermo inferiore viene effettivamente disegnato. Premi ESC per rilasciare il cursore.", "Alleen Windows. Geldt alleen terwijl het onderste scherm daadwerkelijk wordt getekend. Druk op ESC om de cursor vrij te geven.", "Somente Windows. Aplica-se apenas enquanto a tela inferior está sendo desenhada. Pressione ESC para liberar o cursor.", "Только Windows. Применяется только пока нижний экран действительно отрисовывается. Нажмите ESC, чтобы освободить курсор.", "仅限 Windows。仅在下屏实际绘制时生效。按 ESC 释放光标。", "Windows 전용. 하단 화면이 실제로 그려지는 동안에만 적용됩니다. ESC를 눌러 커서를 해제하세요.", "Windows فقط. يُطبّق فقط أثناء رسم الشاشة السفلية فعليًا. اضغط ESC لتحرير المؤشر.", "Hanya Windows. Berlaku hanya saat layar bawah benar-benar digambar. Tekan ESC untuk melepaskan kursor.", "Лише Windows. Застосовується лише поки нижній екран фактично малюється. Натисніть ESC, щоб звільнити курсор.", "Μόνο Windows. Ισχύει μόνο όσο η κάτω οθόνη σχεδιάζεται πραγματικά. Πατήστε ESC για να απελευθερώσετε τον δρομέα.", "Endast Windows. Gäller endast medan den nedre skärmen faktiskt ritas. Tryck ESC för att frigöra markören.", "Windows เท่านั้น ใช้ได้เฉพาะขณะที่หน้าจอล่างถูกวาดจริง กด ESC เพื่อปล่อยเคอร์เซอร์", "Pouze Windows. Platí pouze, dokud se spodní obrazovka skutečně vykresluje. Stiskněte ESC pro uvolnění kurzoru.", "Kun Windows. Gælder kun mens den nederste skærm faktisk tegnes. Tryk ESC for at frigive markøren.", "Yalnızca Windows. Yalnızca alt ekran gerçekten çizilirken geçerlidir. İmleci serbest bırakmak için ESC'ye basın.", "Kun Windows. Gjelder bare mens den nedre skjermen faktisk tegnes. Trykk ESC for å frigjøre markøren.", "Csak Windows. Csak addig érvényes, amíg az alsó képernyő ténylegesen rajzolódik. Nyomja meg az ESC-t a kurzor felszabadításához.", "Vain Windows. Koskee vain, kun alanäyttöä todella piirretään. Paina ESC vapauttaaksesi kohdistimen.", "Chỉ Windows. Chỉ áp dụng khi màn hình dưới thực sự được vẽ. Nhấn ESC để giải phóng con trỏ.", "Tylko Windows. Dotyczy tylko wtedy, gdy dolny ekran jest faktycznie rysowany. Naciśnij ESC, aby zwolnić kursor.", "Doar Windows. Se aplică numai cât timp ecranul inferior este efectiv desenat. Apasă ESC pentru a elibera cursorul."},
    {"Does not overwrite your saved window layout settings. The override is applied only while Metroid Prime Hunters is in-game.", "保存済みのウィンドウレイアウト設定は上書きしません。Metroid Prime Huntersのゲーム中だけ一時的に適用されます。", "Überschreibt nicht die gespeicherten Fensterlayout-Einstellungen. Die Überschreibung gilt nur während Metroid Prime Hunters im Spiel läuft.", "No sobrescribe los ajustes guardados del diseño de ventana. La anulación solo se aplica mientras Metroid Prime Hunters está en juego.", "N'écrase pas vos réglages de disposition de fenêtre enregistrés. La surcharge s'applique uniquement pendant Metroid Prime Hunters en jeu.", "Non sovrascrive le impostazioni salvate del layout finestra. L'override si applica solo durante Metroid Prime Hunters in gioco.", "Overschrijft je opgeslagen vensterlayout-instellingen niet. De override geldt alleen tijdens Metroid Prime Hunters in het spel.", "Não sobrescreve as configurações salvas de layout da janela. A substituição só se aplica enquanto Metroid Prime Hunters estiver em jogo.", "Не перезаписывает сохранённые настройки расположения окон. Переопределение применяется только во время игры в Metroid Prime Hunters.", "不会覆盖已保存的窗口布局设置。仅在 Metroid Prime Hunters 游戏内时应用覆盖。", "저장된 창 레이아웃 설정을 덮어쓰지 않습니다. Metroid Prime Hunters 게임 중에만 재정의가 적용됩니다.", "لا يستبدل إعدادات تخطيط النافذة المحفوظة. يُطبَّق التجاوز فقط أثناء لعب Metroid Prime Hunters.", "Tidak menimpa pengaturan tata letak jendela yang disimpan. Override hanya diterapkan saat Metroid Prime Hunters sedang dimainkan.", "Не перезаписує збережені налаштування розташування вікон. Перевизначення застосовується лише під час гри в Metroid Prime Hunters.", "Δεν αντικαθιστά τις αποθηκευμένες ρυθμίσεις διάταξης παραθύρου. Η παράκαμψη εφαρμόζεται μόνο κατά τη διάρκεια του Metroid Prime Hunters.", "Skriver inte över sparade fönsterlayoutinställningar. Åsidosättningen gäller endast under Metroid Prime Hunters.", "ไม่เขียนทับการตั้งค่าเลย์เอาต์หน้าต่างที่บันทึกไว้ ใช้ override เฉพาะขณะเล่น Metroid Prime Hunters", "Nepřepisuje uložená nastavení rozložení oken. Přepsání platí pouze během hry Metroid Prime Hunters.", "Overskriver ikke gemte vindueslayoutindstillinger. Tilsidesættelsen gælder kun under Metroid Prime Hunters.", "Kayıtlı pencere düzeni ayarlarının üzerine yazmaz. Geçersiz kılma yalnızca Metroid Prime Hunters oynanırken uygulanır.", "Overskriver ikke lagrede vinduslayoutinnstillinger. Overstyringen gjelder bare under Metroid Prime Hunters.", "Nem írja felül a mentett abakelrendezési beállításokat. A felülírás csak Metroid Prime Hunters játék közben érvényes.", "Ei korvaa tallennettuja ikkunasommitusasetuksia. Ohitus koskee vain Metroid Prime Hunters -peliä.", "Không ghi đè cài đặt bố cục cửa sổ đã lưu. Ghi đè chỉ áp dụng khi đang chơi Metroid Prime Hunters.", "Nie nadpisuje zapisanych ustawień układu okien. Nadpisanie działa tylko podczas gry w Metroid Prime Hunters.", "Nu suprascrie setările salvate de layout fereastră. Suprascrierea se aplică doar în timpul Metroid Prime Hunters."},
    {"When playing with an Aspect Ratio other than 4:3 (Native), apply this to change the in-game 3D aspect ratio to match. Changes are applied at the start of each match.", "4:3 (ネイティブ) 以外の画面比率で遊ぶとき、ゲーム内3Dのアスペクト比も一致するように変更します。変更は各試合の開始時に適用されます。", "Bei einem Seitenverhältnis außer 4:3 (Nativ) das ingame 3D-Seitenverhältnis entsprechend anpassen. Änderungen gelten zu Beginn jedes Matches.", "Al jugar con una relación de aspecto distinta de 4:3 (nativa), ajusta la relación 3D en el juego para que coincida. Los cambios se aplican al inicio de cada partida.", "Lors d'un ratio d'aspect autre que 4:3 (natif), adapte le ratio 3D en jeu en conséquence. Les changements s'appliquent au début de chaque match.", "Con un rapporto d'aspetto diverso da 4:3 (Nativo), applica questo per adattare il rapporto 3D in gioco. Le modifiche si applicano all'inizio di ogni partita.", "Bij een beeldverhouding anders dan 4:3 (Natief), pas dit toe om de 3D-beeldverhouding in het spel aan te passen. Wijzigingen gelden aan het begin van elke match.", "Ao jogar com proporção diferente de 4:3 (Nativa), aplique isto para ajustar a proporção 3D no jogo. As alterações são aplicadas no início de cada partida.", "При соотношении сторон, отличном от 4:3 (Нативное), применяйте это для изменения 3D-соотношения в игре. Изменения применяются в начале каждого матча.", "使用 4:3（原生）以外的宽高比时，应用此项以匹配游戏内 3D 宽高比。更改在每场比赛开始时生效。", "4:3(네이티브) 이외의 화면 비율로 플레이할 때 게임 내 3D 비율을 맞추려면 이 옵션을 적용하세요. 변경은 각 매치 시작 시 적용됩니다.", "عند اللعب بنسبة عرض إلى ارتفاع غير 4:3 (Native)، طبّق هذا لتغيير نسبة 3D داخل اللعبة لتتطابق. تُطبّق التغييرات في بداية كل مباراة.", "Saat bermain dengan Aspect Ratio selain 4:3 (Native), terapkan ini untuk menyesuaikan rasio 3D in-game. Perubahan diterapkan di awal setiap pertandingan.", "При співвідношенні сторін, відмінному від 4:3 (Native), застосуйте це для зміни 3D-співвідношення в грі. Зміни застосовуються на початку кожного матчу.", "Όταν παίζετε με αναλογία διαστάσεων διαφορετική από 4:3 (Native), εφαρμόστε αυτό για να ταιριάξει το 3D aspect ratio. Οι αλλαγές εφαρμόζονται στην έναρξη κάθε αγώνα.", "När du spelar med annat bildförhållande än 4:3 (Native), tillämpa detta för att matcha 3D-bildförhållandet i spelet. Ändringar tillämpas i början av varje match.", "เมื่อเล่นด้วยอัตราส่วนภาพที่ไม่ใช่ 4:3 (Native) ใช้ตัวเลือกนี้เพื่อปรับอัตราส่วน 3D ในเกมให้ตรงกัน การเปลี่ยนแปลงจะมีผลเมื่อเริ่มแมตช์", "Při poměru stran jiném než 4:3 (Native) použijte toto pro úpravu 3D poměru ve hře. Změny se aplikují na začátku každého zápasu.", "Når du spiller med andet billedformat end 4:3 (Native), anvend dette for at matche 3D-billedformatet i spillet. Ændringer anvendes ved start af hver kamp.", "4:3 (Native) dışında en-boy oranıyla oynarken, oyun içi 3D oranını eşleştirmek için bunu uygulayın. Değişiklikler her maçın başında uygulanır.", "Når du spiller med annet sideforhold enn 4:3 (Native), bruk dette for å matche 3D-sideforholdet i spillet. Endringer brukes ved start av hver kamp.", "4:3 (Native) eltérő képaránynál alkalmazd ezt a játékbeli 3D képarány igazításához. A változások minden meccs elején érvényesülnek.", "Kun pelaat muulla kuvasuhteella kuin 4:3 (Native), käytä tätä pelin 3D-kuvasuhteen täsmäämiseen. Muutokset otetaan käyttöön jokaisen ottelun alussa.", "Khi chơi với tỷ lệ khung hình khác 4:3 (Native), áp dụng tùy chọn này để khớp tỷ lệ 3D trong game. Thay đổi có hiệu lực khi bắt đầu mỗi trận.", "Gdy grasz z proporcjami innymi niż 4:3 (Native), zastosuj to, aby dopasować proporcje 3D w grze. Zmiany obowiązują na początku każdego meczu.", "Când joci cu un raport de aspect diferit de 4:3 (Native), aplică aceasta pentru a potrivi raportul 3D din joc. Modificările se aplică la începutul fiecărui meci."},
    {"Changes the HP value at which the low-HP warning sound and warning HUD state trigger (vanilla: 25). Applied at the start of each match based on the current Damage setting.", "低HP警告音と警告HUD状態が発生するHP値を変更します (バニラ: 25)。現在のダメージ設定に応じて、各試合の開始時に適用されます。", "Ändert den HP-Wert, bei dem Warnsound und Warn-HUD ausgelöst werden (Vanilla: 25). Wird zu Beginn jedes Matches basierend auf der aktuellen Schaden-Einstellung angewendet.", "Cambia el valor de PV en el que se activan el sonido de aviso de PV bajos y el estado de aviso del HUD (vanilla: 25). Se aplica al inicio de cada partida según el ajuste de daño actual.", "Modifie la valeur de PV déclenchant le son d'avertissement et l'état HUD d'alerte (vanilla : 25). Appliqué au début de chaque match selon le réglage de dégâts actuel.", "Modifica il valore HP che attiva il suono di avviso HP basso e lo stato HUD di avviso (vanilla: 25). Applicato all'inizio di ogni partita in base all'impostazione Danno attuale.", "Wijzigt de HP-waarde waarop het lage-HP-waarschuwingsgeluid en de waarschuwings-HUD-status worden geactiveerd (vanilla: 25). Toegepast aan het begin van elke wedstrijd op basis van de huidige Schade-instelling.", "Altera o valor de HP em que o som de aviso de HP baixo e o estado de aviso do HUD são acionados (vanilla: 25). Aplicado no início de cada partida com base na configuração de Dano atual.", "Изменяет значение HP, при котором срабатывают звук предупреждения о низком HP и состояние предупреждения HUD (vanilla: 25). Применяется в начале каждого матча на основе текущей настройки урона.", "更改触发低 HP 警告音和警告 HUD 状态的 HP 值（原版：25）。根据当前伤害设置在每场比赛开始时应用。", "낮은 HP 경고음과 경고 HUD 상태가 발생하는 HP 값을 변경합니다 (vanilla: 25). 현재 피해 설정에 따라 각 매치 시작 시 적용됩니다.", "يغيّر قيمة HP التي يُفعّل عندها صوت تحذير HP المنخفض وحالة تحذير HUD (vanilla: 25). يُطبّق في بداية كل مباراة حسب إعداد الضرر الحالي.", "Mengubah nilai HP saat peringatan HP rendah dan status peringatan HUD dipicu (vanilla: 25). Diterapkan di awal setiap pertandingan berdasarkan pengaturan Damage saat ini.", "Змінює значення HP, при якому спрацьовують звук попередження про низький HP і стан попередження HUD (vanilla: 25). Застосовується на початку кожного матчу згідно з поточним налаштуванням Damage.", "Αλλάζει την τιμή HP στην οποία ενεργοποιούνται ο ήχος προειδοποίησης χαμηλού HP και η κατάσταση προειδοποίησης HUD (vanilla: 25). Εφαρμόζεται στην έναρξη κάθε αγώνα βάσει της τρέχουσας ρύθμισης Damage.", "Ändrar HP-värdet där låg-HP-varningsljud och varnings-HUD-status utlöses (vanilla: 25). Tillämpas i början av varje match baserat på aktuell Skade-inställning.", "เปลี่ยนค่า HP ที่เสียงเตือน HP ต่ำและสถานะเตือน HUD ทำงาน (vanilla: 25) ใช้ตอนเริ่มแมตช์ตามการตั้งค่า Damage ปัจจุบัน", "Mění hodnotu HP, při které se spustí zvuk varování nízkého HP a stav varování HUD (vanilla: 25). Použije se na začátku každého zápasu podle aktuálního nastavení Damage.", "Ændrer HP-værdien, hvor lav-HP-advarselslyd og advarsels-HUD-status udløses (vanilla: 25). Anvendes ved start af hver kamp baseret på aktuel Skade-indstilling.", "Düşük HP uyarı sesinin ve uyarı HUD durumunun tetiklendiği HP değerini değiştirir (vanilla: 25). Her maçın başında mevcut Damage ayarına göre uygulanır.", "Endrer HP-verdien der lav-HP-advarselslyd og advarsels-HUD-status utløses (vanilla: 25). Brukes ved start av hver kamp basert på gjeldende Skade-innstilling.", "Módosítja azt az HP értéket, amelynél az alacsony HP figyelmeztető hang és a figyelmeztető HUD állapot aktiválódik (vanilla: 25). Minden meccs elején alkalmazza az aktuális Damage beállítás alapján.", "Muuttaa HP-arvoa, jolla matalan HP:n varoitusääni ja varoitus-HUD-tila laukeavat (vanilla: 25). Sovelletaan jokaisen ottelun alussa nykyisen Damage-asetuksen mukaan.", "Thay đổi giá trị HP kích hoạt âm cảnh báo HP thấp và trạng thái cảnh báo HUD (vanilla: 25). Áp dụng khi bắt đầu mỗi trận theo cài đặt Damage hiện tại.", "Zmienia wartość HP, przy której uruchamia się dźwięk ostrzeżenia o niskim HP i stan ostrzeżenia HUD (vanilla: 25). Stosowane na początku każdego meczu według bieżącego ustawienia Damage.", "Schimbă valoarea HP la care se declanșează sunetul de avertizare HP scăzut și starea de avertizare HUD (vanilla: 25). Se aplică la începutul fiecărui meci în funcție de setarea Damage curentă."},
    {"Edit HUD Layout", "HUD配置を編集", "HUD-Layout bearbeiten", "Editar diseño HUD", "Modifier la disposition HUD", "Modifica layout HUD", "HUD-indeling bewerken", "Editar layout HUD", "Редактировать макет HUD", "编辑 HUD 布局", "HUD 레이아웃 편집", "تحرير تخطيط HUD", "Edit Tata Letak HUD", "Редагувати макет HUD", "Επεξεργασία διάταξης HUD", "Redigera HUD-layout", "แก้ไขเลย์เอาต์ HUD", "Upravit rozložení HUD", "Rediger HUD-layout", "HUD Düzenini Düzenle", "Rediger HUD-oppsett", "HUD elrendezés szerkesztése", "Muokkaa HUD-asettelua", "Chỉnh sửa bố cục HUD", "Edytuj układ HUD", "Editează layout HUD"},
    {"Hide this dialog and enter the interactive HUD position editor", "このダイアログを隠して、対話式HUD配置エディターに入ります", "Diesen Dialog ausblenden und interaktiven HUD-Positionseditor öffnen", "Ocultar este diálogo y entrar en el editor interactivo de posición HUD", "Masquer cette boîte de dialogue et ouvrir l'éditeur interactif de position HUD", "Nascondi questa finestra ed entra nell'editor interattivo posizione HUD", "Dit dialoogvenster verbergen en interactieve HUD-positie-editor openen", "Ocultar esta caixa de diálogo e entrar no editor interativo de posição HUD", "Скрыть это окно и войти в интерактивный редактор позиции HUD", "隐藏此对话框并进入交互式 HUD 位置编辑器", "이 대화 상자를 숨기고 대화형 HUD 위치 편집기로 들어가기", "إخفاء هذا الحوار والدخول إلى محرر موضع HUD التفاعلي", "Sembunyikan dialog ini dan masuk ke editor posisi HUD interaktif", "Приховати це вікно та увійти в інтерактивний редактор позиції HUD", "Απόκρυψη αυτού του διαλόγου και είσοδος στον διαδραστικό επεξεργαστή θέσης HUD", "Dölj denna dialog och gå in i den interaktiva HUD-positionsredigeraren", "ซ่อนกล่องโต้ตอบนี้และเข้าสู่ตัวแก้ไขตำแหน่ง HUD แบบโต้ตอบ", "Skrýt tento dialog a vstoupit do interaktivního editoru pozice HUD", "Skjul denne dialog og gå ind i den interaktive HUD-positionseditor", "Bu iletişim kutusunu gizle ve etkileşimli HUD konum düzenleyicisine gir", "Skjul denne dialogen og gå inn i den interaktive HUD-posisjonseditoren", "A párbeszédablak elrejtése és belépés az interaktív HUD pozíciószerkesztőbe", "Piilota tämä valintaikkuna ja siirry interaktiiviseen HUD-sijaintieditoriin", "Ẩn hộp thoại này và vào trình chỉnh vị trí HUD tương tác", "Ukryj to okno dialogowe i wejdź do interaktywnego edytora pozycji HUD", "Ascunde acest dialog și intră în editorul interactiv de poziție HUD"},
    {"Enable Custom HUD (Replaces the in-game HUD with a custom overlay showing HP, ammo, weapon icons and crosshair)", "カスタムHUDを有効化 (ゲーム内HUDを、HP/弾薬/武器アイコン/照準のカスタム表示に置き換えます)", "Benutzerdefiniertes HUD aktivieren (Ersetzt das Ingame-HUD durch ein Overlay mit HP, Munition, Waffensymbolen und Visier)", "Activar HUD personalizado (Reemplaza el HUD del juego por una superposición con HP, munición, iconos de armas y punto de mira)", "Activer le HUD personnalisé (Remplace le HUD en jeu par une superposition affichant HP, munitions, icônes d'armes et réticule)", "Abilita HUD personalizzato (Sostituisce l'HUD di gioco con un overlay personalizzato con HP, munizioni, icone armi e reticolo)", "Aangepaste HUD inschakelen (Vervangt de in-game HUD door een overlay met HP, munitie, wapeniconen en richtkruis)", "Ativar HUD personalizado (Substitui o HUD do jogo por uma sobreposição com HP, munição, ícones de armas e mira)", "Включить пользовательский HUD (Заменяет игровой HUD наложением с HP, боеприпасами, иконками оружия и прицелом)", "启用自定义 HUD（用显示 HP、弹药、武器图标和准星的自定义叠加层替换游戏内 HUD）", "사용자 지정 HUD 활성화 (게임 내 HUD를 HP, 탄약, 무기 아이콘, 조준선이 있는 사용자 지정 오버레이로 대체)", "تفعيل HUD مخصص (يستبدل HUD داخل اللعبة بطبقة مخصصة تعرض HP والذخيرة وأيقونات الأسلحة والتقاطع)", "Aktifkan HUD kustom (Mengganti HUD dalam game dengan overlay kustom yang menampilkan HP, amunisi, ikon senjata, dan bidik)", "Увімкнути користувацький HUD (Замінює ігровий HUD накладенням з HP, боєприпасами, іконками зброї та прицілом)", "Ενεργοποίηση προσαρμοσμένου HUD (Αντικαθιστά το HUD του παιχνιδιού με επικάλυψη HP, πυρομαχικά, εικονίδια όπλων και στόχευση)", "Aktivera anpassad HUD (Ersätter spelets HUD med ett anpassat lager som visar HP, ammo, vapenikoner och sikte)", "เปิดใช้ HUD กำหนดเอง (แทนที่ HUD ในเกมด้วย overlay แสดง HP กระสุน ไอคอนอาวุธ และเล็ง)", "Povolit vlastní HUD (Nahradí herní HUD vlastní vrstvou s HP, municí, ikonami zbraní a zaměřovačem)", "Aktivér brugerdefineret HUD (Erstatter spillets HUD med et tilpasset lag med HP, ammo, våbenikoner og sigte)", "Özel HUD'u etkinleştir (Oyun içi HUD'u HP, cephane, silah simgeleri ve nişangah gösteren özel katmanla değiştirir)", "Aktiver tilpasset HUD (Erstatter spillets HUD med et tilpasset lag som viser HP, ammo, våpenikoner og sikte)", "Egyéni HUD engedélyezése (A játék HUD-ját egyéni rétegre cseréli HP, lőszer, fegyverikonok és irányzék megjelenítésével)", "Ota käyttöön mukautettu HUD (Korvaa pelin HUD:n mukautetulla kerroksella, joka näyttää HP:n, ammot, aseikonit ja tähtäimen)", "Bật HUD tùy chỉnh (Thay HUD trong game bằng lớp phủ hiển thị HP, đạn, biểu tượng vũ khí và tâm ngắm)", "Włącz niestandardowy HUD (Zastępuje HUD w grze nakładką z HP, amunicją, ikonami broni i celownikiem)", "Activează HUD personalizat (Înlocuiește HUD-ul din joc cu un overlay cu HP, muniție, icoane arme și țintă)"},
    {"Share your Custom HUD setup as TOML text, or paste TOML into the input area to apply it to the current dialog.", "カスタムHUD設定をTOMLテキストとして共有、または入力欄に貼り付けて現在のダイアログへ適用します。", "Teile dein benutzerdefiniertes HUD-Setup als TOML-Text oder füge TOML in das Eingabefeld ein, um es auf den aktuellen Dialog anzuwenden.", "Comparte tu configuración de HUD personalizado como texto TOML, o pega TOML en el área de entrada para aplicarlo al diálogo actual.", "Partagez votre configuration HUD personnalisée en texte TOML, ou collez du TOML dans la zone de saisie pour l'appliquer à la boîte de dialogue actuelle.", "Condividi la configurazione HUD personalizzata come testo TOML, o incolla TOML nell'area di input per applicarla alla finestra corrente.", "Deel je aangepaste HUD-configuratie als TOML-tekst, of plak TOML in het invoerveld om het toe te passen op het huidige dialoogvenster.", "Compartilhe sua configuração de HUD personalizado como texto TOML, ou cole TOML na área de entrada para aplicar ao diálogo atual.", "Поделитесь настройкой пользовательского HUD в виде TOML или вставьте TOML в поле ввода для применения к текущему диалогу.", "将自定义 HUD 设置以 TOML 文本分享，或粘贴 TOML 到输入区以应用到当前对话框。", "커스텀 HUD 설정을 TOML 텍스트로 공유하거나, 입력 영역에 TOML을 붙여넣어 현재 대화상자에 적용하세요.", "شارك إعداد HUD المخصص كنص TOML، أو الصق TOML في منطقة الإدخال لتطبيقه على الحوار الحالي.", "Bagikan pengaturan Custom HUD sebagai teks TOML, atau tempel TOML ke area input untuk menerapkannya ke dialog saat ini.", "Поділіться налаштуванням Custom HUD як текст TOML або вставте TOML у поле вводу для застосування до поточного діалогу.", "Μοιραστείτε τη ρύθμιση Custom HUD ως κείμενο TOML ή επικολλήστε TOML στην περιοχή εισόδου για εφαρμογή στο τρέχον παράθυρο.", "Dela din Custom HUD-konfiguration som TOML-text, eller klistra in TOML i inmatningsfältet för att tillämpa på dialogen.", "แชร์การตั้งค่า Custom HUD เป็นข้อความ TOML หรือวาง TOML ในช่องป้อนข้อมูลเพื่อใช้กับไดอะล็อกปัจจุบัน", "Sdílejte nastavení Custom HUD jako text TOML nebo vložte TOML do vstupní oblasti pro použití v aktuálním dialogu.", "Del din Custom HUD-opsætning som TOML-tekst, eller indsæt TOML i inputområdet for at anvende på den aktuelle dialog.", "Custom HUD kurulumunuzu TOML metni olarak paylaşın veya mevcut iletişim kutusuna uygulamak için TOML yapıştırın.", "Del Custom HUD-oppsettet som TOML-tekst, eller lim inn TOML i inndatafeltet for å bruke i gjeldende dialog.", "Oszd meg Custom HUD beállításodat TOML szövegként, vagy illeszd be a TOML-t a beviteli mezőbe az aktuális párbeszédablakhoz.", "Jaa Custom HUD -asetuksesi TOML-tekstinä tai liitä TOML syöttöalueelle nykyiseen valintaikkunaan.", "Chia sẻ thiết lập Custom HUD dưới dạng văn bản TOML, hoặc dán TOML vào vùng nhập để áp dụng cho hộp thoại hiện tại.", "Udostępnij konfigurację Custom HUD jako tekst TOML lub wklej TOML w pole wejściowe, aby zastosować w bieżącym oknie.", "Partajează configurația Custom HUD ca text TOML sau lipește TOML în zona de intrare pentru a o aplica dialogului curent."},
    {"Press Generate to build sharable Custom HUD TOML.", "生成を押すと共有用のカスタムHUD TOMLを作成します。", "Auf Generieren klicken, um teilbares Custom HUD TOML zu erstellen.", "Pulsa Generar para crear un TOML de HUD personalizado compartible.", "Appuyez sur Générer pour créer un TOML de HUD personnalisé partageable.", "Premi Genera per creare un TOML HUD personalizzato condivisibile.", "Druk op Genereren om deelbare aangepaste HUD TOML te maken.", "Pressione Gerar para criar um TOML de HUD personalizado compartilhável.", "Нажмите «Создать», чтобы собрать общий TOML пользовательского HUD.", "按“生成”以构建可共享的自定义 HUD TOML。", "생성을 눌러 공유 가능한 사용자 HUD TOML을 만드세요.", "اضغط إنشاء لبناء Custom HUD TOML قابل للمشاركة.", "Tekan Buat untuk membuat Custom HUD TOML yang dapat dibagikan.", "Натисніть «Створити», щоб зібрати спільний Custom HUD TOML.", "Πατήστε Δημιουργία για κοινόχρηστο Custom HUD TOML.", "Tryck på Generera för att skapa delbar Custom HUD TOML.", "กดสร้างเพื่อสร้าง Custom HUD TOML ที่แชร์ได้", "Stiskněte Generovat pro vytvoření sdíleného Custom HUD TOML.", "Tryk på Generer for at oprette delbar Custom HUD TOML.", "Paylaşılabilir Custom HUD TOML oluşturmak için Oluştur'a basın.", "Trykk Generer for å lage delbar Custom HUD TOML.", "Nyomja meg a Generálás gombot megosztható Custom HUD TOML készítéséhez.", "Paina Luo luodaksesi jaettavan Custom HUD TOML:n.", "Nhấn Tạo để xây dựng Custom HUD TOML có thể chia sẻ.", "Naciśnij Generuj, aby utworzyć udostępnialny Custom HUD TOML.", "Apasă Generează pentru a crea Custom HUD TOML partajabil."},
    {"Paste Custom HUD TOML here, then press Apply Input.", "ここにカスタムHUD TOMLを貼り付けてから、入力を適用してください。", "Benutzerdefiniertes HUD-TOML hier einfügen, dann Eingabe anwenden drücken.", "Pega aquí el TOML HUD personalizado y pulsa Aplicar entrada.", "Collez le TOML HUD personnalisé ici, puis appuyez sur Appliquer l'entrée.", "Incolla qui il TOML HUD personalizzato, quindi premi Applica input.", "Plak aangepaste HUD-TOML hier en druk op Invoer toepassen.", "Cole o TOML HUD personalizado aqui e pressione Aplicar entrada.", "Вставьте пользовательский HUD TOML сюда, затем нажмите «Применить ввод».", "在此粘贴自定义 HUD TOML，然后按应用输入。", "여기에 사용자 HUD TOML을 붙여넣은 후 입력 적용을 누르세요.", "الصق TOML HUD المخصص هنا، ثم اضغط تطبيق الإدخال.", "Tempel TOML HUD Kustom di sini, lalu tekan Terapkan Input.", "Вставте користувацький HUD TOML сюди, потім натисніть Застосувати введення.", "Επικολλήστε προσαρμοσμένο HUD TOML εδώ, μετά πατήστε Εφαρμογή εισόδου.", "Klistra in anpassat HUD-TOML här, tryck sedan Tillämpa indata.", "วาง Custom HUD TOML ที่นี่ แล้วกด Apply Input", "Vložte vlastní HUD TOML sem a stiskněte Použít vstup.", "Indsæt brugerdefineret HUD-TOML her, tryk derefter Anvend input.", "Özel HUD TOML'yi buraya yapıştırın, ardından Girdiyi Uygula'ya basın.", "Lim inn tilpasset HUD-TOML her, trykk deretter Bruk inndata.", "Illeszd be az egyéni HUD TOML-t ide, majd nyomd meg a Bemenet alkalmazása gombot.", "Liitä mukautettu HUD-TOML tähän ja paina Käytä syötettä.", "Dán Custom HUD TOML vào đây, rồi nhấn Áp dụng đầu vào.", "Wklej niestandardowy HUD TOML tutaj, następnie naciśnij Zastosuj wejście.", "Lipește TOML HUD personalizat aici, apoi apasă Aplică intrare."},

    // Custom HUD sections and properties
    {"— Common HUD —", "- 共通HUD -", "— Gemeinsames HUD —", "— HUD común —", "— HUD commun —", "— HUD comune —", "— Gemeenschappelijke HUD —", "— HUD comum —", "— Общий HUD —", "— 通用 HUD —", "— 공통 HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —", "— Gemeinsames HUD —"},
    {"— Score Row (per mode) —", "- スコア行 (モード別) -", "— Punktezeile (pro Modus) —", "— Fila de puntuación (por modo) —", "— Ligne de score (par mode) —", "— Riga punteggio (per modalità) —", "— Score-regel (per modus) —", "— Linha de pontuação (por modo) —", "— Строка счёта (по режиму) —", "— 得分行（按模式）—", "— 점수 행 (모드별) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —", "— Punktezeile (pro Modus) —"},
    {"Hide Helmet (Visor Mask)", "ヘルメット (バイザーマスク) を非表示", "Helm ausblenden (Visiermaske)", "Ocultar casco (máscara del visor)", "Masquer le casque (masque du viseur)", "Nascondi elmo (maschera visore)", "Helm verbergen (vizier)", "Ocultar capacete (máscara do visor)", "Скрыть шлем (маска визора)", "隐藏头盔（面罩）", "헬멧 숨기기 (바이저 마스크)", "إخفاء الخوذة (Visor Mask)", "Sembunyikan helm (Visor Mask)", "Приховати шолом (Visor Mask)", "Απόκρυψη κράνους (Visor Mask)", "Dölj hjälm (Visor Mask)", "ซ่อนหมวก (Visor Mask)", "Skrýt helmu (Visor Mask)", "Skjul hjelm (Visor Mask)", "Kaskı gizle (Visor Mask)", "Skjul hjelm (Visor Mask)", "Sisak elrejtése (Visor Mask)", "Piilota kypärä (Visor Mask)", "Ẩn mũ (Visor Mask)", "Ukryj hełm (Visor Mask)", "Ascunde casca (Visor Mask)"},
    {"Hide Ammo", "弾薬を非表示", "Munition ausblenden", "Ocultar munición", "Masquer les munitions", "Nascondi munizioni", "Munitie verbergen", "Ocultar munição", "Скрыть боеприпасы", "隐藏弹药", "탄약 숨기기", "إخفاء الذخيرة", "Sembunyikan amunisi", "Приховати боєприпаси", "Απόκρυψη πυρομαχικών", "Dölj ammunition", "ซ่อนกระสุน", "Skrýt munici", "Skjul ammunition", "Mühimmatı gizle", "Skjul ammunisjon", "Lőszer elrejtése", "Piilota ammukset", "Ẩn đạn", "Ukryj amunicję", "Ascunde muniția"},
    {"Hide Weapon Icon", "武器アイコンを非表示", "Waffensymbol ausblenden", "Ocultar icono de arma", "Masquer l'icône d'arme", "Nascondi icona arma", "Wapenpictogram verbergen", "Ocultar ícone de arma", "Скрыть значок оружия", "隐藏武器图标", "무기 아이콘 숨기기", "إخفاء أيقونة السلاح", "Sembunyikan Ikon Senjata", "Приховати значок зброї", "Απόκρυψη εικονιδίου όπλου", "Dölj vapenikon", "ซ่อนไอคอนอาวุธ", "Skrýt ikonu zbraně", "Skjul våbenikon", "Silah simgesini gizle", "Skjul våpenikon", "Fegyver ikon elrejtése", "Piilota aseikoni", "Ẩn biểu tượng vũ khí", "Ukryj ikonę broni", "Ascunde pictograma armă"},
    {"Hide HP", "HPを非表示", "HP ausblenden", "Ocultar HP", "Masquer les PV", "Nascondi HP", "HP verbergen", "Ocultar HP", "Скрыть HP", "隐藏 HP", "HP 숨기기", "إخفاء HP", "Sembunyikan HP", "Приховати HP", "Απόκρυψη HP", "Dölj HP", "ซ่อน HP", "Skrýt HP", "Skjul HP", "HP gizle", "Skjul HP", "HP elrejtése", "Piilota HP", "Ẩn HP", "Ukryj HP", "Ascunde HP"},
    {"Hide Crosshair", "照準を非表示", "Visier ausblenden", "Ocultar punto de mira", "Masquer le réticule", "Nascondi reticolo", "Richtkruis verbergen", "Ocultar mira", "Скрыть прицел", "隐藏准星", "조준선 숨기기", "إخفاء التقاطع", "Sembunyikan bidik", "Сховати приціл", "Απόκρυψη στόχευσης", "Dölj sikte", "ซ่อนเล็ง", "Skrýt zaměřovač", "Skjul sigtekors", "Nişangahı gizle", "Skjul trådkors", "Irányzék elrejtése", "Piilota tähtäin", "Ẩn tâm ngắm", "Ukryj celownik", "Ascunde ținta"},
    {"Hide Bomb (Boost Ball kept)", "ボムを非表示 (ブーストボールは維持)", "Bombe ausblenden (Boost Ball bleibt)", "Ocultar bomba (Boost Ball conservado)", "Masquer la bombe (Boost Ball conservé)", "Nascondi bomba (Boost Ball mantenuto)", "Bom verbergen (Boost Ball behouden)", "Ocultar bomba (Boost Ball mantido)", "Скрыть бомбу (Boost Ball сохранён)", "隐藏炸弹（保留 Boost Ball）", "폭탄 숨기기 (Boost Ball 유지)", "إخفاء القنبلة (Boost Ball محفوظ)", "Sembunyikan bom (Boost Ball tetap)", "Приховати бомбу (Boost Ball збережено)", "Απόκρυψη βόμβας (Boost Ball διατηρείται)", "Dölj bomb (Boost Ball behålls)", "ซ่อนบอมบ์ (คง Boost Ball)", "Skrýt bombu (Boost Ball ponechán)", "Skjul bombe (Boost Ball beholdes)", "Bombayı gizle (Boost Ball korunur)", "Skjul bombe (Boost Ball beholdes)", "Bomba elrejtése (Boost Ball megmarad)", "Piilota pommi (Boost Ball säilyy)", "Ẩn bom (Giữ Boost Ball)", "Ukryj bombę (Boost Ball zachowany)", "Ascunde bomba (Boost Ball păstrat)"},
    {"Hide Score: Battle", "スコア非表示: バトル", "Punkte ausblenden: Kampf", "Ocultar puntuación: Batalla", "Masquer le score : Bataille", "Nascondi punteggio: Battaglia", "Score verbergen: Gevecht", "Ocultar pontuação: Batalha", "Скрыть счёт: Битва", "隐藏分数：战斗", "점수 숨기기: 배틀", "إخفاء النقاط: معركة", "Sembunyikan skor: Pertempuran", "Приховати рахунок: Битва", "Απόκρυψη σκορ: Μάχη", "Dölj poäng: Strid", "ซ่อนคะแนน: ต่อสู้", "Skrýt skóre: Bitva", "Skjul score: Kamp", "Skoru gizle: Savaş", "Skjul poeng: Kamp", "Pontszám elrejtése: Harc", "Piilota pisteet: Taistelu", "Ẩn điểm: Trận đấu", "Ukryj wynik: Bitwa", "Ascunde scorul: Bătălie"},
    {"Hide Score: Survival", "スコア非表示: サバイバル", "Punktestand ausblenden: Überleben", "Ocultar puntuación: Supervivencia", "Masquer score : Survie", "Nascondi punteggio: Sopravvivenza", "Score verbergen: Overleven", "Ocultar pontuação: Sobrevivência", "Скрыть счёт: Выживание", "隐藏分数：生存", "점수 숨기기: 서바이벌", "إخفاء النتيجة: البقاء", "Sembunyikan Skor: Survival", "Приховати рахунок: Виживання", "Απόκρυψη σκορ: Επιβίωση", "Dölj poäng: Överlevnad", "ซ่อนคะแนน: Survival", "Skrýt skóre: Přežití", "Skjul score: Overlevelse", "Skoru gizle: Hayatta kalma", "Skjul poeng: Overlevelse", "Pontszám elrejtése: Túlélés", "Piilota pisteet: Selviytyminen", "Ẩn điểm: Sinh tồn", "Ukryj wynik: Przetrwanie", "Ascunde scor: Supraviețuire"},
    {"Hide Score: Prime Hunter", "スコア非表示: プライムハンター", "Punkte ausblenden: Prime Hunter", "Ocultar puntuación: Prime Hunter", "Masquer le score : Prime Hunter", "Nascondi punteggio: Prime Hunter", "Score verbergen: Prime Hunter", "Ocultar pontuação: Prime Hunter", "Скрыть счёт: Prime Hunter", "隐藏分数: Prime Hunter", "점수 숨기기: Prime Hunter", "إخفاء النقاط: Prime Hunter", "Sembunyikan Skor: Prime Hunter", "Приховати рахунок: Prime Hunter", "Απόκρυψη σκορ: Prime Hunter", "Dölj poäng: Prime Hunter", "ซ่อนคะแนน: Prime Hunter", "Skrýt skóre: Prime Hunter", "Skjul score: Prime Hunter", "Skoru gizle: Prime Hunter", "Skjul poeng: Prime Hunter", "Pontszám elrejtése: Prime Hunter", "Piilota pisteet: Prime Hunter", "Ẩn điểm: Prime Hunter", "Ukryj wynik: Prime Hunter", "Ascunde scor: Prime Hunter"},
    {"Hide Score: Bounty", "スコア非表示: バウンティ", "Punkte ausblenden: Kopfgeld", "Ocultar puntuación: Recompensa", "Masquer le score : Prime", "Nascondi punteggio: Taglia", "Score verbergen: Bounty", "Ocultar pontuação: Recompensa", "Скрыть счёт: Награда", "隐藏得分：赏金", "점수 숨기기: 바운티", "إخفاء النقاط: Bounty", "Sembunyikan skor: Bounty", "Сховати рахунок: Bounty", "Απόκρυψη σκορ: Bounty", "Dölj poäng: Bounty", "ซ่อนคะแนน: Bounty", "Skrýt skóre: Bounty", "Skjul score: Bounty", "Skoru gizle: Bounty", "Skjul poeng: Bounty", "Pont elrejtése: Bounty", "Piilota pisteet: Bounty", "Ẩn điểm: Bounty", "Ukryj wynik: Bounty", "Ascunde scorul: Bounty"},
    {"Hide Score: Capture", "スコア非表示: キャプチャー", "Punkte ausblenden: Capture", "Ocultar puntuación: Capture", "Masquer le score : Capture", "Nascondi punteggio: Capture", "Score verbergen: Capture", "Ocultar pontuação: Capture", "Скрыть счёт: Capture", "隐藏分数：Capture", "점수 숨기기: Capture", "إخفاء النقاط: Capture", "Sembunyikan skor: Capture", "Приховати рахунок: Capture", "Απόκρυψη σκορ: Capture", "Dölj poäng: Capture", "ซ่อนคะแนน: Capture", "Skrýt skóre: Capture", "Skjul score: Capture", "Skoru gizle: Capture", "Skjul poeng: Capture", "Pontszám elrejtése: Capture", "Piilota pisteet: Capture", "Ẩn điểm: Capture", "Ukryj wynik: Capture", "Ascunde scorul: Capture"},
    {"Hide Score: Defender", "スコア非表示: ディフェンダー", "Punkte ausblenden: Verteidiger", "Ocultar puntuación: Defensor", "Masquer le score : Défenseur", "Nascondi punteggio: Difensore", "Score verbergen: Verdediger", "Ocultar pontuação: Defensor", "Скрыть счёт: Защитник", "隐藏分数：防守", "점수 숨기기: 디펜더", "إخفاء النقاط: مدافع", "Sembunyikan skor: Bertahan", "Приховати рахунок: Захисник", "Απόκρυψη σκορ: Υπερασπιστής", "Dölj poäng: Försvarare", "ซ่อนคะแนน: ป้องกัน", "Skrýt skóre: Obránce", "Skjul score: Forsvarer", "Skoru gizle: Savunmacı", "Skjul poeng: Forsvarer", "Pontszám elrejtése: Védő", "Piilota pisteet: Puolustaja", "Ẩn điểm: Phòng thủ", "Ukryj wynik: Obrońca", "Ascunde scorul: Apărător"},
    {"Hide Score: Node", "スコア非表示: ノード", "Punktestand ausblenden: Knoten", "Ocultar puntuación: Nodo", "Masquer score : Nœud", "Nascondi punteggio: Nodo", "Score verbergen: Knooppunt", "Ocultar pontuação: Nó", "Скрыть счёт: Узел", "隐藏分数：节点", "점수 숨기기: 노드", "إخفاء النتيجة: العقدة", "Sembunyikan Skor: Node", "Приховати рахунок: Вузол", "Απόκρυψη σκορ: Κόμβος", "Dölj poäng: Nod", "ซ่อนคะแนน: Node", "Skrýt skóre: Uzel", "Skjul score: Node", "Skoru gizle: Düğüm", "Skjul poeng: Node", "Pontszám elrejtése: Csomópont", "Piilota pisteet: Solmu", "Ẩn điểm: Node", "Ukryj wynik: Węzeł", "Ascunde scor: Nod"},
    {"Text Scale (Base %)", "文字スケール (基準 %)", "Textskalierung (Basis %)", "Escala de texto (base %)", "Échelle du texte (base %)", "Scala testo (base %)", "Tekstschaal (basis %)", "Escala de texto (base %)", "Масштаб текста (база %)", "文字缩放 (基准 %)", "텍스트 크기 (기준 %)", "مقياس النص (أساس %)", "Skala Teks (Dasar %)", "Масштаб тексту (база %)", "Κλίμακα κειμένου (βάση %)", "Textskala (bas %)", "ขนาดข้อความ (ฐาน %)", "Měřítko textu (základ %)", "Tekstskala (base %)", "Metin ölçeği (taban %)", "Tekstskala (base %)", "Szöveg méret (alap %)", "Tekstin skaala (perus %)", "Tỷ lệ chữ (cơ sở %)", "Skala tekstu (baza %)", "Scală text (bază %)"},
    {"Auto Scale Enable", "自動スケールを有効化", "Automatische Skalierung aktivieren", "Activar escala automática", "Activer l'échelle auto", "Abilita scala automatica", "Automatische schaling inschakelen", "Ativar escala automática", "Включить автомасштаб", "启用自动缩放", "자동 크기 조절 활성화", "تفعيل المقياس التلقائي", "Aktifkan skala otomatis", "Увімкнути автомасштаб", "Ενεργοποίηση αυτόματης κλίμακας", "Aktivera autoskalning", "เปิดใช้มาตราส่วนอัตโนมัติ", "Povolit automatické měřítko", "Aktivér autoskalering", "Otomatik ölçeği etkinleştir", "Aktiver autoskalering", "Automatikus skála engedélyezése", "Ota automaattinen skaalaus käyttöön", "Bật tỷ lệ tự động", "Włącz skalowanie automatyczne", "Activează scalare automată"},
    {"Auto Scale Global Cap %", "自動スケール全体上限 %", "Autom. Skalierung globales Limit %", "Límite global de escala automática %", "Plafond global d'échelle auto %", "Limite globale scala automatica %", "Globaal limiet auto-schaal %", "Limite global de escala automática %", "Глобальный предел авто-масштаба %", "自动缩放全局上限 %", "자동 배율 전역 상한 %", "حد أقصى عام للمقياس التلقائي %", "Batas global skala otomatis %", "Глобальний ліміт авто-масштабу %", "Καθολικό όριο αυτόματης κλίμακας %", "Global gräns för autoskala %", "ขีดจำกัดสเกลอัตโนมัติทั้งหมด %", "Globální limit auto měřítka %", "Global grænse for autoskala %", "Otomatik ölçek genel üst sınır %", "Global grense for autoskala %", "Automatikus skála globális felső határ %", "Automaattisen skaalauksen yleisraja %", "Giới hạn toàn cục tỷ lệ tự động %", "Globalny limit autoskalowania %", "Limită globală auto scală %"},
    {"Auto Scale Text Cap %", "自動スケール文字上限 %", "Autom. Skalierung Text-Obergrenze %", "Tope de texto de escala automática %", "Plafond de texte échelle auto %", "Limite testo scala automatica %", "Autom. schaal tekstlimiet %", "Limite de texto da escala automática %", "Лимит текста авто-масштаба %", "自动缩放文字上限 %", "자동 배율 텍스트 상한 %", "حد النص للمقياس التلقائي %", "Batas teks skala otomatis %", "Ліміт тексту авто-масштабу %", "Όριο κειμένου αυτόματης κλίμακας %", "Autom. skalning texttak %", "ขีดจำกัดข้อความสเกลอัตโนมัติ %", "Limit textu autom. měřítka %", "Autom. skalering tekstgrænse %", "Otomatik ölçek metin sınırı %", "Autom. skalering tekstgrense %", "Autom. skála szövegkorlát %", "Automaattisen skaalauksen tekstiraja %", "Giới hạn văn bản tỷ lệ tự động %", "Limit tekstu skali automatycznej %", "Limită text scară automată %"},
    {"Auto Scale Icon Cap %", "自動スケールアイコン上限 %", "Auto-Skalierung Symbol-Obergrenze %", "Límite de icono de escala auto %", "Plafond icône échelle auto %", "Limite icona scala auto %", "Auto-schaal pictogramlimiet %", "Limite de ícone de escala auto %", "Предел значка автомасштаба %", "自动缩放图标上限 %", "자동 배율 아이콘 상한 %", "حد أقصى لأيقونة التحجيم التلقائي %", "Batas Ikon Skala Otomatis %", "Ліміт значка автомасштабу %", "Όριο εικονιδίου αυτόματης κλίμακας %", "Autoskalningsikon-tak %", "ขีดจำกัดไอคอนปรับขนาดอัตโนมัติ %", "Limit ikony automatického škálování %", "Autoskalering ikonloft %", "Otomatik ölçek simge üst sınırı %", "Autoskalering ikontak %", "Automatikus skála ikon felső határ %", "Automaattisen skaalauksen ikonikatto %", "Giới hạn biểu tượng tỷ lệ tự động %", "Limit ikony skali automatycznej %", "Plafon pictogramă scalare automată %"},
    {"Auto Scale Gauge Cap %", "自動スケールゲージ上限 %", "Auto-Skalierung Anzeige-Obergrenze %", "Tope de escala automática del indicador %", "Plafond d'échelle auto de la jauge %", "Limite scala automatica indicatore %", "Auto-schaal meterplafond %", "Limite de escala automática do medidor %", "Предел авто-масштаба индикатора %", "自动缩放指示条上限 %", "자동 크기 조절 게이지 상한 %", "حد مقياس تلقائي للمؤشر %", "Batas Skala Otomatis Gauge %", "Межа авто-масштабу індикатора %", "Όριο αυτόματης κλίμακας μετρητή %", "Automatisk skalningsgräns för mätare %", "ขีดจำกัดมาตรวัดปรับขนาดอัตโนมัติ %", "Limit automatického měřítka ukazatele %", "Automatisk skalering af måler loft %", "Otomatik ölçek gösterge sınırı %", "Automatisk skalering av måler tak %", "Automatikus skála mérő felső határ %", "Automaattisen skaalauksen mittarin yläraja %", "Giới hạn tự động thang đo %", "Limit automatycznego skalowania wskaźnika %", "Limită auto-scalare indicator %"},
    {"Auto Scale Crosshair Cap %", "自動スケール照準上限 %", "Obergrenze Visier bei Auto-Skalierung %", "Límite de mira con escala automática %", "Plafond réticule échelle auto %", "Limite reticolo scala auto %", "Richtkruislimiet auto-schaling %", "Limite de mira com escala automática %", "Предел прицела при автомасштабе %", "自动缩放准星上限 %", "자동 크기 조절 조준선 상한 %", "حد التقاطع للمقياس التلقائي %", "Batas bidik skala otomatis %", "Ліміт прицілу при автомасштабі %", "Όριο στόχευσης αυτόματης κλίμακας %", "Autoskalning siktgräns %", "เพดานเล็งมาตราส่วนอัตโนมัติ %", "Limit zaměřovače auto měřítka %", "Autoskalering sigtegrænse %", "Otomatik ölçek nişangah sınırı %", "Autoskalering siktetak %", "Automatikus skála irányzék felső határ %", "Automaattisen skaalauksen tähtäimen yläraja %", "Giới hạn tâm ngắm tỷ lệ tự động %", "Limit celownika auto skali %", "Plafon țintă scalare automată %"},
        {"MPH (Default) is a 6px pixel font tuned for sharpness. For System/file fonts, Font Size sets the base render px (HUD SCALE still applies); weight/italic/effects apply too. The “Aa…” button opens a full font picker.", "MPH (標準) はシャープさに最適化された 6px ピクセルフォントです。システム/ファイルフォントでは、フォントサイズが基本描画 px を設定します (HUD SCALE も適用)。太さ/斜体/効果も反映されます。「Aa…」ボタンでフォントピッカーを開けます。", "MPH (Standard) ist eine 6px-Pixelfont, optimiert für Schärfe. Bei System-/Dateifonts legt Schriftgröße die Basis-Render-px fest (HUD SCALE gilt weiterhin); Stärke/Kursiv/Effekte ebenfalls. Die Schaltfläche „Aa…“ öffnet eine vollständige Schriftauswahl.", "MPH (predeterminado) es una fuente pixel de 6 px optimizada para nitidez. Con fuentes del sistema/archivo, Tamaño de fuente define los px base de renderizado (HUD SCALE sigue aplicándose); peso/cursiva/efectos también. El botón «Aa…» abre un selector de fuentes completo.", "MPH (par défaut) est une police pixel 6 px optimisée pour la netteté. Pour les polices système/fichier, Taille de police définit les px de rendu de base (HUD SCALE s’applique toujours) ; graisse/italique/effets aussi. Le bouton « Aa… » ouvre un sélecteur de police complet.", "MPH (predefinito) è un font pixel da 6 px ottimizzato per la nitidezza. Per font di sistema/file, Dimensione font imposta i px di rendering base (HUD SCALE si applica ancora); peso/corsivo/effetti inclusi. Il pulsante «Aa…» apre un selettore font completo.", "MPH (standaard) is een 6px pixelfont afgestemd op scherpte. Voor systeem-/bestandsfonts stelt Lettergrootte de basis-render-px in (HUD SCALE blijft gelden); gewicht/cursief/effecten ook. De knop „Aa…“ opent een volledige lettertypekiezer.", "MPH (padrão) é uma fonte pixel de 6 px otimizada para nitidez. Para fontes do sistema/arquivo, Tamanho da fonte define os px base de renderização (HUD SCALE ainda se aplica); peso/itálico/efeitos também. O botão «Aa…» abre um seletor de fontes completo.", "MPH (по умолчанию) — пиксельный шрифт 6 px, настроенный на резкость. Для системных/файловых шрифтов Размер шрифта задаёт базовые px рендера (HUD SCALE всё ещё применяется); начертание/курсив/эффекты тоже. Кнопка «Aa…» открывает полный выбор шрифта.", "MPH（默认）是专为清晰锐度调校的 6px 像素字体。系统/文件字体时，字体大小设置基础渲染 px（仍适用 HUD SCALE）；字重/斜体/效果同样生效。「Aa…」按钮可打开完整字体选择器。", "MPH(기본)은 선명도에 맞춘 6px 픽셀 폰트입니다. 시스템/파일 폰트에서는 글꼴 크기가 기본 렌더 px를 설정합니다(HUD SCALE도 적용). 굵기/기울임/효과도 적용됩니다. “Aa…” 버튼으로 전체 글꼴 선택기를 엽니다.", "MPH (افتراضي) خط بكسل 6px مُحسَّن للحدة. لخطوط النظام/الملف، حجم الخط يحدد px العرض الأساسي (HUD SCALE ما زال يُطبَّق)؛ الوزن/المائل/التأثيرات أيضاً. زر «Aa…» يفتح منتقي خطوط كاملاً.", "MPH (Default) adalah font piksel 6px yang disetel untuk ketajaman. Untuk font sistem/file, Ukuran Font menetapkan px render dasar (HUD SCALE tetap berlaku); berat/miring/efek juga. Tombol “Aa…” membuka pemilih font lengkap.", "MPH (за замовч.) — піксельний шрифт 6 px, налаштований на різкість. Для системних/файлових шрифтів Розмір шрифту задає базові px рендера (HUD SCALE все ще застосовується); насиченість/курсив/ефекти теж. Кнопка «Aa…» відкриває повний вибір шрифту.", "Το MPH (Προεπιλογή) είναι pixel γραμματοσειρά 6px βελτιστοποιημένη για ευκρίνεια. Για γραμματοσειρές συστήματος/αρχείου, το Μέγεθος γραμματοσειράς ορίζει τα βασικά px απόδοσης (HUD SCALE εξακολουθεί να ισχύει)· βάρος/πλάγια/εφέ επίσης. Το κουμπί «Aa…» ανοίγει πλήρη επιλογέα γραμματοσειράς.", "MPH (standard) är ett 6px pixelfont finjusterat för skärpa. För system-/filfonter ställer Teckenstorlek in bas-render-px (HUD SCALE gäller fortfarande); vikt/kursiv/effekter också. Knappen ”Aa…” öppnar en fullständig typsnittsväljare.", "MPH (ค่าเริ่มต้น) เป็นฟอนต์พิกเซล 6px ปรับให้คมชัด สำหรับฟอนต์ระบบ/ไฟล์ ขนาดฟอนต์กำหนด px เรนเดอร์พื้นฐาน (HUD SCALE ยังใช้ได้) น้ำหนัก/ตัวเอียง/เอฟเฟกต์ด้วย ปุ่ม “Aa…” เปิดตัวเลือกฟอนต์เต็มรูปแบบ", "MPH (výchozí) je 6px pixelové písmo laděné na ostrost. U systémových/souborových písem Velikost písma nastaví základní render px (HUD SCALE stále platí); tloušťka/kurzíva/efekty také. Tlačítko „Aa…“ otevře úplný výběr písma.", "MPH (standard) er en 6px pixelfont finjusteret til skarphed. For system-/filskrifttyper angiver Skriftstørrelse basis-render-px (HUD SCALE gælder stadig); vægt/kursiv/effekter også. Knappen ”Aa…” åbner en fuld skrifttypevælger.", "MPH (varsayılan), keskinlik için ayarlanmış 6px piksel yazı tipidir. Sistem/dosya yazı tiplerinde Yazı Boyutu temel render px değerini belirler (HUD SCALE hâlâ geçerlidir); kalınlık/italik/efektler de. “Aa…” düğmesi tam yazı tipi seçicisini açar.", "MPH (standard) er en 6px pixelfont finjustert for skarphet. For system-/filfonter setter Skriftstørrelse basis-render-px (HUD SCALE gjelder fortsatt); vekt/kursiv/effekter også. Knappen «Aa…» åpner en full skrifttypevelger.", "Az MPH (alapértelmezett) egy 6px-es pixelfont, élességre hangolva. Rendszer-/fájlbetűknél a Betűméret adja meg az alap render px értéket (a HUD SCALE továbbra is érvényes); vastagság/dőlt/effektusok is. Az „Aa…” gomb teljes betűválasztót nyit meg.", "MPH (oletus) on 6px pikselifontti, joka on viritetty terävyyteen. Järjestelmä-/tiedostofonteilla Fonttikoko asettaa perusrenderöintipx (HUD SCALE edelleen voimassa); paino/kursiivi/tehosteet myös. ”Aa…”-painike avaa täyden fontinvalitsimen.", "MPH (mặc định) là font pixel 6px tối ưu độ sắc nét. Với font hệ thống/tệp, Cỡ chữ đặt px render cơ bản (HUD SCALE vẫn áp dụng); độ đậm/nghiêng/hiệu ứng cũng vậy. Nút “Aa…” mở bộ chọn phông đầy đủ.", "MPH (domyślny) to 6px font pikselowy dostrojony pod ostrość. Dla fontów systemowych/plikowych Rozmiar czcionki ustawia bazowe px renderowania (HUD SCALE nadal obowiązuje); grubość/kursywa/efekty też. Przycisk „Aa…” otwiera pełny wybór czcionki.", "MPH (implicit) este un font pixel de 6px optimizat pentru claritate. Pentru fonturi de sistem/fișier, Mărime font setează px-ul de randare de bază (HUD SCALE se aplică în continuare); greutate/cursiv/efecte de asemenea. Butonul «Aa…» deschide un selector complet de fonturi."},
{"Font Source", "フォントソース", "Schriftquelle", "Fuente de tipografía", "Source de police", "Origine font", "Lettertypebron", "Fonte da fonte", "Источник шрифта", "字体来源", "글꼴 소스", "مصدر الخط", "Sumber font", "Джерело шрифту", "Πηγή γραμματοσειράς", "Teckensnittskälla", "แหล่งฟอนต์", "Zdroj písma", "Skrifttypekilde", "Yazı tipi kaynağı", "Skrifttypekilde", "Betűforrás", "Fontin lähde", "Nguồn phông", "Źródło czcionki", "Sursă font"},
    {"Font Size (px)", "フォントサイズ (px)", "Schriftgröße (px)", "Tamaño de fuente (px)", "Taille de police (px)", "Dimensione carattere (px)", "Lettergrootte (px)", "Tamanho da fonte (px)", "Размер шрифта (px)", "字体大小 (px)", "글꼴 크기 (px)", "حجم الخط (px)", "Ukuran font (px)", "Розмір шрифту (px)", "Μέγεθος γραμματοσειράς (px)", "Teckenstorlek (px)", "ขนาดฟอนต์ (px)", "Velikost písma (px)", "Skriftstørrelse (px)", "Yazı tipi boyutu (px)", "Skriftstørrelse (px)", "Betűméret (px)", "Fonttikoko (px)", "Cỡ phông (px)", "Rozmiar czcionki (px)", "Dimensiune font (px)"},
    {"Font Weight", "フォントウェイト", "Schriftstärke", "Grosor de fuente", "Épaisseur de police", "Spessore carattere", "Lettertypedikte", "Peso da fonte", "Насыщенность шрифта", "字体粗细", "글꼴 굵기", "سمك الخط", "Ketebalan Font", "Насиченість шрифту", "Βάρος γραμματοσειράς", "Teckensnittsvikt", "น้ำหนักฟอนต์", "Tloušťka písma", "Skrifttypevægt", "Yazı Tipi Kalınlığı", "Skriftvekt", "Betűvastagság", "Fontin paksuus", "Độ đậm phông", "Grubość czcionki", "Grosime font"},
    {"Italic", "イタリック", "Kursiv", "Cursiva", "Italique", "Corsivo", "Cursief", "Itálico", "Курсив", "斜体", "기울임", "مائل", "Miring", "Курсив", "Πλάγια", "Kursiv", "ตัวเอียง", "Kurzíva", "Kursiv", "İtalik", "Kursiv", "Dőlt", "Kursiivi", "Nghiêng", "Kursywa", "Cursiv"},
    {"Underline", "下線", "Unterstreichen", "Subrayado", "Souligné", "Sottolineato", "Onderstrepen", "Sublinhado", "Подчёркивание", "下划线", "밑줄", "تسطير", "Garis bawah", "Підкреслення", "Υπογράμμιση", "Understrykning", "ขีดเส้นใต้", "Podtržení", "Understregning", "Altı çizili", "Understrekning", "Aláhúzás", "Alleviivaus", "Gạch chân", "Podkreślenie", "Subliniere"},
    {"Strikethrough", "取り消し線", "Durchgestrichen", "Tachado", "Barré", "Barrato", "Doorhalen", "Tachado", "Зачёркнутый", "删除线", "취소선", "يتوسطه خط", "Coret", "Закреслення", "Διαγράμμιση", "Genomstrykning", "ขีดฆ่า", "Přeškrtnutí", "Gennemstregning", "Üstü çizili", "Gjennomstreking", "Áthúzás", "Yliviivaus", "Gạch ngang", "Przekreślenie", "Tăiere text"},
    {"Color", "色", "Farbe", "Color", "Couleur", "Colore", "Kleur", "Cor", "Цвет", "颜色", "색상", "اللون", "Warna", "Колір", "Χρώμα", "Färg", "สี", "Barva", "Farve", "Renk", "Farge", "Szín", "Väri", "Màu", "Kolor", "Culoare"},
    {"Scale %", "スケール %", "Skalierung %", "Escala %", "Échelle %", "Scala %", "Schaal %", "Escala %", "Масштаб %", "缩放 %", "배율 %", "التحجيم %", "Skala %", "Масштаб %", "Κλίμακα %", "Skalning %", "สเกล %", "Měřítko %", "Skalering %", "Ölçek %", "Skalering %", "Skála %", "Skaalaus %", "Tỷ lệ %", "Skala %", "Scală %"},
    {"Zoom Stage", "ズーム段階", "Zoom-Stufe", "Etapa de zoom", "Niveau de zoom", "Fase zoom", "Zoomfase", "Estágio de zoom", "Стадия зума", "缩放阶段", "줌 단계", "مرحلة التكبير", "Tahap Zoom", "Стадія зуму", "Στάδιο ζουμ", "Zoomsteg", "ขั้นตอนซูม", "Fáze zoomu", "Zoomtrin", "Zoom aşaması", "Zoomtrinn", "Zoom szakasz", "Zoom-vaihe", "Giai đoạn zoom", "Etap zoomu", "Etapă zoom"},
    {"Zoom Base Scale %", "ズーム時元照準スケール %", "Basis-Visiergröße beim Zoom %", "Escala base de mira con zoom %", "Échelle de base du réticule au zoom %", "Scala base reticolo zoom %", "Basis richtkruisschaal bij zoom %", "Escala base de mira com zoom %", "Базовый масштаб прицела при зуме %", "缩放时准星基础比例 %", "확대 시 기본 조준선 크기 %", "مقياس التقاطع الأساسي عند التكبير %", "Skala dasar bidik zoom %", "Базовий масштаб прицілу при зумі %", "Βασική κλίμακα στόχευσης ζουμ %", "Zoom bas-skala sikte %", "สเกลฐานเล็งซูม %", "Základní měřítko zaměřovače při zoomu %", "Zoom basisskala sigte %", "Zoom temel nişangah ölçeği %", "Zoom grunnskalering sikte %", "Zoom alap irányzék skála %", "Zoomin perustähtäimen skaala %", "Tỷ lệ cơ sở tâm ngắm zoom %", "Bazowa skala celownika zoom %", "Scală de bază țintă zoom %"},
    {"Zoom Base Opacity %", "ズーム時元照準不透明度 %", "Zoom-Basis-Deckkraft %", "Opacidad base del zoom %", "Opacité de base du zoom %", "Opacità base zoom %", "Zoom-basisdekking %", "Opacidade base do zoom %", "Базовая непрозрачность зума %", "缩放基础不透明度 %", "줌 기본 불투명도 %", "شفافية أساس التكبير %", "Opasitas dasar zoom %", "Базова непрозорість зуму %", "Βασική αδιαφάνεια zoom %", "Zoom basopacitet %", "ความทึบพื้นฐานซูม %", "Základní neprůhlednost zoomu %", "Zoom basisopacitet %", "Zoom temel opaklık %", "Zoom grunnopasitet %", "Zoom alap átlátszatlanság %", "Zoomin perusläpinäkyvyys %", "Độ mờ cơ sở zoom %", "Podstawowa przezroczystość zoomu %", "Opacitate de bază zoom %"},
    {"Zoom Opacity %", "ズーム時不透明度 %", "Zoom-Deckkraft %", "Opacidad del zoom %", "Opacité du zoom %", "Opacità zoom %", "Zoom-dekking %", "Opacidade do zoom %", "Непрозрачность зума %", "缩放不透明度 %", "줌 불투명도 %", "شفافية التكبير %", "Opasitas zoom %", "Непрозорість зуму %", "Αδιαφάνεια ζουμ %", "Zoom-opacitet %", "ความทึบซูม %", "Neprůhlednost zoomu %", "Zoom-gennemsigtighed %", "Yakınlaştırma opaklığı %", "Zoom-opasitet %", "Zoom átlátszatlanság %", "Zoomin läpinäkymättömyys %", "Độ mờ zoom %", "Krycie zoomu %", "Opacitate zoom %"},
    {"Zoom Scope Reticle", "ズームスコープ照準", "Zoom-Fadenkreuz", "Retícula de zoom", "Réticule de zoom", "Reticolo zoom", "Zoom-richtkruis", "Retículo de zoom", "Прицел увеличения", "缩放瞄准镜", "줌 스코프 조준선", "شبكة التصويب للتكبير", "Reticle Zoom Scope", "Прицільна сітка зума", "Σταυρονόμιο ζουμ", "Zoomsikte", "เรติเคิลซูม", "Mířidla zoomu", "Zoomsigtekorn", "Yakınlaştırma nişangahı", "Zoom-sikte", "Zoom célkereszt", "Zoom-tähtäin", "Tâm ngắm zoom", "Celownik zoomu", "Reticul zoom"},
    {"Zoom Scope", "ズームスコープ", "Zoom-Visier", "Visor de zoom", "Lunette de zoom", "Mirino zoom", "Zoomvizier", "Visor de zoom", "Прицел зума", "缩放瞄准镜", "줌 스코프", "منظار التكبير", "Scope Zoom", "Приціл зуму", "Σκόπευση ζουμ", "Zoomsikte", "กล้องซูม", "Zoom zaměřovač", "Zoomsigte", "Zoom dürbünü", "Zoomsikte", "Zoom távcső", "Zoom-tähtäin", "Ống ngắm zoom", "Luneta zoomu", "Scope zoom"},
    {"Scope Radius", "スコープ半径", "Visierradius", "Radio de mira", "Rayon du viseur", "Raggio mirino", "Vizierradius", "Raio da mira", "Радиус прицела", "准星半径", "스코프 반경", "نصف قطر المنظار", "Radius scope", "Радіус прицілу", "Ακτίνα σκοπευτήρα", "Sikteradius", "รัศมีสโคป", "Poloměr zaměřovače", "Sigteradius", "Nişangah yarıçapı", "Sikteradius", "Távcső sugara", "Tähtäimen säde", "Bán kính scope", "Promień celownika", "Rază lunetă"},
    {"Scope Gap", "スコープ隙間", "Visierabstand", "Separación del visor", "Écart du viseur", "Spazio mirino", "Visierafstand", "Espaço da mira", "Зазор прицела", "瞄准镜间距", "스코프 간격", "فجوة التصويب", "Celah scope", "Зазор прицілу", "Κενό σκοπευτικού", "Siktegap", "ช่องว่าง scope", "Mezera zaměřovače", "Sigteafstand", "Nişangah boşluğu", "Siktegap", "Irányzék rés", "Tähtäimen väli", "Khoảng cách scope", "Luka celownika", "Spațiu reticul"},
    {"Scope Thickness", "スコープ太さ", "Visierstärke", "Grosor del visor", "Épaisseur du viseur", "Spessore mirino", "Vizierdikte", "Espessura da mira", "Толщина прицела", "瞄准镜粗细", "스코프 두께", "سُمك المنظار", "Ketebalan scope", "Товщина прицілу", "Πάχος σκόπευτήρου", "Siktetjocklek", "ความหนาเล็ง", "Tloušťka zaměřovače", "Sigtetykkelse", "Nişangah kalınlığı", "Siktetykkelse", "Irányzék vastagsága", "Tähtäimen paksuus", "Độ dày ống ngắm", "Grubość celownika", "Grosime lunetă"},
    {"Scope Thick.", "スコープ太さ", "Fadenkreuz-Dicke", "Grosor de retícula", "Épaisseur réticule", "Spess. mirino", "Mirino-dikte", "Espess. retícula", "Толщ. прицела", "瞄准镜粗细", "스코프 두께", "سُمك الشبكة", "Ketebalan Reticle", "Товщ. прицілу", "Πάχος σταυρ.", "Siktetjocklek", "ความหนาเรติเคิล", "Tloušťka mířidla", "Sigtekornstykkelse", "Nişangah kalınlığı", "Siktetykkelse", "Célkereszt vastagság", "Tähtäimen paksuus", "Độ dày tâm ngắm", "Grubość celownika", "Grosime reticul"},
    {"Scope Center Dot", "スコープ中央ドット", "Visier-Mittelpunkt", "Punto central del visor", "Point central du viseur", "Punto centrale mirino", "Vizier middenpunt", "Ponto central do visor", "Центральная точка прицела", "瞄准镜中心点", "스코프 중앙 점", "نقطة مركز المنظار", "Titik Tengah Scope", "Центральна точка прицілу", "Κεντρική κουκκίδα σκόπευσης", "Sikte mittpunkt", "จุดกึ่งกลาง scope", "Středová tečka zaměřovače", "Sigte midtpunkt", "Dürbün merkez noktası", "Sikte midtpunkt", "Távcső középpont", "Tähtäimen keskipiste", "Chấm giữa ống ngắm", "Punkt centralny lunety", "Punct central scope"},
    {"Scope Dot Size", "スコープドットサイズ", "Visierpunktgröße", "Tamaño del punto de mira", "Taille du point du viseur", "Dimensione punto mirino", "Vizierpuntgrootte", "Tamanho do ponto da mira", "Размер точки прицела", "准星点大小", "스코프 점 크기", "حجم نقطة المنظار", "Ukuran titik scope", "Розмір точки прицілу", "Μέγεθος σημείου σκοπευτήρα", "Siktpunktstorlek", "ขนาดจุดสโคป", "Velikost bodu zaměřovače", "Sigtepunktstørrelse", "Nişangah nokta boyutu", "Siktepunktstørrelse", "Távcső pont mérete", "Tähtäimen pisteen koko", "Kích thước chấm scope", "Rozmiar punktu celownika", "Dimensiune punct lunetă"},
    {"Scope Dot Opacity %", "スコープドット不透明度 %", "Visierpunkt-Deckkraft %", "Opacidad del punto del visor %", "Opacité du point du viseur %", "Opacità punto mirino %", "Visierpunt-dekking %", "Opacidade do ponto da mira %", "Непрозрачность точки прицела %", "瞄准镜圆点不透明度 %", "스코프 점 불투명도 %", "شفافية نقطة التصويب %", "Opasitas titik scope %", "Непрозорість точки прицілу %", "Αδιαφάνεια κουκκίδας σκοπευτικού %", "Siktepunkt opacitet %", "ความทึบจุด scope %", "Neprůhlednost bodu zaměřovače %", "Sigtepunkt opacitet %", "Nişangah noktası opaklığı %", "Siktepunkt opasitet %", "Irányzék pont átlátszatlanság %", "Tähtäinpisteen läpinäkyvyys %", "Độ mờ chấm scope %", "Przezroczystość kropki celownika %", "Opacitate punct reticul %"},
    {"Scope Opacity %", "スコープ不透明度 %", "Visier-Deckkraft %", "Opacidad del visor %", "Opacité du viseur %", "Opacità mirino %", "Vizierdekking %", "Opacidade da mira %", "Непрозрачность прицела %", "瞄准镜不透明度 %", "스코프 불투명도 %", "شفافية المنظار %", "Opasitas scope %", "Непрозорість прицілу %", "Αδιαφάνεια σκόπευτήρου %", "Siktets opacitet %", "ความทึบเล็ง %", "Neprůhlednost zaměřovače %", "Sigtets gennemsigtighed %", "Nişangah opaklığı %", "Siktets opasitet %", "Irányzék átlátszatlanság %", "Tähtäimen läpinäkymättömyys %", "Độ mờ ống ngắm %", "Krycie celownika %", "Opacitate lunetă %"},
    {"Zoom Transition", "ズームトランジション", "Zoom-Übergang", "Transición de zoom", "Transition de zoom", "Transizione zoom", "Zoom-overgang", "Transição de zoom", "Переход увеличения", "缩放过渡", "줌 전환", "انتقال التكبير", "Transisi Zoom", "Перехід зуму", "Μετάβαση ζουμ", "Zoomövergång", "การเปลี่ยนซูม", "Přechod zoomu", "Zoomovergang", "Yakınlaştırma geçişi", "Zoomovergang", "Zoom átmenet", "Zoom-siirtymä", "Chuyển zoom", "Przejście zoomu", "Tranziție zoom"},
    {"Zoom Transition Speed %", "ズーム遷移速度 %", "Zoom-Übergangsgeschwindigkeit %", "Velocidad de transición de zoom %", "Vitesse de transition du zoom %", "Velocità transizione zoom %", "Zoomovergangssnelheid %", "Velocidade de transição do zoom %", "Скорость перехода зума %", "缩放过渡速度 %", "줌 전환 속도 %", "سرعة انتقال التكبير %", "Kecepatan Transisi Zoom %", "Швидкість переходу зуму %", "Ταχύτητα μετάβασης ζουμ %", "Zoomövergångshastighet %", "ความเร็วการเปลี่ยนซูม %", "Rychlost přechodu zoomu %", "Zoom-overgangshastighed %", "Zoom geçiş hızı %", "Zoom-overgangshastighet %", "Zoom átmeneti sebesség %", "Zoom-siirtymän nopeus %", "Tốc độ chuyển zoom %", "Szybkość przejścia zoomu %", "Viteză tranziție zoom %"},
    {"Transition Speed %", "遷移速度 %", "Übergangsgeschwindigkeit %", "Velocidad de transición %", "Vitesse de transition %", "Velocità transizione %", "Overgangssnelheid %", "Velocidade de transição %", "Скорость перехода %", "过渡速度 %", "전환 속도 %", "سرعة الانتقال %", "Kecepatan transisi %", "Швидкість переходу %", "Ταχύτητα μετάβασης %", "Övergångshastighet %", "ความเร็วการเปลี่ยน %", "Rychlost přechodu %", "Overgangshastighed %", "Geçiş hızı %", "Overgangshastighet %", "Átmeneti sebesség %", "Siirtymänopeus %", "Tốc độ chuyển %", "Szybkość przejścia %", "Viteză tranziție %"},
    {"Zoom Pulse Ring", "ズームパルスリング", "Zoom-Pulsring", "Anillo de pulso del zoom", "Anneau pulsé du zoom", "Anello pulsante zoom", "Zoom-pulsring", "Anel de pulso do zoom", "Пульсирующее кольцо зума", "缩放脉冲环", "줌 펄스 링", "حلقة نبض التكبير", "Cincin pulsa zoom", "Пульсуюче кільце зуму", "Δακτύλιος παλμού zoom", "Zoom-pulsring", "วงแหวนพัลส์ซูม", "Zoom pulzní kruh", "Zoom-pulsring", "Zoom nabız halkası", "Zoom-pulsring", "Zoom pulzáló gyűrű", "Zoom-pulssirengas", "Vòng xung zoom", "Pierścień impulsu zoomu", "Inel puls zoom"},
    {"Pulse Ring", "パルスリング", "Pulsring", "Anillo pulsante", "Anneau pulsant", "Anello pulsante", "Pulsring", "Anel pulsante", "Пульсирующее кольцо", "脉冲环", "펄스 링", "حلقة النبض", "Cincin denyut", "Пульсуюче кільце", "Δακτύλιος παλμού", "Pulsring", "วงแหวนพัลส์", "Pulzní kruh", "Pulsring", "Nabız halkası", "Pulsring", "Pulzáló gyűrű", "Pulssirengas", "Vòng xung", "Pierścień pulsacji", "Inel pulsatoriu"},
    {"Zoom Pulse Strength %", "ズームパルス強度 %", "Zoom-Puls-Stärke %", "Intensidad de pulso de zoom %", "Intensité impulsion zoom %", "Intensità impulso zoom %", "Zoom-pulssterkte %", "Intensidade de pulso de zoom %", "Сила пульсации увеличения %", "缩放脉冲强度 %", "줌 펄스 강도 %", "قوة نبض التكبير %", "Kekuatan Pulsa Zoom %", "Сила імпульсу зуму %", "Ένταση παλμού ζουμ %", "Zoompulstyrka %", "ความแรงพัลส์ซูม %", "Síla pulzu zoomu %", "Zoom-pulsstyrke %", "Yakınlaştırma darbe gücü %", "Zoom-pulsstyrke %", "Zoom impulzus erősség %", "Zoom-pulssin voimakkuus %", "Cường độ xung zoom %", "Siła impulsu zoomu %", "Putere puls zoom %"},
    {"Pulse Strength %", "パルス強度 %", "Pulsstärke %", "Intensidad del pulso %", "Intensité de pulsation %", "Intensità impulso %", "Pulssterkte %", "Intensidade do pulso %", "Сила пульсации %", "脉冲强度 %", "펄스 강도 %", "قوة النبض %", "Kekuatan Pulsa %", "Сила пульсації %", "Ένταση παλμού %", "Pulsstyrka %", "ความแรงพัลส์ %", "Síla pulzu %", "Pulsstyrke %", "Darbe gücü %", "Pulsstyrke %", "Impulzus erősség %", "Pulssin voimakkuus %", "Cường độ xung %", "Siła impulsu %", "Putere puls %"},
    {"Zoom Crosshair", "ズーム照準", "Zoom-Visier", "Punto de mira con zoom", "Réticule au zoom", "Reticolo zoom", "Zoom-richtkruis", "Mira com zoom", "Прицел при зуме", "缩放准星", "확대 조준선", "تقاطع التكبير", "Bidik zoom", "Приціл при зумі", "Στόχευση ζουμ", "Zoom-sikte", "เล็งซูม", "Zaměřovač při zoomu", "Zoom-sigtekors", "Zoom nişangahı", "Zoom-trådkors", "Zoom irányzék", "Zoom-tähtäin", "Tâm ngắm zoom", "Celownik zoom", "Țintă zoom"},
    {"Transition Style", "トランジションスタイル", "Übergangsstil", "Estilo de transición", "Style de transition", "Stile transizione", "Overgangsstijl", "Estilo de transição", "Стиль перехода", "过渡样式", "전환 스타일", "نمط الانتقال", "Gaya transisi", "Стиль переходу", "Στυλ μετάβασης", "Övergångsstil", "สไตล์การเปลี่ยน", "Styl přechodu", "Overgangsstil", "Geçiş stili", "Overgangsstil", "Átmenet stílusa", "Siirtymätyyli", "Kiểu chuyển", "Styl przejścia", "Stil tranziție"},
    {"Custom Scope Dot Color", "スコープドット色を個別指定", "Eigene Visierpunkt-Farbe", "Color personalizado del punto del visor", "Couleur personnalisée du point de visée", "Colore punto mirino personalizzato", "Aangepaste vizierpuntkleur", "Cor personalizada do ponto da mira", "Пользовательский цвет точки прицела", "自定义瞄准点颜色", "사용자 스코프 점 색상", "لون نقطة المنظار المخصص", "Warna titik scope kustom", "Користувацький колір точки прицілу", "Προσαρμοσμένο χρώμα σημείου σκόπευτήρου", "Anpassad siktprickfärg", "สีจุดเล็งกำหนดเอง", "Vlastní barva bodu zaměřovače", "Brugerdefineret sigtepunktfarve", "Özel nişangah noktası rengi", "Egendefinert siktepunktfarge", "Egyéni irányzékpont szín", "Mukautettu tähtäinpisteen väri", "Màu chấm ống ngắm tùy chỉnh", "Niestandardowy kolor punktu celownika", "Culoare personalizată punct lunetă"},
    {"Scope Dot Color", "スコープドット色", "Fadenkreuz-Punktfarbe", "Color del punto de retícula", "Couleur point réticule", "Colore punto mirino", "Mirinopuntkleur", "Cor do ponto da retícula", "Цвет точки прицела", "瞄准镜圆点颜色", "스코프 점 색상", "لون نقطة الشبكة", "Warna Titik Reticle", "Колір точки прицілу", "Χρώμα κουκκίδας σταυρού", "Siktepunktfärg", "สีจุดเรติเคิล", "Barva tečky mířidla", "Sigtekornspunktfarve", "Nişangah nokta rengi", "Siktetpunktfarge", "Célkereszt pont színe", "Tähtäinpisteen väri", "Màu chấm tâm ngắm", "Kolor kropki celownika", "Culoare punct reticul"},
    {"Staged", "段階", "Gestuft", "Por etapas", "Par étapes", "A fasi", "Gefaseerd", "Por estágios", "Поэтапный", "分阶段", "단계별", "متدرج", "Bertahap", "Поетапний", "Σταδιακό", "Stegvis", "เป็นขั้น", "Postupné", "Trinvis", "Aşamalı", "Trinnvis", "Fokozatos", "Vaiheittainen", "Theo giai đoạn", "Etapy", "Pe etape"},
    {"Fade", "フェード", "Einblenden", "Fundido", "Fondu", "Dissolvenza", "Vervagen", "Desvanecer", "Затухание", "淡入淡出", "페이드", "تلاشي", "Fade", "Затухання", "Ξεθώριασμα", "Tona in", "เฟด", "Prolnutí", "Fade", "Solma", "Fade", "Áttűnés", "Häivytys", "Mờ dần", "Zanikanie", "Estompare"},
    {"Glitch", "グリッチ", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "故障", "글리치", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch", "Glitch"},
    {"Glitch2", "グリッチ2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2", "Glitch2"},
    {"Snap", "スナップ", "Einrasten", "Ajuste", "Accrochage", "Aggancio", "Snap", "Encaixe", "Привязка", "吸附", "스냅", "التثبيت", "Snap", "Прив'язка", "Κούμπωμα", "Fäst", "สแนป", "Přichycení", "Snap", "Yapış", "Snap", "Illesztés", "Kiinnitys", "Snap", "Przyciąganie", "Fixare"},
    {"Digital", "デジタル", "Digital", "Digital", "Numérique", "Digitale", "Digitaal", "Digital", "Цифровой", "数字", "디지털", "رقمي", "Digital", "Цифровий", "Ψηφιακό", "Digital", "ดิจิทัล", "Digitální", "Digital", "Dijital", "Digital", "Digitális", "Digitaalinen", "Kỹ thuật số", "Cyfrowy", "Digital"},
    {"Pulse Wave", "パルス波", "Pulswelle", "Onda pulsante", "Onde pulsée", "Onda pulsante", "Pulsgolf", "Onda pulsante", "Импульсная волна", "脉冲波", "펄스 파", "موجة نبضية", "Gelombang pulsa", "Імпульсна хвиля", "Παλμική κυματική", "Pulsvåg", "คลื่นพัลส์", "Pulzní vlna", "Pulsbølge", "Darbe dalgası", "Pulsbølge", "Impulzushullám", "Pulssiaalto", "Sóng xung", "Fala impulsowa", "Undă puls"},
    {"Magic Circle", "魔法陣", "Magischer Kreis", "Círculo mágico", "Cercle magique", "Cerchio magico", "Magische cirkel", "Círculo mágico", "Магический круг", "魔法阵", "마법진", "دائرة سحرية", "Lingkaran ajaib", "Магічне коло", "Μαγικός κύκλος", "Magisk cirkel", "วงเวทมนตร์", "Magický kruh", "Magisk cirkel", "Sihirli daire", "Magisk sirkel", "Varázskör", "Taikapiiri", "Vòng ma thuật", "Magiczne koło", "Cerc magic"},
    {"SF Movie", "SF映画", "SF-Film", "Película SF", "Film SF", "Film SF", "SF-film", "Filme SF", "SF-фильм", "SF 影片", "SF 영화", "فيلم SF", "Film SF", "SF-фільм", "Ταινία SF", "SF-film", "ภาพยนตร์ SF", "SF film", "SF-film", "SF filmi", "SF-film", "SF film", "SF-elokuva", "Phim SF", "Film SF", "Film SF"},
    {"Tactical Lock", "戦術ロック", "Taktische Sperre", "Bloqueo táctico", "Verrouillage tactique", "Blocco tattico", "Tactische vergrendeling", "Bloqueio tático", "Тактическая блокировка", "战术锁定", "전술 잠금", "قفل تكتيكي", "Kunci Taktis", "Тактичне блокування", "Τακτικό κλείδωμα", "Taktiskt lås", "ล็อกเชิงกล", "Taktické zamčení", "Taktisk lås", "Taktik kilitleme", "Taktisk lås", "Taktikai zár", "Taktinen lukitus", "Khóa chiến thuật", "Blokada taktyczna", "Blocare tactică"},
    {"Sniper Optics", "スナイパー光学", "Scharfschützenoptik", "Óptica de francotirador", "Optique de sniper", "Ottica da cecchino", "Scherpschutteroptiek", "Óptica de sniper", "Снайперская оптика", "狙击光学", "스나이퍼 광학", "بصريات القناص", "Optik Sniper", "Снайперська оптика", "Οπτικά σκοπευτή", "Prickskytteoptik", "Sniper Optics", "Odstřelovačská optika", "Snigskytteoptik", "Keskin nişancı optiği", "Skarpskytteroptikk", "Mesterlövész optika", "Tarkkuuskiväärin optiikka", "Quang học bắn tỉa", "Optyka snajperska", "Optică sniper"},
    {"Drone LIDAR", "ドローンLIDAR", "Drohnen-LIDAR", "LIDAR del dron", "LIDAR du drone", "LIDAR drone", "Drone-LIDAR", "LIDAR do drone", "LIDAR дрона", "无人机 LIDAR", "드론 LIDAR", "LIDAR الطائرة بدون طيار", "LIDAR drone", "LIDAR дрона", "LIDAR drone", "Drönar-LIDAR", "LIDAR โดรน", "Dron LIDAR", "Drone-LIDAR", "Drone LIDAR", "Drone-LIDAR", "Drón LIDAR", "Drone-LIDAR", "LIDAR drone", "LIDAR drona", "LIDAR dronă"},
    {"Beam Charge", "ビームチャージ", "Strahlaufladung", "Carga del rayo", "Charge du rayon", "Carica raggio", "Straalopladen", "Carga do raio", "Заряд луча", "光束充能", "빔 충전", "شحن الشعاع", "Charge beam", "Заряд променя", "Φόρτιση δέσμης", "Strålladdning", "ชาร์จลำแสง", "Nabití paprsku", "Strålopladning", "Işın şarjı", "Strållading", "Sugár töltés", "Säteen lataus", "Nạp chùm", "Ładowanie promienia", "Încărcare rază"},
    {"Zoom", "ズーム", "Zoom", "Zoom", "Zoom", "Zoom", "Zoom", "Zoom", "Зум", "缩放", "줌", "تكبير", "Zoom", "Зум", "Ζουμ", "Zoom", "ซูม", "Zoom", "Zoom", "Yakınlaştırma", "Zoom", "Nagyítás", "Zoom", "Thu phóng", "Powiększenie", "Zoom"},
    {"Outline", "アウトライン", "Umriss", "Contorno", "Contour", "Contorno", "Omtrek", "Contorno", "Контур", "轮廓", "외곽선", "حد خارجي", "Garis Luar", "Контур", "Περίγραμμα", "Kontur", "เส้นขอบ", "Obrys", "Kontur", "Ana hat", "Kontur", "Körvonal", "Ääriviiva", "Viền", "Obrys", "Contur"},
    {"Outline Color", "アウトライン色", "Umrissfarbe", "Color del contorno", "Couleur du contour", "Colore contorno", "Omtrekkleur", "Cor do contorno", "Цвет контура", "轮廓颜色", "외곽선 색상", "لون الحد", "Warna Garis", "Колір контуру", "Χρώμα περιγράμματος", "Konturfärg", "สีเส้นขอบ", "Barva obrysu", "Konturfarve", "Ana hat rengi", "Konturfarge", "Körvonal szín", "Ääriviivan väri", "Màu viền", "Kolor obrysu", "Culoare contur"},
    {"Outline Opacity", "アウトライン不透明度", "Konturdeckkraft", "Opacidad del contorno", "Opacité du contour", "Opacità contorno", "Contourdekking", "Opacidade do contorno", "Непрозрачность контура", "轮廓不透明度", "외곽선 불투명도", "شفافية الحدود", "Opasitas garis luar", "Непрозорість контуру", "Αδιαφάνεια περιγράμματος", "Konturopacitet", "ความทึบเส้นขอบ", "Neprůhlednost obrysu", "Konturopacitet", "Dış çizgi opaklığı", "Konturopasitet", "Körvonal átlátszatlansága", "Ääriviivan läpinäkymättömyys", "Độ mờ viền", "Krycie obrysu", "Opacitate contur"},
    {"Outline Thickness", "アウトライン太さ", "Konturstärke", "Grosor del contorno", "Épaisseur du contour", "Spessore contorno", "Contourdikte", "Espessura do contorno", "Толщина контура", "轮廓粗细", "외곽선 두께", "سُمك الحد", "Ketebalan outline", "Товщина контуру", "Πάχος περιγράμματος", "Konturtjocklek", "ความหนาเส้นขอบ", "Tloušťka obrysu", "Konturtykkelse", "Ana hat kalınlığı", "Konturtykkelse", "Körvonal vastagság", "Ääriviivan paksuus", "Độ dày viền", "Grubość obrysu", "Grosime contur"},
    {"Outline Thick.", "アウトライン太さ", "Konturstärke", "Grosor del contorno", "Épaiss. contour", "Spess. contorno", "Omlijndikte", "Espess. contorno", "Толщ. контура", "轮廓粗细", "외곽선 두께", "سُمك الحدود", "Ketebalan garis luar", "Товщина контуру", "Πάχος περιγράμματος", "Konturtjocklek", "ความหนาเส้นขอบ", "Tloušťka obrysu", "Konturtykkelse", "Dış çizgi kalınlığı", "Konturtykkelse", "Kontúrvastagság", "Ääriviivan paksuus", "Độ dày viền", "Grubość konturu", "Grosime contur"},
    {"Center Dot", "中央ドット", "Mittelpunkt", "Punto central", "Point central", "Punto centrale", "Middelpunt", "Ponto central", "Центральная точка", "中心圆点", "중앙 점", "نقطة مركزية", "Titik Tengah", "Центральна точка", "Κεντρική κουκκίδα", "Mittpunkt", "จุดกลาง", "Středová tečka", "Midterpunkt", "Merkez nokta", "Midtpunkt", "Középpont", "Keskipiste", "Chấm giữa", "Kropka środkowa", "Punct central"},
    {"Custom Dot Color", "ドット色を個別指定", "Eigene Punktfarbe", "Color de punto personalizado", "Couleur de point personnalisée", "Colore punto personalizzato", "Aangepaste puntkleur", "Cor de ponto personalizada", "Пользовательский цвет точки", "自定义点颜色", "사용자 지정 점 색상", "لون النقطة المخصص", "Warna Titik Kustom", "Користувацький колір точки", "Προσαρμοσμένο χρώμα κουκκίδας", "Anpassad punktfärg", "สีจุดกำหนดเอง", "Vlastní barva tečky", "Brugerdefineret punktfarve", "Özel nokta rengi", "Egendefinert punktfarge", "Egyéni pont szín", "Mukautettu pisteen väri", "Màu chấm tùy chỉnh", "Niestandardowy kolor punktu", "Culoare punct personalizată"},
    {"Dot Color", "ドット色", "Punktfarbe", "Color del punto", "Couleur du point", "Colore punto", "Puntkleur", "Cor do ponto", "Цвет точки", "点颜色", "점 색상", "لون النقطة", "Warna titik", "Колір точки", "Χρώμα σημείου", "Punktfärg", "สีจุด", "Barva bodu", "Punktfarve", "Nokta rengi", "Punktfarge", "Pont színe", "Pisteen väri", "Màu chấm", "Kolor punktu", "Culoare punct"},
    {"Dot Shape", "ドット形状", "Punktform", "Forma del punto", "Forme du point", "Forma punto", "Puntvorm", "Forma do ponto", "Форма точки", "圆点形状", "점 모양", "شكل النقطة", "Bentuk titik", "Форма точки", "Σχήμα κουκκίδας", "Punktform", "รูปทรงจุด", "Tvar bodu", "Punktform", "Nokta şekli", "Punktform", "Pont alakja", "Pisteen muoto", "Hình dạng chấm", "Kształt kropki", "Formă punct"},
    {"Scope Dot Shape", "スコープドット形状", "Visierpunkt-Form", "Forma del punto del visor", "Forme du point de visée", "Forma punto mirino", "Vizierpuntvorm", "Forma do ponto da mira", "Форма точки прицела", "瞄准点形状", "스코프 점 모양", "شكل نقطة المنظار", "Bentuk titik scope", "Форма точки прицілу", "Σχήμα σημείου σκόπευτήρου", "Siktprickform", "รูปทรงจุดเล็ง", "Tvar bodu zaměřovače", "Sigtepunktform", "Nişangah noktası şekli", "Siktepunktform", "Irányzékpont alakja", "Tähtäinpisteen muoto", "Hình dạng chấm ống ngắm", "Kształt punktu celownika", "Formă punct lunetă"},
    {"Square", "四角", "Quadrat", "Cuadrado", "Carré", "Quadrato", "Vierkant", "Quadrado", "Квадрат", "方形", "사각형", "مربع", "Persegi", "Квадрат", "Τετράγωνο", "Fyrkant", "สี่เหลี่ยม", "Čtverec", "Firkant", "Kare", "Firkant", "Négyzet", "Neliö", "Vuông", "Kwadrat", "Pătrat"},
    {"Circle", "ä¸¸", "Kreis", "Círculo", "Cercle", "Cerchio", "Cirkel", "Círculo", "Круг", "圆形", "원", "دائرة", "Lingkaran", "Коло", "Κύκλος", "Cirkel", "วงกลม", "Kruh", "Cirkel", "Daire", "Sirkel", "Kör", "Ympyrä", "Hình tròn", "Koło", "Cerc"},
    {"Dot Opacity", "ドット不透明度", "Punktdeckkraft", "Opacidad del punto", "Opacité du point", "Opacità punto", "Puntdekking", "Opacidade do ponto", "Непрозрачность точки", "点不透明度", "점 불투명도", "شفافية النقطة", "Opasitas titik", "Непрозорість точки", "Αδιαφάνεια σημείου", "Punktopacitet", "ความทึบจุด", "Neprůhlednost bodu", "Punktopacitet", "Nokta opaklığı", "Punktopasitet", "Pont átlátszatlansága", "Pisteen läpinäkymättömyys", "Độ mờ chấm", "Krycie punktu", "Opacitate punct"},
    {"Dot Thickness", "ドット太さ", "Punktdicke", "Grosor del punto", "Épaisseur du point", "Spessore punto", "Puntdikte", "Espessura do ponto", "Толщина точки", "圆点粗细", "점 두께", "سُمك النقطة", "Ketebalan titik", "Товщина точки", "Πάχος κουκκίδας", "Punktjocklek", "ความหนาจุด", "Tloušťka bodu", "Punkttykkelse", "Nokta kalınlığı", "Punkttykkelse", "Pont vastagság", "Pisteen paksuus", "Độ dày chấm", "Grubość kropki", "Grosime punct"},
    {"Dot Thick.", "ドット太さ", "Punktstärke", "Grosor del punto", "Épaiss. point", "Spess. punto", "Puntdikte", "Espess. ponto", "Толщ. точки", "点粗细", "점 두께", "سُمك النقطة", "Ketebalan titik", "Товщина точки", "Πάχος σημείου", "Punktjocklek", "ความหนาจุด", "Tloušťka bodu", "Punktykkelse", "Nokta kalınlığı", "Punktykkelse", "Pont vastagsága", "Pisteen paksuus", "Độ dày chấm", "Grubość punktu", "Grosime punct"},
    {"T-Style", "Tスタイル", "T-Stil", "Estilo T", "Style T", "Stile T", "T-stijl", "Estilo T", "Стиль T", "T 型", "T 스타일", "نمط T", "Gaya T", "Стиль T", "Στυλ T", "T-stil", "สไตล์ T", "T-styl", "T-stil", "T-Stili", "T-stil", "T-stílus", "T-tyyli", "Kiểu T", "Styl T", "Stil T"},
    {"Inner", "内側", "Innen", "Interior", "Intérieur", "Interno", "Binnen", "Interior", "Внутренний", "内侧", "내부", "داخلي", "Dalam", "Внутрішній", "Εσωτερικό", "Inre", "ด้านใน", "Vnitřní", "Indre", "İç", "Indre", "Belső", "Sisä", "Bên trong", "Wewnętrzny", "Interior"},
    {"Outer", "外側", "Außen", "Exterior", "Extérieur", "Esterno", "Buiten", "Exterior", "Внешний", "外侧", "외측", "خارجي", "Luar", "Зовнішній", "Εξωτερικό", "Yttre", "ด้านนอก", "Vnější", "Ydre", "Dış", "Ytre", "Külső", "Ulkopuoli", "Ngoài", "Zewnętrzny", "Exterior"},
    {"Inner Lines", "内側ライン", "Innere Linien", "Líneas interiores", "Lignes intérieures", "Linee interne", "Binnenlijnen", "Linhas internas", "Внутренние линии", "内部线条", "내부 선", "خطوط داخلية", "Garis dalam", "Внутрішні лінії", "Εσωτερικές γραμμές", "Inre linjer", "เส้นภายใน", "Vnitřní čáry", "Indre linjer", "İç çizgiler", "Indre linjer", "Belső vonalak", "Sisäviivat", "Đường bên trong", "Linie wewnętrzne", "Linii interioare"},
    {"Outer Lines", "外側ライン", "Außenlinien", "Líneas exteriores", "Lignes extérieures", "Linee esterne", "Buitenlijnen", "Linhas externas", "Внешние линии", "外侧线条", "외곽선", "الخطوط الخارجية", "Garis luar", "Зовнішні лінії", "Εξωτερικές γραμμές", "Ytterlinjer", "เส้นนอก", "Vnější čáry", "Ydre linjer", "Dış çizgiler", "Ytre linjer", "Külső vonalak", "Ulommat viivat", "Đường ngoài", "Linie zewnętrzne", "Linii exterioare"},
    {"Show", "表示", "Anzeigen", "Mostrar", "Afficher", "Mostra", "Tonen", "Mostrar", "Показать", "显示", "표시", "إظهار", "Tampilkan", "Показати", "Εμφάνιση", "Visa", "แสดง", "Zobrazit", "Vis", "Göster", "Vis", "Megjelenítés", "Näytä", "Hiện", "Pokaż", "Afișează"},
    {"Show Text", "文字を表示", "Text anzeigen", "Mostrar texto", "Afficher le texte", "Mostra testo", "Tekst tonen", "Mostrar texto", "Показать текст", "显示文字", "텍스트 표시", "إظهار النص", "Tampilkan Teks", "Показати текст", "Εμφάνιση κειμένου", "Visa text", "แสดงข้อความ", "Zobrazit text", "Vis tekst", "Metni göster", "Vis tekst", "Szöveg megjelenítése", "Näytä teksti", "Hiển thị chữ", "Pokaż tekst", "Afișează text"},
    {"Show Number", "数値を表示", "Zahl anzeigen", "Mostrar número", "Afficher le nombre", "Mostra numero", "Nummer tonen", "Mostrar número", "Показать число", "显示数值", "숫자 표시", "إظهار الرقم", "Tampilkan angka", "Показати число", "Εμφάνιση αριθμού", "Visa nummer", "แสดงตัวเลข", "Zobrazit číslo", "Vis nummer", "Sayıyı göster", "Vis nummer", "Szám megjelenítése", "Näytä numero", "Hiển thị số", "Pokaż liczbę", "Afișează numărul"},
    {"Enable", "有効化", "Aktivieren", "Activar", "Activer", "Abilita", "Inschakelen", "Ativar", "Включить", "启用", "활성화", "تفعيل", "Aktifkan", "Увімкнути", "Ενεργοποίηση", "Aktivera", "เปิดใช้", "Povolit", "Aktivér", "Etkinleştir", "Aktiver", "Engedélyezés", "Ota käyttöön", "Bật", "Włącz", "Activează"},
    {"Enable (Override All)", "有効化 (全体上書き)", "Aktivieren (Alles überschreiben)", "Activar (Anular todo)", "Activer (Remplacer tout)", "Abilita (Sostituisci tutto)", "Inschakelen (Alles overschrijven)", "Ativar (Substituir tudo)", "Включить (Переопределить всё)", "启用（覆盖全部）", "활성화 (전체 재정의)", "تفعيل (تجاوز الكل)", "Aktifkan (Ganti semua)", "Увімкнути (Перевизначити все)", "Ενεργοποίηση (Αντικατάσταση όλων)", "Aktivera (Ersätt alla)", "เปิดใช้ (แทนที่ทั้งหมด)", "Povolit (Přepsat vše)", "Aktiver (Tilsidesæt alt)", "Etkinleştir (Tümünü geçersiz kıl)", "Aktiver (Overstyr alt)", "Engedélyezés (Mindent felülír)", "Ota käyttöön (Korvaa kaikki)", "Bật (Ghi đè tất cả)", "Włącz (Zastąp wszystko)", "Activează (Înlocuiește tot)"},
    {"Opacity", "不透明度", "Deckkraft", "Opacidad", "Opacité", "Opacità", "Dekking", "Opacidade", "Непрозрачность", "不透明度", "불투명도", "الشفافية", "Opasitas", "Непрозорість", "Αδιαφάνεια", "Opacitet", "ความทึบ", "Neprůhlednost", "Uigennemsigtighed", "Opaklık", "Opasitet", "Átlátszatlanság", "Peittävyys", "Độ mờ", "Krycie", "Opacitate"},
    {"Length", "長さ", "Länge", "Longitud", "Longueur", "Lunghezza", "Lengte", "Comprimento", "Длина", "长度", "길이", "الطول", "Panjang", "Довжина", "Μήκος", "Längd", "ความยาว", "Délka", "Længde", "Uzunluk", "Lengde", "Hossz", "Pituus", "Chiều dài", "Długość", "Lungime"},
    {"Length X", "長さ X", "Länge X", "Longitud X", "Longueur X", "Lunghezza X", "Lengte X", "Comprimento X", "Длина X", "长度 X", "길이 X", "الطول X", "Panjang X", "Довжина X", "Μήκος X", "Längd X", "ความยาว X", "Délka X", "Længde X", "Uzunluk X", "Lengde X", "Hossz X", "Pituus X", "Chiều dài X", "Długość X", "Lungime X"},
    {"Length Y", "長さ Y", "Länge Y", "Longitud Y", "Longueur Y", "Lunghezza Y", "Lengte Y", "Comprimento Y", "Длина Y", "长度 Y", "길이 Y", "الطول Y", "Panjang Y", "Довжина Y", "Μήκος Y", "Längd Y", "ความยาว Y", "Délka Y", "Længde Y", "Uzunluk Y", "Lengde Y", "Hossz Y", "Pituus Y", "Chiều dài Y", "Długość Y", "Lungime Y"},
    {"Link XY", "XYを連動", "XY verknüpfen", "Vincular XY", "Lier XY", "Collega XY", "XY koppelen", "Vincular XY", "Связать XY", "联动 XY", "XY 연동", "ربط XY", "Tautkan XY", "Зв'язати XY", "Σύνδεση XY", "Länka XY", "เชื่อม XY", "Propojit XY", "Link XY", "XY bağla", "Koble XY", "XY összekapcsolása", "Linkitä XY", "Liên kết XY", "Połącz XY", "Leagă XY"},
    {"Thickness", "太さ", "Dicke", "Grosor", "Épaisseur", "Spessore", "Dikte", "Espessura", "Толщина", "粗细", "두께", "السُمك", "Ketebalan", "Товщина", "Πάχος", "Tjocklek", "ความหนา", "Tloušťka", "Tykkelse", "Kalınlık", "Tykkelse", "Vastagság", "Paksuus", "Độ dày", "Grubość", "Grosime"},
    {"Offset", "オフセット", "Versatz", "Desplazamiento", "Décalage", "Offset", "Offset", "Deslocamento", "Смещение", "偏移", "오프셋", "إزاحة", "Offset", "Зміщення", "Μετατόπιση", "Offset", "ออฟเซ็ต", "Posun", "Forskydning", "Ofset", "Forskyvning", "Eltolás", "Siirtymä", "Offset", "Przesunięcie", "Offset"},
    {"Offset X", "オフセット X", "Versatz X", "Desplazamiento X", "Décalage X", "Offset X", "Offset X", "Deslocamento X", "Смещение X", "偏移 X", "오프셋 X", "إزاحة X", "Offset X", "Зміщення X", "Μετατόπιση X", "Offset X", "ออฟเซ็ต X", "Odsazení X", "Forskydning X", "Ofset X", "Offset X", "Eltolás X", "Siirtymä X", "Offset X", "Przesunięcie X", "Offset X"},
    {"Offset Y", "オフセット Y", "Versatz Y", "Desplazamiento Y", "Décalage Y", "Offset Y", "Verschuiving Y", "Deslocamento Y", "Смещение Y", "偏移 Y", "오프셋 Y", "إزاحة Y", "Offset Y", "Зміщення Y", "Μετατόπιση Y", "Förskjutning Y", "ออฟเซ็ต Y", "Posun Y", "Forskydning Y", "Ofset Y", "Forskyvning Y", "Eltolás Y", "Siirtymä Y", "Offset Y", "Przesunięcie Y", "Offset Y"},
    {"Anchor", "基準位置", "Anker", "Ancla", "Ancrage", "Ancoraggio", "Anker", "Âncora", "Привязка", "锚点", "기준점", "مرساة", "Jangkar", "Прив'язка", "Άγκυρα", "Ankare", "จุดยึด", "Kotva", "Anker", "Çapa", "Anker", "Horgony", "Ankkuri", "Neo", "Kotwica", "Ancoră"},
    {"Top Left", "左上", "Oben links", "Arriba a la izquierda", "En haut à gauche", "In alto a sinistra", "Linksboven", "Superior esquerdo", "Вверху слева", "左上", "왼쪽 위", "أعلى يسار", "Kiri Atas", "Зверху ліворуч", "Πάνω αριστερά", "Övre vänster", "บนซ้าย", "Vlevo nahoře", "Øverst til venstre", "Sol üst", "Øverst til venstre", "Bal felső", "Ylhäällä vasemmalla", "Trên trái", "Góra lewo", "Sus stânga"},
    {"Top Center", "上中央", "Oben Mitte", "Arriba centro", "Haut centre", "In alto al centro", "Boven midden", "Superior central", "Верх по центру", "上中", "상단 중앙", "أعلى الوسط", "Tengah Atas", "Верх по центру", "Πάνω κέντρο", "Överst i mitten", "กลางบน", "Nahoře uprostřed", "Top midt", "Üst orta", "Topp midt", "Felső közép", "Yläkeskellä", "Giữa trên", "Góra środek", "Sus centru"},
    {"Top Right", "右上", "Oben rechts", "Arriba a la derecha", "En haut à droite", "In alto a destra", "Rechtsboven", "Superior direito", "Вверху справа", "右上", "오른쪽 위", "أعلى اليمين", "Kanan atas", "Вгорі справа", "Πάνω δεξιά", "Övre höger", "บนขวา", "Vpravo nahoře", "Øverst til højre", "Sağ üst", "Øverst til høyre", "Jobb felső", "Yläoikea", "Trên phải", "Góra prawo", "Sus dreapta"},
    {"Middle Left", "左中央", "Mitte links", "Centro izquierda", "Milieu gauche", "Centro sinistra", "Midden links", "Centro esquerda", "Середина слева", "左中", "왼쪽 중앙", "وسط يسار", "Tengah kiri", "Середина зліва", "Μέση αριστερά", "Mitten vänster", "กลางซ้าย", "Střed vlevo", "Midt venstre", "Orta sol", "Midt venstre", "Bal közép", "Keskellä vasemmalla", "Giữa trái", "Środek lewo", "Mijloc stânga"},
    {"Middle Center", "中央", "Mitte Mitte", "Centro central", "Centre central", "Centro centrale", "Midden midden", "Centro central", "Центр по центру", "正中", "중앙", "الوسط المركزي", "Tengah tengah", "Центр по центру", "Κέντρο κέντρο", "Mitten mitten", "กลางกลาง", "Střed střed", "Midt midt", "Orta orta", "Midt midt", "Közép közép", "Keskellä keskellä", "Giữa giữa", "Środek środek", "Centru centru"},
    {"Middle Right", "右中央", "Mitte rechts", "Centro derecha", "Milieu droite", "Centro destra", "Midden rechts", "Centro direito", "Середина справа", "右中央", "오른쪽 중앙", "وسط يمين", "Kanan Tengah", "Посередині праворуч", "Μέση δεξιά", "Mitten höger", "กลางขวา", "Uprostřed vpravo", "Midt til højre", "Orta sağ", "Midt til høyre", "Közép jobb", "Keskellä oikealla", "Giữa phải", "Środek prawo", "Mijloc dreapta"},
    {"Bottom Left", "左下", "Unten links", "Abajo izquierda", "Bas gauche", "In basso a sinistra", "Linksonder", "Inferior esquerda", "Внизу слева", "左下", "왼쪽 하단", "أسفل اليسار", "Kiri Bawah", "Внизу зліва", "Κάτω αριστερά", "Nederst till vänster", "ซ้ายล่าง", "Dole vlevo", "Nederst til venstre", "Sol alt", "Nederst til venstre", "Bal alsó", "Alhaalla vasemmalla", "Dưới trái", "Dół lewo", "Jos stânga"},
    {"Bottom Center", "下中央", "Unten Mitte", "Abajo al centro", "En bas au centre", "In basso al centro", "Onder midden", "Inferior central", "Внизу по центру", "下中", "아래 중앙", "أسفل الوسط", "Tengah bawah", "Внизу по центру", "Κάτω κέντρο", "Nedre mitten", "ล่างกลาง", "Dole uprostřed", "Nederst i midten", "Alt orta", "Nederst i midten", "Alsó közép", "Alhaalla keskellä", "Dưới giữa", "Dół środek", "Jos centru"},
    {"Bottom Right", "右下", "Unten rechts", "Abajo a la derecha", "Bas droite", "In basso a destra", "Rechtsonder", "Inferior direita", "Внизу справа", "右下", "오른쪽 하단", "أسفل يمين", "Kanan bawah", "Внизу справа", "Κάτω δεξιά", "Nederst höger", "ล่างขวา", "Dole vpravo", "Nederst højre", "Sağ alt", "Nederst høyre", "Jobb alsó", "Alhaalla oikealla", "Dưới phải", "Dół prawo", "Jos dreapta"},
    {"Mid Left", "左中央", "Mitte links", "Centro izquierda", "Centre gauche", "Centro sinistra", "Midden links", "Centro esquerda", "Центр слева", "左中", "왼쪽 중앙", "يسار الوسط", "Kiri tengah", "Центр зліва", "Κέντρο αριστερά", "Mitten vänster", "กลางซ้าย", "Střed vlevo", "Midt venstre", "Orta sol", "Midt venstre", "Közép bal", "Keskellä vasemmalla", "Giữa trái", "Środek lewo", "Centru stânga"},
    {"Mid Center", "中央", "Mitte zentriert", "Centro", "Centre", "Centro", "Midden", "Centro", "Центр", "中央", "중앙", "وسط", "Tengah", "По центру", "Κέντρο", "Mitten centrerad", "กลาง", "Uprostřed", "Midt centreret", "Orta merkez", "Midt sentrert", "Közép", "Keskellä", "Giữa", "Środek", "Centru"},
    {"Mid Right", "右中央", "Mitte rechts", "Centro derecha", "Milieu droite", "Centro destra", "Midden rechts", "Centro direita", "По центру справа", "右中", "오른쪽 중앙", "وسط اليمين", "Tengah Kanan", "По центру справа", "Μέση δεξιά", "Mitten till höger", "กลางขวา", "Uprostřed vpravo", "Midt til højre", "Orta sağ", "Midt til høyre", "Közép jobb", "Keskellä oikealla", "Giữa phải", "Środek prawo", "Mijloc dreapta"},
    {"Bot Left", "左下", "Unten links", "Abajo a la izquierda", "En bas à gauche", "In basso a sinistra", "Linksonder", "Inferior esquerdo", "Внизу слева", "左下", "왼쪽 아래", "أسفل اليسار", "Kiri bawah", "Внизу зліва", "Κάτω αριστερά", "Nedre vänster", "ล่างซ้าย", "Dole vlevo", "Nederst til venstre", "Sol alt", "Nederst til venstre", "Bal alsó", "Alhaalla vasemmalla", "Dưới trái", "Dół lewo", "Jos stânga"},
    {"Bot Center", "下中央", "Unten Mitte", "Abajo centro", "Bas centre", "Centro inferiore", "Onder midden", "Centro inferior", "Низ по центру", "下中", "하단 중앙", "وسط أسفل", "Tengah bawah", "Низ по центру", "Κάτω κέντρο", "Nederst mitten", "กลางล่าง", "Dole uprostřed", "Nederst midt", "Alt orta", "Nederst midt", "Alsó közép", "Alhaalla keskellä", "Giữa dưới", "Dół środek", "Jos centru"},
    {"Bot Right", "右下", "Unten rechts", "Abajo derecha", "Bas droite", "In basso a destra", "Onder rechts", "Inferior direita", "Внизу справа", "右下", "오른쪽 아래", "أسفل يمين", "Kanan bawah", "Внизу справа", "Κάτω δεξιά", "Nere höger", "ล่างขวา", "Dole vpravo", "Nede højre", "Alt sağ", "Nede høyre", "Lent jobbra", "Alhaalla oikealla", "Dưới phải", "Dół prawo", "Jos dreapta"},
    {"Left", "左", "Links", "Izquierda", "Gauche", "Sinistra", "Links", "Esquerda", "Слева", "左", "왼쪽", "يسار", "Kiri", "Ліворуч", "Αριστερά", "Vänster", "ซ้าย", "Vlevo", "Venstre", "Sol", "Venstre", "Bal", "Vasen", "Trái", "Lewo", "Stânga"},
    {"Center", "中央", "Mitte", "Centro", "Centre", "Centro", "Midden", "Centro", "По центру", "中央", "중앙", "الوسط", "Tengah", "По центру", "Κέντρο", "Centrum", "กลาง", "Střed", "Center", "Merkez", "Senter", "Közép", "Keskellä", "Giữa", "Środek", "Centru"},
    {"Right", "右", "Rechts", "Derecha", "Droite", "Destra", "Rechts", "Direita", "Справа", "右", "오른쪽", "يمين", "Kanan", "Праворуч", "Δεξιά", "Höger", "ขวา", "Vpravo", "Højre", "Sağ", "Høyre", "Jobb", "Oikea", "Phải", "Prawo", "Dreapta"},
    {"Top", "上", "Oben", "Arriba", "Haut", "Alto", "Boven", "Superior", "Верх", "上", "상단", "أعلى", "Atas", "Верх", "Πάνω", "Topp", "บน", "Nahoře", "Top", "Üst", "Topp", "Felső", "Ylä", "Trên", "Góra", "Sus"},
    {"Bottom", "ä¸", "Unten", "Abajo", "Bas", "Inferiore", "Onder", "Inferior", "Низ", "底部", "하단", "الأسفل", "Bawah", "Низ", "Κάτω", "Nederst", "ล่าง", "Dole", "Nederst", "Alt", "Nederst", "Alsó", "Ala", "Dưới", "Dół", "Jos"},
    {"Above", "ä¸", "Oben", "Arriba", "Au-dessus", "Sopra", "Boven", "Acima", "Сверху", "上", "위", "أعلى", "Di Atas", "Зверху", "Πάνω", "Ovan", "ด้านบน", "Nad", "Over", "Üstte", "Over", "Fölötte", "Yläpuolella", "Phía trên", "Powyżej", "Deasupra"},
    {"Below", "ä¸", "Darunter", "Debajo", "En dessous", "Sotto", "Onder", "Abaixo", "Ниже", "下方", "아래", "أسفل", "Di Bawah", "Нижче", "Κάτω", "Nedan", "ด้านล่าง", "Dole", "Nedenfor", "Altında", "Nedenfor", "Alatta", "Alapuolella", "Bên dưới", "Poniżej", "Dedesubt"},
    {"Start", "開始", "Start", "Inicio", "Début", "Inizio", "Start", "Início", "Начало", "开始", "시작", "بداية", "Mulai", "Початок", "Έναρξη", "Start", "เริ่ม", "Začátek", "Start", "Başlangıç", "Start", "Kezdet", "Alku", "Bắt đầu", "Początek", "Început"},
    {"End", "終端", "Ende", "Final", "Fin", "Fine", "Einde", "Fim", "Конец", "末端", "끝", "نهاية", "Akhir", "Кінець", "Τέλος", "Slut", "ปลาย", "Konec", "Slut", "Son", "Slutt", "Vég", "Loppu", "Cuối", "Koniec", "Sfârșit"},
    {"Horiz", "横", "Horiz.", "Horiz.", "Horiz.", "Oriz.", "Horiz.", "Horiz.", "Гориз.", "横", "가로", "أفقي", "Horiz", "Гориз.", "Οριζ.", "Horiz", "แนวนอน", "Horiz", "Horiz", "Yatay", "Horiz", "Vízsz.", "Vaaka", "Ngang", "Poz.", "Oriz."},
    {"Vert", "縦", "Vert.", "Vert.", "Vert.", "Vert.", "Vert.", "Vert.", "Верт.", "纵", "세로", "عمودي", "Vert.", "Верт.", "Καθ.", "Vert.", "แนวตั้ง", "Vert.", "Vert.", "Dikey", "Vert.", "Függ.", "Pysty", "Dọc", "Pion.", "Vert."},
    {"Relative", "相対", "Relativ", "Relativo", "Relatif", "Relativo", "Relatief", "Relativo", "Относительный", "相对", "상대", "نسبي", "Relatif", "Відносний", "Σχετικό", "Relativ", "สัมพัทธ์", "Relativní", "Relativ", "Göreceli", "Relativ", "Relatív", "Suhteellinen", "Tương đối", "Względny", "Relativ"},
    {"Relative to Text", "テキスト基準", "Relativ zum Text", "Relativo al texto", "Relatif au texte", "Relativo al testo", "Relatief ten opzichte van tekst", "Relativo ao texto", "Относительно текста", "相对于文字", "텍스트 기준", "نسبة إلى النص", "Relatif ke teks", "Відносно тексту", "Σχετικά με κείμενο", "Relativt till text", "เทียบกับข้อความ", "Relativně k textu", "Relativt til tekst", "Metne göre", "Relativt til tekst", "Szöveghez viszonyítva", "Suhteessa tekstiin", "Tương đối văn bản", "Względem tekstu", "Relativ la text"},
    {"Independent", "独立", "Unabhängig", "Independiente", "Indépendant", "Indipendente", "Onafhankelijk", "Independente", "Независимо", "独立", "독립", "مستقل", "Independen", "Незалежний", "Ανεξάρτητο", "Oberoende", "อิสระ", "Nezávislé", "Uafhængig", "Bağımsız", "Uavhengig", "Független", "Riippumaton", "Độc lập", "Niezależny", "Independent"},
    {"Gauge→Text", "ゲージ→文字", "Anzeige→Text", "Indicador→Texto", "Jauge→Texte", "Indicatore→Testo", "Meter→Tekst", "Medidor→Texto", "Индикатор→Текст", "指示条→文字", "게이지→텍스트", "مؤشر→نص", "Gauge→Teks", "Індикатор→Текст", "Μετρητής→Κείμενο", "Mätare→Text", "Gauge→ข้อความ", "Ukazatel→Text", "Måler→Tekst", "Gösterge→Metin", "Måler→Tekst", "Mérő→Szöveg", "Mittari→Teksti", "Gauge→Chữ", "Wskaźnik→Tekst", "Indicator→Text"},
    {"Text → Gauge", "文字→ゲージ", "Text → Anzeige", "Texto → Medidor", "Texte → Jauge", "Testo → Indicatore", "Tekst → Meter", "Texto → Medidor", "Текст → Шкала", "文字 → 计量条", "텍스트 → 게이지", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige", "Text → Anzeige"},
    {"Position Mode", "位置モード", "Positionsmodus", "Modo de posición", "Mode de position", "Modalità posizione", "Positiemodus", "Modo de posição", "Режим позиции", "位置模式", "위치 모드", "وضع الموضع", "Mode Posisi", "Режим позиції", "Λειτουργία θέσης", "Positionsläge", "โหมดตำแหน่ง", "Režim pozice", "Positionstilstand", "Konum modu", "Posisjonsmodus", "Pozíció mód", "Sijaintitila", "Chế độ vị trí", "Tryb pozycji", "Mod poziție"},
    {"Pos Mode", "位置モード", "Positionsmodus", "Modo de posición", "Mode de position", "Modo posizione", "Positiemodus", "Modo de posição", "Режим позиции", "位置模式", "위치 모드", "وضع الموضع", "Mode Posisi", "Режим позиції", "Λειτουργία θέσης", "Positionsläge", "โหมดตำแหน่ง", "Režim pozice", "Positionstilstand", "Konum modu", "Posisjonsmodus", "Pozíció mód", "Sijaintitila", "Chế độ vị trí", "Tryb pozycji", "Mod poziție"},
    {"Gauge Side", "ゲージ側", "Anzeigeseite", "Lado del medidor", "Côté de la jauge", "Lato indicatore", "Meterzijde", "Lado do medidor", "Сторона индикатора", "量表侧", "게이지 측면", "جانب المقياس", "Sisi gauge", "Сторона індикатора", "Πλευρά ένδειξης", "Mätarsida", "ด้านเกจ", "Strana ukazatele", "Målerside", "Gösterge tarafı", "Målerside", "Mérő oldala", "Mittarin puoli", "Cạnh đồng hồ", "Strona wskaźnika", "Latura indicatorului"},
    {"Text Side", "文字側", "Textseite", "Lado del texto", "Côté texte", "Lato testo", "Tekstzijde", "Lado do texto", "Сторона текста", "文字侧", "텍스트 쪽", "جانب النص", "Sisi teks", "Сторона тексту", "Πλευρά κειμένου", "Textsida", "ด้านข้อความ", "Strana textu", "Tekstside", "Metin tarafı", "Tekstside", "Szöveg oldal", "Tekstin puoli", "Cạnh chữ", "Strona tekstu", "Latura textului"},
    {"Gauge Anchor", "ゲージ基準", "Anzeigen-Anker", "Ancla del medidor", "Ancrage jauge", "Ancoraggio indicatore", "Meteranker", "Âncora do medidor", "Привязка шкалы", "计量条锚点", "게이지 기준점", "مرساة المقياس", "Jangkar pengukur", "Прив'язка шкали", "Άγκυρα μετρητή", "Mätare-ankare", "จุดยึดเกจ", "Kotva ukazatele", "Måleranker", "Gösterge çapası", "Måleranker", "Mérő horgony", "Mittarin ankkuri", "Neo thanh đo", "Kotwica wskaźnika", "Ancoră indicator"},
    {"Gauge X", "ゲージ X", "Anzeige X", "Medidor X", "Jauge X", "Indicatore X", "Meter X", "Medidor X", "Индикатор X", "计量条 X", "게이지 X", "مقياس X", "Gauge X", "Шкала X", "Μέτρηση X", "Mätare X", "Gauge X", "Měřidlo X", "Måler X", "Gösterge X", "Måler X", "Mérő X", "Mittari X", "Đồng hồ X", "Wskaźnik X", "Indicator X"},
    {"Gauge Y", "ゲージ Y", "Anzeige Y", "Indicador Y", "Jauge Y", "Indicatore Y", "Meter Y", "Medidor Y", "Индикатор Y", "指示条 Y", "게이지 Y", "مؤشر Y", "Gauge Y", "Індикатор Y", "Μετρητής Y", "Mätare Y", "Gauge Y", "Ukazatel Y", "Måler Y", "Gösterge Y", "Måler Y", "Mérő Y", "Mittari Y", "Gauge Y", "Wskaźnik Y", "Indicator Y"},
    {"Text Offset X", "文字オフセット X", "Textversatz X", "Desplazamiento del texto X", "Décalage texte X", "Offset testo X", "Tekstoffset X", "Deslocamento do texto X", "Смещение текста X", "文字偏移 X", "텍스트 오프셋 X", "إزاحة النص X", "Offset teks X", "Зміщення тексту X", "Μετατόπιση κειμένου X", "Textoffset X", "ออฟเซ็ตข้อความ X", "Odsazení textu X", "Tekstforskydning X", "Metin ofseti X", "Tekstoffset X", "Szöveg eltolás X", "Tekstin siirtymä X", "Offset chữ X", "Przesunięcie tekstu X", "Offset text X"},
    {"Text Offset Y", "文字オフセット Y", "Textversatz Y", "Desplazamiento Y del texto", "Décalage Y du texte", "Offset Y testo", "Tekstverschuiving Y", "Deslocamento Y do texto", "Смещение Y текста", "文字偏移 Y", "텍스트 오프셋 Y", "إزاحة Y للنص", "Offset Y teks", "Зміщення Y тексту", "Μετατόπιση Y κειμένου", "Textförskjutning Y", "ออฟเซ็ต Y ข้อความ", "Posun Y textu", "Tekstforskydning Y", "Metin ofset Y", "Tekstforskyvning Y", "Szöveg eltolás Y", "Tekstin siirtymä Y", "Offset Y văn bản", "Przesunięcie Y tekstu", "Offset Y text"},
    {"Text Ofs X", "文字Ofs X", "Text-Ofs X", "Despl. texto X", "Décal. texte X", "Ofs testo X", "Tekst-ofs X", "Desloc. texto X", "Смещ. текста X", "文字偏移 X", "텍스트 Ofs X", "إزاحة النص X", "Ofs teks X", "Зміщ. тексту X", "Μετατόπιση κειμένου X", "Text-ofs X", "Ofs ข้อความ X", "Ofs textu X", "Tekst-ofs X", "Metin ofs X", "Tekst-ofs X", "Szöveg ofs X", "Tekstin ofs X", "Ofs văn bản X", "Ofs tekstu X", "Ofs text X"},
    {"Text Ofs Y", "文字Ofs Y", "Text-Ofs Y", "Ofs texto Y", "Ofs texte Y", "Ofs testo Y", "Tekst-ofs Y", "Ofs texto Y", "Смещ. текста Y", "文字Ofs Y", "텍스트 Ofs Y", "إزاحة النص Y", "Ofs Teks Y", "Зсув тексту Y", "Μετατόπ. κειμένου Y", "Textförskjutning Y", "Ofs ข้อความ Y", "Ofs textu Y", "Tekstforskydning Y", "Metin Ofset Y", "Tekstforskyvning Y", "Szöveg eltolás Y", "Tekstin siirtymä Y", "Ofs văn bản Y", "Ofs tekstu Y", "Ofs text Y"},
    {"Prefix", "接頭辞", "Präfix", "Prefijo", "Préfixe", "Prefisso", "Voorvoegsel", "Prefixo", "Префикс", "前缀", "접두사", "بادئة", "Awalan", "Префікс", "Πρόθεμα", "Prefix", "คำนำหน้า", "Předpona", "Præfiks", "Önek", "Prefiks", "Előtag", "Etuliite", "Tiền tố", "Prefiks", "Prefix"},
    {"Suffix", "接尾辞", "Suffix", "Sufijo", "Suffixe", "Suffisso", "Achtervoegsel", "Sufixo", "Суффикс", "后缀", "접미사", "لاحقة", "Akhiran", "Суфікс", "Επίθημα", "Suffix", "คำต่อท้าย", "Přípona", "Suffiks", "Sonek", "Suffiks", "Utótag", "Pääte", "Hậu tố", "Przyrostek", "Sufix"},
    {"Align", "整列", "Ausrichtung", "Alineación", "Alignement", "Allinea", "Uitlijnen", "Alinhar", "Выравнивание", "对齐", "정렬", "محاذاة", "Ratakan", "Вирівнювання", "Στοίχιση", "Justera", "จัดแนว", "Zarovnat", "Juster", "Hizala", "Juster", "Igazítás", "Tasaus", "Căn chỉnh", "Wyrównaj", "Aliniere"},
    {"Align X", "整列 X", "Ausrichtung X", "Alineación X", "Alignement X", "Allineamento X", "Uitlijning X", "Alinhamento X", "Выравнивание X", "对齐 X", "정렬 X", "محاذاة X", "Perataan X", "Вирівнювання X", "Στοίχιση X", "Justering X", "จัดแนว X", "Zarovnání X", "Justering X", "Hizalama X", "Justering X", "Igazítás X", "Tasaus X", "Căn X", "Wyrównanie X", "Aliniere X"},
    {"Align Y", "整列 Y", "Ausrichtung Y", "Alineación Y", "Alignement Y", "Allineamento Y", "Uitlijning Y", "Alinhamento Y", "Выравнивание Y", "对齐 Y", "정렬 Y", "محاذاة Y", "Perataan Y", "Вирівнювання Y", "Στοίχιση Y", "Justering Y", "จัดแนว Y", "Zarovnání Y", "Justering Y", "Hizalama Y", "Justering Y", "Igazítás Y", "Tasaus Y", "Căn Y", "Wyrównanie Y", "Aliniere Y"},
    {"Orientation", "向き", "Ausrichtung", "Orientación", "Orientation", "Orientamento", "Orientatie", "Orientação", "Ориентация", "方向", "방향", "الاتجاه", "Orientasi", "Орієнтація", "Προσανατολισμός", "Orientering", "การวางแนว", "Orientace", "Retning", "Yönelim", "Retning", "Tájolás", "Suunta", "Hướng", "Orientacja", "Orientare"},
    {"Orient", "向き", "Ausrichtung", "Orientación", "Orientation", "Orientamento", "Uitlijning", "Orientação", "Ориентация", "方向", "방향", "توجيه", "Orientasi", "Орієнтація", "Προσανατολισμός", "Orientering", "ทิศทาง", "Orientace", "Retning", "Yönelim", "Retning", "Tájolás", "Suunta", "Hướng", "Orientacja", "Orientare"},
    {"Width", "幅", "Breite", "Ancho", "Largeur", "Larghezza", "Breedte", "Largura", "Ширина", "宽度", "너비", "العرض", "Lebar", "Ширина", "Πλάτος", "Bredd", "ความกว้าง", "Šířka", "Bredde", "Genişlik", "Bredde", "Szélesség", "Leveys", "Chiều rộng", "Szerokość", "Lățime"},
    {"Height", "高さ", "Höhe", "Altura", "Hauteur", "Altezza", "Hoogte", "Altura", "Высота", "高度", "높이", "الارتفاع", "Tinggi", "Висота", "Ύψος", "Höjd", "ความสูง", "Výška", "Højde", "Yükseklik", "Høyde", "Magasság", "Korkeus", "Chiều cao", "Wysokość", "Înălțime"},
    {"Icon Height", "アイコン高さ", "Symbolhöhe", "Altura de icono", "Hauteur icône", "Altezza icona", "Pictogramhoogte", "Altura do ícone", "Высота значка", "图标高度", "아이콘 높이", "ارتفاع الأيقونة", "Tinggi Ikon", "Висота значка", "Ύψος εικονιδίου", "Ikonhöjd", "ความสูงไอคอน", "Výška ikony", "Ikonhøjde", "Simge yüksekliği", "Ikonhøyde", "Ikon magasság", "Kuvakkeen korkeus", "Chiều cao biểu tượng", "Wysokość ikony", "Înălțime pictogramă"},
    {"Icon Position", "アイコン位置", "Symbolposition", "Posición del icono", "Position de l'icône", "Posizione icona", "Pictogrampositie", "Posição do ícone", "Положение значка", "图标位置", "아이콘 위치", "موضع الأيقونة", "Posisi Ikon", "Положення значка", "Θέση εικονιδίου", "Ikonposition", "ตำแหน่งไอคอน", "Pozice ikony", "Ikonposition", "Simge konumu", "Ikonposisjon", "Ikon pozíció", "Kuvakkeen sijainti", "Vị trí biểu tượng", "Pozycja ikony", "Poziție pictogramă"},
    {"Pos Anchor", "位置基準", "Positionsanker", "Ancla de posición", "Ancre de position", "Ancora posizione", "Positieanker", "Âncora de posição", "Якорь позиции", "位置锚点", "위치 앵커", "مرساة الموضع", "Jangkar posisi", "Якір позиції", "Άγκυρα θέσης", "Positionsankare", "จุดยึดตำแหน่ง", "Kotva pozice", "Positionsanker", "Konum çapası", "Posisjonsanker", "Pozíció horgony", "Sijainnin ankkuri", "Neo vị trí", "Kotwica pozycji", "Ancoră poziție"},
    {"Pos X", "位置 X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Поз X", "位置 X", "위치 X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X", "Pos X"},
    {"Pos Y", "位置 Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Поз Y", "位置 Y", "Pos Y", "الموضع Y", "Pos Y", "Поз Y", "Θέση Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y", "Pos Y"},
    {"Layout", "レイアウト", "Layout", "Diseño", "Disposition", "Layout", "Lay-out", "Layout", "Макет", "布局", "레이아웃", "التخطيط", "Layout", "Макет", "Διάταξη", "Layout", "เลย์เอาต์", "Rozvržení", "Layout", "Düzen", "Oppsett", "Elrendezés", "Asettelu", "Bố cục", "Układ", "Layout"},
    {"Weapon Layout", "武器レイアウト", "Waffenlayout", "Diseño de armas", "Disposition des armes", "Layout armi", "Wapenlayout", "Layout de armas", "Расположение оружия", "武器布局", "무기 레이아웃", "تخطيط الأسلحة", "Tata letak senjata", "Розташування зброї", "Διάταξη όπλων", "Vapenlayout", "เลย์เอาต์อาวุธ", "Rozložení zbraní", "Våbenlayout", "Silah düzeni", "Våpenlayout", "Fegyver elrendezés", "Aseasettelu", "Bố cục vũ khí", "Układ broni", "Layout arme"},
    {"Standard", "標準", "Standard", "Estándar", "Standard", "Standard", "Standaard", "Padrão", "Стандарт", "标准", "표준", "قياسي", "Standar", "Стандарт", "Τυπικό", "Standard", "มาตรฐาน", "Standardní", "Standard", "Standart", "Standard", "Standard", "Vakio", "Tiêu chuẩn", "Standardowy", "Standard"},
    {"Alternative", "代替", "Alternative", "Alternativa", "Alternative", "Alternativa", "Alternatief", "Alternativa", "Альтернатива", "替代", "대체", "بديل", "Alternatif", "Альтернатива", "Εναλλακτικό", "Alternativ", "ทางเลือก", "Alternativa", "Alternativ", "Alternatif", "Alternativ", "Alternatíva", "Vaihtoehto", "Thay thế", "Alternatywa", "Alternativă"},
    {"Label Position", "ラベル位置", "Beschriftungsposition", "Posición de etiqueta", "Position étiquette", "Posizione etichetta", "Labelpositie", "Posição do rótulo", "Положение метки", "标签位置", "라벨 위치", "موضع التسمية", "Posisi Label", "Позиція мітки", "Θέση ετικέτας", "Etikettposition", "ตำแหน่งป้าย", "Pozice popisku", "Etiketposition", "Etiket konumu", "Etikettposisjon", "Címke pozíció", "Tunnisteen sijainti", "Vị trí nhãn", "Pozycja etykiety", "Poziție etichetă"},
    {"Label Pos", "ラベル位置", "Label-Position", "Posición de etiqueta", "Position de l'étiquette", "Pos. etichetta", "Labelpos.", "Pos. rótulo", "Поз. метки", "标签位置", "라벨 위치", "موضع التسمية", "Pos Label", "Поз. мітки", "Θέση ετικέτας", "Etikettpos.", "ตำแหน่งป้าย", "Poz. popisku", "Labelpos.", "Etiket kon.", "Etikettpos.", "Címke poz.", "Tunnisteen sij.", "Vị trí nhãn", "Poz. etykiety", "Poz. etichetă"},
    {"Label Offset X", "ラベルオフセット X", "Label-Versatz X", "Desplazamiento de etiqueta X", "Décalage étiquette X", "Offset etichetta X", "Labeloffset X", "Deslocamento do rótulo X", "Смещение метки X", "标签偏移 X", "라벨 오프셋 X", "إزاحة التسمية X", "Offset label X", "Зміщення мітки X", "Μετατόπιση ετικέτας X", "Etikettoffset X", "ออฟเซ็ตป้ายกำกับ X", "Odsazení popisku X", "Etiketforskydning X", "Etiket ofseti X", "Etikettoffset X", "Címke eltolás X", "Tunnisteen siirtymä X", "Offset nhãn X", "Przesunięcie etykiety X", "Offset etichetă X"},
    {"Label Offset Y", "ラベルオフセット Y", "Label-Versatz Y", "Desplazamiento Y de etiqueta", "Décalage Y de l'étiquette", "Offset Y etichetta", "Labelverschuiving Y", "Deslocamento Y do rótulo", "Смещение Y метки", "标签偏移 Y", "라벨 오프셋 Y", "إزاحة Y للتسمية", "Offset Y label", "Зміщення Y мітки", "Μετατόπιση Y ετικέτας", "Etikettförskjutning Y", "ออฟเซ็ต Y ป้าย", "Posun Y popisku", "Etiketforskydning Y", "Etiket ofset Y", "Etikettforskyvning Y", "Címke eltolás Y", "Tunnisteen siirtymä Y", "Offset Y nhãn", "Przesunięcie Y etykiety", "Offset Y etichetă"},
    {"Label Ofs X", "ラベルOfs X", "Label-Ofs X", "Despl. etiqueta X", "Décal. libellé X", "Ofs etichetta X", "Label-ofs X", "Desloc. rótulo X", "Смещ. метки X", "标签偏移 X", "라벨 Ofs X", "إزاحة التسمية X", "Ofs label X", "Зміщ. мітки X", "Μετατόπιση ετικέτας X", "Etikett-ofs X", "Ofs ป้ายกำกับ X", "Ofs popisku X", "Label-ofs X", "Etiket ofs X", "Etikett-ofs X", "Címke ofs X", "Tunnisteen ofs X", "Ofs nhãn X", "Ofs etykiety X", "Ofs etichetă X"},
    {"Label Ofs Y", "ラベルOfs Y", "Beschriftungs-Ofs Y", "Ofs etiqueta Y", "Ofs étiquette Y", "Ofs etichetta Y", "Label-ofs Y", "Ofs rótulo Y", "Смещ. метки Y", "标签Ofs Y", "라벨 Ofs Y", "إزاحة التسمية Y", "Ofs Label Y", "Зсув мітки Y", "Μετατόπ. ετικέτας Y", "Etikettförskjutning Y", "Ofs ป้าย Y", "Ofs popisku Y", "Etiketforskydning Y", "Etiket Ofset Y", "Etikettforskyvning Y", "Címke eltolás Y", "Tunnisteen siirtymä Y", "Ofs nhãn Y", "Ofs etykiety Y", "Ofs etichetă Y"},
    {"Label: Points", "ラベル: ポイント", "Label: Punkte", "Etiqueta: Puntos", "Étiquette : Points", "Etichetta: Punti", "Label: Punten", "Rótulo: Pontos", "Метка: Очки", "标签: 分数", "라벨: 점수", "تسمية: النقاط", "Label: Poin", "Мітка: Очки", "Ετικέτα: Πόντοι", "Etikett: Poäng", "ป้าย: คะแนน", "Popisek: Body", "Label: Point", "Etiket: Puan", "Etikett: Poeng", "Címke: Pontok", "Tunniste: Pisteet", "Nhãn: Điểm", "Etykieta: Punkty", "Etichetă: Puncte"},
    {"Label: Octoliths", "ラベル: オクトリス", "Label: Oktolithe", "Etiqueta: Octolitos", "Libellé : Octolites", "Etichetta: Octoliti", "Label: Octolieten", "Rótulo: Octolitos", "Метка: Октолиты", "标签：Octoliths", "라벨: 옥톨리스", "تسمية: Octoliths", "Label: Octoliths", "Мітка: Octoliths", "Ετικέτα: Octoliths", "Etikett: Octoliths", "ป้ายกำกับ: Octoliths", "Popisek: Octoliths", "Etiket: Octoliths", "Etiket: Octoliths", "Etikett: Octoliths", "Címke: Octoliths", "Tunniste: Octoliths", "Nhãn: Octoliths", "Etykieta: Octoliths", "Etichetă: Octoliths"},
    {"Label: Lives", "ラベル: ライフ", "Label: Leben", "Etiqueta: Vidas", "Libellé : Vies", "Etichetta: Vite", "Label: Levens", "Rótulo: Vidas", "Метка: Жизни", "标签：生命", "라벨: 목숨", "تسمية: الأرواح", "Label: Nyawa", "Мітка: Життя", "Ετικέτα: Ζωές", "Etikett: Liv", "ป้าย: ชีวิต", "Popisek: Životy", "Etiket: Liv", "Etiket: Can", "Etikett: Liv", "Címke: Életek", "Tunniste: Elämät", "Nhãn: Mạng", "Etykieta: Życia", "Etichetă: Vieți"},
    {"Label: Ring Time", "ラベル: リング時間", "Label: Ringzeit", "Etiqueta: Tiempo de anillo", "Libellé : Temps d'anneau", "Etichetta: Tempo anello", "Label: Ringtijd", "Rótulo: Tempo do anel", "Метка: Время кольца", "标签：环时间", "라벨: 링 시간", "تسمية: وقت الحلقة", "Label: Waktu cincin", "Мітка: Час кільця", "Ετικέτα: Χρόνος δακτυλίου", "Etikett: Ringtid", "ป้ายกำกับ: เวลาวงแหวน", "Popisek: Čas kruhu", "Etiket: Ringtid", "Etiket: Halka süresi", "Etikett: Ringtid", "Címke: Gyűrű idő", "Tunniste: Rengasaika", "Nhãn: Thời gian vòng", "Etykieta: Czas pierścienia", "Etichetă: Timp inel"},
    {"Label: Prime Time", "ラベル: プライム時間", "Beschriftung: Prime Time", "Etiqueta: Prime Time", "Étiquette : Prime Time", "Etichetta: Prime Time", "Label: Prime Time", "Rótulo: Prime Time", "Метка: Prime Time", "标签：Prime Time", "라벨: Prime Time", "تسمية: Prime Time", "Label: Prime Time", "Мітка: Prime Time", "Ετικέτα: Prime Time", "Etikett: Prime Time", "ป้าย: Prime Time", "Popisek: Prime Time", "Etiket: Prime Time", "Etiket: Prime Time", "Etikett: Prime Time", "Címke: Prime Time", "Tunniste: Prime Time", "Nhãn: Prime Time", "Etykieta: Prime Time", "Etichetă: Prime Time"},
    {"Battle", "バトル", "Kampf", "Batalla", "Combat", "Battaglia", "Gevecht", "Batalha", "Бой", "战斗", "배틀", "معركة", "Pertempuran", "Бій", "Μάχη", "Strid", "การต่อสู้", "Bitva", "Kamp", "Savaş", "Kamp", "Csata", "Taistelu", "Trận đấu", "Bitwa", "Luptă"},
    {"Bounty", "バウンティ", "Kopfgeld", "Recompensa", "Prime", "Taglia", "Bounty", "Recompensa", "Награда", "赏金", "바운티", "مكافأة", "Bounty", "Нагорода", "Αμοιβή", "Bounty", "Bounty", "Odměna", "Bounty", "Ödül", "Bounty", "Fejpénz", "Palkkio", "Bounty", "Nagroda", "Recompensă"},
    {"Survival", "サバイバル", "Survival", "Supervivencia", "Survie", "Sopravvivenza", "Overleving", "Sobrevivência", "Выживание", "生存", "서바이벌", "البقاء", "Survival", "Виживання", "Επιβίωση", "Överlevnad", "เอาชีวิตรอด", "Přežití", "Overlevelse", "Hayatta kalma", "Overlevelse", "Túlélés", "Selviytyminen", "Sinh tồn", "Przetrwanie", "Supraviețuire"},
    {"Defender", "ディフェンダー", "Verteidiger", "Defensor", "Défenseur", "Difensore", "Verdediger", "Defensor", "Защитник", "防守方", "디펜더", "مدافع", "Bertahan", "Захисник", "Υπερασπιστής", "Försvarare", "ป้องกัน", "Obránce", "Forsvarer", "Savunmacı", "Forsvarer", "Védő", "Puolustaja", "Phòng thủ", "Obrońca", "Apărător"},
    {"Prime", "プライム", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime", "Prime"},
    {"Label", "ラベル", "Label", "Etiqueta", "Étiquette", "Etichetta", "Label", "Rótulo", "Метка", "标签", "라벨", "تسمية", "Label", "Мітка", "Ετικέτα", "Etikett", "ป้าย", "Popisek", "Label", "Etiket", "Etikett", "Címke", "Tunniste", "Nhãn", "Etykieta", "Etichetă"},
    {"Value", "値", "Wert", "Valor", "Valeur", "Valore", "Waarde", "Valor", "Значение", "值", "값", "قيمة", "Nilai", "Значення", "Τιμή", "Värde", "ค่า", "Hodnota", "Værdi", "Değer", "Verdi", "Érték", "Arvo", "Giá trị", "Wartość", "Valoare"},
    {"Separator", "区切り", "Trennzeichen", "Separador", "Séparateur", "Separatore", "Scheidingsteken", "Separador", "Разделитель", "分隔符", "구분자", "فاصل", "Pemisah", "Роздільник", "Διαχωριστικό", "Avgränsare", "ตัวคั่น", "Oddělovač", "Separator", "Ayırıcı", "Separator", "Elválasztó", "Erotin", "Dấu phân cách", "Separator", "Separator"},
    {"Slash", "スラッシュ", "Schlag", "Corte", "Entaille", "Fendente", "Hak", "Corte", "Рубящий удар", "斩击", "슬래시", "ضربة", "Tebasan", "Удар", "Κόψιμο", "Hugg", "ฟัน", "Sek", "Hug", "Kesme", "Hugg", "Vágás", "Viilto", "Chém", "Cięcie", "Tăietură"},
    {"Goal", "目標", "Ziel", "Objetivo", "Objectif", "Obiettivo", "Doel", "Objetivo", "Цель", "目标", "목표", "الهدف", "Gol", "Мета", "Στόχος", "Mål", "เป้าหมาย", "Cíl", "Mål", "Hedef", "Mål", "Cél", "Tavoite", "Mục tiêu", "Cel", "Obiectiv"},
    {"Label Color", "ラベル色", "Labelfarbe", "Color de etiqueta", "Couleur de l'étiquette", "Colore etichetta", "Labelkleur", "Cor do rótulo", "Цвет метки", "标签颜色", "라벨 색상", "لون التسمية", "Warna Label", "Колір мітки", "Χρώμα ετικέτας", "Etikettfärg", "สีป้าย", "Barva popisku", "Labelfarve", "Etiket rengi", "Etikettfarge", "Címke szín", "Tunnisteen väri", "Màu nhãn", "Kolor etykiety", "Culoare etichetă"},
    {"Label Color: Overall", "ラベル色: 全体", "Labelfarbe: Gesamt", "Color de etiqueta: General", "Couleur étiquette : Global", "Colore etichetta: Generale", "Labelkleur: Totaal", "Cor do rótulo: Geral", "Цвет метки: Общий", "标签颜色：总体", "라벨 색상: 전체", "لون التسمية: إجمالي", "Warna label: Keseluruhan", "Колір мітки: Загальний", "Χρώμα ετικέτας: Συνολικό", "Etikettfärg: Totalt", "สีป้ายกำกับ: รวม", "Barva popisku: Celková", "Etiketfarve: Samlet", "Etiket rengi: Genel", "Etikettfarge: Totalt", "Címke színe: Összesített", "Tunnisteen väri: Kokonaisuus", "Màu nhãn: Tổng thể", "Kolor etykiety: Ogólny", "Culoare etichetă: General"},
    {"Value Color", "値の色", "Wertfarbe", "Color del valor", "Couleur de la valeur", "Colore valore", "Waardekleur", "Cor do valor", "Цвет значения", "数值颜色", "값 색상", "لون القيمة", "Warna nilai", "Колір значення", "Χρώμα τιμής", "Värdefärg", "สีค่า", "Barva hodnoty", "Værdifarve", "Değer rengi", "Verdifarge", "Érték színe", "Arvon väri", "Màu giá trị", "Kolor wartości", "Culoare valoare"},
    {"Value Color: Overall", "値の色: 全体", "Wertfarbe: Gesamt", "Color del valor: General", "Couleur valeur : Global", "Colore valore: Totale", "Waardekleur: Totaal", "Cor do valor: Geral", "Цвет значения: Общий", "数值颜色：整体", "값 색상: 전체", "لون القيمة: الإجمالي", "Warna nilai: Keseluruhan", "Колір значення: Загальний", "Χρώμα τιμής: Συνολικό", "Värdefärg: Totalt", "สีค่า: รวม", "Barva hodnoty: Celková", "Værdifarve: Samlet", "Değer rengi: Genel", "Verdifarge: Totalt", "Értékszín: Összesített", "Arvon väri: Kokonaisuus", "Màu giá trị: Tổng thể", "Kolor wartości: Ogólny", "Culoare valoare: General"},
    {"Sep Color", "区切り色", "Trennfarbe", "Color separador", "Couleur séparateur", "Colore separatore", "Scheidingskleur", "Cor separadora", "Цвет разделителя", "分隔色", "구분선 색상", "لون الفاصل", "Warna Pemisah", "Колір розділювача", "Χρώμα διαχωρισμού", "Separatorfärg", "สีตัวคั่น", "Barva oddělovače", "Separatorfarve", "Ayırıcı rengi", "Skillefarge", "Elválasztó szín", "Erottimen väri", "Màu phân cách", "Kolor separatora", "Culoare separator"},
    {"Sep Color: Overall", "区切り色: 全体", "Trennfarbe: Gesamt", "Color de separación: General", "Couleur de séparation : Global", "Colore separatore: Generale", "Scheidingskleur: Algemeen", "Cor de separação: Geral", "Цвет разделителя: Общий", "分隔颜色: 整体", "구분 색상: 전체", "لون الفاصل: إجمالي", "Warna Pemisah: Keseluruhan", "Колір розділювача: Загальний", "Χρώμα διαχωρισμού: Συνολικό", "Separationsfärg: Övergripande", "สีตัวคั่น: โดยรวม", "Barva oddělovače: Celkové", "Separationsfarve: Samlet", "Ayırıcı rengi: Genel", "Separasjonsfarge: Samlet", "Elválasztó szín: Összesített", "Erottimen väri: Kokonais", "Màu phân cách: Tổng thể", "Kolor separatora: Ogólny", "Culoare separator: General"},
    {"Goal Color", "目標色", "Zielfarbe", "Color de objetivo", "Couleur objectif", "Colore obiettivo", "Doelkleur", "Cor do objetivo", "Цвет цели", "目标颜色", "목표 색상", "لون الهدف", "Warna tujuan", "Колір цілі", "Χρώμα στόχου", "Målfärg", "สีเป้าหมาย", "Barva cíle", "Målfarve", "Hedef rengi", "Målfarge", "Cél színe", "Tavoitteen väri", "Màu mục tiêu", "Kolor celu", "Culoare obiectiv"},
    {"Goal Color: Overall", "目標色: 全体", "Zielfarbe: Gesamt", "Color de objetivo: General", "Couleur d'objectif : Global", "Colore obiettivo: Totale", "Doelkleur: Totaal", "Cor do objetivo: Geral", "Цвет цели: Общий", "目标颜色：总体", "목표 색상: 전체", "لون الهدف: الإجمالي", "Warna tujuan: Keseluruhan", "Колір цілі: Загальний", "Χρώμα στόχου: Συνολικό", "Målfärg: Totalt", "สีเป้าหมาย: รวม", "Barva cíle: Celková", "Målfarve: Samlet", "Hedef rengi: Genel", "Målfarge: Totalt", "Cél szín: Összesített", "Tavoiteväri: Kokonaisuus", "Màu mục tiêu: Tổng thể", "Kolor celu: Ogólny", "Culoare obiectiv: General"},
    {"Ordinal", "序数", "Ordinal", "Ordinal", "Ordinal", "Ordinale", "Ordinaal", "Ordinal", "Порядковый", "序数", "서수", "ترتيبي", "Ordinal", "Порядковий", "Τακτικός", "Ordinal", "ลำดับ", "Pořadové", "Ordinal", "Sıra sayısı", "Ordinal", "Sorrendi", "Järjestysluku", "Thứ tự", "Porządkowy", "Ordinal"},
    {"Color Overlay", "色オーバーレイ", "Farbüberlagerung", "Superposición de color", "Superposition de couleur", "Sovrapposizione colore", "Kleuroverlay", "Sobreposição de cor", "Цветовое наложение", "颜色叠加", "색상 오버레이", "تراكب اللون", "Overlay Warna", "Кольорове накладання", "Επικάλυψη χρώματος", "Färgöverlagring", "โอverlay สี", "Barevné překrytí", "Farveoverlejring", "Renk kaplaması", "Fargeoverlegg", "Szín átfedés", "Värin peitto", "Lớp phủ màu", "Nakładka koloru", "Suprapunere culoare"},
    {"Use Hunter Color", "ハンター色を使用", "Hunter-Farbe verwenden", "Usar color del cazador", "Utiliser la couleur du chasseur", "Usa colore cacciatore", "Hunterkleur gebruiken", "Usar cor do caçador", "Использовать цвет охотника", "使用猎人颜色", "헌터 색상 사용", "استخدام لون الصياد", "Gunakan warna hunter", "Використовувати колір мисливця", "Χρήση χρώματος κυνηγού", "Använd jägarfärg", "ใช้สี hunter", "Použít barvu lovce", "Brug hunterfarve", "Avcı rengini kullan", "Bruk hunterfarge", "Vadász szín használata", "Käytä metsästäjän väriä", "Dùng màu hunter", "Użyj koloru myśliwego", "Folosește culoarea vânătorului"},
    {"Radar Color", "レーダー色", "Radarfarbe", "Color del radar", "Couleur du radar", "Colore radar", "Radarkleur", "Cor do radar", "Цвет радара", "雷达颜色", "레이더 색상", "لون الرادار", "Warna radar", "Колір радара", "Χρώμα ραντάρ", "Radarfärg", "สีเรดาร์", "Barva radaru", "Radarfarve", "Radar rengi", "Radarfarge", "Radar színe", "Tutkan väri", "Màu radar", "Kolor radaru", "Culoare radar"},
    {"Display Size", "表示サイズ", "Anzeigegröße", "Tamaño de visualización", "Taille d'affichage", "Dimensione visualizzazione", "Weergavegrootte", "Tamanho de exibição", "Размер отображения", "显示大小", "표시 크기", "حجم العرض", "Ukuran tampilan", "Розмір відображення", "Μέγεθος εμφάνισης", "Visningsstorlek", "ขนาดแสดงผล", "Velikost zobrazení", "Visningsstørrelse", "Görüntüleme boyutu", "Visningsstørrelse", "Megjelenítési méret", "Näyttökoko", "Kích thước hiển thị", "Rozmiar wyświetlania", "Dimensiune afișare"},
    {"Dst Size", "表示サイズ", "Zielgröße", "Tamaño destino", "Taille cible", "Dim. destinazione", "Doelgrootte", "Tamanho destino", "Размер назначения", "显示大小", "표시 크기", "حجم الهدف", "Ukuran Dst", "Розмір цілі", "Μέγ. προορισμού", "Dest.storlek", "ขนาด Dst", "Vel. cíle", "Dest.størrelse", "Hedef Boyutu", "Dest.størrelse", "Célméret", "Kohteen koko", "Kích thước đích", "Rozm. docel.", "Dim. dest."},
    {"Dst X", "表示 X", "Ziel X", "Dest. X", "Dest. X", "Dest. X", "Best. X", "Dest. X", "Цел. X", "显示 X", "표시 X", "Dst X", "Dst X", "Ціл. X", "Dst X", "Dst X", "Dst X", "Cíl X", "Dst X", "Dst X", "Dst X", "Cél X", "Dst X", "Dst X", "Doc. X", "Dst X"},
    {"Dst Y", "表示 Y", "Ziel Y", "Dest. Y", "Dest. Y", "Dest. Y", "Doel Y", "Dest. Y", "Цел. Y", "显示 Y", "표시 Y", "عرض Y", "Dst Y", "Ціл. Y", "Προβ. Y", "Dst Y", "แสดง Y", "Cíl Y", "Dst Y", "Hedef Y", "Dst Y", "Cél Y", "Dst Y", "Hiển thị Y", "Doc. Y", "Dst Y"},
    {"Source Radius", "ソース半径", "Quellradius", "Radio de origen", "Rayon source", "Raggio sorgente", "Bronradius", "Raio de origem", "Радиус источника", "源半径", "소스 반경", "نصف قطر المصدر", "Radius sumber", "Радіус джерела", "Ακτίνα πηγής", "Källradie", "รัศมีแหล่ง", "Poloměr zdroje", "Kilderadius", "Kaynak yarıçapı", "Kilderadius", "Forrás sugár", "Lähteen säde", "Bán kính nguồn", "Promień źródła", "Rază sursă"},
    {"Src Radius", "ソース半径", "Quell-Radius", "Radio origen", "Rayon source", "Raggio sorgente", "Bronradius", "Raio de origem", "Исходный радиус", "源半径", "소스 반경", "نصف قطر المصدر", "Radius sumber", "Вихідний радіус", "Ακτίνα πηγής", "Källradie", "รัศมีต้นทาง", "Zdrojový poloměr", "Kilderadius", "Kaynak yarıçapı", "Kilderadius", "Forrás sugár", "Lähdesäde", "Bán kính nguồn", "Promień źródła", "Rază sursă"},
    {"Corner Radius", "角丸半径", "Eckenradius", "Radio de esquina", "Rayon d'angle", "Raggio angolo", "Hoekradius", "Raio do canto", "Радиус угла", "圆角半径", "모서리 반경", "نصف قطر الزاوية", "Radius Sudut", "Радіус кута", "Ακτίνα γωνίας", "Hörnradie", "รัศมีมุม", "Poloměr rohu", "Hjørneradius", "Köşe yarıçapı", "Hjørneradius", "Sarok sugár", "Kulmasäde", "Bán kính góc", "Promień rogu", "Rază colț"},
    {"Padding", "余白", "Abstand", "Relleno", "Marge", "Padding", "Padding", "Preenchimento", "Отступ", "边距", "여백", "Padding", "Padding", "Відступ", "Padding", "Padding", "Padding", "Odsazení", "Padding", "Padding", "Padding", "Kitöltés", "Täyte", "Padding", "Padding", "Padding"},
    {"Spacing", "間隔", "Abstand", "Espaciado", "Espacement", "Spaziatura", "Afstand", "Espaçamento", "Интервал", "间距", "간격", "تباعد", "Jarak", "Інтервал", "Διάκενο", "Avstånd", "ระยะห่าง", "Rozestup", "Afstand", "Aralık", "Avstand", "Távolság", "Väli", "Khoảng cách", "Odstęp", "Spațiere"},
    {"Not Owned Opacity", "未所持の不透明度", "Deckkraft (nicht im Besitz)", "Opacidad (no obtenido)", "Opacité (non possédé)", "Opacità non posseduto", "Dekking (niet in bezit)", "Opacidade (não obtido)", "Непрозрачность (не получено)", "未拥有不透明度", "미보유 불투명도", "شفافية غير المملوك", "Opasitas tidak dimiliki", "Непрозорість (не отримано)", "Αδιαφάνεια μη κατεχόμενου", "Opacitet ej ägd", "ความทึบที่ไม่ได้เป็นเจ้าของ", "Neprůhlednost (nevlastněno)", "Opacitet ikke ejet", "Sahip olunmayan opaklık", "Opasitet ikke eid", "Nem birtokolt átlátszatlanság", "Ei omistettu läpinäkyvyys", "Độ mờ chưa sở hữu", "Przezroczystość (nie posiadane)", "Opacitate neposedat"},
    {"Highlight", "ハイライト", "Hervorhebung", "Resaltado", "Surbrillance", "Evidenziazione", "Markering", "Destaque", "Подсветка", "高亮", "하이라이트", "تمييز", "Sorotan", "Підсвітка", "Επισήμανση", "Markering", "ไฮไลต์", "Zvýraznění", "Fremhævning", "Vurgulama", "Utheving", "Kiemelés", "Korostus", "Nổi bật", "Podświetlenie", "Evidențiere"},
    {"Highlight Current Weapon", "現在の武器をハイライト", "Aktuelle Waffe hervorheben", "Resaltar arma actual", "Mettre en évidence l'arme actuelle", "Evidenzia arma corrente", "Huidig wapen markeren", "Destacar arma atual", "Выделить текущее оружие", "高亮当前武器", "현재 무기 강조", "تمييز السلاح الحالي", "Sorot Senjata Saat Ini", "Підсвітити поточну зброю", "Επισήμανση τρέχοντος όπλου", "Markera aktuellt vapen", "ไฮไลต์อาวุธปัจจุบัน", "Zvýraznit aktuální zbraň", "Fremhæv nuværende våben", "Geçerli silahı vurgula", "Uthev gjeldende våpen", "Aktuális fegyver kiemelése", "Korosta nykyinen ase", "Làm nổi bật vũ khí hiện tại", "Podświetl bieżącą broń", "Evidențiază arma curentă"},
    {"Highlight Color", "ハイライト色", "Hervorhebungsfarbe", "Color de resaltado", "Couleur de surbrillance", "Colore evidenziazione", "Highlightkleur", "Cor de destaque", "Цвет выделения", "高亮颜色", "하이라이트 색상", "لون التمييز", "Warna Sorotan", "Колір виділення", "Χρώμα επισήμανσης", "Markeringsfärg", "สีไฮไลต์", "Barva zvýraznění", "Fremhævningsfarve", "Vurgu rengi", "Uthevingsfarge", "Kiemelés szín", "Korostuksen väri", "Màu nổi bật", "Kolor podświetlenia", "Culoare evidențiere"},
    {"Highlight Opacity", "ハイライト不透明度", "Hervorhebungsdeckkraft", "Opacidad del resaltado", "Opacité de surbrillance", "Opacità evidenziazione", "Highlight-dekking", "Opacidade do destaque", "Непрозрачность подсветки", "高亮不透明度", "하이라이트 불투명도", "شفافية التمييز", "Opasitas sorotan", "Непрозорість підсвітки", "Αδιαφάνεια επισήμανσης", "Markeringsopacitet", "ความทึบไฮไลต์", "Neprůhlednost zvýraznění", "Fremhævningsopacitet", "Vurgu opaklığı", "Uthevingsopasitet", "Kiemelés átlátszatlansága", "Korostuksen läpinäkymättömyys", "Độ mờ nổi bật", "Krycie podświetlenia", "Opacitate evidențiere"},
    {"Highlight Thickness", "ハイライト太さ", "Hervorhebungsstärke", "Grosor del resaltado", "Épaisseur de surbrillance", "Spessore evidenziazione", "Highlight-dikte", "Espessura do destaque", "Толщина подсветки", "高亮粗细", "하이라이트 두께", "سُمك التمييز", "Ketebalan highlight", "Товщина підсвітки", "Πάχος επισήμανσης", "Markeringstjocklek", "ความหนาไฮไลต์", "Tloušťka zvýraznění", "Markeringstykkelse", "Vurgu kalınlığı", "Markeringstykkelse", "Kiemelés vastagság", "Korostuksen paksuus", "Độ dày highlight", "Grubość podświetlenia", "Grosime evidențiere"},
    {"Highlight Padding", "ハイライト余白", "Hervorhebungs-Abstand", "Relleno del resaltado", "Marge surbrillance", "Padding evidenziazione", "Markering-opvulling", "Preenchimento do destaque", "Отступ подсветки", "高亮内边距", "하이라이트 패딩", "حشوة التمييز", "Padding sorotan", "Відступ підсвітки", "Περιθώριο επισήμανσης", "Markeringsutfyllnad", "ระยะขอบไฮไลต์", "Odsazení zvýraznění", "Fremhævningspolstring", "Vurgulama dolgusu", "Uthevingspolstring", "Kiemelés kitöltés", "Korostuksen täyte", "Đệm nổi bật", "Wypełnienie podświetlenia", "Padding evidențiere"},
    {"Highlight Corner Radius", "ハイライト角丸", "Hervorhebungs-Eckenradius", "Radio de esquina de resaltado", "Rayon d'angle surbrillance", "Raggio angolo evidenziazione", "Markeringshoekradius", "Raio do canto de destaque", "Радиус угла выделения", "高亮圆角", "강조 모서리 반경", "نصف قطر زاوية التمييز", "Radius Sudut Sorotan", "Радіус кута підсвітки", "Ακτίνα γωνίας επισήμανσης", "Markeringshörnradie", "รัศมีมุมไฮไลต์", "Poloměr rohu zvýraznění", "Fremhævningshjørneradius", "Vurgu köşe yarıçapı", "Uthevningshjørneradius", "Kiemelés sarok sugár", "Korostuksen kulmasäde", "Bán kính góc nổi bật", "Promień rogu podświetlenia", "Rază colț evidențiere"},
    {"Highlight Offset Left", "ハイライト左オフセット", "Hervorhebung Versatz links", "Desplazamiento izquierdo de resaltado", "Décalage gauche de surbrillance", "Offset evidenziazione sinistra", "Highlight-offset links", "Deslocamento esquerdo de destaque", "Смещение выделения влево", "高亮左偏移", "하이라이트 왼쪽 오프셋", "إزاحة التمييز لليسار", "Offset Sorotan Kiri", "Зміщення виділення вліво", "Μετατόπιση επισήμανσης αριστερά", "Markeringsförskjutning vänster", "ออฟเซ็ตไฮไลต์ซ้าย", "Posun zvýraznění vlevo", "Fremhævningsforskydning venstre", "Vurgu ofseti sol", "Uthevingsforskyvning venstre", "Kiemelés eltolás balra", "Korostuksen siirtymä vasemmalle", "Offset nổi bật trái", "Przesunięcie podświetlenia w lewo", "Offset evidențiere stânga"},
    {"Highlight Offset Right", "ハイライト右オフセット", "Hervorhebungsversatz rechts", "Desplazamiento del resaltado a la derecha", "Décalage surbrillance à droite", "Offset evidenziazione destra", "Highlight-offset rechts", "Deslocamento do destaque à direita", "Смещение подсветки вправо", "高亮右偏移", "하이라이트 오른쪽 오프셋", "إزاحة التمييز لليمين", "Offset sorotan kanan", "Зміщення підсвітки вправо", "Μετατόπιση επισήμανσης δεξιά", "Markeringsoffset höger", "ออฟเซ็ตไฮไลต์ขวา", "Odsazení zvýraznění vpravo", "Fremhævningsoffset højre", "Vurgu sağ ofseti", "Uthevingsoffset høyre", "Kiemelés jobb eltolás", "Korostuksen siirtymä oikealle", "Offset nổi bật phải", "Przesunięcie podświetlenia w prawo", "Offset evidențiere dreapta"},
    {"Highlight Offset Top", "ハイライト上オフセット", "Hervorhebungsversatz oben", "Desplazamiento superior del resaltado", "Décalage haut de surbrillance", "Offset superiore evidenziazione", "Highlightverschuiving boven", "Deslocamento superior do destaque", "Смещение подсветки сверху", "高亮上偏移", "하이라이트 상단 오프셋", "إزاحة التمييز للأعلى", "Offset atas highlight", "Зміщення підсвітки зверху", "Μετατόπιση επισήμανσης πάνω", "Markeringsförskjutning topp", "ออฟเซ็ตบน highlight", "Posun zvýraznění nahoře", "Markeringforskydning top", "Vurgu üst ofset", "Markeringsforskyvning topp", "Kiemelés felső eltolás", "Korostuksen yläsiirtymä", "Offset trên highlight", "Przesunięcie górne podświetlenia", "Offset sus evidențiere"},
    {"Highlight Offset Bottom", "ハイライト下オフセット", "Hervorhebungs-Offset unten", "Desplazamiento inferior del resaltado", "Décalage bas surbrillance", "Offset inferiore evidenziazione", "Markering-offset onder", "Deslocamento inferior do destaque", "Смещение подсветки снизу", "高亮下偏移", "하이라이트 하단 오프셋", "إزاحة التمييز للأسفل", "Ofs sorotan bawah", "Зміщення підсвітки знизу", "Μετατόπιση επισήμανσης κάτω", "Markeringsförskjutning nedåt", "ออฟเซ็ตไฮไลต์ล่าง", "Posun zvýraznění dolů", "Fremhævningsoffset bund", "Vurgulama ofseti alt", "Uthevingsoffset bunn", "Kiemelés eltolás alul", "Korostuksen siirto alas", "Ofs nổi bật dưới", "Przesunięcie podświetlenia w dół", "Offset evidențiere jos"},
    {"Size Offset Left", "サイズ左オフセット", "Größenversatz links", "Desplazamiento de tamaño izquierdo", "Décalage taille gauche", "Offset dimensione sinistra", "Grootte-offset links", "Deslocamento de tamanho esquerdo", "Смещение размера влево", "尺寸左偏移", "크기 왼쪽 오프셋", "إزاحة الحجم يسار", "Ofset Ukuran Kiri", "Зсув розміру ліворуч", "Μετατόπ. μεγέθους αριστερά", "Storleksförskjutning vänster", "Ofs ขนาดซ้าย", "Ofs velikosti vlevo", "Størrelsesforskydning venstre", "Boyut ofseti sol", "Størrelsesforskyvning venstre", "Méret eltolás balra", "Koon siirtymä vasemmalle", "Ofs kích thước trái", "Ofs rozmiaru w lewo", "Ofs dimensiune stânga"},
    {"Size Offset Right", "サイズ右オフセット", "Größenversatz rechts", "Desplazamiento derecho de tamaño", "Décalage droit de taille", "Offset dimensione destra", "Grootte-offset rechts", "Deslocamento direito de tamanho", "Смещение размера вправо", "尺寸右偏移", "크기 오른쪽 오프셋", "إزاحة الحجم لليمين", "Offset Ukuran Kanan", "Зміщення розміру вправо", "Μετατόπιση μεγέθους δεξιά", "Storleksförskjutning höger", "ออฟเซ็ตขนาดขวา", "Posun velikosti vpravo", "Størrelsesforskydning højre", "Boyut ofseti sağ", "Størrelsesforskyvning høyre", "Méret eltolás jobbra", "Koon siirtymä oikealle", "Offset kích thước phải", "Przesunięcie rozmiaru w prawo", "Offset dimensiune dreapta"},
    {"Size Offset Top", "サイズ上オフセット", "Größenversatz oben", "Desplazamiento de tamaño arriba", "Décalage taille en haut", "Offset dimensione alto", "Grootte-offset boven", "Deslocamento de tamanho superior", "Смещение размера сверху", "尺寸上偏移", "크기 상단 오프셋", "إزاحة الحجم للأعلى", "Offset ukuran atas", "Зміщення розміру зверху", "Μετατόπιση μεγέθους πάνω", "Storleksoffset topp", "ออฟเซ็ตขนาดบน", "Odsazení velikosti nahoře", "Størrelsesoffset top", "Boyut üst ofseti", "Størrelsesoffset topp", "Méret felső eltolás", "Koon siirtymä ylös", "Offset kích thước trên", "Przesunięcie rozmiaru góra", "Offset dimensiune sus"},
    {"Size Offset Bottom", "サイズ下オフセット", "Größenversatz unten", "Desplazamiento inferior de tamaño", "Décalage bas de taille", "Offset inferiore dimensione", "Grootteverschuiving onder", "Deslocamento inferior de tamanho", "Смещение размера снизу", "尺寸下偏移", "크기 하단 오프셋", "إزاحة الحجم للأسفل", "Offset bawah ukuran", "Зміщення розміру знизу", "Μετατόπιση μεγέθους κάτω", "Storleksförskjutning botten", "ออฟเซ็ตล่างขนาด", "Posun velikosti dole", "Størrelsesforskydning bund", "Boyut alt ofset", "Størrelsesforskyvning bunn", "Méret alsó eltolás", "Koon alasiirtymä", "Offset dưới kích thước", "Przesunięcie dolne rozmiaru", "Offset jos dimensiune"},
    {"Hl Opacity", "HL不透明度", "HL-Deckkraft", "Opacidad HL", "Opacité HL", "Opacità HL", "HL-dekking", "Opacidade HL", "Непрозрачность HL", "HL 不透明度", "HL 불투명도", "شفافية HL", "Opasitas HL", "Непрозорість HL", "Αδιαφάνεια HL", "HL-opacitet", "ความทึบ HL", "Neprůhlednost HL", "HL-gennemsigtighed", "HL opaklığı", "HL-opasitet", "HL átlátszatlanság", "HL:n läpinäkymättömyys", "Độ mờ HL", "Krycie HL", "Opacitate HL"},
    {"Hl Thickness", "HL太さ", "HL-Dicke", "Grosor HL", "Épaisseur HL", "Spess. HL", "HL-dikte", "Espess. HL", "Толщ. HL", "HL粗细", "HL 두께", "سُمك التمييز", "Ketebalan HL", "Товщ. підсв.", "Πάχος HL", "HL-tjocklek", "ความหนา HL", "Tloušťka HL", "HL-tykkelse", "HL kalınlığı", "HL-tykkelse", "HL vastagság", "HL-paksuus", "Độ dày HL", "Grubość HL", "Grosime HL"},
    {"Hl Padding", "HL余白", "HL-Abstand", "Relleno HL", "Marge HL", "Padding HL", "HL-padding", "Preenchimento HL", "Отступ HL", "HL 边距", "HL 여백", "Hl Padding", "Hl Padding", "Відступ HL", "Hl Padding", "Hl-padding", "Hl Padding", "Odsazení HL", "Hl-padding", "Hl Padding", "Hl-padding", "HL kitöltés", "HL-täyte", "Hl Padding", "Padding HL", "Hl Padding"},
    {"Hl Corner Radius", "HL角丸", "HL-Eckenradius", "Radio de esquina HL", "Rayon coin HL", "Raggio angolo HL", "HL-hoekradius", "Raio de canto HL", "Радиус угла HL", "HL 圆角", "HL 모서리 반경", "نصف قطر زاوية HL", "Radius sudut HL", "Радіус кута HL", "Ακτίνα γωνίας HL", "HL-hörnradius", "รัศมีมุม HL", "Poloměr rohu HL", "HL-hjørneradius", "HL köşe yarıçapı", "HL-hjørneradius", "HL sarok sugara", "HL-kulman säde", "Bán kính góc HL", "Promień rogu HL", "Rază colț HL"},
    {"Hl Ofs Left", "HL左Ofs", "HL-Versatz links", "Despl. HL izquierda", "Décalage HL gauche", "Offset HL sinistra", "HL-verschuiving links", "Desloc. HL esquerda", "Смещение HL слева", "HL 左偏移", "HL 왼쪽 오프셋", "إزاحة HL يسار", "Ofs HL kiri", "Зміщення HL зліва", "Hl Ofs αριστερά", "HL-förskjutning vänster", "Hl Ofs ซ้าย", "Hl Ofs vlevo", "HL-forskydning venstre", "HL ofs sol", "HL-forskyvning venstre", "HL ofs bal", "HL siirtymä vasemmalle", "Hl Ofs trái", "Hl Ofs lewo", "Hl Ofs stânga"},
    {"Hl Ofs Right", "HL右Ofs", "HL-Ofs rechts", "Despl. HL derecha", "Décal. HL droite", "Ofs HL destra", "HL-ofs rechts", "Desloc. HL direita", "Смещ. HL вправо", "HL 右偏移", "HL Ofs 오른쪽", "إزاحة HL يمين", "Ofs HL kanan", "Зміщ. HL вправо", "Μετατόπιση HL δεξιά", "HL-ofs höger", "Ofs HL ขวา", "Ofs HL vpravo", "HL-ofs højre", "HL ofs sağ", "HL-ofs høyre", "HL ofs jobbra", "HL:n ofs oikea", "Ofs HL phải", "Ofs HL w prawo", "Ofs HL dreapta"},
    {"Hl Ofs Top", "HLä¸Ofs", "HL-Ofs oben", "Ofs HL superior", "Ofs HL haut", "Ofs HL superiore", "HL-ofs boven", "Ofs HL superior", "Смещ. HL сверху", "HL上Ofs", "HL 위 Ofs", "إزاحة التمييز أعلى", "Ofs HL Atas", "Зсув підсв. зверху", "Μετατόπ. HL πάνω", "HL-förskjutning topp", "Ofs HL บน", "Ofs HL nahoře", "HL-forskydning top", "HL ofset üst", "HL-forskyvning topp", "HL eltolás felül", "HL-siirtymä ylhäällä", "Ofs HL trên", "Ofs HL góra", "Ofs HL sus"},
    {"Hl Ofs Bottom", "HLä¸Ofs", "HL-Versatz unten", "Despl. inf. HL", "Décal. bas HL", "Ofs. inf. HL", "HL-offset onder", "Desloc. inf. HL", "Смещ. низ HL", "HL 下偏移", "HL 하단 오프셋", "Hl Ofs Bottom", "Hl Ofs Bawah", "Зміщ. низ HL", "Hl Ofs Bottom", "Hl ofs botten", "Hl Ofs ล่าง", "Posun spod HL", "Hl ofs bund", "Hl Ofs Alt", "Hl ofs bunn", "HL eltolás alul", "HL-siirtymä alhaalla", "Hl Ofs Dưới", "Ofs. dół HL", "Hl Ofs Jos"},

    // HUD element and subsection names
    {"HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP", "HP"},
    {"HP Number Position", "HP数値位置", "HP-Zahlenposition", "Posición del número de HP", "Position du nombre de PV", "Posizione numero HP", "HP-cijferpositie", "Posição do número de HP", "Позиция числа HP", "HP 数值位置", "HP 숫자 위치", "موضع رقم HP", "Posisi angka HP", "Позиція числа HP", "Θέση αριθμού HP", "HP-siffra position", "ตำแหน่งตัวเลข HP", "Pozice čísla HP", "HP-tal position", "HP sayı konumu", "HP-tall posisjon", "HP szám pozíció", "HP-luvun sijainti", "Vị trí số HP", "Pozycja liczby HP", "Poziție număr HP"},
    {"HP Label Color By Value", "HPラベルの色 (値連動)", "HP-Label-Farbe nach Wert", "Color de etiqueta HP según valor", "Couleur libellé HP selon valeur", "Colore etichetta HP per valore", "HP-labelkleur op waarde", "Cor do rótulo HP por valor", "Цвет метки HP по значению", "HP 标签颜色（按数值）", "HP 라벨 색상 (값별)", "لون تسمية HP حسب القيمة", "Warna label HP menurut nilai", "Колір мітки HP за значенням", "Χρώμα ετικέτας HP ανά τιμή", "HP-etikettfärg efter värde", "สีป้าย HP ตามค่า", "Barva popisku HP podle hodnoty", "HP-etiketfarve efter værdi", "Değere göre HP etiket rengi", "HP-etikettfarge etter verdi", "HP címke színe érték szerint", "HP-tunnisteen väri arvon mukaan", "Màu nhãn HP theo giá trị", "Kolor etykiety HP według wartości", "Culoare etichetă HP după valoare"},
    {"HP Outline", "HPのアウトライン", "HP-Umriss", "Contorno HP", "Contour HP", "Contorno HP", "HP-omtrek", "Contorno HP", "Контур HP", "HP 轮廓", "HP 외곽선", "حد HP", "Garis Luar HP", "Контур HP", "Περίγραμμα HP", "HP-kontur", "เส้นขอบ HP", "Obrys HP", "HP-kontur", "HP ana hat", "HP-kontur", "HP körvonal", "HP-ääriviiva", "Viền HP", "Obrys HP", "Contur HP"},
    {"HP Gauge", "HPゲージ", "HP-Anzeige", "Indicador de HP", "Jauge de PV", "Indicatore HP", "HP-meter", "Medidor de HP", "Индикатор HP", "HP 指示条", "HP 게이지", "مؤشر HP", "Gauge HP", "Індикатор HP", "Μετρητής HP", "HP-mätare", "Gauge HP", "Ukazatel HP", "HP-måler", "HP göstergesi", "HP-måler", "HP mérő", "HP-mittari", "Gauge HP", "Wskaźnik HP", "Indicator HP"},
    {"HP Gauge Color By Value", "HPゲージの色 (値連動)", "HP-Anzeigefarbe nach Wert", "Color del medidor de HP según valor", "Couleur jauge HP selon la valeur", "Colore indicatore HP per valore", "HP-meterkleur op waarde", "Cor do medidor de HP por valor", "Цвет индикатора HP по значению", "HP 量表颜色（按数值）", "HP 게이지 색상 (값 연동)", "لون مقياس HP حسب القيمة", "Warna gauge HP menurut nilai", "Колір індикатора HP за значенням", "Χρώμα ένδειξης HP ανά τιμή", "HP-mätarfärg efter värde", "สีเกจ HP ตามค่า", "Barva ukazatele HP podle hodnoty", "HP-målerfarve efter værdi", "Değere göre HP göstergesi rengi", "HP-målerfarge etter verdi", "HP mérő színe érték szerint", "HP-mittarin väri arvon mukaan", "Màu đồng hồ HP theo giá trị", "Kolor wskaźnika HP według wartości", "Culoare indicator HP după valoare"},
    {"HP Gauge Outline", "HPゲージのアウトライン", "HP-Anzeigen-Kontur", "Contorno del medidor de HP", "Contour de la jauge de PV", "Contorno indicatore HP", "HP-meter contour", "Contorno do medidor de HP", "Контур индикатора HP", "HP 计量条轮廓", "HP 게이지 외곽선", "حد مقياس HP", "Outline gauge HP", "Контур індикатора HP", "Περίγραμμα ένδειξης HP", "HP-mätare kontur", "เส้นขอบเกจ HP", "Obrys ukazatele HP", "HP-måler kontur", "HP göstergesi ana hattı", "HP-måler kontur", "HP mérő körvonal", "HP-mittarin ääriviiva", "Viền thanh HP", "Obrys wskaźnika HP", "Contur indicator HP"},
    {"Ammo", "弾薬", "Munition", "Munición", "Munitions", "Munizioni", "Munitie", "Munição", "Боеприпасы", "弹药", "탄약", "ذخيرة", "Amunisi", "Боєприпаси", "Πυρομαχικά", "Ammunition", "กระสุน", "Munice", "Ammunition", "Mühimmat", "Ammunisjon", "Lőszer", "Ammukset", "Đạn", "Amunicja", "Muniție"},
    {"Ammo Number Position", "弾薬数値位置", "Munitionszahl-Position", "Posición del número de munición", "Position nombre munitions", "Posizione numero munizioni", "Munitienummerpositie", "Posição do número de munição", "Положение числа патронов", "弹药数值位置", "탄약 수치 위치", "موضع رقم الذخيرة", "Posisi Angka Amunisi", "Позиція числа боєприпасів", "Θέση αριθμού πυρομαχικών", "Ammosiffersposition", "ตำแหน่งตัวเลขกระสุน", "Pozice čísla munice", "Ammotallposition", "Mühimmat sayısı konumu", "Ammotallposisjon", "Lőszer szám pozíció", "Ammusnumeron sijainti", "Vị trí số đạn", "Pozycja liczby amunicji", "Poziție număr muniție"},
    {"Ammo Label Color By Value", "弾薬ラベルの色 (値連動)", "Munitionslabel-Farbe nach Wert", "Color de etiqueta de munición según valor", "Couleur d'étiquette munitions selon la valeur", "Colore etichetta munizioni per valore", "Munitielabelkleur op waarde", "Cor do rótulo de munição por valor", "Цвет метки боеприпасов по значению", "弹药标签颜色 (按数值)", "탄약 라벨 색상 (값 연동)", "لون تسمية الذخيرة حسب القيمة", "Warna Label Amunisi Berdasarkan Nilai", "Колір мітки боєприпасів за значенням", "Χρώμα ετικέτας πυρομαχικών ανά τιμή", "Ammo-etikettfärg efter värde", "สีป้ายกระสุนตามค่า", "Barva popisku munice podle hodnoty", "Ammo-label farve efter værdi", "Mühimmat etiketi rengi değere göre", "Ammo-etikettfarge etter verdi", "Lőszer címke szín érték szerint", "Ammustunnisteen väri arvon mukaan", "Màu nhãn đạn theo giá trị", "Kolor etykiety amunicji wg wartości", "Culoare etichetă muniție după valoare"},
    {"Ammo Outline", "弾薬のアウトライン", "Munitionskontur", "Contorno de munición", "Contour des munitions", "Contorno munizioni", "Munitiecontour", "Contorno de munição", "Контур боеприпасов", "弹药轮廓", "탄약 외곽선", "حدود الذخيرة", "Garis luar amunisi", "Контур боєприпасів", "Περίγραμμα πυρομαχικών", "Ammokontur", "เส้นขอบกระสุน", "Obrys munice", "Ammokontur", "Cephane dış çizgisi", "Ammokontur", "Lőszer körvonal", "Ammon ääriviiva", "Viền đạn", "Obrys amunicji", "Contur muniție"},
    {"Ammo Gauge", "弾薬ゲージ", "Munitionsanzeige", "Medidor de munición", "Jauge de munitions", "Indicatore munizioni", "Munitiemeter", "Medidor de munição", "Индикатор боеприпасов", "弹药计量条", "탄약 게이지", "مقياس الذخيرة", "Gauge amunisi", "Індикатор боєприпасів", "Ένδειξη πυρομαχικών", "Ammunitionsmätare", "เกจกระสุน", "Ukazatel munice", "Ammunitionsmåler", "Mühimmat göstergesi", "Ammunisjonsmåler", "Lőszer mérő", "Ammusten mittari", "Thanh đạn", "Wskaźnik amunicji", "Indicator muniție"},
    {"Ammo Gauge Color By Value", "弾薬ゲージの色 (値連動)", "Munitionsanzeige-Farbe nach Wert", "Color del medidor de munición según valor", "Couleur jauge munitions selon valeur", "Colore indicatore munizioni per valore", "Munitiemeterkleur op waarde", "Cor do medidor de munição por valor", "Цвет шкалы боеприпасов по значению", "弹药条颜色（按数值）", "탄약 게이지 색상 (값별)", "لون مقياس الذخيرة حسب القيمة", "Warna pengukur amunisi menurut nilai", "Колір шкали боєприпасів за значенням", "Χρώμα μετρητή πυρομαχικών ανά τιμή", "Ammunitionsmätarfärg efter värde", "สีเกจกระสุนตามค่า", "Barva ukazatele munice podle hodnoty", "Ammunitionsmålerfarve efter værdi", "Değere göre mühimmat göstergesi rengi", "Ammunisjonsmålerfarge etter verdi", "Lőszer mérő színe érték szerint", "Ammusmittarin väri arvon mukaan", "Màu thanh đạn theo giá trị", "Kolor wskaźnika amunicji według wartości", "Culoare indicator muniție după valoare"},
    {"Ammo Gauge Outline", "弾薬ゲージのアウトライン", "Munitionsanzeige-Umriss", "Contorno del medidor de munición", "Contour jauge munitions", "Contorno indicatore munizioni", "Munitiemeter-omtrek", "Contorno do medidor de munição", "Контур индикатора патронов", "弹药计量条轮廓", "탄약 게이지 외곽선", "حد مقياس الذخيرة", "Garis Luar Gauge Amunisi", "Контур шкали боєприпасів", "Περίγραμμα μετρητή πυρομαχικών", "Ammomätarkontur", "เส้นขอบ Gauge กระสุน", "Obrys měřidla munice", "Ammomålerkontur", "Mühimmat göstergesi ana hat", "Ammomålerkontur", "Lőszer mérő körvonal", "Ammusmittarin ääriviiva", "Viền đồng hồ đạn", "Obrys wskaźnika amunicji", "Contur indicator muniție"},
    {"Weapon/Ammo", "武器/弾薬", "Waffe/Munition", "Arma/Munición", "Arme/Munitions", "Arma/Munizioni", "Wapen/Munitie", "Arma/Munição", "Оружие/Боеприпасы", "武器/弹药", "무기/탄약", "سلاح/ذخيرة", "Senjata/Amunisi", "Зброя/Боєприпаси", "Όπλο/Πυρομαχικά", "Vapen/Ammo", "อาวุธ/กระสุน", "Zbraň/Munice", "Våben/Ammo", "Silah/Mühimmat", "Våpen/Ammo", "Fegyver/Lőszer", "Ase/Ammus", "Vũ khí/Đạn", "Broń/Amunicja", "Armă/Muniție"},
    {"Weapon Icon", "武器アイコン", "Waffensymbol", "Icono de arma", "Icône d'arme", "Icona arma", "Wapenpictogram", "Ícone de arma", "Иконка оружия", "武器图标", "무기 아이콘", "أيقونة السلاح", "Ikon senjata", "Іконка зброї", "Εικονίδιο όπλου", "Vapenikon", "ไอคอนอาวุธ", "Ikona zbraně", "Våbenikon", "Silah simgesi", "Våpenikon", "Fegyver ikon", "Aseikoni", "Biểu tượng vũ khí", "Ikona broni", "Pictogram armă"},
    {"Weapon Icon Outline", "武器アイコンのアウトライン", "Waffensymbol-Kontur", "Contorno del icono de arma", "Contour de l'icône d'arme", "Contorno icona arma", "Wapenicoon contour", "Contorno do ícone de arma", "Контур значка оружия", "武器图标轮廓", "무기 아이콘 외곽선", "حد أيقونة السلاح", "Outline ikon senjata", "Контур значка зброї", "Περίγραμμα εικονιδίου όπλου", "Vapenikon kontur", "เส้นขอบไอคอนอาวุธ", "Obrys ikony zbraně", "Våbenikon kontur", "Silah simgesi ana hattı", "Våpenikon kontur", "Fegyver ikon körvonal", "Aseikonin ääriviiva", "Viền biểu tượng vũ khí", "Obrys ikony broni", "Contur pictogramă armă"},
    {"Weapon Icon Color Overlay", "武器アイコンの色オーバーレイ", "Waffensymbol-Farboverlay", "Superposición de color del icono de arma", "Superposition couleur icône d'arme", "Sovrapposizione colore icona arma", "Kleuroverlay wapenpictogram", "Sobreposição de cor do ícone de arma", "Цветовая накладка иконки оружия", "武器图标颜色叠加", "무기 아이콘 색상 오버레이", "تراكب لون أيقونة السلاح", "Overlay warna ikon senjata", "Накладання кольору іконки зброї", "Επικάλυψη χρώματος εικονιδίου όπλου", "Vapenikonfärgöverlagring", "โอเวอร์เลย์สีไอคอนอาวุธ", "Překrytí barvy ikony zbraně", "Våbenikonfarveoverlay", "Silah simgesi renk katmanı", "Våpenikonfargeoverlegg", "Fegyver ikon szín fedvény", "Aseikonin värin peitto", "Lớp phủ màu biểu tượng vũ khí", "Nakładka koloru ikony broni", "Suprapunere culoare pictogramă armă"},
    {"Weapon Inventory", "武器インベントリ", "Waffeninventar", "Inventario de armas", "Inventaire d'armes", "Inventario armi", "Wapeninventaris", "Inventário de armas", "Инвентарь оружия", "武器栏", "무기 인벤토리", "مخزون الأسلحة", "Inventaris Senjata", "Інвентар зброї", "Απόθεμα όπλων", "Vapeninventarie", "คลังอาวุธ", "Inventář zbraní", "Våbeninventar", "Silah envanteri", "Våpeninventar", "Fegyvertár", "Asevarasto", "Kho vũ khí", "Ekwipunek broni", "Inventar arme"},
    {"Weapon Inventory Highlight", "武器インベントリのハイライト", "Waffen-Inventar-Hervorhebung", "Resaltado del inventario de armas", "Surbrillance de l'inventaire d'armes", "Evidenziazione inventario armi", "Wapeninventaris-highlight", "Destaque do inventário de armas", "Выделение инвентаря оружия", "武器栏高亮", "무기 인벤토리 하이라이트", "تمييز مخزون الأسلحة", "Sorot Inventaris Senjata", "Виділення інвентарю зброї", "Επισήμανση αποθέματος όπλων", "Vapeninventarie-markering", "ไฮไลต์คลังอาวุธ", "Zvýraznění inventáře zbraní", "Våbeninventar fremhævning", "Silah envanteri vurgusu", "Våpeninventar utheving", "Fegyver inventár kiemelés", "Asevaraston korostus", "Nổi bật kho vũ khí", "Podświetlenie ekwipunku broni", "Evidențiere inventar arme"},
    {"Weapon Inventory Outline", "武器インベントリのアウトライン", "Kontur Waffeninventar", "Contorno del inventario de armas", "Contour de l'inventaire d'armes", "Contorno inventario armi", "Wapeninventariscontour", "Contorno do inventário de armas", "Контур инвентаря оружия", "武器栏轮廓", "무기 인벤토리 외곽선", "حدود مخزون الأسلحة", "Garis luar inventaris senjata", "Контур інвентаря зброї", "Περίγραμμα αποθέματος όπλων", "Vapeninventarie-kontur", "เส้นขอบคลังอาวุธ", "Obrys inventáře zbraní", "Våbeninventar-kontur", "Silah envanteri dış çizgisi", "Våpeninventar-kontur", "Fegyver készlet körvonal", "Asevaraston ääriviiva", "Viền kho vũ khí", "Obrys ekwipunku broni", "Contur inventar arme"},
    {"Weapon Inventory Icon Outline", "武器インベントリのアイコンのアウトライン", "Waffeninventar-Symbol-Kontur", "Contorno del icono del inventario de armas", "Contour de l'icône d'inventaire d'armes", "Contorno icona inventario armi", "Wapeninventaris-icoon contour", "Contorno do ícone do inventário de armas", "Контур значка инвентаря оружия", "武器栏图标轮廓", "무기 인벤토리 아이콘 외곽선", "حد أيقونة مخزون الأسلحة", "Outline ikon inventori senjata", "Контур значка інвентарю зброї", "Περίγραμμα εικονιδίου αποθέματος όπλων", "Vapeninventarieikon kontur", "เส้นขอบไอคอนคลังอาวุธ", "Obrys ikony inventáře zbraní", "Våbeninventarikon kontur", "Silah envanteri simgesi ana hattı", "Våpeninventarikon kontur", "Fegyver inventár ikon körvonal", "Aseinventaarikonin ääriviiva", "Viền biểu tượng kho vũ khí", "Obrys ikony ekwipunku broni", "Contur pictogramă inventar arme"},
    {"Match Status", "試合情報", "Spielstand", "Estado de partida", "État du match", "Stato partita", "Wedstrijdstatus", "Estado da partida", "Состояние матча", "比赛状态", "매치 상태", "حالة المباراة", "Status pertandingan", "Стан матчу", "Κατάσταση αγώνα", "Matchstatus", "สถานะแมตช์", "Stav zápasu", "Kampstatus", "Maç durumu", "Kampstatus", "Mérkőzés állapota", "Ottelun tila", "Trạng thái trận", "Stan meczu", "Stare meci"},
    {"Score", "スコア", "Punktestand", "Puntuación", "Score", "Punteggio", "Score", "Pontuação", "Счёт", "分数", "점수", "النتيجة", "Skor", "Рахунок", "Σκορ", "Poäng", "คะแนน", "Skóre", "Score", "Skor", "Poeng", "Pontszám", "Pisteet", "Điểm", "Wynik", "Scor"},
    {"Score Labels", "スコアラベル", "Punkte-Labels", "Etiquetas de puntuación", "Étiquettes de score", "Etichette punteggio", "Scorelabels", "Rótulos de pontuação", "Метки счёта", "分数标签", "점수 라벨", "تسميات النقاط", "Label Skor", "Мітки рахунку", "Ετικέτες σκορ", "Poängetiketter", "ป้ายคะแนน", "Popisky skóre", "Scorelabels", "Skor etiketleri", "Poeng-etiketter", "Pontszám címkék", "Pistetunnisteet", "Nhãn điểm", "Etykiety wyniku", "Etichete scor"},
    {"Score Colors", "スコアの色", "Punktefarben", "Colores de puntuación", "Couleurs de score", "Colori punteggio", "Scorekleuren", "Cores de pontuação", "Цвета счёта", "得分颜色", "점수 색상", "ألوان النقاط", "Warna skor", "Кольори рахунку", "Χρώματα σκορ", "Poängfärger", "สีคะแนน", "Barvy skóre", "Scorefarver", "Skor renkleri", "Poengfarger", "Pontszín", "Pisteiden värit", "Màu điểm", "Kolory wyniku", "Culori scor"},
    {"Score Outline", "スコアのアウトライン", "Punkte-Kontur", "Contorno de puntuación", "Contour du score", "Contorno punteggio", "Score contour", "Contorno da pontuação", "Контур счёта", "分数轮廓", "점수 외곽선", "حد النقاط", "Outline skor", "Контур рахунку", "Περίγραμμα σκορ", "Poäng kontur", "เส้นขอบคะแนน", "Obrys skóre", "Score kontur", "Skor ana hattı", "Poeng kontur", "Pontszám körvonal", "Pisteiden ääriviiva", "Viền điểm", "Obrys wyniku", "Contur scor"},
    {"Rank / Time", "順位 / 時間", "Rang / Zeit", "Rango / Tiempo", "Rang / Temps", "Grado / Tempo", "Rang / Tijd", "Classificação / Tempo", "Ранг / Время", "排名 / 时间", "순위 / 시간", "الرتبة / الوقت", "Peringkat / Waktu", "Ранг / Час", "Κατάταξη / Χρόνος", "Rang / Tid", "อันดับ / เวลา", "Pořadí / Čas", "Rang / Tid", "Sıra / Süre", "Rang / Tid", "Rang / Idő", "Sijoitus / Aika", "Hạng / Thời gian", "Ranga / Czas", "Rang / Timp"},
    {"Rank", "順位", "Rang", "Rango", "Rang", "Grado", "Rang", "Classificação", "Ранг", "排名", "순위", "الرتبة", "Peringkat", "Ранг", "Κατάταξη", "Rang", "อันดับ", "Hodnost", "Rang", "Sıra", "Rang", "Rang", "Sijoitus", "Hạng", "Ranga", "Rang"},
    {"Rank Outline", "順位のアウトライン", "Rang-Umriss", "Contorno de rango", "Contour du rang", "Contorno rango", "Rangomtrek", "Contorno de rank", "Контур ранга", "排名轮廓", "순위 외곽선", "حدود الرتبة", "Garis Peringkat", "Контур рангу", "Περίγραμμα βαθμού", "Rangkontur", "เส้นขอบอันดับ", "Obrys hodnosti", "Rangkontur", "Rütbe ana hattı", "Rangkontur", "Rang körvonal", "Arvon ääriviiva", "Viền hạng", "Obrys rangi", "Contur rang"},
    {"Time Left", "残り時間", "Verbleibende Zeit", "Tiempo restante", "Temps restant", "Tempo rimanente", "Resterende tijd", "Tempo restante", "Оставшееся время", "剩余时间", "남은 시간", "الوقت المتبقي", "Waktu tersisa", "Час, що залишився", "Χρόνος που απομένει", "Tid kvar", "เวลาที่เหลือ", "Zbývající čas", "Tid tilbage", "Kalan süre", "Tid igjen", "Hátralévő idő", "Aikaa jäljellä", "Thời gian còn lại", "Pozostały czas", "Timp rămas"},
    {"Time Left Outline", "残り時間のアウトライン", "Restzeit-Kontur", "Contorno de tiempo restante", "Contour du temps restant", "Contorno tempo rimanente", "Resterende tijd contour", "Contorno do tempo restante", "Контур оставшегося времени", "剩余时间轮廓", "남은 시간 외곽선", "حد الوقت المتبقي", "Outline waktu tersisa", "Контур часу, що залишився", "Περίγραμμα υπολειπόμενου χρόνου", "Tid kvar kontur", "เส้นขอบเวลาที่เหลือ", "Obrys zbývajícího času", "Tid tilbage kontur", "Kalan süre ana hattı", "Tid igjen kontur", "Hátralévő idő körvonal", "Jäljellä olevan ajan ääriviiva", "Viền thời gian còn lại", "Obrys pozostałego czasu", "Contur timp rămas"},
    {"Time Limit", "制限時間", "Zeitlimit", "Límite de tiempo", "Limite de temps", "Limite di tempo", "Tijdslimiet", "Limite de tempo", "Лимит времени", "时间限制", "시간 제한", "حد الوقت", "Batas waktu", "Ліміт часу", "Χρονικό όριο", "Tidsgräns", "จำกัดเวลา", "Časový limit", "Tidsgrænse", "Süre sınırı", "Tidsgrense", "Időkorlát", "Aikaraja", "Giới hạn thời gian", "Limit czasu", "Limită de timp"},
    {"Time Limit Outline", "制限時間のアウトライン", "Zeitlimit-Umriss", "Contorno de límite de tiempo", "Contour limite de temps", "Contorno limite tempo", "Tijdslimiet-omtrek", "Contorno de limite de tempo", "Контур лимита времени", "时间限制轮廓", "제한 시간 외곽선", "حد حد الوقت", "Garis Luar Batas Waktu", "Контур ліміту часу", "Περίγραμμα χρονικού ορίου", "Tidsgränskontur", "เส้นขอบจำกัดเวลา", "Obrys časového limitu", "Tidsgrænsekontur", "Süre sınırı ana hat", "Tidsgrensekontur", "Időkorlát körvonal", "Aikarajan ääriviiva", "Viền giới hạn thời gian", "Obrys limitu czasu", "Contur limită timp"},
    {"Bomb", "ボム", "Bombe", "Bomba", "Bombe", "Bomba", "Bom", "Bomba", "Бомба", "炸弹", "폭탄", "قنبلة", "Bom", "Бомба", "Βόμβα", "Bomb", "ระเบิด", "Bomba", "Bombe", "Bomba", "Bombe", "Bomba", "Pommi", "Bom", "Bomba", "Bombă"},
    {"Bomb Left", "残りボム", "Bomben übrig", "Bombas restantes", "Bombes restantes", "Bombe rimaste", "Bommen over", "Bombas restantes", "Бомб осталось", "剩余炸弹", "남은 폭탄", "القنابل المتبقية", "Bom tersisa", "Бомб залишилось", "Βόμβες που απομένουν", "Bomber kvar", "ระเบิดที่เหลือ", "Zbývající bomby", "Bomber tilbage", "Kalan bomba", "Bomber igjen", "Hátralévő bombák", "Pommeja jäljellä", "Bom còn lại", "Pozostałe bomby", "Bombe rămase"},
    {"Bomb Left Outline", "残りボムのアウトライン", "Restbomben-Kontur", "Contorno de bombas restantes", "Contour des bombes restantes", "Contorno bombe rimanenti", "Resterende bommen contour", "Contorno de bombas restantes", "Контур оставшихся бомб", "剩余炸弹轮廓", "남은 폭탄 외곽선", "حد القنابل المتبقية", "Outline bom tersisa", "Контур бомб, що залишилися", "Περίγραμμα υπολειπόμενων βομβών", "Bomber kvar kontur", "เส้นขอบบอมบ์ที่เหลือ", "Obrys zbývajících bomb", "Bomber tilbage kontur", "Kalan bomba ana hattı", "Bomber igjen kontur", "Hátralévő bombák körvonal", "Jäljellä olevien pommien ääriviiva", "Viền bom còn lại", "Obrys pozostałych bomb", "Contur bombe rămase"},
    {"Bomb Icon", "ボムアイコン", "Bomben-Symbol", "Icono de bomba", "Icône bombe", "Icona bomba", "Bompictogram", "Ícone de bomba", "Иконка бомбы", "炸弹图标", "폭탄 아이콘", "أيقونة القنبلة", "Ikon bom", "Іконка бомби", "Εικονίδιο βόμβας", "Bombikon", "ไอคอนวางระเบิด", "Ikona bomby", "Bombeikon", "Bomba simgesi", "Bombeikon", "Bomba ikon", "Pommi-ikoni", "Biểu tượng bom", "Ikona bomby", "Pictogramă bombă"},
    {"Bomb Icon Outline", "ボムアイコンのアウトライン", "Bomben-Symbol-Umriss", "Contorno del icono de bomba", "Contour icône bombe", "Contorno icona bomba", "Bompictogram-omtrek", "Contorno do ícone de bomba", "Контур значка бомбы", "炸弹图标轮廓", "폭탄 아이콘 외곽선", "حد أيقونة القنبلة", "Garis Luar Ikon Bom", "Контур значка бомби", "Περίγραμμα εικονιδίου βόμβας", "Bombikonkontur", "เส้นขอบไอคอนบอมบ์", "Obrys ikony bomby", "Bombeikonkontur", "Bomba simgesi ana hat", "Bombeikonkontur", "Bomba ikon körvonal", "Pommi-ikonin ääriviiva", "Viền biểu tượng bom", "Obrys ikony bomby", "Contur pictogramă bombă"},
    {"Radar", "レーダー", "Radar", "Radar", "Radar", "Radar", "Radar", "Radar", "Radar", "雷达", "레이더", "رادار", "Radar", "Radar", "Ραντάρ", "Radar", "เรดาร์", "Radar", "Radar", "Radar", "Radar", "Radar", "Tutka", "Radar", "Radar", "Radar"},
    {"Radar Settings", "レーダー設定", "Radar-Einstellungen", "Ajustes del radar", "Paramètres du radar", "Impostazioni radar", "Radarinstellingen", "Configurações do radar", "Настройки радара", "雷达设置", "레이더 설정", "إعدادات الرادار", "Pengaturan radar", "Налаштування радара", "Ρυθμίσεις ραντάρ", "Radarinställningar", "การตั้งค่าเรดาร์", "Nastavení radaru", "Radarindstillinger", "Radar ayarları", "Radarinnstillinger", "Radar beállítások", "Tutka-asetukset", "Cài đặt radar", "Ustawienia radaru", "Setări radar"},
    {"Radar Outline", "レーダーのアウトライン", "Radar-Kontur", "Contorno del radar", "Contour du radar", "Contorno radar", "Radar contour", "Contorno do radar", "Контур радара", "雷达轮廓", "레이더 외곽선", "حد الرادار", "Outline radar", "Контур радара", "Περίγραμμα ραντάρ", "Radar kontur", "เส้นขอบเรดาร์", "Obrys radaru", "Radar kontur", "Radar ana hattı", "Radar kontur", "Radar körvonal", "Tutkan ääriviiva", "Viền radar", "Obrys radaru", "Contur radar"},
    {"Frame Outline", "フレームのアウトライン", "Rahmenkontur", "Contorno del marco", "Contour du cadre", "Contorno cornice", "Frame-omlijning", "Contorno da moldura", "Контур рамки", "边框轮廓", "프레임 외곽선", "حدود الإطار", "Garis luar bingkai", "Контур рамки", "Περίγραμμα πλαισίου", "Ramkontur", "เส้นขอบเฟรม", "Obrys rámečku", "Rammeomrids", "Çerçeve dış çizgisi", "Rammeomriss", "Keret körvonal", "Kehyksen ääriviiva", "Viền khung", "Kontur ramki", "Contur cadru"},
    {"Crosshair", "照準", "Fadenkreuz", "Retícula", "Réticule", "Mirino", "Richtkruis", "Mira", "Прицел", "准星", "조준선", "شبكة التصويب", "Reticle", "Приціл", "Σταυρονόμι", "Sikte", "เรติเคิล", "Mířidla", "Sigtekorn", "Nişangah", "Sikte", "Célkereszt", "Tähtäin", "Tâm ngắm", "Celownik", "Reticul"},
    {"Wpn\\\\\\\\\\\\\\\\nIcon", "Armă\\nPictogramă", "Wpn\nSymbol", "Arma\nIcono", "Arme\nIcône", "Arma\nIcona", "Wpn\nPictogram", "Arma\nÍcone", "Оруж.\nЗначок", "武器\n图标", "무기\n아이콘", "Wpn\nIcon", "Wpn\nIkon", "Збр.\nЗначок", "Όπλ.\nΕικονίδιο", "Vap\nIkon", "Wpn\nไอคอน", "Zbr.\nIkona", "Våb\nIkon", "Sil\nSimge", "Våp\nIkon", "Fegy\nIkon", "Ase\nKuvake", "Wpn\nBiểu tượng", "Broń\nIkona", "Armă\nPictogramă"},
    {"Bmb\\\\\\\\\\\\\\\\nIcon", "Pictogram\\nbombă", "Bomben-\nSymbol", "Icono\nde bomba", "Icône\nbombe", "Icona\nbomba", "Bom\npictogram", "Ícone\nde bomba", "Иконка\nбомбы", "炸弹\n图标", "폭탄\n아이콘", "أيقونة\nقنبلة", "Ikon\nbom", "Іконка\nбомби", "Εικονίδιο\nβόμβας", "Bomb-\nikon", "ไอคอน\nระเบิด", "Ikona\nbomby", "Bombe-\nikon", "Bomba\nsimgesi", "Bombe-\nikon", "Bomba\nikon", "Pommi-\nikoni", "Biểu tượng\nbom", "Ikona\nbomby", "Pictogram\nbombă"},
    {"Wpn\\\\\\\\\\\\\\\\nInventory", "Arme\\nInventar", "Waffen-\nInventar", "Armas\nInventario", "Armes\nInventaire", "Armi\nInventario", "Wapen-\nInventaris", "Armas\nInventário", "Оружие\nИнвентарь", "武器\n栏", "무기\n인벤토리", "أسلحة\nالمخزون", "Senjata\nInventori", "Зброя\nІнвентар", "Όπλα\nΑπόθεμα", "Vapen\nInventarie", "อาวุธ\nคลัง", "Zbraně\nInventář", "Våben\nInventar", "Silah\nEnvanter", "Våpen\nInventar", "Fegyver\nInventár", "Aseet\nInventaario", "Vũ khí\nKho", "Broń\nEkwipunek", "Arme\nInventar"},
    {"WPN", "武器", "WPN", "ARM", "ARM", "ARM", "WPN", "ARM", "ОРУ", "武器", "무기", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN", "WPN"},
    {"BMB", "ボム", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "炸弹", "폭탄", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB", "BMB"},
    {"points", "得点", "Punkte", "puntos", "points", "punti", "punten", "pontos", "очки", "得分", "점수", "نقاط", "poin", "очки", "πόντοι", "poäng", "คะแนน", "body", "point", "puan", "poeng", "pont", "pisteet", "điểm", "punkty", "puncte"},
    {"Bombs", "ボム", "Bomben", "Bombas", "Bombes", "Bombe", "Bommen", "Bombas", "Бомбы", "炸弹", "폭탄", "قنابل", "Bom", "Бомби", "Βόμβες", "Bomber", "ระเบิด", "Bomby", "Bomber", "Bombalar", "Bomber", "Bombák", "Pommit", "Bom", "Bomby", "Bombe"},

    // OSD color labels
    {"Enable OSD Color Patch", "OSD色パッチを有効化", "OSD-Farbpatch aktivieren", "Activar parche de color OSD", "Activer le patch de couleur OSD", "Abilita patch colore OSD", "OSD-kleurpatch inschakelen", "Ativar patch de cor OSD", "Включить патч цвета OSD", "启用 OSD 颜色补丁", "OSD 색상 패치 활성화", "تمكين تصحيح لون OSD", "Aktifkan patch warna OSD", "Увімкнути патч кольору OSD", "Ενεργοποίηση patch χρώματος OSD", "Aktivera OSD-färgpatch", "เปิดใช้ OSD Color Patch", "Povolit patch barvy OSD", "Aktivér OSD-farvepatch", "OSD renk yamasını etkinleştir", "Aktiver OSD-fargepatch", "OSD szín patch engedélyezése", "Ota OSD-väripaikka käyttöön", "Bật patch màu OSD", "Włącz łatkę koloru OSD", "Activează patch culoare OSD"},
    {"Global Color", "全体色", "Globale Farbe", "Color global", "Couleur globale", "Colore globale", "Globale kleur", "Cor global", "Глобальный цвет", "全局颜色", "전역 색상", "اللون العام", "Warna global", "Глобальний колір", "Καθολικό χρώμα", "Global färg", "สีทั่วไป", "Globální barva", "Global farve", "Genel renk", "Global farge", "Globális szín", "Yleinen väri", "Màu toàn cục", "Kolor globalny", "Culoare globală"},
    {"Use Global Color for All", "すべてに全体色を使用", "Globale Farbe für alle verwenden", "Usar color global para todo", "Utiliser la couleur globale pour tout", "Usa colore globale per tutti", "Globale kleur voor alles gebruiken", "Usar cor global para tudo", "Использовать глобальный цвет для всех", "对所有项使用全局颜色", "모두에 전역 색상 사용", "استخدام اللون العام للجميع", "Gunakan Warna Global untuk Semua", "Використовувати глобальний колір для всіх", "Χρήση καθολικού χρώματος για όλα", "Använd global färg för alla", "ใช้สีทั่วไปสำหรับทั้งหมด", "Použít globální barvu pro vše", "Brug global farve for alle", "Tümü için genel rengi kullan", "Bruk global farge for alle", "Globális szín használata mindenhez", "Käytä yleistä väriä kaikille", "Dùng màu toàn cục cho tất cả", "Użyj koloru globalnego dla wszystkich", "Folosește culoarea globală pentru toate"},
    {"Enable Separate Color", "個別色を有効化", "Separate Farbe aktivieren", "Activar color separado", "Activer une couleur séparée", "Abilita colore separato", "Aparte kleur inschakelen", "Ativar cor separada", "Включить отдельный цвет", "启用单独颜色", "별도 색상 활성화", "تفعيل لون منفصل", "Aktifkan Warna Terpisah", "Увімкнути окремий колір", "Ενεργοποίηση ξεχωριστού χρώματος", "Aktivera separat färg", "เปิดใช้สีแยก", "Povolit samostatnou barvu", "Aktivér separat farve", "Ayrı rengi etkinleştir", "Aktiver separat farge", "Külön szín engedélyezése", "Ota erillinen väri käyttöön", "Bật màu riêng", "Włącz oddzielny kolor", "Activează culoare separată"},
    {"Color (Default: Red)", "色 (既定: 赤)", "Farbe (Standard: Rot)", "Color (predeterminado: rojo)", "Couleur (par défaut : rouge)", "Colore (predefinito: rosso)", "Kleur (standaard: rood)", "Cor (padrão: vermelho)", "Цвет (по умолчанию: красный)", "颜色（默认：红）", "색상 (기본값: 빨강)", "لون (افتراضي: أحمر)", "Warna (default: Merah)", "Колір (за замовч.: червоний)", "Χρώμα (προεπιλογή: κόκκινο)", "Färg (standard: röd)", "สี (ค่าเริ่มต้น: แดง)", "Barva (výchozí: červená)", "Farve (standard: rød)", "Renk (varsayılan: kırmızı)", "Farge (standard: rød)", "Szín (alapértelmezett: piros)", "Väri (oletus: punainen)", "Màu (mặc định: đỏ)", "Kolor (domyślnie: czerwony)", "Culoare (implicit: roșu)"},
    {"Node Stolen (H211)", "ノード奪取 (H211)", "Knoten gestohlen (H211)", "Nodo robado (H211)", "Nœud volé (H211)", "Nodo rubato (H211)", "Knooppunt gestolen (H211)", "Nó roubado (H211)", "Узел захвачен (H211)", "节点被夺 (H211)", "노드 탈취 (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)", "Node Stolen (H211)"},
    {"Lost Lives", "ライフ喪失", "Verlorene Leben", "Vidas perdidas", "Vies perdues", "Vite perse", "Verloren levens", "Vidas perdidas", "Потерянные жизни", "失去生命", "잃은 목숨", "أرواح مفقودة", "Nyawa hilang", "Втрачені життя", "Χαμένες ζωές", "Förlorade liv", "ชีวิตที่เสีย", "Ztracené životy", "Tabte liv", "Kaybedilen canlar", "Tapte liv", "Elvesztett életek", "Menetetyt elämät", "Mạng mất", "Utracone życia", "Vieți pierdute"},
    {"Kill / Death", "キル / デス", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "击杀 / 死亡", "킬 / 데스", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death", "Kill / Death"},
    {"Return to Base", "基地へ戻れ", "Zur Basis zurück", "Volver a la base", "Retour à la base", "Ritorna alla base", "Terug naar basis", "Retornar à base", "Вернуться на базу", "返回基地", "기지로 복귀", "العودة إلى القاعدة", "Kembali ke Basis", "Повернутися на базу", "Επιστροφή στη βάση", "Återvänd till bas", "กลับฐาน", "Návrat na základnu", "Returner til base", "Üsse dön", "Returner til base", "Visszatérés a bázisra", "Palaa tukikohtaan", "Quay về căn cứ", "Powrót do bazy", "Înapoi la bază"},
    {"No Ammo", "弾薬なし", "Keine Munition", "Sin munición", "Plus de munitions", "Nessuna munizione", "Geen munitie", "Sem munição", "Нет боеприпасов", "无弹药", "탄약 없음", "لا ذخيرة", "Tidak ada amunisi", "Немає боєприпасів", "Χωρίς πυρομαχικά", "Ingen ammo", "ไม่มีกระสุน", "Žádná munice", "Ingen ammo", "Cephane yok", "Ingen ammo", "Nincs lőszer", "Ei ammoja", "Hết đạn", "Brak amunicji", "Fără muniție"},
    {"Coward Detect", "臆病者検出", "Feigheitserkennung", "Detección de cobardía", "Détection de lâcheté", "Rilevamento codardo", "Lafaarddetectie", "Detecção de covarde", "Обнаружение труса", "懦夫检测", "겁쟁이 감지", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect", "Coward Detect"},
    {"Acquiring Node", "ノード取得中", "Knoten wird erobert", "Capturando nodo", "Acquisition de nœud", "Acquisizione nodo", "Knooppunt veroveren", "Capturando nó", "Захват узла", "占领节点中", "노드 점령 중", "الاستحواذ على العقدة", "Mengambil node", "Захоплення вузла", "Απόκτηση κόμβου", "Tar över nod", "ยึดโหนด", "Získávání uzlu", "Overtager node", "Düğüm ele geçiriliyor", "Tar over node", "Csomópont megszerzése", "Solmun valtaus", "Chiếm node", "Przejmowanie węzła", "Achiziționare nod"},
    {"Turret", "タレット", "Geschütz", "Torreta", "Tourelle", "Torretta", "Turret", "Torreta", "Турель", "炮塔", "터렛", "برج", "Menara", "Турель", "Πύργος", "Torn", "ป้อม", "Věž", "Tårn", "Taret", "Tårn", "Torony", "Torni", "Tháp pháo", "Wieżyczka", "Turetă"},
    {"Octo Reset", "オクトリスリセット", "Octo-Reset", "Reinicio Octo", "Réinitialisation Octo", "Reset Octo", "Octo-reset", "Redefinir Octo", "Сброс Octo", "Octo 重置", "Octo 리셋", "Octo Reset", "Reset Octo", "Скинути Octo", "Octo Reset", "Octo-återställning", "Octo Reset", "Reset Octo", "Octo-nulstilling", "Octo Sıfırla", "Octo-tilbakestilling", "Octo visszaállítás", "Octo-nollaus", "Octo Reset", "Reset Octo", "Reset Octo"},
    {"Octo Drop", "オクトリスドロップ", "Oktolith-Drop", "Caída de octolito", "Chute d'octolithe", "Drop octoliti", "Octoliet-drop", "Queda de octolito", "Выпадение октолита", "Octolith 掉落", "옥톨리스 드롭", "سقوط Octolith", "Drop Octolith", "Випадення Octolith", "Πτώση Octolith", "Octolith-drop", "ดรอป Octolith", "Drop Octolith", "Octolith-drop", "Octolith düşüşü", "Octolith-drop", "Octolith drop", "Octolith-pudotus", "Rơi Octolith", "Drop Octolith", "Drop Octolith"},
    {"Octo Condition", "オクトリス条件", "Octo-Bedingung", "Condición Octo", "Condition Octo", "Condizione Octo", "Octo-voorwaarde", "Condição Octo", "Условие Octo", "Octo 条件", "Octo 조건", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition", "Octo Condition"},
    {"Octo Missing", "オクトリス未所持", "Oktolith fehlt", "Octólito ausente", "Octolithe manquant", "Octolito mancante", "Octoliet ontbreekt", "Octólito ausente", "Октолит отсутствует", "八边形缺失", "옥토리스 미보유", "أوكتوليث مفقود", "Octolith hilang", "Октоліт відсутній", "Λείπει Octolith", "Octolith saknas", "ไม่มี Octolith", "Chybí Octolith", "Octolith mangler", "Octolith eksik", "Octolith mangler", "Hiányzó Octolith", "Octolith puuttuu", "Thiếu Octolith", "Brak Octolith", "Octolith lipsă"},
    {"Slot: Kill / Death  [flags=0x02]", "スロット: キル / デス [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot : Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Слот: Kill / Death  [flags=0x02]", "槽位：击杀 / 死亡 [flags=0x02]", "슬롯: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]"},
    {"Slot: Node Capture  [flags=0x11]", "スロット: ノード取得 [flags=0x11]", "Slot: Knotenerfassung  [flags=0x11]", "Ranura: Captura de nodo  [flags=0x11]", "Emplacement : Capture de nœud  [flags=0x11]", "Slot: Cattura nodo  [flags=0x11]", "Slot: Knooppunt vastleggen  [flags=0x11]", "Slot: Captura de nó  [flags=0x11]", "Слот: Захват узла  [flags=0x11]", "槽位: 节点占领 [flags=0x11]", "슬롯: 노드 점령 [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Слот: Захоплення вузла  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]", "Slot: Node Capture  [flags=0x11]"},
    {"Slot: Objective     [flags=0x01]", "スロット: 目標 [flags=0x01]", "Slot: Ziel     [flags=0x01]", "Ranura: Objetivo     [flags=0x01]", "Emplacement : Objectif     [flags=0x01]", "Slot: Obiettivo     [flags=0x01]", "Slot: Doel     [flags=0x01]", "Slot: Objetivo     [flags=0x01]", "Слот: Цель     [flags=0x01]", "槽位：目标     [flags=0x01]", "슬롯: 목표     [flags=0x01]", "فتحة: الهدف     [flags=0x01]", "Slot: Objektif     [flags=0x01]", "Слот: Ціль     [flags=0x01]", "Θύρα: Στόχος     [flags=0x01]", "Plats: Mål     [flags=0x01]", "สล็อต: เป้าหมาย     [flags=0x01]", "Slot: Cíl     [flags=0x01]", "Slot: Mål     [flags=0x01]", "Yuva: Hedef     [flags=0x01]", "Spor: Mål     [flags=0x01]", "Slot: Cél     [flags=0x01]", "Paikka: Tavoite     [flags=0x01]", "Slot: Mục tiêu     [flags=0x01]", "Slot: Cel     [flags=0x01]", "Slot: Obiectiv     [flags=0x01]"},
    {"Slot: System / Misc [flags=0x00]", "スロット: システム/その他 [flags=0x00]", "Slot: System / Sonstiges [flags=0x00]", "Ranura: Sistema / Varios [flags=0x00]", "Emplacement : Système / Divers [flags=0x00]", "Slot: Sistema / Varie [flags=0x00]", "Slot: Systeem / Overig [flags=0x00]", "Slot: Sistema / Diversos [flags=0x00]", "Слот: Система / Прочее [flags=0x00]", "槽位：系统 / 其他 [flags=0x00]", "슬롯: 시스템 / 기타 [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]", "Slot: System / Misc [flags=0x00]"},
    // OSD slot description labels (multi-line)
    {"Applied once on settings close to currently displayed messages (flags=0x02).\nNew messages use the 'Kill / Death' literal color above.", "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x02)。\n新しいメッセージは上の「キル / デス」個別色を使用します。", "Einmal beim Schließen der Einstellungen auf aktuell angezeigte Nachrichten angewendet (flags=0x02).\nNeue Nachrichten verwenden die obige Farbe für „Kill / Death“.", "Se aplica una vez al cerrar los ajustes a los mensajes mostrados (flags=0x02).\nLos mensajes nuevos usan el color literal «Kill / Death» de arriba.", "Appliqué une fois à la fermeture des paramètres aux messages affichés (flags=0x02).\nLes nouveaux messages utilisent la couleur littérale « Kill / Death » ci-dessus.", "Applicato una volta alla chiusura delle impostazioni ai messaggi attualmente visualizzati (flags=0x02).\nI nuovi messaggi usano il colore letterale «Kill / Death» sopra.", "Eenmalig toegepast bij sluiten van instellingen op momenteel weergegeven berichten (flags=0x02).\nNieuwe berichten gebruiken de letterlijke kleur «Kill / Death» hierboven.", "Aplicado uma vez ao fechar as configurações às mensagens exibidas (flags=0x02).\nNovas mensagens usam a cor literal «Kill / Death» acima.", "Применяется один раз при закрытии настроек к текущим сообщениям (flags=0x02).\nНовые сообщения используют буквальный цвет «Kill / Death» выше.", "关闭设置时对当前显示的消息应用一次 (flags=0x02)。\n新消息使用上方的「Kill / Death」字面颜色。", "설정을 닫을 때 현재 표시된 메시지에 한 번 적용됩니다 (flags=0x02).\n새 메시지는 위의 «Kill / Death» 리터럴 색상을 사용합니다.", "يُطبَّق مرة واحدة عند إغلاق الإعدادات على الرسائل المعروضة حالياً (flags=0x02).\nتستخدم الرسائل الجديدة لون «Kill / Death» الحرفي أعلاه.", "Diterapkan sekali saat menutup pengaturan ke pesan yang ditampilkan (flags=0x02).\nPesan baru menggunakan warna literal «Kill / Death» di atas.", "Застосовується один раз при закритті налаштувань до поточних повідомлень (flags=0x02).\nНові повідомлення використовують буквальний колір «Kill / Death» вище.", "Εφαρμόζεται μία φορά στο κλείσιμο ρυθμίσεων στα τρέχοντα μηνύματα (flags=0x02).\nΤα νέα μηνύματα χρησιμοποιούν το κυριολεκτικό χρώμα «Kill / Death» παραπάνω.", "Tillämpas en gång vid stängning av inställningar på aktuella meddelanden (flags=0x02).\nNya meddelanden använder den bokstavliga färgen «Kill / Death» ovan.", "ใช้ครั้งเดียวเมื่อปิดการตั้งค่ากับข้อความที่แสดง (flags=0x02)\nข้อความใหม่ใช้สีตัวอักษร «Kill / Death» ด้านบน", "Použije se jednou při zavření nastavení na aktuálně zobrazené zprávy (flags=0x02).\nNové zprávy používají doslovnou barvu «Kill / Death» výše.", "Anvendes én gang ved lukning af indstillinger på viste meddelelser (flags=0x02).\nNye meddelelser bruger den bogstavelige farve «Kill / Death» ovenfor.", "Ayarlar kapatıldığında görüntülenen mesajlara bir kez uygulanır (flags=0x02).\nYeni mesajlar yukarıdaki «Kill / Death» harfi rengini kullanır.", "Brukes én gang ved lukking av innstillinger på viste meldinger (flags=0x02).\nNye meldinger bruker den bokstavelige fargen «Kill / Death» ovenfor.", "Egyszer alkalmazza a beállítások bezárásakor a jelenleg megjelenített üzenetekre (flags=0x02).\nAz új üzenetek a fenti «Kill / Death» szó szerinti színt használják.", "Sovelletaan kerran asetuksia suljettaessa näytössä oleviin viesteihin (flags=0x02).\nUudet viestit käyttävät yllä olevaa «Kill / Death» -kirjainväriä.", "Áp dụng một lần khi đóng cài đặt cho tin nhắn đang hiển thị (flags=0x02).\nTin nhắn mới dùng màu chữ «Kill / Death» ở trên.", "Stosowane raz po zamknięciu ustawień do aktualnie wyświetlanych wiadomości (flags=0x02).\nNowe wiadomości używają dosłownego koloru «Kill / Death» powyżej.", "Se aplică o dată la închiderea setărilor mesajelor afișate (flags=0x02).\nMesajele noi folosesc culoarea literală «Kill / Death» de mai sus."},
    {"Applied once on settings close to currently displayed messages (flags=0x11).\nNew messages use 'Acquiring Node' or 'Node Stolen' literal colors above.", "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x11)。\n新しいメッセージは上の「ノード取得中」または「ノード奪取」の個別色を使用します。", "Einmal beim Schließen der Einstellungen auf aktuell angezeigte Meldungen angewendet (flags=0x11).\nNeue Meldungen verwenden die Literalfarben „Knoten wird erobert“ oder „Knoten gestohlen“ oben.", "Se aplica una vez al cerrar los ajustes a los mensajes mostrados (flags=0x11).\nLos mensajes nuevos usan los colores literales «Adquiriendo nodo» o «Nodo robado» de arriba.", "Appliqué une fois à la fermeture des paramètres aux messages affichés (flags=0x11).\nLes nouveaux messages utilisent les couleurs littérales « Acquisition du nœud » ou « Nœud volé » ci-dessus.", "Applicato una volta alla chiusura delle impostazioni ai messaggi attualmente visualizzati (flags=0x11).\nI nuovi messaggi usano i colori letterali «Acquisizione nodo» o «Nodo rubato» sopra.", "Eenmalig toegepast bij sluiten van instellingen op momenteel weergegeven berichten (flags=0x11).\nNieuwe berichten gebruiken de letterlijke kleuren «Knooppunt veroveren» of «Knooppunt gestolen» hierboven.", "Aplicado uma vez ao fechar as configurações às mensagens exibidas (flags=0x11).\nNovas mensagens usam as cores literais «Adquirindo nó» ou «Nó roubado» acima.", "Применяется один раз при закрытии настроек к текущим сообщениям (flags=0x11).\nНовые сообщения используют буквальные цвета «Захват узла» или «Узел украден» выше.", "关闭设置时对当前显示的消息应用一次 (flags=0x11)。\n新消息使用上方「正在占领节点」或「节点被夺」的字面颜色。", "설정을 닫을 때 현재 표시 중인 메시지에 한 번 적용됩니다 (flags=0x11).\n새 메시지는 위의 '노드 점령 중' 또는 '노드 탈취' 리터럴 색상을 사용합니다.", "يُطبَّق مرة واحدة عند إغلاق الإعدادات على الرسائل المعروضة حالياً (flags=0x11).\nالرسائل الجديدة تستخدم ألوان «Acquiring Node» أو «Node Stolen» أعلاه.", "Diterapkan sekali saat menutup pengaturan ke pesan yang ditampilkan (flags=0x11).\nPesan baru menggunakan warna literal 'Acquiring Node' atau 'Node Stolen' di atas.", "Застосовується раз при закритті налаштувань до поточних повідомлень (flags=0x11).\nНові повідомлення використовують літеральні кольори «Acquiring Node» або «Node Stolen» вище.", "Εφαρμόζεται μία φορά στο κλείσιμο ρυθμίσεων στα τρέχοντα μηνύματα (flags=0x11).\nΤα νέα μηνύματα χρησιμοποιούν τα κυριολεκτικά χρώματα «Acquiring Node» ή «Node Stolen» παραπάνω.", "Tillämpas en gång vid stängning av inställningar på visade meddelanden (flags=0x11).\nNya meddelanden använder bokstavliga färgerna «Acquiring Node» eller «Node Stolen» ovan.", "ใช้ครั้งเดียวเมื่อปิดการตั้งค่ากับข้อความที่แสดง (flags=0x11)\nข้อความใหม่ใช้สี literal 'Acquiring Node' หรือ 'Node Stolen' ด้านบน", "Použito jednou při zavření nastavení na aktuálně zobrazené zprávy (flags=0x11).\nNové zprávy používají doslovné barvy «Acquiring Node» nebo «Node Stolen» výše.", "Anvendes én gang ved lukning af indstillinger på viste meddelelser (flags=0x11).\nNye meddelelser bruger bogstavelige farver «Acquiring Node» eller «Node Stolen» ovenfor.", "Ayarlar kapatıldığında görüntülenen mesajlara bir kez uygulanır (flags=0x11).\nYeni mesajlar yukarıdaki «Acquiring Node» veya «Node Stolen» literal renklerini kullanır.", "Brukes én gang ved lukking av innstillinger på viste meldinger (flags=0x11).\nNye meldinger bruker bokstavelige farger «Acquiring Node» eller «Node Stolen» ovenfor.", "Egyszer alkalmazódik a beállítások bezárásakor a megjelenített üzenetekre (flags=0x11).\nAz új üzenetek a fenti «Acquiring Node» vagy «Node Stolen» szó szerinti színeket használják.", "Käytetään kerran asetusten sulkeutuessa näytetyille viesteille (flags=0x11).\nUudet viestit käyttävät yllä olevia «Acquiring Node»- tai «Node Stolen» -literaalivärejä.", "Áp dụng một lần khi đóng cài đặt cho thông báo đang hiển thị (flags=0x11).\nThông báo mới dùng màu literal 'Acquiring Node' hoặc 'Node Stolen' ở trên.", "Stosowane raz przy zamknięciu ustawień do wyświetlanych wiadomości (flags=0x11).\nNowe wiadomości używają dosłownych kolorów «Acquiring Node» lub «Node Stolen» powyżej.", "Aplicat o dată la închiderea setărilor mesajelor afișate (flags=0x11).\nMesajele noi folosesc culorile literale «Acquiring Node» sau «Node Stolen» de mai sus."},
    {"Applied once on settings close to currently displayed messages (flags=0x01).\nNew messages use their individual literal colors above (No Ammo / Return to Base / Octo ...).", "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x01)。\n新しいメッセージはそれぞれの個別色を使用します (弾薬なし / 基地へ戻れ / オクト系 ...)。", "Einmal beim Schließen der Einstellungen auf aktuell angezeigte Meldungen angewendet (flags=0x01).\nNeue Meldungen verwenden ihre individuellen Farben oben (Keine Munition / Zur Basis zurück / Octo ...).", "Se aplica una vez al cerrar los ajustes a los mensajes mostrados (flags=0x01).\nLos mensajes nuevos usan sus colores individuales arriba (Sin munición / Volver a la base / Octo ...).", "Appliqué une fois à la fermeture des paramètres aux messages affichés (flags=0x01).\nLes nouveaux messages utilisent leurs couleurs individuelles ci-dessus (Plus de munitions / Retour à la base / Octo ...).", "Applicato una volta alla chiusura delle impostazioni ai messaggi attualmente visualizzati (flags=0x01).\nI nuovi messaggi usano i colori letterali individuali sopra (Nessuna munizione / Ritorna alla base / Octo ...).", "Eenmalig toegepast bij sluiten van instellingen op momenteel weergegeven berichten (flags=0x01).\nNieuwe berichten gebruiken hun individuele kleuren hierboven (Geen munitie / Terug naar basis / Octo ...).", "Aplicado uma vez ao fechar as configurações às mensagens exibidas (flags=0x01).\nNovas mensagens usam suas cores individuais acima (Sem munição / Retornar à base / Octo ...).", "Применяется один раз при закрытии настроек к текущим сообщениям (flags=0x01).\nНовые сообщения используют свои индивидуальные цвета выше (Нет боеприпасов / Вернуться на базу / Octo ...).", "关闭设置时对当前显示的消息应用一次 (flags=0x01)。\n新消息使用上方各自的字面颜色 (无弹药 / 返回基地 / Octo ...)。", "설정을 닫을 때 현재 표시 중인 메시지에 한 번 적용됩니다 (flags=0x01).\n새 메시지는 위의 개별 색상을 사용합니다 (탄약 없음 / 기지로 복귀 / Octo ...).", "يُطبّق مرة واحدة عند إغلاق الإعدادات على الرسائل المعروضة حاليًا (flags=0x01).\nالرسائل الجديدة تستخدم ألوانها الحرفية الفردية أعلاه (No Ammo / Return to Base / Octo ...).", "Diterapkan sekali saat pengaturan ditutup ke pesan yang ditampilkan saat ini (flags=0x01).\nPesan baru menggunakan warna literal individual di atas (No Ammo / Return to Base / Octo ...).", "Застосовується один раз при закритті налаштувань до поточних повідомлень (flags=0x01).\nНові повідомлення використовують свої індивідуальні кольори вище (No Ammo / Return to Base / Octo ...).", "Εφαρμόζεται μία φορά κατά το κλείσιμο ρυθμίσεων στα τρέχοντα μηνύματα (flags=0x01).\nΤα νέα μηνύματα χρησιμοποιούν τα ατομικά χρώματά τους παραπάνω (No Ammo / Return to Base / Octo ...).", "Tillämpas en gång vid stängning av inställningar på aktuellt visade meddelanden (flags=0x01).\nNya meddelanden använder sina individuella färger ovan (No Ammo / Return to Base / Octo ...).", "ใช้ครั้งเดียวเมื่อปิดการตั้งค่ากับข้อความที่แสดงอยู่ (flags=0x01).\nข้อความใหม่ใช้สีเฉพาะด้านบน (No Ammo / Return to Base / Octo ...).", "Použito jednou při zavření nastavení na aktuálně zobrazené zprávy (flags=0x01).\nNové zprávy používají své individuální barvy výše (No Ammo / Return to Base / Octo ...).", "Anvendes én gang ved lukning af indstillinger på aktuelt viste meddelelser (flags=0x01).\nNye meddelelser bruger deres individuelle farver ovenfor (No Ammo / Return to Base / Octo ...).", "Ayarlar kapatıldığında mevcut mesajlara bir kez uygulanır (flags=0x01).\nYeni mesajlar yukarıdaki bireysel renklerini kullanır (No Ammo / Return to Base / Octo ...).", "Brukes én gang ved lukking av innstillinger på viste meldinger (flags=0x01).\nNye meldinger bruker sine individuelle farger over (No Ammo / Return to Base / Octo ...).", "Egyszer alkalmazódik a beállítások bezárásakor a jelenleg megjelenített üzenetekre (flags=0x01).\nAz új üzenetek a fenti egyéni színeiket használják (No Ammo / Return to Base / Octo ...).", "Sovelletaan kerran asetusten sulkeutuessa näytössä oleviin viesteihin (flags=0x01).\nUudet viestit käyttävät yllä olevia yksilöllisiä värejään (No Ammo / Return to Base / Octo ...).", "Áp dụng một lần khi đóng cài đặt cho các thông báo hiện tại (flags=0x01).\nThông báo mới dùng màu riêng ở trên (No Ammo / Return to Base / Octo ...).", "Stosowane raz po zamknięciu ustawień do aktualnie wyświetlanych wiadomości (flags=0x01).\nNowe wiadomości używają swoich indywidualnych kolorów powyżej (No Ammo / Return to Base / Octo ...).", "Aplicat o dată la închiderea setărilor mesajelor afișate (flags=0x01).\nMesajele noi folosesc culorile individuale de mai sus (No Ammo / Return to Base / Octo ...)."},
    {"Applied once on settings close to currently displayed messages (flags=0x00).\nNew messages use their individual literal colors above (Lost Lives / Coward Detect / Turret ...).\nNote: HEADSHOT! (H228) is flags=0x00, not 0x02.", "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x00)。\n新しいメッセージはそれぞれの個別色を使用します (ライフ喪失 / 臆病者検出 / タレット ...)。\n注: HEADSHOT! (H228) は flags=0x00 で、0x02 ではありません。", "Wird einmal beim Schließen der Einstellungen auf aktuell angezeigte Nachrichten angewendet (flags=0x00).\nNeue Nachrichten verwenden ihre individuellen Farben oben (Verlorene Leben / Feigling erkannt / Turm ...).\nHinweis: HEADSHOT! (H228) ist flags=0x00, nicht 0x02.", "Se aplica una vez al cerrar los ajustes a los mensajes mostrados actualmente (flags=0x00).\nLos mensajes nuevos usan sus colores literales individuales arriba (Vidas perdidas / Cobarde detectado / Torreta ...).\nNota: HEADSHOT! (H228) es flags=0x00, no 0x02.", "Appliqué une fois à la fermeture des paramètres aux messages actuellement affichés (flags=0x00).\nLes nouveaux messages utilisent leurs couleurs littérales individuelles ci-dessus (Vies perdues / Lâche détecté / Tourelle ...).\nNote : HEADSHOT! (H228) est flags=0x00, pas 0x02.", "Applicato una volta alla chiusura delle impostazioni ai messaggi attualmente visualizzati (flags=0x00).\nI nuovi messaggi usano i colori letterali individuali sopra (Vite perse / Codardo rilevato / Torretta ...).\nNota: HEADSHOT! (H228) è flags=0x00, non 0x02.", "Eenmalig toegepast bij sluiten instellingen op momenteel getoonde berichten (flags=0x00).\nNieuwe berichten gebruiken hun individuele letterlijke kleuren hierboven (Verloren levens / Lafaard gedetecteerd / Turret ...).\nOpmerking: HEADSHOT! (H228) is flags=0x00, niet 0x02.", "Aplicado uma vez ao fechar as configurações às mensagens exibidas atualmente (flags=0x00).\nNovas mensagens usam suas cores literais individuais acima (Vidas perdidas / Covarde detectado / Torreta ...).\nNota: HEADSHOT! (H228) é flags=0x00, não 0x02.", "Применяется один раз при закрытии настроек к текущим сообщениям (flags=0x00).\nНовые сообщения используют индивидуальные цвета выше (Потерянные жизни / Трус обнаружен / Турель ...).\nПримечание: HEADSHOT! (H228) — flags=0x00, а не 0x02.", "关闭设置时对当前显示的消息应用一次（flags=0x00）。\n新消息使用上方各自的字面颜色（失去生命 / 检测到懦夫 / 炮塔 ...）。\n注意：HEADSHOT! (H228) 为 flags=0x00，而非 0x02。", "설정을 닫을 때 현재 표시 중인 메시지에 한 번 적용됩니다 (flags=0x00).\n새 메시지는 위의 개별 리터럴 색상을 사용합니다 (잃은 목숨 / 겁쟁이 감지 / 터렛 ...).\n참고: HEADSHOT! (H228)은 flags=0x00이며 0x02가 아닙니다.", "يُطبَّق مرة واحدة عند إغلاق الإعدادات على الرسائل المعروضة حاليًا (flags=0x00).\nتستخدم الرسائل الجديدة ألوانها الحرفية الفردية أعلاه (Lost Lives / Coward Detect / Turret ...).\nملاحظة: HEADSHOT! (H228) هو flags=0x00، وليس 0x02.", "Diterapkan sekali saat menutup pengaturan ke pesan yang ditampilkan (flags=0x00).\nPesan baru menggunakan warna literal individual di atas (Lost Lives / Coward Detect / Turret ...).\nCatatan: HEADSHOT! (H228) adalah flags=0x00, bukan 0x02.", "Застосовується один раз при закритті налаштувань до поточних повідомлень (flags=0x00).\nНові повідомлення використовують індивідуальні кольори вище (Lost Lives / Coward Detect / Turret ...).\nПримітка: HEADSHOT! (H228) — flags=0x00, а не 0x02.", "Εφαρμόζεται μία φορά στο κλείσιμο ρυθμίσεων στα τρέχοντα μηνύματα (flags=0x00).\nΤα νέα μηνύματα χρησιμοποιούν τα ατομικά χρώματά τους παραπάνω (Lost Lives / Coward Detect / Turret ...).\nΣημ.: HEADSHOT! (H228) είναι flags=0x00, όχι 0x02.", "Tillämpas en gång vid stängning av inställningar på aktuella meddelanden (flags=0x00).\nNya meddelanden använder sina individuella färger ovan (Lost Lives / Coward Detect / Turret ...).\nObs: HEADSHOT! (H228) är flags=0x00, inte 0x02.", "ใช้ครั้งเดียวเมื่อปิดการตั้งค่ากับข้อความที่แสดง (flags=0x00).\nข้อความใหม่ใช้สีเฉพาะด้านบน (Lost Lives / Coward Detect / Turret ...)\nหมายเหตุ: HEADSHOT! (H228) คือ flags=0x00 ไม่ใช่ 0x02", "Použito jednou při zavření nastavení na aktuální zprávy (flags=0x00).\nNové zprávy používají individuální barvy výše (Lost Lives / Coward Detect / Turret ...).\nPozn.: HEADSHOT! (H228) je flags=0x00, ne 0x02.", "Anvendes én gang ved lukning af indstillinger på viste meddelelser (flags=0x00).\nNye meddelelser bruger deres individuelle farver ovenfor (Lost Lives / Coward Detect / Turret ...).\nBemærk: HEADSHOT! (H228) er flags=0x00, ikke 0x02.", "Ayarlar kapatıldığında mevcut mesajlara bir kez uygulanır (flags=0x00).\nYeni mesajlar yukarıdaki bireysel renklerini kullanır (Lost Lives / Coward Detect / Turret ...).\nNot: HEADSHOT! (H228) flags=0x00'dır, 0x02 değil.", "Brukes én gang ved lukking av innstillinger på viste meldinger (flags=0x00).\nNye meldinger bruker individuelle farger over (Lost Lives / Coward Detect / Turret ...).\nMerk: HEADSHOT! (H228) er flags=0x00, ikke 0x02.", "Egyszer alkalmazódik a beállítások bezárásakor a jelenlegi üzenetekre (flags=0x00).\nAz új üzenetek a fenti egyedi színeket használják (Lost Lives / Coward Detect / Turret ...).\nMegj.: HEADSHOT! (H228) flags=0x00, nem 0x02.", "Sovelletaan kerran asetusten sulkemisen yhteydessä nykyisiin viesteihin (flags=0x00).\nUudet viestit käyttävät yllä olevia yksilöllisiä värejään (Lost Lives / Coward Detect / Turret ...).\nHuom: HEADSHOT! (H228) on flags=0x00, ei 0x02.", "Áp dụng một lần khi đóng cài đặt cho tin nhắn hiện tại (flags=0x00).\nTin mới dùng màu riêng ở trên (Lost Lives / Coward Detect / Turret ...).\nLưu ý: HEADSHOT! (H228) là flags=0x00, không phải 0x02.", "Stosowane raz przy zamknięciu ustawień do bieżących wiadomości (flags=0x00).\nNowe wiadomości używają indywidualnych kolorów powyżej (Lost Lives / Coward Detect / Turret ...).\nUwaga: HEADSHOT! (H228) to flags=0x00, nie 0x02.", "Aplicat o dată la închiderea setărilor mesajelor afișate (flags=0x00).\nMesajele noi folosesc culorile individuale de mai sus (Lost Lives / Coward Detect / Turret ...).\nNotă: HEADSHOT! (H228) este flags=0x00, nu 0x02."},
    // OSD slot color labels
    {"Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "色  (YOU KILLED / KILLED YOU / 5キル / プライムハンター / 味方)", "Farbe  (DU HAST GETÖTET / HAT DICH GETÖTET / 5-Kill / Prime Hunter / Teamkamerad)", "Color  (TÚ MATASTE / TE MATÓ / 5 bajas / prime hunter / compañero)", "Couleur  (VOUS AVEZ TUÉ / VOUS A TUÉ / 5 kills / prime hunter / coéquipier)", "Colore  (HAI UCCISO / TI HA UCCISO / 5-kill / prime hunter / compagno)", "Kleur  (JIJ DODDE / DODDE JOU / 5-kill / prime hunter / teamgenoot)", "Cor  (VOCÊ MATOU / MATOU VOCÊ / 5-kill / prime hunter / companheiro)", "Цвет  (ВЫ УБИЛИ / УБИЛ ВАС / 5-kill / prime hunter / союзник)", "颜色  (YOU KILLED / KILLED YOU / 5 杀 / prime hunter / 队友)", "색상  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / 팀원)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)"},
    {"Color  (acquiring node / node stolen H211)", "色  (ノード取得中 / ノード奪取 H211)", "Farbe  (Knoten wird erobert / Knoten gestohlen H211)", "Color  (capturando nodo / nodo robado H211)", "Couleur  (acquisition de nœud / nœud volé H211)", "Colore  (acquisizione nodo / nodo rubato H211)", "Kleur  (knooppunt veroveren / knooppunt gestolen H211)", "Cor  (capturando nó / nó roubado H211)", "Цвет  (захват узла / узел украден H211)", "颜色  (占领节点 / 节点被夺 H211)", "색상  (노드 점령 중 / 노드 탈취 H211)", "اللون  (الاستحواذ على العقدة / سرقة العقدة H211)", "Warna  (mengambil node / node dicuri H211)", "Колір  (захоплення вузла / вузол викрадено H211)", "Χρώμα  (απόκτηση κόμβου / κλοπή κόμβου H211)", "Färg  (tar över nod / nod stulen H211)", "สี  (ยึดโหนด / โหนดถูกขโมย H211)", "Barva  (získávání uzlu / uzel ukraden H211)", "Farve  (overtager node / node stjålet H211)", "Renk  (düğüm ele geçiriliyor / düğüm çalındı H211)", "Farge  (tar over node / node stjålet H211)", "Szín  (csomópont megszerzése / csomópont ellopva H211)", "Väri  (solmun valtaus / solmu varastettu H211)", "Màu  (chiếm node / node bị cướp H211)", "Kolor  (przejmowanie węzła / węzeł skradziony H211)", "Culoare  (achiziționare nod / nod furat H211)"},
    {"Color  (AMMO DEPLETED / return to base / bounty / octolith events)", "色  (AMMO DEPLETED / 基地へ戻れ / バウンティ / オクトリスイベント)", "Farbe  (AMMO DEPLETED / Rückkehr zur Basis / Kopfgeld / Oktolith-Ereignisse)", "Color  (AMMO DEPLETED / volver a base / recompensa / eventos de octolito)", "Couleur  (AMMO DEPLETED / retour à la base / prime / événements octolithe)", "Colore  (AMMO DEPLETED / ritorno alla base / taglia / eventi octolito)", "Kleur  (AMMO DEPLETED / terug naar basis / premie / octoliet-gebeurtenissen)", "Cor  (AMMO DEPLETED / retorno à base / recompensa / eventos de octolito)", "Цвет  (AMMO DEPLETED / возврат на базу / награда / события октолита)", "颜色  (AMMO DEPLETED / 返回基地 / 赏金 / 八棱石事件)", "색상  (AMMO DEPLETED / 기지 복귀 / 현상금 / 옥토리스 이벤트)", "اللون  (AMMO DEPLETED / return to base / bounty / octolith events)", "Warna  (AMMO DEPLETED / return to base / bounty / octolith events)", "Колір  (AMMO DEPLETED / return to base / bounty / octolith events)", "Χρώμα  (AMMO DEPLETED / return to base / bounty / octolith events)", "Färg  (AMMO DEPLETED / return to base / bounty / octolith events)", "สี  (AMMO DEPLETED / return to base / bounty / octolith events)", "Barva  (AMMO DEPLETED / return to base / bounty / octolith events)", "Farve  (AMMO DEPLETED / return to base / bounty / octolith events)", "Renk  (AMMO DEPLETED / return to base / bounty / octolith events)", "Farge  (AMMO DEPLETED / return to base / bounty / octolith events)", "Szín  (AMMO DEPLETED / return to base / bounty / octolith events)", "Väri  (AMMO DEPLETED / return to base / bounty / octolith events)", "Màu  (AMMO DEPLETED / return to base / bounty / octolith events)", "Kolor  (AMMO DEPLETED / return to base / bounty / octolith events)", "Culoare  (AMMO DEPLETED / return to base / bounty / octolith events)"},
    {"Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "色  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / タレット)", "Farbe  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / Turm)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / torreta)", "Couleur  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / tourelle)", "Colore  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / torretta)", "Kleur  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Cor  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / torreta)", "Цвет  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / турель)", "颜色  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / 炮塔)", "색상  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / 터렛)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)"},

    // Color presets and weapon labels
    {"White", "白", "Weiß", "Blanco", "Blanc", "Bianco", "Wit", "Branco", "Белый", "白", "흰색", "أبيض", "Putih", "Білий", "Λευκό", "Vit", "ขาว", "Bílá", "Hvid", "Beyaz", "Hvit", "Fehér", "Valkoinen", "Trắng", "Biały", "Alb"},
    {"Green", "緑", "Grün", "Verde", "Vert", "Verde", "Groen", "Verde", "Зелёный", "绿色", "녹색", "أخضر", "Hijau", "Зелений", "Πράσινο", "Grön", "เขียว", "Zelená", "Grøn", "Yeşil", "Grønn", "Zöld", "Vihreä", "Xanh lá", "Zielony", "Verde"},
    {"Yellow Green", "黄緑", "Gelbgrün", "Verde amarillento", "Vert jaune", "Giallo-verde", "Geelgroen", "Verde-amarelado", "Жёлто-зелёный", "黄绿色", "황록색", "أخضر مصفر", "Hijau kekuningan", "Жовто-зелений", "Κιτρινοπράσινο", "Gulgrön", "เขียวเหลือง", "Žlutozelená", "Gulgrøn", "Sarı yeşil", "Gulgrønn", "Sárgászöld", "Keltavihreä", "Vàng xanh", "Żółtozielony", "Galben-verde"},
    {"Green Yellow", "緑黄", "Grüngelb", "Verde amarillo", "Jaune vert", "Giallo verde", "Geelgroen", "Verde amarelo", "Жёлто-зелёный", "绿黄", "녹황", "أخضر أصفر", "Hijau Kuning", "Зелено-жовтий", "Πράσινο κίτρινο", "Gröngul", "เขียวเหลือง", "Zeleně žlutá", "Grøngul", "Yeşil sarı", "Grønngul", "Zöldes sárga", "Vihreänkeltainen", "Xanh vàng", "Zielono-żółty", "Galben verzui"},
    {"Yellow", "黄", "Gelb", "Amarillo", "Jaune", "Giallo", "Geel", "Amarelo", "Жёлтый", "黄", "노랑", "أصفر", "Kuning", "Жовтий", "Κίτρινο", "Gul", "เหลือง", "Žlutá", "Gul", "Sarı", "Gul", "Sárga", "Keltainen", "Vàng", "Żółty", "Galben"},
    {"Pure Cyan", "純シアン", "Reines Cyan", "Cian puro", "Cyan pur", "Ciano puro", "Zuiver cyaan", "Ciano puro", "Чистый циан", "纯青", "순수 시안", "سماوي نقي", "Sian murni", "Чистий ціан", "Καθαρό κυανό", "Rent cyan", "ฟ้าบริสุทธิ์", "Čistý azurový", "Ren cyan", "Saf cyan", "Ren cyan", "Tiszta cián", "Puhdas syaani", "Xanh lơ thuần", "Czysty cyjan", "Cian pur"},
    {"Hud Cyan", "HUDシアン", "HUD-Cyan", "HUD cian", "HUD cyan", "HUD ciano", "HUD-cyaan", "HUD ciano", "HUD циан", "HUD 青色", "HUD 시안", "HUD سماوي", "HUD Cyan", "HUD ціан", "HUD κυανό", "HUD cyan", "HUD Cyan", "HUD cyan", "HUD cyan", "HUD cyan", "HUD cyan", "HUD cián", "HUD syaani", "HUD Cyan", "HUD cyan", "HUD cyan"},
    {"Pink", "ピンク", "Rosa", "Rosa", "Rose", "Rosa", "Roze", "Rosa", "Розовый", "粉色", "분홍", "وردي", "Merah muda", "Рожевий", "Ροζ", "Rosa", "ชมพู", "Růžová", "Pink", "Pembe", "Rosa", "Rózsaszín", "Vaaleanpunainen", "Hồng", "Różowy", "Roz"},
    {"Red", "赤", "Rot", "Rojo", "Rouge", "Rosso", "Rood", "Vermelho", "Красный", "红", "빨강", "أحمر", "Merah", "Червоний", "Κόκκινο", "Röd", "แดง", "Červená", "Rød", "Kırmızı", "Rød", "Piros", "Punainen", "Đỏ", "Czerwony", "Roșu"},
    {"Orange", "オレンジ", "Orange", "Naranja", "Orange", "Arancione", "Oranje", "Laranja", "Оранжевый", "橙", "주황", "برتقالي", "Oranye", "Помаранчевий", "Πορτοκαλί", "Orange", "ส้ม", "Oranžová", "Orange", "Turuncu", "Oransje", "Narancs", "Oranssi", "Cam", "Pomarańczowy", "Portocaliu"},
    {"Samus Hud", "サムスHUD", "Samus-HUD", "HUD de Samus", "HUD Samus", "HUD Samus", "Samus-HUD", "HUD da Samus", "HUD Samus", "Samus HUD", "사무스 HUD", "HUD Samus", "HUD Samus", "HUD Samus", "HUD Samus", "Samus-HUD", "HUD Samus", "HUD Samus", "Samus-HUD", "Samus HUD", "Samus-HUD", "Samus HUD", "Samus-HUD", "HUD Samus", "HUD Samus", "HUD Samus"},
    {"Samus Hud Outline", "サムスHUDのアウトライン", "Samus-HUD-Kontur", "Contorno del HUD de Samus", "Contour du HUD de Samus", "Contorno HUD Samus", "Samus-HUD contour", "Contorno do HUD de Samus", "Контур HUD Сamus", "Samus HUD 轮廓", "Samus HUD 외곽선", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline", "Samus Hud Outline"},
    {"Kanden Hud", "カンデンHUD", "Kanden-HUD", "HUD de Kanden", "HUD Kanden", "HUD Kanden", "Kanden-HUD", "HUD de Kanden", "HUD Kanden", "Kanden HUD", "Kanden HUD", "HUD Kanden", "HUD Kanden", "HUD Kanden", "HUD Kanden", "Kanden-HUD", "HUD Kanden", "HUD Kanden", "Kanden-HUD", "Kanden HUD", "Kanden-HUD", "Kanden HUD", "Kanden-HUD", "HUD Kanden", "HUD Kanden", "HUD Kanden"},
    {"Spire Hud", "スパイアHUD", "Spire-HUD", "HUD Spire", "HUD Spire", "HUD Spire", "Spire-HUD", "HUD Spire", "HUD Spire", "Spire HUD", "Spire HUD", "HUD Spire", "HUD Spire", "HUD Spire", "HUD Spire", "Spire-HUD", "HUD Spire", "HUD Spire", "Spire-HUD", "Spire HUD", "Spire-HUD", "Spire HUD", "Spire HUD", "HUD Spire", "HUD Spire", "HUD Spire"},
    {"Spire Hud Outline", "スパイアHUDのアウトライン", "Spire-HUD-Umriss", "Contorno HUD Spire", "Contour HUD Spire", "Contorno HUD Spire", "Spire HUD-omtrek", "Contorno HUD Spire", "Контур HUD Spire", "Spire HUD 轮廓", "Spire HUD 외곽선", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline", "Spire Hud Outline"},
    {"Trace Hud", "トレースHUD", "Trace-HUD", "HUD de Trace", "HUD Trace", "HUD Trace", "Trace-HUD", "HUD do Trace", "HUD Trace", "Trace HUD", "트레이스 HUD", "HUD Trace", "HUD Trace", "HUD Trace", "HUD Trace", "Trace-HUD", "HUD Trace", "HUD Trace", "Trace-HUD", "Trace HUD", "Trace-HUD", "Trace HUD", "Trace-HUD", "HUD Trace", "HUD Trace", "HUD Trace"},
    {"Noxus Hud", "ノクサスHUD", "Noxus-HUD", "HUD de Noxus", "HUD de Noxus", "HUD Noxus", "Noxus-HUD", "HUD de Noxus", "HUD Noxus", "Noxus HUD", "Noxus HUD", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud", "Noxus Hud"},
    {"Noxus Hud Outline", "ノクサスHUDのアウトライン", "Noxus-HUD-Kontur", "Contorno del HUD de Noxus", "Contour HUD Noxus", "Contorno HUD Noxus", "Noxus-HUD-omlijning", "Contorno do HUD de Noxus", "Контур HUD Noxus", "Noxus HUD 轮廓", "Noxus HUD 외곽선", "حدود HUD Noxus", "Garis luar HUD Noxus", "Контур HUD Noxus", "Περίγραμμα HUD Noxus", "Noxus-HUD-kontur", "เส้นขอบ HUD Noxus", "Obrys HUD Noxus", "Noxus-HUD-kontur", "Noxus HUD dış çizgisi", "Noxus-HUD-kontur", "Noxus HUD körvonal", "Noxus-HUD:n ääriviiva", "Viền HUD Noxus", "Kontur HUD Noxus", "Contur HUD Noxus"},
    {"Sylux Hud", "サイラックスHUD", "Sylux-HUD", "HUD Sylux", "HUD Sylux", "HUD Sylux", "Sylux-HUD", "HUD Sylux", "HUD Sylux", "Sylux HUD", "Sylux HUD", "HUD Sylux", "HUD Sylux", "HUD Sylux", "HUD Sylux", "Sylux-HUD", "HUD Sylux", "HUD Sylux", "Sylux-HUD", "Sylux HUD", "Sylux-HUD", "Sylux HUD", "Sylux HUD", "HUD Sylux", "HUD Sylux", "HUD Sylux"},
    {"Sylux Crosshair", "サイラックス照準", "Sylux-Fadenkreuz", "Retícula Sylux", "Réticule Sylux", "Mirino Sylux", "Sylux richtkruis", "Mira Sylux", "Прицел Sylux", "Sylux 准星", "Sylux 조준선", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair", "Sylux Crosshair"},
    {"Weavel Hud", "ウィーヴェルHUD", "Weavel-HUD", "HUD de Weavel", "HUD Weavel", "HUD Weavel", "Weavel-HUD", "HUD do Weavel", "HUD Weavel", "Weavel HUD", "위벨 HUD", "HUD Weavel", "HUD Weavel", "HUD Weavel", "HUD Weavel", "Weavel-HUD", "HUD Weavel", "HUD Weavel", "Weavel-HUD", "Weavel HUD", "Weavel-HUD", "Weavel HUD", "Weavel-HUD", "HUD Weavel", "HUD Weavel", "HUD Weavel"},
    {"Weavel Hud Outline", "ウィーヴェルHUDのアウトライン", "Weavel-HUD-Kontur", "Contorno del HUD de Weavel", "Contour du HUD de Weavel", "Contorno HUD Weavel", "Weavel-HUD contour", "Contorno do HUD de Weavel", "Контур HUD Weavel", "Weavel HUD 轮廓", "Weavel HUD 외곽선", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline", "Weavel Hud Outline"},
    {"Avium Purple", "アヴィウム紫", "Avium-Lila", "Púrpura Avium", "Violet Avium", "Viola Avium", "Avium-paars", "Roxo Avium", "Фиолетовый Avium", "Avium 紫", "Avium 보라", "بنفسجي Avium", "Ungu Avium", "Фіолетовий Avium", "Μωβ Avium", "Avium-lila", "ม่วง Avium", "Fialová Avium", "Avium-lilla", "Avium mor", "Avium-lilla", "Avium lila", "Avium-violetti", "Tím Avium", "Fiolet Avium", "Violet Avium"},
    {"OSD Bright Green", "OSD明るい緑", "OSD Hellgrün", "OSD verde brillante", "OSD vert vif", "OSD verde brillante", "OSD heldergroen", "OSD verde brilhante", "OSD ярко-зелёный", "OSD 亮绿", "OSD 밝은 녹색", "OSD أخضر فاتح", "OSD Hijau Terang", "OSD яскраво-зелений", "OSD φωτεινό πράσινο", "OSD ljusgrön", "OSD เขียวสด", "OSD světle zelená", "OSD lysegrøn", "OSD parlak yeşil", "OSD lysegrønn", "OSD világoszöld", "OSD kirkkaanvihreä", "OSD xanh sáng", "OSD jasnozielony", "OSD verde deschis"},
    {"OSD No-Ammo Red", "OSD弾薬なし赤", "OSD Keine-Munition Rot", "OSD sin munición rojo", "OSD plus de munitions rouge", "OSD munizioni esaurite rosso", "OSD geen munitie rood", "OSD sem munição vermelho", "OSD нет боеприпасов красный", "OSD 无弹药红色", "OSD 탄약 없음 빨강", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red", "OSD No-Ammo Red"},
    {"Power Beam", "パワービーム", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam", "Power Beam"},
    {"Volt Driver", "ボルトドライバー", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver", "Volt Driver"},
    {"VoltDriver", "ボルトドライバー", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver", "VoltDriver"},
    {"Missile", "ミサイル", "Rakete", "Misil", "Missile", "Missile", "Raket", "Míssil", "Ракета", "导弹", "미사일", "صاروخ", "Misil", "Ракета", "Πύραυλος", "Missil", "มิสไซล์", "Střela", "Missil", "Füze", "Missil", "Rakéta", "Ohjus", "Tên lửa", "Pocisk", "Rachetă"},
    {"Battle Hammer", "バトルハンマー", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer", "Battle Hammer"},
    {"Battlehammer", "バトルハンマー", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer", "Battlehammer"},
    {"Imperialist", "インペリアリスト", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist", "Imperialist"},
    {"Judicator", "ジュディケイター", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator", "Judicator"},
    {"Magmaul", "マグモール", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul", "Magmaul"},
    {"Shock Coil", "ショックコイル", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil", "Shock Coil"},
    {"ShockCoil", "ショックコイル", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil", "ShockCoil"},
    {"Omega Cannon", "オメガキャノン", "Omega-Kanone", "Cañón Omega", "Canon Omega", "Cannone Omega", "Omega-kanon", "Canhão Omega", "Пушка Омега", "欧米加炮", "오메가 캐논", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon", "Omega Cannon"},
    {"PB Color", "PB色", "PB-Farbe", "Color PB", "Couleur PB", "Colore PB", "PB-kleur", "Cor PB", "Цвет PB", "PB 颜色", "PB 색상", "لون PB", "Warna PB", "Колір PB", "Χρώμα PB", "PB-färg", "สี PB", "Barva PB", "PB-farve", "PB rengi", "PB-farge", "PB szín", "PB-väri", "Màu PB", "Kolor PB", "Culoare PB"},
    {"VD Color", "VD色", "VD-Farbe", "Color VD", "Couleur VD", "Colore VD", "VD-kleur", "Cor VD", "Цвет VD", "VD 色", "VD 색상", "لون VD", "Warna VD", "Колір VD", "Χρώμα VD", "VD-färg", "สี VD", "Barva VD", "VD-farve", "VD rengi", "VD-farge", "VD szín", "VD-väri", "Màu VD", "Kolor VD", "Culoare VD"},
    {"MSL Color", "MSL色", "MSL-Farbe", "Color MSL", "Couleur MSL", "Colore MSL", "MSL-kleur", "Cor MSL", "Цвет MSL", "MSL 颜色", "MSL 색상", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color", "MSL Color"},
    {"BH Color", "BH色", "BH-Farbe", "Color BH", "Couleur BH", "Colore BH", "BH-kleur", "Cor BH", "Цвет BH", "BH 颜色", "BH 색상", "لون BH", "Warna BH", "Колір BH", "Χρώμα BH", "BH-färg", "สี BH", "Barva BH", "BH-farve", "BH rengi", "BH-farge", "BH szín", "BH-väri", "Màu BH", "Kolor BH", "Culoare BH"},
    {"IMP Color", "IMP色", "IMP-Farbe", "Color IMP", "Couleur IMP", "Colore IMP", "IMP-kleur", "Cor IMP", "Цвет IMP", "IMP 颜色", "IMP 색상", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color", "IMP Color"},
    {"JUD Color", "JUD色", "JUD-Farbe", "Color JUD", "Couleur JUD", "Colore JUD", "JUD-kleur", "Cor JUD", "Цвет JUD", "JUD 颜色", "JUD 색상", "لون JUD", "Warna JUD", "Колір JUD", "Χρώμα JUD", "JUD-färg", "สี JUD", "Barva JUD", "JUD-farve", "JUD rengi", "JUD-farge", "JUD szín", "JUD-väri", "Màu JUD", "Kolor JUD", "Culoare JUD"},
    {"MAG Color", "MAG色", "MAG-Farbe", "Color MAG", "Couleur MAG", "Colore MAG", "MAG-kleur", "Cor MAG", "Цвет MAG", "MAG 色", "MAG 색상", "لون MAG", "Warna MAG", "Колір MAG", "Χρώμα MAG", "MAG-färg", "สี MAG", "Barva MAG", "MAG-farve", "MAG rengi", "MAG-farge", "MAG szín", "MAG-väri", "Màu MAG", "Kolor MAG", "Culoare MAG"},
    {"SCL Color", "SCL色", "SCL-Farbe", "Color SCL", "Couleur SCL", "Colore SCL", "SCL-kleur", "Cor SCL", "Цвет SCL", "SCL 颜色", "SCL 색상", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color", "SCL Color"},
    {"OC Color", "OC色", "OC-Farbe", "Color OC", "Couleur OC", "Colore OC", "OC-kleur", "Cor OC", "Цвет OC", "OC 颜色", "OC 색상", "لون OC", "Warna OC", "Колір OC", "Χρώμα OC", "OC-färg", "สี OC", "Barva OC", "OC-farve", "OC rengi", "OC-farge", "OC szín", "OC-väri", "Màu OC", "Kolor OC", "Culoare OC"},

    // Font weights
    {"Thin", "極細", "Dünn", "Fino", "Fin", "Sottile", "Dun", "Fino", "Тонкий", "极细", "얇게", "رفيع", "Tipis", "Тонкий", "Λεπτό", "Tunn", "บาง", "Tenký", "Tynd", "İnce", "Tynn", "Vékony", "Ohut", "Mỏng", "Cienki", "Subțire"},
    {"Extra Light", "特細", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light", "Extra Light"},
    {"Light", "細字", "Leicht", "Ligera", "Léger", "Leggero", "Licht", "Leve", "Тонкий", "细体", "가는", "خفيف", "Ringan", "Легкий", "Ελαφρύ", "Lätt", "เบา", "Lehké", "Let", "Hafif", "Lett", "Könnyű", "Kevyt", "Nhẹ", "Lekki", "Ușor"},
    {"Medium", "中太", "Mittel", "Medio", "Moyen", "Medio", "Gemiddeld", "Médio", "Средний", "中", "중간", "متوسط", "Sedang", "Середній", "Μέτριο", "Medel", "ปานกลาง", "Střední", "Mellem", "Orta", "Middels", "Közepes", "Keskitaso", "Trung bình", "Średni", "Mediu"},
    {"Semi Bold", "やや太字", "Halbfett", "Seminegrita", "Demi-gras", "Semigrassetto", "Halfvet", "Seminegrito", "Полужирный", "半粗体", "세미 볼드", "نصف عريض", "Semi Bold", "Напівжирний", "Ημιέντονο", "Halvfet", "กึ่งหนา", "Polotučné", "Halvfed", "Yarı kalın", "Halvfet", "Félkövér", "Puolilihava", "Bán đậm", "Półgruby", "Semibold"},
    {"Bold", "太字", "Fett", "Negrita", "Gras", "Grassetto", "Vet", "Negrito", "Жирный", "粗体", "굵게", "عريض", "Tebal", "Жирний", "Έντονα", "Fet", "ตัวหนา", "Tučné", "Fed", "Kalın", "Fet", "Félkövér", "Lihavoitu", "Đậm", "Pogrubienie", "Aldin"},
    {"Extra Bold", "極太", "Extra fett", "Extra negrita", "Extra gras", "Extra grassetto", "Extra vet", "Extra negrito", "Экстра жирный", "极粗", "초굵게", "عريض جداً", "Ekstra Tebal", "Дуже жирний", "Έξτρα έντονο", "Extra fet", "หนามาก", "Extra tučné", "Ekstra fed", "Ekstra kalın", "Ekstra fet", "Extra vastag", "Erittäin lihavoitu", "Cực đậm", "Ekstra pogrubiony", "Extra bold"},
    {"Black", "黒太", "Schwarz", "Negro", "Noir", "Nero", "Zwart", "Preto", "Чёрный", "黑", "검정", "أسود", "Hitam", "Чорний", "Μαύρο", "Svart", "ดำ", "Černá", "Sort", "Siyah", "Svart", "Fekete", "Musta", "Đen", "Czarny", "Negru"},

    // Ramp labels
    {"Number of Colors", "色数", "Anzahl der Farben", "Número de colores", "Nombre de couleurs", "Numero di colori", "Aantal kleuren", "Número de cores", "Количество цветов", "颜色数量", "색상 수", "عدد الألوان", "Jumlah warna", "Кількість кольорів", "Αριθμός χρωμάτων", "Antal färger", "จำนวนสี", "Počet barev", "Antal farver", "Renk sayısı", "Antall farger", "Színek száma", "Värien määrä", "Số màu", "Liczba kolorów", "Număr de culori"},
    {"Threshold 1 (%)", "しきい値 1 (%)", "Schwellenwert 1 (%)", "Umbral 1 (%)", "Seuil 1 (%)", "Soglia 1 (%)", "Drempel 1 (%)", "Limite 1 (%)", "Порог 1 (%)", "阈值 1 (%)", "임계값 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)", "Threshold 1 (%)"},
    {"Threshold 2 (%)", "しきい値 2 (%)", "Schwellwert 2 (%)", "Umbral 2 (%)", "Seuil 2 (%)", "Soglia 2 (%)", "Drempel 2 (%)", "Limite 2 (%)", "Порог 2 (%)", "阈值 2 (%)", "임계값 2 (%)", "العتبة 2 (%)", "Ambang 2 (%)", "Поріг 2 (%)", "Όριο 2 (%)", "Tröskel 2 (%)", "เกณฑ์ 2 (%)", "Práh 2 (%)", "Tærskel 2 (%)", "Eşik 2 (%)", "Terskel 2 (%)", "Küszöb 2 (%)", "Kynnys 2 (%)", "Ngưỡng 2 (%)", "Próg 2 (%)", "Prag 2 (%)"},
    {"Threshold 3 (%)", "しきい値 3 (%)", "Schwellenwert 3 (%)", "Umbral 3 (%)", "Seuil 3 (%)", "Soglia 3 (%)", "Drempel 3 (%)", "Limite 3 (%)", "Порог 3 (%)", "阈值 3 (%)", "임계값 3 (%)", "العتبة 3 (%)", "Ambang 3 (%)", "Поріг 3 (%)", "Όριο 3 (%)", "Tröskel 3 (%)", "เกณฑ์ 3 (%)", "Práh 3 (%)", "Tærskel 3 (%)", "Eşik 3 (%)", "Terskel 3 (%)", "Küszöb 3 (%)", "Kynnys 3 (%)", "Ngưỡng 3 (%)", "Próg 3 (%)", "Prag 3 (%)"},
    {"Threshold 4 (%)", "しきい値 4 (%)", "Schwellenwert 4 (%)", "Umbral 4 (%)", "Seuil 4 (%)", "Soglia 4 (%)", "Drempel 4 (%)", "Limite 4 (%)", "Порог 4 (%)", "阈值 4 (%)", "임계값 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)", "Threshold 4 (%)"},
    {"Threshold 5 (%)", "しきい値 5 (%)", "Schwellwert 5 (%)", "Umbral 5 (%)", "Seuil 5 (%)", "Soglia 5 (%)", "Drempel 5 (%)", "Limite 5 (%)", "Порог 5 (%)", "阈值 5 (%)", "임계값 5 (%)", "عتبة 5 (%)", "Ambang 5 (%)", "Поріг 5 (%)", "Όριο 5 (%)", "Tröskel 5 (%)", "เกณฑ์ 5 (%)", "Prah 5 (%)", "Tærskel 5 (%)", "Eşik 5 (%)", "Terskel 5 (%)", "Küszöb 5 (%)", "Kynnys 5 (%)", "Ngưỡng 5 (%)", "Próg 5 (%)", "Prag 5 (%)"},
    {"Threshold 6 (%)", "しきい値 6 (%)", "Schwellenwert 6 (%)", "Umbral 6 (%)", "Seuil 6 (%)", "Soglia 6 (%)", "Drempel 6 (%)", "Limite 6 (%)", "Порог 6 (%)", "阈值 6 (%)", "임계값 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)", "Threshold 6 (%)"},
    {"Color 1", "色 1", "Farbe 1", "Color 1", "Couleur 1", "Colore 1", "Kleur 1", "Cor 1", "Цвет 1", "颜色 1", "색상 1", "اللون 1", "Warna 1", "Колір 1", "Χρώμα 1", "Färg 1", "สี 1", "Barva 1", "Farve 1", "Renk 1", "Farge 1", "Szín 1", "Väri 1", "Màu 1", "Kolor 1", "Culoare 1"},
    {"Color 2", "色 2", "Farbe 2", "Color 2", "Couleur 2", "Colore 2", "Kleur 2", "Cor 2", "Цвет 2", "颜色 2", "색상 2", "اللون 2", "Warna 2", "Колір 2", "Χρώμα 2", "Färg 2", "สี 2", "Barva 2", "Farve 2", "Renk 2", "Farge 2", "Szín 2", "Väri 2", "Màu 2", "Kolor 2", "Culoare 2"},
    {"Color 3", "色 3", "Farbe 3", "Color 3", "Couleur 3", "Colore 3", "Kleur 3", "Cor 3", "Цвет 3", "颜色 3", "색상 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3", "Color 3"},
    {"Color 4", "色 4", "Farbe 4", "Color 4", "Couleur 4", "Colore 4", "Kleur 4", "Cor 4", "Цвет 4", "颜色 4", "색상 4", "لون 4", "Warna 4", "Колір 4", "Χρώμα 4", "Färg 4", "สี 4", "Barva 4", "Farve 4", "Renk 4", "Farge 4", "Szín 4", "Väri 4", "Màu 4", "Kolor 4", "Culoare 4"},
    {"Color 5", "色 5", "Farbe 5", "Color 5", "Couleur 5", "Colore 5", "Kleur 5", "Cor 5", "Цвет 5", "颜色 5", "색상 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5", "Color 5"},
    {"Color 6", "色 6", "Farbe 6", "Color 6", "Couleur 6", "Colore 6", "Kleur 6", "Cor 6", "Цвет 6", "颜色 6", "색상 6", "اللون 6", "Warna 6", "Колір 6", "Χρώμα 6", "Färg 6", "สี 6", "Barva 6", "Farve 6", "Renk 6", "Farge 6", "Szín 6", "Väri 6", "Màu 6", "Kolor 6", "Culoare 6"},

    // Custom HUD code status
    {"Generate sharable TOML for the current Custom HUD settings, or paste TOML below to apply it.", "現在のカスタムHUD設定から共有用TOMLを生成するか、下にTOMLを貼り付けて適用します。", "Teilbares TOML für die aktuellen benutzerdefinierten HUD-Einstellungen generieren oder TOML unten einfügen, um es anzuwenden.", "Genera TOML compartible para los ajustes HUD personalizados actuales, o pega TOML abajo para aplicarlo.", "Générez un TOML partageable pour les paramètres HUD personnalisés actuels, ou collez le TOML ci-dessous pour l'appliquer.", "Genera TOML condivisibile per le impostazioni HUD personalizzate attuali, oppure incolla TOML sotto per applicarlo.", "Deelbare TOML genereren voor de huidige aangepaste HUD-instellingen, of plak TOML hieronder om toe te passen.", "Gere TOML compartilhável para as configurações HUD personalizadas atuais, ou cole TOML abaixo para aplicá-lo.", "Сгенерировать TOML для обмена текущими настройками HUD или вставьте TOML ниже для применения.", "根据当前自定义 HUD 设定生成可共享 TOML，或在下方粘贴 TOML 以应用。", "현재 사용자 HUD 설정의 공유용 TOML을 생성하거나, 아래에 TOML을 붙여넣어 적용하세요.", "إنشاء TOML قابل للمشاركة لإعدادات HUD المخصصة الحالية، أو الصق TOML أدناه لتطبيقه.", "Buat TOML yang dapat dibagikan untuk pengaturan Custom HUD saat ini, atau tempel TOML di bawah untuk menerapkannya.", "Створити TOML для поширення поточних налаштувань Custom HUD або вставте TOML нижче для застосування.", "Δημιουργήστε κοινόχρηστο TOML για τις τρέχουσες ρυθμίσεις Custom HUD, ή επικολλήστε TOML παρακάτω.", "Generera delbart TOML för aktuella Custom HUD-inställningar, eller klistra in TOML nedan.", "สร้าง TOML ที่แชร์ได้สำหรับการตั้งค่า Custom HUD ปัจจุบัน หรือวาง TOML ด้านล่างเพื่อใช้", "Generovat sdílený TOML pro aktuální nastavení Custom HUD, nebo vložte TOML níže.", "Generer delbart TOML for aktuelle Custom HUD-indstillinger, eller indsæt TOML nedenfor.", "Geçerli Custom HUD ayarları için paylaşılabilir TOML oluşturun veya uygulamak için aşağıya yapıştırın.", "Generer delbart TOML for gjeldende Custom HUD-innstillinger, eller lim inn TOML nedenfor.", "Ossz megosztható TOML-t az aktuális Custom HUD beállításokhoz, vagy illeszd be a TOML-t alább.", "Luo jaettava TOML nykyisille Custom HUD -asetuksille tai liitä TOML alle.", "Tạo TOML có thể chia sẻ cho cài đặt Custom HUD hiện tại, hoặc dán TOML bên dưới để áp dụng.", "Generuj udostępnialny TOML dla bieżących ustawień Custom HUD lub wklej TOML poniżej.", "Generează TOML partajabil pentru setările Custom HUD curente sau lipește TOML mai jos."},
    {"Output refreshed from the current Custom HUD settings.", "現在のカスタムHUD設定から出力を更新しました。", "Ausgabe aus den aktuellen Custom-HUD-Einstellungen aktualisiert.", "Salida actualizada desde los ajustes actuales del HUD personalizado.", "Sortie actualisée à partir des paramètres HUD personnalisés actuels.", "Output aggiornato dalle impostazioni HUD personalizzate attuali.", "Uitvoer ververst vanuit huidige aangepaste HUD-instellingen.", "Saída atualizada a partir das configurações atuais do HUD personalizado.", "Вывод обновлён из текущих настроек пользовательского HUD.", "已从当前自定义 HUD 设置刷新输出。", "현재 사용자 HUD 설정에서 출력을 새로고침했습니다.", "تم تحديث المخرجات من إعدادات Custom HUD الحالية.", "Output diperbarui dari pengaturan Custom HUD saat ini.", "Вивід оновлено з поточних налаштувань Custom HUD.", "Η έξοδος ανανεώθηκε από τις τρέχουσες ρυθμίσεις Custom HUD.", "Utdata uppdaterad från aktuella Custom HUD-inställningar.", "อัปเดตผลลัพธ์จากการตั้งค่า Custom HUD ปัจจุบัน", "Výstup obnoven z aktuálních nastavení Custom HUD.", "Output opdateret fra aktuelle Custom HUD-indstillinger.", "Çıktı mevcut Custom HUD ayarlarından yenilendi.", "Utdata oppdatert fra gjeldende Custom HUD-innstillinger.", "Kimenet frissítve az aktuális Custom HUD beállításokból.", "Tuloste päivitetty nykyisistä Custom HUD -asetuksista.", "Đầu ra được làm mới từ cài đặt Custom HUD hiện tại.", "Dane wyjściowe odświeżone z bieżących ustawień Custom HUD.", "Ieșire reîmprospătată din setările Custom HUD curente."},
    {"Custom HUD code copied to the clipboard.", "カスタムHUDコードをクリップボードにコピーしました。", "Benutzerdefinierter HUD-Code in die Zwischenablage kopiert.", "Código de HUD personalizado copiado al portapapeles.", "Code HUD personnalisé copié dans le presse-papiers.", "Codice HUD personalizzato copiato negli appunti.", "Aangepaste HUD-code gekopieerd naar klembord.", "Código de HUD personalizado copiado para a área de transferência.", "Код пользовательского HUD скопирован в буфер обмена.", "自定义 HUD 代码已复制到剪贴板。", "사용자 지정 HUD 코드가 클립보드에 복사되었습니다.", "تم نسخ كود HUD المخصص إلى الحافظة.", "Kode HUD kustom disalin ke clipboard.", "Код користувацького HUD скопійовано в буфер обміну.", "Ο κώδικας προσαρμοσμένου HUD αντιγράφηκε στο πρόχειρο.", "Anpassad HUD-kod kopierad till urklipp.", "คัดลอกโค้ด HUD กำหนดเองไปยังคลิปบอร์ดแล้ว", "Kód vlastního HUD zkopírován do schránky.", "Brugerdefineret HUD-kode kopieret til udklipsholder.", "Özel HUD kodu panoya kopyalandı.", "Tilpasset HUD-kode kopiert til utklippstavle.", "Egyéni HUD kód a vágólapra másolva.", "Mukautettu HUD-koodi kopioitu leikepöydälle.", "Đã sao chép mã HUD tùy chỉnh vào clipboard.", "Kod niestandardowego HUD skopiowany do schowka.", "Codul HUD personalizat a fost copiat în clipboard."},
    {"Output copied into the input box.", "出力を入力欄へコピーしました。", "Ausgabe in das Eingabefeld kopiert.", "Salida copiada al cuadro de entrada.", "Sortie copiée dans la zone de saisie.", "Output copiato nel campo di input.", "Uitvoer gekopieerd naar het invoerveld.", "Saída copiada para a caixa de entrada.", "Вывод скопирован в поле ввода.", "输出已复制到输入框。", "출력이 입력란에 복사되었습니다.", "تم نسخ الإخراج إلى مربع الإدخال.", "Output disalin ke kotak input.", "Вивід скопійовано в поле вводу.", "Η έξοδος αντιγράφηκε στο πεδίο εισόδου.", "Utdata kopierades till inmatningsrutan.", "คัดลอกผลลัพธ์ไปยังช่องป้อนข้อมูลแล้ว", "Výstup zkopírován do vstupního pole.", "Output kopieret til inputfeltet.", "Çıktı giriş kutusuna kopyalandı.", "Utdata kopiert til inndatafeltet.", "Kimenet bemásolva a beviteli mezőbe.", "Tuloste kopioitu syöttökenttään.", "Đã sao chép đầu ra vào hộp nhập.", "Wynik skopiowano do pola wejściowego.", "Ieșirea a fost copiată în caseta de intrare."},
    {"Custom HUD code applied to the dialog.", "カスタムHUDコードをダイアログへ適用しました。", "Custom HUD-Code auf den Dialog angewendet.", "Código de HUD personalizado aplicado al diálogo.", "Code HUD personnalisé appliqué à la boîte de dialogue.", "Codice HUD personalizzato applicato alla finestra di dialogo.", "Aangepaste HUD-code toegepast op het dialoogvenster.", "Código de HUD personalizado aplicado ao diálogo.", "Код пользовательского HUD применён к диалогу.", "自定义 HUD 代码已应用到对话框。", "사용자 HUD 코드가 대화 상자에 적용되었습니다.", "تم تطبيق كود Custom HUD على مربع الحوار.", "Kode Custom HUD diterapkan ke dialog.", "Код Custom HUD застосовано до діалогу.", "Ο κώδικας Custom HUD εφαρμόστηκε στο παράθυρο διαλόγου.", "Custom HUD-kod tillämpad på dialogrutan.", "ใช้โค้ด Custom HUD กับกล่องโต้ตอบแล้ว", "Kód Custom HUD použit na dialog.", "Custom HUD-kode anvendt på dialogen.", "Custom HUD kodu iletişim kutusuna uygulandı.", "Custom HUD-kode brukt på dialogen.", "Custom HUD kód alkalmazva a párbeszédablakra.", "Custom HUD -koodi otettiin käyttöön valintaikkunassa.", "Đã áp dụng mã Custom HUD vào hộp thoại.", "Kod Custom HUD zastosowany do okna dialogowego.", "Cod Custom HUD aplicat dialogului."},
    {"Paste Custom HUD TOML into the input box first.", "先にカスタムHUD TOMLを入力欄へ貼り付けてください。", "Füge zuerst benutzerdefiniertes HUD-TOML in das Eingabefeld ein.", "Pega primero el TOML HUD personalizado en el cuadro de entrada.", "Collez d'abord le TOML HUD personnalisé dans la zone de saisie.", "Incolla prima il TOML HUD personalizzato nella casella di input.", "Plak eerst aangepaste HUD-TOML in het invoerveld.", "Cole primeiro o TOML HUD personalizado na caixa de entrada.", "Сначала вставьте пользовательский HUD TOML в поле ввода.", "请先将自定义 HUD TOML 粘贴到输入框。", "먼저 입력란에 사용자 HUD TOML을 붙여넣으세요.", "الصق TOML HUD المخصص في مربع الإدخال أولاً.", "Tempel TOML Custom HUD ke kotak input terlebih dahulu.", "Спочатку вставте Custom HUD TOML у поле введення.", "Επικολλήστε πρώτα το Custom HUD TOML στο πεδίο εισόδου.", "Klistra in Custom HUD-TOML i inmatningsrutan först.", "วาง Custom HUD TOML ในช่องอินพุตก่อน", "Nejprve vložte Custom HUD TOML do vstupního pole.", "Indsæt Custom HUD-TOML i inputfeltet først.", "Önce Custom HUD TOML'yi giriş kutusuna yapıştırın.", "Lim inn Custom HUD-TOML i inndatafeltet først.", "Először illeszd be a Custom HUD TOML-t a beviteli mezőbe.", "Liitä Custom HUD-TOML ensin syöttökenttään.", "Dán Custom HUD TOML vào hộp nhập trước.", "Najpierw wklej Custom HUD TOML do pola wejściowego.", "Lipește mai întâi TOML Custom HUD în caseta de intrare."},
    {"The pasted Custom HUD code is not a TOML table.", "貼り付けられたカスタムHUDコードはTOMLテーブルではありません。", "Der eingefügte Custom-HUD-Code ist keine TOML-Tabelle.", "El código HUD personalizado pegado no es una tabla TOML.", "Le code HUD personnalisé collé n'est pas une table TOML.", "Il codice HUD personalizzato incollato non è una tabella TOML.", "De geplakte aangepaste HUD-code is geen TOML-tabel.", "O código HUD personalizado colado não é uma tabela TOML.", "Вставленный код пользовательского HUD не является таблицей TOML.", "粘贴的自定义 HUD 代码不是 TOML 表。", "붙여넣은 사용자 HUD 코드가 TOML 테이블이 아닙니다.", "كود Custom HUD الملصق ليس جدول TOML.", "Kode Custom HUD yang ditempel bukan tabel TOML.", "Вставлений код Custom HUD не є таблицею TOML.", "Ο επικολλημένος κώδικας Custom HUD δεν είναι πίνακας TOML.", "Den inklistrade Custom HUD-koden är inte en TOML-tabell.", "โค้ด Custom HUD ที่วางไม่ใช่ตาราง TOML", "Vložený kód Custom HUD není tabulka TOML.", "Den indsatte Custom HUD-kode er ikke en TOML-tabel.", "Yapıştırılan Custom HUD kodu bir TOML tablosu değil.", "Den limte inn Custom HUD-koden er ikke en TOML-tabell.", "A beillesztett Custom HUD kód nem TOML tábla.", "Liitetty Custom HUD -koodi ei ole TOML-taulukko.", "Mã Custom HUD dán vào không phải bảng TOML.", "Wklejony kod Custom HUD nie jest tabelą TOML.", "Codul Custom HUD lipit nu este un tabel TOML."},
    {"Failed to load Custom HUD code: %1", "カスタムHUDコードの読み込みに失敗しました: %1", "Benutzerdefinierter HUD-Code konnte nicht geladen werden: %1", "Error al cargar el código de HUD personalizado: %1", "Échec du chargement du code HUD personnalisé : %1", "Impossibile caricare il codice HUD personalizzato: %1", "Aangepaste HUD-code laden mislukt: %1", "Falha ao carregar código de HUD personalizado: %1", "Не удалось загрузить код пользовательского HUD: %1", "加载自定义 HUD 代码失败：%1", "사용자 지정 HUD 코드 로드 실패: %1", "فشل تحميل كود HUD المخصص: %1", "Gagal memuat kode HUD kustom: %1", "Не вдалося завантажити код користувацького HUD: %1", "Αποτυχία φόρτωσης κώδικα προσαρμοσμένου HUD: %1", "Det gick inte att ladda anpassad HUD-kod: %1", "โหลดโค้ด HUD กำหนดเองล้มเหลว: %1", "Nepodařilo se načíst kód vlastního HUD: %1", "Kunne ikke indlæse brugerdefineret HUD-kode: %1", "Özel HUD kodu yüklenemedi: %1", "Kunne ikke laste tilpasset HUD-kode: %1", "Az egyéni HUD kód betöltése sikertelen: %1", "Mukautetun HUD-koodin lataus epäonnistui: %1", "Không tải được mã HUD tùy chỉnh: %1", "Nie udało się wczytać kodu niestandardowego HUD: %1", "Nu s-a putut încărca codul HUD personalizat: %1"},

    // Developer input-method UI
    {"Use New Method for Weapon Change", "武器変更に新方式を使う", "Neue Methode für Waffenwechsel verwenden", "Usar método nuevo para cambio de arma", "Utiliser la nouvelle méthode pour le changement d'arme", "Usa nuovo metodo per cambio arma", "Nieuwe methode voor wapenwissel gebruiken", "Usar novo método para troca de arma", "Использовать новый метод смены оружия", "使用新方式切换武器", "무기 변경에 새 방식 사용", "استخدام الطريقة الجديدة لتغيير السلاح", "Gunakan metode baru untuk ganti senjata", "Використовувати новий метод зміни зброї", "Χρήση νέας μεθόδου αλλαγής όπλου", "Använd ny metod för vapenbyte", "ใช้วิธีใหม่สำหรับเปลี่ยนอาวุธ", "Použít novou metodu změny zbraně", "Brug ny metode til våbenskift", "Silah değişimi için yeni yöntemi kullan", "Bruk ny metode for våpenbytte", "Új módszer használata fegyverváltáshoz", "Käytä uutta menetelmää aseenvaihtoon", "Dùng phương pháp mới để đổi vũ khí", "Użyj nowej metody zmiany broni", "Folosește metoda nouă pentru schimbarea armei"},
    {"Use New Method for Biped Fire", "二足時の射撃に新方式を使う", "Neue Methode für Biped-Feuer verwenden", "Usar nuevo método para disparo Biped", "Utiliser la nouvelle méthode pour le tir Biped", "Usa nuovo metodo per fuoco Biped", "Nieuwe methode voor Biped-vuur gebruiken", "Usar novo método para disparo Biped", "Использовать новый метод для огня Biped", "使用 Biped 射击的新方法", "Biped 사격에 새 방식 사용", "استخدام الطريقة الجديدة لإطلاق Biped", "Gunakan metode baru untuk tembakan Biped", "Використовувати новий метод для вогню Biped", "Χρήση νέας μεθόδου για πυροβολισμό Biped", "Använd ny metod för Biped-eld", "ใช้วิธีใหม่สำหรับยิง Biped", "Použít novou metodu pro střelbu Biped", "Brug ny metode til Biped-skydning", "Biped ateşi için yeni yöntemi kullan", "Bruk ny metode for Biped-skudd", "Új módszer használata Biped tüzeléshez", "Käytä uutta menetelmää Biped-tulitukseen", "Dùng phương pháp mới cho bắn Biped", "Użyj nowej metody dla ognia Biped", "Folosește metoda nouă pentru focul Biped"},
    {"Use New Method for Alt-Form Transform", "トランスフォーム変形に新方式を使う", "Neue Methode für Alt-Form-Transformation verwenden", "Usar nuevo método para transformación Alt-Form", "Utiliser la nouvelle méthode pour transformation Alt-Form", "Usa nuovo metodo per trasformazione Alt-Form", "Nieuwe methode voor Alt-Form-transformatie gebruiken", "Usar novo método para transformação Alt-Form", "Использовать новый метод преобразования Alt-Form", "Alt-Form 变形使用新方法", "Alt-Form 변형에 새 방식 사용", "استخدام الطريقة الجديدة لتحويل Alt-Form", "Gunakan Metode Baru untuk Transformasi Alt-Form", "Використовувати новий метод перетворення Alt-Form", "Χρήση νέας μεθόδου για Alt-Form Transform", "Använd ny metod för Alt-Form-transform", "ใช้วิธีใหม่สำหรับ Alt-Form Transform", "Použít novou metodu pro Alt-Form transformaci", "Brug ny metode til Alt-Form-transform", "Alt-Form Dönüşümü için yeni yöntemi kullan", "Bruk ny metode for Alt-Form-transform", "Új módszer használata Alt-Form átalakításhoz", "Käytä uutta menetelmää Alt-Form-muunnokseen", "Dùng phương pháp mới cho Biến đổi Alt-Form", "Użyj nowej metody transformacji Alt-Form", "Folosește metoda nouă pentru transformarea Alt-Form"},
    {"Use New Method for Zoom", "ズームに新方式を使う", "Neue Zoom-Methode verwenden", "Usar nuevo método de zoom", "Utiliser la nouvelle méthode de zoom", "Usa nuovo metodo per zoom", "Nieuwe zoommethode gebruiken", "Usar novo método de zoom", "Использовать новый метод зума", "使用新缩放方式", "줌에 새 방식 사용", "استخدام طريقة جديدة للتكبير", "Gunakan Metode Baru untuk Zoom", "Використовувати новий метод зуму", "Χρήση νέας μεθόδου για ζουμ", "Använd ny metod för zoom", "ใช้วิธีใหม่สำหรับซูม", "Použít novou metodu pro zoom", "Brug ny metode til zoom", "Zoom için yeni yöntemi kullan", "Bruk ny metode for zoom", "Új módszer használata zoomhoz", "Käytä uutta zoom-menetelmää", "Dùng phương pháp mới cho zoom", "Użyj nowej metody zoomu", "Folosește metoda nouă pentru zoom"},
    {"Use New Method 2 for Zoom", "ズームに新方式2を使う", "Neue Methode 2 für Zoom verwenden", "Usar método nuevo 2 para el zoom", "Utiliser la nouvelle méthode 2 pour le zoom", "Usa nuovo metodo 2 per lo zoom", "Nieuwe methode 2 voor zoom gebruiken", "Usar novo método 2 para zoom", "Использовать новый метод 2 для зума", "缩放使用新方法 2", "확대에 새 방식 2 사용", "استخدام الطريقة الجديدة 2 للتكبير", "Gunakan Metode Baru 2 untuk zoom", "Використовувати новий метод 2 для зуму", "Χρήση νέας μεθόδου 2 για ζουμ", "Använd ny metod 2 för zoom", "ใช้วิธีใหม่ 2 สำหรับซูม", "Použít novou metodu 2 pro zoom", "Brug ny metode 2 til zoom", "Zoom için Yeni Yöntem 2'yi kullan", "Bruk ny metode 2 for zoom", "Új 2. módszer használata zoomhoz", "Käytä uutta menetelmää 2 zoomiin", "Dùng Phương pháp mới 2 cho zoom", "Użyj nowej metody 2 do zoomu", "Folosește metoda nouă 2 pentru zoom"},

#include "MelonPrimeLocalizationMelondsDialogs.inc"
};

constexpr ObjectTextTranslation kObjectTextTranslations[] = {
    {
        "metroidMphSensitvityLabel2",
        R"((精密なエイムのため、可能なら1以下にしてください。推奨範囲は -3〜0 です。ただし低すぎるとエイム時のHUD揺れが大きくなります。この値はゲーム内感度に対する相対値なので、0は感度ゼロではなく、1より低いだけです。この設定はMPHのゲーム内感度を上書きするため、ゲーム内で感度を変更しても効果はありません。))",
        R"((Für präzises Zielen nach Möglichkeit auf 1 oder darunter setzen. Empfohlener Bereich: -3 bis 0. Zu niedrige Werte vergrößern das HUD-Wackeln beim Zielen. Dieser Wert ist relativ zur ingame-Empfindlichkeit; 0 bedeutet nicht null Empfindlichkeit, sondern nur niedriger als 1. Diese Einstellung überschreibt die MPH-Empfindlichkeit im Spiel; Änderungen ingame haben keine Wirkung.))",
        R"((Para puntería precisa, usa 1 o menos si es posible. Rango recomendado: -3 a 0. Valores muy bajos aumentan el temblor del HUD al apuntar. Este valor es relativo a la sensibilidad en el juego; 0 no es sensibilidad cero, solo menor que 1. Este ajuste sobrescribe la sensibilidad de MPH en el juego; cambiarla en el juego no surte efecto.))",
        R"((Pour une visée précise, utilisez 1 ou moins si possible. Plage recommandée : -3 à 0. Une valeur trop basse accentue le tremblement du HUD en visée. Cette valeur est relative à la sensibilité en jeu ; 0 n'est pas une sensibilité nulle, juste inférieure à 1. Ce réglage remplace la sensibilité MPH en jeu ; la modifier en jeu n'a aucun effet.))",
        R"((Per una mira precisa, imposta 1 o meno se possibile. Intervallo consigliato: da -3 a 0. Valori troppo bassi aumentano il tremolio dell'HUD durante la mira. Questo valore è relativo alla sensibilità in gioco; 0 non significa sensibilità zero, ma solo inferiore a 1. Questa impostazione sostituisce la sensibilità MPH in gioco; modificarla in gioco non ha effetto.))",
        R"((Voor precies richten, stel indien mogelijk in op 1 of lager. Aanbevolen bereik: -3 tot 0. Te lage waarden vergroten het HUD-trillen tijdens het richten. Deze waarde is relatief ten opzichte van de gevoeligheid in het spel; 0 betekent niet nul gevoeligheid, alleen lager dan 1. Deze instelling overschrijft de MPH-gevoeligheid in het spel; wijzigingen in het spel hebben geen effect.))",
        R"((Para mira precisa, use 1 ou menos, se possível. Intervalo recomendado: -3 a 0. Valores muito baixos aumentam a tremulação do HUD ao mirar. Este valor é relativo à sensibilidade no jogo; 0 não é sensibilidade zero, apenas inferior a 1. Esta definição substitui a sensibilidade MPH no jogo; alterá-la no jogo não surte efeito.))",
        R"((Для точного прицеливания по возможности установите 1 или ниже. Рекомендуемый диапазон: от -3 до 0. Слишком низкие значения усиливают дрожание HUD при прицеливании. Это значение относительно чувствительности в игре; 0 — не нулевая чувствительность, а просто ниже 1. Эта настройка переопределяет чувствительность MPH в игре; изменения в игре не действуют.))",
        "（为精确瞄准，请尽可能设为 1 或更低。推荐范围：-3 至 0。数值过低会增大瞄准时 HUD 的抖动。该值相对于游戏内灵敏度；0 并非零灵敏度，只是低于 1。此设置会覆盖 MPH 游戏内灵敏度；在游戏内更改无效。）",
        R"((정밀한 조준이 필요하면 가능하면 1 이하로 설정하세요. 권장 범위: -3~0. 너무 낮으면 조준 시 HUD 흔들림이 커집니다. 이 값은 게임 내 감도에 대한 상대값이므로 0은 감도 0이 아니라 1보다 낮을 뿐입니다. 이 설정은 MPH 게임 내 감도를 덮어쓰므로 게임 내에서 변경해도 적용되지 않습니다.))",
        R"((لتصويب دقيق، اضبط على 1 أو أقل إن أمكن. النطاق الموصى به: -3 إلى 0. القيم المنخفضة جداً تزيد اهتزاز HUD أثناء التصويب. هذه القيمة نسبية لحساسية اللعبة؛ 0 ليست حساسية صفرية، بل أقل من 1 فقط. هذا الإعداد يتجاوز حساسية MPH داخل اللعبة؛ تغييرها في اللعبة لا يؤثر.))",
        R"((Untuk bidik presisi, atur ke 1 atau lebih rendah jika memungkinkan. Rentang disarankan: -3 hingga 0. Nilai terlalu rendah memperbesar goyangan HUD saat membidik. Nilai ini relatif terhadap sensitivitas dalam game; 0 bukan sensitivitas nol, hanya lebih rendah dari 1. Pengaturan ini menimpa sensitivitas MPH dalam game; mengubahnya di game tidak berpengaruh.))",
        R"((Для точного прицілювання за можливості встановіть 1 або нижче. Рекомендований діапазон: від -3 до 0. Занадто низькі значення посилюють тремтіння HUD під час прицілювання. Це значення відносне до чутливості в грі; 0 — не нульова чутливість, а просто нижча за 1. Це налаштування перекриває чутливість MPH у грі; зміни в грі не діють.))",
        R"((Για ακριβή σκόπευση, ορίστε 1 ή χαμηλότερα αν είναι δυνατόν. Συνιστώμενο εύρος: -3 έως 0. Πολύ χαμηλές τιμές αυξάνουν το τρέμουλο του HUD κατά τη σκόπευση. Αυτή η τιμή είναι σχετική με την ευαισθησία στο παιχνίδι· το 0 δεν σημαίνει μηδενική ευαισθησία, απλώς χαμηλότερη από 1. Αυτή η ρύθμιση αντικαθιστά την ευαισθησία MPH στο παιχνίδι· αλλαγές στο παιχνίδι δεν έχουν αποτέλεσμα.))",
        R"((För exakt sikte, ställ in 1 eller lägre om möjligt. Rekommenderat intervall: -3 till 0. För låga värden ökar HUD-skakningen vid siktning. Detta värde är relativt spelets känslighet; 0 är inte noll känslighet, bara lägre än 1. Denna inställning åsidosätter MPH-känsligheten i spelet; ändringar i spelet har ingen effekt.))",
        R"((เพื่อเล็งให้แม่นยำ ตั้งค่าเป็น 1 หรือต่ำกว่าหากเป็นไปได้ ช่วงที่แนะนำ: -3 ถึง 0 ค่าต่ำเกินไปจะทำให้ HUD สั่นมากขึ้นขณะเล็ง ค่านี้สัมพันธ์กับความไวในเกม 0 ไม่ใช่ความไวศูนย์ แต่ต่ำกว่า 1 เท่านั้น การตั้งค่านี้แทนที่ความไว MPH ในเกม การเปลี่ยนในเกมจึงไม่มีผล))",
        R"((Pro přesné míření nastavte pokud možno 1 nebo méně. Doporučený rozsah: -3 až 0. Příliš nízké hodnoty zvětšují chvění HUD při míření. Tato hodnota je relativní k citlivosti ve hře; 0 neznamená nulovou citlivost, jen nižší než 1. Toto nastavení přepisuje citlivost MPH ve hře; změny ve hře nemají účinek.))",
        R"((For præcist sigte, indstil til 1 eller lavere hvis muligt. Anbefalet område: -3 til 0. For lave værdier øger HUD-rystelse ved sigtning. Denne værdi er relativ til følsomheden i spillet; 0 er ikke nul følsomhed, kun lavere end 1. Denne indstilling tilsidesætter MPH-følsomheden i spillet; ændringer i spillet har ingen effekt.))",
        R"((Hassas nişan için mümkünse 1 veya altına ayarlayın. Önerilen aralık: -3 ile 0. Çok düşük değerler nişan alırken HUD titremesini artırır. Bu değer oyun içi hassasiyete görelidir; 0 sıfır hassasiyet değil, yalnızca 1'den düşüktür. Bu ayar MPH oyun içi hassasiyetini geçersiz kılar; oyunda değiştirmek etkisizdir.))",
        R"((For presis sikting, sett til 1 eller lavere om mulig. Anbefalt område: -3 til 0. For lave verdier øker HUD-risting ved sikting. Denne verdien er relativ til følsomheten i spillet; 0 er ikke null følsomhet, bare lavere enn 1. Denne innstillingen overstyrer MPH-følsomheten i spillet; endringer i spillet har ingen effekt.))",
        R"((Precíz célzáshoz állítsd 1-re vagy alacsonyabbra, ha lehet. Ajánlott tartomány: -3 és 0. A túl alacsony értékek növelik a HUD remegését célzáskor. Ez az érték relatív a játékbeli érzékenységhez; a 0 nem nulla érzékenység, csak 1-nél alacsonyabb. Ez a beállítás felülírja az MPH játékbeli érzékenységét; a játékban történő módosítás nem hat.))",
        R"((Tarkkaa tähtäystä varten aseta 1 tai alemmas, jos mahdollista. Suositeltu alue: -3–0. Liian matalat arvot lisäävät HUD-tärinää tähtäyksessä. Tämä arvo on suhteellinen pelin herkkyyteen; 0 ei ole nollan herkkyys, vain alempi kuin 1. Tämä asetus korvaa MPH-pelin herkkyyden; pelissä tehdyt muutokset eivät vaikuta.))",
        R"((Để ngắm chính xác, đặt 1 hoặc thấp hơn nếu có thể. Phạm vi khuyến nghị: -3 đến 0. Giá trị quá thấp làm tăng rung HUD khi ngắm. Giá trị này tương đối với độ nhạy trong game; 0 không phải độ nhạy bằng không, chỉ thấp hơn 1. Cài đặt này ghi đè độ nhạy MPH trong game; thay đổi trong game không có tác dụng.))",
        R"((Dla precyzyjnego celowania ustaw 1 lub mniej, jeśli to możliwe. Zalecany zakres: -3 do 0. Zbyt niskie wartości zwiększają drganie HUD podczas celowania. Ta wartość jest względem czułości w grze; 0 to nie zerowa czułość, tylko niższa niż 1. To ustawienie nadpisuje czułość MPH w grze; zmiany w grze nie działają.))",
        R"((Pentru țintire precisă, setați 1 sau mai puțin dacă este posibil. Interval recomandat: -3 până la 0. Valori prea mici măresc tremurul HUD la țintire. Această valoare este relativă la sensibilitatea din joc; 0 nu înseamnă sensibilitate zero, ci doar mai mică decât 1. Această setare suprascrie sensibilitatea MPH din joc; modificările în joc nu au efect.))"
    },
    {
        "metroidAimYAxisScaleLabel2",
        "(1.5147 = MPH標準、1.9429 = X/Y角速度一致 [オプション])",
        "(1.5147 = MPH-Standard, 1.9429 = X/Y-Winkelgeschwindigkeit gleich [Optional])",
        "(1.5147 = estándar MPH, 1.9429 = velocidad angular X/Y igual [opcional])",
        "(1.5147 = standard MPH, 1.9429 = vitesse angulaire X/Y identique [option])",
        "(1.5147 = standard MPH, 1.9429 = velocità angolare X/Y uguale [opzionale])",
        "(1.5147 = MPH-standaard, 1.9429 = X/Y-hoeksnelheid gelijk [optioneel])",
        "(1.5147 = padrão MPH, 1.9429 = velocidade angular X/Y igual [opcional])",
        "(1.5147 = стандарт MPH, 1.9429 = одинаковая угловая скорость X/Y [необязательно])",
        "（1.5147 = MPH 标准，1.9429 = X/Y 角速度一致 [可选]）",
        "(1.5147 = MPH 기본값, 1.9429 = X/Y 각속도 일치 [선택])",
        "(1.5147 = معيار MPH، 1.9429 = سرعة زاوية X/Y متطابقة [اختياري])",
        "(1.5147 = standar MPH, 1.9429 = kecepatan sudut X/Y sama [opsional])",
        "(1.5147 = стандарт MPH, 1.9429 = однакова кутова швидкість X/Y [необов'язково])",
        "(1.5147 = πρότυπο MPH, 1.9429 = ίδια γωνιακή ταχύτητα X/Y [προαιρετικό])",
        "(1.5147 = MPH-standard, 1.9429 = samma vinkelhastighet X/Y [valfritt])",
        "(1.5147 = มาตรฐาน MPH, 1.9429 = ความเร็วเชิงมุม X/Y เท่ากัน [ตัวเลือก])",
        "(1.5147 = standard MPH, 1.9429 = stejná úhlová rychlost X/Y [volitelné])",
        "(1.5147 = MPH-standard, 1.9429 = samme vinkelhastighed X/Y [valgfrit])",
        "(1.5147 = MPH standardı, 1.9429 = X/Y açısal hız eşit [isteğe bağlı])",
        "(1.5147 = MPH-standard, 1.9429 = lik vinkelhastighet X/Y [valgfritt])",
        "(1.5147 = MPH alapérték, 1.9429 = azonos X/Y szögsebesség [opcionális])",
        "(1.5147 = MPH-oletus, 1.9429 = sama X/Y-kulmanopeus [valinnainen])",
        "(1.5147 = chuẩn MPH, 1.9429 = tốc độ góc X/Y bằng nhau [tùy chọn])",
        "(1.5147 = standard MPH, 1.9429 = taka sama prędkość kątowa X/Y [opcjonalnie])",
        "(1.5147 = standard MPH, 1.9429 = viteză unghiulară X/Y egală [opțional])"
    },
    {
        "metroidAimAdjustLabel",
        R"(<html><head/><body><p><b>入力しきい値 (デッドゾーン + スナップ)</b> (推奨: 0.01 [既定] または 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → そのまま (a=0: オフ = 1未満をすべて無視、スナップなし)<br/>エイムが思った通りに動かない場合は、この値を下げてみてください。</p></body></html>)",
        R"(<html><head/><body><p><b>Eingabeschwellenwert (Totzone + Snap)</b> (empfohlen: 0.01 [Standard] oder 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → unverändert (a=0: Aus = alles unter 1 ignorieren, kein Snap)<br/>Wenn sich die Zielbewegung nicht wie erwartet anfühlt, versuche diesen Wert zu senken.</p></body></html>)",
        R"(<html><head/><body><p><b>Umbral de entrada (zona muerta + snap)</b> (recomendado: 0.01 [predeterminado] o 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → sin cambios (a=0: desactivado = ignorar todo bajo 1, sin snap)<br/>Si la puntería no se comporta como esperas, prueba a bajar este valor.</p></body></html>)",
        R"(<html><head/><body><p><b>Seuil d'entrée (zone morte + snap)</b> (recommandé : 0.01 [par défaut] ou 0.5) : |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → inchangé (a=0 : désactivé = ignorer tout en dessous de 1, pas de snap)<br/>Si la visée ne réagit pas comme prévu, essayez de baisser cette valeur.</p></body></html>)",
        R"(<html><head/><body><p><b>Soglia di input (zona morta + snap)</b> (consigliato: 0.01 [predefinito] o 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → invariato (a=0: disattivato = ignora tutto sotto 1, nessuno snap)<br/>Se la mira non si comporta come previsto, prova ad abbassare questo valore.</p></body></html>)",
        R"(<html><head/><body><p><b>Invoerdrempel (dode zone + snap)</b> (aanbevolen: 0.01 [standaard] of 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → ongewijzigd (a=0: uit = alles onder 1 negeren, geen snap)<br/>Als het richten niet aanvoelt zoals verwacht, probeer deze waarde te verlagen.</p></body></html>)",
        R"(<html><head/><body><p><b>Limite de entrada (zona morta + snap)</b> (recomendado: 0.01 [predefinição] ou 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → inalterado (a=0: desativado = ignorar tudo abaixo de 1, sem snap)<br/>Se a mira não se comportar como esperado, tente diminuir este valor.</p></body></html>)",
        R"(<html><head/><body><p><b>Порог ввода (мёртвая зона + snap)</b> (рекомендуется: 0.01 [по умолчанию] или 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → без изменений (a=0: выкл. = игнорировать всё ниже 1, без snap)<br/>Если прицеливание ведёт себя не так, как ожидается, попробуйте уменьшить это значение.</p></body></html>)",
        R"(<html><head/><body><p><b>输入阈值（死区 + 吸附）</b>（推荐：0.01 [默认] 或 0.5）：|x|&lt;a → 0，a ≤ |x|&lt;1 → ±1，|x|≥1 → 保持不变（a=0：关闭 = 忽略 1 以下全部，无吸附）<br/>若瞄准手感不符合预期，可尝试降低此值。</p></body></html>)",
        R"(<html><head/><body><p><b>입력 임계값 (데드존 + 스냅)</b> (권장: 0.01 [기본] 또는 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → 그대로 (a=0: 끔 = 1 미만 모두 무시, 스냅 없음)<br/>조준이 예상과 다르게 느껴지면 이 값을 낮춰 보세요.</p></body></html>)",
        R"(<html><head/><body><p><b>عتبة الإدخال (منطقة ميتة + snap)</b> (موصى به: 0.01 [افتراضي] أو 0.5): |x|&lt;a → 0، a ≤ |x|&lt;1 → ±1، |x|≥1 → دون تغيير (a=0: إيقاف = تجاهل كل ما دون 1، بدون snap)<br/>إذا لم يتحرك التصويب كما تتوقع، جرّب خفض هذه القيمة.</p></body></html>)",
        R"(<html><head/><body><p><b>Ambang input (dead zone + snap)</b> (disarankan: 0.01 [bawaan] atau 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → tidak berubah (a=0: mati = abaikan semua di bawah 1, tanpa snap)<br/>Jika bidik tidak terasa seperti yang diharapkan, coba turunkan nilai ini.</p></body></html>)",
        R"(<html><head/><body><p><b>Поріг вводу (мертва зона + snap)</b> (рекомендовано: 0.01 [за замовч.] або 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → без змін (a=0: вимк. = ігнорувати все нижче 1, без snap)<br/>Якщо прицілювання поводиться не так, як очікується, спробуйте зменшити це значення.</p></body></html>)",
        R"(<html><head/><body><p><b>Όριο εισόδου (νεκρή ζώνη + snap)</b> (συνιστάται: 0.01 [προεπιλογή] ή 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → αμετάβλητο (a=0: ανενεργό = αγνόηση όλων κάτω από 1, χωρίς snap)<br/>Αν η σκόπευση δεν συμπεριφέρεται όπως αναμένετε, δοκιμάστε να μειώσετε αυτή την τιμή.</p></body></html>)",
        R"(<html><head/><body><p><b>Ingångströskel (dödzon + snap)</b> (rekommenderat: 0.01 [standard] eller 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → oförändrat (a=0: av = ignorera allt under 1, ingen snap)<br/>Om siktet inte känns som förväntat, prova att sänka detta värde.</p></body></html>)",
        R"(<html><head/><body><p><b>เกณฑ์อินพุต (dead zone + snap)</b> (แนะนำ: 0.01 [ค่าเริ่มต้น] หรือ 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → ไม่เปลี่ยน (a=0: ปิด = ละเว้นทั้งหมดต่ำกว่า 1 ไม่มี snap)<br/>หากการเล็งไม่เป็นไปตามที่คาด ลองลดค่านี้</p></body></html>)",
        R"(<html><head/><body><p><b>Vstupní práh (mrtvá zóna + snap)</b> (doporučeno: 0.01 [výchozí] nebo 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → beze změny (a=0: vyp = ignorovat vše pod 1, bez snap)<br/>Pokud míření neodpovídá očekávání, zkuste snížit tuto hodnotu.</p></body></html>)",
        R"(<html><head/><body><p><b>Inputtærskel (død zone + snap)</b> (anbefalet: 0.01 [standard] eller 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → uændret (a=0: fra = ignorer alt under 1, ingen snap)<br/>Hvis sigtet ikke føles som forventet, prøv at sænke denne værdi.</p></body></html>)",
        R"(<html><head/><body><p><b>Giriş eşiği (ölü bölge + snap)</b> (önerilen: 0.01 [varsayılan] veya 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → değişmez (a=0: kapalı = 1'in altındakileri yoksay, snap yok)<br/>Nişan beklendiği gibi hissettirmiyorsa bu değeri düşürmeyi deneyin.</p></body></html>)",
        R"(<html><head/><body><p><b>Inngangsterskel (dødsone + snap)</b> (anbefalt: 0.01 [standard] eller 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → uendret (a=0: av = ignorer alt under 1, ingen snap)<br/>Hvis siktet ikke føles som forventet, prøv å senke denne verdien.</p></body></html>)",
        R"(<html><head/><body><p><b>Bemeneti küszöb (holtsáv + snap)</b> (ajánlott: 0.01 [alapértelmezett] vagy 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → változatlan (a=0: ki = minden 1 alatti figyelmen kívül, nincs snap)<br/>Ha a célzás nem úgy viselkedik, mint vártad, próbáld csökkenteni ezt az értéket.</p></body></html>)",
        R"(<html><head/><body><p><b>Syötekynnys (kuollut alue + snap)</b> (suositeltu: 0.01 [oletus] tai 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → ennallaan (a=0: pois = ohita kaikki alle 1, ei snap)<br/>Jos tähtäys ei tunnu odotetulta, kokeile laskea tätä arvoa.</p></body></html>)",
        R"(<html><head/><body><p><b>Ngưỡng đầu vào (vùng chết + snap)</b> (khuyến nghị: 0.01 [mặc định] hoặc 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → giữ nguyên (a=0: tắt = bỏ qua mọi giá trị dưới 1, không snap)<br/>Nếu ngắm không như mong đợi, hãy thử giảm giá trị này.</p></body></html>)",
        R"(<html><head/><body><p><b>Próg wejścia (martwa strefa + snap)</b> (zalecane: 0.01 [domyślnie] lub 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → bez zmian (a=0: wył. = ignoruj wszystko poniżej 1, bez snap)<br/>Jeśli celowanie nie działa jak oczekiwano, spróbuj obniżyć tę wartość.</p></body></html>)",
        R"(<html><head/><body><p><b>Prag de intrare (zonă moartă + snap)</b> (recomandat: 0.01 [implicit] sau 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → neschimbat (a=0: oprit = ignoră tot sub 1, fără snap)<br/>Dacă țintirea nu se comportă cum vă așteptați, încercați să scădeți această valoare.</p></body></html>)"
    },
    {
        "lblMetroidLowLatencyAimDesc",
        "即時同期は低遅延ARM9フックで現在の照準を目標の照準へ同期し、照準基準を再構築します。MoonLikeエイムは小さなエイム移動を即時反映し、大きなジャンプだけ最大ステップ付きで追従します。MPHのエイム補間無効化が必要です。",
        R"(Sofortige Synchronisation synchronisiert die aktuelle Visierung über einen latenzarmen ARM9-Hook mit dem Ziel und baut die Visierungsbasis neu auf. MoonLike-Ziel spiegelt kleine Bewegungen sofort wider und folgt großen Sprüngen nur mit maximaler Schrittweite. Deaktivierung der MPH-Zielglättung erforderlich.)",
        R"(La sincronización inmediata sincroniza la mira actual con la objetivo mediante un hook ARM9 de baja latencia y reconstruye la base de puntería. MoonLike Aim refleja al instante movimientos pequeños y sigue saltos grandes solo con paso máximo. Requiere desactivar el suavizado de puntería de MPH.)",
        R"(La synchronisation immédiate aligne la visée actuelle sur la cible via un hook ARM9 à faible latence et reconstruit la base de visée. MoonLike Aim reflète immédiatement les petits mouvements et ne suit les grands sauts qu'avec un pas maximal. Nécessite de désactiver le lissage de visée MPH.)",
        R"(La sincronizzazione immediata allinea la mira attuale al bersaglio tramite un hook ARM9 a bassa latenza e ricostruisce la base di mira. MoonLike Aim riflette subito i piccoli movimenti e segue i grandi salti solo con passo massimo. Richiede la disattivazione dell'interpolazione mira MPH.)",
        R"(Directe synchronisatie synchroniseert de huidige visering met het doel via een ARM9-hook met lage latentie en bouwt de visierbasis opnieuw op. MoonLike Aim weerspiegelt kleine bewegingen direct en volgt grote sprongen alleen met maximale stap. Vereist uitschakeling van MPH-richtgladstrijking.)",
        R"(A sincronização imediata alinha a mira atual ao alvo via um hook ARM9 de baixa latência e reconstrói a base de mira. MoonLike Aim reflete de imediato movimentos pequenos e segue saltos grandes apenas com passo máximo. Requer desativar a suavização de mira MPH.)",
        R"(Немедленная синхронизация выравнивает текущий прицел с целью через низколатентный ARM9-хук и перестраивает базу прицеливания. MoonLike Aim мгновенно отражает малые движения и следует за большими скачками только с максимальным шагом. Требуется отключение сглаживания прицела MPH.)",
        "即时同步通过低延迟 ARM9 钩子将当前准星同步至目标并重建瞄准基准。MoonLike 瞄准会即时反映小幅移动，大幅跳跃仅以最大步长跟随。需要禁用 MPH 瞄准插值。",
        R"(즉시 동기화는 저지연 ARM9 훅으로 현재 조준을 목표에 맞추고 조준 기준을 재구성합니다. MoonLike Aim은 작은 조준 이동을 즉시 반영하고 큰 점프만 최대 스텝으로 따라갑니다. MPH 조준 보간 비활성화가 필요합니다.)",
        R"(المزامنة الفورية تُزامن التصويب الحالي مع الهدف عبر خطاف ARM9 منخفض التأخير وتعيد بناء أساس التصويب. MoonLike Aim يعكس الحركات الصغيرة فوراً ويتبع القفزات الكبيرة فقط بخطوة قصوى. يتطلب تعطيل استيفاء التصويب في MPH.)",
        R"(Sinkronisasi segera menyelaraskan bidik saat ini ke target melalui hook ARM9 latensi rendah dan membangun ulang basis bidik. MoonLike Aim langsung mencerminkan gerakan kecil dan hanya mengikuti lompatan besar dengan langkah maksimum. Memerlukan penonaktifan interpolasi bidik MPH.)",
        R"(Негайна синхронізація вирівнює поточний приціл з ціллю через низьколатентний ARM9-хук і перебудовує базу прицілювання. MoonLike Aim миттєво відображає малі рухи та слідує за великими стрибками лише з максимальним кроком. Потрібно вимкнути інтерполяцію прицілу MPH.)",
        R"(Η άμεση συγχρονισμός ευθυγραμμίζει την τρέχουσα σκόπευση με τον στόχο μέσω ARM9 hook χαμηλής καθυστέρησης και ανακατασκευάζει τη βάση σκόπευσης. Το MoonLike Aim αντικατοπτρίζει αμέσως μικρές κινήσεις και ακολουθεί μεγάλα άλματα μόνο με μέγιστο βήμα. Απαιτείται απενεργοποίηση της παρεμβολής σκόπευσης MPH.)",
        R"(Omedelbar synkronisering synkar aktuellt sikte med målet via en ARM9-hook med låg latens och bygger om siktbasisen. MoonLike Aim speglar små rörelser direkt och följer stora hopp endast med maximalt steg. Kräver inaktivering av MPH-siktsinterpolering.)",
        R"(การซิงค์ทันทีจะซิงค์การเล็งปัจจุบันกับเป้าหมายผ่าน ARM9 hook ความหน่วงต่ำและสร้างฐานการเล็งใหม่ MoonLike Aim สะท้อนการเคลื่อนไหวเล็กน้อยทันทีและตามการกระโดดใหญ่ด้วยขั้นสูงสุดเท่านั้น ต้องปิดการ interpolate การเล็ง MPH)",
        R"(Okamžitá synchronizace sladí aktuální míření s cílem přes nízko-latenční ARM9 hook a obnoví základ míření. MoonLike Aim okamžitě odráží malé pohyby a velké skoky sleduje jen s maximálním krokem. Vyžaduje vypnutí interpolace míření MPH.)",
        R"(Øjeblikkelig synkronisering synkroniserer nuværende sigte med målet via en ARM9-hook med lav latency og genopbygger sigtebasen. MoonLike Aim afspejler små bevægelser med det samme og følger store spring kun med maksimalt trin. Kræver deaktivering af MPH-sigteinterpolation.)",
        R"(Anında senkronizasyon, düşük gecikmeli ARM9 hook ile mevcut nişanı hedefe hizalar ve nişan tabanını yeniden oluşturur. MoonLike Aim küçük hareketleri anında yansıtır, büyük sıçramaları yalnızca maksimum adımla takip eder. MPH nişan interpolasyonunun devre dışı bırakılması gerekir.)",
        R"(Umiddelbar synkronisering synkroniserer nåværende sikt med målet via en ARM9-hook med lav latency og bygger siktebasen på nytt. MoonLike Aim speiler små bevegelser umiddelbart og følger store hopp bare med maksimalt steg. Krever deaktivering av MPH-sikteinterpolering.)",
        R"(Az azonnali szinkronizáció az alacsony késleltetésű ARM9 hookon keresztül igazítja az aktuális célzást a célpontra és újraépíti a célzási bázist. A MoonLike Aim azonnal tükrözi a kis mozgásokat, a nagy ugrásokat csak maximális lépéssel követi. Az MPH célzás-interpoláció kikapcsolása szükséges.)",
        R"(Välitön synkronointi kohdistaa nykyisen tähtäyksen kohteeseen matalan viiveen ARM9-hookin kautta ja rakentaa tähtäysperustan uudelleen. MoonLike Aim heijastaa pienet liikkeet heti ja seuraa suuria hyppyjä vain maksimiaskeleella. Vaatii MPH-tähtäyksen interpoloinnin poistamisen käytöstä.)",
        R"(Đồng bộ tức thì căn chỉnh ngắm hiện tại với mục tiêu qua hook ARM9 độ trễ thấp và tái tạo cơ sở ngắm. MoonLike Aim phản ánh ngay chuyển động nhỏ và chỉ theo bước nhảy lớn với bước tối đa. Cần tắt nội suy ngắm MPH.)",
        R"(Natychmiastowa synchronizacja wyrównuje bieżące celowanie z celem przez hook ARM9 o niskim opóźnieniu i odbudowuje bazę celowania. MoonLike Aim natychmiast odzwierciedla małe ruchy i podąża za dużymi skokami tylko z maksymalnym krokiem. Wymaga wyłączenia interpolacji celowania MPH.)",
        R"(Sincronizarea imediată aliniază ținta curentă cu obiectivul printr-un hook ARM9 cu latență redusă și reconstruiește baza de țintire. MoonLike Aim reflectă imediat mișcările mici și urmează salturile mari doar cu pas maxim. Necesită dezactivarea interpolării țintirii MPH.)"
    },
    {
        "lblMetroidZoomAimScaleDesc",
        "ゲーム本来のズーム状態が有効な間だけ適用します。100%で通常のマウス感度、100%未満でズーム中のエイムが遅くなり、100%超で速くなります。",
        R"(Gilt nur, solange der native Zoom-Zustand des Spiels aktiv ist. 100 % = normale Mausempfindlichkeit, unter 100 % wird das Zielen beim Zoomen langsamer, über 100 % schneller.)",
        R"(Solo se aplica mientras el zoom nativo del juego está activo. 100 % = sensibilidad normal del ratón; por debajo de 100 % la puntería al hacer zoom es más lenta; por encima, más rápida.)",
        R"(S'applique uniquement tant que le zoom natif du jeu est actif. 100 % = sensibilité souris normale ; en dessous de 100 %, la visée au zoom est plus lente ; au-dessus, plus rapide.)",
        R"(Si applica solo mentre lo zoom nativo del gioco è attivo. 100% = sensibilità mouse normale; sotto il 100% la mira durante lo zoom è più lenta; sopra il 100%, più veloce.)",
        R"(Geldt alleen zolang de native zoomtoestand van het spel actief is. 100% = normale muisgevoeligheid; onder 100% is het richten tijdens zoomen trager; boven 100% sneller.)",
        R"(Aplica-se apenas enquanto o zoom nativo do jogo estiver ativo. 100% = sensibilidade normal do rato; abaixo de 100% a mira ao ampliar fica mais lenta; acima de 100%, mais rápida.)",
        R"(Применяется только пока активно нативное состояние зума игры. 100% = обычная чувствительность мыши; ниже 100% прицел при зуме медленнее; выше 100% — быстрее.)",
        "仅在游戏原生缩放状态生效时应用。100% = 正常鼠标灵敏度；低于 100% 时缩放中瞄准更慢；高于 100% 则更快。",
        "게임 기본 줌 상태가 활성인 동안만 적용됩니다. 100% = 일반 마우스 감도, 100% 미만이면 줌 중 조준이 느려지고, 100% 초과면 빨라집니다.",
        R"(يُطبَّق فقط طالما أن حالة التكبير الأصلية للعبة نشطة. 100% = حساسية الفأرة العادية؛ أقل من 100% يبطئ التصويب أثناء التكبير؛ أكثر من 100% يسرّعه.)",
        R"(Hanya berlaku selama status zoom asli game aktif. 100% = sensitivitas mouse normal; di bawah 100% bidik saat zoom lebih lambat; di atas 100% lebih cepat.)",
        R"(Застосовується лише поки активний нативний стан зуму гри. 100% = звичайна чутливість миші; нижче 100% приціл під час зуму повільніший; вище 100% — швидший.)",
        R"(Ισχύει μόνο όσο είναι ενεργή η εγγενής κατάσταση ζουμ του παιχνιδιού. 100% = κανονική ευαισθησία ποντικιού· κάτω από 100% η σκόπευση στο ζουμ είναι πιο αργή· πάνω από 100% πιο γρήγορη.)",
        R"(Gäller endast medan spelets inbyggda zoomläge är aktivt. 100% = normal mushastighet; under 100% blir siktet långsammare vid zoom; över 100% snabbare.)",
        "ใช้เฉพาะเมื่อสถานะซูมดั้งเดิมของเกมเปิดอยู่ 100% = ความไวเมาส์ปกติ ต่ำกว่า 100% การเล็งขณะซูมช้าลง สูงกว่า 100% เร็วขึ้น",
        R"(Platí pouze, dokud je aktivní nativní stav zoomu hry. 100 % = normální citlivost myši; pod 100 % je míření při zoomu pomalejší; nad 100 % rychlejší.)",
        R"(Gælder kun mens spillets native zoomtilstand er aktiv. 100% = normal musfølsomhed; under 100% er sigtet langsommere ved zoom; over 100% hurtigere.)",
        R"(Yalnızca oyunun yerel zoom durumu etkinken uygulanır. %100 = normal fare hassasiyeti; %100'ün altında zoom sırasında nişan yavaşlar; üstünde hızlanır.)",
        R"(Gjelder bare mens spillets native zoomtilstand er aktiv. 100 % = normal musfølsomhet; under 100 % er siktet langsommere ved zoom; over 100 % raskere.)",
        R"(Csak amíg a játék natív zoom állapota aktív. 100% = normál egérérzékenység; 100% alatt a zoom közbeni célzás lassabb; 100% felett gyorsabb.)",
        R"(Voimassa vain pelin alkuperäisen zoom-tilan ollessa aktiivinen. 100 % = normaali hiiren herkkyys; alle 100 % tähtäys zoomauksessa on hitaampaa; yli 100 % nopeampaa.)",
        R"(Chỉ áp dụng khi trạng thái zoom gốc của game đang bật. 100% = độ nhạy chuột bình thường; dưới 100% ngắm khi zoom chậm hơn; trên 100% nhanh hơn.)",
        R"(Stosuje się tylko, gdy aktywny jest natywny stan zoomu gry. 100% = normalna czułość myszy; poniżej 100% celowanie przy zoomie jest wolniejsze; powyżej 100% szybsze.)",
        R"(Se aplică doar cât timp starea nativă de zoom a jocului este activă. 100% = sensibilitate normală a mouse-ului; sub 100% țintirea la zoom este mai lentă; peste 100% mai rapidă.)"
    },
    {
        "lblMetroidNativeAimHookModeDesc",
        "PostFold書き込みはタッチ入力処理の後でフックし、spec108=0 (サムス/カンデン/ノクサス/スパイア) を含むすべてのトランスフォームをカバーします。開発者ビルド専用です。",
        R"(PostFold-Schreibzugriff hookt nach der Touch-Eingabeverarbeitung und deckt alle Transformationen ab, einschließlich spec108=0 (Samus/Kanden/Noxus/Spire). Nur für Entwickler-Builds.)",
        R"(La escritura PostFold engancha tras el procesamiento táctil y cubre todas las transformaciones, incluido spec108=0 (Samus/Kanden/Noxus/Spire). Solo en compilaciones de desarrollador.)",
        R"(L'écriture PostFold s'accroche après le traitement tactile et couvre toutes les transformations, y compris spec108=0 (Samus/Kanden/Noxus/Spire). Réservé aux builds développeur.)",
        R"(La scrittura PostFold si aggancia dopo l'elaborazione dell'input touch e copre tutte le trasformazioni, incluso spec108=0 (Samus/Kanden/Noxus/Spire). Solo per build sviluppatore.)",
        R"(PostFold-schrijven hookt na de touch-invoerverwerking en dekt alle transformaties af, inclusief spec108=0 (Samus/Kanden/Noxus/Spire). Alleen voor ontwikkelaarsbuilds.)",
        R"(A escrita PostFold engancha após o processamento de entrada tátil e cobre todas as transformações, incluindo spec108=0 (Samus/Kanden/Noxus/Spire). Apenas em compilações de programador.)",
        R"(Запись PostFold перехватывает после обработки сенсорного ввода и охватывает все трансформации, включая spec108=0 (Samus/Kanden/Noxus/Spire). Только для сборок разработчика.)",
        "PostFold 写入在触控输入处理之后挂钩，覆盖所有变形，包括 spec108=0（Samus/Kanden/Noxus/Spire）。仅限开发者构建。",
        "PostFold 쓰기는 터치 입력 처리 후에 후크하며 spec108=0(Samus/Kanden/Noxus/Spire)을 포함한 모든 변형을 다룹니다. 개발자 빌드 전용입니다.",
        R"(كتابة PostFold تتصل بعد معالجة إدخال اللمس وتغطي جميع التحولات، بما في ذلك spec108=0 (Samus/Kanden/Noxus/Spire). لبناءات المطور فقط.)",
        R"(Penulisan PostFold hook setelah pemrosesan input sentuh dan mencakup semua transformasi, termasuk spec108=0 (Samus/Kanden/Noxus/Spire). Hanya untuk build pengembang.)",
        R"(Запис PostFold перехоплює після обробки сенсорного вводу та охоплює всі трансформації, включно з spec108=0 (Samus/Kanden/Noxus/Spire). Лише для збірок розробника.)",
        R"(Η εγγραφή PostFold συνδέεται μετά την επεξεργασία αφής και καλύπτει όλες τις μεταμορφώσεις, συμπεριλαμβανομένου spec108=0 (Samus/Kanden/Noxus/Spire). Μόνο για builds προγραμματιστών.)",
        R"(PostFold-skrivning hookar efter pek-inmatningsbearbetning och täcker alla transformationer, inklusive spec108=0 (Samus/Kanden/Noxus/Spire). Endast för utvecklarbyggen.)",
        R"(การเขียน PostFold hook หลังประมวลผลอินพุตสัมผัสและครอบคลุม transform ทั้งหมด รวม spec108=0 (Samus/Kanden/Noxus/Spire) สำหรับ build นักพัฒนาเท่านั้น)",
        R"(Zápis PostFold hookuje po zpracování dotykového vstupu a pokrývá všechny transformace včetně spec108=0 (Samus/Kanden/Noxus/Spire). Pouze pro vývojářské buildy.)",
        R"(PostFold-skrivning hooker efter touch-inputbehandling og dækker alle transformationer, inkl. spec108=0 (Samus/Kanden/Noxus/Spire). Kun til udviklerbuilds.)",
        R"(PostFold yazımı dokunmatik giriş işlemesinden sonra hook yapar ve spec108=0 (Samus/Kanden/Noxus/Spire) dahil tüm dönüşümleri kapsar. Yalnızca geliştirici derlemeleri için.)",
        R"(PostFold-skriving hooker etter touch-inndatahåndtering og dekker alle transformasjoner, inkludert spec108=0 (Samus/Kanden/Noxus/Spire). Kun for utviklerbygg.)",
        R"(A PostFold írás az érintéses bemenet feldolgozása után hookol, és lefedi az összes transzformációt, beleértve a spec108=0-t (Samus/Kanden/Noxus/Spire). Csak fejlesztői buildhez.)",
        R"(PostFold-kirjoitus hookkaa kosketussyötteen käsittelyn jälkeen ja kattaa kaikki muunnokset, mukaan lukien spec108=0 (Samus/Kanden/Noxus/Spire). Vain kehittäjäbuildien käyttöön.)",
        R"(Ghi PostFold hook sau xử lý đầu vào cảm ứng và bao phủ mọi biến hình, gồm spec108=0 (Samus/Kanden/Noxus/Spire). Chỉ dành cho bản build nhà phát triển.)",
        R"(Zapis PostFold hookuje po przetwarzaniu wejścia dotykowego i obejmuje wszystkie transformacje, w tym spec108=0 (Samus/Kanden/Noxus/Spire). Tylko dla buildów deweloperskich.)",
        R"(Scrierea PostFold se conectează după procesarea intrării tactile și acoperă toate transformările, inclusiv spec108=0 (Samus/Kanden/Noxus/Spire). Doar pentru build-uri de dezvoltator.)"
    },
    {
        "lblMetroidDirectAltFormTransformDesc",
        "新方式は短いネイティブ入力ゲートをゲーム本来の変形要求処理へリダイレクトします。旧方式は従来のタッチ/メニュー模擬による変形処理を使います。",
        R"(Die neue Methode leitet ein kurzes natives Eingabe-Gate zur ingame-Transformationsanforderung um. Die alte Methode nutzt die bisherige Touch-/Menü-Simulation.)",
        R"(El método nuevo redirige una breve compuerta de entrada nativa al procesamiento de transformación del juego. El método antiguo usa la simulación táctil/menú tradicional.)",
        R"(La nouvelle méthode redirige une courte porte d'entrée native vers le traitement de transformation du jeu. L'ancienne méthode utilise la simulation tactile/menu traditionnelle.)",
        R"(Il nuovo metodo reindirizza un breve gate di input nativo all'elaborazione trasformazione del gioco. Il metodo vecchio usa la simulazione touch/menu tradizionale.)",
        R"(De nieuwe methode leidt een korte native invoerpoort om naar de transformatieverwerking van het spel. De oude methode gebruikt de traditionele touch-/menusimulatie.)",
        R"(O método novo redireciona um curto portão de entrada nativo para o processamento de transformação do jogo. O método antigo usa a simulação tátil/menu tradicional.)",
        R"(Новый метод перенаправляет короткий нативный входной шлюз к обработке трансформации игры. Старый метод использует традиционную эмуляцию касаний/меню.)",
        "新方式将短暂的原生输入门重定向至游戏原生变形请求处理。旧方式使用传统的触控/菜单模拟变形处理。",
        "새 방식은 짧은 네이티브 입력 게이트를 게임 기본 변형 요청 처리로 리디렉션합니다. 구 방식은 기존 터치/메뉴 시뮬레이션 변형 처리를 사용합니다.",
        R"(الطريقة الجديدة تعيد توجيه بوابة إدخال أصلية قصيرة إلى معالجة طلب التحول الأصلية للعبة. الطريقة القديمة تستخدم محاكاة اللمس/القائمة التقليدية.)",
        R"(Metode baru mengalihkan gerbang input native pendek ke pemrosesan permintaan transformasi asli game. Metode lama menggunakan simulasi sentuh/menu tradisional.)",
        R"(Новий метод перенаправляє короткий нативний вхідний шлюз до обробки запиту трансформації гри. Старий метод використовує традиційну емуляцію дотику/меню.)",
        R"(Η νέα μέθοδος ανακατευθύνει μια σύντομη εγγενή πύλη εισόδου στην επεξεργασία μεταμόρφωσης του παιχνιδιού. Η παλιά μέθοδος χρησιμοποιεί την παραδοσιακή προσομοίωση αφής/μενού.)",
        R"(Den nya metoden omdirigerar en kort inbyggd ingångsport till spelets inbyggda transformationsbearbetning. Den gamla metoden använder traditionell pek-/menysimulering.)",
        R"(วิธีใหม่เปลี่ยนเส้นทางเกตอินพุต native สั้นๆ ไปยังการประมวลผลคำขอ transform ดั้งเดิมของเกม วิธีเก่าใช้การจำลองสัมผัส/เมนูแบบเดิม)",
        R"(Nová metoda přesměruje krátkou nativní vstupní bránu na zpracování transformačního požadavku hry. Stará metoda používá tradiční simulaci dotyku/menu.)",
        R"(Den nye metode omdirigerer en kort native inputgate til spillets native transformationsbehandling. Den gamle metode bruger traditionel touch-/menusimulation.)",
        R"(Yeni yöntem kısa bir yerel giriş kapısını oyunun yerel dönüşüm isteği işlemesine yönlendirir. Eski yöntem geleneksel dokunmatik/menü simülasyonunu kullanır.)",
        R"(Den nye metoden omdirigerer en kort native inngangsport til spillets native transformasjonsbehandling. Den gamle metoden bruker tradisjonell touch-/menysimulering.)",
        R"(Az új módszer egy rövid natív bemeneti kaput irányít át a játék natív transzformációs kérés-feldolgozásához. A régi módszer a hagyományos érintés/menü szimulációt használja.)",
        R"(Uusi menetelmä ohjaa lyhyen natiivin syöteportin pelin natiiviin muunnospyyntökäsittelyyn. Vanha menetelmä käyttää perinteistä kosketus-/valikkosimulaatiota.)",
        R"(Cách mới chuyển hướng cổng đầu vào native ngắn tới xử lý yêu cầu biến hình gốc của game. Cách cũ dùng mô phỏng cảm ứng/menu truyền thống.)",
        R"(Nowa metoda przekierowuje krótką natywną bramkę wejścia do natywnego przetwarzania żądania transformacji gry. Stara metoda używa tradycyjnej symulacji dotyku/menu.)",
        R"(Metoda nouă redirecționează un scurt gate de intrare nativ către procesarea cererii de transformare nativă a jocului. Metoda veche folosește simularea tactilă/meniu tradițională.)"
    },
    {
        "lblMetroidFixWifiBitsetDesc",
        R"(フレンド/ライバルの有効スロットを追跡する疑似64bitビットセットの不具合を修正します。
JP1.0 / US1.0 / EU1.0ではスロット32〜59が正しく扱われず、一部のフレンド/ライバルがオンラインで見えなくなることがあります。
JP1.1 / US1.1 / EU1.1 / KR1.0と同じ、バイト単位のビットセット処理に置き換えます。
(JP1.0 / US1.0 / EU1.0のみ。他のROMバージョンには影響しません))",
        R"(Behebt einen Fehler im pseudo-64-Bit-Bitset, das aktive Freund-/Rivalen-Slots verfolgt.
Bei JP1.0 / US1.0 / EU1.0 werden Slots 32–59 nicht korrekt behandelt; einige Freunde/Rivalen erscheinen online nicht.
Ersetzt die Verarbeitung durch die byteweise Bitset-Logik wie bei JP1.1 / US1.1 / EU1.1 / KR1.0.
(Nur JP1.0 / US1.0 / EU1.0. Andere ROM-Versionen sind nicht betroffen.))",
        R"(Corrige un fallo en el pseudo bitset de 64 bits que rastrea las ranuras activas de amigos/rivales.
En JP1.0 / US1.0 / EU1.0, las ranuras 32–59 no se gestionan bien y algunos amigos/rivales no aparecen en línea.
Sustituye el procesamiento por la lógica de bitset por bytes como en JP1.1 / US1.1 / EU1.1 / KR1.0.
(Solo JP1.0 / US1.0 / EU1.0. No afecta a otras versiones de ROM.))",
        R"(Corrige un défaut du pseudo bitset 64 bits qui suit les emplacements actifs d'amis/rivaux.
Sur JP1.0 / US1.0 / EU1.0, les emplacements 32–59 ne sont pas gérés correctement ; certains amis/rivaux n'apparaissent pas en ligne.
Remplace le traitement par la logique bitset octet par octet comme JP1.1 / US1.1 / EU1.1 / KR1.0.
(JP1.0 / US1.0 / EU1.0 uniquement. N'affecte pas les autres versions de ROM.))",
        R"(Corregge un difetto del pseudo bitset a 64 bit che tiene traccia degli slot attivi di amici/rivali.
Su JP1.0 / US1.0 / EU1.0, gli slot 32–59 non sono gestiti correttamente; alcuni amici/rivali non compaiono online.
Sostituisce l'elaborazione con la logica bitset byte per byte come in JP1.1 / US1.1 / EU1.1 / KR1.0.
(Solo JP1.0 / US1.0 / EU1.0. Non influisce su altre versioni ROM.))",
        R"(Herstelt een fout in het pseudo-64-bit-bitset dat actieve vriend-/rivaal-slots bijhoudt.
Bij JP1.0 / US1.0 / EU1.0 worden slots 32–59 niet correct behandeld; sommige vrienden/rivalen verschijnen online niet.
Vervangt de verwerking door bytewise bitset-logica zoals bij JP1.1 / US1.1 / EU1.1 / KR1.0.
(Alleen JP1.0 / US1.0 / EU1.0. Andere ROM-versies worden niet beïnvloed.))",
        R"(Corrige uma falha no pseudo bitset de 64 bits que rastreia os slots ativos de amigos/rivais.
Em JP1.0 / US1.0 / EU1.0, os slots 32–59 não são tratados corretamente; alguns amigos/rivais não aparecem online.
Substitui o processamento pela lógica de bitset por bytes como em JP1.1 / US1.1 / EU1.1 / KR1.0.
(Apenas JP1.0 / US1.0 / EU1.0. Não afeta outras versões de ROM.))",
        R"(Исправляет ошибку псевдо-64-битного битсета, отслеживающего активные слоты друзей/соперников.
В JP1.0 / US1.0 / EU1.0 слоты 32–59 обрабатываются некорректно; некоторые друзья/соперники не отображаются онлайн.
Заменяет обработку на побайтовую логику битсета как в JP1.1 / US1.1 / EU1.1 / KR1.0.
(Только JP1.0 / US1.0 / EU1.0. Другие версии ROM не затрагиваются.))",
        R"(修复跟踪好友/对手有效槽位的伪 64 位位集缺陷。
在 JP1.0 / US1.0 / EU1.0 中，槽位 32–59 处理不正确，部分好友/对手可能无法在线显示。
替换为与 JP1.1 / US1.1 / EU1.1 / KR1.0 相同的按字节位集处理。
（仅 JP1.0 / US1.0 / EU1.0。不影响其他 ROM 版本。）)",
        R"(친구/라이벌 활성 슬롯을 추적하는 의사 64비트 비트셋 오류를 수정합니다.
JP1.0 / US1.0 / EU1.0에서는 슬롯 32–59가 올바르게 처리되지 않아 일부 친구/라이벌이 온라인에 보이지 않을 수 있습니다.
JP1.1 / US1.1 / EU1.1 / KR1.0과 같은 바이트 단위 비트셋 처리로 교체합니다.
(JP1.0 / US1.0 / EU1.0만 해당. 다른 ROM 버전에는 영향 없음.))",
        R"(يصلح خللاً في bitset وهمي 64 بت يتتبع فتحات الأصدقاء/الخصوم النشطة.
في JP1.0 / US1.0 / EU1.0 لا تُعالَج الفتحات 32–59 بشكل صحيح؛ قد لا يظهر بعض الأصدقاء/الخصوم على الإنترنت.
يستبدل المعالجة بمنطق bitset بايت ببايت كما في JP1.1 / US1.1 / EU1.1 / KR1.0.
(JP1.0 / US1.0 / EU1.0 فقط. لا يؤثر على إصدارات ROM الأخرى.))",
        R"(Memperbaiki cacat pseudo bitset 64-bit yang melacak slot teman/lawan aktif.
Pada JP1.0 / US1.0 / EU1.0, slot 32–59 tidak ditangani dengan benar; beberapa teman/lawan mungkin tidak muncul online.
Mengganti pemrosesan dengan logika bitset per byte seperti JP1.1 / US1.1 / EU1.1 / KR1.0.
(Hanya JP1.0 / US1.0 / EU1.0. Tidak memengaruhi versi ROM lain.))",
        R"(Виправляє помилку псевдо-64-бітного бітсета, що відстежує активні слоти друзів/суперників.
У JP1.0 / US1.0 / EU1.0 слоти 32–59 обробляються некоректно; деякі друзі/суперники не відображаються онлайн.
Замінює обробку на побайтову логіку бітсета як у JP1.1 / US1.1 / EU1.1 / KR1.0.
(Лише JP1.0 / US1.0 / EU1.0. Інші версії ROM не зачіпаються.))",
        R"(Διορθώνει ελάττωμα στο ψευδο bitset 64 bit που παρακολουθεί ενεργές θέσεις φίλων/αντιπάλων.
Σε JP1.0 / US1.0 / EU1.0, οι θέσεις 32–59 δεν διαχειρίζονται σωστά· μερικοί φίλοι/αντίπαλοι μπορεί να μην εμφανίζονται online.
Αντικαθιστά την επεξεργασία με λογική bitset ανά byte όπως στο JP1.1 / US1.1 / EU1.1 / KR1.0.
(Μόνο JP1.0 / US1.0 / EU1.0. Δεν επηρεάζει άλλες εκδόσεις ROM.))",
        R"(Åtgärdar ett fel i pseudo-64-bitars-bitset som spårar aktiva vän-/motståndarplatser.
I JP1.0 / US1.0 / EU1.0 hanteras platser 32–59 inte korrekt; vissa vänner/motståndare syns inte online.
Ersätter bearbetningen med bytvis bitset-logik som i JP1.1 / US1.1 / EU1.1 / KR1.0.
(Endast JP1.0 / US1.0 / EU1.0. Påverkar inte andra ROM-versioner.))",
        R"(แก้ข้อบกพร่อง pseudo bitset 64 บิตที่ติดตามสล็อตเพื่อน/คู่แข่งที่ใช้งาน
ใน JP1.0 / US1.0 / EU1.0 สล็อต 32–59 ไม่ถูกจัดการอย่างถูกต้อง เพื่อน/คู่แข่งบางคนอาจไม่แสดงออนไลน์
แทนที่การประมวลผลด้วยตรรกะ bitset รายไบต์เหมือน JP1.1 / US1.1 / EU1.1 / KR1.0
(เฉพาะ JP1.0 / US1.0 / EU1.0 ไม่กระทบ ROM เวอร์ชันอื่น))",
        R"(Opravuje chybu pseudo 64bitového bitsetu sledujícího aktivní sloty přátel/rivalů.
U JP1.0 / US1.0 / EU1.0 se sloty 32–59 nezpracovávají správně; někteří přátelé/rivalové se online nezobrazí.
Nahrazuje zpracování bytovou bitset logikou jako u JP1.1 / US1.1 / EU1.1 / KR1.0.
(Pouze JP1.0 / US1.0 / EU1.0. Neovlivňuje jiné verze ROM.))",
        R"(Retter en fejl i pseudo-64-bit bitset, der sporer aktive ven-/rival-pladser.
I JP1.0 / US1.0 / EU1.0 håndteres pladser 32–59 ikke korrekt; nogle venner/rivaler vises ikke online.
Erstatter behandlingen med bytvis bitset-logik som i JP1.1 / US1.1 / EU1.1 / KR1.0.
(Kun JP1.0 / US1.0 / EU1.0. Påvirker ikke andre ROM-versioner.))",
        R"(Aktif arkadaş/rakip slotlarını izleyen sahte 64 bit bitset hatasını düzeltir.
JP1.0 / US1.0 / EU1.0'da slot 32–59 doğru işlenmez; bazı arkadaşlar/rakipler çevrimiçi görünmeyebilir.
İşlemeyi JP1.1 / US1.1 / EU1.1 / KR1.0'daki gibi bayt bayt bitset mantığıyla değiştirir.
(Yalnızca JP1.0 / US1.0 / EU1.0. Diğer ROM sürümlerini etkilemez.))",
        R"(Retter en feil i pseudo-64-bit bitset som sporer aktive venn-/rivalplasser.
I JP1.0 / US1.0 / EU1.0 håndteres plasser 32–59 ikke riktig; noen venner/rivaler vises ikke online.
Erstatter behandlingen med bytvis bitset-logikk som i JP1.1 / US1.1 / EU1.1 / KR1.0.
(Kun JP1.0 / US1.0 / EU1.0. Påvirker ikke andre ROM-versjoner.))",
        R"(Javít egy hibát a pseudo 64 bites bitsetben, amely az aktív barát/rivális slotokat követi.
JP1.0 / US1.0 / EU1.0 esetén a 32–59 slotok nem kezelődnek helyesen; egyes barátok/riválisok nem jelennek meg online.
A feldolgozást byte-onkénti bitset logikára cseréli, mint JP1.1 / US1.1 / EU1.1 / KR1.0.
(Csak JP1.0 / US1.0 / EU1.0. Más ROM verziókat nem érint.))",
        R"(Korjaa pseudo-64-bittisen bittijoukon vian, joka seuraa aktiivisia kaveri-/vastustajapaikkoja.
JP1.0 / US1.0 / EU1.0 -versioissa paikat 32–59 eivät käsitellä oikein; osa kavereista/vastustajista ei näy verkossa.
Korvaa käsittelyn tavutasoisella bittijoukkologiikalla kuten JP1.1 / US1.1 / EU1.1 / KR1.0.
(Vain JP1.0 / US1.0 / EU1.0. Ei vaikuta muihin ROM-versioihin.))",
        R"(Sửa lỗi pseudo bitset 64 bit theo dõi slot bạn bè/đối thủ đang hoạt động.
Trong JP1.0 / US1.0 / EU1.0, slot 32–59 không được xử lý đúng; một số bạn bè/đối thủ có thể không hiện online.
Thay xử lý bằng logic bitset theo byte như JP1.1 / US1.1 / EU1.1 / KR1.0.
(Chỉ JP1.0 / US1.0 / EU1.0. Không ảnh hưởng ROM khác.))",
        R"(Naprawia błąd pseudo 64-bitowego bitsetu śledzącego aktywne sloty znajomych/rywali.
W JP1.0 / US1.0 / EU1.0 sloty 32–59 nie są obsługiwane poprawnie; część znajomych/rywali może nie pojawiać się online.
Zastępuje przetwarzanie logiką bitsetu bajt po bajcie jak w JP1.1 / US1.1 / EU1.1 / KR1.0.
(Tylko JP1.0 / US1.0 / EU1.0. Nie wpływa na inne wersje ROM.))",
        R"(Corectează un defect al pseudo bitset-ului de 64 biți care urmărește sloturile active de prieteni/rivali.
În JP1.0 / US1.0 / EU1.0, sloturile 32–59 nu sunt gestionate corect; unii prieteni/rivali pot să nu apară online.
Înlocuiește procesarea cu logica bitset pe octeți ca la JP1.1 / US1.1 / EU1.1 / KR1.0.
(Doar JP1.0 / US1.0 / EU1.0. Nu afectează alte versiuni ROM.))"
    },
    {
        "lblMetroidFixShadowFreezeDesc",
        "アイスウェーブの最後の命中/ミス角度判定を、元の横方向範囲チェックは保ったまま完全な3D判定へ置き換えて、シャドウフリーズ問題を修正します。",
        R"(Ersetzt die letzte Treffer-/Fehlwinkelprüfung von Ice Wave durch eine vollständige 3D-Prüfung bei beibehaltener horizontaler Reichweitenkontrolle und behebt das Shadow-Freeze-Problem.)",
        R"(Sustituye la comprobación final de ángulo de acierto/fallo de Ice Wave por una evaluación 3D completa, manteniendo el control horizontal original, y corrige el problema de shadow freeze.)",
        R"(Remplace la dernière vérification d'angle touché/raté d'Ice Wave par une évaluation 3D complète tout en conservant le contrôle horizontal d'origine, corrigeant le problème de shadow freeze.)",
        R"(Sostituisce l'ultimo controllo angolo colpo/mancato di Ice Wave con una valutazione 3D completa mantenendo il controllo orizzontale originale, correggendo il problema shadow freeze.)",
        R"(Vervangt de laatste treffer-/mis-hoekcontrole van Ice Wave door een volledige 3D-evaluatie met behoud van de oorspronkelijke horizontale controle, en lost het shadow-freeze-probleem op.)",
        R"(Substitui a verificação final de ângulo de acerto/erro do Ice Wave por uma avaliação 3D completa, mantendo o controlo horizontal original, e corrige o problema de shadow freeze.)",
        R"(Заменяет последнюю проверку угла попадания/промаха Ice Wave полной 3D-оценкой с сохранением исходной горизонтальной проверки, исправляя проблему shadow freeze.)",
        "在保留原有横向范围检查的同时，将 Ice Wave 最后一次命中/未命中角度判定替换为完整 3D 判定，修复 shadow freeze 问题。",
        "Ice Wave의 마지막 명중/빗나감 각도 판정을 원래 가로 범위 검사는 유지한 채 완전한 3D 판정으로 바꿔 shadow freeze 문제를 수정합니다.",
        R"(يستبدل فحص زاوية الإصابة/الإخفاق الأخير لـ Ice Wave بتقييم ثلاثي الأبعاد كامل مع الإبقاء على فحص النطاق الأفقي الأصلي، ويصلح مشكلة shadow freeze.)",
        R"(Mengganti pemeriksaan sudut hit/miss terakhir Ice Wave dengan evaluasi 3D penuh sambil mempertahankan pemeriksaan rentang horizontal asli, memperbaiki masalah shadow freeze.)",
        R"(Замінює останню перевірку кута влучення/промаху Ice Wave повною 3D-оцінкою зі збереженням вихідної горизонтальної перевірки, виправляючи проблему shadow freeze.)",
        R"(Αντικαθιστά τον τελευταίο έλεγχο γωνίας επιτυχίας/αποτυχίας του Ice Wave με πλήρη 3D αξιολόγηση διατηρώντας τον αρχικό οριζόντιο έλεγχο, διορθώνοντας το πρόβλημα shadow freeze.)",
        R"(Ersätter Ice Waves sista träff-/missvinkelkontroll med full 3D-utvärdering med bibehållen horisontell kontroll och åtgärdar shadow freeze-problemet.)",
        R"(แทนที่การตรวจมุม hit/miss ครั้งสุดท้ายของ Ice Wave ด้วยการประเมิน 3D เต็มรูปแบบ โดยคงการตรวจช่วงแนวนอนเดิม แก้ปัญหา shadow freeze)",
        R"(Nahrazuje poslední kontrolu úhlu zásahu/miss u Ice Wave plným 3D vyhodnocením se zachováním původní horizontální kontroly a opravuje problém shadow freeze.)",
        R"(Erstatter Ice Waves sidste hit/miss-vinkelkontrol med fuld 3D-evaluering med bevarelse af den oprindelige horisontale kontrol og retter shadow freeze-problemet.)",
        R"(Ice Wave'in son isabet/ıskalama açısı kontrolünü orijinal yatay aralık kontrolü korunarak tam 3D değerlendirme ile değiştirir ve shadow freeze sorununu düzeltir.)",
        R"(Erstatter Ice Waves siste treff-/miss-vinkelkontroll med full 3D-evaluering med beholdt horisontal kontroll og retter shadow freeze-problemet.)",
        R"(Az Ice Wave utolsó találat/miss szögellenőrzését teljes 3D értékelésre cseréli az eredeti vízszintes tartományellenőrzés megtartásával, javítva a shadow freeze problémát.)",
        R"(Korvaa Ice Waven viimeisen osuma/huti-kulmatarkistuksen täydellä 3D-arvioinnilla säilyttäen alkuperäisen vaakasuuntaisen tarkistuksen ja korjaa shadow freeze -ongelman.)",
        R"(Thay kiểm tra góc trúng/trượt cuối của Ice Wave bằng đánh giá 3D đầy đủ, giữ kiểm tra phạm vi ngang gốc, sửa lỗi shadow freeze.)",
        R"(Zastępuje ostatnią kontrolę kąta trafienia/pudła Ice Wave pełną oceną 3D z zachowaniem oryginalnej kontroli poziomej, naprawiając problem shadow freeze.)",
        R"(Înlocuiește ultima verificare a unghiului lovit/ratat al Ice Wave cu evaluare 3D completă păstrând controlul orizontal original, corectând problema shadow freeze.)"
    },
    {
        "lblMetroidFixNoxusBladePersistenceDesc",
        R"(ノクサスのヴォーサイズ/ブレード攻撃が死亡とリスポーン後もダメージを与え続ける不具合を修正します。ブレード攻撃中にノクサスが死亡するとAlt攻撃タイマーがクリアされず、復活後もブレードの当たり判定が残ります。この修正では死亡した瞬間にタイマーをクリアします。)",
        R"(Behebt einen Fehler, bei dem Noxus' Vorsaize-/Klingenangriff nach Tod und Respawn weiter Schaden verursacht. Stirbt Noxus während eines Klingenangriffs, wird der Alt-Angriffs-Timer nicht gelöscht und die Trefferzone bleibt nach der Wiederbelebung. Dieser Fix löscht den Timer im Moment des Todes.)",
        R"(Corrige un fallo en el que el ataque de cuchilla/Vorsaize de Noxus sigue dañando tras morir y reaparecer. Si Noxus muere durante el ataque de cuchilla, el temporizador de ataque Alt no se borra y la hitbox persiste tras revivir. Este arreglo borra el temporizador en el instante de la muerte.)",
        R"(Corrige un défaut où l'attaque lame/Vorsaize de Noxus continue d'infliger des dégâts après la mort et le respawn. Si Noxus meurt pendant l'attaque lame, le minuteur d'attaque Alt n'est pas effacé et la hitbox persiste après la résurrection. Ce correctif efface le minuteur au moment de la mort.)",
        R"(Corregge un difetto per cui l'attacco lama/Vorsaize di Noxus continua a infliggere danni dopo morte e respawn. Se Noxus muore durante l'attacco lama, il timer attacco Alt non viene azzerato e la hitbox persiste dopo la rinascita. Questa correzione azzera il timer al momento della morte.)",
        R"(Herstelt een fout waarbij Noxus' Vorsaize-/mesaanval na dood en respawn schade blijft toebrengen. Sterft Noxus tijdens een mesaanval, wordt de Alt-aanvaltimer niet gewist en blijft de hitbox na herleving bestaan. Deze fix wist de timer op het moment van de dood.)",
        R"(Corrige uma falha em que o ataque de lâmina/Vorsaize de Noxus continua a causar dano após morrer e reaparecer. Se Noxus morrer durante o ataque de lâmina, o temporizador de ataque Alt não é limpo e a hitbox persiste após reviver. Esta correção limpa o temporizador no instante da morte.)",
        R"(Исправляет ошибку, при которой атака клинком/Vorsaize Ноксуса продолжает наносить урон после смерти и возрождения. Если Ноксус умирает во время атаки клинком, таймер Alt-атаки не сбрасывается и хитбокс сохраняется после воскрешения. Это исправление сбрасывает таймер в момент смерти.)",
        "修复 Noxus 的 Vorsaize/刀刃攻击在死亡并重出生后仍继续造成伤害的缺陷。Noxus 在刀刃攻击中死亡时 Alt 攻击计时器不会被清除，复活后刀刃碰撞体仍会残留。此修复会在死亡瞬间清除计时器。",
        R"(Noxus의 Vorsaize/블레이드 공격이 사망 및 리스폰 후에도 계속 피해를 주는 오류를 수정합니다. 블레이드 공격 중 Noxus가 사망하면 Alt 공격 타이머가 지워지지 않아 부활 후에도 블레이드 히트박스가 남습니다. 이 수정은 사망 순간 타이머를 지웁니다.)",
        R"(يصلح خللاً حيث هجوم Vorsaize/النصل لـ Noxus يستمر في إلحاق الضرر بعد الموت وإعادة الظهور. إذا مات Noxus أثناء هجوم النصل، لا يُمسح مؤقت هجوم Alt وتبقى hitbox بعد الإحياء. هذا الإصلاح يمسح المؤقت لحظة الموت.)",
        R"(Memperbaiki cacat di mana serangan Vorsaize/pedang Noxus terus memberi damage setelah mati dan respawn. Jika Noxus mati saat serangan pedang, timer serangan Alt tidak dihapus dan hitbox tetap setelah hidup kembali. Perbaikan ini menghapus timer saat kematian.)",
        R"(Виправляє помилку, коли атака Vorsaize/клинком Noxus продовжує завдавати шкоди після смерті та відродження. Якщо Noxus помирає під час атаки клинком, таймер Alt-атаки не скидається і hitbox залишається після воскресіння. Це виправлення скидає таймер у момент смерті.)",
        R"(Διορθώνει ελάττωμα όπου η επίθεση Vorsaize/λεπίδας του Noxus συνεχίζει να προκαλεί ζημιά μετά τον θάνατο και respawn. Αν ο Noxus πεθάνει κατά τη διάρκεια επίθεσης λεπίδας, ο χρονοδιακόπτης Alt επίθεσης δεν καθαρίζεται και η hitbox παραμένει μετά την αναβίωση. Αυτή η διόρθωση καθαρίζει τον χρονοδιακόπτη τη στιγμή του θανάτου.)",
        R"(Åtgärdar ett fel där Noxus Vorsaize-/bladattack fortsätter skada efter död och respawn. Om Noxus dör under bladattack rensas inte Alt-attacktimern och hitboxen kvarstår efter återupplivning. Denna fix rensar timern vid dödsögonblicket.)",
        R"(แก้ข้อบกพร่องที่การโจมตี Vorsaize/ใบมีดของ Noxus ยังทำดamage หลังตายและ respawn หาก Noxus ตายระหว่างโจมตีใบมีด ตัวจับเวลา Alt attack ไม่ถูกล้างและ hitbox คงอยู่หลังฟื้น การแก้ไขนี้ล้างตัวจับเวลาเมื่อตาย)",
        R"(Opravuje chybu, kdy Noxusův Vorsaize/čepelový útok pokračuje v poškozování po smrti a respawnu. Pokud Noxus zemře během útoku čepelí, Alt-útokový timer se nevymaže a hitbox přetrvá po oživení. Tato oprava vymaže timer v okamžiku smrti.)",
        R"(Retter en fejl, hvor Noxus' Vorsaize-/klingeangreb fortsætter med at skade efter død og respawn. Hvis Noxus dør under klingeangreb, ryddes Alt-angrebstimeren ikke og hitboxen består efter genoplivning. Denne rettelse rydder timeren i dødsøjeblikket.)",
        R"(Noxus'un Vorsaize/kılıç saldırısının ölüm ve respawn sonrası hasar vermeye devam ettiği hatayı düzeltir. Noxus kılıç saldırısı sırasında ölürse Alt saldırı zamanlayıcısı temizlenmez ve dirilişten sonra hitbox kalır. Bu düzeltme ölüm anında zamanlayıcıyı temizler.)",
        R"(Retter en feil der Noxus' Vorsaize-/bladangrep fortsetter å skade etter død og respawn. Hvis Noxus dør under bladangrep, tømmes ikke Alt-angrepstimeren og hitboxen består etter gjenoppliving. Denne fiksen tømmer timeren i dødsøyeblikket.)",
        R"(Javít egy hibát, ahol Noxus Vorsaize/pengés támadása halál és respawn után is sebez. Ha Noxus a pengés támadás közben meghal, az Alt támadás időzítője nem törlődik és a hitbox megmarad feltámadás után. Ez a javítás a halál pillanatában törli az időzítőt.)",
        R"(Korjaa vian, jossa Noxusin Vorsaize/terähyökkäys jatkaa vahingoittamista kuoleman ja respawnin jälkeen. Jos Noxus kuolee teräshyökkäyksen aikana, Alt-hyökkäysajastinta ei tyhjennetä ja hitbox jää eloonnousun jälkeen. Tämä korjaus tyhjentää ajastimen kuoleman hetkellä.)",
        R"(Sửa lỗi tấn công Vorsaize/lưỡi của Noxus vẫn gây sát thương sau khi chết và hồi sinh. Nếu Noxus chết khi đang tấn công lưỡi, bộ đếm Alt attack không bị xóa và hitbox còn sau khi hồi sinh. Bản sửa này xóa bộ đếm ngay lúc chết.)",
        R"(Naprawia błąd, w którym atak Vorsaize/ostrzem Noxusa nadal zadaje obrażenia po śmierci i respawnie. Gdy Noxus ginie podczas ataku ostrzem, timer ataku Alt nie jest czyszczony i hitbox pozostaje po wskrzeszeniu. Ta poprawka czyści timer w momencie śmierci.)",
        R"(Corectează un defect în care atacul Vorsaize/lamă al lui Noxus continuă să provoace daune după moarte și respawn. Dacă Noxus moare în timpul atacului cu lamă, timerul atacului Alt nu se șterge și hitbox-ul persistă după reînviere. Această corecție șterge timerul în momentul morții.)"
    },
    {
        "lblMetroidFixNoxusBladePersistenceWarning",
        "この修正はまだ不安定です。特に韓国版では、場合によってブレードが残り続けることがあります。",
        "Dieser Fix ist noch instabil. Besonders in der koreanischen Version kann die Klinge unter Umständen bestehen bleiben.",
        "Esta corrección aún es inestable. Especialmente en la versión coreana, la cuchilla puede persistir en algunos casos.",
        "Ce correctif est encore instable. Surtout sur la version coréenne, la lame peut parfois persister.",
        "Questa correzione è ancora instabile. Soprattutto nella versione coreana, la lama può talvolta persistere.",
        "Deze fix is nog instabiel. Vooral in de Koreaanse versie kan het mes onder omstandigheden blijven bestaan.",
        "Esta correção ainda é instável. Especialmente na versão coreana, a lâmina pode persistir em alguns casos.",
        "Это исправление пока нестабильно. Особенно в корейской версии клинок иногда может сохраняться.",
        "此修复仍不稳定。尤其在韩版中，刀刃有时可能继续残留。",
        "이 수정은 아직 불안정합니다. 특히 한국판에서는 경우에 따라 블레이드가 남을 수 있습니다.",
        "هذا الإصلاح لا يزال غير مستقر. خاصة في النسخة الكورية، قد يستمر النصل في بعض الأحيان.",
        "Perbaikan ini masih belum stabil. Terutama di versi Korea, pedang kadang masih bisa bertahan.",
        "Це виправлення ще нестабільне. Особливо в корейській версії клинок іноді може залишатися.",
        "Αυτή η διόρθωση είναι ακόμα ασταθής. Ειδικά στην κορεατική έκδοση, η λεπίδα μπορεί μερικές φορές να παραμένει.",
        "Denna fix är fortfarande instabil. Särskilt i den koreanska versionen kan bladet ibland kvarstå.",
        "การแก้ไขนี้ยังไม่เสถียร โดยเฉพาะเวอร์ชันเกาหลี ใบมีดอาจยังคงอยู่ในบางกรณี",
        "Tato oprava je stále nestabilní. Zejména v korejské verzi může čepel někdy přetrvávat.",
        "Denne rettelse er stadig ustabil. Især i den koreanske version kan klingen nogle gange fortsætte.",
        "Bu düzeltme hâlâ kararsız. Özellikle Kore sürümünde kılıç bazen kalabilir.",
        "Denne fiksen er fortsatt ustabil. Spesielt i den koreanske versjonen kan bladet noen ganger vedvare.",
        "Ez a javítás még instabil. Főleg a koreai verzióban a pengének néha megmaradhat.",
        "Tämä korjaus on vielä epävakaa. Erityisesti korealaisversiossa terä saattaa joskus jäädä.",
        "Bản sửa này vẫn chưa ổn định. Đặc biệt bản Hàn, lưỡi đôi khi vẫn còn.",
        "Ta poprawka jest nadal niestabilna. Szczególnie w wersji koreańskiej ostrze czasem może pozostać.",
        "Această corecție este încă instabilă. Mai ales în versiunea coreeană, lama poate persista uneori."
    },
    {
        "lblMetroidUseFirmwareLanguageDesc",
        R"(<html><body>
DSファームウェアの言語設定を読み取り、その言語で表示するようゲームにパッチします。<br>
EU ROMには隠し日本語オプションがあり、DSファームウェアを日本語にすると有効になります。<br><br>
<b>EU / US ROM:</b> すべてのモードで動作します (マルチプレイとアドベンチャー)。<br>
<b>JP ROM:</b> マルチプレイのみ動作します。<font color="red"><b>有効にするとアドベンチャーモードはプレイできません。</b></font><br>
<b>KR ROM:</b> まだ未対応です。
</body></html>)",
        R"(<html><body>
Liest die Spracheinstellung der DS-Firmware aus und patcht das Spiel entsprechend.
<br>
EU-ROMs haben eine versteckte Japanisch-Option, die aktiv wird, wenn die DS-Firmware auf Japanisch steht.
<br><br>
<b>EU / US ROM:</b> Funktioniert in allen Modi (Multiplayer und Abenteuer).
<br>
<b>JP ROM:</b> Nur im Multiplayer. <font color="red"><b>Wenn aktiviert, ist der Abenteuermodus nicht spielbar.</b></font>
<br>
<b>KR ROM:</b> Noch nicht unterstützt.
</body></html>)",
        R"(<html><body>
Lee el idioma de la firmware del DS y parchea el juego para mostrarlo en ese idioma.
<br>
Las ROM EU tienen una opción oculta de japonés que se activa con firmware DS en japonés.
<br><br>
<b>ROM EU / US:</b> Funciona en todos los modos (multijugador y aventura).
<br>
<b>ROM JP:</b> Solo multijugador. <font color="red"><b>Al activarlo, el modo aventura no es jugable.</b></font>
<br>
<b>ROM KR:</b> Aún no compatible.
</body></html>)",
        R"(<html><body>
Lit la langue de la firmware DS et patch le jeu pour l'afficher dans cette langue.
<br>
Les ROM EU ont une option japonaise cachée, active si la firmware DS est en japonais.
<br><br>
<b>ROM EU / US :</b> Fonctionne dans tous les modes (multijoueur et aventure).
<br>
<b>ROM JP :</b> Multijoueur uniquement. <font color="red"><b>Si activé, le mode aventure n'est pas jouable.</b></font>
<br>
<b>ROM KR :</b> Pas encore pris en charge.
</body></html>)",
        R"(<html><body>
Legge l'impostazione lingua del firmware DS e patcha il gioco per visualizzarlo in quella lingua.
<br>
Le ROM EU hanno un'opzione giapponese nascosta, attiva se il firmware DS è in giapponese.
<br><br>
<b>ROM EU / US:</b> Funziona in tutte le modalità (multigiocatore e avventura).
<br>
<b>ROM JP:</b> Solo multigiocatore. <font color="red"><b>Se attivato, la modalità avventura non è giocabile.</b></font>
<br>
<b>ROM KR:</b> Non ancora supportato.
</body></html>)",
        R"(<html><body>
Leest de taalinstelling van de DS-firmware en patcht het spel om die taal te tonen.
<br>
EU-ROM's hebben een verborgen Japanse optie die actief wordt als de DS-firmware op Japans staat.
<br><br>
<b>EU / US ROM:</b> Werkt in alle modi (multiplayer en avontuur).
<br>
<b>JP ROM:</b> Alleen multiplayer. <font color="red"><b>Indien ingeschakeld, is de avontuurmodus niet speelbaar.</b></font>
<br>
<b>KR ROM:</b> Nog niet ondersteund.
</body></html>)",
        R"(<html><body>
Lê a definição de idioma do firmware DS e aplica um patch ao jogo para o exibir nesse idioma.
<br>
As ROM EU têm uma opção japonesa oculta, ativa quando o firmware DS está em japonês.
<br><br>
<b>ROM EU / US:</b> Funciona em todos os modos (multijogador e aventura).
<br>
<b>ROM JP:</b> Apenas multijogador. <font color="red"><b>Se ativado, o modo aventura não é jogável.</b></font>
<br>
<b>ROM KR:</b> Ainda não suportado.
</body></html>)",
        R"(<html><body>
Считывает языковую настройку прошивки DS и патчит игру для отображения на этом языке.
<br>
В EU ROM есть скрытая японская опция, активная при японской прошивке DS.
<br><br>
<b>EU / US ROM:</b> Работает во всех режимах (мультиплеер и приключение).
<br>
<b>JP ROM:</b> Только мультиплеер. <font color="red"><b>При включении режим приключения недоступен.</b></font>
<br>
<b>KR ROM:</b> Пока не поддерживается.
</body></html>)",
        R"(<html><body>
读取 DS 固件语言设置，并 patch 游戏以该语言显示。
<br>
EU ROM 有隐藏日语选项，DS 固件设为日语时生效。
<br><br>
<b>EU / US ROM：</b>所有模式可用（多人与冒险）。
<br>
<b>JP ROM：</b>仅多人可用。<font color="red"><b>启用后无法游玩冒险模式。</b></font>
<br>
<b>KR ROM：</b>暂不支持。
</body></html>)",
        R"(<html><body>
DS 펌웨어 언어 설정을 읽어 해당 언어로 표시되도록 게임을 패치합니다.
<br>
EU ROM에는 숨겨진 일본어 옵션이 있으며 DS 펌웨어가 일본어일 때 활성화됩니다.
<br><br>
<b>EU / US ROM:</b> 모든 모드에서 동작합니다(멀티플레이 및 어드벤처).
<br>
<b>JP ROM:</b> 멀티플레이만 동작합니다. <font color="red"><b>활성화하면 어드벤처 모드를 플레이할 수 없습니다.</b></font>
<br>
<b>KR ROM:</b> 아직 미지원입니다.
</body></html>)",
        R"(<html><body>
يقرأ إعداد لغة firmware الـ DS ويُعدّل اللعبة لعرضها بهذه اللغة.
<br>
ROM EU لديها خيار ياباني مخفي يُفعَّل عند ضبط firmware DS على اليابانية.
<br><br>
<b>ROM EU / US:</b> يعمل في جميع الأوضاع (متعدد اللاعبين والمغامرة).
<br>
<b>ROM JP:</b> متعدد اللاعبين فقط. <font color="red"><b>عند التفعيل، وضع المغامرة غير قابل للعب.</b></font>
<br>
<b>ROM KR:</b> غير مدعوم بعد.
</body></html>)",
        R"(<html><body>
Membaca pengaturan bahasa firmware DS dan mem-patch game agar ditampilkan dalam bahasa tersebut.
<br>
ROM EU memiliki opsi Jepang tersembunyi yang aktif jika firmware DS diset ke Jepang.
<br><br>
<b>ROM EU / US:</b> Berfungsi di semua mode (multipemain dan petualangan).
<br>
<b>ROM JP:</b> Hanya multipemain. <font color="red"><b>Jika diaktifkan, mode petualangan tidak dapat dimainkan.</b></font>
<br>
<b>ROM KR:</b> Belum didukung.
</body></html>)",
        R"(<html><body>
Зчитує мовне налаштування прошивки DS і патчить гру для відображення цією мовою.
<br>
У EU ROM є прихована японська опція, активна при японській прошивці DS.
<br><br>
<b>EU / US ROM:</b> Працює у всіх режимах (мультиплеер і пригода).
<br>
<b>JP ROM:</b> Лише мультиплеер. <font color="red"><b>Якщо увімкнено, режим пригоди недоступний.</b></font>
<br>
<b>KR ROM:</b> Поки не підтримується.
</body></html>)",
        R"(<html><body>
Διαβάζει τη ρύθμιση γλώσσας του firmware DS και κάνει patch το παιχνίδι για εμφάνιση σε αυτή τη γλώσσα.
<br>
Τα EU ROM έχουν κρυφή ιαπωνική επιλογή, ενεργή όταν το firmware DS είναι στα ιαπωνικά.
<br><br>
<b>EU / US ROM:</b> Λειτουργεί σε όλες τις λειτουργίες (πολλαπλών παικτών και περιπέτεια).
<br>
<b>JP ROM:</b> Μόνο πολλαπλών παικτών. <font color="red"><b>Αν ενεργοποιηθεί, η λειτουργία περιπέτειας δεν είναι playable.</b></font>
<br>
<b>KR ROM:</b> Δεν υποστηρίζεται ακόμα.
</body></html>)",
        R"(<html><body>
Läser DS-firmwarens språkinställning och patchar spelet för att visa det språket.
<br>
EU-ROM har ett dolt japanskt alternativ som aktiveras när DS-firmwaren är på japanska.
<br><br>
<b>EU / US ROM:</b> Fungerar i alla lägen (multiplayer och äventyr).
<br>
<b>JP ROM:</b> Endast multiplayer. <font color="red"><b>Om aktiverat går äventyrsläget inte att spela.</b></font>
<br>
<b>KR ROM:</b> Stöds inte ännu.
</body></html>)",
        R"(<html><body>
อ่านการตั้งค่าภาษา firmware DS และ patch เกมให้แสดงภาษานั้น
<br>
ROM EU มีตัวเลือกญี่ปุ่นที่ซ่อนอยู่ เปิดใช้เมื่อ firmware DS เป็นญี่ปุ่น
<br><br>
<b>ROM EU / US:</b> ใช้ได้ทุกโหมด (มัลติเพลเยอร์และผจญภัย)
<br>
<b>ROM JP:</b> มัลติเพลเยอร์เท่านั้น <font color="red"><b>เมื่อเปิดใช้ โหมดผจญภัยเล่นไม่ได้</b></font>
<br>
<b>ROM KR:</b> ยังไม่รองรับ
</body></html>)",
        R"(<html><body>
Načte jazykové nastavení firmware DS a upraví hru pro zobrazení v tomto jazyce.
<br>
EU ROM mají skrytou japonskou volbu aktivní při japonské firmware DS.
<br><br>
<b>EU / US ROM:</b> Funguje ve všech režimech (multiplayer a dobrodružství).
<br>
<b>JP ROM:</b> Pouze multiplayer. <font color="red"><b>Při zapnutí není režim dobrodružství hratelný.</b></font>
<br>
<b>KR ROM:</b> Zatím nepodporováno.
</body></html>)",
        R"(<html><body>
Læser DS-firmwarens sprogindstilling og patcher spillet til at vise det sprog.
<br>
EU-ROM har en skjult japansk mulighed, aktiv når DS-firmware er på japansk.
<br><br>
<b>EU / US ROM:</b> Virker i alle tilstande (multiplayer og eventyr).
<br>
<b>JP ROM:</b> Kun multiplayer. <font color="red"><b>Hvis aktiveret, kan eventyrtilstand ikke spilles.</b></font>
<br>
<b>KR ROM:</b> Endnu ikke understøttet.
</body></html>)",
        R"(<html><body>
DS firmware dil ayarını okur ve oyunu o dilde göstermek için yamalar.
<br>
EU ROM'larda DS firmware Japonca olduğunda etkinleşen gizli Japonca seçeneği vardır.
<br><br>
<b>EU / US ROM:</b> Tüm modlarda çalışır (çok oyunculu ve macera).
<br>
<b>JP ROM:</b> Yalnızca çok oyunculu. <font color="red"><b>Etkinleştirilirse macera modu oynanamaz.</b></font>
<br>
<b>KR ROM:</b> Henüz desteklenmiyor.
</body></html>)",
        R"(<html><body>
Leser DS-firmwarens språkinnstilling og patcher spillet for å vise det språket.
<br>
EU-ROM har et skjult japansk alternativ som aktiveres når DS-firmware er på japansk.
<br><br>
<b>EU / US ROM:</b> Fungerer i alle moduser (flerspiller og eventyr).
<br>
<b>JP ROM:</b> Kun flerspiller. <font color="red"><b>Hvis aktivert, kan eventyrmodus ikke spilles.</b></font>
<br>
<b>KR ROM:</b> Støttes ikke ennå.
</body></html>)",
        R"(<html><body>
Beolvassa a DS firmware nyelvi beállítását és patch-eli a játékot, hogy azon a nyelven jelenjen meg.
<br>
Az EU ROM-okban rejtett japán opció van, amely aktív, ha a DS firmware japán.
<br><br>
<b>EU / US ROM:</b> Minden módban működik (multiplayer és kaland).
<br>
<b>JP ROM:</b> Csak multiplayer. <font color="red"><b>Ha bekapcsolva, a kaland mód nem játszható.</b></font>
<br>
<b>KR ROM:</b> Még nem támogatott.
</body></html>)",
        R"(<html><body>
Lukee DS-firmwaren kieliasetuksen ja patchaa pelin näyttämään kyseistä kieltä.
<br>
EU-ROM:issa on piilotettu japanilainen vaihtoehto, joka aktivoituu kun DS-firmware on japaniksi.
<br><br>
<b>EU / US ROM:</b> Toimii kaikissa tiloissa (moninpeli ja seikkailu).
<br>
<b>JP ROM:</b> Vain moninpeli. <font color="red"><b>Käytössä seikkailutilaa ei voi pelata.</b></font>
<br>
<b>KR ROM:</b> Ei vielä tuettu.
</body></html>)",
        R"(<html><body>
Đọc cài đặt ngôn ngữ firmware DS và patch game để hiển thị bằng ngôn ngữ đó.
<br>
ROM EU có tùy chọn tiếng Nhật ẩn, kích hoạt khi firmware DS là tiếng Nhật.
<br><br>
<b>ROM EU / US:</b> Hoạt động mọi chế độ (nhiều người chơi và phiêu lưu).
<br>
<b>ROM JP:</b> Chỉ nhiều người chơi. <font color="red"><b>Bật thì không chơi được chế độ phiêu lưu.</b></font>
<br>
<b>ROM KR:</b> Chưa hỗ trợ.
</body></html>)",
        R"(<html><body>
Odczytuje ustawienie języka firmware DS i patchuje grę, aby wyświetlała ten język.
<br>
ROM EU mają ukrytą opcję japońską aktywną przy japońskiej firmware DS.
<br><br>
<b>ROM EU / US:</b> Działa we wszystkich trybach (multiplayer i przygoda).
<br>
<b>ROM JP:</b> Tylko multiplayer. <font color="red"><b>Po włączeniu tryb przygody jest niegrywalny.</b></font>
<br>
<b>ROM KR:</b> Jeszcze nieobsługiwane.
</body></html>)",
        R"(<html><body>
Citește setarea de limbă a firmware-ului DS și patch-uiește jocul pentru afișare în acea limbă.
<br>
ROM-urile EU au opțiune japoneză ascunsă, activă când firmware-ul DS este în japoneză.
<br><br>
<b>ROM EU / US:</b> Funcționează în toate modurile (multiplayer și aventură).
<br>
<b>ROM JP:</b> Doar multiplayer. <font color="red"><b>Dacă este activat, modul aventură nu este jucabil.</b></font>
<br>
<b>ROM KR:</b> Încă neacceptat.
</body></html>)"
    },
    {
        "lblMetroidShowHeadshotOnlineDesc",
        "Wi-Fi/オンライン対戦中でも、単独のヘッドショット通知を強制的に表示します。",
        "Erzwingt die Anzeige separater Kopfschuss-Benachrichtigungen auch während Wi-Fi-/Online-Matches.",
        "Fuerza la visualización de notificaciones de headshot independientes también en partidas Wi-Fi/en línea.",
        "Force l'affichage de notifications de headshot distinctes même en match Wi-Fi/en ligne.",
        "Forza la visualizzazione di notifiche headshot separate anche durante le partite Wi-Fi/online.",
        "Forceert de weergave van aparte headshot-meldingen, ook tijdens Wi-Fi-/onlinematches.",
        "Força a exibição de notificações de headshot separadas também em partidas Wi-Fi/online.",
        "Принудительно показывает отдельные уведомления о хедшотах даже в Wi-Fi/онлайн-матчах.",
        "在 Wi-Fi/在线对战中也强制显示单独的爆头通知。",
        "Wi-Fi/온라인 대전 중에도 별도의 헤드샷 알림을 강제로 표시합니다.",
        "يفرض عرض إشعارات headshot منفصلة حتى أثناء مباريات Wi-Fi/عبر الإنترنت.",
        "Memaksa menampilkan notifikasi headshot terpisah bahkan selama pertandingan Wi-Fi/online.",
        "Примусово показує окремі сповіщення про headshot навіть у Wi-Fi/онлайн-матчах.",
        "Επιβάλλει εμφάνιση ξεχωριστών ειδοποιήσεων headshot ακόμα και σε αγώνες Wi-Fi/online.",
        "Tvingar visning av separata headshot-meddelanden även under Wi-Fi-/onlinematcher.",
        "บังคับแสดงการแจ้งเตือน headshot แยกแม้ในแมตช์ Wi-Fi/ออนไลน์",
        "Vynucuje zobrazení samostatných headshot oznámení i během Wi-Fi/online zápasů.",
        "Tvinger visning af separate headshot-notifikationer også under Wi-Fi-/onlinematches.",
        "Wi-Fi/çevrimiçi maçlarda bile ayrı headshot bildirimlerinin gösterilmesini zorlar.",
        "Tvinger visning av separate headshot-varsler også under Wi-Fi-/onlinematcher.",
        "Külön headshot értesítések megjelenítését kényszeríti ki Wi-Fi/online meccsek alatt is.",
        "Pakottaa erillisten headshot-ilmoitusten näyttämisen myös Wi-Fi-/verkkopeleissä.",
        "Buộc hiển thị thông báo headshot riêng ngay cả trong trận Wi-Fi/trực tuyến.",
        "Wymusza wyświetlanie osobnych powiadomień o headshocie także w meczach Wi-Fi/online.",
        "Forțează afișarea notificărilor headshot separate chiar și în meciuri Wi-Fi/online."
    },
    {
        "lblMetroidShowEnemyHpMeterOnlineDesc",
        "HP情報は基本的に更新されないため信頼性は高くありません。誰に当てたかを見る大まかな目安として使えます。",
        R"(HP-Infos werden grundsätzlich selten aktualisiert, daher ist die Zuverlässigkeit begrenzt. Als grobe Orientierung, wer getroffen wurde.)",
        R"(La información de HP rara vez se actualiza, así que no es muy fiable. Sirve como referencia aproximada de a quién has impactado.)",
        "Les infos de PV sont rarement mises à jour, donc peu fiables. Utile comme indication grossière de qui a été touché.",
        R"(Le info HP vengono raramente aggiornate, quindi non sono molto affidabili. Utili come indicazione approssimativa di chi è stato colpito.)",
        "HP-info wordt zelden bijgewerkt, dus de betrouwbaarheid is beperkt. Nuttig als ruwe indicatie van wie is geraakt.",
        R"(A informação de HP raramente é atualizada, por isso não é muito fiável. Serve como referência aproximada de quem foi atingido.)",
        "Данные HP редко обновляются, поэтому ненадёжны. Полезны как грубая ориентировка, кого вы поразили.",
        "HP 信息基本不会更新，因此可靠性不高。可作为查看命中对象的大致参考。",
        "HP 정보는 거의 갱신되지 않아 신뢰도가 높지 않습니다. 누구를 맞췄는지 보는 대략적인 기준으로 쓸 수 있습니다.",
        "معلومات HP نادراً ما تُحدَّث، لذا الموثوقية محدودة. مفيدة كدليل تقريبي لمن أصبت.",
        "Info HP jarang diperbarui, jadi keandalannya terbatas. Berguna sebagai petunjuk kasar siapa yang kena.",
        "Дані HP рідко оновлюються, тому надійність обмежена. Корисні як груба орієнтовка, кого ви вразили.",
        R"(Οι πληροφορίες HP σπάνια ενημερώνονται, άρα η αξιοπιστία είναι περιορισμένη. Χρήσιμες ως χονδρική ένδειξη ποιος χτυπήθηκε.)",
        "HP-info uppdateras sällan, så tillförlitligheten är begränsad. Användbart som grov vägledning om vem som träffades.",
        "ข้อมูล HP แทบไม่อัปเดต ความน่าเชื่อถือจึงจำกัด ใช้เป็นตัวชี้คร่าวๆ ว่าโดนใคร",
        "Informace o HP se zřídka aktualizují, spolehlivost je tedy omezená. Užitečné jako hrubá orientace, koho jste zasáhli.",
        "HP-info opdateres sjældent, så pålideligheden er begrænset. Nyttigt som grov vejledning om, hvem der blev ramt.",
        R"(HP bilgisi nadiren güncellenir, güvenilirlik sınırlıdır. Kime isabet ettiğinize dair kabaca rehber olarak kullanılabilir.)",
        "HP-info oppdateres sjelden, så påliteligheten er begrenset. Nyttig som grov veiledning om hvem som ble truffet.",
        "A HP infó ritkán frissül, ezért a megbízhatóság korlátozott. Durva támpontként hasznos, kit találtál el.",
        "HP-tietoja päivitetään harvoin, joten luotettavuus on rajallinen. Hyödyllinen karkeana ohjeena, kenet osuit.",
        "Thông tin HP hiếm khi cập nhật nên độ tin cậy thấp. Dùng làm tham chiếu sơ bộ ai bị trúng.",
        R"(Informacje o HP rzadko się aktualizują, więc wiarygodność jest ograniczona. Przydatne jako przybliżona wskazówka, kogo trafiłeś.)",
        "Informațiile HP se actualizează rar, deci fiabilitatea este limitată. Utile ca indicație aproximativă a cui ai lovit."
    },
    {
        "lblMetroidExpandStageMatrixDesc",
        R"(<html><body>
安定確認済みのステージ/モード組み合わせを5個解放します。ステージ選択メニューが開いている間、自動で適用されます。<br><br>
<b>バウンティ</b><br>
&nbsp;&nbsp;Fuel Stack — オクトリスあり、アイテムあり<br>
&nbsp;&nbsp;Alinos Gateway — オクトリスあり、坂の下に得意武器が出現<br><br>
<b>キャプチャー</b><br>
&nbsp;&nbsp;Celestial Gateway — オクトリスあり、ジャンパーとアイテムあり<br><br>
<b>ディフェンダー</b><br>
&nbsp;&nbsp;High Ground — アイテムあり、マグモールエリア左下にディフェンダーゾーンリング<br>
&nbsp;&nbsp;Elder Passage — アイテムあり、マグモールエリア左下にディフェンダーゾーンリング
</body></html>)",
        R"(<html><body>
Schaltet 5 stabile Stage-/Modus-Kombinationen frei. Wird automatisch angewendet, solange das Stage-Auswahlmenü offen ist.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — mit Octo, mit Items<br>
&nbsp;&nbsp;Alinos Gateway — mit Octo, Lieblingswaffe unten am Hang<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — mit Octo, Jumper und Items<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — Items, Defender-Zonenring unten links im Magmaul-Bereich<br>
&nbsp;&nbsp;Elder Passage — Items, Defender-Zonenring unten links im Magmaul-Bereich
</body></html>)",
        R"(<html><body>
Desbloquea 5 combinaciones de escenario/modo verificadas como estables. Se aplica automáticamente mientras el menú de selección de escenarios esté abierto.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — con Octo, con objetos<br>
&nbsp;&nbsp;Alinos Gateway — con Octo, arma preferida al pie de la cuesta<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — con Octo, jumper y objetos<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — objetos, anillo de zona Defender abajo a la izquierda en área Magmaul<br>
&nbsp;&nbsp;Elder Passage — objetos, anillo de zona Defender abajo a la izquierda en área Magmaul
</body></html>)",
        R"(<html><body>
Débloque 5 combinaisons stage/mode vérifiées stables. Appliqué automatiquement tant que le menu de sélection de stage est ouvert.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — avec Octo, avec objets<br>
&nbsp;&nbsp;Alinos Gateway — avec Octo, arme favorite en bas de la pente<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — avec Octo, jumper et objets<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — objets, anneau de zone Defender en bas à gauche zone Magmaul<br>
&nbsp;&nbsp;Elder Passage — objets, anneau de zone Defender en bas à gauche zone Magmaul
</body></html>)",
        R"(<html><body>
Sblocca 5 combinazioni stage/modo verificate come stabili. Applicato automaticamente mentre il menu di selezione stage è aperto.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — con Octo, con oggetti<br>
&nbsp;&nbsp;Alinos Gateway — con Octo, arma preferita in fondo alla pendenza<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — con Octo, jumper e oggetti<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — oggetti, anello zona Defender in basso a sinistra nell'area Magmaul<br>
&nbsp;&nbsp;Elder Passage — oggetti, anello zona Defender in basso a sinistra nell'area Magmaul
</body></html>)",
        R"(<html><body>
Ontgrendelt 5 stabiele stage-/moduscombinaties. Wordt automatisch toegepast zolang het stage-selectiemenu open is.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — met Octo, met items<br>
&nbsp;&nbsp;Alinos Gateway — met Octo, favoriet wapen onderaan de helling<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — met Octo, jumper en items<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — items, Defender-zonering linksonder in Magmaul-gebied<br>
&nbsp;&nbsp;Elder Passage — items, Defender-zonering linksonder in Magmaul-gebied
</body></html>)",
        R"(<html><body>
Desbloqueia 5 combinações de cenário/modo verificadas como estáveis. Aplica-se automaticamente enquanto o menu de seleção de cenários estiver aberto.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — com Octo, com itens<br>
&nbsp;&nbsp;Alinos Gateway — com Octo, arma preferida no fundo da rampa<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — com Octo, jumper e itens<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — itens, anel de zona Defender no canto inferior esquerdo da área Magmaul<br>
&nbsp;&nbsp;Elder Passage — itens, anel de zona Defender no canto inferior esquerdo da área Magmaul
</body></html>)",
        R"(<html><body>
Разблокирует 5 проверенных стабильных комбинаций арены/режима. Применяется автоматически, пока открыто меню выбора арены.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — с Octo, с предметами<br>
&nbsp;&nbsp;Alinos Gateway — с Octo, любимое оружие внизу склона<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — с Octo, прыгун и предметы<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — предметы, кольцо зоны Defender внизу слева в области Magmaul<br>
&nbsp;&nbsp;Elder Passage — предметы, кольцо зоны Defender внизу слева в области Magmaul
</body></html>)",
        R"(<html><body>
解锁 5 个已验证稳定的关卡/模式组合。在关卡选择菜单打开期间自动应用。
<br><br>
<b>赏金</b><br>
&nbsp;&nbsp;Fuel Stack — 有 Octo，有道具<br>
&nbsp;&nbsp;Alinos Gateway — 有 Octo，坡底出现得意武器<br><br>
<b>占领</b><br>
&nbsp;&nbsp;Celestial Gateway — 有 Octo，有跳跃台与道具<br><br>
<b>防守</b><br>
&nbsp;&nbsp;High Ground — 有道具，Magmaul 区域左下有防守区环<br>
&nbsp;&nbsp;Elder Passage — 有道具，Magmaul 区域左下有防守区环
</body></html>)",
        R"(<html><body>
안정성이 확인된 스테이지/모드 조합 5개를 해제합니다. 스테이지 선택 메뉴가 열려 있는 동안 자동 적용됩니다.
<br><br>
<b>바운티</b><br>
&nbsp;&nbsp;Fuel Stack — 옥토리스 있음, 아이템 있음<br>
&nbsp;&nbsp;Alinos Gateway — 옥토리스 있음, 경사 아래에 특화 무기 등장<br><br>
<b>캡처</b><br>
&nbsp;&nbsp;Celestial Gateway — 옥토리스 있음, 점퍼와 아이템 있음<br><br>
<b>디펜더</b><br>
&nbsp;&nbsp;High Ground — 아이템 있음, Magmaul 구역 왼쪽 아래 디펜더 존 링<br>
&nbsp;&nbsp;Elder Passage — 아이템 있음, Magmaul 구역 왼쪽 아래 디펜더 존 링
</body></html>)",
        R"(<html><body>
يفتح 5 تركيبات مرحلة/وضع مستقرة مُتحقَّق منها. يُطبَّق تلقائياً طالما قائمة اختيار المرحلة مفتوحة.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — مع Octo، مع عناصر<br>
&nbsp;&nbsp;Alinos Gateway — مع Octo، سلاح مفضل أسفل المنحدر<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — مع Octo، jumper وعناصر<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — عناصر، حلقة منطقة Defender أسفل يسار منطقة Magmaul<br>
&nbsp;&nbsp;Elder Passage — عناصر، حلقة منطقة Defender أسفل يسار منطقة Magmaul
</body></html>)",
        R"(<html><body>
Membuka 5 kombinasi stage/mode yang terverifikasi stabil. Diterapkan otomatis selama menu pemilihan stage terbuka.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — dengan Octo, dengan item<br>
&nbsp;&nbsp;Alinos Gateway — dengan Octo, senjata favorit di bawah lereng<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — dengan Octo, jumper dan item<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — item, cincin zona Defender kiri bawah area Magmaul<br>
&nbsp;&nbsp;Elder Passage — item, cincin zona Defender kiri bawah area Magmaul
</body></html>)",
        R"(<html><body>
Розблоковує 5 перевірених стабільних комбінацій арени/режиму. Застосовується автоматично, поки відкрите меню вибору арени.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — з Octo, з предметами<br>
&nbsp;&nbsp;Alinos Gateway — з Octo, улюблена зброя внизу схилу<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — з Octo, стрибун і предмети<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — предмети, кільце зони Defender знизу зліва в області Magmaul<br>
&nbsp;&nbsp;Elder Passage — предмети, кільце зони Defender знизу зліва в області Magmaul
</body></html>)",
        R"(<html><body>
Ξεκλειδώνει 5 επιβεβαιωμένες σταθερές συνδυασμούς stage/λειτουργίας. Εφαρμόζεται αυτόματα όσο το μενού επιλογής stage είναι ανοιχτό.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — με Octo, με αντικείμενα<br>
&nbsp;&nbsp;Alinos Gateway — με Octo, αγαπημένο όπλο στο κάτω μέρος της πλαγιάς<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — με Octo, jumper και αντικείμενα<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — αντικείμενα, δακτύλιος ζώνης Defender κάτω αριστερά στην περιοχή Magmaul<br>
&nbsp;&nbsp;Elder Passage — αντικείμενα, δακτύλιος ζώνης Defender κάτω αριστερά στην περιοχή Magmaul
</body></html>)",
        R"(<html><body>
Låser upp 5 verifierat stabila stage-/lägeskombinationer. Tillämpas automatiskt medan stage-väljarmenyn är öppen.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — med Octo, med föremål<br>
&nbsp;&nbsp;Alinos Gateway — med Octo, favoritvapen längst ner på sluttningen<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — med Octo, jumper och föremål<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — föremål, Defender-zonring nere till vänster i Magmaul-området<br>
&nbsp;&nbsp;Elder Passage — föremål, Defender-zonring nere till vänster i Magmaul-området
</body></html>)",
        R"(<html><body>
ปลดล็อกชุดสเตจ/โหมดที่เสถียร 5 ชุด ใช้อัตโนมัติขณะเมนูเลือกสเตจเปิดอยู่
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — มี Octo, มีไอเทม<br>
&nbsp;&nbsp;Alinos Gateway — มี Octo, อาวุธโปรดใต้เนิน<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — มี Octo, jumper และไอเทม<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — ไอเทม, วงแหวนโซน Defender ล่างซ้ายพื้นที่ Magmaul<br>
&nbsp;&nbsp;Elder Passage — ไอเทม, วงแหวนโซน Defender ล่างซ้ายพื้นที่ Magmaul
</body></html>)",
        R"(<html><body>
Odemkne 5 ověřeně stabilních kombinací stage/režimu. Aplikuje se automaticky, dokud je otevřené menu výběru stage.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — s Octo, s předměty<br>
&nbsp;&nbsp;Alinos Gateway — s Octo, oblíbená zbraň dole na svahu<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — s Octo, jumper a předměty<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — předměty, kruh zóny Defender vlevo dole v oblasti Magmaul<br>
&nbsp;&nbsp;Elder Passage — předměty, kruh zóny Defender vlevo dole v oblasti Magmaul
</body></html>)",
        R"(<html><body>
Låser 5 verificerede stabile stage-/tilstandskombinationer op. Anvendes automatisk mens stage-valgmenuen er åben.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — med Octo, med genstande<br>
&nbsp;&nbsp;Alinos Gateway — med Octo, favoritvåben nederst på skråningen<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — med Octo, jumper og genstande<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — genstande, Defender-zonering nede til venstre i Magmaul-området<br>
&nbsp;&nbsp;Elder Passage — genstande, Defender-zonering nede til venstre i Magmaul-området
</body></html>)",
        R"(<html><body>
Doğrulanmış 5 kararlı stage/mod kombinasyonunun kilidini açar. Stage seçim menüsü açıkken otomatik uygulanır.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — Octo ile, eşyalarla<br>
&nbsp;&nbsp;Alinos Gateway — Octo ile, yamaç altında favori silah<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — Octo ile, jumper ve eşyalar<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — eşyalar, Magmaul alanında sol altta Defender bölge halkası<br>
&nbsp;&nbsp;Elder Passage — eşyalar, Magmaul alanında sol altta Defender bölge halkası
</body></html>)",
        R"(<html><body>
Låser opp 5 verifiserte stabile stage-/moduskombinasjoner. Brukes automatisk mens stage-valgmenyen er åpen.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — med Octo, med gjenstander<br>
&nbsp;&nbsp;Alinos Gateway — med Octo, favorittvåpen nederst på skråningen<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — med Octo, jumper og gjenstander<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — gjenstander, Defender-sonering nede til venstre i Magmaul-området<br>
&nbsp;&nbsp;Elder Passage — gjenstander, Defender-sonering nede til venstre i Magmaul-området
</body></html>)",
        R"(<html><body>
5 ellenőrzött stabil pálya/mód kombinációt old fel. Automatikusan alkalmazza, amíg a pályaválasztó menü nyitva van.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — Octo-val, tárgyakkal<br>
&nbsp;&nbsp;Alinos Gateway — Octo-val, kedvenc fegyver a lejtő alján<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — Octo-val, jumperrel és tárgyakkal<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — tárgyak, Defender zónagyűrű bal alsó sarokban a Magmaul területen<br>
&nbsp;&nbsp;Elder Passage — tárgyak, Defender zónagyűrű bal alsó sarokban a Magmaul területen
</body></html>)",
        R"(<html><body>
Avaa 5 vahvistetusti vakaata stage-/tilayhdistelmää. Käytetään automaattisesti stage-valikkoa avoinna ollessa.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — Octo mukana, esineitä<br>
&nbsp;&nbsp;Alinos Gateway — Octo mukana, suosikkiase rinteen alaosassa<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — Octo mukana, jumper ja esineitä<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — esineitä, Defender-vyöhykering vasemmassa alakulmassa Magmaul-alueella<br>
&nbsp;&nbsp;Elder Passage — esineitä, Defender-vyöhykering vasemmassa alakulmassa Magmaul-alueella
</body></html>)",
        R"(<html><body>
Mở khóa 5 tổ hợp màn/chế độ đã xác minh ổn định. Tự áp dụng khi menu chọn màn đang mở.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — có Octo, có vật phẩm<br>
&nbsp;&nbsp;Alinos Gateway — có Octo, vũ khí ưa thích dưới dốc<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — có Octo, jumper và vật phẩm<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — vật phẩm, vòng vùng Defender góc dưới trái khu Magmaul<br>
&nbsp;&nbsp;Elder Passage — vật phẩm, vòng vùng Defender góc dưới trái khu Magmaul
</body></html>)",
        R"(<html><body>
Odblokowuje 5 zweryfikowanych stabilnych kombinacji stage/trybu. Stosuje się automatycznie, gdy menu wyboru stage jest otwarte.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — z Octo, z przedmiotami<br>
&nbsp;&nbsp;Alinos Gateway — z Octo, ulubiona broń u podnóża stoku<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — z Octo, jumper i przedmioty<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — przedmioty, pierścień strefy Defender na dole po lewej w obszarze Magmaul<br>
&nbsp;&nbsp;Elder Passage — przedmioty, pierścień strefy Defender na dole po lewej w obszarze Magmaul
</body></html>)",
        R"(<html><body>
Deblochează 5 combinații stage/mod verificate ca stabile. Se aplică automat cât timp meniul de selecție stage este deschis.
<br><br>
<b>Bounty</b><br>
&nbsp;&nbsp;Fuel Stack — cu Octo, cu obiecte<br>
&nbsp;&nbsp;Alinos Gateway — cu Octo, armă favorită la baza pantei<br><br>
<b>Capture</b><br>
&nbsp;&nbsp;Celestial Gateway — cu Octo, jumper și obiecte<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;High Ground — obiecte, inel zonă Defender stânga jos în zona Magmaul<br>
&nbsp;&nbsp;Elder Passage — obiecte, inel zonă Defender stânga jos în zona Magmaul
</body></html>)"
    },
    {
        "lblMetroidExpandStageMatrixExtraDesc",
        R"(<html><body>
基本選択に加えて、さらに9個の組み合わせを解放します。<br><br>
<b>バトル / ノード / プライムハンター / サバイバル</b><br>
&nbsp;&nbsp;Transfer Lock Wide — 通常重力<br><br>
<b>ディフェンダー</b><br>
&nbsp;&nbsp;Transfer Lock — ディフェンダーリングなし、ボルトドライバー + バトルハンマーのみ<br>
&nbsp;&nbsp;Compressor Room — ディフェンダーリングなし、ジャンパーなし、アイテムなし<br>
&nbsp;&nbsp;Incubator — ディフェンダーリングなし、アイテムとジャンパーあり<br>
&nbsp;&nbsp;Fuel Stack — ディフェンダーリングなし、浮遊なし、インペリアリストのみ<br>
&nbsp;&nbsp;Head Shot — ディフェンダーリングなし、通常重力、ジャンパー2個、トランスフォーム通路は射出なし
</body></html>)",
        R"(<html><body>
Schaltet zusätzlich zu den Basiseinträgen 9 weitere Kombinationen frei.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normale Schwerkraft<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — ohne Defender-Ring, nur Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — ohne Defender-Ring, ohne Jumper, ohne Items<br>
&nbsp;&nbsp;Incubator — ohne Defender-Ring, mit Items und Jumper<br>
&nbsp;&nbsp;Fuel Stack — ohne Defender-Ring, ohne Schweben, nur Imperialist<br>
&nbsp;&nbsp;Head Shot — ohne Defender-Ring, normale Schwerkraft, 2 Jumper, Transform-Pfad ohne Abschuss
</body></html>)",
        R"(<html><body>
Desbloquea 9 combinaciones adicionales además de la selección básica.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — gravedad normal<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — sin anillo Defender, solo Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — sin anillo Defender, sin jumper, sin objetos<br>
&nbsp;&nbsp;Incubator — sin anillo Defender, con objetos y jumper<br>
&nbsp;&nbsp;Fuel Stack — sin anillo Defender, sin flotación, solo Imperialist<br>
&nbsp;&nbsp;Head Shot — sin anillo Defender, gravedad normal, 2 jumpers, pasillo transform sin expulsión
</body></html>)",
        R"(<html><body>
Débloque 9 combinaisons supplémentaires en plus de la sélection de base.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — gravité normale<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — sans anneau Defender, Volt Driver + Battlehammer uniquement<br>
&nbsp;&nbsp;Compressor Room — sans anneau Defender, sans jumper, sans objets<br>
&nbsp;&nbsp;Incubator — sans anneau Defender, avec objets et jumper<br>
&nbsp;&nbsp;Fuel Stack — sans anneau Defender, sans flottation, Imperialist uniquement<br>
&nbsp;&nbsp;Head Shot — sans anneau Defender, gravité normale, 2 jumpers, passage transform sans éjection
</body></html>)",
        R"(<html><body>
Sblocca 9 combinazioni aggiuntive oltre alla selezione base.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — gravità normale<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — senza anello Defender, solo Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — senza anello Defender, senza jumper, senza oggetti<br>
&nbsp;&nbsp;Incubator — senza anello Defender, con oggetti e jumper<br>
&nbsp;&nbsp;Fuel Stack — senza anello Defender, senza fluttuazione, solo Imperialist<br>
&nbsp;&nbsp;Head Shot — senza anello Defender, gravità normale, 2 jumper, corridoio transform senza espulsione
</body></html>)",
        R"(<html><body>
Ontgrendelt 9 extra combinaties naast de basisselectie.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normale zwaartekracht<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — zonder Defender-ring, alleen Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — zonder Defender-ring, zonder jumper, zonder items<br>
&nbsp;&nbsp;Incubator — zonder Defender-ring, met items en jumper<br>
&nbsp;&nbsp;Fuel Stack — zonder Defender-ring, zonder zweven, alleen Imperialist<br>
&nbsp;&nbsp;Head Shot — zonder Defender-ring, normale zwaartekracht, 2 jumpers, transform-pad zonder afschieten
</body></html>)",
        R"(<html><body>
Desbloqueia 9 combinações adicionais além da seleção base.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — gravidade normal<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — sem anel Defender, apenas Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — sem anel Defender, sem jumper, sem itens<br>
&nbsp;&nbsp;Incubator — sem anel Defender, com itens e jumper<br>
&nbsp;&nbsp;Fuel Stack — sem anel Defender, sem flutuação, apenas Imperialist<br>
&nbsp;&nbsp;Head Shot — sem anel Defender, gravidade normal, 2 jumpers, corredor transform sem expulsão
</body></html>)",
        R"(<html><body>
Разблокирует 9 дополнительных комбинаций помимо базового выбора.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — обычная гравитация<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — без кольца Defender, только Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — без кольца Defender, без прыгуна, без предметов<br>
&nbsp;&nbsp;Incubator — без кольца Defender, с предметами и прыгуном<br>
&nbsp;&nbsp;Fuel Stack — без кольца Defender, без парения, только Imperialist<br>
&nbsp;&nbsp;Head Shot — без кольца Defender, обычная гравитация, 2 прыгуна, трансформ-коридор без выброса
</body></html>)",
        R"(<html><body>
在基本选择之外再解锁 9 个组合。
<br><br>
<b>战斗 / 节点 / Prime Hunter / 生存</b><br>
&nbsp;&nbsp;Transfer Lock Wide — 正常重力<br><br>
<b>防守</b><br>
&nbsp;&nbsp;Transfer Lock — 无防守环，仅 Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — 无防守环，无跳跃台，无道具<br>
&nbsp;&nbsp;Incubator — 无防守环，有道具与跳跃台<br>
&nbsp;&nbsp;Fuel Stack — 无防守环，无漂浮，仅 Imperialist<br>
&nbsp;&nbsp;Head Shot — 无防守环，正常重力，2 个跳跃台，变形通道无弹射
</body></html>)",
        R"(<html><body>
기본 선택 외에 조합 9개를 추가로 해제합니다.
<br><br>
<b>배틀 / 노드 / 프라임 헌터 / 서바이벌</b><br>
&nbsp;&nbsp;Transfer Lock Wide — 일반 중력<br><br>
<b>디펜더</b><br>
&nbsp;&nbsp;Transfer Lock — 디펜더 링 없음, Volt Driver + Battlehammer만<br>
&nbsp;&nbsp;Compressor Room — 디펜더 링 없음, 점퍼 없음, 아이템 없음<br>
&nbsp;&nbsp;Incubator — 디펜더 링 없음, 아이템과 점퍼 있음<br>
&nbsp;&nbsp;Fuel Stack — 디펜더 링 없음, 부유 없음, Imperialist만<br>
&nbsp;&nbsp;Head Shot — 디펜더 링 없음, 일반 중력, 점퍼 2개, 변형 통로 발사 없음
</body></html>)",
        R"(<html><body>
يفتح 9 تركيبات إضافية بجانب الاختيار الأساسي.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — جاذبية عادية<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — بدون حلقة Defender، Volt Driver + Battlehammer فقط<br>
&nbsp;&nbsp;Compressor Room — بدون حلقة Defender، بدون jumper، بدون عناصر<br>
&nbsp;&nbsp;Incubator — بدون حلقة Defender، مع عناصر وjumper<br>
&nbsp;&nbsp;Fuel Stack — بدون حلقة Defender، بدون طفو، Imperialist فقط<br>
&nbsp;&nbsp;Head Shot — بدون حلقة Defender، جاذبية عادية، 2 jumper، ممر transform بدون قذف
</body></html>)",
        R"(<html><body>
Membuka 9 kombinasi tambahan selain pilihan dasar.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — gravitasi normal<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — tanpa cincin Defender, hanya Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — tanpa cincin Defender, tanpa jumper, tanpa item<br>
&nbsp;&nbsp;Incubator — tanpa cincin Defender, dengan item dan jumper<br>
&nbsp;&nbsp;Fuel Stack — tanpa cincin Defender, tanpa mengambang, hanya Imperialist<br>
&nbsp;&nbsp;Head Shot — tanpa cincin Defender, gravitasi normal, 2 jumper, koridor transform tanpa pelontaran
</body></html>)",
        R"(<html><body>
Розблоковує 9 додаткових комбінацій окремо від базового вибору.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — звичайна гравітація<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — без кільця Defender, лише Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — без кільця Defender, без стрибуна, без предметів<br>
&nbsp;&nbsp;Incubator — без кільця Defender, з предметами і стрибуном<br>
&nbsp;&nbsp;Fuel Stack — без кільця Defender, без паріння, лише Imperialist<br>
&nbsp;&nbsp;Head Shot — без кільця Defender, звичайна гравітація, 2 стрибуні, transform-коридор без викиду
</body></html>)",
        R"(<html><body>
Ξεκλειδώνει 9 επιπλέον συνδυασμούς πέρα από τη βασική επιλογή.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — κανονική βαρύτητα<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — χωρίς δακτύλιο Defender, μόνο Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — χωρίς δακτύλιο Defender, χωρίς jumper, χωρίς αντικείμενα<br>
&nbsp;&nbsp;Incubator — χωρίς δακτύλιο Defender, με αντικείμενα και jumper<br>
&nbsp;&nbsp;Fuel Stack — χωρίς δακτύλιο Defender, χωρίς αιώρηση, μόνο Imperialist<br>
&nbsp;&nbsp;Head Shot — χωρίς δακτύλιο Defender, κανονική βαρύτητα, 2 jumpers, διάδρομος transform χωρίς εκτόξευση
</body></html>)",
        R"(<html><body>
Låser upp 9 extra kombinationer utöver grundvalet.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normal gravitation<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — utan Defender-ring, endast Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — utan Defender-ring, utan jumper, utan föremål<br>
&nbsp;&nbsp;Incubator — utan Defender-ring, med föremål och jumper<br>
&nbsp;&nbsp;Fuel Stack — utan Defender-ring, utan svävning, endast Imperialist<br>
&nbsp;&nbsp;Head Shot — utan Defender-ring, normal gravitation, 2 jumpers, transform-korridor utan utskjutning
</body></html>)",
        R"(<html><body>
ปลดล็อกชุดเพิ่ม 9 ชุดนอกเหนือจากตัวเลือกพื้นฐาน
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — แรงโน้มถ่วงปกติ<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — ไม่มีวงแหวน Defender, Volt Driver + Battlehammer เท่านั้น<br>
&nbsp;&nbsp;Compressor Room — ไม่มีวงแหวน Defender, ไม่มี jumper, ไม่มีไอเทม<br>
&nbsp;&nbsp;Incubator — ไม่มีวงแหวน Defender, มีไอเทมและ jumper<br>
&nbsp;&nbsp;Fuel Stack — ไม่มีวงแหวน Defender, ไม่ลอย, Imperialist เท่านั้น<br>
&nbsp;&nbsp;Head Shot — ไม่มีวงแหวน Defender, แรงโน้มถ่วงปกติ, jumper 2 ตัว, ทาง transform ไม่มีการยิงออก
</body></html>)",
        R"(<html><body>
Odemkne 9 dalších kombinací kromě základního výběru.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normální gravitace<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — bez kruhu Defender, pouze Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — bez kruhu Defender, bez jumperu, bez předmětů<br>
&nbsp;&nbsp;Incubator — bez kruhu Defender, s předměty a jumperem<br>
&nbsp;&nbsp;Fuel Stack — bez kruhu Defender, bez vznášení, pouze Imperialist<br>
&nbsp;&nbsp;Head Shot — bez kruhu Defender, normální gravitace, 2 jumpers, transform chodba bez vystřelení
</body></html>)",
        R"(<html><body>
Låser 9 ekstra kombinationer op ud over grundvalget.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normal tyngdekraft<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — uden Defender-ring, kun Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — uden Defender-ring, uden jumper, uden genstande<br>
&nbsp;&nbsp;Incubator — uden Defender-ring, med genstande og jumper<br>
&nbsp;&nbsp;Fuel Stack — uden Defender-ring, uden svævning, kun Imperialist<br>
&nbsp;&nbsp;Head Shot — uden Defender-ring, normal tyngdekraft, 2 jumpers, transform-korridor uden udskydning
</body></html>)",
        R"(<html><body>
Temel seçime ek olarak 9 kombinasyon daha açar.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normal yerçekimi<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — Defender halkası yok, yalnızca Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — Defender halkası yok, jumper yok, eşya yok<br>
&nbsp;&nbsp;Incubator — Defender halkası yok, eşya ve jumper var<br>
&nbsp;&nbsp;Fuel Stack — Defender halkası yok, süzülme yok, yalnızca Imperialist<br>
&nbsp;&nbsp;Head Shot — Defender halkası yok, normal yerçekimi, 2 jumper, transform koridoru fırlatma yok
</body></html>)",
        R"(<html><body>
Låser opp 9 ekstra kombinasjoner i tillegg til grunnvalget.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normal gravitasjon<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — uten Defender-ring, bare Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — uten Defender-ring, uten jumper, uten gjenstander<br>
&nbsp;&nbsp;Incubator — uten Defender-ring, med gjenstander og jumper<br>
&nbsp;&nbsp;Fuel Stack — uten Defender-ring, uten sveving, bare Imperialist<br>
&nbsp;&nbsp;Head Shot — uten Defender-ring, normal gravitasjon, 2 jumpers, transform-korridor uten utskyting
</body></html>)",
        R"(<html><body>
9 további kombinációt old fel az alapválasztáson felül.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normál gravitáció<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — Defender gyűrű nélkül, csak Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — Defender gyűrű nélkül, jumper nélkül, tárgyak nélkül<br>
&nbsp;&nbsp;Incubator — Defender gyűrű nélkül, tárgyakkal és jumperrel<br>
&nbsp;&nbsp;Fuel Stack — Defender gyűrű nélkül, lebegés nélkül, csak Imperialist<br>
&nbsp;&nbsp;Head Shot — Defender gyűrű nélkül, normál gravitáció, 2 jumper, transform folyosó kilövés nélkül
</body></html>)",
        R"(<html><body>
Avaa 9 ylimääräistä yhdistelmää perusvalinnon lisäksi.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normaali painovoima<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — ilman Defender-rengasta, vain Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — ilman Defender-rengasta, ilman jumperia, ilman esineitä<br>
&nbsp;&nbsp;Incubator — ilman Defender-rengasta, esineillä ja jumperilla<br>
&nbsp;&nbsp;Fuel Stack — ilman Defender-rengasta, ilman leijumista, vain Imperialist<br>
&nbsp;&nbsp;Head Shot — ilman Defender-rengasta, normaali painovoima, 2 jumperia, transform-käytävä ilman laukaisua
</body></html>)",
        R"(<html><body>
Mở thêm 9 tổ hợp ngoài lựa chọn cơ bản.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — trọng lực bình thường<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — không vòng Defender, chỉ Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — không vòng Defender, không jumper, không vật phẩm<br>
&nbsp;&nbsp;Incubator — không vòng Defender, có vật phẩm và jumper<br>
&nbsp;&nbsp;Fuel Stack — không vòng Defender, không lơ lửng, chỉ Imperialist<br>
&nbsp;&nbsp;Head Shot — không vòng Defender, trọng lực bình thường, 2 jumper, hành lang transform không bắn ra
</body></html>)",
        R"(<html><body>
Odblokowuje 9 dodatkowych kombinacji poza wyborem podstawowym.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — normalna grawitacja<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — bez pierścienia Defender, tylko Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — bez pierścienia Defender, bez jumpera, bez przedmiotów<br>
&nbsp;&nbsp;Incubator — bez pierścienia Defender, z przedmiotami i jumperem<br>
&nbsp;&nbsp;Fuel Stack — bez pierścienia Defender, bez unoszenia, tylko Imperialist<br>
&nbsp;&nbsp;Head Shot — bez pierścienia Defender, normalna grawitacja, 2 jumpers, korytarz transform bez wyrzutu
</body></html>)",
        R"(<html><body>
Deblochează 9 combinații suplimentare pe lângă selecția de bază.
<br><br>
<b>Battle / Node / Prime Hunter / Survival</b><br>
&nbsp;&nbsp;Transfer Lock Wide — gravitație normală<br><br>
<b>Defender</b><br>
&nbsp;&nbsp;Transfer Lock — fără inel Defender, doar Volt Driver + Battlehammer<br>
&nbsp;&nbsp;Compressor Room — fără inel Defender, fără jumper, fără obiecte<br>
&nbsp;&nbsp;Incubator — fără inel Defender, cu obiecte și jumper<br>
&nbsp;&nbsp;Fuel Stack — fără inel Defender, fără plutire, doar Imperialist<br>
&nbsp;&nbsp;Head Shot — fără inel Defender, gravitație normală, 2 jumpers, coridor transform fără ejectare
</body></html>)"
    },
    {
        "lblMetroidDisableDoubleDamageMultiplierDesc",
        R"(<html><body>
ダブルダメージの取得、タイマー、HUD、サウンド、視覚効果はそのままに、ダメージ倍率だけを2倍から1倍へ変更します。<br><br>
<font color="red"><b>重要:</b> 2倍 → 1倍の変更は<b>自分側</b>にだけ適用されます。相手側もダメージ1倍を有効にしていない場合、ダブルダメージ中の自分の弾は相手には2倍ダメージとして扱われます。</font><br><br>
参加者全員の同意がある場合にだけ使用してください。
</body></html>)",
        R"(<html><body>
Behält Erhalt, Timer, HUD, Sound und visuelle Effekte von Doppelschaden bei, ändert aber nur den Schadensmultiplikator von 2× auf 1×.
<br><br>
<font color="red"><b>Wichtig:</b> Die Änderung 2× → 1× gilt nur <b>auf deiner Seite</b>. Wenn der Gegner Doppelschaden 1× nicht aktiviert hat, zählen deine Kugeln während Doppelschaden für ihn weiter als 2×.</font>
<br><br>
Nur mit Zustimmung aller Teilnehmer verwenden.
</body></html>)",
        R"(<html><body>
Mantiene la obtención, temporizador, HUD, sonido y efectos visuales del doble daño, pero cambia solo el multiplicador de 2× a 1×.
<br><br>
<font color="red"><b>Importante:</b> el cambio 2× → 1× solo se aplica <b>en tu lado</b>. Si el rival no tiene daño 1× activado, tus balas durante doble daño seguirán contando como 2× para él.</font>
<br><br>
Úsalo solo con el consentimiento de todos los participantes.
</body></html>)",
        R"(<html><body>
Conserve l'obtention, le minuteur, le HUD, le son et les effets visuels des dégâts doubles, mais ne change que le multiplicateur de 2× à 1×.
<br><br>
<font color="red"><b>Important :</b> le passage 2× → 1× ne s'applique qu'<b>à votre côté</b>. Si l'adversaire n'a pas activé dégâts 1×, vos balles en double dégâts compteront toujours 2× pour lui.</font>
<br><br>
À n'utiliser qu'avec l'accord de tous les participants.
</body></html>)",
        R"(<html><body>
Mantiene ottenimento, timer, HUD, suono ed effetti visivi del doppio danno, ma cambia solo il moltiplicatore da 2× a 1×.
<br><br>
<font color="red"><b>Importante:</b> la modifica 2× → 1× si applica solo <b>al tuo lato</b>. Se l'avversario non ha attivato danno 1×, i tuoi colpi durante il doppio danno conteranno ancora come 2× per lui.</font>
<br><br>
Usalo solo con il consenso di tutti i partecipanti.
</body></html>)",
        R"(<html><body>
Behoudt verkrijging, timer, HUD, geluid en visuele effecten van dubbele schade, maar wijzigt alleen de schademultiplier van 2× naar 1×.
<br><br>
<font color="red"><b>Belangrijk:</b> de wijziging 2× → 1× geldt alleen <b>aan jouw kant</b>. Als de tegenstander dubbele schade 1× niet heeft ingeschakeld, tellen jouw kogels tijdens dubbele schade voor hem nog steeds als 2×.</font>
<br><br>
Gebruik dit alleen met toestemming van alle deelnemers.
</body></html>)",
        R"(<html><body>
Mantém a obtenção, temporizador, HUD, som e efeitos visuais do dano duplo, mas altera apenas o multiplicador de 2× para 1×.
<br><br>
<font color="red"><b>Importante:</b> a alteração 2× → 1× aplica-se apenas <b>do seu lado</b>. Se o adversário não tiver dano 1× ativado, os seus projéteis durante dano duplo continuarão a contar como 2× para ele.</font>
<br><br>
Use apenas com o consentimento de todos os participantes.
</body></html>)",
        R"(<html><body>
Сохраняет получение, таймер, HUD, звук и визуальные эффекты двойного урона, но меняет только множитель с 2× на 1×.
<br><br>
<font color="red"><b>Важно:</b> изменение 2× → 1× применяется только <b>на вашей стороне</b>. Если у соперника не включён урон 1×, ваши пули при двойном уроне по-прежнему считаются для него как 2×.</font>
<br><br>
Используйте только с согласия всех участников.
</body></html>)",
        R"(<html><body>
保留双倍伤害的获取、计时、HUD、音效与视觉效果，仅将伤害倍率从 2 倍改为 1 倍。
<br><br>
<font color="red"><b>重要：</b>2 倍 → 1 倍的更改<b>仅适用于你方</b>。若对手未启用 1 倍伤害，你在双倍伤害期间射出的子弹对他仍按 2 倍计算。</font>
<br><br>
请在所有参与者同意后再使用。
</body></html>)",
        R"(<html><body>
더블 데미지 획득, 타이머, HUD, 사운드, 시각 효과는 그대로 두고 데미지 배율만 2배에서 1배로 변경합니다.
<br><br>
<font color="red"><b>중요:</b> 2배 → 1배 변경은 <b>자신 쪽</b>에만 적용됩니다. 상대가 1배 데미지를 켜지 않았다면 더블 데미지 중 자신의 탄환은 상대에게 여전히 2배로 처리됩니다.</font>
<br><br>
모든 참가자의 동의가 있을 때만 사용하세요.
</body></html>)",
        R"(<html><body>
يحافظ على الحصول والمؤقت وHUD والصوت والتأثيرات البصرية للضرر المزدوج، لكنه يغيّر مضاعف الضرر فقط من 2× إلى 1×.
<br><br>
<font color="red"><b>مهم:</b> التغيير 2× → 1× ينطبق <b>على جانبك فقط</b>. إذا لم يفعّل الخصم الضرر 1×، فإن رصاصك أثناء الضرر المزدوج ستُحسب له كـ 2×.</font>
<br><br>
استخدمه فقط بموافقة جميع المشاركين.
</body></html>)",
        R"(<html><body>
Mempertahankan perolehan, timer, HUD, suara, dan efek visual double damage, tetapi hanya mengubah pengali damage dari 2× ke 1×.
<br><br>
<font color="red"><b>Penting:</b> perubahan 2× → 1× hanya berlaku <b>di pihak Anda</b>. Jika lawan tidak mengaktifkan damage 1×, peluru Anda saat double damage tetap dihitung 2× untuk mereka.</font>
<br><br>
Gunakan hanya dengan persetujuan semua peserta.
</body></html>)",
        R"(<html><body>
Зберігає отримання, таймер, HUD, звук і візуальні ефекти подвійної шкоди, але змінює лише множник з 2× на 1×.
<br><br>
<font color="red"><b>Важливо:</b> зміна 2× → 1× застосовується лише <b>на вашій стороні</b>. Якщо суперник не увімкнув шкоду 1×, ваші кулі під час подвійної шкоди для нього все одно рахуються як 2×.</font>
<br><br>
Використовуйте лише за згодою всіх учасників.
</body></html>)",
        R"(<html><body>
Διατηρεί απόκτηση, χρονοδιακόπτη, HUD, ήχο και οπτικά εφέ διπλής ζημιάς, αλλά αλλάζει μόνο τον πολλαπλασιαστή από 2× σε 1×.
<br><br>
<font color="red"><b>Σημαντικό:</b> η αλλαγή 2× → 1× ισχύει μόνο <b>στη δική σας πλευρά</b>. Αν ο αντίπαλος δεν έχει ενεργοποιήσει ζημιά 1×, τα σφαιρίδιά σας κατά τη διπλή ζημιά μετάνε ακόμα ως 2× γι' αυτόν.</font>
<br><br>
Χρησιμοποιήστε το μόνο με τη συγκατάθεση όλων των συμμετεχόντων.
</body></html>)",
        R"(<html><body>
Behåller förvärv, timer, HUD, ljud och visuella effekter av dubbel skada, men ändrar bara skademultiplikatorn från 2× till 1×.
<br><br>
<font color="red"><b>Viktigt:</b> ändringen 2× → 1× gäller bara <b>på din sida</b>. Om motståndaren inte har aktiverat skada 1× räknas dina kulor under dubbel skada fortfarande som 2× för dem.</font>
<br><br>
Använd endast med alla deltagares samtycke.
</body></html>)",
        R"(<html><body>
คงการได้รับ ตัวจับเวลา HUD เสียง และเอฟเฟกต์ภาพของ double damage แต่เปลี่ยนเฉพาะตัวคูณความเสียหายจาก 2× เป็น 1×
<br><br>
<font color="red"><b>สำคัญ:</b> การเปลี่ยน 2× → 1× ใช้กับ<b>ฝั่งคุณเท่านั้น</b> หากคู่แข่งไม่ได้เปิด damage 1× กระสุนของคุณระหว่าง double damage ยังนับเป็น 2× สำหรับเขา</font>
<br><br>
ใช้เมื่อได้รับความยินยอมจากผู้เข้าร่วมทุกคนเท่านั้น
</body></html>)",
        R"(<html><body>
Zachová získání, timer, HUD, zvuk a vizuální efekty dvojitého poškození, ale mění pouze násobitel z 2× na 1×.
<br><br>
<font color="red"><b>Důležité:</b> změna 2× → 1× platí pouze <b>na vaší straně</b>. Pokud soupeř nemá zapnuté poškození 1×, vaše střely při dvojitém poškození se pro něj stále počítají jako 2×.</font>
<br><br>
Používejte pouze se souhlasem všech účastníků.
</body></html>)",
        R"(<html><body>
Bevarer erhvervelse, timer, HUD, lyd og visuelle effekter af dobbelt skade, men ændrer kun skademultiplikatoren fra 2× til 1×.
<br><br>
<font color="red"><b>Vigtigt:</b> ændringen 2× → 1× gælder kun <b>på din side</b>. Hvis modstanderen ikke har aktiveret skade 1×, tæller dine kugler under dobbelt skade stadig som 2× for dem.</font>
<br><br>
Brug kun med alle deltageres samtykke.
</body></html>)",
        R"(<html><body>
Çift hasarın kazanımını, zamanlayıcısını, HUD, ses ve görsel efektlerini korur, yalnızca hasar çarpanını 2×'den 1×'e değiştirir.
<br><br>
<font color="red"><b>Önemli:</b> 2× → 1× değişikliği yalnızca <b>sizin tarafınızda</b> geçerlidir. Rakip 1× hasarı etkinleştirmediyse, çift hasar sırasındaki mermileriniz onun için hâlâ 2× sayılır.</font>
<br><br>
Yalnızca tüm katılımcıların onayıyla kullanın.
</body></html>)",
        R"(<html><body>
Beholder erverv, timer, HUD, lyd og visuelle effekter av dobbel skade, men endrer bare skademultiplikatoren fra 2× til 1×.
<br><br>
<font color="red"><b>Viktig:</b> endringen 2× → 1× gjelder bare <b>på din side</b>. Hvis motstanderen ikke har aktivert skade 1×, teller kulene dine under dobbel skade fortsatt som 2× for dem.</font>
<br><br>
Bruk bare med samtykke fra alle deltakere.
</body></html>)",
        R"(<html><body>
Megőrzi a dupla sebzés megszerzését, időzítőjét, HUD-ját, hangját és vizuális effektjeit, de csak a sebzésszorzót változtatja 2×-ről 1×-re.
<br><br>
<font color="red"><b>Fontos:</b> a 2× → 1× változás csak <b>a te oldaladon</b> érvényes. Ha az ellenfél nem kapcsolta be az 1× sebzést, a dupla sebzés alatti lövedékeid nála továbbra is 2×-ként számítanak.</font>
<br><br>
Csak minden résztvevő beleegyezésével használd.
</body></html>)",
        R"(<html><body>
Säilyttää tuplahaiton hankinnan, ajastimen, HUD:n, äänen ja visuaaliset tehosteet, mutta muuttaa vain vahinkokerrointa 2× → 1×.
<br><br>
<font color="red"><b>Tärkeää:</b> muutos 2× → 1× koskee vain <b>omaasi puolta</b>. Jos vastustaja ei ole ottanut 1× vahinkoa käyttöön, luotisi tuplahaiton aikana lasketaan hänelle edelleen 2×.</font>
<br><br>
Käytä vain kaikkien osallistujien suostumuksella.
</body></html>)",
        R"(<html><body>
Giữ việc nhận, bộ đếm, HUD, âm thanh và hiệu ứng hình ảnh của double damage, nhưng chỉ đổi hệ số sát thương từ 2× xuống 1×.
<br><br>
<font color="red"><b>Quan trọng:</b> thay đổi 2× → 1× chỉ áp dụng <b>phía bạn</b>. Nếu đối thủ chưa bật damage 1×, đạn của bạn khi double damage vẫn tính 2× với họ.</font>
<br><br>
Chỉ dùng khi có đồng ý của mọi người tham gia.
</body></html>)",
        R"(<html><body>
Zachowuje zdobycie, timer, HUD, dźwięk i efekty wizualne podwójnych obrażeń, ale zmienia tylko mnożnik z 2× na 1×.
<br><br>
<font color="red"><b>Ważne:</b> zmiana 2× → 1× dotyczy tylko <b>twojej strony</b>. Jeśli przeciwnik nie włączył obrażeń 1×, twoje pociski podczas podwójnych obrażeń nadal liczą się dla niego jako 2×.</font>
<br><br>
Używaj tylko za zgodą wszystkich uczestników.
</body></html>)",
        R"(<html><body>
Păstrează obținerea, cronometrul, HUD, sunetul și efectele vizuale ale daunei duble, dar schimbă doar multiplicatorul de la 2× la 1×.
<br><br>
<font color="red"><b>Important:</b> schimbarea 2× → 1× se aplică doar <b>pe partea ta</b>. Dacă adversarul nu are activată dauna 1×, gloantele tale în timpul daunei duble încă se numără ca 2× pentru el.</font>
<br><br>
Folosiți doar cu acordul tuturor participanților.
</body></html>)"
    },
    {
        "lblMetroidDamageNotifyPurpleDesc",
        R"(<html><body>
ダメージを受けたときに短時間だけダブルダメージの紫状態で点滅させ、相手から被弾が見えるようにします。<br><br>
<font color="red"><b>重要:</b> これは実際のダブルダメージタイマーを書き込むため、短い間だけ本当に<b>ダブルダメージ状態に入ります</b>。その間、自分の弾は相手にも2倍ダメージを与えます。これを避けるには相手側もダメージ1倍を有効にする必要があります。</font><br><br>
<b>対戦で使う前に、必ず参加者全員の同意を取ってください。</b> 自分のダメージを1倍に保つには、上の「ダブルダメージ倍率を無効化」と組み合わせてください。
</body></html>)",
        R"(<html><body>
Lässt dich kurz im lila Doppelschaden-Zustand blinken, wenn du Schaden nimmst, damit Gegner Treffer sehen.
<br><br>
<font color="red"><b>Wichtig:</b> Dies schreibt den echten Doppelschaden-Timer und setzt dich kurz wirklich in den <b>Doppelschaden-Zustand</b>. In dieser Zeit verursachen deine Kugeln beim Gegner 2× Schaden. Um das zu vermeiden, muss der Gegner ebenfalls Doppelschaden 1× aktivieren.</font>
<br><br>
<b>Hole vor dem Online-Einsatz die Zustimmung aller Teilnehmer ein.</b> Kombiniere mit „Doppelschaden-Multiplikator deaktivieren“ oben, um deinen Schaden bei 1× zu halten.
</body></html>)",
        R"(<html><body>
Te hace parpadear brevemente en estado púrpura de doble daño al recibir daño, para que el rival vea el impacto.
<br><br>
<font color="red"><b>Importante:</b> esto escribe el temporizador real de doble daño y te pone brevemente en <b>estado de doble daño</b>. Durante ese tiempo, tus balas infligen 2× daño al rival. Para evitarlo, el rival también debe activar daño 1×.</font>
<br><br>
<b>Antes de usarlo en línea, obtén el consentimiento de todos.</b> Combínalo con «Desactivar multiplicador de doble daño» arriba para mantener tu daño en 1×.
</body></html>)",
        R"(<html><body>
Vous fait clignoter brièvement en état violet de double dégâts quand vous subissez des dégâts, pour que l'adversaire voie l'impact.
<br><br>
<font color="red"><b>Important :</b> cela écrit le vrai minuteur de double dégâts et vous met brièvement en <b>état double dégâts</b>. Pendant ce temps, vos balles infligent 2× dégâts à l'adversaire. Pour l'éviter, l'adversaire doit aussi activer dégâts 1×.</font>
<br><br>
<b>Avant de l'utiliser en ligne, obtenez l'accord de tous.</b> Combinez avec « Désactiver le multiplicateur de double dégâts » ci-dessus pour garder vos dégâts à 1×.
</body></html>)",
        R"(<html><body>
Ti fa lampeggiare brevemente nello stato viola del doppio danno quando subisci danni, così l'avversario vede l'impatto.
<br><br>
<font color="red"><b>Importante:</b> scrive il timer reale del doppio danno e ti mette brevemente nello <b>stato doppio danno</b>. In quel periodo, i tuoi colpi infliggono 2× danni all'avversario. Per evitarlo, anche l'avversario deve attivare danno 1×.</font>
<br><br>
<b>Prima di usarlo online, ottieni il consenso di tutti.</b> Combinalo con «Disattiva moltiplicatore doppio danno» sopra per mantenere il tuo danno a 1×.
</body></html>)",
        R"(<html><body>
Laat je kort knipperen in de paarse dubbele-schadetoestand wanneer je schade oploopt, zodat de tegenstander de treffer ziet.
<br><br>
<font color="red"><b>Belangrijk:</b> dit schrijft de echte dubbele-schadetimer en zet je kort in de <b>dubbele-schadetoestand</b>. In die tijd veroorzaken jouw kogels 2× schade bij de tegenstander. Om dat te vermijden moet de tegenstander ook dubbele schade 1× inschakelen.</font>
<br><br>
<b>Vraag voor online gebruik toestemming van iedereen.</b> Combineer met «Dubbele-schademultiplier uitschakelen» hierboven om je schade op 1× te houden.
</body></html>)",
        R"(<html><body>
Faz-o piscar brevemente no estado roxo de dano duplo ao receber dano, para o adversário ver o impacto.
<br><br>
<font color="red"><b>Importante:</b> isto escreve o temporizador real de dano duplo e coloca-o brevemente em <b>estado de dano duplo</b>. Nesse período, os seus projéteis causam 2× dano ao adversário. Para evitar isso, o adversário também deve ativar dano 1×.</font>
<br><br>
<b>Antes de usar online, obtenha o consentimento de todos.</b> Combine com «Desativar multiplicador de dano duplo» acima para manter o seu dano em 1×.
</body></html>)",
        R"(<html><body>
Кратко мигает фиолетовым состоянием двойного урона при получении урона, чтобы соперник видел попадание.
<br><br>
<font color="red"><b>Важно:</b> записывает настоящий таймер двойного урона и ненадолго переводит вас в <b>состояние двойного урона</b>. В это время ваши пули наносят сопернику 2× урона. Чтобы избежать этого, соперник тоже должен включить урон 1×.</font>
<br><br>
<b>Перед использованием онлайн получите согласие всех.</b> Сочетайте с «Отключить множитель двойного урона» выше, чтобы сохранить свой урон на 1×.
</body></html>)",
        R"(<html><body>
受伤时短暂以双倍伤害的紫色状态闪烁，让对手能看到被命中。
<br><br>
<font color="red"><b>重要：</b>这会写入真实的双倍伤害计时器，并短暂使您<b>进入双倍伤害状态</b>。期间您的子弹对对手仍造成 2 倍伤害。要避免这一点，对手也需启用 1 倍伤害。</font>
<br><br>
<b>在线使用前请先取得所有人同意。</b>可与上方「禁用双倍伤害倍率」组合，以保持自己的伤害为 1 倍。
</body></html>)",
        R"(<html><body>
피해를 받을 때 짧게 더블 데미지 보라색 상태로 깜빡여 상대가 피격을 볼 수 있게 합니다.
<br><br>
<font color="red"><b>중요:</b> 실제 더블 데미지 타이머를 기록하므로 잠시 <b>더블 데미지 상태</b>가 됩니다. 그 동안 자신의 탄환은 상대에게 2배 데미지를 줍니다. 이를 피하려면 상대도 1배 데미지를 켜야 합니다.</font>
<br><br>
<b>온라인 사용 전 반드시 모든 참가자의 동의를 받으세요.</b> 자신의 데미지를 1배로 유지하려면 위의 «더블 데미지 배율 비활성화»와 함께 사용하세요.
</body></html>)",
        R"(<html><body>
يجعلك تومض بلون بنفسجي double damage لفترة قصيرة عند تلقي الضرر، ليرى الخصم الإصابة.
<br><br>
<font color="red"><b>مهم:</b> هذا يكتب مؤقت double damage الحقيقي ويضعك لفترة قصيرة في <b>حالة double damage</b>. خلال ذلك، رصاصك يسبب 2× ضرر للخصم. لتجنب ذلك، يجب على الخصم أيضاً تفعيل damage 1×.</font>
<br><br>
<b>قبل الاستخدام عبر الإنترنت، احصل على موافقة الجميع.</b> ادمجه مع «تعطيل مضاعف double damage» أعلاه للإبقاء على ضررك 1×.
</body></html>)",
        R"(<html><body>
Membuat Anda berkedip sebentar dalam status ungu double damage saat menerima damage, agar lawan melihat impact.
<br><br>
<font color="red"><b>Penting:</b> ini menulis timer double damage asli dan menempatkan Anda sebentar dalam <b>status double damage</b>. Selama itu, peluru Anda memberi 2× damage ke lawan. Untuk menghindarinya, lawan juga harus mengaktifkan damage 1×.</font>
<br><br>
<b>Sebelum digunakan online, dapatkan persetujuan semua.</b> Gabungkan dengan «Nonaktifkan pengali double damage» di atas agar damage Anda tetap 1×.
</body></html>)",
        R"(<html><body>
Коротко мигає фіолетовим станом подвійної шкоди при отриманні шкоди, щоб суперник бачив влучення.
<br><br>
<font color="red"><b>Важливо:</b> записує справжній таймер подвійної шкоди і ненадовго переводить вас у <b>стан подвійної шкоди</b>. У цей час ваші кулі завдають супернику 2× шкоди. Щоб уникнути цього, суперник теж має увімкнути шкоду 1×.</font>
<br><br>
<b>Перед використанням онлайн отримайте згоду всіх.</b> Поєднуйте з «Вимкнути множник подвійної шкоди» вище, щоб зберегти свою шкоду на 1×.
</body></html>)",
        R"(<html><body>
Σας κάνει να αναβοσβήνετε σύντομα σε μωβ κατάσταση double damage όταν δέχεστε ζημιά, ώστε ο αντίπαλος να βλέπει το χτύπημα.
<br><br>
<font color="red"><b>Σημαντικό:</b> γράφει τον πραγματικό χρονοδιακόπτη double damage και σας βάζει σύντομα σε <b>κατάσταση double damage</b>. Σε αυτό το διάστημα, τα σφαιρίδιά σας προκαλούν 2× ζημιά στον αντίπαλο. Για να το αποφύγετε, ο αντίπαλος πρέπει επίσης να ενεργοποιήσει ζημιά 1×.</font>
<br><br>
<b>Πριν το χρησιμοποιήσετε online, λάβετε τη συγκατάθεση όλων.</b> Συνδυάστε με «Απενεργοποίηση πολλαπλασιαστή double damage» παραπάνω για να κρατήσετε τη ζημιά σας στο 1×.
</body></html>)",
        R"(<html><body>
Får dig att kort blinka i lila double damage-tillstånd när du tar skada, så motståndaren ser träffen.
<br><br>
<font color="red"><b>Viktigt:</b> detta skriver den riktiga double damage-timern och sätter dig kort i <b>double damage-tillstånd</b>. Under tiden orsakar dina kulor 2× skada hos motståndaren. För att undvika det måste motståndaren också aktivera skada 1×.</font>
<br><br>
<b>Innan onlineanvändning, få allas samtycke.</b> Kombinera med «Inaktivera double damage-multiplikator» ovan för att hålla din skada på 1×.
</body></html>)",
        R"(<html><body>
ทำให้คุณกระพริบสีม่วง double damage ช่วงสั้นเมื่อได้รับความเสียหาย เพื่อให้คู่แข่งเห็นการโดน
<br><br>
<font color="red"><b>สำคัญ:</b> นี่เขียนตัวจับเวลา double damage จริงและทำให้คุณเข้า<b>สถานะ double damage</b>ชั่วคราว ระหว่างนั้นกระสุนของคุณทำ 2× damage กับคู่แข่ง หากต้องการหลีกเลี่ยง คู่แข่งต้องเปิด damage 1× ด้วย</font>
<br><br>
<b>ก่อนใช้ออนไลน์ ขอความยินยอมจากทุกคน</b> ใช้ร่วมกับ «ปิดใช้ตัวคูณ double damage» ด้านบนเพื่อให้ damage ของคุณเป็น 1×
</body></html>)",
        R"(<html><body>
Krátce vás nechá blikat fialovým stavem dvojitého poškození při přijetí poškození, aby soupeř viděl zásah.
<br><br>
<font color="red"><b>Důležité:</b> zapisuje skutečný timer dvojitého poškození a na chvíli vás přepne do <b>stavu dvojitého poškození</b>. V té době vaše střely způsobují soupeři 2× poškození. Abyste tomu zabránili, soupeř musí také zapnout poškození 1×.</font>
<br><br>
<b>Před online použitím získejte souhlas všech.</b> Kombinujte s «Vypnout násobič dvojitého poškození» výše, abyste udrželi své poškození na 1×.
</body></html>)",
        R"(<html><body>
Får dig til kort at blinke i lilla double damage-tilstand, når du tager skade, så modstanderen ser træffet.
<br><br>
<font color="red"><b>Vigtigt:</b> dette skriver den rigtige double damage-timer og sætter dig kort i <b>double damage-tilstand</b>. I den tid giver dine kugler 2× skade til modstanderen. For at undgå det skal modstanderen også aktivere skade 1×.</font>
<br><br>
<b>Før online brug, få alles samtykke.</b> Kombiner med «Deaktiver double damage-multiplikator» ovenfor for at holde din skade på 1×.
</body></html>)",
        R"(<html><body>
Hasar aldığınızda kısa süre mor double damage durumunda yanıp söndürür, böylece rakip isabeti görür.
<br><br>
<font color="red"><b>Önemli:</b> bu gerçek double damage zamanlayıcısını yazar ve sizi kısa süre <b>double damage durumuna</b> sokar. Bu sırada mermileriniz rakibe 2× hasar verir. Bunu önlemek için rakibin de 1× hasarı etkinleştirmesi gerekir.</font>
<br><br>
<b>Çevrimiçi kullanmadan önce herkesin onayını alın.</b> Hasarınızı 1× tutmak için yukarıdaki «Double damage çarpanını devre dışı bırak» ile birleştirin.
</body></html>)",
        R"(<html><body>
Får deg til kort å blinke i lilla double damage-tilstand når du tar skade, slik at motstanderen ser treffet.
<br><br>
<font color="red"><b>Viktig:</b> dette skriver den ekte double damage-timeren og setter deg kort i <b>double damage-tilstand</b>. I den tiden gir kulene dine 2× skade til motstanderen. For å unngå det må motstanderen også aktivere skade 1×.</font>
<br><br>
<b>Før online bruk, få alles samtykke.</b> Kombiner med «Deaktiver double damage-multiplikator» over for å holde skaden din på 1×.
</body></html>)",
        R"(<html><body>
Rövid ideig lila double damage állapotban villogtat, amikor sebzést kapsz, hogy az ellenfél lássa a találatot.
<br><br>
<font color="red"><b>Fontos:</b> ez megírja az igazi double damage időzítőt és rövid időre <b>double damage állapotba</b> helyez. Ezalatt a lövedékeid 2× sebzést okoznak az ellenfélnek. Ennek elkerüléséhez az ellenfélnek is be kell kapcsolnia az 1× sebzést.</font>
<br><br>
<b>Online használat előtt kérd el mindenki beleegyezését.</b> Kombináld a fenti «Double damage szorzó kikapcsolása» beállítással, hogy a sebzésed 1× maradjon.
</body></html>)",
        R"(<html><body>
Saa sinut vilkkumaan lyhyesti violetissä double damage -tilassa vahinkoa saadessasi, jotta vastustaja näkee osuman.
<br><br>
<font color="red"><b>Tärkeää:</b> tämä kirjoittaa oikean double damage -ajastimen ja asettaa sinut hetkeksi <b>double damage -tilaan</b>. Silloin luotisi aiheuttavat vastustajalle 2× vahinkoa. Välttyäksesi tältä vastustajan on myös otettava 1× vahinko käyttöön.</font>
<br><br>
<b>Ennen verkkokäyttöä hanki kaikkien suostumus.</b> Yhdistä yllä olevaan «Poista double damage -kerroin käytöstä» pitääksesi vahinkosi 1×:nä.
</body></html>)",
        R"(<html><body>
Khiến bạn nhấp nháy ngắn trạng thái tím double damage khi nhận sát thương, để đối thủ thấy trúng đòn.
<br><br>
<font color="red"><b>Quan trọng:</b> điều này ghi bộ đếm double damage thật và đưa bạn tạm vào <b>trạng thái double damage</b>. Trong lúc đó, đạn bạn gây 2× damage cho đối thủ. Để tránh, đối thủ cũng phải bật damage 1×.</font>
<br><br>
<b>Trước khi dùng online, hãy có đồng ý của mọi người.</b> Kết hợp với «Tắt hệ số double damage» ở trên để giữ damage của bạn ở 1×.
</body></html>)",
        R"(<html><body>
Sprawia, że krótko migasz fioletowym stanem double damage po otrzymaniu obrażeń, aby przeciwnik widział trafienie.
<br><br>
<font color="red"><b>Ważne:</b> zapisuje prawdziwy timer double damage i na chwilę wprowadza cię w <b>stan double damage</b>. W tym czasie twoje pociski zadają przeciwnikowi 2× obrażeń. Aby tego uniknąć, przeciwnik też musi włączyć obrażenia 1×.</font>
<br><br>
<b>Przed użyciem online uzyskaj zgodę wszystkich.</b> Połącz z «Wyłącz mnożnik double damage» powyżej, aby utrzymać swoje obrażenia na 1×.
</body></html>)",
        R"(<html><body>
Vă face să clipiți scurt în starea violet double damage când primiți daune, ca adversarul să vadă impactul.
<br><br>
<font color="red"><b>Important:</b> scrie cronometrul real double damage și vă pune scurt în <b>stare double damage</b>. În acel timp, gloantele voastre provoacă 2× daune adversarului. Pentru a evita, adversarul trebuie să activeze și dauna 1×.</font>
<br><br>
<b>Înainte de utilizare online, obțineți acordul tuturor.</b> Combinați cu «Dezactivare multiplicator double damage» de mai sus pentru a vă menține dauna la 1×.
</body></html>)"
    },
    {
        "lblMetroidDisablePickingUpSpecificItemsDesc",
        R"(有効にすると、選択したパワーアップは取得されて消えますが、プレイヤー側の効果はスキップされます。ダブルダメージ、クローク、デスオルトのタイマー、フラグ、HUD、サウンド、視覚効果は適用されません。この設定が影響するのは自分とボットだけで、オンラインの相手は変更されません。)",
        R"(Wenn aktiviert, werden ausgewählte Power-Ups aufgesammelt und verschwinden, aber die Spielereffekte werden übersprungen. Doppelschaden, Cloak, Death Alt-Timer, Flags, HUD, Sound und visuelle Effekte werden nicht angewendet. Betrifft nur dich und Bots, nicht Online-Gegner.)",
        R"(Al activarlo, los power-ups seleccionados se recogen y desaparecen, pero se omiten los efectos en el jugador. No se aplican doble daño, cloak, temporizador Death Alt, flags, HUD, sonido ni efectos visuales. Solo afecta a ti y a bots, no a rivales en línea.)",
        R"(Si activé, les power-ups sélectionnés sont ramassés et disparaissent, mais leurs effets sur le joueur sont ignorés. Double dégâts, cloak, minuteur Death Alt, flags, HUD, son et effets visuels ne s'appliquent pas. N'affecte que vous et les bots, pas les adversaires en ligne.)",
        R"(Se attivato, i power-up selezionati vengono raccolti e scompaiono, ma gli effetti sul giocatore vengono ignorati. Doppio danno, cloak, timer Death Alt, flag, HUD, suono ed effetti visivi non vengono applicati. Influenza solo te e i bot, non gli avversari online.)",
        R"(Indien ingeschakeld worden geselecteerde power-ups opgepakt en verdwijnen ze, maar spelereffecten worden overgeslagen. Dubbele schade, cloak, Death Alt-timer, flags, HUD, geluid en visuele effecten worden niet toegepast. Geldt alleen voor jou en bots, niet voor online-tegenstanders.)",
        R"(Se ativado, os power-ups selecionados são recolhidos e desaparecem, mas os efeitos no jogador são ignorados. Dano duplo, cloak, temporizador Death Alt, flags, HUD, som e efeitos visuais não são aplicados. Afeta apenas si e bots, não adversários online.)",
        R"(При включении выбранные усиления подбираются и исчезают, но эффекты на игрока пропускаются. Двойной урон, cloak, таймер Death Alt, флаги, HUD, звук и визуальные эффекты не применяются. Влияет только на вас и ботов, не на онлайн-соперников.)",
        "启用后，所选强化会被拾取并消失，但跳过玩家侧效果。不会应用双倍伤害、cloak、Death Alt 计时器、标志、HUD、音效与视觉效果。仅影响您与机器人，不影响在线对手。",
        R"(활성화하면 선택한 파워업은 획득되어 사라지지만 플레이어 측 효과는 건너뜁니다. 더블 데미지, 클로크, Death Alt 타이머, 플래그, HUD, 사운드, 시각 효과는 적용되지 않습니다. 자신과 봇에만 영향을 주며 온라인 상대는 변경되지 않습니다.)",
        R"(عند التفعيل، تُجمع power-ups المحددة وتختفي، لكن تأثيرات اللاعب تُتخطى. لا يُطبَّق double damage أو cloak أو مؤقت Death Alt أو flags أو HUD أو الصوت أو التأثيرات البصرية. يؤثر فقط عليك وعلى البots، وليس على الخصوم عبر الإنترنت.)",
        R"(Saat diaktifkan, power-up yang dipilih diambil dan hilang, tetapi efek pemain dilewati. Double damage, cloak, timer Death Alt, flags, HUD, suara, dan efek visual tidak diterapkan. Hanya memengaruhi Anda dan bot, bukan lawan online.)",
        R"(Коли увімкнено, вибрані power-ups підбираються і зникають, але ефекти на гравця пропускаються. Подвійна шкода, cloak, таймер Death Alt, flags, HUD, звук і візуальні ефекти не застосовуються. Впливає лише на вас і ботів, не на онлайн-суперників.)",
        R"(Όταν ενεργοποιηθεί, τα επιλεγμένα power-ups μαζεύονται και εξαφανίζονται, αλλά τα εφέ στον παίκτη παραλείπονται. Δεν εφαρμόζονται double damage, cloak, χρονοδιακόπτης Death Alt, flags, HUD, ήχος και οπτικά εφέ. Επηρεάζει μόνο εσάς και bots, όχι αντιπάλους online.)",
        R"(När aktiverat plockas valda power-ups upp och försvinner, men spelareffekter hoppas över. Dubbel skada, cloak, Death Alt-timer, flags, HUD, ljud och visuella effekter tillämpas inte. Påverkar bara dig och bots, inte online-motståndare.)",
        R"(เมื่อเปิดใช้ power-up ที่เลือกจะถูกเก็บและหายไป แต่ข้ามเอฟเฟกต์ฝั่งผู้เล่น ไม่ใช้ double damage, cloak, ตัวจับเวลา Death Alt, flags, HUD, เสียง และเอฟเฟกต์ภาพ มีผลเฉพาะคุณและ bot ไม่กระทบคู่แข่งออนไลน์)",
        R"(Po zapnutí se vybrané power-upy seberou a zmizí, ale efekty na hráče se přeskočí. Dvojité poškození, cloak, timer Death Alt, flags, HUD, zvuk a vizuální efekty se neuplatní. Ovlivňuje pouze vás a boty, ne online soupeře.)",
        R"(Når aktiveret samles valgte power-ups op og forsvinder, men spillereffekter springes over. Dobbelt skade, cloak, Death Alt-timer, flags, HUD, lyd og visuelle effekter anvendes ikke. Påvirker kun dig og bots, ikke online-modstandere.)",
        R"(Etkinleştirildiğinde seçilen power-up'lar alınır ve kaybolur, ancak oyuncu efektleri atlanır. Double damage, cloak, Death Alt zamanlayıcısı, flags, HUD, ses ve görsel efektler uygulanmaz. Yalnızca sizi ve botları etkiler, çevrimiçi rakipleri değil.)",
        R"(Når aktivert plukkes valgte power-ups opp og forsvinner, men spillereffekter hoppes over. Dobbel skade, cloak, Death Alt-timer, flags, HUD, lyd og visuelle effekter brukes ikke. Påvirker bare deg og bots, ikke online-motstandere.)",
        R"(Bekapcsolva a kiválasztott power-upok felvételre kerülnek és eltűnnek, de a játékos effektek kimaradnak. Dupla sebzés, cloak, Death Alt időzítő, flags, HUD, hang és vizuális effektek nem érvényesülnek. Csak rád és a botokra hat, online ellenfelekre nem.)",
        R"(Kun käytössä, valitut power-upit poimitaan ja katoavat, mutta pelaajavaikutukset ohitetaan. Tuplahaitto, cloak, Death Alt -ajastin, flags, HUD, ääni ja visuaaliset tehosteet eivät tule voimaan. Vaikuttaa vain sinuun ja botteihin, ei verkkovastustajiin.)",
        R"(Khi bật, power-up được chọn sẽ được nhặt và biến mất, nhưng bỏ qua hiệu ứng phía người chơi. Không áp dụng double damage, cloak, bộ đếm Death Alt, flags, HUD, âm thanh và hiệu ứng hình ảnh. Chỉ ảnh hưởng bạn và bot, không ảnh hưởng đối thủ online.)",
        R"(Po włączeniu wybrane power-upy są podnoszone i znikają, ale efekty gracza są pomijane. Nie stosuje się double damage, cloak, timera Death Alt, flags, HUD, dźwięku ani efektów wizualnych. Dotyczy tylko ciebie i botów, nie rywali online.)",
        R"(Când este activat, power-up-urile selectate sunt ridicate și dispar, dar efectele asupra jucătorului sunt omise. Nu se aplică double damage, cloak, timer Death Alt, flags, HUD, sunet și efecte vizuale. Afectează doar pe dvs. și boții, nu adversarii online.)"
    },
    {
        "lblMetroidJoy2KeySupportDesc",
        R"(JoyToKey、Steam Input、reWASDなど、外部のコントローラー→キーボード/マウス変換ツール向けの互換モードです。割り当てたキーが離した後も押されっぱなしになる場合に役立ちます。通常のキーボードとマウスを直接使う場合は、無効にすると入力遅延が少し減ることがあります。)",
        R"(Kompatibilitätsmodus für externe Controller-zu-Tastatur/Maus-Tools wie JoyToKey, Steam Input oder reWASD. Hilft, wenn zugewiesene Tasten nach Loslassen hängen bleiben. Bei direkter Tastatur-/Maunutzung kann Deaktivierung die Eingabeverzögerung leicht reduzieren.)",
        R"(Modo de compatibilidad para herramientas externas controlador→teclado/ratón como JoyToKey, Steam Input o reWASD. Útil si las teclas asignadas quedan pulsadas tras soltarlas. Con teclado y ratón directos, desactivarlo puede reducir ligeramente la latencia de entrada.)",
        R"(Mode de compatibilité pour les outils externes manette→clavier/souris comme JoyToKey, Steam Input ou reWASD. Utile si les touches assignées restent enfoncées après relâchement. Avec clavier/souris directs, le désactiver peut légèrement réduire la latence d'entrée.)",
        R"(Modalità compatibilità per strumenti esterni controller→tastiera/mouse come JoyToKey, Steam Input o reWASD. Utile se i tasti assegnati restano premuti dopo il rilascio. Con tastiera e mouse diretti, disattivarla può ridurre leggermente la latenza di input.)",
        R"(Compatibiliteitsmodus voor externe controller-naar-toetsenbord/muis-tools zoals JoyToKey, Steam Input of reWASD. Handig als toegewezen toetsen na loslaten blijven ingedrukt. Bij direct toetsenbord/muisgebruik kan uitschakelen de invoervertraging enigszins verlagen.)",
        R"(Modo de compatibilidade para ferramentas externas de comando→teclado/rato como JoyToKey, Steam Input ou reWASD. Útil se as teclas atribuídas ficarem premidas após soltar. Com teclado e rato diretos, desativar pode reduzir ligeiramente a latência de entrada.)",
        R"(Режим совместимости для внешних инструментов геймпад→клавиатура/мышь, таких как JoyToKey, Steam Input или reWASD. Полезен, если назначенные клавиши остаются нажатыми после отпускания. При прямом использовании клавиатуры и мыши отключение может немного снизить задержку ввода.)",
        "面向 JoyToKey、Steam Input、reWASD 等外部手柄→键鼠映射工具的兼容模式。当分配的按键在松开后仍保持按下时有用。直接使用键盘和鼠标时，关闭此选项可能略微降低输入延迟。",
        R"(JoyToKey, Steam Input, reWASD 등 외부 컨트롤러→키보드/마우스 변환 도구용 호환 모드입니다. 할당한 키가 뗀 뒤에도 눌린 상태로 남을 때 유용합니다. 일반 키보드와 마우스를 직접 쓸 때는 끄면 입력 지연이 약간 줄어들 수 있습니다.)",
        R"(وضع توافق لأدوات تحويل التحكم→لوحة مفاتيح/فأرة خارجية مثل JoyToKey وSteam Input وreWASD. مفيد إذا بقيت المفاتيح المعيّنة مضغوطة بعد الإفلات. مع لوحة مفاتيح وفأرة مباشرة، قد يقلل التعطيل تأخير الإدخال قليلاً.)",
        R"(Mode kompatibilitas untuk alat konverter kontroler→keyboard/mouse eksternal seperti JoyToKey, Steam Input, atau reWASD. Berguna jika tombol yang ditetapkan tetap tertekan setelah dilepas. Dengan keyboard dan mouse langsung, menonaktifkannya dapat sedikit mengurangi latensi input.)",
        R"(Режим сумісності для зовнішніх інструментів геймпад→клавіатура/миша, таких як JoyToKey, Steam Input або reWASD. Корисний, якщо призначені клавіші залишаються натиснутими після відпускання. При прямому використанні клавіатури та миші вимкнення може трохи зменшити затримку вводу.)",
        R"(Λειτουργία συμβατότητας για εξωτερικά εργαλεία χειριστηρίου→πληκτρολόγιο/ποντίκι όπως JoyToKey, Steam Input ή reWASD. Χρήσιμη αν τα ανατεθειμένα πλήκτρα μένουν πατημένα μετά την αφήση. Με άμεσο πληκτρολόγιο/ποντίκι, η απενεργοποίηση μπορεί να μειώσει ελαφρώς την καθυστέρηση εισόδου.)",
        R"(Kompatibilitetsläge för externa kontroller→tangentbord/mus-verktyg som JoyToKey, Steam Input eller reWASD. Användbart om tilldelade tangenter förblir nedtryckta efter släpp. Med direkt tangentbord/mus kan inaktivering minska indatalatensen något.)",
        R"(โหมดความเข้ากันได้สำหรับเครื่องมือแปลงคอนโทรลเลอร์→คีย์บอร์ด/เมาส์ภายนอก เช่น JoyToKey, Steam Input หรือ reWASD มีประโยชน์หากปุ่มที่กำหนดยังกดค้างหลังปล่อย หากใช้คีย์บอร์ดและเมาส์โดยตรง การปิดอาจลดความหน่วงอินพุตเล็กน้อย)",
        R"(Režim kompatibility pro externí nástroje ovladač→klávesnice/myš jako JoyToKey, Steam Input nebo reWASD. Užitečné, pokud přiřazené klávesy zůstávají stisknuté po uvolnění. Při přímém použití klávesnice a myši může vypnutí mírně snížit latenci vstupu.)",
        R"(Kompatibilitetstilstand for eksterne controller→tastatur/mus-værktøjer som JoyToKey, Steam Input eller reWASD. Nyttigt hvis tildelte taster forbliver trykket efter slip. Med direkte tastatur/mus kan deaktivering reducere inputforsinkelse en smule.)",
        R"(JoyToKey, Steam Input, reWASD gibi harici kontrolcü→klavye/fare dönüştürme araçları için uyumluluk modu. Atanan tuşlar bırakıldıktan sonra basılı kalıyorsa yararlıdır. Doğrudan klavye ve fare kullanımında kapatmak giriş gecikmesini biraz azaltabilir.)",
        R"(Kompatibilitetsmodus for eksterne kontroller→tastatur/mus-verktøy som JoyToKey, Steam Input eller reWASD. Nyttig hvis tildelte taster forblir trykket etter slipp. Med direkte tastatur/mus kan deaktivering redusere inngangsforsinkelse noe.)",
        R"(Kompatibilitási mód külső JoyToKey, Steam Input, reWASD stb. kontroller→billentyűzet/egér eszközökhöz. Hasznos, ha a hozzárendelt billentyűk elengedés után is lenyomva maradnak. Közvetlen billentyűzet/egér használatnál a kikapcsolás kissé csökkentheti a bemeneti késleltetést.)",
        R"(Yhteensopivuustila ulkoisille JoyToKey-, Steam Input- ja reWASD-tyyppisille ohjain→näppäimistö/hiiri -muunnostyökaluille. Hyödyllinen, jos määritetyt näppäimet jäävät painetuiksi vapautuksen jälkeen. Suoran näppäimistön/hiiren käytössä poistaminen käytöstä voi hieman vähentää syötteen viivettä.)",
        R"(Chế độ tương thích cho công cụ chuyển tay cầm→bàn phím/chuột bên ngoài như JoyToKey, Steam Input hoặc reWASD. Hữu ích nếu phím gán vẫn giữ nhấn sau khi thả. Dùng trực tiếp bàn phím và chuột thì tắt có thể giảm nhẹ độ trễ đầu vào.)",
        R"(Tryb zgodności dla zewnętrznych narzędzi kontroler→klawiatura/mysz, takich jak JoyToKey, Steam Input lub reWASD. Przydatny, gdy przypisane klawisze pozostają wciśnięte po puszczeniu. Przy bezpośrednim użyciu klawiatury i myszy wyłączenie może nieco zmniejszyć opóźnienie wejścia.)",
        R"(Mod de compatibilitate pentru instrumente externe controler→tastatură/mouse precum JoyToKey, Steam Input sau reWASD. Util dacă tastele atribuite rămân apăsate după eliberare. Cu tastatură și mouse directe, dezactivarea poate reduce ușor latența de intrare.)"
    },
    {
        "labelMetroidScreenSyncDesc",
        R"(オフ: 同期なし (最小遅延ですが、表示がカクつくことがあります)。glFinish: 各フレームの描画完了を待つことで表示を滑らかにします。DwmFlush: Windowsコンポジターと同期して表示を滑らかにします (Windowsのみ)。画面がカクついたりちらついたりする場合は、glFinishまたはDwmFlushを試してください。早送り/スローモーション中は自動的に無効になります。)",
        R"(Aus: Keine Synchronisation (geringste Latenz, Anzeige kann ruckeln). glFinish: Glattere Anzeige durch Warten auf den Abschluss jedes Frames. DwmFlush: Synchronisation mit dem Windows-Compositor (nur Windows). Bei Ruckeln oder Flackern glFinish oder DwmFlush testen. Wird bei Vorspulen/Zeitlupe automatisch deaktiviert.)",
        R"(Desactivado: sin sincronización (mínima latencia, pero la imagen puede entrecortarse). glFinish: imagen más fluida esperando a que termine cada fotograma. DwmFlush: sincroniza con el compositor de Windows (solo Windows). Si hay tirones o parpadeos, prueba glFinish o DwmFlush. Se desactiva automáticamente en avance rápido/cámara lenta.)",
        R"(Désactivé : pas de synchronisation (latence minimale, affichage parfois saccadé). glFinish : affichage plus fluide en attendant la fin de chaque image. DwmFlush : synchronise avec le compositeur Windows (Windows uniquement). En cas de saccades ou scintillement, essayez glFinish ou DwmFlush. Désactivé automatiquement en avance rapide/ralenti.)",
        R"(Disattivato: nessuna sincronizzazione (latenza minima, ma l'immagine può scattare). glFinish: immagine più fluida attendendo il completamento di ogni frame. DwmFlush: sincronizza con il compositor Windows (solo Windows). In caso di scatti o sfarfallio, prova glFinish o DwmFlush. Disattivato automaticamente in avanzamento rapido/ralenti.)",
        R"(Uit: geen synchronisatie (laagste latentie, beeld kan haperen). glFinish: vloeiender beeld door te wachten op het einde van elk frame. DwmFlush: synchroniseert met de Windows-compositor (alleen Windows). Bij haperen of flikkeren, probeer glFinish of DwmFlush. Wordt automatisch uitgeschakeld bij vooruitspoelen/slow motion.)",
        R"(Desativado: sem sincronização (latência mínima, mas a imagem pode falhar). glFinish: imagem mais fluida ao aguardar o fim de cada fotograma. DwmFlush: sincroniza com o compositor do Windows (apenas Windows). Se houver falhas ou cintilação, experimente glFinish ou DwmFlush. Desativa-se automaticamente em avanço rápido/câmara lenta.)",
        R"(Выкл.: без синхронизации (минимальная задержка, но возможны рывки). glFinish: более плавное изображение за счёт ожидания завершения каждого кадра. DwmFlush: синхронизация с композитором Windows (только Windows). При рывках или мерцании попробуйте glFinish или DwmFlush. Автоматически отключается при ускорении/замедлении.)",
        R"(关闭：不同步（延迟最低，但画面可能卡顿）。glFinish：等待每帧绘制完成使画面更平滑。DwmFlush：与 Windows 合成器同步（仅 Windows）。若画面卡顿或闪烁，可尝试 glFinish 或 DwmFlush。快进/慢动作时自动禁用。)",
        R"(끔: 동기화 없음(최소 지연이지만 화면이 끊길 수 있음). glFinish: 각 프레임 렌더 완료를 기다려 화면을 부드럽게 함. DwmFlush: Windows 컴포지터와 동기화(Windows 전용). 화면이 끊기거나 깜빡이면 glFinish 또는 DwmFlush를 시도하세요. 빨리감기/슬로 모션 중에는 자동으로 꺼집니다.)",
        R"(إيقاف: بلا مزامنة (أقل تأخير، لكن العرض قد يتقطع). glFinish: عرض أنعم بانتظار اكتمال كل إطار. DwmFlush: يزامن مع مركّب Windows (Windows فقط). عند التقطيع أو الوميض، جرّب glFinish أو DwmFlush. يُعطَّل تلقائياً أثناء التقديم السريع/الحركة البطيئة.)",
        R"(Mati: tanpa sinkronisasi (latensi minimum, tampilan bisa tersendat). glFinish: tampilan lebih halus dengan menunggu setiap frame selesai. DwmFlush: sinkron dengan compositor Windows (hanya Windows). Jika tersendat atau berkedip, coba glFinish atau DwmFlush. Otomatis nonaktif saat fast-forward/slow motion.)",
        R"(Вимк.: без синхронізації (мінімальна затримка, але можливі риви). glFinish: плавніше зображення через очікування завершення кожного кадру. DwmFlush: синхронізація з композитором Windows (лише Windows). При ривках або мерехтінні спробуйте glFinish або DwmFlush. Автоматично вимикається під час прискорення/уповільнення.)",
        R"(Ανενεργό: χωρίς συγχρονισμό (ελάχιστη καθυστέρηση, αλλά η εικόνα μπορεί να τραβάει). glFinish: πιο ομαλή εικόνα περιμένοντας το τέλος κάθε καρέ. DwmFlush: συγχρονίζει με τον συνθέτη Windows (μόνο Windows). Σε τραβήγματα ή τρεμόπαιγμα, δοκιμάστε glFinish ή DwmFlush. Απενεργοποιείται αυτόματα σε fast-forward/slow motion.)",
        R"(Av: ingen synkronisering (lägst latens, bilden kan hacka). glFinish: mjukare bild genom att vänta på varje bildruta. DwmFlush: synkar med Windows compositor (endast Windows). Vid hackning eller flimmer, prova glFinish eller DwmFlush. Inaktiveras automatiskt vid snabbspolning/slow motion.)",
        R"(ปิด: ไม่ซิงค์ (หน่วงต่ำสุด แต่ภาพอาจกระตุก) glFinish: ภาพลื่นขึ้นด้วยการรอแต่ละเฟรมเสร็จ DwmFlush: ซิงค์กับ compositor ของ Windows (เฉพาะ Windows) หากกระตุกหรือกระพริบ ลอง glFinish หรือ DwmFlush ปิดอัตโนมัติขณะ fast-forward/slow motion)",
        R"(Vyp: bez synchronizace (nejnižší latence, obraz může trhat). glFinish: plynulejší obraz čekáním na dokončení každého snímku. DwmFlush: synchronizace s compositor Windows (pouze Windows). Při trhání nebo blikání zkuste glFinish nebo DwmFlush. Automaticky vypnuto při rychlém posunu/zpomalení.)",
        R"(Fra: ingen synkronisering (lavest latency, billedet kan hakke). glFinish: jævnere billede ved at vente på hver frame. DwmFlush: synkroniserer med Windows-compositor (kun Windows). Ved hak eller flimmer, prøv glFinish eller DwmFlush. Deaktiveres automatisk ved spoling frem/slow motion.)",
        R"(Kapalı: senkronizasyon yok (en düşük gecikme, görüntü takılabilir). glFinish: her kare tamamlanana kadar bekleyerek daha akıcı görüntü. DwmFlush: Windows compositor ile senkronize eder (yalnızca Windows). Takılma veya titreme varsa glFinish veya DwmFlush deneyin. Hızlı ileri/yavaş çekimde otomatik kapanır.)",
        R"(Av: ingen synkronisering (lavest latency, bildet kan hakke). glFinish: jevnere bilde ved å vente på hver frame. DwmFlush: synkroniserer med Windows-compositor (kun Windows). Ved hakking eller flimring, prøv glFinish eller DwmFlush. Deaktiveres automatisk ved spoling frem/slow motion.)",
        R"(Ki: nincs szinkronizáció (legalacsonyabb késleltetés, a kép akadozhat). glFinish: simább kép minden képkocka befejezésének várakozásával. DwmFlush: szinkronizál a Windows compositorral (csak Windows). Akadozásnál vagy villogásnál próbáld a glFinish-t vagy DwmFlush-t. Gyorsított/lelassított lejátszásnál automatikusan kikapcsol.)",
        R"(Pois: ei synkronointia (pienin viive, kuva voi pätkiä). glFinish: tasaisempi kuva odottamalla jokaisen ruudun valmistumista. DwmFlush: synkronoi Windows-compositorin kanssa (vain Windows). Pätkimisessä tai välkynnässä kokeile glFinish tai DwmFlush. Poistuu automaattisesti pikakelauksessa/hidastuksessa.)",
        R"(Tắt: không đồng bộ (độ trễ thấp nhất, hình có thể giật). glFinish: hình mượt hơn bằng cách chờ mỗi khung hoàn tất. DwmFlush: đồng bộ với compositor Windows (chỉ Windows). Nếu giật hoặc nhấp nháy, thử glFinish hoặc DwmFlush. Tự tắt khi tua nhanh/chậm.)",
        R"(Wył.: brak synchronizacji (najniższe opóźnienie, obraz może szarpać). glFinish: płynniejszy obraz przez oczekiwanie na zakończenie każdej klatki. DwmFlush: synchronizacja z compositor Windows (tylko Windows). Przy szarpaniu lub migotaniu spróbuj glFinish lub DwmFlush. Automatycznie wyłączone przy przewijaniu/slow motion.)",
        R"(Oprit: fără sincronizare (latență minimă, imaginea poate sări). glFinish: imagine mai fluidă așteptând finalizarea fiecărui cadru. DwmFlush: sincronizează cu compositorul Windows (doar Windows). La sărituri sau pâlpâire, încercați glFinish sau DwmFlush. Se dezactivează automat la derulare rapidă/slow motion.)"
    },
    {
        "labelInGameAspectRatioAutoDesc",
        "自動: 現在のアスペクト比設定に合わせてアスペクト比パッチを自動適用します (4:3 = オフ、5:3/16:9/21:9 = 自動適用、ウィンドウ = オフ)。",
        R"(Auto: Wendet automatisch einen Seitenverhältnis-Patch passend zur aktuellen Einstellung an (4:3 = Aus, 5:3/16:9/21:9 = Auto, Fenster = Aus).)",
        R"(Auto: aplica automáticamente un parche de relación de aspecto según el ajuste actual (4:3 = desactivado, 5:3/16:9/21:9 = auto, ventana = desactivado).)",
        R"(Auto : applique automatiquement un patch de ratio d'aspect selon le réglage actuel (4:3 = désactivé, 5:3/16:9/21:9 = auto, fenêtre = désactivé).)",
        R"(Auto: applica automaticamente una patch del rapporto d'aspetto in base all'impostazione attuale (4:3 = disattivato, 5:3/16:9/21:9 = auto, finestra = disattivato).)",
        R"(Auto: past automatisch een beeldverhouding-patch toe volgens de huidige instelling (4:3 = uit, 5:3/16:9/21:9 = auto, venster = uit).)",
        R"(Auto: aplica automaticamente um patch de proporção de ecrã conforme a definição atual (4:3 = desativado, 5:3/16:9/21:9 = auto, janela = desativado).)",
        R"(Авто: автоматически применяет патч соотношения сторон по текущей настройке (4:3 = выкл., 5:3/16:9/21:9 = авто, окно = выкл.).)",
        "自动：按当前宽高比设置自动应用宽高比补丁（4:3 = 关闭，5:3/16:9/21:9 = 自动，窗口 = 关闭）。",
        "자동: 현재 화면 비율 설정에 맞춰 비율 패치를 자동 적용합니다(4:3 = 끔, 5:3/16:9/21:9 = 자동, 창 = 끔).",
        R"(تلقائي: يطبّق تلقائياً patch نسبة العرض إلى الارتفاع حسب الإعداد الحالي (4:3 = إيقاف، 5:3/16:9/21:9 = تلقائي، نافذة = إيقاف).)",
        R"(Auto: menerapkan patch rasio aspek secara otomatis sesuai pengaturan saat ini (4:3 = mati, 5:3/16:9/21:9 = auto, jendela = mati).)",
        R"(Авто: автоматично застосовує патч співвідношення сторін за поточним налаштуванням (4:3 = вимк., 5:3/16:9/21:9 = авто, вікно = вимк.).)",
        R"(Auto: εφαρμόζει αυτόματα patch αναλογίας διαστάσεων σύμφωνα με την τρέχουσα ρύθμιση (4:3 = ανενεργό, 5:3/16:9/21:9 = auto, παράθυρο = ανενεργό).)",
        R"(Auto: tillämpar automatiskt bildförhållande-patch enligt aktuell inställning (4:3 = av, 5:3/16:9/21:9 = auto, fönster = av).)",
        "อัตโนมัติ: ใช้ patch อัตราส่วนภาพอัตโนมัติตามการตั้งค่าปัจจุบัน (4:3 = ปิด, 5:3/16:9/21:9 = อัตโนมัติ, หน้าต่าง = ปิด)",
        "Auto: automaticky aplikuje patch poměru stran podle aktuálního nastavení (4:3 = vyp, 5:3/16:9/21:9 = auto, okno = vyp).",
        R"(Auto: anvender automatisk billedformat-patch efter nuværende indstilling (4:3 = fra, 5:3/16:9/21:9 = auto, vindue = fra).)",
        R"(Otomatik: geçerli ayara göre en-boy oranı yamasını otomatik uygular (4:3 = kapalı, 5:3/16:9/21:9 = otomatik, pencere = kapalı).)",
        "Auto: bruker automatisk sideforhold-patch etter gjeldende innstilling (4:3 = av, 5:3/16:9/21:9 = auto, vindu = av).",
        R"(Auto: automatikusan alkalmazza a képarány patch-et az aktuális beállítás szerint (4:3 = ki, 5:3/16:9/21:9 = auto, ablak = ki).)",
        R"(Auto: käyttää automaattisesti kuvasuhte-patchiä nykyisen asetuksen mukaan (4:3 = pois, 5:3/16:9/21:9 = auto, ikkuna = pois).)",
        "Tự động: tự áp dụng patch tỷ lệ khung hình theo cài đặt hiện tại (4:3 = tắt, 5:3/16:9/21:9 = tự động, cửa sổ = tắt).",
        R"(Auto: automatycznie stosuje patch proporcji obrazu według bieżącego ustawienia (4:3 = wył., 5:3/16:9/21:9 = auto, okno = wył.).)",
        R"(Auto: aplică automat patch de raport de aspect conform setării curente (4:3 = oprit, 5:3/16:9/21:9 = auto, fereastră = oprit).)"
    },
    {
        "lblMetroidLowHpWarningDesc",
        "自動スケール: Low = 基準 ×0.75、Medium = 基準、High = 基準 ×1.25 (丸め)。0にすると実質的に警告を無効化します。範囲は0-255です。",
        R"(Auto-Skalierung: Low = Basis ×0,75, Medium = Basis, High = Basis ×1,25 (gerundet). 0 deaktiviert die Warnung praktisch. Bereich: 0–255.)",
        R"(Escala automática: Low = base ×0,75, Medium = base, High = base ×1,25 (redondeado). 0 desactiva la advertencia en la práctica. Rango: 0–255.)",
        R"(Échelle auto : Low = base ×0,75, Medium = base, High = base ×1,25 (arrondi). 0 désactive pratiquement l'avertissement. Plage : 0–255.)",
        R"(Scala automatica: Low = base ×0,75, Medium = base, High = base ×1,25 (arrotondato). 0 disattiva praticamente l'avviso. Intervallo: 0–255.)",
        R"(Auto-schaal: Low = basis ×0,75, Medium = basis, High = basis ×1,25 (afgerond). 0 schakelt de waarschuwing praktisch uit. Bereik: 0–255.)",
        R"(Escala automática: Low = base ×0,75, Medium = base, High = base ×1,25 (arredondado). 0 desativa praticamente o aviso. Intervalo: 0–255.)",
        R"(Автомасштаб: Low = база ×0,75, Medium = база, High = база ×1,25 (округление). 0 практически отключает предупреждение. Диапазон: 0–255.)",
        "自动缩放：Low = 基准 ×0.75，Medium = 基准，High = 基准 ×1.25（四舍五入）。设为 0 实质上禁用警告。范围：0–255。",
        "자동 스케일: Low = 기준 ×0.75, Medium = 기준, High = 기준 ×1.25(반올림). 0이면 사실상 경고 비활성화. 범위: 0–255.",
        "مقياس تلقائي: Low = أساس ×0.75، Medium = أساس، High = أساس ×1.25 (تقريب). 0 يعطّل التحذير عملياً. النطاق: 0–255.",
        R"(Skala otomatis: Low = dasar ×0,75, Medium = dasar, High = dasar ×1,25 (pembulatan). 0 praktis menonaktifkan peringatan. Rentang: 0–255.)",
        R"(Автомасштаб: Low = база ×0,75, Medium = база, High = база ×1,25 (округлення). 0 практично вимикає попередження. Діапазон: 0–255.)",
        R"(Αυτόματη κλίμακα: Low = βάση ×0,75, Medium = βάση, High = βάση ×1,25 (στρογγυλοποίηση). Το 0 απενεργοποιεί ουσιαστικά την προειδοποίηση. Εύρος: 0–255.)",
        R"(Autoskala: Low = bas ×0,75, Medium = bas, High = bas ×1,25 (avrundat). 0 inaktiverar praktiskt taget varningen. Intervall: 0–255.)",
        "สเกลอัตโนมัติ: Low = ฐาน ×0.75, Medium = ฐาน, High = ฐาน ×1.25 (ปัดเศษ) 0 ปิดการเตือนอย่างมีผล ช่วง: 0–255",
        R"(Auto škála: Low = základ ×0,75, Medium = základ, High = základ ×1,25 (zaokrouhlení). 0 prakticky vypne varování. Rozsah: 0–255.)",
        R"(Auto-skala: Low = basis ×0,75, Medium = basis, High = basis ×1,25 (afrundet). 0 deaktiverer praktisk talt advarslen. Område: 0–255.)",
        R"(Otomatik ölçek: Low = taban ×0,75, Medium = taban, High = taban ×1,25 (yuvarlama). 0 uyarıyı fiilen devre dışı bırakır. Aralık: 0–255.)",
        R"(Autoskala: Low = base ×0,75, Medium = base, High = base ×1,25 (avrundet). 0 deaktiverer praktisk talt advarselen. Område: 0–255.)",
        R"(Automatikus skála: Low = alap ×0,75, Medium = alap, High = alap ×1,25 (kerekítve). A 0 gyakorlatilag kikapcsolja a figyelmeztetést. Tartomány: 0–255.)",
        R"(Automaattinen skaala: Low = perus ×0,75, Medium = perus, High = perus ×1,25 (pyöristetty). 0 käytännössä poistaa varoituksen. Alue: 0–255.)",
        R"(Tỷ lệ tự động: Low = cơ sở ×0,75, Medium = cơ sở, High = cơ sở ×1,25 (làm tròn). 0 về thực tế tắt cảnh báo. Phạm vi: 0–255.)",
        R"(Skala automatyczna: Low = baza ×0,75, Medium = baza, High = baza ×1,25 (zaokrąglone). 0 praktycznie wyłącza ostrzeżenie. Zakres: 0–255.)",
        R"(Scală automată: Low = bază ×0,75, Medium = bază, High = bază ×1,25 (rotunjit). 0 dezactivează practic avertismentul. Interval: 0–255.)"
    },
    {
        "lblMetroidNativeAimRegisterInjectionDesc",
        R"(最小遅延のため、エイム呼び出し地点でフックします。二足状態と spec108=1 のトランスフォーム (トレース/サイラックス/ウィーヴェル) をカバーします。有効にすると、上のネイティブエイムデルタHook (PostFold書き込み) より優先されます。)",
        R"(Hookt am Zielaufruf-Punkt für minimale Latenz. Deckt Zweibeiner-Zustand und spec108=1-Transformationen (Trace/Sylux/Weavel) ab. Wenn aktiviert, hat Vorrang vor Native Aim Delta Hook (PostFold-Schreibzugriff) oben.)",
        R"(Engancha en el punto de llamada de puntería para mínima latencia. Cubre estado bípedo y transformaciones spec108=1 (Trace/Sylux/Weavel). Si está activo, tiene prioridad sobre Native Aim Delta Hook (escritura PostFold) de arriba.)",
        R"(S'accroche au point d'appel de visée pour une latence minimale. Couvre l'état bipède et les transformations spec108=1 (Trace/Sylux/Weavel). Si activé, prioritaire sur Native Aim Delta Hook (écriture PostFold) ci-dessus.)",
        R"(Si aggancia al punto di chiamata mira per latenza minima. Copre lo stato bipede e le trasformazioni spec108=1 (Trace/Sylux/Weavel). Se attivato, ha priorità su Native Aim Delta Hook (scrittura PostFold) sopra.)",
        R"(Hookt op het richtaanrooppunt voor minimale latentie. Dekkt tweevoeter-toestand en spec108=1-transformaties (Trace/Sylux/Weavel) af. Indien ingeschakeld, heeft voorrang op Native Aim Delta Hook (PostFold-schrijven) hierboven.)",
        R"(Engancha no ponto de chamada de mira para latência mínima. Cobre estado bípede e transformações spec108=1 (Trace/Sylux/Weavel). Se ativo, tem prioridade sobre Native Aim Delta Hook (escrita PostFold) acima.)",
        R"(Перехватывает в точке вызова прицела для минимальной задержки. Охватывает двуногое состояние и трансформации spec108=1 (Trace/Sylux/Weavel). При включении имеет приоритет над Native Aim Delta Hook (запись PostFold) выше.)",
        "在瞄准调用点挂钩以实现最低延迟。覆盖 biped 状态与 spec108=1 变形（Trace/Sylux/Weavel）。启用时优先于上方的 Native Aim Delta Hook（PostFold 写入）。",
        R"(최소 지연을 위해 조준 호출 지점에서 후크합니다. 이족 상태와 spec108=1 변형(Trace/Sylux/Weavel)을 다룹니다. 활성화하면 위의 Native Aim Delta Hook(PostFold 쓰기)보다 우선합니다.)",
        R"(يتصل في نقطة استدعاء التصويب لأقل تأخير. يغطي حالة ثنائية الأرجل وتحولات spec108=1 (Trace/Sylux/Weavel). عند التفعيل، له الأولوية على Native Aim Delta Hook (كتابة PostFold) أعلاه.)",
        R"(Hook di titik pemanggilan bidik untuk latensi minimum. Mencakup status biped dan transformasi spec108=1 (Trace/Sylux/Weavel). Jika aktif, prioritas di atas Native Aim Delta Hook (penulisan PostFold) di atas.)",
        R"(Перехоплює в точці виклику прицілу для мінімальної затримки. Охоплює двоногий стан і трансформації spec108=1 (Trace/Sylux/Weavel). При увімкненні має пріоритет над Native Aim Delta Hook (запис PostFold) вище.)",
        R"(Συνδέεται στο σημείο κλήσης σκόπευσης για ελάχιστη καθυστέρηση. Καλύπτει διποδική κατάσταση και μεταμορφώσεις spec108=1 (Trace/Sylux/Weavel). Αν ενεργοποιηθεί, έχει προτεραιότητα έναντι του Native Aim Delta Hook (εγγραφή PostFold) παραπάνω.)",
        R"(Hookar vid siktanropspunkten för minimal latens. Täcker biped-tillstånd och spec108=1-transformationer (Trace/Sylux/Weavel). Om aktiverat har prioritet före Native Aim Delta Hook (PostFold-skrivning) ovan.)",
        R"(hook ที่จุดเรียกเล็งเพื่อความหน่วงต่ำสุด ครอบคลุมสถานะ biped และ transform spec108=1 (Trace/Sylux/Weavel) เมื่อเปิดใช้ มีลำดับความสำคัญเหนือ Native Aim Delta Hook (การเขียน PostFold) ด้านบน)",
        R"(Hookuje v bodě volání míření pro minimální latenci. Pokrývá biped stav a transformace spec108=1 (Trace/Sylux/Weavel). Pokud je zapnuto, má prioritu před Native Aim Delta Hook (zápis PostFold) výše.)",
        R"(Hooker ved sigtekaldpunktet for minimal latency. Dækker biped-tilstand og spec108=1-transformationer (Trace/Sylux/Weavel). Hvis aktiveret, har prioritet over Native Aim Delta Hook (PostFold-skrivning) ovenfor.)",
        R"(Minimum gecikme için nişan çağrı noktasında hook yapar. Biped durumu ve spec108=1 dönüşümlerini (Trace/Sylux/Weavel) kapsar. Etkinse, yukarıdaki Native Aim Delta Hook (PostFold yazımı) üzerinde önceliği vardır.)",
        R"(Hooker ved siktekaldpunktet for minimal latency. Dekker biped-tilstand og spec108=1-transformasjoner (Trace/Sylux/Weavel). Hvis aktivert, har prioritet over Native Aim Delta Hook (PostFold-skriving) over.)",
        R"(A célzás hívási pontján hookol a minimális késleltetésért. Lefedi a biped állapotot és a spec108=1 transzformációkat (Trace/Sylux/Weavel). Bekapcsolva elsőbbséget élvez a fenti Native Aim Delta Hook (PostFold írás) felett.)",
        R"(Hookkaa tähtäyskutsukohdassa minimaalisen viiveen vuoksi. Kattaa biped-tilan ja spec108=1-muunnokset (Trace/Sylux/Weavel). Käytössä etusijalla yllä olevaan Native Aim Delta Hook (PostFold-kirjoitus) nähden.)",
        R"(Hook tại điểm gọi ngắm để độ trễ tối thiểu. Bao phủ trạng thái biped và biến hình spec108=1 (Trace/Sylux/Weavel). Khi bật, ưu tiên hơn Native Aim Delta Hook (ghi PostFold) ở trên.)",
        R"(Hookuje w punkcie wywołania celowania dla minimalnego opóźnienia. Obejmuje stan biped i transformacje spec108=1 (Trace/Sylux/Weavel). Po włączeniu ma pierwszeństwo nad Native Aim Delta Hook (zapis PostFold) powyżej.)",
        R"(Se conectează la punctul de apel al țintirii pentru latență minimă. Acoperă starea biped și transformările spec108=1 (Trace/Sylux/Weavel). Dacă este activat, are prioritate față de Native Aim Delta Hook (scriere PostFold) de mai sus.)"
    },
    {
        "lblMetroidImmediateInputEdgeOverlayDesc",
        R"(押下/離上エッジをエミュレーター側で生成し、プレイヤーアクション処理前にMPH内部入力状態へ重ねます。射撃、ジャンプ、ズーム、移動をカバーします。ボタンマスクはプレイヤーの現在の操作プリセット割り当て表から読むため、すべてのプリセット (Touch R/L、Dual R/L) に自動対応します。)",
        R"(Erzeugt Drück-/Loslass-Kanten im Emulator und legt sie vor der Spieleraktionsverarbeitung über den MPH-internen Eingabestatus. Deckt Schießen, Springen, Zoomen und Bewegung ab. Button-Masken werden aus der aktuellen Steuerungs-Preset-Zuordnung gelesen; unterstützt alle Presets (Touch R/L, Dual R/L) automatisch.)",
        R"(Genera bordes de pulsación/suelta en el emulador y los superpone al estado de entrada interno de MPH antes del procesamiento de acciones del jugador. Cubre disparo, salto, zoom y movimiento. Lee las máscaras de botón de la asignación del preset actual; compatible con todos los presets (Touch R/L, Dual R/L) automáticamente.)",
        R"(Génère les fronts appui/relâchement côté émulateur et les superpose à l'état d'entrée interne MPH avant le traitement des actions joueur. Couvre tir, saut, zoom et déplacement. Lit les masques de boutons depuis le preset actuel ; compatible automatiquement avec tous les presets (Touch R/L, Dual R/L).)",
        R"(Genera fronti pressione/rilascio lato emulatore e li sovrappone allo stato input interno MPH prima dell'elaborazione azioni giocatore. Copre sparo, salto, zoom e movimento. Legge le maschere pulsante dal preset attuale; compatibile automaticamente con tutti i preset (Touch R/L, Dual R/L).)",
        R"(Genereert druk-/loslaatflanken aan emulatorzijde en legt ze over de MPH-interne invoerstatus vóór spelersactieverwerking. Dekkt schieten, springen, zoomen en beweging af. Leest knopmaskers uit de huidige preset-toewijzing; ondersteunt automatisch alle presets (Touch R/L, Dual R/L).)",
        R"(Gera flancos de premir/soltar no emulador e sobrepõe-os ao estado de entrada interno MPH antes do processamento de ações do jogador. Cobre disparo, salto, zoom e movimento. Lê máscaras de botão da atribuição do preset atual; compatível automaticamente com todos os presets (Touch R/L, Dual R/L).)",
        R"(Генерирует фронты нажатия/отпускания на стороне эмулятора и накладывает их на внутреннее состояние ввода MPH до обработки действий игрока. Охватывает стрельбу, прыжок, зум и движение. Читает маски кнопок из текущего пресета; автоматически поддерживает все пресеты (Touch R/L, Dual R/L).)",
        "在模拟器侧生成按下/松开边沿，并在玩家动作处理前叠加到 MPH 内部输入状态。覆盖射击、跳跃、缩放与移动。从当前操作预设分配表读取按钮掩码，自动适配所有预设（Touch R/L、Dual R/L）。",
        R"(에뮬레이터 측에서 누름/뗌 엣지를 생성해 플레이어 동작 처리 전 MPH 내부 입력 상태에 겹칩니다. 사격, 점프, 줌, 이동을 다룹니다. 버튼 마스크는 현재 조작 프리셋 할당표에서 읽으며 모든 프리셋(Touch R/L, Dual R/L)에 자동 대응합니다.)",
        R"(يولّد حواف الضغط/الإفلات على جانب المحاكي ويُركّبها على حالة إدخال MPH الداخلية قبل معالجة إجراءات اللاعب. يغطي الإطلاق والقفز والتكبير والحركة. يقرأ أقنعة الأزرار من تعيين preset التحكم الحالي؛ يدعم تلقائياً جميع presets (Touch R/L، Dual R/L).)",
        R"(Menghasilkan edge tekan/lepas di sisi emulator dan menumpukkannya ke status input internal MPH sebelum pemrosesan aksi pemain. Mencakup tembak, lompat, zoom, dan gerak. Membaca mask tombol dari preset kontrol saat ini; otomatis mendukung semua preset (Touch R/L, Dual R/L).)",
        R"(Генерує фронти натискання/відпускання на стороні емулятора та накладає їх на внутрішній стан вводу MPH до обробки дій гравця. Охоплює стрільбу, стрибок, зум і рух. Читає маски кнопок з поточного пресета керування; автоматично підтримує всі пресети (Touch R/L, Dual R/L).)",
        R"(Δημιουργεί ακμές πίεσης/αφής στην πλευρά του εξομοιωτή και τις επικαλύπτει στην εσωτερική κατάσταση εισόδου MPH πριν την επεξεργασία ενεργειών παίκτη. Καλύπτει πυροβολισμό, άλμα, ζουμ και κίνηση. Διαβάζει μάσκες κουμπιών από το τρέχον preset ελέγχου· υποστηρίζει αυτόματα όλα τα presets (Touch R/L, Dual R/L).)",
        R"(Genererar tryck-/släppkanter på emulatorsidan och lägger dem över MPH:s interna ingångstillstånd före spelaråtgärdsbearbetning. Täcker skjutning, hopp, zoom och rörelse. Läser knappmasker från aktuell kontrollpreset; stöder automatiskt alla presets (Touch R/L, Dual R/L).)",
        R"(สร้าง edge กด/ปล่อยฝั่งเอมูเลเตอร์และซ้อนบนสถานะอินพุตภายใน MPH ก่อนประมวลผลการกระทำของผู้เล่น ครอบคลุมยิง กระโดด ซูม และเคลื่อนที่ อ่านมาสก์ปุ่มจาก preset การควบคุมปัจจุบัน รองรับ preset ทั้งหมด (Touch R/L, Dual R/L) อัตโนมัติ)",
        R"(Generuje hrany stisku/uvolnění na straně emulátoru a překrývá je na interní stav vstupu MPH před zpracováním akcí hráče. Pokrývá střelbu, skok, zoom a pohyb. Čte masky tlačítek z aktuálního ovládacího presetu; automaticky podporuje všechny presety (Touch R/L, Dual R/L).)",
        R"(Genererer tryk-/slipkanter på emulator-siden og lægger dem over MPH's interne inputtilstand før spillerhandlingsbehandling. Dækker skydning, spring, zoom og bevægelse. Læser knapmasker fra nuværende kontrolpreset; understøtter automatisk alle presets (Touch R/L, Dual R/L).)",
        R"(Emülatör tarafında basma/bırakma kenarları üretir ve oyuncu eylemi işlenmeden önce MPH dahili giriş durumunun üzerine bindirir. Ateş, zıplama, zoom ve hareketi kapsar. Düğme maskelerini mevcut kontrol preset atamasından okur; tüm presetleri (Touch R/L, Dual R/L) otomatik destekler.)",
        R"(Genererer trykk-/slippkanter på emulatorsiden og legger dem over MPHs interne inngangstilstand før spillerhandlingsbehandling. Dekker skyting, hopp, zoom og bevegelse. Leser knappmasker fra gjeldende kontrollpreset; støtter automatisk alle presets (Touch R/L, Dual R/L).)",
        R"(Nyomás/felengedés éleket generál az emulátor oldalán, és a játékos művelet feldolgozása előtt ráhelyezi az MPH belső bemeneti állapotára. Lefedi a lövést, ugrást, zoomot és mozgást. A gombmaszkokat az aktuális vezérlési preset hozzárendelésből olvassa; automatikusan támogat minden presetet (Touch R/L, Dual R/L).)",
        R"(Luo painallus/vapautusreunat emulaattoripuolella ja asettaa ne MPH:n sisäiseen syötetilaan ennen pelaajatoimien käsittelyä. Kattaa ampumisen, hypyn, zoomin ja liikkeen. Lukee painikemaskit nykyisestä ohjauspresetistä; tukee automaattisesti kaikkia presetejä (Touch R/L, Dual R/L).)",
        R"(Tạo cạnh nhấn/thả phía emulator và chồng lên trạng thái đầu vào nội bộ MPH trước khi xử lý hành động người chơi. Bao phủ bắn, nhảy, zoom và di chuyển. Đọc mặt nạ nút từ preset điều khiển hiện tại; tự hỗ trợ mọi preset (Touch R/L, Dual R/L).)",
        R"(Generuje krawędzie naciśnięcia/zwolnienia po stronie emulatora i nakłada je na wewnętrzny stan wejścia MPH przed przetwarzaniem akcji gracza. Obejmuje strzelanie, skok, zoom i ruch. Czyta maski przycisków z bieżącego presetu sterowania; automatycznie obsługuje wszystkie presety (Touch R/L, Dual R/L).)",
        R"(Generează fronturi apăsare/eliberare pe partea emulatorului și le suprapune stării interne de intrare MPH înainte de procesarea acțiunilor jucătorului. Acoperă foc, salt, zoom și mișcare. Citește măștile butoanelor din presetul de control curent; suportă automat toate preseturile (Touch R/L, Dual R/L).)"
    },
    {
        "lblMetroidWeaponSwitchMethodDesc",
        "オンにすると、ARM9フック経由でゲーム本来の武器装備処理を使います。オフでは互換性テスト用に、従来のタッチ/メニュー模擬による武器切替を使います。",
        R"(Wenn aktiviert, nutzt der ARM9-Hook die native Waffenausrüstungslogik des Spiels. Wenn deaktiviert, wird für Kompatibilitätstests die bisherige Touch-/Menü-Simulation verwendet.)",
        R"(Activado: usa el hook ARM9 con el equipamiento de armas nativo del juego. Desactivado: usa la simulación táctil/menú tradicional para pruebas de compatibilidad.)",
        R"(Activé : le hook ARM9 utilise l'équipement d'armes natif du jeu. Désactivé : utilise la simulation tactile/menu traditionnelle pour les tests de compatibilité.)",
        R"(Attivato: l'hook ARM9 usa l'equipaggiamento armi nativo del gioco. Disattivato: usa la simulazione touch/menu tradizionale per test di compatibilità.)",
        R"(Ingeschakeld: ARM9-hook gebruikt native wapenuitrusting van het spel. Uitgeschakeld: gebruikt traditionele touch-/menusimulatie voor compatibiliteitstests.)",
        R"(Ativado: o hook ARM9 usa o equipamento de armas nativo do jogo. Desativado: usa a simulação tátil/menu tradicional para testes de compatibilidade.)",
        R"(Вкл.: ARM9-хук использует нативную логику экипировки оружия игры. Выкл.: использует традиционную эмуляцию касаний/меню для тестов совместимости.)",
        "开启：通过 ARM9 钩子使用游戏原生武器装备处理。关闭：为兼容性测试使用传统触控/菜单模拟武器切换。",
        "켜면 ARM9 훅으로 게임 기본 무기 장착 처리를 사용합니다. 끄면 호환성 테스트용으로 기존 터치/메뉴 시뮬레이션 무기 전환을 사용합니다.",
        R"(عند التفعيل، يستخدم hook ARM9 منطق تجهيز الأسلحة الأصلية للعبة. عند التعطيل، يُستخدم محاكاة اللمس/القائمة التقليدية لاختبار التوافق.)",
        R"(Saat aktif, hook ARM9 menggunakan logika equip senjata asli game. Saat nonaktif, simulasi sentuh/menu tradisional digunakan untuk uji kompatibilitas.)",
        R"(Увімк.: ARM9-хук використовує нативну логіку екіпірування зброї гри. Вимк.: використовується традиційна емуляція дотику/меню для тестів сумісності.)",
        R"(Ενεργό: το ARM9 hook χρησιμοποιεί την εγγενή λογική εξοπλισμού όπλων του παιχνιδιού. Ανενεργό: χρησιμοποιείται παραδοσιακή προσομοίωση αφής/μενού για δοκιμές συμβατότητας.)",
        R"(På: ARM9-hook använder spelets inbyggda vapenutrustningslogik. Av: använder traditionell pek-/menysimulering för kompatibilitetstest.)",
        "เปิด: ARM9 hook ใช้ตรรกะติดอาวุธดั้งเดิมของเกม ปิด: ใช้การจำลองสัมผัส/เมนูแบบเดิมสำหรับทดสอบความเข้ากันได้",
        R"(Zapnuto: ARM9 hook používá nativní logiku vybavení zbraní hry. Vypnuto: používá tradiční simulaci dotyku/menu pro testy kompatibility.)",
        R"(Til: ARM9-hook bruger spillets native våbenudrustningslogik. Fra: bruger traditionel touch-/menusimulation til kompatibilitetstest.)",
        R"(Açık: ARM9 hook oyunun yerel silah donatma mantığını kullanır. Kapalı: uyumluluk testi için geleneksel dokunmatik/menü simülasyonu kullanılır.)",
        R"(På: ARM9-hook bruker spillets native våpenutrustningslogikk. Av: bruker tradisjonell touch-/menysimulering for kompatibilitetstest.)",
        R"(Be: az ARM9 hook a játék natív fegyver-felszerelési logikáját használja. Ki: hagyományos érintés/menü szimuláció kompatibilitási tesztekhez.)",
        R"(Päällä: ARM9-hook käyttää pelin natiivia asevarustuslogiikkaa. Pois: perinteinen kosketus-/valikkosimulaatio yhteensopivuustestejä varten.)",
        "Bật: hook ARM9 dùng logic trang bị vũ khí gốc của game. Tắt: dùng mô phỏng cảm ứng/menu truyền thống để thử tương thích.",
        R"(Wł.: hook ARM9 używa natywnej logiki wyposażenia broni gry. Wył.: używa tradycyjnej symulacji dotyku/menu do testów zgodności.)",
        R"(Activat: hook-ul ARM9 folosește logica nativă de echipare arme a jocului. Dezactivat: folosește simularea tactilă/meniu tradițională pentru teste de compatibilitate.)"
    },
    {
        "lblMetroidBipedFireMethodDesc",
        "オンにすると、ゲーム本来の二足射撃エッジフックで射撃入力ヘルパーの結果をtrueにし、元のクールダウン、弾薬、弾生成、HUD、SFX経路を自然に動かします。旧方式では従来のDS入力/即時入力エッジ合成による射撃経路を使います。",
        R"(Wenn aktiviert, setzt der native Zweibeiner-Schuss-Edge-Hook das Schuss-Eingabe-Hilfsresultat auf true und nutzt natürlich Cooldown, Munition, Projektilerzeugung, HUD und SFX-Pfade. Die alte Methode nutzt die bisherige DS-Eingabe-/Sofort-Edge-Synthese.)",
        R"(Activado: el hook de borde de disparo bípedo nativo pone el resultado del helper de disparo en true y activa naturalmente cooldown, munición, generación de proyectiles, HUD y SFX. El método antiguo usa la síntesis DS/entrada inmediata tradicional.)",
        R"(Activé : le hook de front de tir bipède natif met le résultat du helper de tir à true et active naturellement cooldown, munitions, génération de projectiles, HUD et SFX. L'ancienne méthode utilise la synthèse DS/entrée immédiate traditionnelle.)",
        R"(Attivato: l'hook fronte sparo bipede nativo imposta il risultato dell'helper sparo su true e attiva naturalmente cooldown, munizioni, generazione proiettili, HUD e SFX. Il metodo vecchio usa la sintesi DS/input immediato tradizionale.)",
        R"(Ingeschakeld: native tweevoeter-schotflank-hook zet het schietinvoer-helperresultaat op true en activeert natuurlijk cooldown, munitie, projectielgeneratie, HUD en SFX. Oude methode gebruikt traditionele DS-/directe-invoersynthese.)",
        R"(Ativado: o hook de flanco de disparo bípede nativo define o resultado do helper de disparo como true e ativa naturalmente cooldown, munição, geração de projéteis, HUD e SFX. O método antigo usa a síntese DS/entrada imediata tradicional.)",
        R"(Вкл.: нативный хук фронта выстрела двуногого ставит результат помощника выстрела в true и естественно активирует cooldown, боеприпасы, создание снарядов, HUD и SFX. Старый метод использует традиционный синтез DS/мгновенного ввода.)",
        "开启：游戏原生 biped 射击边沿钩子将射击输入 helper 结果设为 true，并自然走原冷却、弹药、弹体生成、HUD 与 SFX 路径。旧方式使用传统 DS 输入/即时输入边沿合成射击路径。",
        R"(켜면 게임 기본 이족 사격 엣지 훅이 사격 입력 헬퍼 결과를 true로 하고 원래 쿨다운, 탄약, 탄 생성, HUD, SFX 경로를 자연스럽게 사용합니다. 구 방식은 기존 DS 입력/즉시 입력 엣지 합성 사격 경로를 사용합니다.)",
        R"(عند التفعيل، يضع hook حافة إطلاق ثنائي الأرجل الأصلية نتيجة مساعد إدخال الإطلاق على true ويستخدم مسارات cooldown والذخيرة وتوليد المقذوفات وHUD وSFX الأصلية. الطريقة القديمة تستخدم تركيب إدخال DS/الحافة الفورية التقليدي.)",
        R"(Saat aktif, hook edge tembak biped native menetapkan hasil helper input tembak ke true dan secara alami menggunakan jalur cooldown, amunisi, spawn proyektil, HUD, dan SFX asli. Metode lama menggunakan sintesis input DS/edge segera tradisional.)",
        R"(Увімк.: нативний хук фронту пострілу biped ставить результат helper вводу пострілу в true і природно використовує шляхи cooldown, боєприпасів, створення снарядів, HUD і SFX. Старий метод використовує традиційний синтез DS/миттєвого вводу.)",
        R"(Ενεργό: το εγγενές hook front πυροβολισμού biped θέτει το αποτέλεσμα helper εισόδου πυροβολισμού σε true και ενεργοποιεί φυσικά cooldown, πυρομαχικά, δημιουργία βλημάτων, HUD και SFX. Η παλιά μέθοδος χρησιμοποιεί παραδοσιακή σύνθεση DS/άμεσης εισόδου.)",
        R"(På: native biped skottflank-hook sätter skjutinmatningshelperresultatet till true och aktiverar naturligt cooldown, ammunition, projektilgenerering, HUD och SFX. Gamla metoden använder traditionell DS-/omedelbar inmatningssyntes.)",
        R"(เปิด: hook edge ยิง biped ดั้งเดิมตั้งผล helper อินพุตยิงเป็น true และใช้เส้นทาง cooldown กระสุน สร้าง projectile HUD และ SFX ตามธรรมชาติ วิธีเก่าใช้การสังเคราะห์อินพุต DS/edge ทันทีแบบเดิม)",
        R"(Zapnuto: nativní biped střelba edge hook nastaví výsledek střeleckého input helperu na true a přirozeně aktivuje cooldown, munici, generování projektilů, HUD a SFX. Stará metoda používá tradiční syntézu DS/okamžitého vstupu.)",
        R"(Til: native biped skudflank-hook sætter skudinput-helperresultat til true og aktiverer naturligt cooldown, ammunition, projektilgenerering, HUD og SFX. Gamle metode bruger traditionel DS-/øjeblikkelig inputsyntese.)",
        R"(Açık: yerel biped ateş kenarı hook'u ateş girişi helper sonucunu true yapar ve doğal olarak cooldown, mühimmat, mermi oluşturma, HUD ve SFX yollarını kullanır. Eski yöntem geleneksel DS/anlık giriş sentezini kullanır.)",
        R"(På: native biped skuddflank-hook setter skuddinput-helperresultat til true og aktiverer naturlig cooldown, ammunisjon, prosjektilgenerering, HUD og SFX. Gammel metode bruker tradisjonell DS-/umiddelbar inputsyntese.)",
        R"(Be: a natív biped lövés él hook true-ra állítja a lövés bemenet helper eredményét, és természetesen használja az eredeti cooldown, lőszer, lövedék-generálás, HUD és SFX útvonalakat. A régi módszer a hagyományos DS/azonnali bemenet szintézist használja.)",
        R"(Päällä: natiivi biped-laukaisureuna-hook asettaa laukaisusyöte-helperin tuloksen trueksi ja käyttää luonnollisesti cooldown-, ammus-, ammusgenerointi-, HUD- ja SFX-polkuja. Vanha menetelmä käyttää perinteistä DS-/välitöntä syötesynteesiä.)",
        R"(Bật: hook cạnh bắn biped gốc đặt kết quả helper đầu vào bắn thành true và tự nhiên dùng đường cooldown, đạn, tạo đạn, HUD và SFX gốc. Cách cũ dùng tổng hợp đầu vào DS/cạnh tức thì truyền thống.)",
        R"(Wł.: natywny hook krawędzi strzału biped ustawia wynik helpera wejścia strzału na true i naturalnie używa ścieżek cooldown, amunicji, generowania pocisków, HUD i SFX. Stara metoda używa tradycyjnej syntezy wejścia DS/natychmiastowego.)",
        R"(Activat: hook-ul nativ de front foc biped setează rezultatul helperului de intrare foc la true și activează natural cooldown, muniție, generare proiectile, HUD și SFX. Metoda veche folosește sinteza tradițională DS/intrare imediată.)"
    },
    {
        "lblMetroidZoomMethodDesc",
        "新方式はゲーム内のズーム割り当て表を読むため、Touch/Dualプリセットごとに異なるDSボタンへズームを割り当てられます。旧方式より少し低遅延です。両方のチェックを外すと、従来の入力経路と同じく固定Rボタンを使う旧方式になります。",
        R"(Die neue Methode liest die ingame-Zoom-Zuordnungstabelle, sodass Zoom je nach Touch/Dual-Preset unterschiedlichen DS-Tasten zugewiesen wird. Etwas geringere Latenz als die alte Methode. Wenn beide deaktiviert sind, wird wie bisher der feste R-Knopf genutzt.)",
        R"(El método nuevo lee la tabla de asignación de zoom del juego, así el zoom se asigna a botones DS distintos según el preset Touch/Dual. Algo menos de latencia que el método antiguo. Con ambos desactivados, usa el botón R fijo como antes.)",
        R"(La nouvelle méthode lit la table d'affectation zoom en jeu, donc le zoom est assigné à des boutons DS différents selon le preset Touch/Dual. Légèrement moins de latence que l'ancienne méthode. Si les deux sont désactivés, utilise le bouton R fixe comme avant.)",
        R"(Il nuovo metodo legge la tabella assegnazione zoom in gioco, quindi lo zoom è assegnato a pulsanti DS diversi per preset Touch/Dual. Leggermente meno latenza del metodo vecchio. Con entrambi disattivati, usa il pulsante R fisso come prima.)",
        R"(De nieuwe methode leest de ingame zoom-toewijzingstabel, zodat zoom per Touch/Dual-preset aan verschillende DS-knoppen wordt toegewezen. Iets lagere latentie dan de oude methode. Met beide uitgeschakeld wordt de vaste R-knop gebruikt zoals voorheen.)",
        R"(O método novo lê a tabela de atribuição de zoom do jogo, pelo que o zoom é atribuído a botões DS diferentes por preset Touch/Dual. Ligeiramente menos latência que o método antigo. Com ambos desativados, usa o botão R fixo como antes.)",
        R"(Новый метод читает таблицу назначения зума в игре, поэтому зум назначается разным кнопкам DS в зависимости от пресета Touch/Dual. Немного меньше задержка, чем у старого метода. Если оба выключены, используется фиксированная кнопка R, как раньше.)",
        "新方式读取游戏内缩放分配表，因此可按 Touch/Dual 预设将缩放分配到不同 DS 按钮。比旧方式略低延迟。两者均关闭时，与以前一样使用固定 R 键。",
        "새 방식은 게임 내 줌 할당표를 읽어 Touch/Dual 프리셋마다 다른 DS 버튼에 줌을 할당합니다. 구 방식보다 약간 낮은 지연입니다. 둘 다 끄면 이전과 같이 고정 R 버튼을 사용합니다.",
        R"(الطريقة الجديدة تقرأ جدول تعيين zoom داخل اللعبة، لذا يُعيَّن zoom لأزرار DS مختلفة حسب preset Touch/Dual. تأخير أقل قليلاً من الطريقة القديمة. عند تعطيل كليهما، يُستخدم زر R الثابت كما سابقاً.)",
        R"(Metode baru membaca tabel penugasan zoom dalam game, sehingga zoom ditetapkan ke tombol DS berbeda per preset Touch/Dual. Sedikit latensi lebih rendah dari metode lama. Jika keduanya nonaktif, tombol R tetap digunakan seperti sebelumnya.)",
        R"(Новий метод читає таблицю призначення зуму в грі, тому зум призначається різним кнопкам DS залежно від пресета Touch/Dual. Трохи менша затримка, ніж у старого методу. Якщо обидва вимкнено, використовується фіксована кнопка R, як раніше.)",
        R"(Η νέα μέθοδος διαβάζει τον πίνακα ανάθεσης zoom στο παιχνίδι, οπότε το zoom ανατίθεται σε διαφορετικά κουμπιά DS ανά preset Touch/Dual. Ελαφρώς χαμηλότερη καθυστέρηση από την παλιά μέθοδο. Αν και τα δύο είναι ανενεργά, χρησιμοποιείται το σταθερό κουμπί R όπως πριν.)",
        R"(Den nya metoden läser spelets zoom-tilldelningstabell, så zoom tilldelas olika DS-knappar per Touch/Dual-preset. Något lägre latens än gamla metoden. Om båda är av används fast R-knapp som tidigare.)",
        R"(วิธีใหม่อ่านตารางกำหนด zoom ในเกม จึงกำหนด zoom ให้ปุ่ม DS ต่างกันตาม preset Touch/Dual หน่วงต่ำกว่าวิธีเก่าเล็กน้อย หากปิดทั้งคู่ ใช้ปุ่ม R คงที่เหมือนเดิม)",
        R"(Nová metoda čte tabulku přiřazení zoomu ve hře, takže zoom je přiřazen různým DS tlačítkům podle presetu Touch/Dual. O něco nižší latence než stará metoda. Pokud jsou obě vypnuté, použije se pevné tlačítko R jako dříve.)",
        R"(Den nye metode læser spillets zoom-tildelingstabel, så zoom tildeles forskellige DS-knapper pr. Touch/Dual-preset. Lidt lavere latency end gamle metode. Hvis begge er fra, bruges fast R-knap som før.)",
        R"(Yeni yöntem oyun içi zoom atama tablosunu okur, bu yüzden zoom Touch/Dual presetine göre farklı DS düğmelerine atanır. Eski yöntemden biraz daha düşük gecikme. İkisi de kapalıysa eskisi gibi sabit R düğmesi kullanılır.)",
        R"(Den nye metoden leser spillets zoom-tildelingstabell, så zoom tildeles ulike DS-knapper per Touch/Dual-preset. Litt lavere latency enn gamle metode. Hvis begge er av, brukes fast R-knapp som før.)",
        R"(Az új módszer a játék zoom-hozzárendelési tábláját olvassa, így a zoom különböző DS gombokhoz rendelődik Touch/Dual preset szerint. Kissé alacsonyabb késleltetés, mint a régi módszernél. Ha mindkettő ki van kapcsolva, a fix R gomb marad, mint korábban.)",
        R"(Uusi menetelmä lukee pelin zoom-kohdistustaulukon, joten zoom määrätään eri DS-painikkeisiin Touch/Dual-presetin mukaan. Hieman alempi viive kuin vanhassa menetelmässä. Jos molemmat pois päältä, kiinteää R-painiketta käytetään kuten ennen.)",
        R"(Cách mới đọc bảng gán zoom trong game nên zoom được gán cho nút DS khác nhau the mỗi preset Touch/Dual. Độ trễ thấp hơn chút so với cách cũ. Nếu tắt cả hai, dùng nút R cố định như trước.)",
        R"(Nowa metoda czyta tabelę przypisania zoomu w grze, więc zoom przypisuje się do różnych przycisków DS według presetu Touch/Dual. Nieco niższe opóźnienie niż stara metoda. Gdy oba wyłączone, używany jest stały przycisk R jak wcześniej.)",
        R"(Metoda nouă citește tabelul de atribuire zoom din joc, deci zoom-ul este atribuit unor butoane DS diferite per preset Touch/Dual. Latență puțin mai mică decât metoda veche. Dacă ambele sunt oprite, se folosește butonul R fix ca înainte.)"
    },
    {
        "lblMetroidZoomMethod2Desc",
        "新方式2は、押すたびにゲーム本来のズーム状態を切り替えます。「ズームに新方式を使う」とは同時に使えません。",
        R"(Neue Methode 2 schaltet bei jedem Drücken den nativen Zoom-Zustand des Spiels um. Kann nicht gleichzeitig mit „Neue Methode für Zoom verwenden“ genutzt werden.)",
        R"(El método nuevo 2 alterna el estado de zoom nativo del juego en cada pulsación. No se puede usar junto con «Usar método nuevo para zoom».)",
        R"(La nouvelle méthode 2 bascule l'état de zoom natif du jeu à chaque appui. Ne peut pas être utilisée en même temps que « Utiliser la nouvelle méthode pour le zoom ».)",
        R"(Il nuovo metodo 2 alterna lo stato zoom nativo del gioco a ogni pressione. Non può essere usato insieme a «Usa nuovo metodo per zoom».)",
        R"(Nieuwe methode 2 schakelt bij elke druk de native zoomtoestand van het spel om. Kan niet tegelijk met «Nieuwe methode voor zoom gebruiken» worden gebruikt.)",
        R"(O método novo 2 alterna o estado de zoom nativo do jogo a cada pressão. Não pode ser usado juntamente com «Usar método novo para zoom».)",
        R"(Новый метод 2 переключает нативное состояние зума игры при каждом нажатии. Нельзя использовать одновременно с «Использовать новый метод для зума».)",
        "新方式 2 每次按下切换游戏原生缩放状态。不能与「缩放使用新方式」同时使用。",
        "새 방식 2는 누를 때마다 게임 기본 줌 상태를 전환합니다. «줌에 새 방식 사용»과 동시에 쓸 수 없습니다.",
        R"(الطريقة الجديدة 2 تبدّل حالة zoom الأصلية للعبة عند كل ضغطة. لا يمكن استخدامها مع «استخدام الطريقة الجديدة للzoom» في آن واحد.)",
        R"(Metode baru 2 mengalihkan status zoom asli game setiap tekan. Tidak dapat digunakan bersama «Gunakan metode baru untuk zoom».)",
        R"(Новий метод 2 перемикає нативний стан зуму гри при кожному натисканні. Не можна використовувати одночасно з «Використовувати новий метод для зуму».)",
        R"(Η νέα μέθοδος 2 εναλλάσσει την εγγενή κατάσταση zoom του παιχνιδιού σε κάθε πάτημα. Δεν μπορεί να χρησιμοποιηθεί μαζί με «Χρήση νέας μεθόδου για zoom».)",
        R"(Nya metod 2 växlar spelets inbyggda zoomtillstånd vid varje tryck. Kan inte användas tillsammans med «Använd ny metod för zoom».)",
        "วิธีใหม่ 2 สลับสถานะ zoom ดั้งเดิมของเกมทุกครั้งที่กด ใช้พร้อม «ใช้วิธีใหม่สำหรับ zoom» ไม่ได้",
        "Nová metoda 2 přepíná nativní stav zoomu hry při každém stisku. Nelze používat spolu s «Použít novou metodu pro zoom».",
        "Nye metode 2 skifter spillets native zoomtilstand ved hvert tryk. Kan ikke bruges sammen med «Brug ny metode til zoom».",
        R"(Yeni yöntem 2 her basışta oyunun yerel zoom durumunu değiştirir. «Zoom için yeni yöntemi kullan» ile aynı anda kullanılamaz.)",
        "Ny metode 2 veksler spillets native zoomtilstand ved hvert trykk. Kan ikke brukes sammen med «Bruk ny metode for zoom».",
        R"(Az új 2. módszer minden nyomásra váltja a játék natív zoom állapotát. Nem használható együtt a «Új módszer használata zoomhoz» beállítással.)",
        R"(Uusi menetelmä 2 vaihtaa pelin natiivin zoom-tilan jokaisella painalluksella. Ei voi käyttää yhdessä «Käytä uutta menetelmää zoomiin» -asetuksen kanssa.)",
        "Cách mới 2 chuyển trạng thái zoom gốc của game mỗi lần nhấn. Không thể dùng cùng «Dùng cách mới cho zoom».",
        R"(Nowa metoda 2 przełącza natywny stan zoomu gry przy każdym naciśnięciu. Nie można używać razem z «Użyj nowej metody dla zoomu».)",
        R"(Metoda nouă 2 comută starea nativă de zoom a jocului la fiecare apăsare. Nu poate fi folosită împreună cu «Folosește metoda nouă pentru zoom».)"
    },
    {
        "btnClear",
        "クリア",
        "Löschen",
        "Borrar",
        "Effacer",
        "Cancella",
        "Wissen",
        "Limpar",
        "Очистить",
        "清除",
        "지우기",
        "مسح",
        "Hapus",
        "Очистити",
        "Καθαρισμός",
        "Rensa",
        "ล้าง",
        "Vymazat",
        "Ryd",
        "Temizle",
        "Tøm",
        "Törlés",
        "Tyhjennä",
        "Xóa",
        "Wyczyść",
        "Șterge"
    }
};

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

QString Tr(const QString& text)
{
    if (!IsMenuTranslationActive() || text.isEmpty())
        return text;

    const QString exact = TranslateExact(text);
    if (exact != text)
        return exact;

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

    if (text.startsWith(QStringLiteral("Configuring settings for instance ")))
    {
        const QString arg = text.mid(35);
        switch (ActiveMenuLanguage()) {
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
        switch (ActiveMenuLanguage()) {
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
        switch (ActiveMenuLanguage()) {
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
        switch (ActiveMenuLanguage()) {
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

    if (text == QStringLiteral("(none)"))
    {
        switch (ActiveMenuLanguage()) {
        case MenuLangId::German: return QStringLiteral("(keine)");
        case MenuLangId::Spanish: return QStringLiteral("(ninguno)");
        case MenuLangId::French: return QStringLiteral("(aucun)");
        case MenuLangId::Italian: return QStringLiteral("(nessuno)");
        case MenuLangId::Dutch: return QStringLiteral("(geen)");
        case MenuLangId::Portuguese: return QStringLiteral("(nenhum)");
        case MenuLangId::Russian: return QStringLiteral("(нет)");
        case MenuLangId::ChineseSimplified: return QStringLiteral("（无）");
        case MenuLangId::ChineseTraditional: return QStringLiteral("（無）");
        case MenuLangId::Korean: return QStringLiteral("(없음)");
        case MenuLangId::Japanese: return QStringLiteral("(なし)");
        default: return text;
        }
    }

    if (text.startsWith(QStringLiteral("Direct mode (requires "))
        && text.endsWith(QStringLiteral(" and ethernet connection)")))
    {
        const QString middle = text.mid(22, text.size() - 22 - 25);
        switch (ActiveMenuLanguage()) {
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
        switch (ActiveMenuLanguage()) {
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
        switch (ActiveMenuLanguage()) {
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

    return text;
}

QString TrWidgetText(const QWidget* widget, const QString& text)
{
    if (!IsMenuTranslationActive() || text.isEmpty())
        return text;

    const QString objectTranslated = TranslateByObjectName(widget, text);
    if (objectTranslated != text)
        return objectTranslated;

    return Tr(text);
}

QString SourcePropertyText(QWidget* widget, const char* propertyName, const QString& current)
{
    if (!widget)
        return current;

    const QVariant stored = widget->property(propertyName);
    if (stored.isValid())
        return stored.toString();

    widget->setProperty(propertyName, current);
    return current;
}

QStringList SourcePropertyTextList(QWidget* widget, const char* propertyName, const QStringList& current)
{
    if (!widget)
        return current;

    const QVariant stored = widget->property(propertyName);
    if (stored.isValid())
        return stored.toStringList();

    widget->setProperty(propertyName, current);
    return current;
}

QString SourceObjectPropertyText(QObject* object, const char* propertyName, const QString& current)
{
    if (!object)
        return current;

    const QVariant stored = object->property(propertyName);
    if (stored.isValid())
        return stored.toString();

    object->setProperty(propertyName, current);
    return current;
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

void LocalizeWidgetTextProperties(QWidget* widget)
{
    if (!widget)
        return;

    const QString windowTitle = widget->windowTitle();
    if (!windowTitle.isEmpty())
        widget->setWindowTitle(TrWidgetText(
            widget,
            SourcePropertyText(widget, "_melonprime_src_window_title", windowTitle)));

    const QString toolTip = widget->toolTip();
    if (!toolTip.isEmpty())
        widget->setToolTip(TrWidgetText(
            widget,
            SourcePropertyText(widget, "_melonprime_src_tooltip", toolTip)));

    const QString whatsThis = widget->whatsThis();
    if (!whatsThis.isEmpty())
        widget->setWhatsThis(TrWidgetText(
            widget,
            SourcePropertyText(widget, "_melonprime_src_whatsthis", whatsThis)));

    const QString statusTip = widget->statusTip();
    if (!statusTip.isEmpty())
        widget->setStatusTip(TrWidgetText(
            widget,
            SourcePropertyText(widget, "_melonprime_src_statustip", statusTip)));
}

void LocalizeWidgetTree(QWidget* root)
{
    if (!root)
        return;

    LocalizeWidgetTextProperties(root);

    for (QLabel* label : root->findChildren<QLabel*>())
    {
        label->setText(TrWidgetText(
            label,
            SourcePropertyText(label, "_melonprime_src_text", label->text())));
        LocalizeWidgetTextProperties(label);
    }

    for (QAbstractButton* button : root->findChildren<QAbstractButton*>())
    {
        button->setText(TrWidgetText(
            button,
            SourcePropertyText(button, "_melonprime_src_text", button->text())));
        LocalizeWidgetTextProperties(button);
    }

    for (QGroupBox* group : root->findChildren<QGroupBox*>())
    {
        group->setTitle(TrWidgetText(
            group,
            SourcePropertyText(group, "_melonprime_src_title", group->title())));
        LocalizeWidgetTextProperties(group);
    }

    for (QTabWidget* tabs : root->findChildren<QTabWidget*>())
    {
        QStringList tabTexts;
        tabTexts.reserve(tabs->count());
        for (int i = 0; i < tabs->count(); ++i)
            tabTexts.append(tabs->tabText(i));
        const QStringList sourceTabTexts =
            SourcePropertyTextList(tabs, "_melonprime_src_tab_texts", tabTexts);
        for (int i = 0; i < tabs->count(); ++i)
        {
            const QString source =
                (i < sourceTabTexts.size()) ? sourceTabTexts.at(i) : tabs->tabText(i);
            tabs->setTabText(i, Tr(source));
        }
        LocalizeWidgetTextProperties(tabs);
    }

    for (QComboBox* combo : root->findChildren<QComboBox*>())
    {
        if (qobject_cast<QFontComboBox*>(combo))
            continue;
        for (int i = 0; i < combo->count(); ++i)
        {
            static constexpr int kSourceItemTextRole = Qt::UserRole + 30001;
            const QVariant stored = combo->itemData(i, kSourceItemTextRole);
            const QString source = stored.isValid() ? stored.toString() : combo->itemText(i);
            if (!stored.isValid())
                combo->setItemData(i, source, kSourceItemTextRole);
            combo->setItemText(i, Tr(source));
        }
        LocalizeWidgetTextProperties(combo);
    }

    for (QLineEdit* lineEdit : root->findChildren<QLineEdit*>())
    {
        const QString placeholder = lineEdit->placeholderText();
        if (!placeholder.isEmpty())
            lineEdit->setPlaceholderText(TrWidgetText(
                lineEdit,
                SourcePropertyText(lineEdit, "_melonprime_src_placeholder", placeholder)));
        LocalizeWidgetTextProperties(lineEdit);
    }

    for (QPlainTextEdit* textEdit : root->findChildren<QPlainTextEdit*>())
    {
        const QString placeholder = textEdit->placeholderText();
        if (!placeholder.isEmpty())
            textEdit->setPlaceholderText(TrWidgetText(
                textEdit,
                SourcePropertyText(textEdit, "_melonprime_src_placeholder", placeholder)));
        LocalizeWidgetTextProperties(textEdit);
    }
}

void LocalizeActionTextProperties(QAction* action)
{
    if (!action)
        return;

    const QString toolTip = action->toolTip();
    if (!toolTip.isEmpty())
        action->setToolTip(Tr(SourceObjectPropertyText(
            action,
            "_melonprime_src_action_tooltip",
            toolTip)));

    const QString whatsThis = action->whatsThis();
    if (!whatsThis.isEmpty())
        action->setWhatsThis(Tr(SourceObjectPropertyText(
            action,
            "_melonprime_src_action_whatsthis",
            whatsThis)));

    const QString statusTip = action->statusTip();
    if (!statusTip.isEmpty())
        action->setStatusTip(Tr(SourceObjectPropertyText(
            action,
            "_melonprime_src_action_statustip",
            statusTip)));
}

void SetLocalizedActionText(QAction* action, const QString& sourceText)
{
    if (!action)
        return;

    action->setProperty("_melonprime_src_action_text", sourceText);
    action->setText(Tr(sourceText));
    LocalizeActionTextProperties(action);
}

void LocalizeAction(QAction* action);

void LocalizeMenu(QMenu* menu)
{
    if (!menu)
        return;

    const QString title = SourceObjectPropertyText(
        menu,
        "_melonprime_src_menu_title",
        menu->title());
    menu->setTitle(Tr(title));
    SetLocalizedActionText(menu->menuAction(), title);

    for (QAction* action : menu->actions())
        LocalizeAction(action);
}

void LocalizeAction(QAction* action)
{
    if (!action || action->isSeparator())
        return;

    if (QMenu* submenu = action->menu())
    {
        LocalizeMenu(submenu);
        return;
    }

    const QString source = SourceObjectPropertyText(
        action,
        "_melonprime_src_action_text",
        action->text());
    SetLocalizedActionText(action, source);
}

void LocalizeMenuBar(QMenuBar* menuBar)
{
    if (!menuBar)
        return;

    for (QAction* action : menuBar->actions())
        LocalizeAction(action);
}

namespace {

void wireMelonDsDialogDynamicLabels(QWidget* dialog)
{
    if (!dialog || !IsMenuTranslationActive())
        return;

    // CheatsDialog sets chkItemOption text in selection handlers (upstream .cpp).
    // Re-translate after each selection change from MelonPrime side only.
    if (dialog->objectName() != QStringLiteral("CheatsDialog"))
        return;

    auto* tree = dialog->findChild<QTreeView*>(QStringLiteral("tvCodeList"));
    auto* chk = dialog->findChild<QCheckBox*>(QStringLiteral("chkItemOption"));
    if (!tree || !chk)
        return;

    auto* sel = tree->selectionModel();
    if (!sel)
        return;

    const char* hookKey = "_melonprime_cheats_option_hook";
    if (dialog->property(hookKey).toBool())
        return;
    dialog->setProperty(hookKey, true);

    auto relocalizeOption = [chk]() {
        if (!IsMenuTranslationActive() || !chk)
            return;
        const QString raw = chk->text();
        const QString translated = Tr(raw);
        if (translated != raw)
            chk->setText(translated);
    };

    QObject::connect(
        sel,
        &QItemSelectionModel::selectionChanged,
        dialog,
        relocalizeOption,
        Qt::QueuedConnection);

    relocalizeOption();
}

class MelonDsLanPopupLocalizer final : public QObject
{
public:
    explicit MelonDsLanPopupLocalizer(QWidget* owner)
        : QObject(owner)
        , m_owner(owner)
    {
        qApp->installEventFilter(this);
    }

    ~MelonDsLanPopupLocalizer() override
    {
        qApp->removeEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (!IsMenuTranslationActive() || event->type() != QEvent::Show || !m_owner)
            return QObject::eventFilter(watched, event);

        if (auto* box = qobject_cast<QMessageBox*>(watched))
        {
            if (box->parentWidget() != m_owner)
                return QObject::eventFilter(watched, event);

            const QString text = box->text();
            const QString translated = Tr(text);
            if (translated != text)
                box->setText(translated);
        }
        else if (auto* input = qobject_cast<QInputDialog*>(watched))
        {
            if (input->parentWidget() != m_owner)
                return QObject::eventFilter(watched, event);

            input->setWindowTitle(Tr(input->windowTitle()));
            input->setLabelText(Tr(input->labelText()));
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QWidget> m_owner;
};

class MelonDsLanClientDiscoveryLocalizer final : public QObject
{
public:
    MelonDsLanClientDiscoveryLocalizer(QTreeView* tree, QWidget* parent)
        : QObject(parent)
        , m_tree(tree)
    {
        m_timer.setInterval(500);
        connect(&m_timer, &QTimer::timeout, this, &MelonDsLanClientDiscoveryLocalizer::relocalizeStatusColumn);
        m_timer.start();
        relocalizeStatusColumn();
    }

private:
    void relocalizeStatusColumn()
    {
        if (!IsMenuTranslationActive() || !m_tree)
            return;

        auto* model = qobject_cast<QStandardItemModel*>(m_tree->model());
        if (!model)
            return;

        for (int row = 0; row < model->rowCount(); ++row)
        {
            QStandardItem* item = model->item(row, 2);
            if (!item)
                continue;

            static constexpr int kLanStatusSourceRole = Qt::UserRole + 30002;
            const QVariant stored = item->data(kLanStatusSourceRole);
            const QString source = stored.isValid() ? stored.toString() : item->text();
            if (!stored.isValid())
                item->setData(source, kLanStatusSourceRole);
            item->setText(Tr(source));
        }
    }

    QPointer<QTreeView> m_tree;
    QTimer m_timer;
};

void ensureLanRuntimeHooks(QWidget* dialog)
{
    if (!dialog || dialog->property("_melonprime_lan_runtime_hook").toBool())
        return;

    dialog->setProperty("_melonprime_lan_runtime_hook", true);
    new MelonDsLanPopupLocalizer(dialog);
}

void wireMelonDsLANDialogLabels(QWidget* dialog)
{
    if (!dialog || !IsMenuTranslationActive())
        return;

    const QString dialogName = dialog->objectName();

    auto localizeLanWarning = [](QLabel* label)
    {
        if (!label)
            return;
        label->setText(QStringLiteral("<html><head/><body><p>%1</p><p>%2</p></body></html>")
            .arg(Tr(QStringLiteral("Warning: LAN requires low network latency to work.")))
            .arg(Tr(QStringLiteral("Do not expect it to work through a VPN or any sort of tunnel."))));
    };

    if (dialogName == QStringLiteral("LANStartHostDialog"))
    {
        localizeLanWarning(dialog->findChild<QLabel*>(QStringLiteral("label_3")));
        ensureLanRuntimeHooks(dialog);
        return;
    }

    if (dialogName != QStringLiteral("LANStartClientDialog"))
        return;

    localizeLanWarning(dialog->findChild<QLabel*>(QStringLiteral("label_2")));
    ensureLanRuntimeHooks(dialog);

    if (auto* box = dialog->findChild<QDialogButtonBox*>(QStringLiteral("buttonBox")))
    {
        if (auto* ok = box->button(QDialogButtonBox::Ok))
        {
            const QString source = SourceObjectPropertyText(
                ok,
                "_melonprime_src_text",
                QStringLiteral("Connect"));
            ok->setText(Tr(source));
        }

        for (QAbstractButton* btn : box->buttons())
        {
            const QString source = SourceObjectPropertyText(
                btn,
                "_melonprime_src_text",
                btn->text());
            if (source == QStringLiteral("Direct connect..."))
                btn->setText(Tr(source));
        }
    }

    if (auto* tree = dialog->findChild<QTreeView*>(QStringLiteral("tvAvailableGames")))
    {
        if (auto* model = qobject_cast<QStandardItemModel*>(tree->model()))
        {
            const QStringList sourceHeaders = SourcePropertyTextList(
                tree,
                "_melonprime_lan_client_headers",
                {
                    QStringLiteral("Name"),
                    QStringLiteral("Players"),
                    QStringLiteral("Status"),
                    QStringLiteral("Host IP"),
                });
            QStringList translated;
            translated.reserve(sourceHeaders.size());
            for (const QString& header : sourceHeaders)
                translated.append(Tr(header));
            model->setHorizontalHeaderLabels(translated);
        }

        new MelonDsLanClientDiscoveryLocalizer(tree, dialog);
    }
}

class MelonDsDialogShowLocalizer final : public QObject
{
public:
    explicit MelonDsDialogShowLocalizer(QWidget* dialog)
        : QObject(dialog)
        , m_dialog(dialog)
    {
        m_dialog->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched != m_dialog || m_done)
            return QObject::eventFilter(watched, event);

        if (event->type() == QEvent::Show)
        {
            m_done = true;
            LocalizeMelonDsDialog(m_dialog);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* m_dialog;
    bool m_done = false;
};

} // namespace

void InstallMelonDsDialogShowLocalizer(QWidget* dialog)
{
    if (!dialog || !IsMenuTranslationActive())
        return;
    new MelonDsDialogShowLocalizer(dialog);
}

void LocalizeMelonDsDialog(QWidget* dialog)
{
    if (!IsMenuTranslationActive() || !dialog)
        return;
    LocalizeWidgetTree(dialog);
    wireMelonDsLANDialogLabels(dialog);
    wireMelonDsDialogDynamicLabels(dialog);
}

void ApplyNoRomSplashLocalization(char line0[256], char line1[256])
{
    static constexpr const char kLine0En[] = "File->Open ROM...";
    static constexpr const char kLine1En[] = "to get started";

    const QString q0 = IsMenuTranslationActive() ? Tr(kLine0En) : QString::fromUtf8(kLine0En);
    const QString q1 = IsMenuTranslationActive() ? Tr(kLine1En) : QString::fromUtf8(kLine1En);

    auto copyUtf8Bounded = [](char out[256], const QString& text)
    {
        QString truncated = text;
        QByteArray bytes = truncated.toUtf8();
        while (bytes.size() > 255 && !truncated.isEmpty())
        {
            truncated.chop(1);
            bytes = truncated.toUtf8();
        }

        const int n = static_cast<int>(std::min<qsizetype>(bytes.size(), 255));
        std::memcpy(out, bytes.constData(), static_cast<size_t>(n));
        out[n] = '\0';
    };

    copyUtf8Bounded(line0, q0);
    copyUtf8Bounded(line1, q1);
}

namespace {

constexpr unsigned kNoRomSplashIdLine0 = 0x80000000u;
constexpr unsigned kNoRomSplashIdLine1 = 0x80000001u;

[[nodiscard]] unsigned int SplashOsdRainbowColor(int inc) noexcept
{
    if (inc < 100) return 0xFFFF9B9B + (static_cast<unsigned int>(inc) << 8);
    if (inc < 200) return 0xFFFFFF9B - (static_cast<unsigned int>(inc - 100) << 16);
    if (inc < 300) return 0xFF9BFF9B + static_cast<unsigned int>(inc - 200);
    if (inc < 400) return 0xFF9BFFFF - (static_cast<unsigned int>(inc - 300) << 8);
    if (inc < 500) return 0xFF9B9BFF + (static_cast<unsigned int>(inc - 400) << 16);
    return 0xFFFF9BFF - static_cast<unsigned int>(inc - 500);
}

[[nodiscard]] QFont NoRomSplashUiFont()
{
    QFont font;
    switch (ActiveMenuLanguage()) {
    case MenuLangId::Japanese:
        font.setFamilies({
            QStringLiteral("Hiragino Sans"),
            QStringLiteral("Hiragino Kaku Gothic ProN"),
            QStringLiteral("Noto Sans CJK JP"),
            QStringLiteral("Yu Gothic UI"),
            QStringLiteral("Meiryo UI"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case MenuLangId::ChineseSimplified:
        font.setFamilies({
            QStringLiteral("PingFang SC"),
            QStringLiteral("Noto Sans CJK SC"),
            QStringLiteral("Microsoft YaHei UI"),
            QStringLiteral("Source Han Sans SC"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case MenuLangId::ChineseTraditional:
        font.setFamilies({
            QStringLiteral("PingFang TC"),
            QStringLiteral("Noto Sans CJK TC"),
            QStringLiteral("Microsoft JhengHei UI"),
            QStringLiteral("Source Han Sans TC"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case MenuLangId::Korean:
        font.setFamilies({
            QStringLiteral("Apple SD Gothic Neo"),
            QStringLiteral("Noto Sans CJK KR"),
            QStringLiteral("Malgun Gothic"),
            QStringLiteral("Nanum Gothic"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case MenuLangId::Arabic:
        font.setFamilies({
            QStringLiteral("Geeza Pro"),
            QStringLiteral("Noto Sans Arabic"),
            QStringLiteral("Arial"),
            QStringLiteral("Segoe UI"),
        });
        break;
    case MenuLangId::Thai:
        font.setFamilies({
            QStringLiteral("Thonburi"),
            QStringLiteral("Noto Sans Thai"),
            QStringLiteral("Leelawadee UI"),
            QStringLiteral("Segoe UI"),
        });
        break;
    default:
        font.setFamilies({
            QStringLiteral("Segoe UI"),
            QStringLiteral("Helvetica Neue"),
            QStringLiteral("Arial"),
            QStringLiteral("Noto Sans"),
        });
        break;
    }
    font.setPixelSize(12);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

} // namespace

bool UsesLocalizedSplashLayout()
{
    return IsMenuTranslationActive();
}

bool TryRenderNoRomSplashOsdItem(unsigned int id, const char* text, unsigned int color,
    int rainbowstart, int& rainbowend, int maxWidth, QImage* outBitmap)
{
    if (!outBitmap || !text || !text[0])
        return false;
    if (!IsMenuTranslationActive())
        return false;
    if (id != kNoRomSplashIdLine0 && id != kNoRomSplashIdLine1)
        return false;

    const QString qtext = QString::fromUtf8(text);
    if (qtext.isEmpty())
        return false;

    const QFont font = NoRomSplashUiFont();
    const QFontMetrics fm(font);

    const bool rainbow = (color == 0);
    unsigned int rainbowinc = 0;
    if (rainbowstart == -1)
    {
        const unsigned int ticks = static_cast<unsigned int>(QDateTime::currentMSecsSinceEpoch());
        rainbowinc = ((static_cast<unsigned char>(text[0]) * 17u) + (ticks * 13u)) % 600u;
    }
    else
    {
        rainbowinc = static_cast<unsigned int>(rainbowstart);
    }

    const int shadowPad = 1;
    int w = fm.horizontalAdvance(qtext);
    if (maxWidth > 0)
        w = std::min(w, maxWidth);
    const int h = fm.height();
    QImage bitmap(w + shadowPad, h + shadowPad, QImage::Format_ARGB32_Premultiplied);
    bitmap.fill(Qt::transparent);

    QPainter painter(&bitmap);
    painter.setFont(font);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int baseline = fm.ascent();
    const QColor shadowColor(0, 0, 0, 224);
    const bool needsShapedText = ActiveMenuLanguage() == MenuLangId::Arabic
        || ActiveMenuLanguage() == MenuLangId::Thai;

    if (needsShapedText)
    {
        const unsigned int rgba = rainbow ? SplashOsdRainbowColor(static_cast<int>(rainbowinc))
                                        : (color | 0xFF000000u);
        QTextOption option;
        option.setWrapMode(QTextOption::NoWrap);
        if (ActiveMenuLanguage() == MenuLangId::Arabic)
        {
            option.setTextDirection(Qt::RightToLeft);
            option.setAlignment(Qt::AlignRight | Qt::AlignTop);
        }
        else
        {
            option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
        }

        const QRect textRect(0, 0, w, h);
        painter.setPen(shadowColor);
        painter.drawText(textRect.translated(shadowPad, shadowPad), qtext, option);
        painter.setPen(QColor(static_cast<QRgb>(rgba)));
        painter.drawText(textRect, qtext, option);

        if (rainbow)
        {
            for (const QChar ch : qtext)
            {
                if (ch != QLatin1Char(' '))
                    rainbowinc = (rainbowinc + 30u) % 600u;
            }
        }

        rainbowend = static_cast<int>(rainbowinc);
        *outBitmap = std::move(bitmap);
        return true;
    }

    int x = 0;
    for (const QChar ch : qtext)
    {
        const unsigned int rgba = rainbow ? SplashOsdRainbowColor(static_cast<int>(rainbowinc))
                                        : (color | 0xFF000000u);
        const QColor mainColor(static_cast<QRgb>(rgba));

        const QString glyph(ch);
        painter.setPen(shadowColor);
        painter.drawText(x + shadowPad, baseline + shadowPad, glyph);
        painter.setPen(mainColor);
        painter.drawText(x, baseline, glyph);

        x += fm.horizontalAdvance(glyph);
        if (rainbow && ch != QLatin1Char(' '))
            rainbowinc = (rainbowinc + 30u) % 600u;
    }

    rainbowend = static_cast<int>(rainbowinc);
    *outBitmap = std::move(bitmap);
    return true;
}

} // namespace MelonPrime::UiText
