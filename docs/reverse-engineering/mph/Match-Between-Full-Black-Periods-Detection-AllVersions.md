# 試合前完全暗黒終了後～試合終了後完全暗黒到達までの期間検知 - AllVersions

## 目的

次の期間を、毎フレーム連続した `true` として検知する。

```text
試合開始前の完全暗黒
  ↓
完全暗黒が終了した最初のフレーム   ← true開始
  ↓
試合開始演出・通常試合
  ↓
試合終了スコアボード・マップカメラ
  ↓
試合終了後フェードアウト
  ↓
完全暗黒へ到達した最初のフレーム ← falseへ切替
  ↓
結果画面側の黒明け                ← falseのまま
```

「画面が完全に明るくなったか」は見ない。
開始条件は、黒さが `-16` から `-15` 以上へ抜けた最初のフレームでよい。

---

## 結論

検知できる。

ただし、単純なstateless条件だけでは、試合後の完全黒を抜けて結果画面へフェードインするときに再び `true` になる可能性がある。
そのため、実装本命は小さな状態機械を使う。

```text
WAIT_START_BLACK
  試合前の完全黒を待つ

WAIT_START_BLACK_EXIT
  完全黒が解除されるまで待つ

ACTIVE
  対象期間。試合後の完全黒へ到達するまでtrue
```

---

## 完全黒の定義

```c
transitionType == 1 &&
(transitionPhase == 2 || transitionPhase == 3) &&
transitionBrightness <= -16
```

- `transitionType == 1`: darken方向
- `transitionPhase == 2`: 完全黒到達・callback境界
- `transitionPhase == 3`: 完全黒から戻るfade-in側
- `transitionBrightness <= -16`: MASTER_BRIGHT darken factor 16、完全黒

開始時は、phase3でbrightnessが `-16` を抜けた最初のフレームに `ACTIVE` へ入る。
終了時は、試合後fadeでphase2/3かつbrightnessが `-16` に到達した最初のフレームに `ACTIVE` を解除する。

---

## 試合前完全黒の文脈

完全黒だけでは他の画面遷移と区別できないため、次のいずれかがBattle直前／Battleを示すことを確認する。

```c
transitionTargetMode == 0x0D ||
transitionTargetMode == 0x0E ||
currentMode == 0x0D ||
pendingMode == 0x0D ||
pendingMode == 0x0E
```

ただし、試合終了後は除外する。

```c
!(currentMode == 0x0E && flowState == 2)
```

`0x0D` はBattle直前の中間mode、`0x0E` はBattle runtime。

---

## 試合終了後完全黒の文脈

試合終了後fadeは次でarmする。

```c
currentMode == 0x0E &&
flowState == 2 &&
transitionType == 1 &&
transitionPhase == 1
```

この状態を一度観測したあと、完全黒条件へ到達したフレームで対象期間を終了する。

同じフレームで直接次を満たす場合も終了扱いにする。

```c
currentMode == 0x0E &&
flowState == 2 &&
isFullBlack
```

この二段構成により、完全黒到達時のcallbackやmode commitで `currentMode` / `flowState` が直後に変わっても取り逃がしにくい。

---

## 推奨状態機械

```c
typedef enum MatchBlackWindowPhase {
    MATCH_WAIT_START_BLACK = 0,
    MATCH_WAIT_START_BLACK_EXIT,
    MATCH_ACTIVE
} MatchBlackWindowPhase;

typedef struct MatchBlackWindowState {
    MatchBlackWindowPhase phase;
    bool postMatchFadeArmed;
} MatchBlackWindowState;
```

更新手順。

```c
bool UpdateMatchBetweenBlackouts(
    MatchBlackWindowState* state,
    const MatchTransitionSnapshot* s)
{
    const bool fullBlack =
        s->transitionType == 1 &&
        (s->transitionPhase == 2 || s->transitionPhase == 3) &&
        s->transitionBrightness <= -16;

    const bool postMatchContext =
        s->currentMode == 0x0E &&
        s->flowState == 2;

    const bool startMatchContext =
        !postMatchContext &&
        (
            s->transitionTargetMode == 0x0D ||
            s->transitionTargetMode == 0x0E ||
            s->currentMode == 0x0D ||
            s->pendingMode == 0x0D ||
            s->pendingMode == 0x0E
        );

    switch (state->phase) {
    case MATCH_WAIT_START_BLACK:
        if (startMatchContext && fullBlack) {
            state->phase = MATCH_WAIT_START_BLACK_EXIT;
        }
        return false;

    case MATCH_WAIT_START_BLACK_EXIT:
        if (!fullBlack) {
            state->phase = MATCH_ACTIVE;
            state->postMatchFadeArmed = false;
            return true;
        }
        return false;

    case MATCH_ACTIVE:
        if (postMatchContext &&
            s->transitionType == 1 &&
            s->transitionPhase == 1) {
            state->postMatchFadeArmed = true;
        }

        if (fullBlack &&
            (state->postMatchFadeArmed || postMatchContext)) {
            state->phase = MATCH_WAIT_START_BLACK;
            state->postMatchFadeArmed = false;
            return false;
        }

        return true;
    }

    state->phase = MATCH_WAIT_START_BLACK;
    state->postMatchFadeArmed = false;
    return false;
}
```

---

## フレーム単位の挙動

| 状態 | 完全黒 | 戻り値 |
|---|---:|---:|
| 試合前完全黒 | Yes | `false` |
| 黒明け最初のフレーム（brightness `-15`など） | No | `true`開始 |
| 試合開始演出 | No | `true` |
| 通常試合 | No | `true` |
| 試合終了スコアボード／マップカメラ | No | `true` |
| 試合終了後fade、まだ完全黒ではない | No | `true` |
| 試合終了後、完全黒へ到達 | Yes | `false`開始 |
| 結果画面へ黒明け | No | `false`のまま |

これが今回の要求に最も直接一致する。

---

## JP1_0 アドレス

```text
currentMode          020E6B30  u8
pendingMode          020E6B28  u8
flowState            020E6B48  u8
transitionBrightness 020E6B38  s32
transitionTargetMode 020E6B3C  u8
transitionPhase      020E6B3D  u8
transitionType       020E6B3E  u8
transitionDuration   020E6B40  u16
```

JP1_0で必要な読み取り例。

```c
MatchTransitionSnapshot s = {
    .currentMode = *(volatile const uint8_t*)0x020E6B30,
    .pendingMode = *(volatile const uint8_t*)0x020E6B28,
    .flowState = *(volatile const uint8_t*)0x020E6B48,
    .transitionBrightness = *(volatile const int32_t*)0x020E6B38,
    .transitionTargetMode = *(volatile const uint8_t*)0x020E6B3C,
    .transitionPhase = *(volatile const uint8_t*)0x020E6B3D,
    .transitionType = *(volatile const uint8_t*)0x020E6B3E,
};

bool active = UpdateMatchBetweenBlackouts(&state, &s);
```

---

## 全バージョンアドレス

| Version | currentMode | pendingMode | flowState | brightness | targetMode | phase | type | duration |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| JP1_0 | `020E6B30` | `020E6B28` | `020E6B48` | `020E6B38` | `020E6B3C` | `020E6B3D` | `020E6B3E` | `020E6B40` |
| JP1_1 | `020E6AF0` | `020E6AE8` | `020E6B08` | `020E6AF8` | `020E6AFC` | `020E6AFD` | `020E6AFE` | `020E6B00` |
| US1_0 | `020E4A04` | `020E49FC` | `020E4A1C` | `020E4A0C` | `020E4A10` | `020E4A11` | `020E4A12` | `020E4A14` |
| US1_1 | `020E54CC` | `020E54C4` | `020E54E4` | `020E54D4` | `020E54D8` | `020E54D9` | `020E54DA` | `020E54DC` |
| EU1_0 | `020E54EC` | `020E54E4` | `020E5504` | `020E54F4` | `020E54F8` | `020E54F9` | `020E54FA` | `020E54FC` |
| EU1_1 | `020E556C` | `020E5564` | `020E5584` | `020E5574` | `020E5578` | `020E5579` | `020E557A` | `020E557C` |
| KR1_0 | `020DE31A` | `020DE318` | `020DE330` | `020DE320` | `020DE324` | `020DE325` | `020DE326` | `020DE328` |

### KR1_0訂正

過去のstart-match fade成果物では `pendingMode = 020DE312` としていたが、メインmode loopが `currentMode` と比較する実際のpending modeは `020DE318`。
今回の判定では `020DE318` を採用する。

---

## stateless簡易版

ライフサイクル中に再判定されても問題ない環境では、近似的には次でも動く。

```c
bool activeApprox =
    currentMode == 0x0E &&
    !(
        transitionType == 1 &&
        (transitionPhase == 2 || transitionPhase == 3) &&
        transitionBrightness <= -16
    );
```

これは、

- 試合前完全黒中はfalse
- 黒が明け始めるとtrue
- 試合終了後完全黒でfalse

になる。

ただし結果画面側へfade-inするとき、mode commitのタイミング次第では短時間再びtrueになる余地がある。
そのため、正式実装では状態機械版を推奨する。

---

## 途中起動への対応

エミュレータ／機能を試合途中で有効化する場合、試合前完全黒を観測できない。
その場合だけ、初期化時に次を使って `MATCH_ACTIVE` へbootstrapできる。

```c
if (currentMode == 0x0E && !fullBlack) {
    state.phase = MATCH_ACTIVE;
    state.postMatchFadeArmed =
        flowState == 2 &&
        transitionType == 1 &&
        transitionPhase == 1;
}
```

通常の試合開始から監視できる場合はbootstrapしない方が厳密。

---

## 実装上の注意

1. `transitionBrightness` は必ずsigned 32-bitで読む。
2. 完全黒は `== -16` ではなく `<= -16` を使う。
3. 開始判定は「start-match contextが消えたか」ではなく、完全黒そのものが解除されたかを見る。
4. 終了判定はpost-match fadeを一度armしてから完全黒を見ると、mode commit境界で取り逃がしにくい。
5. 状態更新は1エミュレーションフレームにつき1回行う。
6. pauseやhost側の描画スキップではなく、ROM内のmode transition値を使う。

---

## 確度

```text
完全黒の定義: 高
試合前／試合後fade文脈: 高
期間state machine: 高
全バージョンアドレス: 高
KR1_0 pendingMode訂正: 高
```

## 最終推奨

今回の用途では、3D描画callbackやスポーン可能判定まで追う必要はない。

```text
試合前完全黒を観測
  ↓
完全黒解除でactive = true
  ↓
試合後fadeをarm
  ↓
完全黒到達でactive = false
```

この判定が、要求された「完全暗黒状態の終了後～試合～完全暗黒状態まで」を最も直接かつ安定して表す。
