/*
    Copyright 2016-2025 melonDS team

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

#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include <optional>
#include <string>

#include <QApplication>
#include <QStyle>
#include <QMessageBox>
#include <QMenuBar>
#include <QFileDialog>
#include <QInputDialog>
#include <QPainter>
#include <QKeyEvent>
#include <QMimeData>
#include <QVector>
#include <QCommandLineParser>
#include <QStandardPaths>
#ifndef _WIN32
#include <QGuiApplication>
#include <QSocketNotifier>
#include <unistd.h>
#include <sys/socket.h>
#include <csignal>
#endif

#include <SDL2/SDL.h>

#include "OpenGLSupport.h"
#include "duckstation/gl/context.h"

#include "main.h"
#include "version.h"
#include "MelonPrimeBuildInfo.h"
#ifdef MELONPRIME_CUSTOM_HUD
#include "MelonPrimeHudRender.h"
#endif
#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
#include "MelonPrimeMetalFeatureCheck.h"
#endif
#if defined(MELONPRIME_ENABLE_VULKAN)
#include "GPU_Vulkan.h"
#include "GPU3D_Vulkan.h"
#include "MelonPrimeVulkanClearBitmapBootstrap.h"
#include "MelonPrimeVulkanVertexUploadBootstrap.h"
#include "MelonPrimeVulkanPolygonBatchBootstrap.h"
#include "MelonPrimeVulkanOpaqueBootstrap.h"
#include "MelonPrimeVulkanTranslucentBootstrap.h"
#include "MelonPrimeVulkanShadowBootstrap.h"
#include "MelonPrimeVulkanToonHighlightBootstrap.h"
#include "MelonPrimeVulkanToonHighlightDescriptorBootstrap.h"
#include "MelonPrimeVulkanTextureSamplingBootstrap.h"
#include "MelonPrimeVulkanTexturedPolygonBootstrap.h"
#include "MelonPrimeVulkanTextureCacheBootstrap.h"
#include "MelonPrimeVulkanClearPlaneBootstrap.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanRasterBootstrap.h"
#include "MelonPrimeScreenVulkan.h"
#endif

#include "Config.h"
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
#include "GPU.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#endif

#include "EmuInstance.h"
#include "ArchiveUtil.h"
#include "CameraManager.h"
#include "MPInterface.h"
#include "Net.h"

#include "CLI.h"

#include "Net_PCap.h"
#include "Net_Slirp.h"

using namespace melonDS;

QString* systemThemeName;


QString emuDirectory;

const int kMaxEmuInstances = 16;
EmuInstance* emuInstances[kMaxEmuInstances];

CameraManager* camManager[2];
bool camStarted[2];

std::optional<LibPCap> pcap;
Net net;


QElapsedTimer sysTimer;


void NetInit()
{
    Config::Table cfg = Config::GetGlobalTable();
    if (cfg.GetBool("LAN.DirectMode"))
    {
        if (!pcap)
            pcap = LibPCap::New();

        if (pcap)
        {
            std::string devicename = cfg.GetString("LAN.Device");
            std::unique_ptr<Net_PCap> netPcap = pcap->Open(devicename, [](const u8* data, int len) {
                net.RXEnqueue(data, len);
            });

            if (netPcap)
            {
                net.SetDriver(std::move(netPcap));
            }
        }
    }
    else
    {
        net.SetDriver(std::make_unique<Net_Slirp>([](const u8* data, int len) {
            net.RXEnqueue(data, len);
        }));
    }
}


bool createEmuInstance()
{
    int id = -1;
    for (int i = 0; i < kMaxEmuInstances; i++)
    {
        if (!emuInstances[i])
        {
            id = i;
            break;
        }
    }

    if (id == -1)
        return false;

    auto inst = new EmuInstance(id);
    emuInstances[id] = inst;

    return true;
}

void deleteEmuInstance(int id)
{
    auto inst = emuInstances[id];
    if (!inst) return;

    delete inst;
    emuInstances[id] = nullptr;
}

void deleteAllEmuInstances(int first)
{
    for (int i = first; i < kMaxEmuInstances; i++)
        deleteEmuInstance(i);
}

int numEmuInstances()
{
    int ret = 0;

    for (int i = 0; i < kMaxEmuInstances; i++)
    {
        if (emuInstances[i])
            ret++;
    }

    return ret;
}


void broadcastInstanceCommand(int cmd, QVariant& param, int sourceinst)
{
    for (int i = 0; i < kMaxEmuInstances; i++)
    {
        if (i == sourceinst) continue;
        if (!emuInstances[i]) continue;

        emuInstances[i]->handleCommand(cmd, param);
    }
}


void pathInit()
{
    // First, check for the portable directory next to the executable.
    QString appdirpath = QCoreApplication::applicationDirPath();
    QString portablepath = appdirpath + QDir::separator() + "portable";

#if defined(__APPLE__)
    // On Apple platforms we may need to navigate outside an app bundle.
    // The executable directory would be "melonDS.app/Contents/MacOS", so we need to go a total of three steps up.
    QDir bundledir(appdirpath);
    if (bundledir.cd("..") && bundledir.cd("..") && bundledir.dirName().endsWith(".app") && bundledir.cd(".."))
    {
        portablepath = bundledir.absolutePath() + QDir::separator() + "portable";
    }
#endif

    QDir portabledir(portablepath);
    if (portabledir.exists())
    {
        emuDirectory = portabledir.absolutePath();
    }
    else
    {
        // If no overrides are specified, use the default path.
#if defined(__WIN32__) && defined(WIN32_PORTABLE)
        emuDirectory = appdirpath;
#else
        QString confdir;
        QDir config(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation));
        config.mkdir("melonDS");
        confdir = config.absolutePath() + QDir::separator() + "melonDS";
        emuDirectory = confdir;
#endif
    }
}


void setMPInterface(MPInterfaceType type)
{
    // switch to the requested MP interface
    MPInterface::Set(type);

    // set receive timeout
    // TODO: different settings per interface?
    MPInterface::Get().SetRecvTimeout(Config::GetGlobalTable().GetInt("MP.RecvTimeout"));

    // update UI appropriately
    // TODO: decide how to deal with multi-window when it becomes a thing
    for (int i = 0; i < kMaxEmuInstances; i++)
    {
        EmuInstance* inst = emuInstances[i];
        if (!inst) continue;

        MainWindow* win = inst->getMainWindow();
        if (win) win->updateMPInterface(type);
    }
}



MelonApplication::MelonApplication(int& argc, char** argv)
    : QApplication(argc, argv)
{
#if !defined(Q_OS_APPLE)
    setWindowIcon(QIcon(":/melon-icon"));
    #if defined(Q_OS_UNIX)
        setDesktopFileName(QString("net.kuribo64.melonDS"));
    #endif
#endif
}

MelonApplication::~MelonApplication() = default;

#if defined(MELONPRIME_ENABLE_VULKAN)
MelonPrime::Vulkan::InstanceHost& MelonApplication::vulkanInstanceHost()
{
    if (!m_vulkanInstanceHost)
        m_vulkanInstanceHost = std::make_unique<MelonPrime::Vulkan::InstanceHost>();
    return *m_vulkanInstanceHost;
}
#endif

// TODO: ROM loading should be moved to EmuInstance
// especially so the preloading below and in main() can be done in a nicer fashion

bool MelonApplication::event(QEvent *event)
{
    if (event->type() == QEvent::FileOpen)
    {
        EmuInstance* inst = emuInstances[0];
        MainWindow* win = inst->getMainWindow();
        QFileOpenEvent *openEvent = static_cast<QFileOpenEvent*>(event);

        const QStringList file = win->splitArchivePath(openEvent->file(), true);
        win->preloadROMs(file, {}, true);
    }

    return QApplication::event(event);
}

#ifndef _WIN32
static void signalHandler(int signal)
{
    std::signal(signal, SIG_DFL);
    qApp->quit();
}
#endif

static std::optional<QString> melonPrimeHudGoldenOutputPath(int argc, char** argv)
{
#if defined(MELONPRIME_CUSTOM_HUD) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-hud-golden") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanProbeOutputPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-probe") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeOutputLeaseTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-output-lease-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanRendererShellTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-renderer-shell-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanRasterBootstrapTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-raster-bootstrap-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanClearPlaneTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-clear-plane-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanClearBitmapTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-clear-bitmap-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanVertexUploadTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-vertex-upload-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanPolygonBatchTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-polygon-batch-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanOpaquePipelineTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-opaque-pipeline-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanTranslucentPipelineTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-translucent-pipeline-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanShadowPipelineTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-shadow-pipeline-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanToonHighlightContractTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--melonprime-vulkan-toon-highlight-shader-abi-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
#endif
    (void)argc; (void)argv; return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanTextureSamplingTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--melonprime-vulkan-texture-sampling-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
    }
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
static int runMelonPrimeOutputLeaseTest(const QString& outputPath)
{
    struct ReleaseState
    {
        int Calls = 0;
        int PresenterRefs = 1;
    } state;
    auto release = +[](void* opaque) {
        auto* state = static_cast<ReleaseState*>(opaque);
        state->Calls++;
        state->PresenterRefs--;
    };

    melonDS::RendererOutputLease first(
        melonDS::RendererOutput::CpuBgra(nullptr, nullptr), &state, release);
    melonDS::RendererOutputLease second(std::move(first));
    melonDS::RendererOutputLease third;
    third = std::move(second);
    third.ReleaseNow();
    third.ReleaseNow();

    bool vulkanDescriptorPassed = true;
    bool staleGenerationRejected = true;
    qint64 frameSerial = 0;
    qint64 generation = 0;
#if defined(MELONPRIME_ENABLE_VULKAN)
    melonDS::VulkanRendererOutput descriptor;
    descriptor.Format = VK_FORMAT_B8G8R8A8_UNORM;
    descriptor.Extent = {512, 384};
    descriptor.Layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptor.LayerCount = 2;
    descriptor.EngineALayer = 0;
    descriptor.FrameSerial = 42;
    descriptor.Generation = 7;
    descriptor.ProducerValue = 42;
    const melonDS::RendererOutput output =
        melonDS::RendererOutput::VulkanImage(&descriptor);
    vulkanDescriptorPassed =
        output.Kind == melonDS::RendererOutputKind::VulkanImage &&
        output.Top == &descriptor && output.FrameSerial == descriptor.FrameSerial &&
        output.Generation == descriptor.Generation &&
        output.MatchesProducerFrame(descriptor.FrameSerial, descriptor.Generation);
    staleGenerationRejected = !output.MatchesProducerFrame(
        descriptor.FrameSerial, descriptor.Generation + 1);
    frameSerial = static_cast<qint64>(output.FrameSerial);
    generation = static_cast<qint64>(output.Generation);
#endif

    const bool passed = state.Calls == 1 && state.PresenterRefs == 0 &&
        vulkanDescriptorPassed && staleGenerationRejected;
    const QJsonObject result{
        {"schema_version", 1},
        {"passed", passed},
        {"release_calls", state.Calls},
        {"presenter_refs_after_release", state.PresenterRefs},
        {"vulkan_descriptor_compiled",
#if defined(MELONPRIME_ENABLE_VULKAN)
            true},
#else
            false},
#endif
        {"vulkan_descriptor_passed", vulkanDescriptorPassed},
        {"stale_generation_rejected", staleGenerationRejected},
        {"frame_serial", frameSerial},
        {"generation", generation},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
static std::optional<QString> melonPrimeVulkanToonHighlightDescriptorTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--melonprime-vulkan-toon-highlight-descriptor-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

static int runMelonPrimeVulkanRendererShellTest(const QString& outputPath)
{
    // MELONPRIME_VULKAN_RENDERER_SHELL_V1
    const melonDS::VulkanRendererShellContract raster =
        melonDS::DescribeVulkanRendererShell(false);
    const melonDS::VulkanRendererShellContract compute =
        melonDS::DescribeVulkanRendererShell(true);
    const bool passed =
        raster.ContractVersion == 16 && compute.ContractVersion == 16 &&
        !raster.ComputeSelected && compute.ComputeSelected &&
        raster.UsesSoftwareCorrectnessBaseline &&
        compute.UsesSoftwareCorrectnessBaseline &&
        raster.NativeVulkanRasterBootstrapAvailable &&
        compute.NativeVulkanRasterBootstrapAvailable &&
        raster.NativeVulkanClearPlaneBootstrapAvailable &&
        compute.NativeVulkanClearPlaneBootstrapAvailable &&
        raster.NativeVulkanClearBitmapBootstrapAvailable &&
        compute.NativeVulkanClearBitmapBootstrapAvailable &&
        raster.NativeVulkanVertexUploadBootstrapAvailable &&
        compute.NativeVulkanVertexUploadBootstrapAvailable &&
        raster.NativeVulkanPolygonBatchBootstrapAvailable &&
        compute.NativeVulkanPolygonBatchBootstrapAvailable &&
        raster.NativeVulkanOpaquePipelineBootstrapAvailable &&
        compute.NativeVulkanOpaquePipelineBootstrapAvailable &&
        raster.NativeVulkanTranslucentPipelineBootstrapAvailable &&
        compute.NativeVulkanTranslucentPipelineBootstrapAvailable &&
        raster.NativeVulkanShadowPipelineBootstrapAvailable &&
        compute.NativeVulkanShadowPipelineBootstrapAvailable &&
        raster.NativeVulkanToonHighlightContractAvailable &&
        compute.NativeVulkanToonHighlightContractAvailable &&
        raster.NativeVulkanToonHighlightShaderAbiAvailable &&
        compute.NativeVulkanToonHighlightShaderAbiAvailable &&
        raster.NativeVulkanToonHighlightDescriptorRuntimeAvailable &&
        compute.NativeVulkanToonHighlightDescriptorRuntimeAvailable &&
        raster.NativeVulkanToonHighlightGpuDrawAvailable &&
        compute.NativeVulkanToonHighlightGpuDrawAvailable &&
        raster.NativeVulkanTextureSamplingBootstrapAvailable &&
        compute.NativeVulkanTextureSamplingBootstrapAvailable &&
        raster.NativeVulkanTexturedPolygonBootstrapAvailable &&
        compute.NativeVulkanTexturedPolygonBootstrapAvailable &&
        raster.NativeVulkanTextureCacheBootstrapAvailable &&
        compute.NativeVulkanTextureCacheBootstrapAvailable &&
        !raster.NativeVulkan3DImplemented &&
        !compute.NativeVulkan3DImplemented;
    const QJsonObject result{
        {"schema_version", 16},
        {"passed", passed},
        {"contract_version", static_cast<int>(raster.ContractVersion)},
        {"raster_mode", QString::fromLatin1(raster.ModeName)},
        {"compute_mode", QString::fromLatin1(compute.ModeName)},
        {"raster_software_correctness_baseline", raster.UsesSoftwareCorrectnessBaseline},
        {"compute_software_correctness_baseline", compute.UsesSoftwareCorrectnessBaseline},
        {"raster_bootstrap_available", raster.NativeVulkanRasterBootstrapAvailable},
        {"compute_raster_bootstrap_available", compute.NativeVulkanRasterBootstrapAvailable},
        {"raster_clear_plane_bootstrap_available", raster.NativeVulkanClearPlaneBootstrapAvailable},
        {"compute_clear_plane_bootstrap_available", compute.NativeVulkanClearPlaneBootstrapAvailable},
        {"raster_clear_bitmap_bootstrap_available", raster.NativeVulkanClearBitmapBootstrapAvailable},
        {"compute_clear_bitmap_bootstrap_available", compute.NativeVulkanClearBitmapBootstrapAvailable},
        {"raster_vertex_upload_bootstrap_available", raster.NativeVulkanVertexUploadBootstrapAvailable},
        {"compute_vertex_upload_bootstrap_available", compute.NativeVulkanVertexUploadBootstrapAvailable},
        {"raster_polygon_batch_bootstrap_available", raster.NativeVulkanPolygonBatchBootstrapAvailable},
        {"compute_polygon_batch_bootstrap_available", compute.NativeVulkanPolygonBatchBootstrapAvailable},
        {"raster_opaque_pipeline_bootstrap_available", raster.NativeVulkanOpaquePipelineBootstrapAvailable},
        {"compute_opaque_pipeline_bootstrap_available", compute.NativeVulkanOpaquePipelineBootstrapAvailable},
        {"raster_translucent_pipeline_bootstrap_available", raster.NativeVulkanTranslucentPipelineBootstrapAvailable},
        {"compute_translucent_pipeline_bootstrap_available", compute.NativeVulkanTranslucentPipelineBootstrapAvailable},
        {"raster_shadow_pipeline_bootstrap_available", raster.NativeVulkanShadowPipelineBootstrapAvailable},
        {"compute_shadow_pipeline_bootstrap_available", compute.NativeVulkanShadowPipelineBootstrapAvailable},
        {"raster_toon_highlight_contract_available", raster.NativeVulkanToonHighlightContractAvailable},
        {"compute_toon_highlight_contract_available", compute.NativeVulkanToonHighlightContractAvailable},
        {"raster_toon_highlight_shader_abi_available", raster.NativeVulkanToonHighlightShaderAbiAvailable},
        {"compute_toon_highlight_shader_abi_available", compute.NativeVulkanToonHighlightShaderAbiAvailable},
        {"raster_toon_highlight_descriptor_runtime_available", raster.NativeVulkanToonHighlightDescriptorRuntimeAvailable},
        {"compute_toon_highlight_descriptor_runtime_available", compute.NativeVulkanToonHighlightDescriptorRuntimeAvailable},
        {"raster_toon_highlight_gpu_draw_available", raster.NativeVulkanToonHighlightGpuDrawAvailable},
        {"compute_toon_highlight_gpu_draw_available", compute.NativeVulkanToonHighlightGpuDrawAvailable},
        {"raster_texture_sampling_bootstrap_available", raster.NativeVulkanTextureSamplingBootstrapAvailable},
        {"compute_texture_sampling_bootstrap_available", compute.NativeVulkanTextureSamplingBootstrapAvailable},
        {"raster_textured_polygon_bootstrap_available", raster.NativeVulkanTexturedPolygonBootstrapAvailable},
        {"compute_textured_polygon_bootstrap_available", compute.NativeVulkanTexturedPolygonBootstrapAvailable},
        {"raster_texture_cache_bootstrap_available", raster.NativeVulkanTextureCacheBootstrapAvailable},
        {"compute_texture_cache_bootstrap_available", compute.NativeVulkanTextureCacheBootstrapAvailable},
        {"raster_native_vulkan_3d", raster.NativeVulkan3DImplemented},
        {"compute_native_vulkan_3d", compute.NativeVulkan3DImplemented},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    file.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}
#endif

static std::optional<QString> melonPrimeVulkanTexturedPolygonTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--melonprime-vulkan-textured-polygon-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
#endif
    (void)argc; (void)argv; return std::nullopt;
}

static std::optional<QString> melonPrimeVulkanTextureCacheTestPath(int argc, char** argv)
{
#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--melonprime-vulkan-texture-cache-test") == 0 && i + 1 < argc)
            return QString::fromLocal8Bit(argv[i + 1]);
#endif
    (void)argc;
    (void)argv;
    return std::nullopt;
}

int main(int argc, char** argv)
{
    sysTimer.start();
    srand(time(nullptr));

    for (int i = 0; i < kMaxEmuInstances; i++)
        emuInstances[i] = nullptr;

    qputenv("QT_SCALE_FACTOR", "1");

    // D3D12バックエンド指定(Qt QuickのRHIをDirect3D12に固定するため)
    qputenv("QSG_RHI_BACKEND", "d3d12"); // MelonPrimeDS
	// Vsync無効化(Qt QuickのRHIで垂直同期を無効化するため)
    qputenv("QSG_NO_VSYNC", "1"); // MelonPrimeDS
	// 高DPIスケーリング無効化 低遅延になった。
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
	// アンチエイリアス無効化
    qputenv("QT_NO_ANTIALIASING", "1");
    

#if QT_VERSION_MAJOR == 6 && defined(__WIN32__)
    // Allow using the system dark theme palette on Windows
    qputenv("QT_QPA_PLATFORM", "windows:darkmode=2");
#endif

#ifdef _WIN32
    // argc and argv are passed as UTF8 by SDL's WinMain function
    // QT checks for the original value in local encoding though
    // to see whether it is unmodified to activate its hack that
    // retrieves the unicode value via CommandLineToArgvW.
    argc = __argc;
    argv = __argv;

    // Check whether we are already attached to an output stream.
    HANDLE outputHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!outputHandle || (outputHandle == INVALID_HANDLE_VALUE))
    {
        // If started from terminal, attach and output logs to it.
        if (AttachConsole(ATTACH_PARENT_PROCESS))
        {
            freopen("CONOUT$", "a", stdout);
            freopen("CONOUT$", "a", stderr);
        }
        else
        {
            // Otherwise, discard log output.
            freopen("NUL:", "w", stdout);
            freopen("NUL:", "w", stderr);
        }
    }
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif

#ifdef MELONPRIME_DS
    printf(MELONPRIMEDS_TITLE_PREFIX "%s" MELONPRIMEDS_TITLE_SUFFIX "\n", MelonPrime::kBuildStamp);
#else
    printf("melonDS " MELONDS_VERSION "\n");
#endif
    printf(MELONDS_URL "\n");

#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL)
    // Metal-plan Phase 2 (.claude/rules/melonprime-metal-backend-plan.md):
    // startup diagnostic only. No presenter/renderer reads this yet -- Phase
    // 4+ is what actually gates on SupportsRequiredBaseline().
    MelonPrime::Metal::LogFeatureInfoOnce();
#endif

    // easter egg - not worth checking other cases for something so dumb
    if (argc != 0 && (!strcasecmp(argv[0], "derpDS") || !strcasecmp(argv[0], "./derpDS")))
        printf("did you just call me a derp???\n");

    MelonApplication melon(argc, argv);
    pathInit();

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto leaseOut = melonPrimeOutputLeaseTestPath(argc, argv); leaseOut.has_value())
        return runMelonPrimeOutputLeaseTest(*leaseOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto shellOut = melonPrimeVulkanRendererShellTestPath(argc, argv); shellOut.has_value())
        return runMelonPrimeVulkanRendererShellTest(*shellOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto rasterOut = melonPrimeVulkanRasterBootstrapTestPath(argc, argv); rasterOut.has_value())
        return MelonPrime::Vulkan::RunRasterBootstrapHarness(*rasterOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto clearOut = melonPrimeVulkanClearPlaneTestPath(argc, argv); clearOut.has_value())
        return MelonPrime::Vulkan::RunClearPlaneBootstrapHarness(*clearOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto bitmapOut = melonPrimeVulkanClearBitmapTestPath(argc, argv); bitmapOut.has_value())
        return MelonPrime::Vulkan::RunClearBitmapBootstrapHarness(*bitmapOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto vertexOut = melonPrimeVulkanVertexUploadTestPath(argc, argv); vertexOut.has_value())
        return MelonPrime::Vulkan::RunVertexUploadBootstrapHarness(*vertexOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto batchOut = melonPrimeVulkanPolygonBatchTestPath(argc, argv); batchOut.has_value())
        return MelonPrime::Vulkan::RunPolygonBatchBootstrapHarness(*batchOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto opaqueOut = melonPrimeVulkanOpaquePipelineTestPath(argc, argv); opaqueOut.has_value())
        return MelonPrime::Vulkan::RunOpaquePipelineBootstrapHarness(*opaqueOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto translucentOut = melonPrimeVulkanTranslucentPipelineTestPath(argc, argv); translucentOut.has_value())
        return MelonPrime::Vulkan::RunTranslucentPipelineBootstrapHarness(*translucentOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto shadowOut = melonPrimeVulkanShadowPipelineTestPath(argc, argv); shadowOut.has_value())
        return MelonPrime::Vulkan::RunShadowPipelineBootstrapHarness(*shadowOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto toonOut = melonPrimeVulkanToonHighlightContractTestPath(argc, argv); toonOut.has_value())
        return MelonPrime::Vulkan::RunToonHighlightShaderAbiHarness(*toonOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto toonDescriptorOut = melonPrimeVulkanToonHighlightDescriptorTestPath(argc, argv); toonDescriptorOut.has_value())
        return MelonPrime::Vulkan::RunToonHighlightDescriptorRuntimeHarness(*toonDescriptorOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto textureOut = melonPrimeVulkanTextureSamplingTestPath(argc, argv); textureOut.has_value())
        return MelonPrime::Vulkan::RunTextureSamplingBootstrapHarness(*textureOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto probeOut = melonPrimeVulkanProbeOutputPath(argc, argv); probeOut.has_value())
        return MelonPrime::Vulkan::RunProbeHarness(*probeOut);
#endif

#if defined(MELONPRIME_CUSTOM_HUD) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto goldenOut = melonPrimeHudGoldenOutputPath(argc, argv); goldenOut.has_value())
    {
        if (!Config::Load())
        {
            fprintf(stderr, "Unable to write to config.\n");
            return 1;
        }
        return MelonPrime::CustomHud_RunGoldenHarness(*goldenOut);
    }
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto texturedOut = melonPrimeVulkanTexturedPolygonTestPath(argc, argv); texturedOut.has_value())
        return MelonPrime::Vulkan::RunTexturedPolygonBootstrapHarness(*texturedOut);
#endif

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const auto textureCacheOut = melonPrimeVulkanTextureCacheTestPath(argc, argv); textureCacheOut.has_value())
        return MelonPrime::Vulkan::RunTextureCacheBootstrapHarness(*textureCacheOut);
#endif

    CLI::CommandLineOptions* options = CLI::ManageArgs(melon);

    // http://stackoverflow.com/questions/14543333/joystick-wont-work-using-sdl
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    SDL_SetHint(SDL_HINT_APP_NAME, "melonDS");

    if (SDL_Init(SDL_INIT_HAPTIC) < 0)
    {
        printf("SDL couldn't init rumble\n");
    }
    if (SDL_Init(SDL_INIT_JOYSTICK) < 0)
    {
        printf("SDL couldn't init joystick\n");
    }
    if (SDL_Init(SDL_INIT_SENSOR) < 0)
    {
        printf("SDL couldn't init motion sensors\n");
    }
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        const char* err = SDL_GetError();
        QString errorStr = "Failed to initialize SDL. This could indicate an issue with your audio driver.\n\nThe error was: ";
        errorStr += err;

        QMessageBox::critical(nullptr, "melonDS", errorStr);
        return 1;
    }

    SDL_JoystickEventState(SDL_ENABLE);

    SDL_InitSubSystem(SDL_INIT_VIDEO);
    SDL_EnableScreenSaver(); SDL_DisableScreenSaver();

    if (!Config::Load())
        QMessageBox::critical(nullptr,
                              "melonDS",
                              "Unable to write to config.\nPlease check the write permissions of the folder you placed melonDS in.");

    camStarted[0] = false;
    camStarted[1] = false;
    camManager[0] = new CameraManager(0, 640, 480, true);
    camManager[1] = new CameraManager(1, 640, 480, true);

    systemThemeName = new QString(QApplication::style()->objectName());

    {
        Config::Table cfg = Config::GetGlobalTable();
        QString uitheme = cfg.GetQString("UITheme");
        if (!uitheme.isEmpty())
        {
            QApplication::setStyle(uitheme);
        }
    }

    // fix for Wayland OpenGL glitches
    QGuiApplication::setAttribute(Qt::AA_NativeWindows, false);
    QGuiApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings, true);

    // default MP interface type is local MP
    // this will be changed if a LAN or netplay session is initiated
    setMPInterface(MPInterface_Local);

    NetInit();

    createEmuInstance();

#if defined(MELONPRIME_ENABLE_VULKAN) && defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const QString vulkanCapturePath = qEnvironmentVariable(
        "MELONPRIME_VULKAN_PRESENTER_CAPTURE");
    if (!vulkanCapturePath.isEmpty())
    {
        const bool multiWindow = qEnvironmentVariableIntValue(
            "MELONPRIME_VULKAN_PRESENTER_MULTIWINDOW") == 1;
        if (multiWindow)
            createEmuInstance();
        QTimer::singleShot(1500, qApp, [vulkanCapturePath, multiWindow] {
            MainWindow* window = emuInstances[0]
                ? emuInstances[0]->getMainWindow() : nullptr;
            auto* panel = window
                ? dynamic_cast<ScreenPanelVulkan*>(window->panel) : nullptr;
            bool saved = panel && panel->captureVulkanFrame(vulkanCapturePath);
            if (multiWindow)
            {
                MainWindow* secondWindow = emuInstances[1]
                    ? emuInstances[1]->getMainWindow() : nullptr;
                auto* secondPanel = secondWindow
                    ? dynamic_cast<ScreenPanelVulkan*>(secondWindow->panel) : nullptr;
                saved = saved && secondPanel && secondPanel->captureVulkanFrame(
                    vulkanCapturePath + ".window2.png");
            }
            qApp->exit(saved ? 0 : 3);
        });
    }
#endif

    {
        MainWindow* win = emuInstances[0]->getMainWindow();
        bool memberSyntaxUsed = false;
        const auto prepareRomPath = [&](const std::optional<QString> &romPath,
                                        const std::optional<QString> &romArchivePath) -> QStringList
        {
            if (!romPath.has_value())
                return {};

            if (romArchivePath.has_value())
                return {*romPath, *romArchivePath};

            const QStringList path = win->splitArchivePath(*romPath, true);
            if (path.size() > 1) memberSyntaxUsed = true;
            return path;
        };

        const QStringList dsfile = prepareRomPath(options->dsRomPath, options->dsRomArchivePath);
        const QStringList gbafile = prepareRomPath(options->gbaRomPath, options->gbaRomArchivePath);

        if (memberSyntaxUsed) printf("Warning: use the a.zip|b.nds format at your own risk!\n");

        win->preloadROMs(dsfile, gbafile, options->boot);

        if (options->fullscreen)
            win->toggleFullscreen();
    }

    int ret = melon.exec();

    delete options;

    // if we get here, all the existing emu instances should have been deleted already
    // but with this we make extra sure they are all deleted
    deleteAllEmuInstances();

    delete camManager[0];
    delete camManager[1];

    Config::Save();

    SDL_Quit();
    return ret;
}
