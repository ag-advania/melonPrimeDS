# Vulkan選択時に即時クラッシュする問題の調査結果

**更新日:** 2026-07-14  
**対象リポジトリ:** [ag-advania/melonPrimeDS](https://github.com/ag-advania/melonPrimeDS)  
**対象ブランチ:** `highres_fonts_v3`  
**版:** 最新push反映版

## 0. 最新確認対象

今回、GitHub上の`highres_fonts_v3`を再取得して確認した。

| ファイル | GitHub blob SHA |
|---|---|
| `src/GPU3D_Vulkan.h` | `3e16724ca5bb5af57b68f3c8612b53a00a26653b` |
| `src/GPU3D_Vulkan.cpp` | `855e18cd18cd159e92d5bec70dff69f3095c5936` |
| `tests/vulkan/reference/phase13-completion-verification.md` | `e12e6944eb04301e044110f41d7c716d28c31d23` |

`GPU3D_Vulkan.cpp`のGitHub上の更新日時は`2026-07-14T13:17:21Z`。

---

## 1. 最新結論

最新push後も、`VulkanRenderer3D::Init()`は軽量な初期化ではなく、起動時の完全検証処理である`EnsureVulkanReadyForValidation()`を直接呼び出している。

```cpp
bool Init() override
{
    return EnsureVulkanReadyForValidation();
}
```

`EnsureVulkanReadyForValidation()`は、Vulkan context、pipeline、descriptorなどの作成だけでは終了しない。

256×192の検証用render targetと複数のbufferを準備し、最後に`dispatchRasterAndReadback()`を呼んでsynthetic frameをGPUへ投入する。

したがって、現在も最有力原因は次の経路である。

```text
Vulkan選択
→ VulkanRenderer3D::Init()
→ EnsureVulkanReadyForValidation()
→ validation resource作成
→ descriptor更新
→ synthetic raster dispatch
→ GPU driver／shader compiler／pipeline compiler／command submission経路で異常終了
```

ただし、crash dumpまたはクラッシュ直前ログが未取得のため、**正確なクラッシュ命令は未確定**である。

---

## 2. 最新push後も変わっていない確定事項

### 2.1 `Init()`は現在も完全検証を呼ぶ

最新の`src/GPU3D_Vulkan.h`では、現在も次の実装になっている。

```cpp
bool Init() override { return EnsureVulkanReadyForValidation(); }
```

したがって、renderer選択時にsynthetic validation pathへ入る構造は残っている。

### 2.2 validation pathは実描画まで行う

最新の`EnsureVulkanReadyForValidation()`は、概ね次の処理を実行する。

```cpp
bool VulkanRenderer3D::EnsureVulkanReadyForValidation()
{
    if (!ensureInitialized())
        return false;

    constexpr u32 kValidationWidth = 256;
    constexpr u32 kValidationHeight = 192;

    if (!ensureRenderTarget(kValidationWidth, kValidationHeight))
        return false;
    if (!ensureResultBuffer(kValidationWidth, kValidationHeight))
        return false;
    if (!ensureTriangleBuffer(nullptr, 1))
        return false;
    if (!ensureGraphicsVertexBuffer(nullptr, 1))
        return false;
    if (!ensureGraphicsSceneVertexBuffer(nullptr, 1))
        return false;
    if (!ensureGraphicsEdgeIndexBuffer(nullptr, 1))
        return false;
    if (!ensureBinMaskBuffer(0, kValidationWidth, kValidationHeight))
        return false;
    if (!ensureGroupListBuffer(0, kValidationWidth, kValidationHeight))
        return false;
    if (!ensureSpanSetupBuffer(1))
        return false;
    if (!ensureWorkOffsetBuffer(kValidationWidth, kValidationHeight, 1))
        return false;
    if (!ensureToonBuffer(nullptr))
        return false;
    if (!ensureCaptureLineBuffer(nullptr))
        return false;

    const VulkanDeviceProfile& deviceProfile =
        VulkanContext::Get().GetDeviceProfile();

    if (ActiveBackendMode == BackendMode::GraphicsHardware
        && deviceProfile.IsMaliG52Class)
    {
        return true;
    }

    Triangles.clear();
    GraphicsVertices.clear();
    ActiveTextureDescriptorCount = 0;
    ActiveTextureDescriptors.fill(VkDescriptorImageInfo{});
    updateDescriptorSet(nullptr);

    return dispatchRasterAndReadback(...);
}
```

つまり、単なる対応確認ではなく、実際のcommand記録、queue submission、同期処理へ進む。

### 2.3 GraphicsHardwareではgraphics raster pathへ入る

`dispatchRasterAndReadback()`は、現在の`GraphicsHardware` backendでは`dispatchGraphicsRasterAndReadback()`へ分岐する。

```cpp
if (ActiveBackendMode == BackendMode::GraphicsHardware)
{
    return dispatchGraphicsRasterAndReadback(...);
}
```

よって、起動時validationは実際のgraphics pipelineを使用する。

---

## 3. Mali G52特例が示す既知リスク

現在のコードには、Mali G52系GPUだけsynthetic validation drawをスキップする特例がある。

コードコメントでは、synthetic validation drawがMali G52のdriver faultを引き起こすため、実際の検証をgameplay開始後の最初のprepared frameへ遅延させると説明されている。

この特例から、次の点が確定する。

1. synthetic validation drawは通常のgameplay frameとは異なる特殊な起動時経路である。
2. 少なくとも一部のGPU driverでは、この経路がdriver faultを起こし得る。
3. 起動時にsynthetic drawを実行しなくても、最初の実frameでrendererを検証できる。
4. 現在の回避対象はMali G52だけであり、他のGPU／driverで同種の問題が起きる余地が残る。

---

## 4. 最新pushで確認できたPhase 13の耐障害性

最新コードでは、runtime中にdevice lossを検出した場合、死んだ`VkDevice`へ新しいcommandを送り続けない処理が追加・明確化されている。

```cpp
if (VulkanContext::Get().IsDeviceLost())
    return;
```

Phase 13のdeveloper harnessでは、次の項目が検証対象になっている。

- VSync ON／OFF policy
- VSync intervalとframe limitの相互作用
- Fast Forward／Slow Motion中のlatest-frame drop
- audio syncとの独立性
- surface present modeの列挙
- FIFOおよびlow-latency swapchain creation
- 保存済みrendererを変更しないprocess-local device-loss fallback
- one-shot OSD policy
- stale Vulkan resource generationの拒否
- pipeline cacheのcold／warm path
- pipeline cacheのatomic replacement
- required pipelineのprewarm
- missing pipeline variantの明示的失敗
- pending acquire deadlockの防止
- stale output presentationの防止

これは前回版より強いruntime保護である。

ただし、次のようなhard crashでは、通常のdevice-loss fallbackへ到達できない可能性がある。

- GPU driver DLL内部のaccess violation
- shader compiler内部の異常終了
- pipeline compiler内部の異常終了
- Vulkan ICD／loader内部の未処理例外
- application側の未定義動作
- OSによるプロセス終了
- `vkQueueSubmit()`後にdriverがプロセスごと停止するケース

したがって、Phase 13のdevice-loss対策と、今回の起動時synthetic validation crash仮説は矛盾しない。

---

## 5. 現在の起動処理の分類

### 5.1 `ensureInitialized()`で行われる処理

最新の`ensureInitialized()`では、少なくとも次を行う。

```text
VulkanContext::Acquire()
command object作成
sync object作成
descriptor object作成
compute pipeline作成
graphics descriptor object作成
graphics pipeline作成
Initializedフラグ設定
```

概略:

```cpp
if (!VulkanContext::Get().Acquire())
    return false;

if (!createCommandObjects()
    || !createSyncObjects()
    || !createDescriptorObjects()
    || !createComputePipeline())
{
    return false;
}

if (!createGraphicsDescriptorObjects())
    return false;

if (!createGraphicsPipelines())
    return false;

Initialized = true;
```

### 5.2 `EnsureVulkanReadyForValidation()`で追加される処理

`ensureInitialized()`後に、さらに次を強制する。

```text
256×192 render target
result buffer
triangle buffer
graphics vertex buffer
graphics scene vertex buffer
graphics edge index buffer
bin mask buffer
group list buffer
span setup buffer
work offset buffer
toon buffer
capture line buffer
validation descriptor更新
synthetic graphics raster dispatch
```

この追加部分が、今回最初に切り離すべき範囲である。

---

## 6. 前回版からの重要な訂正

前回版では、`Init()`を`ensureInitialized()`だけに変更すればGPU submissionが一切なくなるようにも読める説明があった。

これは正確ではない。

`ensureInitialized()`系のresource初期化中にも、fallback texture uploadなどでcommand bufferを記録し、`vkQueueSubmit()`と`vkWaitForFences()`を行う経路が存在する。

したがって、次の修正で除去できるのは、主に**256×192のsynthetic raster／readback完全検証**である。

```cpp
bool VulkanRenderer3D::Init()
{
    return ensureInitialized();
}
```

この修正後も即落ちする場合、原因は次へ絞られる。

```text
VulkanContext取得
device／queue初期化
command／sync作成
descriptor作成
compute／graphics pipeline作成
fallback texture upload
surface／presenter初期化
frontend backend切替
```

---

## 7. 現時点の判定表

| 項目 | 判定 |
|---|---|
| Vulkan選択時にrendererの`Init()`が呼ばれる | 確定 |
| `Init()`が`EnsureVulkanReadyForValidation()`を呼ぶ | 確定 |
| validation用256×192 resourceを作る | 確定 |
| validation pathが実際のraster dispatchを行う | 確定 |
| GraphicsHardwareでgraphics pathを使用する | 確定 |
| Mali G52で既知driver fault回避がある | 確定 |
| runtime device-loss後の追加submit抑止がある | 確定 |
| process-local fallbackがPhase 13の検証対象である | 確定 |
| 今回の即落ちがsynthetic validation内で発生する | 最有力仮説 |
| 正確なVulkan API／driver命令 | 未確定 |

---

## 8. 推奨する最小切り分け修正

### 8.1 `GPU3D_Vulkan.h`

現在:

```cpp
bool Init() override { return EnsureVulkanReadyForValidation(); }
```

推奨:

```cpp
bool Init() override;
```

### 8.2 `GPU3D_Vulkan.cpp`

追加:

```cpp
bool VulkanRenderer3D::Init()
{
    return ensureInitialized();
}
```

この修正で維持されるもの:

- Vulkan context初期化
- command／sync object作成
- descriptor作成
- compute／graphics pipeline作成
- Phase 13のdevice-loss処理
- gameplay開始後の実frame描画

通常起動から外れるもの:

- validation専用256×192 target
- validation専用bufferの強制準備
- validation descriptor更新
- synthetic raster dispatch
- synthetic validationの完了待機

---

## 9. 修正後の結果の読み方

### ケースA: 即落ちしなくなる

synthetic validation pathのどこかが原因である可能性が非常に高くなる。

主な候補:

```text
validation render target
validation buffer
validation descriptor update
synthetic command recording
vkQueueSubmit
vkWaitForFences
validation用barrier／layout
空geometry状態でのgraphics pipeline実行
driver shader execution
```

### ケースB: まだ即落ちする

原因は`ensureInitialized()`またはfrontend側にある。

主な候補:

```text
VulkanContext::Acquire()
logical device作成
queue取得
command pool／command buffer作成
fence／semaphore作成
descriptor layout／pool作成
compute pipeline作成
graphics pipeline作成
fallback texture upload
surface作成
presentation queue解決
presenter初期化
panel／renderer lifetime
```

---

## 10. 推奨する恒久実装

起動時検証を2段階へ分割する。

### Level A: 通常起動のreadiness check

```text
VulkanContext取得
command／sync作成
descriptor作成
pipeline作成
必須静的resource作成
```

### Level B: 開発者向けsynthetic execution test

synthetic raster／readbackは、明示的に有効化した場合だけ実行する。

例:

```text
MELONPRIME_VULKAN_STARTUP_SELFTEST=1
```

実装例:

```cpp
bool VulkanRenderer3D::Init()
{
    if (!ensureInitialized())
        return false;

    if (ShouldRunVulkanStartupSelfTest())
        return runStartupSyntheticValidation();

    return true;
}
```

推奨する責務分離:

```cpp
bool VulkanRenderer3D::Init();
bool VulkanRenderer3D::ensureInitialized();
bool VulkanRenderer3D::runStartupSyntheticValidation();
bool VulkanRenderer3D::ShouldRunVulkanStartupSelfTest() const;
```

---

## 11. 追加すべき段階ログ

```text
Vulkan startup: context acquire begin
Vulkan startup: context acquire complete

Vulkan startup: command objects begin
Vulkan startup: command objects complete

Vulkan startup: sync objects begin
Vulkan startup: sync objects complete

Vulkan startup: descriptor objects begin
Vulkan startup: descriptor objects complete

Vulkan startup: compute pipelines begin
Vulkan startup: compute pipelines complete

Vulkan startup: graphics descriptors begin
Vulkan startup: graphics descriptors complete

Vulkan startup: graphics pipelines begin
Vulkan startup: graphics pipelines complete

Vulkan startup: fallback texture begin
Vulkan startup: fallback texture submit begin
Vulkan startup: fallback texture submit complete
Vulkan startup: fallback texture fence wait begin
Vulkan startup: fallback texture fence wait complete

Vulkan self-test: render target begin
Vulkan self-test: render target complete

Vulkan self-test: buffers begin
Vulkan self-test: buffers complete

Vulkan self-test: descriptor update begin
Vulkan self-test: descriptor update complete

Vulkan self-test: command recording begin
Vulkan self-test: command recording complete

Vulkan self-test: queue submit begin
Vulkan self-test: queue submit complete

Vulkan self-test: fence wait begin
Vulkan self-test: fence wait complete
```

クラッシュ直前の行が残るよう、重要ログはflushする必要がある。

---

## 12. 優先順位付き実施計画

### 優先度1

`Init()`からsynthetic validationを外す。

対象:

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
```

### 優先度2

`ensureInitialized()`を段階ログ化する。

対象:

```text
src/GPU3D_Vulkan.cpp
src/VulkanContext.cpp
```

### 優先度3

frontend切替を段階ログ化する。

対象候補:

```text
src/frontend/qt_sdl/VideoSettingsDialog.cpp
src/frontend/qt_sdl/Window.cpp
src/frontend/qt_sdl/EmuThread.cpp
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.cpp
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp
```

### 優先度4

Windows crash artifactを取得する。

必要情報:

```text
例外コード
faulting module
faulting address
call stack
GPU名
GPU driver version
Vulkan loader version
最後のstartup stage log
```

---

## 13. 原因候補ランキング

| 順位 | 原因候補 | 状態 |
|---:|---|---|
| 1 | 起動時synthetic validation draw | 最有力 |
| 2 | graphics pipeline作成／driver compiler | 未確定 |
| 3 | fallback texture uploadのsubmit／wait | 未確定 |
| 4 | Vulkan surface／presenter初期化 | 未確定 |
| 5 | validation descriptor／resource state不整合 | 未確定 |
| 6 | frontend切替中のlifetime問題 | 証拠不足 |
| 7 | pipeline cache破損 | 優先度低 |

---

## 14. Phase 13との責務分離

```text
Phase 13
├─ runtime device-loss検出
├─ dead deviceへの追加submit停止
├─ process-local fallback
├─ stale generation拒否
├─ pipeline cache保護
└─ presentation deadlock防止

今回の修正
└─ renderer選択直後のsynthetic full drawを通常起動から分離
```

Phase 13の処理は維持する。

今回の修正は、Phase 13を無効化するものではなく、startup中のhard crash候補を減らすためのもの。

---

## 15. 最終判断

最新push後も、Vulkan renderer選択時にsynthetic validation drawを強制実行する構造は残っている。

さらに、コード内にはMali G52で同validation drawを回避する既知driver fault対策が存在する。

したがって、最初に適用すべき切り分けは次のとおり。

> `VulkanRenderer3D::Init()`からsynthetic raster／readback検証を外し、通常起動では`ensureInitialized()`までに留める。

結果は次のように判断できる。

```text
修正後に起動する
→ synthetic validation pathが原因である可能性が非常に高い

修正後も即落ちする
→ context／pipeline／fallback upload／surface／frontend切替を調査
```

恒久的には、synthetic validationを開発者向けの明示的self-testへ移し、通常利用者のrenderer選択経路では実行しない設計が適切である。

---

## 16. 実施済み

- GitHub上の最新`highres_fonts_v3`再調査
- 最新`GPU3D_Vulkan.h`確認
- 最新`GPU3D_Vulkan.cpp`確認
- 起動時validation経路の再確認
- Mali G52特例の再確認
- Phase 13 device-loss処理の確認
- Phase 13 completion verification文書の確認
- 調査MDの最新化

---

## 17. 未実施

- ソースコード修正
- 修正commit作成
- GitHubへの追加push
- Windows build
- Linux build
- macOS build
- Vulkan renderer実機起動確認
- synthetic validation無効化後の比較試験
- Vulkan validation layer実行
- crash dump取得
- faulting module特定
- GPU vendor別検証
- driver version別検証
- Software／OpenGLからVulkanへの切替試験
- VulkanからSoftware／OpenGLへの復帰試験
- pipeline cache削除前後の比較試験
