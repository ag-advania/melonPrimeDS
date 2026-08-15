# Structured screen routing optimization audit (2026-08-12)

## 結論

`melonPrimeDS_gpu_renderer_efficiency_reaudit_444b3437.md` が最優先とした
REPERF-07を実装し、通常表示でSoftware 2Dが行っていた
`StructuredEnginePlanes -> StructuredScreenPlanes` の4-planeコピーを廃止した。

- 削減したproducer copy: **1,572,864 B/frame -> 0 B/frame**
- 通常F7のroute: **上下画面各1 run、合計2 runs/frame**
- GPU structured staging: **2,757,120 B/frameのまま**（phase 1の境界を維持）
- Vulkanの対象区間中央値（route copy + compose pack）:
  **138.36 us -> 111.00 us、-19.8%**
- DX12の対象区間中央値:
  **209.96 us -> 113.20 us、-46.1%**
- USA Rev 1 F4/F5/F7/F8 native 1x:
  Vulkan / DX12ともSoftware 3Dとの差分0
- off / VRAM / FIFO / forcePlain / screen-disabled相当の出力は
  screen-owned fallback planeを使う契約を維持した。
- capture source Aは引き続きengine Aの4 planesを直接参照する。

作業中に、`CustomHUD=false`でもVulkan / DX12のnative radar passだけが
`BtmOverlayEnable`を見て上画面へ下画面レーダー色を描く既存回帰も再現した。
両backendをCPU HUDと同じ`hudVisible`契約でgateし、実画面で消失を確認した。

## 入力と監査範囲

- 基準HEAD: `444b3437f` (`Fixing performance`)
- 参考再監査:
  `E:/##Users/Admin/Downloads/melonPrimeDS_gpu_renderer_efficiency_reaudit_444b3437.md`
- 参考再監査SHA-256:
  `093362BA815822EF005DC712A217E1E9C7280F06472EC6048F395311049C018D`
- ROM:
  `0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`
- state: `.ml4`, `.ml5`, `.ml7`, `.ml8`
- Windows実機backend: Vulkan、DX12

Metal Computeは今回変更していない。共通fixed variant index、Metal source ratchet、
既存Metal監査の結論は維持されるが、このWindows作業環境ではMSL compile、macOS実機、
Metal/Software differentialを再実行していない。したがって本書はMetal runtime成功を
新たに主張しない。

## 変更前の無駄

通常表示の各scanlineは、engine-owned structured dataを作った直後に、LCD screen-owned
storageへ同じ4 planesをコピーしていた。

```text
StructuredEnginePlanes[engine][plane][line]
    -> 4 x memcpy(256 u32)
StructuredScreenPlanes[screen][plane][line]
    -> VBlankでさらにGPU stagingへコピー
```

上下2画面、4 planes、256 x 192、u32なので中間コピーは次の固定量だった。

```text
2 * 4 * 256 * 192 * 4 = 1,572,864 bytes/frame
```

F7のtelemetryはregular 384 lines/frame、fallback 0 lines/frameであり、計算値と
実測counterが一致した。

## 実装

### Scanline-time routing

`SoftRenderer`が`StructuredScreenSource[2][192]`を所有し、scanline生成時点で次を記録する。

- `0`: engine A
- `1`: engine B
- `2`: screen-owned fallback

通常表示ではrouteだけを書き、`StructuredScreenPlanes`へコピーしない。
screen swapはVBlank時の現在値から推測せず、各scanlineを生成した時点のscreenへ保存する。

fallbackは従来どおり`StructuredScreenPlanes`へ完成pixelを書き、route `2`を記録する。

- display off
- VRAM display
- FIFO display
- `forcePlain`
- screen disabled
- VCOUNT範囲外
- `SnapshotStructuredVramDisplayLine()`が保持したcapture/VRAM line

### Run-based pack

Vulkan / DX12は共通の`PackRoutedScreenPlanes()`を使う。各screenのrouteを連続runへまとめ、
runごとに4 planesをコピーする。通常フレームはscreenごとに1 runなので、最初の8 screen
planesは8回の大きな`memcpy`になる。1,536回のline-sized copyへは分解しない。

capture/provenance 6 planes、line metadata、capture commands、GPU staging総量は変更していない。

### Capture contract

`StructuredVulkanFrameView::CaptureSourcePlane`はroutingとは独立して、従来どおり
`StructuredEnginePlanes`のengine Aを参照する。source B native/reference、capture command、
retained high-resolution capture referenceも変更していない。VRAM display snapshotはfallback
storageを所有し、capture前のread-before-write順序を維持する。

### Telemetry

`MELONPRIME_PERF=1`で次をVulkan / DX12の1 Hz counterへ出す。

- `route_copy_B`
- `route_copy_ns`
- `regular_lines`
- `fallback_lines`
- `route_runs`

通常実行ではruntime gateによりroute copy timerを使用しない。最適化後は通常lineで
timer自体を呼ばず、bytes/timeとも0になる。

### Native radar visibility回帰

原因はVulkan / DX12固有の条件が`gpuFrame && m_radarEnable`だけだったことである。
`BtmOverlayEnable=true`はCustom HUDをoffにしても設定として保持されるため、native
colour-key passだけが上画面へ残っていた。

両backendを`gpuFrame && hudVisible && m_radarEnable`へ変更した。edit modeを含むCPU HUDの
既存visibility判定と同じ結果を使うため、Custom HUD offでは描画されず、edit modeのpreviewは
維持される。SRP/performance auditへ両ファイルのvisibility gate ratchetも追加した。

## Formal性能測定

### 条件

- USA Rev 1 F7 (`.ml7`)
- native 1x、VSync off、FPS limit on
- Software oracleなし
- `MELONPRIME_PERF=1`
- 各processの先頭60個の1 Hz windowを個別に除外
- backend / before-afterごとに300 windowを集計
- percentile列は300 windowが報告した各percentileの中央値
- 複数runは`tools/perf/summarize-renderer-perf.py --skip-first-windows 60`
  でwarmupをrunごとに除外してから結合
- GPU/CPU clock、core affinity、power stateは固定していない

REPERF-07だけを比較するため、formal before/afterはnative radar visibility修正前の同じ
presenter条件を持つ保存済みbinary同士を使用した。

各binaryはそれぞれが生成したconfig schemaを使用しており、`melonDS.toml`全体は
byte-identicalではない。新しいbinary側には追加の既定値キーがmaterializeされ、window
geometryも異なる。一方、比較対象のbackend、native 1x、VSync off、FPS limit on、
`CustomHUD=false`、F7 stateは一致する。従って結果は削除したbytesの直接counterと
`route copy + compose pack`を主根拠とし、window/presenterを含む一般的なframe高速化は
主張しない。

- before SHA-256:
  `ED1BAE377ED3AF3D8443A1DDF0295EBEFFCB6C0DC28ABC2AA7F2532F510ADB4B`
- routing-after SHA-256:
  `9D01E51C94D08EEB640D0C1C7586C5701CE1C2567BC2968543E5FA7E842068F3`
- 全修正後binary SHA-256:
  `31BE0985808E4BFCDE388C54FEA2DB30527E571A2C7F42F257F0D4C94DAC56E8`

raw logとsummaryはignored artifactとして次に保存した。

- `artifacts/perf-baseline/reperf07-before`
- `artifacts/perf-baseline/reperf07-after`

### 結果

| Backend / metric | Before p50 / p95 / p99 | After p50 / p95 / p99 | 評価 |
|---|---:|---:|---|
| Vulkan frame | 16.602 / 17.493 / 18.360 ms | 16.652 / 17.498 / 18.294 ms | p50 +0.30%、p95 +0.03%、p99 -0.36%。60 fps frame timeは同等 |
| Vulkan compose pack | 79.400 / 109.700 / 145.150 us | 111.000 / 118.900 / 148.050 us | engine planeを直接読むcache影響でp50 +31.6 us |
| Vulkan route copy | 58.957 us/frame | 0 | 1,572,864 B/frameを全廃 |
| Vulkan route copy + pack | 138.357 us | 111.000 us | **-19.8%** |
| DX12 frame | 16.657 / 17.117 / 17.802 ms | 16.660 / 17.072 / 17.475 ms | p50 +0.02%、p95 -0.26%、p99 -1.84% |
| DX12 compose pack | 119.600 / 135.100 / 180.690 us | 113.200 / 118.625 / 140.975 us | p50 -5.4%、tailsも改善 |
| DX12 route copy | 90.365 us/frame | 0 | 1,572,864 B/frameを全廃 |
| DX12 route copy + pack | 209.965 us | 113.200 us | **-46.1%** |

| Counter / frame | Before | After |
|---|---:|---:|
| Vulkan route copy | 1,572,864 B | 0 B |
| Vulkan regular / fallback lines | 384 / 0 | 384 / 0 |
| Vulkan route runs | 0 | 2 |
| Vulkan structured pack | 2,757,120 B | 2,757,120 B |
| DX12 route copy | 1,572,864 B | 0 B |
| DX12 regular / fallback lines | 384 / 0 | 384 / 0 |
| DX12 route runs | 0 | 2 |
| DX12 structured pack | 2,757,120 B | 2,757,120 B |

Vulkan routing-after runでは`BuildPolygons`も119.1 usから167.3 usへ悪化しており、変更対象外を
含む全CPU stageが同方向へ動いた。CPU clock/coreを固定していないrun間変動なので、
`structured_2d`全体や個々のVulkan CPU stageから追加の速度向上は主張しない。
直接counterと対象区間合計は、削除したbytesと、cold engine planesを直接packする追加costの両方を
含む。DX12でもframe時間は同等で、targeted workだけが減った。

## 正確性検証

### Executable routing vectors

`melonprime_raster_edge_vectors`へV18/V19を追加した。

- constant screen routes
- screen swap false / true相当
- reverse engine assignment
- fallback screen storage
- mid-frame route change
- 2-run通常frame
- 5-run mixed regular/fallback frame
- 全4 planesのrun境界と先頭/末尾pixel

結果: `PASS: raster parity vectors V1-V19`

### 実ROM full-pixel differential

最終binary、USA Rev 1、native 1x、Software oracleとの3D全49,152 pixel比較。

| State | Vulkan | DX12 |
|---|---:|---:|
| F4 / `.ml4` | 147 frames、差分0 | 137 frames、差分0 |
| F5 / `.ml5` | 332 frames、差分0 | 309 frames、差分0 |
| F7 / `.ml7` | 336 frames、差分0 | 312 frames、差分0 |
| F8 / `.ml8` | 154 frames、差分0 | 137 frames、差分0 |

changed boundaryであるscreen-plane packはV18/V19が旧screen-major layoutと全pixelで一致する
synthetic dataを実行し、shaderとstaging layoutは変更していない。capture source Aのengine A
所有、fallback storage、両packerのrouting consumptionはcontract auditがratchetする。

### Native radar regression

`CustomHUD=false`、`BtmOverlayEnable=true`、同一F7で確認した。

- Vulkan修正前: 下画面レーダーのcolour-key領域が上画面へ表示される
- Vulkan修正後: 上画面から消失、native上下画面は正常
- DX12修正後: DX12 init成功を確認し、上画面から消失

画像証拠はignored artifactの`artifacts/radar-visibility-test`へ保存した。

### Build / audit

- official Windows existing build、`--jobs 1`: PASS
- structured composition contract audit: PASS
- DX12 shader compile audit: 111 variants / 3 scales PASS
- SRP/performance audit（native radar visibility ratchet含む）: PASS
- `git diff --check`: PASS

## 変更ファイルの責務

| Surface | 変更 |
|---|---|
| `MelonPrimeStructuredComposition.h` | route constants/view、共通run packer |
| `GPU_Soft.h/.cpp` | per-instance scanline route所有、fallback/telemetry publication |
| `GPU3D_Vulkan.*`, `GPU3D_DX12.*` | routed first-eight-plane pack |
| `GPU_Vulkan.cpp`, `GPU_DX12.cpp` | routing viewとtelemetryの受渡し |
| `VulkanPerf.h`, `DX12Perf.h` | REPERF-07 counters |
| `raster-edge-vectors.cpp` | V18/V19 executable routing vectors |
| structured contract audit | regular memcpy禁止、route/fallback/packer/capture ratchet |
| renderer perf summarizer | multi-run個別warmup除外 |
| Vulkan/DX12 frontend | native radarを`hudVisible`でgate |
| SRP/performance audit | native radar visibility gate ratchet |

## 残存判断

- REPERF-07 phase 1は完了。通常producer copyは0で、fallback/capture契約も維持した。
- `StructuredScreenPlanes`のstorage自体はfallback用に必要なので削除しない。
- GPU staging 2,757,120 B/frameは今回の対象外。dirty range/hashによるREPERF-03は、
  producer revision設計と専用capture parityが先であり、推測で実装しない。
- Vulkan empty submit、DX12 span transfer、Metal sparse workなどREPERF-08以降は、
  各backendの新しいprofileを着手gateとして維持する。
- macOS Metal runtime、MSL compile、Metal実ROM differentialはmacOS実機gateとして残る。
