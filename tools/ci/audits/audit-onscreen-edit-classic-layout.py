#!/usr/bin/env python3
"""Audit the source-level layout contract for Classic HUD On-Screen Edit.

This is intentionally narrower than audit-onscreen-edit-style.py.  It checks
that the production code has the structural hooks required for localized
geometry, while the companion Qt executable exercises the resulting layout at
runtime.  A source grep alone is not treated as proof of the geometry.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
EDIT = ROOT / "src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenEdit.cpp"
BUILDER = ROOT / "src/frontend/qt_sdl/MelonPrimeHudEditorFormBuilder.cpp"
FORM_LAYOUT = ROOT / "src/frontend/qt_sdl/MelonPrimeHudEditorFormLayout.cpp"
POSITION = ROOT / "src/frontend/qt_sdl/MelonPrimeHudScreenCppHelpers.inc"


def section(source: str, start: str, end: str | None = None) -> str:
    start_at = source.find(start)
    if start_at < 0:
        return ""
    end_at = source.find(end, start_at + len(start)) if end else -1
    return source[start_at:] if end_at < 0 else source[start_at:end_at]


def main() -> int:
    sources = {
        "edit": EDIT.read_text(encoding="utf-8"),
        "builder": BUILDER.read_text(encoding="utf-8"),
        "form_layout": FORM_LAYOUT.read_text(encoding="utf-8"),
        "position": POSITION.read_text(encoding="utf-8"),
    }
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    edit = sources["edit"]
    builder = sources["builder"]
    form_layout = sources["form_layout"]
    position = sources["position"]

    # Fixed panel and fixed typography are the confirmed root causes.
    require("kPanelWidth" not in edit, "Classic editor still declares kPanelWidth")
    require("setFixedWidth(" not in edit, "Classic editor still uses setFixedWidth")
    require("setPixelSize(" not in edit, "Classic editor still sets pixel-size typography")
    require("setRowWrapPolicy(QFormLayout::WrapLongRows)" in edit,
            "Classic editor does not enable QFormLayout::WrapLongRows")
    require("m_title->setWordWrap(true)" in edit,
            "Classic editor title is not configured for wrapping")
    require("ScrollBarAlwaysOff" in edit and "ScrollBarAsNeeded" in edit,
            "Classic editor scrollbar contract changed unexpectedly")

    # Every ordinary translated field must pass through the explicit label
    # helper; implicit QFormLayout QString labels are not enough here.
    require("CreateTranslatedFormLabel(form, UiText::Tr(label))" in builder,
            "AppendLabeledRow does not create an explicit translated QLabel")
    require("form.addRow(UiText::Tr(label)" not in builder,
            "Builder still relies on an implicit translated form label")
    require("ctx.form.addRow(UiText::Tr(label)" not in builder,
            "A specialized row still relies on an implicit translated form label")
    require("ConfigureWrappedFormLabel" in form_layout,
            "Shared wrapped form-label helper is missing")
    require("setWordWrap(true)" in form_layout and "QSizePolicy::Preferred" in form_layout,
            "Shared form-label helper lacks wrapping or preferred size policy")

    bool_row = section(builder, "QWidget* AddBoolRadioRow", "QComboBox* AddComboBoxRow")
    require(bool_row, "Bool row factory could not be located")
    require("rowLabel" not in bool_row, "Bool row still embeds its translated label in the field HBox")
    require("AppendLabeledRow(ctx.form, ctx.rows, label, *container)" in bool_row,
            "Bool row is not a QFormLayout label/field row")
    require("hlay->addWidget(on" in bool_row and "hlay->addWidget(off" in bool_row,
            "Bool row controls are missing from the field container")

    opacity_row = section(builder, "QSlider* AddOpacitySliderRow", "QLineEdit* AddLineEditRow")
    require(opacity_row, "Opacity row factory could not be located")
    require("AppendLabeledRow(ctx.form, ctx.rows, label, *container)" in opacity_row,
            "Opacity row is not a QFormLayout label/field row")

    sub_color_row = section(builder, "void AddSubColorRow", "void AddColorOverlayRow")
    require(sub_color_row, "SubColor row factory could not be located")
    require("AppendLabeledRow(ctx.form, ctx.rows, label, *container)" in sub_color_row,
            "SubColor row is not a QFormLayout label/field row")

    color_row = section(builder, "void AddColorOverlayRow", "} // namespace MelonPrime::HudEditorForm")
    require(color_row, "ColorOverlay row factory could not be located")
    require("rowLabel" not in color_row,
            "ColorOverlay row still embeds its translated label in the field HBox")
    require("AppendLabeledRow(ctx.form, ctx.rows, label, *container)" in color_row,
            "ColorOverlay row is not a QFormLayout label/field row")
    require("hlay->addWidget(on" in color_row and "hlay->addWidget(off" in color_row
            and "hlay->addWidget(btn" in color_row,
            "ColorOverlay field controls are incomplete")
    require("font-size: 9px" not in builder and "setPixelSize(" not in builder,
            "Builder still contains fixed pixel typography")

    # The position helper must establish width before activating the layout,
    # and must clamp the content-aware width to the usable window width.
    for token, description in (
        ("usableWidth", "usable window width"),
        ("naturalWidth", "natural content width"),
        ("desiredWidth", "preferred/natural width arbitration"),
        ("setMaximumWidth(usableWidth)", "adaptive maximum width"),
        ("const int panelW = std::min(desiredWidth, usableWidth)", "final width clamp"),
        ("panel->layout()->activate()", "layout activation after width selection"),
    ):
        require(token in position, f"Position helper lacks {description}")
    require("setMinimumWidth(300)" not in position and "setFixedWidth(" not in position,
            "Position helper reintroduced a fixed panel width")

    # Keep the audit honest if a future edit hides a required call in a comment
    # or changes the helper's role names into an unrelated string.
    require(re.search(r"void\s+AppendLabeledRow\s*\([^)]*\)\s*\{", builder, re.S) is not None,
            "AppendLabeledRow definition is missing")

    if failures:
        print("Classic On-Screen Edit layout audit: FAIL")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("Classic On-Screen Edit layout audit: PASS")
    print("- adaptive width, WrapLongRows, explicit translated labels, and row reflow structure verified")
    print("- fixed panel typography and horizontal-scroll workaround not detected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
