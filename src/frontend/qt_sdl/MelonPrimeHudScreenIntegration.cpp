/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

// Cold/event-side Custom HUD integration for ScreenPanel.
//
// These hooks used to be textual .inc fragments embedded at call sites in
// Screen.cpp. They are real member functions so their inputs, early-return
// contract and ownership are visible to the compiler and reviewers. No
// allocation, virtual dispatch or context copy was added to the move/wheel
// event paths; the edit-mode branch remains explicitly unlikely.

#include "Screen.h"

#ifdef MELONPRIME_CUSTOM_HUD

#include <algorithm>
#include <cmath>

#include "Config.h"
#include "EmuInstance.h"
#include "EmuThread.h"
#include "InputConfig/InputConfigDialog.h"
#include "MelonPrime.h"
#include "MelonPrimeHudConfigState.h"
#include "MelonPrimeHudEdit.h"
#include "MelonPrimeHudScreenEditPanel.h"

void ScreenPanel::initializeHudScreenIntegration()
{
    Overlay[0] = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);
    Overlay[1] = QImage(256, 192, QImage::Format_ARGB32_Premultiplied);
    Overlay[0].fill(0x00000000);
    Overlay[1].fill(0x00000000);

    // Font resolution is constructor-cold. Epoch refresh in the renderer
    // picks up later settings changes without a per-frame config scan.
    auto& hudCfg = emuInstance->getLocalConfig();
    overlayFont = MelonPrime::CustomHud_ResolveBaseFont(hudCfg);
    overlayFont.setPixelSize(
        MelonPrime::CustomHud_ResolveFontPixelSize(hudCfg));

    auto* const thread = emuInstance->getEmuThread();
    auto* const core = thread ? thread->GetMelonPrimeCore() : nullptr;
    if (!core)
        return;

    m_hudEditPanel = new MelonPrimeHudConfigOnScreenEdit(
        this, emuInstance, core->HudConfigState());
    MelonPrime::CustomHud_SetEditSelectionCallback(
        core->HudConfigState(), [this](int index) {
            auto* selectedCore = melonPrimeCore();
            if (!selectedCore
                || !MelonPrime::CustomHud_IsEditMode(
                    selectedCore->HudConfigState())
                || index < 0
                || MelonPrime::CustomHud_IsCrosshairElement(index)) {
                if (m_hudEditPanel)
                    m_hudEditPanel->clear();
                return;
            }

            if (MelonPrime::CustomHud_GetOnScreenEditStyle(
                    selectedCore->HudConfigState())
                == MelonPrime::OnScreenEditStyle::Classic) {
                m_hudEditPanel->populateForElement(index);
                MelonPrime::HudScreenEditPanel::Position(
                    this, m_hudEditPanel, true, true);
                m_hudEditPanel->show();
                m_hudEditPanel->raise();
                return;
            }

            m_hudEditPanel->clear();
        });
}

void ScreenPanel::updateHudScreenLayoutCache()
{
    // This layout boundary is the sole recalculation authority. Paint and
    // high-rate edit input consume only these cached scalars/matrix values.
    m_hudScale = 1.0f;
    m_topStretchX = 1.0f;
    m_hudOriginX = 0.0f;
    m_hudOriginY = 0.0f;
    m_hudTopMatrixValid = false;
    for (int i = 0; i < numScreens; ++i) {
        if (screenKind[i] != 0)
            continue;
        const float* const matrix = screenMatrix[i];
        const float scaleX = std::sqrt(
            matrix[0] * matrix[0] + matrix[1] * matrix[1]);
        const float scaleY = std::sqrt(
            matrix[2] * matrix[2] + matrix[3] * matrix[3]);
        m_hudOriginX = matrix[4];
        m_hudOriginY = matrix[5];
        if (scaleX > 0.0f && scaleY > 0.0f) {
            m_hudScale = scaleY;
            m_topStretchX = scaleX / scaleY;
        }
        std::copy(matrix, matrix + 6, m_hudTopMatrix);
        m_hudTopMatrixValid = true;
        break;
    }
}

void ScreenPanel::handleHudMouseWheel(QWheelEvent* event)
{
    auto* const core = melonPrimeCore();
    if (!core || !MelonPrime::CustomHud_IsEditMode(core->HudConfigState()))
        return;
    auto& config = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_EditMouseWheel(
        core->HudConfigState(), event->position(),
        event->angleDelta().y(), config);
}

bool ScreenPanel::handleHudMousePress(QMouseEvent* event)
{
    auto* const core = melonPrimeCore();
    if (LIKELY(!core
        || !MelonPrime::CustomHud_IsEditMode(core->HudConfigState())))
        return false;

    auto& config = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_UpdateEditContext(
        core->HudConfigState(), m_hudOriginX, m_hudOriginY,
        m_hudScale, m_topStretchX);
    MelonPrime::CustomHud_EditMousePress(
        core->HudConfigState(), event->pos(), event->button(), config);
    if (!MelonPrime::CustomHud_IsEditMode(core->HudConfigState())
        && InputConfigDialog::currentDlg)
        InputConfigDialog::currentDlg->refreshAfterHudEditSave();
    return true;
}

bool ScreenPanel::handleHudMouseRelease(QMouseEvent* event)
{
    auto* const core = melonPrimeCore();
    if (LIKELY(!core
        || !MelonPrime::CustomHud_IsEditMode(core->HudConfigState())))
        return false;

    auto& config = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_EditMouseRelease(
        core->HudConfigState(), event->pos(), event->button(), config);
    return true;
}

bool ScreenPanel::handleHudMouseMove(QMouseEvent* event)
{
    auto* const core = melonPrimeCore();
    if (LIKELY(!core
        || !MelonPrime::CustomHud_IsEditMode(core->HudConfigState())))
        return false;

    auto& config = emuInstance->getLocalConfig();
    MelonPrime::CustomHud_UpdateEditContext(
        core->HudConfigState(), m_hudOriginX, m_hudOriginY,
        m_hudScale, m_topStretchX);
    MelonPrime::CustomHud_EditMouseMove(
        core->HudConfigState(), event->pos(), config);
    return true;
}

void ScreenPanel::repositionHudEditPanel(bool resizeEvent)
{
    if (m_hudEditPanel && m_hudEditPanel->isVisible()) {
        MelonPrime::HudScreenEditPanel::Position(
            this, m_hudEditPanel, resizeEvent, false);
    }
}

#endif // MELONPRIME_CUSTOM_HUD
