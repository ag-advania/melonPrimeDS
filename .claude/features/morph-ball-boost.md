# MelonPrime Morph Ball Boost

## 目的
MelonPrimeのブースト機能は、Metroid Prime HuntersのSamus用Alt Form
「Morph Ball Boost」をキーボード/マウス操作で扱いやすくするための入力補助です。

ユーザー向けには2系統あります。

- 右クリック: 従来どおりのR入力系ブースト。Imperialist Zoom、マップ縮小と同じ
  `HK_MetroidZoom` バインドを使う。
- Shift長押し: MelonPrime独自の高速Morph Ball Boost。`HK_MetroidHoldMorphBallBoost`
  から `IB_MORPH_BOOST` に入り、ブーストのチャージ/解放サイクルを自動化する。

Shift長押しは右クリックを置き換えるものではなく、Morph Ball中に連続ブーストしやすく
する追加操作として実装されている。

## ユーザー向け操作
デフォルト設定:

| 操作 | デフォルト | 内容 |
|---|---:|---|
| `HK_MetroidZoom` | 右クリック | Imperialist Zoom、マップ縮小、通常Morph Ball Boost |
| `HK_MetroidHoldMorphBallBoost` | Shift | 押し続けて高速Morph Ball Boost |

表示ラベル:

- `[Metroid] (Mouse Right) Imperialist Zoom, Map Zoom Out, Morph Ball Boost`
- `[Metroid] (Shift) Hold to Fast Morph Ball Boost`
- 日本語UIではそれぞれ「インペリアリストズーム、マップ縮小、モーフボールブースト」と
  「長押しで高速モーフボールブースト」に翻訳される。

## 発動条件
Shift長押し側の高速ブーストは `MelonPrimeCore::HandleMorphBallBoost()` が担当する。
有効になる条件は次の通り。

- in-game中で `HandleInGameLogic()` が走っている。
- `BIT_IS_SAMUS` が立っている。
  - Adventureでは常にSamus扱い。
  - 対戦ではハンターID `0x00` のSamusのみ。
- `HK_MetroidHoldMorphBallBoost` が押され、内部ビット `IB_MORPH_BOOST` が立っている。
- `isAltForm == 0x02`、つまりMorph Ball中である。
- ブーストゲージとブースト中フラグを見て、R入力の押下/解放を制御する。

Samusではない、またはShiftが押されていないフレームは早期returnする。

## 入力パイプライン
主な流れ:

1. `UpdateInputState()` がホットキー状態をフレーム入力へ投影する。
2. `ProjectDownState()` が `HK_MetroidHoldMorphBallBoost` を `IB_MORPH_BOOST`
   に変換する。
3. `HandleInGameLogic()` の hot path で、移動/ボタン処理とズーム入力処理の後に
   `HandleMorphBallBoost()` が呼ばれる。
4. `HandleMorphBallBoost()` が必要に応じてDSのRボタン入力 (`INPUT_R`) を合成する。
5. その後にマウス/スタイラスaim処理が走る。ブースト中もaimは止めない（方向更新のため）。

実行順としては次の位置にある。

```text
ProcessMoveAndButtonsFastFromReset()
ApplyBipedFireInput()
ApplyZoomBindingInput()
HandleMorphBallBoost()
ProcessAimInputStylus() または ProcessAimInputMouse()
```

右クリック側は `ApplyZoomBindingInput()` が担当する。デフォルトでは右クリックにより
`INPUT_R` が押下扱いになり、Morph Ball中はゲーム本来のブースト入力として働く。

## Shift高速ブーストの制御
MPH側のR入力はDS入力マスク上では active-low で、ビットが `0` なら押下、
`1` なら解放を意味する。MelonPrimeでは `InputSetBranchless(bit, released)` を通じて
この状態を更新する。

Shift高速ブースト中の要点:

- `boostGauge` を読む。
- `boostCooldownActive` を読む（`m_ptrs.isBoosting` 経由。実体は `player+0x14A` の
  Boost cooldown / busy timer。実Boosting flag `player+0x4C4 bit26` ではない）。
- `gaugeEnough = boostGauge > 0x0A` で、最低限のブースト可能量を判定する。
- まだcooldown中でなくゲージが足りていない間は、Rを押してチャージする。
- ゲージが足りていてcooldownが空いている時は、Rを解放してブーストを発動させる。
- cooldownが入った（`boostCooldownActive != 0`）ら、次のサイクルに備えてRを押す側へ戻す。

現在の式は以下。

```cpp
const bool boostCooldownActive = (*m_ptrs.isBoosting) != 0x00;
InputSetBranchless(INPUT_R, !boostCooldownActive && gaugeEnough);
```

`released == true` はR解放、`released == false` はR押下なので、この1行で
「チャージ中は押す、発動タイミングで離す、cooldown中はまた押す」というサイクルを作る。

## Aimとタッチ入力の扱い
Morph Ball Boost中は **aim blockを立てない**。`HandleMorphBallBoost()` は
`AIMBLK_MORPHBALL_BOOST` を設定せず、`ProcessAimInputMouse()` とその後の
center-touch reset (`TouchScreen(CENTER_RESET)`) を通常どおり走らせる。

- ブーストの速度は、その時点のMorph Ball方向ベクトルに沿って適用される。
- このベクトルは「マウスaimが書き込む値」と「center touchが押されている状態」が
  両方そろっている間だけゲーム側で更新される。
- そのため、チャージ中もロール中もaim + center touchを止めない。

過去には `AIMBLK_MORPHBALL_BOOST` を立てて干渉を抑えていたが、これだと
`ProcessAimInputMouse()` が早期returnしてcenter touchも出さなくなり、方向ベクトルが
更新されなかった。結果として「モーフ直後はブーストが発動しても進まない（先にマウスを
動かすと動く）」「ロール中にステアリングできない」という不具合になっていた。
現在はこのブロックを撤廃して解消している。

`ReleaseScreen()` 呼び出し（Weapon Check中以外）と `INPUT_R` の合成は従来どおり。
center touchは呼び出し側 `HandleInGameLogic()` の `!m_aimBlockBits` 経路で毎フレーム
押され、`HandleMorphBallBoost()` 内の `ReleaseScreen()` は同フレームの後段で上書きされる。

## Immediate Input Edge Overlayとの関係
`ImmediateInputEdgeOverlay` はゲーム側の入力ポーリング後に、移動/射撃/ジャンプ/ズームなどの
managed bitを上書きできる。Morph Ball Boostも `INPUT_R` を合成するため、
ズーム系のmanaged bitと衝突する可能性がある。

そのため `HandleMorphBallBoost()` は `m_immediateOverlayPreserveMask` に
`INPUT_R` を追加する。Overlay側はこのpreserve maskを見て、Morph Ball Boostが作った
R入力を消さない。

## ROMアドレスとポインタ解決
ブースト状態はROMバージョンごとのRAMアドレスに依存する。

- `boostGauge`: ブーストゲージ (`player+0x148`)
- `isBoosting`: ポインタ名は `isBoosting` だが、指すのは `player+0x14A` の
  Boost cooldown / busy timer。自動サイクルの「次のブーストを出してよいか」判定に使う。

これらのベースアドレスは `MelonPrimeGameRomAddrTable.h` のX-macroにあり、
ROM検出後に `RomAddresses` へ展開される。ゲーム参加時にはローカルプレイヤー位置から
`offP = playerPosition * PLAYER_ADDR_INC` を足して、`m_ptrs.boostGauge` と
`m_ptrs.isBoosting` としてキャッシュする。

## パフォーマンス上の意図
`HandleMorphBallBoost()` はin-game hot pathで毎フレーム呼ばれるため、共通ケースを軽くしている。

- `!BIT_IS_SAMUS || !IsDown(IB_MORPH_BOOST)` を最初にまとめて判定する。
- 非Samus、またはShiftを押していないフレームでは、ほぼ1分岐で戻る。
- `isAltForm`、`boostGauge`、`isBoosting` は必要になったrare pathでだけ読む。
- per-frame config lookupはしない。

この形により、Samus以外のハンターや通常移動中のフレームで余計なRAM readを増やさない。

## 関連ファイル
- `src/frontend/qt_sdl/MelonPrimeInGame.cpp`
  - `HandleInGameLogic()`
  - `HandleMorphBallBoost()`
- `src/frontend/qt_sdl/MelonPrimeGameInput.cpp`
  - `ProjectDownState()`
  - `ApplyZoomBindingInput()`
- `src/frontend/qt_sdl/MelonPrime.h`
  - `IB_MORPH_BOOST`
  - `HotPointers::boostGauge`
  - `HotPointers::isBoosting`（実体は `player+0x14A` cooldown）
- `src/frontend/qt_sdl/MelonPrime.cpp`
  - game-join時のプレイヤー別アドレス解決
  - Samus判定 (`BIT_IS_SAMUS`)
- `src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h`
  - `boostGauge`
  - `isBoosting`
- `src/frontend/qt_sdl/Config.cpp`
  - `Keyboard.HK_MetroidHoldMorphBallBoost`
  - `Joystick.HK_MetroidHoldMorphBallBoost`
- `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h`
  - 設定画面に出るホットキー名
- `src/frontend/qt_sdl/MelonPrimePatchImmediateInputEdgeOverlay.inc`
  - `m_immediateOverlayPreserveMask` によるR入力保護
- `README.md`
  - ユーザー向けの操作表とリリース説明

## 検証観点
手動確認する場合は、少なくとも次を見る。

- AdventureのSamusでMorph Ballになり、Shift長押しで連続ブーストできる。
- **モーフ直後にマウス未操作のままShift長押しでも、現在の照準方向にブーストが出る**
  （aim blockを撤廃したことの確認。先にマウスを動かす必要がない）。
- Shift長押しのロール中にマウスでステアリングできる。
- 右クリックでも従来のMorph Ball Boostが動く。
- 対戦でSamus以外のハンターではShift高速ブーストが発動しない。
- Immediate Input Edge Overlayや新Zoom入力方式が有効でも、Shift側のR入力が消されない。
- `boostGauge <= 0x0A` の間は発動せず、チャージ後に解放される。

## 注意点
- Shift高速ブーストはSamus用の補助であり、他ハンターのAlt Form能力には適用しない。
- ブーストのON/OFF設定キーはなく、ユーザー操作としてはホットキー割り当てで管理する。
- 右クリックの通常ブースト経路とShift高速ブースト経路は別物なので、片方を変更する時に
  もう片方の挙動を壊さないようにする。
- `INPUT_R` はズーム、マップ縮小、Morph Ball Boostで共有されるため、R入力を扱う変更では
  `ApplyZoomBindingInput()` と `ImmediateInputEdgeOverlay` のpreserve処理も確認する。
