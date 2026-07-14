# melonPrimeDS Vulkan GPU 3D再構築実装計画
## SapphireRhodonite/melonDS-android 0.7.0.rc4準拠

**作成日:** 2026-07-14  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**参照フロントエンド:** `SapphireRhodonite/melonDS-android` タグ `0.7.0.rc4` `https://github.com/SapphireRhodonite/melonDS-android/releases/tag/0.7.0.rc4`
**参照コア:** `SapphireRhodonite/melonDS-android-lib` コミット `d77944275fa61f9b79cfcead2c3e98993429a023`  
**対象状態:** 現在のVulkanレンダリング経路は破損しているものとして扱う  
**既存計画:** 既存計画書の進行度、完了マーク、phase番号、contract versionを実装済み判定に使わない  
**重大修正反映:** 2026-07-14の所有権再調査、R3作業量再調査、Structured2D実レイアウト調査を本文へ統合済み  
**Sapphire直接移植方針:** 固定commit/tagに同一責務の完成コードがある場合は原文移植を最優先とし、desktop／melonPrime固有差分だけを最小adapterとして適用する  

---

# 0. 実装進捗記録

この記録は各フェーズの変更・検証・引き渡しinterfaceを追跡する証拠台帳であり、記録だけをruntime能力判定には使わない。能力は常に生成済みresourceと有効なruntime stateから判定する。

## R0 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/VulkanReferencePortVersion.h`
   - `src/frontend/qt_sdl/MelonPrimeRendererFactory.h`
   - `src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp`
2. 新しく追加した型、関数、resource
   - `VulkanRuntimeCapabilities`
   - `QueryCurrentVulkanCapabilities()`
   - 固定参照tag/commit定数（runtime制御には不使用）
3. 削除した旧経路
   - 先行R0コミット`d3300293e`でhardcoded shell contractと自己検査CLIを削除済み。
   - 「outer VulkanRendererが存在しないこと」をfallback理由にする誤った説明を削除した。
4. 所有権とframe lifecycleの変更
   - `GPU3D::CurrentRenderer`所有は維持。
   - frontend composition/presentation未接続中は`actual=Software`を維持し、`failedStage`を`Vulkan frontend session`として報告する。
5. 参照実装との差分
   - desktop frontend session未接続のため、structured 2D、final compositor、FrameQueue、surface、presenter capabilityは実体が追加されるまでfalseのままにする。
6. 後続フェーズへ渡すinterface
   - R3以降のsession/resource ownerが生成成功後に各capabilityを実体から埋めるための`VulkanRuntimeCapabilities`。
   - R1の唯一の参照点として`0.7.0.rc4`と`d77944275fa61f9b79cfcead2c3e98993429a023`を定数化。

検証: `git diff --check`成功。Vulkanビルドは`build-mingw-vulkan.bat`を正規手順とし、ユーザー指示によりこのフェーズでは再実行せず実装を優先する。

## R1 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/VulkanReferenceManifest.md`
   - `src/frontend/qt_sdl/VulkanReference/FrameQueue.h`
   - `src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp`
2. 新しく追加した型、関数、resource
   - 固定tagの完全な`FrameQueuePolicy`、frame identity、deadline、drop/reuse/resync、age/statistics実装を復元した。
   - 全core/frontend対象ファイルの参照repository、path、commit/tag、保持差分、削除フェーズを追跡する移植manifestを追加した。
3. 削除した旧経路
   - 18行へ圧縮され、deadline/drop cause/age/policy処理を省略していたdesktop-only簡略`FrameQueue`を置換した。
4. 所有権とframe lifecycleの変更
   - `FrameQueue`はまだ実行経路へ接続せず、R19の正式owner化を先取りしない。
   - queue内部のpending/previous/free/present lifecycleは0.7.0.rc4参照実装へ戻した。
5. 参照実装との差分
   - Android EGL/OpenGL texture生成・破棄を持ち込まず、frame resource fieldを`VkFence`とVulkan timeline metadataへ置換した。
   - Vulkan image本体は`VulkanOutput`/presenter ownerが管理し、queueはframe identityと同期metadataだけを搬送する。
6. 後続フェーズへ渡すinterface
   - R3/R19が接続できる`getRenderFrame`、`pushRenderedFrame`、`getPresentCandidate`、`commitPresentedFrame`、resync/statistics一式。
   - R4/R6/R17/R20/R24/R26が残存adapterを削除・完成させるためのファイル単位manifest。

検証: 固定core commitと45ファイル、固定frontend tagと9ファイルの改行正規化比較に成功。`FrameQueue`参照method 18件とAndroid/EGL API非混入を静的監査し、`git diff --check`成功。ビルドはユーザー指示により省略。

## R2 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU.h`
   - `src/GPU3D.h`
   - `src/GPU3D.cpp`
   - `src/frontend/qt_sdl/MelonPrimeRendererFactory.h`
   - `src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp`
   - `src/frontend/qt_sdl/EmuThread.cpp`
2. 新しく追加した型、関数、resource
   - canonical `GPU3D::GetCurrentRenderer()` const/non-const、`HasCurrentRenderer()`。
   - renderer切替ごとに増加する`GetCurrentRendererGeneration()` hook。
   - canonical `CreateRenderer3DForSelection()` factory。
3. 削除した旧経路
   - 実行call siteから`GetCurrentRendererOverride()`と`CreateRenderer3DOverrideForSelection()`を除去し、R26削除用aliasだけを残した。
   - `actual=Software`と`normalized=Vulkan`の差によって同じrendererを再生成する旧切替判定を除去した。
4. 所有権とframe lifecycleの変更
   - active Vulkan `Renderer3D`の唯一のunique ownerを`GPU3D::CurrentRenderer`とした。
   - callerが保持する`renderLock`内で、旧owner停止・破棄、outer生成、Vulkan 3D生成・Init、`SetCurrentRenderer`によるReset、render settings適用を一つの外部非公開transactionとして実行する。
   - `CurrentRendererGeneration`は公開完了後だけ読み取られ、R3 frontend sessionが旧generation resourceを拒否する基準になる。
5. 参照実装との差分
   - 非Vulkan outer rendererが持つ既存`Rend3D`は非MelonPrime互換用に維持し、MelonPrime Vulkan選択時だけ`GPU3D::CurrentRenderer`をactive backendとして優先する。
   - Vulkan初期化失敗時はcurrent ownerを設定せず、outer `SoftRenderer3D`へfallbackする。
6. 後続フェーズへ渡すinterface
   - `HasCurrentRenderer()` + reference-returning `GetCurrentRenderer()`。
   - `GetCurrentRendererGeneration()`とcanonical factory名。

検証: legacy `Override`実行call site 0件、`VulkanRenderer3D` owner unique_ptrがfactory returnと`GPU3D::CurrentRenderer`以外に存在しないこと、切替判定がnormalized renderer基準であることを静的監査。`git diff --check`成功。ビルドはユーザー指示により省略。

## R3a～R3d — 2026-07-14 実装完了

1. 変更したファイル
   - `src/VulkanStructuredControlAbi.h`
   - `src/VulkanStructuredControlAbi.inc`
   - `src/GPU2D_Soft.cpp`
   - `src/GPU_Soft.cpp`
   - `src/GPU3D.h`
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
   - `src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.h`
   - `src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp`
   - `src/frontend/qt_sdl/EmuInstance.h`
   - `src/frontend/qt_sdl/EmuInstance.cpp`
   - `src/frontend/qt_sdl/EmuThread.cpp`
   - `src/frontend/qt_sdl/MelonPrimeScreenVulkan.h`
   - `src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp`
   - `src/frontend/qt_sdl/MelonPrimeRendererFactory.h`
   - `src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp`
   - `src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp`
   - `src/frontend/qt_sdl/VulkanReference/VulkanCompositorShader.comp`
   - `src/frontend/qt_sdl/CMakeLists.txt`
2. 新しく追加した型、関数、resource
   - source bit-fieldとcompositor RGBA-byte形式の差を固定した`VulkanStructuredControlAbi`と`ConvertSourceControlToPacked()`。
   - engine順6配列を同じScreenSwap mappingで物理top/bottomへ固定する`MelonPrimeStructuredSnapshot`。
   - `VulkanOutput`、`FrameQueue`、generation、frame serial、last complete snapshotを所有する`MelonPrimeVulkanFrontendSession`。
   - session実体からstructured/compositor/queue/surface/presenter capabilityを返すruntime query。
3. 削除した旧経路
   - `ScreenPanelVulkan::drawScreen()`から`ScreenPanelNative::drawScreen()`委譲を削除した。
   - `EndStructured2DFrame()`成功時のin-core pack/upload/compositor呼出しを停止し、frontend `VulkanOutput`との同時消費を除去した。残存resource/function本体はR6で削除する。
4. 所有権とframe lifecycleの変更
   - EmuThreadの`NDS::RunFrame()`完了点だけが384 engine-line complete snapshotを取得し、GUI側はlive GPU/structured配列を読まない。
   - producerを`getRenderFrame → prepareFrameForPresentation → composeAndSubmitFrame → pushRenderedFrame`へ接続した。
   - Win32 presenterを`winId/HWND → attachSurface → getPresentCandidate → presentFrame → commitPresentedFrame`へ接続した。
   - renderer generation変更時にqueue、temporal history、frame input、last snapshotをresyncする。
5. 参照実装との差分
   - 現行core producerのControlは0.7.0.rc4 compositor ABIとverbatim一致しないため、Plane0/Plane1の3D layer flagを含めて明示変換する。bindingとcontrol flagはC++/GLSL共通`.inc`を唯一の定義源にした。
   - desktop R3 bridgeはengine配列をCPU getterから一度copyするtemporary実装であり、R17でGPU2D参照型sourceへ置換する。
   - Linux/macOS surface一般化、正式layout/HUD、resize完成は計画どおりR20/R21へ残す。
6. 後続フェーズへ渡すinterface
   - `captureCompletedSnapshot()`、`submitCompletedFrame()`、`acquirePresentFrame()`、`presentAcquiredFrame()`、commit/defer API。
   - R4がdesktop surface extension/queue条件を完成させた時点でそのまま実働attachできるWin32 basic presenter path。
   - R5 shader再生成が直接includeする共通descriptor/control ABI source。

検証: Control変換のconstexpr ABI assertion、384-line complete gate、ScreenSwap false=A/B・true=B/Aの全配列mapping、generation/serial拒否、producer/presenter call順、NativeQt委譲0件、in-core upload call 0件を静的監査。`git diff --check`成功。Vulkanビルド入口は`build-mingw-vulkan.bat`と確認済みだが、ユーザー指示によりこのフェーズでは実行せず実装を優先した。

## R4 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/VulkanContext.h`
   - `src/VulkanContext.cpp`
   - `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h`
   - `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp`
2. 新しく追加した型、関数、resource
   - `VulkanDesktopSurfaceBackend`と`VulkanPlatformRequirements`。
   - 列挙済みinstance extensionからWin32、Wayland/XCB/Xlib、Metalをplatform別に選択する処理。
   - `ResolvePresentQueue()`、graphics/present queue/family getter、separate-family query。
   - surface作成後のpresent family解決に備え、同じVkDeviceから全non-empty queue familyのqueue 0を生成するdevice queue set。
3. 削除した旧経路
   - extension存在確認なしで`VK_KHR_surface`とWin32 extensionを要求する固定instance構築を削除した。
   - graphics familyにpresent supportがあることをattach時に固定仮定する分岐を削除した。
   - descriptor indexing featureの自己代入をやめ、列挙結果から必要なfeatureだけを明示enableする。
4. 所有権とframe lifecycleの変更
   - GPU3D、VulkanOutput、presenterは従来どおり単一`VulkanContext`の同一instance/physical device/deviceをAcquire/Release共有する。
   - graphicsとpresent familyが同じsurfaceでは従来のexclusive swapchainを維持する。
   - 異なるsurfaceではsurfaceごとのpresent queueを保存し、swapchain imageを両familyのconcurrent sharingへ切り替え、graphics submitと`vkQueuePresentKHR`をそれぞれ正しいqueueへ送る。
5. 参照実装との差分
   - R20のplatform surface host実装前でもLinux/macOSが必要extensionを選択できるが、実surface生成はまだWin32 basic pathだけに限定する。
   - explicit queue-family ownership barrierではなく、swapchain作成時のconcurrent family ownershipを使用する。単一familyでは余分なsharing costを追加しない。
6. 後続フェーズへ渡すinterface
   - R20 surface hostがsurface生成直後に呼べる`ResolvePresentQueue(VkSurfaceKHR)`。
   - R20/R21 presenterがsurface単位で保持できるgraphics/present familyとqueue情報。
   - MoltenVK列挙用portability instance flagと、存在時だけenableする`VK_KHR_portability_subset` device extension。

検証: 全platform extension文字列が列挙結果を通してのみenableされること、surface生成前にpresent supportを要求しないこと、surface解決がgraphics family優先かつ別family fallbackを持つこと、presenterがgraphics submit/presentを別queueへ分離すること、runtime ownerが同一`VulkanContext`をAcquireすることを静的監査。`git diff --check`成功。ユーザー指示によりビルドは省略。

## R5 — 2026-07-14 実装完了

1. 変更したファイル
   - `tools/vulkan/generate_sapphire_spirv.py`
   - `tools/vulkan/sapphire_shader_manifest.json`
   - `src/CMakeLists.txt`
   - `src/frontend/qt_sdl/CMakeLists.txt`
   - `src/VulkanStructuredControlAbi.h`
   - `src/VulkanStructuredControlAbi.inc`
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
   - `src/frontend/qt_sdl/VulkanReference/VulkanOutput.h`
   - `src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp`
   - `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp`
   - `src/frontend/qt_sdl/VulkanReference/VulkanCompositorShader.comp`
   - `src/VulkanReferenceManifest.md`
2. 新しく追加した型、関数、resource
   - 固定manifestからcore 25本、frontend 4本を生成する単一`melonprime_sapphire_shaders` target。
   - stage、entry point、variant define、生成header、固定symbol、reference repository/path/commit/tagを一行程へ固定するmanifest。
   - `glslangValidator`解決、SPIR-V magic/size検証、決定的な`uint32_t`配列生成、source/SPIR-V/ABI hash、compiler version、byte/word countを埋め込むgenerator。
   - descriptor binding count、push constant byte数、native dimensions、packed stride/layer、screen index、control variant bitをC++/GLSLで共有するABI定数。
3. 削除した旧経路
   - source treeに手編集可能な形で存在したcore 26個、frontend 4個のgenerated headerを削除した。
   - core専用`MelonPrimeSapphireCompositorShaderData.h`複製を削除し、core/frontendを同じcompositor generated symbolへ統一した。
   - 対応GLSL sourceもruntime consumerも存在しない旧`GPU3D_Vulkan_ShaderData.h`を生成対象から除外した。
4. 所有権とframe lifecycleの変更
   - runtime frame ownershipは変更せず、shader data ownershipだけをsource treeからbuild directoryのgenerated targetへ移した。
   - core、VulkanOutput、presenterは同じ`melonDS::Vulkan::GeneratedShaders` namespaceの`uint32_t`配列を直接`VkShaderModuleCreateInfo::pCode`へ渡し、byte配列copyを廃止した。
   - in-core compositor moduleはR6で削除するまでfrontendと同一generated compositor symbolを一時的に参照する。
5. 参照実装との差分
   - shader sourceとvariant意味は固定core commit/frontend tagへpinし、compiler固有byte列は使用中`glslangValidator`とsource hashを生成headerへ記録する。
   - R3で導入したdesktop structured-control変換に必要な共通ABI includeはmanifest ABI hashへ含め、変更時に全consumerを再生成する。
   - generated headerは参照repositoryのsource tree配置を再現せず、`${CMAKE_BINARY_DIR}/generated/sapphire`だけへ生成する。
6. 後続フェーズへ渡すinterface
   - R6が重複composition resourceを削除してもfrontendに残る単一`VulkanCompositorShaderData.h` generated symbol。
   - 後続shader変更をmanifestへ追加し、platform別一覧を作らず同一targetで再生成するCMake経路。
   - push constant/descriptor/packed-layout変更をC++とGLSLで同時に更新させる`VulkanStructuredControlAbi`。

検証: manifest 29本をMSYS2 glslangとvcpkg glslangの双方で生成・`--check`成功。正式MinGW環境と同じPATHでCMake configure成功。`melonprime_sapphire_shaders`初回生成成功後、再実行が`ninja: no work to do`となることを確認。旧source-tree shader data include 0件、旧private compositor symbol 0件、`git diff --check`成功。アプリ全体の`build-mingw-vulkan.bat`はユーザー指示により実行せず、実装を優先した。

## R6 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
   - `src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp`
   - `src/frontend/qt_sdl/VulkanReference/VulkanOutput.h`
   - `src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp`
   - `src/frontend/qt_sdl/VulkanReference/FrameQueue.h`
   - `src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp`
2. 新しく追加した型、関数、resource
   - 3D color image、layout、寸法、scale、frame serial、renderer generation、completion情報を不変値として渡す`Vulkan3DFrameView`と`GetVulkan3DFrameView()`。
   - `Frame`と`VulkanOutput::FrameResource`へ3D source serial/generationを保存し、異なるframe/generationのsnapshotをcomposition入力として拒否するidentity gate。
3. 削除した旧経路
   - core内の`SapphireVulkanCompositionInput/Resources/CommandContext/ShaderModule/DescriptorAbi/PushConstants`を削除した。
   - `Structured2DPacked`、host-visible structured GPU buffer、pack/upload処理を削除した。
   - core内のfinal composition image/memory/view/sampler、descriptor set/pool/layout、pipeline layout、command pool/buffer/fence、compositor shader moduleと全ensure/update/destroy処理を削除した。
   - frontendが3D targetを個別getterでlive参照する経路を削除した。
4. 所有権とframe lifecycleの変更
   - `VulkanRenderer3D`は3D color target/captureだけを所有し、final compositor resourceは`VulkanOutput`だけが所有する。
   - frontend sessionはcomplete structured snapshotと同一serial/generationの`Vulkan3DFrameView`だけを受理する。
   - `VulkanOutput`はviewのsource imageを各`FrameQueue` slot所有のsnapshot imageへcopyし、そのsnapshotだけをcompositionへ渡す。source identityはslotのrender completionまで保持される。
5. 参照実装との差分
   - 現時点のcore render完了は既存fenceでCPU確認済みのため、viewのcompletion semaphore/valueはnull/0 compatibility stateとし、明示timeline化はR24へ渡す。
   - temporary structured 2D CPU arrays/getterはR17 consumer移行まで維持するが、core内GPU upload/composition resourceは残さない。
6. 後続フェーズへ渡すinterface
   - R7～R16が3D render targetを公開する唯一のGPU-facing interfaceである`Vulkan3DFrameView`。
   - R19/R23/R24がtimeline completionとqueue lifecycleを完成させるためのframe serial/generation metadata。
   - R17で削除する対象をCPU structured arrays/getterだけへ限定したcore renderer境界。

検証: core/frontend全体で旧`SapphireVulkanComposition*`、`Structured2DGpu*`、`Structured2DPacked`、pack/upload関数、個別`GetColorTarget*` getterの参照0件を静的監査。`VulkanOutput`がFrameQueue slotのsnapshot以外をcomposition sourceにしないこと、serial/generation不一致を拒否すること、`git diff --check`成功を確認。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R7 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
2. 新しく追加した型、関数、resource
   - color/attribute/depthの固定format定数と、device format feature検査。
   - `ColorImageLayout`、`AttrImageLayout`、`DepthImageLayout`、`DepthStencilImageLayout`、`CaptureReadbackImageLayout`によるresource別layout追跡。
   - synchronization2形式を既存`vkCmdPipelineBarrier`へ変換する`TransitionImage()` adapter。
   - graphics queue上の既送信workだけをfence待機する`waitForQueueTail()`。
3. 削除した旧経路
   - scale変更時とrender-target再生成時の`vkDeviceWaitIdle()`を削除した。
   - image layoutを`ColorImageInitialized`一個から推測する分岐を削除し、attachmentごとの実layoutをbarrierへ渡すようにした。
   - device-local memoryが見つからない場合に任意memory typeへfallbackするtarget allocationを削除した。
4. 所有権とframe lifecycleの変更
   - 1以上へclampした`ScaleFactor`から`256*scale × 192*scale`の実targetを次frameで生成する。
   - resize時は新target一式を先に作成し、shared graphics queue末尾fenceが完了してから旧framebuffer/view/image/memoryと寸法依存bufferを破棄する。
   - `Vulkan3DFrameView`は追跡中のColor layoutを公開し、未遷移targetをvalidとして公開しない。
5. 参照実装との差分
   - DepthImageは現行graphics MRTでdepth値を`R32_SFLOAT` color attachmentへ書くため、計画のSTORAGE/SAMPLED/TRANSFERに加えてCOLOR_ATTACHMENT usageを維持する。
   - depth/stencil formatは`D32_SFLOAT_S8_UINT → D24_UNORM_S8_UINT → D16_UNORM_S8_UINT`の順で、attachment/transfer featureを全て満たす候補を保存する。
6. 後続フェーズへ渡すinterface
   - R8以降がcontext別commandで利用する一元化されたtarget format/layout state。
   - R16 captureとR24 barrier整理が共有できる`TransitionImage()`。
   - R24 retire queueへ一般化可能な、device全体を止めないqueue-tail completion point。

検証: Color/Attr/Depth/DepthStencilのusage bit、depth/stencil候補順、device-local allocation、target作成前後のcleanup順、scale/recreate箇所の`vkDeviceWaitIdle`不存在、layout fieldの生成・描画・readback・破棄遷移を静的監査。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R8 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
   - `src/frontend/qt_sdl/VulkanReference/VulkanOutput.h`
   - `src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp`
2. 新しく追加した型、関数、resource
   - 6個の`RenderContext`ごとに独立するgraphics scene vertex / edge index bufferとpersistent mapping。
   - contextごとのsubmitted frame serial、renderer generation、completion value。
   - core 3D submitが単調増加値をsignalする`CompletionTimelineSemaphore`。
   - requiredと旧capacityの2倍の大きい方を選ぶ`growBufferCapacity()`。
3. 削除した旧経路
   - graphics hardwareだけを単一global command/fenceへ固定する`ActiveBackendMode != GraphicsHardware`除外条件を削除した。
   - context/global buffer拡張時のrequired-sizeぴったり再確保を、倍増capacity確保へ置換した。
   - result/bin/group/span/work buffer resize時のdevice-wide idleをqueue-tail fenceへ置換した。
4. 所有権とframe lifecycleの変更
   - threaded graphics pathはready slotを探索し、なければ次の1 slotだけを待ってcommand poolをresetして再利用する。
   - triangle、graphics vertex、scene vertex、edge index、bin/group、span、work offset、toon、clear、capture line、descriptor、timestampはslot単位で保持する。
   - core submitのtimeline semaphore/valueを`Vulkan3DFrameView`へ載せ、`VulkanOutput`のsnapshot copy submitがtransfer stageで明示waitしてからFrameQueue-owned imageへcopyする。
5. 参照実装との差分
   - timeline非対応時はshared graphics queueのsubmit順序とcontext fenceをfallbackとし、binary semaphore adapterの一般化はR24へ渡す。
   - non-threaded pathはprimary fenceだけを待ってからglobal mapped bufferを更新し、全context待機は行わない。
6. 後続フェーズへ渡すinterface
   - R9 scene builderがslot-local scene/edge bufferへ直接書けるcontext境界。
   - R16 captureがsource contextだけを待てるsubmitted identity。
   - R23/R24が3D→composition依存へそのまま利用できるcompletion timeline value。

検証: context数6、slot別resource、graphics pathのring取得、対象slot以外の通常frame待機不存在、capacity倍増、persistent mapping、descriptor cache分離、timestamp optional、3D timeline signal→VulkanOutput waitのsubmit連鎖を静的監査。通常frameのbuffer resizeに`vkDeviceWaitIdle`を使わないことと`git diff --check`成功を確認。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R9 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU3D_AcceleratedFrontend.h`
   - `src/GPU3D_AcceleratedFrontend.cpp`
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
2. 新しく追加した型、関数、resource
   - `RenderPolygonRAM`とpolygon数、display/alpha/clear/fog/toon/edge/XPos stateを一度に固定する`AcceleratedSceneBuildInput`と`CaptureAcceleratedSceneBuildInput()`。
   - build結果へ同じscalar/table stateを保持する`AcceleratedSceneRenderState`。
   - draw単位の`TexParam`/`TexPalette`とcoverage depth bias。
   - Sapphire原文どおり、共通frontendがline polygonの2 endpoint/indexを確定し、Vulkan側の`appendLineSegment`が1px幅の4 vertex/2 triangleへ変換する責務境界。
3. 削除した旧経路
   - `SetRenderSettings()`で`HiresCoordinates`を捨てる処理を削除した。
   - live graphics pathからVulkan独自polygon loopを外し、`GPU3D_AcceleratedFrontend`を常時呼ぶよう固定した。旧非到達builder本体のsource削除はR26 cleanupで行う。
   - Vulkan独自の第二polygon builderをlive pathから外しつつ、Sapphire原文のsource line quad化関数は正式scene upload adapterとして維持した。
4. 所有権とframe lifecycleの変更
   - EmuThread上でrender stateとpolygon pointer順をimmutable build inputへ一度captureし、そのinputだけからsceneを構築する。
   - draw順は`RenderPolygonRAM`順のままappendし、非隣接stateを結合・再sortしない。`FirstTranslucentDraw`の確定位置もSapphire原文に合わせた。
   - Vulkan graphics変換区間はsceneのvertex/triangle/draw/texture/render stateだけを読み、source polygonを再dereferenceしない。
5. 参照実装との差分
   - desktop側の既存GPU ABI buffer/pipelineは維持し、scene build後の一括packing adapterとして使用する。line quad化はSapphire原文の`appendLineSegment`だけを使用し、MelonPrime独自展開は追加しない。
   - alpha-zero polygonのboundary line描画はsource line primitive変換ではなくraster pass生成のため、既存scene triangle boundaryから生成する処理を維持する。
6. 後続フェーズへ渡すinterface
   - R10 texture cacheがsource polygon pointerを読まず利用できるdraw単位texture key。
   - R11～R16が同一frameのclear/fog/toon/edge/capture stateを参照できる`AcceleratedSceneRenderState`。
   - R24がslot-local upload/synchronizationへ接続できる、live `GPU3D`から分離されたscene build境界。

検証: build input指定13項目のcapture、`HiresCoordinates`伝播、frontendのline 2 endpoint/index出力、Vulkan参照`appendLineSegment`による4 vertex/2 triangle化、draw append順、graphics変換区間のsource polygon dereference 0件、live geometry pathの共通frontend固定を静的監査。R10着手時の正規化diffでMelonPrime独自line展開を除去し、Sapphire原文の責務境界へ復元した。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R10 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU3D_Texcache.h`
   - `src/GPU3D_AcceleratedFrontend.cpp`
   - `src/GPU3D_Vulkan.cpp`
   - `src/VulkanReferenceManifest.md`
2. Sapphire直接移植とconformance audit
   - 固定core commitとの正規化diffで`GPU3D_TexcacheVulkan.h/.cpp`を照合し、差分が計画で許可された`<volk.h>`／dispatch includeの2行だけであることを確認した。allocation、upload、opaque判定、descriptor取得、cleanupはSapphire原文をそのまま維持する。
   - R9でMelonPrime独自にfrontendへ移していたline quad化を除去し、Sapphire原文どおりfrontendは2 endpoint/indexを出力、`GPU3D_Vulkan.cpp`の参照`appendLineSegment`がquad化する境界へ復元した。
   - `VulkanReferenceManifest.md`を現行のimmutable scene input、coverage adapter、render context／frame handoff差分に合わせ、未分類差分を残さない記録へ更新した。
3. texture cache更新とlifetime
   - 現行melonDSのGPU所有／clear bitmap dirty APIを維持したまま、Sapphireの`BeforeMutation` callbackを`Texcache::Update()`へ移植した。
   - `VRAMDirty_Texture`／`VRAMDirty_TexPal`とVRAM mapからcoherent flat VRAMを作り、hashが変わったcache entryだけをinvalidate/reuseする。
   - dirtyがない通常frameではcontext fenceを待たず、実際にentryをinvalidate/reuseする直前だけ`waitForTextureCacheMutationSafePoint()`を呼ぶ。
   - `Reset()`／`Stop()`では`Texcache.Reset()`によるimage破棄前にprimary fenceとthreaded render contextを待ち、in-flight descriptor/image参照を残さない。
   - 参照loaderのupload command/fenceを維持し、staging copy完了後だけlayerを公開する。texture更新ごとの`vkDeviceWaitIdle()`は使用しない。
4. descriptor、sampler、fallback
   - runtime capabilityから`NonUniform`、`CompatDynamicUniform`、`BaseSingleDescriptor`を選び、descriptor indexing非対応でもVulkan GPU rasterを継続する。
   - DSのclamp/repeat/mirrorは参照shaderの`texParam`座標処理と`texelFetch`で実装済みであり、texture arrayの固定samplerを共有してdrawごとのsampler生成を行わない。
   - renderer初期化時に1×1白fallback textureを一度作り、invalid descriptorやdescriptor上限時もpolygonを削除せずfallback indexで描画する。
   - `IsTextureLayerOpaque()`のlayer maskをNeedOpaque判定へ渡し、同じdecode結果を3 descriptor pathで共有する。
5. 参照実装との差分
   - `GPU3D_TexcacheVulkan.*`の差分はVolk include／dispatchだけである。
   - generic `GPU3D_Texcache.h`は現行melonDSの`GPU&`所有と`clrBitmapDirty`出力をcore compatibility adapterとして維持し、mutation callbackの位置とcache invalidation algorithmをSapphireへ合わせた。
   - desktop resource lifetimeはR8の6 context fenceをsafe pointに利用する。R24で一般retire queueへ統合するまで、texture mutation時だけ明示待機する。

検証: 固定commitに対する`GPU3D_TexcacheVulkan.*`正規化diff、loader API 5関数、dirty derivation/coherency/hash invalidation、3 descriptor mode選択、opaque layer mask、fallback descriptor、shader側clamp/repeat/mirror、mutation callback位置、Reset/Stop破棄前wait、R9 line責務境界を静的監査。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R11 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
   - `src/VulkanReferenceManifest.md`
2. Sapphire直接移植とclear state
   - 固定core commitの`updateGraphicsClearBuffer()`、graphics clear pipeline、full-screen clear draw、`GPU3D_Vulkan_GraphicsClearShader.frag`を正本として維持した。clear shaderは固定commitと正規化diff 0件である。
   - R9のimmutable `AcceleratedSceneRenderState`から`ClearGpuState`を一度構築し、`RenderDispCnt`、`RenderClearAttr1/2`、RGBA8、24-bit depth、polygon ID/fog attribute、stencil初期値、`RenderXPos`、bitmap enableを同じframe identityへ固定した。
   - graphics pathのclear buffer、render-pass clear value、capture fallback clear色はlive `GPU3D`を再参照せず、この`ClearGpuState`を使用する。
3. clear plane
   - `RenderClearAttr1/2`をSapphire原文のbit精度で6-bit RGB／5-bit alpha、24-bit depth、6-bit polygon ID、fog flagへ展開する。
   - Color、Attr、Depth color attachmentとDepthStencil attachmentの4 clear valueへ同じstateを設定し、stencilを参照値`0xFF`で初期化する。
   - developerの`Debug3dClearMagenta`だけがcolorを上書きし、depth／attribute／stencilはDS stateを維持する。
4. clear bitmap
   - `RenderDispCnt` bit 14が有効な場合だけ、coherent texture VRAMの`0x40000` colorと`0x60000` depth/fogを、`RenderClearAttr2`の8-bit X/Y offsetとu8 wrapでSapphire原文どおりdecodeする。
   - decode先は各`RenderContext`のpersistently mapped clear storage bufferであり、Qt/QImage、SoftwareRenderer3D、presenter snapshot、毎frameの一時`std::vector`を使用しない。
   - 固定clear shaderがDS 256×192座標へscaleしてColor／Attr／Depth／DepthStencilへfull-screen drawするため、render scale後も同じclear bitmap semanticsを保持する。
   - `RenderXPos`はframe stateへ保持するが、固定参照どおりclear bitmap VRAM addressへ加算しない。これは3D line出力のscanline/capture shiftであり、R16の`GetLine()`／capture接続で使用する。
5. source generation再利用
   - R10の`clrBitmapDirty`から単調増加`ClearBitmapGeneration`を更新し、global contextと6個のrender contextがそれぞれ前回の`ClearGpuState`／generation／validityを保持する。
   - clear stateとVRAM generationが同一のslotでは49,152 pixelの再decodeとmapped buffer書込を省略する。dirtyまたはoffset/state変更時だけ参照decodeを実行する。
   - buffer再作成、renderer resetではvalidityを破棄し、古いVRAM内容を再利用しない。
6. 参照実装との差分
   - clear decode、bit変換、offset wrap、shader、pipeline、draw順はSapphire原文のままである。
   - `ClearGpuState`はR9 immutable input用adapter、slot別generation cacheはR8 context ringと現行dirty trackerを接続するdesktop lifetime/performance adapterである。

検証: `ClearGpuState`の全field capture、clear planeの4 attachment値、bitmap enable、VRAM base、X/Y offset wrap、RGBA/depth/fog/poly ID decode、full-screen clear pipeline、固定clear shader正規化diff 0件、slot別generation hit/miss、reset/recreate invalidation、Qt/Software/QImage依存0件を静的監査。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R12 — 2026-07-14 実装完了

1. 変更したファイル
   - 本計画書（実装本体は固定Sapphire codeが既に完備していたため、独自source差分を追加していない）
2. Sapphire直接移植conformance
   - `GPU3D_Vulkan_GraphicsRasterShader.vert/.frag`と`GPU3D_Vulkan_GraphicsNoColorShader.frag`は固定core commitとの正規化diff 0件で、texture sample、vertex color combine、toon/highlight、alpha evaluation/test、depth、color/attribute/poly ID/fog出力順を原文のまま維持する。
   - opaque pipeline作成、fragment-depth variant、fast modulate/toon/plain variant、opaque UI overlay pipeline、descriptor path、draw recordingは固定`GPU3D_Vulkan.cpp`の実装を正本としている。
3. vertex ABIとupload
   - `GraphicsVertexGpu`はposition/depth/reciprocal-W、UV、RGBA8、flags、texture layer/index/size/param、polygon attrを56 bytesで保持する。
   - `static_assert`でsize 56、X offset 0、UV offset 16、color offset 24、flags offset 28、texture height offset 44をshader input ABIへ固定する。
   - R8のcurrent `RenderContext` mapped vertex bufferへscene生成後に一括copyし、vertex/index bufferをpass内で再生成しない。
4. opaque draw path
   - `GPU3D_AcceleratedFrontend`が付けたW-buffer、depth equal、fog、texture、shade、coverage flagを固定pipeline selectionへ渡す。
   - `GraphicsPolygons`へのappend順を保持し、texture/state sortを行わず、pipeline／descriptor／stencil stateが変わったときだけbindする。
   - Color、Attr、Depth、DepthStencil attachmentを同じrender passでGPU生成し、SoftwareRenderer3D pixel loopやCPU ownership maskへ依存しない。
5. NeedOpaque
   - Sapphire原文どおり、translucentかつpolygon alpha 31のdrawだけへ`AcceleratedPolygonFlagNeedOpaquePass`を付ける。
   - texture decodeの`IsTextureLayerOpaque()`結果をtriangle flagへ渡し、opaque texture／full alpha用fast pathと、alpha fragmentを残すNeedOpaque／translucent passを分離する。
   - `GraphicsNeedOpaqueDrawIndices`はsource draw append時にだけ追加され、非隣接drawの結合・並べ替え・全translucentへの常時二重描画を行わない。
6. 参照実装との差分
   - R12のopaque algorithm、shader、pipeline family、NeedOpaque分類には新しいMelonPrime独自差分を追加していない。
   - R7～R11で追加したscaled target、context ring、immutable scene/clear state、Volk/desktop contextだけを外側adapterとして使用する。

検証: 3 shader sourceの固定commit正規化diff 0件、`GraphicsVertexGpu` field/offset assertion、opaque/fragment-depth/fast/UI pipeline存在、source-order append、NeedOpaque flag条件、texture opaque flag、draw command、Color/Attr/Depth/DepthStencil attachment、SoftwareRenderer3D/QImage依存0件を静的監査。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R13 — 2026-07-14 実装完了

1. 変更したファイル
   - 本計画書（translucent実装本体は固定Sapphire codeが既に完備していたため、独自source差分を追加していない）
2. Sapphire直接移植conformance
   - `GPU3D_Vulkan_GraphicsRasterShader.frag`と`GPU3D_Vulkan_GraphicsNoColorShader.frag`は固定core commitとの正規化diff 0件である。
   - translucent blend、depth/stencil、polygon ID、background alpha zeroのpipeline作成とdraw処理は、固定`GPU3D_Vulkan.cpp`の同一関数ブロックを一体で維持する。
3. blendとpipeline variant
   - alpha blend有効時はcolorを`SRC_ALPHA`／`ONE_MINUS_SRC_ALPHA`／`ADD`、alphaを`ONE`／`ONE`／`MAX`とするSapphire原文のattachment stateを使用する。
   - DS alpha blend disable時は別のreplace attachmentを選び、一般blend pipelineの係数を実行時に改変しない。
   - W/Z、less/less-or-equal、depth write on/off、fog write on/off、alpha blend on/offを32個の`GraphicsTranslucentPipelines`へ固定index化する。
   - background alpha zeroはW/Z、depth compare、depth write、fog writeの16個の`GraphicsBgZeroTranslucentPipelines`として独立し、通常translucent pipelineへ統合しない。
4. depthとpolygon ID
   - polygon attr bit 14/11とtriangle W-buffer flagからcompare／write／Z-W variantをdraw単位で選択し、全alpha drawへ固定しない。
   - 通常translucent drawはstencil compare mask/write mask `0x7F`、reference `0x40 | polyId`、`NOT_EQUAL`＋`REPLACE`を使用し、同一6-bit polygon IDの同一pixel再blendを抑止する。
   - shadow ownershipのbit 7は`0x80` maskで別管理し、translucent marker＋IDのbit 6..0とclear/write maskを共有しない。
   - background alpha zero pathは参照どおり`EQUAL`＋`INVERT`と`0xFE` maskを使用し、背景alpha-zero固有のcolor/attribute処理を保つ。
5. draw order
   - `GraphicsPolygons`はaccelerated sceneのsource append順のまま走査し、alpha drawの一括sortやtexture単位group化を行わない。
   - `NeedOpaque`があるdrawは同じsource位置でopaque fragment passを先に実行し、その直後にtranslucent fragmentを参照pipelineへ残す。
   - pipeline、descriptor、stencil stateはcached bind helperで変更時だけbindし、CPU polygon-ID maskを生成しない。
6. 参照実装との差分
   - R13のblend式、pipeline family、depth/stencil state、polygon-ID mask、background-alpha-zero分岐にはMelonPrime独自差分を追加していない。
   - R8～R12のcontext ring、immutable scene/clear state、scaled attachments、Volk/desktop contextを外側adapterとして利用する。

検証: 2 shader sourceの固定commit正規化diff 0件、blend factor/op、32通常＋16 bg-zero variant、depth compare/write、W/Z index、`NOT_EQUAL`/`REPLACE`、`0x7F` polygon-ID mask、`0x80` shadow mask、bg-zero `EQUAL`/`INVERT`、NeedOpaque先行、source-order drawを静的監査。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R14 — 2026-07-14 実装完了

1. 変更したファイル
   - 本計画書（shadow実装本体は固定Sapphire codeが既に完備していたため、独自source差分を追加していない）
2. Sapphire直接移植conformance
   - shadow mask／self reject／blendに使用するgraphics raster/no-color shaderは固定core commitとの正規化diff 0件である。
   - `GraphicsShadowMaskPipelines`、`GraphicsShadowClearPipelines`、`GraphicsShadowBlendPipelines`とbackground-alpha-zero variantを固定`createGraphicsPipelines()`ブロックのまま維持する。
3. Shadow Mask
   - accelerated frontendの`IsShadowMask`を`AcceleratedPolygonFlagShadowMask`へ保持し、source append時に`GraphicsShadowMaskDrawIndices`へ分類する。
   - no-color pipelineはcolor/depth writeを無効化し、depth fail時の`VK_STENCIL_OP_REPLACE`でdynamic reference `0x80`をshadow bit 7へ設定する。
   - background-alpha-zero maskは参照どおり`EQUAL`＋depth-fail `INVERT`を使用し、lower polygon-ID bitsを保持する。
4. Self Reject／bit clear／Shadow Blend
   - `GraphicsShadowClearPipelines`はshadow polygon自身のlower 6-bit poly IDを`EQUAL`比較し、depth pass時の`ZERO`をwrite mask `0x80`へ限定してself-shadow bitだけを消す。
   - pass境界の`GraphicsStencilBitClearPipeline`もcompare/write mask `0x80`だけでfull-screen clearし、depth/stencil image全体やlower ID bitsを破壊しない。
   - shadow bitが残るpixelだけ`GraphicsShadowBlendPipelines`へ通し、通常／replace blend、W/Z、depth compare/write、fog、background-alpha-zero variantをR13と同じ参照indexで選ぶ。
   - blend後のreference `0xC0 | polyId`、compare mask `0x80`、write mask `0x7F`によりshadow bitとtranslucent IDを別管理する。
5. draw order
   - `GraphicsShadowMaskDrawIndices`／`GraphicsShadowDrawIndices`はscene source append順で生成し、CPU bitmapや別sortを作らない。
   - active graphics passはmask、self reject/clear、blendの参照順を保持し、shadowを通常translucent一回描画へ縮退させない。
6. 参照実装との差分
   - R14のshader、pipeline state、stencil op/mask、draw順にはMelonPrime独自差分を追加していない。
   - R8～R13のcontext ring、immutable scene state、scaled depth/stencil attachmentを外側adapterとして使用する。

検証: shader固定commit正規化diff 0件、shadow flags/list、mask color/depth write off、depth-fail `REPLACE`、self-reject `EQUAL`/`ZERO`、bit-7限定clear、shadow blend `0x80`/`0x7F` mask、bg-zero variant、source orderを静的監査。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R15 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
   - `src/VulkanReferenceManifest.md`
   - 本計画書
2. Sapphire直接移植conformance
   - `GPU3D_Vulkan_GraphicsRasterShader.frag`、`GraphicsEdgeShader.frag`、`GraphicsEdgeFogShader.frag`、`GraphicsFogShader.frag`、`FinalPassShader.comp`は固定core commitとの正規化diff 0件である。
   - toon／highlight演算、edge／fog適用順、hidden-alpha-zero override、fog density補間、coverage AA、final specialization variantは固定Sapphire shaderとpipeline作成・選択を正本として維持した。
3. immutable final-effect state
   - graphics hardware pathはR9で固定した`SharedGraphicsScene.RenderState`からalpha reference、fog color/offset/shift/density、edge table、toon tableをdispatchへ渡し、command recording直前のlive `gpu.GPU3D.Render*`再読込を除去した。
   - 互換用non-graphics branchも同じ`AcceleratedSceneRenderState`へ一度captureしてからclear/final dispatchへ渡すため、両経路の引き渡しABIを統一した。
4. toon／highlightとtable upload
   - Sapphire原文の32-entry RGB555→RGB6 packとper-context host-visible GPU bufferを維持し、global table generationと各render-contextのuploaded generationを追加した。
   - table内容が変化した場合だけgenerationを進め、同一generationを既に保持するcontextでは32-entry変換とmapped writeを省略する。buffer破棄・再作成時はslotのuploaded generationを無効化する。
   - raster shaderは5-bit intensityで同じtoon bufferを参照し、toon color置換とhighlight加算・clampを別の参照分岐としてtextured/untextured双方へ適用する。
5. edge／fog／AA final pass
   - graphics final pathはAttr/Depth attachment、polygon ID、fog flag、scaled target座標、8-entry edge table、34-entry packed fog density、fog color/offset/shiftを直接使用し、edge、fog、edge+fogをSapphire原文のpipelineで選択する。
   - fixed graphics edge shaderはDS antialias bitでedge alphaを選び、coverage fix設定を参照しない。full coverage resolveは固定`FinalPassShader.comp`のtop/bottom attribute/depth layerとcoverage値を使用する。
   - compute final pipelineはedge、fog、AAの3 specialization bitから8 variant（noneを含む全組合せ）を生成・選択する。CPU画像処理、presenter fog、coverage-fix代用は追加していない。
6. 参照実装との差分
   - shader、final-effect式、attribute解釈、pipeline selection、適用順にはMelonPrime独自差分を追加していない。
   - immutable state dispatchとtoon generation cacheだけをR8/R9 context ring向けdesktop adapterとして追加し、manifestへ分類した。

検証: 5 shader sourceの固定commit正規化diff 0件、toon/highlight分離、32-entry per-context buffer、generation hit/miss/recreate無効化、edge/fog graphics pipeline、8 compute final variant、coverage AA、hidden-alpha-zero override、live final-effect state再読込除去、CPU postprocess依存0件を静的監査。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## R16 — 2026-07-14 実装完了

1. 変更したファイル
   - `src/GPU.h`
   - `src/GPU.cpp`
   - `src/GPU_Soft.cpp`
   - `src/GPU3D_Vulkan.h`
   - `src/GPU3D_Vulkan.cpp`
   - `src/VulkanReferenceManifest.md`
   - 本計画書
2. capture frame snapshot
   - line 0の`CheckCaptureStart()`でcapture control、engine A `DispCnt`、screen swapを`CaptureFrame*`へ固定し、`GPU_Soft::DoCapture()`と`CheckCaptureEnd()`のscanline途中live register再読込を除去した。
   - capture destination bank/offset/size、source A/B、EVA/EVB、FIFO/VRAM source選択は同一snapshotを使う。savestate byte layoutは増やさず、load後にtransient snapshotを現行stateから再構築する。
   - `CaptureFrame*` field、初期化、savestate再構築、参照分岐はすべて`MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN`内へ隔離した。非Vulkan buildのGPU object layoutとcapture処理は従来の`CaptureCnt`／engine A `DispCnt`経路を維持する。
   - `VulkanRenderer3D`はenabled/control/screen swap、source dimensions/scale、destination range、3D frame serial、BG0 3D contributionを`DisplayCaptureFrameState`へ一度captureし、VBlank `Blit`でもlive `GPU`を再読込しない。
3. capture line exportとslot lifecycle
   - `GPU3D_Vulkan_CaptureLineExportShader.comp`は固定core commitとの正規化diff 0件で、scaled `ColorImage`をnative 256×192へnearest resolveし、DS 6-bit RGB＋5-bit alphaへGPU packする。
   - active/pending/readyを分離するglobal 2-slot bufferと6個のrender-context bufferを維持し、pending/readyへscreen swapと3D frame serialを同時に保存する。
   - completion fenceがreadyになったslotだけをline cacheへcopyし、未完了slotはnon-blocking prepareで保持する。global 2-slotの両方がpending/readyなら新しいexportをdeferし、上書きしない。
4. exact capture historyとfallback
   - ready slotはcapture snapshotのscreen swap/frame serialと照合し、別frameのpending exportを同一captureとして採用しない。
   - 有効なexact captureは`LastValidExactCaptureLineCache`へ保存し、source unavailable時は同screen-swapの直前履歴、次にimmutable clear colorから作った明示fallbackを使用する。Software 3Dを並走させない。
   - identical 3D frameのlate exportで引数なしfallback更新になっていた不整合を修正し、immutable `SharedGraphicsScene.RenderState`のclear colorを使用する。
5. VRAM同期とnormal presentation分離
   - `GPU_Soft::DoCapture()`はsnapshotのwidth/height/destinationだけを対象にRGB555へmixし、書いたcapture lineの512-byte dirty granuleだけを更新する。
   - `CheckCaptureStart/End()`のcapture block flagsと既存`SyncVRAMCapture()` ownershipを維持し、必要bank/block以外を同期しない。
   - window presentationはR6の`Vulkan3DFrameView`／frontend compositorを使用し、capture line bufferやcapture readbackを通常表示sourceにしない。
6. 参照実装との差分
   - capture shader、GPU dispatch/barrier、exact history、slot completion、screen-swap照合は固定Sapphire実装を正本として維持した。
   - current upstreamのGPU owner構造へ合わせたline-0 register latchとframe-serial metadataだけをdesktop/core compatibility adapterとして追加し、manifestへ分類した。

検証: capture shader固定commit正規化diff 0件、line-0 control/DispCnt/screen-swap latch、source dimension/scale/destination snapshot、shader write→host read barrier、2-slot busy/defer、6-context fence completion、pending→ready frame identity、exact history/clear fallback、capture range dirty、Vulkan capture判断のlive `CaptureCnt`/`ScreenSwap`/engine-A `DispCnt`再読込0件、SoftwareRenderer3D並走0件を静的監査。追加監査で共通GPU変更をMelonPrime Vulkan build gateへ隔離し、非Vulkan branchが従来fieldだけを参照することを確認。`git diff --check`成功。ユーザー指示によりアプリ全体の`build-mingw-vulkan.bat`は実行せず、実装を優先した。

## Sapphire直接移植方針調査 — 2026-07-14 計画反映

### 結論

Sapphire側の固定参照実装に完成した同一責務のコードがある場合、melonPrimeDS側で同じ処理を新規設計・再実装しない。参照ファイルまたは参照関数ブロックをそのまま取り込み、desktop core API、Volk、Qt、native surface、frame handoffに必要な差分だけを明示的なadapter patchとして適用する。

```text
実装の第一選択  = Sapphire原文を固定commit/tagから同期
実装の第二選択  = 原文 + 薄いdesktop/melonPrime adapter
新規実装         = Sapphireに同等責務が存在しないdesktop統合部だけ
```

### ライセンス確認

対象3リポジトリはGPL-3.0系で整合しているため、本計画ではSapphireコードの直接取り込みを許可する。取り込み時は次を維持する。

```text
既存copyright headerを保持
LICENSEを保持
参照repository/path/commit/tagをmanifestへ記録
変更したファイルはdesktop／melonPrime変更点と変更日を追跡可能にする
バイナリ配布時は対応するsourceとbuild scriptを同じGPL条件で提供可能にする
```

ライセンス互換性を理由に、完成済みSapphireコードを別実装へ書き直す必要はない。

### GitHub比較で確認した同期単位

- `VulkanPerfStats.h`は参照coreとmelonPrimeDSでblob SHAが完全一致している。
- `VulkanFilterMode.h`は参照frontendとmelonPrimeDSでblob SHAが完全一致している。
- 確認した`GPU3D_Vulkan_GraphicsRasterShader.vert`は参照coreとmelonPrimeDSでblob SHAが完全一致している。ほかの固定shader sourceも同じ方針で原文同期し、SPIR-V headerだけをローカルgeneratorで再生成する。
- `GPU3D_AcceleratedFrontend.h`は参照本体の宣言・ABIを保持し、melonPrimeDS側の差分は由来コメントだけである。`.cpp`もファイル単位同期を原則とする。
- `GPU3D_TexcacheVulkan.h`は参照との差分がVulkan includeの`<vulkan/vulkan.h>`から`<volk.h>`への置換だけである。`.cpp`も参照実装を正本にし、dispatch/context差分だけをadapter化する。
- `FrameQueue.h`はqueue policy、statistics、9-frame lifecycleを参照形状のまま保持し、Android EGL/OpenGL resource fieldだけをVulkan frame identity／fence／generationへ置換している。
- `VulkanOutput.h`はsnapshot、temporal history、composition input、descriptor ABIを参照形状のまま保持し、`Vulkan3DFrameView`とStructured Control ABI変換だけをdesktop adapterとして追加している。
- `VulkanSurfacePresenter.h`はlayout、swapchain、descriptor、vertex、background、filter、pacingの責務を参照形状のまま保持し、`ANativeWindow`境界とpresent queue解決だけをdesktop向けに置換している。
- `VulkanContext.*`はAndroid Hardware Buffer、`vulkan_android.h`、Android extensionを含むためファイル全体のverbatim copy対象ではない。device profile、capability query、reference-counted lifetimeなどのplatform-neutral部分だけを移植し、desktop instance/surface/present queue処理はmelonPrimeDS側を正本とする。

### 実装上の優先順位

```text
A. 原文のまま同期
   shader source
   VulkanPerfStats.h
   VulkanFilterMode.h
   platform非依存の小型utility

B. 原文を主体に薄いadapter patch
   GPU3D_AcceleratedFrontend.*
   GPU3D_TexcacheVulkan.*
   GPU3D_Vulkan.*
   VulkanOutput.*
   FrameQueue.*
   VulkanSurfacePresenter.*

C. desktop側で実装
   VulkanContextのplatform extension/surface/present queue部分
   MelonPrimeVulkanFrontendSession.*
   MelonPrimeVulkanSurfaceHost.*
   MelonPrimeScreenVulkan.*
   EmuThread／EmuInstance統合
   Qt layout／HUD／OSD／radar adapter
```

R10以降のフェーズ説明は仕様確認用であり、Sapphireに対応コードが存在する箇所をゼロから書くための疑似コードではない。Sonnetは必ず参照コードを先に移植し、その後に差分だけを実装する。

R8およびR9は実装完了記録を維持する。ただしR10着手前に固定core commitとの正規化diffを取り、adapter以外の乖離があればSapphire原文へ戻す。これは完了状態の取消しではなく、直接移植方針に対するconformance auditとして扱う。

---

# 1. 目的

melonPrimeDSでVulkanを選択した場合に、Nintendo DSの3DポリゴンをVulkan GPUで直接ラスタライズし、そのGPU上の3D画像を2D合成、画面配置、最終表示までGPU常駐で処理する。

最終的な通常フレーム経路は次とする。

```text
GPU3D polygon state
    ↓
GPU3D_AcceleratedFrontend
    ↓
VulkanRenderer3D
    ├─ clear plane
    ├─ clear bitmap
    ├─ texture cache
    ├─ opaque polygons
    ├─ translucent polygons
    ├─ shadow mask / reject / blend
    ├─ toon / highlight
    ├─ depth / stencil
    ├─ edge marking
    ├─ fog
    └─ antialiasing
    ↓
GPU-resident high-resolution 3D image
    ↓
VulkanOutput
    ├─ structured 2D metadata
    ├─ VRAM display
    ├─ FIFO display
    ├─ display capture history
    ├─ screen swap
    └─ master brightness
    ↓
GPU-resident two-screen output
    ↓
FrameQueue
    ↓
VulkanSurfacePresenter
    ↓
Qt native window swapchain
```

Vulkan選択時の3D正解源をSoftware Rendererにしない。

通常表示フレームで次を行わない。

```text
Software 3D framebuffer生成
Software 3D結果のCPUコピー
Software 3D ownership mask生成
CPUによる高解像度再構築
QImageへの3D画像変換
CPU BGRA画面をVulkanへ再アップロード
Vulkan imageの通常フレームreadback
Native Qt painterによる最終表示
```

---

# 2. 現行実装を前提にした再構築方針

現在の`highres_fonts_v3`には、Sapphire由来の`VulkanRenderer3D`、structured 2D metadata、`VulkanOutput`、`FrameQueue`、`VulkanSurfacePresenter`が部分的に存在する。

現在の可視経路は概念的に次の状態である。

```text
CreateRendererForSelection(Vulkan)
    ↓
既存desktop RendererとしてSoftRendererを生成
    ↓
GPU3D::CurrentRendererへVulkanRenderer3Dを設定
    ↓
SoftRendererが2D layer評価とstructured line生成を実行
    ↓
VulkanRenderer3Dへstructured lineを蓄積
    ↓
一方でSoftRendererはCPU framebufferも完成
    ↓
ScreenPanelVulkanはScreenPanelNative::drawScreen()へ委譲
```

問題は`GPU3D::CurrentRenderer`の存在ではない。参照コア`melonDS-android-lib @ d77944275fa61f9b79cfcead2c3e98993429a023`も`GPU3D`が`Renderer3D`を単独所有する。

問題は次の未接続と重複である。

```text
VulkanRenderer3DのGPU 3D target
    └─ Qt frontendのVulkanOutputへ未接続

structured 2D CPU arrays
    ├─ CPU getter経路
    └─ in-core GPU upload/compositor経路
       └─ VulkanOutputと責務重複

VulkanReference/VulkanOutput
VulkanReference/FrameQueue
VulkanReference/VulkanSurfacePresenter
    └─ ScreenPanelVulkanの実行経路へ未接続
```

再構築原則を次へ固定する。

1. `GPU3D::CurrentRenderer`を正式な3D backend所有経路として維持する。
2. 新しい`VulkanRenderer : Renderer`をコアへ作らない。
3. Vulkan選択時もdesktop側の既存2D/timing outer rendererを利用し、3D backendだけを`VulkanRenderer3D`へ差し替える。
4. `VulkanOutput`、`FrameQueue`、`VulkanSurfacePresenter`はQt frontend層が所有する。
5. Qt frontendは`GPU3D::CurrentRenderer`から得た`VulkanRenderer3D&`と、2D structured snapshotを直接consumeする。
6. `VulkanRenderer3D`内部の重複final compositorは、frontend経路が接続された後に削除する。
7. structured 2D sourceは当面既存CPU getterを使用し、R17で参照実装どおりGPU2D/SoftRenderer側へ正規化する。
8. `ScreenPanelVulkan`のNative CPU presenter委譲を廃止する。
9. Vulkan Computeという未完成の別backendを表示せず、0.7.0.rc4準拠のGraphics backendへ正規化する。
10. phase marker、contract bool、class名を実装済み判定に使わない。

`SoftRenderer`がouter rendererとして存在することと、Software 3Dが正解源であることを混同しない。Vulkan正常経路では次を満たす。

```text
2D layer evaluation: existing software 2D logic
3D source: VulkanRenderer3DのVkImage
final composition: VulkanOutputのcompute pass
presentation: VulkanSurfacePresenterのswapchain
```

通常表示がCPU framebufferをconsumeしない限り、既存desktop outer rendererを保持してよい。

---

# 3. 最終クラス所有関係

参照アーキテクチャとdesktop既存構造を両立する最終所有関係を次へ統一する。

```text
NDS::GPU
├─ std::unique_ptr<Renderer> Rend
│  └─ SoftRenderer
│     ├─ GPU2D_A / GPU2D_Bのlayer evaluation
│     ├─ display capture/timing integration
│     └─ Vulkan時はstructured 2D snapshot sourceを生成
│
└─ GPU3D
   └─ std::unique_ptr<Renderer3D> CurrentRenderer
      └─ VulkanRenderer3D
         ├─ GPU 3D render targets
         ├─ texture cache
         ├─ graphics/compute pipelines
         ├─ capture line export
         └─ immutable Vulkan3DFrameView
```

Qt frontend側は次を所有する。

```text
EmuInstance
└─ MelonPrimeVulkanFrontendSession
   ├─ VulkanOutput
   ├─ FrameQueue
   ├─ structured snapshot latch
   ├─ generation/frame serial
   └─ producer-side synchronization

ScreenPanelVulkan
├─ native surface host
├─ VulkanSurfacePresenter
├─ VkSurfaceKHR generation
├─ swapchain/layout state
├─ HUD/OSD/radar overlay state
└─ FrameQueue consumer
```

`MelonPrimeVulkanFrontendSession`はQt widgetより長く、`EmuInstance`と同じ寿命を持たせる。surfaceとswapchainはwidget側、rendered frameとcomposition resourceはsession側に分離する。

共有Vulkan objectは次へ集約する。

```text
VulkanContext
├─ VkInstance
├─ VkPhysicalDevice
├─ VkDevice
├─ graphics queue
├─ present queue
├─ queue family indices
├─ queue mutex
├─ enabled extensions/features
├─ device profile
└─ timeline/descriptor/timestamp function pointers
```

禁止する所有関係:

```text
VulkanRenderer3DがVulkanOutputを所有
GPU3DがFrameQueueを所有
ScreenPanelVulkanがVulkanRenderer3Dを所有
Qt presenterが独自VkDeviceを所有
新規VulkanRenderer : Rendererが全責務を束ねる
```

---

# 4. 実装境界

## 4.1 melonDS共通層

共通層の変更は、3D backend切替とstructured snapshot取得に必要な最小限へ限定する。

```text
src/GPU.h
src/GPU.cpp
src/GPU3D.h
src/GPU3D.cpp
src/GPU_Soft.h
src/GPU_Soft.cpp
src/GPU2D_Soft.h
src/GPU2D_Soft.cpp
src/frontend/qt_sdl/EmuThread.cpp
```

共通層で許可する変更:

- `GPU3D::CurrentRenderer`のlifecycleを正式化する
- `SetRenderer3D`とouter renderer切替を一つのtransactionにする
- structured 2D snapshotをimmutableにlatchするhookを追加する
- display captureとaccelerated rendererの共通hookを整理する
- frame serial/generationを固定する

共通層へ`VulkanOutput`、`FrameQueue`、swapchain、Qt型を持ち込まない。

## 4.2 MelonPrime Vulkan core層

```text
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_TexcacheVulkan.h
src/GPU3D_TexcacheVulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
src/VulkanPerfStats.h
```

`GPU_Vulkan.*`は`VulkanRenderer3D` factoryとruntime capabilityの置き場所とする。outer renderer classは追加しない。

`GPU2D_Vulkan.*`は正式なVulkan 2D rendererとして扱わない。既存phase contractやCPU pixel helperはR17/R26で整理し、structured Control ABI adapterが必要な場合だけ限定利用する。

## 4.3 Qt frontend Vulkan層

```text
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.h
src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp
src/frontend/qt_sdl/MelonPrimeScreenVulkan.h
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.h
src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.*
src/frontend/qt_sdl/VulkanReference/FrameQueue.*
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*
```

役割分担:

```text
VulkanReference/
    Sapphire frontendに近いcomposition、queue、presenter logic

MelonPrimeVulkanFrontendSession.*
    EmuThreadからのframe submit、snapshot latch、generation管理

MelonPrimeVulkanSurfaceHost.*
    HWND、XCB/Xlib、Wayland、CAMetalLayerの取得

MelonPrimeScreenVulkan.*
    Qt window lifecycle、layout、HUD、OSD、present consumer
```

## 4.4 MelonPrime分離規則

MelonPrime固有処理は次のbuild gate内へ限定する。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// MelonPrime Vulkan integration
#endif
```

非MelonPrime buildの既存renderer所有関係と動作を変更しない。

---

# 4A. AI Sonnet共通実装指示

各フェーズをAI Sonnetへ依頼するときは、そのフェーズの指示だけでなく、この共通指示も同時に渡す。

## 4A.1 作業対象

```text
Repository: ag-advania/melonPrimeDS
Branch: highres_fonts_v3
Reference frontend:
  SapphireRhodonite/melonDS-android
  tag 0.7.0.rc4

Reference core:
  SapphireRhodonite/melonDS-android-lib
  commit d77944275fa61f9b79cfcead2c3e98993429a023
```

作業開始時に現在の`highres_fonts_v3`を読み直し、計画書内の過去のファイル内容を現在の実装だと仮定しないこと。

## 4A.2 実装原則

1. 対象フェーズのコードをリポジトリへ直接実装すること。
2. 関数本体を省略しないこと。
3. 宣言だけ、空実装、常に成功を返す実装、ログだけの実装を残さないこと。
4. `TODO`、`FIXME`、`stub`、`bootstrap`、将来実装予定のboolで完了扱いにしないこと。
5. Software 3DをVulkanの正解源、代替描画源、通常表示源にしないこと。
6. Vulkanの通常フレームをCPU framebuffer、`QImage`、Qt painterへ戻さないこと。
7. 参照実装のclass境界、descriptor ABI、push constant ABI、frame ownershipを優先すること。
8. Android固有のwindow、JNI、ANativeWindow処理はdesktop adapterへ置き換えること。
9. 参照実装のコードへQt UI処理を直接混ぜないこと。
10. 同じ責務をcoreとfrontendの両方へ二重実装しないこと。
11. 同じVulkan resourceを複数classが所有しないこと。
12. backup directoryを作成しないこと。
13. source fileをリポジトリ内の隠しbackup treeへ複製しないこと。
14. 既存backup treeを参照元として使わないこと。
15. renderer名、contract version、phase markerを能力判定に使わないこと。
16. 実際に生成済みのresourceと有効なruntime stateから能力を判定すること。
17. renderer切替、ROM切替、savestate、scale変更で旧generationのresourceを再利用しないこと。
18. `vkDeviceWaitIdle()`を通常フレームへ入れないこと。
19. GPU queue操作は`VulkanContext`のqueue ownership規則へ統一すること。
20. 変更対象外のOpenGL、Metal、Software rendererの挙動を変えないこと。
21. `GPU3D::CurrentRenderer`を参照コアの正式3D所有経路として維持すること。
22. 新しい`VulkanRenderer : Renderer`を追加しないこと。
23. `VulkanOutput`と`FrameQueue`はQt frontend層へ置くこと。
24. Structured Control ABIを確認するまでverbatim copyしないこと。
25. engine A/Bからtop/bottomへの変換はlatched `ScreenSwap`で行うこと。
26. Sapphireの固定参照に同一責務の完成実装がある場合は、同じ処理を新規設計・再実装せず、参照ファイルまたは参照関数ブロックを先に取り込むこと。
27. 直接移植後の変更は、`Reference code`と`Desktop/MelonPrime adapter`をdiffで分離できる最小範囲に限定すること。
28. 移植と同時に命名整理、style変更、独自最適化、API再設計を行わないこと。必要な変更は別フェーズへ分離すること。
29. shader、descriptor ABI、push constant、packed buffer、pipeline variantは原文を正本とし、計画文から再構成しないこと。
30. Android固有型を理由にファイル全体を書き直さず、platform境界だけをadapterへ置換すること。
31. 原文同期対象は参照blob/hashまたは正規化diffで追跡し、adapter以外の乖離を残さないこと。
32. copyright header、GPL notice、参照repository/path/commit/tagを保持すること。
33. Sapphireに存在しないdesktop glueだけをmelonPrimeDS独自実装として作成すること。

## 4A.3 melonPrimeDS分離規則

melonDS共通コードを変更する場合は、MelonPrime固有処理を可能な限り次のガード内へ置く。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// MelonPrime Vulkan implementation
#endif
```

既存melonDS処理を変更する必要がある場合は、非MelonPrime側の処理を保持する。

```cpp
#ifdef MELONPRIME_DS
    // MelonPrime implementation
#else
    // Existing melonDS implementation
#endif
```

共通interfaceとして必要な変更はbackend-neutralにし、Vulkan API型を無条件に共通headerへ露出しないこと。

## 4A.4 参照実装の扱い

### 原文移植を既定動作にする

参照実装に対象責務が存在する場合、次の順序で作業する。

```text
1. 固定commit/tagの参照ファイルを取得
2. 参照ファイルを移植先へ原文同期
3. compile境界の差分を列挙
4. desktop／melonPrime adapterだけを適用
5. adapter以外の差分がないことを確認
6. manifestへ由来と差分を記録
```

「参照して似たコードを書く」「計画の箇条書きから再構成する」「既存melonPrimeコードへ部分的に写経する」を禁止する。ファイル全体を同期できない場合も、対応するclass、struct、function、shader blockを同期単位として原文移植する。

### 移植区分

```text
Verbatim
    内容を変更せず取り込む。
    include path変更も不要なutility、enum、shader sourceが対象。

Reference-majority
    参照コードを主体にし、include、base API、native surface、frame handoffだけを変更する。

Desktop-only
    Androidに対応物がないQt／OS／thread integrationだけを新規実装する。
```

### 許可するadapter差分

```text
<vulkan/vulkan.h> → <volk.h>
Android include path → qt_sdl内relative include path
ANativeWindow → VulkanDesktopSurface／native handle abstraction
Android EGL/OpenGL frame resource → Vulkan image/fence/timeline metadata
reference Renderer3D API → melonPrimeDS zero-argument virtual wrapper
VulkanRenderer3D direct input → immutable Vulkan3DFrameView
Android logging/callback → melonPrime logging/Qt notification
Android surface queue assumption → desktop graphics/present queue解決
Structured Control source format → 確認済みpacked compositor ABI変換
```

これ以外の差分は原則として`Unexplained divergence`とし、参照実装へ戻すか、必要性をmanifestへ具体的に記録する。

### ライセンス／由来記録

参照コードを移植するときは次を保持する。

```text
参照元repository
参照path
参照commit/tag
original copyright header
GPL notice
移植先path
desktop向け変更点
MelonPrime向け変更点
変更日
```

GPL互換性は確認済みであり、直接移植そのものを避ける理由にはしない。

## 4A.5 フェーズ間境界

- 指示されたフェーズより後の大規模責務を先回りして別方式で実装しない。
- 次フェーズに必要なinterfaceは定義してよいが、偽の成功状態を返す仮実装は作らない。
- 既存の壊れた経路を互換目的で残す場合は、通常実行経路から切り離す。
- 同じ実装を旧経路と新経路で併走させない。
- 後続フェーズで削除するtemporary bridgeが必要な場合は、用途と削除フェーズを名前とコメントで明示する。

## 4A.6 Sonnetの作業報告形式

各フェーズの実装後は、次の順で報告すること。

```text
1. 変更したファイル
2. 新しく追加した型、関数、resource
3. 削除した旧経路
4. 所有権とframe lifecycleの変更
5. 参照実装との差分
6. 後続フェーズへ渡すinterface
```

コードを変更していないのに完了と報告しないこと。

---

# 5. フェーズR0: runtime能力報告を実体へ一致させる

## 5.1 shell contractを能力判定から外す

次をrenderer選択、fallback、actual backend報告に使わない。

```text
VulkanRendererShellContract
NativeVulkan3DImplemented
SapphireRenderer3DOwnership
SapphireFrameLifecycle
SapphireStructured2DMetadata
SapphireGpuCompositionResources
ContractVersion
A1～A7 marker
```

runtime能力はresource実体から構築する。

```cpp
struct VulkanRuntimeCapabilities
{
    bool ContextReady = false;
    bool Renderer3DReady = false;
    bool Structured2DReady = false;
    bool FinalCompositorReady = false;
    bool FrameQueueReady = false;
    bool SurfaceReady = false;
    bool PresenterReady = false;
    bool TimelineSemaphoreReady = false;
    bool DescriptorIndexingReady = false;
};
```

## 5.2 actual backendの定義

outer rendererのclass名だけでactual backendを決めない。

```text
VulkanRenderer3Dだけ初期化済み
    actual = Software
    reason = Vulkan frontend composition/presentation path incomplete

VulkanRenderer3D
+ complete structured snapshot
+ VulkanOutput
+ FrameQueue
+ surface
+ VulkanSurfacePresenter
が接続済み
    actual = Vulkan
```

Vulkan正常経路でouter `SoftRenderer`が存在しても、通常表示がVulkanOutputとVulkanSurfacePresenterを通るならactualはVulkanでよい。

## R0 AI Sonnetへの具体的な実装指示

### 最初に読むファイル

```text
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/VulkanReferencePortVersion.h
src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp
src/frontend/qt_sdl/MelonPrimeVideoBackend.*
src/frontend/qt_sdl/EmuThread.cpp
src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp
```

### 実装内容

1. shell contractとphase boolを能力判定から除去する。
2. capabilityは各resource生成成功後だけtrueにする。
3. `BackendCreationReport`へ`failedStage`と`fallbackReason`を保持する。
4. current frontendがNative CPU presenterへ委譲している間は`actual=Software`を維持する。
5. outer rendererが`SoftRenderer`であることだけをfallback理由にしない。
6. `VulkanReferencePortVersion.h`は参照tag/commit記録だけに限定する。
7. A1～A7 markerを制御分岐へ使わない。

### 禁止事項

- `GPU3D::CurrentRenderer`を削除しない。
- 新しいouter `VulkanRenderer`を追加しない。
- capabilityをcompile-time trueで初期化しない。
- presenter未接続なのにactualをVulkanへしない。

### 完了状態

```text
実体resourceだけがcapabilityを決定
Vulkan frontend未接続中はactual=Software
参照versionとruntime能力が分離
CurrentRenderer所有経路は維持
```

---

# 6. フェーズR1: 0.7.0.rc4基準ソースを固定する

## 6.1 固定する参照点

コアは必ず次のcommitを使う。

```text
d77944275fa61f9b79cfcead2c3e98993429a023
```

frontendは必ず次のtagを使う。

```text
0.7.0.rc4
```

移植元をmaster、nightly、後続releaseへ自動追従させない。

## 6.2 コア側の再取り込み対象

次を参照commitとファイル単位で再照合し、MelonPrime固有追加を分離する。

```text
GPU3D_AcceleratedFrontend.h
GPU3D_AcceleratedFrontend.cpp
GPU3D_Vulkan.h
GPU3D_Vulkan.cpp
GPU3D_TexcacheVulkan.h
GPU3D_TexcacheVulkan.cpp
VulkanContext.h
VulkanContext.cpp
VulkanPerfStats.h
```

shader sourceとgenerated headerも同じcommitへ揃える。

```text
GPU3D_Vulkan_InterpSpansShader.comp
GPU3D_Vulkan_BinCombinedShader.comp
GPU3D_Vulkan_CalculateWorkOffsetsShader.comp
GPU3D_Vulkan_SortWorkShader.comp
GPU3D_Vulkan_TriRasterShader.comp
GPU3D_Vulkan_TriRasterBaseShader.comp
GPU3D_Vulkan_TriRasterCompatShader.comp
GPU3D_Vulkan_DepthBlendShader.comp
GPU3D_Vulkan_FinalPassShader.comp
GPU3D_Vulkan_CaptureLineExportShader.comp
GPU3D_Vulkan_GraphicsRasterShader.vert
GPU3D_Vulkan_GraphicsRasterShader.frag
GPU3D_Vulkan_GraphicsNoColorShader.frag
GPU3D_Vulkan_GraphicsClearShader.frag
GPU3D_Vulkan_GraphicsFinalShader.vert
GPU3D_Vulkan_GraphicsEdgeShader.frag
GPU3D_Vulkan_GraphicsEdgeFogShader.frag
GPU3D_Vulkan_GraphicsFogShader.frag
```

## 6.3 frontend側の再取り込み対象

```text
VulkanOutput.h
VulkanOutput.cpp
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
FrameQueue.h
FrameQueue.cpp
VulkanSurfacePresenter.h
VulkanSurfacePresenter.cpp
VulkanSurfacePresenter.vert
VulkanSurfacePresenter.frag
VulkanFilterMode.h
```

Android固有部分は削除するのではなく、platform adapterへ置換する。

## 6.4 参照コードへのMelonPrime固有差分の置き方

差分は次の3種類に分ける。

```text
Core compatibility adaptation
Desktop platform adaptation
MelonPrime UI/layout adaptation
```

各差分を別関数、別型、別ファイルへ置き、参照関数の内部へ広範囲に混在させない。

## 6.5 直接移植優先順位

### Verbatim同期

```text
src/VulkanPerfStats.h
src/frontend/qt_sdl/VulkanReference/VulkanFilterMode.h
固定core commitの全Vulkan shader source
固定frontend tagのcompositor／accumulate／presenter shader source
```

shader generated headerはコピーせず、原文GLSLからmelonPrimeDSのgeneratorで再生成する。

### Reference-majority同期

```text
GPU3D_AcceleratedFrontend.*
GPU3D_TexcacheVulkan.*
GPU3D_Vulkan.*
VulkanOutput.*
FrameQueue.*
VulkanSurfacePresenter.*
```

これらは現行melonPrimeDSコードへ機能を継ぎ足すのではなく、固定参照を正本にして同期し、許可済みadapter patchだけを再適用する。

### Desktop-only

```text
VulkanContextのsurface extension／present queue部分
MelonPrimeVulkanFrontendSession.*
MelonPrimeVulkanSurfaceHost.*
MelonPrimeScreenVulkan.*
EmuThread／EmuInstance integration
Qt layout／HUD／OSD／radar
```

Android Java/Kotlin、JNI、ANativeWindow所有、EGL/OpenGLContext、Gradle設定は移植しない。

---


## R1 AI Sonnetへの具体的な実装指示

### 目的

0.7.0.rc4と対応するcore commitを唯一の移植基準として固定し、現在混在している別時点のsource、generated shader、MelonPrime独自改変を分類する。

### 最初に読むファイル

```text
src/GPU3D_AcceleratedFrontend.h
src/GPU3D_AcceleratedFrontend.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_TexcacheVulkan.h
src/GPU3D_TexcacheVulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.h
src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp
src/frontend/qt_sdl/VulkanReference/FrameQueue.h
src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp
```

### 参照元

```text
SapphireRhodonite/melonDS-android-lib
commit d77944275fa61f9b79cfcead2c3e98993429a023

SapphireRhodonite/melonDS-android
tag 0.7.0.rc4
```

### 実装内容

1. 参照元の対象ファイルを同一commit/tagから取得する。
2. 現行ファイルと参照ファイルを関数単位で比較する。
3. 差分を次へ分類する。

```text
Reference code
Required melonPrime core adapter
Required desktop adapter
Obsolete phase/bootstrap code
Unexplained divergence
```

4. `Unexplained divergence`は参照実装へ戻す。
5. MelonPrime固有差分は可能な限り別関数または別adapter fileへ移す。
6. copyright headerとlicenseを保持する。
7. shader sourceとgenerated headerの組を同じ参照時点へ揃える。
8. shader source hashとgenerated headerの不一致が生じない構成にする。
9. `VulkanReferencePortVersion.h`へ次を定数として記録する。

```cpp
inline constexpr const char* kSapphireFrontendTag = "0.7.0.rc4";
inline constexpr const char* kSapphireCoreCommit =
    "d77944275fa61f9b79cfcead2c3e98993429a023";
```

10. 参照実装からAndroid固有JNI関数をcoreへ持ち込まない。
11. `ANativeWindow`、Java callback、Android loggingはdesktop adapter対象として切り分ける。
12. 参照実装のnamespaceを無計画にmelonDSへ統合しない。
13. 既に`MelonDSAndroid` namespaceで移植されているfrontend codeは、後続移行中にABIを崩さない。
14. backup treeのファイルと比較せず、GitHub上の固定参照だけを基準にする。
15. 対象ファイルごとに参照元pathをコメントまたは移植manifestへ記録する。
16. 同一責務が参照側に存在するファイルは、参照コードを先に同期し、melonPrimeDS側の既存実装から必要部分だけを選んで残す方式を禁止する。
17. `Verbatim`対象はblob/hashまたはbyte comparison、`Reference-majority`対象はadapterを除いた正規化diffで一致を確認する。
18. adapter patchは可能な限り別header、wrapper、platform hostへ置き、参照関数本体への変更行を最小化する。
19. 参照コードを移植した後に独自最適化を混ぜず、最適化は別commit／別フェーズに分離する。

### 追加する移植manifest

```text
src/VulkanReferenceManifest.md
```

内容は実装情報だけとし、各ファイルについて次を記載する。

```text
移植先
参照repository
参照path
参照commit/tag
保持したdesktop差分
保持したMelonPrime差分
```

### 変更してはいけない範囲

- 参照実装を現在のmasterへ更新しない。
- Android UI、JNI、activity codeを移植しない。
- 現行の未接続frontend経路とin-core重複compositorを参照実装の完成形として正当化しない。
- generated SPIR-Vだけを別commitから混ぜない。

### フェーズ完了時に残す状態

```text
すべてのVulkan主要ファイルの参照時点が一意
MelonPrime差分とdesktop差分が識別可能
不明な独自改変が参照本体へ混在していない
後続フェーズが固定sourceを基準に実装可能
```

---

# 7. フェーズR2: `GPU3D::CurrentRenderer`所有を正式化する

## 7.1 参照設計

Sapphireコアでは`GPU3D`が`Renderer3D`を単独所有する。この方針をmelonPrimeDSへ正式採用する。

```text
GPU3D
└─ std::unique_ptr<Renderer3D> CurrentRenderer
   ├─ SoftwareRenderer3D
   ├─ GLRenderer3D / ComputeRenderer3D
   ├─ MetalRenderer3D
   └─ VulkanRenderer3D
```

現在のVulkan専用`CurrentRenderer` graftを削除せず、lifecycleと命名を整理する。

## 7.2 canonical API

最終的に次のAPIへ寄せる。

```cpp
Renderer3D& GetCurrentRenderer() noexcept;
const Renderer3D& GetCurrentRenderer() const noexcept;
void SetCurrentRenderer(std::unique_ptr<Renderer3D>&& renderer);
```

既存`GetCurrentRendererOverride()`は移行期間だけaliasとして残し、R26でcall siteがゼロになった後に削除する。

## 7.3 factory整理

```text
CreateRendererForSelection()
    outer 2D/timing Rendererを生成

CreateRenderer3DForSelection()
    Renderer3D backendを生成
```

`CreateRenderer3DOverrideForSelection()`は`CreateRenderer3DForSelection()`へ改名する。機能自体は削除しない。

## 7.4 切替transaction

```text
capture同期
frontend producer停止
旧3D renderer Stop
旧3D renderer破棄
新outer renderer生成
新3D renderer生成
GPU::SetRenderer
GPU3D::SetCurrentRenderer
render settings適用
VRAM/texture cache generation更新
frontend generation更新
producer再開
```

途中状態をGUIへ公開しない。

## R2 AI Sonnetへの具体的な実装指示

### 対象ファイル

```text
src/GPU.h
src/GPU.cpp
src/GPU3D.h
src/GPU3D.cpp
src/frontend/qt_sdl/MelonPrimeRendererFactory.*
src/frontend/qt_sdl/EmuThread.cpp
```

### 実装内容

1. `CurrentRenderer`を維持する。
2. canonical getter/setterを追加または既存APIへ統一する。
3. legacy `Override`名称をtemporary alias化する。
4. factory名から`Override`を除去する。
5. Stop/Reset/SetRenderSettingsが一回だけ呼ばれるよう切替順序を整理する。
6. outer rendererと3D rendererを同じ更新transactionで生成する。
7. Vulkan初期化失敗時だけSoftwareRenderer3Dへ戻す。
8. frontend sessionのgenerationをrenderer切替と同時更新するhookを用意する。

### 禁止事項

- `CurrentRenderer`を削除しない。
- new outer `VulkanRenderer`へ所有を移さない。
- global/static raw pointerへ置き換えない。
- SoftRenderer3DとVulkanRenderer3Dを通常フレームで併走させない。

### 完了状態

```text
GPU3DがRenderer3Dの唯一のowner
VulkanRenderer3DはCurrentRendererへ格納
切替lifecycleが一回
legacy Override APIは移行aliasのみ
```

---

# 8. フェーズR3: Qt frontend Vulkan出力経路を接続する

R3は新しいコアrenderer classを作るフェーズではない。既存`VulkanRenderer3D`、structured 2D data、`VulkanOutput`、`FrameQueue`、`VulkanSurfacePresenter`を実行経路へ接続する。

R3は作業量が大きいためR3a～R3dへ分割する。

## 8.1 R3a: Structured Control ABIを確定し、snapshot bridgeを実装する

### 8.1.1 確認済みStructured2Dレイアウト

2026-07-14時点の`GPU3D_Vulkan.h/.cpp`では次を所有する。

```cpp
std::array<u32, 2 * 256 * 192> Structured2DPlane0{};
std::array<u32, 2 * 256 * 192> Structured2DPlane1{};
std::array<u32, 2 * 256 * 192> Structured2DControl{};
std::array<u32, 2 * 256 * 192> Structured2DNativeFinal{};
std::array<u32, 2 * 192> Structured2DLineMeta{};   // DispCntの生値
std::array<u32, 2 * 192> Structured2DLineState{};  // MasterBrightness | ScreensEnabled<<16 | EngineEnabled<<17 | ForcedBlank<<18
```

`SubmitStructured2DLine`は次のindexで書く。

```cpp
lineIndex = engine * 192 + line;
pixelOffset = lineIndex * 256;
```

したがって配列は物理top/bottomではなくengine順である。

```text
[0, 256*192)           = engine A
[256*192, 2*256*192)   = engine B
```

### 8.1.2 ScreenSwap変換

`SoftPackedFrameSnapshot`へlatchするときだけ物理screenへ割り当てる。

```text
ScreenSwap == false
    top    = engine A
    bottom = engine B

ScreenSwap == true
    top    = engine B
    bottom = engine A
```

Plane0、Plane1、Control、NativeFinal、LineMeta、LineStateの全てへ同じ変換を適用する。

live `GPU.ScreenSwap`をpresent時に再読込せず、snapshotのframe identityへ固定する。

### 8.1.3 complete条件

`EndStructured2DFrame`は`2*192=384` engine-lineの受信が完了した場合だけ`Structured2DFrameValid=true`にする。

現行実装は成功時に`packStructured2DFrame()`と`uploadStructured2DFrameToGpu()`も呼ぶ。CPU getter経路とin-core GPU buffer経路は同じ`Structured2D*` sourceを共有するが、消費先は別である。R3aはCPU getterだけを使用し、in-core GPU upload/compositorは呼ばない。frontend経路が成立した後、R6で後者を削除する。

R3aでは`HasCompleteStructured2DFrame()`がtrueのframeだけをlatchする。不完全frameを前frameと混ぜない。

### 8.1.4 Control ABIの必須確認

実装コードを書く前に次を突き合わせる。

```text
生成側:
    GPU_Soft.cppでStructuredControl[engine][x]へ値を書く箇所
    packet.Controlへ渡すbit layout

消費側:
    SoftPackedFrameSnapshot::packedTopControl
    SoftPackedFrameSnapshot::packedBottomControl
    VulkanOutput.cppのpacking
    VulkanCompositorShader.compのbit decode
```

同一ABIであることを名前だけで仮定しない。

結果に応じて次のどちらかを実装する。

```cpp
// 完全一致時
CopyStructuredControlVerbatim(...);

// 不一致時
u32 ConvertStructuredControlToCompositorControl(u32 source);
```

binding定数とbit定数を共通ABI headerへ置き、C++とshaderで二重定義しない。

### 8.1.5 CPU getter bridge

R3aでは既存CPU getterから`SoftPackedFrameSnapshot`を構築する。

```text
VulkanRenderer3D::HasCompleteStructured2DFrame
VulkanRenderer3D::GetStructured2DPlane0
VulkanRenderer3D::GetStructured2DPlane1
VulkanRenderer3D::GetStructured2DControl
VulkanRenderer3D::GetStructured2DNativeFinal
VulkanRenderer3D::GetStructured2DLineMeta
VulkanRenderer3D::GetStructured2DLineState
```

このbridgeはR17で参照型GPU2D sourceへ置換するtemporary bridgeである。

## 8.2 R3b: frontend sessionを実装する

追加する。

```text
MelonPrimeVulkanFrontendSession.h
MelonPrimeVulkanFrontendSession.cpp
```

最低限のAPI:

```cpp
class MelonPrimeVulkanFrontendSession
{
public:
    bool initialize(melonDS::NDS& nds);
    void shutdown();
    void beginGeneration(u64 generation);
    bool submitCompletedFrame(
        melonDS::VulkanRenderer3D& renderer3D,
        const MelonPrimeStructuredSnapshot& snapshot);
    MelonDSAndroid::Frame* acquirePresentFrame();
    void commitPresentedFrame(MelonDSAndroid::Frame* frame);
};
```

sessionが所有する。

```text
VulkanOutput
FrameQueue
producer-side mutex/state
generation
frame serial
last complete snapshot
```

## 8.3 R3c: FrameQueueとVulkanOutputを接続する

producer flowを次へ固定する。

```text
FrameQueue::getRenderFrame
    ↓
VulkanOutput::prepareFrameForPresentation
    ↓
VulkanOutput::composeAndSubmitFrame
    ↓
FrameQueue::pushRenderedFrame
```

`VulkanOutput`へ渡す`VulkanRenderer3D&`は次から取得する。

```text
EmuInstance::getNDS()
    ↓
NDS::GPU.GPU3D.GetCurrentRenderer()
    ↓
dynamic_cast<VulkanRenderer3D*>
```

legacy branchでcanonical getterがまだ無い場合だけ`GetCurrentRendererOverride()`をtemporary adapterとして使う。

GUI threadからlive GPU stateを読まない。snapshot作成とrender frame submitはEmuThread側のframe completion pointで行う。

## 8.4 R3d: Windows basic presenterを接続する

R3dではWindowsの最小可視経路を作る。

```text
ScreenPanelVulkan::winId()
    ↓
HWND
    ↓
VulkanSurfacePresenter::attachSurface
    ↓
FrameQueue::getPresentFrame
    ↓
VulkanSurfacePresenter::presentFrame
    ↓
FrameQueue::commitPresentedFrame
```

`ScreenPanelNative::drawScreen()`への委譲を削除する。

R3dではWindows basic pathに限定し、Linux/macOS surfaceの一般化はR20、layout/HUD完成はR21で行う。

## R3 AI Sonnetへの具体的な実装指示

### 最初に読むファイル

```text
src/GPU_Soft.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/frontend/qt_sdl/EmuInstance.*
src/frontend/qt_sdl/EmuThread.cpp
src/frontend/qt_sdl/MelonPrimeScreenVulkan.*
src/frontend/qt_sdl/VulkanReference/VulkanOutput.*
src/frontend/qt_sdl/VulkanReference/FrameQueue.*
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*
src/frontend/qt_sdl/VulkanReference/VulkanCompositorShader.comp
```

### 作業順序

```text
R3a-0 Control生成側とcompositor側ABIを比較
R3a-1 共通Control ABIまたは変換関数を実装
R3a-2 engine配列からtop/bottom snapshotをlatch
R3a-3 384 line completeとframe identityを固定
R3b   frontend sessionを追加
R3c   VulkanOutputとFrameQueueをproducerへ接続
R3d   Windows surfaceとbasic presenterを接続
```

### 禁止事項

- Control ABI未確認のまま`memcpy`しない。
- engine Aを常にtopと仮定しない。
- `ScreenSwap`をpresent時のlive値から取得しない。
- incomplete structured frameをsubmitしない。
- GUI threadからstructured arraysを無ロックで読む。
-新しい`VulkanRenderer : Renderer`を作らない。
- in-core `uploadStructured2DFrameToGpu()`をfrontend compositionと同時利用しない。

### 完了状態

```text
Control ABIが明文化または変換済み
engine→top/bottom変換がScreenSwap対応
384 line complete frameだけがsubmit
VulkanOutputとFrameQueueが実行経路へ接続
WindowsでScreenPanelVulkanがVulkan frameをpresent
```

---

# 9. フェーズR4: 実働`VulkanContext`へdesktop surface要件を追加する

現在の`VulkanContext`はスタブではない。既に次を実装している。

```text
Acquire/Release参照カウント
VkInstance生成
physical device列挙と選択
graphics queue family探索
timeline semaphore検出
descriptor indexing検出
VkDevice生成
volkLoadDevice
function pointer解決
Win32 surface extension
```

R4で全面書き直しを行わない。既存動作を保持して不足だけ追加する。

## 9.1 platform instance extensions

```text
Windows: VK_KHR_surface + VK_KHR_win32_surface
X11/XCB: VK_KHR_surface + VK_KHR_xcb_surface または xlib_surface
Wayland: VK_KHR_surface + VK_KHR_wayland_surface
macOS: VK_KHR_surface + VK_EXT_metal_surface
        VK_KHR_portability_enumeration
```

extensionの存在を列挙結果で確認してからenableする。

## 9.2 present queue

surface attach後にpresent supportを解決できるAPIを追加する。

```cpp
bool ResolvePresentQueue(VkSurfaceKHR surface);
u32 GetGraphicsQueueFamily() const;
u32 GetPresentQueueFamily() const;
VkQueue GetGraphicsQueue() const;
VkQueue GetPresentQueue() const;
```

同一familyならqueueを共有し、異なる場合はownership transferを有効にする。

## 9.3 MoltenVK

instance flagへ`VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR`を追加し、portability subsetをdevice extensionへ含める。

## R4 AI Sonnetへの具体的な実装指示

### 実装内容

1. compact implementationを機能単位で読み、既存処理を保持する。
2. 行数差だけを理由に参照版へ全置換しない。
3. platform requirement structを追加する。
4. Linux/macOS extension選択を追加する。
5. surfaceごとのpresent queue解決を追加する。
6. graphics/present familyが異なる場合の情報をfrontendへ公開する。
7. device feature chainの自己代入や未初期化があれば局所修正する。

### 禁止事項

- rendererとpresenterで別deviceを作らない。
- Windows extensionを全platformで要求しない。
- surface未作成をgraphics renderer初期化失敗にしない。
-既存Acquire/Releaseを全面置換しない。

### 完了状態

```text
既存graphics contextが維持
全desktop platformのinstance extensionを選択可能
surfaceごとのpresent queueを解決可能
同一VkDeviceを3D/output/presenterが共有
```

---

# 10. フェーズR5: shader生成経路を一つにする

## 10.1 sourceを正とする

`.comp`、`.vert`、`.frag`を正とし、generated headerを自動生成する。

手編集したSPIR-V配列を正にしない。

## 10.2 generator

```text
tools/vulkan/generate_sapphire_spirv.py
```

へ次を実装する。

```text
glslangValidatorの解決
source一覧読込
define variant生成
SPIR-V生成
C++ header生成
source hash記録
ABI manifest生成
```

## 10.3 core shader一覧

参照commitのshaderをそのまま生成対象にする。

graphics shaderとcompute shaderを同じmanifestへ置く。

## 10.4 compositor shader

現在のinline shader dataとA6/A7専用moduleを廃止し、参照frontendと同じgenerated headerへ統一する。

```text
VulkanCompositorShaderData.h
VulkanAccumulate3dShaderData.h
VulkanSurfacePresenterVertexShaderData.h
VulkanSurfacePresenterFragmentShaderData.h
```

## 10.5 ABI定義

descriptor bindingとpush constantをshader側、core側、frontend側で重複定義しない。

共通headerへ集約する。

```cpp
struct VulkanCompositorAbi
{
    static constexpr u32 OutputImage = 0;
    static constexpr u32 Current3DImage = 1;
    static constexpr u32 TopPackedBuffer = 2;
    static constexpr u32 BottomPackedBuffer = 3;
    static constexpr u32 PreviousTop3DImage = 4;
    static constexpr u32 Capture3DBuffer = 5;
    static constexpr u32 PreviousBottom3DImage = 6;
};
```

---


## R5 AI Sonnetへの具体的な実装指示

### 目的

Vulkan shaderの正本をGLSL sourceへ統一し、参照commitと一致するgenerated SPIR-V headerを一つの生成経路から作る。

### 対象ファイル

```text
src/GPU3D_Vulkan_*Shader.comp
src/GPU3D_Vulkan_*Shader.vert
src/GPU3D_Vulkan_*Shader.frag
src/GPU3D_Vulkan_*ShaderData.h
src/frontend/qt_sdl/VulkanReference/*Shader.*
src/frontend/qt_sdl/VulkanReference/*ShaderData.h
src/CMakeLists.txt
src/frontend/qt_sdl/CMakeLists.txt
tools/vulkan/
```

### generator実装

```text
tools/vulkan/generate_sapphire_spirv.py
```

を追加または再構築する。

generatorは次を行う。

1. shader manifestを読む。
2. `glslangValidator`または指定compilerを解決する。
3. stageごとのentry pointを指定する。
4. variant defineを展開する。
5. `.spv`を生成する。
6. `uint32_t`配列のC++ headerを生成する。
7. byte size、word count、source hashをheaderへ埋め込む。
8. manifest全体のABI hashを生成する。
9. 同じ入力から決定的な出力を作る。
10. 生成物のnamespaceとsymbol名を固定する。

### manifest

```text
tools/vulkan/sapphire_shader_manifest.json
```

へ次を記載する。

```text
source path
stage
entry point
generated header path
symbol name
defines
reference repository
reference commit/tag
```

### shader ABI共通化

次を共通headerへ移す。

```text
descriptor binding constants
push constant size
buffer stride constants
image layer constants
variant bit definitions
```

候補:

```text
src/VulkanShaderAbi.h
src/frontend/qt_sdl/VulkanReference/VulkanCompositorAbi.h
```

coreとfrontendで同じ定数を重複定義しない。

### CMake

1. generated shader targetを一つ作る。
2. coreとfrontendをそのtargetへ依存させる。
3. source変更時だけ再生成する。
4. build directory内へ生成する。
5. source treeの古い手編集headerをincludeしない。
6. generated include directoryを明示する。
7. platformごとにshader一覧を変えない。

### 現行A6/A7 shader data

inline配列、個別に埋め込んだcompositor SPIR-V、ODR回避用複製を削除し、generated symbol一つへ統一する。

### 変更してはいけない範囲

- SPIR-V byte列を手編集しない。
- coreとfrontendで同じshaderを別symbolとして生成しない。
- shader sourceとgenerated headerを別commitから混ぜない。
- push constant sizeをC++側だけ変更しない。
- buildごとにsymbol名が変化する生成方式にしない。

### フェーズ完了時に残す状態

```text
GLSL sourceが唯一の正本
全generated headerが同じgenerator経由
coreとfrontendのABI定数が共有
A6/A7専用inline shader複製が不存在
```

---

# 11. フェーズR6: `VulkanRenderer3D`内の重複compositionを削除する

R3 frontend経路が接続された後、`VulkanRenderer3D`を純粋な3D rendererへ戻す。

## 11.1 削除対象

```text
SapphireVulkanCompositionInput
SapphireVulkanCompositionResources
SapphireVulkanCompositionCommandContext
SapphireVulkanCompositionShaderModule
SapphireCompositionDescriptorAbi
SapphireCompositionPushConstants
SapphireCompositionImage/Memory/View/Sampler
composition descriptor/pipeline/command/fence
packStructured2DFrame
uploadStructured2DFrameToGpu
in-core packed structured GPU buffer
```

これらはfrontend `VulkanOutput`と責務が重複する。

## 11.2 R6で一時的に残すもの

R3a bridgeが使用する次はR17まで残す。

```text
Structured2DPlane0
Structured2DPlane1
Structured2DControl
Structured2DNativeFinal
Structured2DLineMeta
Structured2DLineState
Structured2DLineReceived
Structured2DFrameValid
CPU getter
SubmitStructured2DLine
Begin/EndStructured2DFrame
```

`EndStructured2DFrame`はcomplete判定とpublishだけ行い、GPU upload/compositionを呼ばない形へ変更する。

## 11.3 3D handoff

```cpp
struct Vulkan3DFrameView
{
    VkImage ColorImage = VK_NULL_HANDLE;
    VkImageView ColorImageView = VK_NULL_HANDLE;
    VkImageLayout ColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    u32 Width = 0;
    u32 Height = 0;
    u32 Scale = 1;
    u64 FrameSerial = 0;
    u64 Generation = 0;
    VkSemaphore CompletionSemaphore = VK_NULL_HANDLE;
    u64 CompletionValue = 0;
    bool Valid = false;
};
```

frontendはこのimmutable viewだけをconsumeする。

## R6 AI Sonnetへの具体的な実装指示

1. frontend VulkanOutputが実際にframeをconsumeしていることを前提に作業する。
2. in-core composition resourceとdispatchを削除する。
3. CPU structured source/getterはR17まで保持する。
4. `EndStructured2DFrame`からGPU upload callを除去する。
5. `Vulkan3DFrameView`を追加し、resource lifetimeをFrameQueue completionへ関連付ける。
6. 3D rendererへQt、layout、top/bottom final outputを持ち込まない。

禁止:

- CPU getterまでR6で削除しない。
- frontend compositorとin-core compositorを併走させない。
- 3D ColorImageをtwo-screen final imageとして流用しない。

完了状態:

```text
VulkanRenderer3Dは3D targetとcaptureだけを所有
frontend VulkanOutputが唯一のfinal compositor
R3a CPU structured bridgeは維持
in-core GPU packed/composition resourceは不存在
```

---

# 12. フェーズR7: 3D render targetを完成させる

## 12.1 target size

```text
Width  = 256 × ScaleFactor
Height = 192 × ScaleFactor
```

scale変更時は3D targetを再生成する。

presenter側だけで拡大しない。

## 12.2 image構成

```text
ColorImage
AttrImage
DepthImage
DepthStencilImage
CaptureReadbackImage
```

用途を次へ揃える。

### ColorImage

```text
COLOR_ATTACHMENT
STORAGE
SAMPLED
TRANSFER_SRC
TRANSFER_DST
```

### AttrImage

```text
COLOR_ATTACHMENT
STORAGE
SAMPLED
TRANSFER_SRC
```

### DepthImage

```text
STORAGE
SAMPLED
TRANSFER_SRC
```

### DepthStencilImage

```text
DEPTH_STENCIL_ATTACHMENT
TRANSFER_SRC
```

## 12.3 format

colorとattributeは参照実装のformatを維持する。

depth/stencilはdevice対応から選択する。

```text
D32_SFLOAT_S8_UINT
D24_UNORM_S8_UINT
D16_UNORM_S8_UINT
```

## 12.4 image ownership

3D targetをpresenterへ直接貸さない。

`VulkanOutput`が3D targetを入力として参照し、別のframe output imageへ合成する。

---


## R7 AI Sonnetへの具体的な実装指示

### 目的

内部解像度へ対応したGPU 3D render target一式を、参照実装の用途とlayoutへ揃えて実装する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
```

### 実装するresource

```text
ColorImage
ColorImageMemory
ColorImageView

AttrImage
AttrImageMemory
AttrImageView

DepthImage
DepthImageMemory
DepthImageView

DepthStencilImage
DepthStencilImageMemory
DepthStencilImageView

CaptureReadbackImage
CaptureReadbackMemory
```

### target寸法

```cpp
width = 256u * ScaleFactor;
height = 192u * ScaleFactor;
```

`ScaleFactor`は1以上へclampする。

scale変更時は新targetを作成してから旧targetをretireする。presenterが参照中のimageを即時破棄しない。

### image usage

`ColorImage`:

```text
VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
VK_IMAGE_USAGE_STORAGE_BIT
VK_IMAGE_USAGE_SAMPLED_BIT
VK_IMAGE_USAGE_TRANSFER_SRC_BIT
VK_IMAGE_USAGE_TRANSFER_DST_BIT
```

`AttrImage`:

```text
COLOR_ATTACHMENT
STORAGE
SAMPLED
TRANSFER_SRC
```

`DepthImage`:

```text
STORAGE
SAMPLED
TRANSFER_SRC
TRANSFER_DST
```

`DepthStencilImage`:

```text
DEPTH_STENCIL_ATTACHMENT
TRANSFER_SRC
TRANSFER_DST
```

### format選択

1. color/attrは参照実装と同一formatを使う。
2. depth/stencilはdevice format propertiesから選ぶ。
3. 選択結果を`VulkanRenderer3D`初期化stateへ保存する。
4. render pass、framebuffer、pipeline作成へ同じformatを渡す。
5. format変更時は関連pipelineを再作成する。

### memory

1. device-local memoryを使用する。
2. memory type選択は`VulkanContext::FindMemoryType()`へ統一する。
3. allocation failure時に途中resourceを全て解放する。
4. imageとmemoryの所有を同じclassへ固定する。
5. image view破棄後にimageを破棄し、その後memoryを解放する。

### layout tracking

各imageの現在layoutをfieldで管理する。

barrier関数を共通化する。

```cpp
void TransitionImage(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess);
```

利用するVulkan versionに合わせ、同期2未対応なら旧barrierへadapterする。

### clear

target作成直後に未定義pixelをsampleしないよう、初回利用時に全attachmentを明示clearする。

### 変更してはいけない範囲

- presenter拡大だけでScaleFactorを実装しない。
- CPU配列を高解像度3D targetとして保持しない。
- ColorImageをfinal two-screen outputと兼用しない。
- framebuffer再作成時にdevice全体をidleにしない。
- depth valueを8-bit colorへ詰めて代用しない。

### フェーズ完了時に残す状態

```text
内部解像度に応じた実サイズの3D image
color、attribute、depth、stencilがGPU resource
layoutとlifetimeがrenderer内で一元管理
Vulkan3DFrameViewがColorImageを公開可能
```

---

# 13. フェーズR8: render context ringを完成させる

## Sapphire直接移植規則

`melonDS-android-lib @ d779442...`の`GPU3D_Vulkan.h/.cpp`にある`RenderContext`、context ring、buffer確保、descriptor cache、fence待機、submit処理を同期単位として原文移植する。以下の節は参照コードの再実装仕様ではなく、移植後に保持すべき条件である。melonPrimeDS独自に別のcontext ringを設計しない。

許可する差分はVolk dispatch、melonPrimeDSの`Renderer3D` wrapper、`Vulkan3DFrameView` completion handoff、desktop `VulkanContext` getter名だけとする。


## 13.1 context数

参照実装と同じ6 contextを基本とする。

```cpp
static constexpr size_t AsyncRenderContextCount = 6;
```

## 13.2 contextごとのresource

```text
command pool
command buffer
frame fence
descriptor set
graphics descriptor set
single texture descriptor sets
triangle buffer
graphics vertex buffer
bin mask buffer
group list buffer
span setup buffer
work offset buffer
toon buffer
clear buffer
capture line buffer
timestamp query pool
descriptor cache
```

## 13.3 再利用

次のcontextだけ待つ。

```text
次に再利用するcontext
capture readback source context
texture cache mutationと競合するcontext
```

通常フレームで`vkDeviceWaitIdle()`を呼ばない。

## 13.4 buffer growth

必要capacityを超えた場合だけ再確保する。

```cpp
newCapacity = max(requiredCapacity, oldCapacity * 2);
```

persistent mapped memoryを利用する。

---


## R8 AI Sonnetへの具体的な実装指示

### 目的

複数フレームを安全に処理するrender context ringを実装し、単一command bufferと全体待機への依存を除去する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/VulkanContext.h
src/VulkanContext.cpp
```

### context構造

参照実装に合わせ、`RenderContext`へ次を所有させる。

```text
VkCommandPool
VkCommandBuffer
VkFence FrameFence
VkDescriptorSet
VkDescriptorSet GraphicsDescriptorSet
single texture descriptor sets
TriangleBuffer
GraphicsVertexBuffer
BinMaskBuffer
GroupListBuffer
SpanSetupBuffer
WorkOffsetBuffer
ToonBuffer
ClearBuffer
CaptureLineBuffer
TimestampQueryPool
descriptor cache
```

### context数

```cpp
static constexpr size_t AsyncRenderContextCount = 6;
std::array<RenderContext, AsyncRenderContextCount> RenderContexts;
```

### acquire

`AcquireRenderContext()`を実装する。

```text
NextRenderContextIndexを選択
そのcontextのfenceだけ待つ
fence reset
command pool reset
descriptor cacheのresource identity確認
mapped buffer使用範囲reset
contextをcurrentとして返す
NextRenderContextIndex更新
```

### buffer確保

各bufferへcapacity fieldを持たせる。

必要量がcapacityを超えた場合だけ再確保する。

```cpp
newCapacity = std::max(required, oldCapacity == 0 ? required : oldCapacity * 2);
```

host-visible bufferはpersistent mapする。

non-coherent memoryの場合は書込範囲をflushする。

### descriptor cache

resource handleが変化したbindingだけ更新する。

毎frame全descriptorを書き直さない。

texture descriptor countが減った場合に古いdescriptorを参照しないようfallback descriptorで埋める。

### submit

1. command bufferをendする。
2. queue lockを取得する。
3. 3D dependency semaphoreをwaitへ設定する。
4. context fence付きでsubmitする。
5. completion semaphore/timeline valueを発行する。
6. queue lockを解放する。
7. submitted contextをframe identityへ関連付ける。

### timestamp

対応deviceだけquery poolを作る。

timestamp queryがないdeviceでもrender pathを変えない。

### 変更してはいけない範囲

- 全contextを毎frame待たない。
- 単一global command poolへ戻さない。
- mapped pointerをbuffer再確保後も保持しない。
- context fenceをpresent fenceとして兼用しない。
- descriptor setを複数in-flight frameで書き換えない。

### フェーズ完了時に残す状態

```text
6 contextが独立resourceを所有
再利用対象contextだけ待機
bufferはcapacity growth
in-flight descriptor上書きなし
3D completionをframe handoffへ渡せる
```

---

# 14. フェーズR9: accelerated sceneを正式入力にする

## Sapphire直接移植規則

`GPU3D_AcceleratedFrontend.h/.cpp`をファイル単位の同期対象とする。geometry conversion、line解決、coverage fix、draw orderingを計画文から書き直さない。参照ファイルを原文同期した後、melonPrimeDS固有設定の入力adapterだけを追加する。


## 14.1 共通scene builder

`GPU3D_AcceleratedFrontend`をgraphics pathの唯一のgeometry sourceにする。

別のMelonPrime packed vertex builderを併走させない。

## 14.2 scene内容

```text
Vertices
Indices
EdgeIndices
Triangles
Draws
FirstTranslucentDraw
```

各drawへ次を持たせる。

```text
source polygon
render key
flags
poly attr
poly ID
alpha
primitive type
coverage state
first vertex
vertex count
first index
index count
first edge index
edge index count
first triangle
triangle count
```

## 14.3 source order

`RenderPolygonRAM`順を保持する。

同一pipeline stateでも非隣接polygonをまとめない。

```text
A B A
```

は3 draw rangeのままとする。

## 14.4 line polygon

参照実装のline解決とquad化を使用する。

MelonPrime独自の別line expansionを重ねない。

## 14.5 high-resolution coordinates

```text
ScaleFactor
BetterPolygons
HiresCoordinates
coverage fix
```

をscene build configへ一括で渡す。

`HiresCoordinates`を捨てない。

現在の`SetRenderSettings()`内の`(void)hiresCoordinates`を廃止する。

---


## R9 AI Sonnetへの具体的な実装指示

### 目的

`GPU3D_AcceleratedFrontend`がVulkan graphics pathの唯一のgeometry変換源になるよう統合する。

### 対象ファイル

```text
src/GPU3D_AcceleratedFrontend.h
src/GPU3D_AcceleratedFrontend.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D.h
src/GPU3D.cpp
```

### scene ABI

参照実装の`AcceleratedScene`、vertex、triangle、draw layoutを維持する。

sceneへ最低限次を含める。

```text
Vertices
Indices
EdgeIndices
Triangles
Draws
FirstTranslucentDraw
```

### build input

`GPU3D`から次をimmutable inputとして渡す。

```text
RenderPolygonRAM
RenderNumPolygons
RenderDispCnt
RenderAlphaRef
RenderClearAttr1
RenderClearAttr2
RenderFogColor
RenderFogOffset
RenderFogShift
RenderFogDensityTable
RenderToonTable
RenderEdgeTable
RenderXPos
```

### build config

```cpp
struct AcceleratedSceneBuildConfig
{
    u32 ScaleFactor;
    bool BetterPolygons;
    bool HiresCoordinates;
    bool ConservativeCoverageEnabled;
    float ConservativeCoveragePixels;
    float ConservativeCoverageDepthBias;
    bool CoverageRepeat;
    bool CoverageClamp;
};
```

現在捨てられている`HiresCoordinates`をscene builderへ渡す。

### ordering

1. `RenderPolygonRAM`順を保持する。
2. opaqueとtranslucentの境界は参照実装のsort/orderを使う。
3. pipeline stateが同じでも非隣接drawを結合しない。
4. shadow maskとshadow blendの順序を保持する。
5. line polygon、triangle、quadのprimitive変換を参照実装へ揃える。

### vertex conversion

次を参照精度で変換する。

```text
viewport transform
clip result
high-resolution coordinate
reciprocal W
Z/W depth
5-bit vertex color
texture coordinate
polygon attributes
```

floatへの変換位置を参照実装から動かさない。

### draw key

pipeline variantへ必要なstateを`RenderKey`またはflagsへ格納する。

```text
W-buffer
depth equal
alpha
polygon mode
texture mode
toon/highlight
fog
shadow
edge
coverage
```

### upload

scene build後にcurrent `RenderContext`のmapped bufferへ一度でcopyする。

drawごとのsmall uploadを行わない。

### 変更してはいけない範囲

- Vulkan専用の第二scene builderを追加しない。
- SoftwareRenderer3Dのpolygon loopをコピーしない。
- source polygon pointerをGUI threadへ渡さない。
- non-adjacent drawをstate sortで並べ替えない。
- high-resolution設定をpresenterへ委譲しない。

### フェーズ完了時に残す状態

```text
GPU3D_AcceleratedFrontendが唯一のgeometry frontend
VulkanRenderer3DはAcceleratedSceneだけをupload
polygon orderとDS attributeがsceneに保持
HiresCoordinatesとScaleFactorがgeometryへ反映
```

---

# 15. フェーズR10: Vulkan texture cacheを参照実装へ揃える

## Sapphire直接移植規則

`GPU3D_TexcacheVulkan.h/.cpp`は固定core commitを正本にしてファイル単位で同期する。既知のheader差分は`<vulkan/vulkan.h>`から`<volk.h>`への置換である。実装側もtexture allocation、upload、opaque判定、descriptor取得、cleanupを原文から取り込み、desktop `VulkanContext`／queue accessor差分だけを適用する。

texture cacheを計画の箇条書きから新規実装しない。


## 15.1 loader

```cpp
class TexcacheVulkanLoader
{
public:
    TextureHandle GenerateTexture(
        u32 width,
        u32 height,
        u32 layers);

    void UploadTexture(
        TextureHandle handle,
        u32 width,
        u32 height,
        u32 layer,
        void* data);

    void DeleteTexture(TextureHandle handle);

    bool GetTextureDescriptor(
        TextureHandle handle,
        VkDescriptorImageInfo* out) const;

    bool IsTextureLayerOpaque(
        TextureHandle handle,
        u32 layer) const;
};
```

## 15.2 cache更新

```text
VRAMDirty_Texture
VRAMDirty_TexPal
VRAMMap_Texture
VRAMMap_TexPal
```

を使い、変更textureだけを更新する。

## 15.3 descriptor path

device能力に応じて次を選ぶ。

```text
NonUniform
CompatDynamicUniform
BaseSingleDescriptor
```

いずれもGPU rasterを使う。

descriptor indexing非対応をSoftware fallback理由にしない。

## 15.4 sampler

S/T軸のclamp、repeat、mirror組み合わせを共有sampler tableへする。

drawごとにsamplerを生成しない。

## 15.5 fallback texture

白1×1 textureをrenderer初期化時に一度作る。

texture decode失敗polygonを削除せず、untexturedまたはfallback textureとして描画する。

---


## R10 AI Sonnetへの具体的な実装指示

### 目的

DS textureとpaletteをVulkan imageへ変換し、polygon shaderがSoftware texture decodeへ依存せずsampleできるtexture cacheを完成させる。

### 対象ファイル

```text
src/GPU3D_Texcache.h
src/GPU3D_Texcache.cpp
src/GPU3D_TexcacheVulkan.h
src/GPU3D_TexcacheVulkan.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU.h
src/GPU.cpp
```

### loader API

参照実装の`TexcacheVulkanLoader`相当を完成させる。

```text
GenerateTexture
UploadTexture
DeleteTexture
GetTextureDescriptor
IsTextureLayerOpaque
```

### texture resource

各cache entryへ次を持たせる。

```text
VkImage
VkDeviceMemory
VkImageView
width
height
layer count
format
current layout
descriptor info
opaque mask
generation
last used frame
```

### dirty tracking

```text
VRAMDirty_Texture
VRAMDirty_TexPal
VRAMMap_Texture
VRAMMap_TexPal
```

を使い、変更されたtexture/paletteだけ再decodeする。

VRAM mapping generationをcache keyへ含める。

### upload path

1. decode結果をstaging ringへ書く。
2. non-coherent rangeをflushする。
3. imageをtransfer dstへ遷移する。
4. 必要layerへcopyする。
5. shader read layoutへ遷移する。
6. descriptor cacheへ新しいview/layoutを通知する。

### descriptor mode

runtime capabilityから次を選ぶ。

```text
NonUniform
CompatDynamicUniform
BaseSingleDescriptor
```

3 modeで同じtexture decode結果を使う。

descriptor indexing非対応でもVulkan GPU rasterを継続する。

### sampler cache

S/Tの次の組をkeyにする。

```text
clamp
repeat
mirror
nearest/linear
```

同じkeyのsamplerを共有する。

### opaque情報

texture decode時にlayer単位でalphaが常にopaqueか記録する。

NeedOpaque passの省略判断へ利用する。

### fallback texture

renderer初期化時に1×1白textureを作る。

invalid texture entryはfallback descriptorを使う。

polygonをsceneから削除しない。

### eviction

in-flight frameが参照中のtexture resourceを破棄しない。

retire queueへ入れ、completion value到達後に破棄する。

### 変更してはいけない範囲

- 毎draw textureをdecodeしない。
- textureをCPU framebufferとして保持しない。
- descriptor indexing非対応時にSoftware rendererへ切り替えない。
- texture更新のたびに`vkDeviceWaitIdle()`しない。
- cache entryのVkImageを複数ownerで破棄しない。

### フェーズ完了時に残す状態

```text
全polygon textureがVulkan image
dirty regionだけ更新
3 descriptor pathがGPU描画を維持
NeedOpaque用opaque情報を提供
in-flight texture lifetimeが安全
```

---

# 16. フェーズR11: clear planeとclear bitmapを実装する

## Sapphire直接移植規則

clear plane、clear bitmap、関連resource／pipeline／shaderは固定core commitの`GPU3D_Vulkan.*`とshader sourceから対応ブロックを原文移植する。独自clear passを別実装しない。


## 16.1 clear plane

`RenderClearAttr1`と`RenderClearAttr2`から次を生成する。

```text
RGB
alpha
depth
opaque polygon ID
fog flag
stencil
```

render passのclear valueへ反映する。

## 16.2 clear bitmap

`RenderDispCnt`のclear bitmap bitに従う。

```text
VRAM texture region
    ↓
color bitmap
depth/fog bitmap
    ↓
scaled clear pass
```

offsetは`RenderXPos`とclear attributeから参照実装どおりに反映する。

## 16.3 CPU snapshotを作らない

次を行わない。

```text
毎フレーム256×256をstd::vectorへdecode
presenter用snapshotへclear bitmapを複製
QImageへ変換
```

texture cacheまたはdedicated staging resourceからrendererのGPU imageへ直接送る。

---


## R11 AI Sonnetへの具体的な実装指示

### 目的

DS 3Dのclear planeとclear bitmapをGPU render targetへ正しく反映する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_AcceleratedFrontend.h
src/GPU3D_AcceleratedFrontend.cpp
関連clear shader source
```

### clear state

`RenderClearAttr1`と`RenderClearAttr2`から次を展開する。

```text
clear RGB
clear alpha
clear depth
opaque polygon ID
fog flag
stencil initial value
```

`ClearGpuState`のような明示structへまとめる。

### clear plane

1. render pass beginのclear valuesへcolor/depth/stencilを設定する。
2. attribute imageもpolygon ID/fog stateでclearする。
3. scale後の全pixelへ同じDS clear stateを適用する。
4. debug clear色はdeveloper optionが明示された場合だけ上書きする。
5. clear stateをCPU line cacheへ生成しない。

### clear bitmap

1. clear bitmap enable bitを`RenderDispCnt`から取得する。
2. clear bitmap source VRAMとoffsetを参照実装どおり決定する。
3. colorとdepth/fog sourceをdecodeする。
4. dedicated Vulkan textureまたはtexture cache entryへuploadする。
5. full-screen graphics clear passでscaled targetへ描画する。
6. wrap、offset、alpha、depth、poly ID、fog flagをshaderへ渡す。
7. clear bitmap無効時はresource uploadを行わない。
8. source generationが同じ場合は既存GPU imageを再利用する。

### pipeline

clear planeとclear bitmapのpipelineを分離する。

clear bitmap shaderは通常polygon shaderへ混ぜない。

### RenderXPos

clear bitmapのX offsetとcapture/scanline semanticsへ`RenderXPos`を正しく反映する。

### 変更してはいけない範囲

- 256×256 clear bitmapを毎frame`std::vector`へ複製しない。
- Qt presenterでclear bitmapを合成しない。
- clear depthをcolor alphaへ代用しない。
- clear bitmapをSoftwareRenderer3Dから取得しない。
- clear bitmap有効時に通常clear planeだけで済ませない。

### フェーズ完了時に残す状態

```text
clear planeが全attachmentへ反映
clear bitmapがGPU pass
offset、depth、poly ID、fog stateを保持
Software 3D clear結果へ依存しない
```

---

# 17. フェーズR12: opaque polygon pathを完成させる

## Sapphire直接移植規則

opaque pipeline、NeedOpaque、graphics vertex ABI、descriptor path、draw recordingは固定core commitの対応実装を原文移植する。pipeline keyやfragment処理順を計画文から再構成しない。


## 17.1 pipeline variant

最低限次をvariant keyへ含める。

```text
Z-buffer / W-buffer
depth less / depth equal
texture有無
modulate / decal / toon / highlight
fog write
line / triangle
fragment depth path
descriptor indexing path
```

## 17.2 vertex input

参照実装の`GraphicsVertexGpu` ABIを使う。

```text
position
depth
reciprocal W
texture coordinate
vertex color
polygon flags
texture descriptor index
texture size
texture param
polygon attr
```

## 17.3 opaque fragment処理

```text
vertex color補間
texture sampling
texture combiner
alpha test
depth test
depth write
color write
attribute write
polygon ID write
fog flag write
stencil write
```

## 17.4 NeedOpaque

半透明polygon内の完全不透明fragmentを先行opaque passへ送る。

texture arrayのopaque情報を利用し、不要なpassを省く。

---


## R12 AI Sonnetへの具体的な実装指示

### 目的

opaque polygonをVulkan graphics pipelineで直接描画し、color、depth、attribute、stencilをGPU上で生成する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU3D_AcceleratedFrontend.*
graphics raster vertex shader
graphics raster fragment shader variants
graphics no-color fragment shader
```

### pipeline key

次をkeyへ含める。

```text
Z-buffer / W-buffer
depth less / depth equal
depth write
texture enabled
texture combine mode
toon / highlight
fog write
line polygon
fragment depth path
descriptor indexing path
coverage mode
```

pipeline array index計算を一つの関数へ集約する。

### render pass

opaque passでは次をattachmentとして使う。

```text
ColorImage
AttrImage
DepthStencilImage
必要な補助attachment
```

load/store operationをpass目的へ合わせる。

### draw recording

1. accelerated sceneのopaque draw rangeを順番に処理する。
2. pipeline変更時だけbindする。
3. texture descriptor変更時だけbindする。
4. vertex/index bufferを一度bindする。
5. draw rangeごとに`vkCmdDraw`または`vkCmdDrawIndexed`する。
6. polygon ID、flags、texture indexをvertex dataまたはpush constantから渡す。
7. draw順を変更しない。

### fragment処理

参照shaderの順序を保持する。

```text
texture sample
vertex color combine
toon/highlight
alpha evaluation
alpha test
depth calculation
color output
attribute output
polygon ID
fog flag
```

### NeedOpaque

1. translucent polygonの完全不透明fragmentを先行passへ分離する。
2. texture opaque情報からpassの必要性を決める。
3. NeedOpaque drawをsource orderに沿って処理する。
4. alpha fragmentを後続translucent passへ残す。
5.同一polygonを全面二重描画しない。

### UI overlay opaque path

参照実装に専用opaque UI overlay pipelineがある場合は、その条件を明示的にscene flagへし、一般opaque pipelineへ暗黙統合しない。

### 変更してはいけない範囲

- SoftwareRenderer3Dのpixel loopを呼ばない。
- CPU ownership maskを作らない。
- opaque drawをtexture単位でsortしない。
- fragment depthが必要なvariantを固定function depthへ置き換えない。
- NeedOpaqueを全translucent polygonへ常時実行しない。

### フェーズ完了時に残す状態

```text
opaque color/depth/attribute/stencilがGPU生成
DS polygon orderを保持
texture、toon、fog、depth variantがpipelineへ反映
NeedOpaqueが独立して機能
```

---

# 18. フェーズR13: translucent polygon pathを完成させる

## Sapphire直接移植規則

translucent blend、depth/stencil、polygon ID、background alpha zero pathは固定core commitの対応pipeline／shaderを一体で原文移植する。個別の式だけを抜き出して独自pipelineへ移さない。


## 18.1 blend

```text
src = SRC_ALPHA
dst = ONE_MINUS_SRC_ALPHA
color op = ADD
alpha op = MAX
```

## 18.2 depth

polygon attributeに従い次を分ける。

```text
depth less
depth equal
depth write enabled
depth write disabled
Z-buffer
W-buffer
```

## 18.3 translucent polygon ID

同じtranslucent polygon IDによる重複blendをstencilで防ぐ。

異なるIDのpolygonは重ね合わせを許可する。

## 18.4 background alpha zero

背景alpha zero時の専用pipelineを参照実装どおりに分離する。

一般translucent pipelineへ無理に統合しない。

---


## R13 AI Sonnetへの具体的な実装指示

### 目的

translucent polygonのblend、depth、polygon ID重複抑止、background alpha zero pathをGPUで実装する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
graphics raster fragment shader variants
depth/stencil state作成コード
```

### pipeline variant

次を含める。

```text
Z/W buffer
depth less/equal
depth write enabled/disabled
fog write enabled/disabled
alpha blend enabled
background alpha zero
texture mode
toon/highlight
```

### blend state

参照実装のcolor/alpha blend式をそのまま使う。

```text
src color factor = SRC_ALPHA
dst color factor = ONE_MINUS_SRC_ALPHA
color op = ADD
alpha op = MAX
```

必要なalpha factorは参照shader/output formatへ合わせる。

### translucent polygon ID

1. stencil lower bitsへpolygon IDを保持する。
2. 同じtranslucent polygon IDが同じpixelを再度blendしない条件を設定する。
3. 異なるIDの重ね合わせを許可する。
4. shadow用bit 7とpolygon ID bitsを衝突させない。
5. pass境界で必要なbitだけclearする。

### depth

polygon attributeから次を選ぶ。

```text
less
equal tolerance
write/no write
Z/W
```

W-buffer値をZ-buffer式へ誤変換しない。

### background alpha zero

参照実装の専用pipelineを維持する。

背景alpha zero pixelで通常translucentと異なるcolor/attribute処理が必要な場合は、明示variantへする。

### draw order

accelerated sceneのalpha draw range順を保持する。

NeedOpaque完了後にtranslucent fragmentを描画する。

### 変更してはいけない範囲

- CPUでpolygon ID maskを作らない。
- alpha polygonを一括sortしない。
- depth writeを全alpha polygonで固定しない。
- shadow bitとtranslucent IDを同じmaskとしてclearしない。
- background alpha zero専用処理を削除しない。

### フェーズ完了時に残す状態

```text
translucent blendがGPU
同一polygon IDの重複blend抑止
depth modeとwrite modeをpolygon単位で反映
background alpha zero pathが独立
```

---

# 19. フェーズR14: shadow pathを完成させる

## Sapphire直接移植規則

shadow mask、self reject、blend、stencil bit管理は固定core commitの実装とshaderを一体で原文移植する。三段階passをmelonPrimeDS独自方式で再設計しない。


shadowを次の段階へ分ける。

```text
Shadow Mask
Shadow Self Reject
Shadow Blend
```

## 19.1 Shadow Mask

depth fail時にstencil bit 7を設定する。

lower polygon ID bitsを保持する。

## 19.2 Shadow Self Reject

shadow polygon自身のpoly IDと一致するpixelのshadow bitを消す。

## 19.3 Shadow Blend

shadow bitが残るpixelだけblendする。

blend後にlower stencil bitsを参照実装どおり更新する。

## 19.4 stencil clear

必要なpass境界でshadow bitだけをclearするpipelineを使う。

depth/stencil image全体を不要にclearしない。

---


## R14 AI Sonnetへの具体的な実装指示

### 目的

DS shadow polygonをmask、self reject、blendの三段階としてVulkan depth/stencil上へ実装する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
shadow関連graphics shader
stencil pipeline state作成コード
```

### pass構成

```text
1. Shadow Mask
2. Shadow Self Reject / Clear
3. Shadow Blend
```

各passを明示的なpipelineとdraw listへする。

### Shadow Mask

1. shadow mask polygonをscene flagから抽出する。
2. depth fail条件でstencil bit 7を設定する。
3. lower polygon ID bitsを保持する。
4. color writeを無効化する。
5. depth writeを無効化する。
6. W/Z depth modeをpolygonに合わせる。

### Shadow Self Reject

1. shadow polygon自身のIDを取得する。
2. lower bitsが自身のIDに一致するpixelではbit 7をclearする。
3. 他のpolygon上のmask bitは保持する。
4. color/depthを変更しない。

### Shadow Blend

1. bit 7が設定されたpixelだけ通す。
2. shadow polygon color/alphaでblendする。
3. depth条件を参照実装へ合わせる。
4. blend後のstencil更新を参照実装へ合わせる。
5. background alpha zero専用variantを必要に応じて分離する。

### bit管理

```cpp
constexpr u8 kShadowMaskBit = 0x80;
constexpr u8 kTranslucentPolygonIdMask = 0x3F;
```

実際の参照bit幅へ合わせて定数化する。

### draw list

```text
GraphicsShadowMaskDrawIndices
GraphicsShadowDrawIndices
```

をaccelerated scene build結果から作る。

source orderを保持する。

### 変更してはいけない範囲

- shadowを通常translucent polygonとして一回だけ描かない。
- shadow maskをCPU bitmapへ作らない。
- stencil全体をpassごとにclearしない。
- polygon ID lower bitsをbit 7 clear時に破壊しない。
- depth fail semanticsをdepth passへ変更しない。

### フェーズ完了時に残す状態

```text
shadow maskがstencil bit
self shadowが除外
shadow blendが対象pixelだけへ適用
translucent IDとshadow bitが共存
```

---

# 20. フェーズR15: toon、highlight、edge、fog、AAを完成させる

## Sapphire直接移植規則

toon／highlight／edge／fog／AAのfinal pass、table upload、variant選択、関連shaderは固定core commitの対応コードを原文移植する。適用順とattribute解釈を独自に再構成しない。


## 20.1 toon table

32 entryをGPU bufferへuploadする。

opaque、translucent、textured、untexturedで共通利用する。

## 20.2 highlight

highlight modeを専用shader variantへする。

toonと同じbranchへ無理に統合しない。

## 20.3 edge marking

attribute imageのpolygon IDと隣接pixelを比較する。

edge tableを参照してfinal colorへ適用する。

## 20.4 fog

```text
RenderFogColor
RenderFogOffset
RenderFogShift
RenderFogDensityTable
```

をfinal passへ渡す。

fog write flagがあるpixelだけ処理する。

## 20.5 antialiasing

coverage情報をfinal passへ渡す。

edge、fogとの組み合わせをpipeline variantまたはspecializationへする。

---


## R15 AI Sonnetへの具体的な実装指示

### 目的

toon、highlight、edge marking、fog、antialiasingを3D final GPU passへ統合する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
graphics final vertex shader
graphics edge fragment shader
graphics edge-fog fragment shader
graphics fog fragment shader
関連shader ABI header
```

### toon table

1. 32 entryのtoon tableをper-context GPU bufferへuploadする。
2. dirty generationが変化した場合だけ更新する。
3. fragment shaderで5-bit intensityからentryを選ぶ。
4. toonとhighlightをmode bitで分ける。
5. textured/untextured双方で同じtableを使う。

### highlight

highlight color加算とclampを参照shader順序へ合わせる。

toon color置換とhighlight加算を同じ演算として扱わない。

### edge marking

1. AttrImageからcenter polygon IDを読む。
2. 隣接pixelのpolygon ID、depth、coverageを読む。
3. edge tableから色を選ぶ。
4. hidden alpha zero polygonのoverride条件を参照実装へ合わせる。
5. scaleに応じた隣接sample offsetを使う。
6. output colorへedgeを適用する。

### fog

1. fog density tableをGPU bufferまたはpush constantへpackする。
2. fog offset、shift、colorを渡す。
3. attributeのfog flagがあるpixelだけ処理する。
4. edgeとfogが同時有効なvariantを用意する。
5. alpha fogの有無を`RenderDispCnt`から反映する。

### antialiasing

1. coverage値をraster passからfinal passへ保持する。
2. coverageに基づいてedge pixelを背景と合成する。
3. edge marking、fogとの適用順を参照実装へ合わせる。
4. coverage fixの設定とDS antialiasingを混同しない。

### final pass選択

```text
none
edge
fog
edge + fog
AA combinations
```

を明示variantへする。

### 変更してはいけない範囲

- toon tableを毎draw uploadしない。
- edgeをCPU画像処理で追加しない。
- fogをpresenterへ移さない。
- high-resolution edge offsetをnative 1 pixel固定にしない。
- coverage fixを最終AA coverageとして代用しない。

### フェーズ完了時に残す状態

```text
toon/highlightがpolygon shader
edge/fog/AAがGPU final pass
attribute/depth/coverageを直接利用
CPU postprocessなし
```

---

# 21. フェーズR16: display captureを接続する

## Sapphire直接移植規則

capture line export、exact capture history、slot lifecycle、screen-swap hintは固定core commitの`VulkanRenderer3D`実装を原文移植し、melonPrimeDSのcapture hookへ薄いwrapperで接続する。通常表示用の独自readback経路を追加しない。


## 21.1 capture source

3D capture sourceはVulkan 3D targetと同じframe identityを使う。

## 21.2 capture line export

参照shaderを使用する。

```text
scaled 3D target
    ↓
native 256 pixel line
    ↓
DS color format
    ↓
capture line buffer
```

## 21.3 double buffer

capture line bufferは2 slot以上を持つ。

```text
active
pending
ready
```

を分離する。

## 21.4 `GetLine()`

通常2D合成のために毎line readbackしない。

`GetLine()`はcaptureまたはCPU同期が必要な場面だけexact line cacheを返す。

## 21.5 capture VRAM同期

GPU capture結果をVRAMへ反映する必要がある場合だけ、必要範囲をreadbackする。

normal presentationをcapture readbackへ依存させない。

---


## R16 AI Sonnetへの具体的な実装指示

### 目的

display captureがVulkan 3D frameを正しいnative DS lineへ変換し、必要なVRAM同期だけを行うよう接続する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU.h
src/GPU.cpp
src/GPU_Soft.cpp
capture line export shader
```

### capture state

frameごとに次を固定する。

```text
capture enabled
capture control
capture screen swap
3D frame serial
source image dimensions
scale
destination VRAM range
```

live registerを非同期command recording中に再読込しない。

### capture line export

1. 3D ColorImageをsampleする。
2. scaleされたpixelからnative 256×192 lineへ解決する。
3. DS 6-bitまたはcapture formatへ変換する。
4. screen swap hintを反映する。
5. capture line bufferへ書く。
6. shader writeからhost readへのbarrierを入れる。
7.必要lineだけexportする。

### buffer slots

最低2 slotを持つ。

```text
ActiveCaptureLineBufferSlot
PendingCaptureLineBufferSlot
ReadyCaptureLineBufferSlot
```

slotごとにcompletion stateを持つ。

### `GetLine()`

通常2D表示では呼ばれない前提にする。

capture処理がready slotを必要としたときだけ、完了済みbufferをline cacheへcopyする。

未完了frameを読むためにdevice全体を待たない。

### fallback

参照実装のexact capture履歴を保持する。

source frameが利用できない場合にSoftware 3Dを起動しない。

直前の有効captureまたは明示clear colorを使う。

### VRAM同期

1. destination bank/blockを特定する。
2. capture対象rangeだけCPUへ同期する。
3. GPU resultをDS VRAM formatへ書く。
4. VRAM dirty/capture flagsを更新する。
5. normal presentation用readbackと共有しない。

### 変更してはいけない範囲

- 毎frame3D target全体をreadbackしない。
- captureのためにSoftwareRenderer3Dを並走させない。
- readyでないslotを上書きしない。
- capture screen swapをpresent時のlive値から推測しない。
- capture line cacheを通常window outputに使わない。

### フェーズ完了時に残す状態

```text
display captureがVulkan 3D source
native line exportがGPU
必要rangeだけCPU/VRAM同期
normal presentationはreadback不要
```

---

# 22. フェーズR17: structured 2D sourceを参照型GPU2D経路へ正規化する

R17では新しいVulkan 2D rendererを作らない。Sapphire参照実装と同じく、既存Software 2D layer evaluationがstructured metadataを生成する形へ整理する。

## 22.1 移行前

```text
SoftRenderer/GPU2D evaluation
    ↓
packet
    ↓
VulkanRenderer3D::SubmitStructured2DLine
    ↓
VulkanRenderer3D内CPU arrays
    ↓
R3a getter bridge
```

## 22.2 移行後

```text
GPU2D::SoftRenderer A/B
    ↓
backend-neutral structured line sink
    ↓
SoftPackedFrameSnapshotSource
    ↓
EmuInstance::VulkanFrontendSession
    ↓
VulkanOutput
```

`VulkanRenderer3D`を2D metadata ownerから外す。

## 22.3 source storage

参照実装の`GPU2D::SoftRenderer`にあるstructured plane生成規則を移植する。

```text
plane0
plane1
control
native final fallback
line metadata
line state
capture plane/valid state
```

Engine A/B単位で保持し、top/bottom変換はsnapshot latch時の`ScreenSwap`で行う。

## 22.4 Control ABI

R3aで確定した共通ABIまたは変換関数を唯一の経路として使う。別のControl packerを追加しない。

## 22.5 R3a temporary bridge削除

移行完了後に`VulkanRenderer3D`から次を削除する。

```text
Structured2D* arrays
Structured2DLineReceived
Structured2DFrameValid
SubmitStructured2DLine
Begin/EndStructured2DFrame
CPU getter
```

## 22.6 CPU final framebuffer

Vulkan presentation時はregular displayのCPU final BGRA生成を不要にできるよう、2D evaluationとfinal framebuffer writeを分離する。

Software/OpenGL/Metalの既存経路は保持する。

## R17 AI Sonnetへの具体的な実装指示

### 最初に読むファイル

```text
src/GPU2D_Soft.h
src/GPU2D_Soft.cpp
src/GPU_Soft.h
src/GPU_Soft.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
参照coreのGPU2D_Soft.*
```

### 実装内容

1. Sapphireのstructured plane生成箇所をGPU2D SoftRendererへ移植する。
2. backend-neutral line sinkとframe snapshot sourceを追加する。
3. 384 engine-line completeをsource側で管理する。
4. ScreenSwapはlatch時だけ適用する。
5. Control ABIはR3aの定義を再利用する。
6. frontend sessionを新sourceへ切り替える。
7. call siteがゼロになった後、VulkanRenderer3D内structured arrays/getterを削除する。
8. regular Vulkan presentationでCPU final writeを省ける分岐を追加する。

### 禁止事項

- `GPU2D_Vulkan`という第二2D rendererを作らない。
- 2D blend/window規則をVulkan専用に再実装しない。
- engine配列をtop/bottomとして保存しない。
- incomplete frameをpublishしない。
- R3a bridge削除前にconsumerを切り替え忘れない。

### 完了状態

```text
structured metadata ownerがGPU2D SoftRenderer側
VulkanRenderer3Dは2D dataを所有しない
frontendがimmutable snapshotをconsume
Vulkan presentationでCPU final framebufferは正解源でない
```

---

# 23. フェーズR18: `VulkanOutput`へfinal compositionを集約する

## Sapphire直接移植規則

`melonDS-android @ 0.7.0.rc4`の`VulkanOutput.h/.cpp`、`VulkanCompositorShader.comp`、`VulkanAccumulate3dShader.comp`を一つの同期単位とする。snapshot、capture/class4、temporal history、descriptor、push constant、dispatch順を原文のまま保持する。

許可する差分はinclude path、Volk、`Vulkan3DFrameView`入力、確認済みStructured Control ABI変換、desktop `Frame` metadataだけである。VulkanOutput本体をQt向けに書き直さない。


`VulkanOutput`はQt frontend sessionが所有し、次を唯一の入力とする。

```text
Vulkan3DFrameView
SoftPackedFrameSnapshot
capture/history state
front buffer
screen swap latch
scale/filtering
generation/frame serial
```

## 23.1 descriptor ABI

参照shaderのbindingを厳密に維持する。

```text
0 OutputImage
1 Current3DImage
2 TopPackedBuffer
3 BottomPackedBuffer
4 PreviousTop3DImage
5 Capture3DBuffer
6 PreviousBottom3DImage
```

## 23.2 packed input

Top/Bottom bufferはsnapshot latch後の物理screen順である。

```text
packedTopPlane0
packedTopPlane1
packedTopControl
packedBottomPlane0
packedBottomPlane1
packedBottomControl
line metadata/state
```

engine順のsourceを直接bindingしない。

ControlはR3aで確定したABIを使う。

## 23.3 command flow

```text
host snapshot write/flush
3D completion wait
buffer/image barrier
output image GENERAL
pipeline/descriptor bind
push constants
dispatch
output image shader-read/presenter layout
completion signal
FrameQueue publish
```

## 23.4 temporal history

Top/Bottomを独立して保持し、generation、frame serial、screen swapと関連付ける。history参照中frameはFrameQueueへ返さない。

## R18 AI Sonnetへの具体的な実装指示

1. ownerを`MelonPrimeVulkanFrontendSession`へ固定する。
2. `VulkanRenderer3D&`または`Vulkan3DFrameView`を直接受け取る。
3. `SoftPackedFrameSnapshot`のtop/bottom完成済みbufferをuploadする。
4. descriptor binding、push constant、strideを参照実装へ揃える。
5. dummy resourceが必要なbindingへnull descriptorを渡さない。
6. 3D completion semaphore/valueをcomposition submitへwaitする。
7. output completionをFrameQueueへ渡す。

禁止:

- new outer VulkanRenderer経由へAPIを変更しない。
- 3D imageをCPUへreadbackしない。
- engine A/Bをtop/bottomとして直接bindingしない。
- in-core compositorを呼ばない。

完了状態:

```text
final two-screen imageがVulkanOutputでGPU生成
ownerはfrontend session
Control/packed ABIが参照shaderと一致
3D→composition依存がGPU同期
```

---

# 24. フェーズR19: `FrameQueue`をfrontendの正式frame ownerにする

## Sapphire直接移植規則

`FrameQueue.h/.cpp`のqueue policy、9-frame構成、deadline、drop、previous reuse、statistics、state transitionは固定frontend tagから原文移植する。Android EGL/OpenGL resource fieldだけをVulkan image/fence/timeline/generation metadataへ置換する。

queue algorithmをdesktop向けに簡略化しない。


`FrameQueue`は`MelonPrimeVulkanFrontendSession`が所有する。core `Renderer`へ埋め込まない。

## 24.1 lifecycle

```text
getRenderFrame
    ↓
Rendering
    ↓
composeAndSubmitFrame
    ↓
pushRenderedFrame
    ↓
Ready
    ↓
getPresentFrame
    ↓
AcquiredForPresentation
    ↓
presentFrame
    ↓
commitPresentedFrame
    ↓
Free / HistoryReferenced
```

## 24.2 frame identity

各frameへ次を保持する。

```text
frame serial
generation
VkImage/VkImageView/layout
composition completion semaphore/value
present completion
history references
surface generation
state
```

## 24.3 producer/consumer

EmuThreadがproducer、ScreenPanelVulkanがconsumerとなる。GUI threadはNDS live stateを読まず、Ready frameだけを取得する。

## R19 AI Sonnetへの具体的な実装指示

1. queue ownerをfrontend sessionへ置く。
2. state transitionを専用関数へ集約する。
3. producerとconsumerをthread-safeにする。
4. generation不一致frameをpresent対象から外す。
5. history referenceとpresentation referenceを分離する。
6. latest-ready policyで古い未取得frameをdrop可能にする。
7. present中、history中、lease中frameを再利用しない。

禁止:

- core GPU/RendererへFrameQueueを所有させない。
- raw Frame pointerを無期限保持しない。
- presenterがstate fieldを直接変更しない。
- completion前にFreeへ戻さない。

完了状態:

```text
FrameQueueがfrontend frame ownershipを一元管理
EmuThread producerとGUI consumerが分離
generation/history/present参照が安全
```

---

# 25. フェーズR20: desktop surface adapterを実装する

## Sapphire直接移植規則

このフェーズはdesktop-only adapterである。Androidの`ANativeWindow`、`vkCreateAndroidSurfaceKHR`、JNI lifecycleを直接移植しない。一方、surface作成後のformat／present mode／extent／swapchain policyは`VulkanSurfacePresenter`の参照実装を維持する。


R3dのWindows basic pathをplatform-neutral adapterへ一般化する。

## 25.1 owner

`ScreenPanelVulkan`または専用surface hostだけが`VkSurfaceKHR`を所有する。frontend sessionはsurfaceを所有しない。

## 25.2 interface

```cpp
class MelonPrimeVulkanSurfaceHost
{
public:
    bool createForWidget(QWidget& widget, VkInstance instance);
    void destroy(VkInstance instance);
    VkSurfaceKHR surface() const noexcept;
    QSize pixelSize() const;
    u64 generation() const noexcept;
    VulkanWindowSystem windowSystem() const noexcept;
};
```

## 25.3 platform

```text
Windows: HWND + vkCreateWin32SurfaceKHR
X11/XCB: connection/window + vkCreateXcbSurfaceKHR
Wayland: wl_display/wl_surface + vkCreateWaylandSurfaceKHR
macOS: CAMetalLayer + vkCreateMetalSurfaceEXT
```

Qt native interface取得はfrontend adapter内へ限定する。

## R20 AI Sonnetへの具体的な実装指示

1. R3dのWin32 handle処理をsurface hostへ移す。
2. X11/XCB、Wayland、MoltenVK implementationを追加する。
3. native window再生成時にgenerationを更新する。
4. device pixel ratio込みのpixel sizeを返す。
5. `VulkanContext::ResolvePresentQueue(surface)`を呼ぶ。
6. old surfaceをpresenter detach後に破棄する。

禁止:

- renderer coreからQWidgetを読む。
- X11/Wayland handleをcastで推測する。
- old native window generationのsurfaceを再利用する。
- presenterにsurface ownershipを重複させる。

完了状態:

```text
全desktop platformでVkSurfaceKHR生成
surface generationとQt native windowが同期
coreはQt/native handleを知らない
```

---

# 26. フェーズR21: `VulkanSurfacePresenter`をQtへ完全接続する

## Sapphire直接移植規則

`VulkanSurfacePresenter.h/.cpp`とpresenter shaderを同期単位とし、layout、background、descriptor cache、vertex generation、swapchain recovery、pacing、filter chainを原文主体で維持する。

置換するのは`ANativeWindow`／Android surface生成境界、Qt native handle、graphics/present queue family、desktop surface lifetimeだけである。presenter内部をQt painter方式へ書き直さない。


R3dのbasic presenterを、resize、layout、HUD、radar、pauseを含む正式presenterへ完成させる。

## 26.1 `ScreenPanelVulkan`

次を所有する。

```text
MelonPrimeVulkanSurfaceHost
VulkanSurfacePresenter
surface generation
swapchain config
layout/background/HUD state
last presented frame reference
```

`ScreenPanelNative::drawScreen()`を呼ばない。

## 26.2 draw flow

```text
frontend sessionからgetPresentFrame
surface generation確認
layout config更新
VulkanSurfacePresenter::presentFrame
FrameQueue::commitPresentedFrame
```

CPU framebuffer、`QImage`、`QPainter`を使わない。

## 26.3 overlay

HUD、OSD、crosshair、radarはpresenter GPU passへ置く。bottom screenをtopへ表示するradarはfinal two-screen imageのbottom regionをsampleする。

## 26.4 pause

最後にpresentされたframeをFrameQueue reference付きで保持する。pause中にNative presenterへ切り替えない。

## R21 AI Sonnetへの具体的な実装指示

1. stub `initVulkan()`とNative委譲を削除する。
2. surface/presenterを実初期化する。
3. Ready frameだけをpresentする。
4. out-of-date/suboptimal/resizeへswapchain recreationで対応する。
5. device pixel ratioとScreenLayoutをpresent configへ変換する。
6. radar/HUD/OSDをGPU overlayへ移す。
7. pause frame referenceを安全に保持する。

禁止:

- `ScreenPanelNative::drawScreen()`を呼ばない。
- Vulkan outputをQImageへ変換しない。
- GUI threadからGPU register/VRAMを読む。
- surface resizeで3D texture cacheを破棄しない。

完了状態:

```text
Qt windowへswapchain direct present
layout/HUD/radarがGPU
pauseもVulkan frame
Native CPU presenter不使用
```

---

# 27. フェーズR22: outer renderer、3D backend、frontendを一つの切替transactionにする

新しいouter VulkanRendererは作らない。切替結果は3要素で表現する。

```cpp
struct RendererCreationResult
{
    std::unique_ptr<melonDS::Renderer> OuterRenderer;
    std::unique_ptr<melonDS::Renderer3D> Renderer3D;
    PresentationBackend Presentation;
    int RequestedRenderer;
    int NormalizedRenderer;
    int ActualRenderer;
    std::string FailedStage;
    std::string FallbackReason;
};
```

## 27.1 mapping

```text
Software
    OuterRenderer = SoftRenderer
    Renderer3D = SoftwareRenderer3D
    Presentation = NativeQt

OpenGL
    OuterRenderer = GL/既存desktop renderer
    Renderer3D = GLRenderer3D
    Presentation = OpenGL

Metal
    OuterRenderer = Metal/既存desktop renderer
    Renderer3D = MetalRenderer3D
    Presentation = Metal

Vulkan
    OuterRenderer = existing SoftRenderer/2D timing renderer
    Renderer3D = VulkanRenderer3D
    Presentation = Vulkan
```

## 27.2 actual Vulkan条件

```text
Renderer3DReady
Structured2DReady
FinalCompositorReady
FrameQueueReady
SurfaceReady
PresenterReady
```

全てtrueの場合だけactual=Vulkan。

## 27.3 Vulkan Compute migration

`renderer3D_VulkanCompute`はVulkan Graphicsへ正規化する。UI、config load/save、actual表示で同じmigration関数を使う。

## 27.4 EmuThread順序

```text
config snapshot
requested normalize
creation result生成
render lock
frontend producer停止
旧presentation detach
旧3D renderer stop
GPU::SetRenderer(OuterRenderer)
GPU3D::SetCurrentRenderer(Renderer3D)
settings適用
frontend generation更新
presentation backend切替
producer再開
render lock解除
UIへactual通知
```

## R22 AI Sonnetへの具体的な実装指示

1. factory resultをouter rendererとRenderer3Dに分ける。
2. `CreateRenderer3DForSelection()`を正式APIにする。
3. Vulkan選択でSoftRenderer outerを許可するが、SoftwareRenderer3Dを使わない。
4. frontend sessionとpanel切替を同じtransactionに入れる。
5. capability全成立までactualをVulkanにしない。
6. Vulkan Compute設定をGraphicsへmigrationする。
7. failure stageに応じてSoftware一式を再生成する。

禁止:

- outer classだけでactualを決めない。
- `CurrentRenderer`を削除しない。
- VulkanRenderer3DとSoftwareRenderer3Dを併走させない。
- presenterだけ旧backendのまま公開しない。

完了状態:

```text
outer/3D/frontendが同一transaction
VulkanはGPU3D CurrentRenderer経路
actualが完全frontend能力と一致
```

---

# 28. フェーズR23: frame lifecycleをcoreとfrontendで統合する

同じframe serial/generationを次へ伝搬する。

```text
VulkanRenderer3D
structured 2D source
SoftPackedFrameSnapshot
VulkanOutput
FrameQueue
VulkanSurfacePresenter
```

## 28.1 lifecycle

```text
frame begin
    3D BeginCaptureFrame/RenderFrame
    structured source BeginFrame

active lines
    GPU2D A/B metadata generation
    capture必要時だけ3D GetLine

VCount144/frame completion
    VulkanRenderer3D submit/finish
    structured source complete
    snapshot latch
    frontend session submitCompletedFrame
    VulkanOutput composition submit
    FrameQueue Ready publish

GUI present
    FrameQueue getPresentFrame
    presenter submit
    commitPresentedFrame

VBlank end
    temporal history latch
    next frame serial開始
```

## 28.2 thread boundary

snapshot latchとcomposition producer submitはEmuThread側。GUI threadはReady frameだけを扱う。

## R23 AI Sonnetへの具体的な実装指示

1. frame serialをGPU frame eventで一度だけ増加する。
2. structured sourceと3D viewへ同じserial/generationを付ける。
3. 384 line complete後だけsnapshotをlatchする。
4. composition producerをframe completion pointへ接続する。
5. GUI presentをFrameQueue consumerへ限定する。
6. pause/frame step/fast forwardでも同じownership遷移を使う。
7. capture screen swapをframe identityへlatchする。

禁止:

- subsystemごとにserialを増加しない。
- GUI threadからlive structured arraysを読む。
- normal 2D compositionのため毎line`GetLine()`しない。
- incomplete snapshotを前frameと混ぜない。

完了状態:

```text
3D/2D/composition/presentが同一frame identity
producerとconsumer thread境界が明示
通常表示はReady Vulkan frameだけをconsume
```

---

# 29. フェーズR24: 同期とresource lifetimeを整理する

## 29.1 fence

render context再利用に使う。

## 29.2 timeline semaphore

次のproducer valueを持つ。

```text
3D complete
composition complete
present complete
```

## 29.3 barrier

最低限次を明示する。

```text
host write → vertex shader read
host write → compute shader read
color attachment write → sampled image read
storage image write → sampled image read
shader write → indirect command read
transfer write → shader read
composition write → presenter fragment read
```

## 29.4 queue family ownership

graphics queueとpresent queueが異なる場合はownership transferを行う。

同じqueue familyでは`VK_QUEUE_FAMILY_IGNORED`を使う。

## 29.5 destroy順

```text
new frame publish停止
presenter frame取得停止
present中frame完了
FrameQueue clear
VulkanOutput shutdown
VulkanRenderer3D Stop
texture cache破棄
surface presenter shutdown
surface破棄
VulkanContext Release
```

---


## R24 AI Sonnetへの具体的な実装指示

### 目的

3D render、composition、presentの同期とresource破棄順を明示し、race、早期再利用、全体待機を除去する。

### 対象ファイル

```text
src/VulkanContext.h
src/VulkanContext.cpp
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/GPU_Vulkan.h
src/GPU_Vulkan.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.*
src/frontend/qt_sdl/VulkanReference/FrameQueue.*
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*
```

### timeline model

利用可能な場合は次の単調増加valueを使う。

```text
3D complete value
composition complete value
present complete value
```

同じsemaphoreを使う場合もstageごとのvalueをframeへ保存する。

timeline非対応時はbinary semaphore/fence adapterを使用する。

### barrier helper

用途別の明示関数を実装する。

```text
HostWriteToShaderRead
TransferWriteToShaderRead
ColorAttachmentToSampled
StorageWriteToSampled
ShaderWriteToIndirectRead
CompositionWriteToPresenterRead
PresentToRender
RenderToPresent
```

genericな`ALL_COMMANDS + MEMORY_READ/WRITE`だけに統一しない。

### queue submit

1. command recording後に必要wait semaphoreを組み立てる。
2. queue lockを取得する。
3. submitする。
4. queue lockを解放する。
5. CPU側はfenceを必要なresource再利用時だけ待つ。

### queue family transfer

graphics familyとpresent familyが異なる場合はoutput imageへrelease/acquire barrierを入れる。

同一familyではownership transferを行わない。

### resource retire queue

次のresourceを即時破棄せずretireする。

```text
old render targets
old swapchain
old frame images
old texture images
old descriptor pools
old pipelines
```

各entryへcompletion semaphore/valueまたはfenceを関連付ける。

### device loss

device loss stateをcontextへ通知する。

新規submitを停止し、frame publishを無効化し、resource ownerを上位から順にshutdownする。

CPU presenterへ自動切替して壊れたVulkan frameを表示しない。

### destroy順

実装を次へ揃える。

```text
output acquisition停止
presenter detach
pending present completion
FrameQueue release
VulkanOutput shutdown
VulkanRenderer3D shutdown
texture cache shutdown
surface destroy
VulkanContext Release
```

### 変更してはいけない範囲

- normal frameで`vkDeviceWaitIdle()`しない。
- barrierのoldLayoutを常にUNDEFINEDにしない。
- present queue family transferを無視しない。
- old swapchainを即時破棄しない。
- device loss後に既存queueへsubmitしない。

### フェーズ完了時に残す状態

```text
3D→composition→presentの依存がsemaphoreで明示
必要resourceだけfence待機
queue family ownershipを処理
retire queueで安全に破棄
```

---

# 30. フェーズR25: pipeline cacheを整理する

## 30.1 key

```text
vendor ID
device ID
driver version
pipeline cache UUID
reference port version
shader manifest hash
descriptor indexing mode
```

## 30.2 owner

3D pipeline cacheは`VulkanRenderer3D`が所有する。

compositor/presenter pipeline cacheはfrontend側が所有する。

同じcache fileへ異なるpipeline ABIを書かない。

## 30.3 lazy creation

基本pipelineを初期化時に作る。

大量variantは最初の利用時に作る。

---


## R25 AI Sonnetへの具体的な実装指示

### 目的

3D、compositor、presenterのpipeline cacheをABIごとに分離し、deviceとshader manifestへ対応した永続cacheとして整理する。

### 対象ファイル

```text
src/GPU3D_Vulkan.h
src/GPU3D_Vulkan.cpp
src/frontend/qt_sdl/VulkanReference/VulkanOutput.*
src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*
src/VulkanReferencePortVersion.h
shader manifest/ABI header
Platform file API
```

### cache key

最低限次を含める。

```text
vendor ID
device ID
driver version
pipeline cache UUID
Sapphire core commit
Sapphire frontend tag
shader manifest hash
descriptor indexing mode
texture sampling path
pipeline ABI version
```

### cache分離

```text
3D graphics/compute cache
VulkanOutput compositor cache
VulkanSurfacePresenter cache
```

を別fileへする。

異なるpipeline layoutを同じfileへ保存しない。

### load

1. file headerを読む。
2. magic、version、keyを確認する。
3. key不一致ならdataを利用しない。
4. `VkPipelineCacheCreateInfo`へinitial dataを渡す。
5. invalid dataで`vkCreatePipelineCache`が失敗した場合は空cacheを作る。

### save

1. owner shutdown前にcache data sizeを取得する。
2. dataを取得する。
3. temporary fileへ書く。
4. atomic renameする。
5. dataが空の場合は既存有効cacheを上書きしない。
6. 複数instanceが同じfileへ同時書込しないようlockする。

### lazy pipeline creation

初期frameに必須なpipelineだけ先に作る。

variant pipelineは最初の利用時に作り、同じkeyで再利用する。

pipeline creation中にin-flight frame resourceを変更しない。

### cache invalidation

次でkeyを変更する。

```text
shader source変更
descriptor ABI変更
push constant変更
pipeline state key変更
reference version変更
```

### 変更してはいけない範囲

- deviceのpipeline cache UUIDを無視しない。
- 3Dとpresenterのcache dataを結合しない。
- cache load失敗をrenderer全体の失敗にしない。
- 毎framecacheを保存しない。
- shader ABI変更後も同じversion keyを使わない。

### フェーズ完了時に残す状態

```text
owner別pipeline cache
device/driver/shader ABI一致時だけ再利用
variantはlazy creation
cache破損がrender resourceを破壊しない
```

---

# 31. フェーズR26: phase、重複compositor、stub、temporary adapterを削除する

## 31.1 維持する正式経路

```text
GPU3D::CurrentRenderer
CreateRenderer3DForSelection
VulkanRenderer3D
GPU2D Soft structured source
MelonPrimeVulkanFrontendSession
VulkanOutput
FrameQueue
MelonPrimeVulkanSurfaceHost
VulkanSurfacePresenter
ScreenPanelVulkan
```

## 31.2 削除対象

```text
VulkanRendererShellContract
A1～A7 marker/contract
CreateRenderer3DOverrideForSelectionというlegacy名称
GetCurrentRendererOverrideというlegacy alias
SapphireVulkanComposition* in-core structures
packStructured2DFrame/uploadStructured2DFrameToGpu
in-core final compositor pipeline/resource
CPU safety presentation
ScreenPanelNative::drawScreen委譲
常時trueのinitVulkan
phase/bootstrap source
GPU2D_Vulkanのphase contract/CPU fake compositor
hidden backup source tree
```

legacy getter/factoryはcall siteがゼロになってから削除する。

## 31.3 `VulkanRenderer3D` cleanup

R17完了後は2D structured arrays/getterも削除し、3D target/captureだけを残す。

## 31.4 CMake cleanup

正式sourceだけをtargetへ列挙し、backup、phase、重複shader headerを除去する。

## R26 AI Sonnetへの具体的な実装指示

1. repository全体からlegacy symbolを検索する。
2. call siteを正式APIへ移行する。
3. `CurrentRenderer`本体は削除対象から除外する。
4. in-core compositorとfrontend compositorの重複を完全削除する。
5. Native CPU Vulkan presenter stubを削除する。
6. R17移行後のtemporary structured bridgeを削除する。
7. phase/bootstrap/backup treeを削除する。
8. CMakeとincludeを整理する。

禁止:

- `GPU3D::CurrentRenderer`を削除しない。
- new outer VulkanRendererを追加しない。
- legacy pathを環境変数で再有効化可能にしない。
- duplicate classをaliasで残さない。

完了状態:

```text
3D ownerはGPU3D::CurrentRendererだけ
final compositorはVulkanOutputだけ
frame ownerはfrontend FrameQueueだけ
presenterはVulkanSurfacePresenterだけ
phase/stub/duplicate/backupが不存在
```

---

# 32. CMake実装

## 32.1 core target

`MELONPRIME_VULKAN_ACTIVE`内へ次を追加する。

```text
GPU_Vulkan.cpp
GPU2D_Vulkan.cpp  // Control ABI adapterが残る場合のみ
GPU3D_AcceleratedFrontend.cpp
GPU3D_Vulkan.cpp
GPU3D_TexcacheVulkan.cpp
VulkanContext.cpp
VulkanDesktopCompat.cpp
generated 3D shader headers
```

## 32.2 frontend target

```text
MelonPrimeVulkanFrontendSession.cpp
MelonPrimeScreenVulkan.cpp
MelonPrimeVulkanSurfaceHost.cpp
VulkanReference/VulkanOutput.cpp
VulkanReference/FrameQueue.cpp
VulkanReference/VulkanSurfacePresenter.cpp
generated compositor/presenter shader headers
```

## 32.3 platform definitions

### Windows

```text
VK_USE_PLATFORM_WIN32_KHR
```

### Linux X11

```text
VK_USE_PLATFORM_XLIB_KHR
```

またはXCB。

### Linux Wayland

```text
VK_USE_PLATFORM_WAYLAND_KHR
```

### macOS

```text
VK_USE_PLATFORM_METAL_EXT
```

## 32.4 shader generation dependency

coreとfrontendが同じgenerated shader targetへ依存する。

生成前headerをsource treeへ手動コピーしない。

## 32.5 build gate

```text
MELONPRIME_ENABLE_VULKAN
MELONPRIME_FORCE_DISABLE_VULKAN
MELONPRIME_ENABLE_VULKAN_MOLTENVK
```

の3段階を維持する。

force disable時はsource、definition、dependency、platform linkを全て除外する。

---

# 33. ファイル別変更一覧

| ファイル | 実装内容 |
|---|---|
| `src/GPU.h` | outer rendererと3D backendの切替hook、frame serial |
| `src/GPU.cpp` | `SetRenderer`と`SetRenderer3D` transaction整理 |
| `src/GPU3D.h` | `CurrentRenderer`正式所有、canonical getter/setter |
| `src/GPU3D.cpp` | 3D renderer lifecycle |
| `src/GPU_Soft.h/.cpp` | 2D evaluationとstructured snapshot hook、Vulkan時CPU final write分離 |
| `src/GPU2D_Soft.h/.cpp` | 参照型structured plane/control生成 |
| `src/GPU_Vulkan.h/.cpp` | VulkanRenderer3D factory、runtime capabilities |
| `src/GPU2D_Vulkan.*` | phase contract削除、必要ならControl ABI adapterのみ |
| `src/GPU3D_Vulkan.h/.cpp` | GPU 3D raster、frame view、in-core compositor削除 |
| `src/GPU3D_TexcacheVulkan.*` | GPU texture cache |
| `src/VulkanContext.*` | 既存実働contextへplatform extension/present queue追加 |
| `src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.*` | VulkanOutput/FrameQueue owner、producer submit |
| `src/frontend/qt_sdl/MelonPrimeVulkanSurfaceHost.*` | Win32/XCB/Wayland/Metal surface |
| `src/frontend/qt_sdl/VulkanReference/VulkanOutput.*` | final GPU composition |
| `src/frontend/qt_sdl/VulkanReference/FrameQueue.*` | frontend frame ownership |
| `src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.*` | swapchain presentation |
| `src/frontend/qt_sdl/MelonPrimeScreenVulkan.*` | Qt consumer、layout、HUD、radar |
| `src/frontend/qt_sdl/MelonPrimeRendererFactory.*` | outer rendererとRenderer3Dを別生成 |
| `src/frontend/qt_sdl/EmuThread.cpp` | frame submit、backend transaction |
| `src/frontend/qt_sdl/EmuInstance.*` | frontend session owner |
| `src/CMakeLists.txt` | core Vulkan source、shader generation |
| `src/frontend/qt_sdl/CMakeLists.txt` | frontend session、presenter、surface adapter |


# 33A. Sapphire直接移植マトリクス

| 移植先 | Sapphire参照元 | 方式 | 許可する主な差分 |
|---|---|---|---|
| `src/VulkanPerfStats.h` | core `src/VulkanPerfStats.h` | Verbatim | なし。固定参照と同一blobを維持 |
| `VulkanReference/VulkanFilterMode.h` | frontend `renderer/VulkanFilterMode.h` | Verbatim | なし。固定参照と同一blobを維持 |
| core Vulkan shader source | core同名shader | Verbatim | なし。generated headerはローカル再生成 |
| compositor／presenter shader source | frontend同名shader | Verbatim | 共通ABI includeが必要な場合だけ機械的include追加 |
| `GPU3D_AcceleratedFrontend.*` | core同名file | Reference-majority | provenance comment、melonPrime設定入力adapter |
| `GPU3D_TexcacheVulkan.*` | core同名file | Reference-majority | `<volk.h>`、desktop context/queue accessor |
| `GPU3D_Vulkan.*` | core同名file | Reference-majority | Volk、`GPU3D&` base adapter、zero-arg virtual wrapper、`Vulkan3DFrameView`、desktop context getter |
| `VulkanReference/VulkanOutput.*` | frontend `renderer/VulkanOutput.*` | Reference-majority | include path、Volk、frame view、Control ABI、desktop Frame metadata |
| `VulkanReference/FrameQueue.*` | frontend `renderer/FrameQueue.*` | Reference-majority | EGL/OpenGL fieldsをVulkan fence/timeline/generationへ置換 |
| `VulkanReference/VulkanSurfacePresenter.*` | frontend同名file | Reference-majority | ANativeWindow、surface creation、present queue、Qt native handle |
| `VulkanContext.*` | core同名file | Selective port | Android AHB／Android extensionを除外し、platform-neutral capability/lifetimeだけ利用 |
| `GPU2D_Soft.*` structured metadata | core `GPU2D_Soft.*` | Reference-majority | desktop outer renderer hook、snapshot latch |
| `MelonPrimeVulkanFrontendSession.*` | 対応なし | Desktop-only | frontend producer/session ownershipを実装 |
| `MelonPrimeVulkanSurfaceHost.*` | 対応なし | Desktop-only | Win32/XCB/Xlib/Wayland/Metal surfaceを実装 |
| `MelonPrimeScreenVulkan.*` | 対応なし | Desktop-only | Qt lifecycle/layout/HUD/OSD/radarを実装 |
| `EmuThread`／`EmuInstance` integration | 対応なし | Desktop-only | frame submitとbackend transactionを実装 |

## 33A.1 同期単位の規則

- `Verbatim`対象はローカル独自編集を行わず、参照更新時はファイル全体を置換する。
- `Reference-majority`対象は参照ファイルをbaseにし、adapter patchを再適用する。melonPrimeDS版をbaseにして参照差分を手作業で摘み取らない。
- shaderとそのC++ ABIは別々に変更しない。
- frontendの`VulkanOutput`、compositor shader、accumulate shaderは同時に同期する。
- presenter本体とpresenter shaderは同時に同期する。
- `GPU3D_Vulkan.*`、accelerated frontend、texture cache、core shader群は同一core commitから同時に同期する。
- Android-only codeを除外した事実は「未移植」ではなく、desktop adapter境界としてmanifestへ記録する。

---

# 34. 実装順序

```text
R0   runtime能力報告を実体へ一致
R1   0.7.0.rc4固定ソースを再同期
R2   GPU3D::CurrentRenderer所有を正式化
R3a  Control ABI確認、engine→top/bottom snapshot bridge
R3b  frontend session追加
R3c  VulkanOutput + FrameQueue producer接続
R3d  Windows basic Vulkan presenter
R4   既存VulkanContextへdesktop surface要件追加
R5   shader生成を一本化
R6   in-core重複composition削除
R7   render target完成
R8   render context ring完成
R9   accelerated scene正式入力化
R10  texture cache完成
R11  clear plane / clear bitmap
R12  opaque path
R13  translucent path
R14  shadow path
R15  toon / highlight / edge / fog / AA
R16  display capture
R17  structured 2D sourceをGPU2D SoftRendererへ正規化
R18  VulkanOutput final composition完成
R19  FrameQueue ownership完成
R20  cross-platform desktop surface adapter
R21  Qt presenter、layout、HUD、radar完成
R22  outer/3D/frontend切替transaction完成
R23  frame lifecycle統合
R24  synchronizationとlifetime
R25  pipeline cache
R26  phase、重複、stub、temporary bridge削除
```

順序上の制約:

```text
Control ABI確認前にR3a memcpyを実装しない
R3 frontend接続前にR6 CPU getterを削除しない
R17 consumer移行前にVulkanRenderer3D structured arraysを削除しない
R20/R21前に全platformでactual=Vulkanと報告しない
R26でGPU3D::CurrentRendererを削除しない
```

---

# 35. 各段階の実装完了状態

## R0～R3d

```text
GPU3D::CurrentRendererがVulkanRenderer3Dを所有
Control ABIが確定
ScreenSwap対応snapshotが生成
VulkanOutputとFrameQueueが接続
Windows basic swapchain presentが動作経路に入る
```

## R4～R10

```text
既存VulkanContextが全desktop surface要件へ対応
3D polygonがGPU targetへ描画
textureがGPU cacheからsample
内部解像度が3D target寸法へ反映
```

## R11～R16

```text
clear、opaque、translucent、shadow、toon、edge、fog、captureがGPU経路
Software 3Dを正解源にしない
```

## R17～R21

```text
structured metadata ownerがGPU2D SoftRenderer側
VulkanRenderer3Dのtemporary 2D storageを除去
final two-screen imageがVulkanOutputでGPU生成
全desktop platformでswapchain direct present
HUD/radar/pauseもVulkan経路
```

## R22～R26

```text
outer renderer、3D backend、frontendの切替が一つのtransaction
actual backendがruntime能力と一致
in-core重複compositor、phase、stub、temporary aliasが消える
GPU3D::CurrentRendererを使う参照型経路だけが残る
```

---

# 36. 最終コード構造

```text
Software
    SoftRenderer
    └─ GPU3D::CurrentRenderer = SoftwareRenderer3D

OpenGL
    existing desktop Renderer
    └─ GPU3D::CurrentRenderer = GLRenderer3D / ComputeRenderer3D

Metal
    existing desktop Renderer
    └─ GPU3D::CurrentRenderer = MetalRenderer3D

Vulkan
    SoftRenderer / existing desktop 2D timing Renderer
    ├─ GPU2D Soft structured source
    └─ GPU3D::CurrentRenderer = VulkanRenderer3D

Qt Vulkan frontend
    EmuInstance
    └─ MelonPrimeVulkanFrontendSession
       ├─ VulkanOutput
       └─ FrameQueue

    ScreenPanelVulkan
    ├─ MelonPrimeVulkanSurfaceHost
    └─ VulkanSurfacePresenter
```

Vulkan用の新しいouter `VulkanRenderer : Renderer`は存在しない。

---

# 37. 最終通常フレーム

```text
GPU3D polygon state
    ↓
GPU3D_AcceleratedFrontend
    ↓
GPU3D::CurrentRenderer
    ↓
VulkanRenderer3D::RenderFrame
    ↓
Vulkan3DFrameView

GPU2D A/B layer evaluation
    ↓
structured plane/control metadata
    ↓
384 engine-line complete
    ↓
ScreenSwap latch
    ↓
SoftPackedFrameSnapshot(top/bottom)

Vulkan3DFrameView + SoftPackedFrameSnapshot
    ↓
MelonPrimeVulkanFrontendSession
    ↓
VulkanOutput::prepareFrameForPresentation
    ↓
VulkanOutput::composeAndSubmitFrame
    ↓
FrameQueue::pushRenderedFrame
    ↓
ScreenPanelVulkan
    ↓
VulkanSurfacePresenter::presentFrame
    ↓
vkQueuePresentKHR
```

通常表示はSoftware 3D framebuffer、CPU ownership mask、QImage、Native painterをconsumeしない。

---

# 38. 禁止する実装

- `GPU3D::CurrentRenderer`を削除する
- 新しい`VulkanRenderer : Renderer`へ2D/3D/outputを集約する
- Control ABI未確認のまま`Structured2DControl`をpacked controlへコピーする
- engine A/B配列を常にtop/bottomとみなす
- `ScreenSwap`をpresent時のlive stateから読む
- 384 line未完了frameをpublishする
- GUI threadからlive GPU stateやstructured arraysを無ロックで読む
- SoftwareRenderer3DとVulkanRenderer3Dを通常フレームで併走する
- `GetLine()`を通常2D合成のため毎scanline呼ぶ
- in-core compositorとfrontend VulkanOutputを併走する
- Vulkan imageを通常フレームでCPU readbackする
- `ScreenPanelNative::drawScreen()`へVulkan表示を委譲する
- Qt presenterでQImage/QPainterを使う
- renderer/presenterごとに別VkDeviceを作る
- normal frameへ`vkDeviceWaitIdle()`を入れる
- phase markerやcontract boolで完成扱いにする
- hidden backup source treeを作る
- Sapphireに同一責務の完成コードがあるのに計画文から独自再実装する
- Reference-majority対象をmelonPrimeDS既存fileベースで継ぎ足し続け、参照原文とのdiffを失う
- 移植と同時に無関係な命名変更、style変更、独自最適化を混ぜる
- Android専用`ANativeWindow`、JNI、EGL、AHB処理をdesktop coreへ直接持ち込む
- copyright／GPL notice／参照commit記録を削除する

---

# 39. 最終到達点

完成時のVulkan backendは次を満たす。

```text
3D backend ownership
    GPU3D::CurrentRenderer → VulkanRenderer3D

2D metadata
    GPU2D SoftRendererの参照型structured output

final composition
    Qt frontendのVulkanOutput

frame ownership
    frontend sessionのFrameQueue

presentation
    ScreenPanelVulkan + VulkanSurfacePresenter

normal frame
    GPU-resident 3D → GPU composition → swapchain
```

Sapphire 0.7.0.rc4の責務境界を維持しながら、desktop melonPrimeDSの既存Renderer階層を破壊しない。

同時に、Sapphireに存在する完成済みのcore renderer、texture cache、scene frontend、VulkanOutput、FrameQueue、presenter、shaderを再発明しない。固定参照コードを正本として直接同期し、melonPrimeDS独自コードはdesktop統合境界へ限定する。
