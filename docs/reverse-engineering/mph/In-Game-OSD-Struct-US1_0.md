# In-Game-OSD-Struct-US1_0

`mphDump/US1_0.txt` で確認したUS1.0版のin-game OSD message構造と色定義メモ。

JP1.0版の対応メモは [`In-Game-OSD-Struct-JP1_0.md`](In-Game-OSD-Struct-JP1_0.md)。

## US1.0 Runtime Layout

US1.0のOSD enqueue相当は `0204BCE8`。呼び出し元が `stack+0x04` に置いた色をslotの `entry+0x10` へ保存する。

JP1.0の `0204D748` と役割は同じだが、US1.0ではOSD entry strideが `0x80`。JP1.0の `0x9C` strideをそのまま移植しないこと。

| Item | US1.0 Value | Notes |
| --- | --- | --- |
| OSD enqueue / setup function | `0204BCE8` | `0204D748` JP1.0相当。 |
| OSD draw loop | `0204BBC8` | slotを20件走査。 |
| OSD MSG base | `0x020E3C3C` | slot 1 entry base。 |
| Entry stride | `0x80` | JP1.0は `0x9C`。 |
| Font pointer source | `0x020DE700 -> 0x020CB6D4` | enqueue callerが `stack+0x0C` に置く。 |
| Color offset | `entry+0x10` | 16-bit BGR555。 |
| Timer offset | `entry+0x12` | nonzeroでactive。 |
| Alpha offset | `entry+0x14` | 通常 `0x1F`。 |
| Flags offset | `entry+0x15` | blink/group flags。 |
| Wrap offset | `entry+0x16` | layout width。 |
| Align offset | `entry+0x18` | text alignment。 |
| Text buffer | `entry+0x1C` | JP1.0よりentry全体が短い。 |

slot直書き用の色アドレス:

```text
color_addr = 0x020E3C3C + (slot - 1) * 0x80 + 0x10
```

US1.0の例:

| Slot | Color Address | Halfword Patch Example |
| --- | --- | --- |
| 1 | `0x020E3C4C` | `120E3C4C 00003FEF` |
| 2 | `0x020E3CCC` | `120E3CCC 00003FEF` |
| 3 | `0x020E3D4C` | `120E3D4C 00003FEF` |
| 4 | `0x020E3DCC` | `120E3DCC 00003FEF` |
| 5 | `0x020E3E4C` | `120E3E4C 00003FEF` |

## Color Source Investigation

US1.0では `0204BCE8` への直callが28件あり、色定義は `11 literal + 1 immediate` の12箇所に集約できる。

US1.0で `0203C3B0 -> 02061C34 -> 02061D34` を追うと、message idはASCII keyへ変換されてから `{key, text_ptr, len}` の文字列テーブルを二分探索している。`02061C34` は `r0` のASCII prefixと `r1` の10進値から `H202` のようなkeyを作るため、hexの `0xCA` は10進 `202`、つまり `H202` になる。

`0203C3B0` の範囲分岐:

| Message ID Range | Table Header | File / Prefix | Notes |
| --- | --- | --- | --- |
| `< 0x0C` | `0x020DE7D8` | `HudMsgsCommon.bin`, `H###` | static dumpでは `count=0x0B`, table `0x02235D44`。 |
| `0x0C..0x7A` | `0x020DE7D0` | `HudMessagesSP.bin`, `H###` | single-player側ロード時に使う領域。 |
| `0x7B..0x101` | `0x020DE7F8` | `HudMessagesMP.bin`, `H###` | static dumpでは `count=0x64`, table `0x02235E64`。 |
| `0x12C..0x131` | `0x020DE7F8` | `HudMessagesMP.bin`, `W###` | `id - 0x12C` を `W###` として引く。`W001..W005` はdisconnect系で、`0x12C -> W000` はfallback候補。 |

JP1.0で「アモがない!!」系として見ていた候補は、US1.0では分けて考える必要がある。`message id 0xCD` は `H205 = acquiring\nnode` で、色literalは `0x02031BF8 = 0x00003FEF`。US1.0の `AMMO DEPLETED!` は common tableの `message id 0x09 = H009` で、色literalは `0x0202DE44 = 0x0000295F`。

```text
; US1.0: message id 0x09 / H009 / "AMMO DEPLETED!" color -> cyan
0202DE44 00007FE0

; US1.0: message id 0xCD / H205 / "acquiring\nnode" color -> cyan
02031BF8 00007FE0

; US1.0: slot 1 active color only -> cyan
120E3C4C 00007FE0
```

OSD色調査で出てきたmessage idとUS文字列:

| Message ID | Key | Text | Color Definition / Call |
| --- | --- | --- | --- |
| `0x009` | `H009` | `AMMO DEPLETED!` | `0x0202DE44` / `0202DA90` |
| `0x0C9` | `H201` | `your octolith reset!` | `0x0212D538` or `0x0212DA2C` |
| `0x0CA` | `H202` | `return to base` | `0x0202DE2C` / `0202D7C4` |
| `0x0CB` | `H203` | `bounty received` | `0x0202DE2C` / `0202D824` |
| `0x0CD` | `H205` | `acquiring\nnode` | `0x02031BF8` / `02031B3C` |
| `0x0CE` | `H206` | `complete` | `0x0202DE2C` / `0202D884` |
| `0x0CF` | `H207` | `enemy octolith reset!` | `0x0212D538`, `0x0212D8C4`, or `0x0212DA2C` |
| `0x0D3` | `H211` | `node stolen` | `0202D8B0: mov r2,#0x1F` / `0202D8EC` |
| `0x0E4` | `H228` | `HEADSHOT!` | `0x02018288` / `02017508` |
| `0x0E5` | `H229` | `the octolith\nhas been dropped!` | `0x0212D8C4` / `0212D848` |
| `0x0E7` | `H231` | `your team\ndropped the octolith!` | `0x0212D8C4` / `0212D7A8` |
| `0x0E8` | `H232` | `your octolith is missing!` | `0x0212E28C` / `0212E1E8` |
| `0x0E9` | `H233` | `turret energy: %d` | `0x0210FE2C` / `0210FB80` |
| `0x0EA` | `H234` | `COWARD DETECTED!` | `0x0203051C` / `020304E4` |
| `0x0EB` | `H235` | `YOU SELF-DESTRUCTED!` | `0x02018288` / `020180F8` |
| `0x0EC` | `H236` | `%s's HEADSHOT KILLED YOU!` | `0x02018288` / `0201818C` |
| `0x0ED` | `H237` | `%s KILLED YOU!` | `0x02018288` / `020181F4` |
| `0x0EF` | `H239` | `YOUR HEADSHOT KILLED %s!` | `0x02018288` / `02018588` |
| `0x0F0` | `H240` | `YOU KILLED A TEAMMATE, (%s)!` | `0x02018288` / `02018454` |
| `0x0F1` | `H241` | `%s is the new prime hunter!` | `0x02018288` / `020188F0` |
| `0x0F2` | `H242` | `the prime hunter is dead!` | `0x02018288` / `02018AA8` |
| `0x0F3` | `H243` | `you lost all your lives!\nyou're out of the game` | `0x0200FF18` / `0200F448` |
| `0x0F7` | `H247` | `FACE OFF!` | `0x0200FF18` / `0200FC08` |
| `0x0F8` | `H248` | `position revealed!` | `0x0200FF18` / `0200FD78` |
| `0x0F9` | `H249` | `RETURN TO BATTLE!` | `0x0200FF18` / `0200FDCC` |
| `0x0FC` | `H252` | `MORPH BALL BOMB` | `0x02018288` / `02018380` |
| `0x0FE` | `H254` | `YOU KILLED 5 IN A ROW!` | `0x02018288` / `02018708` |
| `0x0FF` | `H255` | `%s KILLED 5 IN A ROW!` | `0x02018288` / `02018770` |
| `0x101` | `H257` | `octolith reset!` | `0x0212D538` or `0x0212D8C4` |

確認した色ソース:

| Color Definition | Default | Enqueue Call Site(s) | Patch Pattern | Notes |
| --- | --- | --- | --- | --- |
| `0x02031BF8` | `0x3FEF` | `02031B3C` | `02031BF8 0000CCCC` | `0xCD = H205 = acquiring\nnode`。 |
| `0x0202DE2C` | `0x3FEF` | `0202D7C4`, `0202D824`, `0202D884` | `0202DE2C 0000CCCC` | `0xCA/0xCB/0xCE = H202/H203/H206`。 |
| `0x0202DE44` | `0x295F` | `0202DA90` | `0202DE44 0000CCCC` | `0x09 = H009 = AMMO DEPLETED!`。no-ammo系の最小patch候補。 |
| `0x0202D8B0` | immediate `0x001F` | `0202D8EC` | instruction patch | `0xD3 = H211 = node stolen`。literalではなく `mov r2,#0x1F`。 |
| `0x0200FF18` | `0x3FEF` | `0200F448`, `0200FC08`, `0200FD78`, `0200FDCC` | `0200FF18 0000CCCC` | `0xF3/0xF7/0xF8/0xF9 = H243/H247/H248/H249`。 |
| `0x02018288` | `0x3FEF` | `02017508`, `020180F8`, `0201818C`, `020181F4`, `02018380`, `02018454`, `02018588`, `02018708`, `02018770`, `020188F0`, `02018AA8` | `02018288 0000CCCC` | `H228/H235/H236/H237/H252/H240/H239/H254/H255/H241/H242` の通知群。 |
| `0x0203051C` | `0x3FEF` | `020304E4` | `0203051C 0000CCCC` | `0xEA = H234 = COWARD DETECTED!`。 |
| `0x0210FE2C` | `0x3FEF` | `0210FB80` | `0210FE2C 0000CCCC` | `0xE9 = H233 = turret energy: %d`。full側追加領域。 |
| `0x0212D538` | `0x3FEF` | `0212D51C` | `0212D538 0000CCCC` | text message id `0xC9/0xCF/0x101`。`0x82/0x81` は別関数 `02071F24` へ渡すイベント系id。 |
| `0x0212D8C4` | `0x3FEF` | `0212D7A8`, `0212D848` | `0212D8C4 0000CCCC` | text message id `0xCF/0xE7` または `0x101/0xE5`。 |
| `0x0212DA2C` | `0x3FEF` | `0212D9B0` | `0212DA2C 0000CCCC` | message id `0xC9` または `0xCF`。条件分岐で切替。 |
| `0x0212E28C` | `0x3FEF` | `0212E1E8` | `0212E28C 0000CCCC` | message id `0xE8`。full側追加領域の通知。 |

literal patchの考え方:

- `0x02031BF8` のようなliteralは `ldr r1,[r15,#...]` で32-bit wordとして読まれるので、基本は32-bit writeで `0000CCCC` を入れる。
- downstreamでは `0204BCE8` がhalfwordとして保存するため上位16bitは実質不要だが、ゼロにしておく方が安全。
- no-ammo系だけを狙うならUS1.0では `0202DE44` から試す。`02031BF8` は `acquiring\nnode` 用。`0202DE2C` や `02018288` は複数メッセージをまとめて変える。
- `0204BCE8` 本体を命令patchして色を強制する方法もあるが、全OSDへ効いてしまう上、ARM即値で表せる色に制約が出る。個別OSDの最小patchとしてはliteral pool変更の方が安全。

## Direct Enqueue Calls

| Enqueue Call | Message ID | Color Definition | Default | Timer | Flags | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `0200F448` | `0xF3` | `0x0200FF18` | `0x3FEF` | `0x01` | `0x00` | `Y=0x98000`。 |
| `0200FC08` | `0xF7` | `0x0200FF18` | `0x3FEF` | `0x01` | `0x00` | `Y=0xAA000`。 |
| `0200FD78` | `0xF8` | `0x0200FF18` | `0x3FEF` | `0x01` | `0x00` | `Y=0x96000`。 |
| `0200FDCC` | `0xF9` | `0x0200FF18` | `0x3FEF` | `0x01` | `0x00` | `Y=0xA0000`。 |
| `02017508` | `0xE4` | `0x02018288` | `0x3FEF` | `0x14` | `0x00` | `Y=0x28000`。 |
| `020180F8` | `0xEB` | `0x02018288` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `0201818C` | `0xEC` | `0x02018288` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `020181F4` | `0xED` | `0x02018288` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018380` | `0xFC` | `0x02018288` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018454` | `0xF0` | `0x02018288` | `0x3FEF` | `0x3C` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018588` | `0xEF` | `0x02018288` | `0x3FEF` | `0x3C` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018708` | `0xFE` | `0x02018288` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018770` | `0xFF` | `0x02018288` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `020188F0` | `0xF1` | `0x02018288` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `02018AA8` | `0xF2` | `0x02018288` | `0x3FEF` | `0x5A` | `0x02` | `Y=0x46000`, wrap `0x8C`。 |
| `0202D7C4` | `0xCA` | `0x0202DE2C` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D824` | `0xCB` | `0x0202DE2C` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D884` | `0xCE` | `0x0202DE2C` | `0x3FEF` | `0x5A` | `0x01` | shared notification literal。 |
| `0202D8EC` | `0xD3` | `0202D8B0: mov r2,#0x1F` | `0x001F` | `0x5A` | `0x11` | literalなし。赤系の即値色。 |
| `0202DA90` | `0x09` | `0x0202DE44` | `0x295F` | `0x2D` | `0x01` | `H009 = AMMO DEPLETED!`。defaultだけ赤/コーラル系。 |
| `020304E4` | `0xEA` | `0x0203051C` | `0x3FEF` | `0x3C` | `0x00` | 単独literal。 |
| `02031B3C` | `0xCD` | `0x02031BF8` | `0x3FEF` | `0x2D` | `0x11` | `H205 = acquiring\nnode`。 |
| `0210FB80` | `0xE9` | `0x0210FE2C` | `0x3FEF` | `0x01` | `0x00` | full側追加領域。 |
| `0212D51C` | `0xC9/0xCF/0x101` | `0x0212D538` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。`0x82/0x81` は `02071F24` 側のid。 |
| `0212D7A8` | `0xCF/0xE7` | `0x0212D8C4` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。`0x81/0x80` は `02071F24` 側のid。 |
| `0212D848` | `0x101/0xE5` | `0x0212D8C4` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。`0x82/0x80` は `02071F24` 側のid。 |
| `0212D9B0` | `0xC9/0xCF` | `0x0212DA2C` | `0x3FEF` | `0x3C` | `0x01` | 自playerなら `0xC9`、それ以外は `0xCF`。 |
| `0212E1E8` | `0xE8` | `0x0212E28C` | `0x3FEF` | `0x01` | `0x00` | `Y=0x32000`。full側追加領域。 |

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
