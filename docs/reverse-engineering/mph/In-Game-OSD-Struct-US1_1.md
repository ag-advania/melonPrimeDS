# In-Game-OSD-Struct-US1_1

`mphDump/US1_1.txt` で確認したUS1.1版のin-game OSD message構造と色定義メモ。

JP1.0版の対応メモは [`In-Game-OSD-Struct-JP1_0.md`](In-Game-OSD-Struct-JP1_0.md)。
US1.0版の対応メモは [`In-Game-OSD-Struct-US1_0.md`](In-Game-OSD-Struct-US1_0.md)。
EU1.1版の対応メモは [`In-Game-OSD-Struct-EU1_1.md`](In-Game-OSD-Struct-EU1_1.md)。

## US1.1 Runtime Layout

US1.1のOSD enqueue相当は `0204C518`。呼び出し元が `stack+0x04` に置いた色をslotの `entry+0x10` へ保存する。

US1.1はUS1.0の `0x80` strideではなく、EU1.1と同じ `0x9C` stride。US1.0用slot直書きpatchをそのまま移植しないこと。

| Item | US1.1 Value | Notes |
| --- | --- | --- |
| OSD enqueue / setup function | `0204C518` | EU1.1と同じアドレス。 |
| OSD draw loop | `0204C404` | slotを20件走査。 |
| Has active by flag mask | `0204C680` | `entry+0x15 & mask` のactive slot確認。 |
| Clear by flag mask | `0204C6C0` | `entry+0x15 & mask` のactive slotを消す。 |
| Clear all OSD timers | `0204C6FC` | 全slotの `entry+0x12` を0にする。 |
| OSD MSG base | `0x020E44D4` | slot 1 entry base。 |
| Entry stride | `0x9C` | EU1.1と同じ。US1.0は `0x80`。 |
| Font pointer source | `0x020DEF80 -> 0x020CBF5C` | enqueue callerが `stack+0x0C` に置く。 |
| Color offset | `entry+0x10` | 16-bit BGR555。 |
| Timer offset | `entry+0x12` | nonzeroでactive。 |
| Alpha offset | `entry+0x14` | 通常 `0x1F`。 |
| Flags offset | `entry+0x15` | blink/group flags。 |
| Wrap offset | `entry+0x16` | layout width。 |
| Align offset | `entry+0x18` | text alignment。 |
| Text buffer | `entry+0x1C` | `+0x9B` が終端保証。 |

slot直書き用の色アドレス:

```text
color_addr = 0x020E44D4 + (slot - 1) * 0x9C + 0x10
```

US1.1の例:

| Slot | Color Address | Halfword Patch Example |
| --- | --- | --- |
| 1 | `0x020E44E4` | `120E44E4 00003FEF` |
| 2 | `0x020E4580` | `120E4580 00003FEF` |
| 3 | `0x020E461C` | `120E461C 00003FEF` |
| 4 | `0x020E46B8` | `120E46B8 00003FEF` |
| 5 | `0x020E4754` | `120E4754 00003FEF` |

全20slotの色だけを同じ値にするloop patch:

```text
; US1.1: all OSD slots color -> CCCC
D3000000 020E44D4
C0000000 00000013
10000010 0000CCCC
DC000000 0000009C
D2000000 00000000
```

## Message Text Lookup

US1.1でOSD呼び出し元が渡す `message id` は `0203C2E0` で文字列ポインタへ変換される。

keyはmessage idの10進値で作る。たとえば `0xCA` は10進 `202` なので `H202`、`0xCD` は10進 `205` なので `H205`。dump上の4-byte key wordはlittle-endian表示の都合で `48323032` のように見えるが、論理的には `H202`。

| Message ID Range | Table Header | File / Prefix | Notes |
| --- | --- | --- | --- |
| `< 0x0C` | `0x020DF058` | `HudMsgsCommon.bin`, `H###` | static dumpでは `count=0x0B`, table `0x02287B64`。 |
| `0x0C..0x7A` | `0x020DF050` | `HudMessagesSP.bin`, `H###` | single-player側ロード時に使う領域。static dumpでは未ロード。 |
| `0x7B..0x101` | `0x020DF078` | `HudMessagesMP.bin`, `H###` | static dumpでは `count=0x64`, table `0x02287C84`。 |
| `0x12C..0x131` | `0x020DF078` | `HudMessagesMP.bin`, `W###` | `id - 0x12C` を `W###` として引く。 |

US1.1のstatic dumpでロード済みのcommon/MP tableは英語。SP tableは未ロードなので、`H066` や `H121` などSP側候補の本文はこのdumpだけでは確定しない。

OSD色調査で出てきたmessage idとUS1.1文字列:

| Message ID | Key | Text | Color Definition / Call |
| --- | --- | --- | --- |
| `0x009` | `H009` | `AMMO DEPLETED!` | `0x0202DE44` / `0202DA90` |
| `0x042` | `H066` | SP table not loaded in static dump | `0x0201828C` / `020180FC` |
| `0x079` | `H121` | SP table not loaded in static dump | `0x0201828C` / `0201750C` |
| `0x0C9` | `H201` | `your octolith reset!` | `0x0212E058`, `0x0212E3E4`, or `0x0212E54C` |
| `0x0CA` | `H202` | `return to base` | `0x0202DE2C` / `0202D7C4` |
| `0x0CB` | `H203` | `bounty received` | `0x0202DE2C` / `0202D824` |
| `0x0CC` | `H204` | `progress` | `0x02031BA8` / `02031AEC` |
| `0x0CD` | `H205` | `acquiring\nnode` | `0x02031BA8` / `02031AEC` |
| `0x0CE` | `H206` | `complete` | `0x0202DE2C` / `0202D884` |
| `0x0CF` | `H207` | `enemy octolith reset!` | `0x0212E058`, `0x0212E3E4`, or `0x0212E54C` |
| `0x0D3` | `H211` | `node stolen` | `0202D8B0: mov r2,#0x1F` / `0202D8EC` |
| `0x0E4` | `H228` | `HEADSHOT!` | `0x0201828C` / `0201750C` |
| `0x0E5` | `H229` | `the octolith\nhas been dropped!` | `0x0212E3E4` / `0212E368` |
| `0x0E6` | `H230` | `the enemy\ndropped your octolith!` | `0x0212E3E4` / `0212E2C8` |
| `0x0E7` | `H231` | `your team\ndropped the octolith!` | `0x0212E3E4` / `0212E2C8` |
| `0x0E8` | `H232` | `your octolith is missing!` | `0x0212EDAC` / `0212ED08` |
| `0x0E9` | `H233` | `turret energy: %d` | `0x021108EC` / `02110640` |
| `0x0EA` | `H234` | `COWARD DETECTED!` | `0x020304E8` / `020304B0` |
| `0x0EB` | `H235` | `YOU SELF-DESTRUCTED!` | `0x0201828C` / `020180FC` |
| `0x0EC` | `H236` | `%s's HEADSHOT KILLED YOU!` | `0x0201828C` / `02018190` |
| `0x0ED` | `H237` | `%s KILLED YOU!` | `0x0201828C` / `020181F8` |
| `0x0EE` | `H238` | `YOU KILLED %s!` | `0x0201828C` / `0201858C` |
| `0x0EF` | `H239` | `YOUR HEADSHOT KILLED %s!` | `0x0201828C` / `0201858C` |
| `0x0F0` | `H240` | `YOU KILLED A TEAMMATE, (%s)!` | `0x0201828C` / `02018458` |
| `0x0F1` | `H241` | `%s is the new prime hunter!` | `0x0201828C` / `020188F0` |
| `0x0F2` | `H242` | `the prime hunter is dead!` | `0x0201828C` / `02018AA8` |
| `0x0F3` | `H243` | `you lost all your lives!\nyou're out of the game` | `0x0200FEA4` / `0200F458` |
| `0x0F7` | `H247` | `FACE OFF!` | `0x0200FEA4` / `0200FC0C` |
| `0x0F8` | `H248` | `position revealed!` | `0x0200FEA4` / `0200FD7C` |
| `0x0F9` | `H249` | `RETURN TO BATTLE!` | `0x0200FEA4` / `0200FDD0` |
| `0x0FA` | `H250` | `DEATHALT` | `0x0201828C` / `02018384` |
| `0x0FB` | `H251` | `MAGMAUL BURN` | `0x0201828C` / `02018384` |
| `0x0FC` | `H252` | `MORPH BALL BOMB` | `0x0201828C` / `02018384` |
| `0x0FD` | `H253` | `HALFTURRET SLICE` | `0x0201828C` / `02018384` |
| `0x0FE` | `H254` | `YOU KILLED 5 IN A ROW!` | `0x0201828C` / `02018708` |
| `0x0FF` | `H255` | `%s KILLED 5 IN A ROW!` | `0x0201828C` / `02018770` |
| `0x101` | `H257` | `octolith reset!` | `0x0212E058` or `0x0212E3E4` |

## Color Source Investigation

US1.1の `0204C518` への直callは28件あり、色定義は `11 literal + 1 immediate` の12箇所に集約できる。

`0204C518` 本体に「OSD全体の色定義アドレス」はない。各呼び出し元が `stack+0x04` へ色を置き、`0204C518` がそれを `entry+0x10` に保存する。最小行数patchは、目的のOSD呼び出し元のliteral poolを書き換える形が基本。

特にno-ammo系は `0202DA40-0202DA90` のブロックで、`0202DA40` が message id `0x09` をセットし、`0202DA50` が色literal `0x0202DE44 = 0x0000295F` を読む。ここがUS1.1の最小patch候補。

```text
; US1.1: message id 0x09 / H009 / "AMMO DEPLETED!" color -> cyan
0202DE44 00007FE0

; US1.1: same target -> pure red
0202DE44 0000001F

; US1.1: message id 0xCC/0xCD / H204/H205 color -> cyan
02031BA8 00007FE0

; US1.1: shared 0xCA/0xCB/0xCE notification color -> cyan
0202DE2C 00007FE0
```

確認した色ソース:

| Color Definition | Default | Enqueue Call Site(s) | Patch Pattern | Notes |
| --- | --- | --- | --- | --- |
| `0x0200FEA4` | `0x3FEF` | `0200F458`, `0200FC0C`, `0200FD7C`, `0200FDD0` | `0200FEA4 0000CCCC` | `H243/H247/H248/H249`。 |
| `0x0201828C` | `0x3FEF` | `0201750C`, `020180FC`, `02018190`, `020181F8`, `02018384`, `02018458`, `0201858C`, `02018708`, `02018770`, `020188F0`, `02018AA8` | `0201828C 0000CCCC` | kill/death通知群。 |
| `0x0202DE2C` | `0x3FEF` | `0202D7C4`, `0202D824`, `0202D884` | `0202DE2C 0000CCCC` | `0xCA/0xCB/0xCE = H202/H203/H206`。 |
| `0x0202D8B0` | immediate `0x001F` | `0202D8EC` | instruction patch | `0xD3 = H211 = node stolen`。literalではなく `mov r2,#0x1F`。 |
| `0x0202DE44` | `0x295F` | `0202DA90` | `0202DE44 0000CCCC` | `0x09 = H009 = AMMO DEPLETED!`。no-ammo系の最小patch候補。 |
| `0x020304E8` | `0x3FEF` | `020304B0` | `020304E8 0000CCCC` | `0xEA = H234 = COWARD DETECTED!`。 |
| `0x02031BA8` | `0x3FEF` | `02031AEC` | `02031BA8 0000CCCC` | `0xCC/0xCD = H204/H205`。 |
| `0x021108EC` | `0x3FEF` | `02110640` | `021108EC 0000CCCC` | `0xE9 = H233 = turret energy: %d`。full側追加領域。 |
| `0x0212E058` | `0x3FEF` | `0212E03C` | `0212E058 0000CCCC` | message id `0xC9/0xCF/0x101`。 |
| `0x0212E3E4` | `0x3FEF` | `0212E2C8`, `0212E368` | `0212E3E4 0000CCCC` | message id `0xC9/0xE6/0xCF/0xE7` または `0x101/0xE5`。 |
| `0x0212E54C` | `0x3FEF` | `0212E4D0` | `0212E54C 0000CCCC` | message id `0xC9/0xCF`。 |
| `0x0212EDAC` | `0x3FEF` | `0212ED08` | `0212EDAC 0000CCCC` | `0xE8 = H232 = your octolith is missing!`。 |

literal patchの考え方:

- `0x0202DE44` のようなliteralは `ldr r1,[r15,#...]` で32-bit wordとして読まれるので、基本は32-bit writeで `0000CCCC` を入れる。
- downstreamでは `0204C518` がhalfwordとして保存するため上位16bitは実質不要だが、ゼロにしておく方が安全。
- no-ammo系だけを狙うならUS1.1では `0202DE44` から試す。`02031BA8` は `progress` / `acquiring\nnode` 用。`0202DE2C` や `0201828C` は複数メッセージをまとめて変える。
- `0204C518` 本体を命令patchして色を強制する方法もあるが、全OSDへ効いてしまう上、ARM即値で表せる色に制約が出る。個別OSDの最小patchとしてはliteral pool変更の方が安全。

## Direct Enqueue Calls

| Enqueue Call | Message ID | Color Definition | Default | Timer | Flags | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `0200F458` | `0xF3` | `0x0200FEA4` | `0x3FEF` | `0x01` | `0x00` | `H243`。 |
| `0200FC0C` | `0xF7` | `0x0200FEA4` | `0x3FEF` | `0x01` | `0x00` | `H247 = FACE OFF!`。 |
| `0200FD7C` | `0xF8` | `0x0200FEA4` | `0x3FEF` | `0x01` | `0x00` | `H248 = position revealed!`。 |
| `0200FDD0` | `0xF9` | `0x0200FEA4` | `0x3FEF` | `0x01` | `0x00` | `H249 = RETURN TO BATTLE!`。 |
| `0201750C` | `0x79/0xE4` | `0x0201828C` | `0x3FEF` | `0x14` | `0x00` | `H121` はSP table未ロード、`H228 = HEADSHOT!`。 |
| `020180FC` | `0x42/0xEB` | `0x0201828C` | `0x3FEF` | `0x5A` | `0x02` | `H066` はSP table未ロード、`H235 = YOU SELF-DESTRUCTED!`。 |
| `02018190` | `0xEC` | `0x0201828C` | `0x3FEF` | `0x5A` | `0x02` | `H236 = %s's HEADSHOT KILLED YOU!`。 |
| `020181F8` | `0xED` | `0x0201828C` | `0x3FEF` | `0x5A` | `0x02` | `H237 = %s KILLED YOU!`。 |
| `02018384` | `0xFA/0xFB/0xFC/0xFD` or dynamic | `0x0201828C` | `0x3FEF` | `0x5A` | `0x02` | death source表示。weapon name table経由の動的文字列もあり。 |
| `02018458` | `0xF0` | `0x0201828C` | `0x3FEF` | `0x3C` | `0x02` | `H240 = YOU KILLED A TEAMMATE, (%s)!`。 |
| `0201858C` | `0xEE/0xEF` | `0x0201828C` | `0x3FEF` | `0x3C` | `0x02` | `H238/H239`。 |
| `02018708` | `0xFE` | `0x0201828C` | `0x3FEF` | `0x5A` | `0x02` | `H254 = YOU KILLED 5 IN A ROW!`。 |
| `02018770` | `0xFF` | `0x0201828C` | `0x3FEF` | `0x5A` | `0x02` | `H255 = %s KILLED 5 IN A ROW!`。 |
| `020188F0` | `0xF1` | `0x0201828C` | `0x3FEF` | `0x5A` | `0x02` | `H241 = %s is the new prime hunter!`。 |
| `02018AA8` | `0xF2` | `0x0201828C` | `0x3FEF` | `0x5A` | `0x02` | `H242 = the prime hunter is dead!`。 |
| `0202D7C4` | `0xCA` | `0x0202DE2C` | `0x3FEF` | `0x5A` | `0x01` | `H202 = return to base`。 |
| `0202D824` | `0xCB` | `0x0202DE2C` | `0x3FEF` | `0x5A` | `0x01` | `H203 = bounty received`。 |
| `0202D884` | `0xCE` | `0x0202DE2C` | `0x3FEF` | `0x5A` | `0x01` | `H206 = complete`。 |
| `0202D8EC` | `0xD3` | `0202D8B0: mov r2,#0x1F` | `0x001F` | `0x5A` | `0x11` | `H211 = node stolen`。 |
| `0202DA90` | `0x09` | `0x0202DE44` | `0x295F` | `0x2D` | `0x01` | `H009 = AMMO DEPLETED!`。 |
| `020304B0` | `0xEA` | `0x020304E8` | `0x3FEF` | `0x3C` | `0x00` | `H234 = COWARD DETECTED!`。 |
| `02031AEC` | `0xCC/0xCD` | `0x02031BA8` | `0x3FEF` | `0x2D` | `0x11` | `H204/H205`。 |
| `02110640` | `0xE9` | `0x021108EC` | `0x3FEF` | `0x01` | `0x00` | `H233 = turret energy: %d`。 |
| `0212E03C` | `0xC9/0xCF/0x101` | `0x0212E058` | `0x3FEF` | `0x3C` | `0x01` | full側追加領域。 |
| `0212E2C8` | `0xC9/0xE6/0xCF/0xE7` | `0x0212E3E4` | `0x3FEF` | `0x3C` | `0x01` | `H201/H230/H207/H231`。 |
| `0212E368` | `0x101/0xE5` | `0x0212E3E4` | `0x3FEF` | `0x3C` | `0x01` | `H257/H229`。 |
| `0212E4D0` | `0xC9/0xCF` | `0x0212E54C` | `0x3FEF` | `0x3C` | `0x01` | `H201/H207`。 |
| `0212ED08` | `0xE8` | `0x0212EDAC` | `0x3FEF` | `0x01` | dynamic | `H232 = your octolith is missing!`。 |

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
