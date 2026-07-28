#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# MELONPRIME_FINAL_DOC_AUDIT_V18
"""Focused V18 Morph Ball Boost mode-control localization audit."""
from pathlib import Path
import re, sys

ROOT = Path(__file__).resolve().parents[4]
PATH = ROOT / "src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslationsMouseBoost.inc"
KEYS = [
    "Morph Ball Boost Required Mouse Movement",
    "Mouse mode only. Range 1–46339; the MelonPrime default is 90. Smaller values trigger more easily; larger values require more movement. This raw-input value is used only with the custom threshold and is not MPH's native swipe threshold.",
    "Disable Morph Ball Swipe Boost",
    "Mouse mode only. Check this to disable mouse swipe boost. Right-click R boost, the Shift auto-cycle, and Stylus Mode are unchanged.",
    "Use Custom Raw Mouse Movement Threshold",
    "Off uses only the game's internal swipe amount. On requires the current frame's raw mouse movement to reach the value below and uses that same vector for the native swipe pulse.",
]
text = PATH.read_text(encoding="utf-8")
errors=[]
for key in KEYS:
    marker='        "'+key.replace('\\','\\\\').replace('"','\\"')+'",\n'
    start=text.find(marker)
    if start<0:
        errors.append("missing key: "+key); continue
    next_start=text.find('\n    {\n        "', start+len(marker))
    block=text[start: next_start if next_start>=0 else len(text)]
    langs=re.findall(r'\{MenuLangId::([A-Za-z0-9_]+),\s*"',block)
    if len(langs)!=76: errors.append(f"{key}: expected 76 languages, found {len(langs)}")
    if len(set(langs))!=len(langs): errors.append(f"{key}: duplicate language IDs")
if "Range 0–46339; 0 disables mouse swipe boost" in text:
    errors.append("obsolete V12 zero-disable translation remains")
if "90 matches the game's default" in text or "90がゲーム標準" in text:
    errors.append("obsolete native-game-default wording remains")
expected_ja = "マウスモード専用。範囲は1～46339で、MelonPrimeのデフォルトは90です。小さい値ほど発動しやすく、大きい値ほど多くの移動が必要です。このRaw入力値はカスタムしきい値が有効な場合だけ使用され、MPH本来のスワイプしきい値を表すものではありません。"
if expected_ja not in text:
    errors.append("updated Japanese required-movement description missing")
if errors:
    for e in errors: print("ERROR:",e)
    sys.exit(1)
print("[OK] V18 Morph Ball Boost localization structure: 6 keys x 76 languages")
