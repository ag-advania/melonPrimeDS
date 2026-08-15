# melonPrimeDS Vulkan / DirectX 12
# ゼロオーバーヘッド Telemetry 共通最適化 実装指示書

- 作成日: 2026-08-15
- Repository: `ag-advania/melonPrimeDS`
- Branch: `develop_remakeVulkan_ver3`
- 基準HEAD: `75ce99d5afa2110b6ae3b676c7c059d9071145a8`
- 対象: Vulkan / DirectX 12 renderer・presenter・performance telemetry
- 目的: **通常配布buildでperformance telemetry由来のCPU/GPU overheadを完全に0へする**
- 重要方針: runtime `if (MELONPRIME_PERF)` だけに依存せず、shipping buildではcompile-timeでinstrumentationそのものを除去する

---

# 1. 背景

現在のVulkan / DX12 rendererには、performance調査のため次のtelemetryが追加されている。

```text
CPU timer
counter
GPU timestamp query
query resolve / readback
present wait timing
acquire timing
descriptor timing
raster timing
compositor timing
presenter timing
queue timing
```

---

これらは主に、

```text
MELONPRIME_PERF=1
```

というruntime環境変数で有効化される。

しかし現在の実装では、PERF OFF時にもhot path上に、

```cpp
DX12Perf::IsEnabled()
VulkanPerf::IsEnabled()
DX12Perf::AddCounter(...)
VulkanPerf::AddCounter(...)
DX12Perf::ScopedCpuTimer
VulkanPerf::ScopedCpuTimer
RecordDX12GpuMetric(...)
RecordVulkanGpuMetric(...)
Commands.WriteTimestamp(...)
Frames.WriteTimestamp(...)
```

などがソース上残る。

これらの内部で早期returnしていても、shipping binary上に、

```text
branch
load
function call / inline branch
constructor / destructor path
hot-path code-size増加
instruction-cache pressure
branch predictor entry消費
```

が残る可能性がある。

特にrendererは毎frameかつpolygon / variant / descriptor単位で呼ばれる経路があるため、単発では小さいcostでも累積してframe pacingへ影響する可能性を完全には否定できない。

---

# 2. 現在確認できている代表例

## 2.1 DX12

`DX12Perf::IsEnabled()`はcompile-time constantではなく、local staticへcacheしたruntime判定。

```cpp
inline bool IsEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_PERF");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}
```

また`DX12Renderer3D::BindSrvTable()`など、variant単位で呼ばれ得るhot pathの先頭にも、

```cpp
DX12Perf::ScopedCpuTimer descriptorTimer(...);
```

が存在する。

PERF OFFでもconstructor内部のruntime gate評価は残る。

さらにGPU timestampも、query自体が無効なら実GPU commandは発行されないが、

```cpp
Commands.WriteTimestamp(...)
```

の呼び出しと内部条件判定はshipping pathに残る。

---

## 2.2 Vulkan

`VulkanPerf::IsEnabled()`も同じruntime gate方式。

現在のコメントにも、

```text
a disabled runtime costs only one predictable branch
```

という前提がある。

つまり、設計として現在は「OFFなら低コスト」だが、

```text
OFFならzero cost
```

ではない。

Vulkan frame pathにも、

```cpp
VulkanPerf::SetScale(...)
VulkanPerf::ScopedCpuTimer(...)
RecordVulkanGpuMetric(...)
Frames.WriteTimestamp(...)
```

が存在する。

`FrameRing::WriteTimestamp()`も、

```cpp
if (!frame.Recording || !frame.TimestampQueriesEnabled)
    return;
```

というruntime check方式。

PERF OFFならquery pool自体は作られないためGPU command overheadは抑えられているが、CPU側のcall / branchまでは除去されていない。

---

# 3. 今回の目標

通常配布buildでは、

```text
DX12Perf
VulkanPerf
GPU timestamp instrumentation
CPU timer instrumentation
performance-only counters
performance-only Clock::now()
performance-only query/readback
```

を**compile-timeで完全除去する**。

目標は、

```text
MELONPRIME_PERF unset
```

のruntime OFFではなく、

```text
MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=OFF
```

のshipping buildにおいて、compilerがperformance instrumentationを一切生成しない状態。

---

# 4. 新しいbuild option

Qt/SDL側CMakeへ次を追加する。

```cmake
option(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY
    "Compile Vulkan/DX12 renderer performance telemetry instrumentation"
    OFF)
```

**defaultは必ずOFF。**

通常release / nightly / public buildはOFF。

performance調査専用buildでのみONにする。

ONの場合のみ、

```cmake
target_compile_definitions(core PRIVATE
    MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)

target_compile_definitions(melonDS PRIVATE
    MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)
```

を付与する。

VulkanとDX12で別々のoptionへ分割しない。

理由:

```text
backend比較時にinstrumentation条件を同一化するため
shipping buildのperformance contractを一本化するため
将来rendererが増えても共通gateとして利用できるため
```

---

# 5. 二段gate構成

## Compile-time gate

```text
MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY
```

## Runtime gate

compile-time telemetry buildの中だけ、

```text
MELONPRIME_PERF=1
```

を使う。

つまり、

```text
shipping build
  compile-time OFF
  -> telemetryコード自体なし

measurement build
  compile-time ON
  MELONPRIME_PERF unset
  -> telemetryコードは存在するがruntime OFF

measurement build
  compile-time ON
  MELONPRIME_PERF=1
  -> telemetry ON
```

とする。

---

# 6. DX12Perf.h の実装方針

## 6.1 compile-time OFF

`DX12Perf.h`では、

```cpp
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
```

の有効側に現在の本実装を置く。

無効側にはAPI互換のno-op implementationを置く。

ただし重要なのは、compilerが完全に消せる形にすること。

例:

```cpp
namespace melonDS::DX12Perf
{

inline constexpr bool IsEnabled() noexcept
{
    return false;
}

inline constexpr void SetScale(u32) noexcept {}
inline constexpr void AddCounter(Counter, u64 = 1) noexcept {}
inline constexpr void SetCounter(Counter, u64) noexcept {}
inline constexpr void MaybeReport() noexcept {}
inline constexpr void RecordNativeReadbackWait(u64) noexcept {}

class ScopedCpuTimer
{
public:
    constexpr explicit ScopedCpuTimer(CpuMetric, bool = true) noexcept {}
};

class ScopedRasterBeginWait
{
public:
    constexpr explicit ScopedRasterBeginWait(bool = true) noexcept {}
};

}
```

必要ならenum定義は共通側へ残してよい。

ただしshipping binaryで、

```text
std::getenv
std::chrono::steady_clock::now
SampleWindow
Percentile
fprintf
sorting
```

へ到達可能なperformance telemetry codeを生成しないこと。

---

# 7. VulkanPerf.h の実装方針

DX12Perfと同じcompile-time gateへ統一する。

現在の、

```text
runtime gate deliberately does not use developer-feature define
```

という思想は変更する。

新方針:

```text
frontend-only developer flagには依存しない
しかしcore/frontend両targetに同じ専用perf compile definitionを与える
```

とする。

つまり、

```text
MELONPRIME_ENABLE_DEVELOPER_FEATURES
```

とは分離したまま、

```text
MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY
```

をcore / frontend共通に与える。

これならinline ODR差も発生しない。

compile-time OFF側では、

```cpp
VulkanPerf::IsEnabled() == constexpr false
AddCounter() == no-op
SetCounter() == no-op
ScopedCpuTimer == empty
ScopedRasterBeginWait == empty
MaybeReport() == no-op
```

とする。

---

# 8. GPU timestamp instrumentationの完全除去

## DX12

shipping buildでは、

```text
D3D12_QUERY_HEAP_TYPE_TIMESTAMP
CreateQueryHeap
TimestampReadback
EndQuery
ResolveQueryData
GetTimestampFrequency
Map / Unmap timestamp readback
```

をperformance telemetry目的では一切生成しない。

`DX12CommandContext`について、compile-time OFF時は可能なら、

```text
TimestampQueryHeap
TimestampReadback
TimestampFrequency
TimestampWrittenMask
TimestampSnapshotValues
TimestampSnapshotValid
```

などperformance-only member自体もclass layoutから除去する。

最低でもshipping code pathから完全dead-code eliminationされること。

### WriteTimestamp

compile-time OFF時は、

```cpp
inline constexpr void WriteTimestamp(u32) noexcept {}
```

相当まで落とせる設計にする。

単に中でruntime `if (!TimestampQueriesEnabled) return;` にしない。

---

## Vulkan

shipping buildでは、performance telemetry目的の、

```text
VkQueryPool
vkCmdWriteTimestamp
vkGetQueryPoolResults
vkCmdResetQueryPool
TimestampQueryPool
TimestampPeriodNs
TimestampQueriesEnabled
```

を可能な限り生成しない。

`FrameRing::Create()`で現在行っている、

```cpp
const bool timestampSupport = VulkanPerf::IsEnabled() && ...;
```

もcompile-time OFF時にはコード自体を除去する。

例:

```cpp
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    // timestamp support detection
#endif
```

またはconstexpr分岐でcompilerが確実に消せる構成にする。

`FrameRing::WriteTimestamp()`もshipping buildでは完全no-op化する。

---

# 9. Clock::now()の監査

compile-time OFF時に、performance測定だけを目的とする、

```cpp
std::chrono::steady_clock::now()
```

がhot pathへ残ってはいけない。

特に次を全件監査する。

```text
DX12 presenter descriptor timing
Vulkan presenter descriptor timing
acquire wait telemetry
present fence wait telemetry
queue submit timer
descriptor timer
raster timer
compositor timer
hud timer
```

現在のように、

```cpp
const auto start = Clock::now();
...
if (Perf::IsEnabled())
    AddCounter(... now() - start);
```

となっている箇所はNG。

必ず、

```cpp
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    ...
#endif
```

またはcompile-time dead-code elimination可能なhelperで囲う。

---

# 10. hot path重点監査対象

## DX12

最低限次を監査する。

```text
GPU3D_DX12.cpp
DX12Context.cpp
GPU_DX12.cpp
GPU3D_TexcacheDX12.cpp
MelonPrimeDX12SurfacePresenter.cpp
DX12NvidiaReflex.cpp
DX12AmdAntiLag2.cpp
DX12IntelXeLL.cpp
```

特に、

```text
RenderFrame
BindSrvTable
BindFrameUavTable
BindStaticSrvTable
BindCompositionUavTable
ComposeStructuredOutput
CaptureSidecar
BeginFrame
EndFrame
Present
```

を重点確認する。

---

## Vulkan

最低限次を監査する。

```text
GPU3D_Vulkan.cpp
VulkanSync.cpp
GPU_Vulkan.cpp
GPU3D_TexcacheVulkan.cpp
MelonPrimeVulkanPresenter.cpp
VulkanPresentPacer.cpp
VulkanNvidiaReflex.cpp
VulkanAmdAntiLag.cpp
```

特に、

```text
RenderFrame
FrameRing::BeginFrame
FrameRing::SubmitFrame
FrameRing::WriteTimestamp
ComposeStructuredOutput
CaptureSidecar
VulkanPresenter::BeginLowLatencyFrame
VulkanPresenter::BeginFrame
VulkanPresenter::EndFrame
VulkanPresenter::Present
```

を重点確認する。

---

# 11. low-latency機能は絶対にcompile-outしない

今回compile-timeで除去するのは**計測機能だけ**。

次はshipping buildでも必ず維持する。

## NVIDIA

```text
DX12 NVIDIA Reflex
Vulkan VK_NV_low_latency2
sleep
marker
present ID
On / On+Boost
```

## AMD

```text
DX12 AMD Anti-Lag 2
Vulkan AMD Anti-Lag
```

## Intel

```text
DX12 XeLL
```

## generic pacing

```text
DXGI SetMaximumFrameLatency(1)
DXGI frame-latency waitable object
Vulkan present_wait2
legacy present_wait
latest submitted fence fallback
bounded acquire A/B
presenter frames-in-flight A/B
swapchain image-count A/B
```

これらはtelemetryではなく実際のruntime機能なので、compile-time perf gateへ入れてはいけない。

---

# 12. correctness instrumentationとの区別

次を混同しない。

```text
performance-only counter
performance-only timestamp
performance-only percentile report
```

は除去対象。

一方、

```text
runtime error checking
VK_CHECK
HRESULT check
renderer failure logging
swapchain lifecycle logging
fatal validation
```

は除去しない。

Releaseでも必要なcorrectness / diagnostic contractは維持する。

---

# 13. CMake構成

推奨:

```cmake
option(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY
    "Compile Vulkan/DX12 renderer performance telemetry instrumentation"
    OFF)

if (MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    target_compile_definitions(core PRIVATE
        MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)
    target_compile_definitions(melonDS PRIVATE
        MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=1)

    message(STATUS
        "MelonPrime renderer performance telemetry: COMPILED IN. "
        "Set MELONPRIME_PERF=1 at runtime to enable collection.")
else()
    message(STATUS
        "MelonPrime renderer performance telemetry: COMPILED OUT")
endif()
```

---

# 14. public/nightly build contract

GitHub Actions / nightly / release buildについて、

```text
MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=OFF
```

であることを保証する。

workflowで明示的にONにしない限りdefault OFFでよい。

ただしCI auditを追加し、release/nightly CMake commandに誤って、

```text
-DMELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=ON
```

が入らないことを確認する。

---

# 15. static audit追加

`tools/ci/audits/`へauditを追加する。

例:

```text
audit-renderer-perf-zero-overhead.py
```

最低限確認すること。

```text
DX12Perf.hにcompile-time gateが存在
VulkanPerf.hに同じcompile-time gateが存在
core / melonDS両方へ同じdefinitionが付く
shipping default OFF
DX12 GPU timestamp作成がcompile-time gate配下
Vulkan GPU timestamp作成がcompile-time gate配下
performance-only Clock::now()がunguardedでhot pathに残っていない
```

---

# 16. binary-level検証を必須にする

ソース上`#ifdef`があるだけではPASSにしない。

Release / telemetry OFF buildのbinaryを確認する。

## DX12

最低限、disassembly / symbol / strings等で、

```text
DX12Perf
MELONPRIME_PERF
ResolveQueryData(timestamp用途)
GetTimestampFrequency(perf用途)
```

がhot pathへ残っていないことを確認する。

特に、

```text
DX12Renderer3D::BindSrvTable
DX12Renderer3D::RenderFrame
DX12SurfacePresenter::BeginFrame
DX12SurfacePresenter::EndFrame
```

をdisassembleして、telemetry branch / timer codeがないことを確認する。

---

## Vulkan

最低限、

```text
VulkanPerf
MELONPRIME_PERF
vkCmdWriteTimestamp(perf用途)
vkGetQueryPoolResults(perf用途)
```

がshipping hot pathへ残っていないことを確認する。

特に、

```text
VulkanRenderer3D::RenderFrame
FrameRing::BeginFrame
FrameRing::WriteTimestamp
VulkanPresenter::BeginFrame
VulkanPresenter::BeginLowLatencyFrame
```

を確認する。

---

# 17. A/Bテスト

## DX12

```text
A: 75ce99d5 / Release / current implementation / MELONPRIME_PERF unset
B: new HEAD / Release / compile-time telemetry OFF
C: new HEAD / Release / compile-time telemetry ON / MELONPRIME_PERF unset
D: new HEAD / Release / compile-time telemetry ON / MELONPRIME_PERF=1
```

比較:

```text
frame time p50
frame time p95
frame time p99
frame time max
input-to-present feel
stutter count
present slot wait
GPU queue span
CPU utilization
```

最重要比較は、

```text
A vs B
```

Bで体感が戻るなら、runtime-disabled telemetry overheadが原因だった可能性が高い。

---

## Vulkan

同様に、

```text
A: 75ce99d5 / Release / current implementation / MELONPRIME_PERF unset
B: new HEAD / Release / compile-time telemetry OFF
C: new HEAD / Release / compile-time telemetry ON / MELONPRIME_PERF unset
D: new HEAD / Release / compile-time telemetry ON / MELONPRIME_PERF=1
```

で比較する。

Vulkan / DX12のbackend比較では、必ず、

```text
両方B
```

つまりcompile-time telemetry OFF同士を使う。

---

# 18. acceptance criteria

## 必須

```text
Release / normal buildでMELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=OFF
DX12Perf hot-path branchがshipping binaryから消える
VulkanPerf hot-path branchがshipping binaryから消える
DX12 timestamp query instrumentationがshipping binaryから消える
Vulkan timestamp query instrumentationがshipping binaryから消える
performance-only Clock::now()がshipping hot pathから消える
MELONPRIME_PERF環境変数をshipping buildで設定しても何も起きない
Reflex / Anti-Lag / XeLL / generic pacing挙動は変化しない
Software / OpenGL / Metalへ影響しない
```

## build

```text
Windows Release DX12 ON
Windows Release Vulkan ON
Windows Debug
Linux Vulkan
macOS Vulkan/MoltenVK
BSD Vulkan
```

既存build matrixを壊さない。

---

# 19. correctness regression test

最低限、

```text
Software renderer differential
OpenGL Compute reference differential
1x pixel correctness
4x / 8x / 16x presentation
Display Capture
savestate load
renderer switch
resize
minimize / restore
fullscreen toggle
VSync ON / OFF
Reflex Off / On / On+Boost
Anti-Lag OFF / ON
```

を既存test / auditで維持する。

telemetry compile-outでrendering semanticsが変わってはいけない。

---

# 20. 禁止事項

次は禁止。

```text
IsEnabled()へ[[likely]]を付けるだけ
branch predictor任せにして終了
MELONPRIME_PERFのgetenv結果cacheだけで終了
CPU timerだけ消してGPU timestampを残す
DX12だけ対応してVulkanを残す
Vulkanだけ対応してDX12を残す
Reflex / Anti-Lag / XeLLをperf telemetryと一緒にcompile-out
normal runtime error loggingを削除
releaseでdeveloper features全体を無効化することで代用
```

今回の目的は、

```text
performance telemetryだけを正確にcompile-out
```

すること。

---

# 21. 推奨実装順

```text
1. CMake共通option追加
2. DX12Perf.h compile-time no-op化
3. VulkanPerf.h compile-time no-op化
4. DX12 timestamp class/member compile gate
5. Vulkan timestamp pool/member compile gate
6. performance-only Clock::now()全件guard
7. static audit追加
8. Release build
9. binary/disassembly確認
10. DX12 A/B
11. Vulkan A/B
12. Vulkan vs DX12 telemetry-OFF同士で再比較
```

---

# 22. 最終設計

最終的に、通常利用者が受け取るbinaryは、

```text
Renderer functionality
+ Low-latency functionality
+ correctness checks
```

のみを含み、

```text
performance measurement observer
```

は含まない構成にする。

性能調査時だけ、

```text
-DMELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=ON
```

で専用binaryを作り、そのbinary内でさらに、

```text
MELONPRIME_PERF=1
```

を指定して測定する。

これにより、

```text
shipping performance
measurement instrumentation
```

を完全に分離できる。

---

# 23. 完了判定

```text
PASS条件:

Vulkan / DX12ともにshipping buildでtelemetryがcompile-time完全除去される
binary-levelでhot pathにperf branch / timer / timestamp instrumentationがない
low-latency機能とrendering correctnessは維持
A/Bでperformance regressionがない、または改善する
```

---

# 24. 実装後完了報告（2026-08-15）

## 実装結果

以下を実装した。

- `src/frontend/qt_sdl/CMakeLists.txt` に共通option
  `MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY` を追加。defaultは `OFF`。
- `core` と `melonDS` へ、optionが `ON` の場合だけ同じcompile definitionを付与。
  `MELONPRIME_ENABLE_DEVELOPER_FEATURES` とは独立している。
- `DX12Perf.h` / `VulkanPerf.h` を共通compile-time gateで分離し、OFF側を
  `constexpr` no-op facade化。OFF側には `getenv`、chrono timer、report、percentile
  処理を生成しない。
- DX12 query heap/readback/frequency/resolveと、Vulkan query pool/reset/write/readbackを
  compile-time gate化。OFF側では timestamp member と GPU timestamp helper も除去。
- Vulkan `DeviceDispatch` の query function pointer/load と、query pool用の遅延破棄enumも
  同じgateへ揃え、OFF binaryから query API loader 残存も除去した。
- DX12/Vulkan presenter、GPU raster/capture、present pacer の
  performance-only `Clock::now()` を全件gate化。
- Reflex、Anti-Lag、XeLL、DXGI frame-latency waitable object、Vulkan present wait／
  fence pacing、correctness loggingはTelemetry gateの外に残した。
- `tools/ci/audits/audit-renderer-perf-zero-overhead.py` を追加し、Windows / Ubuntu /
  macOS / BSD workflowから実行するようにした。workflowからTelemetryをONにする指定はない。

## 実行した検証

基準はこの文書冒頭の `75ce99d5afa2110b6ae3b676c7c059d9071145a8`。変更は未commit・未pushのまま検証した。

```text
python tools/ci/audits/audit-renderer-perf-zero-overhead.py
PASS: Vulkan/DX12 renderer telemetry compile-time zero-overhead contract

git diff --check
PASS（改行コードのLF->CRLF警告のみ。whitespace errorなし）
```

### Windows Release / Telemetry OFF

ビルドディレクトリ: `build/telemetry-off-release`

```text
CMAKE_BUILD_TYPE=Release
BUILD_STATIC=ON
MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF
MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=OFF
MELONPRIME_ENABLE_DX12=ON
MELONPRIME_ENABLE_VULKAN=ON
MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=OFF
```

CMakeは `COMPILED OUT` を表示し、`[326/326]` でリンクまで完了した。既存の
Vulkan present timing、Vulkan present pacer fake-dispatch、Intel XeLL fake API
state-machineの3テストもPASS。

### Windows Release / Telemetry ON

ビルドディレクトリ: `build/telemetry-on-release`

同じ構成で `MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=ON` とし、CMakeは
`COMPILED IN` を表示。`[326/326]` でリンクし、同じ3テストがPASSした。

### Windows Debug / Telemetry OFF

ビルドディレクトリ: `build/telemetry-off-debug`

`CMAKE_BUILD_TYPE=Debug`、DX12/Vulkan ON、Developer features OFF、Telemetry OFFで
`[326/326]` 完了。上記3テストもPASSした。

## binary-level証跡

Release executableのサイズは次の通り。

```text
telemetry-off-release/melonPrimeDS.exe  62,292,992 bytes
telemetry-on-release/melonPrimeDS.exe   62,301,184 bytes
```

OFF Release executableをraw string scanした結果、
`MELONPRIME_PERF`、`DX12Perf`、`VulkanPerf`、`GpuTimestamps`、`TimestampQuery`、
`vkCreateQueryPool`、`vkCmdWriteTimestamp`、`vkGetQueryPoolResults` などのTelemetry関連
文字列はいずれも0件。ON Release executableではこれらの文字列が検出された。

core objectでも、OFF側の `DX12Context.cpp.obj`（160,891 bytes）と
`VulkanSync.cpp.obj`（159,059 bytes）には timestamp/query symbol がなく、ON側には
`DX12CommandContext::WriteTimestamp`、`ReadTimestampSnapshot`、
`ReadTimestampSpanNanoseconds`、`FrameRing::WriteTimestamp` などが存在した。

したがって、shipping想定のcompile-time OFFでは、`MELONPRIME_PERF` を設定しても
renderer Telemetryを有効化するコードは存在しない。性能調査用だけが
`-DMELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=ON` と `MELONPRIME_PERF=1` の二段構成になる。

## runtime／platform境界

- `C:\DSMPH\melonPrimeDS\all roms\allRoms` は存在確認済み。ユーザー指定のUSA Rev.1
  ROMと、F3/F4/F7に対応する `.ml3` / `.ml4` / `.ml7` ファイルも確認した。
- ただし、この実装ターンではDX12/Vulkanを実GPU上で起動してUSA Rev.1をロードし、
  F3/F4/F7を使ったA/B/C/Dの物理runtime比較までは実施していない。したがって、
  freeze非発生、frame-time、GPU timestamp値、性能regressionについては **NOT RUN / OPEN**。
- Linux Vulkan、macOS Vulkan/MoltenVK、BSD Vulkanの実ビルド・実GPU確認もこのWindows環境では
  **NOT RUN / OPEN**。各workflowには静的監査だけを追加済みで、CI結果を別platformの代用にはしない。

## 完了判定

```text
compile-time gate / no-op facade       PASS
DX12/Vulkan timestamp compile-out      PASS
performance-only Clock::now() audit    PASS
Windows Release OFF/ON build           PASS
Windows Debug OFF build                PASS
static audit CI wiring                 PASS
physical DX12/Vulkan A/B/C/D            NOT RUN / OPEN
cross-platform build/runtime            NOT RUN / OPEN
```

実装・静的監査・Windowsビルドの範囲は完遂。物理GPUでしか得られないA/Bと、Windows外の
platform evidenceは未実施のまま明示的に保留する。
