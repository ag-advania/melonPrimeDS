#!/usr/bin/env python3
"""Extract all missing translation tasks for all newly added MelonPrime languages.

Usage:
  python3 tools/melonprime_extract_all_ai_translation_tasks.py --repo .

Outputs:
  artifacts/localization-ai/all_new_languages.tasks.jsonl
  artifacts/localization-ai/by-language/<Language>.tasks.jsonl

This only extracts tasks. It does not call any MT API.
"""

from __future__ import annotations

import argparse
import html
import json
import re
from pathlib import Path

RAW_STRING_RE = re.compile(r'R"(?P<delim>[A-Za-z0-9_]*)\(')
ROW_START_RE = re.compile(r'\{\s*"(?P<key>(?:\\.|[^"\\])*)"\s*,\s*\{', re.S)
LANG_VALUE_RE = re.compile(r'\{MenuLangId::(?P<lang>[A-Za-z0-9_]+),\s*"(?P<text>(?:\\.|[^"\\])*)"\},')

OBJECT_SOURCE_OVERRIDES = {
    "lblMetroidLowLatencyAimDesc": (
        "Immediate Sync uses the low-latency ARM9 hook to sync currentAim to targetAim at the hook point "
        "and rebuild the aim basis. MoonLike Aim applies small aim movements immediately and limits only "
        "large aim jumps with a max-step chase. Requires Disable Aim Smoothing."
    ),
    "lblMetroidZoomAimScaleDesc": (
        "Applies only while the game's native zoom state is active. 100% keeps normal mouse sensitivity; "
        "lower values slow down zoom aiming and higher values speed it up."
    ),
    "lblMetroidDisablePickingUpSpecificItemsDesc": (
        "When enabled, selected power-ups are still picked up and removed, but their player-side effects are skipped. "
        "Double Damage, Cloak, and DEATH ALT timers, flags, HUD, sounds, and visual effects are not applied. "
        "This setting only affects you and bots; online opponents are not changed."
    ),
    "lblMetroidWeaponSwitchMethodDesc": (
        "Checked uses the game's native TryEquipWeapon path through an ARM9 hook. Unchecked keeps the older "
        "simulated touch/menu weapon switching path for compatibility testing."
    ),
    "lblMetroidBipedFireMethodDesc": (
        "Checked sets the fire input-helper result true at the game's Biped fire edge hook, letting the original "
        "cooldown, ammo, projectile, HUD, and SFX path run naturally. Legacy Method keeps the older DS input/"
        "ImmediateInputEdgeOverlay fire path."
    ),
    "lblMetroidZoomMethodDesc": (
        "New Method reads the game's zoom binding table, so Touch and Dual presets can map zoom to different "
        "DS buttons. It is also slightly lower latency than Legacy Method. If both boxes are unchecked, "
        "Legacy Method always drives the fixed R button like the older input path."
    ),
    "lblMetroidZoomMethod2Desc": (
        "New Method 2 toggles native zoom state through SetPlayerScopeZoom on each press. Mutually exclusive "
        "with New Method for Zoom."
    ),
}

def decode_cpp_escape(s: str, i: int) -> tuple[str, int]:
    if i >= len(s):
        return "\\", i
    ch = s[i]
    simple = {
        "n": "\n",
        "r": "\r",
        "t": "\t",
        '"': '"',
        "'": "'",
        "\\": "\\",
        "0": "\0",
    }
    if ch in simple:
        return simple[ch], i + 1
    if ch == "x":
        j = i + 1
        while j < len(s) and s[j] in "0123456789abcdefABCDEF":
            j += 1
        if j > i + 1:
            return chr(int(s[i + 1:j], 16)), j
    if ch == "u" and i + 4 < len(s):
        digits = s[i + 1:i + 5]
        if all(c in "0123456789abcdefABCDEF" for c in digits):
            return chr(int(digits, 16)), i + 5
    if ch == "U" and i + 8 < len(s):
        digits = s[i + 1:i + 9]
        if all(c in "0123456789abcdefABCDEF" for c in digits):
            return chr(int(digits, 16)), i + 9
    return ch, i + 1

def cpp_unescape(s: str) -> str:
    chars: list[str] = []
    i = 0
    while i < len(s):
        if s[i] == "\\":
            decoded, i = decode_cpp_escape(s, i + 1)
            chars.append(decoded)
            continue
        chars.append(s[i])
        i += 1
    return "".join(chars)

def find_array_body(text: str, name: str) -> str:
    marker = re.search(rf"\b{name}\s*\[\]\s*=\s*\{{", text)
    if not marker:
        raise RuntimeError(f"{name} array not found")
    start = marker.end() - 1
    depth = 0
    in_string = False
    esc = False
    i = start
    while i < len(text):
        ch = text[i]
        if in_string:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_string = False
        else:
            if ch == '"':
                in_string = True
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[start + 1:i]
        i += 1
    raise RuntimeError(f"{name} did not terminate")

def split_rows(body: str):
    rows = []
    depth = 0
    start = None
    in_string = False
    esc = False
    for i,ch in enumerate(body):
        if in_string:
            if esc: esc = False
            elif ch == "\\": esc = True
            elif ch == '"': in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                rows.append(body[start:i+1])
                start = None
    return rows

def parse_rows_in_array(path: Path, array_name: str, surface: str):
    text = path.read_text(encoding="utf-8")
    body = find_array_body(text, array_name)
    return parse_rows_in_text(body, surface)

def parse_rows_in_text(body: str, surface: str):
    parsed = []
    for row in split_rows(body):
        m = ROW_START_RE.search(row)
        if not m:
            continue
        key = cpp_unescape(m.group("key"))
        values = {mm.group("lang"): cpp_unescape(mm.group("text")) for mm in LANG_VALUE_RE.finditer(row)}
        parsed.append({"surface": surface, "key": key, "values": values})
    return parsed

def collect_ui_object_sources(repo: Path) -> dict[str, str]:
    sources = dict(OBJECT_SOURCE_OVERRIDES)
    widget_re = re.compile(
        r'<widget\b[^>]*\bname="(?P<name>[^"]+)"[^>]*>(?P<body>.*?)</widget>',
        re.S,
    )
    string_re = re.compile(
        r'<property name="(?:text|toolTip|whatsThis)">\s*<string[^>]*>(?P<text>.*?)</string>',
        re.S,
    )
    for path in (repo / "src/frontend/qt_sdl").glob("**/*.ui"):
        text = path.read_text(encoding="utf-8")
        for widget in widget_re.finditer(text):
            name = widget.group("name")
            if name in sources:
                continue
            strings = [
                html.unescape(re.sub(r"\s+", " ", m.group("text")).strip())
                for m in string_re.finditer(widget.group("body"))
            ]
            strings = [s for s in strings if s]
            if strings:
                sources[name] = strings[0]
    return sources

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", type=Path, default=Path("."))
    args = ap.parse_args()
    repo = args.repo.resolve()

    metadata = json.loads((repo / "tools/melonprime_all_new_language_metadata.json").read_text(encoding="utf-8"))
    languages = [x["id"] for x in metadata]
    meta_by_id = {x["id"]: x for x in metadata}

    exact_path = repo / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"
    object_path = repo / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"
    # Nested #include pulled in by MelonPrimeTranslations.inc (melonDS settings
    # dialog strings). It lives in qt_sdl/ root, not the MelonPrimeLocalization/
    # subdir, and is a flat sequence of row entries with no "kTranslations[] = {"
    # wrapper of its own (it is spliced into the parent array via #include), so
    # it is parsed directly rather than through find_array_body().
    melonds_dialogs_path = repo / "src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"

    exact = parse_rows_in_array(exact_path, "kTranslations", "exact")
    exact += parse_rows_in_text(melonds_dialogs_path.read_text(encoding="utf-8"), "exact")
    obj = parse_rows_in_array(object_path, "kObjectTextTranslations", "object")
    object_sources = collect_ui_object_sources(repo)

    outdir = repo / "artifacts/localization-ai"
    by_lang = outdir / "by-language"
    by_lang.mkdir(parents=True, exist_ok=True)

    all_out = outdir / "all_new_languages.tasks.jsonl"
    per_lang_files = {lang: (by_lang / f"{lang}.tasks.jsonl").open("w", encoding="utf-8") for lang in languages}
    total = 0
    try:
        with all_out.open("w", encoding="utf-8") as all_f:
            for row in exact + obj:
                source_text = row["key"]
                if row["surface"] == "object":
                    source_text = row["values"].get("English", object_sources.get(row["key"], row["key"]))

                for lang in languages:
                    if row["values"].get(lang):
                        continue
                    m = meta_by_id[lang]
                    task = {
                        "surface": row["surface"],
                        "key": row["key"],
                        "english": source_text,
                        "language_id": lang,
                        "language_code": m["code"],
                        "display": m["display"],
                        "text": "",
                        "notes": "Translate the English field into the target language. Keep placeholders (%1, %2), code-like tokens, ROM/HUD/DS/GBA/MPH names, arrows, punctuation semantics, and accelerator symbols intact."
                    }
                    line = json.dumps(task, ensure_ascii=False)
                    all_f.write(line + "\n")
                    per_lang_files[lang].write(line + "\n")
                    total += 1
    finally:
        for f in per_lang_files.values():
            f.close()

    print(f"[PASS] wrote {total} tasks")
    print(f"[INFO] all: {all_out}")
    print(f"[INFO] by-language: {by_lang}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
