#!/usr/bin/env python3
"""Verify that the V7 Phase 2 row-table refactor produces the identical
sequence of add*() factory calls as the pre-refactor hand-written code, for
every HUD editor side-panel element.

Approach: parse the OLD populate*() function bodies (git HEAD) into a
sequence of (callName, normalizedArgs) tuples. Independently interpret the
NEW row tables (MelonPrimeHudEditorSidePanelRows.inc) + the residual code in
the NEW populate*() bodies, producing the same kind of tuple sequence via the
same RowKind -> add*() mapping the C++ dispatcher uses. Diff the two
sequences per element; report exact equality or the first divergence.
"""
import re
import sys

OLD_PATH = "/tmp/old_edit.cpp"
ROWS_PATH = "src/frontend/qt_sdl/MelonPrimeHudEditorSidePanelRows.inc"
NEW_CPP_PATH = "src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenEdit.cpp"


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def strip_qstringliteral(s):
    s = s.strip()
    m = re.match(r'^QStringLiteral\("(.*)"\)$', s, re.S)
    if m:
        return m.group(1)
    m = re.match(r'^"(.*)"$', s, re.S)
    if m:
        return m.group(1)
    if s == "nullptr":
        return None
    return s


def split_top_level_args(argtext):
    """Split a call's argument text on top-level commas (respecting {}, (), \"\")."""
    args = []
    depth = 0
    cur = []
    in_str = False
    i = 0
    while i < len(argtext):
        c = argtext[i]
        if in_str:
            cur.append(c)
            if c == '"' and argtext[i - 1] != '\\':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            cur.append(c)
            i += 1
            continue
        if c in "({[":
            depth += 1
            cur.append(c)
        elif c in ")}]":
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if cur:
        args.append("".join(cur).strip())
    return [a for a in args if a != ""]


def try_number(s):
    try:
        if "." in s:
            return float(s)
        return int(s)
    except (TypeError, ValueError):
        return s


def expand_outline_keys(prefix):
    return (
        f"MP_HUD_PROP_KEY_{prefix}Outline",
        f"MP_HUD_PROP_KEY_{prefix}OutlineColorR",
        f"MP_HUD_PROP_KEY_{prefix}OutlineColorG",
        f"MP_HUD_PROP_KEY_{prefix}OutlineColorB",
        f"MP_HUD_PROP_KEY_{prefix}OutlineOpacity",
        f"MP_HUD_PROP_KEY_{prefix}OutlineThickness",
    )


def normalize_arg(a):
    a = a.strip()
    # combo item list: {QStringLiteral("X"), QStringLiteral("Y"), ...} or {"X","Y"}
    if a.startswith("{") and a.endswith("}"):
        inner = a[1:-1]
        items = split_top_level_args(inner)
        return tuple(strip_qstringliteral(x) for x in items)
    m = re.match(r"^MP_OUTLINE_KEYS\((\w+)\)$", a)
    if m:
        return ("__OUTLINE_KEYS__",) + expand_outline_keys(m.group(1))
    val = strip_qstringliteral(a)
    if val is None:
        return None
    return try_number(val) if val == a or re.match(r"^-?\d+(\.\d+)?$", val) else val


FUNC_NAMES = [
    "addBuiltins", "addOffsetRows", "addLineEdit", "addAlign3Combo",
    "addOpacitySlider", "addOutlineGroupSection", "addComboBox", "addCheckBox",
    "addColorPicker", "addSubColor", "addSpinBox", "addDoubleSpinBox",
    "addGaugePositionRows", "addSeparator", "addSectionHeader",
    "addColorOverlayRow",
]

CALL_RE = re.compile(
    r"\b(" + "|".join(FUNC_NAMES) + r")\s*\(", re.S
)


def extract_calls_from_body(body):
    """Extract ordered (name, tuple-of-normalized-args) from a C++ function body,
    matching balanced parens starting at each recognized call name."""
    calls = []
    i = 0
    while True:
        m = CALL_RE.search(body, i)
        if not m:
            break
        name = m.group(1)
        start = m.end()  # just after '('
        depth = 1
        j = start
        in_str = False
        while j < len(body) and depth > 0:
            c = body[j]
            if in_str:
                if c == '"' and body[j - 1] != '\\':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            j += 1
        argtext = body[start:j - 1]
        args = tuple(normalize_arg(a) for a in split_top_level_args(argtext))
        calls.append((name, args))
        i = j
    return calls


def extract_function_body(src, funcname_pattern):
    m = re.search(
        r"void MelonPrimeHudConfigOnScreenEdit::" + funcname_pattern + r"\s*\(\)\s*\n\{\n",
        src)
    if not m:
        raise SystemExit(f"function not found: {funcname_pattern}")
    start = m.end()
    depth = 1
    i = start
    while depth > 0:
        c = src[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return src[start:i - 1]


ELEMENTS = [
    "populateHP", "populateHPGauge", "populateWeaponAmmo", "populateWpnIcon",
    "populateAmmoGauge", "populateMatchStatus", "populateRank",
    "populateTimeLeft", "populateTimeLimit", "populateBombLeft",
    "populateBombIcon", "populateWeaponInventory", "populateRadar",
    "populateForCrosshair",
]

old_src = read(OLD_PATH)
old_calls = {}
for fn in ELEMENTS:
    body = extract_function_body(old_src, fn)
    old_calls[fn] = extract_calls_from_body(body)

# ---- NEW: parse row tables ----

rows_src = read(ROWS_PATH)

# Extract MP_SIDE_ROW_COUNT tables: static const Row kRowsXxx[] = { ... };
table_re = re.compile(r"static const Row (kRows\w+)\[\]\s*=\s*\{(.*?)\n\};", re.S)
tables = {}
for m in table_re.finditer(rows_src):
    tname = m.group(1)
    body = m.group(2)
    # split top-level entries by matching RowXxx(...) calls
    entries = []
    i = 0
    entry_re = re.compile(r"Row(\w+)\s*\(")
    while True:
        em = entry_re.search(body, i)
        if not em:
            break
        kind = em.group(1)
        start = em.end()
        depth = 1
        j = start
        in_str = False
        while j < len(body) and depth > 0:
            c = body[j]
            if in_str:
                if c == '"' and body[j - 1] != '\\':
                    in_str = False
            elif c == '"':
                in_str = True
            elif c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            j += 1
        argtext = body[start:j - 1]
        args = split_top_level_args(argtext)
        entries.append((kind, [normalize_arg(a) for a in args]))
        i = j
    tables[tname] = entries

# Simple macro expansion for MP_OUTLINE_KEYS(Prefix) -> 6 key strings
def expand_outline_keys(prefix):
    return (
        f"MP_HUD_PROP_KEY_{prefix}Outline",
        f"MP_HUD_PROP_KEY_{prefix}OutlineColorR",
        f"MP_HUD_PROP_KEY_{prefix}OutlineColorG",
        f"MP_HUD_PROP_KEY_{prefix}OutlineColorB",
        f"MP_HUD_PROP_KEY_{prefix}OutlineOpacity",
        f"MP_HUD_PROP_KEY_{prefix}OutlineThickness",
    )


def row_to_calls(kind, args):
    """Map one Row*(...) factory call back to the equivalent add*() call(s)."""
    if kind == "Bool":
        return [("addCheckBox", (args[0], args[1]))]
    if kind == "Combo":
        return [("addComboBox", (args[0], args[1], tuple(args[2])))]
    if kind == "Spin":
        return [("addSpinBox", (args[0], args[1], int(args[2]), int(args[3])))]
    if kind == "DoubleSpin":
        return [("addDoubleSpinBox", (args[0], args[1], float(args[2]), float(args[3]), float(args[4])))]
    if kind == "Opacity":
        return [("addOpacitySlider", (args[0], args[1]))]
    if kind == "LineEdit":
        return [("addLineEdit", (args[0], args[1]))]
    if kind == "Color":
        return [("addColorPicker", (args[0], args[1], args[2], args[3]))]
    if kind == "SubColor":
        return [("addSubColor", (args[0], args[1], args[2], args[3], args[4]))]
    if kind == "ColorOverlay":
        return [("addColorOverlayRow", (args[0], args[1], args[2], args[3], args[4]))]
    if kind == "Separator":
        return [("addSeparator", ())]
    if kind == "SectionHeader":
        return [("addSectionHeader", (args[0],))]
    if kind == "Builtins":
        return [("addBuiltins", (args[0], args[1], args[2], args[3], args[4]))]
    if kind == "Offset":
        return [("addOffsetRows", (args[0], args[1], int(args[2]), int(args[3]), args[4], args[5]))]
    if kind == "Align3":
        return [("addAlign3Combo", (args[0], args[1]))]
    if kind == "Outline":
        # args[1] is already the ("__OUTLINE_KEYS__", 6 keys...) tuple that
        # normalize_arg produces for a raw MP_OUTLINE_KEYS(Prefix) argument on
        # both the OLD (addOutlineGroupSection) and NEW (RowOutline) sides, so
        # this shape matches OLD's 2-arg call exactly without re-flattening.
        return [("addOutlineGroupSection", (args[0], args[1]))]
    if kind == "GaugePosition":
        return [("addGaugePositionRows", tuple(args))]
    raise SystemExit(f"unknown row kind: {kind}")


def table_to_calls(tname):
    calls = []
    for kind, args in tables[tname]:
        calls.extend(row_to_calls(kind, args))
    return calls


# Fix normalize_arg: it doesn't handle bare identifiers like MP_OUTLINE_KEYS(HudHp)
# being swallowed as ONE token because of parens; re-derive using regex directly
# on raw entries instead: patch by re-parsing entry argtext specially for Outline.
# (Handled by outline detection heuristic above via raw text match already,
#  since split_top_level_args keeps "MP_OUTLINE_KEYS(HudHp)" together as one arg
#  because parens increase depth -- confirm this is really what happens.)

new_src = read(NEW_CPP_PATH)

mapping = {
    "populateHP": ("kRowsHP", None),
    "populateHPGauge": ("kRowsHPGauge", None),
    "populateWeaponAmmo": ("kRowsWeaponAmmo", None),
    "populateAmmoGauge": ("kRowsAmmoGauge", None),
    "populateMatchStatus": ("kRowsMatchStatus", None),
    "populateRank": ("kRowsRank", None),
    "populateTimeLeft": ("kRowsTimeLeft", None),
    "populateTimeLimit": ("kRowsTimeLimit", None),
    "populateBombLeft": ("kRowsBombLeft", None),
    "populateBombIcon": ("kRowsBombIcon", None),
    "populateWeaponInventory": ("kRowsWeaponInventory", None),
    "populateRadar": ("kRowsRadar", None),
    "populateForCrosshair": ("kRowsCrosshair", None),
}

new_calls = {}
for fn, (tname, _) in mapping.items():
    new_calls[fn] = table_to_calls(tname)

# populateWpnIcon: table + residual loop code (extract separately from new .cpp)
wpnicon_body = extract_function_body(new_src, "populateWpnIcon")
wpnicon_calls = table_to_calls("kRowsWpnIcon")
wpnicon_calls.extend(extract_calls_from_body(wpnicon_body))
# the residual body also re-matches "populateFromRowTable(...)" which isn't in
# our FUNC_NAMES list, so extract_calls_from_body will simply skip it (fine).
new_calls["populateWpnIcon"] = wpnicon_calls

ok = True
for fn in ELEMENTS:
    o = old_calls[fn]
    n = new_calls[fn]
    if o != n:
        ok = False
        print(f"MISMATCH in {fn}:")
        for idx in range(max(len(o), len(n))):
            oc = o[idx] if idx < len(o) else None
            nc = n[idx] if idx < len(n) else None
            if oc != nc:
                print(f"  [{idx}] OLD: {oc}")
                print(f"  [{idx}] NEW: {nc}")
    else:
        print(f"OK  {fn}: {len(o)} calls match exactly")

sys.exit(0 if ok else 1)
