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
#include <QtGlobal>

#include "types.h"
#include "Platform.h"
#include "Config.h"
#include "GPU.h"
#include "main.h"

#include "VideoSettingsDialog.h"
#include "ui_VideoSettingsDialog.h"

#ifdef MELONPRIME_DS
#include "MelonPrimeVideoBackend.h"
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

    bool softwareRenderer = renderer == renderer3D_Software;
    ui->cbGLDisplay->setEnabled(softwareRenderer);
    ui->cbSoftwareThreaded->setEnabled(softwareRenderer);
    ui->cbxGLResolution->setEnabled(!softwareRenderer);
    ui->cbBetterPolygons->setEnabled(renderer == renderer3D_OpenGL);
    ui->cbxComputeHiResCoords->setEnabled(renderer == renderer3D_OpenGLCompute);
}

VideoSettingsDialog::VideoSettingsDialog(QWidget* parent) : QDialog(parent), ui(new Ui::VideoSettingsDialog)
{
    ui->setupUi(this);
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

    ui->cbGLDisplay->setChecked(oldGLDisplay != 0);

    ui->cbVSync->setChecked(oldVSync != 0);
    ui->sbVSyncInterval->setValue(oldVSyncInterval);

    ui->cbSoftwareThreaded->setChecked(oldSoftThreaded);

    for (int i = 1; i <= 16; i++)
        ui->cbxGLResolution->addItem(QString("%1x native (%2x%3)").arg(i).arg(256*i).arg(192*i));
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
