# GPU2D 全 Renderer 表示破損 根本原因修正監査

日付: 2026-08-20
対象ブランチ: `develop_hud`
判定: SOURCE / MODEL / BUILD / Software・Vulkan・DX12 state-load exact / scale=4 smoke は PASS。14FPSの物理性能測定は未実施。

## 依頼範囲と添付資料の扱い

`.codex/MelonPrimeDS_GPU2D_第3次再監査_実装残件修正指示書_develop_hud_2026-08-20.md` および関連するGPU2D指示書を実装要件として扱った。ユーザー追加要件は、USA Rev 1 ROMのF1/F2/F3/F4/F5/F8 state loadについてSoftware rendererとの一致を検証し、変更をcommit/pushすることだった。

添付された `libgcc_s_seh-1.dll` ダイアログはGPU2Dの不一致ではなく、MinGW runtime DLLを実行時PATHから解決できなかった環境エラーである。検証時は `C:\msys64\mingw64\bin;C:\msys64\usr\bin;` をPATH先頭へ追加した。ROM/stateファイルと `.codex` 指示書は変更・コミットしていない。

添付された `Unknown option 'build-info-json'` ダイアログは、修正前の古い実行ファイルを起動したときのものだった。現行Debug実行ファイルでは `--build-info-json` が受理されることを確認した。なお現行Debugバイナリの埋め込みSHAは `unknown` のため、今回のSoftware A/B結果は実測済みだが、clean provenance付きのphysical証跡とは区別する。

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
- 高解像度native line passを論理line単位からScaleFactor倍のsub-row単位へ修正し、Vulkan/DX12のdispatchもScale 1/4/16を同じ計算式で扱うようにした。
- Native Display CaptureはGPU側だけの状態に留めず、CaptureEnable時にSoftwareのcapture semanticsを明示的にcore-visible VRAMへmaterializeするmirror経路を追加した。通常経路でGPU readbackを毎frame行わず、Vulkan/DX12 frontendにもcapture同期のoverrideを明示した。
- OBJ/OAMはDrawSpritesのone-line-ahead順序をprivate sprite timelineへ保存し、VCount 262からline 0へのhandoffも保持した。palette/extended paletteはcurrent-line timelineのままとした。
- LCDC VRAM mappingをframe headerではなく各LineStateへ保存し、mid-frame VRAMCNT remapをline単位でnative shaderへ渡すようにした。
- TimelinePayloadをwrite event数ではなく512-byte内容のhash-consed block versionで保持し、高頻度bitmap/DMA stressで同一内容の反復がoverflowを起こさないことを確認した。`TimelineOverflow` が立ったframeは引き続きvalidにしない fail-closed設計である。
- FrameInputの大きなaggregateを `Input = {}` でstack temporary化しないよう、trivially-copyable ABIを保ったmemset resetへ変更した。
- Structured compositionでOBJの `0xD0` flag（OBJ `0x80` と3D `0x40` の併記）を3D層と誤認しないよう、Vulkan/DX12の判定を「`0x40` かつ `0x80` なし」に統一した。これがOBJ選択後にBackdropへ戻る表示破損の直接原因だった。
- DX12 native GPU2DがStage Aとstructured compositorで同一frameに2つの14-UAV tableをbindできるよう、slot descriptor ringを16枠から `kUavTableSize * 2` へ拡張した。
- Savestate指定時のstartup transition frameをexact比較へ混ぜず、UI/diagnosticのstate-load通知後にexact検証をarmするようにした。state load後は引き続き全logical pixelを比較する。
- DX12のBG affine/extended address式をVulkanと同じ明示的なmap/tile base加算へ統一し、オフラインDXBC blobをHLSL変更と同時に再生成した。F1のBG3 direct-color先頭画素でVRAM16値 `0x8623` とSoftware期待値の一致を確認した。

P3のCPU側full pack最適化は、parityと証跡を壊さないため本修正の範囲では実施していない。native pathの通常経路はreadback/compareを行わず、exact validation時だけpixel compareする。

## USA Rev 1 ROM / state fixture

ROM: `C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`
ROM SHA-256: `bcd9c2d408825589c35c6754c0efb547cbae78fbda9ce7f69500a9cab8e70b8f`

| state | SHA-256 | historical baseline Software dump SHA-256 |
|---|---|---|
| F1 `.ml1` | `91d864550e2747c21f6f2c19e67b996b1fac9264b576be070b57d8adb7567579` | `420f0743672a4b23d6dc51d8afd98164bd2f0432d810fe5e0cf1743e3c2b6dc4` |
| F2 `.ml2` | `00c8fa8c5e77f0805be3920a1abe93053b1889764272b8bfffea26b8ff933fd9` | `647c1f64d422d41071aba3a9f3c5ad97477e32e91af228284c3e29ea1e87f50d` |
| F3 `.ml3` | `9f95127a7d0f4a481e2df1b2e4dd0ac3ddfaed5c0e75439bec0105bca1d3be3e` | `b430806fb045151ddc8c0803a2c3c15727480b2c34a83842b36d8b43931715e2` |
| F4 `.ml4` | `013eaa02ec3e16df5be3c2b2a1f3cb48cf6c1b956adb83632ccbc70dd000b853` | `b2278c1c21c79ecfddccfca2e4968f59ca274105fdb6faa2836316c39fd9a85a` |
| F5 `.ml5` | `039565c49be385bc474fed2bd052fd49aadadd3b61ab3093129463f936777c4a` | `b8111771860a19b4566b027261428f9be2e1605eedfce4339bf6895bcd916e23` |
| F8 `.ml8` | `aab2d9a75b728258d6b6c8cd355a5cde748f1f0dcf71b12e849a1147f49c3b6b` | `637fa92f66fc3bd6e6ec41a9a195e8bc4fb05311ce270eec9fa65167eb7c1728` |

この表のdump hashは過去の監査fixture metadataであり、今回の現行candidate比較結果はValidation matrixの表に分離して記載する。

## Validation matrix

### 現行Software state-load A/B（実測済み）

対象ROMは `C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`、stateは同じディレクトリの `.ml1/.ml2/.ml3/.ml4/.ml5/.ml8`。最終Release binary、Scale 1、savestate-load actionで現行Software実行を行い、runnerのprocess/config/state action/bad markerを各ケースで確認した。

state load直後の最初のcanonical Top/Bottom frame（`256 x 192 x 2` logical pixels）を、既存の独立Software baseline dumpと比較した。許容差0で次の6件すべてが `frames_baseline=1`, `frames_candidate=1`, `mismatches=0` だった。

| state | 現行candidate dump | candidate SHA-256 | 結果 |
|---|---|---|---|
| F1 `.ml1` | `third-software-f1b.mp2ddump` | `57591baf313bcbd5c64b51a9770fa6aa38300af3dbd310fff617ca0409157e1c` | PASS |
| F2 `.ml2` | `third-software-f2.mp2ddump` | `a880c456de9a141dc4d799372f6870b6b34a304df903e79883bbd79a3e8b7088` | PASS |
| F3 `.ml3` | `third-software-f3.mp2ddump` | `a05a2d3ff9a6ed2dcedfc4bffe2ad33071bb54f00cff3d8f95013336c0753502` | PASS |
| F4 `.ml4` | `third-software-f4.mp2ddump` | `3f20c78282a200948cbe1f6856c1ceac3668a38c5a7eb84811939f5a16d6fde8` | PASS |
| F5 `.ml5` | `third-software-f5.mp2ddump` | `44fa97822d29ae2ff91d3051f1c5623f79e7b5b0bf85627e06cd9908a3f7962d` | PASS |
| F8 `.ml8` | `third-software-f8.mp2ddump` | `1dcbeee098a415509f9de28e36896c1e7d97b28074f83f7d97c5a32bab27ebb5` | PASS |

この現行Software結果は、working treeがdirtyでDebug build infoの埋め込みSHAが `unknown` のため、clean provenance付きphysical証跡とは呼ばない。既存baselineとのcanonical pixel一致という範囲の実測証拠である。

今回の最終candidateで再取得した6件は、F1/F2/F3/F4/F5/F8のすべてで `process exit=0`、`config restore=PASS`、`state action=1`、`bad markers=0` だった。Software側にはnative exact failure/fallbackはなく、各ケースの実行ログとmetadataを `build/gpu2d-validation-20260820-final-software-*-rootfix/` に保存した。

### Static / model / build evidence（実測済み）

- `py -3 tools\ci\audits\audit-gpu2d-native-temporal-contract.py`: PASS。
- GPU2D native contract vectors: PASS。
- GPU2D native recorder purity: PASS。Scale 1/4/16のsub-rowモデル、private OBJ/OAM latch、per-line LCDC mapping、high-churn bitmap/DMAを含む。
- Vulkan shader source/generated sync: PASS。114 variants、38 pipelines x 3 tile geometry buckets、manifest hash一致。
- DX12 shader source/generated sync: PASS。117 generated modules、3 scales、source sync PASS。
- Release `melonDS` build（Vulkan/DX12 enabled）: PASS。Debug developer buildとrecorder purity targetもPASS。
- `python tools/ci/audits/check-vulkan-shaders.py`: PASS。114 variants、SPIR-V validation 114、scale-specialized 608。
- `python tools/ci/audits/check-dx12-shaders.py`: PASS。HLSLから117 DXBC modulesを再生成し、source/blob同期とFXC検証を完了。
- `python tools/ci/audits/audit-structured-composition-contract.py`: PASS。
- `python tools/ci/audits/audit-gpu2d-native-temporal-contract.py`: PASS。
- `git diff --check`: PASS。`MelonPrimeLifecycle.cpp` の改行をLFへ正規化した。
- `--build-info-json`: 現行Release binaryで受理されschema出力を確認。ただし埋め込みgit SHA/branchは `unknown` で、runtime provenanceはUNVERIFIED。

これらはsource/static/model/build evidenceであり、実GPU上のVulkan/DX12表示一致を意味しない。

### Current native physical matrix

検証時のsource HEADは `0ed223a026af42526a613677c309d8d046584d5b`。ROM/stateは上記fixture、Release binary、Scale 1、`savestate-load` actionで同一条件に固定した。embedded build-infoのgit fieldsは `unknown` なので provenanceはUNVERIFIEDだが、実行したnative routeとexact countersはログで確認できる。

| renderer | F1/F2/F3/F4/F5/F8 | process/config/state | native exact | fallback | runner gate |
|---|---|---|---|---|---|
| Software | 6/6 | `0 / PASS / 1`、bad markers 0 | 対象外 | 0 | PASS |
| Vulkan | 6/6 | `0 / PASS / 1`、bad markers 0 | fail 0、mismatch 0 | frames 0、lines 0 | capture rows=0のためrunner exit 1。ただしexact gateはPASS |
| DX12 | 6/6 | `0 / PASS / 1`、bad markers 0 | fail 0、mismatch 0 | frames 0、lines 0 | PASS |

Vulkan/DX12の全12ケースで、state action markerは1、native GPU2D exact failure/mismatch/fallback/fallback-linesはすべて0だった。DX12はF1で発生していたdescriptor table不足とBG3 extended direct-colorの差分を修正後、native route `gpu2d=DX12 gpu3d=DX12 fallback=0` で完了した。Vulkanも `gpu2d=Vulkan gpu3d=Vulkan fallback=0` を確認した。各runの詳細は `build/gpu2d-validation-20260820-final-{software,vulkan,dx12}-*-rootfix/` に保存した。

### Scale=4 smoke

同じUSA Rev1 F1 stateをScale 4で各renderer一度ずつ起動し、process exit 0、config restore PASS、state action 1、bad markers 0を確認した。Vulkan/DX12は起動直後のpipeline compilation中だけSoftware startup fallbackを記録し、その後それぞれ `gpu2d=Vulkan` / `gpu2d=DX12`、`fallback=0` へ復帰した。これは4x起動・表示経路のsmoke PASSであり、14FPSの持続性能を意味しない。

### Remaining evidence gates

- 14FPS targetの物理performance benchmark（固定scene、warmup、測定窓、平均/最低FPS、frame-time distribution）: **NOT RUN / NOT CLAIMED**。
- OpenGL Classic/Compute runtime matrix: **NOT RUN for this implementation SHA**。
- Scale 1/4/16 native physical high-resolution captureの独立capture証跡: **NOT RUN**。scale=4の起動smokeとsource dispatch/write-bound/model coverageはPASS。
- Native GPU capture read/write/remap/savestate/switchの独立hardware capture: **NOT RUN**。
- macOS/Metal、Linux/BSD、他GPU vendor: **NOT RUN / NOT CLAIMED**。

旧コミットで取得したVulkan/DX12 physical logは履歴資料として扱い、今回の変更のPASS根拠には再利用していない。

## Provenance / handoff

`.codex` 配下の指示書群はuntrackedのまま保持し、コミット対象から除外する。最終コミットSHAとpush先はhandoff本文に記載し、clean provenance付きnative physical matrixを別実施するまで、上記NOT RUN/NOT CLAIMEDを維持する。
