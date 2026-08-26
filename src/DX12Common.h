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

#ifndef DX12_COMMON_H
#define DX12_COMMON_H

// MelonPrime DirectX 12 backend -- shared low-level plumbing.
//
// Everything in this header is a hard build gate: outside a Windows build with
// MELONPRIME_ENABLE_DX12 the translation unit collapses to nothing, so no DX12
// type, symbol or library dependency can leak into the OpenGL / Software /
// Vulkan / Metal paths.
#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <cstddef>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include "types.h"

namespace melonDS::DX12
{

// Minimal COM smart pointer. MinGW-w64 ships no <wrl/client.h>, and the
// backend only needs AddRef/Release ownership plus the IID_PPV_ARGS-style
// out-parameter idiom.
template <typename T>
class ComPtr
{
public:
    ComPtr() noexcept = default;
    ComPtr(std::nullptr_t) noexcept {}

    ComPtr(const ComPtr& other) noexcept : Ptr(other.Ptr)
    {
        if (Ptr) Ptr->AddRef();
    }

    ComPtr(ComPtr&& other) noexcept : Ptr(other.Ptr)
    {
        other.Ptr = nullptr;
    }

    ~ComPtr() noexcept
    {
        Reset();
    }

    ComPtr& operator=(const ComPtr& other) noexcept
    {
        if (this != &other)
        {
            if (other.Ptr) other.Ptr->AddRef();
            if (Ptr) Ptr->Release();
            Ptr = other.Ptr;
        }
        return *this;
    }

    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != &other)
        {
            if (Ptr) Ptr->Release();
            Ptr = other.Ptr;
            other.Ptr = nullptr;
        }
        return *this;
    }

    ComPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    void Reset() noexcept
    {
        if (Ptr)
        {
            T* dead = Ptr;
            Ptr = nullptr;
            dead->Release();
        }
    }

    // Releases any current object and hands out the raw slot, so the pointer
    // can be passed straight to IID_PPV_ARGS()/CreateXXX() out-parameters.
    T** ReleaseAndGetAddressOf() noexcept
    {
        Reset();
        return &Ptr;
    }

    [[nodiscard]] T* Get() const noexcept { return Ptr; }
    T* operator->() const noexcept { return Ptr; }
    explicit operator bool() const noexcept { return Ptr != nullptr; }

    [[nodiscard]] T* Detach() noexcept
    {
        T* out = Ptr;
        Ptr = nullptr;
        return out;
    }

    void Attach(T* raw) noexcept
    {
        Reset();
        Ptr = raw;
    }

private:
    T* Ptr = nullptr;
};

// The DX12 entry points are resolved at runtime instead of being linked, so a
// machine without d3d12.dll (or without a D3D12-capable driver) still starts
// melonPrimeDS and simply reports the DX12 renderer as unavailable.
struct EntryPoints
{
    using PFN_D3D12CreateDevice = HRESULT (WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    using PFN_D3D12GetDebugInterface = HRESULT (WINAPI*)(REFIID, void**);
    using PFN_D3D12SerializeRootSignature = HRESULT (WINAPI*)(
        const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    using PFN_CreateDXGIFactory2 = HRESULT (WINAPI*)(UINT, REFIID, void**);
    using PFN_D3DCompile = HRESULT (WINAPI*)(
        LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
        LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

    PFN_D3D12CreateDevice D3D12CreateDevice = nullptr;
    PFN_D3D12GetDebugInterface D3D12GetDebugInterface = nullptr;
    PFN_D3D12SerializeRootSignature D3D12SerializeRootSignature = nullptr;
    PFN_CreateDXGIFactory2 CreateDXGIFactory2 = nullptr;
    PFN_D3DCompile D3DCompile = nullptr;

    [[nodiscard]] bool IsCoreReady() const noexcept
    {
        return D3D12CreateDevice != nullptr
            && D3D12SerializeRootSignature != nullptr
            && CreateDXGIFactory2 != nullptr;
    }

    [[nodiscard]] bool IsShaderCompilerReady() const noexcept
    {
        return D3DCompile != nullptr;
    }
};

// Loads d3d12.dll / dxgi.dll / d3dcompiler_47.dll once per process and returns
// the resolved entry points. Safe to call from any thread; the modules are
// intentionally never freed (unloading d3d12.dll while a device is alive is
// not supported by the runtime).
const EntryPoints& LoadEntryPoints();

// Human-readable reason the loader failed, or an empty string once every
// required module resolved.
const char* LoaderFailureReason();

// Logs `context` plus the HRESULT (and its system message when available) at
// Error level, then returns false so call sites can `return DX12::Fail(...)`.
bool Fail(const char* context, HRESULT hr);

// Small arithmetic shared by every DX12 dispatch and allocation site.
constexpr u32 DivRoundUp(u32 value, u32 divisor) noexcept
{
    return (value + divisor - 1) / divisor;
}

constexpr u64 AlignUp(u64 value, u64 alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace melonDS::DX12

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_COMMON_H
