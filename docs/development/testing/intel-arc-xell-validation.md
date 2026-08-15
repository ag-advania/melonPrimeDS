# Intel Arc XeLL 実機受け入れチェック

この手順は、Intel Arc 実機がない開発環境で完了した静的監査・Fake API
テストの次段階です。完了するまでは XeLL の実機動作、遅延改善、XeSS
Inspector の妥当性を「確認済み」と表現しません。XeLL の既定値は Off、
ペーシングの既定値は Compatibility のままです。

## テスターが準備するもの

- Intel Arc GPU と、可能ならその GPU に直接接続した可変リフレッシュ対応モニター
- 最新安定版 Intel Graphics Driver と、比較用に1世代前のドライバー
- 開発者機能を有効にした Windows DX12 ビルド一式（`libxell.dll`、
  `LICENSE.txt`、`third-party-programs.txt` を含む）
- Intel XeSS Inspector。可能なら PresentMon または GPUView
- 再現性のある ROM、セーブデータ、同じゲーム内地点

## 1回のテスト手順

1. アプリを起動し、ログ先頭の adapter、vendor、device、driver を保存する。
2. 3D Renderer を DirectX 12 にし、XeLL が選択可能になることを確認する。
3. XeLL Off / Compatibility で同じ地点を60秒実行し、ログと計測値を保存する。
4. XeLL On / Compatibility で同じ操作を繰り返す。
5. 開発者向け4ポリシーを1つずつ選び、各60秒、同一条件で繰り返す。
6. 各設定で VSync Off / On、ウィンドウ / 全画面を試す。
7. 実行中にセーブステート保存・読込、レンダラー Software→DX12→Software、
   高速化・低速化、ROM終了・再起動を各1回行う。
8. Inspector のキャプチャとアプリログを同じ試行名で保存する。

比較ポリシーは次の5通りです。

| ポリシー | ホストリミッター | DXGI待機 | XeLL frame cap |
| --- | --- | --- | --- |
| Compatibility | 使用 | 使用 | なし (`0`) |
| Bypass DXGI wait | 使用 | 迂回 | なし (`0`) |
| Bypass host limiter | 迂回 | 使用 | なし (`0`) |
| XeLL frame cap | 迂回 | 使用 | 使用 |
| Intel recommended | 迂回 | 迂回 | 使用 |

## 合格条件

- `runtimePresent=1 supportedByProbe=1 contextCreated=1 sleepModeApplied=1`
  となり、On では `actualEnabled=1` になる。
- `xellSleep` と Simulation/Input/Render Submit/Present のマーカーが同一の
  単調増加 frame ID で対応し、欠落・逆順・重複がない。
- Compatibility 以外では、ログの `hostLimiterBypass`、
  `frameLatencyWaitBypass`、`minimumIntervalUs` が上表と一致する。
- ROM終了、レンダラー切替、セーブステート、全画面切替、Present失敗からの
  復帰でクラッシュ、ハング、D3D12 device lost、コンテキストリークがない。
- Off と On を最低3試行ずつ比較し、平均値だけでなく P50/P95、フレーム時間、
  dropped/late frame、CPU/GPU使用率を記録する。
- Inspector が有効な XeLL フレーム連鎖を認識する。画面写真だけでなく
  Inspector のエクスポートまたはキャプチャを添付する。

遅延改善の採用判断は、少なくとも2種類の Arc GPUまたは2ドライバー世代で
再現し、映像の乱れやフレームペーシング悪化がないことを条件にします。

## 報告テンプレート

```text
GPU / device ID:
Driver:
Monitor / connection / refresh rate / VRR:
Build commit:
ROM and test scene:
VSync / window mode / policy:
XeLL status log:
Inspector result and attachment:
P50 / P95 latency or frame time:
Dropped/late frames:
Lifecycle tests (state, renderer, fullscreen, ROM restart): PASS/FAIL
Reproduction steps and observations:
```

非Intel環境では XeLL コントロールが無効で、設定ファイルを手動で On にしても
D3D12 自体は起動を継続しなければなりません。この負経路は Fake API テストと
ベンダープローブ監査でも継続して検証します。
