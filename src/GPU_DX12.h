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

#ifndef GPU_DX12_H
#define GPU_DX12_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <string>

#include "DX12AmdAntiLag2.h"
#include "DX12NvidiaReflex.h"
#include "GPU_Soft.h"

namespace melonDS
{

class DX12Renderer3D;

// Software 2D + DirectX 12 3D. The software engines also record structured 2D
// planes, which the DX12 compute compositor combines with the high-resolution
// 3D target. Presentation remains on the existing NativeQt/OpenGL panels so
// Custom HUD, OSD, savestates and window-layout behavior stay shared.
class DX12Renderer final : public SoftRenderer
{
public:
    explicit DX12Renderer(melonDS::NDS& nds);
    ~DX12Renderer() override;

    bool Init() override;
    void Stop() override;

    // SoftRenderer's implementations dynamic_cast Rend3D to SoftRenderer3D and
    // dereference the result unchecked, so all three must be overridden here.
    void PreSavestate() override;
    void PostSavestate() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void Start3DRendering() override;
    void VBlank() override;
    RendererOutput GetOutput() override;

    void BeginReflexFrame();
    void BeginAmdAntiLag2Frame();
    void MarkReflexInputSample();
    void EndReflexRenderPhase();
    void BeginReflexPresent();
    void EndReflexPresent();
    void FinishReflexFrame();

    bool NeedsShaderCompile() override;
    void ShaderCompileStep(int& current, int& count) override;
    [[nodiscard]] bool HasRuntimeFailure() const noexcept;
    [[nodiscard]] const std::string& GetRuntimeFailureReason() const noexcept;

    [[nodiscard]] DX12Renderer3D* GetDX12Renderer3D() noexcept;
    [[nodiscard]] const DX12Renderer3D* GetDX12Renderer3D() const noexcept;

private:
    DX12AmdAntiLag2 AmdAntiLag2;
    DX12NvidiaReflex NvidiaReflex;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // GPU_DX12_H
