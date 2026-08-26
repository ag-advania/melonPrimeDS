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

#ifndef DX12_SHADER_COMPILER_H
#define DX12_SHADER_COMPILER_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <string>
#include <utility>
#include <vector>
#include "DX12Common.h"

namespace melonDS
{

// Runtime HLSL compilation. Stateless: everything it needs comes from the
// process-wide entry points and the arguments, so it is a namespace of
// functions rather than an object with a lifetime.
//
// Separate from the device owner because compile flags, macro assembly and
// compiler diagnostics change for reasons that have nothing to do with which
// adapter was selected.
class DX12ShaderCompiler
{
public:
    // Compiles HLSL with the runtime d3dcompiler_47.dll. `target` is a shader
    // model string such as "cs_5_1". Returns nullptr and logs on failure.
    static DX12::ComPtr<ID3DBlob> Compile(
        const std::string& source,
        const char* entryPoint,
        const char* target,
        const std::vector<std::pair<std::string, std::string>>& defines,
        const char* debugName);
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_SHADER_COMPILER_H
