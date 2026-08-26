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

#include <mutex>
#include <string>
#include "DX12Common.h"
#include "Platform.h"

namespace melonDS
{

// Runtime resolution of d3d12.dll / dxgi.dll / d3dcompiler_47.dll, plus the
// shared HRESULT failure log. Nothing here creates or owns a device: this is
// what has to succeed before a device can be attempted at all, which is why
// it is no longer part of the device owner's translation unit.
//
// The contract lives in DX12Common.h.

namespace DX12
{

namespace
{

std::once_flag gLoaderOnce;
EntryPoints gEntryPoints{};
std::string gLoaderFailure;

void ResolveEntryPoints()
{
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12)
    {
        gLoaderFailure = "d3d12.dll was not found";
        return;
    }

    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi)
    {
        gLoaderFailure = "dxgi.dll was not found";
        return;
    }

    gEntryPoints.D3D12CreateDevice =
        reinterpret_cast<EntryPoints::PFN_D3D12CreateDevice>(
            reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12CreateDevice")));
    gEntryPoints.D3D12GetDebugInterface =
        reinterpret_cast<EntryPoints::PFN_D3D12GetDebugInterface>(
            reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12GetDebugInterface")));
    gEntryPoints.D3D12SerializeRootSignature =
        reinterpret_cast<EntryPoints::PFN_D3D12SerializeRootSignature>(
            reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12SerializeRootSignature")));
    gEntryPoints.CreateDXGIFactory2 =
        reinterpret_cast<EntryPoints::PFN_CreateDXGIFactory2>(
            reinterpret_cast<void*>(GetProcAddress(dxgi, "CreateDXGIFactory2")));

    if (!gEntryPoints.IsCoreReady())
    {
        gLoaderFailure = "d3d12.dll/dxgi.dll are missing required entry points";
        return;
    }

    // The compute renderer uses committed DXBC. The native presenter still
    // compiles its small vertex/pixel shader during initialization.
    // d3dcompiler_47.dll ships with every Windows version that has D3D12, but a
    // stripped system could still be missing it, so this stays a separate,
    // non-fatal-at-load failure.
    static const char* const kCompilerNames[] = { "d3dcompiler_47.dll", "d3dcompiler_46.dll" };
    for (const char* name : kCompilerNames)
    {
        HMODULE compiler = LoadLibraryA(name);
        if (!compiler) continue;

        gEntryPoints.D3DCompile =
            reinterpret_cast<EntryPoints::PFN_D3DCompile>(
                reinterpret_cast<void*>(GetProcAddress(compiler, "D3DCompile")));
        if (gEntryPoints.D3DCompile) break;
    }

    if (!gEntryPoints.IsShaderCompilerReady())
        gLoaderFailure = "d3dcompiler_47.dll was not found";
}

} // namespace

const EntryPoints& LoadEntryPoints()
{
    std::call_once(gLoaderOnce, ResolveEntryPoints);
    return gEntryPoints;
}

const char* LoaderFailureReason()
{
    std::call_once(gLoaderOnce, ResolveEntryPoints);
    return gLoaderFailure.c_str();
}

bool Fail(const char* context, HRESULT hr)
{
    char* message = nullptr;
    const DWORD len = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        static_cast<DWORD>(hr),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&message),
        0,
        nullptr);

    Platform::Log(
        Platform::LogLevel::Error,
        "DX12: %s failed (hr=0x%08lX%s%s)\n",
        context,
        static_cast<unsigned long>(hr),
        (len && message) ? ": " : "",
        (len && message) ? message : "");

    if (message) LocalFree(message);
    return false;
}

} // namespace DX12

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
