# melonPrimeDS Vulkan 2D表示不具合 監査結果

**監査日:** 2026-07-17
**対象リポジトリ:** `ag-advania/melonPrimeDS`
**対象ブランチ:** `develop_vulkan`
**比較元:** `SapphireRhodonite/melonDS-android` `0.7.0.rc4`
**監査種別:** 静的コード監査・移植差分監査
**コード変更:** 実施していない

---

## 1. 監査対象の症状

Vulkan選択時、主にゲーム内メニューなどの2D画面で以下が発生する。

1. 上画面と下画面が入れ替わる
2. 下画面が激しく点滅する
3. 一部の黒色が抜け、背後の3D画像または過去フレームが透けて見える
4. 3D画面自体は正常に描画される

この症状構成から、Vulkan 3Dラスタライザー本体よりも、以下の境界が主監査対象となる。

```text
DS 2Dエンジン
  ↓
Structured Vulkan 2D metadata
  ↓
フレーム単位のTop/Bottom所有権ラッチ
  ↓
VulkanOutput
  ↓
Vulkan compositor
  ↓
Qt native Vulkan presenter
```

---

## 2. 比較に使用した固定リビジョン

対象ブランチ内のピン留め資料に従い、比較元を以下に固定した。

| 役割 | リポジトリ | Ref | Commit |
|---|---|---|---|
| Androidフロントエンド | `SapphireRhodonite/melonDS-android` | `0.7.0.rc4` | `2c10e59d7209d354e90d9ef4228330bac3f6e794` |
| Vulkan対応コア | `SapphireRhodonite/melonDS-android-lib` | submodule gitlink | `d77944275fa61f9b79cfcead2c3e98993429a023` |
| 移植先 | `ag-advania/melonPrimeDS` | `develop_vulkan` | 現在のブランチ内容 |

参照:

- [`docs/development/rendering/vulkan-port-source-pins.md`](https://github.com/ag-advania/melonPrimeDS/blob/develop_vulkan/docs/development/rendering/vulkan-port-source-pins.md)
- [`melonDS-android` 0.7.0.rc4](https://github.com/SapphireRhodonite/melonDS-android/tree/2c10e59d7209d354e90d9ef4228330bac3f6e794)
- [`melonDS-android-lib` pinned core](https://github.com/SapphireRhodonite/melonDS-android-lib/tree/d77944275fa61f9b79cfcead2c3e98993429a023)

---

## 3. 結論

### 3.1 総合判定

今回の3症状は、単一のシェーダーバグではなく、Sapphire版が前提としている**フレーム所有権契約とStructured 2D metadata契約が、デスクトップ移植層で崩れていること**で説明できる。

最重要差分は次の3点である。

1. **移植先はfront bufferを常に`0`として扱っている**
2. **移植先は3D描画時にラッチされたScreenSwapではなく、後からライブの`GPU.ScreenSwap`を読み直している**
3. **移植先はStructured 2Dデータをレンダーフレームに固定した不変スナップショットとして取得せず、現在の可変配列を直接コピーしている**

さらに、黒色抜けについては、移植先のStructured 2D生成がSapphire版より大幅に簡略化されており、**「実在する黒い2D画素」と「3Dスロット／空画素」を区別するprotected-black情報が不足する経路**がある。

### 3.2 症状と主要原因

| 症状 | 主要原因 | 確度 |
|---|---|---:|
| 上下画面が入れ替わる | front buffer固定値とScreenSwap世代不一致 | 非常に高い |
| 下画面が激しく点滅する | 可変Structured配列の世代混在、pending frame再利用、ScreenSwap世代不一致 | 高い |
| 黒色が透過して見える | protected-black／source-class metadataの欠落による論理的フォールスルー | 高い |
| 3D画面は正常 | Vulkan 3D本体ではなく2D合成入力契約の問題 | 非常に高い |

---

# 4. 監査所見

## F-01 Critical: front bufferが`0`に固定されている

### 対象

`src/frontend/qt_sdl/Screen.cpp`

`ScreenPanelVulkan::drawScreen()`では次の値が設定されている。

```cpp
snapshot.frontBufferLatched = 0;
```

さらに、`prepareFrameForPresentation()`にも`frontBuffer`として`0`が渡されている。

```cpp
vulkan->output.prepareFrameForPresentation(
    renderFrame,
    nds->GPU,
    0,
    snapshot.screenSwapLatched,
    snapshot,
    *renderer3D)
```

### Sapphire版

Sapphire版は、実際に完成したフロントバッファ番号`frontbuf`をラッチし、同じ値をVulkanOutputへ渡す。

```cpp
latchSoftPackedFrameSnapshot(
    renderFrame,
    frontbuf,
    preparedFrameScreenSwap,
    useStructuredVulkan2D);

vulkanOutput->prepareFrameForPresentation(
    renderFrame,
    nds->GPU,
    frontbuf,
    preparedFrameScreenSwap,
    lastSoftPackedFrameSnapshot,
    renderer3D);
```

参照:

- 移植先: [`Screen.cpp`](https://github.com/ag-advania/melonPrimeDS/blob/develop_vulkan/src/frontend/qt_sdl/Screen.cpp)
- Sapphire: [`MelonInstance.cpp`](https://github.com/SapphireRhodonite/melonDS-android/blob/2c10e59d7209d354e90d9ef4228330bac3f6e794/app/src/main/cpp/MelonInstance.cpp)

### 影響

`frontBufferLatched`はデバッグ用の飾りではない。VulkanOutput内では、次の状態と同じフレーム世代を表す識別情報として扱われる。

- packed Top/Bottom
- ScreenSwap
- renderer 3D snapshot
- capture 3D source
- previous Top/Bottom source
- temporal carry state

常に`0`を渡すと、実際にはbuffer 1が完成しているフレームでもbuffer 0由来として処理される。Top/Bottom ownership、過去フレーム再利用、capture履歴の組み合わせが不正になる。

### 判定

**確認済みの移植差分。最優先原因。**

---

## F-02 Critical: 3D描画時のScreenSwapではなくライブ値を再取得している

### 移植先

`ScreenPanelVulkan::drawScreen()`は、表示処理時点のライブ値を使用している。

```cpp
snapshot.screenSwapLatched = nds->GPU.ScreenSwap;
```

### Sapphire版

Sapphire版は、3D描画対象が確定した時点の値を使用する。

```cpp
const bool preparedFrameScreenSwap =
    nds->GPU.GPU3D.RenderScreenSwapAt3D;
```

その値を次の両方へ同一世代で渡す。

- `latchSoftPackedFrameSnapshot()`
- `prepareFrameForPresentation()`

### なぜライブ値では駄目か

DSの画面所有権は、単なる最終表示レイアウトではない。

```text
GPU engine A/Bの物理LCD割当
3D targetのLCD所有権
display captureの取得元
packed Top/Bottom metadata
previous-frame 3D source
```

これらは同じフレーム境界でラッチされなければならない。

2D側のStructured planesは、すでにレンダリング中の`GPU.ScreenSwap`に基づいて物理Top／Bottomへ格納されている。一方、その後のQt表示処理でライブ`GPU.ScreenSwap`を読み直すと、フレーム境界やVBlank付近で値が変化した場合に次の組み合わせが発生する。

```text
packed 2D: フレームNのScreenSwap
3D source ownership: フレームN+1のScreenSwap
present frame: 別のframe queue slot
```

### 症状との対応

- 上下が入れ替わる
- 片側だけ別世代の3D／2Dを参照する
- 画面遷移時に交互に正誤フレームが現れる
- 特に下画面が点滅する

### 判定

**Sapphireとの明確な契約違反。最優先原因。**

---

## F-03 Critical: Structured 2Dデータがレンダーフレームへラッチされていない

### 移植先の経路

移植先は次の手順でStructured 2Dデータを取得する。

```text
VulkanRenderer
  ↓
GetStructuredVulkanFrame(view)
  ↓
内部配列への生ポインタ
  ↓
ScreenPanelVulkan::drawScreen()でmemcpy
  ↓
その場でSoftPackedFrameSnapshotを構築
```

`SoftRenderer::GetStructuredVulkanFrame()`が返すのは、次の可変内部配列へのポインタである。

- `StructuredScreenPlanes`
- `StructuredScreenLineMeta`
- `StructuredCapture3DSource`
- `StructuredCapture3DSourceLineValid`

このview自体には、次の保証がない。

- どのfront bufferに対応するか
- どのrenderFrame IDに対応するか
- 3D snapshotと同じ世代か
- 全192ラインが同一フレームか
- 次フレームの先頭ラインによって更新されていないか

### Sapphire版の経路

Sapphire版は、`renderFrame`取得後に明示的にスナップショットをラッチする。

```text
renderFrame
  + actual frontbuf
  + RenderScreenSwapAt3D
  + Structured planes
  ↓
lastSoftPackedFrameSnapshot
  ↓
snapshot.frameId == renderFrame.frameIdを確認
  ↓
prepareFrameForPresentation
```

さらに、Sapphire版には次の有効性条件がある。

```cpp
hasLatchedSoftPackedFrame
&& lastSoftPackedFrameSnapshot.valid
&& lastSoftPackedFrameSnapshot.frontBufferLatched >= 0
&& lastSoftPackedFrameSnapshot.frontBufferLatched <= 1
```

不正または未完成なら、現在の中途半端なpacked frameを送らず、前回の完成フレームを保持する。

### 移植先の問題

移植先は`renderFrame->frameId`を現在のsnapshotへ書き込むが、そのStructured配列が本当にその`renderFrame`と同じ世代であることを検証していない。

つまり、`frameId`は取得元を証明するラッチ値ではなく、**コピー先へ後付けした番号**になっている。

### 下画面で目立つ理由

下画面は次の影響を受けやすい。

- 192ラインの後半で完成する
- display captureやVRAM displayの利用率が高い
- GUIメニューのタッチ画面として更新頻度が高い
- Top側3D履歴との非対称なtemporal sourceを使用する
- frame queue再利用のタイミングで後段の配列更新と重なりやすい

このため、全画面の激しい点滅ではなく、下側だけが強く点滅する症状と整合する。

### 判定

**高確度の世代管理不備。**

---

## F-04 High: frame queueのpending frame再利用条件がSapphireより弱い

### 移植先

`ScreenPanelVulkan`のframe queue policyは次の通り。

```cpp
MaxBacklogDepth = 1;
AllowStealPending = true;
AllowPreviousFrameReuse = true;
PreferOldestFrame = false;
```

レンダーフレーム取得時に、Sapphire版にある次の確認がない。

- presenterがそのframeを消費済みか
- VulkanOutputがprevious Top/Bottom sourceとして参照中でないか
- pending temporal sourceを奪ってよいか

### Sapphire版

通常のVulkanリアルタイムpolicyは原則として次の設定を使う。

```cpp
AllowStealPending = false;
```

さらに、render slotを再利用する前に次を確認する。

```cpp
vulkanSurfacePresenter->waitForFrameConsumption(candidateFrame)
vulkanOutput->isFrameReferencedAsPendingPreviousSource(candidateFrame)
```

まだpresenterまたはtemporal historyが使用中なら、そのslotは再利用しない。

### 影響

移植先では、次の状態が成立し得る。

```text
frame Aをpresenterが表示中
frame Aをprevious-bottom-sourceとして参照中
frame queueがframe Aを新しいrender slotとして奪う
frame Aのpacked buffer／snapshot imageが更新される
表示中の下画面が別内容へ変化する
```

Vulkanのコマンド同期が完了していても、**論理的なフレーム所有権**が終了したとは限らない。

### 判定

**下画面点滅を増幅する高確度の副原因。**

---

## F-05 High: Structured 2D metadataの移植がSapphire版より大幅に簡略化されている

### Sapphire版のStructured 2Dモデル

Sapphire版は1画素について、単なる最終色だけでなく次を追跡する。

- original plane 0
- original plane 1
- original plane 2
- legacy composed plane 0
- legacy composed plane 1
- legacy control
- 3D source class
- capture-backed 3D source class
- 3D slot
- 2D above plane
- 2D-only
- protected black
- no 3D coverage
- capture lineage
- VRAM capture line validity

主要フラグ:

```text
0x40: 3D slot
0x80: 2D above 3D / 2D-only
0x20: protected black 2D
0x10: no 3D coverage
```

Sapphire版は`StructuredVulkan2DIsOpaqueBlack()`とsource class判定を使い、「黒だが実在する2D画素」を空画素から区別する。

参照:

- [`GPU2D_Soft.h`](https://github.com/SapphireRhodonite/melonDS-android-lib/blob/d77944275fa61f9b79cfcead2c3e98993429a023/src/GPU2D_Soft.h)
- [`GPU2D_Soft.cpp`](https://github.com/SapphireRhodonite/melonDS-android-lib/blob/d77944275fa61f9b79cfcead2c3e98993429a023/src/GPU2D_Soft.cpp)

### 移植先のモデル

移植先は既存の旧Renderer構造へ適応するため、主に次だけからStructured metadataを再構築する。

- `val1`
- `val2`
- `ColorComposite()`が返す最終色
- composition mode
- EVA
- EVB

Sapphire版が扱う第3plane、source class、capture source class、複数のcapture merge経路が欠けている。

### 黒色抜けが起きる仕組み

黒い画素はRGB値だけ見ると次のすべてが同じである。

```text
実在する黒いBG／OBJ画素
透明色による空画素
3D placeholder
3D slotに覆われる黒
display capture内の黒
過去フレームで補完すべき黒
```

Sapphire版はsource classと`protected black`で区別する。

移植先で`protected black`が付かなかった場合、コンポジターはその黒を「有効な2D前景」ではなく、次のいずれかとして処理し得る。

- 3D placeholder
- 3D layer slot
- 2Dなし
- capture fallback対象
- previous 3D sourceで補完可能な領域

その結果、黒画素の位置に3D画像または前フレームが選択され、「黒が透過した」ように見える。

### 判定

**黒抜けの主原因候補。構造的差分は確認済み。**

---

## F-06 Confirmed: 実際のウィンドウ透過またはswapchain alpha不良ではない

以下は移植先コード上で確認できる。

### Vulkan compositor出力

`MelonPrimeVulkanCompositorShader.comp`の最終出力alphaは常に`1.0`。

```glsl
imageStore(
    outImage,
    ivec2(gid),
    vec4(..., 1.0)
);
```

### presenterの画面頂点

Top／Bottom画面の全頂点alphaは`1.0f`。

```cpp
SurfaceVertex(..., 1.0f)
```

### swapchain

swapchainは次を使用する。

```cpp
createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
```

### presenter blend

blendは有効だが、画面頂点および通常画面fragmentのalphaが1なので、画面描画がQt背景と半透明合成される経路ではない。

### 結論

ユーザーから見える「透過」は、OSの背後のウィンドウが透けているのではなく、**Vulkan 2Dコンポジターがその画素を別の3D／過去フレームソースへ置換している論理的な抜け**である。

### 判定

**実alpha透過説は除外可能。**

---

# 5. Sapphire版と移植先の契約比較

| 項目 | Sapphire 0.7.0.rc4 | melonPrimeDS `develop_vulkan` | 判定 |
|---|---|---|---|
| front buffer | 実際の`frontbuf`をラッチ | `0`固定 | 不一致 |
| ScreenSwap | `RenderScreenSwapAt3D`をラッチ | 表示時の`GPU.ScreenSwap` | 不一致 |
| frame ID | Structured snapshot取得時にrenderFrameへ関連付け | コピー後に現在のrenderFrame IDを代入 | 不十分 |
| snapshot有効性 | frameId、frontbuf範囲、validを検証 | view.Validのみ | 不十分 |
| invalid packed frame | 前の完成フレームを保持 | 現在viewを使用または表示中断 | 不一致 |
| Structured data | renderFrame単位のラッチ | 可変内部配列へのview | 不一致 |
| pending frame steal | 通常false | true | 不一致 |
| presenter消費確認 | あり | 同等の取得前確認なし | 不一致 |
| temporal source参照確認 | あり | 同等の取得前確認なし | 不一致 |
| protected black | source class込みで追跡 | 限定条件のみ | 不完全 |
| capture lineage | 詳細に保持 | 簡略化 | 不完全 |
| VulkanOutput本体 | 固定元実装 | ほぼ直接移植 | おおむね一致 |
| compositor shader | 固定元実装 | ほぼ直接移植 | おおむね一致 |
| 3D Vulkan core | 固定元実装 | 移植済み | 3D正常症状と整合 |

---

# 6. なぜ「VulkanOutputをそのまま移した」のに壊れるのか

`VulkanOutput.cpp`、`VulkanCompositorShader.comp`、関連構造体はSapphire版に近い。

しかし、これらは単独で完結する部品ではない。入力側に次の契約を要求する。

```text
1つのrenderFrame
  = 1つのfront buffer
  = 1つのScreenSwap世代
  = 1つのStructured Top/Bottom snapshot
  = 1つの3D snapshot
  = 1つのcapture source世代
```

移植先では、VulkanOutput自体を移しても、その前段が次の状態になっている。

```text
renderFrame ID: 現在取得したqueue slot
front buffer: 常に0
ScreenSwap: 表示時点のライブ値
Structured planes: SoftRenderer内部の現在値
3D snapshot: renderer側の別ラッチ時点
```

したがって、同じVulkanOutputアルゴリズムでも入力契約が成立しない。

---

# 7. 症状別の因果関係

## 7.1 上下画面の入れ替わり

最も強い原因:

```text
Structured packed画面は描画時のScreenSwapで物理Top/Bottomへ格納済み
+
VulkanOutputには後刻のライブScreenSwapを渡す
+
front bufferは0固定
=
2Dと3DのLCD所有権が一致しない
```

単純にpresenterのTop／Bottom UVを反転する問題ではない。

---

## 7.2 下画面の激しい点滅

複数の問題が重なる。

1. Structured配列がrenderFrameへ固定されていない
2. 下画面ラインが次世代データへ更新される
3. queueがpending frameをsteal可能
4. previous-bottom sourceとして参照中のslotを再利用し得る
5. live ScreenSwapとpacked ScreenSwapが交互に一致／不一致になる
6. display captureまたはVRAM displayの履歴が下画面側で頻繁に切り替わる

このため、一定位置の単純な上下反転ではなく、フレームごとに異なる内容が交互表示される。

---

## 7.3 黒色が透ける

実際のalpha透過ではない。

```text
黒い2D画素
↓
source classまたはprotected-black情報が不足
↓
空画素／3D slot／capture fallbackと誤分類
↓
compositorがlive 3Dまたはprevious 3Dを選択
↓
黒の位置に背景が見える
```

---

## 7.4 3D画面が正常

3D Vulkanコア、pipeline、renderer snapshot自体が致命的に壊れている場合は、次が発生するはずである。

- 3Dポリゴン崩壊
- 深度異常
- テクスチャ異常
- 全画面黒
- device lost
- pipeline生成失敗

今回それらがなく、2Dメニュー時だけ顕著であるため、監査結果は2D metadata／frame ownership境界へ集中する。

---

# 8. 修正時の優先順位

本監査ではコード変更していない。将来修正する場合の順序のみ示す。

## Priority 0: Sapphireと同じフレームラッチ契約に戻す

同一のsnapshotへ以下を固定する。

```text
renderFrame.frameId
actual frontbuf
GPU3D.RenderScreenSwapAt3D
Structured Top/Bottom planes
line metadata
capture 3D source
capture line mask
```

## Priority 1: 可変Structured viewを直接presentしない

SoftRenderer内部配列を、完成フレーム単位のdouble／ring bufferへコピーし、producerが完成通知した世代だけをconsumerが読む。

最低限必要な情報:

```text
generation
frameId
frontBuffer
screenSwapAt3D
valid
top planes[3]
bottom planes[3]
line metadata[2]
capture source
capture mask
```

## Priority 2: frame queue ownershipをSapphireへ合わせる

- 通常再生時は`AllowStealPending=false`
- presenter消費完了前のslotを再利用しない
- previous Top/Bottom source参照中のslotを再利用しない
- temporal history不要時のprevious frame reuseを抑止する

## Priority 3: SapphireのStructured 2D分類を移植する

最低限、次を欠落なく再現する。

- original plane 0／1／2
- source class
- protected black
- no 3D coverage
- capture-backed 3D source class
- capture overlay merge
- VRAM capture validity
- 2D-only／3D-slot／above-3Dの明確な区別

## Priority 4: シェーダー修正は最後

現状の症状に対し、最初からシェーダーで黒を強制したりTop／Bottomを反転したりすると、入力契約の不整合を隠すだけになる。

---

# 9. 実施してはいけない暫定対策

## 9.1 presenterでTop／Bottomを単純反転する

上下反転が一場面で直っても、次が壊れる。

- ScreenSwap切替
- Adventureとメニューの遷移
- display capture
- Top-only／Bottom-only
- hybrid layout
- touch座標
- previous 3D source ownership

## 9.2 黒色をRGBだけで強制的に不透明化する

黒い3D placeholderと実在する黒い2D画素を区別できないため、3D合成を壊す。

## 9.3 presenterのblendを無効化するだけ

通常画面alphaはすでに1であり、症状の本体はlogical source selectionである。効果がないか、HUD／OSD overlayだけが壊れる。

## 9.4 previous frame reuseを全面禁止して完了とする

点滅が減る可能性はあるが、根本のScreenSwap／front buffer／snapshot世代不一致は残る。

## 9.5 ライブ`GPU.ScreenSwap`を継続利用する

Sapphireの`RenderScreenSwapAt3D`と同じフレームラッチ値でなければならない。

---

# 10. 推奨ログ監査項目

実装修正前後で、1フレームにつき次を同時記録する。

```text
renderFrame.frameId
snapshot.frameId
actual frontbuf
snapshot.frontBufferLatched
GPU.ScreenSwap
GPU3D.RenderScreenSwapAt3D
snapshot.screenSwapLatched
renderer3dSnapshotScreenSwap
captureSourceScreenSwap
presentFrame.frameId
topPackedHash
bottomPackedHash
topLineMetaHash
bottomLineMetaHash
previousTopSourceFrameId
previousBottomSourceFrameId
```

異常条件:

```text
renderFrame.frameId != snapshot.frameId
frontbuf != snapshot.frontBufferLatched
RenderScreenSwapAt3D != snapshot.screenSwapLatched
presentFrameのslotがprevious sourceとして参照中
同一frameIdなのにpacked hashが変化
192ライン完成前にvalidになる
```

---

# 11. 推奨再現テスト

## A. 純2Dメニュー

- ゲーム起動直後
- タイトル画面
- オプション画面
- タッチ操作メニュー
- 黒背景＋白文字
- 黒いパネル／黒いOBJがある画面

確認:

- Top／Bottomの物理位置
- 黒画素の保持
- 下画面の連続フレームhash
- 30秒以上の点滅有無

## B. 2Dから3Dへの遷移

- メニューからゲーム開始
- ゲームからポーズメニュー
- メニュー解除
- ScreenSwapが変化する演出
- フェードイン／フェードアウト

確認:

- ScreenSwap切替フレームで上下が一度も逆転しない
- previous sourceが誤画面へ残らない

## C. display capture

- VRAM display
- regular display capture
- source Aのみ
- source Bのみ
- A/B blend
- direct 3D capture
- 2D overlay付きcapture
- 黒いoverlay付きcapture

## D. レンダラー回帰

- Software
- OpenGL
- Vulkan

Software／OpenGLでは同一ROM・同一操作・同一レイアウトで既存出力が変化しないこと。

---

# 12. 受け入れ条件

修正後は最低限、次を満たす必要がある。

- [ ] 2DメニューでTop／Bottomが正しい
- [ ] 下画面にフレーム交互点滅がない
- [ ] 黒いBG／OBJ／UIパネルが背景へ抜けない
- [ ] ScreenSwap切替中も1フレームの逆転がない
- [ ] packed snapshotとrenderFrameのframeIdが一致する
- [ ] actual front bufferがsnapshotへ保存される
- [ ] `RenderScreenSwapAt3D`がsnapshotへ保存される
- [ ] presenter使用中のframe slotを再利用しない
- [ ] previous Top／Bottom source参照中のframe slotを再利用しない
- [ ] display captureがTop／Bottom双方で正しい
- [ ] 3D表示品質が現状から劣化しない
- [ ] Software rendererに差分がない
- [ ] OpenGL rendererに差分がない
- [ ] Vulkan無効ビルドが成功する
- [ ] 共有melonDSコードへの変更はMelonPrime＋Vulkanガード内に限定される

---

# 13. 将来修正時のガード要件

共有melonDSコードを変更する場合は、原則として次の二重ガードを使用する。

```cpp
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
// MelonPrime Vulkan専用処理
#endif
```

Vulkan専用フロントエンドファイルについても、ファイル全体または実装全体を同じ条件で隔離する。

対象外のSoftware／OpenGL経路へ、Vulkanのsnapshot、queue policy、metadata形式を無条件で混入させない。

---

# 14. 確度評価

| Finding | 内容 | 確度 |
|---|---|---:|
| F-01 | front bufferの0固定 | 確認済み |
| F-02 | live ScreenSwap使用とSapphire契約差分 | 確認済み |
| F-03 | renderFrame単位のStructured snapshot不在 | 確認済み |
| F-04 | pending frame ownership条件の差 | 確認済み |
| F-05 | Structured 2D metadata簡略化 | 確認済み |
| F-05 → 黒抜けの直接因果 | protected-black不足によるsource fall-through | 高確度 |
| F-06 | 実際のswapchain／window透過ではない | 確認済み |
| Vulkan 3Dコアが主原因ではない | 症状とコード境界からの判定 | 非常に高い |

---

# 15. 監査上の制限

本書はGitHub上の固定コミットと`develop_vulkan`に対する静的監査結果である。

実施していないもの:

- ROM実行
- 実機DSとのフレーム比較
- Vulkan validation layerログ採取
- RenderDoc capture
- GPU別再現
- Windows／Linux別再現
- 動画フレーム解析
- コード修正
- commit／push／PR作成

ただし、最重要の契約差分である次の3点は、実行環境に依存しないソース上の事実である。

```text
frontBufferLatched = 0
screenSwapLatched = nds->GPU.ScreenSwap
renderFrameへ固定されたStructured snapshotがない
```

---

# 16. 最終判定

今回の不具合は、Sapphire版Vulkan出力コードそのものよりも、melonPrimeDSへ接続した際の**フレームラッチ層とStructured 2D移植層**にある。

優先順位は次の通り。

```text
1. actual frontbufとRenderScreenSwapAt3Dの同一フレームラッチ
2. Structured 2DをrenderFrameへ固定
3. queue slotの所有権をSapphireと同等にする
4. protected-blackを含むStructured metadataを完全化
5. 必要な場合のみシェーダーを調整
```

単純な上下反転、alpha強制、黒色の特例処理では根治しない。

**監査結論: フレーム所有権契約違反が上下入替と点滅の主因であり、Structured 2D metadataの不完全移植が黒抜けの主因である。**

---

# 17. 修正実装追補（2026-07-17）

本監査後、指摘した境界をSapphireの固定ソースと再比較し、次の修正を実装した。

| Finding | 修正内容 | 実装状態 |
|---|---|---|
| F-01 | `SoftRenderer::SwapBuffers()`で実際の`BackBuffer`を完成フレームの`FrontBuffer`として保存 | 完了 |
| F-02 | VCount 215の3D開始境界で`RenderScreenSwapAt3D`をラッチし、2D snapshotと3D rendererの双方で使用 | 完了 |
| F-03 | 完成192ラインをdouble bufferへ発行し、generation付きデータをproducer mutex下で所有コピー | 完了 |
| F-04 | pending stealを無効化し、presenter fenceとVulkanOutput temporal参照の双方を確認してからslot再利用 | 完了 |
| F-05 | original plane 0/1/2、source class、protected black、no-coverage、capture overlay lineageを移植 | 完了 |
| F-06 | swapchain opaque、compositor alpha 1.0の既存契約を維持 | 維持 |

追加したフレーム所有権規則:

1. `sourceGeneration`が異なる完成世代だけを新規合成する。
2. `lastQueuedStructuredGeneration`と`lastPresentedStructuredGeneration`を分離する。
3. Present失敗時は同じsnapshotを再生成せず、pending frame自身のpacked buffer／3D snapshot／descriptor入力で再送する。
4. 再送フレームと最新フレームの入力を混ぜない。
5. ScreenSwapが毎フレーム反転する場合、capture補完履歴は同じScreenSwap phaseだけを参照する。
6. 実在する黒い2D画素はsource classと`0x20 protected-black`で保持する。
7. `0x10`はSapphireどおり「3D coverageなし」に限定し、2D above planeの有無には使用しない。

Display Captureでは、source Bに含まれる2D overlayが実際のA/B capture結果と一致する画素だけを再統合し、黒overlayのprotected属性も継承する。これによりcapture-backed 3D slotと実在2D黒画素の区別を維持する。

最終ビルド／静的検査は、実装を最後まで完了した後に一括実施する。ROMによる30秒点滅・ScreenSwap・Display Captureの目視受け入れは、生成物を用いた実機確認項目として残す。
