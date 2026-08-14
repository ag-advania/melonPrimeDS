# melonPrimeDS Vulkan Present Timing
## `54809ed` Push後再監査・実装結果

- 実施日: 2026-08-14
- 対象Branch: `develop_remakeVulkan_ver3`
- 起点監査HEAD: `54809ed1cf6838c83d9285ee01f50e1d414527f5`
- 対象指示書: `melonPrimeDS_VulkanPresentTiming_54809ed_プッシュ後再監査_2026-08-14.md`
- 対象範囲: swapchain generation lifecycle、present wait/timing result routing、pure model、contract audit
- 判定: **PASS (source/model/contract/macOS build)**

## 結論

再監査で確認されたP2-7を修正した。

```text
old generation BeginFrame()
    -> decision generation N
same frame swapchain recreation
    -> generation N+1
new generation PreparePresent()
    -> generation N decision is rejected
    -> safe untimed/ID-only present path
next frame BeginFrame()
    -> decision is resolved from generation N+1 capabilities
```

`LastDecision`、target permission、wait attribution、frame intervalをswapchain generationに紐付け、旧generationのbehavioral stateが新swapchainへ漏れないようにした。

## 実装内容

### Swapchain generation guard

- `VulkanFrameDecisionMatchesSwapchain()`をpure helperとして追加した。
- `DecisionSwapchainGeneration`と`WaitAttemptSwapchainGeneration`を保持する。
- `BeginFrame()`でdecisionとwait attemptへcurrent generationをstampする。
- `ResetTimingLifecycle()`で`LastDecision`、generation stamps、`WaitAttemptedThisFrame`、`TargetFrameIntervalNs`をinvalidateする。
- `PreparePresent()`はgeneration mismatch時に`LastDecision.TimingBackend`を使用せず、EXT/GOOGLE target schedulingを発生させない。
- `EvaluateTargetTiming()`にもgeneration guardを置いた。
- `CaptureState()`は旧generationのbounded-wait permission、wait attempt、fallback、frame intervalを新generationへ記録しない。

### GOOGLE API contract分離

- `ClassifyVulkanGoogleRefreshCycleResult()`を`vkGetRefreshCycleDurationGOOGLE()`専用として追加した。
- `ClassifyVulkanGooglePastTimingResult()`を`vkGetPastPresentationTimingGOOGLE()`専用として追加した。
- refresh-cycle queryでは`VK_INCOMPLETE`/`VK_ERROR_OUT_OF_DATE_KHR`をsuccess/lifecycle routeにせずoptional failureへ分類し、past-timing queryでは`VK_INCOMPLETE`と`OUT_OF_DATE`を仕様どおり扱う。

### Tests / audit

- `TestSwapchainRecreationInvalidatesFrameDecision()`を追加した。
- generation一致、不一致、zero stampをpure testで固定した。
- GOOGLE refresh/past timingのAPI固有return contractをpure testで固定した。
- contract auditでgeneration reset、`PreparePresent()` guard、capture attribution guard、new generation testを検査するようにした。

## 仕様根拠

- [`vkWaitForPresent2KHR`](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresent2KHR.html)のSUBOPTIMAL/SUCCESS/TIMEOUT success contractとWSI failure contractを維持した。
- [`vkGetPastPresentationTimingGOOGLE`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingGOOGLE.html)と[`vkGetRefreshCycleDurationGOOGLE`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetRefreshCycleDurationGOOGLE.html)の異なるreturn-code contractをclassifierで分離した。

## 検証結果

| Gate | Result | Evidence |
|---|---|---|
| macOS Vulkan full build, developer features ON | PASS | `tools/build/macos/build-macos-vulkan.sh --build-only --with-metal --jobs 4` |
| macOS Vulkan full build, developer features OFF | PASS | `tools/build/macos/build-macos-vulkan.sh --build-only --release --with-metal --build-dir build-mac-vulkan-devguard-off --jobs 4` |
| current Vulkan present timing model test | PASS | `cmake --build build-mac-vulkan --target melonprime_vulkan_present_timing_check --parallel 4` |
| developer features OFF model test | PASS | `cmake --build build-mac-vulkan-devguard-off --target melonprime_vulkan_present_timing_check --parallel 4` |
| low-latency contract audit | PASS | `python3 tools/ci/audits/audit-low-latency-contract.py` |
| aggregate Vulkan latency tests | PASS | `python3 tools/testing/aggregate-vulkan-latency-tests.py` |
| Software parity audit | PASS | `python3 tools/ci/audits/audit-raster-software-parity.py` |
| whitespace/diff audit | PASS | `git diff --check` |
| Linux Vulkan build | NOT RUN | VMは起動中だがGuest Additions未報告、GuestControlユーザーログオン不能（`VBOX_E_IPRT_ERROR`）。 |
| Windows Vulkan build | NOT RUN | macOS hostにMSYS2/MinGW cross toolchainがない。 |
| API-level fake Vulkan dispatch | NOT RUN | production pacerへfake dispatchを注入するintegration harnessは未導入。pure API contract/source auditを実施。 |
| validation layer / physical runtime endurance | NOT RUN | resize/minimize/restore、SUBOPTIMAL誘発、validation layer、長時間present実行は今回未実施。 |

## 判定

```text
Wait2 SURFACE_LOST / SUBOPTIMAL routing:
    CLOSED

EXT / GOOGLE timing query routing:
    CLOSED on source/model/contract

same-frame swapchain recreation stale decision:
    CLOSED on source/model/contract

GOOGLE refresh-cycle vs past-timing classifier contract:
    CLOSED on source/model/contract

API-level fake-dispatch integration:
    NOT RUN / future P3 hardening

Linux / Windows / physical validation:
    NOT RUN (environment limitation, not source failure)
```

変更はVulkan present pacing、generation guard、関連テスト・監査に限定し、Software/OpenGL/Metal/Metal Compute/DX12、ゲームロジック、入力、音声、ROM patchは変更していない。
