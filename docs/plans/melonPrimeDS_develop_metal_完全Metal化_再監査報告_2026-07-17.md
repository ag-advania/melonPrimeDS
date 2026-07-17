# melonPrimeDS `develop_metal` 完全Metal化 再監査報告

**再監査日:** 2026-07-17  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `develop_metal`  
**前回監査基準HEAD:** `8077db617b0a0e097f621231217da97632069c40`  
**差分状態:** 前回監査HEADから1コミット先行、遅延0  
**変更規模:** 12ファイル、コード部分641行追加／231行削除  
**前回監査書:** `docs/plans/melonPrimeDS_develop_metal_完全Metal化_実装監査報告.md`  
**監査方式:** GitHub上の最新ブランチ内容に対する静的コード監査  
**注意:** 本監査ではmacOS実機上のビルド、Metal Validation、GPU Frame Capture、実ROMプレイを再実行していない。さらに、今回の差分にはコンパイルを阻止するソース破損があるため、実行受け入れ試験へ進める状態ではない。

---

# 1. 結論

## 1.1 総合判定

> **不合格／マージ・リリース停止。**

今回のpushでは、前回監査で指摘した次の領域に対する修正が入っている。

- capture upload完了時のmetadata更新
- capture stateの非同期lifetime
- persistent staging buffer ring
- Metal output metadata validation
- last-known-good Metal textureの保持
- Compute rendererの説明修正
- 2D snapshot中断処理
- Metal 3Dの同一フレーム再利用
- presenterのCPU fallback取り扱い
- stale comment整理

ただし、`src/GPU_MetalCaptureMethods.inc`内で、複数のログ文字列に含まれていた`"\n"`が**文字列リテラル内部の実改行**へ変換されている。

C／C++の通常文字列リテラルは、エスケープされていない改行を含められない。このため、Clangは当該ファイルをコンパイルできない。

想定エラー:

```text
warning/error: missing terminating '"' character
error: expected expression
error: expected ')'
```

したがって、今回追加された非同期upload、output validation、snapshot修正等は、実行以前にbuildへ到達できない。

## 1.2 完全Metal化の判定

今回の差分を構文修正したとしても、完全Metal化は未達のままである。

残存する主要経路:

```text
Display Capture／Full-GPU不適格フレーム
    → Metal 3D readback
    → SoftRenderer 2D
    → CPU BGRA
    → Metal presenterへ再upload

Metal Compute不適格フレーム
    → Metal raster RasterReference

HUD／OSD
    → QImage／QPainter
    → CPU bitmap upload
    → Metal blend
```

未達フェーズ:

- M4: per-scanline／per-segment Display Capture
- M5: 全通常フレームreadback 0
- M6: `SoftRenderer`継承撤廃
- M7: Computeから`RasterReference`撤廃
- M8: presenter Metal texture-only
- M9: HUD／OSD Metal native化
- M10: macOS初回既定Metal
- M11: shader asset分離

## 1.3 優先度

修正順序は次のとおり。

1. **B-01: 文字列リテラル破損を全件修正**
2. **H-01: `CaptureMeta`の非同期データ競合を修正**
3. **H-02: upload再送ループ／stale completionを修正**
4. Metal ON build
5. Metal OFF／FORCE_DISABLE build
6. capture fallback実ROM確認
7. 前回監査項目の再受け入れ
8. M4へ進む

---

# 2. 前回監査後の差分

前回監査HEADから変更されたファイル:

```text
docs/plans/melonPrimeDS_develop_metal_完全Metal化_実装監査報告.md
src/GPU2D_Metal.mm
src/GPU2D_MetalFullGpuMethods.inc
src/GPU3D_Metal.mm
src/GPU3D_MetalCompute.h
src/GPU3D_MetalCompute.mm
src/GPU_Metal.h
src/GPU_Metal.mm
src/GPU_MetalCaptureMethods.inc
src/GPU_MetalFullGpuMethods.inc
src/frontend/qt_sdl/MelonPrimeScreenMetal.mm
src/frontend/qt_sdl/VideoSettingsDialog.cpp
```

コード差分:

| ファイル | 追加 | 削除 | 主な目的 |
|---|---:|---:|---|
| `GPU2D_Metal.mm` | 18 | 0 | segmented snapshot状態補強 |
| `GPU2D_MetalFullGpuMethods.inc` | 15 | 11 | snapshot中断／frame境界補強 |
| `GPU3D_Metal.mm` | 53 | 37 | 同一フレーム再利用・診断整理 |
| `GPU3D_MetalCompute.h` | 7 | 5 | visible source説明の更新 |
| `GPU3D_MetalCompute.mm` | 25 | 4 | Compute状態・診断整理 |
| `GPU_Metal.h` | 28 | 1 | capture state共有所有・upload helper |
| `GPU_Metal.mm` | 61 | 28 | output／診断／fallback補強 |
| `GPU_MetalCaptureMethods.inc` | 238 | 92 | 非同期upload・staging ring |
| `GPU_MetalFullGpuMethods.inc` | 7 | 0 | Full-GPU frame状態補強 |
| `MelonPrimeScreenMetal.mm` | 187 | 51 | output validation・last-good lease |
| `VideoSettingsDialog.cpp` | 2 | 2 | Compute説明更新 |

---

# 3. BLOCKER

## B-01: `GPU_MetalCaptureMethods.inc`がコンパイル不能

**重大度:** BLOCKER  
**状態:** 新規発生  
**影響:** macOS Metal ON build失敗  
**対象:** `src/GPU_MetalCaptureMethods.inc`

## 3.1 問題

正常なC++コード:

```cpp
std::fprintf(stderr,
    "[MelonPrime] message\n");
```

現在のコードに相当する状態:

```cpp
std::fprintf(stderr,
    "[MelonPrime] message
");
```

後者では、文字列を閉じる前にソース上の改行へ到達する。

## 3.2 確認した破損箇所

少なくとも次のログ文字列で破損を確認した。

1. shader compile failure
2. capture pipeline failure
3. readback pipeline failure
4. capture texture allocation failure
5. capture state configured
6. first GPU capture submitted
7. staging buffer ring exhausted
8. CPU fallback upload command failure
9. CPU fallback capture upload
10. incomplete capture sync
11. on-demand CPU readback

代表例:

```cpp
"[MelonPrime] metal display capture: shader compile failed: %s
",
```

```cpp
"[MelonPrime] metal display capture: configured scale=%d "
"capture128=%zux%zux16 capture256=%zux%zux4
",
```

```cpp
"[MelonPrime] metal display capture: staging buffer ring "
"exhausted, falling back to a one-off buffer allocation "
"(further occurrences not logged)
");
```

```cpp
"[MelonPrime] metal display capture: CPU fallback upload "
"command failed: %s
", message);
```

## 3.3 必須修正

すべて次の形式へ戻す。

```cpp
"[MelonPrime] ...\n",
```

複数行連結の場合:

```cpp
std::fprintf(stderr,
    "[MelonPrime] metal display capture: staging buffer ring "
    "exhausted, falling back to a one-off buffer allocation "
    "(further occurrences not logged)\n");
```

## 3.4 再発防止

必須確認:

```zsh
cmake --build build-mac-metal --parallel 4
```

追加推奨:

```zsh
clang++ -fsyntax-only
```

実際にはObjective-C++／Metal framework includeが必要なので、CMake生成compile commandを使用する。

```zsh
cmake -B build-mac-metal \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DMELONPRIME_ENABLE_METAL=ON \
  -DUSE_QT6=ON

cmake --build build-mac-metal --parallel 4
```

CIではMetal sourceを変更したPRに対し、最低でもmacOS configure＋compileを必須checkにする。

---

# 4. HIGH

## H-01: `CaptureMeta`にC++データ競合がある

**重大度:** HIGH  
**状態:** 新規発生  
**対象:** `GPU_MetalCaptureMethods.inc`

## 4.1 現在の変更

lifetime対策として、`CaptureState`が次のように変更された。

```cpp
std::shared_ptr<MetalCaptureState> CaptureState;
```

completion handlerも`shared_ptr`を値captureする。

```cpp
std::shared_ptr<MetalCaptureState> capturedState = CaptureState;
```

これはuse-after-free対策として正しい方向である。

しかし、lifetime安全性とthread safetyは別問題である。

completion handler:

```cpp
meta.Valid = ok;
meta.PendingCpuUpload = !ok;
```

renderer／emulation thread:

```cpp
const auto& meta = CaptureState->Meta[layer];
if (!meta.PendingCpuUpload || meta.Size < 0)
    continue;
```

```cpp
CaptureState->Meta[layer].PendingUploadSerial++;
```

```cpp
return layer < 0 ||
    (layer < 16 && CaptureState->Meta[layer].Valid);
```

`Valid`、`PendingCpuUpload`、`PendingUploadSerial`、`Size`はいずれも非atomicであり、mutexもない。

Metal completion handlerはMetalのcallback threadで実行され得るため、renderer threadとの同時read／writeはC++上のdata raceであり、未定義動作となる。

## 4.2 想定症状

- `Valid`更新をrenderer threadが観測しない
- `PendingCpuUpload`が不定に戻る
- serial比較が誤る
- Full-GPU eligibilityが不規則に変化
- capture frameが恒常的にCPU fallback
- Thread Sanitizerでrace報告
- 最適化buildだけで再現する不安定動作
- scale変更／renderer再構築中の不整合

## 4.3 必須修正

最も安全なのは`MetalCaptureState`へmetadata mutexを追加する方法。

```cpp
struct MetalCaptureState
{
    std::mutex MetaMutex;
    std::array<CaptureMeta, 16> Meta {};
};
```

completion:

```cpp
{
    std::lock_guard<std::mutex> lock(capturedState->MetaMutex);

    for (const auto& entry : layerSerials)
    {
        auto& meta = capturedState->Meta[entry.first];
        if (meta.PendingUploadSerial != entry.second)
            continue;

        meta.Valid = ok;
        meta.PendingCpuUpload = !ok;
    }
}
```

次も同じmutexで保護する。

- `ConfigureMetalCaptureState()`のmetadata copy
- `AllocCapture()`
- `UploadCpuCompletedCaptures()`
- `SyncVRAMCapture()`
- `MetalCaptureResourcesCoherent()`
- `CaptureMetalDisplayCaptureLine()`
- `EncodeMetalDisplayCapture()`
- scale reconfigure
- GPU capture completion

`MetalCaptureResourcesCoherent()`では、layerごとにlockせず、一度lockして全metadataをsnapshotする。

```cpp
std::array<CaptureMeta, 16> metaSnapshot;
{
    std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
    metaSnapshot = CaptureState->Meta;
}
```

GPU command encode中に長時間mutexを保持しない。

---

## H-02: upload完了前の毎フレーム再送でlivelockし得る

**重大度:** HIGH  
**状態:** 新規発生  
**対象:** `UploadCpuCompletedCaptures()`

## 4.4 現在の状態遷移

dispatch時:

```cpp
meta.PendingUploadSerial++;
UploadCaptureTexture(...);
```

completion成功時:

```cpp
meta.Valid = true;
meta.PendingCpuUpload = false;
```

問題は、dispatchした時点で`PendingCpuUpload`をfalseまたは「in flight」へ変えていないことである。

次フレームまでにGPU commandが完了しなかった場合:

```text
frame N:
  PendingCpuUpload=true
  serial=1
  upload #1 submit

frame N+1:
  upload #1未完了
  PendingCpuUpload=true
  serial=2
  upload #2 submit

upload #1 completion:
  current serial=2
  staleとして無視

frame N+2:
  upload #2未完了
  PendingCpuUpload=true
  serial=3
  upload #3 submit

upload #2 completion:
  current serial=3
  staleとして無視
```

GPUが常に1フレーム以上遅れる負荷条件では、新しいuploadが毎フレーム古いuploadをsupersedeし続ける。

その結果:

- `PendingCpuUpload`が永続的にfalseにならない
- `Valid`が永続的にtrueにならない
- staging ringが埋まる
- one-off buffer allocationへ退避
- capture resourcesがcoherentにならない
- Full-GPUへ復帰できない
- CPU fallbackが持続する

前回修正した「持続fallback／持続フリーズ」に近い症状を別経路で再導入し得る。

## 4.5 stale completionが新しいdirty stateを上書きする

`PendingUploadSerial`は**データが変更された時ではなく、uploadをdispatchした時**に増加する。

例:

```text
1. size=0のuploadをserial=1でsubmit
2. 完了前にAllocCapture()がsize=1へ変更
3. AllocCapture()はPendingUploadSerialを増やさない
4. 古いserial=1のcompletionが一致
5. size=1用textureをuploadしていないのにValid=true、Pending=false
```

これはperformanceだけでなくcorrectness問題である。

## 4.6 必須修正

metadataを次の状態へ分離する。

```cpp
struct CaptureMeta
{
    int Size = -1;

    bool Valid = false;
    bool Dirty = false;
    bool UploadInFlight = false;

    uint64_t DirtySerial = 0;
    uint64_t SubmittedSerial = 0;
};
```

データ／size変更時:

```cpp
meta.Size = newSize;
meta.Valid = false;
meta.Dirty = true;
meta.DirtySerial++;
```

dispatch前:

```cpp
if (!meta.Dirty || meta.UploadInFlight)
    return;

meta.UploadInFlight = true;
meta.SubmittedSerial = meta.DirtySerial;
```

completion:

```cpp
std::lock_guard<std::mutex> lock(state->MetaMutex);

meta.UploadInFlight = false;

if (!ok)
{
    meta.Valid = false;
    meta.Dirty = true;
    return;
}

if (meta.DirtySerial == submittedSerial)
{
    meta.Valid = true;
    meta.Dirty = false;
}
else
{
    // upload後にCPU側内容が変化した
    meta.Valid = false;
    meta.Dirty = true;
}
```

256x256 bank uploadは4 layerをまとめるため、bank単位のin-flight stateを持つか、対象layer全てのsubmitted serial snapshotを保持する。

---

# 5. MEDIUM

## M-01: GPU Display Captureも完了前に`Valid=true`

**重大度:** MEDIUM  
**状態:** 前回から残存

`EncodeMetalDisplayCapture()`はcommandをcommitした直後、completion確認前に次を実行する。

```cpp
CaptureState->Meta[layer].Size = size;
CaptureState->Meta[layer].Valid = true;
CaptureState->Meta[layer].PendingCpuUpload = false;
```

同一renderer queue上の後続GPU commandについてはqueue orderingにより成功時の順序は保証できる。

しかし、command bufferがerrorになった場合:

- texture writeは成立していない
- metadataは`Valid=true`
- 後続BG／OBJ／3Dが古いcapture textureを正しいものとして扱い得る
- CPU on-demand readbackも古いtextureをVRAMへ戻し得る

必須修正:

- GPU captureにもcompletion handlerを追加
- commandごとのcapture destination layer／serialを保存
- success時だけ`Valid=true`
- error時は`Valid=false`、`Dirty=true`
- CPU uploadと同じmutex／generation contractを使用
- command errorをFull-GPU eligibilityへ反映

---

## M-02: output validationがfail-open

**重大度:** MEDIUM  
**状態:** 部分修正

presenterに`ValidateMetalRendererOutput()`が追加されたことは妥当である。

検証済み:

- texture non-null
- device一致
- `MTLTextureType2DArray`
- layer数
- metadata width／heightとtexture一致
- 256x192×scale

しかし、現在はmetadataが0の場合に検証を省略する。

```cpp
if (output.ArrayLength != 0 && ...)
```

```cpp
if (output.Width != 0 && ...)
```

```cpp
if (output.Height != 0 && ...)
```

現在のproducerはmetadataを埋める設計なので、0を互換値として許可する必要はない。

また、コメントでは`Generation`も対象としているが、実際には次を検証していない。

- `Generation != 0`
- `FrameSerial != 0`
- generationの退行
- serialの退行
- producer変更
- renderer再構築

必須修正:

Metal production outputはfail closedにする。

```cpp
if (output.ArrayLength != 2)
    fail;
if (output.Width == 0 || output.Height == 0)
    fail;
if (output.Scale == 0)
    fail;
if (output.Generation == 0)
    fail;
if (output.FrameSerial == 0)
    fail;
```

texture側も、現在のcontractが2-layer固定なら次にする。

```cpp
texture.arrayLength == 2
```

将来のproducer追加は、metadata未設定を許容するのではなく、producer側を同時更新する。

---

## M-03: retained Metal leaseのlifecycle境界が不足

**重大度:** MEDIUM  
**状態:** 新規

presenterはlast-known-good textureを保持するため、次をmemberとして持つ。

```cpp
RendererOutputLease lastGoodMetalLease;
```

ring slotを保持する設計自体は妥当である。

ただし、次のlifecycleで明示的にclearする処理が必要。

- renderer切替
- ROM close
- NDS instance再作成
- reset
- savestate loadでgenerationが切り替わる場合
- Metal presenter再attach
- scale変更
- Metal raster↔Metal Compute切替
- Software／OpenGLへ変更

現在のmetadata validationはGenerationを検証せず、新旧producer identityも持たないため、何も新しいoutputがない瞬間に旧ROM／旧rendererのtextureを保持し続ける可能性がある。

修正案:

```cpp
struct RendererOutput
{
    uint64_t ProducerId;
    uint64_t Generation;
    uint64_t FrameSerial;
};
```

- `ProducerId`はrenderer instanceごとに一意
- producer変更時は`lastGoodMetalLease.ReleaseNow()`
- generation退行時もclear
- emulation inactive時もゲーム画面textureをclear
- CPU fallbackがある場合は現在どおりfresh CPU frameを優先

---

## M-04: staging ringの高解像度memory上限が大きい

**重大度:** MEDIUM  
**状態:** 新規

persistent ringはper-upload `newBufferWithBytes`を減らすための正しい方向である。

しかし、bufferは内部解像度scale済みRGBA8画像を保持する。

256x256 capture、16x:

```text
4096 × 4096 × 4 byte
= 67,108,864 byte
= 64 MiB／slot
```

ringは8 slot:

```text
64 MiB × 8
= 512 MiB
```

さらにcapture texture自体もscale済みである。

16x時の概算:

```text
Capture128 + Snapshot128 ≈ 512 MiB
Capture256 + Snapshot256 ≈ 512 MiB
staging ring最大          ≈ 512 MiB
--------------------------------
capture関連だけで         ≈ 1.5 GiB
```

これに3D color／depth／attribute、2D output、Compute tile memory、presenter textureが加わる。

Intel Mac、統合GPU、メモリ8GB環境では現実的なリスクである。

修正案:

1. staging bufferはnative 16-bit／native sizeで保持
2. scale拡大をMetal compute kernelで実行
3. CPU側のscale²複製loopを撤廃
4. ring slot数を実測値に基づき2～3へ縮小
5. slotごとの最大capacityを制限
6. memory pressure時はscale済みcapture textureを再設計
7. 8x／16x時のcapture memory予算をfeature probeへ追加
8. allocation総量を診断ログへ出す

最低限:

```cpp
constexpr NSUInteger kMaxCaptureStagingBytes = ...;
```

上限超過時は、one-off巨大bufferへ逃げず、native-size upload＋GPU expansionへ切り替える。

---

## M-05: `CaptureMeta`のshared ownershipはlifetimeのみ解決

**重大度:** MEDIUM  
**状態:** 部分修正

今回の修正コメントはuse-after-scope／use-after-freeを正しく認識している。

改善点:

- `CaptureState`を`shared_ptr`化
- completion handlerがstateを値capture
- staging slotのBusy stateも`shared_ptr<atomic<bool>>`
- reconfigure中のdangling stateを防止
- C++ lambda内でObjective-C blockを作る構造をmember functionへ移動

これらは妥当。

ただし、次は未解決。

- metadata data race
- dirty generation
- upload in-flight suppression
- GPU capture failure
- state transition atomicity
- old stateからnew stateへmetadata copyする際の同期
- multiple layer／bank transaction

したがって、この項目は「修正完了」ではなく「lifetime部分のみ完了」と判定する。

---

## M-06: CPU fallbackを正常経路として維持

**重大度:** MEDIUM  
**状態:** 仕様上継続

presenterは、fresh `CpuBgra`が存在する場合、retained Metal textureよりCPU frameを優先するよう整理されている。

これはM4未完了の現在において、黒画面／stale frameを避けるため正しい。

一方、完全Metal contractとしては未達。

```text
Metal renderer selected
→ CpuBgra accepted
→ screenTexへreplaceRegion
→ Metal presenterで表示
```

したがってM8は未完了。

M4完了までは現挙動を維持し、M4完了後に次へ切り替える。

```text
valid Metal lease
→ present

no new Metal lease + previous valid lease
→ retain previous

no valid Metal output
→ explicit failure／black clear

CpuBgra
→ debug comparison buildだけ
```

---

# 6. LOW

## L-01: 成功完了前に「uploaded」とログする

`UploadCpuCompletedCaptures()`はcommandをsubmitした直後に次の趣旨をログする。

```text
CPU fallback capture uploaded to GPU texture arrays
```

実際のcompletionは非同期であり、失敗する可能性がある。

文言は次へ変更する。

```text
CPU fallback capture upload submitted to GPU texture arrays
```

success logが必要ならcompletion handler側でrate-limitして記録する。

---

## L-02: presenter commentと実際の選択順が不一致

`CpuBgra` branch付近のコメントは「retained Metal frameがない場合にだけ使う」と読める。

実際にはfresh `CpuBgra`を常に優先している。

現在の挙動はM4未完了下では正しいため、コメントを修正する。

```text
Fresh CpuBgra is always preferred over a retained Metal texture while
the production capture fallback remains enabled. The retained texture
is used only when no new output of either kind exists.
```

---

## L-03: output validationの理由文字列API

```cpp
const char** outReason
```

は内部関数としては動くが、null pointer防御がない。

```cpp
*outReason = nullptr;
```

callerが常にnonnullでも、より安全にするなら戻り値を構造化する。

```cpp
enum class MetalOutputValidationError
{
    None,
    NilTexture,
    DeviceMismatch,
    WrongTextureType,
    WrongArrayLength,
    MetadataMissing,
    SizeMismatch,
    ScaleMismatch,
    GenerationInvalid,
    SerialInvalid,
};
```

ログ文字列はenumから変換する。

---

# 7. 前回指摘の対応状況

| 前回指摘 | 今回の状態 | 判定 |
|---|---|---|
| capture upload完了前にmetadataをvalid化 | CPU upload側はcompletion更新へ変更 | **部分修正** |
| capture state reconfigure時のdangling handler | `shared_ptr`化 | **改善** |
| capture uploadのper-frame `MTLBuffer`確保 | staging ring追加 | **改善。ただしmemory risk** |
| output metadataをpresenterが検証しない | validator追加 | **部分修正** |
| invalid outputをそのまま表示 | last-good textureへ退避 | **改善** |
| output generation未検証 | 未実装 | **残存** |
| snapshot line 0中断 | 関連差分あり | **build復旧後に実行再確認** |
| GPU-only identical frame reuse | 関連差分あり | **build復旧後に実行再確認** |
| Compute headerの古い説明 | visible cutover説明へ更新 | **修正** |
| Compute `RasterReference`依存 | 依然保持 | **未修正** |
| presenter CPU BGRA fallback | 意図的に維持 | **未修正** |
| HUD CPU framebuffer依存 | 維持 | **未修正** |
| `SoftRenderer`継承 | 維持 | **未修正** |
| M4 capture interleave | 未実装 | **未修正** |

---

# 8. 構文修正後の必須テスト

## 8.1 Build gate

### macOS Metal ON

```zsh
./tools/build/macos/build-macos-metal-test.sh
```

期待:

```text
GPU_MetalCaptureMethods.inc compile PASS
Metal framework link PASS
QuartzCore framework link PASS
app bundle link PASS
```

### macOS Metal OFF

```zsh
cmake -B build-mac-no-metal \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMELONPRIME_ENABLE_METAL=OFF \
  -DUSE_QT6=ON

cmake --build build-mac-no-metal --parallel 4
```

### FORCE_DISABLE

```zsh
cmake -B build-mac-metal-disabled \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DMELONPRIME_ENABLE_METAL=ON \
  -DMELONPRIME_FORCE_DISABLE_METAL=ON \
  -DUSE_QT6=ON

cmake --build build-mac-metal-disabled --parallel 4
```

## 8.2 Sanitizer

非同期metadata変更を入れたため、Thread Sanitizerを必須にする。

```zsh
-DCMAKE_BUILD_TYPE=Debug
-DCMAKE_C_FLAGS="-fsanitize=thread"
-DCMAKE_CXX_FLAGS="-fsanitize=thread"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
```

確認対象:

- `CaptureState->Meta`
- `PendingUploadSerial`
- `Valid`
- `PendingCpuUpload`
- staging ring cursor
- reconfigure
- renderer shutdown
- scale変更
- completion handler

## 8.3 GPU遅延再現

意図的にGPUを遅らせ、uploadが1フレーム以内に完了しない条件を作る。

確認:

```text
同一layerのupload submitが毎フレーム増えない
UploadInFlight中は再送しない
dirty更新時だけ次uploadを予約
ring exhaustedが持続しない
PendingCpuUploadが最終的にfalse
Validが最終的にtrue
Full-GPUへ復帰
```

## 8.4 Scale matrix

- 1x
- 2x
- 4x
- 8x
- 16x

各scaleで記録:

- Capture128 texture bytes
- Snapshot128 texture bytes
- Capture256 texture bytes
- Snapshot256 texture bytes
- staging ring allocated bytes
- peak resident memory
- upload count
- one-off allocation count
- CPU conversion time
- GPU blit time
- fallback frame count

## 8.5 Lifecycle

- capture中のscale変更
- capture中のrenderer切替
- capture中のROM close
- capture中のreset
- capture中のsavestate load
- app終了直前のin-flight upload
- dual instance
- fullscreen切替
- sleep／wake

## 8.6 Output validation

fault injectionで次を個別に拒否できることを確認する。

- nil texture
- wrong device
- 2D texture
- arrayLength 1
- arrayLength 3
- metadata width 0
- metadata height 0
- scale 0
- width mismatch
- height mismatch
- generation 0
- serial 0
- generation rollback
- producer change

---

# 9. 推奨修正パッチ構造

## PR-A: build復旧のみ

変更:

- `GPU_MetalCaptureMethods.inc`の実改行を全て`\n`へ戻す
- 他のロジックは変更しない

目的:

- build failureとロジック問題を分離
- CIを最初にgreenへ戻す

受け入れ:

- macOS Metal ON compile
- macOS Metal OFF compile
- FORCE_DISABLE compile

## PR-B: capture metadata state machine

変更:

- `MetaMutex`
- `DirtySerial`
- `SubmittedSerial`
- `UploadInFlight`
- GPU capture completion
- CPU upload completion
- reconfigure同期
- error retry

目的:

- lifetimeだけでなくthread safetyとstate correctnessを確立

受け入れ:

- TSan 0件
- GPU遅延時に重複submitなし
- stale completionが新しいdirty stateを上書きしない

## PR-C: staging memory制御

変更:

- native-size staging
- GPU expansion
- ring slot数縮小
- memory budget
- allocation telemetry

受け入れ:

- 8x／16xで異常なmemory増加なし
- one-off allocation 0
- CPU scale² loop撤廃

## PR-D: output contract厳密化

変更:

- metadata必須
- Generation／FrameSerial／ProducerId
- lifecycleでlast-good lease clear
- invalid output fail closed

受け入れ:

- fault injection全項目合格
- stale ROM frameなし
- renderer切替正常

---

# 10. 完全Metal化フェーズへの影響

## M4

今回の修正はcapture fallbackの安定性を高める目的であり、per-segment GPU captureそのものではない。

M4は未完了。

## M5

CPU fallback capture uploadの待ち時間削減は前進だが、capture frameの3D readbackとCPU compositorは残る。

M5は未完了。

## M6

`MetalRenderer : public SoftRenderer`は維持。

M6は未完了。

## M7

Compute headerの説明は実装へ近づいたが、`RasterReference` fieldとper-frame fallbackは維持。

M7は未完了。

## M8

output validationとlast-good leaseは前進。

ただしfresh CpuBgra fallbackをproductionで受け入れるため、texture-onlyではない。

M8は部分完了。

## M9

今回の差分でも`QImage`、`QPainter`、CPU HUD framebufferは残る。

M9は未完了。

---

# 11. 最終判定表

| 項目 | 前回 | 今回 |
|---|---|---|
| ソースがbuild可能 | 未確認 | **不合格** |
| capture state lifetime | 危険 | **改善** |
| capture metadata thread safety | 未実装 | **不合格** |
| stale completion対策 | 未実装 | **不完全** |
| upload in-flight抑制 | 未実装 | **不合格** |
| staging buffer再利用 | 未実装 | **実装、要memory修正** |
| output texture validation | 未実装 | **部分合格** |
| last-good texture retention | 未実装 | **合格、lifecycle不足** |
| Generation validation | 未実装 | **未実装** |
| CPU BGRA fallback | 残存 | **残存** |
| M4 capture GPU常駐 | 未実装 | **未実装** |
| SoftRenderer撤廃 | 未実装 | **未実装** |
| Compute独立化 | 未実装 | **未実装** |
| HUD／OSD Metal化 | 未実装 | **未実装** |
| 完全Metal化 | 未達 | **未達** |

---

# 12. 最終結論

今回のpushは、前回監査で指摘した問題を具体的に修正しようとしており、方向性は概ね正しい。

特に次は有意な改善である。

- capture stateの`shared_ptr`化
- completion serialの導入
- persistent staging ring
- output metadata validator
- invalid output時のlast-known-good texture
- Compute visible source説明の更新

しかし、現HEADには次の三段階の問題がある。

```text
第1段階:
GPU_MetalCaptureMethods.incが構文破損しbuild不能

第2段階:
構文修正してもCaptureMetaにdata raceがある

第3段階:
mutexを追加してもupload再送state machineが不完全
```

このため、現時点では「前回監査指摘を修正済み」とは判定できない。

正確な判定:

> **修正意図は妥当だが、実装は未受け入れ。現HEADはコンパイルBLOCKERを含み、capture metadataのthread safetyとupload state machineにもHIGH問題が残る。**

まずPR-Aでbuildを復旧し、その後PR-Bでmetadata state machineを作り直すこと。これらを分離しないままM4へ進むと、Display Captureの複雑なGPU因果関係と非同期metadata不具合が重なり、原因切り分けが困難になる。
