<p align="center"><img src="./res/icon/melon_128x128.png"></p>
<h1 align="center"><b>melonPrimeDS</b></h1>
  

Modded version of [melonDS](https://melonds.kuribo64.net/) emulator to play Metroid Prime Hunters.

It brings full mouse-and-keyboard FPS controls to the game, along with a customizable HUD and many quality-of-life features.

**Please read the instructions carefully.**

## You can buy a coffee for the current maintainer and updater, Zection, if you’d like:
https://ko-fi.com/zection

<table width="100%">
  <tr>
    <td align="left" valign="middle" width="76%">
      <a href="https://ko-fi.com/zection">
        <img src="https://github.com/user-attachments/assets/ec1dd31e-2727-4539-b88d-3c1fc0b31799"
             alt="Ko-fi Banner" width="100%">
      </a>
    </td>
    <td align="center" valign="middle" width="24%">
      <a href="https://ko-fi.com/zection">
        <img src="https://github.com/user-attachments/assets/a8afd222-960c-432c-9d0b-4a28865939e7"
             alt="Ko-fi QR" width="100%">
      </a>
    </td>
  </tr>
</table>

## If you’d like to see new HUD features added, you can also support Livetek .
https://ko-fi.com/livetek 
      <a href="https://ko-fi.com/livetek">
<img width="1536" height="1024" alt="image" src="https://github.com/user-attachments/assets/ec6a2871-2b67-4de0-8b1a-ac6740c8d388" />

      </a>


[melonPrimeDS.webm](https://github.com/makidoll/melonPrimeDS/assets/8362329/69ab26bb-7205-451a-a11c-70a2ca0b549d)

### Download

Releases for [Windows, Linux and macOS here!](https://github.com/ag-advania/melonPrimeDS/releases)

[![aur](https://img.shields.io/aur/version/melonprimeds-bin?style=flat&logo=archlinux)](https://aur.archlinux.org/packages/melonprimeds-bin)


> **⚠️🖱️ Warning if using mouse acceleration!**   
> Please disable mouse acceleration or it will feel strange.  
> Find a [guide for Windows here](https://www.lifewire.com/turn-off-mouse-acceleration-in-windows-11-5193828) and use [SteerMouse if on macOS](https://plentycom.jp/en/steermouse/index.html)  

> **⚠️ Warning for macOS users!**   
> Once you start the program, you're going to have to go into macOS settings:  
> **Privacy & Security > Accessibility**, and ensure melonPrimeDS is enabled.

### Instructions

-   Fyi. the emulator hack uses a different config path than melonDS, so this won't conflict

-   Make sure to set all DS bindings to `None` in  
    `Config → Input and hotkeys → DS keypad`  
    Defaults should already be empty  
    _(click binding and press backspace)_

-   Find Metroid related `Keyboard mappings` in  
    `Metroid → Input settings`  
    Recommended defaults have already been set, but feel free to change them if you want to

    Notes:

    -   Focusing the window will capture your mouse. Use `ESC` to release.
    -   If you prefer inverted Y-axis, enable the in-game **Lock Invert** option.

-   Find Metroid sensitivity settings in  
    `Metroid → Other settings`  
      
    When in-game, **make sure to set the aim sensitivty to the lowest!**   
    The DS touchscreen isn't very precise, so setting it to lowest helps  
-   Also recommended to set audio settings in-game to headphones

-   **Custom HUD** (optional): in `Metroid → Other settings`, enable **Custom HUD** to
    replace the native HUD with a customizable overlay (crosshair, HP, weapon/ammo,
    match status, rank/timer, bomb count) and the bottom-screen radar overlay.
    -   Press **Edit HUD Layout** in the same dialog to enter the interactive in-game
        editor: drag elements to reposition them and tweak each one in the properties panel.
    -   You can share or import a setup as TOML text via **Custom HUD Input/Output**.

-   **Bug fixes & gameplay options** (optional): the `Metroid` menu has quick toggles
    (Shadow Freeze fix, Disable Double Damage Multiplier, Power-Ups: Pick Up With No
    Effect, In-Game Top Screen Only, …), and `Metroid → Other settings` holds the full set,
    including the **In-Game Aspect Ratio** patch (Auto / 5:3 / 16:10 / 16:9 / 21:9).

  
| Function                              | Key Binding                         |
|---------------------------------------|-------------------------------------|
| Move Forward                          | W                                  |
| Move Back                             | S                                  |
| Move Left                             | A                                  |
| Move Right                            | D                                  |
| Jump                                  | Spacebar                           |
| Transform                             | Left Ctrl                          |
| Imperialist Zoom, Map Zoom Out, Morph Ball Boost | Right Click                    |
| Fast Morph Ball Boost (Hold to sustain) | Shift                              |
| Scan Visor                            | C                                  |
| UI Left（Adventure ← / License L） | Z |
| UI Right（Adventure → / License R） | X |
| UI OK | F |
| UI "Yes" (Enter Starship)             | G                                  |
| UI "No" (Enter Starship)              | H                                  |
| Shoot/Scan, Map Zoom In               | Left Click                         |
| Scan/Shoot, Map Zoom In               | V                                  |
| Power Beam                            | Mouse 5 (Side Top)                 |
| Missile                               | Mouse 4 (Side Bottom)              |
| Affinity Weapon（Last Used Special / Omega Cannon） | R |
| Next Weapon (Sorted Order)            | J or Mouse Wheel Down              |
| Previous Weapon (Sorted Order)        | K or Mouse Wheel Up                |
| Weapon 1: ShockCoil | 1 |
| Weapon 2: Magmaul | 2 |
| Weapon 3: Judicator | 3 |
| Weapon 4: Imperialist | 4 |
| Weapon 5: Battlehammer | 5 |
| Weapon 6: Volt Driver | 6 |
| Menu/Map                              | Tab                                |
| Weapon Check                          | Y                                  |
| Aim Sensitivity Up                    | PageUp                             |
| Aim Sensitivity Down                  | PageDown                           |


### Gameplay tips

-   **Rapid-firing the Volt Driver (and other tap-fire weapons):** don't mash the fire
    button as fast as you can. Instead, fire your next shot *after you see the previous
    shot leave the gun*. Pacing your clicks to the visible shots gives a more consistent,
    faster sustained rate than spamming, because the game only registers a new shot once
    the previous one has been emitted.


### Default settings changed from melonDS

-   Fullscreen toggle set to `F11`
-   Screen filter set to **false**
-   3D renderer set to **OpenGL**
-   JIT recompiler set to **enabled** _(helps with performance)_

For a large top screen during FPS play, enable the optional **In-Game Top Screen Only**
(in the `Metroid` menu): while in-game it temporarily switches to Top Only / Natural layout
and restores your normal window settings in menus. The base screen layout/sizing defaults are
left at melonDS' stock values, so you can also set them yourself in `Config → Screen` settings.

VSync defaults to off, which helps latency and performance. MelonPrimeDS now respects your `Screen.VSync` setting instead of forcing it off, so you can turn it back on if you want.

### Build

Downloadable builds are produced with GitHub Actions. Windows is the primary
supported platform; the project now uses **CMake presets** with **vcpkg** (a
submodule) for dependencies.

**Windows (MSYS2 / MinGW UCRT64):**

```bash
git submodule update --init --recursive   # fetch vcpkg
cmake --preset=release-mingw-x86_64
cmake --build --preset=release-mingw-x86_64
```

**Linux (generic, no longer CI-tested):**

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja
ninja
```

### Features

**Compatibility & performance**

- Compatible with all ROM versions (ensure the ROM is unmodified, untrimmed, and unencrypted):
  - USA, USA rev1 (the Australian version shares the same binary as the USA)
  - EU, EU rev1
  - Japan, Japan rev1
  - Korea
- Gameplay remains smooth and problem-free regardless of player role, whether as the host or as a guest player (P2, P3, or P4).
- Optimized so the adventure function is not loaded during multiplayer.
- Menu flickering has been resolved.
- VSync defaults to off to minimize latency (this also lowers latency in OpenGL Classic mode), and now follows your `Screen.VSync` setting, so you can re-enable it if you prefer.
- Heavily optimized input-latency and frame-pacing path for the lowest possible mouse-to-screen delay.

**Aim & input**

- The aim pipeline has been reworked for more comfortable mouse control (sub-pixel accumulation, optional aim-smoothing disable, and low-latency aim options).
- Optimized morph ball boosting. Hold the Shift key to continue boosting at a higher speed; right-click still boosts as before.
- Prevents jumping when switching weapons rapidly.
- The mouse cursor controls aiming directly rather than through a virtual stylus, so aiming stays responsive. An optional **Stylus Mode** (off by default, in `Metroid → Other settings`) is available if you prefer to play with the stylus.
- Key for switching to the last-used special weapon or Omega Cannon, plus next/previous weapon cycling with the mouse wheel or dedicated keys.
- Keys for real-time aim-sensitivity adjustment.
- Keys for clicking "YES"/"NO" in Adventure Mode, and L/R navigation on the Hunter License screen (assign UI Left/Right in the key config).
- Quick Stop Movement: press both opposing movement keys (left+right or forward+back) at once to halt faster.
- SnapTap for faster directional switching and smoother strafing (toggle in settings). Learn more: [SnapTap Introduction](https://www.youtube.com/watch?v=wDcRf4uCzuM).

**Custom HUD & visuals**

- Fully customizable Custom HUD — crosshair, HP, weapon/ammo, match status, rank/timer, and alt-form bomb count — with an in-game drag-and-drop **edit mode** and live preview.
- Bottom-screen **radar overlay** drawn on top of the top screen, with a hunter-colored frame and configurable position, size, and opacity.
- **In-game aspect ratio** patch (Auto / 5:3 / 16:10 / 16:9 / 21:9) to widen the gameplay field of view.
- High-resolution HUD fonts: the bundled MPH font, or your own system / file font.
- Customizable OSD message colors and an optional low-HP warning.
- English / Japanese UI text, selected automatically from your OS locale.

**Bug fixes (toggleable, under BUG FIXES)**

- Friend/Rival Wi-Fi active bitset fix.
- Shadow Freeze fix (Ice Wave full 3D angle check).
- Noxus Blade persistence-on-death fix (still experimental).

**Gameplay options (toggleable)**

- Show Headshot notification online, and an enemy HP meter online. Note: the HP value is generally not updated, so it is unreliable — treat it only as a rough indicator of who you hit.
- Disable the Double Damage multiplier.
- Pick up specific power-ups (Double Damage / Cloak / Death Alt) with no effect — affects only you and bots, not online opponents.
- Use firmware language/name, headphone audio mode, and unlock-all options.


<p align="center"><img src="./res/icon/melon_128x128.png"></p>
<h1 align="center"><b>melonPrimeDS</b></h1>
  

[melonDS](https://melonds.kuribo64.net/)エミュレータの改造版で、Metroid Prime Huntersをプレイするためのもの。
マウスとキーボードによる本格的なFPS操作に加え、カスタマイズ可能なHUDや多数の便利機能を備えています。
**説明をよくお読みください。**

melonPrimeDS の元の作者、Makidoll をサポートする：

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Q5Q0MLBI)

現在のメンテナー兼アップデーターであるZectionに、もしよければコーヒーをご馳走できます：

<table width="100%">
  <tr>
    <td align="left" valign="middle" width="76%">
      <a href="https://ko-fi.com/zection">
        <img src="https://github.com/user-attachments/assets/ec1dd31e-2727-4539-b88d-3c1fc0b31799"
             alt="Ko-fi Banner" width="100%">
      </a>
    </td>
    <td align="center" valign="middle" width="24%">
      <a href="https://ko-fi.com/zection">
        <img src="https://github.com/user-attachments/assets/a8afd222-960c-432c-9d0b-4a28865939e7"
             alt="Ko-fi QR" width="100%">
      </a>
    </td>
  </tr>
</table>

[melonPrimeDS.webm](https://github.com/makidoll/melonPrimeDS/assets/8362329/69ab26bb-7205-451a-a11c-70a2ca0b549d)

### ダウンロード

[Windows、Linux、macOS用のリリースはこちら！](https://github.com/ag-advania/melonPrimeDS/releases)

[![aur](https://img.shields.io/aur/version/melonprimeds-bin?style=flat&logo=archlinux)](https://aur.archlinux.org/packages/melonprimeds-bin)

> **⚠️🖱️ マウス加速を使用している場合の注意！**   
> マウス加速を無効にしてください。さもないと違和感があります。  
> [Windowsの場合はこちらのガイド](https://trlog.org/mouse-acceleration/)を参照し、[macOSの場合はSteerMouse](https://plentycom.jp/en/steermouse/index.html)を使用してください。  

> **⚠️ macOSユーザーへの注意！**   
> プログラムを起動したら、macOSの設定で以下の操作が必要です：  
> **プライバシーとセキュリティ > アクセシビリティ**で、melonPrimeDSが有効になっていることを確認してください。

### 説明

-   エミュレータのハックはmelonDSとは異なる設定パスを使用するため、競合しません
-   必ず以下の場所ですべてのDSバインディングを`None`に設定してください  
   `Config → Input and hotkeys → DS keypad`  
   デフォルトですでに空になっているはずです  
   *（バインディングをクリックしてバックスペースを押してください）*
-   以下の場所でMetroid関連の`キーボードマッピング`を見つけてください  
   `Metroid → Input settings`  
   推奨のデフォルト設定がすでに設定されていますが、必要に応じて変更してください
   注意点：
   -   ウィンドウにフォーカスするとマウスがキャプチャされます。`ESC`で解放できます。
   -   Y軸反転を使いたい場合は、ゲーム内の設定画面からオプションで有効にしてください。
-   以下の場所でMetroidの感度設定を見つけてください  
   `Metroid → Other settings`  
     
   ゲーム内では、**必ず照準感度を最低に設定してください！**   
   DSのタッチスクリーンはあまり精密ではないので、最低に設定すると役立ちます  
-   ゲーム内でのオーディオ設定をヘッドフォンに設定することもおすすめします

-   **カスタムHUD**（任意）：`Metroid → Other settings` で **Custom HUD** を有効にすると、
    ゲーム内のHUDをカスタマイズ可能なオーバーレイ（クロスヘア、HP、武器/弾数、試合ステータス、
    ランク/タイマー、ボム数）と下画面レーダーオーバーレイに置き換えられます。
    -   同じダイアログの **Edit HUD Layout** を押すと、対話式のゲーム内エディターに入れます。
        要素をドラッグして配置し、プロパティパネルで個別に調整できます。
    -   設定は **Custom HUD Input/Output** からTOMLテキストとして共有・読み込みできます。

-   **バグ修正とゲームプレイオプション**（任意）：`Metroid` メニューにクイックトグル
    （Shadow Freeze修正、ダブルダメージ倍率の無効化、パワーアップの無効取得、上画面のみ表示 など）があり、
    `Metroid → Other settings` には **In-Game Aspect Ratio** パッチ（Auto / 5:3 / 16:10 / 16:9 / 21:9）を含む全項目があります。

  

| 機能                                   | キー設定                             |
|--------------------------------------|-------------------------------------|
| 前進                                   | Wキー                               |
| 後退                                   | Sキー                               |
| 左に移動                               | Aキー                               |
| 右に移動                               | Dキー                               |
| ジャンプ                                | スペースバー                          |
| 変身                                   | 左コントロール                        |
| インペリアリストズーム、マップズームアウト、モーフボールブースト | 右クリック                           |
| 高速モーフボールブースト（押し続けて継続）  | Shiftキー                            |
| スキャンバイザー                         | Cキー                               |
| UI左                                   | Zキー                               |
| UI右                                   | Xキー                               |
| UI決定                                 | Fキー                               |
| UI「はい」（スターシップに入る）          | Gキー                               |
| UI「いいえ」（スターシップに入る）        | Hキー                               |
| 射撃/スキャン、マップズームイン           | 左クリック                           |
| スキャン/射撃、マップズームイン           | Vキー                               |
| パワービーム                             | マウス5（サイド上）                   |
| ミサイル                                | マウス4（サイド下）                   |
| 特殊武器（最後に使用した武器、オメガキャノン）| Rキー                               |
| 次の武器（ソート順）                     | Jキー または マウスホイールダウン     |
| 前の武器（ソート順）                     | Kキー または マウスホイールアップ     |
| 武器1（ショックコイル） | 1 |
| 武器2（マグモール） | 2 |
| 武器3（ジュディケーター） | 3 |
| 武器4（インペリアリスト） | 4 |
| 武器5（バトルハンマー） | 5 |
| 武器6（ボルトドライバー） | 6 |
| 所有武器確認                             | Y                                  |
| メニュー/マップ                          | Tabキー                             |
| エイム感度アップ                         | PageUpキー                          |
| エイム感度ダウン                         | PageDownキー                        |


### ゲームプレイのコツ

-   **ボルトドライバー（など連射武器）の連射：** 射撃ボタンをただ高速で連打するのではなく、
    *前の弾が銃から出たのが見えてから*次の弾を撃つと連射しやすくなります。見えた弾に合わせて
    クリックのリズムを取ると、闇雲な連打よりも安定して速い連射になります。ゲーム側は前の弾が
    発射されて初めて次の射撃を受け付けるためです。


### melonDSからのデフォルト設定の変更点

-   フルスクリーン切り替えを`F11`に設定
-   画面フィルターを**無効**に設定
-   3Dレンダラーを**OpenGL**に設定
-   JITリコンパイラを**有効**に設定 *（パフォーマンス向上に役立ちます）*

FPSプレイで上画面を大きくしたい場合は、任意機能の **In-Game Top Screen Only**（`Metroid` メニュー）を
有効にしてください。ゲーム中だけ一時的に Top Only / Natural レイアウトに切り替わり、メニューでは
通常のウィンドウ設定に戻ります。基本の画面レイアウト/サイズはmelonDS標準のままなので、
`Config → Screen` 設定で自分で変更することもできます。

VSyncはデフォルトでオフで、これによりレイテンシーとパフォーマンスが改善します。現在のMelonPrimeDSは強制オフにせずユーザーの `Screen.VSync` 設定を尊重するため、必要なら再度オンにできます

### ビルド

ダウンロード可能なビルドはGitHub Actionsで作成されています。現在の主対応プラットフォームは
**Windows** で、依存関係には **CMakeプリセット** と **vcpkg**（サブモジュール）を使用します。

**Windows（MSYS2 / MinGW UCRT64）：**

```bash
git submodule update --init --recursive   # vcpkg を取得
cmake --preset=release-mingw-x86_64
cmake --build --preset=release-mingw-x86_64
```

**Linux（汎用・CI検証対象外）：**

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja
ninja
```

# 機能

**互換性とパフォーマンス**

- すべてのROMバージョンと互換性があります（ROMが未修正、未トリム、未暗号化であることを確認してください）：
  - アメリカ版、アメリカ版rev1（オーストラリア版はアメリカ版と同じバイナリを共有）
  - ヨーロッパ版、ヨーロッパ版rev1
  - 日本版、日本版rev1
  - 韓国版
- ホストとしても、ゲストプレイヤー（P2、P3、P4）としても、プレイヤーの役割に関係なくゲームプレイはスムーズで問題ありません
- マルチプレイヤーでアドベンチャー機能を読み込まないように最適化
- メニューのちらつきが解決されました
- レイテンシーを抑えるためVSyncはデフォルトでオフです（OpenGL Classicモードでもレイテンシーが減少します）。現在はユーザーの `Screen.VSync` 設定に従うため、必要なら再度オンにできます
- 入力遅延とフレームペーシングの処理を徹底的に最適化し、マウスから画面までの遅延を可能な限り小さくしています

**エイムと入力**

- マウス操作をより快適にするためエイム処理を見直しています（サブピクセル蓄積、エイムスムージングの無効化オプション、低遅延エイムオプション）
- モーフボールブーストの最適化。Shiftキーを押し続けると高速ブーストを継続できます（従来通り右クリックでもブースト可能）
- 武器の素早い切り替え時にジャンプするのを防止
- マウスカーソルは仮想スタイラスを介さず直接エイムを操作するため、操作が軽快です。スタイラスで遊びたい場合は任意の **Stylus Mode**（デフォルトはオフ、`Metroid → Other settings`）を利用できます
- 最後に使用した特殊武器またはオメガキャノンに切り替えるキー、マウスホイールや専用キーでの次/前の武器切り替え
- リアルタイムでエイム感度を調整するキー
- アドベンチャーモードで「はい」/「いいえ」を押すキー、ハンターライセンス画面でのL/R操作（キー設定でUI左/右を割り当て）
- クイック停止移動：左右（または前後）の移動キーを同時に押すことで、より素早く停止できます
- SnapTap：方向キーの切り替えを高速化し、ストレイフィングなどをよりスムーズにします（設定から有効化）。詳しくは：[SnapTap紹介](https://www.youtube.com/watch?v=wDcRf4uCzuM)

**カスタムHUDと表示**

- フルカスタマイズ可能なカスタムHUD（クロスヘア、HP、武器/弾数、試合ステータス、ランク/タイマー、オルトフォームのボム数）。ゲーム内のドラッグ＆ドロップ式**編集モード**とライブプレビュー付き
- 上画面に重ねて表示する下画面の**レーダーオーバーレイ**。ハンターカラーのフレーム、位置・サイズ・不透明度を設定可能
- **ゲーム内アスペクト比**パッチ（Auto / 5:3 / 16:10 / 16:9 / 21:9）で視野を横に広げられます
- 高解像度HUDフォント：同梱のMPHフォント、またはシステム/ファイルの任意フォント
- カスタマイズ可能なOSDメッセージの色、および任意の低HP警告
- OSのロケールに応じて自動選択される英語/日本語のUI表示

**バグ修正（BUG FIXESから個別にON/OFF可能）**

- フレンド/ライバルのWi-Fiアクティブビットセット修正
- Shadow Freeze修正（アイスウェーブの完全な3D角度判定）
- Noxus Bladeの死亡後持続バグ修正（まだ実験的）

**ゲームプレイオプション（個別にON/OFF可能）**

- オンラインでのヘッドショット通知表示、および敵HPメーター表示。なお、HP情報は基本的に更新されないため信頼性は高くなく、誰に当てたかを見る大まかな目安としてのみ使えます
- ダブルダメージ倍率の無効化
- 特定のパワーアップ（ダブルダメージ / クローク / デスオルト）を効果なしで取得（自分とボットのみに影響、オンラインの相手には影響しません）
- ファームウェアの言語/名前の使用、ヘッドフォン音声モード、すべて解放などのオプション
