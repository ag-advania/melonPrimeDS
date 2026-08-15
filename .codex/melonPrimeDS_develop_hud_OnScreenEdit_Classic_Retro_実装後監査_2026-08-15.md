# On-Screen Edit Classic / Retro 実装後監査

実装指示書: `melonPrimeDS_develop_hud_OnScreenEdit_Classic_Retro_スタイル選択_詳細実装指示書_2026-08-15.md`

## 実装結果

- `MelonPrime::OnScreenEditStyle` を追加し、`Classic=0` を既定値にした。未知の保存値は `Classic` に正規化する。
- `Metroid.UI.OnScreenEditStyle` を Custom HUD プリセットのスキーマとは分離した設定キーとして保存する。
- Custom HUD 設定画面に `Classic` / `Retro` の選択欄、説明文、Apply/OK 復元を追加した。設定変更はプレビューにも反映する。
- On-Screen Edit 開始時にスタイルを HUD 編集状態へ一度だけ取り込み、描画・入力の汎用プロパティパネルを `Retro` のときだけ有効化した。
- `Classic` では既存の Qt ネイティブ汎用プロパティ編集パネルを表示し、`Retro` では DS 面内プロパティパネルを使用する。
- Crosshair はスタイル判定より先に専用 DS 編集経路へ固定し、どちらのスタイルでも汎用プロパティ編集パネルへ流れないようにした。
- 非表示化する Qt パネルのフォーカスを解除し、隠れたパネルが入力を保持しないようにした。
- 表示文3件を登録済み `MenuLangId` 82件すべてに追加した。`Classic` / `Retro` は固有スタイル名として英語表示を維持した。
- Renderer backend、HUD プロパティスキーマ、既存の HUD 配置データ形式は変更していない。

## 検証結果

| 検証 | 結果 |
|---|---|
| `python tools/ci/audits/audit-onscreen-edit-style.py` | PASS |
| `python tools/ci/audits/localization/audit-melonprime-localization.py` | PASS。選択可能言語82、exact 1025、object 30 |
| 追加表示文の明示登録 | PASS。3文とも82/82 |
| `MelonPrimeInputConfig.ui` XML parse | PASS |
| `git diff --check` | PASS。出力はWindows改行変換警告のみ |
| `cmd /c tools\build\windows\build-mingw-existing.bat --jobs 1` | PASS。Vulkan/DX12有効の既存MinGWビルド |
| ビルド内テスト | PASS。Vulkan present timing、Vulkan present pacer fake-dispatch、Intel XeLL state-machine |

`audit-melonprime-all-new-language-coverage.py` は既存の新規言語カタログ未充足行について WARN を出すが、今回追加した3行は全82言語を明示登録済みである。CTest は登録テストなしと報告したが、上記3テストはビルドターゲットの実行ステップで完了している。

## 未検証

指示書の T01-T16 に相当する実アプリ上の Qt 操作、再起動後の実設定ファイル確認、Crosshair の実画面操作、物理GPU/実ROMを用いた表示差分は、この環境では実行していない。コード監査・XML監査・Windowsビルド・ビルド内テストまでを完了し、実機UI確認は未検証として残す。
