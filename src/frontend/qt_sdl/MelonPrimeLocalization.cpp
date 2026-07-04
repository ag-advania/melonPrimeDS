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
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTreeView>
#include <QVariant>

#include <utility>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace MelonPrime::UiText
{

namespace {

bool LanguageTagIsJapanese(const QString& tag)
{
    if (tag.isEmpty())
        return false;

    const QString lower = tag.toLower();
    return lower.startsWith(QStringLiteral("ja"));
}

#ifdef __APPLE__
bool ApplePreferredLanguagesContainJapanese()
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

            if (LanguageTagIsJapanese(QString::fromUtf8(buf))) {
                found = true;
                break;
            }
        }
    }

    CFRelease(value);
    return found;
}
#endif

bool DetectJapaneseSystemLocale()
{
    const QLocale sys = QLocale::system();
    if (sys.language() == QLocale::Japanese)
        return true;

    for (const QString& tag : sys.uiLanguages()) {
        if (LanguageTagIsJapanese(tag))
            return true;
    }

    if (LanguageTagIsJapanese(sys.name()))
        return true;

#ifdef __APPLE__
    // QLocale::system() can stay English when Info.plist pins
    // CFBundleDevelopmentRegion (QTBUG-72491). Read macOS prefs directly.
    if (ApplePreferredLanguagesContainJapanese())
        return true;
#endif

    for (const char* envName : {"LANG", "LC_ALL", "LC_MESSAGES", "LANGUAGE"}) {
        const QByteArray env = qgetenv(envName);
        if (LanguageTagIsJapanese(QString::fromLatin1(env)))
            return true;
    }

    return false;
}

} // namespace

bool IsJapaneseSystemLocale()
{
    static const bool isJapanese = DetectJapaneseSystemLocale();
    return isJapanese;
}

struct Translation
{
    const char* en;
    const char* ja;
};

struct ObjectTextTranslation
{
    const char* objectName;
    const char* ja;
};

constexpr Translation kTranslations[] = {
    // Common controls
    {"ON", "ON"},
    {"OFF", "OFF"},
    {"Off", "オフ"},
    {"Save", "保存"},
    {"Cancel", "キャンセル"},
    {"Reset", "リセット"},
    {"Generate", "生成"},
    {"Copy Output", "出力をコピー"},
    {"Copy Output to Input", "出力を入力へコピー"},
    {"Apply", "適用"},
    {"Browse…", "参照…"},
    {"Select HUD Font", "HUDフォントを選択"},
    {"Select HUD Font File", "HUDフォントファイルを選択"},
    {"Pick Color", "色を選択"},
    {"Pick a system font…", "システムフォントを選択…"},
    {"Crosshair Color", "照準の色"},
    {"Edit", "編集"},
    {"Chng", "変更"},
    {"OVR", "全体"},
    {"Preview", "プレビュー"},
    {"Preview ON", "プレビューON"},
    {"preview", "プレビュー"},
    {"Normal", "通常"},
    {"Zoomed (Scope)", "ズーム時 (スコープ)"},
    {"Scope reticle off", "スコープレティクル無効"},
    {"Text", "文字"},
    {"Auto", "自動"},
    {"Overall", "全体"},
    {"Custom", "カスタム"},
    {"Default (MPH)", "標準 (MPH)"},
    {"System Font", "システムフォント"},
    {"Font File", "フォントファイル"},
    {"Font files (*.ttf *.otf *.ttc);;All files (*)", "フォントファイル (*.ttf *.otf *.ttc);;すべてのファイル (*)"},
    {"Menu Language", "メニュー言語"},
    {"Japanese", "日本語"},
    {"English", "英語"},

    // Main menu bar
    {"File", "ファイル"},
    {"Open ROM...", "ROMを開く..."},
    {"Open recent", "最近使ったROM"},
    {"Boot firmware", "ファームウェアを起動"},
    {"DS slot", "DSスロット"},
    {"GBA slot", "GBAスロット"},
    {"Insert cart...", "カートリッジを挿入..."},
    {"Eject cart", "カートリッジを取り出す"},
    {"Insert ROM cart...", "ROMカートリッジを挿入..."},
    {"Insert add-on cart", "アドオンカートリッジを挿入"},
    {"Import savefile", "セーブファイルをインポート"},
    {"Save state", "ステートを保存"},
    {"Load state", "ステートを読み込み"},
    {"File...", "ファイル..."},
    {"Undo state load", "ステート読み込みを取り消す"},
    {"Open melonDS directory", "melonDSフォルダを開く"},
    {"Quit", "終了"},
    {"System", "システム"},
    {"Pause", "一時停止"},
    {"Stop", "停止"},
    {"Frame step", "フレーム送り"},
    {"Power management", "電源管理"},
    {"Date and time", "日付と時刻"},
    {"Enable cheats", "チートを有効化"},
    {"Setup cheat codes", "チートコード設定"},
    {"ROM info", "ROM情報"},
    {"RAM search", "RAM検索"},
    {"Manage DSi titles", "DSiタイトル管理"},
    {"Multiplayer", "マルチプレイ"},
    {"Launch new instance", "新しいインスタンスを起動"},
    {"Host LAN game", "LANゲームをホスト"},
    {"Join LAN game", "LANゲームに参加"},
    {"View", "表示"},
    {"Screen size", "画面サイズ"},
    {"Screen rotation", "画面回転"},
    {"Screen gap", "画面間隔"},
    {"Screen layout", "画面レイアウト"},
    {"Natural", "自然"},
    {"Vertical", "縦"},
    {"Horizontal", "横"},
    {"Hybrid", "ハイブリッド"},
    {"Swap screens", "上下画面を入れ替え"},
    {"Screen sizing", "画面の拡大方式"},
    {"Even", "均等"},
    {"Emphasize top", "上画面を重視"},
    {"Emphasize bottom", "下画面を重視"},
    {"Top only", "上画面のみ"},
    {"Bottom only", "下画面のみ"},
    {"Force integer scaling", "整数スケーリングを強制"},
    {"Aspect ratio", "アスペクト比"},
    {"Open new window", "新しいウィンドウを開く"},
    {"Screen filtering", "画面フィルタリング"},
    {"Show OSD", "OSDを表示"},
    {"Config", "設定"},
    {"Emu settings", "エミュレーター設定"},
    {"Preferences...", "環境設定..."},
    {"Input and hotkeys", "入力とホットキー"},
    {"Video settings", "映像設定"},
    {"Camera settings", "カメラ設定"},
    {"Audio settings", "音声設定"},
    {"Multiplayer settings", "マルチプレイ設定"},
    {"Wifi settings", "Wi-Fi設定"},
    {"Firmware settings", "ファームウェア設定"},
    {"Interface settings", "インターフェース設定"},
    {"Path settings", "パス設定"},
    {"Limit framerate", "フレームレート制限"},
    {"Audio sync", "音声同期"},
    {"MelonPrime", "MelonPrime"},
    {"Input settings", "操作設定"},
    {"Other settings", "MelonPrime設定"},
    {"MelonPrime settings", "MelonPrime設定"},
    {"Custom HUD settings", "カスタムHUD設定"},
    {"Disable SF (Shadow Freeze)", "SF (シャドウフリーズ) を無効化"},
    {"In-Game Top Screen Only", "ゲーム中は上画面のみ"},
    {"Help", "ヘルプ"},
    {"About...", "このアプリについて..."},
    {"Clear", "履歴を消去"},

    // Tabs and major sections
    {"MelonPrime Settings", "MelonPrime設定"},
    {"Controls", "操作"},
    {"Controls 2", "操作 2"},
    {"Custom HUD", "カスタムHUD"},
    {"Custom HUD Input/Output", "カスタムHUD 入出力"},
    {"Input", "入力"},
    {"Output", "出力"},
    {"INPUT SETTINGS", "入力設定"},
    {"INPUT METHOD", "入力方式"},
    {"SCREEN SYNC", "画面同期"},
    {"CURSOR CLIP SETTINGS", "カーソル制限"},
    {"IN-GAME APPLY", "ゲーム内反映"},
    {"IN-GAME ASPECT RATIO", "ゲーム内アスペクト比"},
    {"LOW HP WARNING", "低HP警告"},
    {"SENSITIVITY", "感度"},
    {"BUG FIXES", "不具合修正"},
    {"GAME FEATURE IMPROVEMENTS", "ゲーム機能改善"},
    {"DISABLE FEATURES", "機能無効化"},
    {"Power-Up Pickup Effects", "パワーアップ取得効果"},
    {"GAMEPLAY TOGGLES", "ゲームプレイ切替"},
    {"VIDEO QUALITY", "映像品質"},
    {"VOLUME", "音量"},
    {"LICENSE APPLY", "ライセンス反映"},
    {"DEVELOPER ONLY", "開発者向け"},
    {"DISABLE DEFAULT HUD", "標準HUDを非表示"},
    {"OUTLINE OVERRIDE", "アウトライン一括設定"},
    {"HUD SCALE", "HUDスケール"},
    {"HUD FONT", "HUDフォント"},
    {"CROSSHAIR", "照準"},
    {"HP / AMMO", "HP / 弾薬"},
    {"MATCH STATUS HUD", "試合情報HUD"},
    {"HUD RADAR", "HUDレーダー"},
    {"IN-GAME OSD COLOR", "ゲーム内OSD色"},

    // Hotkey page
    {"Keyboard mappings", "キーボード割り当て"},
    {"Keyboard && mouse mappings", "キーボード・マウス割り当て"},
    {"Joystick mappings", "ジョイスティック割り当て"},
    {"[Metroid] (W) Move Forward", "[Metroid] (W) 前進"},
    {"[Metroid] (S) Move Back", "[Metroid] (S) 後退"},
    {"[Metroid] (A) Move Left", "[Metroid] (A) 左移動"},
    {"[Metroid] (D) Move Right", "[Metroid] (D) 右移動"},
    {"[Metroid] (Mouse Left) Shoot/Scan", "[Metroid] (マウス左) 射撃/スキャン"},
    {"[Metroid] (V) Scan/Shoot, Map Zoom In", "[Metroid] (V) スキャン/射撃、マップ拡大"},
    {"[Metroid] (Mouse Right) Imperialist Zoom, Map Zoom Out, Morph Ball Boost", "[Metroid] (マウス右) インペリアリストズーム、マップ縮小、モーフボールブースト"},
    {"[Metroid] (Space) Jump", "[Metroid] (Space) ジャンプ"},
    {"[Metroid] (L. Ctrl) Transform", "[Metroid] (左Ctrl) 変形"},
    {"[Metroid] (Shift) Hold to Fast Morph Ball Boost", "[Metroid] (Shift) 長押しで高速モーフボールブースト"},
    {"[Metroid] (Mouse 5, Side Top) Weapon Beam", "[Metroid] (Mouse 5/サイド上) ビーム武器"},
    {"[Metroid] (Mouse 4, Side Bottom) Weapon Missile", "[Metroid] (Mouse 4/サイド下) ミサイル"},
    {"[Metroid] (1) Weapon 1. ShockCoil", "[Metroid] (1) 武器1: ショックコイル"},
    {"[Metroid] (2) Weapon 2. Magmaul", "[Metroid] (2) 武器2: マグモール"},
    {"[Metroid] (3) Weapon 3. Judicator", "[Metroid] (3) 武器3: ジュディケイター"},
    {"[Metroid] (4) Weapon 4. Imperialist", "[Metroid] (4) 武器4: インペリアリスト"},
    {"[Metroid] (5) Weapon 5. Battlehammer", "[Metroid] (5) 武器5: バトルハンマー"},
    {"[Metroid] (6) Weapon 6. VoltDriver", "[Metroid] (6) 武器6: ボルトドライバー"},
    {"[Metroid] (R) Affinity Weapon (Last used Weapon/Omega cannon)", "[Metroid] (R) 得意武器 (最後の武器/オメガキャノン)"},
    {"[Metroid] (Tab) Menu/Map", "[Metroid] (Tab) メニュー/マップ"},
    {"[Metroid] (PgUp) AimSensitivity Up", "[Metroid] (PgUp) エイム感度を上げる"},
    {"[Metroid] (PgDown) AimSensitivity Down", "[Metroid] (PgDown) エイム感度を下げる"},
    {"[Metroid] (J) Next Weapon in the sorted order", "[Metroid] (J) 次の武器"},
    {"[Metroid] (K) Previous Weapon in the sorted order", "[Metroid] (K) 前の武器"},
    {"[Metroid] (C) Scan Visor", "[Metroid] (C) スキャンバイザー"},
    {"[Metroid] (Z) UI Left (Adventure Left Arrow / Hunter License L)", "[Metroid] (Z) UI左 (アドベンチャー左/ライセンスL)"},
    {"[Metroid] (X) UI Right (Adventure Right Arrow / Hunter License R)", "[Metroid] (X) UI右 (アドベンチャー右/ライセンスR)"},
    {"[Metroid] (F) UI Ok", "[Metroid] (F) UI決定"},
    {"[Metroid] (G) UI Yes (Enter Starship)", "[Metroid] (G) UIはい (スターシップに入る)"},
    {"[Metroid] (H) UI No (Enter Starship)", "[Metroid] (H) UIいいえ (スターシップに入る)"},
    {"[Metroid] (Y) Weapon Check", "[Metroid] (Y) 武器確認"},

    // General Metroid settings
    {"MPH Sensitivity (default: -3)", "MPH感度 (既定: -3)"},
    {"Aim sensitivity (default: 63)", "エイム感度 (既定: 63)"},
    {"Aim Y-Axis Scale (default: 1.5147)", "エイムY軸スケール (既定: 1.5147)"},
    {"Mode", "モード"},
    {"Low-Latency Aim Mode", "低遅延エイム方式"},
    {"Instant Aim Follow", "即時エイム追従"},
    {"Instant Aim Follow (Developer Only)", "即時エイム追従（開発者専用）"},
    {"Immediate Sync", "即時同期"},
    {"MoonLike Aim", "MoonLikeエイム"},
    {"Enable SnapTap (Faster directional switching for smooth strafing — may slightly increase input delay)", "SnapTapを有効化 (ストレイフの方向切替を高速化。入力遅延が少し増える場合あり)"},
    {"Unlock All Hunters/Maps/SoundTest/Gallery (Change a setting in MPH and save to update the save data. Uncheck this option after saving)", "全ハンター/マップ/サウンドテスト/ギャラリーを解放 (MPH側で設定を変更して保存するとセーブに反映。保存後はオフ推奨)"},
    {"Set MPH audio settings to headphones.(recommended) (Change a setting in MPH and save to update the save data. Uncheck this option after saving)", "MPHの音声設定をヘッドホンにする (推奨。MPH側で設定を変更して保存するとセーブに反映。保存後はオフ推奨)"},
    {"Use DS Name (Resets in-game name and HL color):You can change the DS name from [File -> Boot firmware] or [Config -> Firmware Settings].", "DS本体名を使う (ゲーム内名とHL色をリセット): DS名は [File -> Boot firmware] または [Config -> Firmware Settings] で変更できます。"},
    {"Enable Joy2KeySupport (enable this if keys sometimes get stuck; slightly increases input delay)", "Joy2Keyサポートを有効化 (キーが押しっぱなしになる場合に使用。入力遅延が少し増えます)"},
    {"Enable Stylus Mode (Leave this unchecked unless you want to play with the stylus)", "スタイラスモードを有効化 (スタイラス操作で遊ぶ場合以外はオフ推奨)"},
    {"Disable MPH Aim Smoothing (Disables the in-game aim smoothing. Note: Sensitivity will be reduced to 25% in Stylus Mode)", "MPHのエイム補間を無効化 (ゲーム内のエイム補間を無効化。スタイラスモードでは感度が25%になります)"},
    {"Enable Aim Sub-pixel Accumulator (Carry fractional mouse movement across frames. Enable for smoother low-sensitivity aiming)", "エイムのサブピクセル蓄積を有効化 (小数のマウス移動を次フレームへ持ち越し。低感度エイムが滑らかになります)"},
    {"Scale aim sensitivity while zoomed", "ズーム中のエイム感度を倍率変更"},
    {"Zoom Aim Scale %", "ズーム時エイム倍率 %"},
    {"Enable Direct Alt-Form Transform", "直接トランスフォーム変形を有効化"},
    {"Enable Immediate Input Edge Overlay", "即時入力エッジ合成を有効化"},
    {"Enable Native Aim Delta Hook (PostFold Write)", "ネイティブエイムデルタHookを有効化 (PostFold書き込み)"},
    {"Enable Native Aim Register Injection", "ネイティブエイムのレジスタ注入を有効化"},
    {"Apply Input to Custom HUD", "入力をカスタムHUDに反映"},
    {"Screen Sync Mode — Default: Off", "画面同期方式 - 既定: オフ"},
    {"Screen Sync Mode", "画面同期方式"},
    {"Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete", "画面同期方式: オフ = 同期呼び出しなし、glFinish = GLコマンド完了まで待機"},
    {"When not in-game and the bottom screen is visible, confine the mouse cursor to the bottom screen area. Press ESC to release the cursor.", "ゲーム外で下画面が表示されているとき、マウスカーソルを下画面内に制限します。ESCで解除します。"},
    {"In-game only, temporarily force Screen Sizing to Top Only and Screen Layout to Natural. Outside of gameplay, restore the normal window settings.", "ゲーム中だけ一時的に Screen Sizing を Top Only、Screen Layout を Natural に強制します。ゲーム外では通常のウィンドウ設定に戻します。"},
    {"Enable In-Game Aspect Ratio", "ゲーム内アスペクト比を有効化"},
    {"Aspect Ratio Mode", "アスペクト比モード"},
    {"Auto (match Aspect Ratio)", "自動 (画面比率に合わせる)"},
    {"5:3 (3DS)", "5:3 (3DS)"},
    {"16:10 (3DS)", "16:10 (3DS)"},
    {"16:9", "16:9"},
    {"21:9", "21:9"},
    {"Use DS Firmware Language (EU-style Auto Patch)", "DSファームウェア言語を使う (EU版風の自動パッチ)"},
    {"Show Headshot Notification Online", "オンラインでヘッドショット通知を表示"},
    {"Show Enemy HP Meter Online", "オンラインで敵HPメーターを表示"},
    {"Fix Noxus Blade Persistence on Death", "ノクサスのブレード残留を修正"},
    {"Friend/Rival Wi-Fi Active Bitset Fix (v2)", "フレンド/ライバルWi-Fi有効ビット修正 (v2)"},
    {"Shadow Freeze Fix (Ice Wave full 3D angle check)", "シャドウフリーズ修正 (アイスウェーブの3D角度判定)"},
    {"Expand Stage Selection (Unlock Extra Stages)", "ステージ選択を拡張 (追加ステージ解放)"},
    {"Also unlock additional stages", "追加ステージも解放"},
    {"Disable Double Damage Multiplier", "ダブルダメージ倍率を無効化"},
    {"Damage Notify Purple", "ダメージ通知を紫にする"},
    {"Power-Ups: Pick Up With No Effect", "パワーアップ: 取得しても効果なし"},
    {"Double Damage", "ダブルダメージ"},
    {"Cloak", "クローク"},
    {"DEATH ALT", "デスオルト"},
    {"Reset sensitivity values", "感度を既定値に戻す"},
    {"Video quality: Low (High Performance)", "映像品質: 低 (高パフォーマンス)"},
    {"Video quality: High (Lower Performance)", "映像品質: 高 (低パフォーマンス)"},
    {"Video quality: High2 (Recommended. Best Performance)", "映像品質: 高2 (推奨。最高パフォーマンス)"},
    {"Apply SFX volume", "効果音音量を反映"},
    {"Apply music volume", "BGM音量を反映"},
    {"Apply the selected hunter to your license. (Renaming will update the save data)", "選択したハンターをライセンスに反映 (名前変更でセーブデータ更新)"},
    {"Apply the selected color to your license. (Renaming will update the save data)", "選択した色をライセンスに反映 (名前変更でセーブデータ更新)"},
    {"Select the hunter to apply.", "反映するハンターを選択します。"},
    {"Select the color to apply.", "反映する色を選択します。"},
    {"Red(JP,KR)", "赤 (JP,KR)"},
    {"Blue(US)", "青 (US)"},
    {"Green(EU)", "緑 (EU)"},
    {"Auto Scale — Base", "自動スケール - 基準"},
    {"Auto Scale (by Damage)", "自動スケール (ダメージ別)"},
    {"Disabled (vanilla 25)", "無効 (バニラ25)"},
    {"Fixed", "固定"},
    {"Fixed Threshold", "固定しきい値"},
    {"Per Damage (Low / Medium / High)", "ダメージ別 (低/中/高)"},
    {"Per Damage — Low", "ダメージ別 - 低"},
    {"Per Damage — Medium", "ダメージ別 - 中"},
    {"Per Damage — High", "ダメージ別 - 高"},
    {"Developer-only option. Currently disabled.", "開発者向けオプションです。現在は無効です。"},
    {"Developer-only option enabled in this build.", "このビルドでは開発者向けオプションが有効です。"},
    {"Developer-only option. Build with MELONPRIME_ENABLE_DEVELOPER_FEATURES to enable it.", "開発者向けオプションです。有効にするには MELONPRIME_ENABLE_DEVELOPER_FEATURES 付きでビルドしてください。"},
    {"Features in this section are still in development and are not ready for public release. They may or may not be released.", "このセクションの機能は開発中で、公開リリースの準備はまだできていません。今後公開されるとは限りません。"},
    {"Controls how the game's current aim direction follows the target aim direction.", "ゲーム内の現在の照準方向を、目標の照準方向へどう追従させるかを設定します。"},
    {"Checked: use the native ARM9 game function hook. Unchecked: use the older touch/menu simulation path.", "オン: ゲーム本来のARM9関数をフックして使います。オフ: 従来のタッチ/メニュー模擬処理を使います。"},
    {"Checked: inject a native fire edge inside the game's Biped fire update. Unchecked: use the older fixed input/overlay path.", "オン: ゲーム本来の二足射撃更新内へ射撃入力エッジを注入します。オフ: 従来の固定入力/オーバーレイ経路を使います。"},
    {"Checked: use the native ARM9 TransformRequest hook. Unchecked: use the older touch/menu simulation path.", "オン: ゲーム本来のARM9変形要求フックを使います。オフ: 従来のタッチ/メニュー模擬処理を使います。"},
    {"Checked: use the current in-game zoom binding from the player's control preset. Unchecked: use the older fixed R-button path.", "オン: 現在の操作プリセットにあるゲーム内ズーム割り当てを使います。オフ: 従来の固定Rボタン経路を使います。"},
    {"Checked: toggle native weapon zoom by calling the game's SetPlayerScopeZoom setter. Unchecked with New Method also off: use Legacy fixed R-button input.", "オン: ゲーム本来のズーム切替処理を呼んで武器ズームを切り替えます。新方式もオフの場合は、旧方式の固定Rボタン入力を使います。"},
    {"Setting Key: Metroid.Apply.SfxVolume (check to apply)", "設定キー: Metroid.Apply.SfxVolume (オンで反映)"},
    {"Setting Key: Metroid.Volume.SFX (0–9)", "設定キー: Metroid.Volume.SFX (0-9)"},
    {"Setting Key: Metroid.Apply.MusicVolume (check to apply)", "設定キー: Metroid.Apply.MusicVolume (オンで反映)"},
    {"Setting Key: Metroid.Volume.Music (0–9)", "設定キー: Metroid.Volume.Music (0-9)"},
    {"Enables applying the selected hunter to the license.", "選択したハンターをライセンスに反映できるようにします。"},
    {"Enables applying the selected color to the license.", "選択した色をライセンスに反映できるようにします。"},
    {"Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete, DwmFlush = wait for DWM compositor (Windows only)", "画面同期方式: オフ = 同期呼び出しなし、glFinish = GLコマンド完了まで待機、DwmFlush = DWMコンポジター待機 (Windowsのみ)"},
    {"Off: No sync (lowest latency, but the display may look choppy). glFinish: Smoother display by waiting for rendering to fully complete each frame. Automatically disabled during FastForward/SlowMo.", "オフ: 同期なし (最小遅延ですが、表示がカクつくことがあります)。glFinish: 各フレームの描画完了を待つことで表示を滑らかにします。早送り/スローモーション中は自動的に無効になります。"},
    {"Windows only. Applies only while the bottom screen is actually being drawn. Press ESC to release the cursor.", "Windowsのみ。下画面が実際に描画されている間だけ適用されます。ESCでカーソル制限を解除します。"},
    {"Does not overwrite your saved window layout settings. The override is applied only while Metroid Prime Hunters is in-game.", "保存済みのウィンドウレイアウト設定は上書きしません。Metroid Prime Huntersのゲーム中だけ一時的に適用されます。"},
    {"When playing with an Aspect Ratio other than 4:3 (Native), apply this to change the in-game 3D aspect ratio to match. Changes are applied at the start of each match.", "4:3 (ネイティブ) 以外の画面比率で遊ぶとき、ゲーム内3Dのアスペクト比も一致するように変更します。変更は各試合の開始時に適用されます。"},
    {"Changes the HP value at which the low-HP warning sound and warning HUD state trigger (vanilla: 25). Applied at the start of each match based on the current Damage setting.", "低HP警告音と警告HUD状態が発生するHP値を変更します (バニラ: 25)。現在のダメージ設定に応じて、各試合の開始時に適用されます。"},
    {"Edit HUD Layout", "HUD配置を編集"},
    {"Hide this dialog and enter the interactive HUD position editor", "このダイアログを隠して、対話式HUD配置エディターに入ります"},
    {"Enable Custom HUD (Replaces the in-game HUD with a custom overlay showing HP, ammo, weapon icons and crosshair)", "カスタムHUDを有効化 (ゲーム内HUDを、HP/弾薬/武器アイコン/照準のカスタム表示に置き換えます)"},
    {"Share your Custom HUD setup as TOML text, or paste TOML into the input area to apply it to the current dialog.", "カスタムHUD設定をTOMLテキストとして共有、または入力欄に貼り付けて現在のダイアログへ適用します。"},
    {"Press Generate to build sharable Custom HUD TOML.", "生成を押すと共有用のカスタムHUD TOMLを作成します。"},
    {"Paste Custom HUD TOML here, then press Apply Input.", "ここにカスタムHUD TOMLを貼り付けてから、入力を適用してください。"},

    // Custom HUD sections and properties
    {"— Common HUD —", "- 共通HUD -"},
    {"— Score Row (per mode) —", "- スコア行 (モード別) -"},
    {"Hide Helmet (Visor Mask)", "ヘルメット (バイザーマスク) を非表示"},
    {"Hide Ammo", "弾薬を非表示"},
    {"Hide Weapon Icon", "武器アイコンを非表示"},
    {"Hide HP", "HPを非表示"},
    {"Hide Crosshair", "照準を非表示"},
    {"Hide Bomb (Boost Ball kept)", "ボムを非表示 (ブーストボールは維持)"},
    {"Hide Score: Battle", "スコア非表示: バトル"},
    {"Hide Score: Survival", "スコア非表示: サバイバル"},
    {"Hide Score: Prime Hunter", "スコア非表示: プライムハンター"},
    {"Hide Score: Bounty", "スコア非表示: バウンティ"},
    {"Hide Score: Capture", "スコア非表示: キャプチャー"},
    {"Hide Score: Defender", "スコア非表示: ディフェンダー"},
    {"Hide Score: Node", "スコア非表示: ノード"},
    {"Text Scale (Base %)", "文字スケール (基準 %)"},
    {"Auto Scale Enable", "自動スケールを有効化"},
    {"Auto Scale Global Cap %", "自動スケール全体上限 %"},
    {"Auto Scale Text Cap %", "自動スケール文字上限 %"},
    {"Auto Scale Icon Cap %", "自動スケールアイコン上限 %"},
    {"Auto Scale Gauge Cap %", "自動スケールゲージ上限 %"},
    {"Auto Scale Crosshair Cap %", "自動スケール照準上限 %"},
    {"Font Source", "フォントソース"},
    {"Font Size (px)", "フォントサイズ (px)"},
    {"Font Weight", "フォントウェイト"},
    {"Italic", "イタリック"},
    {"Underline", "下線"},
    {"Strikethrough", "取り消し線"},
    {"Color", "色"},
    {"Scale %", "スケール %"},
    {"Zoom Stage", "ズーム段階"},
    {"Zoom Base Scale %", "ズーム時元照準スケール %"},
    {"Zoom Base Opacity %", "ズーム時元照準不透明度 %"},
    {"Zoom Opacity %", "ズーム時不透明度 %"},
    {"Zoom Scope Reticle", "ズームスコープ照準"},
    {"Zoom Scope", "ズームスコープ"},
    {"Scope Radius", "スコープ半径"},
    {"Scope Gap", "スコープ隙間"},
    {"Scope Thickness", "スコープ太さ"},
    {"Scope Thick.", "スコープ太さ"},
    {"Scope Center Dot", "スコープ中央ドット"},
    {"Scope Dot Size", "スコープドットサイズ"},
    {"Scope Dot Opacity %", "スコープドット不透明度 %"},
    {"Scope Opacity %", "スコープ不透明度 %"},
    {"Zoom Transition", "ズームトランジション"},
    {"Zoom Transition Speed %", "ズーム遷移速度 %"},
    {"Transition Speed %", "遷移速度 %"},
    {"Zoom Pulse Ring", "ズームパルスリング"},
    {"Pulse Ring", "パルスリング"},
    {"Zoom Pulse Strength %", "ズームパルス強度 %"},
    {"Pulse Strength %", "パルス強度 %"},
    {"Zoom Crosshair", "ズーム照準"},
    {"Transition Style", "トランジションスタイル"},
    {"Custom Scope Dot Color", "スコープドット色を個別指定"},
    {"Scope Dot Color", "スコープドット色"},
    {"Staged", "段階"},
    {"Fade", "フェード"},
    {"Glitch", "グリッチ"},
    {"Glitch2", "グリッチ2"},
    {"Snap", "スナップ"},
    {"Digital", "デジタル"},
    {"Pulse Wave", "パルス波"},
    {"Magic Circle", "魔法陣"},
    {"SF Movie", "SF映画"},
    {"Tactical Lock", "戦術ロック"},
    {"Sniper Optics", "スナイパー光学"},
    {"Drone LIDAR", "ドローンLIDAR"},
    {"Beam Charge", "ビームチャージ"},
    {"Zoom", "ズーム"},
    {"Outline", "アウトライン"},
    {"Outline Color", "アウトライン色"},
    {"Outline Opacity", "アウトライン不透明度"},
    {"Outline Thickness", "アウトライン太さ"},
    {"Outline Thick.", "アウトライン太さ"},
    {"Center Dot", "中央ドット"},
    {"Custom Dot Color", "ドット色を個別指定"},
    {"Dot Color", "ドット色"},
    {"Dot Shape", "ドット形状"},
    {"Scope Dot Shape", "スコープドット形状"},
    {"Square", "四角"},
    {"Circle", "丸"},
    {"Dot Opacity", "ドット不透明度"},
    {"Dot Thickness", "ドット太さ"},
    {"Dot Thick.", "ドット太さ"},
    {"T-Style", "Tスタイル"},
    {"Inner", "内側"},
    {"Outer", "外側"},
    {"Inner Lines", "内側ライン"},
    {"Outer Lines", "外側ライン"},
    {"Show", "表示"},
    {"Show Text", "文字を表示"},
    {"Show Number", "数値を表示"},
    {"Enable", "有効化"},
    {"Enable (Override All)", "有効化 (全体上書き)"},
    {"Opacity", "不透明度"},
    {"Length", "長さ"},
    {"Length X", "長さ X"},
    {"Length Y", "長さ Y"},
    {"Link XY", "XYを連動"},
    {"Thickness", "太さ"},
    {"Offset", "オフセット"},
    {"Offset X", "オフセット X"},
    {"Offset Y", "オフセット Y"},
    {"Anchor", "基準位置"},
    {"Top Left", "左上"},
    {"Top Center", "上中央"},
    {"Top Right", "右上"},
    {"Middle Left", "左中央"},
    {"Middle Center", "中央"},
    {"Middle Right", "右中央"},
    {"Bottom Left", "左下"},
    {"Bottom Center", "下中央"},
    {"Bottom Right", "右下"},
    {"Mid Left", "左中央"},
    {"Mid Center", "中央"},
    {"Mid Right", "右中央"},
    {"Bot Left", "左下"},
    {"Bot Center", "下中央"},
    {"Bot Right", "右下"},
    {"Left", "左"},
    {"Center", "中央"},
    {"Right", "右"},
    {"Top", "上"},
    {"Bottom", "下"},
    {"Above", "上"},
    {"Below", "下"},
    {"Start", "開始"},
    {"End", "終端"},
    {"Horizontal", "横"},
    {"Vertical", "縦"},
    {"Horiz", "横"},
    {"Vert", "縦"},
    {"Relative", "相対"},
    {"Relative to Text", "テキスト基準"},
    {"Independent", "独立"},
    {"Gauge→Text", "ゲージ→文字"},
    {"Text→Gauge", "文字→ゲージ"},
    {"Gauge → Text", "ゲージ→文字"},
    {"Text → Gauge", "文字→ゲージ"},
    {"Position Mode", "位置モード"},
    {"Pos Mode", "位置モード"},
    {"Gauge Side", "ゲージ側"},
    {"Text Side", "文字側"},
    {"Gauge Anchor", "ゲージ基準"},
    {"Gauge X", "ゲージ X"},
    {"Gauge Y", "ゲージ Y"},
    {"Text Offset X", "文字オフセット X"},
    {"Text Offset Y", "文字オフセット Y"},
    {"Text Ofs X", "文字Ofs X"},
    {"Text Ofs Y", "文字Ofs Y"},
    {"Prefix", "接頭辞"},
    {"Suffix", "接尾辞"},
    {"Align", "整列"},
    {"Align X", "整列 X"},
    {"Align Y", "整列 Y"},
    {"Orientation", "向き"},
    {"Orient", "向き"},
    {"Width", "幅"},
    {"Height", "高さ"},
    {"Icon Height", "アイコン高さ"},
    {"Icon Position", "アイコン位置"},
    {"Pos Anchor", "位置基準"},
    {"Pos X", "位置 X"},
    {"Pos Y", "位置 Y"},
    {"Mode", "モード"},
    {"Layout", "レイアウト"},
    {"Weapon Layout", "武器レイアウト"},
    {"Standard", "標準"},
    {"Alternative", "代替"},
    {"Label Position", "ラベル位置"},
    {"Label Pos", "ラベル位置"},
    {"Label Offset X", "ラベルオフセット X"},
    {"Label Offset Y", "ラベルオフセット Y"},
    {"Label Ofs X", "ラベルOfs X"},
    {"Label Ofs Y", "ラベルOfs Y"},
    {"Label: Points", "ラベル: ポイント"},
    {"Label: Octoliths", "ラベル: オクトリス"},
    {"Label: Lives", "ラベル: ライフ"},
    {"Label: Ring Time", "ラベル: リング時間"},
    {"Label: Prime Time", "ラベル: プライム時間"},
    {"Battle", "バトル"},
    {"Bounty", "バウンティ"},
    {"Survival", "サバイバル"},
    {"Defender", "ディフェンダー"},
    {"Prime", "プライム"},
    {"Label", "ラベル"},
    {"Value", "値"},
    {"Separator", "区切り"},
    {"Slash", "スラッシュ"},
    {"Goal", "目標"},
    {"Label Color", "ラベル色"},
    {"Label Color: Overall", "ラベル色: 全体"},
    {"Value Color", "値の色"},
    {"Value Color: Overall", "値の色: 全体"},
    {"Sep Color", "区切り色"},
    {"Sep Color: Overall", "区切り色: 全体"},
    {"Goal Color", "目標色"},
    {"Goal Color: Overall", "目標色: 全体"},
    {"Ordinal", "序数"},
    {"Text", "文字"},
    {"Color Overlay", "色オーバーレイ"},
    {"Use Hunter Color", "ハンター色を使用"},
    {"Radar Color", "レーダー色"},
    {"Display Size", "表示サイズ"},
    {"Dst Size", "表示サイズ"},
    {"Dst X", "表示 X"},
    {"Dst Y", "表示 Y"},
    {"Source Radius", "ソース半径"},
    {"Src Radius", "ソース半径"},
    {"Corner Radius", "角丸半径"},
    {"Padding", "余白"},
    {"Spacing", "間隔"},
    {"Not Owned Opacity", "未所持の不透明度"},
    {"Highlight", "ハイライト"},
    {"Highlight Current Weapon", "現在の武器をハイライト"},
    {"Highlight Color", "ハイライト色"},
    {"Highlight Opacity", "ハイライト不透明度"},
    {"Highlight Thickness", "ハイライト太さ"},
    {"Highlight Padding", "ハイライト余白"},
    {"Highlight Corner Radius", "ハイライト角丸"},
    {"Highlight Offset Left", "ハイライト左オフセット"},
    {"Highlight Offset Right", "ハイライト右オフセット"},
    {"Highlight Offset Top", "ハイライト上オフセット"},
    {"Highlight Offset Bottom", "ハイライト下オフセット"},
    {"Size Offset Left", "サイズ左オフセット"},
    {"Size Offset Right", "サイズ右オフセット"},
    {"Size Offset Top", "サイズ上オフセット"},
    {"Size Offset Bottom", "サイズ下オフセット"},
    {"Hl Opacity", "HL不透明度"},
    {"Hl Thickness", "HL太さ"},
    {"Hl Padding", "HL余白"},
    {"Hl Corner Radius", "HL角丸"},
    {"Hl Ofs Left", "HL左Ofs"},
    {"Hl Ofs Right", "HL右Ofs"},
    {"Hl Ofs Top", "HL上Ofs"},
    {"Hl Ofs Bottom", "HL下Ofs"},

    // HUD element and subsection names
    {"HP", "HP"},
    {"HP Number Position", "HP数値位置"},
    {"HP Label Color By Value", "HPラベルの色 (値連動)"},
    {"HP Outline", "HPのアウトライン"},
    {"HP Gauge", "HPゲージ"},
    {"HP Gauge Color By Value", "HPゲージの色 (値連動)"},
    {"HP Gauge Outline", "HPゲージのアウトライン"},
    {"Ammo", "弾薬"},
    {"Ammo Number Position", "弾薬数値位置"},
    {"Ammo Label Color By Value", "弾薬ラベルの色 (値連動)"},
    {"Ammo Outline", "弾薬のアウトライン"},
    {"Ammo Gauge", "弾薬ゲージ"},
    {"Ammo Gauge Color By Value", "弾薬ゲージの色 (値連動)"},
    {"Ammo Gauge Outline", "弾薬ゲージのアウトライン"},
    {"Weapon/Ammo", "武器/弾薬"},
    {"Weapon Icon", "武器アイコン"},
    {"Weapon Icon Outline", "武器アイコンのアウトライン"},
    {"Weapon Icon Color Overlay", "武器アイコンの色オーバーレイ"},
    {"Weapon Inventory", "武器インベントリ"},
    {"Weapon Inventory Highlight", "武器インベントリのハイライト"},
    {"Weapon Inventory Outline", "武器インベントリのアウトライン"},
    {"Weapon Inventory Icon Outline", "武器インベントリのアイコンのアウトライン"},
    {"Match Status", "試合情報"},
    {"Score", "スコア"},
    {"Score Labels", "スコアラベル"},
    {"Score Colors", "スコアの色"},
    {"Score Outline", "スコアのアウトライン"},
    {"Rank / Time", "順位 / 時間"},
    {"Rank", "順位"},
    {"Rank Outline", "順位のアウトライン"},
    {"Time Left", "残り時間"},
    {"Time Left Outline", "残り時間のアウトライン"},
    {"Time Limit", "制限時間"},
    {"Time Limit Outline", "制限時間のアウトライン"},
    {"Bomb", "ボム"},
    {"Bomb Left", "残りボム"},
    {"Bomb Left Outline", "残りボムのアウトライン"},
    {"Bomb Icon", "ボムアイコン"},
    {"Bomb Icon Outline", "ボムアイコンのアウトライン"},
    {"Radar", "レーダー"},
    {"Radar Settings", "レーダー設定"},
    {"Radar Outline", "レーダーのアウトライン"},
    {"Frame Outline", "フレームのアウトライン"},
    {"Crosshair", "照準"},
    {"Wpn\nIcon", "武器\nアイコン"},
    {"Bmb\nIcon", "ボム\nアイコン"},
    {"Wpn\nInventory", "武器\n一覧"},
    {"WPN", "武器"},
    {"BMB", "ボム"},
    {"points", "得点"},
    {"Bombs", "ボム"},

    // OSD color labels
    {"Enable OSD Color Patch", "OSD色パッチを有効化"},
    {"Global Color", "全体色"},
    {"Use Global Color for All", "すべてに全体色を使用"},
    {"Enable Separate Color", "個別色を有効化"},
    {"Color (Default: Red)", "色 (既定: 赤)"},
    {"Node Stolen (H211)", "ノード奪取 (H211)"},
    {"Lost Lives", "ライフ喪失"},
    {"Kill / Death", "キル / デス"},
    {"Return to Base", "基地へ戻れ"},
    {"No Ammo", "弾薬なし"},
    {"Coward Detect", "臆病者検出"},
    {"Acquiring Node", "ノード取得中"},
    {"Turret", "タレット"},
    {"Octo Reset", "オクトリスリセット"},
    {"Octo Drop", "オクトリスドロップ"},
    {"Octo Condition", "オクトリス条件"},
    {"Octo Missing", "オクトリス未所持"},
    {"Slot: Kill / Death  [flags=0x02]", "スロット: キル / デス [flags=0x02]"},
    {"Slot: Node Capture  [flags=0x11]", "スロット: ノード取得 [flags=0x11]"},
    {"Slot: Objective     [flags=0x01]", "スロット: 目標 [flags=0x01]"},
    {"Slot: System / Misc [flags=0x00]", "スロット: システム/その他 [flags=0x00]"},
    // OSD slot description labels (multi-line)
    {"Applied once on settings close to currently displayed messages (flags=0x02).\nNew messages use the 'Kill / Death' literal color above.",
     "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x02)。\n新しいメッセージは上の「キル / デス」個別色を使用します。"},
    {"Applied once on settings close to currently displayed messages (flags=0x11).\nNew messages use 'Acquiring Node' or 'Node Stolen' literal colors above.",
     "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x11)。\n新しいメッセージは上の「ノード取得中」または「ノード奪取」の個別色を使用します。"},
    {"Applied once on settings close to currently displayed messages (flags=0x01).\nNew messages use their individual literal colors above (No Ammo / Return to Base / Octo ...).",
     "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x01)。\n新しいメッセージはそれぞれの個別色を使用します (弾薬なし / 基地へ戻れ / オクト系 ...)。"},
    {"Applied once on settings close to currently displayed messages (flags=0x00).\nNew messages use their individual literal colors above (Lost Lives / Coward Detect / Turret ...).\nNote: HEADSHOT! (H228) is flags=0x00, not 0x02.",
     "設定を閉じたとき、現在表示中のメッセージへ一度だけ適用されます (flags=0x00)。\n新しいメッセージはそれぞれの個別色を使用します (ライフ喪失 / 臆病者検出 / タレット ...)。\n注: HEADSHOT! (H228) は flags=0x00 で、0x02 ではありません。"},
    // OSD slot color labels
    {"Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)",
     "色  (YOU KILLED / KILLED YOU / 5キル / プライムハンター / 味方)"},
    {"Color  (acquiring node / node stolen H211)",
     "色  (ノード取得中 / ノード奪取 H211)"},
    {"Color  (AMMO DEPLETED / return to base / bounty / octolith events)",
     "色  (AMMO DEPLETED / 基地へ戻れ / バウンティ / オクトリスイベント)"},
    {"Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)",
     "色  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / タレット)"},

    // Color presets and weapon labels
    {"White", "白"},
    {"Green", "緑"},
    {"Yellow Green", "黄緑"},
    {"Green Yellow", "緑黄"},
    {"Yellow", "黄"},
    {"Pure Cyan", "純シアン"},
    {"Hud Cyan", "HUDシアン"},
    {"Pink", "ピンク"},
    {"Red", "赤"},
    {"Orange", "オレンジ"},
    {"Samus Hud", "サムスHUD"},
    {"Samus Hud Outline", "サムスHUDのアウトライン"},
    {"Kanden Hud", "カンデンHUD"},
    {"Spire Hud", "スパイアHUD"},
    {"Spire Hud Outline", "スパイアHUDのアウトライン"},
    {"Trace Hud", "トレースHUD"},
    {"Noxus Hud", "ノクサスHUD"},
    {"Noxus Hud Outline", "ノクサスHUDのアウトライン"},
    {"Sylux Hud", "サイラックスHUD"},
    {"Sylux Crosshair", "サイラックス照準"},
    {"Weavel Hud", "ウィーヴェルHUD"},
    {"Weavel Hud Outline", "ウィーヴェルHUDのアウトライン"},
    {"Avium Purple", "アヴィウム紫"},
    {"OSD Bright Green", "OSD明るい緑"},
    {"OSD No-Ammo Red", "OSD弾薬なし赤"},
    {"Power Beam", "パワービーム"},
    {"Volt Driver", "ボルトドライバー"},
    {"VoltDriver", "ボルトドライバー"},
    {"Missile", "ミサイル"},
    {"Battle Hammer", "バトルハンマー"},
    {"Battlehammer", "バトルハンマー"},
    {"Imperialist", "インペリアリスト"},
    {"Judicator", "ジュディケイター"},
    {"Magmaul", "マグモール"},
    {"Shock Coil", "ショックコイル"},
    {"ShockCoil", "ショックコイル"},
    {"Omega Cannon", "オメガキャノン"},
    {"PB Color", "PB色"},
    {"VD Color", "VD色"},
    {"MSL Color", "MSL色"},
    {"BH Color", "BH色"},
    {"IMP Color", "IMP色"},
    {"JUD Color", "JUD色"},
    {"MAG Color", "MAG色"},
    {"SCL Color", "SCL色"},
    {"OC Color", "OC色"},

    // Font weights
    {"Thin", "極細"},
    {"Extra Light", "特細"},
    {"Light", "細字"},
    {"Normal", "標準"},
    {"Medium", "中太"},
    {"Semi Bold", "やや太字"},
    {"Bold", "太字"},
    {"Extra Bold", "極太"},
    {"Black", "黒太"},

    // Ramp labels
    {"Number of Colors", "色数"},
    {"Threshold 1 (%)", "しきい値 1 (%)"},
    {"Threshold 2 (%)", "しきい値 2 (%)"},
    {"Threshold 3 (%)", "しきい値 3 (%)"},
    {"Threshold 4 (%)", "しきい値 4 (%)"},
    {"Threshold 5 (%)", "しきい値 5 (%)"},
    {"Threshold 6 (%)", "しきい値 6 (%)"},
    {"Color 1", "色 1"},
    {"Color 2", "色 2"},
    {"Color 3", "色 3"},
    {"Color 4", "色 4"},
    {"Color 5", "色 5"},
    {"Color 6", "色 6"},

    // Custom HUD code status
    {"Generate sharable TOML for the current Custom HUD settings, or paste TOML below to apply it.", "現在のカスタムHUD設定から共有用TOMLを生成するか、下にTOMLを貼り付けて適用します。"},
    {"Output refreshed from the current Custom HUD settings.", "現在のカスタムHUD設定から出力を更新しました。"},
    {"Custom HUD code copied to the clipboard.", "カスタムHUDコードをクリップボードにコピーしました。"},
    {"Output copied into the input box.", "出力を入力欄へコピーしました。"},
    {"Custom HUD code applied to the dialog.", "カスタムHUDコードをダイアログへ適用しました。"},
    {"Paste Custom HUD TOML into the input box first.", "先にカスタムHUD TOMLを入力欄へ貼り付けてください。"},
    {"The pasted Custom HUD code is not a TOML table.", "貼り付けられたカスタムHUDコードはTOMLテーブルではありません。"},
    {"Failed to load Custom HUD code: %1", "カスタムHUDコードの読み込みに失敗しました: %1"},

    // Developer input-method UI
    {"Use New Method for Weapon Change", "武器変更に新方式を使う"},
    {"Use New Method for Biped Fire", "二足時の射撃に新方式を使う"},
    {"Use New Method for Alt-Form Transform", "トランスフォーム変形に新方式を使う"},
    {"Use New Method for Zoom", "ズームに新方式を使う"},
    {"Use New Method 2 for Zoom", "ズームに新方式2を使う"},

#include "MelonPrimeLocalizationMelondsDialogs.inc"
};

constexpr ObjectTextTranslation kObjectTextTranslations[] = {
    {
        "metroidMphSensitvityLabel2",
        "(精密なエイムのため、可能なら1以下にしてください。推奨範囲は -3〜0 です。ただし低すぎるとエイム時のHUD揺れが大きくなります。この値はゲーム内感度に対する相対値なので、0は感度ゼロではなく、1より低いだけです。この設定はMPHのゲーム内感度を上書きするため、ゲーム内で感度を変更しても効果はありません。)"
    },
    {
        "metroidAimYAxisScaleLabel2",
        "(1.5147 = MPH標準、1.9429 = X/Y角速度一致 [オプション])"
    },
    {
        "metroidAimAdjustLabel",
        R"(<html><head/><body><p><b>入力しきい値 (デッドゾーン + スナップ)</b> (推奨: 0.01 [既定] または 0.5): |x|&lt;a → 0, a ≤ |x|&lt;1 → ±1, |x|≥1 → そのまま (a=0: オフ = 1未満をすべて無視、スナップなし)<br/>エイムが思った通りに動かない場合は、この値を下げてみてください。</p></body></html>)"
    },
    {
        "lblMetroidLowLatencyAimDesc",
        "即時同期は低遅延ARM9フックで現在の照準を目標の照準へ同期し、照準基準を再構築します。MoonLikeエイムは小さなエイム移動を即時反映し、大きなジャンプだけ最大ステップ付きで追従します。MPHのエイム補間無効化が必要です。"
    },
    {
        "lblMetroidZoomAimScaleDesc",
        "ゲーム本来のズーム状態が有効な間だけ適用します。100%で通常のマウス感度、100%未満でズーム中のエイムが遅くなり、100%超で速くなります。"
    },
    {
        "lblMetroidNativeAimHookModeDesc",
        "PostFold書き込みはタッチ入力処理の後でフックし、spec108=0 (サムス/カンデン/ノクサス/スパイア) を含むすべてのトランスフォームをカバーします。開発者ビルド専用です。"
    },
    {
        "lblMetroidDirectAltFormTransformDesc",
        "新方式は短いネイティブ入力ゲートをゲーム本来の変形要求処理へリダイレクトします。旧方式は従来のタッチ/メニュー模擬による変形処理を使います。"
    },
    {
        "lblMetroidFixWifiBitsetDesc",
        "フレンド/ライバルの有効スロットを追跡する疑似64bitビットセットの不具合を修正します。\nJP1.0 / US1.0 / EU1.0ではスロット32〜59が正しく扱われず、一部のフレンド/ライバルがオンラインで見えなくなることがあります。\nJP1.1 / US1.1 / EU1.1 / KR1.0と同じ、バイト単位のビットセット処理に置き換えます。\n(JP1.0 / US1.0 / EU1.0のみ。他のROMバージョンには影響しません)"
    },
    {
        "lblMetroidFixShadowFreezeDesc",
        "アイスウェーブの最後の命中/ミス角度判定を、元の横方向範囲チェックは保ったまま完全な3D判定へ置き換えて、シャドウフリーズ問題を修正します。"
    },
    {
        "lblMetroidFixNoxusBladePersistenceDesc",
        "ノクサスのヴォーサイズ/ブレード攻撃が死亡とリスポーン後もダメージを与え続ける不具合を修正します。ブレード攻撃中にノクサスが死亡するとAlt攻撃タイマーがクリアされず、復活後もブレードの当たり判定が残ります。この修正では死亡した瞬間にタイマーをクリアします。"
    },
    {
        "lblMetroidFixNoxusBladePersistenceWarning",
        "この修正はまだ不安定です。特に韓国版では、場合によってブレードが残り続けることがあります。"
    },
    {
        "lblMetroidUseFirmwareLanguageDesc",
        R"(<html><body>
DSファームウェアの言語設定を読み取り、その言語で表示するようゲームにパッチします。<br>
EU ROMには隠し日本語オプションがあり、DSファームウェアを日本語にすると有効になります。<br><br>
<b>EU / US ROM:</b> すべてのモードで動作します (マルチプレイとアドベンチャー)。<br>
<b>JP ROM:</b> マルチプレイのみ動作します。<font color="red"><b>有効にするとアドベンチャーモードはプレイできません。</b></font><br>
<b>KR ROM:</b> まだ未対応です。
</body></html>)"
    },
    {
        "lblMetroidShowHeadshotOnlineDesc",
        "Wi-Fi/オンライン対戦中でも、単独のヘッドショット通知を強制的に表示します。"
    },
    {
        "lblMetroidShowEnemyHpMeterOnlineDesc",
        "HP情報は基本的に更新されないため信頼性は高くありません。誰に当てたかを見る大まかな目安として使えます。"
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
</body></html>)"
    },
    {
        "lblMetroidDisableDoubleDamageMultiplierDesc",
        R"(<html><body>
ダブルダメージの取得、タイマー、HUD、サウンド、視覚効果はそのままに、ダメージ倍率だけを2倍から1倍へ変更します。<br><br>
<font color="red"><b>重要:</b> 2倍 → 1倍の変更は<b>自分側</b>にだけ適用されます。相手側もダメージ1倍を有効にしていない場合、ダブルダメージ中の自分の弾は相手には2倍ダメージとして扱われます。</font><br><br>
参加者全員の同意がある場合にだけ使用してください。
</body></html>)"
    },
    {
        "lblMetroidDamageNotifyPurpleDesc",
        R"(<html><body>
ダメージを受けたときに短時間だけダブルダメージの紫状態で点滅させ、相手から被弾が見えるようにします。<br><br>
<font color="red"><b>重要:</b> これは実際のダブルダメージタイマーを書き込むため、短い間だけ本当に<b>ダブルダメージ状態に入ります</b>。その間、自分の弾は相手にも2倍ダメージを与えます。これを避けるには相手側もダメージ1倍を有効にする必要があります。</font><br><br>
<b>対戦で使う前に、必ず参加者全員の同意を取ってください。</b> 自分のダメージを1倍に保つには、上の「ダブルダメージ倍率を無効化」と組み合わせてください。
</body></html>)"
    },
    {
        "lblMetroidDisablePickingUpSpecificItemsDesc",
        "有効にすると、選択したパワーアップは取得されて消えますが、プレイヤー側の効果はスキップされます。ダブルダメージ、クローク、デスオルトのタイマー、フラグ、HUD、サウンド、視覚効果は適用されません。この設定が影響するのは自分とボットだけで、オンラインの相手は変更されません。"
    },
    {
        "lblMetroidJoy2KeySupportDesc",
        "JoyToKey、Steam Input、reWASDなど、外部のコントローラー→キーボード/マウス変換ツール向けの互換モードです。割り当てたキーが離した後も押されっぱなしになる場合に役立ちます。通常のキーボードとマウスを直接使う場合は、無効にすると入力遅延が少し減ることがあります。"
    },
    {
        "labelMetroidScreenSyncDesc",
        "オフ: 同期なし (最小遅延ですが、表示がカクつくことがあります)。glFinish: 各フレームの描画完了を待つことで表示を滑らかにします。DwmFlush: Windowsコンポジターと同期して表示を滑らかにします (Windowsのみ)。画面がカクついたりちらついたりする場合は、glFinishまたはDwmFlushを試してください。早送り/スローモーション中は自動的に無効になります。"
    },
    {
        "labelInGameAspectRatioAutoDesc",
        "自動: 現在のアスペクト比設定に合わせてアスペクト比パッチを自動適用します (4:3 = オフ、5:3/16:9/21:9 = 自動適用、ウィンドウ = オフ)。"
    },
    {
        "lblMetroidLowHpWarningDesc",
        "自動スケール: Low = 基準 ×0.75、Medium = 基準、High = 基準 ×1.25 (丸め)。0にすると実質的に警告を無効化します。範囲は0-255です。"
    },
    {
        "lblMetroidNativeAimRegisterInjectionDesc",
        "最小遅延のため、エイム呼び出し地点でフックします。二足状態と spec108=1 のトランスフォーム (トレース/サイラックス/ウィーヴェル) をカバーします。有効にすると、上のネイティブエイムデルタHook (PostFold書き込み) より優先されます。"
    },
    {
        "lblMetroidImmediateInputEdgeOverlayDesc",
        "押下/離上エッジをエミュレーター側で生成し、プレイヤーアクション処理前にMPH内部入力状態へ重ねます。射撃、ジャンプ、ズーム、移動をカバーします。ボタンマスクはプレイヤーの現在の操作プリセット割り当て表から読むため、すべてのプリセット (Touch R/L、Dual R/L) に自動対応します。"
    },
    {
        "lblMetroidWeaponSwitchMethodDesc",
        "オンにすると、ARM9フック経由でゲーム本来の武器装備処理を使います。オフでは互換性テスト用に、従来のタッチ/メニュー模擬による武器切替を使います。"
    },
    {
        "lblMetroidBipedFireMethodDesc",
        "オンにすると、ゲーム本来の二足射撃エッジフックで射撃入力ヘルパーの結果をtrueにし、元のクールダウン、弾薬、弾生成、HUD、SFX経路を自然に動かします。旧方式では従来のDS入力/即時入力エッジ合成による射撃経路を使います。"
    },
    {
        "lblMetroidZoomMethodDesc",
        "新方式はゲーム内のズーム割り当て表を読むため、Touch/Dualプリセットごとに異なるDSボタンへズームを割り当てられます。旧方式より少し低遅延です。両方のチェックを外すと、従来の入力経路と同じく固定Rボタンを使う旧方式になります。"
    },
    {
        "lblMetroidZoomMethod2Desc",
        "新方式2は、押すたびにゲーム本来のズーム状態を切り替えます。「ズームに新方式を使う」とは同時に使えません。"
    },
    {"btnClear", "クリア"},
};

QString TranslateExact(const QString& text)
{
    for (const Translation& entry : kTranslations)
    {
        if (text == QString::fromUtf8(entry.en))
            return QString::fromUtf8(entry.ja);
    }
    return text;
}

QString TranslateByObjectName(const QWidget* widget, const QString& text)
{
    if (!widget || text.isEmpty())
        return text;

    const QString objectName = widget->objectName();
    if (objectName.isEmpty())
        return text;

    for (const ObjectTextTranslation& entry : kObjectTextTranslations)
    {
        if (objectName == QString::fromUtf8(entry.objectName))
            return QString::fromUtf8(entry.ja);
    }
    return text;
}

QString Tr(const QString& text)
{
    if (!IsJapaneseLocale() || text.isEmpty())
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
        return QStringLiteral("インスタンス %1 の設定")
            .arg(text.mid(35));

    if (text.startsWith(QStringLiteral("Configuring mappings for instance ")))
        return QStringLiteral("インスタンス %1 の割り当て")
            .arg(text.mid(35));

    if (text.startsWith(QStringLiteral("Configuring paths for instance ")))
        return QStringLiteral("インスタンス %1 のパス")
            .arg(text.mid(32));

    if (text.startsWith(QStringLiteral("Setting battery levels for instance ")))
        return QStringLiteral("インスタンス %1 のバッテリー残量")
            .arg(text.mid(36));

    if (text == QStringLiteral("(none)"))
        return QStringLiteral("(なし)");

    if (text.startsWith(QStringLiteral("Direct mode (requires "))
        && text.endsWith(QStringLiteral(" and ethernet connection)")))
    {
        const QString middle = text.mid(22, text.size() - 22 - 25);
        return QStringLiteral("ダイレクトモード (要 %1・イーサネット接続)").arg(middle);
    }

    const int nativeIdx = text.indexOf(QStringLiteral(" native ("));
    if (nativeIdx > 0)
        return text.left(nativeIdx) + QStringLiteral(" ネイティブ (") + text.mid(nativeIdx + 9);

    {
        QString cameraText = text;
        if (cameraText.contains(QStringLiteral(" (inner camera)")))
            cameraText.replace(QStringLiteral(" (inner camera)"), QStringLiteral(" (内側カメラ)"));
        if (cameraText.contains(QStringLiteral(" (outer camera)")))
            cameraText.replace(QStringLiteral(" (outer camera)"), QStringLiteral(" (外側カメラ)"));
        if (cameraText != text)
            return cameraText;
    }

    const std::pair<QString, QString> dynamicPrefixes[] = {
        {QStringLiteral("DS slot: "), QStringLiteral("DSスロット: ")},
        {QStringLiteral("GBA slot: "), QStringLiteral("GBAスロット: ")},
        {QStringLiteral("Top "), QStringLiteral("上画面 ")},
        {QStringLiteral("Bottom "), QStringLiteral("下画面 ")},
    };
    for (const auto& prefix : dynamicPrefixes)
    {
        if (text.startsWith(prefix.first))
            return prefix.second + Tr(text.mid(prefix.first.size()));
    }

    return text;
}

QString TrWidgetText(const QWidget* widget, const QString& text)
{
    if (!IsJapaneseLocale() || text.isEmpty())
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
    if (!IsJapaneseLocale())
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
    if (!dialog || !IsJapaneseLocale())
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
        if (!IsJapaneseLocale() || !chk)
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

void wireMelonDsLANDialogLabels(QWidget* dialog)
{
    if (!dialog || !IsJapaneseLocale())
        return;

    const QString dialogName = dialog->objectName();

    auto localizeLanWarning = [](QLabel* label)
    {
        if (!label)
            return;
        label->setText(QStringLiteral(
            "<html><head/><body>"
            "<p>警告: LANは低レイテンシのネットワーク接続が必要です。</p>"
            "<p>VPNやトンネル経由では動作しない可能性があります。</p>"
            "</body></html>"));
    };

    if (dialogName == QStringLiteral("LANStartHostDialog"))
    {
        localizeLanWarning(dialog->findChild<QLabel*>(QStringLiteral("label_3")));
        return;
    }

    if (dialogName != QStringLiteral("LANStartClientDialog"))
        return;

    localizeLanWarning(dialog->findChild<QLabel*>(QStringLiteral("label_2")));

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
    if (!dialog || !IsJapaneseLocale())
        return;
    new MelonDsDialogShowLocalizer(dialog);
}

void LocalizeMelonDsDialog(QWidget* dialog)
{
    if (!IsJapaneseLocale() || !dialog)
        return;
    LocalizeWidgetTree(dialog);
    wireMelonDsLANDialogLabels(dialog);
    wireMelonDsDialogDynamicLabels(dialog);
}

} // namespace MelonPrime::UiText
