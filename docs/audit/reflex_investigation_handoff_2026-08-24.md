# NVIDIA Reflex 調査 — 引き継ぎ文書

- 日付: 2026-08-24
- ブランチ: `develop_hud`（未コミット、作業ツリーに変更あり）
- 起点: `.codex/MelonPrimeDS_develop_hud_Vulkan_Reflex_ON_FPS低下_根本原因監査_修正指示書_2026-08-24.md`
- 関連: `docs/archive/audits/rendering/2026-08/vulkan_reflex_on_preissue_ab_2026-08-24.md` /
  `docs/archive/audits/rendering/2026-08/dx12_reflex_latency_verification_2026-08-24.md`
- 状態: **継続中**。コード変更は完了・検証済み。§8.1 完了（2026-08-25）。性能の原因究明は未決着。

---

## 1. 一行でいうと

指示書が提案した修正は実測で否定され、**両バックエンドの Reflex 実装に欠陥は見つからなかった**。
Vulkan Reflex On の FPS 低下は `vkLatencySleepNV` 内部で起きており、リポジトリ側から届く
レバーは今のところ見つかっていない。今回の主な成果は**修正ではなく、計測基盤と否定された仮説の確定**。

---

## 2. 確定した事実（フォーカス制御済み、再現性あり）

最終ベースライン。Vulkan / Scale 2 / VSync Off / Frame Limit Off / F7 /
`-Action steady-state`（ウィンドウフォーカス固定）/ developer build / 各3ラン。

| mode | FPS | `vkLatencySleepNV` p50 | join p50 |
|---|---:|---:|---:|
| Off | 534 / 534 / 534 | 1798.6 / 1798.4 / 1798.8 µs | 165 / 159 / 179 µs |
| On | 348 / 344 / 344 | 1234.7 / 1232.7 / 1230.1 µs | — |

ばらつきは sleep で 0.4 µs 以内。これが本調査で唯一、条件を完全に制御して取れた数値。

その他、複数回再現した事実:

- **`vkWaitSemaphores` は p50 2 µs**。ブロックは `vkLatencySleepNV` 本体にある。
- **Vulkan の GPU 実働は 52〜65 µs/frame**（独立 GPU timestamp）。GPU 律速ではない。
- **DX12 の `NvAPI_D3D_Sleep` は p50 1.2 µs**、Reflex On/Off/Boost すべてで 533 FPS 維持。
- **DX12 の GPU 実働は `gpuActiveRenderTimeUs` 33〜35 µs**、`gpuFrameTimeUs` ≈ 1804 µs。
- **フレームリミッタ 60 FPS 時、Vulkan の sleep は 17〜29 µs**。固定コストではない。
- **present mode（IMMEDIATE↔FIFO）と swapchain 枚数（3↔2）を変えても sleep は動かない**
  （1798→1798、1249→1244）。ただし §5 の注意書きを読むこと。

---

## 3. 公式ドキュメントとの照合結果

公式 `nvapi.h`（NVIDIA/nvapi main）をローカルに取得して全項目照合。

| 項目 | 判定 |
|---|---|
| `NV_SET_SLEEP_MODE_PARAMS` 全フィールド | 一致（`bUseMinQueueTime` は実在フィールド） |
| `NV_LATENCY_MARKER_PARAMS`（88 B） | 一致 |
| `NV_LATENCY_MARKER_TYPE` 0〜6 | 一致 |
| `NvAPI_D3D_Sleep` の呼び出し位置 | 公式「フレーム冒頭、入力サンプリング前」に一致 |
| Off 時も Sleep を呼ぶ | 公式推奨に一致 |
| `NvAPI_D3D12_SetCreateCommandQueueLowLatencyHint` | 公式で "Reserved call." → 未使用が正しい |

公式に明記されている重要な一文:

> low latency mode は "to intelligently lower latency **without impacting frame rate**"

Vulkan Reflex On の -36% は、この記述に反する。DX12 の 533 FPS 維持が仕様どおりの姿。

---

## 4. コード変更（すべて実装・検証済み）

### 4.1 採用したもの

| 変更 | 内容 |
|---|---|
| sleep の世代所有 | `VulkanReflexSleepIsOwnedByFrame()`。present N 後に発行した sleep はフレーム N+1 の所有物で、`FinishFrame()` は自世代のものだけ join する。従来 Off 経路で暗黙 `bool` だった規則を明示化し、単体テスト（`TestVulkanReflexSleepGenerationOwnership`）と CI ratchet を追加 |
| `reflex_sleep_join_us` | Vulkan。worker のドライバ待ちと hot path の join を分離。これが無ければ今回の判定は不能だった |
| `DX12NvidiaReflex::QueryTimings()` | `NvAPI_D3D_GetLatency`（id `0x1A587F9C`、実行時解決・任意）。DX12 に存在しなかった読み戻し。`vkGetLatencyTimingsNV` の対 |
| `DX12NvidiaReflexLatencyReportStatus` | 「エントリポイント未解決 / クエリ拒否 / 成功したが空」を区別。§6 の教訓を形にしたもの |
| `DX12Perf::CpuMetric::ReflexSleep` | `NvAPI_D3D_Sleep` の実測。Vulkan 側 `reflex_latency_sleep_us` と対称 |
| `ReportReflexLatencyTimings()` | developer build 限定、600 フレーム毎。Vulkan 側と同形式 |
| `MELONPRIME_VULKAN_DISABLE_SWAPCHAIN_LATENCY_MODE` | 開発者 A/B 専用フック（既定 off）。`VkSwapchainLatencyCreateInfoNV::latencyModeEnable` を外せる。**効果は未測定**（§5.3） |

### 4.2 A/B で否定し、採用しなかったもの

**指示書 §7 の本命案（sleep を前フレーム present 直後に先行発行）** — 実測で棄却。

- 理由: ブロックは semaphore ではなく `vkLatencySleepNV` 本体（wait は 2 µs）。熟成させる semaphore が存在しない
- Off が速いのは sleep をフレーム丸ごと（約1.7 ms）に重ねられるから（join 165 µs）。On は入力前に完了必須で、重ねられるのは present→次 BeginFrame の数十 µs しかない
- 実測: p50 1247 → 1240 µs、FPS はむしろ 337→332 と約5 FPS 悪化（worker ホップ分）
- **deadline は重畳できない**

On/OnBoost は inline 同期 sleep のまま（公式の推奨位置）。

### 4.3 自分のコードで見つけて直した欠陥

- `NvAPI_D3D_GetLatency` の interface id を記憶で `0x1452F25A` としていた（正: `0x1A587F9C`）
- `NvLatencyFrameReport` 末尾の `cameraConstructedTime` / `crossAdapterCopyTimeUs` /
  `aiFrameTimeUs` が欠落し `rsvd[120]` にしていた。合計 240 B が偶然一致し `static_assert` を
  通過していた。読んでいた値は先頭 120 B 内なので実測値への影響はなし

---

## 5. 否定された仮説（戻らないこと）

| # | 仮説 | 判定 |
|---|---|---|
| 1 | sleep 発行直後に join するから遅い（指示書 §6.3） | **否定**。wait は 2 µs。ブロックは `vkLatencySleepNV` 本体 |
| 2 | 先行発行すれば熟成して速くなる（指示書 §7） | **否定**。1247→1240 µs、FPS は悪化 |
| 3 | DX12 Reflex は動いていない | **否定**。ドライバは我々の frameID と一致するレポートを返す。コスト 0 なのは GPU が 2〜3% しか動かずキューが無いから |
| 4 | DX12 に present 基準の sleep guard が無いのは欠陥 | **否定**。公式契約は "exactly once on each frame"。DX12 側が正しい |
| 5 | Vulkan は GPU 律速だから Reflex が待たせている | **否定**。GPU 実働 52〜65 µs |
| 6 | Vulkan の 1.25 ms は正当な pacing なので 337 FPS で正しい | **不採用**。公式が "without impacting frame rate" と明記 |
| 7 | VSync が壊れている（FIFO なのに 534 FPS） | **否定**。モニタが 540 Hz。534 FPS は FIFO の正常動作 |
| 8 | DX12 の post-input fence wait が Reflex の効果を相殺している | **否定**。`present_begin_wait_us` は p50 14.8〜15.0 µs / p95 16.5 µs（9 ラン全モードで一致）。フレームの 0.7%。`Commands.Begin()`→`TryBegin()` にしても最大 15 µs しか縮まず、代わりにフレームドロップを導入することになる |

### 5.3 判定保留（重要）

**present mode / swapchain 枚数を変えても sleep が動かない → WSI backpressure ではない**、
という §2 の結論は **成立していない**。

- VSync arm: モニタが 540 Hz なので FIFO の上限が 534 FPS のアプリを何も制約しない。**空振り**
- image arm: 540 Hz 律速なら 2 枚でも足りるので、動かなくて当然
- したがって WSI 仮説は**生きている**

`MELONPRIME_VULKAN_DISABLE_SWAPCHAIN_LATENCY_MODE` の A/B も、フォーカス未制御で実施したため
**結果は無効**（同一条件の r1/r2 で sleep が 13 µs と 1800 µs に割れた）。

---

## 6. 計測プロトコル（今回の失敗から確定した必須事項）

このセッションでは計測方法の欠陥により **3 回、誤った結論を出しかけた**。以後の Reflex 検証では
以下を契約とする。

1. **ウィンドウフォーカスを固定する。**
   `-Action savestate-load` は `Focus-RendererWindow` を呼ばない。呼ぶのは `steady-state` /
   `projectile-burst` / `room-transition`。ハーネス自身が
   「背景ウィンドウのスケジューリングは Raw Input 配送と観測 FPS の両方を変える」と警告している。
   **本セッションの大半の計測はこの条件を満たしていない。** フォーカス固定後は sleep のばらつきが
   0.4 µs 以内に収束した。
2. **計測中に他の作業を一切走らせない。** ビルド・`curl`・コンパイルの並行実行で 1 バッチを汚染した
   （FPS が 5〜503 に暴れた）。
3. **run 別に出す。pooled median 禁止。** プールした中央値が 2 ランに引きずられ、存在しない
   「DX12 On が -230 µs」を作り出した。ラン別に分解して初めて 9 ラン中 8 つが同一と判明した。
4. **テレメトリは全レポートの中央値を取る。`tail -1` 禁止。** 最後の 1 行は終了処理中の値を拾う。
5. **run 前に FPS の min/max を確認して汚染を判定してから中身を読む。**

---

## 7. 未証明・未決着

- ~~**Vulkan / DX12 Reflex のレイテンシ効果**~~ → §8.1 で決着（Vulkan -60 µs、DX12 ±4 µs）
- **`inputSample→presentEnd` は CPU 側の時刻**。Vulkan On は提示回数が 534→345 に減るため、
  photon 到達までの総レイテンシで勝っているかは未証明
- **`vkLatencySleepNV` が何を待っているか**。無制限時 1798 µs は 540 Hz の 1 周期 1852 µs に近く、
  3 バックエンド（537 / 534 / 534）が揃って 540 Hz のすぐ下に着地している。
  **ディスプレイ律速の可能性を潰せていない**
- **`bUseMinQueueTime=1`**（DX12）。未実験。修正ではなく研究的 A/B としてのみ

---

## 8. 次にやること（優先順）

### 8.1 フォーカス固定でレイテンシを取り直す — **完了（2026-08-25）**

`-Action steady-state`、各 3 ラン、`build/verification/vk-focus-controlled-20260824/`。
全 18 ランで FPS の min/max が狭く汚染なし。`sim` も二極化していない。

| condition | n | p25 / median / p75 | sim med | Δ vs Off |
|---|---:|---|---:|---:|
| Vulkan Off | 102 | 1832 / **1834** / 1841 | 1655 | — |
| Vulkan On | 66 | 1749 / **1770** / 1799 | 1674 | **-64 µs** |
| Vulkan Boost | 66 | 1750 / **1776** / 1808 | 1676 | **-58 µs** |
| DX12 Off | 95 | 1798 / **1818** / 1838 | 962 | — |
| DX12 On | 96 | 1797 / **1814** / 1834 | 960 | -4 µs |
| DX12 Boost | 96 | 1800 / **1822** / 1851 | 963 | +4 µs |

**Vulkan Reflex は実際にレイテンシを下げている**（Vulkan Off の p25 1832 が Vulkan On の
p75 1799 を上回り、分布が重ならない）。**DX12 Reflex は何もしていない**（±4 µs、
ばらつき幅 約 40 µs に埋没、n=95〜96）。

ただし幅は **-60 µs** であり、フォーカス未制御下で観測した -260〜380 µs は**過大だった**。
代償は 534 → 344 FPS（-36%）。

副産物として F2（§8.1b）が裏付いた。DX12 の `sim` 962 µs と Vulkan の 1655 µs の差は
エミュ速度差ではなく RenderSubmit の切り方の違い（DX12: 962+605=1567 /
Vulkan: 1655+30=1685、合計は近い）。

#### ここで生じた最重要の緊張

-60 µs は人間の知覚閾値をはるかに下回る。さらに `presentEnd` は CPU 側の時刻であり、
540 Hz ディスプレイ上で 534 FPS は提示周期と噛み合うが 344 FPS では噛み合わないため、
スキャンアウト待ちが平均 0.5 ms 程度増える。

**photon 到達までの総レイテンシでは Vulkan On が負けている可能性が高い**
（CPU 側 -60 µs に対し表示側 +440 µs 程度）。これはユーザの体感と逆になる。

体感が本物なら、平均レイテンシではなくフレームペーシングの安定性など別の要因を
見ている可能性がある。**§8.6（display 側計測）が最優先論点に繰り上がった。**

### 8.1b RenderSubmit marker の意味の非対称（未決着・要検討）

`.codex/melonPrimeDS_Vulkan_vs_DX12_Reflex_実装差_静的監査_比較調査_2026-08-24.md` の
指摘（F2）。コードと実測の両方で裏付け済み。

- DX12 は `GPU_DX12.cpp:255`（レンダラ/GPU2D 生成側）で RENDERSUBMIT_START を発行
- Vulkan は `MelonPrimeVulkanPresenter.cpp:2757`（最終 presenter queue submit）で発行
- 実測スパン: **DX12 605 µs / Vulkan 22〜37 µs**。同じマーカーが別のものを囲っている

DX12 は `bUseMarkersToOptimize=1` なので、ドライバの最適化モデルに影響しうる。
実害は未証明だが、どちらが NVIDIA の意図する RENDERSUBMIT かを公式仕様で確定させ、
両バックエンドで揃える価値がある。

## 8.2 ディスプレイ律速の切り分け

540 Hz が効いているかを確定させる。モニタのリフレッシュレートを下げて（要ユーザ許可）
無制限 FPS と sleep がそれに追従するかを見る。追従すればディスプレイ律速が確定し、
「CPU 律速だから Reflex は何もすべきでない」という §5 の前提が崩れる。

### 8.3 swapchain latency mode の A/B をやり直す

`MELONPRIME_VULKAN_DISABLE_SWAPCHAIN_LATENCY_MODE=1` をフォーカス固定で。
Off 経路の 1798 µs が消えるならリポジトリ側にレバーがある。消えなければドライバ内部で確定。

### 8.4 最小再現コード

melonPrimeDS 固有処理を排除するため、swapchain + 軽量 GPU 負荷 + `VK_NV_low_latency2` だけの
最小再現を書く。同じ 1 フレーム分 sleep が出ればドライバ/WSI 側に強く帰属できる。

### 8.5 ドライババージョン A/B

アプリ側に欠陥が無い以上、価値が高い。

### 8.6 display 側レイテンシ計測

LDAT / 高速度カメラ / PresentMon。この環境では未実施。

---

## 9. やってはいけないこと

1. **UI は On のまま内部で Reflex を実質無効化する**（性能偽装）。
   「sleep を 1 フレーム早く発行して熟成させる」案は、成功すると FPS は戻るが Vulkan を
   DX12 と同じ「速いがレイテンシ削減ゼロ」にするだけの可能性が高い。FPS だけで合格判定しない
2. **marker / present ID を性能目的で削除する**（Off 経路で効果なしと確認済み）
3. **frames-in-flight を増やして FPS を稼ぐ**
4. **DX12 を Vulkan の FPS 挙動へ無理に合わせる**（DX12 が仕様どおり）
5. **`useMinQueueTime=1` を根拠なく既定化する**
6. **ドライバ内部のブロックを worker で隠して「解決」扱いにする**（On では入力前完了が契約）

---

## 10. 検証成果物

```
build/verification/vk-focus-controlled-20260824/   最終ベースライン（唯一の完全制御条件）
build/verification/reflex-preissue-20260824/       指示書 §7 の A/B と Phase 4/5
build/verification/dx12-reflex-latency-20260824/   DX12 読み戻しとレイテンシ
build/verification/vk-wsi-matrix-20260824/         present mode / image 数 / frame limit
build/verification/vk-latencymode-ab-20260824/     latency mode A/B（フォーカス未制御・無効）
build/verification/vsync-check-20260824/           VSync 対照実験
```

FPS は各 `.harness.log` の `title=[NNN/60`、テレメトリは `MELONPRIME_PERF=1` + `.err.log` の
`[VulkanPerf] cpu` / `[DX12Perf] cpu` 行、ドライバレポートは `.out.log` の
`Reflex timings:` 行から取得する。

---

## 11. リポジトリの状態

- 監査: low-latency contract / DX12 shaders（117 variants）/ SRP / thread-boundary /
  instance-state / inc-ownership / config-defaults / hud-key-parity / color-dialog-prefs、
  すべて rc=0
- `git diff --check` クリーン
- 指示書 Phase 5（F1/F2/F6/F7 の 1x exact、Off と Reflex On）: 8/8 PASS、
  mismatch / fallback / bad marker / unexpected blank すべて 0
- 未コミット。コミットは未実施（指示なし）
