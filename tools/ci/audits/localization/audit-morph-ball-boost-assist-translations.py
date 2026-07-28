#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# MELONPRIME_DISABLE_CHECKBOX_SEMANTICS_V15
"""Focused V15 Morph Ball Boost mode-control localization audit."""
from pathlib import Path
import re, sys

ROOT = Path(__file__).resolve().parents[4]
PATH = ROOT / "src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslationsMouseBoost.inc"
KEYS = [
    "Morph Ball Boost Required Mouse Movement",
    "Mouse mode only. Range 1–46339; 90 matches the game's default. Smaller values trigger more easily; larger values require more movement. This value is used only while the custom raw threshold option is enabled.",
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
if errors:
    for e in errors: print("ERROR:",e)
    sys.exit(1)
print("[OK] V15 Morph Ball Boost localization structure: 6 keys x 76 languages")
