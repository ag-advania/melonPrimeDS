# 完全Metal化 残作業ステータス

対象計画: [`melonPrimeDS_develop_完全Metal化_詳細修正指示書.md`](./melonPrimeDS_develop_完全Metal化_詳細修正指示書.md)
最終更新: 2026-07-17(develop_metal, `0ece862d` 時点)

## 完了済み (実機・実ROM検証済み)

| フェーズ | 状態 | 主なコミット |
|---|---|---|
| M0 診断/カウンター | 完了 | `33c93359` |
| M1 output contract (lease メタデータ) | 完了 | `33c93359` |
| M2 共有 MTLDevice | 完了 | `33c93359` |
| M3 Full-GPU 2D のデフォルト化 | 完了 | `33c93359` |

このセッションで修正したバグ(全て実ROM検証、ユーザープレイ確認済み):

1. **フリーズ/黒画面 2件** (`33c93359`): capture 適格性判定のタイミングバグ(`Start3DRendering` は VCount==215 で前フレームの VBlank 内なので `GPU.CaptureEnable` ではなく `CaptureCnt` bit31 を読む必要がある)+ 中間フレーム無効化の毎フレーム再試行による持続フリーズ(sticky フラグ + 60フレーム cooldown で1フレーム上限に抑制)。
2. **capture アップロードのホットパス** (`c893f091`): フレーム毎ヒープ確保・同期 GPU 待ち・無制限ログを除去。最悪フレーム時間 2673ms → 113ms(実測、`tools/perf/`)。
3. **published スロット再利用競合** (`a7ade315`): compose が presenter の唯一の表示可能フレームを上書きし、lease 失敗 → 1フレームの CpuBgra フラッシュ。published スロットを再利用候補から除外。
4. **presenter オーバーレイの dirty-rect 化** (`88686875`): GL 側 OPT-DR1 契約の移植。presenter 時間 0.63–1.2ms → 0.16–0.26ms。
5. **Metal Compute の蘇生** (`0ece862d`): 過去の Phase 8V/8W カウンター整理が MSL に `if (secondAccepted)` の宙ぶらりんを残し、depth-blend シェーダーがコンパイル不能 → **Compute 基盤全体が起動時に silent fail し、Renderer=4 は常に raster で動いていた**。修正後、全 self-test PASS、CUTOVER 発動、p50 12.0ms(raster 16.5ms 比で約27%高速、Intel Iris 655)。あわせて未配線だった `Metal3DSetCaptureTextures` を `ConfigureMetalCaptureState` 成功時に配線。

## 重要な再解釈(実測で確認済み)

**M4 の残作業は正しさの問題ではなく GPU 利用率の問題。**
capture がアクティブなフレームは適格性判定で事前に除外され、実証済みの CPU/Software 経路で正しく描画される(3600フレーム計測で same-frame capture ハザード分岐への到達 0 件)。誤ったピクセルが出るリスクは現状ない。残っているのは「capture フレームも GPU 経路に乗せる」高速化のみ。

## 残作業(フェーズ別)

### M4: per-scanline capture/2D 混載(未実装)
- 実 DS はスキャンライン単位で capture するが、Metal 2D はフレーム一括バッチ + VBlank 後の一括 capture dispatch のため、同一フレームで自分の capture 先を読むゲームの line-precise 因果を再現できない。
- 推奨アプローチ: opt-in の実験パスとして実装し、既知正解の CPU 出力との checksum diff(`MetalGetLineDiffEnabled()` と同じパターン)で**ログのみで**検証してから昇格する。
- 対象: `GPU_MetalFullGpuMethods.inc` の適格性判定(`CaptureCnt` bit31 除外の緩和)、`GPU_MetalCaptureMethods.inc` の per-line dispatch 化、`GPU2D_MetalFullGpuMethods.inc` のセグメント/capture 順序。

### M5: 通常フレーム readback 0 化(3分の2完了)
- 済: softwareDelegate=0/600(デフォルトで恒常的に成立、`Delegate` の全呼出箇所は環境変数ゲート内)/ GetLine 経路もデフォルト不使用。
- 残: capture フレームの CPU フォールバック時の `ReadbackNativeColorTargetToLineBuffer()`。これは現状**必要かつ正しい**(CPU 2D compositor がデータを要求するため)。M4 完了で自然に消える。

### M6: SoftRenderer 継承の撤廃(依存マップ作成済み、実行は M4/M5 待ち)
- `MetalRenderer` 内の `SoftRenderer::` 呼出 7 箇所を列挙済み: DrawScanline / DrawSprites / VBlankEnd / GetFramebuffers / VBlank(5箇所、すべて `!FullGpuState->FrameActive` ゲート)+ AcquireOutputLease / GetOutput のフォールバック(2箇所、起動時のみ)。
- 前者5箇所は capture フレームで唯一の正しい出力手段なので、M4 完了前に外すと正しさが壊れる。

### M7: Metal Compute 独立化(実質達成、形式未了)
- 済: compute final texture が可視ソース(CUTOVER 発動、rasterReference=stopped)、UI 名称と実経路の一致、実プレイ確認。
- 残: capture フレームでの RasterReference 描画(M4 依存)、`GetLine()` の RasterReference 委譲の廃止(同上)、`VideoSettingsDialog` のツールチップ「raster fallback」文言の更新(計画は全受け入れ試験後と指定)。

### M8: presenter の Metal texture-only 化(実質達成)
- 済: 定常状態で CpuBgra を受けない(strict モードで監視、6000フレーム 0 件)。デバイス共有済み。lease 解放は completion handler。
- 残: 起動 grace(180フレーム)中の CpuBgra 受理は**意図的に維持**(黒画面より正しい CPU 画像の方が良い UX)。厳密化するなら `None` + 黒クリア + 明示エラーに変更。

### M9: HUD/OSD の Metal 化(実利は取得済み、フル実装は未着手)
- 済: dirty-rect 化により毎フレーム全画面 QImage クリア/アップロードは撤廃(計画の禁止事項 7 の実質解消)。
- 残: 描画 command list + glyph atlas の Metal UI renderer(計画の完了B)。次の有界ステップ: **レーダーの Metal ネイティブ化** — GL 側 `MelonPrimeHudScreenCppOverlayOfGl.inc` の btmOverlay 実装(circle-mask シェーダーで final texture の layer 1 をサンプル)を presenter に移植すると、HUD 表示中の毎フレーム 196KB bottomImage memcpy と CPU レーダー合成が消える。動作中の CPU 合成の置き換えなので要プレイ検証。

### M10: macOS 初回デフォルトの Metal 化(未着手、ポリシー判断待ち)
- コード変更自体は小さい: `Config.cpp` の初回既定 `3D.Renderer=renderer3D_Metal`、`Screen.UseGL=false`(macOS Metal ビルドのみ、既存ユーザー設定は不変更)。
- 計画は「完了A 受け入れ後」と指定。今日の安定化でかなり近いが、§8 の受け入れ試験(スケールマトリクス、savestate、マルチプレイ等)は未実施。

### M11: shader 資産の分離(未着手)
- 画質 parity 完了後と計画に明記。埋め込み MSL の `.metal` ファイル分離 + 事前コンパイル。

## 検証レシピ(このセッションで使った手順)

```zsh
# Metal 有効テストビルド(canonical build-mac とは別ツリー)
./tools/build/macos/build-macos-metal-test.sh

# 診断付き起動(いずれもデフォルト無効)
MELONPRIME_METAL_PERF=1 MELONPRIME_METAL_ASSERT_GPU_ONLY=1 \
  build-mac-metal/melonPrimeDS.app/Contents/MacOS/melonPrimeDS <rom>
# ASSERT_GPU_ONLY=1 はログのみ。abort させるには =abort。

# 見るべきログ:
#  - "visible-source mix ... retainPrevious=N/600"  ← N>0 が続けばフリーズ
#  - "STRICT GPU-ONLY VIOLATION"                    ← 0 であるべき
#  - "CUTOVER active ... rasterReference=stopped"   ← Compute が本物で動いている証拠
#  - MELONPRIME_PERF=1 + 正常終了で shutdown summary(p50/p95/p99/max)

# 性能比較(before/after は git stash でビルドを分けて計測)
tools/perf/summarize-melonprime-perf.py <log>
tools/perf/compare-perf-repro.py <before> <after>
```

## 既知の残存事項(軽微)

- シーン遷移時の中間フレーム無効化は cooldown 設計により最大1フレームの RetainPrevious として現れる(1ウィンドウ 0〜6 回程度)。設計内の挙動。
- Metal OFF / FORCE_DISABLE ビルドの再検証はこのセッションの `GPU.h` 変更後に未実施(変更は全て `MELONPRIME_ENABLE_METAL` ゲート内なのでリスクは低いが、§9.2 のマトリクス確認は未了)。
