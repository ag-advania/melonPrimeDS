#!/usr/bin/env python3
from __future__ import annotations
import ast
import re
import unicodedata
from pathlib import Path

FILES = {
    "main": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"),
    "object": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"),
    "dialogs": Path("src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"),
}

TARGET_LANGS = [
    "Filipino","Swahili","Amharic","Kazakh","Hebrew","Basque","Slovak","Slovenian","Zulu",
    "Kyrgyz","Assamese","Estonian","Odia","Catalan",
]

NATIVE_PREFIXES = {
    "Amharic": ["አማርኛ:"],
    "Kazakh": ["Қазақша:"],
    "Hebrew": ["עברית:"],
    "Basque": ["Euskara:"],
    "Slovak": ["Slovenčina:"],
    "Slovenian": ["Slovenščina:"],
    "Zulu": ["isiZulu:"],
    "Filipino": ["Filipino:"],
    "Swahili": ["Kiswahili:"],
    "Kyrgyz": ["Кыргызча:"],
    "Assamese": ["অসমীয়া:"],
    "Estonian": ["Eesti:"],
    "Odia": ["ଓଡ଼ିଆ:"],
    "Catalan": ["Català:"],
}

TECH_ALLOW_WORDS = {
    "OpenGL","VSync","ARM9","ARM7","BIOS","DS","DSi","JIT","HUD","OSD","FPS","ROM","RAM","CPU","GPU","SD","MAC","IP",
    "DLDI","DSP","HLE","TOML","SDL","Qt","Wi-Fi","online","offline","melonDS","MelonPrime","Metroid","Prime","Hunters",
    "Samus","Kanden","Noxus","Spire","Trace","Sylux","Weavel","Vorsaize","Octolith","Octo","Magmaul","Judicator",
    "Imperialist","Volt","Driver","Battlehammer","Battle","Hammer","Shock","Coil","Omega","Cannon","Power","Beam",
    "Morph","Ball","Boost","Crosshair","Headshot","Zoom","scope","Scope","Native","Direct","PostFold","MoonLike",
    "glFinish","DwmFlush","Windows","Steam","JoyToKey","reWASD","Touch","Dual",
}

block_re = re.compile(r'(?P<head>\{\s*"(?P<key>(?:\\.|[^"\\])*)"\s*,\s*\{\n)(?P<body>.*?)(?P<tail>\n\s*\}\s*\})', re.S)

def cpp_unescape(lit: str) -> str:
    return ast.literal_eval(lit)

def count_lang(text: str, lang: str) -> int:
    return len(re.findall(r'\{MenuLangId::' + re.escape(lang) + r'\s*,', text))

def parse_lang_value(body: str, lang: str) -> str | None:
    mm = re.search(r'\{MenuLangId::' + re.escape(lang) + r',\s*("(?:(?:\\.)|[^"\\])*")\},', body)
    return cpp_unescape(mm.group(1)) if mm else None

def english_tokens(s: str) -> list[str]:
    return re.findall(r'[A-Za-z][A-Za-z0-9_\-]*', s)

def nontech_english_tokens(s: str) -> list[str]:
    return [t for t in english_tokens(s) if t not in TECH_ALLOW_WORDS and not re.fullmatch(r'[A-Z]{1,4}', t)]

def ratio(s: str, lo: str, hi: str) -> float:
    letters = [ch for ch in s if ch.isalpha()]
    if not letters:
        return 0.0
    return sum(1 for ch in letters if lo <= ch <= hi) / len(letters)

def main() -> int:
    failed = False
    for name, path in FILES.items():
        text = path.read_text(encoding="utf-8")
        rows = len(list(block_re.finditer(text)))
        print(f"[INFO] {name}: rows={rows}")
        langs = re.findall(r'MenuLangId::([A-Za-z0-9_]+)', text)
        for lang in sorted(set(langs)):
            c = count_lang(text, lang)
            if c != rows:
                print(f"[FAIL] {name}: {lang} coverage {c}/{rows}")
                failed = True
        for lang in TARGET_LANGS:
            prefixes = NATIVE_PREFIXES.get(lang, [])
            prefix_hits = 0
            for m in block_re.finditer(text):
                val = parse_lang_value(m.group("body"), lang)
                if val and any(val.startswith(p + " ") or val == p for p in prefixes):
                    prefix_hits += 1
            if prefix_hits:
                print(f"[FAIL] {name}: {lang} prefix fallback remains: {prefix_hits}")
                failed = True
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
