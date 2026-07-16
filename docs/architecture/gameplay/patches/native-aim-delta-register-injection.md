# Native Aim Delta Hook - AltForm VectorSteer tuning memo

## 目的

`MelonPrimePatchNativeAimDeltaHookRegisterInjectionVersion.inc` は、MPH の通常 aim / alt-form aim に
エミュレータ側の mouse delta (`m_nativeAimDeltaX/Y`) を割り込ませるための
ARM9 instruction hook。

このメモは、Samus / Kanden / Noxus / Spire の alt-form AIM が動くが小さい問題と、
その調整内容を残す。

---

## 問題

Samus / Kanden / Noxus / Spire の alt-form AIM は動作するが、
mouse aim としては移動量が小さすぎた。

対象は `spec108 == 0` の AltForm VectorSteer 経路。

---

## 原因

VectorSteer input write 経路では、`m_nativeAimDeltaX/Y` を
`player+0x464` の input struct にある以下の field へ書く。

| Field | Offset | 内容 |
| --- | ---: | --- |
| Horizontal delta | `input+0x2A` | VectorSteer が読む horizontal 入力 |
| Vertical delta | `input+0x2C` | VectorSteer が読む vertical 入力 |

最初の安全版では、この書き込み値を `±4` に直接 clamp していた。

`±4` はフリーズ回避には安全だが、実用的な mouse aim には弱すぎる。

---

## 現在の方式

V1 / V2 / H1 / H2 の register override は再有効化しない。

現在は、VectorSteer の入力 memory (`input+0x2A/+0x2C`) へ直接書く方式を使う。
これは movement solver の register に割り込まないため、安全側の構成。

必要だったのは、方式の変更ではなく scale の調整。

---

## 変更内容

`NativeAimDeltaHook_ClampToS16()` の近くに、gain と clamp を一緒に扱う helper を追加。

```cpp
[[nodiscard]] static FORCE_INLINE int16_t NativeAimDeltaHook_ClampScaledS16(
    int32_t value,
    int32_t gain,
    int32_t limit) noexcept
{
    int64_t scaled = static_cast<int64_t>(value) * static_cast<int64_t>(gain);

    if (scaled > limit)
        scaled = limit;
    else if (scaled < -limit)
        scaled = -limit;

    return static_cast<int16_t>(scaled);
}
```

VectorSteer input write の旧 `±4` direct clamp を、gain + clamp に置き換えた。

```cpp
static constexpr int32_t kVSteerGain  = 8;
static constexpr int32_t kVSteerClamp = 128;

const int16_t dx = NativeAimDeltaHook_ClampScaledS16(
    m_nativeAimDeltaX,
    kVSteerGain,
    kVSteerClamp);
const int16_t dy = NativeAimDeltaHook_ClampScaledS16(
    m_nativeAimDeltaY,
    kVSteerGain,
    kVSteerClamp);
```

---

## チューニング履歴

| Gain | Clamp | 結果 |
| ---: | ---: | --- |
| `4` | `64` | 安全だがまだ小さい |
| `6` | `96` | 改善したがまだわずかに小さい |
| `8` | `128` | 現在値。OK 判定 |

---

## 今後の調整候補

もし今後、別環境や別感度設定でまだ小さく感じる場合は、
register override へ戻さず、同じ input write 経路の定数だけ調整する。

細かく上げるなら:

| Gain | Clamp | 用途 |
| ---: | ---: | --- |
| `9` | `144` | `8/128` から少しだけ増やす |
| `10` | `160` | まだ小さい場合の次候補 |

不安定化、停止、aim の暴れが出た場合は、直前の安定値へ戻す。

---

## 関連ファイル

```text
MelonPrimePatchNativeAimDeltaHookRegisterInjectionVersion.inc  register injection hook 本体
MelonPrimePatchNativeAimDeltaHookPostFoldWriteVersion.inc      PostFold Write hook 本体
MelonPrimeGameInput.cpp                     .inc の unity include 元
MelonPrimeArm9Hook.cpp                      NativeAimDelta dispatch 登録
MelonPrimeArm9InstructionHook.inc           instruction hook 管理
```
