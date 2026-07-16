# melonPrimeDS / upstream melonDS `MELONPRIME_DS` 境界監査

## 結論

**現状の`highres_fonts_v3`は、MelonPrime固有コードがすべて`#ifdef MELONPRIME_DS`で隔離されている状態ではありません。**

さらに重要なのは、現在の構成では次の2点が成立していないことです。

1. **`MELONPRIME_DS`を無効にした通常melonDSビルドが用意されていない**
2. **仮に定義を手動で外しても、共有ソース内にコンパイル不能箇所と挙動変更箇所が残る**

今回の判定基準は、共有melonDSコードへのMelonPrime変更をガードし、非MelonPrime側ではアップストリームの処理を維持することです。

リポジトリ内の既存監査文書にも、以下の方針が記載されています。

- 共有ファイルの全fork変更を`#ifdef MELONPRIME_DS`で隔離する
- `#else`または非MelonPrime経路でアップストリーム挙動を維持する
- MelonPrimeヘッダーやAPIをガード外で参照しない
- 共有melonDSコードへの変更は最小化する

---

## 確定した重大違反

| 判定 | ファイル | 問題 |
|---|---|---|
| **FAIL** | `src/CMakeLists.txt` | `MELONPRIME_DS`を無条件定義 |
| **FAIL** | `src/frontend/qt_sdl/CMakeLists.txt` | MelonPrimeソース、定義、出力名を無条件適用 |
| **FAIL** | `src/GPU3D_Compute.cpp` | アップストリーム実装を無条件で置換 |
| **FAIL** | `src/DMA.cpp` | アップストリーム処理を無条件で別実装へ置換 |
| **FAIL** | `src/frontend/qt_sdl/EmuInstance.cpp` | MelonPrime限定関数をガード外から呼び出す |
| **FAIL** | `src/frontend/qt_sdl/EmuThread.cpp` | MelonPrime限定メンバーをガード外から使用 |
| **FAIL** | `src/frontend/qt_sdl/main.cpp` | MelonPrime用Qt低遅延設定を全ビルドへ強制 |
| **WARN** | `src/ARMJIT.cpp` | OFF時の意味は同じだが、アップストリーム経路を正確に保存していない |
| **WARN** | `Screen.*`、shader、Metal関連 | 補助マクロが`MELONPRIME_DS`と連動していない |
| **WARN** | `Window.cpp` | MelonPrime以外の汎用変更も共有ソースへ直接混在 |

---

## 1. CMake上で`MELONPRIME_DS`を無効にできない

### Coreターゲット

`src/CMakeLists.txt`では、次の定義が無条件で付与されています。

```cmake
target_compile_definitions(core PUBLIC MELONPRIME_DS)
```

このため、core側のMelonPrimeコードを無効化する正式なビルド経路がありません。

### Qtフロントエンド

`src/frontend/qt_sdl/CMakeLists.txt`でも、次の定義が無条件です。

```cmake
add_compile_definitions(MELONPRIME_DS)
add_compile_definitions(MELONPRIME_CUSTOM_HUD)
```

さらに、以下も通常のQtフロントエンドターゲットへ直接組み込まれています。

- MelonPrime専用ソース一式
- MelonPrime専用Input Configソース
- MelonPrime HUD関連ソース
- MelonPrime patch関連ソース
- MelonPrime platform input関連ソース
- MelonPrime localization関連ソース

出力バイナリ名の変更も無条件です。

```cmake
set_target_properties(melonDS PROPERTIES OUTPUT_NAME melonPrimeDS)
```

したがって、現状では次のようなOFFビルドを構成できません。

```bash
cmake -DMELONPRIME_DS=OFF ...
```

正確には、`MELONPRIME_DS`を制御するCMake option自体が存在せず、常時ONです。

### Metal関連

Metal専用ソースについては、`MELONPRIME_METAL_ACTIVE`内で以下がまとまって制御されています。

- Metal compile definition
- Metal core sources
- Metal frontend sources
- Metal framework link
- Objective-C++ compile option

このファイル単位のゲートは概ね適切です。

---

## 2. `GPU3D_Compute.cpp`はアップストリーム実装を直接置換している

これは最も明確な違反です。

MelonPrime側では、アップストリームのタイル計算処理がコメントアウトされ、その直後に別の算術実装が配置されています。

実際の構造は概念的に次の状態です。

```cpp
// upstream処理をコメントアウト

// MelonPrime版を常時実行
```

`#ifdef MELONPRIME_DS`も`#else`もありません。

そのため、非MelonPrimeビルドを作ったとしても、Compute rendererの挙動はアップストリームmelonDSへ戻りません。

必要な構造は次です。

```cpp
#ifdef MELONPRIME_DS
    // MelonPrime版
#else
    // upstream melonDS版を原文どおり保持
#endif
```

このファイルは、**非MelonPrime時にもレンダラー挙動を変更する直接編集**です。

---

## 3. `DMA.cpp`も共有coreを無条件変更している

アップストリームでは、MRAM burst timing tableを通常の`std::array`代入で設定しています。

```cpp
MRAMBurstTable = DMATiming::MRAMDummy;
```

MelonPrime側では、次のような`__builtin_memcpy`へ置換されています。

```cpp
__builtin_memcpy(
    MRAMBurstTable.data(),
    DMATiming::MRAMDummy.data(),
    sizeof(MRAMBurstTable));
```

結果が実質的に同じ可能性はありますが、境界監査としては共有melonDSコードの無条件置換です。

この変更を維持する場合は、次のどちらかへ分類すべきです。

1. MelonPrime専用最適化として`#ifdef MELONPRIME_DS`と`#else`で分離する
2. melonDS全体へ適用する汎用修正として、MelonPrime変更とは別コミット・別台帳で管理する

現在はどちらにも明確に分離されていません。

---

## 4. `EmuInstance.cpp`は`MELONPRIME_DS`を外すとコンパイルできない

`ShouldMigrateLegacyHudAnchors()`はMelonPrime HUD用関数であり、定義自体は`#ifdef MELONPRIME_DS`内にあります。

```cpp
#ifdef MELONPRIME_DS

bool ShouldMigrateLegacyHudAnchors(Config::Table& cfg)
{
    ...
}

#endif
```

しかし、`EmuInstance`コンストラクタではこの関数を無条件に呼び出しています。

```cpp
if (ShouldMigrateLegacyHudAnchors(localCfg))
{
    ...
}
```

処理内部では、次のようなMelonPrime HUD専用シンボルも使用されています。

```cpp
MP_HUD_PROP_KEY_HudHpAnchor
MP_HUD_PROP_KEY_HudHpX
MP_HUD_PROP_KEY_HudTextScale
```

したがって`MELONPRIME_DS`を外すと、少なくとも次のエラーが発生します。

- `ShouldMigrateLegacyHudAnchors`が未定義
- `MP_HUD_PROP_KEY_*`が未定義

修正方法は、HUD移行処理全体をガードすることです。

```cpp
#ifdef MELONPRIME_DS
if (ShouldMigrateLegacyHudAnchors(localCfg))
{
    ...
}
#endif
```

この追加処理はMelonPrimeにしか存在しないため、`#else`は不要です。OFF時は何もしないのが正しい挙動です。

---

## 5. `EmuThread.cpp`にも非MelonPrimeビルドを壊す漏れがある

### `melonPrime`メンバーへの無条件アクセス

`EmuThread.h`では、`melonPrime`メンバーはガードされています。

```cpp
#ifdef MELONPRIME_DS
std::unique_ptr<MelonPrime::MelonPrimeCore> melonPrime;
#endif
```

しかし、`EmuThread.cpp`には次の無条件アクセスがあります。

```cpp
melonPrime->isFastForward = fastforward | slowmo;
```

`MELONPRIME_DS`を外すと、`melonPrime`メンバー自体が存在しないためコンパイルできません。

必要な修正は次です。

```cpp
#ifdef MELONPRIME_DS
melonPrime->isFastForward = fastforward | slowmo;
#endif
```

### デストラクタ宣言と実装の条件不一致

ヘッダーでは、デストラクタ宣言がMelonPrime時だけ存在します。

```cpp
#ifdef MELONPRIME_DS
~EmuThread();
#endif
```

一方、`.cpp`側のデストラクタ実装はガード外です。

```cpp
EmuThread::~EmuThread()
{
}
```

宣言と実装の条件を一致させる必要があります。

```cpp
#ifdef MELONPRIME_DS
EmuThread::~EmuThread()
{
}
#endif
```

または、デストラクタ自体を通常melonDS側でも常に宣言する設計へ戻す必要があります。

### 正しく分離されている部分

同じ`EmuThread.cpp`内でも、以下は概ね正しく`#ifdef/#else`されています。

- renderer選択
- shader compile判定
- input処理
- save flush
- VSync復帰
- frame limiter
- hotkey処理
- DSi volume sync回避
- auto screen sizing回避
- title表示

したがって、ファイル全体が崩れているのではなく、**一部のガード漏れがOFFビルド全体を破壊している状態**です。

---

## 6. `main.cpp`の低遅延設定が全ビルドへ漏れている

次の環境変数設定は`MELONPRIME_DS`の外にあります。

```cpp
qputenv("QSG_RHI_BACKEND", "d3d12");
qputenv("QSG_NO_VSYNC", "1");
qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
qputenv("QT_NO_ANTIALIASING", "1");
```

このため、非MelonPrimeビルドでも以下が強制されます。

- Qt Quick RHIをD3D12へ固定
- Qt Quick VSyncを無効化
- Qt高DPI scalingを無効化
- Qt anti-aliasingを無効化

アップストリームmelonDSでは、この位置で基本的に`QT_SCALE_FACTOR=1`とWindows theme設定のみです。

少なくとも次の条件へ変更する必要があります。

```cpp
#if defined(MELONPRIME_DS) && defined(_WIN32)
qputenv("QSG_RHI_BACKEND", "d3d12");
qputenv("QSG_NO_VSYNC", "1");
qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
qputenv("QT_NO_ANTIALIASING", "1");
#endif
```

D3D12指定はWindows専用なので、`MELONPRIME_DS`だけでなく`_WIN32`条件も必要です。

---

## 7. `ARMJIT.cpp`は動作上は逃がしているが、構造が不完全

現在のコードは概念的に次の構造です。

```cpp
bool melonPrimeHookRedirected = false;

#ifdef MELONPRIME_DS
melonPrimeHookRedirected = ...
#endif

if (!melonPrimeHookRedirected)
{
    // upstream処理
}
```

`MELONPRIME_DS`がOFFなら`melonPrimeHookRedirected`は常に`false`なので、実質的には元の処理が実行されます。

このため、挙動上の破壊ではありません。

ただし、アップストリームmelonDSは余分な変数や分岐を持たず、直接元処理を実行します。

厳密にOFF時のソース経路までアップストリームへ一致させるなら、次の構造が適切です。

```cpp
#ifdef MELONPRIME_DS
bool redirected = ...;
if (!redirected)
{
    // upstream処理
}
#else
// upstream処理を原文どおり実行
#endif
```

判定は**WARN**です。

---

## 8. 補助マクロが`MELONPRIME_DS`へ従属していない

複数の共有ファイルで、MelonPrime関連コードが次の補助マクロだけで制御されています。

```cpp
#ifdef MELONPRIME_CUSTOM_HUD
```

```cpp
#if defined(MELONPRIME_ENABLE_METAL)
```

例として、以下が該当します。

- custom HUDの型
- custom HUD overlay buffer
- GL overlay texture
- radar shader resource
- Metal renderer enum
- Metal frontend include
- Metal feature check
- developer feature用コード

現状のCMakeでは補助マクロがMelonPrimeビルドと同時に定義されるため、通常ビルドでは問題が表面化していません。

しかし、境界を強くするなら次のように統一すべきです。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_CUSTOM_HUD)
```

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_METAL)
```

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
```

これにより、補助マクロだけが誤って有効になった場合でも、MelonPrimeコードが通常melonDSへ漏れません。

---

## 9. `Window.cpp`にはMelonPrime以外の共有変更も混在している

`Window.cpp`では、多くのMelonPrime UI処理が正しく`#ifdef MELONPRIME_DS`へ分離されています。

例:

- MelonPrime menu
- menu localization
- Metroid settings actions
- runtime patch toggles
- MelonPrime title
- Metal screen panel
- localized dialog open helper

一方で、MelonPrimeとは直接関係しない共有変更も存在します。

例:

```cpp
ssize_t n = write(signalFd[0], &a, sizeof(a));
(void)n;
```

この変更は未使用戻り値警告を避ける一般的な修正であり、MelonPrime固有ではありません。

このような変更は次のどちらかへ分離すべきです。

- upstreamへ送れる汎用bugfix
- fork共通patchとしてMelonPrime変更とは別管理

「MelonPrimeコードをすべて`MELONPRIME_DS`へ隔離する」という監査対象とは種類が異なりますが、upstream merge時の差分ノイズと競合原因になります。

---

## 正しく分離されている主な箇所

すべてが崩れているわけではありません。以下は概ね適切です。

### `ARM.cpp`

ARM interpreter hookとアップストリーム処理が明示的な`#ifdef/#else`になっています。

```cpp
#ifdef MELONPRIME_DS
    // MelonPrime hook
#else
    // original melonDS condition execution
#endif
```

### `NDS.h` / `NDS.cpp`

ARM9 instruction hook関連の以下がガードされています。

- public type
- fields
- methods
- implementation include

### `ARMJIT_x64`

以下のhook integrationがガードされています。

- trampoline
- compile loop
- method

### `Config.cpp`

MelonPrime用デフォルトとアップストリーム用デフォルトが`#else`で分離されています。

```cpp
#ifdef MELONPRIME_DS
{"3D.Renderer", renderer3D_OpenGL},
{"3D.GL.ScaleFactor", 4},
#else
{"3D.Renderer", renderer3D_Software},
{"3D.GL.ScaleFactor", 1},
#endif
```

画面filter設定についても、非MelonPrime側の元設定が保持されています。

### `EmuInstanceInput.cpp`

以下について、MelonPrime側と通常melonDS側が分離されています。

- keyboard key handling
- modifier handling
- mouse button hotkey
- 64bit hotkey mask
- joystick polling fast path
- original melonDS inputProcess
- analogue hotkey helper

### `OSD_shaders.h`

MelonPrime shaderとアップストリームshaderが明示的に分離されています。

### Input Config関連

`MapButton.h`と`InputConfigDialog.*`では、以下が概ね分離されています。

- MelonPrime mouse binding
- MelonPrime modifier binding
- MelonPrime専用tabs
- 通常melonDS keypad page
- 通常melonDS add-on page
- 通常melonDS key mapping behavior

---

## 総合判定

| 項目 | 判定 |
|---|---|
| MelonPrime固有コードが全部ガードされているか | **NO** |
| `MELONPRIME_DS` OFFでアップストリームmelonDSへ戻るか | **NO** |
| OFFビルドをCMakeから選べるか | **NO** |
| 手動で定義を外せばコンパイルできるか | **NO** |
| core rendererへ無条件変更があるか | **YES** |
| frontendの通常melonDS挙動を無条件変更しているか | **YES** |
| 正しい`#ifdef/#else`実装も存在するか | **YES** |
| 専用MetalファイルのCMakeゲート | **概ねPASS** |

既存台帳では、MelonPrime専用ファイルを除いても多数の共有差分があります。

ただし、その多くは次の分類です。

- documentation
- assets
- CI
- build metadata
- formatting
- fork共通patch

実際のランタイム上の危険箇所は、主に以下へ集中しています。

- core
- CMake
- `EmuInstance`
- `EmuThread`
- `main.cpp`
- renderer
- Screen/HUD補助マクロ

---

## 修正優先順位

1. **CMakeへ正式なMelonPrime build optionを追加する**
2. MelonPrime OFF時は専用ソース、定義、リソース、出力名変更を完全除外する
3. `EmuInstance.cpp`のHUD移行処理をガードする
4. `EmuThread.cpp`のデストラクタと`melonPrime`アクセスをガードする
5. `main.cpp`のQt環境変数をMelonPrimeかつWindows限定にする
6. `GPU3D_Compute.cpp`へ正確なアップストリーム`#else`を復元する
7. `DMA.cpp`をアップストリームへ戻すか、汎用修正として別管理する
8. `ARMJIT.cpp`のOFF経路をアップストリーム原文と一致させる
9. HUD・Metal・developer用補助マクロをすべて`MELONPRIME_DS`へ従属させる
10. CIでMelonPrime ONとOFFの両方を毎回ビルドする

---

## 推奨CMake構造

例として、次のような正式なbuild optionを追加します。

```cmake
option(MELONPRIME_BUILD "Build melonPrimeDS integration" ON)

if (MELONPRIME_BUILD)
    target_compile_definitions(core PUBLIC MELONPRIME_DS)
endif()
```

Qtフロントエンド側も同様に分離します。

```cmake
if (MELONPRIME_BUILD)
    target_compile_definitions(melonDS PRIVATE
        MELONPRIME_DS
        MELONPRIME_CUSTOM_HUD
    )

    target_sources(melonDS PRIVATE
        ${MELONPRIME_COMMON_SOURCES}
        ${MELONPRIME_PLATFORM_SOURCES}
    )

    set_target_properties(melonDS PROPERTIES OUTPUT_NAME melonPrimeDS)
else()
    set_target_properties(melonDS PROPERTIES OUTPUT_NAME melonDS)
endif()
```

MelonPrime専用ファイル一覧を、通常の`SOURCES_QT_SDL`へ直接混ぜず、別リストに分離することも重要です。

```cmake
set(MELONPRIME_COMMON_SOURCES
    MelonPrime.cpp
    MelonPrimeLifecycle.cpp
    MelonPrimeRuntimeConfig.cpp
    ...
)
```

---

## 推奨CI構成

最低でも、次の2構成を毎回ビルドする必要があります。

### MelonPrime ON

```bash
cmake -S . -B build-melonprime \
    -DMELONPRIME_BUILD=ON
cmake --build build-melonprime
```

### MelonPrime OFF

```bash
cmake -S . -B build-upstream-compatible \
    -DMELONPRIME_BUILD=OFF
cmake --build build-upstream-compatible
```

OFFビルドでは、少なくとも以下を検査します。

- MelonPrime headerがincludeされていない
- MelonPrime sourceがcompileされていない
- `MELONPRIME_DS`が定義されていない
- 出力名が`melonDS`
- 通常melonDS UIが維持される
- 通常melonDS input pathが維持される
- 通常melonDS renderer pathが維持される
- 通常melonDS shaderが使用される
- 通常melonDS config defaultsが使用される

---

## 最終結論

現状は、**MelonPrimeコードを通常melonDSから完全に隔離できていません。**

特に次の問題は確定です。

- CMakeでMelonPrimeを無効化できない
- `GPU3D_Compute.cpp`がアップストリーム実装を無条件置換している
- `DMA.cpp`がアップストリーム実装を無条件置換している
- `EmuInstance.cpp`にガード外のMelonPrime HUD処理がある
- `EmuThread.cpp`にガード外の`melonPrime`アクセスがある
- `main.cpp`にガード外のMelonPrime低遅延設定がある
- HUD、Metal、developer補助マクロが`MELONPRIME_DS`へ完全には従属していない

したがって、現在のコードは次の条件を満たしていません。

> `MELONPRIME_DS`を外せば、同一コードベースからアップストリーム相当のmelonDSへ安全に戻れること。

この条件を満たすには、まずCMakeのON/OFF構成を正式化し、そのOFF構成をCIで継続的にビルドする必要があります。
