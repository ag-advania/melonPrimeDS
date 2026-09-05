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
#include <QComboBox>
#include <QLabel>
#include <QRadioButton>
#include <QSignalBlocker>
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
#include "MelonPrimeDef.h"
#include "MelonPrimeVideoBackend.h"
#include "MelonPrimeLocalization.h"
#if defined(MELONPRIME_ENABLE_METAL)
#include "MelonPrimeMetalFeatureCheck.h"
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
#include "MelonPrimeVulkanFeatureCheck.h"
#endif
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
#include "MelonPrimeDX12FeatureCheck.h"
#endif
#endif // MELONPRIME_DS

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
namespace
{

QString VulkanRendererDescription()
{
    return MelonPrime::UiText::Tr(
        "Native Vulkan compute renderer. Supports internal-resolution scaling and "
        "high-resolution coordinates. Polygons are rasterized directly as spans, "
        "so improved polygon splitting is unnecessary.");
}

QString VulkanBetterPolygonsDescription()
{
    return MelonPrime::UiText::Tr(
        "Improved polygon splitting is not used by Vulkan because its compute rasterizer "
        "processes DS polygons directly without splitting them into triangles.");
}

QString HiresCoordinatesDescription()
{
    return MelonPrime::UiText::Tr(
        "Use the DS GPU's high-resolution vertex coordinates in renderers that support "
        "this mode, including Vulkan.");
}

} // namespace
#endif

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
namespace
{

QString DX12BetterPolygonsDescription()
{
    return MelonPrime::UiText::Tr(
        "Improved polygon splitting is not used by DirectX 12 because its compute rasterizer "
        "processes DS polygons directly without splitting them into triangles.");
}

QString DX12HiresCoordinatesDescription()
{
    return MelonPrime::UiText::Tr(
        "Use the DS GPU's high-resolution vertex coordinates with DirectX 12.");
}

} // namespace
#endif

#ifdef MELONPRIME_DS
namespace
{

bool RendererForcesHiresCoordinates(int renderer)
{
    if (renderer == renderer3D_OpenGLCompute)
        return true;
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    if (renderer == renderer3D_Metal || renderer == renderer3D_MetalCompute)
        return true;
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (renderer == renderer3D_Vulkan)
        return true;
#endif
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    if (renderer == renderer3D_DX12)
        return true;
#endif
    return false;
}

bool RendererForcesBetterPolygons(int renderer)
{
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    return renderer == renderer3D_Metal;
#else
    (void)renderer;
    return false;
#endif
}

} // namespace
#endif

#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
namespace
{

QString MetalBetterPolygonsDescription()
{
    return MelonPrime::UiText::Tr(
        "Metal raster always enables Improved polygon splitting for DS polygons "
        "with more than three vertices.");
}

QString MetalComputeBetterPolygonsDescription()
{
    return MelonPrime::UiText::Tr(
        "Improved polygon splitting does not affect Metal Compute's normal rendering "
        "path because it processes DS polygons directly without splitting them into "
        "triangles.");
}

QString MetalHiresCoordinatesDescription()
{
    return MelonPrime::UiText::Tr(
        "Metal and Metal Compute always use the DS GPU's high-resolution vertex "
        "coordinates above 1x internal resolution.");
}

} // namespace
#endif


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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    const bool vulkanRenderer = renderer == renderer3D_Vulkan;
#else
    const bool vulkanRenderer = false;
#endif
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    const bool dx12Renderer = renderer == renderer3D_DX12;
#else
    const bool dx12Renderer = false;
#endif
    // Native GPU backends own their presentation path. Screen.UseGL only
    // selects the display path for the software renderer, so exposing it for
    // DX12 would present a setting that cannot affect the active backend.
    ui->cbGLDisplay->setEnabled(softwareRenderer);
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // MELONPRIME_METAL_NATIVE_THREAD_SETTING_V1
    // This controls the Software renderer worker thread. Native Metal and
    // Metal Compute submit on the emulation/render thread.
    ui->cbSoftwareThreaded->setEnabled(softwareRenderer);
#else
    ui->cbSoftwareThreaded->setEnabled(softwareRenderer);
#endif
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer || metalRenderer || vulkanRenderer || dx12Renderer);

    // Do not create a Vulkan device merely to refresh help text while a
    // foreign native backend is live. The selected backend has already been
    // probed by renderer normalization; unselected backends are deliberately
    // deferred until their transition has torn down the old renderer.
    QString vulkanRendererDescription = VulkanRendererDescription();
    if (vulkanRenderer)
    {
        const auto& vulkanProbe = MelonPrime::VulkanFeatureCheck::Probe();
        if (!vulkanProbe.Available)
            vulkanRendererDescription = QString::fromStdString(vulkanProbe.Reason);
    }
    rb3DVulkan->setToolTip(vulkanRendererDescription);
    rb3DVulkan->setWhatsThis(vulkanRendererDescription);

    const QString resolutionDescription = MelonPrime::UiText::Tr(
        "The resolution at which the 3D graphics will be rendered. Higher resolutions improve graphics quality when the main window is enlarged, but may also cause glitches.");
    ui->cbxGLResolution->setToolTip(resolutionDescription);
    ui->cbxGLResolution->setWhatsThis(resolutionDescription);
#else
    ui->cbxGLResolution->setEnabled(openGLRenderer || computeRenderer || metalRenderer || dx12Renderer);
#endif

    // MELONPRIME_METAL_RENDER_OPTIONS_V1
    // BetterPolygons is a center-fan workaround for renderers that must split
    // DS polygons into GPU triangles. Metal raster always uses it, while Metal
    // Compute processes the original polygon as scanline spans and has no
    // triangle-splitting stage to improve.
#ifdef MELONPRIME_DS
    const bool forceBetterPolygons = RendererForcesBetterPolygons(renderer);
#else
    const bool forceBetterPolygons = false;
#endif
    if (forceBetterPolygons)
    {
        const QSignalBlocker blocker(ui->cbBetterPolygons);
        ui->cbBetterPolygons->setChecked(true);
        cfg.SetBool("3D.GL.BetterPolygons", true);
    }
    ui->cbBetterPolygons->setEnabled(openGLRenderer);

#ifdef MELONPRIME_DS
    constexpr const char* originalBetterPolygonsHelp =
        "MelonPrimeOriginalBetterPolygonsHelp";
    if (!ui->cbBetterPolygons->property(originalBetterPolygonsHelp).isValid())
    {
        ui->cbBetterPolygons->setProperty(
            originalBetterPolygonsHelp, ui->cbBetterPolygons->whatsThis());
    }
    QString betterPolygonsDescription =
        ui->cbBetterPolygons->property(originalBetterPolygonsHelp).toString();
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    if (metalRasterRenderer)
        betterPolygonsDescription = MetalBetterPolygonsDescription();
    if (metalComputeRenderer)
        betterPolygonsDescription = MetalComputeBetterPolygonsDescription();
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (vulkanRenderer)
        betterPolygonsDescription = VulkanBetterPolygonsDescription();
#endif
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    if (dx12Renderer)
        betterPolygonsDescription = DX12BetterPolygonsDescription();
#endif
    ui->cbBetterPolygons->setToolTip(betterPolygonsDescription);
    ui->cbBetterPolygons->setWhatsThis(betterPolygonsDescription);
#endif

    // OpenGL Compute, Metal, Vulkan and DirectX 12 always use high-resolution
    // coordinates above 1x. Reflect that renderer contract in the shared
    // control and configuration; their rendering paths also force the value
    // independently of this UI.
    const bool forceHiresCoordinates =
        computeRenderer || metalRenderer || vulkanRenderer || dx12Renderer;
    if (forceHiresCoordinates)
    {
        const QSignalBlocker blocker(ui->cbxComputeHiResCoords);
        ui->cbxComputeHiResCoords->setChecked(true);
        cfg.SetBool("3D.GL.HiresCoordinates", true);
    }
    ui->cbxComputeHiResCoords->setEnabled(
        !forceHiresCoordinates && metalRenderer);
#ifdef MELONPRIME_DS
    constexpr const char* originalHiresCoordinatesHelp =
        "MelonPrimeOriginalHiresCoordinatesHelp";
    if (!ui->cbxComputeHiResCoords->property(originalHiresCoordinatesHelp).isValid())
    {
        ui->cbxComputeHiResCoords->setProperty(
            originalHiresCoordinatesHelp, ui->cbxComputeHiResCoords->whatsThis());
    }
    QString hiresCoordinatesDescription =
        ui->cbxComputeHiResCoords->property(originalHiresCoordinatesHelp).toString();
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (vulkanRenderer)
        hiresCoordinatesDescription = HiresCoordinatesDescription();
#endif
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    if (dx12Renderer)
        hiresCoordinatesDescription = DX12HiresCoordinatesDescription();
#endif
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    if (metalRenderer)
        hiresCoordinatesDescription = MetalHiresCoordinatesDescription();
#endif
    ui->cbxComputeHiResCoords->setToolTip(hiresCoordinatesDescription);
    ui->cbxComputeHiResCoords->setWhatsThis(hiresCoordinatesDescription);
#endif

#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    bool reflexEnabled = false;
    std::string reflexUnavailableReason;
    bool antiLag2Enabled = false;
    std::string antiLag2UnavailableReason;
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    bool intelXeLLEnabled = false;
    std::string intelXeLLUnavailableReason;
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (vulkanRenderer)
    {
        const auto& vulkanProbe = MelonPrime::VulkanFeatureCheck::Probe();
        reflexEnabled = vulkanProbe.Available && vulkanProbe.NvidiaReflexAvailable;
        reflexUnavailableReason = vulkanProbe.NvidiaReflexReason;
        antiLag2Enabled = vulkanProbe.Available && vulkanProbe.AmdAntiLag2Available;
        antiLag2UnavailableReason = vulkanProbe.AmdAntiLag2Reason;
    }
#endif
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    if (dx12Renderer)
    {
        const auto& dx12Probe = MelonPrime::DX12FeatureCheck::Probe();
        reflexEnabled = dx12Probe.Available && dx12Probe.NvidiaReflexAvailable;
        reflexUnavailableReason = dx12Probe.NvidiaReflexReason;
        antiLag2Enabled = dx12Probe.Available && dx12Probe.AmdAntiLag2Available;
        antiLag2UnavailableReason = dx12Probe.AmdAntiLag2Reason;
        intelXeLLEnabled = dx12Probe.Available && dx12Probe.IntelXeLLAvailable;
        intelXeLLUnavailableReason = dx12Probe.IntelXeLLReason;
    }
#endif
    lblNvidiaReflex->setEnabled(reflexEnabled);
    cbxNvidiaReflex->setEnabled(reflexEnabled);

    const QString reflexDescription = reflexEnabled
        ? MelonPrime::UiText::Tr(
            "NVIDIA Reflex reduces CPU-to-render latency. Boost also requests maximum GPU clocks and can increase power usage.")
        : (!(dx12Renderer || vulkanRenderer)
            ? MelonPrime::UiText::Tr(
                "Available only with DirectX 12 or Vulkan on a supported NVIDIA GPU.")
            : MelonPrime::UiText::Tr(QString::fromStdString(
                reflexUnavailableReason.empty()
                    ? std::string("NVIDIA Reflex is unavailable")
                    : reflexUnavailableReason)));
    lblNvidiaReflex->setToolTip(reflexDescription);
    cbxNvidiaReflex->setToolTip(reflexDescription);

    lblAmdAntiLag2->setEnabled(antiLag2Enabled);
    cbxAmdAntiLag2->setEnabled(antiLag2Enabled);
    const QString antiLag2Description = antiLag2Enabled
        ? MelonPrime::UiText::Tr(
            "AMD Anti-Lag 2 reduces system latency for improved responsiveness.")
        : (!(dx12Renderer || vulkanRenderer)
            ? MelonPrime::UiText::Tr(
                "Available only with DirectX 12 or Vulkan on a supported AMD Radeon GPU.")
            : MelonPrime::UiText::Tr(QString::fromStdString(
                antiLag2UnavailableReason.empty()
                    ? std::string("AMD Radeon Anti-Lag 2 is unavailable")
                    : antiLag2UnavailableReason)));
    lblAmdAntiLag2->setToolTip(antiLag2Description);
    cbxAmdAntiLag2->setToolTip(antiLag2Description);

#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    lblIntelXeLL->setEnabled(intelXeLLEnabled);
    cbxIntelXeLL->setEnabled(intelXeLLEnabled);
    const QString intelXeLLDescription = intelXeLLEnabled
        ? MelonPrime::UiText::Tr(
            "Intel Xe Low Latency (XeLL) reduces render-queue and presentation latency on supported Intel Arc GPUs.")
        : (!dx12Renderer
            ? MelonPrime::UiText::Tr(
                "Available only with DirectX 12 on a supported Intel Arc GPU.")
            : MelonPrime::UiText::Tr(QString::fromStdString(
                intelXeLLUnavailableReason.empty()
                    ? std::string("Intel XeLL is unavailable")
                    : intelXeLLUnavailableReason)));
    lblIntelXeLL->setToolTip(intelXeLLDescription);
    cbxIntelXeLL->setToolTip(intelXeLLDescription);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const bool intelXeLLPacingEnabled = intelXeLLEnabled && cbxIntelXeLL->currentIndex() != 0;
    lblIntelXeLLPacingPolicy->setEnabled(intelXeLLPacingEnabled);
    cbxIntelXeLLPacingPolicy->setEnabled(intelXeLLPacingEnabled);
    const QString intelXeLLPacingDescription = MelonPrime::UiText::Tr(
        "Developer-only Intel XeLL pacing experiments. Compatibility remains the default until Intel Arc hardware validation is complete.");
    lblIntelXeLLPacingPolicy->setToolTip(intelXeLLPacingDescription);
    cbxIntelXeLLPacingPolicy->setToolTip(intelXeLLPacingDescription);
#endif
#endif
#endif
}

VideoSettingsDialog::VideoSettingsDialog(QWidget* parent) : QDialog(parent), ui(new Ui::VideoSettingsDialog)
{
    ui->setupUi(this);
#ifdef MELONPRIME_DS
    // These controls are shared by OpenGL, Metal, Vulkan, and DirectX 12.
    // Keep the upstream .ui untouched and localize the backend-neutral label
    // through MelonPrime's runtime catalog.
    ui->groupBox_3->setTitle(MelonPrime::UiText::Tr("3D renderer settings"));
#endif
    // MELONPRIME_METAL_COMPUTE_UI_LAYOUT_V2: rows 4/5 are reserved for native Metal renderers.
    setAttribute(Qt::WA_DeleteOnClose);

    emuInstance = ((MainWindow*)parent)->getEmuInstance();

    auto& cfg = emuInstance->getGlobalConfig();
    oldRenderer = cfg.GetInt("3D.Renderer");
    oldGLDisplay = cfg.GetBool("Screen.UseGL");
    oldVSync = cfg.GetBool("Screen.VSync");
    oldVSyncInterval = cfg.GetInt("Screen.VSyncInterval");
    oldSoftThreaded = cfg.GetBool("3D.Soft.Threaded");
#ifdef MELONPRIME_DS
    oldForceSoftwareOutsideMatch = cfg.GetBool("3D.ForceSoftwareOutsideMatch");
#endif
    oldGLScale = cfg.GetInt("3D.GL.ScaleFactor");
    oldGLBetterPolygons = cfg.GetBool("3D.GL.BetterPolygons");
    oldHiresCoordinates = cfg.GetBool("3D.GL.HiresCoordinates");
#ifdef MELONPRIME_DS
    if (RendererForcesHiresCoordinates(oldRenderer))
    {
        oldHiresCoordinates = true;
        cfg.SetBool("3D.GL.HiresCoordinates", true);
    }
    if (RendererForcesBetterPolygons(oldRenderer))
    {
        oldGLBetterPolygons = true;
        cfg.SetBool("3D.GL.BetterPolygons", true);
    }
#endif
#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    oldNvidiaReflexMode = cfg.GetInt(MelonPrime::CfgKey::NvidiaReflexMode);
    oldAmdAntiLag2Enabled = cfg.GetBool(MelonPrime::CfgKey::AmdAntiLag2Enabled);
#endif
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    oldIntelXeLLEnabled = cfg.GetBool(MelonPrime::CfgKey::IntelXeLLEnabled);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    oldIntelXeLLPacingPolicy = cfg.GetInt(MelonPrime::CfgKey::IntelXeLLPacingPolicy);
#endif
#endif

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
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Keep the upstream .ui renderer IDs/layout untouched. Vulkan is appended
    // after every existing renderer so persisted numeric IDs remain stable.
    ui->gridLayout_2->removeItem(ui->verticalSpacer);
    ui->gridLayout_2->removeWidget(ui->cbGLDisplay);
    ui->gridLayout_2->removeWidget(ui->cbVSync);
    ui->gridLayout_2->removeWidget(ui->label_2);
    ui->gridLayout_2->removeWidget(ui->sbVSyncInterval);

#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    constexpr int vulkanRow = 6;
#else
    constexpr int vulkanRow = 4;
#endif
    rb3DVulkan = new QRadioButton(ui->groupBox);
    rb3DVulkan->setObjectName(QStringLiteral("rb3DVulkan"));
    rb3DVulkan->setText(MelonPrime::UiText::Tr("Vulkan"));
    rb3DVulkan->setWhatsThis(VulkanRendererDescription());
    ui->gridLayout_2->addWidget(rb3DVulkan, vulkanRow, 0, 1, 2);
    grp3DRenderer->addButton(rb3DVulkan, renderer3D_Vulkan);

    ui->gridLayout_2->addItem(ui->verticalSpacer, vulkanRow + 1, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->cbGLDisplay, vulkanRow + 2, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->cbVSync, vulkanRow + 3, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->label_2, vulkanRow + 4, 0, 1, 1);
    ui->gridLayout_2->addWidget(ui->sbVSyncInterval, vulkanRow + 4, 1, 1, 1);
    ui->gridLayout_2->invalidate();
    ui->gridLayout_2->activate();
    adjustSize();
#endif
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    // Same approach as the Vulkan block above: the upstream .ui rows and the
    // persisted numeric renderer IDs stay untouched, and DX12 is appended after
    // every renderer that already exists on this platform. Metal is Apple-only,
    // so only the Vulkan row has to be accounted for here.
    ui->gridLayout_2->removeItem(ui->verticalSpacer);
    ui->gridLayout_2->removeWidget(ui->cbGLDisplay);
    ui->gridLayout_2->removeWidget(ui->cbVSync);
    ui->gridLayout_2->removeWidget(ui->label_2);
    ui->gridLayout_2->removeWidget(ui->sbVSyncInterval);

#if defined(MELONPRIME_ENABLE_VULKAN)
    constexpr int dx12Row = 5;
#else
    constexpr int dx12Row = 4;
#endif
    rb3DDX12 = new QRadioButton(ui->groupBox);
    rb3DDX12->setObjectName(QStringLiteral("rb3DDX12"));
    rb3DDX12->setText(MelonPrime::UiText::Tr("DirectX 12"));
    rb3DDX12->setWhatsThis(MelonPrime::UiText::Tr(
        "<html><head/><body><p>Native DirectX 12 renderer. The GPU rasterizes 3D and recomposites the software 2D layers at the selected internal resolution.</p></body></html>"));
    ui->gridLayout_2->addWidget(rb3DDX12, dx12Row, 0, 1, 2);
    grp3DRenderer->addButton(rb3DDX12, renderer3D_DX12);

    ui->gridLayout_2->addItem(ui->verticalSpacer, dx12Row + 1, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->cbGLDisplay, dx12Row + 2, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->cbVSync, dx12Row + 3, 0, 1, 2);
    ui->gridLayout_2->addWidget(ui->label_2, dx12Row + 4, 0, 1, 1);
    ui->gridLayout_2->addWidget(ui->sbVSyncInterval, dx12Row + 4, 1, 1, 1);
    ui->gridLayout_2->invalidate();
    ui->gridLayout_2->activate();
    adjustSize();
#endif
#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    lblNvidiaReflex = new QLabel(ui->groupBox_3);
    lblNvidiaReflex->setObjectName(QStringLiteral("lblNvidiaReflex"));
    lblNvidiaReflex->setText(MelonPrime::UiText::Tr("NVIDIA Reflex Low Latency:"));
    cbxNvidiaReflex = new QComboBox(ui->groupBox_3);
    cbxNvidiaReflex->setObjectName(QStringLiteral("cbxNvidiaReflex"));
    cbxNvidiaReflex->addItem(MelonPrime::UiText::Tr("Off"));
    cbxNvidiaReflex->addItem(MelonPrime::UiText::Tr("On"));
    cbxNvidiaReflex->addItem(MelonPrime::UiText::Tr("On + Boost"));
    cbxNvidiaReflex->setCurrentIndex(qBound(0, oldNvidiaReflexMode, 2));
    ui->gridLayout_4->addWidget(lblNvidiaReflex, 4, 0, 1, 1);
    ui->gridLayout_4->addWidget(cbxNvidiaReflex, 5, 0, 1, 1);
    connect(
        cbxNvidiaReflex,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &VideoSettingsDialog::onNvidiaReflexModeChanged);

    lblAmdAntiLag2 = new QLabel(ui->groupBox_3);
    lblAmdAntiLag2->setObjectName(QStringLiteral("lblAmdAntiLag2"));
    lblAmdAntiLag2->setText(MelonPrime::UiText::Tr("AMD Radeon Anti-Lag 2:"));
    cbxAmdAntiLag2 = new QComboBox(ui->groupBox_3);
    cbxAmdAntiLag2->setObjectName(QStringLiteral("cbxAmdAntiLag2"));
    cbxAmdAntiLag2->addItem(MelonPrime::UiText::Tr("Off"));
    cbxAmdAntiLag2->addItem(MelonPrime::UiText::Tr("On"));
    cbxAmdAntiLag2->setCurrentIndex(oldAmdAntiLag2Enabled ? 1 : 0);
    ui->gridLayout_4->addWidget(lblAmdAntiLag2, 6, 0, 1, 1);
    ui->gridLayout_4->addWidget(cbxAmdAntiLag2, 7, 0, 1, 1);
    connect(
        cbxAmdAntiLag2,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &VideoSettingsDialog::onAmdAntiLag2ModeChanged);

#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    lblIntelXeLL = new QLabel(ui->groupBox_3);
    lblIntelXeLL->setObjectName(QStringLiteral("lblIntelXeLL"));
    lblIntelXeLL->setText(MelonPrime::UiText::Tr("Intel Xe Low Latency (XeLL):"));
    cbxIntelXeLL = new QComboBox(ui->groupBox_3);
    cbxIntelXeLL->setObjectName(QStringLiteral("cbxIntelXeLL"));
    cbxIntelXeLL->addItem(MelonPrime::UiText::Tr("Off"));
    cbxIntelXeLL->addItem(MelonPrime::UiText::Tr("On"));
    cbxIntelXeLL->setCurrentIndex(oldIntelXeLLEnabled ? 1 : 0);
    ui->gridLayout_4->addWidget(lblIntelXeLL, 8, 0, 1, 1);
    ui->gridLayout_4->addWidget(cbxIntelXeLL, 9, 0, 1, 1);
    connect(
        cbxIntelXeLL,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &VideoSettingsDialog::onIntelXeLLModeChanged);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    lblIntelXeLLPacingPolicy = new QLabel(ui->groupBox_3);
    lblIntelXeLLPacingPolicy->setObjectName(QStringLiteral("lblIntelXeLLPacingPolicy"));
    lblIntelXeLLPacingPolicy->setText(MelonPrime::UiText::Tr("Intel XeLL pacing policy:"));
    cbxIntelXeLLPacingPolicy = new QComboBox(ui->groupBox_3);
    cbxIntelXeLLPacingPolicy->setObjectName(QStringLiteral("cbxIntelXeLLPacingPolicy"));
    cbxIntelXeLLPacingPolicy->addItem(MelonPrime::UiText::Tr("Compatibility"));
    cbxIntelXeLLPacingPolicy->addItem(MelonPrime::UiText::Tr("Bypass DXGI wait (experimental)"));
    cbxIntelXeLLPacingPolicy->addItem(MelonPrime::UiText::Tr("Bypass host limiter (experimental)"));
    cbxIntelXeLLPacingPolicy->addItem(MelonPrime::UiText::Tr("XeLL frame cap (experimental)"));
    cbxIntelXeLLPacingPolicy->addItem(MelonPrime::UiText::Tr("Intel recommended (experimental)"));
    cbxIntelXeLLPacingPolicy->setCurrentIndex(qBound(0, oldIntelXeLLPacingPolicy, 4));
    ui->gridLayout_4->addWidget(lblIntelXeLLPacingPolicy, 10, 0, 1, 1);
    ui->gridLayout_4->addWidget(cbxIntelXeLLPacingPolicy, 11, 0, 1, 1);
    connect(
        cbxIntelXeLLPacingPolicy,
        QOverload<int>::of(&QComboBox::currentIndexChanged),
        this,
        &VideoSettingsDialog::onIntelXeLLPacingPolicyChanged);
#endif
#endif
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
    // Native Metal is exposed in normal macOS builds. Keep the runtime
    // feature probe so an unsupported Mac receives a disabled choice with the
    // precise device/pipeline failure reason.
    const bool metalSupported = MelonPrime::Metal::SupportsRequiredBaseline();
    const QString metalTooltip = metalSupported
        ? QStringLiteral("Native Metal renderer for macOS. Supports internal resolution scaling, improved polygon splitting, and high-resolution coordinates.")
        : QString::fromStdString(MelonPrime::Metal::CachedFeatureInfo().unavailableReason);
    rb3DMetal->setEnabled(metalSupported);
    rb3DMetal->setToolTip(MelonPrime::UiText::Tr(metalTooltip));
    rb3DMetalCompute->setEnabled(metalSupported);
    rb3DMetalCompute->setToolTip(MelonPrime::UiText::Tr(
        metalSupported
            ? QStringLiteral("Experimental Metal compute-shader renderer. Compute stages run natively while the validated Metal raster output remains the safe visible source.")
            : metalTooltip));
#endif

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)
    // Opening Video Settings must not probe a foreign GPU API. Creating a
    // second native device while the active renderer owns its queues can
    // destabilize the live driver (observed as VK_ERROR_DEVICE_LOST when a
    // DX12 probe ran over Vulkan). Retry only the selected backend; choices
    // that are not active remain selectable and are probed during transition.
    if (oldRenderer == renderer3D_Vulkan)
    {
        MelonPrime::VulkanFeatureCheck::ResetProbeForRetry();
        const auto& vulkanProbe = MelonPrime::VulkanFeatureCheck::Probe();
        rb3DVulkan->setEnabled(vulkanProbe.Available);
        rb3DVulkan->setToolTip(MelonPrime::UiText::Tr(
            vulkanProbe.Available
                ? VulkanRendererDescription()
                : QString::fromStdString(vulkanProbe.Reason)));
    }
    else
    {
        rb3DVulkan->setEnabled(true);
        rb3DVulkan->setToolTip(VulkanRendererDescription());
    }
    rb3DVulkan->setWhatsThis(rb3DVulkan->toolTip());
#endif

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    const QString dx12Description = MelonPrime::UiText::Tr(
        "Native DirectX 12 renderer. Internal-resolution scaling is applied to the composed screen output.");
    if (oldRenderer == renderer3D_DX12)
    {
        // Refreshing the dialog is not a request to use DX12, so it must not
        // clear a latched failure -- doing so would silently re-enable the
        // radio button for a backend that just failed, with no user action.
        // Probe() returns the cached answer when there is a durable one, so
        // this reads state rather than re-running a device probe.
        const auto& dx12Probe = MelonPrime::DX12FeatureCheck::Probe();
        rb3DDX12->setEnabled(dx12Probe.Available);
        rb3DDX12->setToolTip(MelonPrime::UiText::Tr(
            dx12Probe.Available
                ? dx12Description
                : QString::fromStdString(dx12Probe.Reason)));
    }
    else
    {
        rb3DDX12->setEnabled(true);
        rb3DDX12->setToolTip(dx12Description);
    }
#endif

    ui->cbGLDisplay->setChecked(oldGLDisplay != 0);

    ui->cbVSync->setChecked(oldVSync != 0);
    ui->sbVSyncInterval->setValue(oldVSyncInterval);

    ui->cbSoftwareThreaded->setChecked(oldSoftThreaded);
#ifdef MELONPRIME_DS
    ui->cbForceSoftwareOutsideMatch->setChecked(oldForceSoftwareOutsideMatch);
#else
    ui->cbForceSoftwareOutsideMatch->hide();
#endif

    for (int i = 1; i <= 16; i++)
        ui->cbxGLResolution->addItem(QString("%1x native (%2x%3)").arg(i).arg(256*i).arg(192*i));
#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // Metal and Metal Compute intentionally share the existing hardware
    // renderer scale setting with OpenGL and OpenGL Compute.
    ui->cbxGLResolution->setToolTip(MelonPrime::UiText::Tr(
        "Internal 3D render scale. Used by OpenGL, OpenGL Compute, Metal, and Metal Compute Shader."));
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

    auto& cfg = emuInstance->getGlobalConfig();
#ifdef MELONPRIME_DS
    const auto currentBackend = MelonPrime::VideoBackend::ResolvePresentationBackend(
        cfg.GetBool("Screen.UseGL"), cfg.GetInt("3D.Renderer"));
#else
    const bool old_gl = UsesGL();
#endif

    cfg.SetInt("3D.Renderer", oldRenderer);
    cfg.SetBool("Screen.UseGL", oldGLDisplay);
    cfg.SetBool("Screen.VSync", oldVSync);
    cfg.SetInt("Screen.VSyncInterval", oldVSyncInterval);
    cfg.SetBool("3D.Soft.Threaded", oldSoftThreaded);
#ifdef MELONPRIME_DS
    cfg.SetBool("3D.ForceSoftwareOutsideMatch", oldForceSoftwareOutsideMatch);
#endif
    cfg.SetInt("3D.GL.ScaleFactor", oldGLScale);
    cfg.SetBool("3D.GL.BetterPolygons", oldGLBetterPolygons);
    cfg.SetBool("3D.GL.HiresCoordinates", oldHiresCoordinates);
#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    cfg.SetInt(MelonPrime::CfgKey::NvidiaReflexMode, oldNvidiaReflexMode);
    cfg.SetBool(MelonPrime::CfgKey::AmdAntiLag2Enabled, oldAmdAntiLag2Enabled);
#endif
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    cfg.SetBool(MelonPrime::CfgKey::IntelXeLLEnabled, oldIntelXeLLEnabled);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    cfg.SetInt(MelonPrime::CfgKey::IntelXeLLPacingPolicy, oldIntelXeLLPacingPolicy);
#endif
#endif

#ifdef MELONPRIME_DS
    const auto restoredBackend = MelonPrime::VideoBackend::ResolvePresentationBackend(
        cfg.GetBool("Screen.UseGL"), cfg.GetInt("3D.Renderer"));
    emit updateVideoSettings(currentBackend != restoredBackend);
#else
    emit updateVideoSettings(old_gl != UsesGL());
#endif

    closeDlg();
}

void VideoSettingsDialog::setVsyncControlEnable(bool hasOGL)
{
#if defined(MELONPRIME_DS) && \
    (defined(MELONPRIME_ENABLE_VULKAN) || \
     (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    bool hasVSyncControl = hasOGL;
#if defined(MELONPRIME_ENABLE_VULKAN)
    const bool vulkanRenderer =
        emuInstance->getGlobalConfig().GetInt("3D.Renderer") == renderer3D_Vulkan;
    hasVSyncControl = hasVSyncControl || vulkanRenderer;
#endif
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    const bool dx12Renderer =
        emuInstance->getGlobalConfig().GetInt("3D.Renderer") == renderer3D_DX12;
    hasVSyncControl = hasVSyncControl || dx12Renderer;
#endif
    ui->cbVSync->setEnabled(hasVSyncControl);
    // Native presenters control synchronization without exposing the OpenGL
    // swap-interval setting.
    const bool intervalEnabled = hasOGL && ui->cbVSync->isChecked();
    ui->label_2->setEnabled(intervalEnabled);
    ui->sbVSyncInterval->setEnabled(intervalEnabled);

    // Keep the saved interval untouched for other renderers, but explain why
    // the disabled control is unused by the selected native presenter.
    QString intervalDescription;
#if defined(MELONPRIME_ENABLE_VULKAN)
    if (vulkanRenderer)
    {
        intervalDescription = MelonPrime::UiText::Tr(
            "VSync Interval is not used by the native Vulkan presenter.");
    }
#endif
#if defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
    if (dx12Renderer)
    {
        intervalDescription = MelonPrime::UiText::Tr(
            "VSync Interval is not used by the native DirectX 12 presenter.");
    }
#endif
    ui->label_2->setToolTip(intervalDescription);
    ui->sbVSyncInterval->setToolTip(intervalDescription);

    // Keep the same explanation available to keyboard/accessibility users.
    // Preserve Designer's renderer-neutral What's This text so changing the
    // radio button back to OpenGL restores its original help instead of
    // leaving Vulkan guidance attached to another backend.
    constexpr const char* originalHelpProperty = "MelonPrimeOriginalVSyncIntervalHelp";
    if (!ui->sbVSyncInterval->property(originalHelpProperty).isValid())
        ui->sbVSyncInterval->setProperty(originalHelpProperty, ui->sbVSyncInterval->whatsThis());
    if (!ui->label_2->property(originalHelpProperty).isValid())
        ui->label_2->setProperty(originalHelpProperty, ui->label_2->whatsThis());
    ui->sbVSyncInterval->setWhatsThis(!intervalDescription.isEmpty()
        ? intervalDescription
        : ui->sbVSyncInterval->property(originalHelpProperty).toString());
    ui->label_2->setWhatsThis(!intervalDescription.isEmpty()
        ? intervalDescription
        : ui->label_2->property(originalHelpProperty).toString());
#else
    ui->cbVSync->setEnabled(hasOGL);
    ui->label_2->setEnabled(hasOGL);
    ui->sbVSyncInterval->setEnabled(hasOGL);
#endif
}

void VideoSettingsDialog::onChange3DRenderer(int renderer)
{
#ifdef MELONPRIME_DS
    auto& cfg = emuInstance->getGlobalConfig();
    // This slot fires on a click, including a click on the button that is
    // already checked -- which is exactly how a user retries a backend that
    // failed. Announcing the request here is what lets a latched DX12
    // runtime failure clear and recover in the same process; an automatic
    // fallback reaches the transition by a different route and never gets
    // here, so it cannot loop.
    MelonPrime::VideoBackend::NotifyRendererRequest(
        renderer, MelonPrime::VideoBackend::RendererRequestOrigin::User);
    cfg.SetInt("3D.Renderer", renderer);

    // The direct signal performs the synchronous backend transition. Keep all
    // selected-backend feature probes after it so the old native device has
    // been destroyed before another graphics API creates or acquires one. A
    // renderer radio-button change always rebuilds presentation; computing
    // the new backend here would itself run the selected backend's probe.
    emit updateVideoSettings(true);

    setEnabled();
    setVsyncControlEnable(UsesGL());
#else
    bool old_gl = UsesGL();

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.Renderer", renderer);

    setEnabled();

    emit updateVideoSettings(old_gl != UsesGL());
#endif
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
#if !defined(MELONPRIME_DS) || \
    (!defined(MELONPRIME_ENABLE_VULKAN) && \
     (!defined(_WIN32) || !defined(MELONPRIME_ENABLE_DX12)))
    ui->sbVSyncInterval->setEnabled(vsync);
#endif

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("Screen.VSync", vsync);
#if defined(MELONPRIME_DS) && \
    (defined(MELONPRIME_ENABLE_VULKAN) || \
     (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
    setVsyncControlEnable(UsesGL());
#endif

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

void VideoSettingsDialog::on_cbForceSoftwareOutsideMatch_stateChanged(int state)
{
#ifdef MELONPRIME_DS
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool("3D.ForceSoftwareOutsideMatch", state != 0);

    emit updateVideoSettings(false);
#else
    Q_UNUSED(state);
#endif
}

void VideoSettingsDialog::on_cbxGLResolution_currentIndexChanged(int idx)
{
    // prevent a spurious change
    if (ui->cbxGLResolution->count() < 16) return;

    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt("3D.GL.ScaleFactor", idx+1);

#if defined(MELONPRIME_DS) && defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // MELONPRIME_METAL_COMPUTE_LIVE_SCALE_V2
    // Do not reconstruct Metal Compute at the temporary default 1x scale.
    // updateVideoSettings() below applies RendererSettings to the existing
    // renderer, which resizes the raster target, compute buffers and final
    // Metal output as one live settings update.
#endif

    setVsyncControlEnable(UsesGL());

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbBetterPolygons_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    const bool forced =
#ifdef MELONPRIME_DS
        RendererForcesBetterPolygons(cfg.GetInt("3D.Renderer"));
#else
        false;
#endif
    if (forced && state == 0)
    {
        const QSignalBlocker blocker(ui->cbBetterPolygons);
        ui->cbBetterPolygons->setChecked(true);
    }
    cfg.SetBool("3D.GL.BetterPolygons", forced || state != 0);

    emit updateVideoSettings(false);
}

void VideoSettingsDialog::on_cbxComputeHiResCoords_stateChanged(int state)
{
    auto& cfg = emuInstance->getGlobalConfig();
    const bool forced =
#ifdef MELONPRIME_DS
        RendererForcesHiresCoordinates(cfg.GetInt("3D.Renderer"));
#else
        false;
#endif
    if (forced && state == 0)
    {
        const QSignalBlocker blocker(ui->cbxComputeHiResCoords);
        ui->cbxComputeHiResCoords->setChecked(true);
    }
    cfg.SetBool("3D.GL.HiresCoordinates", forced || state != 0);

    emit updateVideoSettings(false);
}

#if defined(MELONPRIME_DS) && (defined(MELONPRIME_ENABLE_VULKAN) \
    || (defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)))
void VideoSettingsDialog::onNvidiaReflexModeChanged(int mode)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt(MelonPrime::CfgKey::NvidiaReflexMode, mode);
    emit updateVideoSettings(false);
}

void VideoSettingsDialog::onAmdAntiLag2ModeChanged(int mode)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool(MelonPrime::CfgKey::AmdAntiLag2Enabled, mode != 0);
    emit updateVideoSettings(false);
}
#endif

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)
void VideoSettingsDialog::onIntelXeLLModeChanged(int mode)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetBool(MelonPrime::CfgKey::IntelXeLLEnabled, mode != 0);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    setEnabled();
#endif
    emit updateVideoSettings(false);
}
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
void VideoSettingsDialog::onIntelXeLLPacingPolicyChanged(int policy)
{
    auto& cfg = emuInstance->getGlobalConfig();
    cfg.SetInt(MelonPrime::CfgKey::IntelXeLLPacingPolicy, policy);
    emit updateVideoSettings(false);
}
#endif
#endif
