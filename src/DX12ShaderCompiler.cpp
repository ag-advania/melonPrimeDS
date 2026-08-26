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

#include <d3dcompiler.h>

#include <string>
#include <vector>
#include "DX12ShaderCompiler.h"
#include "Platform.h"

namespace melonDS
{

DX12::ComPtr<ID3DBlob> DX12ShaderCompiler::Compile(
    const std::string& source,
    const char* entryPoint,
    const char* target,
    const std::vector<std::pair<std::string, std::string>>& defines,
    const char* debugName)
{
    const auto& entry = DX12::LoadEntryPoints();
    DX12::ComPtr<ID3DBlob> result;

    if (!entry.IsShaderCompilerReady())
        return result;

    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(defines.size() + 1);
    for (const auto& def : defines)
        macros.push_back(D3D_SHADER_MACRO{ def.first.c_str(), def.second.c_str() });
    macros.push_back(D3D_SHADER_MACRO{ nullptr, nullptr });

    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS;
#if !defined(NDEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_ENABLE_STRICTNESS;
#endif

    DX12::ComPtr<ID3DBlob> errors;
    const HRESULT hr = entry.D3DCompile(
        source.data(),
        source.size(),
        debugName,
        macros.data(),
        nullptr,
        entryPoint,
        target,
        flags,
        0,
        result.ReleaseAndGetAddressOf(),
        errors.ReleaseAndGetAddressOf());

    if (FAILED(hr))
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12: shader \"%s\" (%s/%s) failed to compile: %s\n",
            debugName ? debugName : "?",
            entryPoint,
            target,
            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no compiler output");
        result.Reset();
        return result;
    }

    if (errors && errors->GetBufferSize() > 1)
    {
        Platform::Log(
            Platform::LogLevel::Warn,
            "DX12: shader \"%s\" compiled with warnings: %s\n",
            debugName ? debugName : "?",
            static_cast<const char*>(errors->GetBufferPointer()));
    }

    return result;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
