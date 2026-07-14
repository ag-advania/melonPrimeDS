# ROM起動時および映像設定のRenderer選択時に即時クラッシュする問題の追加調査結果

**作成日:** 2026-07-15  
**対象リポジトリ:** [ag-advania/melonPrimeDS](https://github.com/ag-advania/melonPrimeDS)  
**対象ブランチ:** `highres_fonts_v3`  
**確認時ブランチHEAD:** `8639ce9e892ed79b0837a97263d48db629ee9ce5`  
**主要実装commit:** `130e0714c4980fd3d0295b29107cf5f00da54923`（Vulkan R26）  
**症状:**

1. ROMを開いた瞬間にプロセスが終了する。
2. ROM未起動状態で、映像設定からSoftwareまたはVulkanを選択した瞬間にプロセスが終了する。
3. `VulkanRenderer3D::Init()`からsynthetic validationを外しても改善しない。

---

## 1. 結論

今回の再現条件を最新コードへ当てはめると、クラッシュ原因はVulkanのsynthetic raster dispatchだけでは説明できません。

追加調査により、**二つの独立したクラッシュ可能経路**が確認できました。

### 原因A: 映像設定変更時の確定的なnull pointer dereference

ROM未起動状態では`EmuInstance::nds`は`nullptr`です。

しかし、映像設定でpresentation backendが変わると、`msg_ApplyVideoBackendSwitch`が次を実行します。

```cpp
auto result = MelonPrime::VideoBackend::CreateRendererForSelection(
    *emuInstance->nds,
    cfg.GetInt("3D.Renderer"),
    cfg.GetBool("Screen.UseGL"));
```

`emuInstance->nds`のnull確認がありません。

そのため、ROMを一度も起動していない状態でSoftware／Vulkanを切り替えると、`*emuInstance->nds`で即時にnull pointer dereferenceが発生します。

### 原因B: ROM起動時のGPU constructor内にある未定義動作

新しい`NDS`を作る際、`NDSArgs::Renderer`は既定値の`nullptr`です。

```cpp
std::unique_ptr<melonDS::Renderer> Renderer = nullptr;
```

`NDS` constructorは、その`nullptr`を`GPU` constructorへ渡します。

```cpp
GPU(*this, std::move(args.Renderer))
```

`GPU` constructorは、まだrendererが存在しない状態で直ちに`SetRenderer(nullptr)`を呼びます。

```cpp
GPU::GPU(NDS& nds, std::unique_ptr<Renderer>&& renderer) noexcept
    : NDS(nds),
      GPU2D_A(0, *this),
      GPU2D_B(1, *this),
      GPU3D(*this)
{
    ...
    SetRenderer(std::move(renderer));
}
```

ところが`GPU::SetRenderer()`の先頭では、`Rend`のnull確認より先に`SyncAllVRAMCaptures()`を呼びます。

```cpp
void GPU::SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept
{
    SyncAllVRAMCaptures();

    GPU3D.SetCurrentRenderer(nullptr);

    ...
}
```

さらに、`SyncAllVRAMCaptures()`が読む`VRAMCaptureBlockFlags`は初期化されていません。

```cpp
u16 VRAMCaptureBlockFlags[16];
```

`Rend`自体は、この時点ではまだ`nullptr`です。

```cpp
std::unique_ptr<Renderer> Rend = nullptr;
```

`VRAMCaptureBlockFlags`の未初期化値に`CBFlag_IsCapture`が偶然含まれていると、次が実行されます。

```cpp
Rend->SyncVRAMCapture(bank, start, len, complete);
```

したがって、ROM起動時には次の経路が成立します。

```text
ROMを開く
→ EmuInstance::updateConsole()
→ new NDS(...)
→ NDS constructor
→ GPU constructor
→ SetRenderer(nullptr)
→ SyncAllVRAMCaptures()
→ 未初期化VRAMCaptureBlockFlagsを読む
→ garbage値をcapture flagとして解釈
→ nullのRendをdereference
→ 即時クラッシュ
```

これは単なる推測ではなく、コード上に存在する明確な未定義動作です。

---

## 2. 前回修正が効かなかった理由

前回は次の修正を実施しました。

```cpp
bool VulkanRenderer3D::Init()
{
    return ensureInitialized();
}
```

これにより、起動時の256×192 synthetic raster dispatchは通常の`Init()`経路から外れました。

しかし今回の症状には、Vulkan rendererへ到達する前の共通経路が含まれています。

### ROM起動時

クラッシュ候補は新しい`NDS`／`GPU`を構築している最中です。

この時点では、Vulkan rendererの生成より前に`GPU::SetRenderer(nullptr)`が実行されます。

### 映像設定変更時

ROM未起動では`nds == nullptr`のため、Vulkan rendererを生成する前に`*emuInstance->nds`で落ちます。

したがって、synthetic validationを外しても、今回確認された二つの経路には影響しません。

---

## 3. 映像設定変更時の詳細経路

### 3.1 Dialogは選択した瞬間に設定を反映する

`VideoSettingsDialog::onChange3DRenderer()`は、radio buttonを押した時点で`3D.Renderer`を更新し、直ちに`updateVideoSettings`をemitします。

```cpp
void VideoSettingsDialog::onChange3DRenderer(int renderer)
{
    const int oldPresentation = presentationBackendId();

    auto& cfg = emuInstance->getGlobalConfig();
    renderer = MelonPrime::VideoBackend::MigrateLegacyRendererId(renderer);
    cfg.SetInt("3D.Renderer", renderer);

    setEnabled();

    emit updateVideoSettings(
        oldPresentation != presentationBackendId());
}
```

OKボタンを押すまで待つ設計ではありません。

### 3.2 MainWindowがbackend switchを開始する

presentation backendが変わる場合、`MainWindow::onUpdateVideoSettings()`は次を行います。

```text
emuPause()
beginBackendSwitch()
screen panel再生成
applyVideoBackendSwitch()
emuUnpause()
```

### 3.3 EmuThreadでnullのNDSを参照する

`applyVideoBackendSwitch()`はmessageを送り、EmuThread側の`msg_ApplyVideoBackendSwitch`へ到達します。

そこで、ROMがまだ起動されていないにもかかわらず、次を実行します。

```cpp
CreateRendererForSelection(
    *emuInstance->nds,
    ...
);
```

`EmuInstance` constructorでは明示的に次が設定されています。

```cpp
nds = nullptr;
```

したがって、ROM未起動状態のbackend switchは安全に処理できません。

---

## 4. ROM起動時の詳細経路

### 4.1 Rendererは既定でnull

`NDSArgs`ではrendererが次のように定義されています。

```cpp
std::unique_ptr<melonDS::Renderer> Renderer = nullptr;
```

### 4.2 NDS constructorがnull rendererをGPUへ渡す

```cpp
NDS::NDS(NDSArgs&& args, int type, void* userdata) noexcept
    :
    ...
    GPU(*this, std::move(args.Renderer)),
    ...
{
}
```

### 4.3 GPU constructorが初期化前にSetRendererを実行する

```cpp
GPU::GPU(NDS& nds, std::unique_ptr<Renderer>&& renderer) noexcept
    :
    NDS(nds),
    GPU2D_A(0, *this),
    GPU2D_B(1, *this),
    GPU3D(*this)
{
    ...
    SetRenderer(std::move(renderer));
}
```

### 4.4 SetRendererが旧rendererの存在確認なしでcapture同期する

```cpp
void GPU::SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept
{
    SyncAllVRAMCaptures();

    ...
}
```

初回構築時には、同期対象となる旧rendererは存在しません。

### 4.5 Capture flagsも未初期化

現在:

```cpp
u16 VRAMCaptureBlockFlags[16];
```

正しくは少なくとも次である必要があります。

```cpp
u16 VRAMCaptureBlockFlags[16] {};
```

### 4.6 SyncAllVRAMCapturesがRendを使用する

```cpp
void GPU::SyncAllVRAMCaptures()
{
    for (u32 b = 0; b < 16; b++)
    {
        u16 flags = VRAMCaptureBlockFlags[b];

        if (!(flags & CBFlag_IsCapture))
            continue;
        if (flags & CBFlag_Synced)
            continue;

        ...

        Rend->SyncVRAMCapture(...);
    }
}
```

初回constructor内では`Rend == nullptr`です。

よって、未初期化flagsがcaptureとして判定されれば即時にaccess violationとなります。

---

## 5. 判定表

| 問題 | 判定 | 根拠 |
|---|---|---|
| `VulkanRenderer3D::Init()`のsynthetic validationが今回の共通原因 | 否定 | Software選択時およびVulkan renderer生成前にも落ちる |
| ROM未起動時の`*emuInstance->nds` | 確定不具合 | null確認なしで参照している |
| `VRAMCaptureBlockFlags`の未初期化 | 確定不具合 | 配列に`{}`がない |
| 初回`SetRenderer()`時の`Rend == nullptr` | 確定 | member初期値が`nullptr` |
| 初回`SetRenderer()`が`SyncAllVRAMCaptures()`を呼ぶ | 確定 | 関数先頭で無条件実行 |
| ROM起動時に未定義動作が発生し得る | 確定 | 未初期化readとnull dereference可能経路がある |
| 報告されたROM起動クラッシュの正確なfaulting instruction | crash dump未取得のため未確定 | ただし最有力経路は上記constructor path |

---

## 6. 修正案A: ROM未起動時のbackend switchを安全にする

対象:

```text
src/frontend/qt_sdl/EmuThread.cpp
```

`msg_ApplyVideoBackendSwitch`で、rendererを生成する前に`nds`を確認します。

### 推奨実装

```cpp
case msg_ApplyVideoBackendSwitch:
{
    const auto stagedPresentation =
        static_cast<MelonPrime::VideoBackend::PresentationBackend>(
            msg.param.value<int>());

    auto& cfg = emuInstance->getGlobalConfig();

    if (emuInstance->nds == nullptr)
    {
        // No console exists yet. Commit only the staged presentation state.
        // The actual renderer transaction will be performed after NDS creation,
        // because updateVideoRenderer() forces lastVideoRenderer = -1.
        videoBackend = stagedPresentation;
        videoSettingsDirty = true;
        lastVideoRenderer = -1;

#if defined(MELONPRIME_ENABLE_VULKAN)
        auto& session = emuInstance->vulkanFrontendSession();
        session.completeBackendSwitch(
            videoBackend
                == MelonPrime::VideoBackend::PresentationBackend::Vulkan);

        if (videoBackend
            != MelonPrime::VideoBackend::PresentationBackend::Vulkan)
        {
            session.shutdown();
        }
#endif

        msgResult = static_cast<int>(videoBackend);
        break;
    }

    auto result =
        MelonPrime::VideoBackend::CreateRendererForSelection(
            *emuInstance->nds,
            cfg.GetInt("3D.Renderer"),
            cfg.GetBool("Screen.UseGL"));

    ...
}
```

### この修正の目的

ROM未起動状態では、以下を行いません。

- `NDS&`を必要とするrenderer生成
- `GPU::SetRenderer`
- `GPU3D::SetCurrentRenderer`
- renderer settings適用
- actual renderer capability評価

この時点ではpresentation panelと設定値だけを確定し、実rendererの生成はNDS作成後へ遅延させます。

`EmuInstance::updateConsole()`は新しいNDS作成後に`updateVideoRenderer()`を呼びます。

```cpp
void updateVideoRenderer()
{
    videoSettingsDirty = true;
    lastVideoRenderer = -1;
}
```

そのため、ROM起動後には必ず正式なrenderer transactionが実行されます。

---

## 7. 修正案B: GPU constructorの未定義動作を除去する

対象:

```text
src/GPU.h
src/GPU.cpp
```

### 7.1 Capture flagsをゼロ初期化する

現在:

```cpp
u16 VRAMCaptureBlockFlags[16];
```

修正:

```cpp
u16 VRAMCaptureBlockFlags[16] {};
```

これにより、初回`SyncAllVRAMCaptures()`がgarbage値をcapture metadataとして扱うことを防ぎます。

### 7.2 旧rendererが存在するときだけ同期する

現在:

```cpp
void GPU::SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept
{
    SyncAllVRAMCaptures();

    ...
}
```

修正:

```cpp
void GPU::SetRenderer(std::unique_ptr<Renderer>&& renderer) noexcept
{
    if (Rend)
        SyncAllVRAMCaptures();

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    GPU3D.SetCurrentRenderer(nullptr);
#endif

    ...
}
```

### 両方必要な理由

`VRAMCaptureBlockFlags[16] {}`だけでも初回のgarbage判定は防げます。

しかし、初回constructorで旧rendererが存在しないにもかかわらず同期関数を呼ぶ設計自体が不自然です。

`if (Rend)`も追加することで、次を明確に分離できます。

```text
初回renderer作成
→ 同期対象なし
→ SyncAllVRAMCapturesを呼ばない

runtime renderer切替
→ 旧rendererあり
→ 未同期captureを旧rendererからVRAMへ同期
→ rendererを破棄
```

ゼロ初期化とnull guardの両方を入れるのが安全です。

---

## 8. 推奨修正順

### 第1段階

次の三点だけを修正します。

```text
1. VRAMCaptureBlockFlagsをゼロ初期化
2. GPU::SetRenderer()のSyncAllVRAMCaptures()をif (Rend)で保護
3. msg_ApplyVideoBackendSwitchでnds == nullptrを処理
```

この段階ではVulkan pipeline、presenter、surface、shaderには触れません。

### 第2段階

以下を個別に検証します。

```text
A. アプリ起動
B. ROM未起動でSoftwareを選択
C. ROM未起動でVulkanを選択
D. Software設定でROMを開く
E. Vulkan設定でROMを開く
F. ROM実行中にSoftwareからVulkanへ変更
G. ROM実行中にVulkanからSoftwareへ変更
H. ROM停止後に再度ROMを開く
```

### 第3段階

第1段階の修正後もVulkanだけが落ちる場合に限り、次を再調査します。

```text
VulkanContext::Acquire()
graphics pipeline作成
fallback texture upload
VulkanSurfacePresenter::init()
surface作成
present queue解決
swapchain作成
```

---

## 9. 推奨ログ

### 9.1 NDS／GPU構築

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] NDS construction: GPU begin\n");
```

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] GPU::SetRenderer begin old=%p requested=%p\n",
    Rend.get(), renderer.get());
```

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] GPU::SetRenderer capture sync begin old=%p\n",
    Rend.get());
```

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] GPU::SetRenderer capture sync complete\n");
```

### 9.2 Video backend switch

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] backend switch staged=%d nds=%p\n",
    static_cast<int>(stagedPresentation),
    static_cast<void*>(emuInstance->nds));
```

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] backend switch deferred: no NDS\n");
```

### 9.3 Renderer transaction

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] renderer factory begin requested=%d\n",
    cfg.GetInt("3D.Renderer"));
```

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] outer renderer install begin\n");
```

```cpp
Log(LogLevel::Info,
    "[RendererCrashTrace] independent Renderer3D install begin\n");
```

ログは即時flushされる出力先へ記録します。

---

## 10. Test matrix

| No. | 初期状態 | 操作 | 期待結果 |
|---:|---|---|---|
| 1 | ROMなし／Software | Softwareを再選択 | クラッシュしない |
| 2 | ROMなし／Software | Vulkanを選択 | 設定とpanelだけ切替、renderer生成は遅延 |
| 3 | ROMなし／Vulkan | Softwareを選択 | クラッシュせずNativeQtへ切替 |
| 4 | ROMなし／Software | ROMを開く | NDS／GPU構築成功 |
| 5 | ROMなし／Vulkan | ROMを開く | NDS構築後にVulkan renderer生成 |
| 6 | ROM実行中／Software | Vulkanを選択 | transaction成功またはSoftware fallback |
| 7 | ROM実行中／Vulkan | Softwareを選択 | Vulkan resourceを安全に破棄 |
| 8 | ROM実行中 | ROM停止 | renderer／sessionを安全に保持または破棄 |
| 9 | ROM停止後 | 別ROMを開く | stale resourceなしで再起動 |
| 10 | Vulkan初期化失敗環境 | Vulkanを選択 | プロセス終了せずSoftware fallback |

---

## 11. 追加で注意すべき点

### 11.1 Screen panelはNDSより先に存在できる

`EmuInstance` constructorでは、NDS作成前にwindowが作られます。

```text
nds = nullptr
VulkanFrontendSession作成
window作成
createScreenPanel()
emuThread開始
```

そのため、presentation backendとemulation rendererは別のlifecycleとして扱う必要があります。

```text
presentation panel
→ ROMなしでも存在可能

NDS／GPU renderer
→ ROMまたはfirmware起動後にのみ存在
```

現在の`msg_ApplyVideoBackendSwitch`は、この違いを考慮せず常にNDS renderer transactionを実行しています。

### 11.2 Synthetic validationの修正は無駄ではない

前回の修正は、起動時に不要なsynthetic full drawを強制しないという意味では妥当です。

ただし、今回の即時クラッシュの共通原因ではありませんでした。

synthetic validationは次のように扱うのが適切です。

```text
通常起動
→ ensureInitialized()

開発者向け明示self-test
→ EnsureVulkanReadyForValidation()
```

### 11.3 Vulkanだけが残って落ちる場合

今回の三修正後にSoftwareとROM起動が正常化し、Vulkanだけが落ちる場合は、初めて`ensureInitialized()`内部を主対象にします。

その場合の優先順位は次のとおりです。

```text
1. VulkanContext::Acquire()
2. createGraphicsPipelines()
3. persistent pipeline cache
4. fallback texture upload
5. VulkanSurfacePresenter::init()
6. surface／present queue
7. first gameplay frame
```

---

## 12. 最終判断

今回の報告に対する最も整合的な説明は、次の二つです。

### 映像設定からSoftware／Vulkanを選ぶと落ちる

```text
ROM未起動
→ emuInstance->nds == nullptr
→ msg_ApplyVideoBackendSwitch
→ *emuInstance->nds
→ null pointer dereference
```

### ROMを開くと落ちる

```text
new NDS
→ GPU constructor
→ SetRenderer(nullptr)
→ SyncAllVRAMCaptures()
→ 未初期化VRAMCaptureBlockFlags
→ nullのRendを使用する可能性
→ undefined behavior／access violation
```

したがって、最初に修正すべき箇所はVulkan shaderやpipelineではありません。

```text
src/frontend/qt_sdl/EmuThread.cpp
src/GPU.h
src/GPU.cpp
```

推奨する最小修正は次の三点です。

```cpp
// GPU.h
u16 VRAMCaptureBlockFlags[16] {};
```

```cpp
// GPU.cpp
if (Rend)
    SyncAllVRAMCaptures();
```

```cpp
// EmuThread.cpp
if (emuInstance->nds == nullptr)
{
    // Commit/defer presentation only.
    // Do not construct a renderer without an NDS.
    ...
}
```

---

## 13. 実施済み

- 最新`highres_fonts_v3`の再取得
- synthetic validation除去修正の反映確認
- ROM起動経路の追跡
- `NDSArgs`から`GPU` constructorまでの追跡
- `GPU::SetRenderer()`の初回呼び出し条件確認
- `VRAMCaptureBlockFlags`の初期化状態確認
- `SyncAllVRAMCaptures()`のnull renderer使用可能性確認
- 映像設定signalからbackend switchまでの追跡
- ROM未起動時の`nds == nullptr`確認
- `msg_ApplyVideoBackendSwitch`のnull guard不存在確認
- 修正案およびtest matrix作成

---

## 14. 未実施

- ソースコード修正
- commit作成
- GitHubへのpush
- Windows build
- 実機でのROM起動試験
- 実機でのSoftware／Vulkan切替試験
- crash dump取得
- faulting instruction確認
- Vulkan validation layer実行
