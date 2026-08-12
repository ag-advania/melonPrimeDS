/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef DX12_LOW_LATENCY_PACING_H
#define DX12_LOW_LATENCY_PACING_H

namespace melonDS
{

enum class DX12LowLatencyPacingAuthority : int
{
    GenericHost = 0,
    NvidiaReflex,
    AmdAntiLag2,
    IntelXeLL,
};

// Hardware-validation experiments. Compatibility remains the release and
// developer default until Intel Arc + XeSS Inspector/GPUView evidence exists.
enum class DX12IntelXeLLPacingPolicy : int
{
    Compatibility = 0,
    BypassPresentWait = 1,
    BypassHostLimiter = 2,
    XeLLFrameCap = 3,
    IntelRecommended = 4,
    Count,
};

struct DX12LowLatencyPacingDecision
{
    DX12LowLatencyPacingAuthority Authority =
        DX12LowLatencyPacingAuthority::GenericHost;
    bool BypassHostLimiter = false;
    bool BypassPresentWait = false;
    bool XeLLOwnsFrameCap = false;
};

constexpr DX12IntelXeLLPacingPolicy DX12IntelXeLLPacingPolicyFromConfig(int value)
{
    return value >= 0
        && value < static_cast<int>(DX12IntelXeLLPacingPolicy::Count)
        ? static_cast<DX12IntelXeLLPacingPolicy>(value)
        : DX12IntelXeLLPacingPolicy::Compatibility;
}

constexpr DX12LowLatencyPacingDecision ResolveDX12LowLatencyPacing(
    bool nvidiaReflexActive,
    bool amdAntiLag2Active,
    bool intelXeLLActive,
    DX12IntelXeLLPacingPolicy intelPolicy)
{
    if (nvidiaReflexActive)
    {
        // Reflex already performs its driver-directed sleep immediately before
        // late input. Keep the host limiter as the exact FPS cap, but do not
        // layer the DXGI frame-latency wait on top of the Reflex wait.
        return {DX12LowLatencyPacingAuthority::NvidiaReflex, false, true, false};
    }
    if (amdAntiLag2Active)
        return {DX12LowLatencyPacingAuthority::AmdAntiLag2, false, false, false};
    if (!intelXeLLActive)
        return {};

    DX12LowLatencyPacingDecision decision;
    decision.Authority = DX12LowLatencyPacingAuthority::IntelXeLL;
    switch (intelPolicy)
    {
    case DX12IntelXeLLPacingPolicy::BypassPresentWait:
        decision.BypassPresentWait = true;
        break;
    case DX12IntelXeLLPacingPolicy::BypassHostLimiter:
        decision.BypassHostLimiter = true;
        break;
    case DX12IntelXeLLPacingPolicy::XeLLFrameCap:
        decision.BypassHostLimiter = true;
        decision.XeLLOwnsFrameCap = true;
        break;
    case DX12IntelXeLLPacingPolicy::IntelRecommended:
        decision.BypassHostLimiter = true;
        decision.BypassPresentWait = true;
        decision.XeLLOwnsFrameCap = true;
        break;
    case DX12IntelXeLLPacingPolicy::Compatibility:
    case DX12IntelXeLLPacingPolicy::Count:
        break;
    }
    return decision;
}

constexpr bool ShouldBypassDX12HostLimiter(
    const DX12LowLatencyPacingDecision& decision,
    bool normalSpeed)
{
    // A pure wait-bypass experiment is limited to normal speed. When XeLL
    // owns the frame cap, fast-forward/slow-motion remain safe because their
    // changed target interval is transferred to minimumIntervalUs instead.
    return decision.BypassHostLimiter
        && (normalSpeed || decision.XeLLOwnsFrameCap);
}

constexpr const char* DX12LowLatencyPacingAuthorityName(
    DX12LowLatencyPacingAuthority authority)
{
    switch (authority)
    {
    case DX12LowLatencyPacingAuthority::NvidiaReflex: return "NvidiaReflex";
    case DX12LowLatencyPacingAuthority::AmdAntiLag2: return "AmdAntiLag2";
    case DX12LowLatencyPacingAuthority::IntelXeLL: return "IntelXeLL";
    case DX12LowLatencyPacingAuthority::GenericHost: return "GenericHost";
    }
    return "GenericHost";
}

constexpr const char* DX12IntelXeLLPacingPolicyName(
    DX12IntelXeLLPacingPolicy policy)
{
    switch (policy)
    {
    case DX12IntelXeLLPacingPolicy::Compatibility: return "Compatibility";
    case DX12IntelXeLLPacingPolicy::BypassPresentWait: return "BypassPresentWait";
    case DX12IntelXeLLPacingPolicy::BypassHostLimiter: return "BypassHostLimiter";
    case DX12IntelXeLLPacingPolicy::XeLLFrameCap: return "XeLLFrameCap";
    case DX12IntelXeLLPacingPolicy::IntelRecommended: return "IntelRecommended";
    case DX12IntelXeLLPacingPolicy::Count: break;
    }
    return "Compatibility";
}

} // namespace melonDS

#endif // DX12_LOW_LATENCY_PACING_H
