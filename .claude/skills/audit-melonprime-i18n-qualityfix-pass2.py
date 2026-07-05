#!/usr/bin/env python3
from __future__ import annotations
import ast
import re
from pathlib import Path

FILES = {
    "main": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"),
    "object": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"),
    "dialogs": Path("src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"),
}

TARGET_LANGS = ["Filipino","Swahili","Amharic","Kazakh","Hebrew","Basque","Slovak","Slovenian","Zulu","Kyrgyz","Assamese","Estonian","Odia","Catalan"]
NONLATIN = ["Amharic","Kazakh","Hebrew","Kyrgyz","Assamese","Odia"]

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
    "DLDI","DSP","HLE","TOML","SDL","Qt","Wi-Fi","VPN","GDB","LAN","GL","MPH","melonDS","MelonPrime","Metroid","Prime","Hunters",
    "Samus","Kanden","Noxus","Spire","Trace","Sylux","Weavel","Vorsaize","Octolith","Octo","Magmaul","Judicator",
    "Imperialist","Volt","Driver","Battlehammer","Battle","Hammer","Shock","Coil","Omega","Cannon","Power","Beam",
    "Morph","Ball","Boost","Crosshair","Headshot","Zoom","scope","Scope","Native","Direct","PostFold","MoonLike",
    "glFinish","DwmFlush","Windows","Steam","JoyToKey","reWASD","Touch","Dual","SnapTap","Joy2KeySupport","Stylus",
    "Power-Up","SoundTest","Gallery","Starship","Adventure","Hunter","License","Biped","Alt","Fire","Slot","Kill","Death",
    "Node","Capture","System","Misc","flags","headshot","face","off","battle","coward","detected","turret","friend","rival","active","bitset",
}

block_re = re.compile(r'(?P<head>\{\s*"(?P<key>(?:\\.|[^"\\])*)"\s*,\s*\{\n)(?P<body>.*?)(?P<tail>\n\s*\}\s*\})', re.S)

def cpp_unescape(lit: str) -> str:
    return ast.literal_eval(lit)

def parse_lang_value(body: str, lang: str) -> str | None:
    mm = re.search(r'\{MenuLangId::' + re.escape(lang) + r',\s*("(?:(?:\\.)|[^"\\])*")\},', body)
    return cpp_unescape(mm.group(1)) if mm else None

def count_lang(text: str, lang: str) -> int:
    return len(re.findall(r'\{MenuLangId::' + re.escape(lang) + r'\s*,', text))

def english_tokens(s: str) -> list[str]:
    return re.findall(r'[A-Za-z][A-Za-z0-9_\-]*', s)

def nontech_english_tokens(s: str) -> list[str]:
    return [t for t in english_tokens(s) if t not in TECH_ALLOW_WORDS and not re.fullmatch(r'[A-Z]{1,4}', t)]

def ratio(s: str, lo: str, hi: str) -> float:
    letters = [ch for ch in s if ch.isalpha()]
    if not letters:
        return 0.0
    return sum(1 for ch in letters if lo <= ch <= hi) / len(letters)

RATIO = {
    "Amharic": lambda s: ratio(s, "\u1200", "\u137F"),
    "Kazakh": lambda s: ratio(s, "\u0400", "\u04FF"),
    "Hebrew": lambda s: ratio(s, "\u0590", "\u05FF"),
    "Kyrgyz": lambda s: ratio(s, "\u0400", "\u04FF"),
    "Assamese": lambda s: ratio(s, "\u0980", "\u09FF"),
    "Odia": lambda s: ratio(s, "\u0B00", "\u0B7F"),
}

def main() -> int:
    failed = False
    for name, path in FILES.items():
        text = path.read_text(encoding="utf-8")
        rows = len(list(block_re.finditer(text)))
        print(f"[INFO] {name}: rows={rows}")
        langs = sorted(set(re.findall(r'MenuLangId::([A-Za-z0-9_]+)', text)))
        for lang in langs:
            c = count_lang(text, lang)
            if c != rows:
                print(f"[FAIL] {name}: {lang} coverage {c}/{rows}")
                failed = True
        if name != "object":
            for lang in TARGET_LANGS:
                pref_hits = 0
                heavy_hits = 0
                for m in block_re.finditer(text):
                    val = parse_lang_value(m.group("body"), lang)
                    if not val:
                        continue
                    if any(val.startswith(pref + " ") or val == pref for pref in NATIVE_PREFIXES.get(lang, [])):
                        pref_hits += 1
                    if lang in NONLATIN:
                        toks = nontech_english_tokens(val)
                        if len(toks) >= 5 and RATIO[lang](val) < 0.20:
                            heavy_hits += 1
                if pref_hits:
                    print(f"[FAIL] {name}: {lang} prefix fallback remains: {pref_hits}")
                    failed = True
                if heavy_hits:
                    print(f"[FAIL] {name}: {lang} non-technical English-heavy rows remain: {heavy_hits}")
                    failed = True
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
