# melonPrimeDS Vulkan
# Validation Layer → NVIDIA 実機 A/B 実施結果

- 対象リポジトリ: `ag-advania/melonPrimeDS`
- 対象ブランチ: `develop_remakeVulkan_ver2`
- 実施日: 2026-08-13
- 対応指示書: `melonPrimeDS_Vulkan_ValidationLayer_NVIDIA実機AB_実施指示書_2026-08-13.md`
- 対応監査書: `melonPrimeDS_最新Push監査_Runtime移行可否_2026-08-13.md`
- 報告構成: 指示書 §83 に準拠

---

# 0. 要約

Phase 1（Validation Layer）は **Core / Synchronization とも実機で実行し、
最終的に ERROR 0 / hazard 0**。ただしセッション中に実在の VUID を1件発見し、
修正してから再取得している。

Phase 2 / Phase 3 は **未実施**。理由は能力不足ではなく2点:

1. エミュレータの対話操作（ウィンドウ操作、ホットキー、クリック計測）が必要
2. **このドライバでは target-time scheduling が原理的に有効化できない**
   （surface が `presentAtAbsoluteTimeSupported = false`）ため、
   A2/A3 と A1 の差が定義上ゼロになり、A/B を回しても測るものがない

指示書 §71 の分類で表すと:

```text
Phase 1 Core Validation          PASS
Phase 1 Sync Validation          PASS
Phase 1 イベント行列              NOT RUN
Phase 2 NVIDIA functional        PARTIAL
Phase 3 A/B                      BLOCKED（下記 §13 参照）
click-to-photon                  NOT RUN
```

---

# 1. Tested SHA

```text
945823c7a23c8e3b7af8767f6a02137f8860cb1a
```

Validation 実行時のコードは同一内容。working tree は clean
（指示書 §2「dirty working tree の binary で正式 A/B を取らない」を満たす）。

セッション中の VUID 修正は `639a6e8b6` で入っており、**修正前後の両方**で
実機取得している（§3 参照）。

---

# 2. Environment

指示書 §3 のテンプレートに対する実測値。

```text
Commit SHA            945823c7a23c8e3b7af8767f6a02137f8860cb1a
Build type            Debug (preset debug-mingw-x86_64)
Build script          tools/build/windows/build-mingw-validation.bat --jobs 1
Compiler/toolchain    MSYS2 MINGW64 g++ (C++17)
Windows version       Windows 11 Home 10.0.22621
NVIDIA GPU            GeForce RTX 5070 Ti (discrete)
NVIDIA driver         610.74.0.0  (Windows: 32.0.16.1074)
Vulkan loader         vulkan-1.dll, instance API 1.4.357
Device API            1.4.341
Validation layer      VK_LAYER_KHRONOS_validation (C:\VulkanSDK\1.4.357.0)
ビルド時 Vulkan header MinGW v296 → VulkanModernPresentCompat.h が実際に稼働
Monitor / refresh     未記録（Phase 3 未実施のため）
G-SYNC / VRR          未記録
VSync                 ON（C0 のみ OFF）
Window mode           windowed
Internal resolution   default
TargetFPS             60.0
ROM                   build/codex-visual-checks/mph-test.nds
Savestate             未使用（直起動）
Power plan            未記録
NVIDIA CP overrides   未記録
Overlays              未無効化（記録のみ。§12 の clean run は未実施）
```

未記録項目は Phase 3 を実施する際に必ず埋めること。Phase 1 の
correctness 判定には影響しない。

---

# 3. Validation result

## 3.1 前提ゲート（指示書 §10）

```text
[Vulkan] loaded vulkan-1.dll, instance API version 1.4.357
[Vulkan] validation layer enabled
```

を起動ログで確認。BLOCKED 条件（layer 未インストール）には該当しない。

**重要**: `tools/build/windows/build-mingw.bat` の成功は Validation が
動いた証拠にならない（`MELONDS_VULKAN_ENABLE_VALIDATION` は
`CONFIG:Debug` でしか付かない）。今回のために
`debug-mingw-x86_64` preset と専用スクリプトを追加した。
release ツリーの build.ninja には当該 define が 0 箇所、debug ツリーには
124 箇所あることを確認済み。

## 3.2 Pass A — Core Validation

初回実行で **VUID を検出**:

```text
VUID-VkPresentTimingInfoEXT-timeDomainId-12400
vkQueuePresentKHR(): pPresentInfo->pTimingInfos[0].timeDomainId is 0,
which is not a valid time domain id that has been returned by
vkGetSwapchainTimeDomainPropertiesEXT().
(duplicate_message_limit 10 に到達)
```

原因: `VkPresentTimingInfoEXT::timeDomainId` は target time を要求しない
present でも列挙済み ID でなければならないが、target-time 分岐の中でしか
設定していなかった。**出荷デフォルトの TelemetryOnly でも常時発生**していた。

修正（`639a6e8b6`）:
- timing metadata は `TimeDomainsReady` 成立後のみ添付
- `timeDomainId` は target の有無にかかわらず常に設定
- `targetTimeDomainPresentStage` は新規 VUID を作らないため据え置き

修正後の再取得結果:

| 構成 | Policy | Reflex | validation ERROR | WARNING |
|---|---|---|---|---|
| 1 | 0 TelemetryOnly | Off | 0 | 0 |
| 2 | 1 PresentWait | Off | 0 | 0 |
| 3 | 2 JustInTime | Off | 0 | 0 |
| 4 | 3 JustInTimeFifoLatestReady | Off | 0 | 0 |
| 5 | 2 JustInTime | On | 0 | 0 |
| 6 | 2 JustInTime | On+Boost | 0 | 0 |
| C0 | 2 JustInTime, VSync OFF | Off | 0 | 0 |

各回 ROM 起動あり、約40〜50秒。timing/present 関連 VUID も 0。

## 3.3 Pass B — Synchronization Validation

`vk_layer_settings.txt`:

```text
khronos_validation.validate_core = true
khronos_validation.validate_sync = true
khronos_validation.report_flags = error,warn,perf
khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
khronos_validation.log_filename = C:\tmp\vk-sync-run.log
```

**空ログを「所見なし」と読む前に、設定が読まれている証明を取った**。
`report_flags` に `info` を足した control run で layer 自身が出力:

```text
Validation Information: [ CURRENT-VALIDATION-ENABLED ]
vkCreateInstance(): Current Validaiton Enabled:
  - Core Checks
  - Synchronization      ← 有効化の証拠
  - Stateless Parameter
  - Object lifetime
  - Thread Safety
  - Handle Wrapping
```

結果: policy 0 / 2 / 3 と Reflex On+Boost で
**blocking hazard 0、error 0、warn 0、perf 0**。

実施後 `vk_layer_settings.txt` は削除済み（残すと以後の全実行で暗黙に
sync validation が有効化され、特に latency 計測を汚染するため）。

## 3.4 未実施（Pass A / B とも）

指示書 §18〜§21 のイベント行列は **NOT RUN**:

```text
fullscreen toggle x20
resize x50 / DPI change / minimize / restore
F2 Video Settings open/cancel/apply x20
Vulkan ↔ Software / OpenGL Compute / DX12 切替 x20
ROM close / reopen / savestate load / reset
Fast Forward hold / toggle / Slow Motion
```

いずれもウィンドウ操作とホットキー入力を伴うため未実施。

---

# 4. Extension capability

デバイスレベル（起動ログ実測）:

```text
caps2                      yes
present-id2                yes
present-wait2              yes
calibrated-timestamps      yes
present-timing             yes
present-at-absolute-time   yes
present-at-relative-time   yes
fifo-latest-ready          yes
VK_NV_low_latency2         yes (device-extension-enabled=yes)
```

surface レベル（実測）:

```text
present-id2                yes
present-wait2              yes
present-timing             yes
presentAtAbsoluteTimeSupported   NO   ← 決定的
target present stage       IMAGE_FIRST_PIXEL_OUT
time domain                PRESENT_STAGE_LOCAL (domainId=1000208000)
timing queue size          16
available present modes    FIFO, FIFO_RELAXED, MAILBOX, IMMEDIATE,
                           FIFO_LATEST_READY
```

**デバイスは absolute time scheduling に対応しているが、surface が
非対応を返す。** FIFO_LATEST_READY は present mode としては利用可能。

---

# 5. Functional runtime result

指示書 §85 に対する実測。

```text
[x] expected NVIDIA GPU selected      RTX 5070 Ti
[x] VK_NV_low_latency2 enabled        device-extension-enabled=yes
[x] Reflex actual=active              Reflex On で確認
[x] Reflex On                         mode=on lowLatencyMode=true
[x] Reflex On+Boost                   lowLatencyMode=true lowLatencyBoost=true
[ ] present ID correlation verified   NOT RUN（ログ突合未実施）
[ ] vkGetLatencyTimingsNV report      NOT RUN
[ ] marker timestamps non-zero        NOT RUN
[ ] JIT bootstrap → active 遷移        BLOCKED（§13）
[ ] targetTime non-zero               BLOCKED（§13）
[x] FIFO_LATEST_READY checked         capability gate により非選択 = UNSUPPORTED
[x] no queue full storm               queue-full 0
[x] no device lost                    DEVICE_LOST 0
[ ] no F2 regression                  NOT RUN（F2 未操作）
```

pacing authority の実機挙動（指示書 §51 の期待どおり）:

| Reflex | Policy | authority | boundedWait | targetScheduling |
|---|---|---|---|---|
| Off | 0 | GenericHost | off | off |
| Off | 1 | GenericPresentTiming | **on** | off |
| Off | 2 | GenericPresentTiming | **on** | off |
| Off | 3 | GenericPresentTiming | **on** | off |
| On | 2 | **NvidiaReflex** | off | off |
| On+Boost | 2 | **NvidiaReflex** | off | off |

> Reflex 有効時に generic の wait / target が両方 off になることを実機で確認。
> vendor authority ルールの runtime 証明が取れた。

VSync OFF control（§16）:

```text
selected-present-mode = IMMEDIATE
targetScheduling      = off
validation error       = 0
```

ただし fallback reason は指示書が期待する `present mode is not FIFO` ではなく
`absolute timing unsupported by surface`。この surface では両条件が成立して
おり、classifier は debug しやすい順で先に来る方を報告する設計のため。
absolute timing 対応 surface では非FIFO理由が表に出る。**バグではない。**

---

# 6. A/B matrix

**NOT RUN。** 指示書 §70 の表は1行も埋まっていない。

| Mode | Runs | FPS | FT P50 | FT P95 | FT P99 | Latency P50 | P95 | P99 | QueueFull | WaitTimeout | Status |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| A0 Telemetry | 0 | | | | | | | | | | NOT RUN |
| A1 PresentWait | 0 | | | | | | | | | | NOT RUN |
| A2 JIT | 0 | | | | | | | | | | BLOCKED |
| A3 JIT Latest | 0 | | | | | | | | | | UNSUPPORTED |
| B1 Reflex On | 0 | | | | | | | | | | NOT RUN |
| B2 Reflex Boost | 0 | | | | | | | | | | NOT RUN |

---

# 7. Raw metrics

Phase 1 の生ログ（本コミットで `docs/archive/audits/vulkan/2026-08-13-validation/`
へ保存）:

```text
core-p0.log            policy 0 / Reflex Off
core-p1.log            policy 1 / Reflex Off
core-p2.log            policy 2 / Reflex Off
core-p3.log            policy 3 / Reflex Off
core-reflex-on.log     policy 2 / Reflex On
core-vsync-off.log     policy 2 / Reflex Off / VSync OFF
sync-enabled-banner.log  Synchronization 有効化の証拠
```

Phase 3 の CSV は存在しない（未実施）。

---

# 8. Aggregated metrics

**NOT RUN。** 集計対象データがない。

集計器自体は用意済みで動作確認済み:
`tools/perf/aggregate-vulkan-latency.py`（run 単位で percentile を出してから
mode 比較。全 frame を pool すると長い run が過剰 weight になるため）。
合成データ9 run で入出力を検証済み。

---

# 9. Regression

**なし。**

```text
DEVICE_LOST                 0
software fallback           0
Vulkan option grey-out      0（未操作のため観測機会なし）
swapchain recreate storm    0
hang / crash                0
timing queue full           0
timing queue recovery       0
present wait timeout        0
```

Reflex / Anti-Lag の marker 順序、host FPS limiter の所有権、
F2 probe 修正、Windows device lifetime 修正はいずれも静的監査で維持を確認済み。

---

# 10. Unsupported

```text
target-time scheduling      UNSUPPORTED
  理由: surface presentAtAbsoluteTimeSupported = false
  挙動: targetTime = 0 のまま、fallback reason を明示して継続
  判定: capability gate の正常動作。FAIL ではない。

FIFO_LATEST_READY           UNSUPPORTED
  理由: 上記により JIT capability gate が成立しない
  挙動: FIFO を選択
  判定: 指示書 §46 のとおり fallback FIFO を FAIL 扱いしない
```

---

# 11. Winner / No material difference

**判定不能。** A/B 未実施のため。

指示書 §72 の「1 run だけ勝った mode を winner にしない」以前の段階。

---

# 12. Remaining NOT RUN

```text
Phase 1 イベント行列        NOT RUN（要人手操作）
Phase 2 present ID 相関     NOT RUN
Phase 2 Reflex timing report NOT RUN
Phase 3 A/B 全 mode         NOT RUN
click-to-photon             NOT RUN（Reflex Analyzer 未使用）
external latency 測定       NOT RUN
AMD runtime                 NOT RUN
Intel runtime               NOT TESTED（Arc 実機なし）
MoltenVK runtime            NOT RUN
Linux build / runtime       NOT RUN
macOS build / runtime       NOT RUN
Vulkan OFF build            NOT RUN
GitHub Actions              NOT VERIFIED
120/144Hz, VRR ON/OFF       NOT RUN
```

指示書 §88 の「今回はまだ PASS と書いてはいけない項目」を遵守。

---

# 13. Recommendation

## 13.1 最優先: relative-time scheduling の実装

現状、このドライバでは **A2/A3 が A1 と定義上まったく同じ挙動**になる。
absolute time が surface 非対応である以上、A/B を回しても target-time の
効果は測定できない。「差がなかった」という結果すら得られず、
「そもそも動いていない」だけになる。

一方でデバイスは `presentAtRelativeTime = yes` を報告している。
`VK_PRESENT_TIMING_INFO_PRESENT_AT_RELATIVE_TIME_BIT_EXT` を使えば、
このハードウェアで target-time presentation を実際に動かせる可能性が高い。

コード側は `PresentTimingRelative` を既に surface capability として
保持しているが、scheduler へ未接続。前回指示書 §16 でも
「最初は relative が比較的単純」とされていた経緯がある。

必要作業の概略:

```text
VulkanPacingCapabilities へ RelativeTimingDevice / RelativeTimingSurface
ClassifyVulkanTargetFallback を absolute / relative の二択へ
relative の場合 targetTime = 次フレームまでの相対 ns
flags = PRESENT_AT_RELATIVE_TIME_BIT
timeDomainId の扱いを relative 用に確認（VUID 再発防止）
pure test に relative 経路を追加
CI contract に「absolute 非対応でも relative で target が出る」を追加
```

**これを先に行わないと Phase 3 に意味がない。**

## 13.2 次点: Phase 1 イベント行列の消化

fullscreen / resize / DPI / minimize / F2 / renderer 切替 / 速度モード。
人手操作が必要だが、swapchain 再生成と device lifetime に触る最も
危険な経路であり、A/B より先に潰す価値が高い。

runbook（`docs/development/testing/vulkan-present-pacing-runbook.md`）に
回数付きの手順を用意済み。

## 13.3 Phase 3 開始条件

```text
13.1 完了（または absolute 対応 surface を持つ環境を用意）
13.2 完了
Phase 3 用ビルドは MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF かつ
MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=ON
Validation Layer は OFF
```

計測基盤（capture flag / CSV / 集計器 / runbook）は整備済みで、
着手条件が揃えばそのまま実行できる。

## 13.4 default policy

`TelemetryOnly` を維持。指示書 §75 のとおり source default は変更しない。
今回の VUID 修正は**まさにその TelemetryOnly でも発生していた**ため、
実機 Validation を A/B より先に置いた指示書の順序判断は正しかった。

---

# 14. 一次資料

- 実施指示書: `melonPrimeDS_Vulkan_ValidationLayer_NVIDIA実機AB_実施指示書_2026-08-13.md`
- 監査書: `melonPrimeDS_最新Push監査_Runtime移行可否_2026-08-13.md`
- 手順書: `docs/development/testing/vulkan-present-pacing-runbook.md`
- backend 仕様: `docs/features/rendering/vulkan-backend.md`
- 生ログ: `docs/archive/audits/vulkan/2026-08-13-validation/`
- VUID: https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html#VUID-VkPresentTimingInfoEXT-timeDomainId-12400
