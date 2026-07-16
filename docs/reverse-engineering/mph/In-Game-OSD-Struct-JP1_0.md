# In-Game-OSD-Struct-JP1_0

Excelシート `address アモがない！のOSD` の整理版。
「アモがない！」などのOSDメッセージは、各リージョンのOSD MSG baseから最大20件並ぶ。
JP1.0静的ダンプでは `0x020E5B38` 以降はゼロ初期化領域で、シートの値は実行中にOSD投入関数が書いたruntime観測値。

## Version Address Map

| Version | OSD MSG Base | Font Address | Notes |
| --- | --- | --- | --- |
| JP1.0 | `0x020E5B38` | `0x020CD510` | このメモの基準。 |
| JP1.1 | `0x020E5AF8` | `0x020CD4D0` | JP1.0よりOSD base/fontともに `-0x40`。 |
| KR1.0 | `0x020DCD34` | `0x020C4D40` | 詳細は [`In-Game-OSD-Struct-KR1_0.md`](In-Game-OSD-Struct-KR1_0.md)。JP/KRはfont addressを書かなくても動くという観測あり。 |
| US1.0 | `0x020E3C3C` | `0x020CB6D4` | 詳細は [`In-Game-OSD-Struct-US1_0.md`](In-Game-OSD-Struct-US1_0.md)。 |
| US1.1 | `0x020E44D4` | `0x020CBF5C` | - |
| EU1.0 | `0x020E44F4` | `0x020CBF7C` | 詳細は [`In-Game-OSD-Struct-EU1_0.md`](In-Game-OSD-Struct-EU1_0.md)。 |
| EU1.1 | `0x020E4574` | `0x020CBFFC` | 詳細は [`In-Game-OSD-Struct-EU1_1.md`](In-Game-OSD-Struct-EU1_1.md)。`アモがないフォントアドレスの位置` 側にも同値メモあり。 |

## OSD MSG Layout

JP1.0 entry base = `OSD_MSG_BASE + (slot - 1) * 0x9C`。slotは `1-20` まで。
slot 21以降は存在しないという観測。

| Offset | Size | JP1.0 Slot 1 Address | Observed / Example | Description | Notes |
| --- | --- | --- | --- | --- | --- |
| `0x00` | 4 | `0x020E5B38` | `0x00080000` | X position | 表示位置X。 |
| `0x04` | 4 | `0x020E5B3C` | `0x00078000` | Y position | `0x00000000`で最上部、`0x000B9000`で最下部付近。 |
| `0x08` | 4 | `0x020E5B40` | `0x00008000` | Font size / line height | 描画時のサイズ。stacking時のY移動量にも使われる。 |
| `0x0C` | 4 | `0x020E5B44` | `0x020CD510` | Stored font pointer | 投入関数は保存するが、JP1.0の描画loop `0204D658` では直接読まない。JP/KRでは省略しても動く観測と整合。 |
| `0x10` | 2 | `0x020E5B48` | `0x295F` | Text color | 16-bit BGR555 color code。詳細は下の色メモ。 |
| `0x12` | 2 | `0x020E5B4A` | `0x002C` | Active/display timer | nonzeroなら有効。描画loopが毎回 `-1` し、0で非表示。 |
| `0x14` | 1 | `0x020E5B4C` | `0x1F` | Alpha / opacity | `0xFF`にすると塗りつぶしのようになる観測。 |
| `0x15` | 1 | `0x020E5B4D` | `0x01` | Display/group flags | bit0 = blink。bits1-3 = vertical stacking group。bit4以降も query/clear mask として使える。 |
| `0x16` | 2 | `0x020E5B4E` | `0x0100` | Wrap width / layout width | 投入時に `0204A33C` の折り返し幅として使われ、その値が保存される。 |
| `0x18` | 4 | `0x020E5B50` | `0x00000002` | Text alignment | `0` = 中央から右へ、`1` = 中央を終点に左へ、`2` = 中央揃え。 |
| `0x1C` | `0x80` | `0x020E5B54` | `4F4D4D41` | Text buffer | `4F4D4D41` はlittle-endianで `AMMO`。入力範囲は `+0x1C` から `+0x9B`。 |

## JP1.0 Code Analysis

| Address | Role | What It Confirms |
| --- | --- | --- |
| `0204D658-0204D738` | OSD update/render loop | `0x020E5B38` から `0x9C` strideで20件を走査。`+0x12` timerがnonzeroなら描画し、最後にtimerを1減らす。 |
| `0204D748-0204D8A0` | OSD enqueue / setup function | 文字列をformatして `+0x1C` へコピーし、空きslotまたは残りtimer最小slotへ投入する。 |
| `0204D8B0-0204D8E8` | Has active by flag mask | `entry+0x15 & mask` かつ `+0x12 != 0` のslotがあれば1を返す。 |
| `0204D8F0-0204D924` | Clear by flag mask | `entry+0x15 & mask` のactive slotの `+0x12` を0にする。 |
| `0204D92C-0204D958` | Clear all OSD timers | 全20slotの `+0x12` を0にする。 |
| `02055F34` | Render path caller | HUD/render系の更新中に `0204D658` が呼ばれる。状態によって呼ばれないframeがある。 |
| `020E05B4` | Runtime font pointer holder | JP1.0では `0x020CD510` が入っており、多くのOSD投入呼び出し元がここからfont pointerを渡す。 |

### Enqueue Function Signature

JP1.0の呼び出し元から見た `0204D748` の実用的な引数順。

```text
0204D748(
    r0 = x,
    r1 = y,
    r2 = alignment,
    r3 = wrap_width,
    stack+0x00 = font_size,
    stack+0x04 = color,
    stack+0x08 = alpha,
    stack+0x0C = font_pointer,
    stack+0x10 = timer,
    stack+0x14 = flags,
    stack+0x18 = format_string,
    stack+0x1C... = format args
)
```

保存先との対応:

| Argument | Stored Offset |
| --- | --- |
| `r0` x | `+0x00` |
| `r1` y | `+0x04` |
| `stack+0x00` font_size | `+0x08` |
| `stack+0x0C` font_pointer | `+0x0C` |
| `stack+0x04` color | `+0x10` |
| `stack+0x10` timer | `+0x12` |
| `stack+0x08` alpha | `+0x14` |
| `stack+0x14` flags | `+0x15` |
| `r3` wrap_width | `+0x16` |
| `r2` alignment | `+0x18` |
| formatted text | `+0x1C` |

### Runtime Behavior

- `+0x12` is the active timer. `0204D658` renders active entries and decrements this field every pass.
- If `+0x15 bit0` is set, the render loop blinks the message: when `(timer & 7) > 3`, drawing is skipped for that frame but the timer still decrements.
- `+0x15 bits1-3` work as stacking groups. When a new message shares any of these bits with an active entry, older matching messages are shifted upward by `new_line_count * old_entry_font_size`.
- If no stacking-group bit matches but an active entry has the same Y position as the new message, the old entry timer is cleared. This prevents overlapping messages on the same line.
- If no empty slot exists, the enqueue function overwrites the active slot with the smallest remaining timer.
- `+0x1C` text is generated through a safe format path (`020A32DC`) and copied with `strcpy` (`020A6C50`). The final byte `+0x9B` is forced to `0`, so the effective text buffer is safely terminated.
- `+0x0C` is stored by enqueue, but the confirmed JP1.0 draw loop does not read it directly. The actual draw path uses the current/global font state, which explains why some versions work even when this field is not patched.

### JP1.0 Message Text Lookup

JP1.0でOSD呼び出し元が渡す `message id` は `0203C494` で文字列ポインタへ変換される。内部では `020639C8 -> 02063A5C -> 02063AC8` が、ASCII keyを作って `{key, text_ptr, len}` テーブルを検索する。

keyはmessage idの10進値で作る。たとえば `0xCA` は10進 `202` なので `H202`、`0xCD` は10進 `205` なので `H205`。

| Message ID Range | Table Header | File / Prefix | Notes |
| --- | --- | --- | --- |
| `< 0x0C` | `0x020E06AC` | `HudMsgsCommon.bin`, `H###` | static dumpでは `count=0x10`, table `0x02289684`。 |
| `0x0C..0x7A` | `0x020E06A4` | `HudMessagesSP.bin`, `H###` | single-player側ロード時に使う領域。現在のstatic dumpでは未ロード状態。 |
| `0x7B..0x101` | `0x020E06CC` | `HudMessagesMP.bin`, `H###` | static dumpでは `count=0x64`, table `0x02289844`。 |
| `0x12C..0x131` | `0x020E06CC` | `HudMessagesMP.bin`, `W###` | `id - 0x12C` を `W###` として引く。disconnect系の追加メッセージ。 |

本文の日本語部分は `mphCodeDatabase/文字コード.md` の独自文字コードで復元できる。今回のOSD対象では、同表に未収録の `C0A1`, `C0BA`, `C0B5` も出るため、US文脈との対応からそれぞれ `！`, `：`, `5` と推定して読んでいる。

OSD色調査で出てきたmessage idとJP文字列:

| Message ID | Key | Text | Color Definition / Call |
| --- | --- | --- | --- |
| `0x009` | `H009` | `アモがない！` | `0x0202DE68` / `0202DA6C` |
| `0x0C9` | `H201` | `オクトリスがリセットされました！` | `0x0212F6D8` or `0x0212FBCC` |
| `0x0CA` | `H202` | `ベースへもどれ！` | `0x0202DE50` / `0202D7A0` |
| `0x0CB` | `H203` | `ボーナスゲット！` | `0x0202DE50` / `0202D800` |
| `0x0CD` | `H205` | `ノードハック\nスタート` | `0x02031D0C` / `02031C50` |
| `0x0CE` | `H206` | `ノードハッククリア！` | `0x0202DE50` / `0202D860` |
| `0x0CF` | `H207` | `オクトリスはリセットされた！` | `0x0212F6D8`, `0x0212FA64`, or `0x0212FBCC` |
| `0x0D3` | `H211` | `ノードハックされました！` | `0202D88C: mov r2,#0x1F` / `0202D8C8` |
| `0x0E4` | `H228` | `ヘッドショット！` | `0x02018268` / `020174E8` |
| `0x0E5` | `H229` | `オクトリスがおちた！` | `0x0212FA64` / `0212F9E8` |
| `0x0E7` | `H231` | `オクトリスを\nおとしてしまった！` | `0x0212FA64` / `0212F948` |
| `0x0E8` | `H232` | `オクトリスをとりもどせ！` | `0x0213042C` / `02130388` |
| `0x0E9` | `H233` | `ハーフタレット： %d` | `0x02111F6C` / `02111CC0` |
| `0x0EA` | `H234` | `ハンターをロックオン！` | `0x02030534` / `020304FC` |
| `0x0EB` | `H235` | `セルフキル！` | `0x02018268` / `020180D8` |
| `0x0EC` | `H236` | `%sからの\nヘッドショットキル！` | `0x02018268` / `0201816C` |
| `0x0ED` | `H237` | `%sにたおされた！` | `0x02018268` / `020181D4` |
| `0x0EF` | `H239` | `%sを\nヘッドショットキル！` | `0x02018268` / `02018568` |
| `0x0F0` | `H240` | `%sをたおしてしまった！` | `0x02018268` / `02018434` |
| `0x0F1` | `H241` | `%sがプライムハンター！` | `0x02018268` / `020188CC` |
| `0x0F2` | `H242` | `プライムハンターがたおれた！` | `0x02018268` / `02018A84` |
| `0x0F3` | `H243` | `リタイア：\nのこりライフがなくなりました。` | `0x0200FED4` / `0200F458` |
| `0x0F7` | `H247` | `ラストバトル！` | `0x0200FED4` / `0200FBEC` |
| `0x0F8` | `H248` | `エマージェンシー！` | `0x0200FED4` / `0200FD5C` |
| `0x0F9` | `H249` | `ロックオンされた！` | `0x0200FED4` / `0200FDB0` |
| `0x0FC` | `H252` | `モーフボールボム` | `0x02018268` / `02018360` |
| `0x0FE` | `H254` | `5れんぞくキル！` | `0x02018268` / `020186E4` |
| `0x0FF` | `H255` | `%sが5れんぞくキル！` | `0x02018268` / `0201874C` |
| `0x101` | `H257` | `オクトリスがリセット！` | `0x0212F6D8` or `0x0212FA64` |

### Observed JP1.0 Enqueue Calls

代表的な `0204D748` 呼び出し。文字列そのものは `0203C494(message_id)` で引いたポインタを `format_string` として渡している。

| Call Site | Message ID Source | X | Y | Align | Wrap | Size | Color | Alpha | Timer | Flags | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `02031C50` | `0203C494(0xCD)` | `0x00080000` | `0x00085000` | `2` | `0x0100` | `0x00008000` | `0x3FEF` | `0x1F` | `0x2D` | `0x11` | `H205 = ノードハック\nスタート`。 |
| `0202D7A0` | `0203C494(0xCA)` | `0x00080000` | `0x00085000` | `2` | `0x0100` | `0x00008000` | `0x3FEF` | `0x1F` | `0x5A` | `0x01` | 同型の通知OSD。 |
| `0202D800` | `0203C494(0xCB)` | `0x00080000` | `0x00085000` | `2` | `0x0100` | `0x00008000` | `0x3FEF` | `0x1F` | `0x5A` | `0x01` | 同型の通知OSD。 |
| `0202D860` | `0203C494(0xCE)` | `0x00080000` | `0x00085000` | `2` | `0x0100` | `0x00008000` | `0x3FEF` | `0x1F` | `0x5A` | `0x01` | 同型の通知OSD。 |
| `0202DA6C` | `0203C494(0x09)` | `0x00080000` | `0x00078000` | `2` | `0x0100` | `0x00008000` | `0x295F` | `0x1F` | `0x2D` | `0x01` | `H009 = アモがない！`。 |
| `0200F458` | `0203C494(0xF3)` | `0x00080000` | `0x00098000` | `2` | `0x0100` | `0x00008000` | `0x3FEF` | `0x1F` | `0x0001` | `0x00` | timerが短い一時表示系。 |

### Packed Word Notes

`+0x10` のcolorと `+0x12` のdisplay timerは、32-bit書き込みなら1行でまとめられる。

```text
0x10 word = 0xTTTTCCCC
TTTT = display timer
CCCC = color code

例: color 0x295F, timer 0x002B
020E5B48 002B295F
```

`+0x14` の4 byteも、既定値なら `0100011F` としてまとめて置ける。

```text
020E5B4C 0100011F
```

### Color Patch Strategy

OSDの色変更は、目的によってpatch対象を分けるのが扱いやすい。

1. **表示中slotの色だけ変える**
   - `entry+0x10` にhalfword colorを書く。
   - すでに投入済みのOSDを後から塗り替える用途。
   - 次に `0204D748` で同じslotへ新規投入されると、enqueue引数の色で上書きされる。

```text
; slot 1 color only: bright green
120E5B48 00003FEF

; slot 1 color only: cyan
120E5B48 00007FE0
```

2. **slotを表示状態ごと固定する**
   - `entry+0x10` の32-bit wordを書き、colorとtimerを同時に入れる。
   - `0xTTTTCCCC` なので、下位halfwordが色、上位halfwordがtimer。
   - 常時有効なチートにするとtimerが減らず、blink/fade感も固定される。色だけ変えたいならhalfword writeの方が安全。

```text
; slot 1: color 0x3FEF, timer 0x002B
020E5B48 002B3FEF

; slot 1: color 0x7FE0, timer 0x002B
020E5B48 002B7FE0
```

3. **全20slotを同じ色にする**
   - OSDは `0x9C` strideなので、`entry+0x10` だけを20回halfword writeする。
   - どのslotへ投入されても色を揃えたい時向け。
   - timerは触らないので、空slotを無理にactive化しない。

```text
; all OSD slots: bright green
D3000000 020E5B38
C0000000 00000013
10000010 00003FEF
DC000000 0000009C
D2000000 00000000
```

4. **今後投入されるメッセージの既定色を変える**
   - 呼び出し元が `0204D748` に渡す `stack+0x04 = color` を変える。
   - active slotを後から塗るのではなく、新しく出るOSDの保存色そのものを変える方法。
   - JP1.0では各呼び出しブロックのliteral poolに `0x3FEF` などが置かれている。目的のOSDが分かっているなら、このliteralを1行で変えるのが最小。
   - literal poolを変えるpatchは簡単だが、同じliteralを読む複数メッセージがまとめて変わる。1メッセージだけ変えるなら、呼び出し元の命令列を個別に追う必要がある。

```text
; JP1.0 only: shared 0x3FEF OSD caller literal -> cyan
0202DE50 00007FE0

; JP1.0 only: 0x295F OSD caller literal near 0202DA2C -> cyan
0202DE68 00007FE0
```

### JP1.0 Color Source Investigation

JP1.0の `0204D748` 呼び出しを全走査した結果、OSD enqueue関数内に「OSD全体の色定義アドレス」は見つからない。`0204D748` は呼び出し元が `stack+0x04` に置いた色を `entry+0x10` へ保存するだけ。

つまり最小行数patchは、slot直書きではなく、目的のOSD呼び出し元のliteral poolを書き換える形になる。

特に「アモがない！」は `0202DA1C-0202DA6C` のブロックで、`0202DA1C` が message id `0x09` をセットし、`0202DA2C` が色literal `0x0202DE68 = 0x0000295F` を読む。ここを変えるのが最小候補。

以前候補にしていた `0xCD` は `H205 = ノードハック\nスタート`。`02031D0C` を変えるとこのメッセージの色が変わる。

```text
; JP1.0: message id 0x09 / H009 / "アモがない！" color -> cyan
0202DE68 00007FE0

; JP1.0: same target -> pure red
0202DE68 0000001F

; JP1.0: same target -> purple
0202DE68 00004C4C

; JP1.0: message id 0xCD / H205 / "ノードハック\nスタート" color -> cyan
02031D0C 00007FE0

; JP1.0: same H205 target -> purple
02031D0C 00004C4C
```

確認した色ソース。`0204D748` への直callはJP1.0内に28件あり、色定義としては次の12箇所に集約できる。

| Color Definition | Default | Enqueue Call Site(s) | Patch Pattern | Notes |
| --- | --- | --- | --- | --- |
| `0x02031D0C` | `0x3FEF` | `02031C50` | `02031D0C 0000CCCC` | `0xCD = H205 = ノードハック\nスタート`。 |
| `0x0202DE50` | `0x3FEF` | `0202D7A0`, `0202D800`, `0202D860` | `0202DE50 0000CCCC` | `0xCA/0xCB/0xCE = H202/H203/H206`。 |
| `0x0202DE68` | `0x295F` | `0202DA6C` | `0202DE68 0000CCCC` | `0x09 = H009 = アモがない！`。アモなしOSDの最小patch候補。 |
| `0x0202D88C` | immediate `0x001F` | `0202D8C8` | instruction patch | `0xD3 = H211 = ノードハックされました！`。literalではなく `mov r2,#0x1F`。 |
| `0x0200FED4` | `0x3FEF` | `0200F458`, `0200FBEC`, `0200FD5C`, `0200FDB0` | `0200FED4 0000CCCC` | `0xF3/0xF7/0xF8/0xF9 = H243/H247/H248/H249`。 |
| `0x02018268` | `0x3FEF` | `020174E8`, `020180D8`, `0201816C`, `020181D4`, `02018360`, `02018434`, `02018568`, `020186E4`, `0201874C`, `020188CC`, `02018A84` | `02018268 0000CCCC` | `H228/H235/H236/H237/H252/H240/H239/H254/H255/H241/H242` の通知群。 |
| `0x02030534` | `0x3FEF` | `020304FC` | `02030534 0000CCCC` | `0xEA = H234 = ハンターをロックオン！`。 |
| `0x02111F6C` | `0x3FEF` | `02111CC0` | `02111F6C 0000CCCC` | `0xE9 = H233 = ハーフタレット： %d`。full側追加領域。 |
| `0x0212F6D8` | `0x3FEF` | `0212F6BC` | `0212F6D8 0000CCCC` | text message id `0xC9/0xCF/0x101`。`0x82/0x81` は別関数 `02073C38` へ渡すイベント系id。 |
| `0x0212FA64` | `0x3FEF` | `0212F948`, `0212F9E8` | `0212FA64 0000CCCC` | text message id `0xCF/0xE7` または `0x101/0xE5`。`0x80/0x81/0x82` は別関数 `02073C38` へ渡すイベント系id。 |
| `0x0212FBCC` | `0x3FEF` | `0212FB50` | `0212FBCC 0000CCCC` | message id `0xC9` または `0xCF`。条件分岐で切替。 |
| `0x0213042C` | `0x3FEF` | `02130388` | `0213042C 0000CCCC` | message id `0xE8`。full側追加領域の通知。 |

呼び出し別の全内訳:

| Enqueue Call | Message ID | Color Definition | Default | Timer | Flags | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `0200F458` | `0xF3` | `0x0200FED4` | `0x3FEF` | `0x01` | `0x00` | `Y=0x98000`。 |
| `0200FBEC` | `0xF7` | `0x0200FED4` | `0x3FEF` | `0x01` | `0x00` | `Y=0xAA000`。 |
| `0200FD5C` | `0xF8` | `0x0200FED4` | `0x3FEF` | `0x01` | `0x00` | `Y=0x96000`。 |
| `0200FDB0` | `0xF9` | `0x0200FED4` | `0x3FEF` | `0x01` | `0x00` | `Y=0xA0000`。 |
| `020174E8` | `0xE4` | `0x02018268` | `0x3FEF` | `0x14` | `0x00` | `Y=0x28000`。 |
| `020180D8` | `0xEB` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `0201816C` | `0xEC` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `020181D4` | `0xED` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018360` | `0xFC` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018434` | `0xF0` | `0x02018268` | `0x3FEF` | `0x3C` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018568` | `0xEF` | `0x02018268` | `0x3FEF` | `0x3C` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `020186E4` | `0xFE` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `0201874C` | `0xFF` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `020188CC` | `0xF1` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018A84` | `0xF2` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `0202D7A0` | `0xCA` | `0x0202DE50` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D800` | `0xCB` | `0x0202DE50` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D860` | `0xCE` | `0x0202DE50` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D8C8` | `0xD3` | `0202D88C: mov r2,#0x1F` | `0x001F` | `0x5A` | `0x11` | literalなし。赤系の即値色。 |
| `0202DA6C` | `0x09` | `0x0202DE68` | `0x295F` | `0x2D` | `0x01` | `H009 = アモがない！`。defaultだけ赤/コーラル系。 |
| `020304FC` | `0xEA` | `0x02030534` | `0x3FEF` | `0x3C` | `0x00` | 単独literal。 |
| `02031C50` | `0xCD` | `0x02031D0C` | `0x3FEF` | `0x2D` | `0x11` | `H205 = ノードハック\nスタート`。 |
| `02111CC0` | `0xE9` | `0x02111F6C` | `0x3FEF` | `0x01` | `0x00` | full側追加領域。 |
| `0212F6BC` | `0xC9/0xCF/0x101` | `0x0212F6D8` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。`0x82/0x81` は `02073C38` 側のid。 |
| `0212F948` | `0xCF/0xE7` | `0x0212FA64` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。`0x81/0x80` は `02073C38` 側のid。 |
| `0212F9E8` | `0x101/0xE5` | `0x0212FA64` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。`0x82/0x80` は `02073C38` 側のid。 |
| `0212FB50` | `0xC9/0xCF` | `0x0212FBCC` | `0x3FEF` | `0x3C` | `0x01` | 自playerなら `0xC9`、それ以外は `0xCF`。 |
| `02130388` | `0xE8` | `0x0213042C` | `0x3FEF` | `0x01` | `0x00` | `Y=0x32000`。full側追加領域。 |

literal patchの考え方:

- `0x02031D0C` のようなliteralは `ldr r1,[r15,#...]` で32-bit wordとして読まれるので、基本は32-bit writeで `0000CCCC` を入れる。
- downstreamでは `0204D748` がhalfwordとして保存するため上位16bitは実質不要だが、ゼロにしておく方が安全。
- 「アモがない！」だけを狙うなら `0202DE68` から試す。`02031D0C` は `ノードハック\nスタート` 用。`0202DE50` は別の通知群もまとめて変わる。
- `0204D748` 本体を命令patchして色を強制する方法もあるが、全OSDへ効いてしまう上、ARM即値で表せる色に制約が出る。個別OSDの最小patchとしてはliteral pool変更の方が安全。

JP1.0のslot直書き用の色アドレスは次の式で出せる。

```text
color_addr = OSD_MSG_BASE + (slot - 1) * 0x9C + 0x10
```

JP1.0の例:

| Slot | Color Address | Halfword Patch Example |
| --- | --- | --- |
| 1 | `0x020E5B48` | `120E5B48 00003FEF` |
| 2 | `0x020E5BE4` | `120E5BE4 00003FEF` |
| 3 | `0x020E5C80` | `120E5C80 00003FEF` |
| 4 | `0x020E5D1C` | `120E5D1C 00003FEF` |
| 5 | `0x020E5DB8` | `120E5DB8 00003FEF` |

注意点:

- BGR555の上位bit `X` は透明度ではない。OSDの透明度は別フィールド `entry+0x14`。
- byte writeで下位byteだけ変えると、R成分とG成分の一部だけを触ることになる。色指定はhalfword writeを基本にする。
- `0204D748` は空きslotまたはtimer最小slotを選んで上書きするので、slot固定patchは「そのslotを使った時だけ効く」。汎用化するなら全slot loopか、呼び出し元のcolor引数patchが向く。
- Version移植ではslot直書きもOSD baseだけでなくentry strideを確認する。JP1.0は `0x9C`、US1.0は `0x80`。呼び出し元/literal patchはリージョンごとに再確認が必要。

## JP1.0 Slot Map

| Slot | Entry Base | Text Buffer | Text End | Notes |
| --- | --- | --- | --- | --- |
| 1 | `0x020E5B38` | `0x020E5B54` | `0x020E5BD3` | シート上で詳細確認。 |
| 2 | `0x020E5BD4` | `0x020E5BF0` | `0x020E5C6F` | `AMMO`既定テキスト確認。 |
| 3 | `0x020E5C70` | `0x020E5C8C` | `0x020E5D0B` | `+0x12` timer byte write例あり。 |
| 4 | `0x020E5D0C` | `0x020E5D28` | `0x020E5DA7` | 終端クリア例 `0x020E5DA4`。 |
| 5 | `0x020E5DA8` | `0x020E5DC4` | `0x020E5E43` | 終端クリア例 `0x020E5E40`。 |
| 6 | `0x020E5E44` | `0x020E5E60` | `0x020E5EDF` | strideから補完。 |
| 7 | `0x020E5EE0` | `0x020E5EFC` | `0x020E5F7B` | strideから補完。 |
| 8 | `0x020E5F7C` | `0x020E5F98` | `0x020E6017` | strideから補完。 |
| 9 | `0x020E6018` | `0x020E6034` | `0x020E60B3` | strideから補完。 |
| 10 | `0x020E60B4` | `0x020E60D0` | `0x020E614F` | strideから補完。 |
| 11 | `0x020E6150` | `0x020E616C` | `0x020E61EB` | strideから補完。 |
| 12 | `0x020E61EC` | `0x020E6208` | `0x020E6287` | strideから補完。 |
| 13 | `0x020E6288` | `0x020E62A4` | `0x020E6323` | strideから補完。 |
| 14 | `0x020E6324` | `0x020E6340` | `0x020E63BF` | strideから補完。 |
| 15 | `0x020E63C0` | `0x020E63DC` | `0x020E645B` | strideから補完。 |
| 16 | `0x020E645C` | `0x020E6478` | `0x020E64F7` | strideから補完。 |
| 17 | `0x020E64F8` | `0x020E6514` | `0x020E6593` | strideから補完。 |
| 18 | `0x020E6594` | `0x020E65B0` | `0x020E662F` | strideから補完。 |
| 19 | `0x020E6630` | `0x020E664C` | `0x020E66CB` | strideから補完。 |
| 20 | `0x020E66CC` | `0x020E66E8` | `0x020E6767` | シート上でラストと明記。 |

## Color Notes

OSD MSG colorは、`0xCCRR` 風の独自形式ではなく、NDSでよく使う16-bit BGR555として読むのが自然。
`0204D658` は `entry+0x10` を `ldrh` で読み、text draw (`0204A53C`) へ渡す。`0204A53C` 側では同系のhalfword colorを3D color pathへ流している。`Weapon-Data-Struct-JP1_0.md` の弾色メモも `0x001F` 赤、`0x03E0` 緑、`0x7C00` 青、`0x7FFF` 白として整理済みで、OSD観測値とも整合する。

### BGR555 Format

```text
code = XBBBBBGGGGGRRRRR

R5 = code & 0x1F
G5 = (code >> 5) & 0x1F
B5 = (code >> 10) & 0x1F

R8 = (R5 << 3) | (R5 >> 2)
G8 = (G5 << 3) | (G5 >> 2)
B8 = (B5 << 3) | (B5 >> 2)

code = (B5 << 10) | (G5 << 5) | R5
```

基本は `0x0000-0x7FFF` を使う。上位bit `X` は色としては不要なので、`0xEA00` のような観測値は、まず下位15bit (`0x6A00`) として見るのが安全。

### Observed Colors Re-read As BGR555

| Code | Observed Color | BGR555 Approx RGB | Notes |
| --- | --- | --- | --- |
| `0x2900` | 青 | `#004252` | 暗い青緑。 |
| `0x005F` | 真赤 | `#FF1000` | ほぼ赤。純赤なら `0x001F`。 |
| `0x5F5F` | 白 | `#FFD6BD` | 明るい暖色なので白っぽく見える。純白なら `0x7FFF`。 |
| `0x0000` | 黒 | `#000000` | 黒。 |
| `0x5F00` | 水色 | `#00C6BD` | やや暗いシアン。最大シアンなら `0x7FE0`。 |
| `0x1F00` | 緑寄り | `#00C639` | 緑。 |
| `0x4C4C` | 紫 | `#63109C` | Bが強く、R少し、G少し。観測とかなり合う。 |
| `0x1400` | 暗い紫 | `#000029` | 式上は暗い青。紫に寄せるなら `0x1408` などでRを足す。 |
| `0x295F` | OSD runtime例 | `#FF5252` | 赤/コーラル系。zoom/notice系で使われる。 |
| `0x3FEF` | OSD runtime例、武器メモでは緑 | `#7BFF7B` | 実呼び出しで多い明るい緑。 |

### Basic Color Codes

| Color | Code | Approx RGB |
| --- | --- | --- |
| Black | `0x0000` | `#000000` |
| Red | `0x001F` | `#FF0000` |
| Green | `0x03E0` | `#00FF00` |
| Blue | `0x7C00` | `#0000FF` |
| Yellow | `0x03FF` | `#FFFF00` |
| Cyan | `0x7FE0` | `#00FFFF` |
| Magenta | `0x7C1F` | `#FF00FF` |
| White | `0x7FFF` | `#FFFFFF` |

### Useful OSD Color Codes

紫/インディゴ系。

| Code | Approx RGB | Notes |
| --- | --- | --- |
| `0x1400` | `#000029` | 暗い青。元メモでは暗い紫寄りに見えた値。 |
| `0x1408` | `#420029` | 暗い紫。 |
| `0x2008` | `#420042` | 低輝度の紫。 |
| `0x300C` | `#630063` | 紫。 |
| `0x4010` | `#840084` | 中間紫。 |
| `0x5014` | `#A500A5` | 明るい紫。 |
| `0x4C4C` | `#63109C` | 観測済みの紫。 |

赤/ピンク/オレンジ系。

| Code | Approx RGB | Notes |
| --- | --- | --- |
| `0x001F` | `#FF0000` | 純赤。 |
| `0x141F` | `#FF0029` | 深い赤ピンク。 |
| `0x295F` | `#FF5252` | OSD runtime例。赤/コーラル。 |
| `0x3D5F` | `#FF527B` | 武器メモではMagmaul赤。 |
| `0x235F` | `#FFD642` | 武器メモではPower Beam系のオレンジ/黄色。 |
| `0x5E9F` | `#FFA5BD` | 明るいピンク。 |

水色/緑/黄色系。

| Code | Approx RGB | Notes |
| --- | --- | --- |
| `0x1F00` | `#00C639` | 観測済みの緑。 |
| `0x03E0` | `#00FF00` | 純緑。 |
| `0x3FEF` | `#7BFF7B` | 実呼び出しで多い明るい緑。 |
| `0x0FFF` | `#FFFF18` | 武器メモでは黄色。 |
| `0x5F00` | `#00C6BD` | 観測済みの水色。 |
| `0x6A00` | `#0084D6` | `0xEA00` 観測の下位15bit。青寄りの水色。 |
| `0x7FE0` | `#00FFFF` | 最大シアン。 |

狙い撃ちの目安:

- 紫: RとBを同じくらいにし、Gを低くする。暗めなら `0x2008`、明るめなら `0x5014`。
- 赤/ピンク: Rを高め、Bを少し足す。OSD既定寄りなら `0x295F`。
- 緑: Gを高める。ゲーム内通知っぽい明るい緑は `0x3FEF`。
- 水色: GとBを高め、Rを低くする。観測値寄りなら `0x5F00`、鮮やかなら `0x7FE0`。
- 暗い色はOSDのalpha/font描画で潰れやすいので、実用上は5-bit成分を `8` 以上にしておくと見やすい。

## JP1.0 Example

slot 1に中央揃えで `AMMO` を表示する最小例。

```text
020E5B38 00080000
020E5B3C 00078000
020E5B40 00008000
020E5B44 020CD510
020E5B48 002B295F
020E5B4C 0100011F
020E5B50 00000002
020E5B54 4F4D4D41
020E5BD0 00000000
```

slot 3のtimerだけを `0x29` にする例。

```text
220E5C82 00000029
```

## Text Buffer Notes

- 文字列はlittle-endian 32-bit単位で置く。`AMMO` はメモリ上のbyte列 `41 4D 4D 4F` なので、コード値は `4F4D4D41`。
- 各slotの文字列入力範囲は `entry+0x1C` から `entry+0x9B` までの `0x80` byte。
- 終端用に末尾付近へ `00000000` を入れると安全。slot 1なら `0x020E5BD0`、slot 4なら `0x020E5DA4`、slot 5なら `0x020E5E40` のクリア例がある。
- 20件目の次、つまりslot 21以降は使えない。
