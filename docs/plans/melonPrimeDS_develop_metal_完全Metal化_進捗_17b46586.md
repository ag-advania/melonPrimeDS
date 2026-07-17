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
| PR-9 | presenter MetalTexture-only | **完了（部分）** | （本コミット） | AcquireOutputLease／presenter を MetalTexture／None のみに限定。screenTex CPU upload 撤廃。実ROM未実施 |
| PR-10 | radar native Metal | **完了（部分）** | （本コミット） | GL-native btmOverlay相当のcircle-mask fragment shaderでMetalTexture layer 1を直接sample。bottomImage memcpy撤廃。実ROM未実施 |
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

## PR-9 要約（2026-07-17）

変更:

- `GPU_Metal.mm`: `AcquireOutputLease()`／`GetOutput()` は元々 `MetalTexture` か空／None のみを返す実装済み（PR-7）。`MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1` マーカーを追加し、`RendererOutputKind::CpuBgra` を返さないという契約を明文化
- `MelonPrimeScreenMetal.mm`:
  - developer-only の CPU アップロード用 `screenTex`（BGRA8 2D-array texture、top/bottom `replaceRegion`）を完全削除。DS top/bottom 画面の描画元は `AcquireRendererOutputLease()` が返す `MetalTexture` のみ
  - `CpuBgra` 出力を受けた場合、Metal renderer 選択中は strict violation（`MetalStrictGpuOnlyViolation` + log-once）として扱い、`finalMetalTextureForFrame` には絶対に代入しない（displayしない、stale textureへの無期限fallbackもしない）。以前の「fresh CpuBgraは常にMetalTextureより優先して表示する」ロジックを削除
  - 180 frame の「CpuBgraはlegitimateな起動フォールバック」grace window を撤廃。残る grace window（60 frame、`kStartupGraceFrames`）は「まだ何の出力も無い」起動直後だけに適用される汎用の短い初期化猶予で、CpuBgra とは無関係
  - `hasCpuBaseFallbackForFrame`／`loggedSustainedCpuFallback`／`loggedNativeTextureFallback` を削除。perf submission の `softwareFallback` は常に `false`
  - uiOverlay（HUD／OSD／splash の QPainter 合成）はそのまま維持（PR-10～12 の対象。DS screenTex ではなく UI overlay のため PR-9 の対象外）
- `MelonPrimeScreenMetal.h`: ヘッダコメントを「CPU BGRA framebufferをアップロードする」という記述から MetalTexture-only の記述へ更新
- 新規 audit `audit-metal-presenter-metaltexture-only.py` を `run-metal-fullgpu-audits.sh` に追加（`GPU_Metal.mm`／presenter の live code に `CpuBgra` 系シンボルが再出現しないこと、`screenTex`／`hasCpuBaseFallbackForFrame` が残っていないこと、CpuBgra 分岐が `finalMetalTextureForFrame` へ代入しないこと、strict violation を報告することを検証）

検証:

- `cmake --build build-mac-metal -j8` PASS
- `cmake --build build-mac -j8`（Metal OFF; 対象ファイルはビルド対象外のため no-op）
- `cmake --build build-mac-metal-force-disabled -j8`（同様に no-op）
- `bash tools/ci/audits/run-metal-fullgpu-audits.sh` 7/7 PASS

acceptance（コード上）:

```text
CpuBgra accepted=0   (Metal選択中、AcquireOutputLease は CpuBgra を返さず、presenterもCpuBgra分岐でfinalMetalTextureForFrameへ代入しない)
screenTex upload=0   (screenTexメンバー自体を削除)
MetalTexture presented>0 (finalMetalTextureForFrameが有効な場合のみdraw)
None sustained=0     (grace window超過でMetalStrictGpuOnlyViolationを報告)
```

未実施:

- 実ROM／実機での目視・スクリーンショット検証
- TSan／Windows／Linux rebuild
- 「Software rendererをMetal presenterで表示するdebug mode」の再導入（別pathへ隔離する設計は別PR。PR-9では単純に削除のみ）

**完了A（emulator GPU pathの完全Metal化）ノート**: PR-5（capture Full-GPU）／PR-6（normal readback 0）／PR-7（SoftRenderer継承撤廃）／PR-8（Compute RasterReference撤廃）／PR-9（presenter MetalTexture-only）が揃ったことで、指示書 §28 の完了A チェックリスト（`Metal Raster: MetalTexture-only／Display Capture Full-GPU／normal readback 0／SoftRenderer dependency 0／CpuBgra 0`、`Metal Compute: Compute final texture-only／RasterReference 0／...`、`Presenter: lease付きMetalTexture-only／CPU screen upload 0`）はコードレベルでは全項目が実装済み。**エミュレータ GPU path は code-complete** と言える状態だが、各PRの備考にある通り実ROM／実機検証・TSan・Windows/Linux rebuild は未実施のため、完了A の受け入れ gate（§13.1）自体は依然オープン。

## PR-15 要約（2026-07-17）

追加:

- `tools/ci/audits/run-metal-fullgpu-audits.sh`
- macOS GitHub Actions で checkout 直後に metal audits 実行

未:

- Windows／Linux metal audit job
- ROM／TSan／release gate

## PR-10 要約（2026-07-17）

変更:

- `MelonPrimeScreenMetal.mm`:
  - 新規 `mp_radar_fs` MSLフラグメント関数（`kRadarShaderSource`）: GL-native
    `kBtmOverlayFS`（main_shaders.h）と同一のcircle-mask（`dist > 1.0` discard）
    + smoothstepフェード + 15色パレットフィルタを、`texture2d_array<float>`の
    layer 1（bottom screen）に対して直接sample。頂点stageは既存の`mp_ui_vs`
    （UI overlay quadと同じrect/screenSize/yFlipSign uniform layout）を再利用
  - 新規 `radarPipeline`（GLの`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`
    と同じstraight-alpha blend。UI overlay quadのpremultiplied blendとは別設定）
  - `Impl::bottomImage`（QImage, 256x192 CPU buffer）を完全削除。
    `hasHudCpuBuffersForFrame`/`topCpuBufForFrame`/`bottomCpuBufForFrame`と
    それらを埋めていた`nds->GPU.GetFramebuffers(...)`呼び出しも削除
    （PR-7以降 `MetalRenderer::GetFramebuffers()` は常に`false`を返すため、
    この経路は実質常にdead codeだった）
  - `CustomHud_Render(...)`呼び出しの`btmBuffer`引数を`&m->bottomImage`から
    `nullptr`に変更（GL-nativeパスの`MelonPrimeHud_RenderTopOverlay`と同じ
    契約。`DrawBottomScreenOverlay()`内のQPainter+CPUバッファのcrop-circle
    描画がSoftware専用パスになる）
  - radar config（enable/anchor/dstX/Y/size/opacity/srcRadius）を
    `MelonPrime::CustomHud_GetCacheEpoch()`でepoch-cache付きで直接読み込み。
    `MelonPrimeHud_RefreshRadarConfigIfNeeded`（Screen.cppのunity-build内部
    static関数）とロジックは同一だが、別TUのため直接呼べず複製 -- コメントで
    同期を明記
  - dst rect計算（`m_hudTopMatrix`/`m_hudOriginX,Y`/`m_hudScale`からwindow
    pixel座標を導出）は`MelonPrimeHudScreenCppOverlayOfGl.inc`のGL版と同一
  - radar quadはUI overlay quad描画の直後、`[encoder endEncoding]`の直前に
    描画（GL-nativeパスの描画順序: overlay HUD quad → radar circle、と一致）
  - `MelonPrimeScreenMetal.h`のヘッダコメントに`MELONPRIME_METAL_RADAR_NATIVE_V1`
    セクションを追加
- 新規 audit `audit-metal-radar-native.py`（`run-metal-fullgpu-audits.sh`に登録）:
  `bottomImage`/`hasHudCpuBuffersForFrame`/`GetFramebuffers(`がpresenterの
  live codeに存在しないこと、`mp_radar_fs`がtexture2d_arrayのlayer 1を
  sampleすること、circle-mask discardが存在すること、radar draw callが
  `finalMetalTextureForFrame`を直接bindすること、`CustomHud_Render`が
  `btmBuffer=nullptr`で呼ばれることを検証

検証:

- `cmake --build build-mac-metal -j8` PASS
- `cmake --build build-mac -j8`（Metal OFF; 対象ファイルはビルド対象外のため no-op）
- `cmake --build build-mac-metal-force-disabled -j8`（同様に no-op）
- `bash tools/ci/audits/run-metal-fullgpu-audits.sh` 8/8 PASS

未実施:

- 実ROM／実機での目視・スクリーンショット検証（radar circleの位置・サイズ・
  パレットフィルタが実際のゲームプレイで正しいこと）
- TSan／Windows／Linux rebuild

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
