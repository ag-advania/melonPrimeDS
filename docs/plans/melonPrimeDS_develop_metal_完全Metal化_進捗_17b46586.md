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
| PR-11 | HUD primitive renderer | **完了（transitional tier）** | （本コミット） | 固定長Metal draw-command list導入（HUD/OSD/splash/radar quadを1つのencode loopで発行）。QPainterはHUD/OSD/splashのCPUラスタライズには残存（真のprimitive/glyph atlasは未実装、指示書の「at least」tierとして明示的にdocumented） |
| PR-12 | glyph atlas／OSD／splash | **完了（部分: OSD／splashのみ）** | （本コミット） | splash logo／splashText／OSD toastを各々専用MetalTextureへキャッシュ化し、PR-11のHUD command list経由で個別quad描画。uiOverlayへのQPainter compositingは撤廃。custom HUD本体（ゲージ／クロスヘア等）のglyph atlas化は未着手（指示書の「splash+OSD at minimum」フォールバックを採用） |
| PR-13 | macOS 初回 Metal 既定 | **完了（部分）** | （本コミット） | 新規configのみ `3D.Renderer`/`Screen.UseGL` 既定をMetal probe結果でMetal Rasterへ切替。既存config・Compute既定・Soft/OpenGLビルドは不変。実ROM未実施 |
| PR-14 | MSL asset／metallib | **完了（部分）** | （本コミット） | 8箇所のembedded MSLをsrc/shaders/metal/へ移動、metallib build＋bundle化、release fallback禁止。GPU3D_Metal.mm/GPU2D_Metal.mm layer/compute主kernel/feature probe/capture experimentは対象外（下記） |
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

## PR-11 要約（2026-07-17） -- transitional tier（指示書の "at least" fallbackを明示採用）

**スコープ判断**: 指示書は「full MelonPrimeHudRender → Metal command listが
1セッションに大きすぎる場合」の優先順位として、"(2) at least route main
MelonPrimeHudRender output through Metal textured quads... document as
transitional" を明示的に許可している。`MelonPrimeHudRenderDraw.inc` の実装
（DrawHP/DrawWeaponAmmo/DrawWeaponInventory/DrawCrosshair、12種類のzoom
transition FXスタイル、outlineスタンプキャッシュ等、数千行）を調査した結果、
テキストグリフ・ゲージ・クロスヘアFXをQPainterなしの真のMetal primitiveへ
安全に書き換えるのは本セッションの範囲を明確に超えると判断した（実ROM検証
なしでゲームプレイに直結するクロスヘア位置/スケーリングを変更するリスクが
高すぎる）。よって本PRは明示的にtransitional tierを採用する。

変更:

- `MelonPrimeScreenMetal.mm`:
  - 新規 `MetalHudDrawCommand`（pipeline/texture/sampler/vertex uniforms/
    fragment uniforms）+ `EncodeMetalHudCommand()`。固定長
    `std::array<MetalHudDrawCommand, kMaxMetalHudCommands>`（per-frameヒープ
    割り当てなし）で、HUD/OSD/splash（uiOverlay textured quad）とPR-10の
    radar quadを、drawScreen()内の個別のインラインdraw call箇所ではなく、
    共通の「push → 1つのencode loop」経路に統一
  - `[encoder endEncoding]`直前の1箇所のencode loopが、それまでdrawScreen()
    の複数箇所に分散していた`setRenderPipelineState`/`drawPrimitives`呼び出し
    を置き換え。将来、solid-fill quad（ゲージ）やglyph-atlas quad（テキスト）
    等の新しいprimitive種別を追加する際、drawScreen()の制御フローを再度
    変更せずにこのcommand listへ追加できる土台となる
- 新規 audit `audit-metal-hud-command-list.py`（`run-metal-fullgpu-audits.sh`
  に登録): `MetalHudDrawCommand`/`EncodeMetalHudCommand`の存在、command list
  が`std::array`（`std::vector`不可）であること、draw箇所が
  `hudCommands[hudCommandCount++]`へpushしていること、encode loop呼び出しが
  ちょうど1箇所であること、単位quadの`drawPrimitives`呼び出しが
  `EncodeMetalHudCommand`内の1箇所のみであることを検証

**明示的に未達（次回以降の作業として残存）**:

- QPainterはHUD/OSD/splashの**CPUラスタライズ**（`uiOverlay`への描画）には
  引き続き使用される。本PRが変えたのは「そのラスタライズ結果をどうGPUへ
  出すか」（command list経由のMetal textured quad）のみで、「どうラスタ
  ライズするか」ではない。Metal frameでの「custom HUDにQPainterを一切
  使わない」という完全な要件は未達
  - HP/ammo/weapon inventory/crosshair FX/match status等の実描画コードは
    `MelonPrimeHudRenderDraw.inc`のQPainter呼び出しのまま
  - テキストはビットマップキャッシュ経由だが、キャッシュされたビットマップ
    自体もQPainterで生成される（グリフをMetal atlasへ焼くPR-12以降の作業）
- Edit modeのオーバーレイボックス（`DrawEditOverlay`）もQPainterのまま
  （指示書は "or only debug" として明示的に許容）

検証:

- `cmake --build build-mac-metal -j8` PASS
- `cmake --build build-mac -j8`（Metal OFF; no-op）
- `cmake --build build-mac-metal-force-disabled -j8`（no-op）
- `bash tools/ci/audits/run-metal-fullgpu-audits.sh` 9/9 PASS

未実施:

- 実ROM／実機での目視確認
- 真のMetal primitive HUD（QPainter完全撤廃）— 別PRとして継続予定
- TSan／Windows／Linux rebuild

## PR-12 要約（2026-07-17） -- 「splash+OSD at minimum」フォールバックを採用

**スコープ判断**: 指示書のフォールバック優先順位は「3. PR-12: splash+OSD at
minimum」。custom HUD本体（ゲージ／クロスヘア／HP/ammo等、
`MelonPrimeHudRenderDraw.inc`）のテキストをMetal glyph atlasへ焼き直す作業は
PR-11で明示した通り本セッション範囲外（実ROM検証なしでゲームプレイに直結する
描画を変更するリスクが高すぎる）。よって本PRはOSD toast／splashロゴ／
splashテキストのみをQPainter/uiOverlay compositingから撤廃し、真のPer-item
Metal textureへ移行する（フォールバックの「at minimum」を満たす）。

変更:

- `MelonPrimeScreenMetal.h`/`.mm`:
  - `ScreenPanel::osdRenderItem`/`osdDeleteItem`（`virtual`、`Screen.h`）を
    `ScreenPanelGL`と同じパターンでoverride: `ScreenPanel::osdRenderItem`
    （QPainterでの`item->bitmap`ラスタライズ）を呼んだ後、その`bitmap`を
    新規`id<MTLTexture>`へ`replaceRegion`でupload、`OSDItem::id`をキーにした
    `std::unordered_map<unsigned int, MetalOsdTexture> osdTextures`
    （`Impl`メンバー）へキャッシュ。両overrideは`ScreenPanel::osdUpdate()`/
    `osdAddMessage()`が`osdMutex`を保持している間にのみ呼ばれるため
    （GLの`osdTextures`と同じ契約）、追加のlockは不要。`osdDeleteItem`は
    該当エントリを`erase`するのみ（ARCが`id<MTLTexture>`を解放。GLの
    `glDeleteTextures`ループに相当する明示的破棄処理は不要）
  - `initMetal()`に splash logo（`splashLogo`）専用の一回限りの
    `logoTex`アップロードを追加。GLの`initOpenGL()`の
    "splash logo texture" ブロック（2倍supersample→`kLogoWidth`論理サイズで
    描画、`GL_NEAREST`相当）と同一のソース処理・フィルタ設定
  - `drawScreen()`: splash（ロゴ＋3行の`splashText`）とOSD toast
    （`osdItems`）の描画を、`overlayPainter.drawPixmap`/`drawImage`による
    `uiOverlay`への合成から、`pushMetalTexturedQuad()`ヘルパー
    （`m->uiPipeline`/`m->nearestSampler`を再利用し、PR-11の
    `hudCommands`固定長command listへpush）に置き換え。GL-nativeの描画順序
    （`MelonPrimeHudScreenCppOverlayOfGl.inc`のoverlay+radarパス→
    `ScreenPanelGL::drawScreen()`の2つの`osdShader`パス、Screen.cpp）に
    合わせ、UI overlay quad／radar quadのpush箇所より後にpushして最前面に
    描画
  - `kMaxMetalHudCommands`を4→32に拡張（uiOverlay＋radar＋splash logo＋
    splashText×3＋同時表示OSD toast分の余裕。上限超過分は既存の
    `hudCommandCount < hudCommands.size()`ガードにより黒フレームなどの
    未定義動作ではなく単純に描画skipされる）
- 新規 audit `audit-metal-osd-splash-native.py`（`run-metal-fullgpu-audits.sh`
  に登録）: `MetalOsdTexture`／`osdRenderItem`／`osdDeleteItem`
  override（`.h`/`.mm`双方）の存在、`overlayPainter.drawPixmap(`/
  `overlayPainter.drawImage(`がpresenterのlive codeに存在しないこと、
  splash logo／OSD/splashテキストが`pushMetalTexturedQuad(...)`経由でpush
  されていること、splash/OSD quadのpush箇所がUI overlay quad／radar quadの
  push箇所より後（描画順が正しい）であることを検証

**明示的に未達（次回以降の作業として残存）**:

- custom HUD本体（HP/ammo/weapon inventory/crosshair FX/match status等）は
  `MelonPrimeHudRenderDraw.inc`のQPainter呼び出しのまま、`uiOverlay`への
  ラスタライズも継続（Metal frameでの「custom HUDにQPainterを一切使わない」
  という完全な要件は未達 -- PR-11で明示した通り）
- グリフをフォントアトラスとして事前に焼き、CPU baseは font/language変更時
  のみ行うという「glyph atlas」要件は未実装。現状のtext bitmap cacheは
  `MelonPrimeHudRenderDraw.inc`側のQImageキャッシュのままで、Metal atlasへの
  統合は行っていない
- Edit modeのオーバーレイボックス（`DrawEditOverlay`）もQPainterのまま
  （指示書は "or only debug" として明示的に許容）
- 「Metal normal frameでuiOverlay CPU replaceRegion = 0」は custom HUDが
  非表示（メインメニュー／ROM未検出時など、splashのみ表示）の場合には
  達成済み（splash/OSDが`uiOverlay`を一切使わなくなったため）。custom HUDが
  表示されるゲームプレイ中フレームでは、custom HUD自体のQPainter
  ラスタライズが残っているため未達

検証:

- `cmake --build build-mac-metal -j8` PASS
- `cmake --build build-mac -j8`（Metal OFF; 対象ファイルはビルド対象外の
  ため no-op）
- `cmake --build build-mac-metal-force-disabled -j8`（同様に no-op）
- `bash tools/ci/audits/run-metal-fullgpu-audits.sh` 10/10 PASS

未実施:

- 実ROM／実機での目視確認（splashロゴ／テキスト／OSD toastの表示位置・
  サイズ・タイミングが実際に正しいこと）
- custom HUD本体のglyph atlas化（QPainter完全撤廃）— 別PRとして継続予定
- TSan／Windows／Linux rebuild

## PR-13 要約（2026-07-17）

変更:

- `Config.cpp`: `Table::GetInt("3D.Renderer")`／`Table::GetBool("Screen.UseGL")`に
  `#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)`ブロックを追加。
  既存の`tval.is_integer()`/`tval.is_boolean()`判定（＝既存保存済みconfigは
  この分岐を通らない）はそのまま維持し、"今回初めて値が未設定だった"
  （`wasUnset`）場合のみ`MelonPrime::Metal::SupportsRequiredBaseline()`を
  参照して既定を上書きする
  - probe PASS: `3D.Renderer = renderer3D_Metal`／`Screen.UseGL = false`
  - probe FAIL（unsupported device）: 既存のmelonPrimeDS既定
    （`renderer3D_OpenGL`／`Screen.UseGL`はそのまま）を維持
- `DefaultInts`の`"3D.Renderer"`静的既定値自体は`renderer3D_OpenGL`のまま
  （コメントでGetInt側のoverrideを明記）。Metal probe（`MTLCreateSystemDefaultDevice`
  ＋pipeline smoke test）を静的初期化時に走らせないための意図的な選択
  （probeの最初の呼び出しは`main.cpp`の`LogFeatureInfoOnce()`または
  `Config::GetInt`/`GetBool`の実際の呼び出し時点＝`main()`開始後）
- `renderer3D_MetalCompute`への既定変更は対象外（指示書どおり別判断）
- Soft/OpenGLビルド（`MELONPRIME_ENABLE_METAL`未定義）は`#if`ガードにより
  この分岐自体がコンパイルされず、挙動は完全に不変

未変更（意図的）:

- `MelonPrimeMetalFeatureCheck`の`SupportsRequiredBaseline()`ロジック自体
  （既存のunsupported device→false判定をそのまま利用）
- metallib／HUD／SoftRenderer

検証:

- `cmake --build build-mac-metal -j8` PASS
- `cmake --build build-mac -j8`（Metal OFF）PASS
- `cmake --build build-mac-metal-force-disabled -j8` PASS
- `bash tools/ci/audits/run-metal-fullgpu-audits.sh` 10/10 PASS

未実施:

- 実ROM／実機での新規config初回起動確認（Metal対応Mac／非対応Mac双方）
- 既存configファイルを変更しないことの実機・自動テストでの確認
  （コードレベルの`wasUnset`ガードのみで検証、実ファイルI/Oテスト未追加）

## PR-14 要約（2026-07-17）

**スコープ判断**: 指示書§20の対象（capture／Full-GPU 2D／visible
output／presenter HUD・radar／compute final-pass・textured・depth-blend）を
実装範囲とし、raster 3D本体（`GPU3D_Metal.mm`の3shader）・Metal 2D
`kMetal2DLayerShaderSource`・Compute主kernel（`kMetalComputeSource`）・feature
probe・capture experiment scaffoldは指示書どおり「残件として明示的にdocument」
する側に回した（下記「対象外」）。

変更:

- 新規 `src/shaders/metal/`（8ファイル）: 既存の`R"MSL(...)MSL"`／`NSString`
  concatenationから**内容無変更**で物理移動
  - `DisplayCapture.metal`（← `GPU_MetalCaptureMethods.inc`
    `kMetalDisplayCaptureShaderSource`）
  - `GPU2DFullGpu.metal`（← `GPU2D_MetalFullGpuShaders.inc`
    `kMetal2DFullGpuShaderSource`）
  - `FullGpuOutput.metal`（← `GPU_MetalFullGpuMethods.inc`
    `kMetalFullGpuOutputShaderSource`）
  - `VisibleOutput.metal`（← `GPU_Metal.mm`
    `kMetalVisibleOutputShaderSource`）
  - `ComputeFinalPass.metal`（← `GPU3D_MetalComputeFinalPassShaders.inc`
    `kMetalComputeFinalPassSource`）
  - `ComputeTextured.metal`（← `GPU3D_MetalComputeTexturedShaders.inc`
    `kMetalComputeTexturedSource`）
  - `ComputeDepthBlend.metal`（← `GPU3D_MetalComputeDepthBlendShaders.inc`
    `kMetalComputeCompleteDepthBlendSource`）
  - `Presenter.metal`（← `MelonPrimeScreenMetal.mm`の
    `kScreenShaderSource`/`kUiShaderSource`/`kRadarShaderSource`
    NSString literalを平文MSLへ復元。diffで内容一致を確認済み）
  - diff検証: 抽出7ファイルは元の埋め込みブロックとバイト単位で一致
    （`diff`で確認）。Presenterのみ元がNSString逐次concat文字列だったため
    手動復元だが、生成MSLはmp_screen_vs/fs・mp_ui_vs/fs・mp_radar_fsの
    ロジックを変更していない
- 新規 `src/MelonPrimeMetalLibrary.h`/`.mm`
  （`MELONPRIME_METAL_BUNDLED_METALLIB_V1`）: app bundle
  `Contents/Resources/melonPrimeDS.metallib`をdeviceごとにcacheしてロードする
  `MelonPrimeMetalDefaultLibrary(device)`。`newLibraryWithURL:`経由（ソース
  コンパイルは一切行わない）。`MelonPrimeMetalDefaultLibraryLoadAttempted()`/
  `LoadSucceeded()`をaudit／診断用に公開
- `src/frontend/qt_sdl/CMakeLists.txt`: `MELONPRIME_METAL_ACTIVE`内に
  `xcrun -sdk macosx metal`（各`.metal`→`.air`）＋
  `xcrun -sdk macosx metallib`（`.air`群→`melonPrimeDS.metallib`）の
  `add_custom_command`を追加し、`MACOSX_PACKAGE_LOCATION "Resources"`で
  `melonDS`ターゲットのbundleへ埋め込む。`MelonPrimeMetalLibrary.mm`を
  `core`ターゲット（ARC付き）へ追加
  - **toolchain可用性検出**: `xcrun -sdk macosx -f metal`/`-f metallib`で
    実際にツールが存在するかを検出（`find_program(xcrun)`だけでは
    Command Line Tools単体でも常に真になるため不十分）。
    - 検出成功 → metallib構築＋bundle化（本来の本番経路）
    - 検出失敗 かつ `MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON` → `WARNING`
      を出し`core`へ`MELONPRIME_METAL_ALLOW_SOURCE_FALLBACK=1`を定義。
      全移行済み呼び出し箇所はPR-14以前と同じ`-newLibraryWithSource:`実行時
      コンパイルへfallback（このセッションの実機はCommand Line Tools単体で
      Xcode本体のMetal Toolchainが存在しないため、この分岐を実際に
      通している）
    - 検出失敗 かつ developer features OFF（本当のrelease構成）→
      `FATAL_ERROR`。releaseはmetallibなしでconfigureを完了させない
- 8箇所の生成site（`GPU_MetalCaptureMethods.inc`／
  `GPU_MetalFullGpuMethods.inc`／`GPU_Metal.mm`／
  `GPU2D_MetalFullGpuMethods.inc`／`GPU3D_MetalCompute.mm`
  （textured/depth-blend/final-passの3ライブラリ）／
  `MelonPrimeScreenMetal.mm`）: `MelonPrimeMetalDefaultLibrary(device)`を
  最初に呼び、成功時はそのまま`newFunctionWithName:`。失敗時のみ
  `#if !defined(NDEBUG) || defined(MELONPRIME_METAL_ALLOW_SOURCE_FALLBACK)`
  で囲った既存の`-newLibraryWithSource:`へfallback（guardの外に出た
  release buildではfallbackコード自体がコンパイルされない）
- 新規 audit `audit-metal-shader-asset-metallib.py`（`run-metal-fullgpu-audits.sh`
  に登録): 8 `.metal`アセットの存在／entry point名一致、
  `MelonPrimeMetalLibrary.h/.mm`のmarker／`newLibraryWithURL`実装／
  source-compileしないこと、CMakeLists.txtのmetallib断片、8箇所の
  埋め込みshader定数が上記guard外で`initWithUTF8String`/
  `stringWithUTF8String`されていないことを検証

**対象外（release/production経路でも`-newLibraryWithSource:`が残る、
指示書どおり明示document）**:

- `GPU3D_Metal.mm`: `kMetal3DShaderSource`／`kMetal3DOpaqueShaderSource`／
  `kMetal3DFinalPassShaderSource`（Metal Raster 3Dの本体shader、3箇所）
- `GPU2D_Metal.mm`: `kMetal2DLayerShaderSource`（layer合成shader）
- `GPU3D_MetalCompute.mm`: `kMetalComputeSource`（compute主kernel、
  `CreateComputeFoundation()`内のコメントで明示）
- `MelonPrimeMetalFeatureCheck.mm`: probe shader 2種（feature-detection用の
  smoke testであり本番描画shaderではないため対象外）
- `GPU_MetalCaptureExperiment.inc`: `kMetalCaptureExperimentShaderSource`
  （`MELONPRIME_METAL_CAPTURE_EXPERIMENT`環境変数gatedのdebug scaffold）

検証:

- `cmake --build build-mac-metal -j4` PASS（本機はCommand Line Tools単体
  でXcode本体のMetal Toolchainがなく、`xcrun -sdk macosx -f metal`が失敗する
  ため、`MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`のCMake WARNING分岐を
  実際に経由してビルド。metallibはbundleへ含まれず
  `MELONPRIME_METAL_ALLOW_SOURCE_FALLBACK`で全箇所が旧来のsource
  compileへfallbackしていることをビルドログで確認）
- `cmake --build build-mac -j4`（Metal OFF）PASS
- `cmake --build build-mac-metal-force-disabled -j4` PASS
- release構成（`-DMELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF
  -DMELONPRIME_ENABLE_METAL=ON`）のconfigureが本機ではtoolchain不在のため
  意図どおり`FATAL_ERROR`で停止することを確認（`/tmp`の使い捨てbuild
  ディレクトリで検証、コミット対象外）
- `bash tools/ci/audits/run-metal-fullgpu-audits.sh` 11/11 PASS

未実施:

- **実機でのmetallib構築自体**: 本セッションの実機にはXcode本体（Metal
  Toolchainコンポーネント）が入っておらず、`xcrun metal`/`xcrun metallib`
  が存在しない。そのため`build-mac-metal`のapp bundleに実際に
  `melonPrimeDS.metallib`が入った状態のビルド・起動は未検証。フルXcode
  （またはXcode 16+の`xcodebuild -downloadComponent MetalToolchain`）が
  入ったmacOS機（想定: GitHub Actions macOSランナー）での
  `cmake --build build-mac-metal`実行と、生成された
  `melonPrimeDS.app/Contents/Resources/melonPrimeDS.metallib`の存在確認が
  次の検証必須項目
- 実ROM／実機での目視・スクリーンショット検証（bundled metallib経由の
  描画が旧来のsource compile経路と画面上ビット一致すること）
- ABI static_assert（sizeof/alignof/offsetof、指示書§20.4）は本PRでは
  追加していない（既存の`static_assert(sizeof(ScreenUniforms) == 40, ...)`
  等の個別assertは既存のまま維持のみ）
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
