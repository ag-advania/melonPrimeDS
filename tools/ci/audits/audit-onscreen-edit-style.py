#!/usr/bin/env python3
"""Audit the On-Screen Edit Classic/Retro routing contract.

This is intentionally a small source-level audit.  It checks the policy
boundaries that are easy to regress without attempting to execute Qt or the
DS overlay: typed config normalization, settings persistence, one-session
style capture, generic-panel gating, and Crosshair special-case precedence.
"""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []

    definition = read("src/frontend/qt_sdl/MelonPrimeDef.h")
    config = read("src/frontend/qt_sdl/Config.cpp")
    state = read("src/frontend/qt_sdl/MelonPrimeHudRenderConfig.inc")
    editor_header = read("src/frontend/qt_sdl/MelonPrimeHudEdit.h")
    input_source = read("src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenInput.inc")
    draw_source = read("src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenDraw.inc")
    unity_source = read("src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenUnity.inc")
    screen_init = read("src/frontend/qt_sdl/MelonPrimeHudScreenCppInit.inc")
    settings_ui = read("src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.ui")
    settings_source = read("src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp")
    settings_config = read("src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp")
    settings_preview = read("src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigPreview.cpp")
    schema = read("src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc")

    require(
        "enum class OnScreenEditStyle" in definition
        and "Classic = 0" in definition
        and "Retro = 1" in definition,
        "typed Classic/Retro enum is missing",
        failures,
    )
    require(
        "value == static_cast<int>(OnScreenEditStyle::Retro)" in definition
        and "? OnScreenEditStyle::Retro" in definition
        and ": OnScreenEditStyle::Classic" in definition,
        "invalid style values must normalize to Classic",
        failures,
    )
    require(
        '"Instance*.Metroid.UI.OnScreenEditStyle"' in config,
        "OnScreenEditStyle default is missing from the MelonPrime config defaults",
        failures,
    )
    require(
        "OnScreenEditStyle editStyle = OnScreenEditStyle::Classic" in state
        and state.count("OnScreenEditStyle editStyle") == 1,
        "HUD state must own exactly one captured style value",
        failures,
    )
    require(
        "CustomHud_GetOnScreenEditStyle" in editor_header
        and "CustomHud_IsCrosshairElement" in editor_header,
        "style/crosshair routing API is missing",
        failures,
    )
    require(
        "s_editStyle         = ResolveOnScreenEditStyle(cfg)" in input_source,
        "style must be resolved when On-Screen Edit opens",
        failures,
    )
    require(
        input_source.count("s_editStyle == OnScreenEditStyle::Retro") >= 2
        and "s_editStyle != OnScreenEditStyle::Retro" in input_source
        and "s_editStyle == OnScreenEditStyle::Retro" in draw_source,
        "generic Retro drawing/input gates are incomplete",
        failures,
    )
    require(
        "kShowDsEditPropsPanel" not in input_source
        and "kShowDsEditPropsPanel" not in draw_source
        and "kShowDsEditPropsPanel" not in unity_source,
        "compile-time Retro policy flag remains",
        failures,
    )
    crosshair_pos = screen_init.find("CustomHud_IsCrosshairElement")
    style_pos = screen_init.find("CustomHud_GetOnScreenEditStyle")
    require(
        crosshair_pos >= 0 and style_pos > crosshair_pos,
        "Crosshair must be checked before generic Classic/Retro routing",
        failures,
    )
    require(
        "comboMetroidOnScreenEditStyle" in settings_ui
        and "On-Screen Edit Style" in settings_ui
        and "Choose the property editing interface used by On-Screen Edit." in settings_ui
        and "Crosshair editing uses its dedicated interface." in settings_ui,
        "settings UI does not expose the style and its Crosshair description",
        failures,
    )
    require(
        "setItemData" in settings_source
        and "OnScreenEditStyle::Classic" in settings_source
        and "OnScreenEditStyle::Retro" in settings_source,
        "style ComboBox values must use explicit enum item data",
        failures,
    )
    require(
        "CfgKey::OnScreenEditStyle" in settings_config
        and "CfgKey::OnScreenEditStyle" in settings_preview
        and "cOnScreenEditStyle" in settings_preview,
        "style persistence/snapshot wiring is incomplete",
        failures,
    )
    require(
        "OnScreenEditStyle" not in schema,
        "editor style must not enter the HUD preset property schema",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        return 1

    print("PASS: On-Screen Edit Classic/Retro routing contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
