#!/usr/bin/env python3
"""Generate the Phase 2a HUD property schema and drift report.

The generator treats Config.cpp defaults as the authoritative key list, then
extracts metadata from the existing hand-written HUD surfaces:

- InputConfig/MelonPrimeInputConfig.cpp dialog tables
- MelonPrimeHudConfigOnScreenDefs.inc edit descriptors
- MelonPrimeHudConfigOnScreenEdit.cpp side panel builders
- MelonPrimeHudRenderConfig.inc runtime Load*Config reads

It does not normalize disagreements. Range/type drift is recorded so Phase 2
can preserve current behavior deliberately.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
import argparse
import json
import re
from typing import Iterable


ROOT = Path(__file__).resolve().parents[3]
QT_SDL = ROOT / "src/frontend/qt_sdl"
CONFIG_CPP = QT_SDL / "Config.cpp"
DIALOG_CPP = QT_SDL / "InputConfig/MelonPrimeInputConfig.cpp"
DIALOG_PROPS_INC = QT_SDL / "InputConfig/MelonPrimeInputConfigHudDialogProps.inc"
EDIT_DEFS = QT_SDL / "MelonPrimeHudConfigOnScreenDefs.inc"
EDIT_PROPS_INC = QT_SDL / "MelonPrimeHudConfigOnScreenEditProps.inc"
SIDE_PANEL_CPP = QT_SDL / "MelonPrimeHudConfigOnScreenEdit.cpp"
# V7 Phase 2: the side panel's per-element populate*() bodies became row
# tables in this file (RowBuiltins/RowOffset/... factory calls), so it must
# be scanned alongside SIDE_PANEL_CPP for side-surface coverage.
SIDE_PANEL_ROWS_INC = QT_SDL / "MelonPrimeHudEditorSidePanelRows.inc"
RUNTIME_INC = QT_SDL / "MelonPrimeHudRenderConfig.inc"
SCHEMA_OUT = QT_SDL / "MelonPrimeHudPropSchema.inc"
REPORT_OUT = ROOT / "docs/generated/hud/MelonPrimeHudPropSchemaPhase2a.md"

VISUAL_PREFIX = "Metroid.Visual."

SURFACES = {
    "default": "MP_HUD_SURFACE_DEFAULT",
    "dialog": "MP_HUD_SURFACE_DIALOG",
    "edit": "MP_HUD_SURFACE_EDIT",
    "side": "MP_HUD_SURFACE_SIDE_PANEL",
    "runtime": "MP_HUD_SURFACE_RUNTIME_LOAD",
}

SURFACE_BITS = {
    "default": 0x01,
    "dialog": 0x02,
    "edit": 0x04,
    "side": 0x08,
    "runtime": 0x10,
}

FLAG_RANGE_DRIFT = 0x01
FLAG_TYPE_DRIFT = 0x02

WEAPON_SUFFIXES = [
    "PowerBeam",
    "VoltDriver",
    "Missile",
    "BattleHammer",
    "Imperialist",
    "Judicator",
    "Magmaul",
    "ShockCoil",
    "OmegaCannon",
]


@dataclass
class Meta:
    surface: str
    key: str
    inferred_type: str | None = None
    label: str = ""
    ui_kind: str = ""
    min_value: str | None = None
    max_value: str | None = None
    step: str | None = None
    origin: str = ""

    def range_tuple(self) -> tuple[str, str, str] | None:
        if self.min_value is None or self.max_value is None:
            return None
        return (self.min_value, self.max_value, self.step or "0")


@dataclass
class Prop:
    key: str
    cfg_type: str
    default_expr: str
    metas: list[Meta] = field(default_factory=list)

    def surfaces(self) -> set[str]:
        return {"default"} | {m.surface for m in self.metas}

    def preferred_label(self) -> str:
        for surface in ("dialog", "side", "edit", "runtime"):
            for meta in self.metas:
                if meta.surface == surface and meta.label:
                    return meta.label
        return ""

    def preferred_kind(self) -> str:
        for surface in ("dialog", "side", "edit", "runtime"):
            for meta in self.metas:
                if meta.surface == surface and meta.ui_kind:
                    return meta.ui_kind
        return ""

    def preferred_range(self) -> tuple[str, str, str]:
        for surface in ("dialog", "side", "edit", "runtime"):
            for meta in self.metas:
                rng = meta.range_tuple()
                if meta.surface == surface and rng:
                    return rng
        return ("0", "0", "0")

    def type_drift(self) -> list[Meta]:
        return [
            meta for meta in self.metas
            if meta.inferred_type and meta.inferred_type != self.cfg_type
        ]

    def range_drift(self) -> dict[tuple[str, str, str], list[Meta]]:
        ranges: dict[tuple[str, str, str], list[Meta]] = {}
        for meta in self.metas:
            rng = meta.range_tuple()
            if not rng:
                continue
            ranges.setdefault(rng, []).append(meta)
        return ranges if len(ranges) > 1 else {}


@dataclass
class DialogRow:
    label: str
    widget_type: str
    cfg_key: str | None
    min_value: str
    max_value: str
    step: str
    cfg_key_g: str | None = None
    cfg_key_b: str | None = None


@dataclass
class DialogSection:
    name: str
    rows: list[DialogRow]


OSD_DIALOG_SECTION_NAMES = {
    "kSecOsdH211",
    "kSecOsdLostLives",
    "kSecOsdKillDeath",
    "kSecOsdReturnBase",
    "kSecOsdNoAmmo",
    "kSecOsdCowardDetect",
    "kSecOsdAcquiringNode",
    "kSecOsdTurret",
    "kSecOsdOctoReset",
    "kSecOsdOctoDrop",
    "kSecOsdOctoCond",
    "kSecOsdOctoMissing",
    "kSecOsdGlobal",
    "kSecOsdSlotKillDeath",
    "kSecOsdSlotNode",
    "kSecOsdSlotObjective",
    "kSecOsdSlotSystem",
}


@dataclass
class EditPropRow:
    label: str
    prop_type: str
    cfg_key: str | None
    min_value: str
    max_value: str
    step: str
    extra1: str | None = None
    extra2: str | None = None
    extra3: str | None = None


@dataclass
class EditPropArray:
    name: str
    rows: list[EditPropRow]


@dataclass
class EditElemRow:
    name: str
    anchor_key: str | None
    ofs_x_key: str | None
    ofs_y_key: str | None
    orient_key: str | None
    length_key: str | None
    width_key: str | None
    pos_mode_key: str | None
    show_key: str | None
    color_r_key: str | None
    color_g_key: str | None
    color_b_key: str | None
    props_expr: str
    prop_count_expr: str


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def strip_comments(text: str) -> str:
    out: list[str] = []
    i = 0
    n = len(text)
    in_str = False
    in_char = False
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue
        if c == "'":
            in_char = True
            out.append(c)
            i += 1
            continue
        if c == "/" and nxt == "/":
            while i < n and text[i] != "\n":
                i += 1
            out.append("\n")
            continue
        if c == "/" and nxt == "*":
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            i += 2
            continue
        out.append(c)
        i += 1
    return "".join(out)


def cpp_string_value(expr: str) -> str | None:
    pieces = re.findall(r'"(?:\\.|[^"\\])*"', expr)
    if not pieces:
        return None
    result = ""
    for piece in pieces:
        try:
            result += json.loads(piece)
        except json.JSONDecodeError:
            result += piece[1:-1]
    return result


def cpp_quote(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def split_top_level_args(arg_text: str) -> list[str]:
    args: list[str] = []
    start = 0
    paren = brace = bracket = 0
    in_str = False
    in_char = False
    i = 0
    while i < len(arg_text):
        c = arg_text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == '"':
            in_str = True
        elif c == "'":
            in_char = True
        elif c == "(":
            paren += 1
        elif c == ")":
            paren -= 1
        elif c == "{":
            brace += 1
        elif c == "}":
            brace -= 1
        elif c == "[":
            bracket += 1
        elif c == "]":
            bracket -= 1
        elif c == "," and paren == 0 and brace == 0 and bracket == 0:
            args.append(arg_text[start:i].strip())
            start = i + 1
        i += 1
    tail = arg_text[start:].strip()
    if tail:
        args.append(tail)
    return args


def matching_index(text: str, start: int, open_ch: str, close_ch: str) -> int:
    depth = 0
    in_str = False
    in_char = False
    i = start
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if in_char:
            if c == "\\":
                i += 2
                continue
            if c == "'":
                in_char = False
            i += 1
            continue
        if c == '"':
            in_str = True
        elif c == "'":
            in_char = True
        elif c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError(f"unmatched {open_ch} at {start}")


def array_blocks(text: str, typename: str, name_regex: str = r"\w+") -> Iterable[tuple[str, str]]:
    pattern = re.compile(
        rf"static\s+const\s+{re.escape(typename)}\s+({name_regex})(?:\[[^\]]*\])?\s*=\s*\{{",
        re.MULTILINE,
    )
    for match in pattern.finditer(text):
        open_idx = text.find("{", match.start())
        close_idx = matching_index(text, open_idx, "{", "}")
        yield match.group(1), text[open_idx + 1:close_idx]


def function_calls(text: str, names: Iterable[str]) -> Iterable[tuple[str, list[str]]]:
    names_re = "|".join(re.escape(name) for name in sorted(names, key=len, reverse=True))
    pattern = re.compile(rf"\b({names_re})\s*\(")
    for match in pattern.finditer(text):
        open_idx = text.find("(", match.start())
        close_idx = matching_index(text, open_idx, "(", ")")
        yield match.group(1), split_top_level_args(text[open_idx + 1:close_idx])


def brace_entries(block: str) -> Iterable[str]:
    i = 0
    while i < len(block):
        if block[i] == "{":
            close_idx = matching_index(block, i, "{", "}")
            yield block[i + 1:close_idx]
            i = close_idx + 1
        else:
            i += 1


def literal_key(arg: str) -> str | None:
    value = cpp_string_value(arg)
    if value and value.startswith(VISUAL_PREFIX) and "%" not in value:
        return value
    return None


def literal_label(arg: str) -> str:
    return cpp_string_value(arg) or ""


def add_meta(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    key: str | None,
    surface: str,
    inferred_type: str | None = None,
    label: str = "",
    ui_kind: str = "",
    min_value: object | None = None,
    max_value: object | None = None,
    step: object | None = None,
    origin: str = "",
) -> None:
    if not key or not key.startswith(VISUAL_PREFIX) or "%" in key:
        return
    meta = Meta(
        surface=surface,
        key=key,
        inferred_type=inferred_type,
        label=label,
        ui_kind=ui_kind,
        min_value=None if min_value is None else str(min_value),
        max_value=None if max_value is None else str(max_value),
        step=None if step is None else str(step),
        origin=origin,
    )
    if key in props:
        props[key].metas.append(meta)
    else:
        extra_refs.setdefault(key, []).append(meta)


def add_color3(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    keys: list[str | None],
    surface: str,
    label: str,
    ui_kind: str,
    origin: str,
) -> None:
    for suffix, key in zip(("R", "G", "B"), keys):
        add_meta(
            props,
            extra_refs,
            key,
            surface,
            "Int",
            f"{label} {suffix}".strip(),
            ui_kind,
            0,
            255,
            1,
            origin,
        )


def add_outline_group(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    prefix: str,
    surface: str,
    origin: str,
) -> None:
    add_meta(props, extra_refs, f"{VISUAL_PREFIX}{prefix}Outline", surface, "Bool", "Outline", "OutlineGroup", None, None, None, origin)
    add_color3(
        props,
        extra_refs,
        [
            f"{VISUAL_PREFIX}{prefix}OutlineColorR",
            f"{VISUAL_PREFIX}{prefix}OutlineColorG",
            f"{VISUAL_PREFIX}{prefix}OutlineColorB",
        ],
        surface,
        "Outline Color",
        "OutlineGroup",
        origin,
    )
    add_meta(props, extra_refs, f"{VISUAL_PREFIX}{prefix}OutlineOpacity", surface, "Double", "Outline Opacity", "OutlineGroup", 0, 100, 5, origin)
    add_meta(props, extra_refs, f"{VISUAL_PREFIX}{prefix}OutlineThickness", surface, "Int", "Outline Thickness", "OutlineGroup", 1, 10, 1, origin)


def add_ramp_stop(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    prefix: str,
    index: str,
    surface: str,
    origin: str,
) -> None:
    add_meta(props, extra_refs, f"{prefix}{index}Pct", surface, "Int", f"Threshold {index} (%)", "RampStop", 0, 100, 1, origin)
    add_color3(
        props,
        extra_refs,
        [f"{prefix}{index}R", f"{prefix}{index}G", f"{prefix}{index}B"],
        surface,
        f"Color {index}",
        "RampStop",
        origin,
    )


def add_ramp_family(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    prefix: str,
    surface: str,
    origin: str,
) -> None:
    add_meta(props, extra_refs, f"{prefix}Enable", surface, "Bool", "Enable", "ColorRamp", None, None, None, origin)
    add_meta(props, extra_refs, f"{prefix}Count", surface, "Int", "Number of Colors", "ColorRamp", 1, 6, 1, origin)
    for i in range(1, 7):
        add_ramp_stop(props, extra_refs, prefix, str(i), surface, origin)


def add_weapon_tints(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    surface: str,
    origin: str,
) -> None:
    for weapon in WEAPON_SUFFIXES:
        add_meta(
            props,
            extra_refs,
            f"{VISUAL_PREFIX}HudWeaponIconColorOverlay{weapon}",
            surface,
            "Bool",
            weapon,
            "WeaponTint",
            None,
            None,
            None,
            origin,
        )
        add_color3(
            props,
            extra_refs,
            [
                f"{VISUAL_PREFIX}HudWeaponIconOverlayColorR{weapon}",
                f"{VISUAL_PREFIX}HudWeaponIconOverlayColorG{weapon}",
                f"{VISUAL_PREFIX}HudWeaponIconOverlayColorB{weapon}",
            ],
            surface,
            f"{weapon} Color",
            "WeaponTint",
            origin,
        )


def parse_defaults(defaults_source: Path) -> tuple[list[str], dict[str, Prop]]:
    text = strip_comments(read(defaults_source))
    props: dict[str, Prop] = {}
    order: list[str] = []
    state = ""
    type_re = {
        "DefaultList<int>": "Int",
        "DefaultList<bool>": "Bool",
        "DefaultList<std::string>": "String",
        "DefaultList<double>": "Double",
    }
    for raw_line in text.splitlines():
        line = raw_line.strip()
        for marker, cfg_type in type_re.items():
            if line.startswith(marker):
                state = cfg_type
                break
        else:
            if state and line.startswith("};"):
                state = ""
            if not state:
                continue
            for match in re.finditer(r'\{\s*"Instance\*\.(Metroid\.Visual\.[^"]+)"\s*,\s*([^}]+?)\s*\}', line):
                key = match.group(1)
                default_expr = match.group(2).strip()
                if key not in props:
                    order.append(key)
                props[key] = Prop(key=key, cfg_type=state, default_expr=default_expr)
    return order, props


def parse_existing_schema(path: Path) -> tuple[list[str], dict[str, Prop]]:
    if not path.exists():
        return [], {}
    text = read(path)
    props: dict[str, Prop] = {}
    order: list[str] = []
    key_macros = {
        match.group(1): match.group(2)
        for match in re.finditer(r'#define\s+MP_HUD_PROP_KEY_(\w+)\s+"(Metroid\.Visual\.[^"]+)"', text)
    }
    pattern = re.compile(r'#define\s+MP_HUD_PROP_ROW_(\w+)\(X\)\s+X\((.*)\)')
    for match in pattern.finditer(text):
        args = split_top_level_args(match.group(2))
        if len(args) < 4:
            continue
        key_token = args[1].strip()
        key = cpp_string_value(key_token)
        if key is None and key_token.startswith("MP_HUD_PROP_KEY_"):
            key = key_macros.get(key_token.removeprefix("MP_HUD_PROP_KEY_"))
        if not key:
            continue
        if key in props:
            continue
        cfg_type = args[2].strip()
        if cfg_type not in {"Int", "Bool", "String", "Double"}:
            continue
        order.append(key)
        props[key] = Prop(
            key=key,
            cfg_type=cfg_type,
            default_expr=args[3].strip(),
        )
    return order, props


def dialog_key_from_expr(expr: str, ident_to_key: dict[str, str] | None = None) -> str | None:
    key = literal_key(expr)
    if key:
        return key
    token = expr.strip()
    if token.startswith("MP_HUD_PROP_KEY_") and ident_to_key is not None:
        return ident_to_key.get(token.removeprefix("MP_HUD_PROP_KEY_"))
    return None


def parse_dialog_sections_from_legacy(text: str) -> list[DialogSection]:
    macro_names = {
        "P_LABEL", "P_BOOL", "P_INT", "P_FLOAT", "P_DOUBLE", "P_STR", "P_ANC", "P_ALN",
        "P_CLR", "P_ORIENT", "P_RELINDEP", "P_POSMODE3", "P_GANCHOR",
        "P_ANCHY", "P_LPOS", "P_FONTMODE", "P_FONTFAMILY", "P_FONTFILE",
        "P_FONTWEIGHT", "P_RAMP_STOP",
    }
    sections: list[DialogSection] = []
    for section_name, block in array_blocks(text, "HudWidgetProp"):
        rows: list[DialogRow] = []
        for name, args in function_calls(block, macro_names):
            if name == "P_LABEL" and len(args) >= 1:
                rows.append(DialogRow(literal_label(args[0]), "Label", None, "0", "0", "0"))
            elif name == "P_BOOL" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "Bool", literal_key(args[1]), "0", "0", "0"))
            elif name == "P_INT" and len(args) >= 5:
                rows.append(DialogRow(literal_label(args[0]), "Int", literal_key(args[1]), args[2].strip(), args[3].strip(), args[4].strip()))
            elif name == "P_FLOAT" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "Float", literal_key(args[1]), "0", "100", "5"))
            elif name == "P_DOUBLE" and len(args) >= 5:
                rows.append(DialogRow(literal_label(args[0]), "Double", literal_key(args[1]), args[2].strip(), args[3].strip(), args[4].strip()))
            elif name == "P_STR" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "String", literal_key(args[1]), "0", "0", "0"))
            elif name == "P_ANC" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "Anchor9", literal_key(args[1]), "0", "8", "1"))
            elif name == "P_ALN" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "Align3", literal_key(args[1]), "0", "2", "1"))
            elif name == "P_CLR" and len(args) >= 4:
                rows.append(DialogRow(literal_label(args[0]), "Color3", literal_key(args[1]), "0", "255", "1", literal_key(args[2]), literal_key(args[3])))
            elif name == "P_ORIENT" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "HorizVert", literal_key(args[1]), "0", "1", "1"))
            elif name == "P_RELINDEP" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "RelIndep", literal_key(args[1]), "0", "1", "1"))
            elif name == "P_POSMODE3" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "PosMode3", literal_key(args[1]), "0", "2", "1"))
            elif name == "P_GANCHOR" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "GaugeAnchor5", literal_key(args[1]), "0", "4", "1"))
            elif name == "P_ANCHY" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "AnchorY3", literal_key(args[1]), "0", "2", "1"))
            elif name == "P_LPOS" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "LabelPos5", literal_key(args[1]), "0", "4", "1"))
            elif name == "P_FONTMODE" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "FontMode", literal_key(args[1]), "0", "2", "1"))
            elif name == "P_FONTFAMILY" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "FontFamily", literal_key(args[1]), "0", "0", "0"))
            elif name == "P_FONTFILE" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "FontFile", literal_key(args[1]), "0", "0", "0"))
            elif name == "P_FONTWEIGHT" and len(args) >= 2:
                rows.append(DialogRow(literal_label(args[0]), "FontWeight", literal_key(args[1]), "0", "8", "1"))
            elif name == "P_RAMP_STOP" and len(args) >= 2:
                index = cpp_string_value(args[0])
                prefix = cpp_string_value(args[1])
                if index and prefix:
                    rows.append(DialogRow(f"Threshold {index} (%)", "Int", f"{prefix}{index}Pct", "0", "100", "1"))
                    rows.append(DialogRow(f"Color {index}", "Color3", f"{prefix}{index}R", "0", "255", "1", f"{prefix}{index}G", f"{prefix}{index}B"))
        if rows:
            sections.append(DialogSection(section_name, rows))
    return sections


def parse_dialog_sections_from_generated(text: str, ident_to_key: dict[str, str]) -> list[DialogSection]:
    sections: list[DialogSection] = []
    for section_name, block in array_blocks(text, "HudWidgetProp"):
        rows: list[DialogRow] = []
        for entry in brace_entries(block):
            args = split_top_level_args(entry)
            if len(args) < 8:
                continue
            widget_type = args[1].strip().removeprefix("HWType::")
            rows.append(DialogRow(
                literal_label(args[0]),
                widget_type,
                dialog_key_from_expr(args[2], ident_to_key),
                args[3].strip(),
                args[4].strip(),
                args[5].strip(),
                dialog_key_from_expr(args[6], ident_to_key),
                dialog_key_from_expr(args[7], ident_to_key),
            ))
        if rows:
            sections.append(DialogSection(section_name, rows))
    return sections


def osd_dialog_sections() -> list[DialogSection]:
    def color_row(label: str, prefix: str) -> DialogRow:
        return DialogRow(label, "Color3", f"Metroid.Visual.{prefix}R", "0", "255", "1",
                         f"Metroid.Visual.{prefix}G", f"Metroid.Visual.{prefix}B")

    sections: list[DialogSection] = [
        DialogSection("kSecOsdH211", [
            DialogRow("Enable Separate Color", "Bool", "Metroid.Visual.OsdColorH211", "0", "0", "0"),
            color_row("Color (Default: Red)", "OsdColorH211"),
        ]),
        DialogSection("kSecOsdGlobal", [
            DialogRow("Enable OSD Color Patch", "Bool", "Metroid.Visual.OsdColor", "0", "0", "0"),
            color_row("Global Color", "OsdColor"),
            DialogRow("Use Global Color for All", "Bool", "Metroid.Visual.OsdColorApplyGlobal", "0", "0", "0"),
        ]),
    ]

    literal_sections = [
        ("kSecOsdLostLives", "OsdColorLostLives"),
        ("kSecOsdKillDeath", "OsdColorKillDeath"),
        ("kSecOsdReturnBase", "OsdColorReturnBase"),
        ("kSecOsdNoAmmo", "OsdColorNoAmmo"),
        ("kSecOsdCowardDetect", "OsdColorCowardDetect"),
        ("kSecOsdAcquiringNode", "OsdColorAcquiringNode"),
        ("kSecOsdTurret", "OsdColorTurret"),
        ("kSecOsdOctoReset", "OsdColorOctoReset"),
        ("kSecOsdOctoDrop", "OsdColorOctoDrop"),
        ("kSecOsdOctoCond", "OsdColorOctoCond"),
        ("kSecOsdOctoMissing", "OsdColorOctoMissing"),
    ]
    sections[1:1] = [
        DialogSection(section_name, [color_row("Color", prefix)])
        for section_name, prefix in literal_sections
    ]

    slot_sections = [
        ("kSecOsdSlotKillDeath", "OsdColorSlotKillDeath",
         "Applied once on settings close to currently displayed messages (flags=0x02).\nNew messages use the 'Kill / Death' literal color above.",
         "Color  (YOU KILLED / KILLED YOU / 5-kill / prime hunter / teammate)"),
        ("kSecOsdSlotNode", "OsdColorSlotNode",
         "Applied once on settings close to currently displayed messages (flags=0x11).\nNew messages use 'Acquiring Node' or 'Node Stolen' literal colors above.",
         "Color  (acquiring node / node stolen H211)"),
        ("kSecOsdSlotObjective", "OsdColorSlotObjective",
         "Applied once on settings close to currently displayed messages (flags=0x01).\nNew messages use their individual literal colors above (No Ammo / Return to Base / Octo ...).",
         "Color  (AMMO DEPLETED / return to base / bounty / octolith events)"),
        ("kSecOsdSlotSystem", "OsdColorSlotSystem",
         "Applied once on settings close to currently displayed messages (flags=0x00).\nNew messages use their individual literal colors above (Lost Lives / Coward Detect / Turret ...).\nNote: HEADSHOT! (H228) is flags=0x00, not 0x02.",
         "Color  (HEADSHOT! / FACE OFF! / RETURN TO BATTLE! / COWARD DETECTED / turret)"),
    ]
    sections.extend(
        DialogSection(section_name, [
            DialogRow(description, "Label", None, "0", "0", "0"),
            color_row(color_label, prefix),
        ])
        for section_name, prefix, description, color_label in slot_sections
    )
    return sections


def parse_dialog_sections(ident_to_key: dict[str, str]) -> list[DialogSection]:
    source_text = strip_comments(read(DIALOG_CPP))
    legacy_sections = parse_dialog_sections_from_legacy(source_text)
    if legacy_sections:
        return legacy_sections
    if DIALOG_PROPS_INC.exists():
        sections = [
            section for section in parse_dialog_sections_from_generated(strip_comments(read(DIALOG_PROPS_INC)), ident_to_key)
            if section.name not in OSD_DIALOG_SECTION_NAMES
        ]
        sections.extend(osd_dialog_sections())
        return sections
    return []


def add_dialog_row_meta(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    section_name: str,
    row: DialogRow,
) -> None:
    origin = f"dialog:{section_name}"
    if row.widget_type == "Label":
        return
    if row.widget_type == "Color3":
        add_color3(props, extra_refs, [row.cfg_key, row.cfg_key_g, row.cfg_key_b], "dialog", row.label, "Color3", origin)
        return

    type_kind = {
        "Bool": ("Bool", "Bool", False),
        "Int": ("Int", "Int", True),
        "Float": ("Double", "Float", True),
        "Double": ("Double", "Double", True),
        "String": ("String", "String", False),
        "Anchor9": ("Int", "Anchor9", True),
        "Align3": ("Int", "Align3", True),
        "HorizVert": ("Int", "HorizVert", True),
        "RelIndep": ("Int", "RelIndep", True),
        "PosMode3": ("Int", "PosMode3", True),
        "GaugeAnchor5": ("Int", "GaugeAnchor5", True),
        "AnchorY3": ("Int", "AnchorY3", True),
        "LabelPos5": ("Int", "LabelPos5", True),
        "FontMode": ("Int", "FontMode", True),
        "FontFamily": ("String", "FontFamily", False),
        "FontFile": ("String", "FontFile", False),
        "FontWeight": ("Int", "FontWeight", True),
    }
    if row.widget_type not in type_kind:
        return
    inferred_type, ui_kind, has_range = type_kind[row.widget_type]
    if has_range:
        add_meta(props, extra_refs, row.cfg_key, "dialog", inferred_type, row.label, ui_kind, row.min_value, row.max_value, row.step, origin)
    else:
        add_meta(props, extra_refs, row.cfg_key, "dialog", inferred_type, row.label, ui_kind, origin=origin)


def parse_dialog(props: dict[str, Prop], extra_refs: dict[str, list[Meta]], ident_to_key: dict[str, str]) -> list[DialogSection]:
    sections = parse_dialog_sections(ident_to_key)
    for section in sections:
        for row in section.rows:
            add_dialog_row_meta(props, extra_refs, section.name, row)

    text = strip_comments(read(DIALOG_CPP))

    for match in re.finditer(r'"(Metroid\.Visual\.[^"]+)"', text):
        key = match.group(1)
        if key in props:
            add_meta(props, extra_refs, key, "dialog", origin="dialog:literal")
    for match in re.finditer(r'\bMP_HUD_PROP_KEY_(\w+)\b', text):
        key = ident_to_key.get(match.group(1))
        if key in props:
            add_meta(props, extra_refs, key, "dialog", origin="dialog:macro-ref")
    return sections


def edit_value_from_expr(expr: str, ident_to_key: dict[str, str]) -> str | None:
    key = dialog_key_from_expr(expr, ident_to_key)
    if key:
        return key
    token = expr.strip()
    if token == "nullptr":
        return None
    return token


def parse_edit_descriptor_tables_from_text(
    text: str,
    ident_to_key: dict[str, str],
) -> tuple[list[EditPropArray], list[EditElemRow]]:
    prop_arrays: list[EditPropArray] = []
    for array_name, block in array_blocks(text, "HudEditPropDesc"):
        rows: list[EditPropRow] = []
        for entry in brace_entries(block):
            args = split_top_level_args(entry)
            if len(args) < 9:
                continue
            rows.append(EditPropRow(
                literal_label(args[0]),
                args[1].replace("EditPropType::", "").strip(),
                edit_value_from_expr(args[2], ident_to_key),
                args[3].strip(),
                args[4].strip(),
                args[5].strip(),
                edit_value_from_expr(args[6], ident_to_key),
                edit_value_from_expr(args[7], ident_to_key),
                edit_value_from_expr(args[8], ident_to_key),
            ))
        if rows:
            prop_arrays.append(EditPropArray(array_name, rows))

    edit_elems: list[EditElemRow] = []
    for _, block in array_blocks(text, "HudEditElemDesc", r"kEditElems"):
        for entry in brace_entries(block):
            args = split_top_level_args(entry)
            if len(args) < 14:
                continue
            edit_elems.append(EditElemRow(
                literal_label(args[0]),
                edit_value_from_expr(args[1], ident_to_key),
                edit_value_from_expr(args[2], ident_to_key),
                edit_value_from_expr(args[3], ident_to_key),
                edit_value_from_expr(args[4], ident_to_key),
                edit_value_from_expr(args[5], ident_to_key),
                edit_value_from_expr(args[6], ident_to_key),
                edit_value_from_expr(args[7], ident_to_key),
                edit_value_from_expr(args[8], ident_to_key),
                edit_value_from_expr(args[9], ident_to_key),
                edit_value_from_expr(args[10], ident_to_key),
                edit_value_from_expr(args[11], ident_to_key),
                args[12].strip(),
                args[13].strip(),
            ))
    return prop_arrays, edit_elems


def parse_edit_descriptor_tables(ident_to_key: dict[str, str]) -> tuple[list[EditPropArray], list[EditElemRow]]:
    source_tables = parse_edit_descriptor_tables_from_text(strip_comments(read(EDIT_DEFS)), ident_to_key)
    if source_tables[0] and source_tables[1]:
        return source_tables
    if EDIT_PROPS_INC.exists():
        return parse_edit_descriptor_tables_from_text(strip_comments(read(EDIT_PROPS_INC)), ident_to_key)
    return [], []


def add_edit_prop_meta(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    array_name: str,
    row: EditPropRow,
) -> None:
    origin = f"edit:{array_name}"
    if row.prop_type == "Bool":
        add_meta(props, extra_refs, row.cfg_key, "edit", "Bool", row.label, row.prop_type, origin=origin)
    elif row.prop_type in {"Int", "Enum"}:
        add_meta(props, extra_refs, row.cfg_key, "edit", "Int", row.label, row.prop_type, row.min_value, row.max_value, row.step, origin)
    elif row.prop_type == "Float":
        add_meta(props, extra_refs, row.cfg_key, "edit", "Double", row.label, row.prop_type, row.min_value, row.max_value, row.step, origin)
    elif row.prop_type == "String":
        add_meta(props, extra_refs, row.cfg_key, "edit", "String", row.label, row.prop_type, origin=origin)
    elif row.prop_type == "SubColor":
        add_meta(props, extra_refs, row.cfg_key, "edit", "Bool", row.label, row.prop_type, origin=origin)
        add_color3(props, extra_refs, [row.extra1, row.extra2, row.extra3], "edit", row.label, row.prop_type, origin)
    elif row.prop_type == "Color":
        add_meta(props, extra_refs, row.cfg_key, "edit", "Int", row.label, row.prop_type, row.min_value, row.max_value, row.step, origin)


def parse_edit_descriptors(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    ident_to_key: dict[str, str],
) -> tuple[list[EditPropArray], list[EditElemRow]]:
    prop_arrays, edit_elems = parse_edit_descriptor_tables(ident_to_key)
    for prop_array in prop_arrays:
        for row in prop_array.rows:
            add_edit_prop_meta(props, extra_refs, prop_array.name, row)

    for row in edit_elems:
        origin = f"edit:kEditElems:{row.name}"
        add_meta(props, extra_refs, row.anchor_key, "edit", "Int", "Anchor", "EditElemAnchor", 0, 8, 1, origin)
        add_meta(props, extra_refs, row.ofs_x_key, "edit", "Int", "Offset X", "EditElemOffset", origin=origin)
        add_meta(props, extra_refs, row.ofs_y_key, "edit", "Int", "Offset Y", "EditElemOffset", origin=origin)
        add_meta(props, extra_refs, row.orient_key, "edit", "Int", "Orientation", "EditElemOrient", 0, 1, 1, origin)
        add_meta(props, extra_refs, row.length_key, "edit", "Int", "Length", "EditElemSize", origin=origin)
        add_meta(props, extra_refs, row.width_key, "edit", "Int", "Width", "EditElemSize", origin=origin)
        add_meta(props, extra_refs, row.pos_mode_key, "edit", "Int", "Position Mode", "EditElemPosMode", 0, 2, 1, origin)
        add_meta(props, extra_refs, row.show_key, "edit", "Bool", "Show", "EditElemShow", origin=origin)
        add_color3(
            props,
            extra_refs,
            [row.color_r_key, row.color_g_key, row.color_b_key],
            "edit",
            "Color",
            "EditElemColor",
            origin,
        )
    return prop_arrays, edit_elems


def parse_side_panel(props: dict[str, Prop], extra_refs: dict[str, list[Meta]], ident_to_key: dict[str, str]) -> None:
    # V7 Phase 2 moved the per-element populate*() bodies from hand-written
    # addXxx(...) call sequences in SIDE_PANEL_CPP into row tables (RowXxx(...)
    # factory calls) in SIDE_PANEL_ROWS_INC. Both files are scanned together;
    # the Row* names below are argument-for-argument equivalent to their old
    # addXxx counterparts (verified 1:1 by a call-sequence diff at the time of
    # that refactor), so they share the same parsing branches.
    text = strip_comments(read(SIDE_PANEL_CPP)) + "\n" + strip_comments(read(SIDE_PANEL_ROWS_INC))
    call_names = {
        "addOutlineGroup", "addOutlineGroupSection", "addBuiltins",
        "addOffsetRows", "addLineEdit", "addAlign3Combo", "addOpacitySlider",
        "addSpinBox", "addDoubleSpinBox", "addCheckBox", "addColorPicker",
        "addComboBox", "addGaugePositionRows", "addSubColor", "addColorOverlayRow",
        "RowOutline", "RowBuiltins", "RowOffset", "RowLineEdit", "RowAlign3",
        "RowOpacity", "RowSpin", "RowDoubleSpin", "RowBool", "RowColor",
        "RowCombo", "RowGaugePosition", "RowSubColor", "RowColorOverlay",
    }
    for name, args in function_calls(text, call_names):
        origin = f"side:{name}"
        if name == "addOutlineGroup" and len(args) >= 1:
            # Call sites are addOutlineGroup(MP_OUTLINE_KEYS(Prefix)); pull the prefix token.
            m = re.search(r"MP_OUTLINE_KEYS\(\s*(\w+)\s*\)", args[0])
            if m:
                add_outline_group(props, extra_refs, m.group(1), "side", origin)
        elif name in ("addOutlineGroupSection", "RowOutline") and len(args) >= 2:
            # Call sites are addOutlineGroupSection/RowOutline(Label, MP_OUTLINE_KEYS(Prefix)).
            m = re.search(r"MP_OUTLINE_KEYS\(\s*(\w+)\s*\)", args[1])
            if m:
                add_outline_group(props, extra_refs, m.group(1), "side", origin)
        elif name in ("addBuiltins", "RowBuiltins") and len(args) >= 5:
            add_meta(props, extra_refs, dialog_key_from_expr(args[0], ident_to_key), "side", "Bool", "Show", "Builtins", origin=origin)
            add_color3(props, extra_refs, [dialog_key_from_expr(args[1], ident_to_key), dialog_key_from_expr(args[2], ident_to_key), dialog_key_from_expr(args[3], ident_to_key)], "side", "Color", "Builtins", origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[4], ident_to_key), "side", "Int", "Anchor", "Builtins", 0, 8, 1, origin)
        elif name in ("addOffsetRows", "RowOffset") and len(args) >= 4:
            add_meta(props, extra_refs, dialog_key_from_expr(args[0], ident_to_key), "side", "Int", "Offset X", "OffsetRows", args[2], args[3], 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Int", "Offset Y", "OffsetRows", args[2], args[3], 1, origin)
        elif name in ("addLineEdit", "RowLineEdit") and len(args) >= 2:
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "String", literal_label(args[0]), "LineEdit", origin=origin)
        elif name in ("addAlign3Combo", "RowAlign3") and len(args) >= 2:
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Int", literal_label(args[0]), "Align3", 0, 2, 1, origin)
        elif name in ("addOpacitySlider", "RowOpacity") and len(args) >= 2:
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Double", literal_label(args[0]), "OpacitySlider", 0, 100, 5, origin)
        elif name in ("addSpinBox", "RowSpin") and len(args) >= 4:
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Int", literal_label(args[0]), "SpinBox", args[2], args[3], 1, origin)
        elif name in ("addDoubleSpinBox", "RowDoubleSpin") and len(args) >= 5:
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Double", literal_label(args[0]), "DoubleSpinBox", args[2], args[3], args[4], origin)
        elif name in ("addCheckBox", "RowBool") and len(args) >= 2:
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Bool", literal_label(args[0]), "CheckBox", origin=origin)
        elif name in ("addColorPicker", "RowColor") and len(args) >= 4:
            add_color3(props, extra_refs, [dialog_key_from_expr(args[1], ident_to_key), dialog_key_from_expr(args[2], ident_to_key), dialog_key_from_expr(args[3], ident_to_key)], "side", literal_label(args[0]), "ColorPicker", origin)
        elif name in ("addSubColor", "RowSubColor") and len(args) >= 5:
            # addSubColor/RowSubColor(label, overallKey, keyR, keyG, keyB): an
            # Overall-toggle bool plus an RGB triple, mirroring the runtime
            # parser's ReadOptionalSubColor handling below.
            label = literal_label(args[0])
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Bool", f"{label} Overall".strip(), "SubColor", origin=origin)
            add_color3(props, extra_refs, [dialog_key_from_expr(args[2], ident_to_key), dialog_key_from_expr(args[3], ident_to_key), dialog_key_from_expr(args[4], ident_to_key)], "side", label, "SubColor", origin)
        elif name in ("addColorOverlayRow", "RowColorOverlay") and len(args) >= 5:
            # addColorOverlayRow/RowColorOverlay(label, enableKey, keyR, keyG, keyB):
            # an enable bool plus an RGB triple (per-weapon icon tint rows).
            label = literal_label(args[0])
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Bool", f"{label} Enable".strip(), "ColorOverlay", origin=origin)
            add_color3(props, extra_refs, [dialog_key_from_expr(args[2], ident_to_key), dialog_key_from_expr(args[3], ident_to_key), dialog_key_from_expr(args[4], ident_to_key)], "side", label, "ColorOverlay", origin)
        elif name in ("addComboBox", "RowCombo") and len(args) >= 2:
            # Item lists appear either as {QStringLiteral("X"), ...} (addComboBox
            # call sites) or {"X", ...} (RowCombo row-table entries); both are
            # counted by matching quoted string literals regardless of wrapper.
            items = re.findall(r'"[^"]*"', args[2]) if len(args) >= 3 else []
            max_value = len(items) - 1 if items else None
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Int", literal_label(args[0]), "ComboBox", 0 if items else None, max_value, 1 if items else None, origin)
        elif name in ("addGaugePositionRows", "RowGaugePosition") and len(args) >= 9:
            add_meta(props, extra_refs, dialog_key_from_expr(args[0], ident_to_key), "side", "Int", "Position Mode", "GaugePositionRows", 0, 2, 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[1], ident_to_key), "side", "Int", "Gauge Side", "GaugePositionRows", 0, 4, 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[2], ident_to_key), "side", "Int", "Offset X", "GaugePositionRows", -128, 128, 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[3], ident_to_key), "side", "Int", "Offset Y", "GaugePositionRows", -128, 128, 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[4], ident_to_key), "side", "Int", "Gauge X", "GaugePositionRows", -256, 256, 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[5], ident_to_key), "side", "Int", "Gauge Y", "GaugePositionRows", -256, 256, 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[6], ident_to_key), "side", "Int", "Text Side", "GaugePositionRows", 0, 4, 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[7], ident_to_key), "side", "Int", "Text Offset X", "GaugePositionRows", -128, 128, 1, origin)
            add_meta(props, extra_refs, dialog_key_from_expr(args[8], ident_to_key), "side", "Int", "Text Offset Y", "GaugePositionRows", -128, 128, 1, origin)

    # The per-weapon tint keys are no longer built via a "...%s" snprintf loop;
    # they are now MP_HUD_PROP_KEY_* macro references, picked up by the generic
    # macro pass below.
    for match in re.finditer(r'"(Metroid\.Visual\.[^"]+)"', text):
        key = match.group(1)
        if key in props:
            add_meta(props, extra_refs, key, "side", origin="side:literal")
    for match in re.finditer(r'\bMP_HUD_PROP_KEY_(\w+)\b', text):
        key = ident_to_key.get(match.group(1))
        if key in props:
            add_meta(props, extra_refs, key, "side", origin="side:keyMacro")


def parse_runtime(
    props: dict[str, Prop],
    extra_refs: dict[str, list[Meta]],
    ident_to_key: dict[str, str],
) -> None:
    text = strip_comments(read(RUNTIME_INC))

    for accessor, cfg_type in (("GetInt", "Int"), ("GetBool", "Bool"), ("GetDouble", "Double"), ("GetString", "String")):
        for match in re.finditer(rf"\bcfg\.{accessor}\s*\(\s*\"(Metroid\.Visual\.[^\"]+)\"", text):
            add_meta(props, extra_refs, match.group(1), "runtime", cfg_type, ui_kind=accessor, origin=f"runtime:{accessor}")
        for _, args in function_calls(text, [accessor]):
            if not args:
                continue
            key = dialog_key_from_expr(args[0], ident_to_key)
            if key:
                add_meta(props, extra_refs, key, "runtime", cfg_type, ui_kind=accessor, origin=f"runtime:{accessor}")

    for match in re.finditer(r"ReadRgbColor\s*\(\s*cfg\s*,\s*([^)]*)\)", text, re.DOTALL):
        args = split_top_level_args(match.group(1))
        if len(args) >= 3:
            add_color3(
                props,
                extra_refs,
                [
                    dialog_key_from_expr(args[0], ident_to_key),
                    dialog_key_from_expr(args[1], ident_to_key),
                    dialog_key_from_expr(args[2], ident_to_key),
                ],
                "runtime",
                "RGB",
                "ReadRgbColor",
                "runtime:ReadRgbColor",
            )

    for match in re.finditer(r"ReadOptionalSubColor\s*\(\s*cfg\s*,\s*([^)]*)\)", text, re.DOTALL):
        args = split_top_level_args(match.group(1))
        if len(args) >= 4:
            add_meta(
                props,
                extra_refs,
                dialog_key_from_expr(args[0], ident_to_key),
                "runtime",
                "Bool",
                "Overall",
                "ReadOptionalSubColor",
                origin="runtime:ReadOptionalSubColor",
            )
            add_color3(
                props,
                extra_refs,
                [
                    dialog_key_from_expr(args[1], ident_to_key),
                    dialog_key_from_expr(args[2], ident_to_key),
                    dialog_key_from_expr(args[3], ident_to_key),
                ],
                "runtime",
                "SubColor",
                "ReadOptionalSubColor",
                "runtime:ReadOptionalSubColor",
            )

    for match in re.finditer(r'LoadColorRamp\([^;]+,\s*"((?:Metroid\.Visual\.)[^"]+)"\)', text):
        add_ramp_family(props, extra_refs, match.group(1), "runtime", "runtime:LoadColorRamp")
    for match in re.finditer(r"MP_HUD_RAMP_KEYS\(\s*(\w+)\s*\)", text):
        if match.group(1) != "prefix":
            add_ramp_family(props, extra_refs, f"{VISUAL_PREFIX}{match.group(1)}", "runtime", "runtime:MP_HUD_RAMP_KEYS")

    for match in re.finditer(r'loadOL\([^,]+,\s*"([^"]+)"\)', text):
        add_outline_group(props, extra_refs, match.group(1), "runtime", "runtime:loadOL")
    for match in re.finditer(r"MP_HUD_OUTLINE_KEYS\(\s*(\w+)\s*\)", text):
        if match.group(1) != "prefix":
            add_outline_group(props, extra_refs, match.group(1), "runtime", "runtime:MP_HUD_OUTLINE_KEYS")

    if "HudWeaponIconColorOverlay%s" in text:
        add_weapon_tints(props, extra_refs, "runtime", "runtime:weaponTintLoop")

    # String literals passed through helper lambdas such as capScale() still
    # count as runtime references even if the accessor type is indirect. Keep
    # this pass limited to default-backed keys so helper prefixes such as
    # LoadColorRamp("...Ramp") do not become false missing-default entries.
    for match in re.finditer(r'"(Metroid\.Visual\.[^"]+)"', text):
        key = match.group(1)
        if key in props:
            add_meta(props, extra_refs, key, "runtime", origin="runtime:literal")
    for match in re.finditer(r'\bMP_HUD_PROP_KEY_(\w+)\b', text):
        key = ident_to_key.get(match.group(1))
        if key in props:
            add_meta(props, extra_refs, key, "runtime", origin="runtime:keyMacro")


def make_identifier(key: str, used: set[str]) -> str:
    stem = key.removeprefix(VISUAL_PREFIX)
    ident = re.sub(r"[^0-9A-Za-z_]", "_", stem)
    if not ident or ident[0].isdigit():
        ident = f"K_{ident}"
    base = ident
    i = 2
    while ident in used:
        ident = f"{base}_{i}"
        i += 1
    used.add(ident)
    return ident


def build_identifier_maps(order: list[str]) -> tuple[dict[str, str], dict[str, str]]:
    used: set[str] = set()
    key_to_ident: dict[str, str] = {}
    ident_to_key: dict[str, str] = {}
    for key in order:
        ident = make_identifier(key, used)
        key_to_ident[key] = ident
        ident_to_key[ident] = key
    return key_to_ident, ident_to_key


def surface_expr(surfaces: set[str]) -> str:
    parts = [SURFACES[name] for name in ("default", "dialog", "edit", "side", "runtime") if name in surfaces]
    return "(" + " | ".join(parts) + ")"


def schema_row(prop: Prop, ident: str) -> str:
    ui_min, ui_max, ui_step = prop.preferred_range()
    flags = 0
    if prop.range_drift():
        flags |= FLAG_RANGE_DRIFT
    if prop.type_drift():
        flags |= FLAG_TYPE_DRIFT
    return (
        "    X("
        f"{ident}, MP_HUD_PROP_KEY_{ident}, {prop.cfg_type}, {prop.default_expr}, "
        f"{ui_min}, {ui_max}, {ui_step}, "
        f"{cpp_quote(prop.preferred_label())}, {cpp_quote(prop.preferred_kind())}, "
        f"{surface_expr(prop.surfaces())}, 0x{flags:02X})"
    )


def append_schema_macro(lines: list[str], name: str, row_refs: list[str]) -> None:
    lines.append(f"#define {name}(X) \\")
    for index, row_ref in enumerate(row_refs):
        suffix = " \\" if index + 1 < len(row_refs) else ""
        lines.append(row_ref + suffix)


def write_schema(order: list[str], props: dict[str, Prop], path: Path) -> None:
    key_to_ident, _ = build_identifier_maps(order)
    row_records: list[tuple[Prop, str, str]] = []
    for key in order:
        prop = props[key]
        ident = key_to_ident[key]
        row_records.append((prop, ident, schema_row(prop, ident)))

    lines: list[str] = [
        "// Generated by tools/codegen/hud/generate-hud-prop-schema.py.",
        "// Phase 2 schema seed: one row per Config.cpp Metroid.Visual.* default.",
        "// Do not edit rows by hand; regenerate from the existing source surfaces.",
        "",
        "#ifndef MELONPRIME_HUD_PROP_SCHEMA_INC",
        "#define MELONPRIME_HUD_PROP_SCHEMA_INC",
        "",
        "#define MP_HUD_SURFACE_DEFAULT      0x01",
        "#define MP_HUD_SURFACE_DIALOG       0x02",
        "#define MP_HUD_SURFACE_EDIT         0x04",
        "#define MP_HUD_SURFACE_SIDE_PANEL   0x08",
        "#define MP_HUD_SURFACE_RUNTIME_LOAD 0x10",
        "",
        "#define MP_HUD_PROP_FLAG_RANGE_DRIFT 0x01",
        "#define MP_HUD_PROP_FLAG_TYPE_DRIFT  0x02",
        "",
        "// Key literals live here. Generated consumers should reference MP_HUD_PROP_KEY_*.",
    ]
    for prop, ident, _ in row_records:
        lines.append(f"#define MP_HUD_PROP_KEY_{ident} {cpp_quote(prop.key)}")
    lines.extend([
        "",
        "// X(identifier, key, cfgType, defaultExpr, uiMin, uiMax, uiStep, label, uiKind, surfaceMask, flags)",
    ])
    for _, ident, row in row_records:
        lines.append(f"#define MP_HUD_PROP_ROW_{ident}(X) {row.strip()}")
    lines.append("")
    lines.append("// Schema views reference row macros so each key literal has a single owner.")
    append_schema_macro(lines, "MP_HUD_PROP_SCHEMA", [f"    MP_HUD_PROP_ROW_{ident}(X)" for _, ident, _ in row_records])
    lines.append("")
    lines.append("// Type-filtered views preserve the Config.cpp DefaultList<T> ownership.")
    for cfg_type in ("Int", "Bool", "String", "Double"):
        typed_rows = [f"    MP_HUD_PROP_ROW_{ident}(X)" for prop, ident, _ in row_records if prop.cfg_type == cfg_type]
        append_schema_macro(lines, f"MP_HUD_PROP_SCHEMA_{cfg_type.upper()}", typed_rows)
        lines.append("")
    lines.extend(["#endif // MELONPRIME_HUD_PROP_SCHEMA_INC", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def dialog_key_expr(key: str | None, key_to_ident: dict[str, str], empty_expr: str) -> str:
    if not key:
        return empty_expr
    ident = key_to_ident.get(key)
    if ident:
        return f"MP_HUD_PROP_KEY_{ident}"
    return cpp_quote(key)


def write_dialog_props(sections: list[DialogSection], key_to_ident: dict[str, str], path: Path) -> None:
    lines: list[str] = [
        "// Generated by tools/codegen/hud/generate-hud-prop-schema.py.",
        "// Dialog HudWidgetProp tables for MelonPrimeInputConfig.cpp.",
        "// Requires MelonPrimeHudPropSchema.inc to be included first.",
        "// Do not edit rows by hand; regenerate from the HUD schema source surfaces.",
        "",
        "#ifndef MELONPRIME_INPUT_CONFIG_HUD_DIALOG_PROPS_INC",
        "#define MELONPRIME_INPUT_CONFIG_HUD_DIALOG_PROPS_INC",
        "",
    ]
    emitted_osd_include = False
    for section in sections:
        if section.name in OSD_DIALOG_SECTION_NAMES:
            if not emitted_osd_include:
                lines.extend([
                    "#define MELONPRIME_OSD_COLOR_EMIT_DIALOG_PROPS",
                    "#include \"../MelonPrimeOsdColorSchema.inc\"",
                    "#undef MELONPRIME_OSD_COLOR_EMIT_DIALOG_PROPS",
                    "",
                ])
                emitted_osd_include = True
            continue
        lines.append(f"static const HudWidgetProp {section.name}[] = {{")
        for row in section.rows:
            cfg_key = dialog_key_expr(row.cfg_key, key_to_ident, '""')
            cfg_key_g = dialog_key_expr(row.cfg_key_g, key_to_ident, "nullptr")
            cfg_key_b = dialog_key_expr(row.cfg_key_b, key_to_ident, "nullptr")
            lines.append(
                "    { "
                f"{cpp_quote(row.label)}, HWType::{row.widget_type}, {cfg_key}, "
                f"{row.min_value}, {row.max_value}, {row.step}, {cfg_key_g}, {cfg_key_b}"
                " },"
            )
        lines.append("};")
        lines.append("")
    lines.extend(["#endif // MELONPRIME_INPUT_CONFIG_HUD_DIALOG_PROPS_INC", ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def edit_ref_expr(value: str | None, key_to_ident: dict[str, str]) -> str:
    if value is None:
        return "nullptr"
    ident = key_to_ident.get(value)
    if ident:
        return f"MP_HUD_PROP_KEY_{ident}"
    return value


def write_edit_props(
    prop_arrays: list[EditPropArray],
    edit_elems: list[EditElemRow],
    key_to_ident: dict[str, str],
    path: Path,
) -> None:
    lines: list[str] = [
        "// Generated by tools/codegen/hud/generate-hud-prop-schema.py.",
        "// On-screen edit-mode HudEditPropDesc/HudEditElemDesc tables.",
        "// Requires MelonPrimeHudPropSchema.inc and HUD_EDIT_PROP_COUNT to be defined first.",
        "// Do not edit rows by hand; regenerate from the HUD schema source surfaces.",
        "",
        "#ifndef MELONPRIME_HUD_CONFIG_ON_SCREEN_EDIT_PROPS_INC",
        "#define MELONPRIME_HUD_CONFIG_ON_SCREEN_EDIT_PROPS_INC",
        "",
    ]
    for prop_array in prop_arrays:
        lines.append(f"static const HudEditPropDesc {prop_array.name}[] = {{")
        for row in prop_array.rows:
            lines.append(
                "    {"
                f"{cpp_quote(row.label)}, EditPropType::{row.prop_type}, {edit_ref_expr(row.cfg_key, key_to_ident)}, "
                f"{row.min_value}, {row.max_value}, {row.step}, "
                f"{edit_ref_expr(row.extra1, key_to_ident)}, {edit_ref_expr(row.extra2, key_to_ident)}, {edit_ref_expr(row.extra3, key_to_ident)}"
                "},"
            )
        lines.append("};")
        lines.append("")

    count_names = {
        "kPropsCrosshairMain": "kCrosshairMainCount",
        "kPropsCrosshairNormal": "kCrosshairNormalCount",
        "kPropsCrosshairZoom": "kCrosshairZoomCount",
        "kPropsCrosshairInner": "kCrosshairInnerCount",
        "kPropsCrosshairOuter": "kCrosshairOuterCount",
    }
    for prop_array in prop_arrays:
        if prop_array.name in count_names:
            lines.append(f"static constexpr int {count_names[prop_array.name]} = HUD_EDIT_PROP_COUNT({prop_array.name});")
    lines.append("")

    lines.append("static const HudEditElemDesc kEditElems[kEditElemCount] = {")
    for row in edit_elems:
        lines.append("    {")
        lines.append(f"        {cpp_quote(row.name)},")
        lines.append(
            "        "
            f"{edit_ref_expr(row.anchor_key, key_to_ident)}, {edit_ref_expr(row.ofs_x_key, key_to_ident)}, {edit_ref_expr(row.ofs_y_key, key_to_ident)},"
        )
        lines.append(
            "        "
            f"{edit_ref_expr(row.orient_key, key_to_ident)}, {edit_ref_expr(row.length_key, key_to_ident)}, "
            f"{edit_ref_expr(row.width_key, key_to_ident)}, {edit_ref_expr(row.pos_mode_key, key_to_ident)},"
        )
        lines.append(f"        {edit_ref_expr(row.show_key, key_to_ident)},")
        lines.append(
            "        "
            f"{edit_ref_expr(row.color_r_key, key_to_ident)}, {edit_ref_expr(row.color_g_key, key_to_ident)}, {edit_ref_expr(row.color_b_key, key_to_ident)},"
        )
        lines.append(f"        {row.props_expr}, {row.prop_count_expr}")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.extend([
        "#undef HUD_EDIT_PROP_COUNT",
        "",
        "#endif // MELONPRIME_HUD_CONFIG_ON_SCREEN_EDIT_PROPS_INC",
        "",
    ])
    path.write_text("\n".join(lines), encoding="utf-8")


def set_for_surface(props: dict[str, Prop], surface: str) -> set[str]:
    return {key for key, prop in props.items() if surface in prop.surfaces()}


def write_report(order: list[str], props: dict[str, Prop], extra_refs: dict[str, list[Meta]], path: Path) -> None:
    default_keys = set(order)
    surface_sets = {name: set_for_surface(props, name) for name in ("dialog", "edit", "side", "runtime")}
    missing_default = {name: sorted(extra_refs_for_surface(extra_refs, name)) for name in surface_sets}
    defaults_only = {name: sorted(default_keys - keys) for name, keys in surface_sets.items()}

    type_drifts = [prop for prop in props.values() if prop.type_drift()]
    range_drifts = [prop for prop in props.values() if prop.range_drift()]
    schema_rows = len(order)

    lines: list[str] = [
        "# MelonPrime HUD Prop Schema Phase 2a Report",
        "",
        "Generated by `tools/codegen/hud/generate-hud-prop-schema.py`.",
        "",
        "## Summary",
        "",
        f"- Schema rows: {schema_rows}",
        f"- Config.cpp `Metroid.Visual.*` defaults: {len(default_keys)}",
        f"- Extra face references missing defaults: {sum(len(v) for v in missing_default.values())}",
        f"- Type drift rows: {len(type_drifts)}",
        f"- Range drift rows: {len(range_drifts)}",
        "",
        "## Surface Counts",
        "",
        "| Surface | Keys | Missing defaults | Defaults not referenced |",
        "|---|---:|---:|---:|",
    ]
    for name in ("dialog", "edit", "side", "runtime"):
        lines.append(f"| {name} | {len(surface_sets[name])} | {len(missing_default[name])} | {len(defaults_only[name])} |")

    lines.extend(["", "## Missing Defaults", ""])
    for name in ("dialog", "edit", "side", "runtime"):
        lines.append(f"### {name}")
        if missing_default[name]:
            lines.extend(f"- `{key}`" for key in missing_default[name])
        else:
            lines.append("- None")
        lines.append("")

    lines.extend(["## Defaults Not Referenced By Surface", ""])
    for name in ("dialog", "edit", "side", "runtime"):
        lines.append(f"### {name} ({len(defaults_only[name])})")
        for key in defaults_only[name][:80]:
            lines.append(f"- `{key}`")
        if len(defaults_only[name]) > 80:
            lines.append(f"- ... {len(defaults_only[name]) - 80} more")
        lines.append("")

    lines.extend(["## Type Drift", ""])
    if not type_drifts:
        lines.append("- None")
    else:
        for prop in sorted(type_drifts, key=lambda p: p.key):
            details = ", ".join(f"{m.surface}:{m.inferred_type}@{m.origin}" for m in prop.type_drift())
            lines.append(f"- `{prop.key}` default={prop.cfg_type}; {details}")
    lines.append("")

    lines.extend(["## Range Drift", ""])
    if not range_drifts:
        lines.append("- None")
    else:
        for prop in sorted(range_drifts, key=lambda p: p.key):
            lines.append(f"- `{prop.key}`")
            for rng, metas in sorted(prop.range_drift().items(), key=lambda item: item[0]):
                origins = ", ".join(f"{m.surface}@{m.origin}" for m in metas)
                lines.append(f"  - {rng[0]}..{rng[1]} step {rng[2]}: {origins}")
    lines.append("")

    lines.extend(["## Pairwise Surface Drift", ""])
    names = ("dialog", "edit", "side", "runtime")
    for a in names:
        for b in names:
            if a == b:
                continue
            diff = sorted(surface_sets[a] - surface_sets[b])
            lines.append(f"### {a} not in {b} ({len(diff)})")
            for key in diff[:60]:
                lines.append(f"- `{key}`")
            if len(diff) > 60:
                lines.append(f"- ... {len(diff) - 60} more")
            lines.append("")

    path.write_text("\n".join(lines), encoding="utf-8")


def extra_refs_for_surface(extra_refs: dict[str, list[Meta]], surface: str) -> set[str]:
    result: set[str] = set()
    for key, metas in extra_refs.items():
        if any(meta.surface == surface for meta in metas):
            result.add(key)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, default=SCHEMA_OUT)
    parser.add_argument("--dialog-props", type=Path, default=DIALOG_PROPS_INC)
    parser.add_argument("--edit-props", type=Path, default=EDIT_PROPS_INC)
    parser.add_argument("--report", type=Path, default=REPORT_OUT)
    parser.add_argument("--defaults-source", type=Path, default=CONFIG_CPP)
    parser.add_argument("--no-write", action="store_true")
    args = parser.parse_args()

    order, props = parse_defaults(args.defaults_source)
    if not order:
        order, props = parse_existing_schema(args.schema)
    key_to_ident, ident_to_key = build_identifier_maps(order)
    extra_refs: dict[str, list[Meta]] = {}
    dialog_sections = parse_dialog(props, extra_refs, ident_to_key)
    edit_prop_arrays, edit_elems = parse_edit_descriptors(props, extra_refs, ident_to_key)
    parse_side_panel(props, extra_refs, ident_to_key)
    parse_runtime(props, extra_refs, ident_to_key)

    if not args.no_write:
        write_schema(order, props, args.schema)
        write_dialog_props(dialog_sections, key_to_ident, args.dialog_props)
        write_edit_props(edit_prop_arrays, edit_elems, key_to_ident, args.edit_props)
        write_report(order, props, extra_refs, args.report)

    surface_counts = {
        name: len(set_for_surface(props, name))
        for name in ("dialog", "edit", "side", "runtime")
    }
    print(f"schema rows: {len(order)}")
    for name, count in surface_counts.items():
        missing = len(extra_refs_for_surface(extra_refs, name))
        print(f"{name}: {count} key(s), missing defaults {missing}")
    print(f"type drift rows: {sum(1 for prop in props.values() if prop.type_drift())}")
    print(f"range drift rows: {sum(1 for prop in props.values() if prop.range_drift())}")
    if not args.no_write:
        print(f"wrote {args.schema}")
        print(f"wrote {args.dialog_props}")
        print(f"wrote {args.edit_props}")
        print(f"wrote {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
