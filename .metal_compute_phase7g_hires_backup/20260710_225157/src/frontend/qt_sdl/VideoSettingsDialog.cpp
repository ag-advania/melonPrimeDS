/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <QFileDialog>
#include <QRadioButton>
#include <QtGlobal>

#include "types.h"
#include "Platform.h"
#include "Config.h"
#include "GPU.h"
#include "main.h"
#include "EmuInstance.h"
#include "EmuThread.h"

#include "VideoSettingsDialog.h"
#include "ui_VideoSettingsDialog.h"

#ifdef MELONPRIME_DS
#include "MelonPrimeVideoBackend.h"
#include "MelonPrimeLocalization.h"
#if defined(MELONPRIME_ENABLE_METAL)
#include "MelonPrimeMetalFeatureCheck.h"
#endif
#endif // MELONPRIME_DS


inline bool VideoSettingsDialog::UsesGL()
{
    auto& cfg = emuInstance->getGlobalConfig();
#ifdef MELONPRIME_DS
    // Metal-plan Phase 8/9 prep: "does the current selection need a GL
    // context" is no longer the same question as "is the renderer
    // Software" once a non-GL, non-Software backend (Metal) exists. A
    // plain `renderer != Software` check would treat Metal as needing GL,
    // which would wrongly enable this dialog's VSync-via-GL controls and
    // request a GL context reinit on a config value this dialog has no UI
    // for yet (see the button-group null-check below).
    return MelonPrime::VideoBackend::IsOpenGLPresentation(
        MelonPrime::VideoBackend::ResolvePresentationBackend(
            cfg.GetBool("Screen.UseGL"), cfg.GetInt("3D.Renderer")));
#else
    return cfg.GetBool("Screen.UseGL") || (cfg.GetInt("3D.Renderer") != renderer3D_Software);
#endif // MELONPRIME_DS
}

VideoSettingsDialog* VideoSettingsDialog::currentDlg = nullptr;

void VideoSettingsDialog::setEnabled()
{
    auto& cfg = emuInstance->getGlobalConfig();
    int renderer = cfg.GetInt("3D.Renderer");

    const bool softwareRenderer = renderer == renderer3D_Software;
    const bool openGLRenderer = renderer == renderer3D_OpenGL;
    const bool computeRenderer = renderer == renderer3D_OpenGLCompute;
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    const bool metalRasterRenderer = renderer == renderer3D_Metal;
    const bool metalComputeRenderer = renderer == renderer3D_MetalCompute;
    const bool metalRenderer = metalRasterRenderer || metalComputeRenderer;
#else
    const bool metalRasterRenderer = false;
    const bool metalComputeRenderer = false;
    const bool metalRenderer = false;
#endif
    ui->cbGLDisplay->setEnabled(softwareRenderer);
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // Metal-plan Phase 9 (tester UI exposure): MetalRenderer3D's actual
    // visible 3D output is still produced by its internal SoftRenderer3D
    // delegate (see GPU3D_Metal.mm / GPU_Metal.mm and
    // .claude/rules/melonprime-metal-backend-plan.md Phase 8), so
    // "Software.Threaded" genuinely affects Metal's performance today.
    // BetterPolygons/HiresCoordinates are not consumed by the Metal path at
    // all yet, so they stay OpenGL/Compute-only below rather than being
    // exposed as if they already work for Metal.
    ui->cbSoftwareThreaded->setEnabled(softwareRenderer || metalRenderer);
#else
    ui->cbSoftwareThreaded->setEnabled(softwareRenderer);
#endif
    ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer || metalRenderer);
    ui->cbBetterPolygons->setEnabled(openGLRenderer);
    ui->cbxComputeHiResCoords->setEnabled(computeRenderer || metalComputeRenderer);
}

VideoSettingsDialog::VideoSettingsDialog(QWidget* parent) : QDialog(parent), ui(new Ui::VideoSettingsDialog)
{
    ui->setupUi(this);
    // MELONPRIME_METAL_COMPUTE_UI_LAYOUT_V2: rows 4/5 are reserved for native Metal renderers.
    setAttribute(Qt::WA_DeleteOnClose);

    emuInstance = ((MainWindow*)parent)->getEmuInstance();

    auto& cfg = emuInstance->getGlobalConfig();
    oldRenderer = cfg.GetInt("3D.Renderer");
    oldGLDisplay = cfg.GetBool("Screen.UseGL");
    oldVSync = cfg.GetBool("Screen.VSync");
    oldVSyncInterval = cfg.GetInt("Screen.VSyncInterval");
    oldSoftThreaded = cfg.GetBool("3D.Soft.Threaded");
    oldGLScale = cfg.GetInt("3D.GL.ScaleFactor");
    oldGLBetterPolygons = cfg.GetBool("3D.GL.BetterPolygons");
    oldHiresCoordinates = cfg.GetBool("3D.GL.HiresCoordinates");

    grp3DRenderer = new QButtonGroup(this);
    grp3DRenderer->addButton(ui->rb3DSoftware, renderer3D_Software);
    grp3DRenderer->addButton(ui->rb3DOpenGL,   renderer3D_OpenGL);
    grp3DRenderer->addButton(ui->rb3DCompute,  renderer3D_OpenGLCompute);
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // MELONPRIME_METAL_DYNAMIC_LAYOUT_V3
    // Keep the .ui file platform-neutral. Only a MelonPrime macOS Metal build
    // moves the existing controls and inserts the two native Metal choices.
    ui->gridLayout_2->removeItem(ui->verticalSpacer);
    ui->gridLayout_2->removeWidget(ui->cbGLDisplay);
    ui->gridLayout_2->removeWidget(ui->cbVSync);
    ui->gridLayout_2->removeWidget(ui->label_2);
    ui->gridLayout_2->removeWidget(ui->sbVSyncInterval);

    rb3DMetal = new QRadioButton(ui->groupBox);
    rb3DMetal->setObjectName(QStringLiteral("rb3DMetal"));
    rb3DMetal->setText(MelonPrime::UiText::Tr("Metal"));
    rb3DMetal->setWhatsThis(MelonPrime::UiText::Tr(
        "<html><head/><body><p>Native Metal raster renderer for macOS.</p></body></html>"));
    ui->gridLayout_2->addWidget(rb3DMetal, 4, 0, 1, 2);
    grp3DRenderer->addButton(rb3DMetal, renderer3D_Metal);

    rb3DMetalCompute = new QRadioButton(ui->groupBox);
    rb3DMetalCompute->setObjectName(QStringLiteral("rb3DMetalCompute"));
    rb3DMetalCompute->setText(MelonPrime::UiText::Tr("Metal Compute Shader"));
    rb3DMetalCompute->setWhatsThis(MelonPrime::UiText::Tr(
        "<html><head/><body><p>Experimental native Metal compute-shader renderer. The validated Metal raster renderer remains the visible fallback until compute rendering reaches full parity.</p></body></html>"));
    ui->gridLayout_2->addWidget(rb3DMetalCompute, 5, 0, 1, 2);
    grp3DRenderer->addButton(rb3DMetalCompute, renderer3D_MetalCompute);

    ui->gridLayout_2->addItem(ui->verticalSpacer, 6, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->cbGLDisplay, 7, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->cbVSync, 8, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->label_2, 9, 0, 1, 1);
    ui->gridLayout_2->addWidget(ui->sbVSyncInterval, 9, 1, 1, 1);
    ui->gridLayout_2->invalidate();
    ui->gridLayout_2->activate();
    adjustSize();
#endif
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    connect(grp3DRenderer, SIGNAL(buttonClicked(int)), this, SLOT(onChange3DRenderer(int)));
#else
    connect(grp3DRenderer, SIGNAL(idClicked(int)), this, SLOT(onChange3DRenderer(int)));
#endif
#ifdef MELONPRIME_DS
    // Metal-plan Phase 8/9 prep: `oldRenderer` can hold a value this dialog
    // has no radio button for -- either `renderer3D_Metal` itself (no
    // `rb3DMetal` here; Metal exposure is MelonPrime's own settings dialog,
    // Phase 9), or any other stray/out-of-range int left over from a
    // rebuild with a different renderer set compiled in. QButtonGroup::
    // button() returns nullptr for an unregistered id; calling
    // setChecked() on that would crash. Leave nothing checked in that case
    // rather than guessing -- `oldRenderer` itself is left untouched so
    // Cancel still restores the original value exactly.
    if (QAbstractButton* rendererButton = grp3DRenderer->button(oldRenderer))
        rendererButton->setChecked(true);
#else
    grp3DRenderer->button(oldRenderer)->setChecked(true);
#endif // MELONPRIME_DS

#ifndef OGLRENDERER_ENABLED
    ui->rb3DOpenGL->setEnabled(false);
#endif

#ifdef __APPLE__
    ui->rb3DCompute->setEnabled(false);
#endif

#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // Metal-plan Phase 9 (tester UI exposure): visible only on Metal-capable
    // builds; enabled only once the runtime feature probe
    // (MelonPrimeMetalFeatureCheck.h) confirms this Mac/GPU/driver can
    // actually construct a Metal device + pipeline. High2 is left
    // untouched -- it stays OpenGL Compute only, see
    // melonprime_macos_compute_renderer_restriction.md. Label/tooltip must
    // stay honest about "experimental"; see
    // .claude/rules/plan/metal_tester_ui_perf_audit_and_execution_instructions_v2.md.
    const bool metalSupported = MelonPrime::Metal::SupportsRequiredBaseline();
    const QString metalTooltip = metalSupported
        ? QStringLiteral("Experimental native Metal renderer for testing. Not the same as High2 / OpenGL Compute. Visual parity and performance are not final.")
        : QString::fromStdString(MelonPrime::Metal::CachedFeatureInfo().unavailableReason);
    rb3DMetal->setEnabled(metalSupported);
    rb3DMetal->setToolTip(MelonPrime::UiText::Tr(metalTooltip));
    rb3DMetalCompute->setEnabled(metalSupported);
    rb3DMetalCompute->setToolTip(MelonPrime::UiText::Tr(
        metalSupported
            ? QStringLiteral("Experimental Metal compute-shader renderer. Compute stages run natively while the validated Metal raster output remains the safe visible source.")
            : metalTooltip));
#endif

    ui->cbGLDisplay->setChecked(oldGLDisplay != 0);

    ui->cbVSync->setChecked(oldVSync != 0);
    ui->sbVSyncInterval->setValue(oldVSyncInterval);

    ui->cbSoftwareThreaded->setChecked(oldSoftThreaded);

    for (int i = 1; i <= 16; i++)
        ui->cbxGLResolution->addItem(QString("%1x native (%2x%3)").arg(i).arg(256*i).arg(192*i));
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // Tester-phase shortcut: Metal intentionally reuses 3D.GL.ScaleFactor
    // as the shared hardware-renderer internal scale. Rename/split only after
    // Metal becomes stable enough to need separate defaults/migration.
    ui->cbxGLResolution->setToolTip(MelonPrime::UiText::Tr(
        "Internal 3D render scale. Used by OpenGL/OpenGL Compute and experimental Metal. Metal scale support is still under test."));
#endif
    ui->cbxGLResolution->setCurrentIndex(oldGLScale-1);

    ui->cbBetterPolygons->setChecked(oldGLBetterPolygons != 0);
    ui->cbxComputeHiResCoords->setChecked(oldHiresCoordinates != 0);

    if (!oldVSync)
        ui->sbVSyncInterval->setEnabled(false);
    setVsyncControlEnable(UsesGL());

    setEnabled();
}

VideoSettingsDialog::~VideoSettingsDialog()
{
    delete ui;
}

void VideoSettingsDialog::on_VideoSettingsDialog_accepted()
{
    Config::Save();

    closeDlg();
}

void VideoSettingsDialog::on_VideoSettingsDialog_rejected()
{
    if (!((MainWindow*)parent())->getEmuInstance())
    {
        closeDlg();
        return;
    }

    bool old_gl = UsesGL();

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.Renderer", oldRenderer);
    cfg.SetBool("Screen.UseGL", oldGLDisplay);
    cfg.SetBool("Screen.VSync", oldVSync);
    cfg.SetInt("Screen.VSyncInterval", oldVSyncInterval);
    cfg.SetBool("3D.Soft.Threaded", oldSoftThreaded);
    cfg.SetInt("3D.GL.ScaleFactor", oldGLScale);
    cfg.SetBool("3D.GL.BetterPolygons", oldGLBetterPolygons);
    cfg.SetBool("3D.GL.HiresCoordinates", oldHiresCoordinates);

    emit updateVideoSettings(old_gl != UsesGL());

    closeDlg();
}

void VideoSettingsDialog::setVsyncControlEnable(bool hasOGL)
{
    ui->cbVSync->setEnabled(hasOGL);
    ui->sbVSyncInterval->setEnabled(hasOGL);
}

void VideoSettingsDialog::onChange3DRenderer(int renderer)
{
    bool old_gl = UsesGL();

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.Renderer", renderer);

    setEnabled();

    emit updateVideoSettings(old_gl != UsesGL());
}

void VideoSettingsDialog::on_cbGLDisplay_stateChanged(int state)
{
    bool old_gl = UsesGL();

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.UseGL", (state != 0));

    setVsyncControlEnable(UsesGL());

    emit updateVideoSettings(old_gl != UsesGL());
}

void VideoSettingsDialog::on_cbVSync_stateChanged(int state)
{
    bool vsync = (state != 0);
    ui->sbVSyncInterval->setEnabled(vsync);

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.VSync", vsync);

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_sbVSyncInterval_valueChanged(int val)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("Screen.VSyncInterval", val);

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbSoftwareThreaded_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.Soft.Threaded", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxGLResolution_currentIndexChanged(int idx)
{
    // prevent a spurious change
    if (ui->cbxGLResolution->count() < 16) return;

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.GL.ScaleFactor", idx+1);

#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // MELONPRIME_METAL_COMPUTE_SCALE_SYNC_V1
    // Recreate the compute wrapper when its scale changes. This guarantees
    // that the raster-reference target, compute tile buffers and visible Metal
    // output texture are all created from the same requested scale.
    if (cfg.GetInt("3D.Renderer") == renderer3D_MetalCompute &&
        emuInstance && emuInstance->getEmuThread())
    {
        emuInstance->getEmuThread()->updateVideoRenderer();
    }
#endif

    setVsyncControlEnable(UsesGL());

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbBetterPolygons_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.BetterPolygons", (state != 0));

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxComputeHiResCoords_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.GL.HiresCoordinates", (state != 0));

    emit updateVideoSettings(false);
}
