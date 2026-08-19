# GPU2D 全 Renderer 表示破損 根本原因修正監査

日付: 2026-08-20
対象ブランチ: `develop_hud`
実装時の source HEAD: `859f52a45421ca6067ab9c4ffd6c4aa87c406547`
判定: PASS（下記の未実施範囲を除く）

## 依頼範囲と添付資料の扱い

`.codex/MelonPrimeDS_GPU2D_全Renderer表示破損_根本原因修正指示書_develop_hud_2026-08-20.md` は実装要件として扱った。ユーザー追加要件は、独立 Software baseline/current 比較を USA Rev 1 ROM の F1/F2/F3/F4/F5/F8 state load で実施すること、対象変更を commit/push することだった。

添付された `libgcc_s_seh-1.dll` ダイアログは検証環境の MinGW runtime 検出失敗であり、GPU2D の判定根拠ではない。実行時は `C:\msys64\mingw64\bin` を `PATH` に先頭追加して解消した。ROM/state ファイルおよび既存の `.codex` 指示書は変更していない。

## 根本原因と first-bad

`GPU2DNative::FrameRecorder` は Software の `SoftRenderer::DrawScanline()` 経路から呼ばれ、共有 `GPU.VRAMDirty[]` を `VRAMTrackingSet::CommitState` で消費していた。これにより recorder は観測者ではなく共有 dirty state の所有者となり、Software oracle 自体を変質させた。`PeekState + CommitState` への変更は消費タイミングを変えただけで、所有権問題を解決していなかった。従って、同じプロセス内の Software と Vulkan/DX12 の比較は循環証拠になっていた。

対象履歴の source audit は次の通り。`226b639883b0189397be5b0a154174d4ab28ebee` が FrameRecorder を初めて導入した first-bad（`b4aec...` と `0418...` には FrameRecorder なし）。`0e1d8084834b10deb564939683d7931a23a7e378` で `PeekState/CommitState` が明示化され、破壊的共有状態依存が確定した。

| commit | 役割 | FrameRecorder | CommitState |
|---|---|---:|---:|
| `b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4` | 独立 Software baseline | なし | なし |
| `0418a7db85cb82c1238ebcb2ff3184f8b2c84faa` | A/B 証跡基盤のみ | なし | なし |
| `226b639883b0189397be5b0a154174d4ab28ebee` | FrameRecorder 導入 | あり | なし |
| `fb141d2fa330163de68f060658e81a18a9ec0b02` | native Vulkan/DX12 | あり | なし |
| `33c6398352c65d8eb91be3de545d54b576ca8f85` | exact validation/capture | あり | なし |
| `0e1d8084834b10deb564939683d7931a23a7e378` | Peek/Commit 世代管理 | あり | あり |
| `c0805005ab6ed33c659b6c0fdbfd369a331a6fd8` | 旧 parity closure | あり | あり |
| `859f52a45421ca6067ab9c4ffd6c4aa87c406547` | 修正前 current | あり | あり |

## 実装した修正

- `GPU2DNative::FrameRecorder` を `const GPU&` の pure observer に変更し、`PeekState/CommitState` とその実装を削除した。
- VRAM dirty bit を消費せず、VRAM bank mapping/mask に従って物理 mapped VRAM、palette、OAM、FIFO を recorder 固有の `FrameInput` に snapshot するようにした。
- 通常の Software renderer は native recorder を記録しない。native backend が producer になる frame、または明示的 exact diagnostic の場合だけ private input を作る。
- Vulkan/DX12 の通常経路は readback/compare を行わず、exact diagnostic のみ全画面 pixel compare を行う。state load の generation 1 だけは既存 RasterDifferential と同じ transition discard とし、後続 frame は完全一致を要求する。許容誤差や golden の上書きは導入していない。
- `gpu2d-native-recorder-purity` を build に組み込み、shared dirty/mapping/VRAM/palette/OAM/FIFO と renderer state の非破壊性、multi-consumer 順序を検証可能にした。
- canonical Software frame dump と exact raw Top/Bottom pixel comparator を追加した。

## Build / provenance

独立 worktree/build を分離して使用した。

- baseline worktree: `C:\Users\Admin\Documents\git\melonPrimeDS\build\gpu2d-baseline-worktree-20260820` at `b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4`
- baseline executable SHA-256: `68dadf65f561e50e9d86e991859795422ca58bb7a022cb545ee1054d20e1f574`
- renderer-matrix candidate executable SHA-256: `19c8f230e4dfd6ff3354fca3e642a9cc28a31282d3d67e4b9e2db7c4213dc99b`
- final developer-test gating rebuild executable SHA-256: `2f45cdbece1456983f0d3c7e1f8b07f7036c479e638c83caa41b1c086bcb5fe2`
- candidate build-info: source `859f52a45421ca6067ab9c4ffd6c4aa87c406547`, Release, developer features ON, Vulkan/DX12 ON, provenance PASS。source patch が commit 前だったため `git_dirty=true` は意図した記録値。
- renderer matrix build は exact-transition 修正後に再リンク済み。続く developer-test source gating の再 configure/rebuild も全対象 target 成功（LTO の serial warning のみ）し、purity exit 0 を再確認した。gating は test source/include の developer-only 配置だけで renderer 実装を変更していない。

## USA Rev 1 ROM / state fixture

ROM: `C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`
ROM SHA-256: `bcd9c2d408825589c35c6754c0efb547cbae78fbda9ce7f69500a9cab8e70b8f`

| state | SHA-256 | Software dump SHA-256 (baseline = candidate) |
|---|---|---|
| F1 `.ml1` | `91d864550e2747c21f6f2c19e67b996b1fac9264b576be070b57d8adb7567579` | `420f0743672a4b23d6dc51d8afd98164bd2f0432d810fe5e0cf1743e3c2b6dc4` |
| F2 `.ml2` | `00c8fa8c5e77f0805be3920a1abe93053b1889764272b8bfffea26b8ff933fd9` | `647c1f64d422d41071aba3a9f3c5ad97477e32e91af228284c3e29ea1e87f50d` |
| F3 `.ml3` | `9f95127a7d0f4a481e2df1b2e4dd0ac3ddfaed5c0e75439bec0105bca1d3be3e` | `b430806fb045151ddc8c0803a2c3c15727480b2c34a83842b36d8b43931715e2` |
| F4 `.ml4` | `013eaa02ec3e16df5be3c2b2a1f3cb48cf6c1b956adb83632ccbc70dd000b853` | `b2278c1c21c79ecfddccfca2e4968f59ca274105fdb6faa2836316c39fd9a85a` |
| F5 `.ml5` | `039565c49be385bc474fed2bd052fd49aadadd3b61ab3093129463f936777c4a` | `b8111771860a19b4566b027261428f9be2e1605eedfce4339bf6895bcd916e23` |
| F8 `.ml8` | `aab2d9a75b728258d6b6c8cd355a5cde748f1f0dcf71b12e849a1147f49c3b6b` | `637fa92f66fc3bd6e6ec41a9a195e8bc4fb05311ce270eec9fa65167eb7c1728` |

## Validation matrix

### Independent Software oracle

Baseline and candidate were run in separate builds with the same ROM/state, Scale 1, VSync off, HUD off, and startup diagnostic state load. `[SavestateDiff] loaded=1` was present for all six states. Each side produced 240 frames; the comparator checked every raw canonical Top/Bottom logical pixel (`256 x 192 x 2`) with zero tolerance:

- F1/F2/F3/F4/F5/F8: `mismatches=0`, `frames_baseline=240`, `frames_candidate=240`.
- The six dump SHA pairs above are identical, which also guards against a comparator-only pass.
- A separate latest-candidate Software run with the real post-start F1/F2/F3/F4/F5/F8 actions recorded `savestate_startup_marker=1` and `savestate_action_marker=1` for every state, process exit 0, config restore PASS, and provenance PASS.

Baseline binary predates `--build-info-json`, so its provenance is explicitly `UNVERIFIED`; its source checkout and executable SHA are recorded above. Candidate build-info provenance is PASS. No current golden was overwritten.

### Other renderers

- OpenGLClassic and OpenGLCompute: latest candidate clean runs PASS; process exit 0, config restore PASS, provenance PASS, bad markers 0, native mismatch/fallback 0.
- Vulkan exact savestate runs F1/F2/F3/F4/F5/F8: process exit 0, config restore PASS, provenance PASS, exact failure 0, mismatch 0, fallback 0, fallback lines 0. Capture rows were 535/530/526/538/530/536 respectively (F1/F4/F5/F8/F2/F3 order).
- DX12 exact savestate runs F1/F2/F3/F4/F5/F8: process exit 0, config restore PASS, provenance PASS, exact failure 0, mismatch 0, fallback 0, fallback lines 0.
- Vulkan/DX12 normal clean runs: `native_gpu2d_readbacks=0`, `native_gpu2d_readback_B=0`, `native_gpu2d_mismatches=0`, `native_gpu2d_fallback_frames=0`.
- Renderer switch stress (Software↔OpenGL, Software→Vulkan, Software→DX12, Vulkan→OpenGL, DX12→OpenGL): all 5 cases completed `2/2` switches, restored configuration PASS, process exit 0, and no native mismatch/fallback markers.
- Final candidate `--gpu2d-recorder-purity`: exit code `0`.
- `python -m py_compile tools/testing/compare-gpu2d-frame-dumps.py`: PASS.

## Evidence boundary

This is Windows physical runtime evidence for the tested machine and the listed ROM/states. It does not claim macOS/Metal, Linux/BSD, other GPU vendors, or an independent GPU capture run. Model/static/build evidence is kept separate from physical runtime evidence. The implementation and audit do not add mismatch tolerance, use a contaminated Software oracle, or replace the supplied state fixtures.
