# Vulkan S80 監査・フェーズ別修正指示書
## ROM起動直後 ACCESS_VIOLATION 継続
## First HBlank完走後にtraceが消える観測欠陥
## 固定RVA symbolization・event/JIT境界特定
## Sapphire GPU2D本体を推測修正しない

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `97c802422a718f9e783385b44b06ba9fc96954e7`  
**HEADコミット:** `S79: Fix HBlank trace logging and runtime git identity fallback`  
**前回監査HEAD:** `b7037ef6e6c89c185135c26470d4a2366efc0661`  
**差分:** 8 commits ahead / 0 behind  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

## 進捗 (S80)

| Phase | Status | Commit | Notes |
|-------|--------|--------|-------|
| S80-1 | done | `c70b41579` | consumeBudget moved to EmuThread; rawLog; state in .cpp |
| S80-2 | done | `6703ae47f` | ACCESS_VIOLATION operands, full x64 registers, trace ring dump |
| S80-3 | done | `34a565e7a` | Diagnostic symbols option + symbolize scripts |
| S80-4 | done | `6703ae47f` | Per-frame abs/module/base/RVA (with S80-2) |
| S80-5 | done | `c3ba3f648` | StartScanline + event ring + CPU slice traces |
| S80-6 | done | `34a565e7a` | `generate_build_identity.py` in incremental build |
| S80-7 | done | `8ba568f19` | Framebuffer canary guard |
| S80-8 | done | `b0abf6747` | Exact-pin GPU2D A/B build target |
| S80-9 | partial | `277e304aa` | Publish deferred; crash persists at RVA ~0xF941 read [-1] post-FinishFrame |
| S80-10 | pending | | blocked on cold-start green |

---

# 0. 結論

今回のログにより、S79で最有力候補としていた次の区間はすべて正常通過した。

```text
DrawScanline Unit A
DrawScanline Unit B
DrawSprites Unit A
DrawSprites Unit B
post-sprite path latch
HBlank DMA
HBlank IRQ
LCD event scheduling
StartHBlank(0)末尾
```

実ログ:

```text
[FirstGpuLine] after path recheck result=1
[FirstGpuLine] before CheckDMAs HBlank VCount=0 line=0
[FirstGpuLine] after CheckDMAs HBlank VCount=0 line=0
[FirstGpuLine] before IRQ HBlank VCount=0 line=0
[FirstGpuLine] after IRQ HBlank VCount=0 line=0
[FirstGpuLine] before ScheduleEvent HBlank VCount=0 line=0
[FirstGpuLine] after ScheduleEvent HBlank VCount=0 line=0
[FirstGpuLine] StartHBlank done VCount=0 line=0
ACCESS_VIOLATION
```

したがって:

```text
S79のpost-sprite runtime gate仮説は否定
```

された。

ただし、クラッシュが`StartHBlank()`の直後だと断定してはならない。

理由は、最後のログの直後に:

```cpp
FirstVulkanFrameTrace::consumeBudget();
```

を呼び、以後のfirst-frame traceをすべて無効化しているため。

つまり次のどこで落ちても、現在のログは同じ終端になる。

```text
consumeBudget()
event callback return
CPU/JIT再開
StartScanline(1)
CheckWindows(1)
HBlank DMA mode 3
StartHBlank(1)
DrawScanline(1)
DrawSprites(2)
DisplayFIFO event
その他のNDS event
```

今回の最優先課題はGPU2Dコードをさらに推測変更することではない。

最優先は:

```text
1. traceをfirst RunFrame完了まで維持
2. 固定RVAを正しいMinGW symbolsへ解決
3. ACCESS_VIOLATIONのfault target addressを記録
```

である。

---

# 1. 今回の確定事項

## 1.1 first HBlankは完走

次はすべて完了している。

```text
Unit A line 0
Unit B line 0
Unit A sprites line 1
Unit B sprites line 1
DMA HBlank
IRQ HBlank
ScheduleEvent LCD_StartScanline
```

`SapphireGpu2DHBlankCore.inc`の実処理列は正常に最後まで到達した。

## 1.2 同一binaryで決定的に再現

添付された2件のcrash report:

```text
runId=89524298317825
runId=55679956025345
```

は別runだが、次が完全一致している。

```text
binarySha256=
4f764ed35b3678f208689e705804ee94f3cd5d4ea6f6767aaab171b41d14954e

exception.code=0xC0000005
exceptionRva=0xF901

#00 0x00007FF6CCA2F901
#01 0x00007FF6CCBF515E
#02 0x00007FF6CCBFA6CB
#03 0x00007FF6CCA8C698
#04 0x00007FF6CCA91873
```

したがって、偶発的raceより:

```text
同一命令列で発生する決定的fault
```

の可能性が高い。

## 1.3 module-relative RVA

module base:

```text
0x00007FF6CCA20000
```

各frame RVA:

```text
fault/#00 = 0x0000F901
#01       = 0x001D515E
#02       = 0x001DA6CB
#03       = 0x0006C698
#04       = 0x00071873
```

この5個を正しい同一binaryへ`addr2line`すれば、
推測ではなくfunction/file/lineを取得できる。

## 1.4 console logとreportは同一runではない

console:

```text
runId=120516782325761
```

添付report:

```text
89524298317825
55679956025345
```

同一binary SHAなので再現性の証拠には使えるが、
consoleの厳密な同一run stackではない。

## 1.5 build identityはまだunknown

```text
commit=unknown
commitFull=unknown
branch=unknown
```

S79のruntime `.git`探索は、
exeがrepository外の:

```text
C:\DSMPH\melonPrimeDS2\
```

へコピーされているため失敗している。

build identityはruntime探索ではなく、
build/link時に必ず埋め込む必要がある。

---

# 2. 最後のログ直後の実コード

最新HEADの`GPU::StartHBlank()`末尾:

```cpp
if (useSapphire2D && line <= 2)
{
    FirstVulkanFrameTrace::log(
        "[FirstGpuLine] after ScheduleEvent ...");
    FirstVulkanFrameTrace::log(
        "[FirstGpuLine] StartHBlank done ...");
}

if (useSapphire2D && line == 0)
    FirstVulkanFrameTrace::consumeBudget();
```

`consumeBudget()`:

```cpp
inline void consumeBudget() noexcept
{
    budgetActive() = false;
}
```

`budgetActive()`:

```cpp
inline bool& budgetActive() noexcept
{
    static bool active = true;
    return active;
}
```

正常なmemoryなら単純なbool writeである。

そのため候補は2つ。

```text
A. consumeBudget自体／function-local staticのwriteでfault
B. consumeBudgetで観測が停止し、その後の別処理でfault
```

現在のログだけではA/Bを分離できない。

---

# 3. P0
# trace budgetをGPU timing内で終了してはいけない

固定SapphireのGPU timingには、
Desktop診断用trace budgetの状態変更は存在しない。

現在はGPU event callbackの末尾で:

```text
診断global stateを書き換える
```

というDesktop固有処理が入っている。

これは最終実装として不要。

## 即時修正

`GPU::StartHBlank()`から次を削除。

```cpp
FirstVulkanFrameTrace::consumeBudget();
```

trace終了はEmuThread側の:

```text
[RomBootTrace] first RunFrame complete
```

の直後へ移す。

例:

```cpp
nds->RunFrame();

FirstVulkanFrameTrace::log(
    "[RomBootTrace] first RunFrame complete\n");

FirstVulkanFrameTrace::consumeBudget();
```

これによりfirst frame全体を観測できる。

## 完了条件

```text
GPU.cpp内のconsumeBudget呼出し = 0
first RunFrame完了まではtrace active
```

---

# 4. P0
# consumeBudget前後をraw traceで分離

移動前の一回限りの診断として:

```cpp
FirstVulkanFrameTrace::rawLog(
    "[FirstGpuLine] before consumeBudget\n");

FirstVulkanFrameTrace::consumeBudget();

FirstVulkanFrameTrace::rawLog(
    "[FirstGpuLine] after consumeBudget\n");
```

`rawLog`はbudgetを参照してはいけない。

結果判定:

```text
beforeのみ:
    consumeBudget／static state writeが直接fault候補

afterまで:
    faultはその後
```

ただし根本修正はGPU coreからconsumeを除去すること。

---

# 5. P0
# StartScanlineと次HBlankをfirst-frame全域traceする

次の境界を追加。

## StartScanline

```text
StartScanline enter
after VCount update
before/after CheckWindows Unit A
before/after CheckWindows Unit B
before/after DMA mode 3
before/after VBlankEnd renderer
before/after Unit A VBlankEnd
before/after Unit B VBlankEnd
before/after DisplayFIFO scheduling
before/after VCount match IRQ
before/after HBlank event scheduling
StartScanline done
```

## StartHBlank

line 0だけで終了せず:

```text
line 0
line 1
line 2
line 3
```

まで追跡。

## RunFrame/Event dispatcher

```text
before event callback
event id
event param
callback object pointer
after event callback
before CPU/JIT resume
after CPU/JIT slice
```

## ログ共通field

```text
seq
threadId
eventId
line
VCount
CPU timestamp
```

---

# 6. P0
# crash時にtrace ringをdumpする

大量の`printf`はtimingを変える。

最終診断方式は固定長ring buffer。

例:

```cpp
struct TraceRecord
{
    u64 sequence;
    u32 threadId;
    u32 eventId;
    u32 line;
    u32 vcount;
    const char* marker;
};
```

要件:

```text
- allocationなし
- mutexなし
- fixed capacity
- atomic write index
- crash handlerが最後の256件をreportへ出力
```

このringはDesktop診断層へ置く。

Sapphire GPU2D本体へ持ち込まない。

---

# 7. P0
# ACCESS_VIOLATIONのfault targetを記録する

WindowsのACCESS_VIOLATIONでは:

```text
ExceptionInformation[0]
    0 = read
    1 = write
    8 = execute

ExceptionInformation[1]
    fault target address
```

現在のreportはこれを保存していない。

追加:

```cpp
if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
    && record->NumberParameters >= 2)
{
    fprintf(
        out,
        "exception.accessType=%llu "
        "exception.faultAddress=0x%016llX\n",
        record->ExceptionInformation[0],
        record->ExceptionInformation[1]);
}
```

これにより:

```text
read from near-null
write to readonly/corrupt pointer
execute invalid function pointer
```

を即座に区別できる。

---

# 8. P0
# 全registerを保存する

現在は:

```text
RIP RSP RBP RAX
```

だけ。

最低追加:

```text
RBX RCX RDX RSI RDI
R8 R9 R10 R11 R12 R13 R14 R15
EFlags
```

理由:

```text
RAX=-1
RBP=0x608
```

だけではfaulting operandを復元できない。

fault instructionをdisassembleし、
全registerから実効addressを計算する必要がある。

---

# 9. P0
# MinGW Releaseをaddr2line可能にする

DbgHelpはMinGW DWARFを完全には解決しない。

現在の:

```text
NeAACDecAudioSpecificConfig+0x7D8EA4
```

は信頼できるfunction名ではない。

## build要件

Release診断build:

```text
-g
-fno-omit-frame-pointer
-Wl,-Map,melonPrimeDS.map,--cref
```

binaryを配布用にstripする場合も:

```bash
objcopy --only-keep-debug melonPrimeDS.exe melonPrimeDS.exe.debug
objcopy --strip-debug melonPrimeDS.exe
objcopy --add-gnu-debuglink=melonPrimeDS.exe.debug melonPrimeDS.exe
```

CI artifact:

```text
melonPrimeDS.exe
melonPrimeDS.exe.debug
melonPrimeDS.map
binary.sha256
build-identity.txt
```

## symbolization

同一SHA binaryでpreferred ImageBaseを取得。

```bash
objdump -p "melonPrimeDS v3.4.3 v85.exe" | grep ImageBase
```

仮にImageBaseが:

```text
0x140000000
```

なら:

```text
0x14000F901
0x1401D515E
0x1401DA6CB
0x14006C698
0x140071873
```

を解決。

```bash
addr2line -e "melonPrimeDS v3.4.3 v85.exe" \
  -f -C -p \
  0x14000F901 \
  0x1401D515E \
  0x1401DA6CB \
  0x14006C698 \
  0x140071873
```

split debug使用時:

```bash
addr2line -e melonPrimeDS.exe.debug ...
```

結果をcrash reportへ自動追記する。

---

# 10. P0
# current stack writerを修正する

現在のstack report問題:

```text
- frameごとのmodule base/RVAがない
- symbolが見つかった場合にabsolute addressを出さない
- MinGW DWARF fallbackがない
- fault access addressがない
- binaryのpreferred ImageBaseがない
```

各frameは必ず:

```text
absolute
module
moduleBase
RVA
symbol
file
line
```

を出す。

例:

```text
#00 abs=... module=melonPrimeDS.exe rva=0xF901
    symbol=...
    file=... line=...
```

---

# 11. P0
# build identityはruntime探索で直さない

最新commitは`unknown`時に、
exe directoryから上へ`.git`を探す。

これはportable/copy済みbuildで必ず失敗する。

## 正しい方法

build時生成headerを、
configure時ではなく各build時に更新。

例:

```cmake
add_custom_command(
    OUTPUT MelonPrimeGitBuildIdentity.h
    COMMAND ${Python3_EXECUTABLE}
        tools/generate_build_identity.py
        --repo ${CMAKE_SOURCE_DIR}
        --output ${GENERATED_HEADER}
    DEPENDS
        ${CMAKE_SOURCE_DIR}/.git/HEAD
        ${CMAKE_SOURCE_DIR}/.git/index
)
```

またはbuild scriptが直接:

```text
commit full
commit short
branch
dirty
```

をcompiler definitionとして渡す。

## 完了条件

repository外へexeをコピーしても:

```text
commit != unknown
branch != unknown
```

---

# 12. Sapphire GPU2D監査結果

## 12.1 HBlank GPU2D event block

生成blockの実処理:

```text
DrawScanline A
DrawScanline B
DrawSprites A
DrawSprites B
CheckDMAs
```

は固定Sapphireと一致。

今回のログでも完走。

このblockを再設計しない。

## 12.2 DrawScanline framebuffer契約

accelerated stride:

```text
256 * 3 + 1 = 769 u32
```

1 line:

```text
0..767  : BGOBJ structured payload
768     : metadata
```

GPU allocation:

```text
769 * 192 u32
```

境界計算自体は一致している。

## 12.3 localと固定Sapphire

`DrawScanline()`本体は、
Desktop traceとcurrent renderer存在guardを除き、
固定Sapphireの処理列と実質一致している。

現時点で:

```text
GPU2D_Soft.cppへ新しいheuristic修正を追加しない
```

こと。

---

# 13. ただしmemory corruptionはguardで検証する

first HBlankが完走しても、
描画中のout-of-boundsが後で発火する可能性は残る。

Sapphire本体を改造せず、
Framebuffer allocation境界で検査。

## guard allocation

各Framebuffer plane:

```text
guard page
payload
guard page
```

または:

```text
prefix canary
payload
suffix canary
```

検査点:

```text
after Unit A DrawScanline
after Unit B DrawScanline
after Unit A DrawSprites
after Unit B DrawSprites
after StartHBlank
before StartScanline(1)
```

canary破壊時:

```text
buffer index
plane
first damaged offset
expected
actual
```

を記録して即abort。

## 禁止

```text
GPU2D_Soft.cpp内部配列へ独自sentinel memberを追加
```

Sapphire class layoutを変えない。

---

# 14. exact Sapphire A/B build

推測を排除するため、
同一Desktop adapterへ2 targetを用意。

## Target A

```text
pinned Sapphire GPU2D source exact
```

## Target B

```text
current generated/normalized source
```

同一:

```text
ROM
Vulkan 3D
Framebuffer allocator
event lifecycle
frontend
```

でcold-start比較。

結果:

```text
Aも同じRVAでcrash:
    adapter/event/JIT側

Aはgreen、Bだけcrash:
    local GPU2D差分
```

最終的にはAをproduction sourceにする。

Android/Desktop差はcompat headerとWSI層だけに置く。

---

# 15. StartScanlineで特に確認する箇所

first HBlank後に予定されている次イベント:

```text
LCD_StartScanline line=1
```

`StartScanline(1)`では:

```text
VCount++
GPU2D_A.CheckWindows
GPU2D_B.CheckWindows
DMA mode 3条件
VCount match
LCD_StartHBlank scheduling
```

を実行。

この全境界をtraceする。

特に:

```text
GPU2D_A.CheckWindows(1)
GPU2D_B.CheckWindows(1)
```

が固定Sapphire Unitとlocal event timingで正しいことを確認。

ただしsymbolization前にここを改変しない。

---

# 16. CPU/JIT境界のA/B

GPU callback完了後、
CPU/JITへ戻った直後に落ちる可能性もある。

診断matrix:

```text
Vulkan + JIT ON
Vulkan + JIT OFF
Software + JIT ON
Software + JIT OFF
```

判定:

```text
Vulkanのみ:
    GPU/Vulkan activationに依存

JIT ONのみ:
    JIT lifecycle／patch ordering

Vulkan + JIT ONのみ:
    renderer activation時のJIT reset/patch interaction

全backend:
    ROM/CPU一般問題
```

fallbackとして採用するのではなく、
根本位置を分離する一回限りの診断。

---

# 17. FirstVulkanFrameTrace設計修正

現在:

```cpp
inline bool& budgetActive()
{
    static bool active = true;
}
```

問題:

```text
- header inline function-local mutable static
- hot event callbackから書換え
- thread ownership不明
- first HBlankで観測終了
```

最終設計:

```text
- stateは1つの.cppで定義
- atomic phase enum
- GPU coreはread-only trace hookのみ
- phase終了はEmuThread
```

例:

```cpp
enum class FirstFrameTracePhase : u8
{
    Disabled,
    Boot,
    FirstRunFrame,
    FirstPresent
};
```

GPU event callbackはphaseを終了させない。

---

# 18. cold-start regression test修正

現在の成功testへ追加するcheckpoint:

```text
StartHBlank line 0 done
StartScanline line 1 enter
StartScanline line 1 done
StartHBlank line 1 enter
StartHBlank line 1 done
first RunFrame complete
producerEnd
queuePush
present
```

失敗時artifact:

```text
exact run console
exact run crash report
exact run minidump
binary
debug symbols
map
trace ring
```

同一run IDのみbundleする。

---

# 19. フェーズ別修正指示

---

## S80-1 — trace budgetをfirst RunFrame末尾へ移動

### 作業

```text
GPU::StartHBlankからconsumeBudget削除
EmuThread first RunFrame complete後へ移動
before/after consume raw marker
```

### 完了条件

```text
line 1以降のtraceが出る
GPU event coreにtrace state writeがない
```

### 推奨コミット

```text
S80-1: Keep first-frame tracing active through RunFrame completion
```

---

## S80-2 — ACCESS_VIOLATION target address記録

### 作業

```text
ExceptionInformation[0]
ExceptionInformation[1]
全x64 register
```

### 完了条件

```text
read/write/execute区分
fault target address
```

### 推奨コミット

```text
S80-2: Record Windows access-violation operands and full registers
```

---

## S80-3 — MinGW map/debug artifact生成

### 作業

```text
-g
-fno-omit-frame-pointer
linker map
split debug
SHA manifest
```

### 完了条件

5 RVAすべてが:

```text
function
file
line
```

へ解決。

### 推奨コミット

```text
S80-3: Produce addr2line-ready MinGW release symbols and linker maps
```

---

## S80-4 — crash handlerのRVA出力完全化

### 作業

各frameへ:

```text
absolute/module/base/RVA
```

を必ず出力。

### 完了条件

外部計算なしで全frame RVA取得可能。

### 推奨コミット

```text
S80-4: Emit module-relative addresses for every crash frame
```

---

## S80-5 — StartScanline・event dispatcher trace ring

### 作業

```text
StartScanline全境界
event callback enter/exit
CPU/JIT resume
fixed ring
crash dump
```

### 完了条件

fault直前の最後のevent/phaseを確定。

### 推奨コミット

```text
S80-5: Trace the first-frame event and CPU-resume lifecycle
```

---

## S80-6 — build identityをbuild-time生成へ修正

### 作業

runtime `.git` fallbackを補助扱いへ降格。

build-time generated identityを必須化。

### 完了条件

コピー済みexeでもcommit/branchが埋まる。

### 推奨コミット

```text
S80-6: Generate immutable git identity at build time
```

---

## S80-7 — Framebuffer guard allocation

### 作業

Desktop allocation層へguard/canary追加。

### 完了条件

first frame中のOOB有無を確定。

### 推奨コミット

```text
S80-7: Guard Sapphire framebuffer allocations during cold-start
```

---

## S80-8 — pinned Sapphire exact A/B target

### 作業

固定upstream sourceを無改変compileするtargetを追加。

### 完了条件

currentとの差を実行結果で判定。

### 推奨コミット

```text
S80-8: Add an exact pinned-Sapphire GPU2D cold-start target
```

---

## S80-9 — faulting source lineの修正

### 前提

```text
RVA symbolized
fault access type/address取得
trace ring取得
```

後にのみ実施。

### 修正方針

```text
Sapphire source内ならupstream exact codeへ戻す
Desktop adapterならDesktop境界だけ修正
event/JITならGPU2Dへworkaroundを入れない
```

### 推奨コミット

```text
S80-9: Fix the symbolized Vulkan cold-start fault at its owning layer
```

---

## S80-10 — cold-start green後の画像監査

### 前提

```text
first RunFrame complete
first present
splash hidden
ACCESS_VIOLATION 0
```

その後に:

```text
black transparency
flicker
screen contamination
capture
```

へ戻る。

---

# 20. 推奨実装順

```text
1. S80-1
2. S80-2
3. S80-3
4. 同一binaryで再実行
5. 5 RVAをsymbolize
6. S80-4
7. S80-5
8. S80-6
9. 必要時のみS80-7
10. 必要時のみS80-8
11. S80-9
12. cold-start green
13. S80-10
```

---

# 21. 禁止事項

```text
- 今回のログだけでconsumeBudgetが根本原因と断定
- StartHBlank直後に落ちたと断定
- Unit Bをskip
- first frameをSoftware fallback
- Vulkan activationを遅延して隠す
- GPU2D_Soft.cppへ追加heuristic
- 黒透過・点滅対策を先に再開
- symbolization前に複数層を同時変更
- DbgHelpのNeAAC表示を正しいstackとして扱う
- repository外exeからruntime .git探索できる前提
- crash runと異なるrun IDのartifactを一組として扱う
```

---

# 22. 完了条件

## Observability

```text
first RunFrame全体のtrace
fault access type
fault target address
全register
全frame RVA
```

## Symbolization

```text
RVA 0xF901
RVA 0x1D515E
RVA 0x1DA6CB
RVA 0x6C698
RVA 0x71873
```

がfunction/file/lineへ解決。

## Sapphire parity

```text
GPU2D HBlank operational block = pinned Sapphire
GPU2D coreへのDesktop workaround = 0
```

## Crash

```text
ACCESS_VIOLATION = 0
first RunFrame complete
first present complete
```

## Build identity

```text
commit unknown = 0
branch unknown = 0
binary SHAとsymbols SHAがartifactに存在
```

---

# 23. 最終判断

S79で修正したpost-sprite gate、DMA、IRQ、event schedulingは
今回のrunですべて通過している。

したがって、これ以上その区間を推測修正してはいけない。

現在の最大の問題は:

```text
first HBlank完了時にtrace budgetを消費し、
その後の本当のクラッシュ位置が隠れていること
```

である。

同時に、2件のcrash reportが同一binary・同一RVAで一致しているため、
正しいdebug artifactさえ生成すれば、
根本functionは直ちに特定できる状態にある。

次の修正はGPU2Dの再発明ではなく:

```text
traceをfirst RunFrameまで維持
RVAをaddr2line
fault targetを記録
owner層だけ直す
```

の順で行うこと。
