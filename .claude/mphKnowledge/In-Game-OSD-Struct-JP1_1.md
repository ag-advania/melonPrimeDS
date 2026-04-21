# In-Game-OSD-Struct-JP1_1

`mphDump/JP1_1.txt` で確認したJP1.1版のin-game OSD message構造と色定義メモ。

JP1.0版の対応メモは [`In-Game-OSD-Struct-JP1_0.md`](In-Game-OSD-Struct-JP1_0.md)。

## JP1.1 Runtime Layout

JP1.1のOSD enqueue相当は `0204D748`。呼び出し元が `stack+0x04` に置いた色をslotの `entry+0x10` へ保存する。

JP1.1はJP1.0と同じ `0x9C` stride。OSD関数の主要アドレスもJP1.0と同じで、OSD MSG baseとfont pointer周辺のdataだけがJP1.0から `-0x40` ずれている。

| Item | JP1.1 Value | Notes |
| --- | --- | --- |
| OSD enqueue / setup function | `0204D748` | JP1.0と同じ。 |
| OSD draw loop | `0204D658` | slotを20件走査。 |
| Has active by flag mask | `0204D8B0` | `entry+0x15 & mask` のactive slot確認。 |
| Clear by flag mask | `0204D8F0` | `entry+0x15 & mask` のactive slotを消す。 |
| Clear all OSD timers | `0204D92C` | 全slotの `entry+0x12` を0にする。 |
| OSD MSG base | `0x020E5AF8` | slot 1 entry base。JP1.0 `0x020E5B38` から `-0x40`。 |
| Entry stride | `0x9C` | JP1.0と同じ。 |
| Font pointer source | `0x020E0574 -> 0x020CD4D0` | enqueue callerが `stack+0x0C` に置く。 |
| Color offset | `entry+0x10` | 16-bit BGR555。 |
| Timer offset | `entry+0x12` | nonzeroでactive。 |
| Alpha offset | `entry+0x14` | 通常 `0x1F`。 |
| Flags offset | `entry+0x15` | blink/group flags。 |
| Wrap offset | `entry+0x16` | layout width。 |
| Align offset | `entry+0x18` | text alignment。 |
| Text buffer | `entry+0x1C` | `+0x9B` が終端保証。 |

slot直書き用の色アドレス:

```text
color_addr = 0x020E5AF8 + (slot - 1) * 0x9C + 0x10
```

JP1.1の例:

| Slot | Color Address | Halfword Patch Example |
| --- | --- | --- |
| 1 | `0x020E5B08` | `120E5B08 00003FEF` |
| 2 | `0x020E5BA4` | `120E5BA4 00003FEF` |
| 3 | `0x020E5C40` | `120E5C40 00003FEF` |
| 4 | `0x020E5CDC` | `120E5CDC 00003FEF` |
| 5 | `0x020E5D78` | `120E5D78 00003FEF` |

全20slotの色だけを同じ値にするloop patch:

```text
; JP1.1: all OSD slots color -> CCCC
D3000000 020E5AF8
C0000000 00000013
10000010 0000CCCC
DC000000 0000009C
D2000000 00000000
```

## Message Text Lookup

JP1.1でOSD呼び出し元が渡す `message id` は `0203C494` で文字列ポインタへ変換される。内部の挙動はJP1.0と同じ。

keyはmessage idの10進値で作る。たとえば `0xCA` は10進 `202` なので `H202`、`0xCD` は10進 `205` なので `H205`。

| Message ID Range | Table Header | File / Prefix | Notes |
| --- | --- | --- | --- |
| `< 0x0C` | `0x020E066C` | `HudMsgsCommon.bin`, `H###` | static dumpでは `count=0x10`, table `0x02289644`。 |
| `0x0C..0x7A` | `0x020E0664` | `HudMessagesSP.bin`, `H###` | single-player側ロード時に使う領域。static dumpでは未ロード状態。 |
| `0x7B..0x101` | `0x020E068C` | `HudMessagesMP.bin`, `H###` | static dumpでは `count=0x64`, table `0x02289804`。 |
| `0x12C..0x131` | `0x020E068C` | `HudMessagesMP.bin`, `W###` | `id - 0x12C` を `W###` として引く。 |

OSD色調査で出てきたmessage idとJP1.1文字列:

| Message ID | Key | Text | Color Definition / Call |
| --- | --- | --- | --- |
| `0x009` | `H009` | `アモがない！` | `0x0202DE68` / `0202DA6C` |
| `0x0C9` | `H201` | `オクトリスがリセットされました！` | `0x0212F698` or `0x0212FB8C` |
| `0x0CA` | `H202` | `ベースへもどれ！` | `0x0202DE50` / `0202D7A0` |
| `0x0CB` | `H203` | `ボーナスゲット！` | `0x0202DE50` / `0202D800` |
| `0x0CD` | `H205` | `ノードハック\nスタート` | `0x02031D0C` / `02031C50` |
| `0x0CE` | `H206` | `ノードハッククリア！` | `0x0202DE50` / `0202D860` |
| `0x0CF` | `H207` | `オクトリスはリセットされた！` | `0x0212F698`, `0x0212FA24`, or `0x0212FB8C` |
| `0x0D3` | `H211` | `ノードハックされました！` | `0202D88C: mov r2,#0x1F` / `0202D8C8` |
| `0x0E4` | `H228` | `ヘッドショット！` | `0x02018268` / `020174E8` |
| `0x0E5` | `H229` | `オクトリスがおちた！` | `0x0212FA24` / `0212F9A8` |
| `0x0E7` | `H231` | `オクトリスを\nおとしてしまった！` | `0x0212FA24` / `0212F908` |
| `0x0E8` | `H232` | `オクトリスをとりもどせ！` | `0x021303EC` / `02130348` |
| `0x0E9` | `H233` | `ハーフタレット： %d` | `0x02111F2C` / `02111C80` |
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
| `0x101` | `H257` | `オクトリスがリセット！` | `0x0212F698` or `0x0212FA24` |

## Color Source Investigation

JP1.1の `0204D748` への直callは28件あり、色定義は `11 literal + 1 immediate` の12箇所に集約できる。

`0204D748` 本体に「OSD全体の色定義アドレス」はない。各呼び出し元が `stack+0x04` へ色を置き、`0204D748` がそれを `entry+0x10` に保存する。最小行数patchは、目的のOSD呼び出し元のliteral poolを書き換える形が基本。

特にno-ammo系は `0202DA1C-0202DA6C` のブロックで、`0202DA1C` が message id `0x09` をセットし、`0202DA2C` が色literal `0x0202DE68 = 0x0000295F` を読む。ここがJP1.1の最小patch候補。

```text
; JP1.1: message id 0x09 / H009 / "アモがない！" color -> cyan
0202DE68 00007FE0

; JP1.1: same target -> pure red
0202DE68 0000001F

; JP1.1: message id 0xCD / H205 color -> cyan
02031D0C 00007FE0

; JP1.1: shared 0xCA/0xCB/0xCE notification color -> cyan
0202DE50 00007FE0
```

確認した色ソース:

| Color Definition | Default | Enqueue Call Site(s) | Patch Pattern | Notes |
| --- | --- | --- | --- | --- |
| `0x0200FED4` | `0x3FEF` | `0200F458`, `0200FBEC`, `0200FD5C`, `0200FDB0` | `0200FED4 0000CCCC` | `H243/H247/H248/H249`。 |
| `0x02018268` | `0x3FEF` | `020174E8`, `020180D8`, `0201816C`, `020181D4`, `02018360`, `02018434`, `02018568`, `020186E4`, `0201874C`, `020188CC`, `02018A84` | `02018268 0000CCCC` | kill/death通知群。 |
| `0x0202DE50` | `0x3FEF` | `0202D7A0`, `0202D800`, `0202D860` | `0202DE50 0000CCCC` | `0xCA/0xCB/0xCE = H202/H203/H206`。 |
| `0x0202D88C` | immediate `0x001F` | `0202D8C8` | instruction patch | `0xD3 = H211`。literalではなく `mov r2,#0x1F`。 |
| `0x0202DE68` | `0x295F` | `0202DA6C` | `0202DE68 0000CCCC` | `0x09 = H009 = アモがない！`。no-ammo系の最小patch候補。 |
| `0x02030534` | `0x3FEF` | `020304FC` | `02030534 0000CCCC` | `0xEA = H234`。 |
| `0x02031D0C` | `0x3FEF` | `02031C50` | `02031D0C 0000CCCC` | `0xCD = H205`。 |
| `0x02111F2C` | `0x3FEF` | `02111C80` | `02111F2C 0000CCCC` | `0xE9 = H233`。full側追加領域。 |
| `0x0212F698` | `0x3FEF` | `0212F67C` | `0212F698 0000CCCC` | message id `0xC9/0xCF/0x101`。 |
| `0x0212FA24` | `0x3FEF` | `0212F908`, `0212F9A8` | `0212FA24 0000CCCC` | message id `0xCF/0xE7` または `0x101/0xE5`。 |
| `0x0212FB8C` | `0x3FEF` | `0212FB10` | `0212FB8C 0000CCCC` | message id `0xC9/0xCF`。 |
| `0x021303EC` | `0x3FEF` | `02130348` | `021303EC 0000CCCC` | `0xE8 = H232`。 |

literal patchの考え方:

- `0x0202DE68` のようなliteralは `ldr r1,[r15,#...]` で32-bit wordとして読まれるので、基本は32-bit writeで `0000CCCC` を入れる。
- downstreamでは `0204D748` がhalfwordとして保存するため上位16bitは実質不要だが、ゼロにしておく方が安全。
- no-ammo系だけを狙うならJP1.1では `0202DE68` から試す。`02031D0C` は `ノードハック\nスタート` 用。`0202DE50` や `02018268` は複数メッセージをまとめて変える。
- `0204D748` 本体を命令patchして色を強制する方法もあるが、全OSDへ効いてしまう上、ARM即値で表せる色に制約が出る。個別OSDの最小patchとしてはliteral pool変更の方が安全。

## Direct Enqueue Calls

| Enqueue Call | Message ID | Color Definition | Default | Timer | Flags | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `0200F458` | `0xF3` | `0x0200FED4` | `0x3FEF` | `0x01` | `0x00` | `H243`。 |
| `0200FBEC` | `0xF7` | `0x0200FED4` | `0x3FEF` | `0x01` | `0x00` | `H247`。 |
| `0200FD5C` | `0xF8` | `0x0200FED4` | `0x3FEF` | `0x01` | `0x00` | `H248`。 |
| `0200FDB0` | `0xF9` | `0x0200FED4` | `0x3FEF` | `0x01` | `0x00` | `H249`。 |
| `020174E8` | `0xE4` | `0x02018268` | `0x3FEF` | `0x14` | `0x00` | `H228 = ヘッドショット！`。 |
| `020180D8` | `0xEB` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `H235`。 |
| `0201816C` | `0xEC` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `H236`。 |
| `020181D4` | `0xED` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `H237`。 |
| `02018360` | `0xFC` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `H252`。 |
| `02018434` | `0xF0` | `0x02018268` | `0x3FEF` | `0x3C` | `0x02` | `H240`。 |
| `02018568` | `0xEF` | `0x02018268` | `0x3FEF` | `0x3C` | `0x02` | `H239`。 |
| `020186E4` | `0xFE` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `H254`。 |
| `0201874C` | `0xFF` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `H255`。 |
| `020188CC` | `0xF1` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `H241`。 |
| `02018A84` | `0xF2` | `0x02018268` | `0x3FEF` | `0x5A` | `0x02` | `H242`。 |
| `0202D7A0` | `0xCA` | `0x0202DE50` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D800` | `0xCB` | `0x0202DE50` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D860` | `0xCE` | `0x0202DE50` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D8C8` | `0xD3` | `0202D88C: mov r2,#0x1F` | `0x001F` | `0x5A` | `0x11` | literalなし。 |
| `0202DA6C` | `0x09` | `0x0202DE68` | `0x295F` | `0x2D` | `0x01` | `H009 = アモがない！`。 |
| `020304FC` | `0xEA` | `0x02030534` | `0x3FEF` | `0x3C` | `0x00` | `H234`。 |
| `02031C50` | `0xCD` | `0x02031D0C` | `0x3FEF` | `0x2D` | `0x11` | `H205`。 |
| `02111C80` | `0xE9` | `0x02111F2C` | `0x3FEF` | `0x01` | `0x00` | full側追加領域。 |
| `0212F67C` | `0xC9/0xCF/0x101` | `0x0212F698` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。 |
| `0212F908` | `0xCF/0xE7` | `0x0212FA24` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。 |
| `0212F9A8` | `0x101/0xE5` | `0x0212FA24` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。 |
| `0212FB10` | `0xC9/0xCF` | `0x0212FB8C` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。 |
| `02130348` | `0xE8` | `0x021303EC` | `0x3FEF` | `0x01` | dynamic | `H232`。 |

## Color Format

OSD colorは16-bit BGR555。

```text
code = XBBBBBGGGGGRRRRR

R5 = code & 0x1F
G5 = (code >> 5) & 0x1F
B5 = (code >> 10) & 0x1F

code = (B5 << 10) | (G5 << 5) | R5
```

よく使う値:

| Color | Code |
| --- | --- |
| Red | `0x001F` |
| Green | `0x03E0` |
| Blue | `0x7C00` |
| Yellow | `0x03FF` |
| Cyan | `0x7FE0` |
| Magenta | `0x7C1F` |
| White | `0x7FFF` |
| Bright green OSD default | `0x3FEF` |
| Red/coral OSD default | `0x295F` |
