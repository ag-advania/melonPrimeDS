// This file is a unity-build fragment included by MelonPrimeHudConfigOnScreen.cpp.
// Do not add it to CMakeLists.txt.

// ── Public API ──────────────────────────────────────────────────────────────

void CustomHud_EnterEditMode(EmuInstance* emu, Config::Table& cfg)
{
    s_editEmu           = emu;
    s_editSelected      = -1;
    s_editHovered       = -1;
    s_dragging          = false;
    s_resizingLength    = false;
    s_resizingWidth     = false;
    s_editPreviewMode   = false;
    SnapshotEditConfig(cfg);
    s_editMode = true;
    CustomHud_InvalidateConfigCache();
    NotifySelectionChanged(-1);

    if (auto* core = emu->getEmuThread()->GetMelonPrimeCore())
        core->isCursorMode = true;
}

void CustomHud_ExitEditMode(bool save, Config::Table& cfg)
{
    if (!save) {
        RestoreEditSnapshot(cfg);
    }
    CustomHud_InvalidateConfigCache();
    if (save) Config::Save();

    s_editMode           = false;
    s_editSelected       = -1;
    s_editHovered           = -1;
    s_dragging              = false;
    s_textScaleDragging     = false;
    s_anchorPickerOpen      = false;
    s_crosshairPanelOpen    = false;
    s_innerSectionOpen      = false;
    s_outerSectionOpen      = false;
    s_crosshairPanelScroll  = 0;
    s_resizingLength     = false;
    s_resizingWidth      = false;
    s_editPreviewMode    = false;
    s_editEmu            = nullptr;
    NotifySelectionChanged(-1);
}

bool CustomHud_IsEditMode()
{
    return s_editMode;
}

void CustomHud_SetEditSelectionCallback(std::function<void(int)> cb)
{
    s_editSelectionCb = std::move(cb);
}

int CustomHud_GetSelectedElement()
{
    return s_editSelected;
}

void CustomHud_UpdateEditContext(float originX, float originY,
                                  float hudScale, float topStretchX)
{
    s_editOriginX     = originX;
    s_editOriginY     = originY;
    s_editHudScale    = hudScale;
    s_editTopStretchX = topStretchX;
}

static int HitTestEditElement(const QPointF& ds)
{
    // Radar first: it can overlap other UI near the top-right corner.
    if (!s_editRects[11].isEmpty() && s_editRects[11].contains(ds))
        return 11;

    for (int i = 0; i < kEditElemCount; ++i) {
        if (i == 11) continue;
        if (!s_editRects[i].isEmpty() && s_editRects[i].contains(ds))
            return i;
    }
    return -1;
}

void CustomHud_EditMousePress(QPointF pt, Qt::MouseButton btn, Config::Table& cfg)
{
    // Accept both left and right mouse buttons
    if (btn != Qt::LeftButton && btn != Qt::RightButton) return;
    const QPointF ds = WidgetToDS(pt);
    const int hitElem = HitTestEditElement(ds);

    // Right-click: select element under cursor for property editing (both modes)
    if (btn == Qt::RightButton) {
        // Hit-test elements first so overlapped items like the radar can still be selected.
        if (hitElem >= 0) {
            if (s_editSelected != hitElem) {
                s_editSelected = hitElem;
                s_editPropScroll = 0;
                s_anchorPickerOpen = false;
            }
            NotifySelectionChanged(hitElem);
            return;
        }
        // Absorb click inside open properties panel
        if (kShowDsEditPropsPanel && s_editSelected >= 0) {
            const HudEditElemDesc& d = kEditElems[s_editSelected];
            const int totalRows = CountBuiltinRows(d) + d.propCount;
            if (totalRows > 0) {
                QRectF panelRect = ComputePropsPanelRect(s_editRects[s_editSelected], totalRows);
                if (panelRect.contains(ds)) return;
            }
        }
        // Absorb click on button bar items
        if (kEditSaveRect.contains(ds) || kEditCancelRect.contains(ds) ||
            kEditResetRect.contains(ds) || kEditTextScaleRect.contains(ds) ||
            kEditCrosshairBtnRect.contains(ds) || kEditPreviewBtnRect.contains(ds))
            return;
        // Right-click on empty space: deselect
        if (s_editSelected >= 0) {
            s_editSelected = -1;
            s_anchorPickerOpen = false;
            NotifySelectionChanged(-1);
        }
        return;
    }
    // ── Left-click priority system ──────────────────────────────────────────

    const bool hitTopControl = kEditSaveRect.contains(ds) || kEditCancelRect.contains(ds) ||
                               kEditResetRect.contains(ds) || kEditTextScaleRect.contains(ds) ||
                               kEditCrosshairBtnRect.contains(ds) || kEditPreviewBtnRect.contains(ds);
    if (hitElem >= 0 && hitTopControl) {
        const HudEditElemDesc& di = kEditElems[hitElem];

        if (di.posModeKey != nullptr && cfg.GetInt(di.posModeKey) == 0) {
            if (UNLIKELY(!s_cache.valid) || s_cache.lastHudScale != s_editHudScale) {
                RefreshCachedConfig(cfg, s_editTopStretchX, s_editHudScale);
                s_cache.valid = true;
            } else if (s_cache.lastStretchX != s_editTopStretchX) {
                RecomputeAnchorPositions(s_editTopStretchX);
            }
            int visualX = 0, visualY = 0;
            if (hitElem == 1) { visualX = s_cache.hp.hpGaugePosX;      visualY = s_cache.hp.hpGaugePosY; }
            if (hitElem == 4) { visualX = s_cache.weapon.ammoGaugePosX; visualY = s_cache.weapon.ammoGaugePosY; }
            cfg.SetInt(di.posModeKey, 1);
            cfg.SetInt(di.anchorKey, 0);
            cfg.SetInt(di.ofsXKey, visualX);
            cfg.SetInt(di.ofsYKey, visualY);
            CustomHud_InvalidateConfigCache();
        }

        s_editSelected = hitElem;
        s_editPropScroll = 0;
        s_anchorPickerOpen = false;
        if (di.ofsXKey) {
            s_dragging = true;
            s_dragStartDS = ds;
            s_dragStartOfsX = cfg.GetInt(di.ofsXKey);
            s_dragStartOfsY = cfg.GetInt(di.ofsYKey);
        }
        s_editHovered = hitElem;
        NotifySelectionChanged(hitElem);
        return;
    }

    // Priority 1: Save / Cancel / Reset
    if (kEditSaveRect.contains(ds)) {
        CustomHud_ExitEditMode(true, cfg);
        return;
    }
    if (kEditCancelRect.contains(ds)) {
        CustomHud_ExitEditMode(false, cfg);
        return;
    }
    if (kEditResetRect.contains(ds)) {
        ResetEditToDefaults(cfg);
        s_editSelected = -1;
        CustomHud_InvalidateConfigCache();
        NotifySelectionChanged(-1);
        return;
    }

    // Priority 1b: Text Scale slider
    if (kEditTextScaleRect.contains(ds)) {
        const QRectF tr = TsTrackRect();
        if (tr.contains(ds)) {
            cfg.SetInt("Metroid.Visual.HudTextScale", TsValueFromX(static_cast<float>(ds.x())));
            CustomHud_InvalidateConfigCache();
            s_textScaleDragging = true;
        }
        return;
    }

    // Priority 1b2: Auto-Scale Cap slider
    if (kEditAutoScaleCapRect.contains(ds)) {
        const QRectF atr = AscTrackRect();
        if (atr.contains(ds)) {
            cfg.SetInt("Metroid.Visual.HudAutoScaleCap", AscValueFromX(static_cast<float>(ds.x())));
            CustomHud_InvalidateConfigCache();
            s_autoScaleCapDragging = true;
        }
        return;
    }

    // Priority 1c: Crosshair panel toggle
    if (kEditCrosshairBtnRect.contains(ds)) {
        s_crosshairPanelOpen = !s_crosshairPanelOpen;
        s_crosshairPanelScroll = 0;
        return;
    }

    // Priority 1d: Preview toggle
    if (kEditPreviewBtnRect.contains(ds)) {
        s_editPreviewMode = !s_editPreviewMode;
        if (s_editPreviewMode) {
            s_anchorPickerOpen = false;
        }
        return;
    }

    // Priority 1e: Crosshair panel clicks (when open — works in both modes)
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows();
        const int visCount  = std::min(totalRows, kCrosshairMaxVisible);
        const float panelH  = visCount * kPropRowH + 4.0f;
        const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);

        if (panelRect.contains(ds)) {
            const float rowX  = kCrosshairPanelX + 2.0f;
            const float ctrlX = rowX + kPropLabelW;
            const float ctrlW = kPropCtrlW;
            const int scrollOfs = std::min(s_crosshairPanelScroll,
                                           std::max(0, totalRows - kCrosshairMaxVisible));

            for (int vi = 0; vi < visCount; ++vi) {
                const int rowIdx = vi + scrollOfs;
                const float rowY = kCrosshairPanelY + 2.0f + vi * kPropRowH;
                QRectF rowRect(rowX, rowY, kPropLabelW + ctrlW, kPropRowH);
                if (!rowRect.contains(ds)) continue;

                bool isColorRow, isInnerHdr, isOuterHdr;
                const HudEditPropDesc* pr = GetCrosshairPropForRow(rowIdx, isColorRow, isInnerHdr, isOuterHdr);
                if (isColorRow) {
                    if (s_colorDialogOpen) return;
                    s_colorDialogOpen = true;
                    int r = cfg.GetInt("Metroid.Visual.CrosshairColorR");
                    int g = cfg.GetInt("Metroid.Visual.CrosshairColorG");
                    int b = cfg.GetInt("Metroid.Visual.CrosshairColorB");
                    QColor chosen = QColorDialog::getColor(QColor(r, g, b), nullptr,
                        QStringLiteral("Crosshair Color"));
                    s_colorDialogOpen = false;
                    if (chosen.isValid()) {
                        cfg.SetInt("Metroid.Visual.CrosshairColorR", chosen.red());
                        cfg.SetInt("Metroid.Visual.CrosshairColorG", chosen.green());
                        cfg.SetInt("Metroid.Visual.CrosshairColorB", chosen.blue());
                        CustomHud_InvalidateConfigCache();
                    }
                    return;
                }
                if (isInnerHdr) {
                    s_innerSectionOpen = !s_innerSectionOpen;
                    if (s_innerSectionOpen) s_outerSectionOpen = false;
                    return;
                }
                if (isOuterHdr) {
                    s_outerSectionOpen = !s_outerSectionOpen;
                    if (s_outerSectionOpen) s_innerSectionOpen = false;
                    return;
                }
                if (!pr) return;

                switch (pr->type) {
                case EditPropType::Bool:
                    cfg.SetBool(pr->cfgKey, !cfg.GetBool(pr->cfgKey));
                    CustomHud_InvalidateConfigCache();
                    return;
                case EditPropType::Enum:
                case EditPropType::Int: {
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    int val = cfg.GetInt(pr->cfgKey);
                    int step = pr->step > 0 ? pr->step : 1;
                    if (decR.contains(ds))
                        cfg.SetInt(pr->cfgKey, std::max(pr->minVal, val - step));
                    else if (incR.contains(ds))
                        cfg.SetInt(pr->cfgKey, std::min(pr->maxVal, val + step));
                    CustomHud_InvalidateConfigCache();
                    return;
                }
                case EditPropType::Float: {
                    float btnW2 = 8.0f;
                    QRectF decR(ctrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    QRectF incR(ctrlX + ctrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                    int pct = static_cast<int>(cfg.GetDouble(pr->cfgKey) * 100.0 + 0.5);
                    int step = pr->step > 0 ? pr->step : 5;
                    if (decR.contains(ds))
                        cfg.SetDouble(pr->cfgKey, std::max(pr->minVal, pct - step) / 100.0);
                    else if (incR.contains(ds))
                        cfg.SetDouble(pr->cfgKey, std::min(pr->maxVal, pct + step) / 100.0);
                    CustomHud_InvalidateConfigCache();
                    return;
                }
                default: return;
                }
            }
            return; // absorbed by panel
        }

        // Side panel clicks (Inner / Outer props)
        {
            const HudEditPropDesc* sideClickProps = nullptr;
            int sideClickCount = 0;
            if (s_innerSectionOpen) {
                sideClickProps = kPropsCrosshairInner; sideClickCount = kCrosshairInnerCount;
            } else if (s_outerSectionOpen) {
                sideClickProps = kPropsCrosshairOuter; sideClickCount = kCrosshairOuterCount;
            }
            if (sideClickProps) {
                const float sidePanelH = sideClickCount * kPropRowH + 4.0f;
                const QRectF sideRect(kCrosshairSidePanelX, kCrosshairPanelY, kPropPanelW, sidePanelH);
                if (sideRect.contains(ds)) {
                    const float sRowX  = kCrosshairSidePanelX + 2.0f;
                    const float sCtrlX = sRowX + kPropLabelW;
                    for (int i = 0; i < sideClickCount; ++i) {
                        const float rowY = kCrosshairPanelY + 2.0f + i * kPropRowH;
                        QRectF rowRect(sRowX, rowY, kPropLabelW + kPropCtrlW, kPropRowH);
                        if (!rowRect.contains(ds)) continue;
                        const HudEditPropDesc& pr2 = sideClickProps[i];
                        switch (pr2.type) {
                        case EditPropType::Bool:
                            cfg.SetBool(pr2.cfgKey, !cfg.GetBool(pr2.cfgKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        case EditPropType::Int: {
                            float btnW2 = 8.0f;
                            QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            int val = cfg.GetInt(pr2.cfgKey);
                            int step = pr2.step > 0 ? pr2.step : 1;
                            if (decR.contains(ds))
                                cfg.SetInt(pr2.cfgKey, std::max(pr2.minVal, val - step));
                            else if (incR.contains(ds))
                                cfg.SetInt(pr2.cfgKey, std::min(pr2.maxVal, val + step));
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::Float: {
                            float btnW2 = 8.0f;
                            QRectF decR(sCtrlX, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            QRectF incR(sCtrlX + kPropCtrlW - btnW2, rowY + 1.0f, btnW2, kPropRowH - 2.0f);
                            int pct = static_cast<int>(cfg.GetDouble(pr2.cfgKey) * 100.0 + 0.5);
                            int step = pr2.step > 0 ? pr2.step : 5;
                            if (decR.contains(ds))
                                cfg.SetDouble(pr2.cfgKey, std::max(pr2.minVal, pct - step) / 100.0);
                            else if (incR.contains(ds))
                                cfg.SetDouble(pr2.cfgKey, std::min(pr2.maxVal, pct + step) / 100.0);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        default: return;
                        }
                    }
                    return; // absorbed by side panel
                }
            }
        }
    }

    // Priority 2–4: properties panel, anchor picker, orientation, resize (works in both modes)
    if (s_editSelected >= 0 && s_editSelected < kEditElemCount) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];

        if (kShowDsEditPropsPanel) {
        // Priority 2: DS-space properties panel
        {
            const int builtinRows = CountBuiltinRows(d);
            const int totalRows = builtinRows + d.propCount;
            if (totalRows > 0) {
                QRectF panelRect = ComputePropsPanelRect(s_editRects[s_editSelected], totalRows);
                if (panelRect.contains(ds)) {
                    int visCount = std::min(totalRows, kPropMaxVisible);
                    int scrollOfs = std::min(s_editPropScroll, std::max(0, totalRows - kPropMaxVisible));
                    float panelInnerY = static_cast<float>(panelRect.top()) + 2.0f;
                    float rowX = static_cast<float>(panelRect.left()) + 2.0f;
                    for (int vi = 0; vi < visCount; ++vi) {
                        int rowIdx = vi + scrollOfs;
                        if (rowIdx >= totalRows) break;
                        float rowY = panelInnerY + vi * kPropRowH;
                        float ctrlX = rowX + kPropLabelW;
                        float ctrlW = kPropCtrlW;
                        QRectF rowRect(ctrlX, rowY, ctrlW, kPropRowH);
                        if (!rowRect.contains(ds)) continue;

                        int builtinIdx = rowIdx;
                        bool isShowRow = false, isColorRow = false, isAnchorRow = false;
                        if (d.showKey && builtinIdx == 0) {
                            isShowRow = true;
                        } else {
                            if (d.showKey) --builtinIdx;
                            if (d.colorRKey && builtinIdx == 0) {
                                isColorRow = true;
                            } else {
                                if (d.colorRKey) --builtinIdx;
                                if (builtinIdx == 0 && rowIdx < builtinRows) {
                                    isAnchorRow = true;
                                }
                            }
                        }

                        if (isShowRow) {
                            cfg.SetBool(d.showKey, !cfg.GetBool(d.showKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        if (isColorRow) {
                            if (s_colorDialogOpen) return;
                            QColor cur(cfg.GetInt(d.colorRKey), cfg.GetInt(d.colorGKey), cfg.GetInt(d.colorBKey));
                            s_colorDialogOpen = true;
                            QColor picked = QColorDialog::getColor(cur, nullptr, QStringLiteral("Pick Color"));
                            s_colorDialogOpen = false;
                            if (picked.isValid()) {
                                cfg.SetInt(d.colorRKey, picked.red());
                                cfg.SetInt(d.colorGKey, picked.green());
                                cfg.SetInt(d.colorBKey, picked.blue());
                                CustomHud_InvalidateConfigCache();
                            }
                            return;
                        }
                        if (isAnchorRow) {
                            float halfW = ctrlW / 2.0f;
                            QRectF btnR(ctrlX + halfW, rowRect.top(), halfW, kPropRowH);
                            if (btnR.contains(ds)) {
                                s_anchorPickerOpen = !s_anchorPickerOpen;
                            }
                            return;
                        }

                        int propIdx = rowIdx - builtinRows;
                        if (propIdx < 0 || propIdx >= d.propCount) continue;
                        const HudEditPropDesc& pr = d.props[propIdx];

                        switch (pr.type) {
                        case EditPropType::Bool:
                            cfg.SetBool(pr.cfgKey, !cfg.GetBool(pr.cfgKey));
                            CustomHud_InvalidateConfigCache();
                            return;
                        case EditPropType::Enum:
                        case EditPropType::Int: {
                            float arrowW = 8.0f;
                            int step = pr.step > 0 ? pr.step : 1;
                            int val = cfg.GetInt(pr.cfgKey);
                            if (ds.x() < ctrlX + arrowW) val = std::max(pr.minVal, val - step);
                            else if (ds.x() > ctrlX + ctrlW - arrowW) val = std::min(pr.maxVal, val + step);
                            cfg.SetInt(pr.cfgKey, val);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::Float: {
                            float arrowW = 8.0f;
                            double step = (pr.step > 0 ? pr.step : 5) / 100.0;
                            double val = cfg.GetDouble(pr.cfgKey);
                            double minV = pr.minVal / 100.0, maxV = pr.maxVal / 100.0;
                            if (ds.x() < ctrlX + arrowW) val = std::max(minV, val - step);
                            else if (ds.x() > ctrlX + ctrlW - arrowW) val = std::min(maxV, val + step);
                            cfg.SetDouble(pr.cfgKey, val);
                            CustomHud_InvalidateConfigCache();
                            return;
                        }
                        case EditPropType::String: {
                            if (s_colorDialogOpen) return;
                            s_colorDialogOpen = true;
                            QString cur = QString::fromStdString(cfg.GetString(pr.cfgKey));
                            bool ok = false;
                            QString txt = QInputDialog::getText(nullptr,
                                QStringLiteral("Edit ") + QString::fromUtf8(pr.label),
                                QString::fromUtf8(pr.label), QLineEdit::Normal, cur, &ok);
                            s_colorDialogOpen = false;
                            if (ok) {
                                cfg.SetString(pr.cfgKey, txt.toStdString());
                                CustomHud_InvalidateConfigCache();
                            }
                            return;
                        }
                        case EditPropType::SubColor: {
                            float ovrW = 14.0f;
                            if (ds.x() < ctrlX + ovrW) {
                                cfg.SetBool(pr.cfgKey, !cfg.GetBool(pr.cfgKey));
                                CustomHud_InvalidateConfigCache();
                            } else if (!s_colorDialogOpen) {
                                QColor cur(cfg.GetInt(pr.extra1), cfg.GetInt(pr.extra2), cfg.GetInt(pr.extra3));
                                s_colorDialogOpen = true;
                                QColor picked = QColorDialog::getColor(cur, nullptr,
                                    QStringLiteral("Pick ") + QString::fromUtf8(pr.label) + QStringLiteral(" Color"));
                                s_colorDialogOpen = false;
                                if (picked.isValid()) {
                                    cfg.SetInt(pr.extra1, picked.red());
                                    cfg.SetInt(pr.extra2, picked.green());
                                    cfg.SetInt(pr.extra3, picked.blue());
                                    cfg.SetBool(pr.cfgKey, false);
                                    CustomHud_InvalidateConfigCache();
                                }
                            }
                            return;
                        }
                        case EditPropType::Color: break;
                        }
                    }
                    // Anchor grid click
                    if (s_anchorPickerOpen) {
                        float gridLeft = static_cast<float>(panelRect.left()) + 2.0f;
                        float gridTop  = static_cast<float>(panelRect.top()) + 2.0f +
                                         visCount * kPropRowH + 2.0f;
                        float cellW = (kPropPanelW - 4.0f) / 3.0f;
                        float cellH = kAnchorGridCellH;
                        for (int a = 0; a < 9; ++a) {
                            int col = a % 3, row = a / 3;
                            QRectF cell(gridLeft + col * cellW, gridTop + row * cellH, cellW, cellH);
                            if (cell.contains(ds)) {
                                const int oldAnchor = cfg.GetInt(d.anchorKey);
                                if (a != oldAnchor) {
                                    const int oldOfsX = cfg.GetInt(d.ofsXKey);
                                    const int oldOfsY = cfg.GetInt(d.ofsYKey);
                                    int finalX, finalY, newBaseX, newBaseY;
                                    ApplyAnchor(oldAnchor, oldOfsX, oldOfsY, finalX, finalY, s_editTopStretchX);
                                    ApplyAnchor(a, 0, 0, newBaseX, newBaseY, s_editTopStretchX);
                                    cfg.SetInt(d.anchorKey, a);
                                    cfg.SetInt(d.ofsXKey, finalX - newBaseX);
                                    cfg.SetInt(d.ofsYKey, finalY - newBaseY);
                                    CustomHud_InvalidateConfigCache();
                                }
                                s_anchorPickerOpen = false;
                                return;
                            }
                        }
                    }
                    return; // absorbed by panel
                }
            }
        }

        }

        // Priority 3: Orientation toggle (normal mode only — in preview mode it's hard to see)
        if (!s_editPreviewMode && d.orientKey != nullptr && GetOrientToggleRect(s_editSelected).contains(ds)) {
            cfg.SetInt(d.orientKey, cfg.GetInt(d.orientKey) == 0 ? 1 : 0);
            CustomHud_InvalidateConfigCache();
            s_editRects[s_editSelected] = ComputeEditBounds(s_editSelected, cfg, s_editTopStretchX);
            return;
        }

        // Priority 4: Resize handles (normal mode only)
        if (!s_editPreviewMode && d.lengthKey != nullptr) {
            QRectF lenH, widH;
            GetResizeHandles(s_editSelected, cfg, lenH, widH);
            if (lenH.contains(ds)) {
                s_resizingLength = true;
                s_resizeStartVal = cfg.GetInt(d.lengthKey);
                s_resizeStartDS  = ds;
                return;
            }
            if (widH.contains(ds)) {
                s_resizingWidth  = true;
                s_resizeStartVal = cfg.GetInt(d.widthKey);
                s_resizeStartDS  = ds;
                return;
            }
        }
    }

    // Priority 5: Element drag (left-click on element — works in both modes)
    if (hitElem >= 0) {
        const int i = hitElem;
        const HudEditElemDesc& di = kEditElems[i];

        // Auto-switch gauge PosMode from text-relative (0) to independent (1)
        if (di.posModeKey != nullptr && cfg.GetInt(di.posModeKey) == 0) {
            if (UNLIKELY(!s_cache.valid) || s_cache.lastHudScale != s_editHudScale) {
                RefreshCachedConfig(cfg, s_editTopStretchX, s_editHudScale);
                s_cache.valid = true;
            } else if (s_cache.lastStretchX != s_editTopStretchX) {
                RecomputeAnchorPositions(s_editTopStretchX);
            }
            int visualX = 0, visualY = 0;
            if (i == 1) { visualX = s_cache.hp.hpGaugePosX;      visualY = s_cache.hp.hpGaugePosY; }
            if (i == 4) { visualX = s_cache.weapon.ammoGaugePosX; visualY = s_cache.weapon.ammoGaugePosY; }
            cfg.SetInt(di.posModeKey, 1);
            cfg.SetInt(di.anchorKey, 0);
            cfg.SetInt(di.ofsXKey, visualX);
            cfg.SetInt(di.ofsYKey, visualY);
            CustomHud_InvalidateConfigCache();
        }

        s_editSelected = i;
        s_editPropScroll = 0;
        s_anchorPickerOpen = false;
        if (di.ofsXKey) { // crosshair (idx==13) has no position keys — select only, no drag
            s_dragging      = true;
            s_dragStartDS   = ds;
            s_dragStartOfsX = cfg.GetInt(di.ofsXKey);
            s_dragStartOfsY = cfg.GetInt(di.ofsYKey);
        }
        s_editHovered = i;
        NotifySelectionChanged(i);
        return;
    }
    s_editSelected = -1;  // deselect
    s_anchorPickerOpen = false;
    NotifySelectionChanged(-1);
}

void CustomHud_EditMouseMove(QPointF pt, Config::Table& cfg)
{
    const QPointF ds = WidgetToDS(pt);

    if (s_textScaleDragging) {
        cfg.SetInt("Metroid.Visual.HudTextScale", TsValueFromX(static_cast<float>(ds.x())));
        CustomHud_InvalidateConfigCache();
        return;
    }

    if (s_autoScaleCapDragging) {
        cfg.SetInt("Metroid.Visual.HudAutoScaleCap", AscValueFromX(static_cast<float>(ds.x())));
        CustomHud_InvalidateConfigCache();
        return;
    }

    if (s_dragging && s_editSelected >= 0) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];
        const int newOfsX = static_cast<int>(std::round(s_dragStartOfsX + ds.x() - s_dragStartDS.x()));
        const int newOfsY = static_cast<int>(std::round(s_dragStartOfsY + ds.y() - s_dragStartDS.y()));
        cfg.SetInt(d.ofsXKey, std::max(-512, std::min(512, newOfsX)));
        cfg.SetInt(d.ofsYKey, std::max(-512, std::min(512, newOfsY)));
        CustomHud_InvalidateConfigCache();
        return;
    }

    if ((s_resizingLength || s_resizingWidth) && s_editSelected >= 0) {
        const HudEditElemDesc& d = kEditElems[s_editSelected];
        const int ori = d.orientKey ? cfg.GetInt(d.orientKey) : 0;
        const bool squareResize = d.lengthKey && d.widthKey
                                  && strcmp(d.lengthKey, d.widthKey) == 0;
        // Gauge configs are in actual pixels; radar (square) stays DS-space
        const double hs = squareResize ? 1.0 : static_cast<double>(s_editHudScale);
        const int maxLen = squareResize ? 192 : 512;
        const int minLen = squareResize ? 16  : 4;
        const int maxWid = squareResize ? 192 : 128;
        const int minWid = squareResize ? 16  : 1;

        if (s_resizingLength) {
            const double delta = ((ori == 1) ? ds.y() - s_resizeStartDS.y()
                                             : ds.x() - s_resizeStartDS.x()) * hs;
            const int newVal = std::max(minLen, std::min(maxLen,
                static_cast<int>(std::round(s_resizeStartVal + delta))));
            cfg.SetInt(d.lengthKey, newVal);
            if (squareResize) cfg.SetInt(d.widthKey, newVal);
            CustomHud_InvalidateConfigCache();
        } else {
            const double delta = ((ori == 1) ? ds.x() - s_resizeStartDS.x()
                                             : ds.y() - s_resizeStartDS.y()) * hs;
            const int newVal = std::max(minWid, std::min(maxWid,
                static_cast<int>(std::round(s_resizeStartVal + delta))));
            cfg.SetInt(d.widthKey, newVal);
            if (squareResize) cfg.SetInt(d.lengthKey, newVal);
            CustomHud_InvalidateConfigCache();
        }
        return;
    }

    s_editHovered = HitTestEditElement(ds);
}

void CustomHud_EditMouseRelease(QPointF pt, Qt::MouseButton btn, Config::Table& cfg)
{
    Q_UNUSED(pt);
    Q_UNUSED(btn);
    Q_UNUSED(cfg);
    s_dragging          = false;
    s_resizingLength    = false;
    s_resizingWidth     = false;
    s_textScaleDragging = false;
    s_autoScaleCapDragging = false;
}

void CustomHud_EditMouseWheel(QPointF pt, int delta, Config::Table& cfg)
{
    if (!s_editMode) return;
    QPointF ds = WidgetToDS(pt);

    // Text Scale slider wheel
    if (kEditTextScaleRect.contains(ds)) {
        int val = cfg.GetInt("Metroid.Visual.HudTextScale");
        val = std::max(kTsMin, std::min(kTsMax, val + (delta > 0 ? 5 : -5)));
        cfg.SetInt("Metroid.Visual.HudTextScale", val);
        CustomHud_InvalidateConfigCache();
        return;
    }

    // Auto-Scale Cap slider wheel
    if (kEditAutoScaleCapRect.contains(ds)) {
        int val = cfg.GetInt("Metroid.Visual.HudAutoScaleCap");
        val = std::clamp(val + (delta > 0 ? 25 : -25), kAscMin, kAscMax);
        cfg.SetInt("Metroid.Visual.HudAutoScaleCap", val);
        CustomHud_InvalidateConfigCache();
        return;
    }

    // Crosshair panel scroll
    if (s_crosshairPanelOpen) {
        const int totalRows = CountCrosshairRows();
        if (totalRows > kCrosshairMaxVisible) {
            const int visCount = std::min(totalRows, kCrosshairMaxVisible);
            const float panelH = visCount * kPropRowH + 4.0f;
            const QRectF panelRect(kCrosshairPanelX, kCrosshairPanelY, kPropPanelW, panelH);
            if (panelRect.contains(ds)) {
                int maxScroll = totalRows - kCrosshairMaxVisible;
                if (delta > 0) s_crosshairPanelScroll = std::max(0, s_crosshairPanelScroll - 1);
                else if (delta < 0) s_crosshairPanelScroll = std::min(maxScroll, s_crosshairPanelScroll + 1);
                return;
            }
        }
    }

    if (!kShowDsEditPropsPanel) return;

    // Element props panel scroll
    if (s_editSelected < 0) return;
    const HudEditElemDesc& d = kEditElems[s_editSelected];
    const int totalRows = CountBuiltinRows(d) + d.propCount;
    if (totalRows <= kPropMaxVisible) return;

    QRectF panelRect = ComputePropsPanelRect(s_editRects[s_editSelected], totalRows);
    if (!panelRect.contains(ds)) return;

    int maxScroll = totalRows - kPropMaxVisible;
    if (delta > 0) s_editPropScroll = std::max(0, s_editPropScroll - 1);
    else if (delta < 0) s_editPropScroll = std::min(maxScroll, s_editPropScroll + 1);
}
