# melonPrimeDS 完全Metal化 進捗（基準 `17b46586`）

**基準指示書:** `docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md`  
**運用:** 1フェーズ（PR）ごとに実装／検証 → 本ファイル更新 → コミット／プッシュ。

| PR | 内容 | 状態 | Commit | 備考 |
|---|---|---|---|---|
| PR-0 | 最新修正 validation gate | **完了（部分）** | `69993462` | macOS Intel Metal ON／OFF／FORCE_DISABLE PASS。TSan／Windows／Linux／実ROM未実施 |
| PR-1 | output contract 最終仕上げ | **完了（部分）** | `cf962615` | PixelFormat metadata、FallbackReason、fault inject env。lease pool／CI未着手 |
| PR-2 | capture differential scaffold | **完了（部分）** | `f83aeea5` | EXPERIMENT flag、Soft対Metal candidate、artifact／CSV、homebrew設計。実ROM／homebrew ROM／capture-backed Bは未 |
| PR-3 | native canonical capture storage | **完了（部分）** | `f75ba9f4` | R16Uint native、scale非依存再生成、upload／readback直結。Enhanced cache／実ROM diff未 |
| PR-4 | per-scanline／segment capture | **完了（部分）** | （本コミット） | segment loop + ping-pong + CaptureWriteTicket。実ROM same-frame feedback はユーザー検証前提 |
| PR-5 | capture Full-GPU cutover | **完了（部分）** | `9b256370` | CaptureCnt exclusion 撤廃。実ROM／strict counter／diff 0未 |
| PR-6 | normal readback 0 | **完了（部分）** | `e3d6b47f` | reason／counter 導入。Soft GetLine／UploadCpu 削除は PR-7 待ち |
| PR-7 | SoftRenderer 継承撤廃 | **完了（部分）** | （本コミット） | `MetalRenderer : public Renderer, public MetalRendererHost` へ flip。GPU_Metal* の `SoftRenderer::` 呼び出しは 0。§13.1 の実機受け入れ gate は明示的指示により先行実施（ビルド／audit のみ検証、実ROM未実施） |
| PR-8 | Compute RasterReference 撤廃 | **完了（部分）** | （本コミット） | `MetalRenderer3D RasterReference` メンバーを削除。Init／RenderFrame／GetLine／texture getter は fail-closed（no raster fallback）。実ROM未実施 |
| PR-9 | presenter MetalTexture-only | 未着手 | — | PR-7 後 |
| PR-10 | radar native Metal | 未着手 | — | |
| PR-11 | HUD primitive renderer | 未着手 | — | |
| PR-12 | glyph atlas／OSD／splash | 未着手 | — | |
| PR-13 | macOS 初回 Metal 既定 | 未着手 | — | 完了A後 |
| PR-14 | MSL asset／metallib | 未着手 | — | |
| PR-15 | CI／release gate | **完了（部分）** | （本コミット） | macOS CI に metal audit runner。ROM／TSan gate未 |

## PR-2 要約（2026-07-17）

追加:

- `MELONPRIME_METAL_CAPTURE_EXPERIMENT=1`（表示は Soft のまま、Metal candidate は side-channel）
- `MELONPRIME_METAL_CAPTURE_EXPERIMENT_DIR` で rgb5551／PPM／CSV／JSON artifact
- `tools/ci/audits/audit-metal-capture-experiment-scaffold.py`
- homebrew test 設計書

意図的に未着手／スキップ:

- CaptureCnt exclusion 撤廃（PR-5）
- capture-backed source B の candidate 比較（PR-3 以降）
- PNG（PPM で代替）
- 実ROM／homebrew 自動 checksum CI

## PR-3 要約（2026-07-17）

追加／変更:

- canonical capture = native `R16Uint` RGB5551（~2 MiB with snapshots）
- scale 変更で capture texture 再生成不要
- CPU upload／readback は native 直結（16 MiB staging 撤廃）
- 2D／Compute3D は `.read()` + unpack

未実施:

- EnhancedCaptureCache LRU
- 実ROM／scale matrix pixel diff
- PR-4 segment scheduler

## PR-4 要約（2026-07-17）

追加:

- `RenderMetalFullGpuFrameSegmented`（A→B→Capture per segment）
- line-range encode／2D render
- `MELONPRIME_METAL_CAPTURE_PINGPONG_V1`: R16Uint Capture128/256[2] + PublishedIndex
- `CaptureWriteTicket`（FrameSerial／SegmentIndex／Layer／DirtySerial／Generation／Token）
- stale completion は新 Generation を finalize しない

未実施:

- 実ROM same-frame feedback 自動 diff CI

## PR-5 要約（2026-07-17）

変更:

- CaptureCnt bit31 Full-GPU 除外を削除
- capture frame は segment scheduler 経由で Full-GPU 継続
- allowCaptureTextures=true（segment 内は 2D→encode 順）

未実施:

- 6000 frame strict counter
- 実ROM／homebrew／scale matrix

## PR-6 要約（2026-07-17）

追加:

- `MetalReadbackReason` + normal／explicit byte counters
- SyncVRAMCapture / Soft GetLine を分類計上
- 600 frame ごとの readback ログ

未削除（PR-7）:

- Soft VBlank／GetLine／ComposeMetalVisibleOutput CPU upload

## PR-7 要約（2026-07-17）

変更:

- `GPU_MetalHost.h` 新規追加（`MetalRendererHost`: 仮想デストラクタのみの最小ホスト interface）
- `MetalRenderer : public SoftRenderer` → `class MetalRenderer : public Renderer, public MetalRendererHost`
- ctor: `SoftRenderer(nds)` → `Renderer(nds.GPU)`。`Reset()`／`Stop()`／`GetFramebuffers()`（常に `false`）を新規実装
- `MetalRenderer3D` / `MetalComputeRenderer3D` ctor: `SoftRenderer&` → `MetalRendererHost&`
- `SoftRenderer3D` の未使用 `SoftRenderer& Parent` を削除（ctor は `GPU3D&` のみ）。Soft 側 `Rend3D` 生成も追従
- `GPU_MetalFullGpuMethods.inc`: 非 FrameActive の `DrawScanline`／`DrawSprites`／`VBlankEnd` は Soft 呼び出しなしの no-op（§11.4: Soft への無言 mid-frame escape 禁止、presenter は RetainPrevious）
- `GPU_Metal.mm`: `VBlank`／`GetOutput`／`AcquireOutputLease` から `SoftRenderer::` 呼び出しを全撤廃。フォールバックは CpuBgra ではなく空／None output（presenter は既存の RetainPrevious 経路でそのまま last-known-good texture を保持）
- `ComposeMetalVisibleOutput` は Soft CPU composite 取得ではなく自前の `GetFramebuffers()`（常に false）を参照する恒常 no-op に変更（Full-GPU compose のみが実質パス）
- `GPU_MetalCaptureExperiment.inc`: `Output2D`/`Output3D` 参照を削除（source A は zero-fill。hook 呼び出し元が無くなったため実質常に unreachable）
- audit 更新: `audit-metal-capture-fullgpu-cutover.py`／`audit-metal-capture-experiment-scaffold.py`／`audit-metal-output-state-publication.py` を「継承なし」「`SoftRenderer::` 呼び出し 0」を要求する方向に反転

検証:

- `cmake --build build-mac-metal -j8` PASS
- `cmake --build build-mac -j8`（Metal OFF）PASS
- `cmake --build build-mac-metal-force-disabled -j8` PASS
- `bash tools/ci/audits/run-metal-fullgpu-audits.sh` 5/5 PASS

未実施（§13.1 の元の gate。今回は明示的なユーザー指示によりビルド／audit 検証のみで先行実施）:

- 実ROM／実機での目視・スクリーンショット検証
- `normalReadbackBytes == 0` 6000 frame strict counter
- capture Full-GPU strict counters green
- TSan／Windows／Linux rebuild

## PR-8 要約（2026-07-17）

変更:

- `GPU3D_MetalCompute.h`／`.mm`: `MetalRenderer3D RasterReference` メンバーを完全削除（GetLine を必要とする呼び出し元が本番経路に存在しないため、debug env gating ではなく削除を選択）
- `CreateComputeFoundation()`: device／command queue を `RasterReference` から借用せず、`MelonPrimeSharedMetalDeviceHandle()` と自前の `newCommandQueue` で直接取得
- `Init()`: foundation／self-test 失敗、または debug env（`MELONPRIME_METAL_COMPUTE_DISABLE_VISIBLE`／`MELONPRIME_METAL_COMPUTE_VISIBLE=0`）による無効化は `false` を返して fail-closed。`GPU::SetRenderer()` の既存の明示的・ログ付き Software renderer フォールバック（`GPU.cpp`）に委ねる。raster-only での「成功したことにする」経路を削除
- `RenderFrame()`: 非 eligible フレーム／identical-frame keepalive／final slot 未送出の全ケースで `RasterReference.RenderFrame()` 呼び出しを削除。フレームは visible output なしのまま return し、presenter 側の既存 RetainPrevious 経路がそのまま前フレームを保持する
- `GetLine()`: 常に `nullptr`（`GLRenderer3D`／OpenGL `ComputeRenderer3D` と同型）。PR-7 以降、本番経路で Metal 3D renderer の `GetLine()` を呼ぶ箇所は存在しない（`ComposeMetalVisibleOutput()` は `GetFramebuffers()` が常に false のため恒久 no-op）
- `GetColorTargetTexture()`: compute final texture のみ、フレームが visible でなければ `nullptr`（fail closed）
- `GetNativeResolveTexture()`: 常に `nullptr`（fail closed）。唯一の本番呼び出し元 `CaptureMetalVisible3DFrame()` は null pair を「このフレームは capture しない」として扱い、その消費者 `ComposeMetalVisibleOutput()` も PR-7 以降恒久 no-op
- `SetThreaded`／`IsThreaded`／`SetupRenderThread`／`EnableRenderThread`: compute には embedded CPU worker thread が存在しないため no-op／`false`（`RasterReference` 経由の Soft delegate thread 管理を削除）
- `SetBetterPolygons`: no-op（compute 自身の rasterizer kernel に BetterPolygons 実装はなく、これまでも `RasterReference` 経由の raster-only 実装への転送だった）
- `VideoSettingsDialog.cpp`: `rb3DMetalCompute` の `whatsThis`／`toolTip` から "raster renderer ... automatic fallback" 文言を削除し、fail-closed／RetainPrevious を明記
- 新規 audit `audit-metal-compute-raster-reference-removal.py` を `run-metal-fullgpu-audits.sh` に追加（`RasterReference` 症状／raster fallback tooltip 文言の再出現を検出）

検証:

- `cmake --build build-mac-metal -j8` PASS
- `cmake --build build-mac -j8`（Metal OFF）PASS
- `cmake --build build-mac-metal-force-disabled -j8` PASS
- `bash tools/ci/audits/run-metal-fullgpu-audits.sh` 6/6 PASS

未実施:

- 実ROM／実機での目視・スクリーンショット検証（compute foundation self-test 失敗時に Software renderer へ正しく fallback するかを含む）
- `MELONPRIME_METAL_COMPUTE_DISABLE_VISIBLE=1` での Init 失敗→Software fallback の実機確認
- TSan／Windows／Linux rebuild

## PR-15 要約（2026-07-17）

追加:

- `tools/ci/audits/run-metal-fullgpu-audits.sh`
- macOS GitHub Actions で checkout 直後に metal audits 実行

未:

- Windows／Linux metal audit job
- ROM／TSan／release gate

## PR-0 証拠要約（2026-07-17）

**Host:** macOS 15.7.7 / Intel Core i5-8259U (x86_64)

| Gate | Result |
|---|---|
| Static: `tools/ci/audits/audit-metal-output-state-publication.py` | PASS |
| `cmake --build build-mac-metal` (Metal ON) | PASS |
| `cmake --build build-mac` (Metal OFF) | PASS |
| `cmake --build build-mac-metal-force-disabled` | PASS |
| TSan 1000× scale stress | **未実施**（Qt+Metal TSan 専用 tree 未構成） |
| Windows / Linux rebuild | **未実施**（本機は macOS；`build-linux` cache は別マシン path） |
| Capture callback ordering harness | **未実施**（自動テスト未追加） |
| 実ROM scale stress | **未実施** |

詳細: `docs/plans/evidence/pr0-validation-17b46586_2026-07-17.md`
