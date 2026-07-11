# MelonPrime固有コード限定  
# Dolphin比較監査・修正計画

- 対象リポジトリ: <https://github.com/ag-advania/melonPrimeDS>
- 対象ブランチ: <https://github.com/ag-advania/melonPrimeDS/tree/highres_fonts_v3>
- 比較対象: <https://github.com/dolphin-emu/dolphin>
- 作成日: 2026-07-11
- 適用方針: **melonDS本体由来の処理は原則変更せず、MelonPrime固有コードだけを修正する**

---

## 1. この文書の方針

前回の比較監査では、MelonPrime固有コードとmelonDS本体由来の共通コードをまとめて改善候補として扱っていた。

本計画では、修正範囲を次のように限定する。

### 1.1 原則として修正対象にするもの

- `MelonPrime*.cpp`
- `MelonPrime*.h`
- `MelonPrime*.mm`
- `MelonPrime*.inc`
- `InputConfig/MelonPrime*.cpp`
- `InputConfig/MelonPrime*.h`
- 共有ファイル内の`#ifdef MELONPRIME_DS`ブロック
- MelonPrime専用の新規helper／service／stateクラス
- MelonPrime固有処理から呼ばれる、安全なadapterまたはmailbox

### 1.2 原則として修正対象にしないもの

- melonDS上流由来の`Config.cpp`／`Config.h`の基本設計
- melonDS共通のTOML保存方式
- melonDS共通のframe limiter
- melonDS共通のNDS core動作
- melonDS共通の通常入力処理
- melonDS共通のwindow／screen layout処理
- melonDS共通rendererの大規模な再設計
- Dolphinの構造を理由にした上流melonDSコードの全面置換

### 1.3 例外

MelonPrime側だけでは安全に解決できず、melonDS共通コードへの変更が必要になった場合は、次の条件を満たす別案件として扱う。

1. MelonPrime専用adapterでは解決できない理由を明記する
2. 上流melonDSとの差分を最小化する
3. `#ifdef MELONPRIME_DS`で隔離できるか先に検討する
4. MelonPrime無効ビルドへ影響しないことを確認する
5. この文書の実装Phaseには混ぜない

---

# 2. 結論

MelonPrime固有コードだけに限定した優先順位は次のとおり。

| 優先度 | 修正対象 | 判定 |
|---|---|---|
| P0 | ARM9 Hookのprocess共有dispatch state | 複数instanceで誤ったhook tableを参照する可能性 |
| P0 | Patch moduleのprocess共有runtime state | 別instanceのapply／restore状態が混線する可能性 |
| P0 | Custom HUDのprocess共有mutable state | 設定・解像度・editor・dirty stateがinstance間で混線 |
| P0 | Zoom capability cacheのfile-static共有 | 別instanceのweapon判定結果を再利用する可能性 |
| P0 | MelonPrime Raw Input共有singleton | 入力差分消失、window target／binding上書きの可能性 |
| P0 | MelonPrime GUI／EmuThread境界 | data raceおよびQt／AppKit操作thread違反の可能性 |
| P1 | MelonPrime側のConfig利用方法 | EmuThreadからの即時保存を停止する |
| P1 | macOS Raw Input callback lifetime | 停止時のcallback完了待ちとowner管理が必要 |
| P2 | Linux Raw Input初期化待ち | constructorでの最大約500ms待機を非同期化 |
| P2 | MelonPrime Metal／cursor backendのplatform lifecycle | MelonPrime専用範囲で追加検証 |

最優先事項は、次の原則をMelonPrime全体へ適用すること。

```text
process-globalに置いてよいもの
    = immutableな定数、table、shader、読み取り専用asset

MelonPrimeCore／EmuInstanceごとに持つもの
    = ROM、RAM、hook、patch、HUD、入力consumer、editor、transition状態
```

---

# 3. P0: ARM9 Hookのdispatch stateをinstanceごとに分離する

## 3.1 現状

`MelonPrimeArm9Hook.cpp`には、以下のfile-static mutable stateがある。

- `s_dispatchEntries`
- `s_dispatchCount`
- `s_lastDispatchAddress`
- `s_lastDispatchMask`

参照:

- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeArm9Hook.cpp>

各NDS instanceにはcallbackと`MelonPrimeCore*`が登録されるが、callbackが検索するdispatch tableはprocess共通である。

別instanceで`ARM9Hook_Install()`が呼ばれると、共有tableが消去・再構築される。

## 3.2 想定される問題

```text
instance A:
US 1.0用hook addressを登録

instance B:
JP 1.1用hook addressを登録
→ process共有dispatch tableがJP用に置換

instance A:
callbackは残っているが、JP用dispatch tableを検索
```

設定の異なる同一ROM同士でも、登録maskが異なれば同様の問題が起こり得る。

## 3.3 修正方針

新しいinstance-owned stateを導入する。

```cpp
struct MelonPrimeArm9HookState
{
    DispatchEntry entries[melonDS::NDS::ARM9InstructionHookMaxAddresses];
    uint32_t count;
    uint32_t lastAddress;
    uint16_t lastMask;
};
```

配置候補:

```text
MelonPrimeCore
└─ MelonPrimeArm9HookState m_arm9HookState
```

または:

```text
MelonPrimeInstanceContext
└─ MelonPrimeArm9HookState arm9Hook
```

callbackでは`userdata`の`MelonPrimeCore*`からstateを取得する。

## 3.4 実装上の原則

- `DispatcherCallback`自体はstatic関数のままでよい
- dispatch tableだけをinstance memberへ移す
- `FindDispatchMask()`はstate引数を取る
- `ClearDispatchEntries()`は対象instanceだけを消す
- `ARM9Hook_Uninstall()`が他instanceのstateを消さない
- ROMごとのaddress tableなどimmutable dataは共有のままでよい
- melonDSの`NDS::SetARM9InstructionHook()`自体は変更しない

## 3.5 完了条件

- AとBで異なるROMを同時起動できる
- Bのhook install後もAのdispatch count／maskが変化しない
- A停止後もBのhookが動作する
- AのConfig ReloadがBのhook tableへ影響しない
- 単一instanceの性能に有意な回帰がない

---

# 4. P0: Patch runtime stateをinstanceごとに分離する

## 4.1 現状

MelonPrimeの各Patch moduleには、次のようなfile-static mutable stateが存在する可能性がある。

- `s_applied`
- `s_originalInstruction`
- `s_originalValue`
- restore bookkeeping
- runtime hook state
- 現在のROM group
- 現在のConfig pointer

例:

- `MelonPrimePatchAspectRatio.cpp`
- `MelonPrimePatchOsdColor.cpp`
- `MelonPrimePatchInstantAimFollow.cpp`
- `MelonPrimePatchShadowFreezeRuntimeHook.cpp`
- `MelonPrimePatchFixNoxusBladePersistence.cpp`
- その他`MelonPrimePatch*.cpp`

Patch Registryにはprocess-static stateを前提にした記述があるが、melonPrimeDSは同一process内に複数`EmuInstance`を持てる。

参照:

- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimePatchRegistry.cpp>
- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/main.cpp>

## 4.2 修正方針

各Patch moduleへ個別のglobal stateを残すのではなく、instanceごとの集約stateを作る。

```text
MelonPrimePatchState
├─ aspectRatio
├─ osdColor
├─ lowHpWarning
├─ instantAimFollow
├─ showHeadshot
├─ showEnemyHp
├─ disableDoubleDamage
├─ noSpecificItemPickup
├─ shadowFreeze
├─ noxusBlade
└─ ...
```

各moduleのinterface例:

```cpp
void Apply(const PatchCtx& ctx, PatchModuleState& state);
void Restore(const PatchCtx& ctx, PatchModuleState& state);
void Reset(PatchModuleState& state);
```

## 4.3 段階的な移行

一度に全Patchを書き換えず、次の順で移行する。

1. 実際にmutable staticを持つmoduleを一覧化
2. 元命令／適用状態を持つmoduleを優先
3. stateをstruct化
4. `PatchCtx`に`MelonPrimePatchState&`を追加
5. Registry adapterから各stateを渡す
6. static stateを削除
7. multi-instance試験を追加

## 4.4 melonDS側を変更しないための制約

- ARM RAM read/write APIは既存のまま使用
- `NDS` classへMelonPrime専用stateを追加しない
- `MelonPrimeCore`またはMelonPrime専用contextが所有する
- Config基盤へstateを保存しない
- Patch address定数は共有可能

## 4.5 完了条件

- AのPatch適用がBのskip条件へ影響しない
- A停止時のrestoreがBのRAMへ書き込まない
- BのConfig Reload後もAのbookkeepingが維持される
- ROM未検出instanceのresetが他instanceへ影響しない

---

# 5. P0: Custom HUD stateをinstanceごとに分離する

## 5.1 現状

Custom HUDには、多数のfile-static mutable stateがある。

例:

- `CachedHudConfig`
- config cache epoch
- crosshair transition state
- dirty rect accumulator
- previous dirty rect
- editor mode
- editor rect
- editor対象ROM
- editor対象address
- font runtime cache
- image tint cache
- radar cache
- zoom reticle state

参照:

- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeHudRender.cpp>
- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeHudRenderConfig.inc>
- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeHudRenderMain.inc>
- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenUnity.inc>

## 5.2 想定される問題

- AのHUD設定cacheをBが使用する
- Aのwindow scaleでBのcrosshair geometryを構築する
- AのHUD editor状態がBへ反映される
- Aのdirty rectをBがclearする
- 同時描画でfile-static stateにdata raceが起こる
- 異なるfont、asset、hunter colorが混線する

## 5.3 修正方針

stateを用途別に分割する。

```text
MelonPrimeHudState
├─ configCache
├─ configEpoch
├─ runtimeState
├─ crosshairState
├─ dirtyState
├─ editorState
├─ fontState
└─ assetView
```

`assetView`のうち完全に読み取り専用で、設定やGPU／QImage mutationを伴わないものだけprocess共有を許可する。

## 5.4 API変更例

現状:

```cpp
QRect CustomHud_Render(
    EmuInstance* emu,
    Config::Table& cfg,
    ...
);
```

変更案:

```cpp
QRect CustomHud_Render(
    MelonPrimeHudState& hud,
    EmuInstance* emu,
    const RuntimeHudConfigSnapshot& cfg,
    ...
);
```

## 5.5 Configに関する制約

- `Config.cpp`／`Config.h`は変更しない
- HUD描画中に`Config::Table`を大量に参照しない
- GUI側またはConfig reload cold pathでsnapshotを作る
- Emu／render pathはsnapshotを読む
- snapshot generationはMelonPrime側で管理する

## 5.6 完了条件

- AとBで異なるHUD設定を同時使用できる
- Aのeditor起動がBへ影響しない
- 異なるDPI／window sizeでcacheが独立する
- ThreadSanitizerでHUD static由来のraceが出ない
- 単一instanceで見た目が変わらない

---

# 6. P0: Zoom capability cacheをMelonPrimeCoreへ移す

## 6.1 現状

`MelonPrimeGameInput.cpp`の`ZoomCapabilityCache`がfile-staticになっている。

参照:

- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeGameInput.cpp>
- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeZoomStatus.h>

cache keyはplayer addressとweapon addressである。

別instanceでもDS仮想addressは同じ値になり得るため、別RAMの結果を再利用する可能性がある。

## 6.2 修正方針

`MelonPrimeCore` memberへ移す。

```cpp
ZoomStatus::ZoomCapabilityCache m_zoomAimCanZoomCache;
```

reset条件:

- ROM変更
- emulation start
- reset
- game leave
- local player pointer invalid
- weapon pointer invalid
- Config reloadで関連機能をOFF
- savestate load後の必要なタイミング

## 6.3 完了条件

- Aのzoom可能weapon判定がBへ影響しない
- savestate／ROM切り替え後に古いcacheを使わない
- scope表示とaim scaleが一致する

---

# 7. P0: MelonPrime Raw Inputをserviceとsubscriptionへ分離する

## 7.1 現状

Windows、macOS、LinuxのMelonPrime Raw Inputはprocess共有singletonに近い構造を持つ。

### Windows

共有objectへ以下が集約されている。

- device登録先window
- Joy2Key mode
- hotkey binding
- edge state
- mouse delta consumer state

参照:

- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.cpp>
- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeRawInputState.h>

### macOS／Linux

OS event累積値だけでなく、`lastReadX/Y`も共有されている。

参照:

- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeRawInputMacFilter.mm>
- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeRawInputLinuxFilter.cpp>

## 7.2 問題

- Aが先にdeltaを読むとBのdeltaが消える
- 後からAcquireしたwindowが登録先を奪う
- hotkey bindingが別instanceの設定で上書きされる
- focus loss resetが全consumerへ影響する
- process-global cursor captureにownerがない
- 複数threadからのconsumer readでraceが起こる

## 7.3 Dolphinから採用する設計

参考:

- <https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/InputCommon/ControllerInterface>

採用するのはDolphinの具体的なdevice mappingではなく、次の責務分離。

```text
PlatformInputService
├─ OS eventを1回だけ収集
├─ monotonic counter
├─ physical button state
├─ hotplug state
└─ active capture owner

MelonPrimeInputSubscription
├─ instance ID
├─ lastRead counter
├─ hotkey snapshot
├─ previous edge state
└─ focus／generation
```

## 7.4 active owner

FPS mouse aimを実際に消費できるinstanceは、原則1つに限定する。

owner選択条件:

1. windowがactive
2. panelがvisible
3. MelonPrime aim mode
4. cursor capture requested
5. modal windowにblockされていない
6. instance generationが有効

owner変更時:

- 旧ownerのpending deltaを破棄
- edge stateを同期
- 新ownerのlastReadを現在counterへ合わせる
- 古い移動が新ownerへ流れないようにする

## 7.5 melonDS側を変更しないための制約

- melonDS標準input systemへ統合しない
- MelonPrime Raw Input files内で完結させる
- `EmuInstance`への追加はMelonPrime compile guard内
- 既存melonDS hotkey経路を壊さない
- Joy2Key互換動作は維持する

## 7.6 完了条件

- A/B同時起動でもactive windowだけがmouse deltaを受け取る
- focus切り替え直後に古いdeltaが流れない
- Bの起動でAのwindow registrationが破壊されない
- hotkey bindingがinstanceごとに独立する
- key-up／mouse-upが失われない
- Windows、macOS、X11、Waylandで同じowner semanticsになる

---

# 8. P0: MelonPrime固有のGUI／EmuThread境界を整理する

## 8.1 対象

MelonPrime固有コードからGUI objectへ直接アクセスする経路。

例:

- `MelonPrimeCore`から`ScreenPanel`を直接参照
- EmuThreadからcursor containmentを実行
- EmuThreadから`QWidget::mapToGlobal()`へ到達
- MelonPrimeのwheel delta消費
- GUIとEmuThreadがMelonPrime mode flagを直接共有

## 8.2 修正方針

次の3種類に分離する。

```text
MelonPrimeEmuRuntimeState
    owner: EmuThread
    ROM／game／aim／patch state

MelonPrimeUiSnapshot
    producer: EmuThread
    consumer: GUI thread
    atomicまたはdouble-buffer snapshot

MelonPrimeUiCommandMailbox
    producer: GUI thread
    consumer: EmuThread
    mode変更、capture request、editor command
```

cursor再センタリングなどGUI操作は逆方向のmailboxを使う。

```text
MelonPrimeGuiRequestMailbox
    producer: EmuThread
    consumer: GUI thread
    recenter、capture refresh、cursor visibility update
```

## 8.3 Qt／AppKit操作

以下はGUI／main threadだけで行う。

- `QWidget`操作
- `QCursor`
- `mapToGlobal`
- `grabMouse`／`releaseMouse`
- native window handleのGUI-side更新
- macOS cursor association API
- modal／window activation stateの評価

EmuThreadはrequest bitまたは座標snapshotだけを更新する。

## 8.4 共有bool

次のような値を、所有者不明の通常`bool`として共有しない。

- cursor mode
- clip wanted
- stylus mode
- aim active
- layout change request
- editor active
- capture suspended

単純にすべて`std::atomic<bool>`へ変更するだけでなく、読み書き方向を決める。

## 8.5 wheel入力

melonDS共通のConfigやinput systemを変更せず、MelonPrimeが必要とするwheel操作だけをMelonPrime mailboxへ蓄積する。

例:

```cpp
std::atomic<int> melonPrimeWheelSteps{0};
```

GUI event:

```cpp
melonPrimeWheelSteps.fetch_add(steps, std::memory_order_release);
```

EmuThread:

```cpp
const int steps =
    melonPrimeWheelSteps.exchange(0, std::memory_order_acq_rel);
```

共有`ScreenPanel`全体の設計変更は避け、`#ifdef MELONPRIME_DS`またはMelonPrime helperで隔離する。

## 8.6 完了条件

- EmuThreadからQWidget／AppKit APIを呼ばない
- GUI threadからMelonPrime emulation stateを直接書かない
- Alt+Tab／modal／fullscreen復帰が安定する
- ThreadSanitizerで対象flagのraceが出ない
- cursor capture latencyが悪化しない

---

# 9. P1: Config基盤を変更せず、MelonPrime側の利用だけ修正する

## 9.1 方針

`Config.cpp`と`Config.h`はmelonDS本体由来のため、今回の修正対象外とする。

変更しないもの:

- `RootTable`
- `Config::Table`
- getterのdefault補完
- `Config::Save()`の内部実装
- TOMLの直接保存方式
- melonDS全体のConfig lock設計

## 9.2 MelonPrime固有の問題

MelonPrimeの感度hotkey処理は、frame／EmuThread経路から次を実行する。

```cpp
localCfg.SetInt(CfgKey::AimSens, next);
Config::Save();
RecalcAimSensitivityCache(localCfg);
```

参照:

- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrime.cpp>

問題:

- frame処理中にdisk I/Oが入る
- GUI設定変更と同時にConfigへ触れる可能性
- Configの保存責務がEmuThreadへ入る
- input hotkeyと永続設定が密結合
- sensitivity変更が毎回同期保存になる

## 9.3 修正方針

### Runtime値

感度hotkeyでは、まず`MelonPrimeCore`内のruntime sensitivityだけを更新する。

```text
m_runtimeAimSensitivity
m_aimFixedScaleX
m_aimFixedScaleY
```

### 永続化要求

EmuThreadからGUIへ、MelonPrime専用requestを送る。

```cpp
struct MelonPrimePersistRequest
{
    enum class Type
    {
        AimSensitivity
    };

    Type type;
    int value;
    uint64_t generation;
};
```

GUI thread側で:

1. 対象instanceがまだ存在するか確認
2. 最新generationか確認
3. `localCfg.SetInt()`を実行
4. debounce後に既存`Config::Save()`を呼ぶ

## 9.4 debounce

例:

```text
hotkey入力
→ runtime値は即時反映
→ 500～1000msのsave timerを再開始
→ 最後の変更だけ保存
```

アプリ終了時はpending saveをflushする。

## 9.5 既存RuntimeConfigSnapshotの活用

MelonPrimeには既にruntime snapshotとconfig reload pendingがある。

参照:

- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeRuntimeConfig.h>
- <https://github.com/ag-advania/melonPrimeDS/blob/highres_fonts_v3/src/frontend/qt_sdl/MelonPrimeLifecycle.cpp>

全面的なConfig改修ではなく、この仕組みを以下へ広げる。

- sensitivity runtime override
- HUD config snapshot
- input method snapshot
- cursor policy snapshot
- patch option snapshot

## 9.6 完了条件

- EmuThreadから`Config::Save()`を呼ばない
- hotkey感度変更は即時反映される
- 連打しても保存は最後に1回だけ
- GUIで変更した値と競合しない
- 再起動後に最後の値が保持される
- `Config.cpp`／`Config.h`に差分がない

---

# 10. P1: macOS Raw Input callback lifetimeを安全化する

## 10.1 現状

`MelonPrimeRawInputMacFilter.mm`では、GCMouse handlerや接続通知が実装objectをcaptureする。

停止時にhandler解除とobserver削除を行っても、実行中callbackの完了前に実装objectが解放される可能性を除外できない。

## 10.2 修正方針

- serial callback queue
- `stopping` atomic flag
- in-flight callback count
- teardown barrier
- callback lifetime token
- deviceごとのfractional residual
- capture owner ID

例:

```text
Stop requested
→ stopping = true
→ 新規callbackは処理をskip
→ handler／observer解除
→ serial queue barrier
→ in-flight = 0を確認
→ object解放
```

## 10.3 制約

- melonDSのmacOS input backendは変更しない
- MelonPrimeの`.mm`ファイル内で完結させる
- main threadが必要なAPIはmain queue経由
- cursor captureのownerはMelonPrime serviceで管理する

## 10.4 完了条件

- mouse接続／切断中に終了してもcrashしない
- ROM停止とwindow closeが重なってもcrashしない
- callbackが解放済みobjectへアクセスしない
- mouse deltaのfractional carryがdevice間で混ざらない

---

# 11. P2: Linux Raw Input初期化を非同期化する

## 11.1 現状

`MelonPrimeRawInputLinuxFilter.cpp`の初期化は、worker開始後にavailabilityを一定回数pollして待つ。

これにより、環境によってはinstance開始時に最大約500ms停止する可能性がある。

## 11.2 修正方針

constructorはworker起動後すぐ返す。

```cpp
enum class BackendState
{
    Starting,
    Available,
    Unavailable,
    Failed
};
```

consumer側:

- `Starting`ではQt fallbackを使用
- `Available`になったframeからraw inputへ切り替える
- 切り替え時にlastReadを現在counterへ同期
- `Unavailable`／`Failed`ではfallbackを継続
- 一定時間ごとの再初期化は必要性を検討する

## 11.3 Dolphinから採用する考え方

device populationやbackend probeで、host／emulation threadを不必要に停止させない。

参考:

- <https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/InputCommon/ControllerInterface>

## 11.4 完了条件

- constructorで長時間待たない
- backend準備前もQt fallbackで操作可能
- raw input開始時にcursor jumpが起こらない
- backend失敗でもゲーム起動を妨げない
- Wayland／X11判定が変わっても安全に再評価できる

---

# 12. P2: MelonPrime専用platform backendの追加監査

この項目はmelonDS共通描画処理を変更するものではない。

対象:

- `MelonPrimeScreenMetal.mm`
- `MelonPrimeWaylandPointerLock.cpp`
- `MelonPrimeScreenCursorPolicy.cpp`
- MelonPrime固有のOpenGL／Metal integration
- `MELONPRIME_DS`分岐内のsurface／cursor処理

確認項目:

- window移動時のdevice pixel ratio
- screen変更時のdrawable再作成
- fullscreen切り替え
- WinId変更
- Metal layer lifetime
- Wayland surface generation
- focus／modal／hideでのcapture解除
- capture再取得のevent順序
- backend破棄時のcallback解除

共通`Screen.cpp`へ変更が必要な場合は、まずMelonPrime helperと`#ifdef MELONPRIME_DS`で隔離する。

---

# 13. 修正対象外

次の項目は今回のMelonPrime固有修正計画から除外する。

## 13.1 melonDS Config基盤

除外:

- Config全体への`shared_mutex`
- getterの非破壊化
- `RootTable`の全面置換
- temp file＋renameへの共通保存方式変更
- melonDS全設定のruntime layer化

MelonPrime側は利用方法だけを修正する。

## 13.2 melonDS共通frame limiter

除外:

- `EmuThread.cpp`の共通limiter全面改修
- melonDS全体のsleep／busy-spin policy変更
- battery policyの共通導入

MelonPrime固有コードが独自に追加した待機処理がある場合だけ別途対象にする。

## 13.3 melonDS共通window／DPI処理

除外:

- 通常のmelonDS window lifecycle全面改修
- 全renderer共通surface management変更
- 全platform共通DPI処理変更

MelonPrime Metal／Wayland／cursor integration内の問題だけ修正する。

## 13.4 melonDS core

除外:

- NDS classへのMelonPrime runtime state追加
- ARM9 coreの一般hook architecture変更
- JIT共通仕様の変更
- input coreの一般再設計

MelonPrimeのhook userdata／context内で解決する。

---

# 14. 実装Phase

## Phase 0: 検証基盤

進捗: **完了 (2026-07-11)**

- developer buildへinstance ID／core address付きlifecycle logを追加
- GUI thread／EmuThread ownership checkを追加
- `MELONPRIME_STRICT_THREAD_ASSERTS=1`でthread違反をassertion化可能
- process-global mutable state監査を
  `.claude/skills/audit-melonprime-instance-state.ps1`へ固定
- A/B再現手順と保存すべき証跡を
  `src/frontend/qt_sdl/tests/melonprime-multi-instance-repro.md`へ固定
- sanitizer optionは既存のtop-level `include(Sanitizers)`を継続利用

コード挙動を変える前に追加する。

- multi-instance test harness
- instance ID付きdebug log
- hook table dump
- patch state dump
- active input owner表示
- HUD state address／generation表示
- Config save thread assertion
- GUI API thread assertion
- sanitizer build option

完了条件:

- A/Bのstateがログ上で区別できる
- process-global mutable stateの利用箇所を一覧化できる
- 修正前の再現ケースを保存できる

## Phase 1: 小さく分離できるstate

進捗: **完了 (2026-07-11)**

- Zoom capability cacheを`MelonPrimeCore` memberへ移動し、boot／stop／ROM検出でreset
- ARM9 dispatch entries／count／last-hit cacheをinstance-owned stateへ移動
- 単純write patchのapplied／ROM group／dirty bookkeepingを
  `MelonPrimePatchState`へ集約
- HUD config cache／epochをinstance-owned opaque stateへ移動し、render／editorへ明示伝播
- developer logへhook／patch／HUD state addressをinstance ID付きで追加
- mutable-state監査ratchetを199件から180件へ削減

1. Zoom capability cache
2. ARM9 dispatch state
3. Patch stateのうち単純bool／original value
4. HUD config cache

特徴:

- 挙動を変えず、所有場所だけを変える
- 回帰範囲が比較的小さい
- multi-instance改善効果が大きい

## Phase 2: HUD完全分離

進捗: **完了 (2026-07-11)**

- HUD config stateをinstance-owned containerへ拡張し、editor／dirty／crosshair transitionを集約
- font／image／radar／text cacheとzoom reticle／battle frame stateをinstanceごとに分離
- software／OpenGL overlay dirty・upload stateを`Screen` instanceへ移動
- no-HUD patch bookkeepingを`NoHudPatchState`としてHUD stateへ所有させ、呼び出し境界で明示伝播
- HUD render／editor entry pointへinstance state scopeを設定し、multi-instance間の参照を遮断
- HUD golden harnessの既存hash一致、Windows MinGW build、全CI auditを確認
- mutable-state監査ratchetを180件から35件へ削減（HUD runtimeは0件）

- editor state
- dirty state
- crosshair transition
- font／image runtime cache
- zoom reticle state
- no-HUD patch bookkeepingとの境界

## Phase 3: Input service／subscription

進捗: **完了 (2026-07-11)**

- `PlatformInputOwnerService`とinstance-owned `MelonPrimeInputSubscription`を追加
- process-wide OS collectorとper-instance delta cursor／focus generation／owner stateを分離
- Windows Raw Inputへinstance別`InputState`を登録し、hotkey binding／edge stateを独立化
- active ownerだけがwindow registration／Qt native filter／mouse deltaを利用するよう統一
- owner切替時に旧stateを破棄し、新ownerのphysical key stateとmonotonic cursorを同期
- macOS／Linux accumulatorを単調64-bit counter化し、fetch/resetをsubscription cursor基準へ変更
- start／stop／focus／capture切替でownerを解放し、instance ID付きgeneration logを追加
- Windows MinGW build、HUD golden、全CI auditを確認
- mutable-state監査ratchetを35件から22件へ削減

- process-global collector
- per-instance consumer cursor
- active owner
- hotkey snapshot
- focus generation
- platformごとのcapture backend

## Phase 4: Thread境界

進捗: **完了 (2026-07-11)**

- `MelonPrimeThreadBridge`へGUI→Emu command/input mailboxとEmu→GUI runtime snapshot／requestを実装
- focus／capture／cursor mode／layout generation／native window handleを方向付きatomic stateへ分離
- wheel stepとWayland／Qt panel deltaをGUI producer・Emu consumerのexchange mailboxへ移動
- center座標をGUI側でpublishし、EmuThreadから`QWidget::mapToGlobal()`／`QCursor`／panel参照を除去
- cursor show／hide／recenter／capture refreshをGUI request bitとしてcoalesceし、draw時にGUI threadで処理
- GUI側のcursor／stylus／in-game／ROM／fast-forward／screen-sync参照をruntime snapshotへ移行
- macOSの非Raw fallback delta生成もGUI mouse event側へ移動
- `.claude/skills/audit-melonprime-thread-boundary.ps1 -Strict`で直接GUI API／legacy state参照0件を固定
- Windows MinGW build、HUD golden、全CI auditを確認

- GUI／Emu snapshot
- command mailbox
- cursor GUI request
- wheel mailbox
- shared boolの所有権整理

## Phase 5: Config利用方法

進捗: **完了 (2026-07-11)**

- 感度hotkeyをEmuThread-owned runtime値へ移し、frame pathから
  `Config::Table`のread／writeと`Config::Save()`を除去
- `MelonPrimeThreadBridge`へ世代付き・latest-winsの
  `AimSensitivity`永続化mailboxを追加
- primary `ScreenPanel`がGUI threadで要求を消費し、750ms debounce後に
  `localCfg.SetInt()`と既存`Config::Save()`を実行
- 連続hotkey入力はruntimeへ即時反映し、disk保存は最後の値へ集約
- 設定画面のOK保存時は古いmailbox要求とpending timerを破棄し、
  明示的なGUI設定を優先
- primary window／app終了時は未保存要求を消費して同期flush
- `RuntimeConfigSnapshot`へ`AimConfigSnapshot`を統合し、
  full reload時のConfig readを単一Load／Apply境界へ集約
- `Config.cpp`／`Config.h`およびmelonDS共通保存実装は変更なし
- multi-instanceではcore別mailbox／primary panel別timerとして分離

完了項目:

- EmuThread保存除去
- runtime sensitivity
- GUI-side debounce save
- RuntimeConfigSnapshot拡張

## Phase 6: platform hardening

- macOS callback drain
- Linux async initialization
- Wayland／X11 owner transition
- Metal／cursor lifecycle test

---

# 15. 必須回帰試験

## 15.1 Multi-instance

```text
A = US 1.0
B = JP 1.1

A/Bで別Config
A/Bで別HUD
A/Bで別window size
A/Bで別hook option
A/Bで別weapon
```

操作:

1. A起動
2. B起動
3. Aだけmatch開始
4. Bだけmatch開始
5. AだけConfig Reload
6. Bだけreset
7. Aだけ停止
8. Bが継続動作
9. Aを再起動
10. Bだけ停止

確認:

- hook addressが混ざらない
- patch restoreが他instanceへ行かない
- HUDが混ざらない
- zoom cacheが混ざらない
- active windowだけがmouse deltaを受け取る

## 15.2 Focus／cursor

platformごとに確認する。

- Windows Raw Input
- macOS GCMouse
- Linux X11／XWayland
- Linux Wayland native lock
- Qt fallback

操作:

- Alt+Tab
- modal dialog
- fullscreen切り替え
- window移動
- instance AからBへfocus移動
- mouse device接続／切断
- window close中のinput callback

## 15.3 Config

- sensitivity hotkeyを連打
- GUI sliderとhotkeyを交互に変更
- 変更直後に終了
- A/Bで別感度
- save回数確認
- EmuThreadからdisk I/Oが発生しないことを確認

## 15.4 HUD

- A/Bで別font
- A/Bで別scale
- Aのみeditor
- Bのみzoom
- Aのみradar overlay
- resizeを交互に実行
- screenshot比較

---

# 16. Dolphinから参考にする部分

参考にする設計:

- state ownershipの明確化
- input backendとconsumerの分離
- UI threadとemulation threadの分離
- request stateとactive stateの分離
- lifecycleに応じたcapture解除／再取得
- event-driven config snapshot更新
- backend probeの非同期化
- instance／manager単位のruntime state

参考にしないもの:

- Dolphin固有のGC／Wii input mapping
- Dolphinのglobal singletonをそのまま導入すること
- Dolphinのrenderer全体
- DolphinのConfig実装をmelonDSへそのまま移植すること
- Dolphinのframe timingをmelonDS共通部へ移植すること

主な参照先:

- <https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/Core/System.h>
- <https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/InputCommon/ControllerInterface>
- <https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/DolphinQt/RenderWidget.cpp>
- <https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/InputCommon/ControllerInterface/Quartz>
- <https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/Common/Config>

---

# 17. 最終方針

本計画の中心は、melonDS本体を一般的に改良することではない。

対象は次の一点に集約される。

```text
MelonPrime固有のmutable runtime stateを、
process-globalからinstance-ownedへ移す。
```

次に、MelonPrime固有の入力とGUI連携を次の構造へ整理する。

```text
OS input collector
→ per-instance subscription
→ MelonPrimeCore runtime snapshot
→ game input

EmuThread request
→ MelonPrime GUI mailbox
→ GUI／platform cursor operation
```

ConfigについてもmelonDS基盤は変更せず、MelonPrime側で次だけを行う。

```text
EmuThread:
    runtime値を即時変更
    永続化requestを送る

GUI thread:
    MelonPrimeのlocalCfgを更新
    debounce後に既存Config::Save()
```

最初に実装すべき順番:

1. Zoom cacheのmember化
2. ARM9 dispatch stateのinstance化
3. Patch runtime stateのinstance化
4. HUD stateのinstance化
5. Raw Input service／subscription化
6. GUI／EmuThread mailbox化
7. Config保存経路のMelonPrime側修正
8. macOS／Linux platform lifetime hardening

この順序であれば、melonDS本体由来コードへの変更を避けながら、比較監査で見つかった主要なMelonPrime固有riskを解消できる。
