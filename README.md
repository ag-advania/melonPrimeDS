<p align="center"><img src="./res/icon/melon_128x128.png"></p>
<h1 align="center"><b>melonPrimeDS</b></h1>
  

Modded version of [melonDS](https://melonds.kuribo64.net/) emulator to play Metroid Prime Hunters.

It's a bit of a hack but the goal is to make the game as fun as possible using mouse and keyboard.

I originally made this for controller but because there's no lock-on, it wasn't really fun to play.

**Please read the instructions carefully.**

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Q5Q0MLBI)

[melonPrimeDS.webm](https://github.com/makidoll/melonPrimeDS/assets/8362329/69ab26bb-7205-451a-a11c-70a2ca0b549d)

### Download

Releases for [Windows, Linux and macOS here!](https://github.com/makidoll/melonPrimeDS/releases)

[![aur](https://img.shields.io/aur/version/melonprimeds-bin?style=flat&logo=archlinux)](https://aur.archlinux.org/packages/melonprimeds-bin)


> **⚠️🖱️ Warning if using mouse acceleration!**   
> Please disable mouse acceleration or it will feel strange.  
> Find a [guide for Windows here](https://www.lifewire.com/turn-off-mouse-acceleration-in-windows-11-5193828) and use [SteerMouse if on macOS](https://plentycom.jp/en/steermouse/index.html)  
> Wasn't able to get raw mouse input unfortunately.

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
    -   The stylus gets placed in the middle of the DS screen for aiming which can cause accidental presses

-   Find Metroid sensitivity settings in  
    `Metroid → Other settings`  
      
    When in-game, **make sure to set the aim sensitivty to the lowest!**   
    The DS touchscreen isn't very precise, so setting it to lowest helps  
-   Also recommended to set audio settings in-game to headphones

  
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
| UI Left                               | Z                                  |
| UI Right                              | X                                  |
| UI Confirm                            | F                                  |
| UI "Yes" (Enter Starship)             | G                                  |
| UI "No" (Enter Starship)              | H                                  |
| Shoot/Scan, Map Zoom In               | Left Click                         |
| Scan/Shoot, Map Zoom In               | V                                  |
| Power Beam                            | Mouse 5 (Side Top)                 |
| Missile                               | Mouse 4 (Side Bottom)              |
| Special Weapon (Last Used, Omega Cannon) | R                              |
| Next Weapon (Sorted Order)            | J or Mouse Wheel Down              |
| Previous Weapon (Sorted Order)        | K or Mouse Wheel Up                |
| Weapon 1                              | 1                                  |
| Weapon 2                              | 2                                  |
| Weapon 3                              | 3                                  |
| Weapon 4                              | 4                                  |
| Weapon 5                              | 5                                  |
| Weapon 6                              | 6                                  |
| Menu/Map                              | Tab                                |
| Aim Sensitivity Up                    | PageUp                             |
| Aim Sensitivity Down                  | PageDown                           |


### Default settings changed from melonDS

-   Fullscreen toggle set to `F11`
-   Screen layout set to **horizontal**
-   Screen sizing set to **emphasize top**
-   Screen filter set to **false**
-   3D renderer set to **OpenGL**
-   JIT recompiler set to **enabled** _(helps with performance)_

VSync was already disabled but keeping it off also helps with performance

### Build

I modified melonDS and played Hunters on Linux. Building is straightforward

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja
ninja
```

Downloadable builds were made using GitHub actions

### Todo

-   Update to latest melonDS

<p align="center"><img src="./res/icon/melon_128x128.png"></p>
<h1 align="center"><b>melonPrimeDS</b></h1>
  

[melonDS](https://melonds.kuribo64.net/)エミュレータの改造版で、Metroid Prime Huntersをプレイするためのもの。
少しハック的ですが、マウスとキーボードを使ってできるだけ楽しくゲームをプレイすることが目的です。
元々はコントローラー用に作りましたが、ロックオン機能がないため、あまり楽しくありませんでした。
**説明をよくお読みください。**

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/Q5Q0MLBI)

[melonPrimeDS.webm](https://github.com/makidoll/melonPrimeDS/assets/8362329/69ab26bb-7205-451a-a11c-70a2ca0b549d)

### ダウンロード

[Windows、Linux、macOS用のリリースはこちら！](https://github.com/makidoll/melonPrimeDS/releases)

[![aur](https://img.shields.io/aur/version/melonprimeds-bin?style=flat&logo=archlinux)](https://aur.archlinux.org/packages/melonprimeds-bin)

> **⚠️🖱️ マウス加速を使用している場合の注意！**   
> マウス加速を無効にしてください。さもないと違和感があります。  
> [Windowsの場合はこちらのガイド](https://trlog.org/mouse-acceleration/)を参照し、[macOSの場合はSteerMouse](https://plentycom.jp/en/steermouse/index.html)を使用してください。  
> 残念ながら、生のマウス入力を取得することはできませんでした。

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
   -   スタイラスはDS画面の中央に配置され、狙いを定めるために使用されますが、誤ってタッチする可能性があります
-   以下の場所でMetroidの感度設定を見つけてください  
   `Metroid → Other settings`  
     
   ゲーム内では、**必ず照準感度を最低に設定してください！**   
   DSのタッチスクリーンはあまり精密ではないので、最低に設定すると役立ちます  
-   ゲーム内でのオーディオ設定をヘッドフォンに設定することもおすすめします

  

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
| 武器1                                   | 1キー                               |
| 武器2                                   | 2キー                               |
| 武器3                                   | 3キー                               |
| 武器4                                   | 4キー                               |
| 武器5                                   | 5キー                               |
| 武器6                                   | 6キー                               |
| メニュー/マップ                          | Tabキー                             |
| エイム感度アップ                         | PageUpキー                          |
| エイム感度ダウン                         | PageDownキー                        |


### melonDSからのデフォルト設定の変更点

-   フルスクリーン切り替えを`F11`に設定
-   画面レイアウトを**横向き**に設定
-   画面サイズを**上画面を強調**に設定
-   画面フィルターを**無効**に設定
-   3Dレンダラーを**OpenGL**に設定
-   JITリコンパイラを**有効**に設定 *（パフォーマンス向上に役立ちます）*

VSyncはすでに無効になっていましたが、オフのままにするとパフォーマンスの向上に役立ちます

### ビルド

melonDSを修正し、LinuxでHuntersをプレイしました。ビルドは簡単です

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -GNinja
ninja
```

ダウンロード可能なビルドはGitHub actionsを使用して作成されました

### Todo

-   最新のmelonDSに更新する
