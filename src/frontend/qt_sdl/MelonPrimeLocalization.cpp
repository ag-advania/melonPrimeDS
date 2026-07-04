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
    return tag.toLower().startsWith(QString::fromLatin1(prefix));
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
    if (LanguageTagMatches(tag, "ja"))
        return MenuLangId::Japanese;
    if (LanguageTagMatches(tag, "de"))
        return MenuLangId::German;
    if (LanguageTagMatches(tag, "es"))
        return MenuLangId::Spanish;
    if (LanguageTagMatches(tag, "fr"))
        return MenuLangId::French;
    return MenuLangId::English;
}

MenuLangId DetectLanguageFromEnvironment()
{
    const QLocale sys = QLocale::system();
    switch (sys.language()) {
    case QLocale::Japanese: return MenuLangId::Japanese;
    case QLocale::German: return MenuLangId::German;
    case QLocale::Spanish: return MenuLangId::Spanish;
    case QLocale::French: return MenuLangId::French;
    default: break;
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
    if (ApplePreferredLanguagesContain("ja"))
        return MenuLangId::Japanese;
    if (ApplePreferredLanguagesContain("de"))
        return MenuLangId::German;
    if (ApplePreferredLanguagesContain("es"))
        return MenuLangId::Spanish;
    if (ApplePreferredLanguagesContain("fr"))
        return MenuLangId::French;
#endif

    for (const char* envName : {"LANG", "LC_ALL", "LC_MESSAGES", "LANGUAGE"}) {
        const MenuLangId lang =
            LanguageTagToMenuLang(QString::fromLatin1(qgetenv(envName)));
        if (lang != MenuLangId::English)
            return lang;
    }

    return MenuLangId::English;
}

struct Translation
{
    const char* en;
    const char* ja;
    const char* de;
    const char* es;
    const char* fr;
};

struct ObjectTextTranslation
{
    const char* objectName;
    const char* ja;
    const char* de;
    const char* es;
    const char* fr;
};

const char* TranslationFieldForLang(const Translation& entry, MenuLangId lang)
{
    switch (lang) {
    case MenuLangId::Japanese: return entry.ja;
    case MenuLangId::German: return entry.de;
    case MenuLangId::Spanish: return entry.es;
    case MenuLangId::French: return entry.fr;
    default: return entry.en;
    }
}

const char* ObjectTranslationFieldForLang(const ObjectTextTranslation& entry, MenuLangId lang)
{
    switch (lang) {
    case MenuLangId::Japanese: return entry.ja;
    case MenuLangId::German: return entry.de;
    case MenuLangId::Spanish: return entry.es;
    case MenuLangId::French: return entry.fr;
    default: return nullptr;
    }
}

} // namespace

MenuLangId DetectSystemMenuLanguage()
{
    static const MenuLangId lang = DetectLanguageFromEnvironment();
    return lang;
}

bool IsJapaneseSystemLocale()
{
    return DetectSystemMenuLanguage() == MenuLangId::Japanese;
}

bool IsMenuTranslationActive()
{
    return MenuLanguageMode() != kMenuLanguageEnglish
        && DetectSystemMenuLanguage() != MenuLangId::English;
}

MenuLangId ActiveMenuLanguage()
{
    if (MenuLanguageMode() == kMenuLanguageEnglish)
        return MenuLangId::English;
    const MenuLangId systemLang = DetectSystemMenuLanguage();
    return systemLang == MenuLangId::English ? MenuLangId::English : systemLang;
}

QString MenuLanguageDisplayName(MenuLangId lang)
{
    switch (lang) {
    case MenuLangId::Japanese: return QStringLiteral("日本語");
    case MenuLangId::German: return QStringLiteral("Deutsch");
    case MenuLangId::Spanish: return QStringLiteral("Español");
    case MenuLangId::French: return QStringLiteral("Français");
    default: return QStringLiteral("English");
    }
}

QString MenuLanguageNativeLabel()
{
    return MenuLanguageDisplayName(DetectSystemMenuLanguage());
}

constexpr Translation kTranslations[] = {
    // Common controls
    {"ON", "ON", "ON", "ON", "ON"},
    {"OFF", "OFF", "OFF", "APAGADO", "ARRÊT"},
    {"Off", "オフ", "Aus", "Off", "Off"},
    {"Save", "保存", "Speichern", "Guardar", "Enregistrer"},
    {"Cancel", "キャンセル", "Abbrechen", "Cancelar", "Annuler"},
    {"Reset", "リセット", "Zurücksetzen", "Restablecer", "Réinitialiser"},
    {"Generate", "生成", "Generieren", "Generar", "Générer"},
    {"Copy Output", "出力をコピー", "Ausgabe kopieren", "Copiar salida", "Copier la sortie"},
    {"Copy Output to Input", "出力を入力へコピー", "Ausgabe in Eingabe kopieren", "Copiar salida a entrada", "Copier la sortie vers l'entrée"},
    {"Apply", "適用", "Anwenden", "Aplicar", "Appliquer"},
    {"Browse…", "参照…", "Durchsuchen...", "Examinar...", "Parcourir..."},
    {"Select HUD Font", "HUDフォントを選択", "HUD-Schriftart auswählen", "Seleccionar fuente HUD", "Sélectionner la police HUD"},
    {"Select HUD Font File", "HUDフォントファイルを選択", "HUD-Schriftdatei auswählen", "Seleccionar archivo de fuente HUD", "Sélectionner le fichier de police HUD"},
    {"Pick Color", "色を選択", "Farbe wählen", "Elegir color", "Choisir une couleur"},
    {"Pick a system font…", "システムフォントを選択…", "Systemschriftart wählen…", "Elegir una fuente del sistema…", "Choisir une police système…"},
    {"Crosshair Color", "照準の色", "Fadenkreuzfarbe", "Color del punto de mira", "Couleur du réticule"},
    {"Edit", "編集", "Bearbeiten", "Editar", "Modifier"},
    {"Chng", "変更", "Änd.", "Camb.", "Mod."},
    {"OVR", "全体", "Ges.", "Gral.", "Gén."},
    {"Preview", "プレビュー", "Vorschau", "Vista previa", "Aperçu"},
    {"Preview ON", "プレビューON", "Vorschau ON", "Vista previa ON", "Aperçu ON"},
    {"preview", "プレビュー", "Vorschau", "vista previa", "aperçu"},
    {"Normal", "通常", "Standard", "Estándar", "Standard"},
    {"Zoomed (Scope)", "ズーム時 (スコープ)", "Gezoomt (Visier)", "Con zoom (mira)", "Zoom (viseur)"},
    {"Scope reticle off", "スコープレティクル無効", "Visierpunkt deaktiviert", "Retícula del visor desactivada", "Réticule du viseur désactivé"},
    {"Text", "文字", "Text", "Texto", "Texte"},
    {"Auto", "自動", "Automatisch", "Automático", "Auto"},
    {"Overall", "全体", "Gesamt", "General", "Global"},
    {"Custom", "カスタム", "Benutzerdefiniert", "Personalizado", "Personnalisé"},
    {"Default (MPH)", "標準 (MPH)", "Standard (MPH)", "Predeterminado (MPH)", "Par défaut (MPH)"},
    {"System Font", "システムフォント", "Systemschriftart", "Fuente del sistema", "Police système"},
    {"Font File", "フォントファイル", "Schriftdatei", "Archivo de fuente", "Fichier de police"},
    {"Font files (*.ttf *.otf *.ttc);;All files (*)", "フォントファイル (*.ttf *.otf *.ttc);;すべてのファイル (*)", "Schriftdateien (*.ttf *.otf *.ttc);;Alle Dateien (*)", "Archivos de fuente (*.ttf *.otf *.ttc);;Todos los archivos (*)", "Fichiers de polices (*.ttf *.otf *.ttc);;Tous les fichiers (*)"},
    {"Menu Language", "メニュー言語", "Menüsprache", "Idioma del menú", "Langue du menu"},
    {"Japanese", "日本語", "Japanisch", "Japonés", "Japonais"},
    {"English", "英語", "Englisch", "Inglés", "Anglais"},
    {"German", "ドイツ語", "Deutsch", "Alemán", "Allemand"},
    {"Spanish", "スペイン語", "Spanisch", "Español", "Espagnol"},
    {"French", "フランス語", "Französisch", "Francés", "Français"},

    // Main menu bar
    {"File", "ファイル", "Datei", "Archivo", "Fichier"},
    {"Open ROM...", "ROMを開く...", "ROM öffnen...", "Abrir ROM...", "Ouvrir une ROM..."},
    {"File->Open ROM...", "ファイル → ROMを開く...", "Datei → ROM öffnen...", "Archivo → Abrir ROM...", "Fichier → Ouvrir une ROM..."},
    {"to get started", "で始めよう", "um loszulegen", "para empezar", "pour commencer"},
    {"Open recent", "最近使ったROM", "Zuletzt geöffnet", "Abrir reciente", "Ouvrir récent"},
    {"Boot firmware", "ファームウェアを起動", "Firmware starten", "Iniciar firmware", "Démarrer le firmware"},
    {"DS slot", "DSスロット", "DS-Steckplatz", "Ranura DS", "Emplacement DS"},
    {"GBA slot", "GBAスロット", "GBA-Steckplatz", "Ranura GBA", "Emplacement GBA"},
    {"Insert cart...", "カートリッジを挿入...", "Modul einlegen...", "Insertar cartucho...", "Insérer une cartouche..."},
    {"Eject cart", "カートリッジを取り出す", "Modul auswerfen", "Expulsar cartucho", "Éjecter la cartouche"},
    {"Insert ROM cart...", "ROMカートリッジを挿入...", "ROM-Karte einlegen…", "Insertar cartucho ROM…", "Insérer cartouche ROM…"},
    {"Insert add-on cart", "アドオンカートリッジを挿入", "Add-on-Karte einlegen", "Insertar cartucho add-on", "Insérer la cartouche add-on"},
    {"Import savefile", "セーブファイルをインポート", "Speicherdatei importieren", "Importar partida guardada", "Importer la sauvegarde"},
    {"Save state", "ステートを保存", "Zustand speichern", "Guardar estado", "Sauvegarder l'état"},
    {"Load state", "ステートを読み込み", "Stand laden", "Cargar estado", "Charger l'état"},
    {"File...", "ファイル...", "Datei…", "Archivo…", "Fichier…"},
    {"Undo state load", "ステート読み込みを取り消す", "Zustandsladen rückgängig", "Deshacer carga de estado", "Annuler le chargement d'état"},
    {"Open melonDS directory", "melonDSフォルダを開く", "melonDS-Ordner öffnen", "Abrir carpeta de melonDS", "Ouvrir le dossier melonDS"},
    {"Quit", "終了", "Beenden", "Salir", "Quitter"},
    {"System", "システム", "System", "Sistema", "Système"},
    {"Pause", "一時停止", "Pause", "Pausa", "Pause"},
    {"Stop", "停止", "Stopp", "Detener", "Arrêter"},
    {"Frame step", "フレーム送り", "Frame-Schritt", "Avance de fotograma", "Avance image par image"},
    {"Power management", "電源管理", "Energieverwaltung", "Gestión de energía", "Gestion de l'alimentation"},
    {"Date and time", "日付と時刻", "Datum und Uhrzeit", "Fecha y hora", "Date et heure"},
    {"Enable cheats", "チートを有効化", "Cheats aktivieren", "Activar trucos", "Activer les triches"},
    {"Setup cheat codes", "チートコード設定", "Cheat-Codes einrichten", "Configurar códigos de trucos", "Configurer les codes triche"},
    {"ROM info", "ROM情報", "ROM-Info", "Info de ROM", "Infos ROM"},
    {"RAM search", "RAM検索", "RAM-Suche", "Búsqueda RAM", "Recherche RAM"},
    {"Manage DSi titles", "DSiタイトル管理", "DSi-Titel verwalten", "Administrar títulos DSi", "Gérer les titres DSi"},
    {"Multiplayer", "マルチプレイ", "Mehrspieler", "Multijugador", "Multijoueur"},
    {"Launch new instance", "新しいインスタンスを起動", "Neue Instanz starten", "Iniciar nueva instancia", "Lancer une nouvelle instance"},
    {"Host LAN game", "LANゲームをホスト", "LAN-Spiel hosten", "Alojar partida LAN", "Héberger une partie LAN"},
    {"Join LAN game", "LANゲームに参加", "LAN-Spiel beitreten", "Unirse a partida LAN", "Rejoindre une partie LAN"},
    {"View", "表示", "Ansicht", "Ver", "Affichage"},
    {"Screen size", "画面サイズ", "Bildschirmgröße", "Tamaño de pantalla", "Taille de l'écran"},
    {"Screen rotation", "画面回転", "Bildschirmdrehung", "Rotación de pantalla", "Rotation de l'écran"},
    {"Screen gap", "画面間隔", "Bildschirmabstand", "Separación de pantallas", "Espacement des écrans"},
    {"Screen layout", "画面レイアウト", "Bildschirmlayout", "Diseño de pantalla", "Disposition de l'écran"},
    {"Natural", "自然", "Natürlich", "Natural", "Naturel"},
    {"Vertical", "縦", "Vertikal", "Vertical", "Vertical"},
    {"Horizontal", "横", "Horizontal", "Horizontal", "Horizontal"},
    {"Hybrid", "ハイブリッド", "Hybrid", "Híbrido", "Hybride"},
    {"Swap screens", "上下画面を入れ替え", "Bildschirme tauschen", "Intercambiar pantallas", "Inverser les écrans"},
    {"Screen sizing", "画面の拡大方式", "Bildschirmgröße", "Tamaño de pantalla", "Dimensionnement d'écran"},
    {"Even", "均等", "Gleichmäßig", "Uniforme", "Uniforme"},
    {"Emphasize top", "上画面を重視", "Oberen Bildschirm betonen", "Enfatizar pantalla superior", "Mettre en avant l'écran du haut"},
    {"Emphasize bottom", "下画面を重視", "Unteren Bildschirm betonen", "Enfatizar pantalla inferior", "Mettre en avant l'écran du bas"},
    {"Top only", "上画面のみ", "Nur oberer Bildschirm", "Solo pantalla superior", "Écran supérieur uniquement"},
    {"Bottom only", "下画面のみ", "Nur unten", "Solo inferior", "Bas seulement"},
    {"Force integer scaling", "整数スケーリングを強制", "Ganzzahl-Skalierung erzwingen", "Forzar escala entera", "Forcer mise à l'échelle entière"},
    {"Aspect ratio", "アスペクト比", "Seitenverhältnis", "Relación de aspecto", "Format d'image"},
    {"Open new window", "新しいウィンドウを開く", "Neues Fenster öffnen", "Abrir ventana nueva", "Ouvrir une nouvelle fenêtre"},
    {"Screen filtering", "画面フィルタリング", "Bildschirmfilterung", "Filtrado de pantalla", "Filtrage de l'écran"},
    {"Show OSD", "OSDを表示", "OSD anzeigen", "Mostrar OSD", "Afficher l'OSD"},
    {"Config", "設定", "Konfiguration", "Configuración", "Configuration"},
    {"Emu settings", "エミュレーター設定", "Emulator-Einstellungen", "Ajustes del emulador", "Paramètres de l'émulateur"},
    {"Preferences...", "環境設定...", "Einstellungen...", "Preferencias...", "Préférences..."},
    {"Input and hotkeys", "入力とホットキー", "Eingabe und Hotkeys", "Entrada y atajos", "Entrées et raccourcis"},
    {"Video settings", "映像設定", "Videoeinstellungen", "Ajustes de vídeo", "Paramètres vidéo"},
    {"Camera settings", "カメラ設定", "Kameraeinstellungen", "Ajustes de cámara", "Paramètres caméra"},
    {"Audio settings", "音声設定", "Audio-Einstellungen", "Ajustes de audio", "Paramètres audio"},
    {"Multiplayer settings", "マルチプレイ設定", "Mehrspieler-Einstellungen", "Ajustes multijugador", "Paramètres multijoueur"},
    {"Wifi settings", "Wi-Fi設定", "WLAN-Einstellungen", "Ajustes Wi-Fi", "Paramètres Wi-Fi"},
    {"Firmware settings", "ファームウェア設定", "Firmware-Einstellungen", "Ajustes de firmware", "Paramètres firmware"},
    {"Interface settings", "インターフェース設定", "Oberflächeneinstellungen", "Ajustes de interfaz", "Paramètres interface"},
    {"Path settings", "パス設定", "Pfad-Einstellungen", "Ajustes de rutas", "Paramètres des chemins"},
    {"Limit framerate", "フレームレート制限", "Bildrate begrenzen", "Limitar FPS", "Limiter le taux d'images"},
    {"Audio sync", "音声同期", "Audiosynchronisation", "Sincronización de audio", "Synchronisation audio"},
    {"MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime", "MelonPrime"},
    {"Input settings", "操作設定", "Eingabeeinstellungen", "Ajustes de entrada", "Paramètres entrée"},
    {"Other settings", "MelonPrime設定", "MelonPrime-Einstellungen", "Ajustes de MelonPrime", "Paramètres MelonPrime"},
    {"MelonPrime settings", "MelonPrime設定", "MelonPrime-Einstellungen", "Ajustes de MelonPrime", "Paramètres MelonPrime"},
    {"Custom HUD settings", "カスタムHUD設定", "Benutzerdefinierte HUD-Einstellungen", "Ajustes del HUD personalizado", "Paramètres HUD personnalisés"},
    {"Disable SF (Shadow Freeze)", "SF (シャドウフリーズ) を無効化", "SF (Shadow Freeze) deaktivieren", "Desactivar SF (Shadow Freeze)", "Désactiver SF (Shadow Freeze)"},
    {"In-Game Top Screen Only", "ゲーム中は上画面のみ", "Nur oberer Bildschirm im Spiel", "Solo pantalla superior en juego", "Écran supérieur uniquement en jeu"},
    {"Help", "ヘルプ", "Hilfe", "Ayuda", "Aide"},
    {"About...", "このアプリについて...", "Über...", "Acerca de...", "À propos..."},
    {"Clear", "履歴を消去", "Verlauf löschen", "Borrar historial", "Effacer l'historique"},

    // Tabs and major sections
    {"MelonPrime Settings", "MelonPrime設定", "MelonPrime-Einstellungen", "Ajustes de MelonPrime", "Paramètres MelonPrime"},
    {"Controls", "操作", "Steuerung", "Controles", "Commandes"},
    {"Controls 2", "操作 2", "Steuerung 2", "Controles 2", "Commandes 2"},
    {"Custom HUD", "カスタムHUD", "Benutzerdefiniertes HUD", "HUD personalizado", "HUD personnalisé"},
    {"Custom HUD Input/Output", "カスタムHUD 入出力", "Benutzerdefiniertes HUD Ein-/Ausgabe", "HUD personalizado Entrada/Salida", "HUD personnalisé Entrée/Sortie"},
    {"Input", "入力", "Eingabe", "Entrada", "Entrées"},
    {"Output", "出力", "Ausgabe", "Salida", "Sortie"},
    {"INPUT SETTINGS", "入力設定", "EINGABE-EINSTELLUNGEN", "AJUSTES DE ENTRADA", "PARAMÈTRES D'ENTRÉE"},
    {"INPUT METHOD", "入力方式", "EINGABEMETHODE", "MÉTODO DE ENTRADA", "MÉTHODE DE SAISIE"},
    {"SCREEN SYNC", "画面同期", "BILDSYNCHRONISATION", "SINCRONIZACIÓN DE PANTALLA", "SYNCHRONISATION ÉCRAN"},
    {"CURSOR CLIP SETTINGS", "カーソル制限", "CURSOREINGRENZUNG", "LÍMITES DEL CURSOR", "LIMITES DU CURSEUR"},
    {"IN-GAME APPLY", "ゲーム内反映", "IM SPIEL ANWENDEN", "APLICAR EN JUEGO", "APPLIQUER EN JEU"},
    {"IN-GAME ASPECT RATIO", "ゲーム内アスペクト比", "SEITENVERHÄLTNIS IM SPIEL", "RELACIÓN DE ASPECTO EN EL JUEGO", "FORMAT D'IMAGE EN JEU"},
    {"LOW HP WARNING", "低HP警告", "NIEDRIG-HP-WARNUNG", "AVISO DE HP BAJO", "ALERTE HP FAIBLE"},
    {"SENSITIVITY", "感度", "EMPFINDLICHKEIT", "SENSIBILIDAD", "SENSIBILITÉ"},
    {"BUG FIXES", "不具合修正", "FEHLERBEHEBUNGEN", "CORRECCIONES DE ERRORES", "CORRECTIONS DE BUGS"},
    {"GAME FEATURE IMPROVEMENTS", "ゲーム機能改善", "SPIELFUNKTIONS-VERBESSERUNGEN", "MEJORAS DE FUNCIONES DEL JUEGO", "AMÉLIORATIONS DES FONCTIONS DE JEU"},
    {"DISABLE FEATURES", "機能無効化", "FUNKTIONEN DEAKTIVIEREN", "DESACTIVAR FUNCIONES", "DÉSACTIVER DES FONCTIONS"},
    {"Power-Up Pickup Effects", "パワーアップ取得効果", "Effekte beim Aufsammeln von Power-Ups", "Efectos al recoger potenciadores", "Effets de ramassage des bonus"},
    {"GAMEPLAY TOGGLES", "ゲームプレイ切替", "GAMEPLAY-OPTIONEN", "OPCIONES DE JUEGO", "OPTIONS DE GAMEPLAY"},
    {"VIDEO QUALITY", "映像品質", "VIDEOQUALITÄT", "CALIDAD DE VÍDEO", "QUALITÉ VIDÉO"},
    {"VOLUME", "音量", "LAUTSTÄRKE", "VOLUMEN", "VOLUME"},
    {"LICENSE APPLY", "ライセンス反映", "LIZENZ ANWENDEN", "APLICAR LICENCIA", "APPLIQUER LA LICENCE"},
    {"DEVELOPER ONLY", "開発者向け", "NUR FÜR ENTWICKLER", "SOLO DESARROLLADORES", "RÉSERVÉ AUX DÉVELOPPEURS"},
    {"DISABLE DEFAULT HUD", "標準HUDを非表示", "STANDARD-HUD DEAKTIVIEREN", "DESACTIVAR HUD PREDETERMINADO", "DÉSACTIVER LE HUD PAR DÉFAUT"},
    {"OUTLINE OVERRIDE", "アウトライン一括設定", "KONTUR-ÜBERSCHREIBUNG", "ANULACIÓN DE CONTORNO", "REMPLACEMENT DE CONTOUR"},
    {"HUD SCALE", "HUDスケール", "HUD-SKALIERUNG", "ESCALA HUD", "ÉCHELLE HUD"},
    {"HUD FONT", "HUDフォント", "HUD-SCHRIFT", "FUENTE HUD", "POLICE HUD"},
    {"CROSSHAIR", "照準", "VISIER", "PUNTO DE MIRA", "RÉTICULE"},
    {"HP / AMMO", "HP / 弾薬", "HP / MUNITION", "HP / MUNICIÓN", "PV / MUNITIONS"},
    {"MATCH STATUS HUD", "試合情報HUD", "SPIELSTAND-HUD", "HUD DE ESTADO DE PARTIDA", "HUD D'ÉTAT DE MATCH"},
    {"HUD RADAR", "HUDレーダー", "HUD-RADAR", "RADAR HUD", "RADAR HUD"},
    {"IN-GAME OSD COLOR", "ゲーム内OSD色", "OSD-FARBE IM SPIEL", "COLOR OSD EN EL JUEGO", "COULEUR OSD EN JEU"},

    // Hotkey page
    {"Keyboard mappings", "キーボード割り当て", "Tastaturbelegung", "Asignación de teclado", "Assignation clavier"},
    {"Keyboard && mouse mappings", "キーボード・マウス割り当て", "Tastatur- und Mausbelegung", "Asignación de teclado y ratón", "Mappage clavier et souris"},
    {"Joystick mappings", "ジョイスティック割り当て", "Joystick-Zuweisungen", "Asignaciones de joystick", "Assignations manette"},
    {"[Metroid] (W) Move Forward", "[Metroid] (W) 前進", "[Metroid] (W) Vorwärts", "[Metroid] (W) Avanzar", "[Metroid] (W) Avancer"},
    {"[Metroid] (S) Move Back", "[Metroid] (S) 後退", "[Metroid] (S) Zurück", "[Metroid] (S) Retroceder", "[Metroid] (S) Reculer"},
    {"[Metroid] (A) Move Left", "[Metroid] (A) 左移動", "[Metroid] (A) Nach links", "[Metroid] (A) Mover a la izquierda", "[Metroid] (A) Aller à gauche"},
    {"[Metroid] (D) Move Right", "[Metroid] (D) 右移動", "[Metroid] (D) Nach rechts", "[Metroid] (D) Mover a la derecha", "[Metroid] (D) Déplacer à droite"},
    {"[Metroid] (Mouse Left) Shoot/Scan", "[Metroid] (マウス左) 射撃/スキャン", "[Metroid] (Linke Maustaste) Schießen/Scannen", "[Metroid] (Clic izquierdo) Disparar/Escanear", "[Metroid] (Clic gauche) Tirer/Scanner"},
    {"[Metroid] (V) Scan/Shoot, Map Zoom In", "[Metroid] (V) スキャン/射撃、マップ拡大", "[Metroid] (V) Scannen/Schießen, Karte vergrößern", "[Metroid] (V) Escanear/Disparar, zoom del mapa", "[Metroid] (V) Scanner/Tirer, zoom carte"},
    {"[Metroid] (Mouse Right) Imperialist Zoom, Map Zoom Out, Morph Ball Boost", "[Metroid] (マウス右) インペリアリストズーム、マップ縮小、モーフボールブースト", "[Metroid] (Rechte Maustaste) Imperialist-Zoom, Karte verkleinern, Morph Ball Boost", "[Metroid] (Clic derecho) Zoom Imperialist, alejar mapa, impulso Morph Ball", "[Metroid] (Clic droit) Zoom Imperialist, dézoom carte, boost Morph Ball"},
    {"[Metroid] (Space) Jump", "[Metroid] (Space) ジャンプ", "[Metroid] (Leertaste) Springen", "[Metroid] (Espacio) Saltar", "[Metroid] (Espace) Sauter"},
    {"[Metroid] (L. Ctrl) Transform", "[Metroid] (左Ctrl) 変形", "[Metroid] (L. Strg) Transformation", "[Metroid] (Ctrl izq.) Transformarse", "[Metroid] (Ctrl G) Transformer"},
    {"[Metroid] (Shift) Hold to Fast Morph Ball Boost", "[Metroid] (Shift) 長押しで高速モーフボールブースト", "[Metroid] (Shift) Gedrückt halten für schnellen Morph Ball Boost", "[Metroid] (Mayús) Mantener para impulso Morph Ball rápido", "[Metroid] (Maj) Maintenir pour boost Morph Ball rapide"},
    {"[Metroid] (Mouse 5, Side Top) Weapon Beam", "[Metroid] (Mouse 5/サイド上) ビーム武器", "[Metroid] (Maus 5, Seitentaste oben) Strahlenwaffe", "[Metroid] (Ratón 5, lateral superior) Rayo", "[Metroid] (Souris 5, côté haut) Rayon"},
    {"[Metroid] (Mouse 4, Side Bottom) Weapon Missile", "[Metroid] (Mouse 4/サイド下) ミサイル", "[Metroid] (Maustaste 4, Seitentaste unten) Waffe Rakete", "[Metroid] (Ratón 4, lateral inferior) Arma misil", "[Metroid] (Souris 4, latéral bas) Arme missile"},
    {"[Metroid] (1) Weapon 1. ShockCoil", "[Metroid] (1) 武器1: ショックコイル", "[Metroid] (1) Waffe 1: ShockCoil", "[Metroid] (1) Arma 1: ShockCoil", "[Metroid] (1) Arme 1 : ShockCoil"},
    {"[Metroid] (2) Weapon 2. Magmaul", "[Metroid] (2) 武器2: マグモール", "[Metroid] (2) Waffe 2: Magmaul", "[Metroid] (2) Arma 2: Magmaul", "[Metroid] (2) Arme 2: Magmaul"},
    {"[Metroid] (3) Weapon 3. Judicator", "[Metroid] (3) 武器3: ジュディケイター", "[Metroid] (3) Waffe 3: Judicator", "[Metroid] (3) Arma 3: Judicator", "[Metroid] (3) Arme 3 : Judicator"},
    {"[Metroid] (4) Weapon 4. Imperialist", "[Metroid] (4) 武器4: インペリアリスト", "[Metroid] (4) Waffe 4: Imperialist", "[Metroid] (4) Arma 4: Imperialist", "[Metroid] (4) Arme 4 : Imperialist"},
    {"[Metroid] (5) Weapon 5. Battlehammer", "[Metroid] (5) 武器5: バトルハンマー", "[Metroid] (5) Waffe 5: Battlehammer", "[Metroid] (5) Arma 5: Battlehammer", "[Metroid] (5) Arme 5 : Battlehammer"},
    {"[Metroid] (6) Weapon 6. VoltDriver", "[Metroid] (6) 武器6: ボルトドライバー", "[Metroid] (6) Waffe 6: VoltDriver", "[Metroid] (6) Arma 6: VoltDriver", "[Metroid] (6) Arme 6 : VoltDriver"},
    {"[Metroid] (R) Affinity Weapon (Last used Weapon/Omega cannon)", "[Metroid] (R) 得意武器 (最後の武器/オメガキャノン)", "[Metroid] (R) Lieblingswaffe (Letzte Waffe/Omega-Kanone)", "[Metroid] (R) Arma preferida (Última arma/Cañón Omega)", "[Metroid] (R) Arme favorite (Dernière arme/Canon Omega)"},
    {"[Metroid] (Tab) Menu/Map", "[Metroid] (Tab) メニュー/マップ", "[Metroid] (Tab) Menü/Karte", "[Metroid] (Tab) Menú/Mapa", "[Metroid] (Tab) Menu/Carte"},
    {"[Metroid] (PgUp) AimSensitivity Up", "[Metroid] (PgUp) エイム感度を上げる", "[Metroid] (Bild auf) Ziel-Empfindlichkeit erhöhen", "[Metroid] (RePág) Subir sensibilidad de puntería", "[Metroid] (PgPréc) Augmenter sensibilité visée"},
    {"[Metroid] (PgDown) AimSensitivity Down", "[Metroid] (PgDown) エイム感度を下げる", "[Metroid] (PgDown) Ziel-Empfindlichkeit verringern", "[Metroid] (RePág) Reducir sensibilidad de puntería", "[Metroid] (PgBas) Diminuer la sensibilité de visée"},
    {"[Metroid] (J) Next Weapon in the sorted order", "[Metroid] (J) 次の武器", "[Metroid] (J) Nächste Waffe", "[Metroid] (J) Siguiente arma", "[Metroid] (J) Arme suivante"},
    {"[Metroid] (K) Previous Weapon in the sorted order", "[Metroid] (K) 前の武器", "[Metroid] (K) Vorherige Waffe (Sortierreihenfolge)", "[Metroid] (K) Arma anterior (orden)", "[Metroid] (K) Arme précédente (ordre trié)"},
    {"[Metroid] (C) Scan Visor", "[Metroid] (C) スキャンバイザー", "[Metroid] (C) Scan-Visor", "[Metroid] (C) Visor de escaneo", "[Metroid] (C) Visor de scan"},
    {"[Metroid] (Z) UI Left (Adventure Left Arrow / Hunter License L)", "[Metroid] (Z) UI左 (アドベンチャー左/ライセンスL)", "[Metroid] (Z) UI links (Abenteuer-Pfeil links / Hunter-Lizenz L)", "[Metroid] (Z) UI izquierda (flecha izquierda Aventura / Licencia Cazador L)", "[Metroid] (Z) UI gauche (flèche gauche Aventure / Licence Chasseur L)"},
    {"[Metroid] (X) UI Right (Adventure Right Arrow / Hunter License R)", "[Metroid] (X) UI右 (アドベンチャー右/ライセンスR)", "[Metroid] (X) UI rechts (Abenteuer-Pfeil rechts / Hunter-Lizenz R)", "[Metroid] (X) UI derecha (Flecha derecha aventura / Licencia Hunter R)", "[Metroid] (X) UI droite (Flèche droite aventure / Licence Hunter R)"},
    {"[Metroid] (F) UI Ok", "[Metroid] (F) UI決定", "[Metroid] (F) UI Bestätigen", "[Metroid] (F) Confirmar UI", "[Metroid] (F) Valider UI"},
    {"[Metroid] (G) UI Yes (Enter Starship)", "[Metroid] (G) UIはい (スターシップに入る)", "[Metroid] (G) UI Ja (Raumschiff betreten)", "[Metroid] (G) UI Sí (Entrar en nave)", "[Metroid] (G) UI Oui (Entrer dans le vaisseau)"},
    {"[Metroid] (H) UI No (Enter Starship)", "[Metroid] (H) UIいいえ (スターシップに入る)", "[Metroid] (H) UI Nein (Raumschiff betreten)", "[Metroid] (H) UI No (Entrar a la nave)", "[Metroid] (H) UI Non (Entrer dans le vaisseau)"},
    {"[Metroid] (Y) Weapon Check", "[Metroid] (Y) 武器確認", "[Metroid] (Y) Waffe prüfen", "[Metroid] (Y) Verificar arma", "[Metroid] (Y) Vérifier arme"},

    // General Metroid settings
    {"MPH Sensitivity (default: -3)", "MPH感度 (既定: -3)", "MPH-Empfindlichkeit (Standard: -3)", "Sensibilidad MPH (predeterminado: -3)", "Sensibilité MPH (par défaut : -3)"},
    {"Aim sensitivity (default: 63)", "エイム感度 (既定: 63)", "Ziel-Empfindlichkeit (Standard: 63)", "Sensibilidad de puntería (predeterminado: 63)", "Sensibilité de visée (par défaut : 63)"},
    {"Aim Y-Axis Scale (default: 1.5147)", "エイムY軸スケール (既定: 1.5147)", "Ziel-Y-Achsen-Skalierung (Standard: 1.5147)", "Escala del eje Y de puntería (predeterminado: 1.5147)", "Échelle de l'axe Y de visée (par défaut : 1.5147)"},
    {"Mode", "モード", "Modus", "Modo", "Mode"},
    {"Low-Latency Aim Mode", "低遅延エイム方式", "Zielmodus mit niedriger Latenz", "Modo de puntería de baja latencia", "Mode visée faible latence"},
    {"Instant Aim Follow", "即時エイム追従", "Sofortige Zielverfolgung", "Seguimiento de puntería instantáneo", "Suivi de visée instantané"},
    {"Instant Aim Follow (Developer Only)", "即時エイム追従（開発者専用）", "Sofortige Zielverfolgung (nur Entwickler)", "Seguimiento de puntería instantáneo (solo desarrolladores)", "Suivi de visée instantané (développeurs uniquement)"},
    {"Immediate Sync", "即時同期", "Sofortige Synchronisation", "Sincronización inmediata", "Synchronisation immédiate"},
    {"MoonLike Aim", "MoonLikeエイム", "MoonLike-Ziel", "Puntería MoonLike", "Visée MoonLike"},
    {"Enable SnapTap (Faster directional switching for smooth strafing — may slightly increase input delay)", "SnapTapを有効化 (ストレイフの方向切替を高速化。入力遅延が少し増える場合あり)", "SnapTap aktivieren (schnellerer Richtungswechsel beim Strafen — kann die Eingabeverzögerung leicht erhöhen)", "Activar SnapTap (cambio de dirección más rápido al strafe — puede aumentar ligeramente el retardo de entrada)", "Activer SnapTap (changement de direction plus rapide en strafe — peut légèrement augmenter le délai d'entrée)"},
    {"Unlock All Hunters/Maps/SoundTest/Gallery (Change a setting in MPH and save to update the save data. Uncheck this option after saving)", "全ハンター/マップ/サウンドテスト/ギャラリーを解放 (MPH側で設定を変更して保存するとセーブに反映。保存後はオフ推奨)", "Alle Hunter/Karten/Soundtest/Galerie freischalten (Einstellung in MPH ändern und speichern, um Speicherdaten zu aktualisieren. Option nach dem Speichern deaktivieren)", "Desbloquear todos los cazadores/mapas/prueba de sonido/galería (Cambia un ajuste en MPH y guarda para actualizar los datos. Desmarca esta opción tras guardar)", "Débloquer tous les chasseurs/cartes/test audio/galerie (Modifiez un paramètre dans MPH et sauvegardez pour mettre à jour les données. Décochez cette option après la sauvegarde)"},
    {"Set MPH audio settings to headphones.(recommended) (Change a setting in MPH and save to update the save data. Uncheck this option after saving)", "MPHの音声設定をヘッドホンにする (推奨。MPH側で設定を変更して保存するとセーブに反映。保存後はオフ推奨)", "MPH-Audioeinstellung auf Kopfhörer setzen (empfohlen). (Einstellung in MPH ändern und speichern, um die Speicherdaten zu aktualisieren. Nach dem Speichern deaktivieren)", "Configurar el audio de MPH en auriculares (recomendado). (Cambia un ajuste en MPH y guarda para actualizar la partida. Desmarca esta opción después de guardar)", "Régler l'audio MPH sur casque (recommandé). (Modifiez un réglage dans MPH et sauvegardez pour mettre à jour la sauvegarde. Décochez cette option après la sauvegarde)"},
    {"Use DS Name (Resets in-game name and HL color):You can change the DS name from [File -> Boot firmware] or [Config -> Firmware Settings].", "DS本体名を使う (ゲーム内名とHL色をリセット): DS名は [File -> Boot firmware] または [Config -> Firmware Settings] で変更できます。", "DS-Namen verwenden (setzt Spielname und HL-Farbe zurück): Den DS-Namen kannst du unter [Datei -> Firmware starten] oder [Konfiguration -> Firmware-Einstellungen] ändern.", "Usar nombre del DS (restablece el nombre en el juego y el color HL): Puedes cambiar el nombre del DS en [Archivo -> Iniciar firmware] o [Configuración -> Ajustes de firmware].", "Utiliser le nom du DS (réinitialise le nom en jeu et la couleur HL) : vous pouvez modifier le nom du DS via [Fichier -> Démarrer le firmware] ou [Config -> Paramètres firmware]."},
    {"Enable Joy2KeySupport (enable this if keys sometimes get stuck; slightly increases input delay)", "Joy2Keyサポートを有効化 (キーが押しっぱなしになる場合に使用。入力遅延が少し増えます)", "Joy2KeySupport aktivieren (aktivieren, wenn Tasten manchmal hängen bleiben; leicht erhöhte Eingabeverzögerung)", "Activar Joy2KeySupport (actívalo si las teclas a veces se quedan pulsadas; aumenta ligeramente el retardo de entrada)", "Activer Joy2KeySupport (à activer si les touches restent parfois bloquées ; augmente légèrement le délai d'entrée)"},
    {"Enable Stylus Mode (Leave this unchecked unless you want to play with the stylus)", "スタイラスモードを有効化 (スタイラス操作で遊ぶ場合以外はオフ推奨)", "Stylus-Modus aktivieren (Nur aktivieren, wenn du mit dem Stylus spielen möchtest)", "Activar modo Stylus (Déjalo desmarcado salvo que quieras jugar con el stylus)", "Activer le mode Stylet (Laissez décoché sauf si vous voulez jouer au stylet)"},
    {"Disable MPH Aim Smoothing (Disables the in-game aim smoothing. Note: Sensitivity will be reduced to 25% in Stylus Mode)", "MPHのエイム補間を無効化 (ゲーム内のエイム補間を無効化。スタイラスモードでは感度が25%になります)", "MPH-Zielglättung deaktivieren (Deaktiviert die Zielglättung im Spiel. Hinweis: Empfindlichkeit wird im Stylus-Modus auf 25 % reduziert)", "Desactivar suavizado de puntería MPH (Desactiva el suavizado de puntería en el juego. Nota: la sensibilidad se reduce al 25 % en modo stylus)", "Désactiver le lissage de visée MPH (Désactive le lissage de visée en jeu. Remarque : la sensibilité passe à 25 % en mode stylet)"},
    {"Enable Aim Sub-pixel Accumulator (Carry fractional mouse movement across frames. Enable for smoother low-sensitivity aiming)", "エイムのサブピクセル蓄積を有効化 (小数のマウス移動を次フレームへ持ち越し。低感度エイムが滑らかになります)", "Subpixel-Zielakkumulator aktivieren (Überträgt Bruchteile der Mausbewegung auf den nächsten Frame. Für flüssigeres Zielen bei niedriger Empfindlichkeit)", "Activar acumulador subpíxel de puntería (Conserva el movimiento fraccional del ratón entre fotogramas. Actívalo para una puntería más suave con baja sensibilidad)", "Activer l'accumulateur sub-pixel de visée (Reporte les mouvements fractionnels de la souris d'une image à l'autre. Pour une visée plus fluide à faible sensibilité)"},
    {"Scale aim sensitivity while zoomed", "ズーム中のエイム感度を倍率変更", "Zielempfindlichkeit beim Zoomen skalieren", "Escalar sensibilidad de puntería al hacer zoom", "Mettre à l'échelle la sensibilité de visée en zoom"},
    {"Zoom Aim Scale %", "ズーム時エイム倍率 %", "Zoom-Ziel-Skalierung %", "Escala de puntería con zoom %", "Échelle de visée au zoom %"},
    {"Enable Direct Alt-Form Transform", "直接トランスフォーム変形を有効化", "Direkte Alt-Form-Transformation aktivieren", "Activar transformación Alt-Form directa", "Activer transformation Alt-Form directe"},
    {"Enable Immediate Input Edge Overlay", "即時入力エッジ合成を有効化", "Sofortige Eingabe-Kantenüberlagerung aktivieren", "Activar superposición de bordes de entrada inmediata", "Activer la superposition immédiate des bords d'entrée"},
    {"Enable Native Aim Delta Hook (PostFold Write)", "ネイティブエイムデルタHookを有効化 (PostFold書き込み)", "Nativen Aim-Delta-Hook aktivieren (PostFold-Schreibzugriff)", "Activar hook nativo de delta de puntería (escritura PostFold)", "Activer le hook natif de delta de visée (écriture PostFold)"},
    {"Enable Native Aim Register Injection", "ネイティブエイムのレジスタ注入を有効化", "Native Zielregister-Injektion aktivieren", "Activar inyección nativa de registro de puntería", "Activer l'injection native du registre de visée"},
    {"Apply Input to Custom HUD", "入力をカスタムHUDに反映", "Eingabe auf Custom HUD anwenden", "Aplicar entrada al HUD personalizado", "Appliquer les entrées au HUD personnalisé"},
    {"Screen Sync Mode — Default: Off", "画面同期方式 - 既定: オフ", "Bildschirm-Sync-Modus — Standard: Aus", "Modo de sincronización de pantalla — Predeterminado: Desactivado", "Mode de synchronisation d'écran — Par défaut : Désactivé"},
    {"Screen Sync Mode", "画面同期方式", "Bildschirm-Synchronisationsmodus", "Modo de sincronización de pantalla", "Mode de synchronisation d'écran"},
    {"Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete", "画面同期方式: オフ = 同期呼び出しなし、glFinish = GLコマンド完了まで待機", "Bildschirm-Sync-Modus: Aus = kein Sync-Aufruf, glFinish = warten bis GL-Befehle abgeschlossen sind", "Modo de sincronización de pantalla: Desactivado = sin llamada de sync, glFinish = esperar a que terminen los comandos GL", "Mode de synchronisation d'écran : Désactivé = pas d'appel sync, glFinish = attendre la fin des commandes GL"},
    {"When not in-game and the bottom screen is visible, confine the mouse cursor to the bottom screen area. Press ESC to release the cursor.", "ゲーム外で下画面が表示されているとき、マウスカーソルを下画面内に制限します。ESCで解除します。", "Außerhalb des Spiels, wenn der untere Bildschirm sichtbar ist, Mauszeiger auf den unteren Bildschirmbereich beschränken. ESC drücken, um den Zeiger freizugeben.", "Fuera del juego, con la pantalla inferior visible, confina el cursor al área inferior. Pulsa ESC para liberarlo.", "Hors jeu, lorsque l'écran inférieur est visible, confiner le curseur à cette zone. Appuyez sur Échap pour le libérer."},
    {"In-game only, temporarily force Screen Sizing to Top Only and Screen Layout to Natural. Outside of gameplay, restore the normal window settings.", "ゲーム中だけ一時的に Screen Sizing を Top Only、Screen Layout を Natural に強制します。ゲーム外では通常のウィンドウ設定に戻します。", "Nur im Spiel vorübergehend Screen Sizing auf Top Only und Screen Layout auf Natural erzwingen. Außerhalb des Spiels werden die normalen Fenstereinstellungen wiederhergestellt.", "Solo en partida, fuerza temporalmente Screen Sizing a Top Only y Screen Layout a Natural. Fuera del juego, restaura los ajustes normales de ventana.", "En jeu uniquement, force temporairement Screen Sizing sur Top Only et Screen Layout sur Natural. Hors jeu, restaure les paramètres de fenêtre normaux."},
    {"Enable In-Game Aspect Ratio", "ゲーム内アスペクト比を有効化", "Seitenverhältnis im Spiel aktivieren", "Activar relación de aspecto en juego", "Activer le ratio d'aspect en jeu"},
    {"Aspect Ratio Mode", "アスペクト比モード", "Seitenverhältnis-Modus", "Modo de relación de aspecto", "Mode de format d'image"},
    {"Auto (match Aspect Ratio)", "自動 (画面比率に合わせる)", "Automatisch (Seitenverhältnis anpassen)", "Automático (ajustar relación de aspecto)", "Auto (adapter au ratio d'aspect)"},
    {"5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)", "5:3 (3DS)"},
    {"16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)", "16:10 (3DS)"},
    {"16:9", "16:9", "16:9", "16:9", "16:9"},
    {"21:9", "21:9", "21:9", "21:9", "21:9"},
    {"Use DS Firmware Language (EU-style Auto Patch)", "DSファームウェア言語を使う (EU版風の自動パッチ)", "DS-Firmware-Sprache verwenden (EU-Auto-Patch)", "Usar idioma del firmware DS (parche automático estilo UE)", "Utiliser la langue du firmware DS (patch auto style UE)"},
    {"Show Headshot Notification Online", "オンラインでヘッドショット通知を表示", "Kopfschuss-Benachrichtigung online anzeigen", "Mostrar notificación de headshot en línea", "Afficher la notification de headshot en ligne"},
    {"Show Enemy HP Meter Online", "オンラインで敵HPメーターを表示", "Feind-HP-Anzeige online anzeigen", "Mostrar medidor de PV del enemigo en línea", "Afficher la jauge PV ennemi en ligne"},
    {"Fix Noxus Blade Persistence on Death", "ノクサスのブレード残留を修正", "Noxus-Klinge bei Tod beibehalten beheben", "Corregir persistencia de espada Noxus al morir", "Corriger la persistance de la lame Noxus à la mort"},
    {"Friend/Rival Wi-Fi Active Bitset Fix (v2)", "フレンド/ライバルWi-Fi有効ビット修正 (v2)", "Freund/Rivale Wi-Fi-Aktiv-Bitset-Fix (v2)", "Corrección de bitset activo Wi-Fi amigo/rival (v2)", "Correctif bitset actif Wi-Fi ami/rival (v2)"},
    {"Shadow Freeze Fix (Ice Wave full 3D angle check)", "シャドウフリーズ修正 (アイスウェーブの3D角度判定)", "Shadow-Freeze-Fix (Ice Wave: vollständige 3D-Winkelprüfung)", "Corrección de congelación de sombras (Ice Wave: comprobación 3D completa de ángulos)", "Correctif gel d'ombre (Ice Wave : vérification d'angle 3D complète)"},
    {"Expand Stage Selection (Unlock Extra Stages)", "ステージ選択を拡張 (追加ステージ解放)", "Stagewahl erweitern (Zusatzstages freischalten)", "Ampliar selección de escenarios (desbloquear extras)", "Étendre la sélection de stages (débloquer des stages supplémentaires)"},
    {"Also unlock additional stages", "追加ステージも解放", "Zusätzliche Stufen freischalten", "También desbloquear etapas adicionales", "Débloquer aussi des stages supplémentaires"},
    {"Disable Double Damage Multiplier", "ダブルダメージ倍率を無効化", "Doppelten Schadensmultiplikator deaktivieren", "Desactivar multiplicador de daño doble", "Désactiver le multiplicateur de dégâts double"},
    {"Damage Notify Purple", "ダメージ通知を紫にする", "Schadensmeldung violett", "Notificación de daño en púrpura", "Notification de dégâts en violet"},
    {"Power-Ups: Pick Up With No Effect", "パワーアップ: 取得しても効果なし", "Power-Ups: Aufsammeln ohne Effekt", "Potenciadores: recoger sin efecto", "Bonus : ramassage sans effet"},
    {"Double Damage", "ダブルダメージ", "Doppelter Schaden", "Doble daño", "Dégâts doubles"},
    {"Cloak", "クローク", "Tarnung", "Capa", "Camouflage"},
    {"DEATH ALT", "デスオルト", "TOD-ALT", "ALT MUERTE", "ALT MORT"},
    {"Reset sensitivity values", "感度を既定値に戻す", "Empfindlichkeitswerte zurücksetzen", "Restablecer valores de sensibilidad", "Réinitialiser les valeurs de sensibilité"},
    {"Video quality: Low (High Performance)", "映像品質: 低 (高パフォーマンス)", "Videoqualität: Niedrig (Hohe Leistung)", "Calidad de vídeo: Baja (alto rendimiento)", "Qualité vidéo : Basse (hautes performances)"},
    {"Video quality: High (Lower Performance)", "映像品質: 高 (低パフォーマンス)", "Grafikqualität: Hoch (geringere Leistung)", "Calidad de vídeo: Alta (menor rendimiento)", "Qualité vidéo : Élevée (performances réduites)"},
    {"Video quality: High2 (Recommended. Best Performance)", "映像品質: 高2 (推奨。最高パフォーマンス)", "Videoqualität: Hoch2 (Empfohlen. Beste Leistung)", "Calidad de vídeo: Alta2 (Recomendada. Mejor rendimiento)", "Qualité vidéo : Élevée2 (Recommandée. Meilleures performances)"},
    {"Apply SFX volume", "効果音音量を反映", "SFX-Lautstärke anwenden", "Aplicar volumen de efectos", "Appliquer le volume des effets"},
    {"Apply music volume", "BGM音量を反映", "Musiklautstärke anwenden", "Aplicar volumen de música", "Appliquer le volume de la musique"},
    {"Apply the selected hunter to your license. (Renaming will update the save data)", "選択したハンターをライセンスに反映 (名前変更でセーブデータ更新)", "Ausgewählten Hunter auf die Lizenz anwenden. (Umbenennen aktualisiert die Speicherdaten)", "Aplicar el cazador seleccionado a tu licencia. (Cambiar el nombre actualizará la partida guardada)", "Appliquer le chasseur sélectionné à votre licence. (Renommer mettra à jour la sauvegarde)"},
    {"Apply the selected color to your license. (Renaming will update the save data)", "選択した色をライセンスに反映 (名前変更でセーブデータ更新)", "Ausgewählte Farbe auf die Lizenz anwenden. (Umbenennen aktualisiert die Speicherdaten)", "Aplicar el color seleccionado a tu licencia. (Renombrar actualizará los datos de guardado)", "Appliquer la couleur sélectionnée à votre licence. (Renommer mettra à jour les données de sauvegarde)"},
    {"Select the hunter to apply.", "反映するハンターを選択します。", "Jäger zum Anwenden auswählen.", "Selecciona el cazador a aplicar.", "Sélectionnez le chasseur à appliquer."},
    {"Select the color to apply.", "反映する色を選択します。", "Wähle die anzuwendende Farbe.", "Selecciona el color a aplicar.", "Sélectionnez la couleur à appliquer."},
    {"Red(JP,KR)", "赤 (JP,KR)", "Rot (JP,KR)", "Rojo (JP,KR)", "Rouge (JP,KR)"},
    {"Blue(US)", "青 (US)", "Blau (US)", "Azul (US)", "Bleu (US)"},
    {"Green(EU)", "緑 (EU)", "Grün (EU)", "Verde (EU)", "Vert (EU)"},
    {"Auto Scale — Base", "自動スケール - 基準", "Automatische Skalierung — Basis", "Escala automática — Base", "Échelle automatique — Base"},
    {"Auto Scale (by Damage)", "自動スケール (ダメージ別)", "Auto-Skalierung (nach Schaden)", "Escala automática (por daño)", "Échelle auto (selon dégâts)"},
    {"Disabled (vanilla 25)", "無効 (バニラ25)", "Deaktiviert (Vanilla 25)", "Desactivado (vanilla 25)", "Désactivé (vanilla 25)"},
    {"Fixed", "固定", "Fest", "Fijo", "Fixe"},
    {"Fixed Threshold", "固定しきい値", "Fester Schwellenwert", "Umbral fijo", "Seuil fixe"},
    {"Per Damage (Low / Medium / High)", "ダメージ別 (低/中/高)", "Pro Schaden (Niedrig / Mittel / Hoch)", "Por daño (Bajo / Medio / Alto)", "Par dégâts (Faible / Moyen / Élevé)"},
    {"Per Damage — Low", "ダメージ別 - 低", "Pro Schaden — Niedrig", "Por daño — Bajo", "Par dégâts — Faible"},
    {"Per Damage — Medium", "ダメージ別 - 中", "Pro Schaden — Mittel", "Por daño — Medio", "Par dégâts — Moyen"},
    {"Per Damage — High", "ダメージ別 - 高", "Pro Schaden — Hoch", "Por daño — Alto", "Par dégâts — Élevé"},
    {"Developer-only option. Currently disabled.", "開発者向けオプションです。現在は無効です。", "Nur für Entwickler. Derzeit deaktiviert.", "Opción solo para desarrolladores. Actualmente desactivada.", "Option réservée aux développeurs. Actuellement désactivée."},
    {"Developer-only option enabled in this build.", "このビルドでは開発者向けオプションが有効です。", "Entwickleroption in diesem Build aktiviert.", "Opción solo para desarrolladores activada en esta compilación.", "Option réservée aux développeurs activée dans cette version."},
    {"Developer-only option. Build with MELONPRIME_ENABLE_DEVELOPER_FEATURES to enable it.", "開発者向けオプションです。有効にするには MELONPRIME_ENABLE_DEVELOPER_FEATURES 付きでビルドしてください。", "Nur für Entwickler. Mit MELONPRIME_ENABLE_DEVELOPER_FEATURES kompilieren, um es zu aktivieren.", "Opción solo para desarrolladores. Compila con MELONPRIME_ENABLE_DEVELOPER_FEATURES para activarla.", "Option réservée aux développeurs. Compilez avec MELONPRIME_ENABLE_DEVELOPER_FEATURES pour l'activer."},
    {"Features in this section are still in development and are not ready for public release. They may or may not be released.", "このセクションの機能は開発中で、公開リリースの準備はまだできていません。今後公開されるとは限りません。", "Funktionen in diesem Abschnitt befinden sich noch in der Entwicklung und sind nicht für die Veröffentlichung bereit. Eine Veröffentlichung ist nicht garantiert.", "Las funciones de esta sección aún están en desarrollo y no están listas para su lanzamiento público. Puede que se publiquen o no.", "Les fonctionnalités de cette section sont encore en développement et ne sont pas prêtes pour une diffusion publique. Elles pourront être publiées ou non."},
    {"Controls how the game's current aim direction follows the target aim direction.", "ゲーム内の現在の照準方向を、目標の照準方向へどう追従させるかを設定します。", "Steuert, wie die aktuelle Zielrichtung im Spiel der Zielrichtung folgt.", "Controla cómo la dirección de puntería actual del juego sigue la dirección objetivo.", "Contrôle la façon dont la direction de visée actuelle du jeu suit la direction cible."},
    {"Checked: use the native ARM9 game function hook. Unchecked: use the older touch/menu simulation path.", "オン: ゲーム本来のARM9関数をフックして使います。オフ: 従来のタッチ/メニュー模擬処理を使います。", "Aktiviert: nativen ARM9-Spielfunktions-Hook verwenden. Deaktiviert: älteren Touch-/Menü-Simulationspfad verwenden.", "Marcado: usar el hook nativo de función ARM9 del juego. Desmarcado: usar la ruta antigua de simulación táctil/menú.", "Coché : utiliser le hook natif de fonction ARM9 du jeu. Décoché : utiliser l'ancien chemin de simulation tactile/menu."},
    {"Checked: inject a native fire edge inside the game's Biped fire update. Unchecked: use the older fixed input/overlay path.", "オン: ゲーム本来の二足射撃更新内へ射撃入力エッジを注入します。オフ: 従来の固定入力/オーバーレイ経路を使います。", "Aktiviert: Feuer-Edge nativ in das Biped-Feuer-Update des Spiels einschleusen. Deaktiviert: älteren festen Eingabe-/Overlay-Pfad verwenden.", "Activado: inyecta un borde de disparo nativo en la actualización de fuego Biped del juego. Desactivado: usa la ruta antigua de entrada/superposición fija.", "Coché : injecte un signal de tir natif dans la mise à jour de tir Biped du jeu. Décoché : utilise l'ancien chemin d'entrée/superposition fixe."},
    {"Checked: use the native ARM9 TransformRequest hook. Unchecked: use the older touch/menu simulation path.", "オン: ゲーム本来のARM9変形要求フックを使います。オフ: 従来のタッチ/メニュー模擬処理を使います。", "Aktiviert: nativen ARM9-TransformRequest-Hook verwenden. Deaktiviert: älteren Touch-/Menü-Simulationspfad verwenden.", "Marcado: usar el hook nativo ARM9 TransformRequest. Desmarcado: usar la ruta antigua de simulación táctil/menú.", "Coché : utiliser le hook natif ARM9 TransformRequest. Décoché : utiliser l'ancien chemin de simulation tactile/menu."},
    {"Checked: use the current in-game zoom binding from the player's control preset. Unchecked: use the older fixed R-button path.", "オン: 現在の操作プリセットにあるゲーム内ズーム割り当てを使います。オフ: 従来の固定Rボタン経路を使います。", "Aktiviert: aktuelle Zoom-Belegung aus dem Steuerungs-Preset verwenden. Deaktiviert: älteren festen R-Tasten-Pfad verwenden.", "Marcado: usa la asignación de zoom en el juego del preset de controles del jugador. Desmarcado: usa la ruta fija antigua del botón R.", "Coché : utilise la touche de zoom en jeu du preset de contrôles du joueur. Décoché : utilise l'ancien chemin fixe du bouton R."},
    {"Checked: toggle native weapon zoom by calling the game's SetPlayerScopeZoom setter. Unchecked with New Method also off: use Legacy fixed R-button input.", "オン: ゲーム本来のズーム切替処理を呼んで武器ズームを切り替えます。新方式もオフの場合は、旧方式の固定Rボタン入力を使います。", "Aktiviert: Waffen-Zoom per SetPlayerScopeZoom des Spiels umschalten. Deaktiviert und Neue Methode aus: Legacy-Eingabe mit fester R-Taste.", "Marcado: alternar el zoom nativo del arma llamando a SetPlayerScopeZoom del juego. Desmarcado y Método nuevo también desactivado: usar entrada fija del botón R heredada.", "Coché : basculer le zoom d'arme natif via SetPlayerScopeZoom du jeu. Décoché et Nouvelle méthode aussi désactivée : utiliser l'entrée héritée du bouton R fixe."},
    {"Setting Key: Metroid.Apply.SfxVolume (check to apply)", "設定キー: Metroid.Apply.SfxVolume (オンで反映)", "Einstellungsschlüssel: Metroid.Apply.SfxVolume (zum Anwenden aktivieren)", "Clave de ajuste: Metroid.Apply.SfxVolume (marcar para aplicar)", "Clé de réglage : Metroid.Apply.SfxVolume (cocher pour appliquer)"},
    {"Setting Key: Metroid.Volume.SFX (0–9)", "設定キー: Metroid.Volume.SFX (0-9)", "Einstellungsschlüssel: Metroid.Volume.SFX (0–9)", "Clave de ajuste: Metroid.Volume.SFX (0–9)", "Clé de réglage : Metroid.Volume.SFX (0–9)"},
    {"Setting Key: Metroid.Apply.MusicVolume (check to apply)", "設定キー: Metroid.Apply.MusicVolume (オンで反映)", "Einstellungsschlüssel: Metroid.Apply.MusicVolume (zum Anwenden aktivieren)", "Clave de ajuste: Metroid.Apply.MusicVolume (marcar para aplicar)", "Clé de réglage : Metroid.Apply.MusicVolume (cocher pour appliquer)"},
    {"Setting Key: Metroid.Volume.Music (0–9)", "設定キー: Metroid.Volume.Music (0-9)", "Einstellungsschlüssel: Metroid.Volume.Music (0–9)", "Clave de ajuste: Metroid.Volume.Music (0–9)", "Clé de réglage : Metroid.Volume.Music (0–9)"},
    {"Enables applying the selected hunter to the license.", "選択したハンターをライセンスに反映できるようにします。", "Ermöglicht, den ausgewählten Hunter auf die Lizenz anzuwenden.", "Permite aplicar el cazador seleccionado a la licencia.", "Permet d'appliquer le chasseur sélectionné à la licence."},
    {"Enables applying the selected color to the license.", "選択した色をライセンスに反映できるようにします。", "Ermöglicht das Anwenden der ausgewählten Farbe auf die Lizenz.", "Permite aplicar el color seleccionado a la licencia.", "Permet d'appliquer la couleur sélectionnée à la licence."},
    {"Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete, DwmFlush = wait for DWM compositor (Windows only)", "画面同期方式: オフ = 同期呼び出しなし、glFinish = GLコマンド完了まで待機、DwmFlush = DWMコンポジター待機 (Windowsのみ)", "Bildschirm-Sync-Modus: OFF = kein Sync-Aufruf, glFinish = auf GL-Befehle warten, DwmFlush = auf DWM-Compositor warten (nur Windows)", "Modo de sincronización de pantalla: Desactivado = sin llamada de sync, glFinish = esperar a que terminen los comandos GL, DwmFlush = esperar al compositor DWM (solo Windows)", "Mode de sync écran : Désactivé = pas d'appel de sync, glFinish = attendre la fin des commandes GL, DwmFlush = attendre le compositeur DWM (Windows uniquement)"},
    {"Off: No sync (lowest latency, but the display may look choppy). glFinish: Smoother display by waiting for rendering to fully complete each frame. Automatically disabled during FastForward/SlowMo.", "オフ: 同期なし (最小遅延ですが、表示がカクつくことがあります)。glFinish: 各フレームの描画完了を待つことで表示を滑らかにします。早送り/スローモーション中は自動的に無効になります。", "Aus: Keine Synchronisation (niedrigste Latenz, Anzeige kann ruckeln). glFinish: Glattere Anzeige durch Warten auf vollständiges Rendering pro Frame. Wird bei Schnellvorlauf/SlowMo automatisch deaktiviert.", "Desactivado: Sin sincronización (menor latencia, pero la pantalla puede verse entrecortada). glFinish: Pantalla más fluida esperando a que el renderizado termine cada fotograma. Se desactiva automáticamente durante avance rápido/cámara lenta.", "Désactivé : Pas de synchro (latence minimale, mais l'affichage peut saccader). glFinish : Affichage plus fluide en attendant la fin du rendu de chaque image. Désactivé automatiquement pendant avance rapide/ralenti."},
    {"Windows only. Applies only while the bottom screen is actually being drawn. Press ESC to release the cursor.", "Windowsのみ。下画面が実際に描画されている間だけ適用されます。ESCでカーソル制限を解除します。", "Nur Windows. Gilt nur, solange der untere Bildschirm tatsächlich gezeichnet wird. ESC drücken, um den Cursor freizugeben.", "Solo Windows. Se aplica solo mientras se dibuja la pantalla inferior. Pulsa ESC para liberar el cursor.", "Windows uniquement. S'applique uniquement tant que l'écran du bas est réellement affiché. Appuyez sur Échap pour libérer le curseur."},
    {"Does not overwrite your saved window layout settings. The override is applied only while Metroid Prime Hunters is in-game.", "保存済みのウィンドウレイアウト設定は上書きしません。Metroid Prime Huntersのゲーム中だけ一時的に適用されます。", "Überschreibt nicht die gespeicherten Fensterlayout-Einstellungen. Die Überschreibung gilt nur während Metroid Prime Hunters im Spiel läuft.", "No sobrescribe los ajustes guardados del diseño de ventana. La anulación solo se aplica mientras Metroid Prime Hunters está en juego.", "N'écrase pas vos réglages de disposition de fenêtre enregistrés. La surcharge s'applique uniquement pendant Metroid Prime Hunters en jeu."},
    {"When playing with an Aspect Ratio other than 4:3 (Native), apply this to change the in-game 3D aspect ratio to match. Changes are applied at the start of each match.", "4:3 (ネイティブ) 以外の画面比率で遊ぶとき、ゲーム内3Dのアスペクト比も一致するように変更します。変更は各試合の開始時に適用されます。", "Bei einem Seitenverhältnis außer 4:3 (Nativ) das ingame 3D-Seitenverhältnis entsprechend anpassen. Änderungen gelten zu Beginn jedes Matches.", "Al jugar con una relación de aspecto distinta de 4:3 (nativa), ajusta la relación 3D en el juego para que coincida. Los cambios se aplican al inicio de cada partida.", "Lors d'un ratio d'aspect autre que 4:3 (natif), adapte le ratio 3D en jeu en conséquence. Les changements s'appliquent au début de chaque match."},
    {"Changes the HP value at which the low-HP warning sound and warning HUD state trigger (vanilla: 25). Applied at the start of each match based on the current Damage setting.", "低HP警告音と警告HUD状態が発生するHP値を変更します (バニラ: 25)。現在のダメージ設定に応じて、各試合の開始時に適用されます。", "Ändert den HP-Wert, bei dem Warnsound und Warn-HUD ausgelöst werden (Vanilla: 25). Wird zu Beginn jedes Matches basierend auf der aktuellen Schaden-Einstellung angewendet.", "Cambia el valor de PV en el que se activan el sonido de aviso de PV bajos y el estado de aviso del HUD (vanilla: 25). Se aplica al inicio de cada partida según el ajuste de daño actual.", "Modifie la valeur de PV déclenchant le son d'avertissement et l'état HUD d'alerte (vanilla : 25). Appliqué au début de chaque match selon le réglage de dégâts actuel."},
    {"Edit HUD Layout", "HUD配置を編集", "HUD-Layout bearbeiten", "Editar diseño HUD", "Modifier la disposition HUD"},
    {"Hide this dialog and enter the interactive HUD position editor", "このダイアログを隠して、対話式HUD配置エディターに入ります", "Diesen Dialog ausblenden und interaktiven HUD-Positionseditor öffnen", "Ocultar este diálogo y entrar en el editor interactivo de posición HUD", "Masquer cette boîte de dialogue et ouvrir l'éditeur interactif de position HUD"},
    {"Enable Custom HUD (Replaces the in-game HUD with a custom overlay showing HP, ammo, weapon icons and crosshair)", "カスタムHUDを有効化 (ゲーム内HUDを、HP/弾薬/武器アイコン/照準のカスタム表示に置き換えます)", "Benutzerdefiniertes HUD aktivieren (Ersetzt das Ingame-HUD durch ein Overlay mit HP, Munition, Waffensymbolen und Visier)", "Activar HUD personalizado (Reemplaza el HUD del juego por una superposición con HP, munición, iconos de armas y punto de mira)", "Activer le HUD personnalisé (Remplace le HUD en jeu par une superposition affichant HP, munitions, icônes d'armes et réticule)"},
    {"Share your Custom HUD setup as TOML text, or paste TOML into the input area to apply it to the current dialog.", "カスタムHUD設定をTOMLテキストとして共有、または入力欄に貼り付けて現在のダイアログへ適用します。", "Teile dein benutzerdefiniertes HUD-Setup als TOML-Text oder füge TOML in das Eingabefeld ein, um es auf den aktuellen Dialog anzuwenden.", "Comparte tu configuración de HUD personalizado como texto TOML, o pega TOML en el área de entrada para aplicarlo al diálogo actual.", "Partagez votre configuration HUD personnalisée en texte TOML, ou collez du TOML dans la zone de saisie pour l'appliquer à la boîte de dialogue actuelle."},
    {"Press Generate to build sharable Custom HUD TOML.", "生成を押すと共有用のカスタムHUD TOMLを作成します。", "Auf Generieren klicken, um teilbares Custom HUD TOML zu erstellen.", "Pulsa Generar para crear un TOML de HUD personalizado compartible.", "Appuyez sur Générer pour créer un TOML de HUD personnalisé partageable."},
    {"Paste Custom HUD TOML here, then press Apply Input.", "ここにカスタムHUD TOMLを貼り付けてから、入力を適用してください。", "Benutzerdefiniertes HUD-TOML hier einfügen, dann Eingabe anwenden drücken.", "Pega aquí el TOML HUD personalizado y pulsa Aplicar entrada.", "Collez le TOML HUD personnalisé ici, puis appuyez sur Appliquer l'entrée."},

    // Custom HUD sections and properties
    {"— Common HUD —", "- 共通HUD -", "— Gemeinsames HUD —", "— HUD común —", "— HUD commun —"},
    {"— Score Row (per mode) —", "- スコア行 (モード別) -", "— Punktezeile (pro Modus) —", "— Fila de puntuación (por modo) —", "— Ligne de score (par mode) —"},
    {"Hide Helmet (Visor Mask)", "ヘルメット (バイザーマスク) を非表示", "Helm ausblenden (Visiermaske)", "Ocultar casco (máscara del visor)", "Masquer le casque (masque du viseur)"},
    {"Hide Ammo", "弾薬を非表示", "Munition ausblenden", "Ocultar munición", "Masquer les munitions"},
    {"Hide Weapon Icon", "武器アイコンを非表示", "Waffensymbol ausblenden", "Ocultar icono de arma", "Masquer l'icône d'arme"},
    {"Hide HP", "HPを非表示", "HP ausblenden", "Ocultar HP", "Masquer les PV"},
    {"Hide Crosshair", "照準を非表示", "Visier ausblenden", "Ocultar punto de mira", "Masquer le réticule"},
    {"Hide Bomb (Boost Ball kept)", "ボムを非表示 (ブーストボールは維持)", "Bombe ausblenden (Boost Ball bleibt)", "Ocultar bomba (Boost Ball conservado)", "Masquer la bombe (Boost Ball conservé)"},
    {"Hide Score: Battle", "スコア非表示: バトル", "Punkte ausblenden: Kampf", "Ocultar puntuación: Batalla", "Masquer le score : Bataille"},
    {"Hide Score: Survival", "スコア非表示: サバイバル", "Punktestand ausblenden: Überleben", "Ocultar puntuación: Supervivencia", "Masquer score : Survie"},
    {"Hide Score: Prime Hunter", "スコア非表示: プライムハンター", "Punkte ausblenden: Prime Hunter", "Ocultar puntuación: Prime Hunter", "Masquer le score : Prime Hunter"},
    {"Hide Score: Bounty", "スコア非表示: バウンティ", "Punkte ausblenden: Kopfgeld", "Ocultar puntuación: Recompensa", "Masquer le score : Prime"},
    {"Hide Score: Capture", "スコア非表示: キャプチャー", "Punkte ausblenden: Capture", "Ocultar puntuación: Capture", "Masquer le score : Capture"},
    {"Hide Score: Defender", "スコア非表示: ディフェンダー", "Punkte ausblenden: Verteidiger", "Ocultar puntuación: Defensor", "Masquer le score : Défenseur"},
    {"Hide Score: Node", "スコア非表示: ノード", "Punktestand ausblenden: Knoten", "Ocultar puntuación: Nodo", "Masquer score : Nœud"},
    {"Text Scale (Base %)", "文字スケール (基準 %)", "Textskalierung (Basis %)", "Escala de texto (base %)", "Échelle du texte (base %)"},
    {"Auto Scale Enable", "自動スケールを有効化", "Automatische Skalierung aktivieren", "Activar escala automática", "Activer l'échelle auto"},
    {"Auto Scale Global Cap %", "自動スケール全体上限 %", "Autom. Skalierung globales Limit %", "Límite global de escala automática %", "Plafond global d'échelle auto %"},
    {"Auto Scale Text Cap %", "自動スケール文字上限 %", "Autom. Skalierung Text-Obergrenze %", "Tope de texto de escala automática %", "Plafond de texte échelle auto %"},
    {"Auto Scale Icon Cap %", "自動スケールアイコン上限 %", "Auto-Skalierung Symbol-Obergrenze %", "Límite de icono de escala auto %", "Plafond icône échelle auto %"},
    {"Auto Scale Gauge Cap %", "自動スケールゲージ上限 %", "Auto-Skalierung Anzeige-Obergrenze %", "Tope de escala automática del indicador %", "Plafond d'échelle auto de la jauge %"},
    {"Auto Scale Crosshair Cap %", "自動スケール照準上限 %", "Obergrenze Visier bei Auto-Skalierung %", "Límite de mira con escala automática %", "Plafond réticule échelle auto %"},
    {"Font Source", "フォントソース", "Schriftquelle", "Fuente de tipografía", "Source de police"},
    {"Font Size (px)", "フォントサイズ (px)", "Schriftgröße (px)", "Tamaño de fuente (px)", "Taille de police (px)"},
    {"Font Weight", "フォントウェイト", "Schriftstärke", "Grosor de fuente", "Épaisseur de police"},
    {"Italic", "イタリック", "Kursiv", "Cursiva", "Italique"},
    {"Underline", "下線", "Unterstreichen", "Subrayado", "Souligné"},
    {"Strikethrough", "取り消し線", "Durchgestrichen", "Tachado", "Barré"},
    {"Color", "色", "Farbe", "Color", "Couleur"},
    {"Scale %", "スケール %", "Skalierung %", "Escala %", "Échelle %"},
    {"Zoom Stage", "ズーム段階", "Zoom-Stufe", "Etapa de zoom", "Niveau de zoom"},
    {"Zoom Base Scale %", "ズーム時元照準スケール %", "Basis-Visiergröße beim Zoom %", "Escala base de mira con zoom %", "Échelle de base du réticule au zoom %"},
    {"Zoom Base Opacity %", "ズーム時元照準不透明度 %", "Zoom-Basis-Deckkraft %", "Opacidad base del zoom %", "Opacité de base du zoom %"},
    {"Zoom Opacity %", "ズーム時不透明度 %", "Zoom-Deckkraft %", "Opacidad del zoom %", "Opacité du zoom %"},
    {"Zoom Scope Reticle", "ズームスコープ照準", "Zoom-Fadenkreuz", "Retícula de zoom", "Réticule de zoom"},
    {"Zoom Scope", "ズームスコープ", "Zoom-Visier", "Visor de zoom", "Lunette de zoom"},
    {"Scope Radius", "スコープ半径", "Visierradius", "Radio de mira", "Rayon du viseur"},
    {"Scope Gap", "スコープ隙間", "Visierabstand", "Separación del visor", "Écart du viseur"},
    {"Scope Thickness", "スコープ太さ", "Visierstärke", "Grosor del visor", "Épaisseur du viseur"},
    {"Scope Thick.", "スコープ太さ", "Fadenkreuz-Dicke", "Grosor de retícula", "Épaisseur réticule"},
    {"Scope Center Dot", "スコープ中央ドット", "Visier-Mittelpunkt", "Punto central del visor", "Point central du viseur"},
    {"Scope Dot Size", "スコープドットサイズ", "Visierpunktgröße", "Tamaño del punto de mira", "Taille du point du viseur"},
    {"Scope Dot Opacity %", "スコープドット不透明度 %", "Visierpunkt-Deckkraft %", "Opacidad del punto del visor %", "Opacité du point du viseur %"},
    {"Scope Opacity %", "スコープ不透明度 %", "Visier-Deckkraft %", "Opacidad del visor %", "Opacité du viseur %"},
    {"Zoom Transition", "ズームトランジション", "Zoom-Übergang", "Transición de zoom", "Transition de zoom"},
    {"Zoom Transition Speed %", "ズーム遷移速度 %", "Zoom-Übergangsgeschwindigkeit %", "Velocidad de transición de zoom %", "Vitesse de transition du zoom %"},
    {"Transition Speed %", "遷移速度 %", "Übergangsgeschwindigkeit %", "Velocidad de transición %", "Vitesse de transition %"},
    {"Zoom Pulse Ring", "ズームパルスリング", "Zoom-Pulsring", "Anillo de pulso del zoom", "Anneau pulsé du zoom"},
    {"Pulse Ring", "パルスリング", "Pulsring", "Anillo pulsante", "Anneau pulsant"},
    {"Zoom Pulse Strength %", "ズームパルス強度 %", "Zoom-Puls-Stärke %", "Intensidad de pulso de zoom %", "Intensité impulsion zoom %"},
    {"Pulse Strength %", "パルス強度 %", "Pulsstärke %", "Intensidad del pulso %", "Intensité de pulsation %"},
    {"Zoom Crosshair", "ズーム照準", "Zoom-Visier", "Punto de mira con zoom", "Réticule au zoom"},
    {"Transition Style", "トランジションスタイル", "Übergangsstil", "Estilo de transición", "Style de transition"},
    {"Custom Scope Dot Color", "スコープドット色を個別指定", "Eigene Visierpunkt-Farbe", "Color personalizado del punto del visor", "Couleur personnalisée du point de visée"},
    {"Scope Dot Color", "スコープドット色", "Fadenkreuz-Punktfarbe", "Color del punto de retícula", "Couleur point réticule"},
    {"Staged", "段階", "Gestuft", "Por etapas", "Par étapes"},
    {"Fade", "フェード", "Einblenden", "Fundido", "Fondu"},
    {"Glitch", "グリッチ", "Glitch", "Glitch", "Glitch"},
    {"Glitch2", "グリッチ2", "Glitch2", "Glitch2", "Glitch2"},
    {"Snap", "スナップ", "Einrasten", "Ajuste", "Accrochage"},
    {"Digital", "デジタル", "Digital", "Digital", "Numérique"},
    {"Pulse Wave", "パルス波", "Pulswelle", "Onda pulsante", "Onde pulsée"},
    {"Magic Circle", "魔法陣", "Magischer Kreis", "Círculo mágico", "Cercle magique"},
    {"SF Movie", "SF映画", "SF-Film", "Película SF", "Film SF"},
    {"Tactical Lock", "戦術ロック", "Taktische Sperre", "Bloqueo táctico", "Verrouillage tactique"},
    {"Sniper Optics", "スナイパー光学", "Scharfschützenoptik", "Óptica de francotirador", "Optique de sniper"},
    {"Drone LIDAR", "ドローンLIDAR", "Drohnen-LIDAR", "LIDAR del dron", "LIDAR du drone"},
    {"Beam Charge", "ビームチャージ", "Strahlaufladung", "Carga del rayo", "Charge du rayon"},
    {"Zoom", "ズーム", "Zoom", "Zoom", "Zoom"},
    {"Outline", "アウトライン", "Umriss", "Contorno", "Contour"},
    {"Outline Color", "アウトライン色", "Umrissfarbe", "Color del contorno", "Couleur du contour"},
    {"Outline Opacity", "アウトライン不透明度", "Konturdeckkraft", "Opacidad del contorno", "Opacité du contour"},
    {"Outline Thickness", "アウトライン太さ", "Konturstärke", "Grosor del contorno", "Épaisseur du contour"},
    {"Outline Thick.", "アウトライン太さ", "Konturstärke", "Grosor del contorno", "Épaiss. contour"},
    {"Center Dot", "中央ドット", "Mittelpunkt", "Punto central", "Point central"},
    {"Custom Dot Color", "ドット色を個別指定", "Eigene Punktfarbe", "Color de punto personalizado", "Couleur de point personnalisée"},
    {"Dot Color", "ドット色", "Punktfarbe", "Color del punto", "Couleur du point"},
    {"Dot Shape", "ドット形状", "Punktform", "Forma del punto", "Forme du point"},
    {"Scope Dot Shape", "スコープドット形状", "Visierpunkt-Form", "Forma del punto del visor", "Forme du point de visée"},
    {"Square", "四角", "Quadrat", "Cuadrado", "Carré"},
    {"Circle", "丸", "Kreis", "Círculo", "Cercle"},
    {"Dot Opacity", "ドット不透明度", "Punktdeckkraft", "Opacidad del punto", "Opacité du point"},
    {"Dot Thickness", "ドット太さ", "Punktdicke", "Grosor del punto", "Épaisseur du point"},
    {"Dot Thick.", "ドット太さ", "Punktstärke", "Grosor del punto", "Épaiss. point"},
    {"T-Style", "Tスタイル", "T-Stil", "Estilo T", "Style T"},
    {"Inner", "内側", "Innen", "Interior", "Intérieur"},
    {"Outer", "外側", "Außen", "Exterior", "Extérieur"},
    {"Inner Lines", "内側ライン", "Innere Linien", "Líneas interiores", "Lignes intérieures"},
    {"Outer Lines", "外側ライン", "Außenlinien", "Líneas exteriores", "Lignes extérieures"},
    {"Show", "表示", "Anzeigen", "Mostrar", "Afficher"},
    {"Show Text", "文字を表示", "Text anzeigen", "Mostrar texto", "Afficher le texte"},
    {"Show Number", "数値を表示", "Zahl anzeigen", "Mostrar número", "Afficher le nombre"},
    {"Enable", "有効化", "Aktivieren", "Activar", "Activer"},
    {"Enable (Override All)", "有効化 (全体上書き)", "Aktivieren (Alles überschreiben)", "Activar (Anular todo)", "Activer (Remplacer tout)"},
    {"Opacity", "不透明度", "Deckkraft", "Opacidad", "Opacité"},
    {"Length", "長さ", "Länge", "Longitud", "Longueur"},
    {"Length X", "長さ X", "Länge X", "Longitud X", "Longueur X"},
    {"Length Y", "長さ Y", "Länge Y", "Longitud Y", "Longueur Y"},
    {"Link XY", "XYを連動", "XY verknüpfen", "Vincular XY", "Lier XY"},
    {"Thickness", "太さ", "Dicke", "Grosor", "Épaisseur"},
    {"Offset", "オフセット", "Versatz", "Desplazamiento", "Décalage"},
    {"Offset X", "オフセット X", "Versatz X", "Desplazamiento X", "Décalage X"},
    {"Offset Y", "オフセット Y", "Versatz Y", "Desplazamiento Y", "Décalage Y"},
    {"Anchor", "基準位置", "Anker", "Ancla", "Ancrage"},
    {"Top Left", "左上", "Oben links", "Arriba a la izquierda", "En haut à gauche"},
    {"Top Center", "上中央", "Oben Mitte", "Arriba centro", "Haut centre"},
    {"Top Right", "右上", "Oben rechts", "Arriba a la derecha", "En haut à droite"},
    {"Middle Left", "左中央", "Mitte links", "Centro izquierda", "Milieu gauche"},
    {"Middle Center", "中央", "Mitte Mitte", "Centro central", "Centre central"},
    {"Middle Right", "右中央", "Mitte rechts", "Centro derecha", "Milieu droite"},
    {"Bottom Left", "左下", "Unten links", "Abajo izquierda", "Bas gauche"},
    {"Bottom Center", "下中央", "Unten Mitte", "Abajo al centro", "En bas au centre"},
    {"Bottom Right", "右下", "Unten rechts", "Abajo a la derecha", "Bas droite"},
    {"Mid Left", "左中央", "Mitte links", "Centro izquierda", "Centre gauche"},
    {"Mid Center", "中央", "Mitte zentriert", "Centro", "Centre"},
    {"Mid Right", "右中央", "Mitte rechts", "Centro derecha", "Milieu droite"},
    {"Bot Left", "左下", "Unten links", "Abajo a la izquierda", "En bas à gauche"},
    {"Bot Center", "下中央", "Unten Mitte", "Abajo centro", "Bas centre"},
    {"Bot Right", "右下", "Unten rechts", "Abajo derecha", "Bas droite"},
    {"Left", "左", "Links", "Izquierda", "Gauche"},
    {"Center", "中央", "Mitte", "Centro", "Centre"},
    {"Right", "右", "Rechts", "Derecha", "Droite"},
    {"Top", "上", "Oben", "Arriba", "Haut"},
    {"Bottom", "下", "Unten", "Abajo", "Bas"},
    {"Above", "上", "Oben", "Arriba", "Au-dessus"},
    {"Below", "下", "Darunter", "Debajo", "En dessous"},
    {"Start", "開始", "Start", "Inicio", "Début"},
    {"End", "終端", "Ende", "Final", "Fin"},
    {"Horizontal", "横", "Horizontal", "Horizontal", "Horizontal"},
    {"Vertical", "縦", "Vertikal", "Vertical", "Vertical"},
    {"Horiz", "横", "Horiz.", "Horiz.", "Horiz."},
    {"Vert", "縦", "Vert.", "Vert.", "Vert."},
    {"Relative", "相対", "Relativ", "Relativo", "Relatif"},
    {"Relative to Text", "テキスト基準", "Relativ zum Text", "Relativo al texto", "Relatif au texte"},
    {"Independent", "独立", "Unabhängig", "Independiente", "Indépendant"},
    {"Gauge→Text", "ゲージ→文字", "Anzeige → Text", "Medidor → Texto", "Jauge → Texte"},
    {"Text→Gauge", "文字→ゲージ", "Text → Anzeige", "Texto → Medidor", "Texte → Jauge"},
    {"Gauge → Text", "ゲージ→文字", "Anzeige → Text", "Medidor → Texto", "Jauge → Texte"},
    {"Text → Gauge", "文字→ゲージ", "Text → Anzeige", "Texto → Medidor", "Texte → Jauge"},
    {"Position Mode", "位置モード", "Positionsmodus", "Modo de posición", "Mode de position"},
    {"Pos Mode", "位置モード", "Positionsmodus", "Modo de posición", "Mode de position"},
    {"Gauge Side", "ゲージ側", "Anzeigeseite", "Lado del medidor", "Côté de la jauge"},
    {"Text Side", "文字側", "Textseite", "Lado del texto", "Côté texte"},
    {"Gauge Anchor", "ゲージ基準", "Anzeigen-Anker", "Ancla del medidor", "Ancrage jauge"},
    {"Gauge X", "ゲージ X", "Anzeige X", "Medidor X", "Jauge X"},
    {"Gauge Y", "ゲージ Y", "Anzeige Y", "Indicador Y", "Jauge Y"},
    {"Text Offset X", "文字オフセット X", "Textversatz X", "Desplazamiento del texto X", "Décalage texte X"},
    {"Text Offset Y", "文字オフセット Y", "Textversatz Y", "Desplazamiento Y del texto", "Décalage Y du texte"},
    {"Text Ofs X", "文字Ofs X", "Text-Ofs X", "Despl. texto X", "Décal. texte X"},
    {"Text Ofs Y", "文字Ofs Y", "Text-Ofs Y", "Ofs texto Y", "Ofs texte Y"},
    {"Prefix", "接頭辞", "Präfix", "Prefijo", "Préfixe"},
    {"Suffix", "接尾辞", "Suffix", "Sufijo", "Suffixe"},
    {"Align", "整列", "Ausrichtung", "Alineación", "Alignement"},
    {"Align X", "整列 X", "Ausrichtung X", "Alineación X", "Alignement X"},
    {"Align Y", "整列 Y", "Ausrichtung Y", "Alineación Y", "Alignement Y"},
    {"Orientation", "向き", "Ausrichtung", "Orientación", "Orientation"},
    {"Orient", "向き", "Ausrichtung", "Orientación", "Orientation"},
    {"Width", "幅", "Breite", "Ancho", "Largeur"},
    {"Height", "高さ", "Höhe", "Altura", "Hauteur"},
    {"Icon Height", "アイコン高さ", "Symbolhöhe", "Altura de icono", "Hauteur icône"},
    {"Icon Position", "アイコン位置", "Symbolposition", "Posición del icono", "Position de l'icône"},
    {"Pos Anchor", "位置基準", "Positionsanker", "Ancla de posición", "Ancre de position"},
    {"Pos X", "位置 X", "Pos X", "Pos X", "Pos X"},
    {"Pos Y", "位置 Y", "Pos Y", "Pos Y", "Pos Y"},
    {"Mode", "モード", "Modus", "Modo", "Mode"},
    {"Layout", "レイアウト", "Layout", "Diseño", "Disposition"},
    {"Weapon Layout", "武器レイアウト", "Waffenlayout", "Diseño de armas", "Disposition des armes"},
    {"Standard", "標準", "Standard", "Estándar", "Standard"},
    {"Alternative", "代替", "Alternative", "Alternativa", "Alternative"},
    {"Label Position", "ラベル位置", "Beschriftungsposition", "Posición de etiqueta", "Position étiquette"},
    {"Label Pos", "ラベル位置", "Label-Position", "Posición de etiqueta", "Position de l'étiquette"},
    {"Label Offset X", "ラベルオフセット X", "Label-Versatz X", "Desplazamiento de etiqueta X", "Décalage étiquette X"},
    {"Label Offset Y", "ラベルオフセット Y", "Label-Versatz Y", "Desplazamiento Y de etiqueta", "Décalage Y de l'étiquette"},
    {"Label Ofs X", "ラベルOfs X", "Label-Ofs X", "Despl. etiqueta X", "Décal. libellé X"},
    {"Label Ofs Y", "ラベルOfs Y", "Beschriftungs-Ofs Y", "Ofs etiqueta Y", "Ofs étiquette Y"},
    {"Label: Points", "ラベル: ポイント", "Label: Punkte", "Etiqueta: Puntos", "Étiquette : Points"},
    {"Label: Octoliths", "ラベル: オクトリス", "Label: Oktolithe", "Etiqueta: Octolitos", "Libellé : Octolites"},
    {"Label: Lives", "ラベル: ライフ", "Label: Leben", "Etiqueta: Vidas", "Libellé : Vies"},
    {"Label: Ring Time", "ラベル: リング時間", "Label: Ringzeit", "Etiqueta: Tiempo de anillo", "Libellé : Temps d'anneau"},
    {"Label: Prime Time", "ラベル: プライム時間", "Beschriftung: Prime Time", "Etiqueta: Prime Time", "Étiquette : Prime Time"},
    {"Battle", "バトル", "Kampf", "Batalla", "Combat"},
    {"Bounty", "バウンティ", "Kopfgeld", "Recompensa", "Prime"},
    {"Survival", "サバイバル", "Survival", "Supervivencia", "Survie"},
    {"Defender", "ディフェンダー", "Verteidiger", "Defensor", "Défenseur"},
    {"Prime", "プライム", "Prime", "Prime", "Prime"},
    {"Label", "ラベル", "Label", "Etiqueta", "Étiquette"},
    {"Value", "値", "Wert", "Valor", "Valeur"},
    {"Separator", "区切り", "Trennzeichen", "Separador", "Séparateur"},
    {"Slash", "スラッシュ", "Schlag", "Corte", "Entaille"},
    {"Goal", "目標", "Ziel", "Objetivo", "Objectif"},
    {"Label Color", "ラベル色", "Labelfarbe", "Color de etiqueta", "Couleur de l'étiquette"},
    {"Label Color: Overall", "ラベル色: 全体", "Labelfarbe: Gesamt", "Color de etiqueta: General", "Couleur étiquette : Global"},
    {"Value Color", "値の色", "Wertfarbe", "Color del valor", "Couleur de la valeur"},
    {"Value Color: Overall", "値の色: 全体", "Wertfarbe: Gesamt", "Color del valor: General", "Couleur valeur : Global"},
    {"Sep Color", "区切り色", "Trennfarbe", "Color separador", "Couleur séparateur"},
    {"Sep Color: Overall", "区切り色: 全体", "Trennfarbe: Gesamt", "Color de separación: General", "Couleur de séparation : Global"},
    {"Goal Color", "目標色", "Zielfarbe", "Color de objetivo", "Couleur objectif"},
    {"Goal Color: Overall", "目標色: 全体", "Zielfarbe: Gesamt", "Color de objetivo: General", "Couleur d'objectif : Global"},
    {"Ordinal", "序数", "Ordinal", "Ordinal", "Ordinal"},
    {"Text", "文字", "Text", "Texto", "Texte"},
    {"Color Overlay", "色オーバーレイ", "Farbüberlagerung", "Superposición de color", "Superposition de couleur"},
    {"Use Hunter Color", "ハンター色を使用", "Hunter-Farbe verwenden", "Usar color del cazador", "Utiliser la couleur du chasseur"},
    {"Radar Color", "レーダー色", "Radarfarbe", "Color del radar", "Couleur du radar"},
    {"Display Size", "表示サイズ", "Anzeigegröße", "Tamaño de visualización", "Taille d'affichage"},
    {"Dst Size", "表示サイズ", "Zielgröße", "Tamaño destino", "Taille cible"},
    {"Dst X", "表示 X", "Ziel X", "Dest. X", "Dest. X"},
    {"Dst Y", "表示 Y", "Ziel Y", "Dest. Y", "Dest. Y"},
    {"Source Radius", "ソース半径", "Quellradius", "Radio de origen", "Rayon source"},
    {"Src Radius", "ソース半径", "Quell-Radius", "Radio origen", "Rayon source"},
    {"Corner Radius", "角丸半径", "Eckenradius", "Radio de esquina", "Rayon d'angle"},
    {"Padding", "余白", "Abstand", "Relleno", "Marge"},
    {"Spacing", "間隔", "Abstand", "Espaciado", "Espacement"},
    {"Not Owned Opacity", "未所持の不透明度", "Deckkraft (nicht im Besitz)", "Opacidad (no obtenido)", "Opacité (non possédé)"},
    {"Highlight", "ハイライト", "Hervorhebung", "Resaltado", "Surbrillance"},
    {"Highlight Current Weapon", "現在の武器をハイライト", "Aktuelle Waffe hervorheben", "Resaltar arma actual", "Mettre en évidence l'arme actuelle"},
    {"Highlight Color", "ハイライト色", "Hervorhebungsfarbe", "Color de resaltado", "Couleur de surbrillance"},
    {"Highlight Opacity", "ハイライト不透明度", "Hervorhebungsdeckkraft", "Opacidad del resaltado", "Opacité de surbrillance"},
    {"Highlight Thickness", "ハイライト太さ", "Hervorhebungsstärke", "Grosor del resaltado", "Épaisseur de surbrillance"},
    {"Highlight Padding", "ハイライト余白", "Hervorhebungs-Abstand", "Relleno del resaltado", "Marge surbrillance"},
    {"Highlight Corner Radius", "ハイライト角丸", "Hervorhebungs-Eckenradius", "Radio de esquina de resaltado", "Rayon d'angle surbrillance"},
    {"Highlight Offset Left", "ハイライト左オフセット", "Hervorhebung Versatz links", "Desplazamiento izquierdo de resaltado", "Décalage gauche de surbrillance"},
    {"Highlight Offset Right", "ハイライト右オフセット", "Hervorhebungsversatz rechts", "Desplazamiento del resaltado a la derecha", "Décalage surbrillance à droite"},
    {"Highlight Offset Top", "ハイライト上オフセット", "Hervorhebungsversatz oben", "Desplazamiento superior del resaltado", "Décalage haut de surbrillance"},
    {"Highlight Offset Bottom", "ハイライト下オフセット", "Hervorhebungs-Offset unten", "Desplazamiento inferior del resaltado", "Décalage bas surbrillance"},
    {"Size Offset Left", "サイズ左オフセット", "Größenversatz links", "Desplazamiento de tamaño izquierdo", "Décalage taille gauche"},
    {"Size Offset Right", "サイズ右オフセット", "Größenversatz rechts", "Desplazamiento derecho de tamaño", "Décalage droit de taille"},
    {"Size Offset Top", "サイズ上オフセット", "Größenversatz oben", "Desplazamiento de tamaño arriba", "Décalage taille en haut"},
    {"Size Offset Bottom", "サイズ下オフセット", "Größenversatz unten", "Desplazamiento inferior de tamaño", "Décalage bas de taille"},
    {"Hl Opacity", "HL不透明度", "HL-Deckkraft", "Opacidad HL", "Opacité HL"},
    {"Hl Thickness", "HL太さ", "HL-Dicke", "Grosor HL", "Épaisseur HL"},
    {"Hl Padding", "HL余白", "HL-Abstand", "Relleno HL", "Marge HL"},
    {"Hl Corner Radius", "HL角丸", "HL-Eckenradius", "Radio de esquina HL", "Rayon coin HL"},
    {"Hl Ofs Left", "HL左Ofs", "HL-Versatz links", "Despl. HL izquierda", "Décalage HL gauche"},
    {"Hl Ofs Right", "HL右Ofs", "HL-Ofs rechts", "Despl. HL derecha", "Décal. HL droite"},
    {"Hl Ofs Top", "HL上Ofs", "HL-Ofs oben", "Ofs HL superior", "Ofs HL haut"},
    {"Hl Ofs Bottom", "HL下Ofs", "HL-Versatz unten", "Despl. inf. HL", "Décal. bas HL"},

    // HUD element and subsection names
    {"HP", "HP", "HP", "HP", "HP"},
    {"HP Number Position", "HP数値位置", "HP-Zahlenposition", "Posición del número de HP", "Position du nombre de PV"},
    {"HP Label Color By Value", "HPラベルの色 (値連動)", "HP-Label-Farbe nach Wert", "Color de etiqueta HP según valor", "Couleur libellé HP selon valeur"},
    {"HP Outline", "HPのアウトライン", "HP-Umriss", "Contorno HP", "Contour HP"},
    {"HP Gauge", "HPゲージ", "HP-Anzeige", "Indicador de HP", "Jauge de PV"},
    {"HP Gauge Color By Value", "HPゲージの色 (値連動)", "HP-Anzeigefarbe nach Wert", "Color del medidor de HP según valor", "Couleur jauge HP selon la valeur"},
    {"HP Gauge Outline", "HPゲージのアウトライン", "HP-Anzeigen-Kontur", "Contorno del medidor de HP", "Contour de la jauge de PV"},
    {"Ammo", "弾薬", "Munition", "Munición", "Munitions"},
    {"Ammo Number Position", "弾薬数値位置", "Munitionszahl-Position", "Posición del número de munición", "Position nombre munitions"},
    {"Ammo Label Color By Value", "弾薬ラベルの色 (値連動)", "Munitionslabel-Farbe nach Wert", "Color de etiqueta de munición según valor", "Couleur d'étiquette munitions selon la valeur"},
    {"Ammo Outline", "弾薬のアウトライン", "Munitionskontur", "Contorno de munición", "Contour des munitions"},
    {"Ammo Gauge", "弾薬ゲージ", "Munitionsanzeige", "Medidor de munición", "Jauge de munitions"},
    {"Ammo Gauge Color By Value", "弾薬ゲージの色 (値連動)", "Munitionsanzeige-Farbe nach Wert", "Color del medidor de munición según valor", "Couleur jauge munitions selon valeur"},
    {"Ammo Gauge Outline", "弾薬ゲージのアウトライン", "Munitionsanzeige-Umriss", "Contorno del medidor de munición", "Contour jauge munitions"},
    {"Weapon/Ammo", "武器/弾薬", "Waffe/Munition", "Arma/Munición", "Arme/Munitions"},
    {"Weapon Icon", "武器アイコン", "Waffensymbol", "Icono de arma", "Icône d'arme"},
    {"Weapon Icon Outline", "武器アイコンのアウトライン", "Waffensymbol-Kontur", "Contorno del icono de arma", "Contour de l'icône d'arme"},
    {"Weapon Icon Color Overlay", "武器アイコンの色オーバーレイ", "Waffensymbol-Farboverlay", "Superposición de color del icono de arma", "Superposition couleur icône d'arme"},
    {"Weapon Inventory", "武器インベントリ", "Waffeninventar", "Inventario de armas", "Inventaire d'armes"},
    {"Weapon Inventory Highlight", "武器インベントリのハイライト", "Waffen-Inventar-Hervorhebung", "Resaltado del inventario de armas", "Surbrillance de l'inventaire d'armes"},
    {"Weapon Inventory Outline", "武器インベントリのアウトライン", "Kontur Waffeninventar", "Contorno del inventario de armas", "Contour de l'inventaire d'armes"},
    {"Weapon Inventory Icon Outline", "武器インベントリのアイコンのアウトライン", "Waffeninventar-Symbol-Kontur", "Contorno del icono del inventario de armas", "Contour de l'icône d'inventaire d'armes"},
    {"Match Status", "試合情報", "Spielstand", "Estado de partida", "État du match"},
    {"Score", "スコア", "Punktestand", "Puntuación", "Score"},
    {"Score Labels", "スコアラベル", "Punkte-Labels", "Etiquetas de puntuación", "Étiquettes de score"},
    {"Score Colors", "スコアの色", "Punktefarben", "Colores de puntuación", "Couleurs de score"},
    {"Score Outline", "スコアのアウトライン", "Punkte-Kontur", "Contorno de puntuación", "Contour du score"},
    {"Rank / Time", "順位 / 時間", "Rang / Zeit", "Rango / Tiempo", "Rang / Temps"},
    {"Rank", "順位", "Rang", "Rango", "Rang"},
    {"Rank Outline", "順位のアウトライン", "Rang-Umriss", "Contorno de rango", "Contour du rang"},
    {"Time Left", "残り時間", "Verbleibende Zeit", "Tiempo restante", "Temps restant"},
    {"Time Left Outline", "残り時間のアウトライン", "Restzeit-Kontur", "Contorno de tiempo restante", "Contour du temps restant"},
    {"Time Limit", "制限時間", "Zeitlimit", "Límite de tiempo", "Limite de temps"},
    {"Time Limit Outline", "制限時間のアウトライン", "Zeitlimit-Umriss", "Contorno de límite de tiempo", "Contour limite de temps"},
    {"Bomb", "ボム", "Bombe", "Bomba", "Bombe"},
    {"Bomb Left", "残りボム", "Bomben übrig", "Bombas restantes", "Bombes restantes"},
    {"Bomb Left Outline", "残りボムのアウトライン", "Restbomben-Kontur", "Contorno de bombas restantes", "Contour des bombes restantes"},
    {"Bomb Icon", "ボムアイコン", "Bomben-Symbol", "Icono de bomba", "Icône bombe"},
    {"Bomb Icon Outline", "ボムアイコンのアウトライン", "Bomben-Symbol-Umriss", "Contorno del icono de bomba", "Contour icône bombe"},
    {"Radar", "レーダー", "Radar", "Radar", "Radar"},
    {"Radar Settings", "レーダー設定", "Radar-Einstellungen", "Ajustes del radar", "Paramètres du radar"},
    {"Radar Outline", "レーダーのアウトライン", "Radar-Kontur", "Contorno del radar", "Contour du radar"},
    {"Frame Outline", "フレームのアウトライン", "Rahmenkontur", "Contorno del marco", "Contour du cadre"},
    {"Crosshair", "照準", "Fadenkreuz", "Retícula", "Réticule"},
    {"Wpn\\nIcon", "武器\\nアイコン", "Wpn\nSymbol", "Arma\nIcono", "Arme\nIcône"},
    {"Bmb\\nIcon", "ボム\\nアイコン", "Bomben-\nSymbol", "Icono\nde bomba", "Icône\nbombe"},
    {"Wpn\\nInventory", "武器\\n一覧", "Wpn\\nInventar", "Wpn\\nInventario", "Wpn\\nInventaire"},
    {"WPN", "武器", "WPN", "ARM", "ARM"},
    {"BMB", "ボム", "BMB", "BMB", "BMB"},
    {"points", "得点", "Punkte", "puntos", "points"},
    {"Bombs", "ボム", "Bomben", "Bombas", "Bombes"},

    // OSD color labels
    {"Enable OSD Color Patch", "OSD色パッチを有効化", "OSD-Farbpatch aktivieren", "Activar parche de color OSD", "Activer le patch de couleur OSD"},
    {"Global Color", "全体色", "Globale Farbe", "Color global", "Couleur globale"},
    {"Use Global Color for All", "すべてに全体色を使用", "Globale Farbe für alle verwenden", "Usar color global para todo", "Utiliser la couleur globale pour tout"},
    {"Enable Separate Color", "個別色を有効化", "Separate Farbe aktivieren", "Activar color separado", "Activer une couleur séparée"},
    {"Color (Default: Red)", "色 (既定: 赤)", "Farbe (Standard: Rot)", "Color (predeterminado: rojo)", "Couleur (par défaut : rouge)"},
    {"Node Stolen (H211)", "ノード奪取 (H211)", "Knoten gestohlen (H211)", "Nodo robado (H211)", "Nœud volé (H211)"},
    {"Lost Lives", "ライフ喪失", "Verlorene Leben", "Vidas perdidas", "Vies perdues"},
    {"Kill / Death", "キル / デス", "Kill / Death", "Kill / Death", "Kill / Death"},
    {"Return to Base", "基地へ戻れ", "Zur Basis zurück", "Volver a la base", "Retour à la base"},
    {"No Ammo", "弾薬なし", "Keine Munition", "Sin munición", "Plus de munitions"},
    {"Coward Detect", "臆病者検出", "Feigheitserkennung", "Detección de cobardía", "Détection de lâcheté"},
    {"Acquiring Node", "ノード取得中", "Knoten wird erobert", "Capturando nodo", "Acquisition de nœud"},
    {"Turret", "タレット", "Geschütz", "Torreta", "Tourelle"},
    {"Octo Reset", "オクトリスリセット", "Octo-Reset", "Reinicio Octo", "Réinitialisation Octo"},
    {"Octo Drop", "オクトリスドロップ", "Oktolith-Drop", "Caída de octolito", "Chute d'octolithe"},
    {"Octo Condition", "オクトリス条件", "Octo-Bedingung", "Condición Octo", "Condition Octo"},
    {"Octo Missing", "オクトリス未所持", "Oktolith fehlt", "Octólito ausente", "Octolithe manquant"},
    {"Slot: Kill / Death  [flags=0x02]", "スロット: キル / デス [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot: Kill / Death  [flags=0x02]", "Slot : Kill / Death  [flags=0x02]"},
    {"Slot: Node Capture  [flags=0x11]", "スロット: ノード取得 [flags=0x11]", "Slot: Knotenerfassung  [flags=0x11]", "Ranura: Captura de nodo  [flags=0x11]", "Emplacement : Capture de nœud  [flags=0x11]"},
    {"Slot: Objective     [flags=0x01]", "スロット: 目標 [flags=0x01]", "Slot: Ziel     [flags=0x01]", "Ranura: Objetivo     [flags=0x01]", "Emplacement : Objectif     [flags=0x01]"},
    {"Slot: System / Misc [flags=0x00]", "スロット: システム/その他 [flags=0x00]", "Slot: System / Sonstiges [flags=0x00]", "Ranura: Sistema / Varios [flags=0x00]", "Emplacement : Système / Divers [flags=0x00]"},
    // OSD slot description labels (multi-line)
    {"Applied once on settings close to currently displayed messages (flags=0x02).\\nNew messages use the 'Kill / Death' literal color above.", "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x02)。\\n新しいメッセージは上の「キル / デス」個別色を使用します。", "Einmal beim Schließen der Einstellungen auf aktuell angezeigte Nachrichten angewendet (flags=0x02).\nNeue Nachrichten verwenden die obige Farbe für „Kill / Death“.", "Se aplica una vez al cerrar los ajustes a los mensajes mostrados (flags=0x02).\nLos mensajes nuevos usan el color literal «Kill / Death» de arriba.", "Appliqué une fois à la fermeture des paramètres aux messages affichés (flags=0x02).\nLes nouveaux messages utilisent la couleur littérale « Kill / Death » ci-dessus."},
    {"Applied once on settings close to currently displayed messages (flags=0x11).\\nNew messages use 'Acquiring Node' or 'Node Stolen' literal colors above.", "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x11)。\\n新しいメッセージは上の「ノード取得中」または「ノード奪取」の個別色を使用します。", "Einmalig beim Schließen der Einstellungen auf aktuell angezeigte Nachrichten angewendet (flags=0x11).\\nNeue Nachrichten verwenden die obigen Farben für „Acquiring Node“ oder „Node Stolen“.", "Se aplica una vez al cerrar los ajustes a los mensajes mostrados (flags=0x11).\\nLos mensajes nuevos usan los colores literales «Acquiring Node» o «Node Stolen» de arriba.", "Appliqué une fois à la fermeture des réglages aux messages affichés (flags=0x11).\\nLes nouveaux messages utilisent les couleurs littérales « Acquiring Node » ou « Node Stolen » ci-dessus."},
    {"Applied once on settings close to currently displayed messages (flags=0x01).\\nNew messages use their individual literal colors above (No Ammo / Return to Base / Octo ...).", "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x01)。\\n新しいメッセージはそれぞれの個別色を使用します (弾薬なし / 基地へ戻れ / オクト系 ...)。", "Einmalig beim Schließen der Einstellungen auf aktuell angezeigte Nachrichten angewendet (flags=0x01).\\nNeue Nachrichten verwenden ihre jeweiligen Farben oben (No Ammo / Return to Base / Octo ...).", "Se aplica una vez al cerrar los ajustes a los mensajes mostrados (flags=0x01).\\nLos mensajes nuevos usan sus colores literales individuales de arriba (No Ammo / Return to Base / Octo ...).", "Appliqué une fois à la fermeture des réglages aux messages affichés (flags=0x01).\\nLes nouveaux messages utilisent leurs couleurs littérales individuelles ci-dessus (No Ammo / Return to Base / Octo ...)."},
    {"Applied once on settings close to currently displayed messages (flags=0x00).\\nNew messages use their individual literal colors above (Lost Lives / Coward Detect / Turret ...).\\nNote: HEADSHOT! (H228) is flags=0x00, not 0x02.", "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x00)。\\n新しいメッセージはそれぞれの個別色を使用します (ライフ喪失 / 臆病者検出 / タレット ...)。\\n注: HEADSHOT! (H228) は flags=0x00 で、0x02 ではありません。", "Wird einmal beim Schließen der Einstellungen auf aktuell angezeigte Nachrichten angewendet (flags=0x00).\nNeue Nachrichten verwenden ihre individuellen Farben oben (Verlorene Leben / Feigling erkannt / Turm ...).\nHinweis: HEADSHOT! (H228) ist flags=0x00, nicht 0x02.", "Se aplica una vez al cerrar los ajustes a los mensajes mostrados actualmente (flags=0x00).\nLos mensajes nuevos usan sus colores literales individuales arriba (Vidas perdidas / Cobarde detectado / Torreta ...).\nNota: HEADSHOT! (H228) es flags=0x00, no 0x02.", "Appliqué une fois à la fermeture des paramètres aux messages actuellement affichés (flags=0x00).\nLes nouveaux messages utilisent leurs couleurs littérales individuelles ci-dessus (Vies perdues / Lâche détecté / Tourelle ...).\nNote : HEADSHOT! (H228) est flags=0x00, pas 0x02."},
    // OSD slot color labels
    {"Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)", "色  (YOU KILLED / KILLED YOU / 5キル / プライムハンター / 味方)", "Farbe  (DU HAST GETÖTET / HAT DICH GETÖTET / 5-Kill / Prime Hunter / Teamkamerad)", "Color  (TÚ MATASTE / TE MATÓ / 5 bajas / prime hunter / compañero)", "Couleur  (VOUS AVEZ TUÉ / VOUS A TUÉ / 5 kills / prime hunter / coéquipier)"},
    {"Color  (acquiring node / node stolen H211)", "色  (ノード取得中 / ノード奪取 H211)", "Farbe  (Knoten wird erobert / Knoten gestohlen H211)", "Color  (capturando nodo / nodo robado H211)", "Couleur  (acquisition de nœud / nœud volé H211)"},
    {"Color  (AMMO DEPLETED / return to base / bounty / octolith events)", "色  (AMMO DEPLETED / 基地へ戻れ / バウンティ / オクトリスイベント)", "Farbe  (AMMO DEPLETED / Rückkehr zur Basis / Kopfgeld / Oktolith-Ereignisse)", "Color  (AMMO DEPLETED / volver a base / recompensa / eventos de octolito)", "Couleur  (AMMO DEPLETED / retour à la base / prime / événements octolithe)"},
    {"Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)", "色  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / タレット)", "Farbe  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / Turm)", "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / torreta)", "Couleur  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / tourelle)"},

    // Color presets and weapon labels
    {"White", "白", "Weiß", "Blanco", "Blanc"},
    {"Green", "緑", "Grün", "Verde", "Vert"},
    {"Yellow Green", "黄緑", "Gelbgrün", "Verde amarillento", "Vert jaune"},
    {"Green Yellow", "緑黄", "Grüngelb", "Verde amarillo", "Jaune vert"},
    {"Yellow", "黄", "Gelb", "Amarillo", "Jaune"},
    {"Pure Cyan", "純シアン", "Reines Cyan", "Cian puro", "Cyan pur"},
    {"Hud Cyan", "HUDシアン", "HUD-Cyan", "HUD cian", "HUD cyan"},
    {"Pink", "ピンク", "Rosa", "Rosa", "Rose"},
    {"Red", "赤", "Rot", "Rojo", "Rouge"},
    {"Orange", "オレンジ", "Orange", "Naranja", "Orange"},
    {"Samus Hud", "サムスHUD", "Samus-HUD", "HUD de Samus", "HUD Samus"},
    {"Samus Hud Outline", "サムスHUDのアウトライン", "Samus-HUD-Kontur", "Contorno del HUD de Samus", "Contour du HUD de Samus"},
    {"Kanden Hud", "カンデンHUD", "Kanden-HUD", "HUD de Kanden", "HUD Kanden"},
    {"Spire Hud", "スパイアHUD", "Spire-HUD", "HUD Spire", "HUD Spire"},
    {"Spire Hud Outline", "スパイアHUDのアウトライン", "Spire-HUD-Umriss", "Contorno HUD Spire", "Contour HUD Spire"},
    {"Trace Hud", "トレースHUD", "Trace-HUD", "HUD de Trace", "HUD Trace"},
    {"Noxus Hud", "ノクサスHUD", "Noxus-HUD", "HUD de Noxus", "HUD de Noxus"},
    {"Noxus Hud Outline", "ノクサスHUDのアウトライン", "Noxus-HUD-Kontur", "Contorno del HUD de Noxus", "Contour HUD Noxus"},
    {"Sylux Hud", "サイラックスHUD", "Sylux-HUD", "HUD Sylux", "HUD Sylux"},
    {"Sylux Crosshair", "サイラックス照準", "Sylux-Fadenkreuz", "Retícula Sylux", "Réticule Sylux"},
    {"Weavel Hud", "ウィーヴェルHUD", "Weavel-HUD", "HUD de Weavel", "HUD Weavel"},
    {"Weavel Hud Outline", "ウィーヴェルHUDのアウトライン", "Weavel-HUD-Kontur", "Contorno del HUD de Weavel", "Contour du HUD de Weavel"},
    {"Avium Purple", "アヴィウム紫", "Avium-Lila", "Púrpura Avium", "Violet Avium"},
    {"OSD Bright Green", "OSD明るい緑", "OSD Hellgrün", "OSD verde brillante", "OSD vert vif"},
    {"OSD No-Ammo Red", "OSD弾薬なし赤", "OSD Keine-Munition Rot", "OSD sin munición rojo", "OSD plus de munitions rouge"},
    {"Power Beam", "パワービーム", "Power Beam", "Power Beam", "Power Beam"},
    {"Volt Driver", "ボルトドライバー", "Volt Driver", "Volt Driver", "Volt Driver"},
    {"VoltDriver", "ボルトドライバー", "VoltDriver", "VoltDriver", "VoltDriver"},
    {"Missile", "ミサイル", "Rakete", "Misil", "Missile"},
    {"Battle Hammer", "バトルハンマー", "Battlehammer", "Battlehammer", "Battlehammer"},
    {"Battlehammer", "バトルハンマー", "Battlehammer", "Battlehammer", "Battlehammer"},
    {"Imperialist", "インペリアリスト", "Imperialist", "Imperialist", "Imperialist"},
    {"Judicator", "ジュディケイター", "Judicator", "Judicator", "Judicator"},
    {"Magmaul", "マグモール", "Magmaul", "Magmaul", "Magmaul"},
    {"Shock Coil", "ショックコイル", "Shock Coil", "Shock Coil", "Shock Coil"},
    {"ShockCoil", "ショックコイル", "ShockCoil", "ShockCoil", "ShockCoil"},
    {"Omega Cannon", "オメガキャノン", "Omega-Kanone", "Cañón Omega", "Canon Omega"},
    {"PB Color", "PB色", "PB-Farbe", "Color PB", "Couleur PB"},
    {"VD Color", "VD色", "VD-Farbe", "Color VD", "Couleur VD"},
    {"MSL Color", "MSL色", "MSL-Farbe", "Color MSL", "Couleur MSL"},
    {"BH Color", "BH色", "BH-Farbe", "Color BH", "Couleur BH"},
    {"IMP Color", "IMP色", "IMP-Farbe", "Color IMP", "Couleur IMP"},
    {"JUD Color", "JUD色", "JUD-Farbe", "Color JUD", "Couleur JUD"},
    {"MAG Color", "MAG色", "MAG-Farbe", "Color MAG", "Couleur MAG"},
    {"SCL Color", "SCL色", "SCL-Farbe", "Color SCL", "Couleur SCL"},
    {"OC Color", "OC色", "OC-Farbe", "Color OC", "Couleur OC"},

    // Font weights
    {"Thin", "極細", "Dünn", "Fino", "Fin"},
    {"Extra Light", "特細", "Extra Light", "Extra Light", "Extra Light"},
    {"Light", "細字", "Leicht", "Ligera", "Léger"},
    {"Normal", "標準", "Standard", "Estándar", "Standard"},
    {"Medium", "中太", "Mittel", "Medio", "Moyen"},
    {"Semi Bold", "やや太字", "Halbfett", "Seminegrita", "Demi-gras"},
    {"Bold", "太字", "Fett", "Negrita", "Gras"},
    {"Extra Bold", "極太", "Extra fett", "Extra negrita", "Extra gras"},
    {"Black", "黒太", "Schwarz", "Negro", "Noir"},

    // Ramp labels
    {"Number of Colors", "色数", "Anzahl der Farben", "Número de colores", "Nombre de couleurs"},
    {"Threshold 1 (%)", "しきい値 1 (%)", "Schwellenwert 1 (%)", "Umbral 1 (%)", "Seuil 1 (%)"},
    {"Threshold 2 (%)", "しきい値 2 (%)", "Schwellwert 2 (%)", "Umbral 2 (%)", "Seuil 2 (%)"},
    {"Threshold 3 (%)", "しきい値 3 (%)", "Schwellenwert 3 (%)", "Umbral 3 (%)", "Seuil 3 (%)"},
    {"Threshold 4 (%)", "しきい値 4 (%)", "Schwellenwert 4 (%)", "Umbral 4 (%)", "Seuil 4 (%)"},
    {"Threshold 5 (%)", "しきい値 5 (%)", "Schwellwert 5 (%)", "Umbral 5 (%)", "Seuil 5 (%)"},
    {"Threshold 6 (%)", "しきい値 6 (%)", "Schwellenwert 6 (%)", "Umbral 6 (%)", "Seuil 6 (%)"},
    {"Color 1", "色 1", "Farbe 1", "Color 1", "Couleur 1"},
    {"Color 2", "色 2", "Farbe 2", "Color 2", "Couleur 2"},
    {"Color 3", "色 3", "Farbe 3", "Color 3", "Couleur 3"},
    {"Color 4", "色 4", "Farbe 4", "Color 4", "Couleur 4"},
    {"Color 5", "色 5", "Farbe 5", "Color 5", "Couleur 5"},
    {"Color 6", "色 6", "Farbe 6", "Color 6", "Couleur 6"},

    // Custom HUD code status
    {"Generate sharable TOML for the current Custom HUD settings, or paste TOML below to apply it.", "現在のカスタムHUD設定から共有用TOMLを生成するか、下にTOMLを貼り付けて適用します。", "Teilbares TOML für die aktuellen benutzerdefinierten HUD-Einstellungen generieren oder TOML unten einfügen, um es anzuwenden.", "Genera TOML compartible para los ajustes HUD personalizados actuales, o pega TOML abajo para aplicarlo.", "Générez un TOML partageable pour les paramètres HUD personnalisés actuels, ou collez le TOML ci-dessous pour l'appliquer."},
    {"Output refreshed from the current Custom HUD settings.", "現在のカスタムHUD設定から出力を更新しました。", "Ausgabe aus den aktuellen Custom-HUD-Einstellungen aktualisiert.", "Salida actualizada desde los ajustes actuales del HUD personalizado.", "Sortie actualisée à partir des paramètres HUD personnalisés actuels."},
    {"Custom HUD code copied to the clipboard.", "カスタムHUDコードをクリップボードにコピーしました。", "Benutzerdefinierter HUD-Code in die Zwischenablage kopiert.", "Código de HUD personalizado copiado al portapapeles.", "Code HUD personnalisé copié dans le presse-papiers."},
    {"Output copied into the input box.", "出力を入力欄へコピーしました。", "Ausgabe in das Eingabefeld kopiert.", "Salida copiada al cuadro de entrada.", "Sortie copiée dans la zone de saisie."},
    {"Custom HUD code applied to the dialog.", "カスタムHUDコードをダイアログへ適用しました。", "Custom HUD-Code auf den Dialog angewendet.", "Código de HUD personalizado aplicado al diálogo.", "Code HUD personnalisé appliqué à la boîte de dialogue."},
    {"Paste Custom HUD TOML into the input box first.", "先にカスタムHUD TOMLを入力欄へ貼り付けてください。", "Füge zuerst benutzerdefiniertes HUD-TOML in das Eingabefeld ein.", "Pega primero el TOML HUD personalizado en el cuadro de entrada.", "Collez d'abord le TOML HUD personnalisé dans la zone de saisie."},
    {"The pasted Custom HUD code is not a TOML table.", "貼り付けられたカスタムHUDコードはTOMLテーブルではありません。", "Der eingefügte Custom-HUD-Code ist keine TOML-Tabelle.", "El código HUD personalizado pegado no es una tabla TOML.", "Le code HUD personnalisé collé n'est pas une table TOML."},
    {"Failed to load Custom HUD code: %1", "カスタムHUDコードの読み込みに失敗しました: %1", "Benutzerdefinierter HUD-Code konnte nicht geladen werden: %1", "Error al cargar el código de HUD personalizado: %1", "Échec du chargement du code HUD personnalisé : %1"},

    // Developer input-method UI
    {"Use New Method for Weapon Change", "武器変更に新方式を使う", "Neue Methode für Waffenwechsel verwenden", "Usar método nuevo para cambio de arma", "Utiliser la nouvelle méthode pour le changement d'arme"},
    {"Use New Method for Biped Fire", "二足時の射撃に新方式を使う", "Neue Methode für Biped-Feuer verwenden", "Usar nuevo método para disparo Biped", "Utiliser la nouvelle méthode pour le tir Biped"},
    {"Use New Method for Alt-Form Transform", "トランスフォーム変形に新方式を使う", "Neue Methode für Alt-Form-Transformation verwenden", "Usar nuevo método para transformación Alt-Form", "Utiliser la nouvelle méthode pour transformation Alt-Form"},
    {"Use New Method for Zoom", "ズームに新方式を使う", "Neue Zoom-Methode verwenden", "Usar nuevo método de zoom", "Utiliser la nouvelle méthode de zoom"},
    {"Use New Method 2 for Zoom", "ズームに新方式2を使う", "Neue Methode 2 für Zoom verwenden", "Usar método nuevo 2 para el zoom", "Utiliser la nouvelle méthode 2 pour le zoom"},

#include "MelonPrimeLocalizationMelondsDialogs.inc"
};

constexpr ObjectTextTranslation kObjectTextTranslations[] = {
    {
        "metroidMphSensitvityLabel2",
        R"((精密なエイムのため、可能なら1以下にしてください。推奨範囲は -3〜0 です。ただし低すぎるとエイム時のHUD揺れが大きくなります。この値はゲーム内感度に対する相対値なので、0は感度ゼロではなく、1より低いだけです。この設定はMPHのゲーム内感度を上書きするため、ゲーム内で感度を変更しても効果はありません。))",
        R"((Für präzises Zielen nach Möglichkeit auf 1 oder darunter setzen. Empfohlener Bereich: -3 bis 0. Zu niedrige Werte vergrößern das HUD-Wackeln beim Zielen. Dieser Wert ist relativ zur ingame-Empfindlichkeit; 0 bedeutet nicht null Empfindlichkeit, sondern nur niedriger als 1. Diese Einstellung überschreibt die MPH-Empfindlichkeit im Spiel; Änderungen ingame haben keine Wirkung.))",
        R"((Para puntería precisa, usa 1 o menos si es posible. Rango recomendado: -3 a 0. Valores muy bajos aumentan el temblor del HUD al apuntar. Este valor es relativo a la sensibilidad en el juego; 0 no es sensibilidad cero, solo menor que 1. Este ajuste sobrescribe la sensibilidad de MPH en el juego; cambiarla en el juego no surte efecto.))",
        R"((Pour une visée précise, utilisez 1 ou moins si possible. Plage recommandée : -3 à 0. Une valeur trop basse accentue le tremblement du HUD en visée. Cette valeur est relative à la sensibilité en jeu ; 0 n'est pas une sensibilité nulle, juste inférieure à 1. Ce réglage remplace la sensibilité MPH en jeu ; la modifier en jeu n'a aucun effet.))"
    },
    {
        "metroidAimYAxisScaleLabel2",
        "(1.5147 = MPH標準、1.9429 = X/Y角速度一致 [オプション])",
        "(1.5147 = MPH-Standard, 1.9429 = X/Y-Winkelgeschwindigkeit gleich [Optional])",
        "(1.5147 = estándar MPH, 1.9429 = velocidad angular X/Y igual [opcional])",
        "(1.5147 = standard MPH, 1.9429 = vitesse angulaire X/Y identique [option])"
    },
    {
        "metroidAimAdjustLabel",
        R"(<html><head/><body><p><b>入力しきい値 (デッドゾーン + スナップ)</b> (推奨: 0.01 [既定] または 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → そのまま (a=0: オフ = 1未満をすべて無視、スナップなし)<br/>エイムが思った通りに動かない場合は、この値を下げてみてください。</p></body></html>)",
        R"(<html><head/><body><p><b>Eingabeschwellenwert (Totzone + Snap)</b> (empfohlen: 0.01 [Standard] oder 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → unverändert (a=0: Aus = alles unter 1 ignorieren, kein Snap)<br/>Wenn sich die Zielbewegung nicht wie erwartet anfühlt, versuche diesen Wert zu senken.</p></body></html>)",
        R"(<html><head/><body><p><b>Umbral de entrada (zona muerta + snap)</b> (recomendado: 0.01 [predeterminado] o 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → sin cambios (a=0: desactivado = ignorar todo bajo 1, sin snap)<br/>Si la puntería no se comporta como esperas, prueba a bajar este valor.</p></body></html>)",
        R"(<html><head/><body><p><b>Seuil d'entrée (zone morte + snap)</b> (recommandé : 0.01 [par défaut] ou 0.5) : |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → inchangé (a=0 : désactivé = ignorer tout en dessous de 1, pas de snap)<br/>Si la visée ne réagit pas comme prévu, essayez de baisser cette valeur.</p></body></html>)"
    },
    {
        "lblMetroidLowLatencyAimDesc",
        "即時同期は低遅延ARM9フックで現在の照準を目標の照準へ同期し、照準基準を再構築します。MoonLikeエイムは小さなエイム移動を即時反映し、大きなジャンプだけ最大ステップ付きで追従します。MPHのエイム補間無効化が必要です。",
        R"(Sofortige Synchronisation synchronisiert die aktuelle Visierung über einen latenzarmen ARM9-Hook mit dem Ziel und baut die Visierungsbasis neu auf. MoonLike-Ziel spiegelt kleine Bewegungen sofort wider und folgt großen Sprüngen nur mit maximaler Schrittweite. Deaktivierung der MPH-Zielglättung erforderlich.)",
        R"(La sincronización inmediata sincroniza la mira actual con la objetivo mediante un hook ARM9 de baja latencia y reconstruye la base de puntería. MoonLike Aim refleja al instante movimientos pequeños y sigue saltos grandes solo con paso máximo. Requiere desactivar el suavizado de puntería de MPH.)",
        R"(La synchronisation immédiate aligne la visée actuelle sur la cible via un hook ARM9 à faible latence et reconstruit la base de visée. MoonLike Aim reflète immédiatement les petits mouvements et ne suit les grands sauts qu'avec un pas maximal. Nécessite de désactiver le lissage de visée MPH.)"
    },
    {
        "lblMetroidZoomAimScaleDesc",
        "ゲーム本来のズーム状態が有効な間だけ適用します。100%で通常のマウス感度、100%未満でズーム中のエイムが遅くなり、100%超で速くなります。",
        R"(Gilt nur, solange der native Zoom-Zustand des Spiels aktiv ist. 100 % = normale Mausempfindlichkeit, unter 100 % wird das Zielen beim Zoomen langsamer, über 100 % schneller.)",
        R"(Solo se aplica mientras el zoom nativo del juego está activo. 100 % = sensibilidad normal del ratón; por debajo de 100 % la puntería al hacer zoom es más lenta; por encima, más rápida.)",
        R"(S'applique uniquement tant que le zoom natif du jeu est actif. 100 % = sensibilité souris normale ; en dessous de 100 %, la visée au zoom est plus lente ; au-dessus, plus rapide.)"
    },
    {
        "lblMetroidNativeAimHookModeDesc",
        "PostFold書き込みはタッチ入力処理の後でフックし、spec108=0 (サムス/カンデン/ノクサス/スパイア) を含むすべてのトランスフォームをカバーします。開発者ビルド専用です。",
        R"(PostFold-Schreibzugriff hookt nach der Touch-Eingabeverarbeitung und deckt alle Transformationen ab, einschließlich spec108=0 (Samus/Kanden/Noxus/Spire). Nur für Entwickler-Builds.)",
        R"(La escritura PostFold engancha tras el procesamiento táctil y cubre todas las transformaciones, incluido spec108=0 (Samus/Kanden/Noxus/Spire). Solo en compilaciones de desarrollador.)",
        R"(L'écriture PostFold s'accroche après le traitement tactile et couvre toutes les transformations, y compris spec108=0 (Samus/Kanden/Noxus/Spire). Réservé aux builds développeur.)"
    },
    {
        "lblMetroidDirectAltFormTransformDesc",
        "新方式は短いネイティブ入力ゲートをゲーム本来の変形要求処理へリダイレクトします。旧方式は従来のタッチ/メニュー模擬による変形処理を使います。",
        R"(Die neue Methode leitet ein kurzes natives Eingabe-Gate zur ingame-Transformationsanforderung um. Die alte Methode nutzt die bisherige Touch-/Menü-Simulation.)",
        R"(El método nuevo redirige una breve compuerta de entrada nativa al procesamiento de transformación del juego. El método antiguo usa la simulación táctil/menú tradicional.)",
        R"(La nouvelle méthode redirige une courte porte d'entrée native vers le traitement de transformation du jeu. L'ancienne méthode utilise la simulation tactile/menu traditionnelle.)"
    },
    {
        "lblMetroidFixWifiBitsetDesc",
        R"(フレンド/ライバルの有効スロットを追跡する疑似64bitビットセットの不具合を修正します。\nJP1.0 / US1.0 / EU1.0ではスロット32〜59が正しく扱われず、一部のフレンド/ライバルがオンラインで見えなくなることがあります。\nJP1.1 / US1.1 / EU1.1 / KR1.0と同じ、バイト単位のビットセット処理に置き換えます。\n(JP1.0 / US1.0 / EU1.0のみ。他のROMバージョンには影響しません))",
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
(JP1.0 / US1.0 / EU1.0 uniquement. N'affecte pas les autres versions de ROM.))"
    },
    {
        "lblMetroidFixShadowFreezeDesc",
        "アイスウェーブの最後の命中/ミス角度判定を、元の横方向範囲チェックは保ったまま完全な3D判定へ置き換えて、シャドウフリーズ問題を修正します。",
        R"(Ersetzt die letzte Treffer-/Fehlwinkelprüfung von Ice Wave durch eine vollständige 3D-Prüfung bei beibehaltener horizontaler Reichweitenkontrolle und behebt das Shadow-Freeze-Problem.)",
        R"(Sustituye la comprobación final de ángulo de acierto/fallo de Ice Wave por una evaluación 3D completa, manteniendo el control horizontal original, y corrige el problema de shadow freeze.)",
        R"(Remplace la dernière vérification d'angle touché/raté d'Ice Wave par une évaluation 3D complète tout en conservant le contrôle horizontal d'origine, corrigeant le problème de shadow freeze.)"
    },
    {
        "lblMetroidFixNoxusBladePersistenceDesc",
        R"(ノクサスのヴォーサイズ/ブレード攻撃が死亡とリスポーン後もダメージを与え続ける不具合を修正します。ブレード攻撃中にノクサスが死亡するとAlt攻撃タイマーがクリアされず、復活後もブレードの当たり判定が残ります。この修正では死亡した瞬間にタイマーをクリアします。)",
        R"(Behebt einen Fehler, bei dem Noxus' Vorsaize-/Klingenangriff nach Tod und Respawn weiter Schaden verursacht. Stirbt Noxus während eines Klingenangriffs, wird der Alt-Angriffs-Timer nicht gelöscht und die Trefferzone bleibt nach der Wiederbelebung. Dieser Fix löscht den Timer im Moment des Todes.)",
        R"(Corrige un fallo en el que el ataque de cuchilla/Vorsaize de Noxus sigue dañando tras morir y reaparecer. Si Noxus muere durante el ataque de cuchilla, el temporizador de ataque Alt no se borra y la hitbox persiste tras revivir. Este arreglo borra el temporizador en el instante de la muerte.)",
        R"(Corrige un défaut où l'attaque lame/Vorsaize de Noxus continue d'infliger des dégâts après la mort et le respawn. Si Noxus meurt pendant l'attaque lame, le minuteur d'attaque Alt n'est pas effacé et la hitbox persiste après la résurrection. Ce correctif efface le minuteur au moment de la mort.)"
    },
    {
        "lblMetroidFixNoxusBladePersistenceWarning",
        "この修正はまだ不安定です。特に韓国版では、場合によってブレードが残り続けることがあります。",
        "Dieser Fix ist noch instabil. Besonders in der koreanischen Version kann die Klinge unter Umständen bestehen bleiben.",
        "Esta corrección aún es inestable. Especialmente en la versión coreana, la cuchilla puede persistir en algunos casos.",
        "Ce correctif est encore instable. Surtout sur la version coréenne, la lame peut parfois persister."
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
</body></html>)"
    },
    {
        "lblMetroidShowHeadshotOnlineDesc",
        "Wi-Fi/オンライン対戦中でも、単独のヘッドショット通知を強制的に表示します。",
        "Erzwingt die Anzeige separater Kopfschuss-Benachrichtigungen auch während Wi-Fi-/Online-Matches.",
        "Fuerza la visualización de notificaciones de headshot independientes también en partidas Wi-Fi/en línea.",
        "Force l'affichage de notifications de headshot distinctes même en match Wi-Fi/en ligne."
    },
    {
        "lblMetroidShowEnemyHpMeterOnlineDesc",
        "HP情報は基本的に更新されないため信頼性は高くありません。誰に当てたかを見る大まかな目安として使えます。",
        R"(HP-Infos werden grundsätzlich selten aktualisiert, daher ist die Zuverlässigkeit begrenzt. Als grobe Orientierung, wer getroffen wurde.)",
        R"(La información de HP rara vez se actualiza, así que no es muy fiable. Sirve como referencia aproximada de a quién has impactado.)",
        "Les infos de PV sont rarement mises à jour, donc peu fiables. Utile comme indication grossière de qui a été touché."
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
</body></html>)"
    },
    {
        "lblMetroidDisablePickingUpSpecificItemsDesc",
        R"(有効にすると、選択したパワーアップは取得されて消えますが、プレイヤー側の効果はスキップされます。ダブルダメージ、クローク、デスオルトのタイマー、フラグ、HUD、サウンド、視覚効果は適用されません。この設定が影響するのは自分とボットだけで、オンラインの相手は変更されません。)",
        R"(Wenn aktiviert, werden ausgewählte Power-Ups aufgesammelt und verschwinden, aber die Spielereffekte werden übersprungen. Doppelschaden, Cloak, Death Alt-Timer, Flags, HUD, Sound und visuelle Effekte werden nicht angewendet. Betrifft nur dich und Bots, nicht Online-Gegner.)",
        R"(Al activarlo, los power-ups seleccionados se recogen y desaparecen, pero se omiten los efectos en el jugador. No se aplican doble daño, cloak, temporizador Death Alt, flags, HUD, sonido ni efectos visuales. Solo afecta a ti y a bots, no a rivales en línea.)",
        R"(Si activé, les power-ups sélectionnés sont ramassés et disparaissent, mais leurs effets sur le joueur sont ignorés. Double dégâts, cloak, minuteur Death Alt, flags, HUD, son et effets visuels ne s'appliquent pas. N'affecte que vous et les bots, pas les adversaires en ligne.)"
    },
    {
        "lblMetroidJoy2KeySupportDesc",
        R"(JoyToKey、Steam Input、reWASDなど、外部のコントローラー→キーボード/マウス変換ツール向けの互換モードです。割り当てたキーが離した後も押されっぱなしになる場合に役立ちます。通常のキーボードとマウスを直接使う場合は、無効にすると入力遅延が少し減ることがあります。)",
        R"(Kompatibilitätsmodus für externe Controller-zu-Tastatur/Maus-Tools wie JoyToKey, Steam Input oder reWASD. Hilft, wenn zugewiesene Tasten nach Loslassen hängen bleiben. Bei direkter Tastatur-/Maunutzung kann Deaktivierung die Eingabeverzögerung leicht reduzieren.)",
        R"(Modo de compatibilidad para herramientas externas controlador→teclado/ratón como JoyToKey, Steam Input o reWASD. Útil si las teclas asignadas quedan pulsadas tras soltarlas. Con teclado y ratón directos, desactivarlo puede reducir ligeramente la latencia de entrada.)",
        R"(Mode de compatibilité pour les outils externes manette→clavier/souris comme JoyToKey, Steam Input ou reWASD. Utile si les touches assignées restent enfoncées après relâchement. Avec clavier/souris directs, le désactiver peut légèrement réduire la latence d'entrée.)"
    },
    {
        "labelMetroidScreenSyncDesc",
        R"(オフ: 同期なし (最小遅延ですが、表示がカクつくことがあります)。glFinish: 各フレームの描画完了を待つことで表示を滑らかにします。DwmFlush: Windowsコンポジターと同期して表示を滑らかにします (Windowsのみ)。画面がカクついたりちらついたりする場合は、glFinishまたはDwmFlushを試してください。早送り/スローモーション中は自動的に無効になります。)",
        R"(Aus: Keine Synchronisation (geringste Latenz, Anzeige kann ruckeln). glFinish: Glattere Anzeige durch Warten auf den Abschluss jedes Frames. DwmFlush: Synchronisation mit dem Windows-Compositor (nur Windows). Bei Ruckeln oder Flackern glFinish oder DwmFlush testen. Wird bei Vorspulen/Zeitlupe automatisch deaktiviert.)",
        R"(Desactivado: sin sincronización (mínima latencia, pero la imagen puede entrecortarse). glFinish: imagen más fluida esperando a que termine cada fotograma. DwmFlush: sincroniza con el compositor de Windows (solo Windows). Si hay tirones o parpadeos, prueba glFinish o DwmFlush. Se desactiva automáticamente en avance rápido/cámara lenta.)",
        R"(Désactivé : pas de synchronisation (latence minimale, affichage parfois saccadé). glFinish : affichage plus fluide en attendant la fin de chaque image. DwmFlush : synchronise avec le compositeur Windows (Windows uniquement). En cas de saccades ou scintillement, essayez glFinish ou DwmFlush. Désactivé automatiquement en avance rapide/ralenti.)"
    },
    {
        "labelInGameAspectRatioAutoDesc",
        "自動: 現在のアスペクト比設定に合わせてアスペクト比パッチを自動適用します (4:3 = オフ、5:3/16:9/21:9 = 自動適用、ウィンドウ = オフ)。",
        R"(Auto: Wendet automatisch einen Seitenverhältnis-Patch passend zur aktuellen Einstellung an (4:3 = Aus, 5:3/16:9/21:9 = Auto, Fenster = Aus).)",
        R"(Auto: aplica automáticamente un parche de relación de aspecto según el ajuste actual (4:3 = desactivado, 5:3/16:9/21:9 = auto, ventana = desactivado).)",
        R"(Auto : applique automatiquement un patch de ratio d'aspect selon le réglage actuel (4:3 = désactivé, 5:3/16:9/21:9 = auto, fenêtre = désactivé).)"
    },
    {
        "lblMetroidLowHpWarningDesc",
        "自動スケール: Low = 基準 ×0.75、Medium = 基準、High = 基準 ×1.25 (丸め)。0にすると実質的に警告を無効化します。範囲は0-255です。",
        R"(Auto-Skalierung: Low = Basis ×0,75, Medium = Basis, High = Basis ×1,25 (gerundet). 0 deaktiviert die Warnung praktisch. Bereich: 0–255.)",
        R"(Escala automática: Low = base ×0,75, Medium = base, High = base ×1,25 (redondeado). 0 desactiva la advertencia en la práctica. Rango: 0–255.)",
        R"(Échelle auto : Low = base ×0,75, Medium = base, High = base ×1,25 (arrondi). 0 désactive pratiquement l'avertissement. Plage : 0–255.)"
    },
    {
        "lblMetroidNativeAimRegisterInjectionDesc",
        R"(最小遅延のため、エイム呼び出し地点でフックします。二足状態と spec108=1 のトランスフォーム (トレース/サイラックス/ウィーヴェル) をカバーします。有効にすると、上のネイティブエイムデルタHook (PostFold書き込み) より優先されます。)",
        R"(Hookt am Zielaufruf-Punkt für minimale Latenz. Deckt Zweibeiner-Zustand und spec108=1-Transformationen (Trace/Sylux/Weavel) ab. Wenn aktiviert, hat Vorrang vor Native Aim Delta Hook (PostFold-Schreibzugriff) oben.)",
        R"(Engancha en el punto de llamada de puntería para mínima latencia. Cubre estado bípedo y transformaciones spec108=1 (Trace/Sylux/Weavel). Si está activo, tiene prioridad sobre Native Aim Delta Hook (escritura PostFold) de arriba.)",
        R"(S'accroche au point d'appel de visée pour une latence minimale. Couvre l'état bipède et les transformations spec108=1 (Trace/Sylux/Weavel). Si activé, prioritaire sur Native Aim Delta Hook (écriture PostFold) ci-dessus.)"
    },
    {
        "lblMetroidImmediateInputEdgeOverlayDesc",
        R"(押下/離上エッジをエミュレーター側で生成し、プレイヤーアクション処理前にMPH内部入力状態へ重ねます。射撃、ジャンプ、ズーム、移動をカバーします。ボタンマスクはプレイヤーの現在の操作プリセット割り当て表から読むため、すべてのプリセット (Touch R/L、Dual R/L) に自動対応します。)",
        R"(Erzeugt Drück-/Loslass-Kanten im Emulator und legt sie vor der Spieleraktionsverarbeitung über den MPH-internen Eingabestatus. Deckt Schießen, Springen, Zoomen und Bewegung ab. Button-Masken werden aus der aktuellen Steuerungs-Preset-Zuordnung gelesen; unterstützt alle Presets (Touch R/L, Dual R/L) automatisch.)",
        R"(Genera bordes de pulsación/suelta en el emulador y los superpone al estado de entrada interno de MPH antes del procesamiento de acciones del jugador. Cubre disparo, salto, zoom y movimiento. Lee las máscaras de botón de la asignación del preset actual; compatible con todos los presets (Touch R/L, Dual R/L) automáticamente.)",
        R"(Génère les fronts appui/relâchement côté émulateur et les superpose à l'état d'entrée interne MPH avant le traitement des actions joueur. Couvre tir, saut, zoom et déplacement. Lit les masques de boutons depuis le preset actuel ; compatible automatiquement avec tous les presets (Touch R/L, Dual R/L).)"
    },
    {
        "lblMetroidWeaponSwitchMethodDesc",
        "オンにすると、ARM9フック経由でゲーム本来の武器装備処理を使います。オフでは互換性テスト用に、従来のタッチ/メニュー模擬による武器切替を使います。",
        R"(Wenn aktiviert, nutzt der ARM9-Hook die native Waffenausrüstungslogik des Spiels. Wenn deaktiviert, wird für Kompatibilitätstests die bisherige Touch-/Menü-Simulation verwendet.)",
        R"(Activado: usa el hook ARM9 con el equipamiento de armas nativo del juego. Desactivado: usa la simulación táctil/menú tradicional para pruebas de compatibilidad.)",
        R"(Activé : le hook ARM9 utilise l'équipement d'armes natif du jeu. Désactivé : utilise la simulation tactile/menu traditionnelle pour les tests de compatibilité.)"
    },
    {
        "lblMetroidBipedFireMethodDesc",
        "オンにすると、ゲーム本来の二足射撃エッジフックで射撃入力ヘルパーの結果をtrueにし、元のクールダウン、弾薬、弾生成、HUD、SFX経路を自然に動かします。旧方式では従来のDS入力/即時入力エッジ合成による射撃経路を使います。",
        R"(Wenn aktiviert, setzt der native Zweibeiner-Schuss-Edge-Hook das Schuss-Eingabe-Hilfsresultat auf true und nutzt natürlich Cooldown, Munition, Projektilerzeugung, HUD und SFX-Pfade. Die alte Methode nutzt die bisherige DS-Eingabe-/Sofort-Edge-Synthese.)",
        R"(Activado: el hook de borde de disparo bípedo nativo pone el resultado del helper de disparo en true y activa naturalmente cooldown, munición, generación de proyectiles, HUD y SFX. El método antiguo usa la síntesis DS/entrada inmediata tradicional.)",
        R"(Activé : le hook de front de tir bipède natif met le résultat du helper de tir à true et active naturellement cooldown, munitions, génération de projectiles, HUD et SFX. L'ancienne méthode utilise la synthèse DS/entrée immédiate traditionnelle.)"
    },
    {
        "lblMetroidZoomMethodDesc",
        "新方式はゲーム内のズーム割り当て表を読むため、Touch/Dualプリセットごとに異なるDSボタンへズームを割り当てられます。旧方式より少し低遅延です。両方のチェックを外すと、従来の入力経路と同じく固定Rボタンを使う旧方式になります。",
        R"(Die neue Methode liest die ingame-Zoom-Zuordnungstabelle, sodass Zoom je nach Touch/Dual-Preset unterschiedlichen DS-Tasten zugewiesen wird. Etwas geringere Latenz als die alte Methode. Wenn beide deaktiviert sind, wird wie bisher der feste R-Knopf genutzt.)",
        R"(El método nuevo lee la tabla de asignación de zoom del juego, así el zoom se asigna a botones DS distintos según el preset Touch/Dual. Algo menos de latencia que el método antiguo. Con ambos desactivados, usa el botón R fijo como antes.)",
        R"(La nouvelle méthode lit la table d'affectation zoom en jeu, donc le zoom est assigné à des boutons DS différents selon le preset Touch/Dual. Légèrement moins de latence que l'ancienne méthode. Si les deux sont désactivés, utilise le bouton R fixe comme avant.)"
    },
    {
        "lblMetroidZoomMethod2Desc",
        "新方式2は、押すたびにゲーム本来のズーム状態を切り替えます。「ズームに新方式を使う」とは同時に使えません。",
        R"(Neue Methode 2 schaltet bei jedem Drücken den nativen Zoom-Zustand des Spiels um. Kann nicht gleichzeitig mit „Neue Methode für Zoom verwenden“ genutzt werden.)",
        R"(El método nuevo 2 alterna el estado de zoom nativo del juego en cada pulsación. No se puede usar junto con «Usar método nuevo para zoom».)",
        R"(La nouvelle méthode 2 bascule l'état de zoom natif du jeu à chaque appui. Ne peut pas être utilisée en même temps que « Utiliser la nouvelle méthode pour le zoom ».)"
    },
    {
        "btnClear",
        "クリア",
        "Löschen",
        "Borrar",
        "Effacer"
    },
};

QString TranslateExact(const QString& text)
{
    const MenuLangId lang = ActiveMenuLanguage();
    if (lang == MenuLangId::English)
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
    if (lang == MenuLangId::English)
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
        default:
            return QStringLiteral("インスタンス %1 の設定").arg(arg);
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
        default:
            return QStringLiteral("インスタンス %1 の割り当て").arg(arg);
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
        default:
            return QStringLiteral("インスタンス %1 のパス").arg(arg);
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
        default:
            return QStringLiteral("インスタンス %1 のバッテリー残量").arg(arg);
        }
    }

    if (text == QStringLiteral("(none)"))
    {
        switch (ActiveMenuLanguage()) {
        case MenuLangId::German: return QStringLiteral("(keine)");
        case MenuLangId::Spanish: return QStringLiteral("(ninguno)");
        case MenuLangId::French: return QStringLiteral("(aucun)");
        default: return QStringLiteral("(なし)");
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
        default:
            return QStringLiteral("ダイレクトモード (要 %1・イーサネット接続)").arg(middle);
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
        default:
            return text.left(nativeIdx) + QStringLiteral(" ネイティブ (") + text.mid(nativeIdx + 9);
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
        default:
            if (cameraText.contains(QStringLiteral(" (inner camera)")))
                cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (内側カメラ)"));
            if (cameraText.contains(QStringLiteral(" (outer camera)")))
                cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (外側カメラ)"));
            break;
        }
        if (cameraText != text)
            return cameraText;
    }

    struct PrefixPair {
        const char* en;
        const char* ja;
        const char* de;
        const char* es;
        const char* fr;
    };
    const PrefixPair dynamicPrefixes[] = {
        {"DS slot: ", "DSスロット: ", "DS-Slot: ", "Ranura DS: ", "Slot DS : "},
        {"GBA slot: ", "GBAスロット: ", "GBA-Slot: ", "Ranura GBA: ", "Slot GBA : "},
        {"Top ", "上画面 ", "Oberer Bildschirm ", "Pantalla superior ", "Écran du haut "},
        {"Bottom ", "下画面 ", "Unterer Bildschirm ", "Pantalla inferior ", "Écran du bas "},
    };
    for (const PrefixPair& prefix : dynamicPrefixes)
    {
        const QString enPrefix = QString::fromUtf8(prefix.en);
        if (!text.startsWith(enPrefix))
            continue;
        const char* localizedPrefix = prefix.en;
        switch (ActiveMenuLanguage()) {
        case MenuLangId::German: localizedPrefix = prefix.de; break;
        case MenuLangId::Spanish: localizedPrefix = prefix.es; break;
        case MenuLangId::French: localizedPrefix = prefix.fr; break;
        case MenuLangId::Japanese: localizedPrefix = prefix.ja; break;
        default: break;
        }
        return QString::fromUtf8(localizedPrefix) + Tr(text.mid(enPrefix.size()));
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
        switch (ActiveMenuLanguage()) {
        case MenuLangId::German:
            label->setText(QStringLiteral(
                "<html><head/><body>"
                "<p>Warnung: LAN erfordert eine latenzarme Netzwerkverbindung.</p>"
                "<p>Über VPN oder Tunnel funktioniert es möglicherweise nicht.</p>"
                "</body></html>"));
            break;
        case MenuLangId::Spanish:
            label->setText(QStringLiteral(
                "<html><head/><body>"
                "<p>Advertencia: LAN requiere una conexión de red de baja latencia.</p>"
                "<p>Puede no funcionar a través de VPN o túneles.</p>"
                "</body></html>"));
            break;
        case MenuLangId::French:
            label->setText(QStringLiteral(
                "<html><head/><body>"
                "<p>Avertissement : le LAN nécessite une connexion réseau à faible latence.</p>"
                "<p>Peut ne pas fonctionner via VPN ou tunnel.</p>"
                "</body></html>"));
            break;
        default:
            label->setText(QStringLiteral(
                "<html><head/><body>"
                "<p>警告: LANは低レイテンシのネットワーク接続が必要です。</p>"
                "<p>VPNやトンネル経由では動作しない可能性があります。</p>"
                "</body></html>"));
            break;
        }
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

    const QByteArray b0 = q0.toUtf8();
    const QByteArray b1 = q1.toUtf8();
    std::strncpy(line0, b0.constData(), 255);
    line0[255] = '\0';
    std::strncpy(line1, b1.constData(), 255);
    line1[255] = '\0';
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
    int x = 0;
    for (const QChar ch : qtext)
    {
        const unsigned int rgba = rainbow ? SplashOsdRainbowColor(static_cast<int>(rainbowinc))
                                        : (color | 0xFF000000u);
        const QColor mainColor(static_cast<QRgb>(rgba));
        const QColor shadowColor(0, 0, 0, 224);

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
