# In-Game-OSD-Struct-EU1_0

`mphDump/EU1_0.txt` で確認したEU1.0版のin-game OSD message構造と色定義メモ。

JP1.0版の対応メモは [`In-Game-OSD-Struct-JP1_0.md`](In-Game-OSD-Struct-JP1_0.md)。
US1.0版の対応メモは [`In-Game-OSD-Struct-US1_0.md`](In-Game-OSD-Struct-US1_0.md)。
EU1.1版の対応メモは [`In-Game-OSD-Struct-EU1_1.md`](In-Game-OSD-Struct-EU1_1.md)。

## EU1.0 Runtime Layout

EU1.0のOSD enqueue相当は `0204C4EC`。呼び出し元が `stack+0x04` に置いた色をslotの `entry+0x10` へ保存する。

EU1.0はJP1.0/KR1.0と同じ `0x9C` stride。US1.0の `0x80` strideとは違うので、US用slot直書きpatchをそのまま移植しないこと。

| Item | EU1.0 Value | Notes |
| --- | --- | --- |
| OSD enqueue / setup function | `0204C4EC` | JP1.0 `0204D748` 相当。 |
| OSD draw loop | `0204C3D8` | slotを20件走査。 |
| Has active by flag mask | `0204C654` | `entry+0x15 & mask` のactive slot確認。 |
| Clear by flag mask | `0204C694` | `entry+0x15 & mask` のactive slotを消す。 |
| Clear all OSD timers | `0204C6D0` | 全slotの `entry+0x12` を0にする。 |
| OSD MSG base | `0x020E44F4` | slot 1 entry base。 |
| Entry stride | `0x9C` | JP1.0/KR1.0と同じ。 |
| Font pointer source | `0x020DEFA0 -> 0x020CBF7C` | enqueue callerが `stack+0x0C` に置く。 |
| Color offset | `entry+0x10` | 16-bit BGR555。 |
| Timer offset | `entry+0x12` | nonzeroでactive。 |
| Alpha offset | `entry+0x14` | 通常 `0x1F`。 |
| Flags offset | `entry+0x15` | blink/group flags。 |
| Wrap offset | `entry+0x16` | layout width。 |
| Align offset | `entry+0x18` | text alignment。 |
| Text buffer | `entry+0x1C` | `+0x9B` が終端保証。 |

slot直書き用の色アドレス:

```text
color_addr = 0x020E44F4 + (slot - 1) * 0x9C + 0x10
```

EU1.0の例:

| Slot | Color Address | Halfword Patch Example |
| --- | --- | --- |
| 1 | `0x020E4504` | `120E4504 00003FEF` |
| 2 | `0x020E45A0` | `120E45A0 00003FEF` |
| 3 | `0x020E463C` | `120E463C 00003FEF` |
| 4 | `0x020E46D8` | `120E46D8 00003FEF` |
| 5 | `0x020E4774` | `120E4774 00003FEF` |

## Message Text Lookup

EU1.0でOSD呼び出し元が渡す `message id` は `0203C2D8` で文字列ポインタへ変換される。

keyはmessage idの10進値で作る。たとえば `0xCA` は10進 `202` なので `H202`、`0xCD` は10進 `205` なので `H205`。dump上の4-byte key wordはlittle-endian表示の都合で `48323032` のように見えるが、論理的には `H202`。

| Message ID Range | Table Header | File / Prefix | Notes |
| --- | --- | --- | --- |
| `< 0x0C` | `0x020DF078` | `HudMsgsCommon.bin`, `H###` | static dumpでは `count=0x0B`, table `0x02287C44`。 |
| `0x0C..0x7A` | `0x020DF070` | `HudMessagesSP.bin`, `H###` | single-player側ロード時に使う領域。static dumpでは未ロード。 |
| `0x7B..0x101` | `0x020DF098` | `HudMessagesMP.bin`, `H###` | static dumpでは `count=0x64`, table `0x02287D64`。 |
| `0x12C..0x131` | `0x020DF098` | `HudMessagesMP.bin`, `W###` | `id - 0x12C` を `W###` として引く。 |

EU1.0のstatic dumpでロード済みのcommon/MP tableは英語。SP tableは未ロードなので、`H066` や `H121` などSP側候補の本文はこのdumpだけでは確定しない。

OSD色調査で出てきたmessage idとEU1.0文字列:

| Message ID | Key | Text | Color Definition / Call |
| --- | --- | --- | --- |
| `0x009` | `H009` | `AMMO DEPLETED!` | `0x0202DE3C` / `0202DA88` |
| `0x042` | `H066` | SP table not loaded in static dump | `0x02018280` / `020180F0` |
| `0x079` | `H121` | SP table not loaded in static dump | `0x02018280` / `02017500` |
| `0x0C9` | `H201` | `your octolith reset!` | `0x0212E018`, `0x0212E3A4`, or `0x0212E50C` |
| `0x0CA` | `H202` | `return to base` | `0x0202DE24` / `0202D7BC` |
| `0x0CB` | `H203` | `bounty received` | `0x0202DE24` / `0202D81C` |
| `0x0CC` | `H204` | `progress` | `0x02031BA0` / `02031AE4` |
| `0x0CD` | `H205` | `acquiring\nnode` | `0x02031BA0` / `02031AE4` |
| `0x0CE` | `H206` | `complete` | `0x0202DE24` / `0202D87C` |
| `0x0CF` | `H207` | `enemy octolith reset!` | `0x0212E018`, `0x0212E3A4`, or `0x0212E50C` |
| `0x0D3` | `H211` | `node stolen` | `0202D8A8: mov r2,#0x1F` / `0202D8E4` |
| `0x0E4` | `H228` | `HEADSHOT!` | `0x02018280` / `02017500` |
| `0x0E5` | `H229` | `the octolith\nhas been dropped!` | `0x0212E3A4` / `0212E328` |
| `0x0E6` | `H230` | `the enemy\ndropped your octolith!` | `0x0212E3A4` / `0212E288` |
| `0x0E7` | `H231` | `your team\ndropped the octolith!` | `0x0212E3A4` / `0212E288` |
| `0x0E8` | `H232` | `your octolith is missing!` | `0x0212ED6C` / `0212ECC8` |
| `0x0E9` | `H233` | `turret energy: %d` | `0x0211090C` / `02110660` |
| `0x0EA` | `H234` | `COWARD DETECTED!` | `0x020304E0` / `020304A8` |
| `0x0EB` | `H235` | `YOU SELF-DESTRUCTED!` | `0x02018280` / `020180F0` |
| `0x0EC` | `H236` | `%s's HEADSHOT KILLED YOU!` | `0x02018280` / `02018184` |
| `0x0ED` | `H237` | `%s KILLED YOU!` | `0x02018280` / `020181EC` |
| `0x0EE` | `H238` | `YOU KILLED %s!` | `0x02018280` / `02018580` |
| `0x0EF` | `H239` | `YOUR HEADSHOT KILLED %s!` | `0x02018280` / `02018580` |
| `0x0F0` | `H240` | `YOU KILLED A TEAMMATE, (%s)!` | `0x02018280` / `0201844C` |
| `0x0F1` | `H241` | `%s is the new prime hunter!` | `0x02018280` / `020188E8` |
| `0x0F2` | `H242` | `the prime hunter is dead!` | `0x02018280` / `02018AA0` |
| `0x0F3` | `H243` | `you lost all your lives!\nyou're out of the game` | `0x0200FF10` / `0200F44C` |
| `0x0F7` | `H247` | `FACE OFF!` | `0x0200FF10` / `0200FC00` |
| `0x0F8` | `H248` | `position revealed!` | `0x0200FF10` / `0200FD70` |
| `0x0F9` | `H249` | `RETURN TO BATTLE!` | `0x0200FF10` / `0200FDC4` |
| `0x0FA` | `H250` | `DEATHALT` | `0x02018280` / `02018378` |
| `0x0FB` | `H251` | `MAGMAUL BURN` | `0x02018280` / `02018378` |
| `0x0FC` | `H252` | `MORPH BALL BOMB` | `0x02018280` / `02018378` |
| `0x0FD` | `H253` | `HALFTURRET SLICE` | `0x02018280` / `02018378` |
| `0x0FE` | `H254` | `YOU KILLED 5 IN A ROW!` | `0x02018280` / `02018700` |
| `0x0FF` | `H255` | `%s KILLED 5 IN A ROW!` | `0x02018280` / `02018768` |
| `0x101` | `H257` | `octolith reset!` | `0x0212E018` or `0x0212E3A4` |

## Color Source Investigation

EU1.0の `0204C4EC` への直callは28件あり、色定義は `11 literal + 1 immediate` の12箇所に集約できる。

`0204C4EC` 本体に「OSD全体の色定義アドレス」はない。各呼び出し元が `stack+0x04` へ色を置き、`0204C4EC` がそれを `entry+0x10` に保存する。最小行数patchは、目的のOSD呼び出し元のliteral poolを書き換える形が基本。

特にno-ammo系は `0202DA38-0202DA88` のブロックで、`0202DA38` が message id `0x09` をセットし、`0202DA48` が色literal `0x0202DE3C = 0x0000295F` を読む。ここがEU1.0の最小patch候補。

```text
; EU1.0: message id 0x09 / H009 / "AMMO DEPLETED!" color -> cyan
0202DE3C 00007FE0

; EU1.0: same target -> pure red
0202DE3C 0000001F

; EU1.0: message id 0xCC/0xCD / H204/H205 color -> cyan
02031BA0 00007FE0

; EU1.0: shared 0xCA/0xCB/0xCE notification color -> cyan
0202DE24 00007FE0
```

確認した色ソース:

| Color Definition | Default | Enqueue Call Site(s) | Patch Pattern | Notes |
| --- | --- | --- | --- | --- |
| `0x0200FF10` | `0x3FEF` | `0200F44C`, `0200FC00`, `0200FD70`, `0200FDC4` | `0200FF10 0000CCCC` | `H243/H247/H248/H249`。 |
| `0x02018280` | `0x3FEF` | `02017500`, `020180F0`, `02018184`, `020181EC`, `02018378`, `0201844C`, `02018580`, `02018700`, `02018768`, `020188E8`, `02018AA0` | `02018280 0000CCCC` | kill/death通知群。 |
| `0x0202DE24` | `0x3FEF` | `0202D7BC`, `0202D81C`, `0202D87C` | `0202DE24 0000CCCC` | `0xCA/0xCB/0xCE = H202/H203/H206`。 |
| `0x0202D8A8` | immediate `0x001F` | `0202D8E4` | instruction patch | `0xD3 = H211 = node stolen`。literalではなく `mov r2,#0x1F`。 |
| `0x0202DE3C` | `0x295F` | `0202DA88` | `0202DE3C 0000CCCC` | `0x09 = H009 = AMMO DEPLETED!`。no-ammo系の最小patch候補。 |
| `0x020304E0` | `0x3FEF` | `020304A8` | `020304E0 0000CCCC` | `0xEA = H234 = COWARD DETECTED!`。 |
| `0x02031BA0` | `0x3FEF` | `02031AE4` | `02031BA0 0000CCCC` | `0xCC/0xCD = H204/H205`。 |
| `0x0211090C` | `0x3FEF` | `02110660` | `0211090C 0000CCCC` | `0xE9 = H233 = turret energy: %d`。full側追加領域。 |
| `0x0212E018` | `0x3FEF` | `0212DFFC` | `0212E018 0000CCCC` | message id `0xC9/0xCF/0x101`。 |
| `0x0212E3A4` | `0x3FEF` | `0212E288`, `0212E328` | `0212E3A4 0000CCCC` | message id `0xC9/0xE6/0xCF/0xE7` または `0x101/0xE5`。 |
| `0x0212E50C` | `0x3FEF` | `0212E490` | `0212E50C 0000CCCC` | message id `0xC9/0xCF`。 |
| `0x0212ED6C` | `0x3FEF` | `0212ECC8` | `0212ED6C 0000CCCC` | `0xE8 = H232 = your octolith is missing!`。 |

literal patchの考え方:

- `0x0202DE3C` のようなliteralは `ldr r1,[r15,#...]` で32-bit wordとして読まれるので、基本は32-bit writeで `0000CCCC` を入れる。
- downstreamでは `0204C4EC` がhalfwordとして保存するため上位16bitは実質不要だが、ゼロにしておく方が安全。
- no-ammo系だけを狙うなら `0202DE3C` から試す。`02031BA0` は `progress` / `acquiring\nnode` 用。`0202DE24` や `02018280` は複数メッセージをまとめて変える。
- `0204C4EC` 本体を命令patchして色を強制する方法もあるが、全OSDへ効いてしまう上、ARM即値で表せる色に制約が出る。個別OSDの最小patchとしてはliteral pool変更の方が安全。

## Direct Enqueue Calls

| Enqueue Call | Message ID | Color Definition | Default | Timer | Flags | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `0200F44C` | `0xF3` | `0x0200FF10` | `0x3FEF` | `0x01` | `0x00` | `H243`。 |
| `0200FC00` | `0xF7` | `0x0200FF10` | `0x3FEF` | `0x01` | `0x00` | `H247 = FACE OFF!`。 |
| `0200FD70` | `0xF8` | `0x0200FF10` | `0x3FEF` | `0x01` | `0x00` | `H248 = position revealed!`。 |
| `0200FDC4` | `0xF9` | `0x0200FF10` | `0x3FEF` | `0x01` | `0x00` | `H249 = RETURN TO BATTLE!`。 |
| `02017500` | `0x79/0xE4` | `0x02018280` | `0x3FEF` | `0x14` | `0x00` | `H121` はSP table未ロード、`H228 = HEADSHOT!`。 |
| `020180F0` | `0x42/0xEB` | `0x02018280` | `0x3FEF` | `0x5A` | `0x02` | `H066` はSP table未ロード、`H235 = YOU SELF-DESTRUCTED!`。 |
| `02018184` | `0xEC` | `0x02018280` | `0x3FEF` | `0x5A` | `0x02` | `H236 = %s's HEADSHOT KILLED YOU!`。 |
| `020181EC` | `0xED` | `0x02018280` | `0x3FEF` | `0x5A` | `0x02` | `H237 = %s KILLED YOU!`。 |
| `02018378` | `0xFA/0xFB/0xFC/0xFD` or dynamic | `0x02018280` | `0x3FEF` | `0x5A` | `0x02` | death source表示。weapon name table経由の動的文字列もあり。 |
| `0201844C` | `0xF0` | `0x02018280` | `0x3FEF` | `0x3C` | `0x02` | `H240 = YOU KILLED A TEAMMATE, (%s)!`。 |
| `02018580` | `0xEE/0xEF` | `0x02018280` | `0x3FEF` | `0x3C` | `0x02` | `H238/H239`。 |
| `02018700` | `0xFE` | `0x02018280` | `0x3FEF` | `0x5A` | `0x02` | `H254 = YOU KILLED 5 IN A ROW!`。 |
| `02018768` | `0xFF` | `0x02018280` | `0x3FEF` | `0x5A` | `0x02` | `H255 = %s KILLED 5 IN A ROW!`。 |
| `020188E8` | `0xF1` | `0x02018280` | `0x3FEF` | `0x5A` | `0x02` | `H241 = %s is the new prime hunter!`。 |
| `02018AA0` | `0xF2` | `0x02018280` | `0x3FEF` | `0x5A` | `0x02` | `H242 = the prime hunter is dead!`。 |
| `0202D7BC` | `0xCA` | `0x0202DE24` | `0x3FEF` | `0x5A` | `0x01` | `H202 = return to base`。 |
| `0202D81C` | `0xCB` | `0x0202DE24` | `0x3FEF` | `0x5A` | `0x01` | `H203 = bounty received`。 |
| `0202D87C` | `0xCE` | `0x0202DE24` | `0x3FEF` | `0x5A` | `0x01` | `H206 = complete`。 |
| `0202D8E4` | `0xD3` | `0202D8A8: mov r2,#0x1F` | `0x001F` | `0x5A` | `0x11` | `H211 = node stolen`。 |
| `0202DA88` | `0x09` | `0x0202DE3C` | `0x295F` | `0x2D` | `0x01` | `H009 = AMMO DEPLETED!`。 |
| `020304A8` | `0xEA` | `0x020304E0` | `0x3FEF` | `0x3C` | `0x00` | `H234 = COWARD DETECTED!`。 |
| `02031AE4` | `0xCC/0xCD` | `0x02031BA0` | `0x3FEF` | `0x2D` | `0x11` | `H204/H205`。 |
| `02110660` | `0xE9` | `0x0211090C` | `0x3FEF` | `0x01` | `0x00` | `H233 = turret energy: %d`。 |
| `0212DFFC` | `0xC9/0xCF/0x101` | `0x0212E018` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。 |
| `0212E288` | `0xC9/0xE6/0xCF/0xE7` | `0x0212E3A4` | `0x3FEF` | `0x3C` | `0x01` | `H201/H230/H207/H231`。 |
| `0212E328` | `0x101/0xE5` | `0x0212E3A4` | `0x3FEF` | `0x3C` | `0x01` | `H257/H229`。 |
| `0212E490` | `0xC9/0xCF` | `0x0212E50C` | `0x3FEF` | `0x3C` | `0x01` | `H201/H207`。 |
| `0212ECC8` | `0xE8` | `0x0212ED6C` | `0x3FEF` | `0x01` | `0x00` | `H232 = your octolith is missing!`。 |

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
