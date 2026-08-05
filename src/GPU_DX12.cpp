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

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "GPU_DX12.h"

#include "GPU3D_DX12.h"
#include "NDS.h"
#include "Platform.h"

namespace melonDS
{

namespace
{

bool ScreenUsesDominant3D(const u32* control)
{
    if (!control)
        return false;

    constexpr u32 sampleColumns = 32u;
    constexpr u32 sampleRows = 24u;
    u32 samplesUsing3D = 0u;
    for (u32 sampleY = 0; sampleY < sampleRows; ++sampleY)
    {
        const u32 y = sampleY * (192u / sampleRows);
        for (u32 sampleX = 0; sampleX < sampleColumns; ++sampleX)
        {
            const u32 x = sampleX * (256u / sampleColumns);
            const u32 controlAlpha = control[y * 256u + x] >> 24u;
            if ((controlAlpha & 0x40u) != 0u)
                ++samplesUsing3D;
        }
    }

    return samplesUsing3D > (sampleColumns * sampleRows) / 2u;
}

} // namespace

DX12Renderer::DX12Renderer(melonDS::NDS& nds)
    : SoftRenderer(nds)
{
    // Replaces the SoftRenderer3D the base constructor installed. A null result
    // leaves Rend3D empty, which Init() reports as a failure so the frontend can
    // fall back to Software.
    if (auto renderer3D = DX12Renderer3D::New(GPU.GPU3D))
        Rend3D = std::move(renderer3D);
    else
        Rend3D.reset();
}

DX12Renderer::~DX12Renderer() = default;

bool DX12Renderer::Init()
{
    if (!Rend3D || !Rend3D->Init())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12 renderer init failed stage=3D-device actual=Software\n");
        return false;
    }

    Platform::Log(
        Platform::LogLevel::Info,
        "DX12 renderer init succeeded requested=DX12 actual=DX12 presentation=high-resolution-composed\n");
    return true;
}

void DX12Renderer::Stop()
{
    if (auto* dx12 = GetDX12Renderer3D())
        dx12->Stop();
    SoftRenderer::Stop();
}

void DX12Renderer::PreSavestate()
{
    // The DX12 renderer owns its own GPU synchronization and has no software
    // render thread to suspend, so a savestate needs nothing here.
}

void DX12Renderer::PostSavestate()
{
}

void DX12Renderer::SetRenderSettings(RendererSettings& settings)
{
    if (auto* dx12 = GetDX12Renderer3D())
        dx12->SetRenderSettings(settings.ScaleFactor, settings.BetterPolygons, settings.HiresCoordinates);
}

RendererOutput DX12Renderer::GetOutput()
{
    auto* dx12 = GetDX12Renderer3D();
    StructuredVulkanFrameView view{};
    if (!dx12 || !GetStructuredVulkanFrame(view) || !view.Valid)
        return {};

    const std::array<const u32*, 6> planes = {
        view.Plane[0][0],
        view.Plane[0][1],
        view.Plane[0][2],
        view.Plane[1][0],
        view.Plane[1][1],
        view.Plane[1][2],
    };
    const std::array<const u32*, 2> lineMeta = {
        view.LineMeta[0],
        view.LineMeta[1],
    };

    const bool bothScreensUseDominant3D =
        ScreenUsesDominant3D(view.Plane[0][2])
        && ScreenUsesDominant3D(view.Plane[1][2]);
    const bool composed = dx12->ComposeStructuredOutput(
        planes,
        lineMeta,
        view.Generation,
        GPU.ScreenSwap,
        bothScreensUseDominant3D);
    const u32* top = dx12->GetComposedScreen(0);
    const u32* bottom = dx12->GetComposedScreen(1);
    if ((!composed && (!top || !bottom)) || !top || !bottom)
        return {};

    return RendererOutput::CpuBgra(
        const_cast<u32*>(top),
        const_cast<u32*>(bottom),
        dx12->GetComposedWidth(),
        dx12->GetComposedHeight());
}

bool DX12Renderer::NeedsShaderCompile()
{
    return Rend3D && Rend3D->NeedsShaderCompile();
}

void DX12Renderer::ShaderCompileStep(int& current, int& count)
{
    if (Rend3D)
        Rend3D->ShaderCompileStep(current, count);
}

DX12Renderer3D* DX12Renderer::GetDX12Renderer3D() noexcept
{
    return dynamic_cast<DX12Renderer3D*>(Rend3D.get());
}

const DX12Renderer3D* DX12Renderer::GetDX12Renderer3D() const noexcept
{
    return dynamic_cast<const DX12Renderer3D*>(Rend3D.get());
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
