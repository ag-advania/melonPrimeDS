# In-Game-OSD-Struct-KR1_0

`mphDump/KR1_0.txt` で確認したKR1.0版のin-game OSD message構造と色定義メモ。

JP1.0版の対応メモは [`In-Game-OSD-Struct-JP1_0.md`](In-Game-OSD-Struct-JP1_0.md)。
US1.0版の対応メモは [`In-Game-OSD-Struct-US1_0.md`](In-Game-OSD-Struct-US1_0.md)。

## KR1.0 Runtime Layout

KR1.0のOSD enqueue相当は `02045910`。呼び出し元が `stack+0x04` に置いた色をslotの `entry+0x10` へ保存する。

KR1.0はJP1.0と同じ `0x9C` stride。US1.0の `0x80` strideとは違うので、US用slot直書きpatchをそのまま移植しないこと。

| Item | KR1.0 Value | Notes |
| --- | --- | --- |
| OSD enqueue / setup function | `02045910` | JP1.0 `0204D748` 相当。 |
| OSD draw loop | `02045A7C` | slotを20件走査。 |
| Clear all OSD timers | `02045868` | 全slotの `entry+0x12` を0にする。 |
| Clear by flag mask | `0204589C` | `entry+0x15 & mask` のactive slotを消す。 |
| Has active by flag mask | `020458D4` | active slot確認。 |
| OSD MSG base | `0x020DCD34` | slot 1 entry base。 |
| Entry stride | `0x9C` | JP1.0と同じ。 |
| Font pointer source | `0x020D7DA8 -> 0x020C4D40` | enqueue callerが `stack+0x0C` に置く。 |
| Color offset | `entry+0x10` | 16-bit BGR555。 |
| Timer offset | `entry+0x12` | nonzeroでactive。 |
| Alpha offset | `entry+0x14` | 通常 `0x1F`。 |
| Flags offset | `entry+0x15` | blink/group flags。 |
| Wrap offset | `entry+0x16` | layout width。 |
| Align offset | `entry+0x18` | text alignment。 |
| Text buffer | `entry+0x1C` | `+0x9B` が終端保証。 |

slot直書き用の色アドレス:

```text
color_addr = 0x020DCD34 + (slot - 1) * 0x9C + 0x10
```

KR1.0の例:

| Slot | Color Address | Halfword Patch Example |
| --- | --- | --- |
| 1 | `0x020DCD44` | `120DCD44 00003FEF` |
| 2 | `0x020DCDE0` | `120DCDE0 00003FEF` |
| 3 | `0x020DCE7C` | `120DCE7C 00003FEF` |
| 4 | `0x020DCF18` | `120DCF18 00003FEF` |
| 5 | `0x020DCFB4` | `120DCFB4 00003FEF` |

## Message Text Lookup

KR1.0でOSD呼び出し元が渡す `message id` は `02028D70` で文字列ポインタへ変換される。

keyはmessage idの10進値で作る。たとえば `0xCA` は10進 `202` なので `H202`、`0xCD` は10進 `205` なので `H205`。dump上の4-byte key wordはlittle-endian表示の都合で `48323032` のように見えるが、論理的には `H202`。

| Message ID Range | Table Header | File / Prefix | Notes |
| --- | --- | --- | --- |
| `< 0x0C` | `0x020D7E78` | `HudMsgsCommon.bin`, `H###` | static dumpでは `count=0x10`, table `0x02279EC4`。 |
| `0x0C..0x7A` | `0x020D7E80` | `HudMessagesSP.bin`, `H###` | single-player側ロード時に使う領域。static dumpでは未ロード。 |
| `0x7B..0x101` | `0x020D7E70` | `HudMessagesMP.bin`, `H###` | static dumpでは `count=0x64`, table `0x0227A064`。 |
| `0x12C..0x131` | `0x020D7E70` | `HudMessagesMP.bin`, `W###` | `id - 0x12C` を `W###` として引く。 |
| `0x190..0x199` | `0x020D7E80` | `HudMessagesSP.bin`, `V###` | KR1.0のlookupにある追加範囲。static dumpでは未ロード。 |

KR1.0のOSD本文は、少なくともstatic dumpで見えているcommon/MP tableでは韓国語ではなくASCII英語。US1.0と近いが、`H009 = AMMO ZERO!`、`H234 = HUNTER DETECTED!`、`H247 = LAST BATTLE!` など文言差分がある。

OSD色調査で出てきたmessage idとKR文字列:

| Message ID | Key | Text | Color Definition / Call |
| --- | --- | --- | --- |
| `0x009` | `H009` | `AMMO ZERO!` | `0x02036AD4` / `0203679C` |
| `0x042` | `H066` | SP table not loaded in static dump | `0x0201A7F8` / `0201A640` |
| `0x079` | `H121` | SP table not loaded in static dump | `0x0201A7F8` / `02019AA0` |
| `0x0C9` | `H201` | `your octolith reset!` | `0x02124D6C` or `0x021251C4` |
| `0x0CA` | `H202` | `return to base` | `0x02036AC8` / `02036518` |
| `0x0CB` | `H203` | `bounty received` | `0x02036AC8` / `02036574` |
| `0x0CD` | `H205` | `acquiring\nnode` | `0x0203285C` / `020327B8` |
| `0x0CE` | `H206` | `complete` | `0x02036AC8` / `020365D0` |
| `0x0CF` | `H207` | `enemy octolith reset!` | `0x02124D6C`, `0x02124F98`, or `0x021251C4` |
| `0x0D3` | `H211` | `node stolen` | `020365F8: mov r2,#0x1F` / `02036634` |
| `0x0E4` | `H228` | `HEADSHOT!` | `0x0201A7F8` / `02019AA0` |
| `0x0E5` | `H229` | `the octolith\nhas been dropped!` | `0x02124F98` / `02124F24` |
| `0x0E6` | `H230` | `the enemy\ndropped your octolith!` | `0x02124F98` / `02124E84` |
| `0x0E7` | `H231` | `your team\ndropped the octolith!` | `0x02124F98` / `02124E84` |
| `0x0E8` | `H232` | `your octolith is missing!` | `0x02126268` / `021261C8` |
| `0x0E9` | `H233` | `turret energy: %d` | `0x02108C28` / `02108988` |
| `0x0EA` | `H234` | `HUNTER DETECTED!` | `0x02033D98` / `02033D6C` |
| `0x0EB` | `H235` | `YOU SELF-DESTRUCTED!` | `0x0201A7F8` / `0201A640` |
| `0x0EC` | `H236` | `%s's HEADSHOT KILLED YOU!` | `0x0201A7F8` / `0201A6D0` |
| `0x0ED` | `H237` | `%s KILLED YOU!` | `0x0201A7F8` / `0201A738` |
| `0x0EE` | `H238` | `YOU KILLED %s!` | `0x0201A7F8` / `0201AAC8` |
| `0x0EF` | `H239` | `YOUR HEADSHOT KILLED %s!` | `0x0201A7F8` / `0201AAC8` |
| `0x0F0` | `H240` | `YOU KILLED A TEAMMATE, (%s)!` | `0x0201A7F8` / `0201A994` |
| `0x0F1` | `H241` | `%s is the new prime hunter!` | `0x0201A7F8` / `0201AE28` |
| `0x0F2` | `H242` | `the prime hunter is dead!` | `0x0201A7F8` / `0201AFE0` |
| `0x0F3` | `H243` | `you lost all your lives!\nyou're out of the game` | `0x02021844` / `02020DFC` |
| `0x0F7` | `H247` | `LAST BATTLE!` | `0x02021844` / `02021570` |
| `0x0F8` | `H248` | `EMERGENCY!` | `0x02021844` / `020216DC` |
| `0x0F9` | `H249` | `RETURN TO BATTLE!` | `0x02021844` / `02021730` |
| `0x0FA` | `H250` | `DEATHTRANS` | `0x0201A7F8` / `0201A8C0` |
| `0x0FB` | `H251` | `MAGMAUL BURN` | `0x0201A7F8` / `0201A8C0` |
| `0x0FC` | `H252` | `MORPH BALL BOMB` | `0x0201A7F8` / `0201A8C0` |
| `0x0FD` | `H253` | `HALFTURRET SLICE` | `0x0201A7F8` / `0201A8C0` |
| `0x0FE` | `H254` | `YOU KILLED 5 IN A ROW!` | `0x0201A7F8` / `0201AC40` |
| `0x0FF` | `H255` | `%s KILLED 5 IN A ROW!` | `0x0201A7F8` / `0201ACA8` |
| `0x101` | `H257` | `octolith reset!` | `0x02124F98` or `0x021251C4` |

## Color Source Investigation

KR1.0の `02045910` への直callは28件あり、色定義は `11 literal + 1 immediate` の12箇所に集約できる。

`02045910` 本体に「OSD全体の色定義アドレス」はない。各呼び出し元が `stack+0x04` へ色を置き、`02045910` がそれを `entry+0x10` に保存する。最小行数patchは、目的のOSD呼び出し元のliteral poolを書き換える形が基本。

特にno-ammo系は `0203674C-0203679C` のブロックで、`0203674C` が message id `0x09` をセットし、`0203675C` が色literal `0x02036AD4 = 0x0000295F` を読む。ここがKR1.0の最小patch候補。

```text
; KR1.0: message id 0x09 / H009 / "AMMO ZERO!" color -> cyan
02036AD4 00007FE0

; KR1.0: same target -> pure red
02036AD4 0000001F

; KR1.0: message id 0xCD / H205 / "acquiring\nnode" color -> cyan
0203285C 00007FE0

; KR1.0: shared 0xCA/0xCB/0xCE notification color -> cyan
02036AC8 00007FE0
```

確認した色ソース:

| Color Definition | Default | Enqueue Call Site(s) | Patch Pattern | Notes |
| --- | --- | --- | --- | --- |
| `0x0201A7F8` | `0x3FEF` | `02019AA0`, `0201A640`, `0201A6D0`, `0201A738`, `0201A8C0`, `0201A994`, `0201AAC8`, `0201AC40`, `0201ACA8`, `0201AE28`, `0201AFE0` | `0201A7F8 0000CCCC` | kill/death通知群。 |
| `0x02021844` | `0x3FEF` | `02020DFC`, `02021570`, `020216DC`, `02021730` | `02021844 0000CCCC` | `H243/H247/H248/H249`。 |
| `0x0203285C` | `0x3FEF` | `020327B8` | `0203285C 0000CCCC` | `0xCD = H205 = acquiring\nnode`。 |
| `0x02033D98` | `0x3FEF` | `02033D6C` | `02033D98 0000CCCC` | `0xEA = H234 = HUNTER DETECTED!`。 |
| `0x02036AC8` | `0x3FEF` | `02036518`, `02036574`, `020365D0` | `02036AC8 0000CCCC` | `0xCA/0xCB/0xCE = H202/H203/H206`。 |
| `0x020365F8` | immediate `0x001F` | `02036634` | instruction patch | `0xD3 = H211 = node stolen`。literalではなく `mov r2,#0x1F`。 |
| `0x02036AD4` | `0x295F` | `0203679C` | `02036AD4 0000CCCC` | `0x09 = H009 = AMMO ZERO!`。no-ammo系の最小patch候補。 |
| `0x02108C28` | `0x3FEF` | `02108988` | `02108C28 0000CCCC` | `0xE9 = H233 = turret energy: %d`。full側追加領域。 |
| `0x02124D6C` | `0x3FEF` | `02124D00` | `02124D6C 0000CCCC` | message id `0xC9/0xCF`。 |
| `0x02124F98` | `0x3FEF` | `02124E84`, `02124F24` | `02124F98 0000CCCC` | message id `0xE6/0xE7` または `0x101/0xE5`。 |
| `0x021251C4` | `0x3FEF` | `021251AC` | `021251C4 0000CCCC` | message id `0xC9/0xCF/0x101`。 |
| `0x02126268` | `0x3FEF` | `021261C8` | `02126268 0000CCCC` | `0xE8 = H232 = your octolith is missing!`。 |

literal patchの考え方:

- `0x02036AD4` のようなliteralは `ldr r1,[r15,#...]` で32-bit wordとして読まれるので、基本は32-bit writeで `0000CCCC` を入れる。
- downstreamでは `02045910` がhalfwordとして保存するため上位16bitは実質不要だが、ゼロにしておく方が安全。
- no-ammo系だけを狙うなら `02036AD4` から試す。`0203285C` は `acquiring\nnode` 用。`02036AC8` や `0201A7F8` は複数メッセージをまとめて変える。
- `02045910` 本体を命令patchして色を強制する方法もあるが、全OSDへ効いてしまう上、ARM即値で表せる色に制約が出る。個別OSDの最小patchとしてはliteral pool変更の方が安全。

## Direct Enqueue Calls

| Enqueue Call | Message ID | Color Definition | Default | Timer | Flags | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `02019AA0` | `0x79/0xE4` | `0x0201A7F8` | `0x3FEF` | `0x14` | `0x00` | `H121` はSP table未ロード、`H228 = HEADSHOT!`。 |
| `0201A640` | `0x42/0xEB` | `0x0201A7F8` | `0x3FEF` | `0x5A` | `0x02` | `H066` はSP table未ロード、`H235 = YOU SELF-DESTRUCTED!`。 |
| `0201A6D0` | `0xEC` | `0x0201A7F8` | `0x3FEF` | `0x5A` | `0x02` | `H236 = %s's HEADSHOT KILLED YOU!`。 |
| `0201A738` | `0xED` | `0x0201A7F8` | `0x3FEF` | `0x5A` | `0x02` | `H237 = %s KILLED YOU!`。 |
| `0201A8C0` | `0xFA/0xFB/0xFC/0xFD` or dynamic | `0x0201A7F8` | `0x3FEF` | `0x5A` | `0x02` | death source表示。weapon name table経由の動的文字列もあり。 |
| `0201A994` | `0xF0` | `0x0201A7F8` | `0x3FEF` | `0x3C` | `0x02` | `H240 = YOU KILLED A TEAMMATE, (%s)!`。 |
| `0201AAC8` | `0xEE/0xEF` | `0x0201A7F8` | `0x3FEF` | `0x3C` | `0x02` | `H238/H239`。 |
| `0201AC40` | `0xFE` | `0x0201A7F8` | `0x3FEF` | `0x5A` | `0x02` | `H254 = YOU KILLED 5 IN A ROW!`。 |
| `0201ACA8` | `0xFF` | `0x0201A7F8` | `0x3FEF` | `0x5A` | `0x02` | `H255 = %s KILLED 5 IN A ROW!`。 |
| `0201AE28` | `0xF1` | `0x0201A7F8` | `0x3FEF` | `0x5A` | `0x02` | `H241 = %s is the new prime hunter!`。 |
| `0201AFE0` | `0xF2` | `0x0201A7F8` | `0x3FEF` | `0x5A` | `0x02` | `H242 = the prime hunter is dead!`。 |
| `02020DFC` | `0xF3` | `0x02021844` | `0x3FEF` | `0x01` | `0x00` | `H243`。 |
| `02021570` | `0xF7` | `0x02021844` | `0x3FEF` | `0x01` | `0x00` | `H247 = LAST BATTLE!`。 |
| `020216DC` | `0xF8` | `0x02021844` | `0x3FEF` | `0x01` | `0x00` | `H248 = EMERGENCY!`。 |
| `02021730` | `0xF9` | `0x02021844` | `0x3FEF` | `0x01` | `0x00` | `H249 = RETURN TO BATTLE!`。 |
| `020327B8` | `0xCD` | `0x0203285C` | `0x3FEF` | `0x2D` | `0x11` | `H205 = acquiring\nnode`。 |
| `02033D6C` | `0xEA` | `0x02033D98` | `0x3FEF` | `0x3C` | `0x00` | `H234 = HUNTER DETECTED!`。 |
| `02036518` | `0xCA` | `0x02036AC8` | `0x3FEF` | `0x5A` | `0x01` | `H202 = return to base`。 |
| `02036574` | `0xCB` | `0x02036AC8` | `0x3FEF` | `0x5A` | `0x01` | `H203 = bounty received`。 |
| `020365D0` | `0xCE` | `0x02036AC8` | `0x3FEF` | `0x5A` | `0x01` | `H206 = complete`。 |
| `02036634` | `0xD3` | `020365F8: mov r2,#0x1F` | `0x001F` | `0x5A` | `0x11` | `H211 = node stolen`。 |
| `0203679C` | `0x09` | `0x02036AD4` | `0x295F` | `0x2D` | `0x01` | `H009 = AMMO ZERO!`。 |
| `02108988` | `0xE9` | `0x02108C28` | `0x3FEF` | `0x01` | `0x00` | `H233 = turret energy: %d`。 |
| `02124D00` | `0xC9/0xCF` | `0x02124D6C` | `0x3FEF` | `0x3C` | `0x01` | `H201/H207`。 |
| `02124E84` | `0xE6/0xCF/0xE7` | `0x02124F98` | `0x3FEF` | `0x3C` | `0x01` | `H230/H207/H231`。 |
| `02124F24` | `0x101/0xE5` | `0x02124F98` | `0x3FEF` | `0x3C` | `0x01` | `H257/H229`。 |
| `021251AC` | `0xC9/0xCF/0x101` | `0x021251C4` | `0x3FEF` | `0x3C` | `0x01` | `H201/H207/H257`。 |
| `021261C8` | `0xE8` | `0x02126268` | `0x3FEF` | `0x01` | dynamic | `H232 = your octolith is missing!`。 |

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
