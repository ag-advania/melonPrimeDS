/*
    Copyright 2016-2026 melonDS team

    Presentation-scheduling arithmetic for the vendor-neutral Vulkan pacer.

    This header deliberately contains no Vulkan handles, no dispatch and no Qt.
    Everything here is the part of target-time presentation that can be wrong in
    a way a static audit cannot see -- sequence accounting, baseline rebasing,
    staleness and overflow -- so it is kept separately testable on a host with
    no Vulkan driver at all (tools/testing/vulkan-present-timing-tests.cpp).
*/

#ifndef VULKAN_PRESENT_TIMING_MODEL_H
#define VULKAN_PRESENT_TIMING_MODEL_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <cstddef>
#include <limits>

#include "types.h"

namespace melonDS
{

// What a single presentation-timing report did to the scheduling baseline.
enum class VulkanPresentFeedbackResult
{
    // The report carried no usable baseline: unknown present ID, no timestamp
    // for the target stage, or an out-of-order result older than the current
    // baseline. The previous baseline is kept.
    Ignored,
    // The baseline was rebased onto this report.
    Accepted,
    // The driver answered in a different time domain than the one the target
    // times are being computed in. Continuing would schedule against a clock
    // that is not the clock the timestamps came from, so the baseline is
    // dropped and the caller must re-enumerate the swapchain's time domains.
    DomainMismatch,
};

// Presentation scheduling state for one swapchain.
//
// Two sequences are tracked on purpose:
//
//   * the logical present ID, which is the Reflex frame ID when Reflex is
//     running. It counts emulation frames and therefore skips values whenever
//     a frame is simulated but not presented.
//   * the presentation sequence, which counts only presents the presentation
//     engine actually accepted.
//
// Target times are "baseline + N frame intervals", and N must be a count of
// presents, not a count of emulation frames. Conflating the two makes every
// dropped frame permanently shift the target cadence.
class VulkanPresentTimingModel
{
public:
    // Enough history to cover the 16-entry timing-results queue plus the
    // frames in flight while those results are still being drained. Fixed size:
    // this is touched on the present path and must never allocate.
    static constexpr std::size_t HistorySize = 32;

    // Extrapolating more than this many frames past the newest feedback means
    // the driver stopped reporting; the projected time is guesswork by then.
    static constexpr u64 MaxProjectedFrames = 600;

    struct Record
    {
        u64 LogicalId = 0;
        u64 Sequence = 0;
    };

    // Full reset. Used for swapchain creation/destruction, where neither the
    // sequence numbering nor any past timestamp survives.
    void Reset() noexcept
    {
        CommittedSequence = 0;
        PendingSequence = 0;
        PendingLogicalId = 0;
        History = {};
        HistoryCursor = 0;
        InvalidateBaseline();
    }

    // Drops the scheduling baseline but keeps the sequence numbering. Used when
    // the timing model is still valid but its reference point is not: a time
    // domain change, a timing-property change, or a domain mismatch.
    void InvalidateBaseline() noexcept
    {
        BaselineValid = false;
        BaselineLogicalId = 0;
        BaselineSequence = 0;
        BaselineStageTimeNs = 0;
    }

    // Declares the clock that target times are expressed in. A change drops the
    // baseline, because a timestamp from the old domain cannot be projected
    // forward on the new one.
    void SetTimeDomain(s32 timeDomain, u64 timeDomainId) noexcept
    {
        if (DomainSelected && timeDomain == TimeDomain && timeDomainId == TimeDomainId)
            return;
        TimeDomain = timeDomain;
        TimeDomainId = timeDomainId;
        DomainSelected = true;
        InvalidateBaseline();
    }

    void ClearTimeDomain() noexcept
    {
        DomainSelected = false;
        TimeDomain = 0;
        TimeDomainId = 0;
        InvalidateBaseline();
    }

    [[nodiscard]] bool HasTimeDomain() const noexcept { return DomainSelected; }
    [[nodiscard]] s32 GetTimeDomain() const noexcept { return TimeDomain; }
    [[nodiscard]] u64 GetTimeDomainId() const noexcept { return TimeDomainId; }

    // Reserves the presentation sequence a present being prepared right now
    // would occupy. The sequence is not committed here: a present the engine
    // rejects never happened, and a retry of the same image must reuse the same
    // number rather than leaving a hole in the cadence.
    u64 BeginPresent(u64 logicalId) noexcept
    {
        PendingLogicalId = logicalId;
        PendingSequence = CommittedSequence + 1;
        return PendingSequence;
    }

    // The presentation engine accepted the present (VK_SUCCESS or
    // VK_SUBOPTIMAL_KHR). Only now does the sequence advance and become
    // resolvable from the logical ID that timing feedback will report.
    void CommitPresent() noexcept
    {
        if (PendingSequence == 0)
            return;
        CommittedSequence = PendingSequence;
        if (PendingLogicalId != 0)
        {
            History[HistoryCursor] = Record{PendingLogicalId, CommittedSequence};
            HistoryCursor = (HistoryCursor + 1) % HistorySize;
        }
        PendingSequence = 0;
        PendingLogicalId = 0;
    }

    // The present was not accepted. The reserved sequence is released so the
    // next attempt reuses it.
    void AbandonPresent() noexcept
    {
        PendingSequence = 0;
        PendingLogicalId = 0;
    }

    [[nodiscard]] u64 GetPendingSequence() const noexcept { return PendingSequence; }
    [[nodiscard]] u64 GetCommittedSequence() const noexcept { return CommittedSequence; }

    [[nodiscard]] bool FindSequence(u64 logicalId, u64& sequence) const noexcept
    {
        if (logicalId == 0)
            return false;
        for (const Record& record : History)
        {
            if (record.LogicalId == logicalId && record.Sequence != 0)
            {
                sequence = record.Sequence;
                return true;
            }
        }
        return false;
    }

    // Rebases the scheduling baseline onto one presentation-timing report.
    //
    // Rebasing on every complete report (rather than keeping the first one
    // forever) is what keeps rounding error and clock drift from accumulating:
    // each target is at most a few frames of extrapolation away from a real
    // measured presentation time.
    VulkanPresentFeedbackResult RecordFeedback(
        u64 logicalId, u64 stageTimeNs, s32 timeDomain, u64 timeDomainId) noexcept
    {
        if (logicalId == 0 || stageTimeNs == 0)
            return VulkanPresentFeedbackResult::Ignored;

        if (DomainSelected && (timeDomain != TimeDomain || timeDomainId != TimeDomainId))
        {
            InvalidateBaseline();
            return VulkanPresentFeedbackResult::DomainMismatch;
        }

        u64 sequence = 0;
        if (!FindSequence(logicalId, sequence))
            return VulkanPresentFeedbackResult::Ignored;

        // Out-of-order results are allowed by the extension. An older report
        // must not drag the baseline backwards.
        if (BaselineValid && sequence <= BaselineSequence)
            return VulkanPresentFeedbackResult::Ignored;

        BaselineLogicalId = logicalId;
        BaselineSequence = sequence;
        BaselineStageTimeNs = stageTimeNs;
        BaselineValid = true;
        return VulkanPresentFeedbackResult::Accepted;
    }

    // The absolute presentation time to request for `sequence`, or 0 when no
    // target may be requested. Zero is always a safe answer: it means "present
    // as usual", which is exactly the telemetry-only behaviour.
    [[nodiscard]] u64 ComputeTargetTime(u64 sequence, u64 frameIntervalNs) const noexcept
    {
        if (!BaselineValid || frameIntervalNs == 0 || sequence == 0)
            return 0;
        if (sequence <= BaselineSequence)
            return 0;

        const u64 delta = sequence - BaselineSequence;
        if (delta > MaxProjectedFrames)
            return 0;
        if (delta > (std::numeric_limits<u64>::max)() / frameIntervalNs)
            return 0;

        const u64 offset = delta * frameIntervalNs;
        if (offset > (std::numeric_limits<u64>::max)() - BaselineStageTimeNs)
            return 0;
        return BaselineStageTimeNs + offset;
    }

    // True when the baseline exists but is too far behind `sequence` to project
    // from. The caller reports this as its fallback reason and keeps presenting
    // untimed until fresh feedback arrives.
    [[nodiscard]] bool IsBaselineStale(u64 sequence) const noexcept
    {
        return BaselineValid
            && (sequence <= BaselineSequence || sequence - BaselineSequence > MaxProjectedFrames);
    }

    [[nodiscard]] bool HasBaseline() const noexcept { return BaselineValid; }
    [[nodiscard]] u64 GetBaselineLogicalId() const noexcept { return BaselineLogicalId; }
    [[nodiscard]] u64 GetBaselineSequence() const noexcept { return BaselineSequence; }
    [[nodiscard]] u64 GetBaselineStageTimeNs() const noexcept { return BaselineStageTimeNs; }

private:
    u64 CommittedSequence = 0;
    u64 PendingSequence = 0;
    u64 PendingLogicalId = 0;

    std::array<Record, HistorySize> History{};
    std::size_t HistoryCursor = 0;

    bool DomainSelected = false;
    s32 TimeDomain = 0;
    u64 TimeDomainId = 0;

    bool BaselineValid = false;
    u64 BaselineLogicalId = 0;
    u64 BaselineSequence = 0;
    u64 BaselineStageTimeNs = 0;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_PRESENT_TIMING_MODEL_H
