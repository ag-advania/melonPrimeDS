# melonPrimeDS Vulkan Present Timing
## `154b7b6` Push後再監査・実装結果

- 実施日: 2026-08-14
- 対象Branch: `develop_remakeVulkan_ver3`
- 起点監査HEAD: `154b7b6a573ff623167f13b4675dea9473a0ccd2`
- 対象指示書: `melonPrimeDS_VulkanPresentTiming_154b7b6_プッシュ後再監査_2026-08-14.md`
- 対象範囲: Vulkan present timing / `vkWaitForPresent2KHR` lifecycle routing、pure model、contract audit
- 判定: **PASS (source/model/contract/macOS build)**

## 結論

再監査で確認された`vkWaitForPresent2KHR()`の残件を修正した。

```text
VK_SUCCESS
    -> Continue
VK_TIMEOUT
    -> timeout counter + Continue
VK_SUBOPTIMAL_KHR
    -> SwapchainSuboptimal -> RebuildSwapchain
VK_ERROR_OUT_OF_DATE_KHR
    -> SwapchainOutOfDate -> RebuildSwapchain
VK_ERROR_DEVICE_LOST
    -> DeviceLost -> FailRenderer
VK_ERROR_SURFACE_LOST_KHR
    -> SurfaceLost -> FailRenderer
other optional wait failure
    -> DisableWait -> Continue
```

`VK_SUBOPTIMAL_KHR`は`DisableWait()`へfallthroughせず、`VK_ERROR_SURFACE_LOST_KHR`もoptional wait failureへ潰れない。

## 実装内容

### PresentWait2 routing

- `VulkanPacerBeginResult::SwapchainSuboptimal`を追加し、既存の`DeviceLost`/`SurfaceLost`の数値を維持した。
- `ClassifyVulkanPresentWait2Result()`をpure API-specific classifierとして追加した。
- `BeginFrame()`はclassifier結果をswitchし、SUBOPTIMAL/OUT_OF_DATEをswapchain dirty、DEVICE_LOST/SURFACE_LOSTを既存renderer failureへ渡す。
- lifecycle logは`query=result=route=action`形式でwait2にも適用した。
- `VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT`は、現構成でfullscreen-exclusiveを使用していないため、P3 future-proofingとしてoptional wait disableへ分類した。

### EXT / GOOGLE query contracts

- `ClassifyVulkanPastTimingResult()`、`ClassifyVulkanTimingPropertiesResult()`、`ClassifyVulkanTimeDomainResult()`、`ClassifyVulkanGoogleTimingResult()`を追加した。
- APIごとのsuccess/pending contractをproduction sourceで使用し、generic lifecycle classifierだけに依存しないようにした。
- Timing propertiesの`VK_NOT_READY`は`RetryAfterPresent`、time-domainの`VK_SUCCESS`/`VK_INCOMPLETE`はenumeration継続、time-domainの`VK_NOT_READY`はtarget lifecycle failureとして扱う。
- count queryの`VK_INCOMPLETE`もbounded enumeration retryへ統一した。

### Tests / audit

- `TestPresentWait2ResultClassification()`を追加し、SUCCESS/TIMEOUT/SUBOPTIMAL/OUT_OF_DATE/DEVICE_LOST/SURFACE_LOST/UNKNOWN/fullscreen-exclusive lossを確認した。
- `TestPresentTimingQueryContractClassification()`を追加し、EXT/GOOGLE各APIのsuccess/pending/fatal mappingを確認した。
- `TestBeginResultRouting()`にSUBOPTIMALのswapchain rebuild actionを追加した。
- source contract auditで、SUBOPTIMAL/SURFACE_LOSTのfallthrough、API classifierの使用、time-domainの`VK_NOT_READY`誤扱いを検出できるようにした。

## 仕様根拠

- [`vkWaitForPresent2KHR`](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresent2KHR.html)のSUCCESSコード（SUBOPTIMAL/SUCCESS/TIMEOUT）とfailure code（DEVICE_LOST/OUT_OF_DATE/SURFACE_LOST等）に合わせた。
- [`vkGetPastPresentationTimingEXT`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html)、[`vkGetSwapchainTimingPropertiesEXT`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimingPropertiesEXT.html)、[`vkGetSwapchainTimeDomainPropertiesEXT`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimeDomainPropertiesEXT.html)のAPI固有return contractをclassifierへ反映した。
- [`VK_EXT_present_timing` proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html)のrequired `VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT` contractを維持した。

## 検証結果

| Gate | Result | Evidence |
|---|---|---|
| macOS Vulkan full build, developer features ON | PASS | `tools/build/macos/build-macos-vulkan.sh --build-only --with-metal --jobs 4` |
| macOS Vulkan full build, developer features OFF | PASS | `tools/build/macos/build-macos-vulkan.sh --build-only --release --with-metal --build-dir build-mac-vulkan-devguard-off --jobs 4` |
| Vulkan present timing model tests | PASS | ON/OFF full builds and direct target both report `Vulkan present timing model tests PASS` |
| low-latency contract audit | PASS | `python3 tools/ci/audits/audit-low-latency-contract.py` |
| aggregate Vulkan latency tests | PASS | `python3 tools/testing/aggregate-vulkan-latency-tests.py` |
| Software parity audit | PASS | `python3 tools/ci/audits/audit-raster-software-parity.py` |
| whitespace/diff audit | PASS | `git diff --check` |
| Linux Vulkan build | NOT RUN | VMは起動中だがGuest Additions未報告、GuestControlのユーザーログオン不能（`VBOX_E_IPRT_ERROR`）。build開始前に中断。 |
| Windows Vulkan build | NOT RUN | macOS hostにMSYS2/MinGW cross toolchainがない。 |
| validation layer / physical runtime endurance | NOT RUN | 対象GPU上のresize/minimize/restore、validation layer、長時間present実行は今回未実施。 |

## 判定

```text
P2-1 mixed EXT telemetry + GOOGLE target:
    CLOSED on source/model

P2-2 GOOGLE + FIFO_LATEST_READY:
    CLOSED on source/model

EXT/GOOGLE fatal query routing:
    CLOSED on source/model/contract

WaitForPresent2 SURFACE_LOST:
    CLOSED on source/model/contract

WaitForPresent2 SUBOPTIMAL:
    CLOSED on source/model/contract

API-specific pure query contracts:
    CLOSED on source/model/contract

Linux / Windows / physical validation gates:
    NOT RUN (environment limitation, not source failure)
```

変更はVulkan present pacingとそのテスト・監査に限定し、Software/OpenGL/Metal/Metal Compute/DX12、ゲームロジック、入力、音声、ROM patchは変更していない。
