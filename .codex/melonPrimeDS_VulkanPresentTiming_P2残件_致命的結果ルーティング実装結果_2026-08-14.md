# melonPrimeDS Vulkan Present Timing P2残件
## 致命的VkResultライフサイクル・ルーティング実装結果

- 実施日: 2026-08-14
- 対象Branch: `develop_remakeVulkan_ver3`
- 対象指示書: `melonPrimeDS_VulkanPresentTiming_P2残件_致命的結果ルーティング修正指示書_2026-08-14.md`
- 対象範囲: `src/VulkanPresentPacer.*` / `src/VulkanPresentPacingPolicy.h` / Vulkan present timing tests and contract audit
- 判定: **PASS (source/model/contract/build gates)**

## 結論

present timing queryの結果を、optional timing failureとVulkan lifecycle failureへ分離した。

```text
query VkResult
  -> VulkanPacerBeginResult
  -> VulkanPacerActionFor()
  -> existing presenter/runtime action
```

`VK_ERROR_OUT_OF_DATE_KHR`、`VK_ERROR_DEVICE_LOST`、`VK_ERROR_SURFACE_LOST_KHR`は、`TimingQueryFailed`へ潰さずtyped routeへ伝播する。その他のoptional timing failureだけはtiming機能を停止して`Continue`する。

## 実装内容

### EXT present timing

- `ReportPastTiming()`を`VulkanPacerBeginResult`返却へ変更し、`BeginFrame()`が結果を必ず確認するようにした。
- `vkGetPastPresentationTimingEXT()`の`VK_SUCCESS`/`VK_INCOMPLETE`は継続し、`OUT_OF_DATE`/`DEVICE_LOST`/`SURFACE_LOST`はそれぞれ`SwapchainOutOfDate`/`DeviceLost`/`SurfaceLost`へ送る。
- `RefreshTimingProperties()`をtyped result化した。`VK_NOT_READY`は初回present後の再試行状態として維持し、`SURFACE_LOST`はtyped routeへ送る。
- `RefreshTimeDomains()`をtyped result化し、count queryとarray queryの`SURFACE_LOST`をtyped routeへ送る。
- time-domain queryは`VK_SUCCESS`/`VK_INCOMPLETE`だけをsuccess/enumeration contractとし、`VK_NOT_READY`専用のpending branchを削除した。
- `VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT`をrequired contractとして確認し、domainがない状態をbootstrap pendingと誤認しないようにした。

### GOOGLE display timing

- `RefreshGoogleTiming()`と`ReportGooglePastTiming()`のtyped lifecycle resultを維持・共通分類化した。
- `OnSwapchainCreated()`のeager queryで得たfatal resultを`PendingBeginResult`へラッチし、次の`BeginFrame()`から既存presenter routingへ渡すようにした。logだけで継続する経路は残していない。
- pending resultはswapchain lifecycle resetで消去し、新swapchainのquery結果だけを次のrouting pointへ持ち越す。

### 共通分類・テスト

- `VulkanPresentPacer::ClassifyPresentLifecycleResult()`と`VulkanLatchBeginResult()`をpure helperとして追加した。
- pure testsで`SUCCESS`、`INCOMPLETE`、`NOT_READY`、`OUT_OF_DATE`、`DEVICE_LOST`、`SURFACE_LOST`、optional unknown failure、およびpendingのfirst-fatal保持を確認した。
- source contract auditで、呼び出し側がtyped resultを捨てていないこと、time-domainの`VK_NOT_READY`誤扱いがないこと、count/arrayのtyped routeが存在することを確認した。

## 仕様整合

- [`vkGetPastPresentationTimingEXT`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingEXT.html)の`VK_SUCCESS`/`VK_INCOMPLETE`とfatal failure分類に合わせた。
- [`vkGetSwapchainTimingPropertiesEXT`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimingPropertiesEXT.html)の`VK_NOT_READY` pendingおよび`SURFACE_LOST` routingに合わせた。
- [`vkGetSwapchainTimeDomainPropertiesEXT`](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetSwapchainTimeDomainPropertiesEXT.html)の`VK_SUCCESS`/`VK_INCOMPLETE` contractおよび`SURFACE_LOST` routingに合わせた。
- [`VK_EXT_present_timing` proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html)の`VK_TIME_DOMAIN_PRESENT_STAGE_LOCAL_EXT` required contractを反映した。

## 検証結果

| Gate | Result | Evidence |
|---|---|---|
| macOS Vulkan build, developer features ON | PASS | `tools/build/macos/build-macos-vulkan.sh --build-only --with-metal --jobs 4` |
| macOS Vulkan build, developer features OFF | PASS | `tools/build/macos/build-macos-vulkan.sh --build-only --release --with-metal --build-dir build-mac-vulkan-devguard-off --jobs 4` |
| Vulkan present timing model test | PASS | Both macOS builds run `Vulkan present timing model tests PASS` |
| low-latency contract audit | PASS | `python3 tools/ci/audits/audit-low-latency-contract.py` |
| aggregate Vulkan latency tests | PASS | `python3 tools/testing/aggregate-vulkan-latency-tests.py` |
| Software parity audit | PASS | `python3 tools/ci/audits/audit-raster-software-parity.py` |
| whitespace/diff audit | PASS | `git diff --check` |
| Linux Vulkan build | NOT RUN | VMは起動済みだったがGuest Additions/guestcontrolのログオンが成立せず、wrapperをビルド前に中断。source failureではない。 |
| Windows Vulkan build | NOT RUN | このmacOS hostではMSYS2/MinGW build環境を実行していない。 |
| physical runtime / validation-layer endurance | NOT RUN | 対象GPU・validation layer・長時間present実行環境はこの監査では使用していない。 |

非Vulkan renderer、ゲームロジック、入力、音声、ROM patchには変更を加えていない。
