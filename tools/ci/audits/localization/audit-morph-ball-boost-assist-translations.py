#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# MELONPRIME_FINAL_DOC_AUDIT_V18
"""Focused V18 Morph Ball Boost mode-control localization audit."""
from pathlib import Path
import re, sys

ROOT = Path(__file__).resolve().parents[4]
PATH = ROOT / "src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslationsMouseBoost.inc"
UI_PATH = ROOT / "src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp"
KEYS = [
    "Morph Ball Boost Required Mouse Movement",
    "Mouse mode only. Range 1–46339; the MelonPrime default is 90. Smaller values trigger more easily; larger values require more movement. This raw-input value is used only with the custom threshold and is not MPH's native swipe threshold.",
    "Disable Morph Ball Swipe Boost",
    "Mouse mode only. Check this to disable mouse swipe boost. Right-click R boost, the Shift auto-cycle, and Stylus Mode are unchanged.",
    "Use Custom Raw Mouse Movement Threshold",
    "Off uses only the game's internal swipe amount. On requires the current frame's raw mouse movement to reach the value below and uses that same vector for the native swipe pulse.",
]
# Grade A/B terminology comes from:
# docs/development/localization/metroid-prime-hunters-terminology-reference.md
MORPH_BALL_TERMS = {
    "Japanese": "モーフボール",
    "German": "Morph-Ball",
    "Spanish": "MORFOSFERA",
    "French": "BOULE MORPHING",
    "Italian": "MORFOSFERA",
    "Korean": "모프볼",
    "Portuguese": "Morph Ball",
    "Russian": "Морфошар",
    "Swedish": "Morph Ball",
    "ChineseSimplified": "变球",
    "ChineseTraditional": "變球",
    "ChineseHongKong": "變球",
    "Sinhala": "මෝර්ෆ් බෝල",
}
OFFICIAL_BOOST_TERMS = {
    "Japanese": "ブースト",
    "German": "BOOST",
    "Spanish": "TURBO",
    "French": "BOOST",
    "Italian": "TURBO",
}
text = PATH.read_text(encoding="utf-8")
ui_text = re.sub(r'"\s*"', "", UI_PATH.read_text(encoding="utf-8"))
errors=[]
for key in KEYS:
    marker='        "'+key.replace('\\','\\\\').replace('"','\\"')+'",\n'
    start=text.find(marker)
    if start<0:
        errors.append("missing key: "+key); continue
    next_start=text.find('\n    {\n        "', start+len(marker))
    block=text[start: next_start if next_start>=0 else len(text)]
    entries=re.findall(
        r'\{MenuLangId::([A-Za-z0-9_]+),\s*"((?:\\.|[^"\\])*)"\},',
        block,
    )
    langs=[lang for lang, _ in entries]
    if len(langs)!=76: errors.append(f"{key}: expected 76 languages, found {len(langs)}")
    if len(set(langs))!=len(langs): errors.append(f"{key}: duplicate language IDs")
    placeholders=[lang for lang, value in entries if value == key]
    if placeholders:
        errors.append(f"{key}: English source placeholder remains for {', '.join(placeholders)}")
    sinhala=dict(entries).get("Sinhala", "")
    sinhala_without_identifiers=sinhala
    for identifier in ("MelonPrime", "MPH", "Shift", "R"):
        sinhala_without_identifiers=sinhala_without_identifiers.replace(identifier, "")
    latin_fragments=re.findall(r"[A-Za-z]+", sinhala_without_identifiers)
    if latin_fragments:
        errors.append(
            f"{key}: untranslated Latin text remains in Sinhala: {', '.join(latin_fragments)}"
        )
    if key == KEYS[1]:
        stale=[lang for lang, value in entries if "MelonPrime" not in value or "MPH" not in value]
        if stale:
            errors.append(
                f"{key}: current MelonPrime/MPH threshold semantics missing for {', '.join(stale)}"
            )
    if key not in ui_text:
        errors.append(f"UI source does not use the exact catalog key: {key}")
if "Range 0–46339; 0 disables mouse swipe boost" in text:
    errors.append("obsolete V12 zero-disable translation remains")
if "90 matches the game's default" in text or "90がゲーム標準" in text:
    errors.append("obsolete native-game-default wording remains")
expected_ja = "マウスモード専用。範囲は1～46339で、MelonPrimeのデフォルトは90です。小さい値ほど発動しやすく、大きい値ほど多くの移動が必要です。このRaw入力値はカスタムしきい値が有効な場合だけ使用され、MPH本来のスワイプしきい値を表すものではありません。"
if expected_ja not in text:
    errors.append("updated Japanese required-movement description missing")

morph_rows=[]
for shard in PATH.parent.glob("MelonPrime*Translations*.inc"):
    shard_text=shard.read_text(encoding="utf-8")
    for block in re.split(r'(?=^    \{\n        ")', shard_text, flags=re.MULTILINE):
        key_match=re.match(r'    \{\n        "((?:\\.|[^"\\])*)",', block)
        if not key_match or "Morph Ball" not in key_match.group(1):
            continue
        key=key_match.group(1)
        entries=re.findall(
            r'\{MenuLangId::([A-Za-z0-9_]+),\s*"((?:\\.|[^"\\])*)"\},',
            block,
        )
        values=dict(entries)
        morph_rows.append((shard.name, key))
        for lang, term in MORPH_BALL_TERMS.items():
            if term.casefold() not in values.get(lang, "").casefold():
                errors.append(
                    f"{shard.name}: {key}: preferred terminology missing for {lang}: {term}"
                )
        for lang, term in OFFICIAL_BOOST_TERMS.items():
            if term.casefold() not in values.get(lang, "").casefold():
                errors.append(
                    f"{shard.name}: {key}: official manual BOOST term missing for {lang}: {term}"
                )
if len(morph_rows) < 4:
    errors.append(f"expected at least 4 catalog-wide Morph Ball rows, found {len(morph_rows)}")
if errors:
    for e in errors: print("ERROR:",e)
    sys.exit(1)
print(
    "[OK] V18 Morph Ball Boost localization quality: "
    f"6 settings keys x 76 languages; {len(morph_rows)} catalog-wide terminology rows"
)
