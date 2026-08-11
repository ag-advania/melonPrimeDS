# Vulkan / DirectX 12 / Metal Compute 性能・正確性監査

**監査日:** 2026-08-12  
**基準:** `SoftRenderer3D` の native 1x raster 結果、および既存の structured software 2D composition 契約  
**対象:** `GPU3D_Vulkan`、`GPU3D_DX12`、`GPU3D_MetalCompute` と各 compute shader

## 結論

三つの backend は、Software renderer の固定小数点補間、depth/blend、shadow、edge/fog/AA、
native capture を再現するための parity 修正と回帰ラチェットを既に持つ。監査時点の
Vulkan 111 shader variants と DX12 111 shader variants はコンパイルを通過し、静的な
Software parity audit も通過した。

一方、正確性を変えずに除去できるホットパス上の無駄を三件確認した。最優先は Metal
Compute の fixed-capacity dispatch である。Metal は binning 後の実 work count を GPU 上で
既に計算し、indirect dispatch 用に近い引数まで生成しているのに、Sort と Raster を毎 batch
`MaxWorkTiles` 全域へ dispatch している。Vulkan / DX12 は同じ段階を実 work count の indirect
dispatch にしており、Metal だけが高解像度ほど大量の空 work を起動する。

## 確認した正確性契約

- native 1x は `FinalPosition` を使い、high-resolution coordinate は scale > 1 の拡張に限定される。
- polygon stream は順序を維持した consecutive batch に分割され、各 batch の保守的 tile 上界が
  固定 work buffer 容量以下になる。overflow 時に polygon layer を部分破棄しない。
- accepted-pixel AA coverage correction、front/back-facing depth tie、二層 translucent blend、
  shadow continuation、signed-16 texture coordinate narrowingが三 backend でラチェットされる。
- native resolve / display capture は 256x192 の Software 契約を維持し、visible high-resolution
  presentation と分離される。
- clear bitmap、texture / palette VRAM、structured 2D planes は同一 frame の入力として同期される。

これらは今回の最適化で変更しない。特に GPU work の dispatch 数だけを実 count に狭め、shader
内の work index、polygon order、tile order、buffer layout は維持する。

## 指摘事項

### P1: Metal Sort / Raster が実 work 数ではなく全 capacity を dispatch する

**根拠:** `GPU3D_MetalCompute.mm` は `mp_compute_calc_offsets` で global work count と
`SortWorkCountStart` を生成する。しかし command encoding は Sort を
`DispatchGroups(MaxWorkTiles, 32)`、Raster を `MaxWorkTiles` threadgroups で起動し、shader の
early return に空 work の除去を任せている。

4x の通常 geometry では `MaxWorkTiles = 196,608` であるため、1 batch あたり Sort は
196,608 threads、Raster は 196,608 groups x 64 threads = 12,582,912 threads を起動する。
9x-16x bucket は一 workgroup が 1,024 threadsであり、256 MiB/slot の tile budgetで capacity が
制限されても空 group のコストが大きい。これは性能上の上限値であり、実際の空 work 数と時間短縮率は
scene / GPU に依存するため、macOS 実機測定なしに速度向上率は主張しない。

**修正方針:** header に Sort と Raster の `MTLDispatchThreadgroupsIndirectArguments` を別々に保持し、
`mp_compute_calc_offsets` が `min(globalCount, maxWorkTiles)` から両方を生成する。Sort と Raster は
`dispatchThreadgroupsWithIndirectBuffer` を使う。zero work は zero-group indirect dispatch とし、
既存 shader bounds check は防御として残す。

### P1: Metal が毎 compute frame 640 KiB の VRAM snapshot を `memcmp` する

**根拠:** texture 512 KiB と palette 128 KiB の各 slot shared buffer を
`UpdateSharedSnapshotIfChanged()` で全比較している。一方、呼出元は直前に
`MakeVRAMFlat_*Coherent()` の正確な changed bit を取得済みである。

**修正方針:** texture / palette ごとに renderer 所有 version を持ち、dirty state が変化した時だけ
version を進める。slot version と renderer version が異なる時だけ snapshot をコピーする。
これにより内容の同一性を推測せず、既存 dirty/coherency 契約をそのまま利用して定常時の全領域比較を除く。

### P2: Vulkan / DX12 の CPU span orchestration がフレームごとに一時 vector を確保する

**根拠:** 両 backend の `BuildPolygonBatches()` は local `std::vector` を返し、Vulkan はさらに
`variantTextureSets` を local vector として生成する。最大要素数はいずれも DS polygon 上限 2,048 で
既知であり、Metal は既に固定 `std::array` を使う。

**修正方針:** renderer instance 所有の `std::array<..., MaxRenderPolygons>` と count に置き換える。
allocation lifetime だけを変え、batch partition と descriptor selection の順序は変えない。DX12 の
per-frame SRV cache は固定容量・epoch付きopen addressingへ変更し、node allocationと毎frameの全消去を
避ける。Vulkan texture descriptorも `(image view, sampler)` が同じvariant間でframe内再利用する。

## 実装結果

監査後、上記三件を実装した。

- Metal header に Sort用 `(ceil(work/32), 1, 1)` と Raster用 `(work, 1, 1)` の独立したindirect
  argumentsを追加した。production pathの二つのfixed-capacity dispatchを
  `dispatchThreadgroupsWithIndirectBuffer` に置換し、foundation self-testもGPU生成のSort indirect
  argumentsを実際に消費してRaster argumentsを検査する。これは、GPUが生成した引数をgrid開始直前に
  取得する[Appleのindirect dispatch契約](https://developer.apple.com/documentation/metal/mtlcomputecommandencoder/dispatchthreadgroups%28indirectbuffer%3Aindirectbufferoffset%3Athreadsperthreadgroup%3A%29?language=objc)
  に沿い、各offsetも要求どおり4-byte alignmentを満たす。
- Metal texture / palette snapshotはrenderer versionとslot versionが不一致の時だけコピーする。
  dirty trackerが変化なしと確定したframeでは640 KiBの`memcmp`を行わない。
- Vulkan / DX12 polygon batchesとVulkan variant descriptor tableをinstance scratchへ移し、local
  vector allocationを除去した。Vulkanは同一view/sampler descriptorを再利用し、DX12は4,096-entryの
  epoch付き固定SRV cacheを使う。
- `audit-raster-software-parity.py` に、bounded batching、allocation-free scratch、Metal indirect
  dispatch、versioned VRAM snapshotを後退させないsource ratchetを追加した。

work index、work descriptor、polygon order、shader内bounds check、depth/blend/final outputには変更を
加えていない。したがって最適化で減るのは空dispatch、重複descriptor work、CPU allocation/比較であり、
Software parityを定義する演算経路は同一である。

## 維持する設計

- Vulkan / DX12 の一つの raster working set と frame-start fence wait。16x で working set を複製する
  メモリコストが大きく、現在の一 frame 分のCPU/GPU overlapを崩す根拠がない。
- Metal の三 frame slots。今回の監査環境は macOS GPU timingを取得できないため、slot数変更は
  back-pressure とメモリのトレードオフを測定せず実施しない。
- conditional native capture readback、GPU-native presentation、retained HUD upload、pipeline cache。
- bounded polygon batching。work overflow の部分破棄を再導入しない。
- shader bounds checks。indirect arguments が正しくても malformed state に対する防御として残す。

## 監査時点の検証結果

| 検証 | 結果 | 備考 |
|---|---|---|
| `audit-raster-software-parity.py` | PASS | Metal / Vulkan / DX12 の静的 parity contract |
| `check-vulkan-shaders.py` | PASS | 111 variants compile、111 SPIR-V validate、scale-specialized 592 modules validate |
| `check-dx12-shaders.py` | PASS | 111 variants compile |
| `compile-metal-compute-msl.py` | 未実行 | Windows には `xcrun metal` がないため。macOSで必須 |
| Windows MinGW Release build | PASS | developer features ON、Vulkan / DX12 translation unitsを再コンパイル |
| `melonprime_raster_edge_vectors` | PASS | V1-V15を再ビルドして実行 |
| `check-doc-links.py` | PASS | 437 local links |
| `git diff --check` | PASS | whitespace errorなし |
| `check-inc-ownership.ps1` / `audit-melonprime-srp-performance.ps1` | 環境制約 | Windows PowerShell 5.1に`Path.GetRelativePath`がなく起動前提を満たさない |
| native ROM pixel differential | 未実行 | ROM path / agreed savestate が監査環境に指定されていないため |
| macOS GPU timing | 未実行 | Windows監査環境のため。`MELONPRIME_PERF=1` の同一scene比較をmacOSで行う必要あり |

Vulkan audit が出す device-limit notes は failure ではない。runtime feature probe が実 device limit と
memory budgetからscale ceilingを決め、未対応scaleを拒否する既存契約に対応する。

## 修正後ゲート

1. Software parity audit: **達成**。
2. Vulkan / DX12 shader全variantsのコンパイル・検証: **達成**。
3. fixed-capacity Sort / Raster dispatch と per-frame batch/descriptor vectorの除去: **達成**。
4. dirty-derived versionが不一致のslotだけMetal VRAM snapshotを更新: **達成**。
5. Windows repository entry point buildと`git diff --check`: **達成**。
6. Metal MSL offline compile / 実ROM differential / macOS timing: **macOS・ROM fixtureでの継続ゲート**。
   現監査では未実行であり、速度向上の実測値やmacOS runtime成功として扱わない。
