/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef DX12_AMD_ANTI_LAG_2_H
#define DX12_AMD_ANTI_LAG_2_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <string>

#include "DX12Common.h"

namespace melonDS
{

struct DX12AmdAntiLag2Support
{
    bool Available = false;
    std::string Reason;
};

// Per-renderer wrapper around AMD's public Anti-Lag 2 DX12 ABI. The driver
// interface is acquired from the active D3D12 device and released before that
// device is destroyed. All Update calls remain on the emulation thread.
class DX12AmdAntiLag2
{
public:
    static DX12AmdAntiLag2Support Probe(ID3D12Device* device, u32 vendorId);

    bool Initialize(ID3D12Device* device, u32 vendorId);
    void Shutdown() noexcept;

    void SetEnabled(bool enabled) noexcept { Enabled = enabled; }
    void BeginFrame();

    [[nodiscard]] bool IsAvailable() const noexcept { return Available; }
    [[nodiscard]] bool IsActive() const noexcept
    {
        return Available && StateApplied && AppliedEnabled;
    }
    [[nodiscard]] const std::string& GetUnavailableReason() const noexcept { return UnavailableReason; }

private:
    struct AmdExtAntiLagApi;

    bool ApplyState();
    void DisableForRuntimeFailure(const char* operation, HRESULT status);

    AmdExtAntiLagApi* Api = nullptr;
    std::string UnavailableReason;
    bool Available = false;
    bool Enabled = false;
    bool AppliedEnabled = false;
    bool StateApplied = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_AMD_ANTI_LAG_2_H
