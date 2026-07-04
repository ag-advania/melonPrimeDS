# melonPrimeDS `highres_fonts_v3` リファクタリング調査・ロードマップ

> **ステータス（2026-07-04 照合）:** 本ノートの有効な提案は
> [melonprime-full-refactor-plan-v5.md](../melonprime-full-refactor-plan-v5.md) に統合済み。
> **アクティブな計画は V5 のみ**（本ノートは調査記録として保存）。照合結果:
>
> | 本ノートの項目 | 判定 | 理由 / 統合先 |
> |---|---|---|
> | Phase 0（カウンタ群） | ✅ 採用 | V5 Phase 0 に統合（フレームタイム percentile と併設） |
> | Phase 2（per-frame patch / OsdColor） | ✅ 採用（計測ゲート） | V5 §2 W7 + Phase 6。ただし gameplay-runtime.md §7 が「意図的な cheap cold-path」と明記しており、OsdColor は pattern B（ゲームがRAMを上書き）のため edge 化は安全性検証が前提 |
> | Phase 3（入力source enum / fallback限定 / warp整理） | ✅ 採用 | V5 Phase 2 (4) に統合 |
> | Phase 4（RAM read 予算） | ✅ 採用（計測ゲート） | V5 Phase 6 |
> | Phase 5（HUD element-level render cache + pixel-hash 検証） | ✅ 採用（計測ゲート） | V5 §2 W8 + Phase 4b。QPainter 支配が計測で確認された場合のみ |
> | Phase 1（Core 責務分離） | △ ストレッチ採用 | V5 Phase 6。メンバ配置・RunFrameHook 分岐不変が条件 |
> | Phase 6（HUD schema/default型監査/UIテーブル統合） | ❌ 大半が既済 | default 型監査は CI 稼働中（`audit-config-defaults.ps1`）、テーブル重複は V2/V3 で生成ファイル化済み。ジオメトリ残りは V4 Phase 3 完了監査済み |
> | Phase 8（`.inc` を通常 `.cpp` へ） | ❌ 不採用 | unity 断片は意図的規約（inc-ownership CI 検査・upstream diff 衛生）。clip 重複整理と `getScreenWidgetRect` キャッシュのみ V5 Phase 6 へ採用 |
> | Phase 9（static→instance 所有化） | ❌ 不採用 | melonDS のマルチインスタンスは別プロセス（patch-system.md 明記）で動機が弱く、diff リスク過大。V1 で per-process 前提を文書化済み |
> | Phase 7 / 10（分類・監査固定） | △ 部分採用 | site 分類・registry は実装済み。予算系は V5 Phase 7 のラチェットに統合 |

## 調査結果の要約

`highres_fonts_v3` は、単に高解像度フォントを追加しただけのブランチではなく、既に **入力・HUD・プラットフォーム入力・ROMパッチ・CI監査・配布整備** までかなり踏み込んだリファクタリング作業が入っているブランチです。

README上でも melonPrimeDS は melonDS を Metroid Prime Hunters 向けに改造し、マウス＋キーボードFPS操作、Custom HUD、QoL機能を追加したものとして説明されています。

重要なのは、ブランチ内に既に `MelonPrime 全面リファクタリング計画 V4` が存在し、`highres_fonts_v3` の HEAD `2977aa46` 時点で再計測された計画として記録されている点です。このV4計画は、V1～V3で解消済みの「パッチレジストリ」「ROM X-macro」「HUDスキーマ」「リテラル予算」などを前提に、macOS/Linux対応で新たに増えた負債を主対象にしています。

そのため、ここでは **既に整理済みの箇所を尊重しつつ、次に進めるべき完全リファクタリングロードマップ** として整理します。実装には入らず、フェーズごとの対象・目的・作業・効果・リスク・検証方法に分けます。

---

## 1. コードベース全体の俯瞰

### 1.1 MelonPrime 固有処理の中心

MelonPrime固有処理は主に `src/frontend/qt_sdl/` 以下に集中しています。中心は以下です。

| 領域 | 主なファイル | 役割 |
|---|---|---|
| コア制御 | `MelonPrime.h`, `MelonPrime.cpp` | フレーム更新、ROM検出、設定反映、入力処理呼び出し、パッチ適用、状態管理 |
| 入力・エイム | `MelonPrimeGameInput.cpp`, `MelonPrimeRawInput*`, `MelonPrimePlatformInput.h` | Raw Input、mac/Linux入力、マウスエイム、SnapTap、ズーム、武器入力 |
| HUD描画 | `MelonPrimeHudRender.cpp`, `MelonPrimeHudRender*.inc`, `MelonPrimeHudGeometry.h` | Custom HUD、dirty rect、クロスヘア、HP、武器、レーダー、フォント |
| Screen統合 | `Screen.cpp`, `Screen.h`, `MelonPrimeHudScreenCpp*.inc` | Qt/GL描画、カーソル制御、HUDオーバーレイ合成、クリップ制御 |
| ROMパッチ | `MelonPrimePatch*.cpp`, `MelonPrimePatchRegistry.*`, `MelonPrimeArm9Hook.*` | ARM9メモリ書き換え、フック、復元、ROM別アドレス |
| 設定 | `Config.cpp`, `MelonPrimeGameSettings.*`, `MelonPrimeHudPropSchema.inc` | Metroid設定、HUD設定、デフォルト値、実行時キャッシュ |
| UI設定 | `InputConfig/MelonPrimeInputConfig*.cpp/inc` | Metroid入力設定、HUD設定UI、プレビュー |

`MelonPrimeCore` は `RunFrameHook()` を中心に、入力、ROM状態、HUDプリフレーム処理、パッチ、Damage Notify、カーソルモード切り替えなどをまとめて扱っています。

`MelonPrime.h` では、毎フレーム参照される入力状態を 64 byte align の `FrameInputState` にまとめ、ROMアドレスも `GameAddressesHot` と `HotPointers` に分離してキャッシュラインを意識した設計になっています。

---

## 2. 既に入っている重要な最適化

### 2.1 入力ホットパス

入力処理はかなり最適化済みです。

`InputCacheBit` でMetroid操作をビット化し、`FrameInputState` に down / press / mouse delta / wheel / moveIndex をまとめています。

`MelonPrimeGameInput.cpp` では、ホットキー列が連続していることを `static_assert` で保証し、テーブル走査ではなくシフトとマスクで `down` / `press` を作る構造になっています。これは毎フレームの入力変換をかなり軽くする設計です。

`UpdateInputStateImpl<kReentrant>` は通常フレームと再入フレームを `if constexpr` で共通化し、再入時は press map scan と wheel fetch を省くように分岐がコンパイル時に消える設計になっています。

### 2.2 エイム処理

エイムは固定小数点化され、float計算を避ける形に整理されています。

`AIM_FRAC_BITS`, `AIM_DIRECT_BITS`, residual clamp などが `MelonPrime.h` 側にまとまり、残差蓄積もメンバに保持されています。

`ProcessAimInputMouse()` では、aim block / layout change を単一の early-out gate にまとめ、マウス入力が完全にゼロで残差もゼロなら掛け算・clamp・RAM書き込みまで到達しない構造になっています。

### 2.3 HUD

HUDは設定キャッシュ、dirty rect、GL部分アップロード、フォントキャッシュが既に導入されています。

`CachedHudConfig` はHUD設定をまとめて持ち、コメント上も「毎フレーム約50回のhash-map lookupを避ける」ためのキャッシュとして定義されています。

`CustomHud_Render()` は `QRect` を返し、描画された範囲だけを dirty rect として扱います。edit mode は全画面扱いですが、通常HUDは `CurrentHudDirtyRect()` を返す設計です。

リポジトリ内のアーキテクチャ文書でも、Custom HUDのdirty rect最適化は「毎フレームの全画面 memset と全GL texture upload を避ける」目的だと明記されています。GLパスでは `glTexSubImage2D` の範囲を `prevDirty ∪ curDirty` に限定する設計になっています。

さらに `OPT-DR3` として、大きなHUD upload rect が前回と同じ内容ならハッシュで `glTexSubImage2D` 自体をスキップする仕組みも入っています。ただし、この最適化はGLアップロードを止めるもので、CPU側のHUD clear / re-render はまだ残ると明記されています。

### 2.4 ROMパッチ管理

ROMパッチは `MelonPrimePatchRegistry` によって「どのパッチを、どのライフサイクルで適用・復元・リセットするか」を表で管理する形に整理されています。

新しいwrite patchを追加する場合は registry に1エントリ追加すればよい、という設計です。

`PatchApplySite` には `GameJoin`, `ConfigReload`, `OutOfGameFrame`, `BattleRuntime` があり、`Patches_Apply()` は site mask に一致する entry を順に適用します。

ROMアドレスは `MelonPrimeGameRomAddrTable.h` のX-macroに集約され、各ROMグループのアドレスを1行で追加・検証できる形になっています。

---

## 3. ボトルネック候補と負債候補

### P0: すぐ優先して調べるべき箇所

#### 3.1 `RunFrameHook()` の責務過多

`RunFrameHook()` は再入処理、設定reload、入力更新、ROM検出、in-game判定、match lifecycle、HUD pre-frame、Damage Notify、cursor mode、out-of-game patch、focus transition、direct transform、weapon switch pending まで扱っています。

現状はかなりearly-outされていますが、保守性の観点では `MelonPrimeCore` が巨大な神クラス化しやすい状態です。

ここは「高速化のためにそのままにする部分」と「cold pathとして分離する部分」を厳密に分けるべきです。

#### 3.2 `PatchSite_OutOfGameFrame` の毎フレーム適用

out-of-game中、focusedなら `Patches_Apply(PatchSite_OutOfGameFrame, ctx)` が毎フレーム呼ばれます。

対象は `FixWifi`, `UseFirmwareLanguage`, `ExpandStageMatrix` などで、各パッチ側が self-guard する設計です。

これは安全な設計ですが、「毎フレームregistry loop＋各パッチ関数呼び出し」が残ります。小さいとはいえ、メニュー中の安定性や低CPU化を詰めるなら優先的に計測すべきです。

#### 3.3 `OsdColor_ApplyOnce()` の battle runtime 毎フレーム再評価

`RunFrameHook()` 内で、battle runtime mode中は `OsdColor_ApplyOnce()` が毎フレーム呼ばれます。

`ApplyOnce` という名前から内部ガードはありそうですが、実際にどれだけRAM read / branch / write guard が走るかを計測し、config epoch や game-state edge で置換できるか確認する価値があります。

#### 3.4 HUDはGL uploadだけでなくCPU redrawが残っている

dirty rectとGL upload skipは強いですが、アーキテクチャ文書上も CPU clear＋overlay re-render は残ると書かれています。

高解像度フォント、ズームスコープ、大きいレーダー、outline、weapon inventoryを組み合わせると、GPU uploadより前の `QPainter` コストが支配的になる可能性があります。

次の大きな改善余地は「dirty rect」ではなく **element-level render cache / static layer分離** です。

#### 3.5 Linux/macOS入力の fallback / warp / Qt 呼び出し

`MelonPrimePlatformInput.h` でmac/Linuxのraw filterとcursor warpは一本化されています。

macは `MacWarpCursorGlobal`、Linux xcb は `LinuxWarpCursorGlobal`、それ以外は `QCursor::setPos` にfallbackします。

Linux側はXInput2 rawが有効ならraw delta、そうでなければ `ScreenPanel` の前回位置差分、さらに最後のfallbackとして `QCursor::pos()` 差分が残っています。

ここは低遅延の観点でかなり重要です。

`QCursor::pos()` や warp は環境依存の重さ・副作用があるので、「どの入力sourceが毎フレーム使われているか」を計測し、不要fallbackを削る必要があります。

---

## 4. 完全リファクタリングロードマップ

以下は、現在の `highres_fonts_v3` をベースにした **次段階のロードマップ** です。

既にV4で完了したPlatformInput facadeやHUD Geometry共有化は前提にします。

---

# Phase 0: ベースライン計測と安全網の固定

## 対象ファイル

- `.claude/skills/*`
- `.github/workflows/build-*.yml`
- `MelonPrime.cpp`
- `MelonPrimeGameInput.cpp`
- `MelonPrimeHudRender*.inc`
- `Screen.cpp`
- `MelonPrimePatchRegistry.cpp`

## 目的

実装前に、ホットパスの負荷を数値化します。

現在のコードは既に多くの最適化が入っているため、感覚で削ると逆に壊しやすいです。

## 作業内容

1. developer feature限定で軽量カウンタを追加する。
   - `RunFrameHook()` 実行回数
   - `UpdateInputState()` / `ProcessAimInputMouse()` 回数
   - raw / panel / QCursor の入力source比率
   - warp回数
   - `Patches_Apply(PatchSite_OutOfGameFrame)` 回数
   - `OsdColor_ApplyOnce()` 回数
   - HUD dirty rect面積
   - GL upload byte数
   - OPT-DR3 hash skip回数
   - `CustomHud_Render()` 実行時間

2. 計測は `MELONPRIME_ENABLE_DEVELOPER_FEATURES` または専用 `MELONPRIME_ENABLE_PERF_PROBES` のときだけ有効にする。

3. Windows / macOS / Linux でビルドが通ることをCIに固定する。

4. 既存の散乱予算、HUD schema監査、config default監査と合わせて「これ以上悪化させない予算」を作る。

## 期待される効果

- 以後のフェーズで「速くなった・遅くなった」を判断できる。
- ホットパスに新しいQt呼び出しやRAM readを増やした場合に検出できる。
- 実機ROMスモーク前でも悪化を見つけやすくなる。

## リスク

- 計測コード自体がホットパスを汚す。
- developer buildとrelease buildで挙動がズレる。

## 検証方法

- release buildでは計測コードが完全に無効化されること。
- 計測ON/OFFで通常プレイの入力挙動が変わらないこと。
- 1分間のin-game / out-of-game / HUD ON / HUD OFF ログを保存する。

---

# Phase 1: `MelonPrimeCore` の責務分離

## 対象ファイル

- `MelonPrime.h`
- `MelonPrime.cpp`
- 新規候補:
  - `MelonPrimeLifecycle.cpp`
  - `MelonPrimeRuntimeConfig.cpp`
  - `MelonPrimeFrameRuntime.cpp`
  - `MelonPrimePatchRuntime.cpp`

## 目的

`MelonPrimeCore` の神クラス化を抑えます。

ただし、ホットパスのメンバ配置やinline関数は不用意に動かしません。

## 作業内容

1. `MelonPrime.cpp` から cold path を分離する。
   - `OnEmuStart`
   - `OnEmuStop`
   - `ResetRuntimeStateForBoot`
   - `OnEmuUnpause`
   - `ApplyConfigReload`
   - `ReloadConfigFlags`
   - `HandleGameJoinInit`
   - `HandleBattleRuntimeEnter`

2. `RunFrameHook()` 自体は最初は大きく変えず、呼び出し先だけ整理する。

3. `MelonPrime.h` のメンバ配置は維持する。
   - `FrameInputState`
   - `HotPointers`
   - aim residual
   - config cache
   - platform raw filter

4. cold functionには `COLD_FUNCTION` を徹底する。

## 期待される効果

- 保守性が上がる。
- `RunFrameHook()` の見通しが良くなる。
- hot codeとcold codeの分離でi-cache汚染を抑えやすくなる。

## リスク

- include依存が崩れる。
- unity include前提のhook fragmentを誤って分離すると壊れる。
- メンバ配置変更でキャッシュ最適化の意図が崩れる。

## 検証方法

- バイナリ挙動差なし。
- Windows/mac/Linux build green。
- `RunFrameHook()` の呼び出し順が変わっていないことをdiffで確認。
- 起動、ROMロード、試合参加、試合終了、リセット、pause/unpauseのスモーク。

---

# Phase 2: per-frame patch処理の削減

## 対象ファイル

- `MelonPrime.cpp`
- `MelonPrimePatchRegistry.h`
- `MelonPrimePatchRegistry.cpp`
- `MelonPrimePatchFixWifi.cpp`
- `MelonPrimePatchUseFirmwareLanguage.cpp`
- `MelonPrimePatchExpandStageMatrix.cpp`
- `MelonPrimePatchOsdColor.cpp`

## 目的

毎フレームの registry loop と self-guard patch呼び出しを削減します。

## 作業内容

1. `PatchSite_OutOfGameFrame` 専用の軽量リストを作る。
   - full registryを毎回走査しない。
   - `constexpr` な site別 view を作る。

2. `OutOfGameFrame` patchを分類する。
   - 毎フレーム必要なもの
   - config reload時だけでよいもの
   - ROMロード直後だけでよいもの
   - メニュー遷移時だけでよいもの

3. `ExpandStageMatrix_ApplyIfLoaded` のように「遅延ロード検出」が必要なものは、完全削除せず呼び出し頻度を下げる。
   - 例: 1フレーム毎ではなく、状態遷移時＋低頻度poll。
   - ただし、ロード検出が遅れて機能が壊れるなら現状維持。

4. `OsdColor_ApplyOnce()` の毎フレーム再評価を調査する。
   - 内部で本当にwriteが発生しているか。
   - config epoch / battle state edge / current mode edge で代替できるか。
   - 代替できない場合は、理由をコメント化して現状維持。

## 期待される効果

- out-of-game時の無駄な関数呼び出し削減。
- メニュー・ロビー中のCPU負荷低下。
- patch追加時の「とりあえず毎フレームself-guard」設計を防げる。

## リスク

- 遅延ロード系patchが適用されなくなる。
- config変更後の即時反映が遅れる。
- ROMごとの差異で特定バージョンだけpatch timingが壊れる。

## 検証方法

- 全ROMグループでROM検出。
- Wi-Fi/Friend/Rival fix。
- firmware language/name。
- stage matrix拡張。
- out-of-game画面遷移。
- config toggle即時反映。
- Phase 0のカウンタで `OutOfGameFrame` 呼び出し数が減っていることを確認。

---

# Phase 3: 入力source選択とcursor/warp経路の整理

## 対象ファイル

- `MelonPrimeGameInput.cpp`
- `MelonPrimePlatformInput.h`
- `MelonPrimeRawInputWinFilter.*`
- `MelonPrimeRawInputMacFilter.*`
- `MelonPrimeRawInputLinuxFilter.*`
- `Screen.cpp`
- `Screen.h`

## 目的

Raw Input / macOS / Linux / Qt fallback / QCursor fallback の責務を明確にし、毎フレームの不要Qt呼び出しとwarp副作用を減らします。

## 作業内容

1. 入力sourceを enum 化する。
   - `WinRaw`
   - `MacRaw`
   - `LinuxRaw`
   - `PanelDelta`
   - `QCursorFallback`
   - `None`

2. `UpdateInputStateImpl()` 内で、そのフレームに使うsourceを一度だけ決める。

3. Linuxではraw active時にpanel fallbackを完全に休ませる。
   - 既に `resetAimMouseDelta()` はあるため、source ownershipをコメントではなく構造で保証する。

4. `QCursor::pos()` fallbackの使用条件を狭める。
   - panel deltaが存在する環境では使わない。
   - macのHID fallbackで必要な場合だけ残す。
   - Waylandなど不安定な環境は「暴走するくらいならaim停止」のガードを優先する。

5. `WarpCursorTo()` のstatic wrapperを削る。
   - 直接 `PlatformInput_WarpCursor()` に統一する。

6. resize / layout / focus / show cursor の reset経路を一覧化し、重複を削る。
   - `HandleAimEarlyReset`
   - `ShowCursor`
   - `ScreenPanel::clipCursorCenter1px`
   - `ScreenPanel::mouseMoveEvent`
   - focus lost block

## 期待される効果

- 低遅延入力の安定化。
- Linux/macOSでのwarp由来のevent storm抑制。
- `QCursor` 系の環境依存負荷を削減。
- focus復帰・layout変更・Alt-Tab後のaim jumpを防ぎやすくなる。

## リスク

- macOSのGCMouse / IOHID / fallback優先順を壊す。
- Linux XInput2 / XWayland / VirtualBox tabletの挙動を壊す。
- cursor mode と aim mode の切り替えでタッチ操作が壊れる。

## 検証方法

- Windows Raw Input: aim, hotkey, Joy2Key, focus loss。
- macOS: GCMouse backend、IOHID fallback（内蔵トラックパッド）、`MacSetAimCursorCaptured` は GCMouse のみ、Alt-Tab、layout変更、trackpad クリック押しっぱなしなし。
- Linux X11: XInput2 raw、VirtualBox、panel fallback。
- Wayland: raw不可時に暴走しないこと。
- 1分間のsource counterで不要な `QCursorFallback` が出ないこと。

---

# Phase 4: エイム・ズーム・Damage Notify のRAM read予算化

## 対象ファイル

- `MelonPrimeGameInput.cpp`
- `MelonPrimeZoomStatus.*`
- `MelonPrimeDamageNotifyPurple.cpp`
- `MelonPrime.h`
- `MelonPrime.cpp`

## 目的

毎フレームのメモリアクセスを減らし、状態遷移で済むものを状態キャッシュに寄せます。

## 作業内容

1. `ProcessAimInputMouse()` 内のRAM write/readを一覧化する。
   - aimX / aimY
   - zoom state
   - native aim hook delta
   - weapon / alt-form state

2. `UpdateZoomAimEffectiveScale()` の呼び出し条件をさらに絞る。
   - 現状は `m_enableZoomAimScale` かつ delta/residual があると scope state を読む可能性があります。
   - weaponがzoom不可の間はscope readを避ける。
   - zoom可能性cacheを `static` ではなく `MelonPrimeCore` 所有に移す候補を検討する。

3. `DamageNotifyPurpleTick()` を計測する。
   - 有効時だけ呼ばれる構造にはなっています。
   - HP read、Weavel proxy read、double damage timer writeの頻度を測る。
   - Weavel以外ではproxy系readを完全に避ける。

4. `HotPointers` に未集約の毎フレームRAMアクセスがないか監査する。
   - `ARM9Read*` が毎フレーム直接呼ばれていないか。
   - address計算が毎フレーム残っていないか。

## 期待される効果

- frame timeの揺れを減らす。
- aim pathのメモリ依存を減らす。
- zoom設定ON時だけ重くなる問題を防ぐ。

## リスク

- zoom中クロスヘア・ズーム感度の追従が遅れる。
- Damage Notify Purple のfalse positive / false negative。
- Weavel proxy HP処理の破損。

## 検証方法

- 全ハンター。
- Samus / Sylux alt-form。
- Weavel proxy。
- Imperialist zoom。
- zoom可能化チート環境。
- HP減少、死亡、変身、proxy attach/detach。
- Phase 0のRAM read counterで削減確認。

---

# Phase 5: HUDのelement-level incremental render化

## 対象ファイル

- `MelonPrimeHudRenderMain.inc`
- `MelonPrimeHudRenderDraw.inc`
- `MelonPrimeHudRenderRuntime.inc`
- `MelonPrimeHudRenderConfig.inc`
- `MelonPrimeHudRenderAssets.inc`
- `MelonPrimeHudScreenCppOverlayOfGl.inc`
- `Screen.cpp`
- `Screen.h`

## 目的

dirty rectの次の段階として、HUDを「毎フレーム全部描く」構造から「変わった要素だけ描く」構造へ進めます。

## 作業内容

1. HUD要素ごとに render signature を作る。
   - HP
   - ammo
   - weapon icon
   - weapon inventory
   - match status
   - rank/time
   - bomb count
   - radar frame
   - radar crop
   - crosshair
   - zoom scope

2. static layer と dynamic layer に分ける。
   - static: HP枠、ラベル、固定アイコン、outline、frame
   - dynamic: 数値、ammo、現在武器、crosshair、radar crop、timer

3. crosshairは別扱いにする。
   - aim positionで毎フレーム変わる。
   - zoom transition中だけ大きくdirtyになる。
   - idle中は前回stampを再利用できる。

4. high-res fontのglyph cacheを要素単位で再利用する。
   - フォント・文字列・色・outline・scaleが同じなら再描画しない。

5. GL upload skipだけでなく、CPU QPainter renderもskipできるようにする。

6. edit modeは例外として現状維持する。
   - edit modeは毎フレーム全要素boundsが必要なため、最適化対象から外す。

## 期待される効果

- 高解像度フォント使用時のCPU負荷低下。
- 大きいzoom scopeやradar overlayでのframe time安定化。
- HUD ON時とOFF時の差を縮める。

## リスク

- signature漏れによるHUD stale。
- resize / config変更 / TOML import / HUD editor後の更新漏れ。
- alpha / outline / font styleの差分検出漏れ。

## 検証方法

- 現行rendererと新rendererのpixel hash比較。
- HUD設定全カテゴリのON/OFF。
- フォントモード: MPH / system / file。
- HUD import/export。
- resize / DPI変更。
- zoom scope hold中のCPU time比較。
- GL upload byte数とQPainter実行時間の両方を比較。

---

# Phase 6: HUD設定スキーマとUI重複の統合

## 対象ファイル

- `MelonPrimeHudPropSchema.inc`
- `InputConfig/MelonPrimeInputConfig*.cpp`
- `InputConfig/MelonPrimeInputConfig*.inc`
- `MelonPrimeHudRenderConfig.inc`
- `MelonPrimeHudGeometry.h`
- `Config.cpp`

## 目的

HUD設定の重複実装、型ミス、preview/runtime差分を減らします。

## 作業内容

1. HUD設定を単一schemaに寄せる。
   - key
   - 型
   - default
   - UI control
   - min/max
   - runtime owner
   - preview owner

2. `Config.cpp` の default type不一致を機械検出する。
   - `GetBool()` するkeyは bool list。
   - `GetDouble()` するkeyは double list。
   - `GetInt()` するkeyは int list。

リポジトリ文書でも、`Config.cpp` には `DefaultInts`, `DefaultBools`, `DefaultDoubles` があり、違う型のdefault listに置くと `GetXxx()` から見えず、fallback値になる危険があると明記されています。

3. previewとruntimeのgeometry式を `MelonPrimeHudGeometry.h` に寄せる。
   - V4で一部進んでいるため、残りを潰す。

4. `InputConfig` 側のテーブル重複を削る。
   - HUD property table
   - preview table
   - build UI table
   - schema table

5. TOML import/exportのキー名は絶対に変えない。

## 期待される効果

- HUD新項目追加時の触る場所が減る。
- previewと実描画のズレが減る。
- default値の型ミスによるサイレントバグを防げる。

## リスク

- 既存ユーザー設定の互換性破壊。
- UI表示順の変化。
- previewだけ変わる、runtimeだけ変わる、という片側差分。

## 検証方法

- HUD schema再生成diffなし。
- config default audit。
- TOML round trip。
- previewとruntimeのスクリーンショット比較。
- 既存設定ファイル読み込み。
- 全HUD要素の初期値確認。

---

# Phase 7: ROMパッチ・ARM9 hook の責務再分類

## 対象ファイル

- `MelonPrimePatchRegistry.*`
- `MelonPrimePatch*.cpp`
- `MelonPrimeArm9Hook.cpp`
- `MelonPrimePatchNative*.inc`
- `MelonPrimeGameInput.cpp`
- `MelonPrimeGameRomAddrTable.h`

## 目的

write patch、runtime hook、input hook、HUD patch、out-of-game patchを明確に分け、追加・復元・検証を安全にします。

## 作業内容

1. patchを4分類にする。
   - static write patch
   - lifecycle write patch
   - per-frame monitored patch
   - ARM9 instruction hook

2. `PatchRegistry` に site別 view を持たせる。
   - `GameJoin`
   - `BattleRuntime`
   - `ConfigReload`
   - `OutOfGameFrame`

3. hook fragmentの所有者を明確化する。
   - 現状 `MelonPrimeGameInput.cpp` は複数hook fragmentをunity includeしています。
   - コメント上も、local aim helper scopeに依存するため移動禁止になっています。
   - すぐに分離せず、まず依存関係表を作る。
   - その後、必要なら `MelonPrimeInputHooks.cpp` 的な専用ownerへ移す。

4. ROM address tableをowner別に見える化する。
   - input
   - HUD
   - patch
   - gameplay
   - online
   - debug/developer

5. ROM groupごとのサポート状況を自動検査する。
   - address range
   - hook site expected instruction
   - restore value
   - unsupported ROM時のearly-out

## 期待される効果

- ROM別修正が安全になる。
- patch追加時にrestore漏れしにくくなる。
- `MelonPrimeGameInput.cpp` の肥大化を抑えられる。

## リスク

- ARM9 hook の順序変更。
- native aim / native zoom / weapon switch のregression。
- ROM別命令一致チェックの漏れ。

## 検証方法

- JP1.0 / JP1.1 / US1.0 / US1.1 / EU1.0 / EU1.1 / KR1.0。
- NativeAimDeltaHook。
- LowLatencyAimHook。
- NativeBipedFire。
- NativeZoomToggle。
- WeaponSwitchHook。
- patch restore on leave / stop / reset。

---

# Phase 8: Screen統合層の分離

## 対象ファイル

- `Screen.cpp`
- `Screen.h`
- `MelonPrimeHudScreenCpp*.inc`
- `Window.cpp`
- `EmuInstance.cpp`
- 新規候補:
  - `MelonPrimeScreenIntegration.h`
  - `MelonPrimeScreenIntegration.cpp`
  - `MelonPrimeCursorController.cpp`
  - `MelonPrimeHudOverlayHost.cpp`

## 目的

`Screen.cpp` に混ざっているQtイベント、cursor clipping、HUD overlay、Metroid固有入力を分離します。

## 作業内容

1. `ScreenPanel` のMetroid固有処理を façade に逃がす。
   - mouse press/release/move
   - wheel
   - focus
   - clip cursor
   - top-screen-only override
   - bottom-screen confinement

2. `getScreenWidgetRect()` の結果を layout更新時にキャッシュする。
   - 現状は `screenKind` をscanしてQTransformで矩形計算します。
   - layout変更時だけ計算し、clipやHUDで使い回す。

3. `resizeEvent()` と `setupScreenLayout()` のclip更新重複を整理する。
   - `resizeEvent()` は `setupScreenLayout()` を呼び、その後Windowsでは `updateClipIfNeeded()` も呼びます。
   - `setupScreenLayout()` 側にもWindowsの `updateClipIfNeeded()` があります。
   - 二重呼び出しが不要なら1箇所に寄せる。

4. `MelonPrimeHudScreenCpp*.inc` の数を減らす。
   - call-siteごとのinclude fragmentは便利だが、長期保守では追いにくい。
   - GL overlay / software overlay / mouse / layout / helpersを通常 `.cpp` に近づける。

## 期待される効果

- Qt/Screen本体とMelonPrime拡張の境界が明確になる。
- upstream melonDSとの差分管理が楽になる。
- clip/update/layout周りの重複呼び出しを削れる。

## リスク

- Screen layout、DPI、回転、TopOnly、BottomOnlyのregression。
- cursor confinementの破損。
- HUD overlay texture管理の破損。

## 検証方法

- Native renderer。
- OpenGL renderer。
- Screen rotation。
- Top Only / Natural / Hybrid。
- In-game Top Screen Only。
- Bottom screen cursor confinement。
- HUD ON/OFF。
- radar overlay。
- fullscreen/windowed切り替え。

---

# Phase 9: グローバルstatic状態の分類とinstance所有化

## 対象ファイル

- `MelonPrimeHudRender*.inc`
- `MelonPrimePatch*.cpp`
- `MelonPrimeRawInput*`
- `MelonPrimeGameInput.cpp`
- `MelonPrimeZoomStatus.*`

## 目的

将来のmulti-instanceやテスト容易性を考え、static mutable stateを整理します。

## 作業内容

1. staticを3分類する。
   - immutable asset cache
   - process-wide singleton
   - per-instance runtime state

2. per-instance stateは `MelonPrimeCore` または `ScreenPanel` 所有へ移す。
   - HUD runtime cache
   - crosshair previous dirty
   - zoom capability cache
   - patch applied state

3. asset cacheは共有のまま残す。
   - font family cache
   - loaded image/icon cache
   - immutable lookup table

4. raw input singletonはplatform別に維持する。
   - V4でも内部共通化はリスクが大きいとして見送り判断されています。
   - ここでも無理に統合しない。

## 期待される効果

- 将来的な複数インスタンス対応に強くなる。
- テストしやすくなる。
- hidden global state由来のバグを減らせる。

## リスク

- 差分が大きくなる。
- HUD/pipelineの状態引き回しが増える。
- 現在single-instance前提なら効果が見えにくい。

## 検証方法

- single instance挙動完全一致。
- 可能なら2インスタンス起動。
- HUD dirty rect / patch restore / raw input singletonの破損確認。
- static allowlist監査。

---

# Phase 10: 最終収束・監査・ドキュメント更新

## 対象ファイル

- `.claude/rules/repo-architecture.md`
- `.claude/rules/melonprime-refactoring.md`
- `.claude/rules/melonprime-aim-input.md`
- `.claude/skills/*.ps1`
- `.github/workflows/*.yml`
- `CLAUDE.md`

## 目的

リファクタリング後に負債が戻らないように、CIとドキュメントで固定します。

## 作業内容

1. 監査項目を追加する。
   - platform scatter budget
   - config literal budget
   - HUD schema parity
   - default value type audit
   - `QCursor::setPos` のmac禁止
   - per-frame patch call budget
   - `RunFrameHook()` LOC / cyclomatic budget
   - HUD full upload禁止
   - Screen.cpp内のMelonPrime include fragment数
   - static mutable state allowlist

2. `repo-architecture.md` を更新する。
   - PlatformInput
   - ScreenIntegration
   - HUD render cache
   - ROM patch lifecycle
   - hot path ownership

3. `melonprime-aim-input.md` を更新する。
   - source選択
   - raw/panel/fallback
   - focus/layout reset
   - warp policy

4. release checklistに性能確認を追加する。
   - HUD ON/OFF CPU差
   - input source log
   - GL upload byte
   - frame time percentile

## 期待される効果

- 同じ負債が戻りにくくなる。
- PR reviewで性能退化を検出できる。
- 将来の機能追加が安全になる。

## リスク

- 監査が厳しすぎて開発が止まる。
- allowlist運用が雑になる。

## 検証方法

- Windows / macOS / Linux CI green。
- audit green。
- リリース前スモークチェック完了。
- 主要機能のスクリーンショット・ログ保存。

---

## 5. 優先順位

最初に着手すべき順番は以下です。

1. **Phase 0: 計測と安全網**
2. **Phase 2: per-frame patch処理削減**
3. **Phase 3: 入力source / warp整理**
4. **Phase 5: HUD element-level incremental render**
5. **Phase 1: Core責務分離**
6. **Phase 6: HUD schema/UI統合**
7. **Phase 8: Screen統合層分離**
8. **Phase 7: ROM patch/hook再分類**
9. **Phase 9: static状態整理**
10. **Phase 10: 監査・ドキュメント固定**

理由は、Phase 2 / 3 / 5 が **低遅延・低CPU負荷・安定frame time** に直結し、Phase 1 / 6 / 8 / 7 / 9 が **保守性・可読性・拡張性** に効くためです。

---

## 6. 結論

`highres_fonts_v3` は、既に「雑な改修が蓄積しただけのブランチ」ではなく、かなり性能意識の高い整理が進んでいます。

特に以下は良い方向です。

- 入力ビット投影
- 固定小数点エイム
- `HotPointers`
- `PlatformInput` facade
- HUD config cache
- dirty rect
- patch registry
- ROM X-macro

次の本丸はこの3つです。

1. **毎フレームpatch / state再評価の削減**
2. **入力source / cursor warp / Qt fallbackの完全整理**
3. **HUDのCPU redraw削減**

この順番で進めると、既存機能を壊すリスクを抑えながら、低遅延・低CPU負荷・安定したフレーム時間・保守しやすい構造に近づけられます。

---

## 参考にした主なファイル

| ファイル | 確認内容 |
|---|---|
| `README.md` | melonPrimeDSの目的、Custom HUD、入力機能、対応ROM、低遅延方針 |
| `.claude/rules/completed/melonprime-full-refactor-plan-v4.md` | 既存V4リファクタリング計画、完了済みフェーズ、残課題 |
| `src/frontend/qt_sdl/MelonPrime.h` | `MelonPrimeCore`, `FrameInputState`, `HotPointers`, aim cache, state flags |
| `src/frontend/qt_sdl/MelonPrime.cpp` | `RunFrameHook`, lifecycle, config reload, game join, patch apply |
| `src/frontend/qt_sdl/MelonPrimeGameInput.cpp` | 入力投影、Raw Input、エイム、ズーム、hook fragments |
| `src/frontend/qt_sdl/MelonPrimePlatformInput.h` | mac/Linux raw filter facade, cursor warp |
| `src/frontend/qt_sdl/Screen.cpp` / `Screen.h` | Qtイベント、cursor clip、HUD overlay、GL upload |
| `src/frontend/qt_sdl/MelonPrimeHudRender*.inc` | HUD config cache、dirty rect、render pipeline、font cache |
| `src/frontend/qt_sdl/MelonPrimePatchRegistry.*` | patch lifecycle registry |
| `src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h` | ROM address X-macro |
| `.claude/rules/repo-architecture.md` | HUD dirty rect、GL upload skip、PlatformInput、config default分類 |
