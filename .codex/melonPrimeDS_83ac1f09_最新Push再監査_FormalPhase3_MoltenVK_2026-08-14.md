# melonPrimeDS 最新Push再監査
## Formal Phase 3 A/B・Physical Intel Mac / MoltenVK・FixWifi・残存ゲート

- Repository: `ag-advania/melonPrimeDS`
- Local branch: `develop_remakeVulkan_ver3`
- Remote branch verified: `origin/develop_remakeVulkan_ver3` resolves to this HEAD
- `origin/develop_remakeVulkan_ver2` is only a local remote-tracking ref; live `ls-remote` did not return that branch
- 監査HEAD: `83ac1f0971593e6482e38b5e92fe23540c626e48`
- HEAD message: `docs: record renderer switch stress evidence`
- Physical Intel Mac runtime source: `c72dd79b464d8335e62baad53ba7be9b92657f1a`
- Formal NVIDIA A/B fixed capture revision: `ca390a48bbeb5a4c1135417e9070d59e018ac1c1`
- 監査日: 2026-08-14
- Backend: **現行Vulkan clean-room backend**

## 今回の再監査スコープ（2026-08-14 継続）

今回の作業対象は、Windows exact-head validation、Formal NVIDIA Phase 3、repository static gates、およびexact-head CI metadataの再確認に限定する。

Physical Intel Mac / MoltenVK実機作業とFixWifiの実装・資料・runtime再監査は今回実施しない。Mac側は後日のMacBook作業へ延期し、FixWifi関連は既存記録を変更せず今回の完了判定から除外する。この文書内のMac/FixWifi記述は過去監査の履歴として残すが、今回新たにPASSへ繰り上げる根拠には使わない。

---

# 1. 結論

以下の既存Mac/FixWifi記述は履歴・参照用であり、今回の完了判定は後述の「今回の再監査の完了判定（Mac/FixWifi除外）」を正とする。

今回の再監査では、前回問題だった

```text
Physical Intel Mac evidence missing
```

は解消した。

current remoteには、sanitizedされたPhysical Intel MacBookの証跡一式が存在する。

```text
docs/archive/audits/vulkan/2026-08-14-intel-macbook/
  README.md
  environment.txt
  platform-availability.txt
  build-checks.txt
  runtime-excerpts.txt
  renderer-switch-stress.txt
  fixwifi-verification.txt
  validation-excerpts.txt
  pinned-1.4.0/
```

よって前回の:

```text
P1 / EVIDENCE & DOCUMENTATION INTEGRITY
```

は **CLOSED**。

ただし、Physical Intel Mac Vulkan gate全体は **PASSではなくPARTIAL**。

repo正本が示す現在の状態は:

```text
Physical Intel Mac / MoltenVK startup          PASS
Real Intel GPU selection                       PASS
VK_EXT_metal_surface                           PASS
Swapchain / presenter ready                    PASS
AMHP ROM first Vulkan frame                    PASS
AMHP visual Vulkan output                      PASS
MoltenVK 1.4.2                                 PASS
Pinned MoltenVK 1.4.0 smoke                    PASS
AMHJ guarded Vulkan first frame                PASS
FixWifi root-cause fix                         PASS
Renderer switch lifecycle stress               PASS, 30/30

Controlled match gameplay                      NOT RUN
Match-end recap                                NOT RUN
Resize / fullscreen / minimize                 NOT RUN
Manual visual renderer handover                NOT RUN
Savestate / reset                              NOT RUN
30-minute stability                            NOT RUN
Validation Layer                               BLOCKED
Native Intel Vulkan                            UNSUPPORTED on this macOS path
```

したがって最終判定は:

```text
FORMAL NVIDIA PHASE 3       COMPLETE
FORMAL RESULT               NO WINNER
SHIPPING DEFAULT            TelemetryOnly

PHYSICAL INTEL MAC/MOLTENVK PARTIAL
MAC EVIDENCE INTEGRITY      PASS
NEW RUNTIME P0/P1           NONE FOUND

DOC / EVIDENCE P2           2件
EXACT HEAD CI               NOT VERIFIED
```

---

# 2. Current HEAD

current local branch `develop_remakeVulkan_ver3`:

```text
83ac1f0971593e6482e38b5e92fe23540c626e48
docs: record renderer switch stress evidence
```

このHEADは、Physical Intel Macのrenderer-switch stress evidenceを追加した文書commit。

その直前:

```text
3b4bcec7726f921477df87e04b487303f038b2bf
docs: record FixWifi Vulkan root cause audit
```

runtime実装修正:

```text
c72dd79b464d8335e62baad53ba7be9b92657f1a
fix: guard and cache FixWifi patch state
```

Physical Intel Mac evidence初回記録:

```text
49bcc4b2413e8e37b0937d9f2dee70e688b2fa78
test: record physical Intel Mac Vulkan runtime
```

つまりcurrent HEADのruntime implementationは、
Physical Macで最終確認された`c72dd79b...`を包含しており、
その後は主にevidence/documentation追加。

---

## 2.1 Windows / Formal Phase 3 再監査結果（今回の対象範囲）

監査HEAD `83ac1f0971593e6482e38b5e92fe23540c626e48`について、local HEADとremote branchのSHAが一致することを確認した。

Windows exact-head local validation:

```text
command:
  cmd /c tools\build\windows\build-mingw-existing.bat --jobs 1 --tail 80
exit code: 0
jobs: 1
build result: succeeded

melonprime_vulkan_present_timing_tests.exe
  Vulkan present timing model tests PASS

melonprime_xell_state_machine_tests.exe
  Intel XeLL fake API state-machine tests PASS
```

既存のrelease-mingw-x86_64 build treeを使用した再ビルドであり、CMake/vcpkg再構成は行っていない。今回のHEADはruntime変更を含まないdocumentation commitなので、source/options不変の既存build entry pointによる検証条件を満たす。

Formal NVIDIA Phase 3:

```text
python tools/perf/verify-vulkan-formal-ab.py \
  docs/archive/audits/vulkan/2026-08-13-formal-ab \
  --warmup 600 --minimum-rows 10600

FORMAL A/B VERIFICATION PASS
csv_files=21
runs_per_mode=3
minimum_rows=10600
invalid_rows=0
measured_generation_changes=0
a2_a3_target_active_at_least=95%
queue_full_and_recovery=0
wait_timeout_rate_lt=1%
```

21 metadata filesはすべて`process_exit_code=0`、config/layer restoreがPASS、最小行数は13,243で、21件のformal stderr logは空だった。Formalの判定は従来どおりNO WINNER、shipping defaultはTelemetryOnly。

Repository static gates:

- config defaults / HUD key parity / `.inc` ownership / literal・platform scatter budgets: PASS
- color-dialog preferences / SRP performance / thread boundary / software parity / radar-color contract / low-latency contract: PASS
- Vulkan shader source sync・compile・spirv-val、DX12 shader source sync・compile: PASS
- Vulkan latency aggregate、Markdown local-link audit（472 links）: PASS
- `audit-melonprime-instance-state.ps1 -Strict`: exit 0。ツールが既知の許容対象として22件を報告するが、ゲート失敗ではない

この範囲で新たなWindows runtime blockerは見つからなかった。

---

# 3. Physical Intel Mac evidence integrity

## 3.1 前回P1はCLOSED

前回はsecurity cleanup後に:

```text
docs/archive/audits/vulkan/2026-08-14-intel-macbook/
```

がremoteから確認できず、
ユーザー報告だけでPhysical Mac PASSを主張できなかった。

現在はdirectoryそのものがcurrent branchから取得可能で、
sanitized evidenceが複数ファイルに分離されている。

特に:

```text
README.md
environment.txt
build-checks.txt
runtime-excerpts.txt
renderer-switch-stress.txt
fixwifi-verification.txt
validation-excerpts.txt
```

を第三者がremoteから追跡できる。

よって:

```text
P1 / EVIDENCE & DOCUMENTATION INTEGRITY = CLOSED
```

。

## 3.2 Security hygieneも維持

raw runtime log、ROM、savestate、private screenshotはrepositoryへ入れず、
結果・hash・環境・redacted excerptだけを残す設計になっている。

これは妥当。

証拠公開性とprivate-data hygieneを両立している。

---

# 4. Physical Intel Mac fixed environment

repo evidence:

```text
source_revision:
  c72dd79b464d8335e62baad53ba7be9b92657f1a

host:
  MacBookPro15,2
  x86_64

macOS:
  15.7.7
  Darwin build 24G720

CPU:
  Intel Core i5-8259U
  4 cores
  2.30GHz

Memory:
  16GB

GPU:
  Intel Iris Plus Graphics 655 integrated
  Metal 3

Display:
  internal Retina
  2560x1600
  one physical display
```

runtime:

```text
local MoltenVK:
  1.4.2

shipping-pin smoke:
  MoltenVK 1.4.0

Vulkan headers:
  1.4.357.0

Vulkan loader:
  1.4.357.0
```

primary ROM evidence:

```text
AMHP Rev.01
SHA-256:
4c0510ae0389f793bf95bd095d8ecd29868cd85f3b15f8f72999685e813790c9
```

additional Japanese ROM:

```text
AMHJ Rev.00
SHA-256:
8116cff4964daa430c4c4039170ecd063348fc6f768636b9bc3a19a951306e02
```

raw ROM binary/pathはrepositoryにない。

---

# 5. macOS build gate

## 5.1 Release

command:

```text
./tools/build/macos/build-macos-vulkan.sh \
  --install-deps \
  --with-metal \
  --release \
  --jobs 2
```

result:

```text
211 / 211 build steps PASS

CMake Vulkan gate       ENABLED
CMake Metal gate        ENABLED
developer features      OFF
present-timing tests    PASS
bundle MoltenVK         PRESENT
executable architecture x86_64
MoltenVK architecture   x86_64
codesign --deep --strict PASS
```

判定:

```text
Physical Intel Mac x86_64 Release Build = PASS
```

## 5.2 Developer build

FixWifi follow-up:

```text
206 / 206 build steps PASS
developer features ON
FixWifi OSD path compiled
present-timing model tests PASS
bundle/signature PASS
```

判定:

```text
Developer diagnostic build = PASS
```

## 5.3 Debug build

```text
295 / 295 build steps PASS
present-timing model tests PASS
```

ただしValidation Layer runtime enablementは別問題でBLOCKED。

---

# 6. Physical MoltenVK startup / WSI

Local MoltenVK 1.4.2 no-ROM evidence:

```text
Vulkan instance created
VK_EXT_metal_surface created
Intel Iris Plus Graphics 655 selected
renderer requested=Vulkan
presenter actual=Vulkan
VSync off -> IMMEDIATE
presenter ready
```

よって:

```text
Physical Intel GPU selection    PASS
MoltenVK driver path            PASS
Metal surface                   PASS
Vulkan presenter                PASS
Swapchain/present path          PASS
```

これはhosted Apple Paravirtual deviceではない。

**実機 Intel Iris Plus Graphics 655上のMoltenVK runtime evidence**。

ただしこれは:

```text
native Intel Vulkan
```

ではない。

macOSでは:

```text
Vulkan API
  ↓
MoltenVK
  ↓
Metal
  ↓
Intel Iris Plus 655
```

。

したがってcross-platform native Intel Vulkan gateを閉じる証跡ではない。

---

# 7. AMHP ROM Vulkan runtime

AMHP Rev.01:

```text
Intel Iris Plus Graphics 655 selected
renderer requested=Vulkan
actual=Vulkan
first frame presented
valid game imagery observed
```

さらに:

```text
MoltenVK 1.4.2 PASS
MoltenVK 1.4.0 PASS
```

。

pinned 1.4.0 smokeでは:

```text
instance
VK_EXT_metal_surface
Intel GPU selection
presenter
first ROM frame
```

まで到達。

primitive-restart warningが2件記録されたが:

```text
VUID
SYNC-HAZARD
DEVICE_LOST
SIGABRT
SIGSEGV
Vulkan runtime-failure
```

はsmoke evidence中に見つかっていない。

ただしValidation Layerが有効だった証跡ではないため、
これをValidation cleanとは呼ばない。

判定:

```text
AMHP ROM launch              PASS
AMHP first Vulkan frame      PASS
AMHP visible Vulkan output   PASS
Pinned MoltenVK 1.4.0 smoke  PASS
Visual parity                NOT CLAIMED
```

---

# 8. AMHJ white-screen / ARM9 data-abort root cause

Physical Macの追加AMHJ testでは、
最初:

```text
white Vulkan window
ARM9 data abort
```

が発生した。

Software rendererではgame imageryが出たため、
一見:

```text
Intel / MoltenVK Vulkan defect
```

に見える。

しかしA/Bで:

```text
WifiBitset=true   -> data abort reproduction
WifiBitset=false  -> 30 sec no data abort
```

となり、
rendererそのものではなくFixWifi patch pathへ切り分けられた。

---

# 9. FixWifi root cause

AMHJ Rev.00:

```text
ROM group:
  JP1.0

checksum:
  0xD75F539D

target base:
  0x020662EC

observed:
  0xE2466005

expected apply:
  0xE2052007

expected restore:
  0xE3A01001
```

observed wordは:

```text
apply stateでもない
restore stateでもない
```

。

旧実装はROM groupだけでlayoutを選び、
実質的に代表canaryのみを前提として
51-word patchを書ける構造だった。

そのため同一group判定でも実コードlayoutが想定と異なるROMでは、
ARM9 memoryを破壊し得た。

これは:

```text
MoltenVK graphics bug
Vulkan rasterization bug
Intel GPU bug
```

ではない。

**ROM patch safety bug**。

---

# 10. FixWifi根本修正監査

commit:

```text
c72dd79b464d8335e62baad53ba7be9b92657f1a
fix: guard and cache FixWifi patch state
```

現在の実装は書き込み前に全target wordを検査する。

概念:

```cpp
for each of 51 words:
    current = ARM9Read32(base + offset)

    if current != applyVal
       && current != revertVal:
        reject entire patch

only after full validation:
    write patch
```

これにより:

```text
未知layout
partial mismatch
unexpected ROM state
```

では**1 wordも書かない**。

さらにstate:

```text
Unchecked
Applied
Rejected
Unsupported
```

をcache。

ROM re-detection / lifecycle transitionではpatch bookkeepingをresetする。

重要:

```text
Applied / Rejected / Unsupported
```

が確定した後はout-of-game frame pathで51-word scanを毎frame繰り返さない。

したがって:

```text
Safety:
  PASS

ROM lifecycle:
  PASS

same-group ROM contamination protection:
  PASS

steady-state scan overhead:
  avoided

developer diagnosis:
  FixWifi Applied
  FixWifi Rejected
  FixWifi Unsupported
```

。

---

# 11. mphCodex reference audit

checked-in FixWifi verificationでは:

```text
JP1.0
US1.0
EU1.0
```

について51 apply words + 51 restore wordsを
mphCodex proposal/cheat sectionsと比較。

結果:

```text
transcription mismatch = 0
```

。

一方:

```text
JP1.1
US1.1
EU1.1
KR1.0
```

はFixWifi target baseを持たず、
当該patchを書かない設計。

そのversion policyも証跡に記録済み。

---

# 12. AMHJ guarded follow-up

FixWifi guard適用後:

```text
WifiBitset=true
AMHJ Rev.00
```

で:

```text
unexpected layout detected
patch = Rejected
ARM9 writes = 0
Vulkan first frame reached
ARM9 data abort = none during 30 sec smoke
clean exit
```

。

従って元のAMHJ failure:

```text
FAIL
```

は現runtime sourceでは:

```text
PASS (guarded smoke)
```

へ更新済み。

ただしこれは:

```text
full AMHJ match gameplay PASS
```

ではない。

30秒のfirst-frame smoke範囲。

---

# 13. Renderer switching stress

current HEADで追加されたevidence:

```text
MELONPRIME_RENDERER_SWITCH_STRESS=5,0,5,3,5,4
MELONPRIME_RENDERER_SWITCH_STRESS_ITERATIONS=5
MELONPRIME_RENDERER_SWITCH_STRESS_INTERVAL_MS=600
```

renderer IDs:

```text
0 = Software
3 = Metal
4 = Metal Compute
5 = Vulkan
```

実施:

```text
Vulkan <-> Software
Vulkan <-> Metal
Vulkan <-> Metal Compute
```

それぞれ5 round trips。

total:

```text
30 / 30 transitions completed
```

final restore:

```text
renderer 5 / Vulkan
```

再initializeごとにVulkan first frameが確認された。

stress evidenceでは:

```text
ARM9 data abort
SIGABRT
SIGSEGV
DEVICE_LOST
runtime-failure
```

なし。

判定:

```text
Renderer teardown/recreate lifecycle = PASS
ROM-preserving renderer switching     = PASS
30/30 transition stress               = PASS
```

ただし:

```text
manual visual handover
visual parity after every switch
```

はこのstress hookでは確認していない。

従って表記は:

```text
PASS (lifecycle/log-level stress)
```

が正しい。

---

# 14. GenericPresentTiming on Physical Intel Mac

source:

```text
GenericPresentTiming = true
```

を維持。

physical Intel/MoltenVK capability evidence:

```text
VK_KHR_present_id2            yes
VK_KHR_present_wait2          yes
VK_EXT_present_timing         no
absolute timing               no
relative timing               no
FIFO_LATEST_READY             no
```

runtimeはoptional-capability fail-softで:

```text
policy    = TelemetryOnly
authority = GenericHost
```

系へ落ちる。

baseline presenter pathはPASS。

よってconditional:

```text
GenericPresentTiming OFF
```

A/Bは実行されていない。

この判断は妥当。

unsupported capabilityを無理に使わず、
現在のfail-soft contractが実機Macでも成立している。

判定:

```text
GenericPresentTiming source true        PASS
Optional-capability fallback            PASS
Physical Mac presenter baseline         PASS
GenericPresentTiming ON/OFF A/B          NOT RUN / not required by failure trigger
```

---

# 15. Validation Layer

Debug buildそのもの:

```text
295 / 295
PASS
```

Homebrew:

```text
vulkan-validationlayers 1.4.357.0
```

もinstall済み。

`VK_LAYER_PATH`も設定。

しかしruntime:

```text
VK_LAYER_KHRONOS_validation is not installed
instance created (..., 0 layers)
```

。

原因としてrepo evidenceが示しているのは、
macOS Vulkan dispatchがまず:

```text
@executable_path/../Frameworks/libMoltenVK.dylib
```

をdirect `dlopen()`し、
shipping-style bundled MoltenVK pathが
Khronos loaderのexplicit layer enumerationを通らないこと。

したがって:

```text
Validation = BLOCKED
```

。

重要:

smoke logに:

```text
VUIDなし
SYNC-HAZARDなし
DEVICE_LOSTなし
```

でも、

```text
Validation PASS
```

とは扱わない。

Validation Layer自体が有効化されていないから。

これはrepo文書の扱いが正しい。

---

# 16. Formal NVIDIA Phase 3は今回も有効

Physical Intel Mac workは別gate。

Windows/NVIDIA Formal capture:

```text
source:
  ca390a48bbeb5a4c1135417e9070d59e018ac1c1

runs:
  21 CSV
  7 modes x 3 runs

warmup:
  600

measured:
  >= 10,000 / run

invalid_rows:
  0

measured swapchain generation changes:
  0

A2 / A3 target active:
  100%

timing queue full/recovery:
  0 / 0
```

Formal result:

| Mode | Pipeline P50 ms | Pipeline P95 ms | Frame P99 ms | Result |
|---|---:|---:|---:|---|
| A0 TelemetryOnly | 4.081 | 7.046 | 20.614 | baseline |
| A1 PresentWait | 4.119 | 7.078 | 20.606 | no material difference |
| A2 JIT | 3.943 | 6.910 | 20.513 | closest candidate |
| A3 JIT + FIFO latest-ready | 4.159 | 7.034 | 20.498 | no material difference |
| B1 Reflex On + JIT | 4.098 | 7.126 | 20.604 | no material difference |
| B2 Reflex On+Boost + JIT | 4.156 | 7.194 | 20.707 | no adoption |
| C0 JIT / VSync Off | 3.988 | 6.960 | 20.503 | control |

A2:

```text
P50 improvement:
  3.3815%

P95 improvement:
  1.9302%
```

winner thresholdのP95 2%を満たさない。

よって:

```text
FORMAL NVIDIA PHASE 3 = COMPLETE
RESULT                 = NO MATERIAL DIFFERENCE / NO WINNER
DEFAULT                = TelemetryOnly
```

。

Physical Mac evidenceが追加されてもこの結論は変わらない。

---

# 17. Mac証跡で新たに閉じたもの

前回まで:

```text
Physical retail-Mac/full-ROM = NOT VERIFIED
```

だった部分を細分化できる。

現在:

```text
Physical Intel Mac hardware present        CONFIRMED
Physical Intel GPU through MoltenVK        PASS
Local MoltenVK 1.4.2 startup               PASS
Pinned MoltenVK 1.4.0 startup              PASS
Metal WSI                                  PASS
Presenter                                  PASS
ROM real frame                             PASS
Visual game imagery                        PASS
Renderer lifecycle stress                  PASS
FixWifi guarded AMHJ first-frame smoke     PASS
```

したがって:

```text
physical retail-Mac startup/WSI/ROM-frame gate
```

は閉じたと言ってよい。

ただし:

```text
physical retail-Mac full gameplay parity gate
```

はまだ閉じていない。

---

# 18. Physical Macで残っている実行項目

repo正本:

```text
match gameplay
  NOT RUN

match-end recap
  NOT RUN

resize / fullscreen / minimize
  NOT RUN

manual visual renderer handover
  NOT RUN

savestate/reset
  NOT RUN

30-minute stability
  NOT RUN

Validation layer
  BLOCKED
```

Full physical-Mac PASSには少なくとも:

```text
same-ROM controlled match lifecycle
>=5 min actual gameplay
visual handover
match end -> recap
resize
fullscreen
minimize/restore
manual renderer switch visual inspection
savestate/load/reset
30-minute session
```

が必要。

---

# 19. New P2-1 — macos-vulkan.mdがPhysical Mac最新証跡に追従していない

`docs/development/build/macos-vulkan.md`の
Physical Intel follow-upは現在も:

```text
source revision 7f56b91b...
```

を記載。

さらに:

```text
renderer switching ... remain NOT RUN
```

としている。

しかしcurrent Physical Mac authoritative evidenceは:

```text
source revision:
  c72dd79b...

renderer switch:
  PASS (stress)
  30/30
```

。

従って:

```text
P2 / DOCUMENTATION STALENESS
```

。

修正:

```text
7f56b91b...
↓
c72dd79b...

renderer switch:
NOT RUN
↓
PASS (lifecycle/log-level stress)
manual visual handover remains NOT RUN
```

。

またFixWifi AMHJ guarded resultも短く追記するとよい。

これはruntime blockerではない。

---

# 20. New P2-2 — AC / long-session evidence表記が不一致

current `README.md`:

```text
30-minute stability:
  NOT RUN
  AC available for follow-up
```

一方:

`environment.txt`:

```text
power_at_final_inventory=battery; discharging; AC prerequisite not met
long_session=BLOCKED; AC power was unavailable
```

`platform-availability.txt`:

```text
ac_power=BLOCKED at final inventory
long_session=NOT RUN because the AC prerequisite was blocked
```

。

つまりlatest READMEとsupporting environment filesで:

```text
AC is now available
```

と

```text
AC unavailable / blocked
```

が混在。

実際の30分sessionはどちらにせよ:

```text
NOT RUN
```

なので判定は変わらない。

しかしevidence consistency上:

```text
P2 / DOCUMENTATION CONSISTENCY
```

。

推奨:

もし後日ACへ接続できたがsession未実行なら:

```text
environment.txt:
  initial_inventory_power=battery/discharging
  followup_ac_available=true
  long_session=NOT_RUN
```

のように時間軸を分ける。

`BLOCKED`は解除し:

```text
NOT RUN
```

へ統一。

---

# 21. Exact current HEAD CI

current HEAD:

```text
83ac1f0971593e6482e38b5e92fe23540c626e48
```

についてGitHub combined status:

```text
state = pending
status entries = 0
total_count = 0
```

associated workflow runs:

```text
check-runs = 0
workflow-runs = 0
```

GitHub API / `gh run list --commit 83ac1f0971593e6482e38b5e92fe23540c626e48`で確認。

従って:

```text
EXACT CURRENT HEAD GITHUB CI = NOT RUN / NOT VERIFIED
```

。

これはlocal validationの失敗ではなく、exact HEADに紐づくGitHub status/check-run/workflowが存在しないという外部CI状態である。過去CIのPASSをexact-head PASSとは呼ばない。

ただしcurrent HEADはrenderer-switch evidence documentation commit。

runtime sourceは直前の:

```text
c72dd79b...
```

以降にrenderer implementationの変更を含まない。

過去CIのPASSをexact-head PASSとは呼ばないこと。

---

# 22. Hosted macOS evidenceとの関係

既存hosted CI:

```text
arm64 MoltenVK startup/WSI smoke
  PASS

x86_64/arm64/universal bundle checks
  PASS

Intel-host diagnostic
  NOT PASS / NON-GATING
  Apple Paravirtual Metal
  SIGABRT before presenter ready
```

。

今回のPhysical Intel Mac evidenceはhosted Intel diagnosticとは別。

重要な違い:

```text
Hosted Intel job:
  Apple Paravirtual Metal device

Physical Mac:
  Intel Iris Plus Graphics 655
```

。

したがってPhysical evidenceにより:

```text
real Intel GPU + MoltenVK presenter + ROM frame
```

は新たに実証済み。

hosted Intel diagnosticのSIGABRTを
Physical Intel GPUのFAILとして一般化してはいけない。

---

# 23. Cross-platform / vendor residual gates

## AMD

```text
AMD GPU:
  unavailable

AMD Anti-Lag native runtime:
  NOT RUN
```

NVIDIA Formal A/Bのblockerではない。

cross-vendor default変更の根拠には不足。

## Intel

Physical macOS:

```text
Intel Iris Plus 655 via MoltenVK
PASS partial
```

native Intel Vulkan:

```text
not represented by macOS MoltenVK
```

。

## Linux

hosted:

```text
llvmpipe / Xvfb validation smoke
PASS limited
```

hardware AMD/Intel Vulkan:

```text
NOT RUN
```

。

## DPI

Windows host:

```text
one physical monitor
```

cross-DPI transition:

```text
NOT RUN
```

。

## click-to-photon

```text
NOT RUN
```

Formal CSVはhost pipeline proxyであり、
click-to-photonではない。

---

# 24. Risk ranking

## P0

```text
NONE FOUND
```

## P1 runtime

```text
NONE FOUND in this re-audit
```

前回のevidence-missing P1:

```text
CLOSED
```

。

## P2

### P2-A

```text
macos-vulkan.md stale Physical Mac revision/status
```

### P2-B

```text
AC / long-session sanitized evidence state inconsistency
```

## P3

必要ならFormal READMEのPhysical Mac follow-upに:

```text
renderer-switch stress PASS
FixWifi guarded AMHJ PASS
```

を追記すると最新statusが一箇所から追いやすい。

ただし現在でもdated Mac READMEへのリンクは存在するためnon-blocking。

---

# 25. 完了表

| 項目 | 判定 |
|---|---|
| Vulkan clean-room implementation | PASS |
| Sync integrity / Windows evidence | PASS |
| Formal NVIDIA Phase 3 | COMPLETE |
| Formal winner | NONE |
| Default | TelemetryOnly |
| Windows Manual Phase 1 | PASS except DPI |
| Windows DPI transition | NOT RUN |
| Physical Intel Mac evidence availability | **PASS** |
| Physical Intel Mac x86_64 build | **PASS** |
| Intel Iris Plus 655 selected via MoltenVK | **PASS** |
| VK_EXT_metal_surface | **PASS** |
| Physical presenter ready | **PASS** |
| AMHP first Vulkan frame | **PASS** |
| AMHP visible Vulkan output | **PASS** |
| MoltenVK 1.4.2 | **PASS** |
| pinned MoltenVK 1.4.0 smoke | **PASS** |
| AMHJ guarded first-frame smoke | **PASS** |
| FixWifi safety fix | **PASS** |
| Renderer-switch stress | **PASS 30/30** |
| Renderer-switch visual parity | NOT RUN |
| Controlled match gameplay | NOT RUN |
| Match-end recap | NOT RUN |
| Resize/fullscreen/minimize | NOT RUN |
| Savestate/reset on Mac | NOT RUN |
| 30-minute Mac session | NOT RUN |
| macOS Validation Layer | BLOCKED |
| Physical Intel Mac overall | **PARTIAL** |
| Hosted arm64 MoltenVK smoke | PASS limited |
| Hosted Intel diagnostic | NOT PASS / NON-GATING |
| Native Intel Vulkan | NOT RUN / unsupported on this macOS path |
| AMD Anti-Lag runtime | NOT RUN |
| Linux AMD/Intel hardware | NOT RUN |
| click-to-photon | NOT RUN |
| Exact HEAD GitHub CI | NOT RUN / NOT VERIFIED |

---

## 25.1 今回の対象範囲の完了表

| 今回再確認した項目 | 判定 |
|---|---|
| local/remote exact HEAD一致 | PASS |
| Windows exact-head existing-tree build | PASS（exit 0） |
| Vulkan present timing model tests | PASS |
| Intel XeLL fake API state-machine tests | PASS |
| Formal NVIDIA Phase 3 verifier | PASS（21 CSV / invalid 0 / generation change 0） |
| Repository static gates | PASS |
| Exact-head GitHub CI | NOT RUN / NOT VERIFIED（status/check-runs/workflows 0） |
| Physical Intel Mac / MoltenVK | DEFERRED（今回対象外） |
| FixWifi | DEFERRED / UNCHANGED（今回対象外） |

---

# 26. 次にやるべきこと

今回のWindows / Formal Phase 3 / static-gate範囲には、ローカルで残る必須作業はない。Mac実機/MoltenVKの残存runtime matrixとFixWifi関連は今回のスコープ外であり、下記の旧記録・候補項目は後日の別作業として扱う。

Physical Intel Mac gateをfull PASSへ閉じるなら、
renderer実装をさらに触るより先に残りruntime matrixを実施する。

優先順:

```text
1. controlled match >=5 min
2. match-end -> recap
3. resize / fullscreen / minimize
4. manual visual Vulkan <-> Software / Metal / Metal Compute
5. savestate/load/reset
6. 30-minute AC session
7.可能ならvalidation loader pathを別途整備してDebug Validation
```

同時にP2文書だけ修正:

```text
docs/development/build/macos-vulkan.md
environment.txt
platform-availability.txt
```

。

---

# 27. Full Physical Intel Mac PASSのDoD

```markdown
- [x] Physical Intel Mac evidence sanitized and remote-visible
- [x] x86_64 Release build
- [x] Bundle MoltenVK
- [x] codesign verify
- [x] Intel Iris Plus 655 actual selection
- [x] VK_EXT_metal_surface
- [x] swapchain / presenter ready
- [x] AMHP ROM first frame
- [x] visual Vulkan output
- [x] local MoltenVK 1.4.2
- [x] pinned MoltenVK 1.4.0 smoke
- [x] AMHJ FixWifi root cause isolated
- [x] FixWifi full-signature guard
- [x] AMHJ guarded first-frame smoke
- [x] renderer switch lifecycle stress 30/30
- [ ] manual renderer visual handover
- [ ] controlled match >=5 min
- [ ] match-end recap
- [ ] resize
- [ ] fullscreen
- [ ] minimize/restore
- [ ] savestate/load/reset
- [ ] 30-minute stable session
- [ ] Validation Layer enabled run, or document direct-loader limitation as accepted release risk
```

---

# 28. 最終判定

```text
CURRENT HEAD:
  83ac1f0971593e6482e38b5e92fe23540c626e48

FORMAL NVIDIA PHASE 3:
  COMPLETE

FORMAL OUTCOME:
  NO WINNER

SHIPPING DEFAULT:
  TelemetryOnly

PHYSICAL INTEL MAC EVIDENCE:
  AVAILABLE / AUDITABLE / PASS

PHYSICAL INTEL MAC MOLTENVK SMOKE:
  PASS

PHYSICAL INTEL MAC FULL GATE:
  PARTIAL

FIXWIFI AMHJ ROOT CAUSE:
  CONFIRMED

FIXWIFI ROOT FIX:
  PASS

RENDERER SWITCH STRESS:
  PASS 30/30

NEW RUNTIME P0/P1:
  NONE FOUND

PREVIOUS EVIDENCE P1:
  CLOSED

NEW P2:
  1. macos-vulkan.md stale Physical Mac revision/status
  2. AC/long-session evidence state inconsistency

EXACT HEAD CI:
  NOT VERIFIED

CROSS-VENDOR DEFAULT CHANGE:
  NOT JUSTIFIED

CLICK-TO-PHOTON CLAIM:
  NOT AUTHORIZED
```

Physical Intel Macについては、前回の「証跡が見えないため監査不能」状態から明確に前進している。

現在は:

```text
MacでVulkanが起動する
```

だけではなく:

```text
実Intel GPU
MoltenVK
Metal WSI
actual Vulkan presenter
real ROM frame
visual game imagery
pinned shipping MoltenVK
AMHJ failure root-cause isolation
FixWifi safe rejection
renderer teardown/recreate stress
```

までremote evidenceで追跡可能。

一方で、まだ:

```text
実match gameplay
match completion
window lifecycle
savestate/reset
long-session
Validation
```

が不足しているため、
**Physical Intel Mac Vulkan = FULL PASS**
とする段階ではない。

現時点の最も正確な表現は:

```text
Physical Intel Mac / MoltenVK:
PARTIAL — startup/WSI/ROM/visual/switch-stress PASS,
          gameplay/lifecycle/long-session/validation still open.
```

## 28.1 今回の再監査の完了判定（Mac/FixWifi除外）

今回の対象範囲は完了と判定する。

```text
WINDOWS EXACT-HEAD LOCAL VALIDATION:
  COMPLETE / PASS

FORMAL NVIDIA PHASE 3:
  COMPLETE / PASS
  OUTCOME: NO WINNER
  SHIPPING DEFAULT: TelemetryOnly

REPOSITORY STATIC GATES:
  PASS

EXACT-HEAD GITHUB CI:
  NOT RUN / NOT VERIFIED
  (no statuses, check-runs, or workflow-runs for this SHA)

PHYSICAL INTEL MAC / MOLTENVK:
  DEFERRED / NOT IN SCOPE

FIXWIFI:
  DEFERRED / UNCHANGED / NOT IN SCOPE
```

したがって、今回の再監査についてはWindows/Formal側の必要な確認を完遂した。Mac/FixWifiを理由にこの完了判定を拡張しない。

---

# 29. 監査に使用したrepository evidence

```text
docs/archive/audits/vulkan/2026-08-14-intel-macbook/README.md
docs/archive/audits/vulkan/2026-08-14-intel-macbook/environment.txt
docs/archive/audits/vulkan/2026-08-14-intel-macbook/platform-availability.txt
docs/archive/audits/vulkan/2026-08-14-intel-macbook/build-checks.txt
docs/archive/audits/vulkan/2026-08-14-intel-macbook/runtime-excerpts.txt
docs/archive/audits/vulkan/2026-08-14-intel-macbook/renderer-switch-stress.txt
docs/archive/audits/vulkan/2026-08-14-intel-macbook/fixwifi-verification.txt
docs/archive/audits/vulkan/2026-08-14-intel-macbook/validation-excerpts.txt

docs/archive/audits/vulkan/2026-08-13-formal-ab/README.md
docs/development/build/macos-vulkan.md

commit:
49bcc4b2413e8e37b0937d9f2dee70e688b2fa78
c72dd79b464d8335e62baad53ba7be9b92657f1a
3b4bcec7726f921477df87e04b487303f038b2bf
83ac1f0971593e6482e38b5e92fe23540c626e48
```

今回のWindows/Formal再監査で使用したコマンド・生成物:

```text
tools/build/windows/build-mingw-existing.bat --jobs 1 --tail 80
build/release-mingw-x86_64/last-build.log
tools/perf/verify-vulkan-formal-ab.py
tools/perf/aggregate-vulkan-latency.py
tools/ci/audits/*
tools/vulkan/compile-shaders.py --check-source-sync
tools/dx12/compile-shaders.py --check-source-sync
tools/maintenance/check-doc-links.py
docs/archive/audits/vulkan/2026-08-13-formal-ab/
```
