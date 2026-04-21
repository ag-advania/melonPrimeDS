# OSD-COLOR-PATCH

エミュレータ用のin-game OSD色変更patchまとめ。

ここでいうOSDは `AMMO DEPLETED!` / `アモがない！`、node通知、kill/death通知など、OSD MSG slotへ投入されるメッセージ群。

## 使い分け

- **literal patchで既定色を変える**
  - 呼び出し元のliteral poolを書き換える。
  - すでに表示中のslotは即時には変わらず、次にそのOSDが投入された時から変わる。
  - エミュレータのaddress/value patchなら、まずこの方式を使う。

- **runtime slotを強制的に同じ色へ上書きする**
  - `OSD MSG base + (slot - 1) * stride + 0x10` を全20slotぶんhalfword writeする。
  - cheatを有効にしている間、表示中slotも新規投入slotも同じ色へ寄せられる。
  - timerは触らないので、空slotをactive化しない。
  - 下の `Runtime Slot Override` は参考用。literal patchだけ使うなら不要。

`CCCC` は16-bit BGR555色コードに置換する。

```text
Red      = 001F
Green    = 03E0
Blue     = 7C00
Yellow   = 03FF
Cyan     = 7FE0
Magenta  = 7C1F
White    = 7FFF

Default bright green = 3FEF
Default no-ammo red  = 295F
```

OSD color format:

```text
code = XBBBBBGGGGGRRRRR

R5 = code & 0x1F
G5 = (code >> 5) & 0x1F
B5 = (code >> 10) & 0x1F

code = (B5 << 10) | (G5 << 5) | R5
```

## Address/Value Patch Blocks

確認済みdirect OSD callの色literalと、H211 / node stolen系を既存literalへ合わせる命令patchをまとめたaddress/value patch。

```text
literal write size     = 32-bit
literal value          = 0000CCCC
instruction write size = 32-bit
```

`CCCC` を目的のBGR555色に置換する。たとえばCyanなら `00007FE0`。

行末コメントは説明用。使用するエミュレータ/patcherが行末コメント非対応なら、`;` 以降を削って使う。

H211 / node stolen系は、既存のreturn/base/complete系literalを読むように命令列を組み替える。これで即値色ではなく他の通知と同じliteral patchに揃う。詳細は `H211 Literal-Match Shim Notes` を参照。

### JP1.0

```text
; JP1.0: confirmed OSD color patch points
0200FED4 0000CCCC ; H243/H247/H248/H249 = リタイア / ラストバトル / エマージェンシー / ロックオンされた
02018268 0000CCCC ; H228/H235/H236/H237/H239/H240/H241/H242/H252/H254/H255 = kill/death通知群
0202DE50 0000CCCC ; H202/H203/H206 = ベースへもどれ！ / ボーナスゲット！ / ノードハッククリア！
0202DE68 0000CCCC ; H009 = アモがない！
0202D88C E59F25BC ; H211 shim: ノードハックされました！ color <- 0202DE50
0202D890 E3A0301F ; H211 shim: alpha = 0x1F
0202D894 E98D000C ; H211 shim: store color + alpha
0202D898 E59F15B4 ; H211 shim: font pointer load restore
02030534 0000CCCC ; H234 = ハンターをロックオン！
02031D0C 0000CCCC ; H205 = ノードハック\nスタート
02111F6C 0000CCCC ; H233 = ハーフタレット： %d
0212F6D8 0000CCCC ; H201/H207/H257 = octolith reset系
0212FA64 0000CCCC ; H207/H231 または H257/H229 = octolith dropped/reset系
0212FBCC 0000CCCC ; H201/H207 = octolith reset条件分岐
0213042C 0000CCCC ; H232 = octolith missing系
```

### JP1.1

```text
; JP1.1: confirmed OSD color patch points
0200FED4 0000CCCC ; H243/H247/H248/H249 = リタイア / ラストバトル / エマージェンシー / ロックオンされた
02018268 0000CCCC ; H228/H235/H236/H237/H239/H240/H241/H242/H252/H254/H255 = kill/death通知群
0202DE50 0000CCCC ; H202/H203/H206 = ベースへもどれ！ / ボーナスゲット！ / ノードハッククリア！
0202DE68 0000CCCC ; H009 = アモがない！
0202D88C E59F25BC ; H211 shim: ノードハックされました！ color <- 0202DE50
0202D890 E3A0301F ; H211 shim: alpha = 0x1F
0202D894 E98D000C ; H211 shim: store color + alpha
0202D898 E59F15B4 ; H211 shim: font pointer load restore
02030534 0000CCCC ; H234 = ハンターをロックオン！
02031D0C 0000CCCC ; H205 = ノードハック\nスタート
02111F2C 0000CCCC ; H233 = ハーフタレット： %d
0212F698 0000CCCC ; H201/H207/H257 = octolith reset系
0212FA24 0000CCCC ; H207/H231 または H257/H229 = octolith dropped/reset系
0212FB8C 0000CCCC ; H201/H207 = octolith reset条件分岐
021303EC 0000CCCC ; H232 = octolith missing系
```

### US1.0

```text
; US1.0: confirmed OSD color patch points
0200FF18 0000CCCC ; H243/H247/H248/H249 = lost lives / FACE OFF / position revealed / RETURN TO BATTLE
02018288 0000CCCC ; H228/H235/H236/H237/H239/H240/H241/H242/H252/H254/H255 = kill/death通知群
0202DE2C 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
0202DE44 0000CCCC ; H009 = AMMO DEPLETED!
0202D8B0 E59F2574 ; H211 shim: node stolen color <- 0202DE2C
0202D8B4 E3A0301F ; H211 shim: alpha = 0x1F
0202D8B8 E98D000C ; H211 shim: store color + alpha
0202D8BC E59F156C ; H211 shim: font pointer load restore
0203051C 0000CCCC ; H234 = COWARD DETECTED!
02031BF8 0000CCCC ; H205 = acquiring\nnode
0210FE2C 0000CCCC ; H233 = turret energy: %d
0212D538 0000CCCC ; H201/H207/H257 = octolith reset系
0212D8C4 0000CCCC ; H207/H231 または H257/H229 = octolith dropped/reset系
0212DA2C 0000CCCC ; H201/H207 = octolith reset条件分岐
0212E28C 0000CCCC ; H232 = your octolith is missing!
```

### US1.1

```text
; US1.1: confirmed OSD color patch points
0200FEA4 0000CCCC ; H243/H247/H248/H249 = lost lives / FACE OFF / position revealed / RETURN TO BATTLE
0201828C 0000CCCC ; H228/H235-H242/H250-H255 = kill/death通知群
0202DE2C 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
0202DE44 0000CCCC ; H009 = AMMO DEPLETED!
0202D8B0 E59F2574 ; H211 shim: node stolen color <- 0202DE2C
0202D8B4 E3A0301F ; H211 shim: alpha = 0x1F
0202D8B8 E98D000C ; H211 shim: store color + alpha
0202D8BC E59F156C ; H211 shim: font pointer load restore
020304E8 0000CCCC ; H234 = COWARD DETECTED!
02031BA8 0000CCCC ; H204/H205 = progress / acquiring\nnode
021108EC 0000CCCC ; H233 = turret energy: %d
0212E058 0000CCCC ; H201/H207/H257 = octolith reset系
0212E3E4 0000CCCC ; H201/H230/H207/H231 または H257/H229 = octolith dropped/reset系
0212E54C 0000CCCC ; H201/H207 = octolith reset条件分岐
0212EDAC 0000CCCC ; H232 = your octolith is missing!
```

### EU1.0

```text
; EU1.0: confirmed OSD color patch points
0200FF10 0000CCCC ; H243/H247/H248/H249 = lost lives / FACE OFF / position revealed / RETURN TO BATTLE
02018280 0000CCCC ; H228/H235-H242/H250-H255 = kill/death通知群
0202DE24 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
0202DE3C 0000CCCC ; H009 = AMMO DEPLETED!
0202D8A8 E59F2574 ; H211 shim: node stolen color <- 0202DE24
0202D8AC E3A0301F ; H211 shim: alpha = 0x1F
0202D8B0 E98D000C ; H211 shim: store color + alpha
0202D8B4 E59F156C ; H211 shim: font pointer load restore
020304E0 0000CCCC ; H234 = COWARD DETECTED!
02031BA0 0000CCCC ; H204/H205 = progress / acquiring\nnode
0211090C 0000CCCC ; H233 = turret energy: %d
0212E018 0000CCCC ; H201/H207/H257 = octolith reset系
0212E3A4 0000CCCC ; H201/H230/H207/H231 または H257/H229 = octolith dropped/reset系
0212E50C 0000CCCC ; H201/H207 = octolith reset条件分岐
0212ED6C 0000CCCC ; H232 = your octolith is missing!
```

### EU1.1

```text
; EU1.1: confirmed OSD color patch points
0200FEA4 0000CCCC ; H243/H247/H248/H249 = lost lives / FACE OFF / position revealed / RETURN TO BATTLE
0201828C 0000CCCC ; H228/H235-H242/H250-H255 = kill/death通知群
0202DE2C 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
0202DE44 0000CCCC ; H009 = AMMO DEPLETED!
0202D8B0 E59F2574 ; H211 shim: node stolen color <- 0202DE2C
0202D8B4 E3A0301F ; H211 shim: alpha = 0x1F
0202D8B8 E98D000C ; H211 shim: store color + alpha
0202D8BC E59F156C ; H211 shim: font pointer load restore
020304E8 0000CCCC ; H234 = COWARD DETECTED!
02031BA8 0000CCCC ; H204/H205 = progress / acquiring\nnode
0211098C 0000CCCC ; H233 = turret energy: %d
0212E0F8 0000CCCC ; H201/H207/H257 = octolith reset系
0212E484 0000CCCC ; H201/H230/H207/H231 または H257/H229 = octolith dropped/reset系
0212E5EC 0000CCCC ; H201/H207 = octolith reset条件分岐
0212EE4C 0000CCCC ; H232 = your octolith is missing!
```

### KR1.0

```text
; KR1.0: confirmed OSD color patch points
0201A7F8 0000CCCC ; H228/H235-H242/H250-H255 = kill/death通知群
02021844 0000CCCC ; H243/H247/H248/H249 = lost lives / LAST BATTLE / EMERGENCY / RETURN TO BATTLE
0203285C 0000CCCC ; H205 = acquiring\nnode
02033D98 0000CCCC ; H234 = HUNTER DETECTED!
02036AC8 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
02036AD4 0000CCCC ; H009 = AMMO ZERO!
020365F8 E59F24C8 ; H211 shim: node stolen color <- 02036AC8
020365FC E3A0301F ; H211 shim: alpha = 0x1F
02036600 E98D000C ; H211 shim: store color + alpha
02036604 E59F1488 ; H211 shim: font pointer load restore
02108C28 0000CCCC ; H233 = turret energy: %d
02124D6C 0000CCCC ; H201/H207 = octolith reset条件分岐
02124F98 0000CCCC ; H230/H207/H231 または H257/H229 = octolith dropped/reset系
021251C4 0000CCCC ; H201/H207/H257 = octolith reset系
02126268 0000CCCC ; H232 = your octolith is missing!
```

## Variant: H211 Untouched

H211 / `node stolen` / `ノードハックされました！` の即値命令を触らない版。H211は元の赤系 `0x001F` のまま。

ここでいう「ノードハック」は前後の文脈どおりH211の即値色を指す。H205 `acquiring node` / `ノードハック\nスタート` やH206 `complete` / `ノードハッククリア！` のliteralは、下のblockでは通常どおり対象に含めている。

### JP1.0

```text
; JP1.0: H211 untouched
0200FED4 0000CCCC ; H243/H247/H248/H249 = リタイア / ラストバトル / エマージェンシー / ロックオンされた
02018268 0000CCCC ; H228/H235/H236/H237/H239/H240/H241/H242/H252/H254/H255 = kill/death通知群
0202DE50 0000CCCC ; H202/H203/H206 = ベースへもどれ！ / ボーナスゲット！ / ノードハッククリア！
0202DE68 0000CCCC ; H009 = アモがない！
02030534 0000CCCC ; H234 = ハンターをロックオン！
02031D0C 0000CCCC ; H205 = ノードハック\nスタート
02111F6C 0000CCCC ; H233 = ハーフタレット： %d
0212F6D8 0000CCCC ; H201/H207/H257 = octolith reset系
0212FA64 0000CCCC ; H207/H231 または H257/H229 = octolith dropped/reset系
0212FBCC 0000CCCC ; H201/H207 = octolith reset条件分岐
0213042C 0000CCCC ; H232 = octolith missing系
```

### JP1.1

```text
; JP1.1: H211 untouched
0200FED4 0000CCCC ; H243/H247/H248/H249 = リタイア / ラストバトル / エマージェンシー / ロックオンされた
02018268 0000CCCC ; H228/H235/H236/H237/H239/H240/H241/H242/H252/H254/H255 = kill/death通知群
0202DE50 0000CCCC ; H202/H203/H206 = ベースへもどれ！ / ボーナスゲット！ / ノードハッククリア！
0202DE68 0000CCCC ; H009 = アモがない！
02030534 0000CCCC ; H234 = ハンターをロックオン！
02031D0C 0000CCCC ; H205 = ノードハック\nスタート
02111F2C 0000CCCC ; H233 = ハーフタレット： %d
0212F698 0000CCCC ; H201/H207/H257 = octolith reset系
0212FA24 0000CCCC ; H207/H231 または H257/H229 = octolith dropped/reset系
0212FB8C 0000CCCC ; H201/H207 = octolith reset条件分岐
021303EC 0000CCCC ; H232 = octolith missing系
```

### US1.0

```text
; US1.0: H211 untouched
0200FF18 0000CCCC ; H243/H247/H248/H249 = lost lives / FACE OFF / position revealed / RETURN TO BATTLE
02018288 0000CCCC ; H228/H235/H236/H237/H239/H240/H241/H242/H252/H254/H255 = kill/death通知群
0202DE2C 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
0202DE44 0000CCCC ; H009 = AMMO DEPLETED!
0203051C 0000CCCC ; H234 = COWARD DETECTED!
02031BF8 0000CCCC ; H205 = acquiring\nnode
0210FE2C 0000CCCC ; H233 = turret energy: %d
0212D538 0000CCCC ; H201/H207/H257 = octolith reset系
0212D8C4 0000CCCC ; H207/H231 または H257/H229 = octolith dropped/reset系
0212DA2C 0000CCCC ; H201/H207 = octolith reset条件分岐
0212E28C 0000CCCC ; H232 = your octolith is missing!
```

### US1.1

```text
; US1.1: H211 untouched
0200FEA4 0000CCCC ; H243/H247/H248/H249 = lost lives / FACE OFF / position revealed / RETURN TO BATTLE
0201828C 0000CCCC ; H228/H235-H242/H250-H255 = kill/death通知群
0202DE2C 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
0202DE44 0000CCCC ; H009 = AMMO DEPLETED!
020304E8 0000CCCC ; H234 = COWARD DETECTED!
02031BA8 0000CCCC ; H204/H205 = progress / acquiring\nnode
021108EC 0000CCCC ; H233 = turret energy: %d
0212E058 0000CCCC ; H201/H207/H257 = octolith reset系
0212E3E4 0000CCCC ; H201/H230/H207/H231 または H257/H229 = octolith dropped/reset系
0212E54C 0000CCCC ; H201/H207 = octolith reset条件分岐
0212EDAC 0000CCCC ; H232 = your octolith is missing!
```

### EU1.0

```text
; EU1.0: H211 untouched
0200FF10 0000CCCC ; H243/H247/H248/H249 = lost lives / FACE OFF / position revealed / RETURN TO BATTLE
02018280 0000CCCC ; H228/H235-H242/H250-H255 = kill/death通知群
0202DE24 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
0202DE3C 0000CCCC ; H009 = AMMO DEPLETED!
020304E0 0000CCCC ; H234 = COWARD DETECTED!
02031BA0 0000CCCC ; H204/H205 = progress / acquiring\nnode
0211090C 0000CCCC ; H233 = turret energy: %d
0212E018 0000CCCC ; H201/H207/H257 = octolith reset系
0212E3A4 0000CCCC ; H201/H230/H207/H231 または H257/H229 = octolith dropped/reset系
0212E50C 0000CCCC ; H201/H207 = octolith reset条件分岐
0212ED6C 0000CCCC ; H232 = your octolith is missing!
```

### EU1.1

```text
; EU1.1: H211 untouched
0200FEA4 0000CCCC ; H243/H247/H248/H249 = lost lives / FACE OFF / position revealed / RETURN TO BATTLE
0201828C 0000CCCC ; H228/H235-H242/H250-H255 = kill/death通知群
0202DE2C 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
0202DE44 0000CCCC ; H009 = AMMO DEPLETED!
020304E8 0000CCCC ; H234 = COWARD DETECTED!
02031BA8 0000CCCC ; H204/H205 = progress / acquiring\nnode
0211098C 0000CCCC ; H233 = turret energy: %d
0212E0F8 0000CCCC ; H201/H207/H257 = octolith reset系
0212E484 0000CCCC ; H201/H230/H207/H231 または H257/H229 = octolith dropped/reset系
0212E5EC 0000CCCC ; H201/H207 = octolith reset条件分岐
0212EE4C 0000CCCC ; H232 = your octolith is missing!
```

### KR1.0

```text
; KR1.0: H211 untouched
0201A7F8 0000CCCC ; H228/H235-H242/H250-H255 = kill/death通知群
02021844 0000CCCC ; H243/H247/H248/H249 = lost lives / LAST BATTLE / EMERGENCY / RETURN TO BATTLE
0203285C 0000CCCC ; H205 = acquiring\nnode
02033D98 0000CCCC ; H234 = HUNTER DETECTED!
02036AC8 0000CCCC ; H202/H203/H206 = return to base / bounty received / complete
02036AD4 0000CCCC ; H009 = AMMO ZERO!
02108C28 0000CCCC ; H233 = turret energy: %d
02124D6C 0000CCCC ; H201/H207 = octolith reset条件分岐
02124F98 0000CCCC ; H230/H207/H231 または H257/H229 = octolith dropped/reset系
021251C4 0000CCCC ; H201/H207/H257 = octolith reset系
02126268 0000CCCC ; H232 = your octolith is missing!
```

## Variant: H211 Separate Color

H211 / `node stolen` / `ノードハックされました！` だけ別色にする版。上の `H211 Untouched` blockをベースにして、同じversionの下記blockを追加する。

この版でやることは2つだけ。

1. 通常OSD色は `H211 Untouched` の `CCCC` で指定する。
2. H211だけの色は、このsectionの `NNNN` から作った `LL` / `HH` で指定する。

つまり、`CCCC` と `NNNN` は別物。

```text
CCCC = H211以外のOSD色
NNNN = H211だけのOSD色
```

H211別色版は、`0202DE50 0000NNNN` のようなliteral writeではない。命令そのものを書き換えて、CPUに `NNNN` を作らせる。

### NNNNからLL/HHを作る

`NNNN` はH211専用のBGR555色。必ず4桁のhexで考える。

```text
NNNN = 7FE0
```

この4桁を、後ろ2桁と前2桁に分ける。

```text
前2桁 = 7F
後2桁 = E0
```

patchでは名前をこう呼ぶ。

```text
NNNN = BGR555 color
LL   = 後2桁 = NNNN & 00FF
HH   = 前2桁 = (NNNN >> 8) & 00FF
```

なので `NNNN = 7FE0` ならこうなる。

```text
LL = E0
HH = 7F
```

次に、各version blockの最初の2行だけ `LL` / `HH` を置き換える。

```text
mov row = E3A020LL
orr row = E3822CHH
```

Cyan `0x7FE0` の場合:

```text
E3A020LL -> E3A020E0
E3822CHH -> E3822C7F
```

### 具体例

JP1.0 / JP1.1でH211だけCyan `7FE0` にするなら、下のように最初の2行だけ具体値にする。

```text
0202D88C E3A020E0 ; mov r2,#0xE0
0202D890 E3822C7F ; orr r2,r2,#0x7F00
0202D894 E3A0301F ; alpha = 0x1F
0202D898 E98D000C ; store color + alpha
0202D89C E59F15B0 ; load font pointer holder -> 0202DE54
0202D8A0 E5911000 ; load font pointer
0202D8A4 E3A0205A ; timer = 0x5A
0202D8A8 E3A03011 ; flags = 0x11
0202D8AC E28DC00C ; r12 = sp+0x0C
0202D8B0 E88C000E ; store font/timer/flags
```

色ごとの置換例:

| H211 Color | NNNN | LL | HH | mov row | orr row |
| --- | --- | --- | --- | --- | --- |
| Red | `001F` | `1F` | `00` | `E3A0201F` | `E3822C00` |
| Green | `03E0` | `E0` | `03` | `E3A020E0` | `E3822C03` |
| Cyan | `7FE0` | `E0` | `7F` | `E3A020E0` | `E3822C7F` |
| White | `7FFF` | `FF` | `7F` | `E3A020FF` | `E3822C7F` |

注意:

- 各version blockは10行全部入れる。
- `LL` / `HH` を置き換えるのは最初の2行だけ。
- `NNNN` や `0000NNNN` をそのまま命令行へ入れない。
- `H211 Untouched`、通常の `Address/Value Patch Blocks` 内のH211 shim、`H211 Separate Color` は同時に使わない。H211に対して使う方式を1つだけ選ぶ。

### JP1.0 / JP1.1

```text
; JP1.0 / JP1.1: H211 separate color -> NNNN
0202D88C E3A020LL ; mov r2,#LL
0202D890 E3822CHH ; orr r2,r2,#HH<<8
0202D894 E3A0301F ; alpha = 0x1F
0202D898 E98D000C ; store color + alpha
0202D89C E59F15B0 ; load font pointer holder -> 0202DE54
0202D8A0 E5911000 ; load font pointer
0202D8A4 E3A0205A ; timer = 0x5A
0202D8A8 E3A03011 ; flags = 0x11
0202D8AC E28DC00C ; r12 = sp+0x0C
0202D8B0 E88C000E ; store font/timer/flags
```

### US1.0 / US1.1 / EU1.1

```text
; US1.0 / US1.1 / EU1.1: H211 separate color -> NNNN
0202D8B0 E3A020LL ; mov r2,#LL
0202D8B4 E3822CHH ; orr r2,r2,#HH<<8
0202D8B8 E3A0301F ; alpha = 0x1F
0202D8BC E98D000C ; store color + alpha
0202D8C0 E59F1568 ; load font pointer holder -> 0202DE30
0202D8C4 E5911000 ; load font pointer
0202D8C8 E3A0205A ; timer = 0x5A
0202D8CC E3A03011 ; flags = 0x11
0202D8D0 E28DC00C ; r12 = sp+0x0C
0202D8D4 E88C000E ; store font/timer/flags
```

### EU1.0

```text
; EU1.0: H211 separate color -> NNNN
0202D8A8 E3A020LL ; mov r2,#LL
0202D8AC E3822CHH ; orr r2,r2,#HH<<8
0202D8B0 E3A0301F ; alpha = 0x1F
0202D8B4 E98D000C ; store color + alpha
0202D8B8 E59F1568 ; load font pointer holder -> 0202DE28
0202D8BC E5911000 ; load font pointer
0202D8C0 E3A0205A ; timer = 0x5A
0202D8C4 E3A03011 ; flags = 0x11
0202D8C8 E28DC00C ; r12 = sp+0x0C
0202D8CC E88C000E ; store font/timer/flags
```

### KR1.0

```text
; KR1.0: H211 separate color -> NNNN
020365F8 E3A020LL ; mov r2,#LL
020365FC E3822CHH ; orr r2,r2,#HH<<8
02036600 E3A0301F ; alpha = 0x1F
02036604 E98D000C ; store color + alpha
02036608 E59F1484 ; load font pointer holder -> 02036A94
0203660C E59110A8 ; load font pointer
02036610 E3A0205A ; timer = 0x5A
02036614 E3A03011 ; flags = 0x11
02036618 E28DC00C ; r12 = sp+0x0C
0203661C E88C000E ; store font/timer/flags
```

## H211 Literal-Match Shim Notes

`node stolen` / `ノードハックされました！` 系は元々literalではなく `mov r2,#0x1F` の命令即値。

単純に `mov r2,#color` へ変えるとARM immediate制約を受けるうえ、同じ `r2` をalphaにも使うため壊れやすい。上のpatchでは5命令の枠内で次の形へ組み替える。

```text
ldr r2,[pc,#color_literal] ; return/base/complete系literalを読む
mov r3,#0x1F               ; alpha
stmib sp,{r2,r3}           ; sp+4=color, sp+8=alpha
ldr r1,[pc,#font_pointer]
```

このため、H211の色は下のliteralと連動する。

| Version | H211 Color Source After Patch |
| --- | --- |
| JP1.0 | `0202DE50 0000CCCC` |
| JP1.1 | `0202DE50 0000CCCC` |
| US1.0 | `0202DE2C 0000CCCC` |
| US1.1 | `0202DE2C 0000CCCC` |
| EU1.0 | `0202DE24 0000CCCC` |
| EU1.1 | `0202DE2C 0000CCCC` |
| KR1.0 | `02036AC8 0000CCCC` |

| Version | Message | Immediate Source |
| --- | --- | --- |
| JP1.0 | `ノードハックされました！` | `0202D88C: mov r2,#0x1F` |
| JP1.1 | `ノードハックされました！` | `0202D88C: mov r2,#0x1F` |
| US1.0 | `node stolen` | `0202D8B0: mov r2,#0x1F` |
| US1.1 | `node stolen` | `0202D8B0: mov r2,#0x1F` |
| EU1.0 | `node stolen` | `0202D8A8: mov r2,#0x1F` |
| EU1.1 | `node stolen` | `0202D8B0: mov r2,#0x1F` |
| KR1.0 | `node stolen` | `020365F8: mov r2,#0x1F` |

H211だけを独立色にしたい場合は、上の `H211 Separate Color` のように2命令でBGR555値を組み立てる。専用literal候補を潰さないので、code caveなしで試せる。

## Slot Address Map

`US1.0` だけ entry stride が `0x80`。他バージョンは `0x9C`。

| Version | Struct Doc | OSD MSG Base | Entry Stride | Slot 1 Color | Slot 1 Halfword Pattern |
| --- | --- | --- | --- | --- | --- |
| JP1.0 | [`In-Game-OSD-Struct-JP1_0.md`](In-Game-OSD-Struct-JP1_0.md) | `0x020E5B38` | `0x9C` | `0x020E5B48` | `120E5B48 0000CCCC` |
| JP1.1 | [`In-Game-OSD-Struct-JP1_1.md`](In-Game-OSD-Struct-JP1_1.md) | `0x020E5AF8` | `0x9C` | `0x020E5B08` | `120E5B08 0000CCCC` |
| US1.0 | [`In-Game-OSD-Struct-US1_0.md`](In-Game-OSD-Struct-US1_0.md) | `0x020E3C3C` | `0x80` | `0x020E3C4C` | `120E3C4C 0000CCCC` |
| US1.1 | [`In-Game-OSD-Struct-US1_1.md`](In-Game-OSD-Struct-US1_1.md) | `0x020E44D4` | `0x9C` | `0x020E44E4` | `120E44E4 0000CCCC` |
| EU1.0 | [`In-Game-OSD-Struct-EU1_0.md`](In-Game-OSD-Struct-EU1_0.md) | `0x020E44F4` | `0x9C` | `0x020E4504` | `120E4504 0000CCCC` |
| EU1.1 | [`In-Game-OSD-Struct-EU1_1.md`](In-Game-OSD-Struct-EU1_1.md) | `0x020E4574` | `0x9C` | `0x020E4584` | `120E4584 0000CCCC` |
| KR1.0 | [`In-Game-OSD-Struct-KR1_0.md`](In-Game-OSD-Struct-KR1_0.md) | `0x020DCD34` | `0x9C` | `0x020DCD44` | `120DCD44 0000CCCC` |

任意slotの式:

```text
color_addr = OSD_MSG_BASE + (slot - 1) * ENTRY_STRIDE + 0x10
```

## Runtime Slot Override

`C0000000 00000013` は20回loop。`10000010` は現在baseの `+0x10` へhalfword write。`DC000000` で次slotへ進める。

### JP1.0

```text
; JP1.0: all OSD slots color -> CCCC
D3000000 020E5B38
C0000000 00000013
10000010 0000CCCC
DC000000 0000009C
D2000000 00000000
```

### JP1.1

```text
; JP1.1: all OSD slots color -> CCCC
D3000000 020E5AF8
C0000000 00000013
10000010 0000CCCC
DC000000 0000009C
D2000000 00000000
```

### US1.0

```text
; US1.0: all OSD slots color -> CCCC
D3000000 020E3C3C
C0000000 00000013
10000010 0000CCCC
DC000000 00000080
D2000000 00000000
```

### US1.1

```text
; US1.1: all OSD slots color -> CCCC
D3000000 020E44D4
C0000000 00000013
10000010 0000CCCC
DC000000 0000009C
D2000000 00000000
```

### EU1.0

```text
; EU1.0: all OSD slots color -> CCCC
D3000000 020E44F4
C0000000 00000013
10000010 0000CCCC
DC000000 0000009C
D2000000 00000000
```

### EU1.1

```text
; EU1.1: all OSD slots color -> CCCC
D3000000 020E4574
C0000000 00000013
10000010 0000CCCC
DC000000 0000009C
D2000000 00000000
```

### KR1.0

```text
; KR1.0: all OSD slots color -> CCCC
D3000000 020DCD34
C0000000 00000013
10000010 0000CCCC
DC000000 0000009C
D2000000 00000000
```

## Revert: 元の状態に戻すパッチ

適用したvariantに応じて戻す命令のセットが異なる。

- literal patchのみ (`H211 Untouched` 含む) → `Literal Patch Revert` だけ適用
- `Address/Value Patch Blocks` のH211 shim (4命令) も適用済み → `Literal Patch Revert` + `H211 Shim Revert`
- `H211 Separate Color` (10命令) を適用済み → `Literal Patch Revert` + `H211 Separate Color Revert`

### Literal Patch Revert

各addressを元のデフォルト色に戻す。`0000CCCC` を下表のdefault値に置き換える。

| 用途 | Default値 |
| --- | --- |
| 通常OSD (kill/death, node, octolith系) | `00003FEF` |
| no-ammo (H009) | `0000295F` |

#### JP1.0

```text
; JP1.0: revert OSD color literals to defaults
0200FED4 00003FEF
02018268 00003FEF
0202DE50 00003FEF
0202DE68 0000295F
02030534 00003FEF
02031D0C 00003FEF
02111F6C 00003FEF
0212F6D8 00003FEF
0212FA64 00003FEF
0212FBCC 00003FEF
0213042C 00003FEF
```

#### JP1.1

```text
; JP1.1: revert OSD color literals to defaults
0200FED4 00003FEF
02018268 00003FEF
0202DE50 00003FEF
0202DE68 0000295F
02030534 00003FEF
02031D0C 00003FEF
02111F2C 00003FEF
0212F698 00003FEF
0212FA24 00003FEF
0212FB8C 00003FEF
021303EC 00003FEF
```

#### US1.0

```text
; US1.0: revert OSD color literals to defaults
0200FF18 00003FEF
02018288 00003FEF
0202DE2C 00003FEF
0202DE44 0000295F
0203051C 00003FEF
02031BF8 00003FEF
0210FE2C 00003FEF
0212D538 00003FEF
0212D8C4 00003FEF
0212DA2C 00003FEF
0212E28C 00003FEF
```

#### US1.1

```text
; US1.1: revert OSD color literals to defaults
0200FEA4 00003FEF
0201828C 00003FEF
0202DE2C 00003FEF
0202DE44 0000295F
020304E8 00003FEF
02031BA8 00003FEF
021108EC 00003FEF
0212E058 00003FEF
0212E3E4 00003FEF
0212E54C 00003FEF
0212EDAC 00003FEF
```

#### EU1.0

```text
; EU1.0: revert OSD color literals to defaults
0200FF10 00003FEF
02018280 00003FEF
0202DE24 00003FEF
0202DE3C 0000295F
020304E0 00003FEF
02031BA0 00003FEF
0211090C 00003FEF
0212E018 00003FEF
0212E3A4 00003FEF
0212E50C 00003FEF
0212ED6C 00003FEF
```

#### EU1.1

```text
; EU1.1: revert OSD color literals to defaults
0200FEA4 00003FEF
0201828C 00003FEF
0202DE2C 00003FEF
0202DE44 0000295F
020304E8 00003FEF
02031BA8 00003FEF
0211098C 00003FEF
0212E0F8 00003FEF
0212E484 00003FEF
0212E5EC 00003FEF
0212EE4C 00003FEF
```

#### KR1.0

```text
; KR1.0: revert OSD color literals to defaults
0201A7F8 00003FEF
02021844 00003FEF
0203285C 00003FEF
02033D98 00003FEF
02036AC8 00003FEF
02036AD4 0000295F
02108C28 00003FEF
02124D6C 00003FEF
02124F98 00003FEF
021251C4 00003FEF
02126268 00003FEF
```

### H211 Shim Revert

`Address/Value Patch Blocks` のH211 4命令パッチを元に戻す。ダンプから取得した元の命令値。

#### JP1.0 / JP1.1

```text
; JP1.0 / JP1.1: H211 shim revert (4 instructions)
0202D88C E3A0201F
0202D890 E58D2004
0202D894 E59F15B8
0202D898 E58D2008
```

#### US1.0 / US1.1 / EU1.1

```text
; US1.0 / US1.1 / EU1.1: H211 shim revert (4 instructions)
0202D8B0 E3A0201F
0202D8B4 E58D2004
0202D8B8 E59F1570
0202D8BC E58D2008
```

#### EU1.0

```text
; EU1.0: H211 shim revert (4 instructions)
0202D8A8 E3A0201F
0202D8AC E58D2004
0202D8B0 E59F1570
0202D8B4 E58D2008
```

#### KR1.0

```text
; KR1.0: H211 shim revert (4 instructions)
020365F8 E3A0201F
020365FC E58D2004
02036600 E59F148C
02036604 E58D2008
```

### H211 Separate Color Revert

`H211 Separate Color` の10命令パッチを元に戻す。ダンプから取得した元の命令値。

#### JP1.0 / JP1.1

```text
; JP1.0 / JP1.1: H211 separate color revert (10 instructions)
0202D88C E3A0201F
0202D890 E58D2004
0202D894 E59F15B8
0202D898 E58D2008
0202D89C E5912000
0202D8A0 E3A0105A
0202D8A4 E58D200C
0202D8A8 E58D1010
0202D8AC E3A01011
0202D8B0 E58D1014
```

#### US1.0 / US1.1 / EU1.1

```text
; US1.0 / US1.1 / EU1.1: H211 separate color revert (10 instructions)
0202D8B0 E3A0201F
0202D8B4 E58D2004
0202D8B8 E59F1570
0202D8BC E58D2008
0202D8C0 E5912000
0202D8C4 E3A0105A
0202D8C8 E58D200C
0202D8CC E58D1010
0202D8D0 E3A01011
0202D8D4 E58D1014
```

#### EU1.0

```text
; EU1.0: H211 separate color revert (10 instructions)
0202D8A8 E3A0201F
0202D8AC E58D2004
0202D8B0 E59F1570
0202D8B4 E58D2008
0202D8B8 E5912000
0202D8BC E3A0105A
0202D8C0 E58D200C
0202D8C4 E58D1010
0202D8C8 E3A01011
0202D8CC E58D1014
```

#### KR1.0

```text
; KR1.0: H211 separate color revert (10 instructions)
020365F8 E3A0201F
020365FC E58D2004
02036600 E59F148C
02036604 E58D2008
02036608 E59120A8
0203660C E3A0105A
02036610 E58D200C
02036614 E58D1010
02036618 E3A01011
0203661C E58D1014
```

## No Ammo Only

no-ammo系だけを変える最小候補。32-bit literal writeなので、`0000CCCC` として書く。

| Version | Message | Patch Pattern |
| --- | --- | --- |
| JP1.0 | `アモがない！` | `0202DE68 0000CCCC` |
| JP1.1 | `アモがない！` | `0202DE68 0000CCCC` |
| US1.0 | `AMMO DEPLETED!` | `0202DE44 0000CCCC` |
| US1.1 | `AMMO DEPLETED!` | `0202DE44 0000CCCC` |
| EU1.0 | `AMMO DEPLETED!` | `0202DE3C 0000CCCC` |
| EU1.1 | `AMMO DEPLETED!` | `0202DE44 0000CCCC` |
| KR1.0 | `AMMO ZERO!` | `02036AD4 0000CCCC` |

Cyan例:

```text
; JP1.0 / JP1.1
0202DE68 00007FE0

; US1.0 / US1.1 / EU1.1
0202DE44 00007FE0

; EU1.0
0202DE3C 00007FE0

; KR1.0
02036AD4 00007FE0
```

## Representative Patch Groups

目的別に既定色を変える場合の代表patch point。literal列は `ADDR 0000CCCC` 形式で書く。H211 patch start列は、H211命令列patchの先頭。

| Version | no-ammo | return/base/complete系 | node/acquiring系 | H211 patch start | kill/death通知群 |
| --- | --- | --- | --- | --- | --- |
| JP1.0 | `0202DE68` | `0202DE50` | `02031D0C` | `0202D88C` | `02018268` |
| JP1.1 | `0202DE68` | `0202DE50` | `02031D0C` | `0202D88C` | `02018268` |
| US1.0 | `0202DE44` | `0202DE2C` | `02031BF8` | `0202D8B0` | `02018288` |
| US1.1 | `0202DE44` | `0202DE2C` | `02031BA8` | `0202D8B0` | `0201828C` |
| EU1.0 | `0202DE3C` | `0202DE24` | `02031BA0` | `0202D8A8` | `02018280` |
| EU1.1 | `0202DE44` | `0202DE2C` | `02031BA8` | `0202D8B0` | `0201828C` |
| KR1.0 | `02036AD4` | `02036AC8` | `0203285C` | `020365F8` | `0201A7F8` |

注意:

- literal patchは「新しく投入されるOSDの保存色」を変える方法。
- `H211 Untouched` ではH211 patch start列の命令を書かない。
- H211 match-common shimは4命令を書き換え、色をreturn/base/complete系literalへ合わせる方法。
- `H211 Separate Color` は10命令を書き換え、H211専用色を `LL` / `HH` から組み立てる方法。
- all slot overrideは「runtime slotの `entry+0x10` を毎回塗る」方法。
- 1つのliteralを複数メッセージが共有するため、個別1メッセージだけに絞る場合は各版の構造メモから呼び出し元命令列を追う。
- `entry+0x10` の隣 `entry+0x12` はtimer。色だけ変えるならhalfword writeを使い、32-bitで `entry+0x10` へ書かない。
