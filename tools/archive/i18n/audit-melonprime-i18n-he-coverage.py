#!/usr/bin/env python3
from __future__ import annotations
import re
from pathlib import Path

FILES = [
    Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"),
    Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"),
    Path("src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"),
]

def count_lang(text: str, lang: str) -> int:
    return len(re.findall(r'\{MenuLangId::' + re.escape(lang) + r'\s*,', text))

def count_rows(text: str) -> int:
    return len(re.findall(r'\{\s*"(?:(?:\\.)|[^"\\])*"\s*,\s*\{', text))

def main() -> int:
    failed = False
    langs = ["Filipino", "Swahili", "Croatian", "Icelandic", "Hindi", "Bengali", "Urdu", "Punjabi", "Telugu", "Persian", "Malay", "Marathi", "Tamil", "Gujarati", "Kannada", "Odia", "Malayalam", "Burmese", "Uzbek", "Azerbaijani", "Nepali", "Sinhala", "Khmer", "Lao", "Armenian", "Albanian", "Georgian", "Bosnian", "Serbian", "Macedonian", "Bulgarian", "Belarusian", "Maltese", "Mongolian", "Latvian", "Lithuanian", "Afrikaans", "Irish", "Assamese", "Amharic", "Estonian", "Kazakh", "Catalan", "Kyrgyz", "Slovak", "Slovenian", "Zulu", "Basque", "Hebrew"]
    for path in FILES:
        text = path.read_text(encoding="utf-8")
        rows = count_rows(text)
        zh_hant = count_lang(text, "ChineseTraditional")
        zh_hk = count_lang(text, "ChineseHongKong")
        counts = {lang: count_lang(text, lang) for lang in langs}
        print(f"[INFO] {path}: rows={rows}, zh-Hant={zh_hant}, zh-HK={zh_hk}, " + ", ".join(f"{k}={v}" for k,v in counts.items()))
        if zh_hant == 0 or zh_hk != zh_hant:
            print(f"[FAIL] {path}: zh-HK coverage must match zh-Hant")
            failed = True
        else:
            print(f"[PASS] {path}: zh-HK coverage complete")
        for lang, count in counts.items():
            if count != rows:
                print(f"[FAIL] {path}: {lang} coverage must match row count")
                failed = True
            else:
                print(f"[PASS] {path}: {lang} coverage complete")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
