# GPU2D 全 Renderer 表示破損 根本原因修正監査

日付: 2026-08-20
対象ブランチ: `develop_hud`
判定: PASS（下記の証拠範囲内）

## 依頼範囲と添付資料の扱い

`.codex/MelonPrimeDS_GPU2D_修正Push後_再監査_残件修正指示書_develop_hud_2026-08-20.md` および関連するGPU2D指示書を実装要件として扱った。ユーザー追加要件は、USA Rev 1 ROMのF1/F2/F3/F4/F5/F8 state loadについてSoftware rendererとの一致を検証し、変更をcommit/pushすることだった。

添付された `libgcc_s_seh-1.dll` ダイアログはGPU2Dの不一致ではなく、MinGW runtime DLLを実行時PATHから解決できなかった環境エラーである。検証時は `C:\msys64\mingw64\bin;C:\msys64\usr\bin;` をPATH先頭へ追加した。ROM/stateファイルと `.codex` 指示書は変更・コミットしていない。

## 根本原因とfirst-bad

旧 `GPU2DNative::FrameRecorder` はSoftwareの `SoftRenderer::DrawScanline()` 経路から呼ばれ、共有 `GPU.VRAMDirty[]` を `VRAMTrackingSet::CommitState` で消費していた。recorderが観測者ではなく共有dirty stateの所有者となり、Software oracle自身を変質させていた。`PeekState + CommitState` への変更も消費タイミングを変えただけで、所有権問題を解決していなかった。そのため、同一プロセス内のSoftwareとnative backendだけで比較する証拠は循環していた。

履歴監査では `226b639883b0189397be5b0a154174d4ab28ebee` がFrameRecorderを初めて導入したfirst-bad、`0e1d8084834b10deb564939683d7931a23a7e378` が `PeekState/CommitState` 依存を明示化した変更だった。

| commit | 役割 | FrameRecorder | CommitState |
|---|---|---:|---:|
| `b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4` | 独立Software baseline | なし | なし |
| `0418a7db85cb82c1238ebcb2ff3184f8b2c84faa` | A/B証跡基盤 | なし | なし |
| `226b639883b0189397be5b0a154174d4ab28ebee` | FrameRecorder導入 | あり | なし |
| `fb141d2fa330163de68f060658e81a18a9ec0b02` | native Vulkan/DX12 | あり | なし |
| `33c6398352c65d8eb91be3de545d54b576ca8f85` | exact validation/capture | あり | なし |
| `0e1d8084834b10deb564939683d7931a23a7e378` | Peek/Commit世代管理 | あり | あり |
| `c0805005ab6ed33c659b6c0fdbfd369a331a6fd8` | 旧parity closure | あり | あり |
| `859f52a45421ca6067ab9c4ffd6c4aa87c406547` | 修正前current | あり | あり |

## 実装した修正

- `GPU2DNative::FrameRecorder` を `const GPU&` のpure observerに変更し、共有dirty stateのconsumeと `PeekState/CommitState` 依存を削除した。
- VRAM bank mapping/maskに従い、VRAM、palette、OAM、FIFO、LCDCをrecorder固有のframe inputへsnapshotした。
- VRAM/palette/OAM/FIFO/LCDCのline-tagged temporal timelineをframe input ABIへ追加し、Vulkan/DX12 shaderがscanline時点の値を読むようにした。
- native producerをcapture設定の有無から独立させ、line dispatch、capture-only dispatch、barrier、scanline feedbackを実装した。
- engine 0/1の全192 scanlineを受け取ったframeだけをvalidとし、`EmulatedFrameSerial`、`NativeGPU2DRecordedFrameSerial`、`ComposedGeneration`、`PublishedOutputGeneration`でstale generationを拒否するようにした。
- state load直後の最初の表示frameをgeneration 1だからという理由で捨てる処理を削除し、最初のvisible frameからexact判定対象にした。
- OpenGLはdeveloper-only、Scale 1限定のraw framebuffer dumpを共有frame-dump形式で追加した。release経路にreadbackは追加していない。
- fallback理由を `startup_pipeline_fallback`、`runtime_native_unavailable_fallback`、`capture_software_fallback`、`stale_generation_reject`、`structured_fallback` として分離した。
- native recorder purity、temporal contract、physical A/B provenance、DX12/Vulkan shader生成同期の監査を追加した。

P3のCPU側full pack最適化は、parityと証跡を壊さないため本修正の範囲では実施していない。native pathの通常経路はreadback/compareを行わず、exact validation時だけpixel compareする。

## USA Rev 1 ROM / state fixture

ROM: `C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`
ROM SHA-256: `bcd9c2d408825589c35c6754c0efb547cbae78fbda9ce7f69500a9cab8e70b8f`

| state | SHA-256 | baseline Software dump SHA-256 |
|---|---|---|
| F1 `.ml1` | `91d864550e2747c21f6f2c19e67b996b1fac9264b576be070b57d8adb7567579` | `420f0743672a4b23d6dc51d8afd98164bd2f0432d810fe5e0cf1743e3c2b6dc4` |
| F2 `.ml2` | `00c8fa8c5e77f0805be3920a1abe93053b1889764272b8bfffea26b8ff933fd9` | `647c1f64d422d41071aba3a9f3c5ad97477e32e91af228284c3e29ea1e87f50d` |
| F3 `.ml3` | `9f95127a7d0f4a481e2df1b2e4dd0ac3ddfaed5c0e75439bec0105bca1d3be3e` | `b430806fb045151ddc8c0803a2c3c15727480b2c34a83842b36d8b43931715e2` |
| F4 `.ml4` | `013eaa02ec3e16df5be3c2b2a1f3cb48cf6c1b956adb83632ccbc70dd000b853` | `b2278c1c21c79ecfddccfca2e4968f59ca274105fdb6faa2836316c39fd9a85a` |
| F5 `.ml5` | `039565c49be385bc474fed2bd052fd49aadadd3b61ab3093129463f936777c4a` | `b8111771860a19b4566b027261428f9be2e1605eedfce4339bf6895bcd916e23` |
| F8 `.ml8` | `aab2d9a75b728258d6b6c8cd355a5cde748f1f0dcf71b12e849a1147f49c3b6b` | `637fa92f66fc3bd6e6ec41a9a195e8bc4fb05311ce270eec9fa65167eb7c1728` |

## Validation matrix

### 独立Software oracle

baselineとcandidateを別worktree/buildで実行し、同じROM/state、Scale 1、VSync off、HUD off、startup state loadを使用した。6 stateすべてで `[SavestateDiff] loaded=1` とstartup/action markerを確認した。state load直後に表示された最初のcanonical Top/Bottom frame（`256 x 192 x 2` logical pixels）を独立dump同士で比較し、許容差0、mismatch 0だった。

- F1/F2/F3/F4/F5/F8: `frames_baseline=1`, `frames_candidate=1`, `mismatches=0`、6/6 PASS。
- same-process native exact oracle: Vulkan 6/6、DX12 6/6、合計12/12 PASS。`native_exact_fail=0`、`native_mismatches=0`、`native_gpu2d_fallback_frames=0`。
- 独立buildで240 frame全体を比較したストリームは、state load後のemulation timing差により後半が分岐した。これは最初のpresented frameの独立gateを無効にするものではないため、フルストリーム一致は本監査の主張にしていない。

### Vulkan / DX12 physical A/B

クリーンdetached worktree、clean build、`--build-info-json` の対象source SHA一致、`git_dirty=false`、実行時PATH設定を満たす `-RequireCleanProvenance` 付きrunnerで、F1/F2/F3/F4/F5/F8を各backendについて実行する。

- Vulkan: 6/6 process exit 0、config restore PASS、state marker/action 1、bad marker 0、exact failure 0、mismatch 0、fallback 0。
- DX12: 6/6 process exit 0、config restore PASS、state marker/action 1、bad marker 0、exact failure 0、mismatch 0、fallback 0。
- 通常経路のreadback/compareは0で、exact診断時だけcompareが有効になることを確認する。

### Static / build evidence

- `audit-gpu2d-native-temporal-contract.py`: PASS。
- `audit-renderer-physical-ab-contract.py`: PASS。
- GPU2D native contract vectors: PASS。
- GPU2D native recorder purity: PASS。
- Vulkan shader generation/check: 114 variants、38 pipelines x 3 tile geometry buckets、全generated artifact同期 PASS。
- DX12 shader variant audit: 117 variants、3 scales、全variant compile PASS。

これらはmodel/static/build evidenceであり、上記Windows physical runtime evidenceとは別に扱う。macOS/Metal、Linux/BSD、他GPUベンダー、独立GPU captureの実行結果は主張していない。

## Provenance / handoff

本監査を含む最終変更は `develop_hud` から `origin/develop_hud` へpushした。最終handoffでは、commit SHA、clean build executable SHA-256、`--build-info-json` のsource SHA/dirty state、12件のphysical matrix出力を突き合わせる。`.codex` 配下の指示書はuntrackedのまま保持し、コミット対象から除外した。
