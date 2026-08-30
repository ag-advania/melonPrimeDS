# GPU2D Vulkan/DX12 下画面欠落・起動停止 根本原因修正監査

日付: 2026-08-22
対象ブランチ: `develop_hud`
対象指示書: `.codex/MelonPrimeDS_GPU2D_Vulkan_ROM起動フリーズ_DX12表示崩壊_性能最適化Push後_根本原因修正指示書_develop_hud_2026-08-22.md`

## 依頼範囲と現象

USA Rev 1 ROMのF1/F2/F3/F4/F5/F6/F8 state loadを重点対象にし、Software rendererを独立した期待値としてVulkan/DX12のGPU2D表示と照合した。ユーザー報告の「下画面への全面黒挿入」、VulkanのROM起動停止、DX12の2D表示崩壊を同一のnative GPU2D capture/composition契約として監査した。

ROM/stateは次を使用した。

`C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`

同ディレクトリの `.ml1/.ml2/.ml3/.ml4/.ml5/.ml6/.ml8`。

## 根本原因

- native capture mappingのpacked frame header word 15は「いずれかのoverlayが存在する」というframe単位のsummaryだったが、shader側で各mapping entryのbit 4として扱っていた。未更新のheaderを読んだslotでは有効なmapping maskまで早期終了し、下画面全体が黒になる経路を作っていた。
- mapping rowはframe input内でVRAM/content mirrorとは別に変化するが、ring slotのupload planに独立した世代がなかった。同じ値に戻った行でも、途中のframeを取り逃したslotは古いmappingを保持できた。
- Vulkan/DX12のcapture Source Aがlogical Stage Aのimmutable structured planesではなく、capture dispatch内でsemantic compositionを再評価していた。Scale²の再評価と論理→captureの依存不足が、Vulkanの起動停止およびDX12の表示欠落を悪化させていた。

## 実装した修正

- Vulkan/DX12 shaderのoverlay判定をpacked header word 15のframe summaryに限定し、実際の判定はentryのbank maskで行うよう修正した。
- Vulkan/DX12のcapture Source Aをimmutable structured output（below/above/control/captureReference/lineMeta）読み取りへ変更し、capture dispatchからsemantic producerの再実行を除去した。
- `NativeCaptureMappingGeneration`を追加し、mapping row変更時とframe header変更時を同じdirty/upload世代として記録した。再利用されたring slotが取り逃した場合は、packed native mapping mirror全体を再送する。
- Vulkanではstructured output WRITE、native capture READ/WRITE、DX12ではlogical outputとcapture stateのUAV依存をcapture dispatch前に明示し、logical→capture orderingを固定した。
- CPU側のu64 fast path、active bank scan、incremental dirty range、capture history scan 0の設計は維持した。
- 生成済みVulkan SPIR-V/DX12 DXBCを公式compile scriptで更新し、source/blob/manifest同期を確認した。

## USA Rev 1 state-load validation

すべてScale 1、`savestate-load`、frame limit解除、VSync解除で実行した。native exactはdeveloper buildでSoftware oracleと比較した。

| backend | state | process/config/action | native exact | fallback / blank |
|---|---|---|---|---|
| Software | F1/F2/F3/F4/F5/F8 | 6/6 `0 / PASS / 1` | Software oracle | bad marker 0 |
| Vulkan | F1/F2/F3/F4/F5/F6/F8 | 7/7 `0 / PASS / 1` | fail 0, mismatch 0 | fallback 0, lines 0, blank 0 |
| DX12 | F1/F2/F3/F4/F5/F6/F8 | 7/7 `0 / PASS / 1` | fail 0, mismatch 0 | fallback 0, lines 0, blank 0 |

F6のクリーンアップ後再実行も、Vulkan/DX12とも `provenance=PASS`、`semantic-only rows=0`、`unexpected blanks=0` だった。ログにはそれぞれ `Vulkan renderer gpu2d=Vulkan gpu3d=Vulkan fallback=0`、`DX12 renderer gpu2d=DX12 gpu3d=DX12 fallback=0` が出ており、OpenGL経由ではない。

主な実行記録は `build/audit-runs/gpu2d-rootfix-20260822/` の `gpu2d-*-native-matrix-20260822` および `gpu2d-*-postcleanup-20260822` である。

## Build / static evidence

- `build-mingw.bat --jobs 1`: developer build完了、GPU2D native contract vectorsを含む全model target PASS。
- `build-mingw-release.bat --jobs 1`: shipping build `[44/44]` 完了。
- shipping flags: developer features OFF、renderer perf telemetry OFF、Vulkan latency capture OFF、Vulkan ON、DX12 ON。
- `audit-gpu2d-native-temporal-contract.py`: PASS。
- `audit-structured-composition-contract.py`: PASS。
- `audit-renderer-physical-ab-contract.py`: PASS。
- Vulkan shader source sync / `check-vulkan-shaders.py --scales 1,2,4,8,16`: PASS。
- DX12 shader source sync / `check-dx12-shaders.py --scales 1,5,9`: PASS。
- `git diff --check`: PASS。

## Shipping runtime

shipping binaryでもF6のVulkan/DX12 ROM起動はprocess exit 0、state action 1、freezeなしで完了した。`--build-info-json` はdeveloper telemetry/exactを含まないため、shippingではnative exact countersを要求しない。起動直後の `gpu2d=Software ... startupFallback=1 reason=pipeline compilation` は初回pipeline compile中の一時状態で、その後 `gpu2d=Vulkan` または `gpu2d=DX12`、`fallback=0` へ遷移した。

## Evidence boundary

実機相当の物理検証はこのWindows環境のNVIDIA GeForce RTX 5070 Tiで実施した。他GPU、他OS、Metal、OpenGL、および独立GPUキャプチャツールによるフレーム取得はこの監査のPASS範囲に含めない。`PrintWindow`のGPU surface画像は表示面の完全な証拠に使わず、Scale 1の内部Software oracle exact/readback、実際のbackend log、fallback/blank countersを判定根拠とした。

指示書自体は`.codex`配下のuntracked資料として保持し、コミット対象から除外する。
