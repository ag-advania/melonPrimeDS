# melonPrimeDS 完全Metal化 進捗（基準 `17b46586`）

**基準指示書:** `docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md`  
**運用:** 1フェーズ（PR）ごとに実装／検証 → 本ファイル更新 → コミット／プッシュ。

| PR | 内容 | 状態 | Commit | 備考 |
|---|---|---|---|---|
| PR-0 | 最新修正 validation gate | **完了（部分）** | `69993462` | macOS Intel Metal ON／OFF／FORCE_DISABLE PASS。TSan／Windows／Linux／実ROM未実施 |
| PR-1 | output contract 最終仕上げ | **完了（部分）** | （本コミット） | PixelFormat metadata、FallbackReason、fault inject env。lease pool／CI未着手 |
| PR-2 | capture differential scaffold | 未着手 | — | |
| PR-3 | native canonical capture storage | 未着手 | — | |
| PR-4 | per-scanline／segment capture | 未着手 | — | |
| PR-5 | capture Full-GPU cutover | 未着手 | — | |
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

## PR-1 要約（2026-07-17）

追加:

- `RendererPixelFormat` / `RendererOutputFallbackReason`（`GPU.h`、ObjC enum 非公開）
- MetalTexture metadata に `PixelFormat=Bgra8Unorm`
- presenter が metadata と `MTLPixelFormatBGRA8Unorm` の両方が一致することを検証
- `AcquireOutputLease` の CpuBgra fallback に `ResourceUnavailable` reason
- `MELONPRIME_METAL_FAULT_INJECT=<name>` で validation fail-closed を強制

未実施（意図的に分離）:

- lease context allocation pool（指示書 §7.5: contract と混ぜない）
- CI workflow
- fault injection の自動テストバイナリ

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
