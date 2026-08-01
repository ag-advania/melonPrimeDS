#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verify MPH weapon names against the regional Nintendo manuals."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[4]
CATALOG_DIR = ROOT / "src/frontend/qt_sdl/MelonPrimeLocalization/inc"
COLORS_PATH = CATALOG_DIR / "MelonPrimeTranslationsColorsWeapons.inc"
HOTKEYS_PATH = CATALOG_DIR / "MelonPrimeTranslationsHotkeys.inc"
INPUT_PATH = ROOT / "src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h"
REFERENCE_PATH = (
    ROOT
    / "docs/development/localization/"
    / "metroid-prime-hunters-terminology-reference.md"
)

LANGUAGES = ("Japanese", "German", "Spanish", "French", "Italian")

# Nintendo's Japanese, German, Spanish, French, and Italian MPH manuals.
OFFICIAL_NAMES = {
    "Power Beam": (
        "パワービーム",
        "Power Beam",
        "Rayo",
        "Rayon de puissance",
        "Raggio Energia",
    ),
    "Missile": (
        "ミサイル",
        "Raketenwerfer",
        "Lanzamisiles",
        "Lance-missiles",
        "Missili",
    ),
    "Volt Driver": (
        "ボルトドライバー",
        "Volt Driver",
        "Voltric",
        "Voltar",
        "Raggio Voltaico",
    ),
    "Magmaul": (
        "マグモール",
        "Magmaul",
        "Magmaul",
        "Magma",
        "Raggio Magmatico",
    ),
    "Imperialist": (
        "インペリアリスト",
        "Imperialist",
        "Imperialist",
        "Impérialiste",
        "Raggio Imperium",
    ),
    "Judicator": (
        "ジュディケイター",
        "Judicator",
        "Judicator",
        "Justicier",
        "Raggio del Giudizio",
    ),
    "Shock Coil": (
        "ショックコイル",
        "Shock Coil",
        "Neutrinarm",
        "Hélicoïchoc",
        "Raggio Shock",
    ),
    "Battlehammer": (
        "バトルハンマー",
        "Battlehammer",
        "Destruktor",
        "Marteau",
        "Raggio da Guerra",
    ),
}

# Omega Cannon is absent from the manuals' six-subweapon table. These are the
# repository's preferred display forms from the terminology reference, not a
# claim that the five regional ROMs use these exact strings.
PREFERRED_OMEGA_NAMES = (
    "オメガキャノン",
    "Omega-Kanone",
    "Cañón Omega",
    "Canon Oméga",
    "Cannone Omega",
)

HOTKEY_KEYS = {
    "Missile": "[Metroid] (Mouse 4, Side Bottom) Weapon Missile",
    "Shock Coil": "[Metroid] (1) Weapon 1. Shock Coil",
    "Magmaul": "[Metroid] (2) Weapon 2. Magmaul",
    "Judicator": "[Metroid] (3) Weapon 3. Judicator",
    "Imperialist": "[Metroid] (4) Weapon 4. Imperialist",
    "Battlehammer": "[Metroid] (5) Weapon 5. Battlehammer",
    "Volt Driver": "[Metroid] (6) Weapon 6. Volt Driver",
}
OMEGA_HOTKEY_KEY = (
    "[Metroid] (R) Affinity Weapon (Last used Weapon/Omega cannon)"
)

ROW_PATTERN = re.compile(
    r'^    \{\n'
    r'        "((?:\\.|[^"\\])*)",\n'
    r'        \{\n'
    r'(?P<body>.*?)'
    r'^        \}\n'
    r'    \}',
    re.MULTILINE | re.DOTALL,
)
ENTRY_PATTERN = re.compile(
    r'\{MenuLangId::([A-Za-z0-9_]+),\s*"((?:\\.|[^"\\])*)"\},'
)


def load_rows(path: Path) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for match in ROW_PATTERN.finditer(path.read_text(encoding="utf-8")):
        key = match.group(1)
        if key in rows:
            raise ValueError(f"{path.name}: duplicate source key: {key}")
        rows[key] = dict(ENTRY_PATTERN.findall(match.group("body")))
    return rows


def expected_by_language(names: tuple[str, ...]) -> dict[str, str]:
    return dict(zip(LANGUAGES, names, strict=True))


def main() -> int:
    errors: list[str] = []
    colors = load_rows(COLORS_PATH)
    hotkeys = load_rows(HOTKEYS_PATH)

    for weapon, names in OFFICIAL_NAMES.items():
        expected = expected_by_language(names)
        values = colors.get(weapon)
        if values is None:
            errors.append(f"{COLORS_PATH.name}: missing row: {weapon}")
        else:
            for language, term in expected.items():
                if values.get(language) != term:
                    errors.append(
                        f"{COLORS_PATH.name}: {weapon}: {language} must be {term!r}, "
                        f"found {values.get(language)!r}"
                    )

        hotkey_key = HOTKEY_KEYS.get(weapon)
        if hotkey_key is not None:
            hotkey_values = hotkeys.get(hotkey_key)
            if hotkey_values is None:
                errors.append(f"{HOTKEYS_PATH.name}: missing row: {hotkey_key}")
            else:
                for language, term in expected.items():
                    if term not in hotkey_values.get(language, ""):
                        errors.append(
                            f"{HOTKEYS_PATH.name}: {hotkey_key}: "
                            f"{language} is missing official weapon name {term!r}"
                        )

    # Keep the two legacy catalog aliases usable, but never display the old
    # identifier-style spellings.
    for legacy_key, official_key in (
        ("VoltDriver", "Volt Driver"),
        ("ShockCoil", "Shock Coil"),
    ):
        values = colors.get(legacy_key)
        if values is None:
            errors.append(f"{COLORS_PATH.name}: missing legacy row: {legacy_key}")
            continue
        expected = expected_by_language(OFFICIAL_NAMES[official_key])
        for language, term in expected.items():
            if values.get(language) != term:
                errors.append(
                    f"{COLORS_PATH.name}: {legacy_key}: "
                    f"{language} must display {term!r}"
                )
        stale = [language for language, value in values.items() if legacy_key in value]
        if stale:
            errors.append(
                f"{COLORS_PATH.name}: {legacy_key}: identifier spelling remains for "
                + ", ".join(stale)
            )

    omega_expected = expected_by_language(PREFERRED_OMEGA_NAMES)
    for path, values in (
        (COLORS_PATH, colors.get("Omega Cannon")),
        (HOTKEYS_PATH, hotkeys.get(OMEGA_HOTKEY_KEY)),
    ):
        if values is None:
            errors.append(f"{path.name}: missing Omega Cannon row")
            continue
        for language, term in omega_expected.items():
            if term not in values.get(language, ""):
                errors.append(
                    f"{path.name}: preferred Omega Cannon form missing for "
                    f"{language}: {term!r}"
                )

    input_text = INPUT_PATH.read_text(encoding="utf-8")
    for source_label in (
        HOTKEY_KEYS["Shock Coil"],
        HOTKEY_KEYS["Volt Driver"],
    ):
        if source_label not in input_text:
            errors.append(f"{INPUT_PATH.name}: missing source label: {source_label}")
    for stale in ("Weapon 1. ShockCoil", "Weapon 6. VoltDriver"):
        if stale in input_text:
            errors.append(f"{INPUT_PATH.name}: stale identifier spelling: {stale}")

    reference_text = REFERENCE_PATH.read_text(encoding="utf-8")
    for term in (
        "Raketenwerfer",
        "Lanzamisiles",
        "Lance-missiles",
        "Raggio da Guerra",
        "Omega Cannonは提供された取扱説明書の6種サブウェポン一覧に含まれない",
    ):
        if term not in reference_text:
            errors.append(f"{REFERENCE_PATH.name}: missing evidence term: {term}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        return 1

    print(
        "[OK] MPH weapon terminology: 8 manual-backed names across 5 regions; "
        "7 direct hotkeys; preferred Omega Cannon forms"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
