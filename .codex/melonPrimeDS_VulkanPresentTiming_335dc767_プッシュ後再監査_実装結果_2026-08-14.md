# melonPrimeDS Vulkan Present Timing
## `335dc767` Push後再監査・実装結果

- 実施日: 2026-08-14
- 対象Branch: `develop_remakeVulkan_ver3`
- 起点監査HEAD: `335dc767743d6585cf5bb397b279d745ec4ed4d2`
- 対象指示書: `melonPrimeDS_VulkanPresentTiming_335dc767_プッシュ後再監査_2026-08-14.md`
- 対象範囲: swapchain generation lifecycle、present wait/timing result routing、pure model、contract audit
- 判定: **PASS (source/model/contract/macOS build)**

## 結論

前回P2-7のgeneration guardを維持したまま、再監査で見つかったP3診断意味不整合を修正した。

```text
old-generation decision
    -> same-frame swapchain recreation
    -> decisionCurrent = false
    -> safe untimed/ID-only present
    -> FallbackReason = FrameDecisionInvalidatedBySwapchainRecreation
```

`TelemetryOnlyPolicy`はTelemetryOnly policyそのものの理由としてのみ使用し、generation mismatchのsentinelには使用しない。

## 実装内容

### Swapchain generation guard

- `VulkanFrameDecisionMatchesSwapchain()`、`DecisionSwapchainGeneration`、`WaitAttemptSwapchainGeneration`を維持した。
- `ResetTimingLifecycle()`でdecision、generation stamp、wait attribution、frame intervalをinvalidateし、fallback reasonはneutralな`None`へ戻す。
- `PreparePresent()`と`EvaluateTargetTiming()`はgeneration mismatch時に旧backend/target permissionを使用しない。
- `CaptureState()`は旧generationのbounded wait、wait attempt、frame intervalを新generationへ帰属させない。

### Generation mismatch診断

- `VulkanJitFallbackReason`末尾へ`FrameDecisionInvalidatedBySwapchainRecreation`を追加し、既存capture CSVのnumeric値を維持した。
- `VulkanFrameDecisionFallbackReason()`をpure helperとして追加した。
- `CaptureState()`と`EvaluateTargetTiming()`はgeneration mismatchを明示的なrecreation reasonとして記録する。
- `TestSwapchainRecreationFallbackReason()`でJustInTime、JustInTimeFifoLatestReady、PresentWaitの全policyについて`TelemetryOnlyPolicy`誤用を禁止した。

### GOOGLE API contract分離

- `ClassifyVulkanGoogleRefreshCycleResult()`を`vkGetRefreshCycleDurationGOOGLE()`専用として使用する。
- `ClassifyVulkanGooglePastTimingResult()`を`vkGetPastPresentationTimingGOOGLE()`専用として使用する。
- refresh-cycleでは`VK_INCOMPLETE`/`VK_ERROR_OUT_OF_DATE_KHR`を正規success/lifecycle resultとして扱わず、past-timingでは仕様どおり`VK_INCOMPLETE`と`OUT_OF_DATE`を扱う。

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
same-frame swapchain recreation stale decision:
    CLOSED on source/model/contract

generation mismatch capture fallback semantics:
    CLOSED on source/model/contract

GOOGLE refresh-cycle vs past-timing classifier contract:
    CLOSED on source/model/contract

API-level fake-dispatch integration:
    NOT RUN / future P3 hardening

Linux / Windows / physical validation:
    NOT RUN (environment limitation, not source failure)
```

変更はVulkan present pacing、generation診断、関連テスト・監査に限定し、Software/OpenGL/Metal/Metal Compute/DX12、ゲームロジック、入力、音声、ROM patchは変更していない。

## 仕様参照

- [`vkWaitForPresent2KHR`](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresent2KHR.html)
- [`vkGetPastPresentationTimingGOOGLE`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingGOOGLE.html)
- [`vkGetRefreshCycleDurationGOOGLE`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetRefreshCycleDurationGOOGLE.html)
- [`VkResult`](https://docs.vulkan.org/refpages/latest/refpages/source/VkResult.html)
