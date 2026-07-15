# Vulkan S71 監査・修正指示書
## 2D黒色透過再発／Protected Black契約破壊／Engine Ownership分離／Sapphire準拠

**作成日:** 2026-07-16  
**対象リポジトリ:** `ag-advania/melonPrimeDS`  
**対象ブランチ:** `highres_fonts_v3`  
**監査HEAD:** `79f96d5aa9c70a3d76d51ec24dcf8dab38ab7132`  
**HEADコミット:** `vulkan 2d swap Fix`  
**Sapphire frontend基準:** `SapphireRhodonite/melonDS-android@0.7.0.rc4`  
**Sapphire core基準:** `SapphireRhodonite/melonDS-android-lib@d77944275fa61f9b79cfcead2c3e98993429a023`

---

# 0. 対象範囲

以下は**解決済みとして維持**する。

```text
- 2D上画面／下画面の入れ替わり
- 上下画面の誤合成
- physical top／bottom framebuffer slotの修正
```

現在のHEADで採用された次のmappingは戻さない。

```cpp
view.packed = Framebuffer[frontBuffer][top ? 1u : 0u];
```

今回の対象は、2Dの黒色pixelだけが透明扱いされ、背後の3D、capture履歴、前frameなどが見える問題である。

---

# 1. 結論

Sapphire GPU2D coreは、黒色2Dを保護する`Protected Black` flagを正しく生成している。

```text
Structured control alpha bit 0x20
```

Vulkan compositor shaderも、このflagを認識して3D fallbackや履歴置換から黒色2Dを保護する。

したがって根本原因はSapphire coreやcompositor shaderではなく、desktop側の`SapphireVulkanFrameLatch`にある。

主要不具合は3点。

```text
P0-1:
physical screenのEngine ownershipはhardwareScreenSwapで決まるのに、
FrameLatchはrenderScreenSwapAt3DをscreenSwapLatchedとして保存し、
Engine Aのtop／bottom所有判定へ使っている。

P0-2:
cache eligibilityが「RGBが非黒か」で判定される。
opaque blackやprotected blackは意味のある2D内容なのに、
空／無意味として扱われる。

P0-3:
structured slotへpacked 2Dを昇格する処理が、
plane1に有効2Dがある場合でもpackedPlane0だけをコピーする。
その結果、黒pixelと0x20 protected flagを失う。
```

最新コミットは次の真偽を反転した。

```cpp
const bool engineAOnTop =
    !lastSoftPackedFrameSnapshot.screenSwapLatched;
```

しかし`screenSwapLatched`自体は`published.renderScreenSwapAt3D`由来のままである。polarityだけを直した半修正であり、ownership sourceの不一致は残っている。

---

# 2. Sapphireの黒保護契約

Structured 2D controlのalpha byte:

```text
0x40 = 3D slot
0x80 = above-3D plane
     またはslotなしの場合の2D-only
0x20 = protected opaque black 2D
0x10 = no 3D coverage
0x0F = composition mode
```

Sapphire coreのopaque black判定:

```cpp
bool StructuredVulkan2DIsOpaqueBlack(u32 value)
{
    return value != 0u
        && (value >> 24u) != 0x40u
        && (value & 0x00FFFFFFu) == 0u;
}
```

黒色が3Dより上の2D planeにある場合:

```cpp
structuredAlpha =
    legacyAlpha
    | Slot3D
    | Above3D
    | ProtectedBlack;
```

黒色が2D-onlyの場合:

```cpp
structuredAlpha =
    legacyAlpha
    | TwoDOnly
    | ProtectedBlack;
```

この実装は現在のmelonPrimeDSと固定Sapphire coreで一致する。core側へ新しい黒判定を追加しない。

---

# 3. Compositor shaderの契約

shaderは次を持つ。

```glsl
bool hasStructured2DProtectedBlack(Rgba6 control)
{
    return (control.a & 0x20) != 0;
}
```

structured slotでは、protected blackなら3D fallbackで置換せず、above planeが黒でも有効2Dとして選択する。

特にcompMode 7では、protected flagが無い黒pixelを次のように解釈する可能性がある。

```text
regular capture backdrop
wide black / blank 3D pixel
capture 3Dで解決すべきpixel
temporal 3D historyで補うpixel
```

そのためdesktop adapterで`0x20`が失われると、黒が透明になったように背後の3Dが出る。

全RGB黒をshaderで強制opaqueにする修正は禁止。正当な3D黒、clear color、placeholder、未使用pixel、2D opaque blackを区別できなくなる。

---

# 4. P0-1: hardware screen ownerと3D render ownerの混同

publicationには既に2種類のswapがある。

```cpp
bool hardwareScreenSwap;
bool renderScreenSwapAt3D;
```

physical screenごとのengine番号も公開済み。

```cpp
published.top.engine
published.bottom.engine
```

現在のpublication:

```cpp
published.hardwareScreenSwap = GPU.ScreenSwap;
published.renderScreenSwapAt3D = GPU.GPU3D.RenderScreenSwapAt3D;
published.top.engine = topView.engine;
published.bottom.engine = bottomView.engine;
```

physical top／bottomにどの2D engineが入るかは、`hardwareScreenSwap`または公開済みengine metadataで決まる。

ところがFrameLatchは:

```cpp
const bool screenSwap = published.renderScreenSwapAt3D;
lastSoftPackedFrameSnapshot.screenSwapLatched = screenSwap;
```

とし、その後Engine A cache ownershipを:

```cpp
const bool engineAOnTop =
    !lastSoftPackedFrameSnapshot.screenSwapLatched;
```

で決める。

つまり2D Engine Aのphysical ownerを、3D frameをrenderした時点のswapから推測している。capture、VBlank latch、frame boundary、画面切替の瞬間に一致する保証はない。

## 黒透過への影響

```text
- 黒を含む現在screenをEngine A cacheへ保存しない
- 反対screenのcontrolをEngine A cacheとして保存
- cache repairを誤ったphysical screenへ適用
- current protected-black controlを古いcontrolで置換
- 0x20の無いstructured slotになり、3Dが黒の上へ出る
```

上下入れ替え自体が見た目上解決していても、temporal cache内部のownerだけが逆になることは可能。

---

# 5. 必須修正: Snapshotでownershipを分離

`SoftPackedFrameSnapshot`の単一bool:

```cpp
bool screenSwapLatched;
```

を次へ分離する。

```cpp
bool hardwareScreenSwapLatched = false;
bool renderScreenSwapAt3DLatched = false;

u32 topEngineLatched = UINT32_MAX;
u32 bottomEngineLatched = UINT32_MAX;
```

latch時:

```cpp
snapshot.hardwareScreenSwapLatched =
    published.hardwareScreenSwap;

snapshot.renderScreenSwapAt3DLatched =
    published.renderScreenSwapAt3D;

snapshot.topEngineLatched =
    published.top.engine;

snapshot.bottomEngineLatched =
    published.bottom.engine;
```

Engine Aのphysical ownerは推測せず、公開済みengine metadataを使う。

```cpp
const bool engineAOnTop =
    snapshot.topEngineLatched == 0u
    && snapshot.bottomEngineLatched == 1u;
```

assert:

```cpp
assert(
    (snapshot.topEngineLatched == 0u
        && snapshot.bottomEngineLatched == 1u)
    || (snapshot.topEngineLatched == 1u
        && snapshot.bottomEngineLatched == 0u));
```

## transitionも2種類へ分離

```cpp
const bool physical2DOwnerChanged =
    previous.valid
    && previous.topEngineLatched
        != current.topEngineLatched;

const bool render3DOwnerChanged =
    previous.valid
    && previous.renderScreenSwapAt3DLatched
        != current.renderScreenSwapAt3DLatched;
```

用途:

```text
Engine A cache更新／top-bottom cache選択:
    physical2DOwnerChanged

previous 3D image／capture 3D source選択:
    render3DOwnerChanged
```

同じboolを両方に使わない。

---

# 6. P0-2: RGBが黒なら意味のある内容ではない扱い

現在のhelper:

```cpp
bool packedPixelHasVisibleColor(u32 pixel)
{
    return pixel != 0u
        && pixel != kPacked3dPlaceholder
        && (pixel & 0x00FFFFFFu) != 0u;
}
```

これは「色付きpixel」判定としては正しいが、2D content ownershipやcache eligibilityへ使ってはいけない。

opaque blackは:

```text
pixel != 0
RGB == 0
alpha/source class != 0
```

であり、明確に存在する2D pixel。

## 現在の誤使用

`screenHasMeaningfulContent()`は`packedPixelHasVisibleColor()`だけを数えるため、黒主体または黒のみの2D frameをEngine A cacheへ保存しない。

`screenHasStructured2DOnlyContent()`も:

```cpp
structured2DOnly
&& packedPixelHasVisibleColor(plane0[i])
```

だけを有効とする。controlにprotected black `0x20`があってもRGBが黒なら無効扱い。

## 症状の発生順

```text
frame N:
黒い2D背景／黒いHUD／黒い文字を表示

cache判定:
visible RGBなし → meaningful=false

frame N+1:
capture／alternating ownerによりcurrent 2D sourceが一時欠落

repair:
黒frameのcacheが無い、または古い非黒cacheを使用

compositor:
protected black controlが無い
3D／previous frameが表示
```

---

# 7. 必須修正: 2D payloadとcolored RGBを分離

```cpp
bool packedPixelIsPresent2D(u32 pixel)
{
    return pixel != 0u
        && pixel != kPacked3dPlaceholder
        && ((pixel >> 24u) & 0xC0u) != 0x40u;
}

bool packedPixelHasColoredRGB(u32 pixel)
{
    return packedPixelIsPresent2D(pixel)
        && (pixel & 0x00FFFFFFu) != 0u;
}

bool packedPixelIsProtectedBlack2D(
    u32 pixel,
    u32 control)
{
    return packedPixelIsPresent2D(pixel)
        && (pixel & 0x00FFFFFFu) == 0u
        && packedControlMarksProtectedBlack2D(control);
}
```

## cache eligibility

`screenHasMeaningfulContent`は色ではなくpresent 2D payloadを数える。

```cpp
bool screenHasPresent2DContent(
    const Plane& plane0,
    const Plane& plane1,
    const Plane& control)
{
    for (...)
    {
        const bool p0 =
            packedPixelIsPresent2D(plane0[i]);

        const bool p1 =
            packedPixelIsPresent2D(plane1[i]);

        const bool protectedBlack =
            packedControlMarksProtectedBlack2D(control[i]);

        if (p0 || p1 || protectedBlack)
            ...
    }
}
```

## structured 2D-only判定

```cpp
const bool present2D =
    packedPixelIsPresent2D(plane0[i]);

const bool protectedBlack =
    packedControlMarksProtectedBlack2D(control[i]);

if (structured2DOnly
    && (present2D || protectedBlack))
{
    ...
}
```

`packedPixelHasColoredRGB()`は近傍の色付き3D support、debug visualizationなどに限定し、frame validity、cache ownership、protected black preservationには使わない。

---

# 8. P0-3: mergeStructuredDisplayLineがpackedPlane0固定

現在はpacked側に現在の2Dが存在するかをplane 0またはplane 1で判定する。

```cpp
const bool packedHasCurrent2D =
    plane0 useful
    || plane1 useful;
```

しかしstructured slotへabove planeを作る際は常に:

```cpp
plane1[index] = packedPlane0;
```

protected black判定も`packedPlane0`だけ。

```cpp
const bool protectedBlack =
    packedPixelIsOpaqueBlack(packedPlane0);
```

そのため:

```text
packedPlane0 = zero／placeholder
packedPlane1 = valid black 2D
```

でも:

```text
plane1へzeroをコピー
protectedBlack=false
```

となり、黒2Dとcontrol `0x20`を同時に失う。

---

# 9. 必須修正: actual packed 2D sourceを選択

```cpp
const bool packedPlane0Is2D =
    packedPixelIsPresent2D(packedPlane0);

const bool packedPlane1Is2D =
    packedPixelIsPresent2D(packedPlane1);

const u32 packed2D =
    packedPlane0Is2D
        ? packedPlane0
        : packedPlane1Is2D
            ? packedPlane1
            : 0u;
```

既存controlを優先してflagを維持する。

```cpp
const bool protectedBlack =
    (((packedControlAlpha
        | structuredControlAlpha)
        & 0x20u) != 0u)
    || packedPixelIsOpaqueBlack(packed2D);
```

昇格:

```cpp
if (structuredHas3DSlot
    && !structuredHasAbove
    && packed2D != 0u)
{
    plane1[index] = packed2D;

    control[index] =
        overlayControlRgb
        | ((structuredControlAlpha
            | 0x40u
            | 0x80u
            | (protectedBlack ? 0x20u : 0u))
            << 24u);
}
```

既存のSapphire metadataを最優先し、RGBだけから毎frame flagを再構築しない。

---

# 10. compositor直前のcontract validation

screenごとに次を数える。

```text
present2DPixels
opaqueBlack2DPixels
protectedBlackPixels
blackWithoutProtectionPixels
protectedFlagWithoutBlackPixels
structuredSlotPixels
structuredAbovePixels
structured2DOnlyPixels
```

ログ:

```text
[Vulkan2DBlackContract]
frameId
physicalScreen
topEngine
bottomEngine
hardwareSwap
render3DSwap
opaqueBlack
protectedBlack
blackWithoutProtection
cacheApplied
cacheSourceScreen
physicalOwnerChanged
render3DOwnerChanged
```

structured 2D-onlyまたはstructured above planeに対して:

```cpp
assert(
    blackWithoutProtectionPixels == 0
    || explicitlyAllowedNonStructuredPath);
```

raw packedの全黒pixelへ一律assertしない。

---

# 11. cache copy時のcontrol preservation

`applyCachedEngineASnapshot()`はplaneとcontrolを丸ごと置換するため、ownerを誤ると破壊力が大きい。

追加確認:

```text
- source cacheのengine確認
- target physical screenのengine確認
- source frame serial確認
- protected-black countをcopy前後で比較
```

```cpp
assert(cachedEngine == targetEngine);
```

current frameに必須のprotected-black maskがある場合、そのpixelを古いcacheで消さない。

---

# 12. Sapphireからそのまま使用する範囲

## 変更しない

```text
src/SapphireGPU2DCore/GPU2D_Soft.cpp
src/SapphireGPU2DCore/GPU2D_Soft.h
VulkanCompositorShader.comp
VulkanAccumulate3dShader.comp
```

理由:

```text
- coreのopaque black判定は正しい
- coreは0x20を正しく生成
- shaderは0x20を正しく消費
- pinned upstreamと実装が一致
```

## desktop adapterだけを変更

```text
src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp
src/frontend/qt_sdl/SapphireVulkanFrameLatch.h
src/frontend/qt_sdl/VulkanReference/VulkanOutput.h
```

必要に応じて新規:

```text
MelonPrimeDesktop2DFrameOwnership.h
MelonPrimeDesktop2DBlackContract.h
```

---

# 13. FrameLatchをparity対象へ追加

現在のvendor manifestはshader、VulkanOutput、presenter、GPU2D_Softを追跡するが、`SapphireVulkanFrameLatch.cpp`はSapphire `MelonInstance.cpp`由来というコメントだけで、vendor fileに含まれていない。

対応:

```text
- upstream MelonInstance.cppからlatch dependency regionを抽出
- deterministic generatorでlocal FrameLatchを生成
- desktop ownership extensionは明示marker内へ隔離
- generated bodyをCIでbyte comparison
```

少なくとも次をCIで固定する。

```text
- generic packed black helpers
- protected-black control semantics
- compositor handoff rules
- temporal capture rules
```

---

# 14. 推奨実装構造

```cpp
struct Physical2DOwnership
{
    u32 topEngine = UINT32_MAX;
    u32 bottomEngine = UINT32_MAX;
    bool hardwareSwap = false;
};

struct Render3DOwnership
{
    bool screenSwapAtRender = false;
};
```

さらにpayloadを型で分類する。

```cpp
enum class Packed2DPayloadKind
{
    Empty,
    Placeholder3D,
    LayerSlot3D,
    Colored2D,
    OpaqueBlack2D,
};
```

同じRGB判定を複数箇所で再発明しない。

---

# 15. 推奨commit分割

## S71-1

```text
Split physical 2D ownership from render-time 3D screen ownership
```

## S71-2

```text
Treat opaque black as present 2D payload in desktop frame caching
```

## S71-3

```text
Preserve the actual packed 2D source and protected-black metadata during structured merges
```

## S71-4

```text
Validate protected-black invariants before Vulkan composition
```

## S71-5

```text
Add black-over-3D and ownership-transition golden tests
```

## S71-6

```text
Track Sapphire FrameLatch extraction and desktop adapter parity in CI
```

---

# 16. 必須test matrix

## 基本黒表示

```text
pure black 2D-only screen
black text on colored 2D
black border on colored 2D
black HUD over live 3D
black sprite over live 3D
```

期待:

```text
黒pixelは完全な黒
背後3Dが見えない
protected black countが消えない
```

## ownership差分

意図的に:

```text
hardwareScreenSwap != renderScreenSwapAt3D
```

を作る。

期待:

```text
Engine A cache owner = published.top/bottom.engine
3D history owner = renderScreenSwapAt3D
両者を混同しない
```

## plane source

```text
A: plane0=opaque black 2D, plane1=zero
B: plane0=zero, plane1=opaque black 2D
C: plane0=3D placeholder, plane1=opaque black 2D
D: plane0=colored 2D, plane1=opaque black 2D
```

期待:

```text
actual 2D sourceを選ぶ
0x20を保持
zeroで上書きしない
```

## temporal／capture

```text
regular capture compMode 7
VRAM capture
partial capture
full-screen capture
screen owner alternating
previous 3D history active
capture3D fallback active
```

## 回帰防止

```text
top／bottom screen order
physical slot 1／0 mapping
3Dと2Dの正常合成
CustomHUD
1x／2x／4x
fullscreen transition
```

---

# 17. Golden image判定

参照:

```text
native Software renderer output
またはSapphire Android pinned build output
```

比較対象:

```text
final composited screen
packed plane0
packed plane1
structured control
```

完了条件:

```text
black leakage pixels = 0
protected flag loss pixels = 0
wrong cache owner frames = 0
```

最終画像だけでなくcontrol `0x20`も比較する。

---

# 18. static testの不足

現在のS69 testはphysical framebuffer slotとengine metadataに`GPU.ScreenSwap`が存在することを文字列検査するだけ。

未検査:

```text
- engineAOnTopがどのswap sourceを使うか
- hardware swapとrender swapが異なるframe
- opaque black cache eligibility
- protected-black flagの維持
- plane1だけに存在する2D source
```

S71ではC++ unit testまたは小型reference harnessを追加し、Python source testだけでdoneにしない。

---

# 19. 禁止事項

```text
- 解決済みのtop ? 1 : 0 mappingを戻す
- 全RGB黒をshaderで強制opaqueにする
- near-black閾値を追加する
- ROM固有座標で黒を塗り直す
- 最終画像へ黒maskを後付けする
- 3D fallback／temporal historyを全面無効化する
- protected-black flagをRGBだけから毎frame再発明する
- hardwareScreenSwapとrenderScreenSwapAt3Dを同じboolにする
- packedPlane0固定のままplane1を無視する
- Sapphire GPU2D coreへdesktop cache codeを入れる
- shader hashだけ更新してparity通過扱いにする
```

---

# 20. 完了条件

```text
1. 上下画面の順序は現在の正常状態を維持
2. 2D／3D合成は現在の正常状態を維持
3. opaque black 2Dが3Dを完全に遮蔽
4. hardware ownerとrender 3D ownerを別々に記録
5. cacheはblack-only frameを有効内容として保存
6. structured mergeはplane0／plane1からactual 2D sourceを選択
7. protected black 0x20がcache／merge／temporal経路で消えない
8. blackWithoutProtectionPixels = 0
9. Sapphire core／shaderはpinned upstream parityを維持
10. golden testでblack leakage pixels = 0
```

---

# 21. 最終判断

今回の再発はSapphireの黒処理が不足している問題ではない。

```text
Sapphire core:
    黒をopaque 2Dとして分類
    protected-black 0x20を生成

Sapphire compositor:
    0x20を認識し、3D置換から保護

melonPrime desktop FrameLatch:
    RGB黒をcache上の空に近いものとして扱う
    2D ownerに3D render swapを使用
    merge時にpackedPlane0だけを選ぶ
```

修正箇所はdesktop adapter境界。

最新のphysical screen修正を維持しつつ、次を別々の契約として扱う。

```text
physical 2D ownership
render-time 3D ownership
2D payload presence
colored RGB presence
protected opaque black
```

これにより、上下画面・合成の正常状態を壊さず、黒色透過だけを根本修正できる。

---

# 40. 実装進捗

| Phase | Commit | Status | Notes |
|---|---|---|---|
| S71-1 | Split physical 2D ownership from render-time 3D screen ownership | **done** | `SoftPackedFrameSnapshot` に ownership フィールド追加。`engineAOnTop` は公開 engine metadata から判定。`physical2DOwnerChanged` と `render3DOwnerChanged` を分離。 |
| S71-2 | Treat opaque black as present 2D payload in desktop frame caching | pending | |
| S71-3 | Preserve actual packed 2D source and protected-black metadata during structured merges | pending | |
| S71-4 | Validate protected-black invariants before Vulkan composition | pending | |
| S71-5 | Add black-over-3D and ownership-transition golden tests | pending | |
| S71-6 | Track Sapphire FrameLatch extraction and desktop adapter parity in CI | pending | |

