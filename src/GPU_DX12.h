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

#include "GPU3D_RasterDifferential.h"
#include "GPU_Soft.h"

namespace melonDS
{

class DX12Renderer3D;

// Software 2D + DirectX 12 3D. The software engines also record structured 2D
// planes, which the DX12 compute compositor combines with the high-resolution
// 3D target. ScreenPanelDX12 performs the final shared Qt image composition
// and submits it through a native flip-model DXGI swapchain.
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
    RendererOutputLease AcquireOutputLease() override;
    void AllocCapture(u32 bank, u32 start, u32 len) override;
    CaptureSyncResult SyncVRAMCapture(
        u32 bank, u32 start, u32 len, bool complete) override;
    void InvalidateVRAMCapture(
        u32 bank,
        u32 start,
        u32 len,
        CaptureAuthorityTransitionReason reason) override;
    [[nodiscard]] NativeCaptureStateIdentity
    GetNativeCaptureStateIdentity() const noexcept override;
    [[nodiscard]] const char* GetCaptureBackendName() const noexcept override
    {
        return "DX12";
    }

    // Vendor low-latency state lives in DX12LowLatencyController, which the
    // presenter and the emulation thread reach directly. This renderer is only
    // one of that controller's event sources -- it reports render-submit
    // boundaries, because it is what submits -- and it forwards the configured
    // low-latency settings it receives through RendererSettings. It owns no
    // vendor object and publishes no Present marker.

    bool NeedsShaderCompile() override;
    void ShaderCompileStep(int& current, int& count) override;
    [[nodiscard]] bool HasRuntimeFailure() const noexcept;
    [[nodiscard]] const std::string& GetRuntimeFailureReason() const noexcept;

    [[nodiscard]] DX12Renderer3D* GetDX12Renderer3D() noexcept;
    [[nodiscard]] const DX12Renderer3D* GetDX12Renderer3D() const noexcept;

private:
    [[nodiscard]] bool CanUseNativeGPU2DForFrame() const noexcept override;

    // Developer-only 3D oracle comparison. Called from both publication paths
    // because it compares the 3D renderers, not the composed 2D frame.
    void CompareRasterDifferentialFrame();

    // The controller asks for a queue-idle window before an Intel XeLL sleep
    // transition. Passing it as a hook keeps the dependency pointing the right
    // way: the controller never learns what a Renderer3D is.
    static bool WaitForQueueIdleHook(void* userData) noexcept;

    std::unique_ptr<Renderer3D> DifferentialReference;
    RasterDifferential::State DifferentialState;
    bool NativeGPU2DAnnounced = false;
    bool NativeGPU2DFallbackAnnounced = false;
    bool NativeGPU2DStartupFallbackAnnounced = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // GPU_DX12_H
