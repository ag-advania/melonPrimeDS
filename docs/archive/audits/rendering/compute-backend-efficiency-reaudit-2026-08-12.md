# Vulkan / DirectX 12 / Metal Compute 効率再監査

**監査日:** 2026-08-12  
**監査基準:** `de921c387` (`Fix performance`)  
**参照資料:** `melonPrimeDS_gpu_renderer_efficiency_reaudit_de921c38.md`  
**正確性基準:** `SoftRenderer3D` native 1x output

## 結論

参照再監査の新規P1 `REPERF-01` は実在した。Vulkan / DX12 の
`BuildPolygons()`は既存の直前polygon / texture-local fast pathで見つからないvariantを
reverse linear scanし、Metal Computeは`VariantData`を`std::find`していた。2,048個がすべて
uniqueなら旧方式は一frameに最大2,096,128回のkey比較を行う。

三backendを、64-entry small tierと4,096-entry large tierからなる固定容量・epoch reset・
open addressingの共通indexへ置換した。
canonical variant array/vectorと初出indexは従来どおりの順で追加し、indexだけをhash tableへ
登録する。frame hot pathのheap allocation、全table clear、polygon順序、raster math、texture key、
`Width` / `Height`の扱いには変更がない。

同時に、再監査の`REPERF-02`もWindows実機で解消した。DX12は従来、texture variantごとに
`t0`～`t4`の同一static SRVを複製し`t5`と合わせた6-descriptor tableを作っていた。root
signatureを`t0`～`t4`と`t5`へ分離し、static tableはframe開始時に1回、texture tableは
1 descriptorだけ作成する。HLSL register番号は変えていない。

USA Rev 1のF4/F5/F7/F8で、最終binaryのVulkan 843 frame / 41,435,136 px、DX12
784 frame / 38,535,168 pxはSoftwareと全画素一致した。developer-only verifierも旧linear
lookupと新indexの`RenderPolygon.Variant`をpolygon単位で比較し、不一致は0だった。

## 実装した修正

### 1. 三backend共通のfixed variant index

`GPU3D_FixedVariantIndex.h`に以下の契約を実装した。

- 最初の32 variantsはL1に収まる64-entry table、超過時は4,096-entry tableへ一度だけ再index
- 最大canonical variants 2,048
- `std::array`だけを所有し、lookup / insertionでallocationしない
- frame resetは32-bit epoch incrementだけ
- epoch wrap時だけentry generationをclearする
- hash衝突はlinear probing後、canonical keyの`operator==`で解決する
- hash tableはindexを返すだけで、canonical insertion orderを決めない

hash対象は既存equalityと同一である。

| Backend | hash key |
|---|---|
| Vulkan / DX12 | `Texture`, `WrapS`, `WrapT`, `CaptureReference`, `CaptureYOffset`, `CaptureType`, `BlendMode` |
| Metal Compute | `TexParam`, `TexPalette`, `BlendMode`, `Textured`, `CaptureKind`, `CaptureLayer`, `CaptureYOffset` |

Vulkan / DX12の`Width` / `Height`は従来からequality対象ではないためhashにも含めない。
直前polygon fast pathと`textureLastVariant` / capture fast pathは維持し、その後のlinear fallback
だけを置換した。Metalの`VariantData.push_back(key)`も初出時に従来と同じ位置で実行する。

### 2. DX12 static / texture SRV table分離

root parametersを次のように変更した。

| Parameter | 内容 |
|---|---|
| 0 | dispatch constants |
| 1 | meta CBV |
| 2 | static SRV table `t0`～`t4` |
| 3 | texture SRV table `t5` |
| 4 | UAV table `u0`～`u11` |

旧方式のtexture cache missは6 descriptorsを消費したが、新方式は1 descriptorだけを消費する。
最大2,048 texture variantsでも、static 5 + dynamic 2,048 + UAV 12は8,192-entry shader-visible
heap内に収まる。F7 formal sampleではdescriptor writesが78.0/frameから38.0/frameへ減った。

### 3. 回帰防止

`audit-raster-software-parity.py`へ以下を追加した。

- Vulkan / DX12 reverse linear fallback禁止
- Metal `std::find(VariantData...)`禁止
- fixed 4,096-entry indexとepoch rollover clearの存在確認
- canonical insert後にindexへ登録する順序確認
- DX12 static / texture table分離と`t5` register確認
- developer differential用legacy sequence verifier確認

実行vectorもV16 / V17へ拡張した。V16は2,048 keysを同じhash bucketへ衝突させても初出indexを
維持すること、V17は8-bit epochを実際にwrapさせ、古いentryが復活しないことを検証する。

## 正確性検証

### 実ROM full-pixel differential

環境はWindows、NVIDIA GeForce RTX 5070 Ti、Metroid Prime Hunters USA Rev 1、native 1x、
VSync off、Software threaded off。savestateはfrontend test hookで直接ロードし、load transitionを
比較対象から除外した。各runは非空3D frame、candidate/reference hash、全49,152 pixels、旧/new
variant sequenceを検査した。

| State | Vulkan | DX12 |
|---|---:|---:|
| F4 / `.ml4` | 212 frame / 差分0 | 199 frame / 差分0 |
| F5 / `.ml5` | 210 frame / 差分0 | 193 frame / 差分0 |
| F7 / `.ml7` | 211 frame / 差分0 | 198 frame / 差分0 |
| F8 / `.ml8` | 210 frame / 差分0 | 194 frame / 差分0 |
| 合計 | 843 / 41,435,136 px | 784 / 38,535,168 px |

Vulkan / DX12の双方で、capture / shadow / toon / decal / modulateを含む実stateがSoftwareと一致した。
Metal runtimeはWindowsでは実行できないため、同じ共通indexのV16/V17、Metal source ratchet、
既存shader contract auditで補完した。macOSでのMSL compileと実ROM differentialは継続gateである。

## 性能測定

### 条件

- ROM / state: USA Rev 1、F7 (`.ml7`)
- native 1x、VSync off、同じwindow mode / renderer config
- `MELONPRIME_PERF=1`、Software oracleなし
- 60秒以上warmup後、末尾300個の1 Hz window（5分以上）を集計
- before binary: SHA-256 `84F19E2D8C37A60A9F47F6035017C0B0D80F242127899EFC049CCF994371D3E9`
- after binary: SHA-256 `AD231AA11A784FC3E35AB1A40806838B23BDC46CB780AF6B10E71012BC710716`
- config SHA-256はbackendごとにbefore / after同一

raw logは`artifacts/perf-baseline/reperf01-before`と
`artifacts/perf-baseline/reperf01-after-final`に保存し、
`tools/perf/summarize-renderer-perf.py --last-windows 300`で再集計できる。

### Formal結果

percentile列は300個の1 Hz windowが報告した各percentileの中央値である。

| Backend / metric | Before p50 / p95 / p99 | After p50 / p95 / p99 | 評価 |
|---|---:|---:|---|
| Vulkan frame | 16.650 / 17.453 / 18.032 ms | 16.641 / 17.483 / 18.216 ms | 60 fps frame timeは同等 |
| Vulkan `BuildPolygons` | 117.950 / 136.500 / 178.325 us | 118.800 / 139.205 / 194.830 us | p50 +0.72%。6 variants/frameではnoise内、高percentile改善は主張しない |
| Vulkan descriptor update | 0.200 / 0.900 / 1.300 us | 0.100 / 0.900 / 1.440 us | timer分解能付近、同等 |
| Vulkan compose pack | 77.800 / 101.090 / 135.430 us | 77.725 / 108.060 / 137.410 us | p50同等 |
| Vulkan queue submit | 13.500 / 24.560 / 37.280 us | 13.600 / 25.345 / 37.585 us | 同等 |
| DX12 frame | 16.624 / 18.749 / 19.267 ms | 16.622 / 18.784 / 19.285 ms | 60 fps frame timeは同等 |
| DX12 `BuildPolygons` | 122.300 / 237.300 / 265.330 us | 118.700 / 230.805 / 258.330 us | p50 -2.94%、p95 -2.74%、p99 -2.64% |
| DX12 descriptor update | 1.200 / 9.605 / 17.055 us | 0.600 / 9.400 / 16.660 us | p50 -50.0% |
| DX12 compose pack | 79.200 / 145.380 / 166.500 us | 79.800 / 145.840 / 165.500 us | 同等 |
| DX12 queue submit | 50.500 / 99.020 / 115.040 us | 51.000 / 100.290 / 118.290 us | 同等 |

| Counter / frame | Before | After |
|---|---:|---:|
| Vulkan variants | 5.9997 | 6.0000 |
| Vulkan structured pack | 2,757,120 B | 2,757,120 B |
| Vulkan texture / scratch upload | 0 / 0 B | 0 / 0 B |
| DX12 variants | 12.0000 | 12.0000 |
| DX12 descriptor writes | 78.0 | 38.0 (-51.28%) |
| DX12 structured pack | 2,757,120 B | 2,757,120 B |
| DX12 span upload | 606,830.67 B | 606,830.72 B |
| DX12 texture / spill upload | 0 / 0 B | 0 / 0 B |

GPU clock / power stateは固定していない。このため、低cardinalityのF7における数µsの差はnoiseを
含み、backend間比較や一般的な速度向上率には使用しない。DX12 descriptor writesの削減はcounterで
直接確認できる。

### 2,048-variant synthetic上限

`tools/perf/variant-index-benchmark.cpp`は7-word keyを2,048個初出させ、逆順で再利用する同一sequenceを
旧reverse scanと新indexへ入力する。50 frames x 5 runsの結果は68.39x～76.49x、中央値71.20xだった。
これは`BuildPolygons`全体や通常game frameの速度向上率ではなく、置換したlookup単体の上限stressである。
checksumは全runで209,612,800と一致した。

## 残存項目の判断

| ID | 判断 | 根拠 / 次のgate |
|---|---|---|
| REPERF-03 structured compositor dirty range | 保留 | 全量2,757,120 B/frameは確認。consumer側full hash/memcmpは追加しない。安全なproducer revision / dirty-line設計と専用parity試験が先。現F7の`compose_pack`は100µs未満。 |
| REPERF-04 Metal sparse VRAM snapshot | macOS計測後 | dirty frameの512 KiB + 128 KiB copy削減候補。全slot pending dirty bitの寿命をMetal実機で検証してから実装する。 |
| REPERF-05 Metal header memset | 保留 | 約48 KiB/frame。zero polygon、batch continuation、abort/self-testを含む全reader初期化証明なしには削除しない。 |
| REPERF-06 DX12 presenter SRV lifetime cache | 保留 | draw数が小さく、今回の3D variant / descriptor修正より優先度が低い。presenter単独telemetry取得後。 |

以下は変更禁止を維持した。

- DX12 `EnsureFrameReadback()`のcontext fence wait
- Vulkan raster一frame-in-flight
- Metal三slot
- native 1x `FinalPosition`
- polygon order、fixed-point interpolation、depth/blend/shadow規則
- shader bounds checks

## 検証一覧

| 検証 | 結果 |
|---|---|
| Windows MinGW official build (`--jobs 1`) | PASS |
| `melonprime_raster_edge_vectors` V1～V17 | PASS |
| Software parity / hot-path ratchet | PASS |
| Vulkan 111 variants + SPIR-V validation + 592 scale modules | PASS |
| DX12 111 variants | PASS |
| F4/F5/F7/F8 Vulkan + DX12 full-pixel differential | PASS |
| legacy/new variant sequence differential | PASS |
| `.inc` ownership | PASS、90 files |
| SRP / performance audit | PASS |
| local Markdown links | PASS、437 links（最終本文を含む） |
| Metal MSL compile / runtime | Windowsでは実行不可（`xcrun metal`なし） |
