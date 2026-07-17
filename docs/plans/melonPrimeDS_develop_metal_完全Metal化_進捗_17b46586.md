# melonPrimeDS 完全Metal化 進捗（基準 `17b46586`）

**基準指示書:** `docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md`  
**運用:** 1フェーズ（PR）ごとに実装／検証 → 本ファイル更新 → コミット／プッシュ。

| PR | 内容 | 状態 | Commit | 備考 |
|---|---|---|---|---|
| PR-0 | 最新修正 validation gate | **完了（部分）** | `69993462` | macOS Intel Metal ON／OFF／FORCE_DISABLE PASS。TSan／Windows／Linux／実ROM未実施 |
| PR-1 | output contract 最終仕上げ | **完了（部分）** | `cf962615` | PixelFormat metadata、FallbackReason、fault inject env。lease pool／CI未着手 |
| PR-2 | capture differential scaffold | **完了（部分）** | `f83aeea5` | EXPERIMENT flag、Soft対Metal candidate、artifact／CSV、homebrew設計。実ROM／homebrew ROM／capture-backed Bは未 |
| PR-3 | native canonical capture storage | **完了（部分）** | `f75ba9f4` | R16Uint native、scale非依存再生成、upload／readback直結。Enhanced cache／実ROM diff未 |
| PR-4 | per-scanline／segment capture | **完了（部分）** | `87c79f76` | segment loop A→B→Capture。ticket／ping-pong／実ROM同frame feedback未 |
| PR-5 | capture Full-GPU cutover | **完了（部分）** | （本コミット） | CaptureCnt exclusion 撤廃。実ROM／strict counter／diff 0未 |
| PR-6 | normal readback 0 | 未着手 | — | |
| PR-7 | SoftRenderer 継承撤廃 | 未着手 | — | M4/M5 後 |
| PR-8 | Compute RasterReference 撤廃 | 未着手 | — | |
| PR-9 | presenter MetalTexture-only | 未着手 | — | |
| PR-10 | radar native Metal | 未着手 | — | |
| PR-11 | HUD primitive renderer | 未着手 | — | |
| PR-12 | glyph atlas／OSD／splash | 未着手 | — | |
| PR-13 | macOS 初回 Metal 既定 | 未着手 | — | 完了A後 |
| PR-14 | MSL asset／metallib | 未着手 | — | |
| PR-15 | CI／release gate | 未着手 | — | |

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
- CaptureCnt exclusion は維持（PR-5）

未実施:

- generation ticket／ping-pong
- 実ROM same-frame feedback diff 0
- CaptureCnt exclusion 撤廃

## PR-5 要約（2026-07-17）

変更:

- CaptureCnt bit31 Full-GPU 除外を削除
- capture frame は segment scheduler 経由で Full-GPU 継続
- allowCaptureTextures=true（segment 内は 2D→encode 順）

未実施:

- 6000 frame strict counter
- 実ROM／homebrew／scale matrix

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
