# Metal Compute F2 performance optimization audit (2026-08-14)

## 結論

試合中の日本版 F2 state を主対象に、Software renderer の native 1x 出力を維持したまま
Metal Compute の write-only / duplicate work を削除した。

- binning が生成していた未使用 `CoarseMask` と、その全画面 clear dispatch を削除
- `mp_compute_clear_indirect` が全 reader 必要域を初期化する契約へ一本化し、
  CPU の 49,180 B/frame header memset を削除
- final pass の RGB6/A5 を `FinalTexture` と `FinalColorBuffer` の二重出力から
  `FinalTexture` 単独所有へ変更
- native resolve は BGRA8Unorm から RGB6/A5 を可逆復元し、従来と同じ整数 box average を実行
- runtime self-test は RGB6 全64 code、A5 全32 code の texture round-trip を検証

F2 native 1x は最終 build で 363 raster frames / 363 composed frames、補助 F4 は
950 / 951 frames が Software と全画素一致した。F2 4x smoke も fallback と GPU failure なしで
通過した。16x は低スペック Intel Mac の制約により実行していない。

## 責務と KISS

変更は [GPU3D_MetalCompute.mm](../../../../src/GPU3D_MetalCompute.mm) と、その owner である
[final-pass shader](../../../../src/GPU3D_MetalComputeFinalPassShaders.inc) /
[methods](../../../../src/GPU3D_MetalComputeFinalPassMethods.inc) に限定した。

- binning は downstream consumer が読む `FineMask` / `WorkOffsets` / `WorkDescs` だけを生成する
- indirect header の初期化 owner は GPU clear kernel だけとする
- final-pass color の high-resolution owner は `FinalTexture` だけとする
- native resolve は表示用 texture から renderer-neutral RGB6/A5 buffer / native texture を生成する

新しい renderer abstraction、heap allocation、lock、atomic、config lookup は追加していない。

## 固定 work / memory 削減

4x (`1024x768`) では次を削減した。

| 項目 | 削減量 |
|---|---:|
| duplicate `FinalColorBuffer` write | 3,145,728 B/frame |
| production header memset | 49,180 B/frame |
| `FinalColorBuffer` slot storage | 3,145,728 B/slot |
| write-only `CoarseMask` slot storage | 98,304 B/slot |
| 3 slots の常駐 buffer 合計 | 9,732,096 B |
| coarse clear | 1 dispatch / polygon batch |
| coarse mask publish | non-empty tile/group ごとの atomic OR を全廃 |

`FinalTexture` の write と native resolve の全 pixel read、Software と同じ integer box average、
polygon/tile order、span/depth/blend/fog/edge/AA 演算は維持している。

## F2 4x short A/B

環境は Intel Iris Plus Graphics 655、Release + developer features、VSync off、
`MELONPRIME_PERF=1`、Software oracle off。同じ日本版 ROM / `.ml2` を直接ロードした。
変更前 HEAD は detached temporary worktree から同じ公式 macOS wrapper で build し、
変更後と実行順を反転した2組を採取した。各 run は25秒、先頭3 windowsを除いた18 windows。

| 順序 | build | frame p50 | frame p95 | `run` | `draw` |
|---|---|---:|---:|---:|---:|
| after -> before | after | 25.382 ms | 37.147 ms | 25.210 ms | 0.343 ms |
| after -> before | before | 28.109 ms | 36.870 ms | 25.041 ms | 0.706 ms |
| before -> after | before | 24.254 ms | 38.351 ms | 24.721 ms | 0.879 ms |
| before -> after | after | 25.657 ms | 38.028 ms | 25.309 ms | 0.376 ms |

2組の中央値の平均では frame p50 は -2.5%、p95 は同等、`run` は +1.5%、`draw` は
-54.6%だった。`draw` は両順序で短縮した一方、frame / run は熱・clock drift に対して
一貫しない。従って固定 work / bytes と `draw` 区間の短縮は確認結果とするが、
formal な総 frame speedup は主張しない。10分 soak、固定 power/clock、より長い interleaved A/B は
別 gate とする。

## 正確性・build・audit

- official existing-tree macOS Metal + Vulkan Release build: PASS
- Metal runtime MSL compile と foundation/span/raster/depth/final self-tests: PASS
- F2 native 1x Metal/Software raster + final-composed differential: PASS
- F4 native 1x supplemental differential: PASS
- F2 Metal Compute 4x smoke: PASS
- Software parity / hot-path ratchet: PASS
- `git diff --check`: PASS
- offline `xcrun metal`: Command Line Tools 環境に compiler がなく実行不可

ROM、savestate、raw logs、一時 worktree は repository へ追加していない。
