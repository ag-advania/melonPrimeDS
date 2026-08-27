/*
    Deterministic fake-RAM coverage for the Stage Matrix loaded-state guard.

    This target calls the production templated implementation through its
    callback seam. It exercises the real sentinel, readiness signature, full
    guard, retry/backoff, cell reconciliation, and lifecycle invalidation
    rather than maintaining a second copy of those decisions.
*/

#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "MelonPrimePatchExpandStageMatrix.h"
#include "MelonPrimePatchExpandStageMatrixTesting.h"
#include "MelonPrimePatchState.h"

namespace {

int gFailures = 0;

void Check(bool condition, const char* what)
{
    if (condition)
        return;
    std::printf("FAIL: %s\n", what);
    ++gFailures;
}

struct Write {
    uint32_t address = 0;
    uint8_t value = 0;
};

struct FakeRam {
    // The production signature is intentionally duplicated as test fixture
    // data: the callback fake must distinguish a valid matrix prefix from a
    // partially copied block without constructing an NDS instance.
    static constexpr std::array<uint8_t, 32> kPrefix = {
        0x01u, 0x00u, 0x00u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x01u,
        0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x01u, 0x01u, 0x01u,
    };

    bool codeReady = true;
    bool strictReady = true;
    uint8_t romGroupIndex = 0;
    uint32_t matrixBase = 0;
    uint32_t countFunction = 0;
    uint32_t compatibilityFunction = 0;
    uint32_t countLiteral = 0;
    uint32_t checkLiteral = 0;

    bool cheapProbeActive = false;
    uint32_t cheapRead32Ordinal = 0;
    bool strictMode = false;
    bool strictFailed = false;
    int fullValidationAttempts = 0;
    int totalReads = 0;
    int writeCount = 0;
    std::vector<Write> writes;

    uint32_t ExpectedCountPrologue() const
    {
        return romGroupIndex == 6u ? 0xE92D40F8u : 0xE92D40F0u;
    }

    static uint32_t PreludeWord(uint32_t index)
    {
        static constexpr uint32_t kPrelude[10] = {
            0x00000014u, 0x00000019u, 0x0000001Eu, 0x00000028u, 0x00000032u,
            0x0000003Cu, 0x00000046u, 0x00000050u, 0x0000005Au, 0x00000064u,
        };
        return kPrelude[index];
    }

    uint8_t Read8(uint32_t address)
    {
        ++totalReads;
        if (matrixBase == 0u)
            matrixBase = address;

        if (strictMode && !strictFailed)
        {
            if (address >= matrixBase && address < matrixBase + 32u)
                return kPrefix[address - matrixBase];
        }
        else if (address == matrixBase)
        {
            // Every non-strict prefix-byte read begins a new cheap probe.
            cheapProbeActive = true;
            cheapRead32Ordinal = 0u;
        }

        if (address >= matrixBase && address < matrixBase + 32u)
            return kPrefix[address - matrixBase];

        for (auto it = writes.rbegin(); it != writes.rend(); ++it)
        {
            if (it->address == address)
                return it->value;
        }
        return 0u;
    }

    uint32_t Read32(uint32_t address)
    {
        ++totalReads;
        if (matrixBase == 0u)
            matrixBase = address + 0x28u;

        const uint32_t preludeBase = matrixBase - 0x28u;
        if (address >= preludeBase && address < preludeBase + 40u
            && ((address - preludeBase) % 4u) == 0u)
        {
            const uint32_t index = (address - preludeBase) / 4u;
            if (cheapProbeActive)
            {
                ++cheapRead32Ordinal;
                if (cheapRead32Ordinal == 1u)
                    return PreludeWord(0u);
                return 0u;
            }

            if (!strictMode)
            {
                strictMode = true;
                strictFailed = !strictReady;
                ++fullValidationAttempts;
            }
            return strictReady ? PreludeWord(index) : 0u;
        }

        if (cheapProbeActive)
        {
            const uint32_t ordinal = ++cheapRead32Ordinal;
            uint32_t expected = 0u;
            uint32_t* discoveredAddress = nullptr;
            switch (ordinal)
            {
            case 2u:
                expected = ExpectedCountPrologue();
                discoveredAddress = &countFunction;
                break;
            case 3u:
                expected = 0xE92D4070u;
                discoveredAddress = &compatibilityFunction;
                break;
            case 4u:
                expected = matrixBase;
                discoveredAddress = &countLiteral;
                break;
            case 5u:
                expected = matrixBase;
                discoveredAddress = &checkLiteral;
                cheapProbeActive = false;
                break;
            default:
                return 0u;
            }
            if (discoveredAddress && *discoveredAddress == 0u)
                *discoveredAddress = address;
            return codeReady ? expected : 0u;
        }

        if (strictMode)
        {
            if (address == countFunction)
                return strictReady ? ExpectedCountPrologue() : 0u;
            if (address == compatibilityFunction)
                return strictReady ? 0xE92D4070u : 0u;
            if (address == countLiteral || address == checkLiteral)
            {
                if (address == checkLiteral)
                    strictMode = false;
                return strictReady ? matrixBase : 0u;
            }
        }
        return 0u;
    }

    void Write8(uint32_t address, uint8_t value)
    {
        ++writeCount;
        writes.push_back({ address, value });
    }
};

uint8_t Read8(void* context, uint32_t address)
{
    return static_cast<FakeRam*>(context)->Read8(address);
}

uint32_t Read32(void* context, uint32_t address)
{
    return static_cast<FakeRam*>(context)->Read32(address);
}

void Write8(void* context, uint32_t address, uint8_t value)
{
    static_cast<FakeRam*>(context)->Write8(address, value);
}

MelonPrime::StageMatrixTestMemory Bind(FakeRam& ram)
{
    return {
        &ram,
        &Read8,
        &Read32,
        &Write8,
    };
}

void RunDelayedCompleteLoad()
{
    FakeRam ram;
    ram.codeReady = false;
    MelonPrime::MelonPrimePatchState state;
    auto memory = Bind(ram);

    MelonPrime::ExpandStageMatrix_ApplyIfLoadedForTesting(
        state, memory, true, false, 0u);
    Check(
        state.expandStageMatrix.status
            == MelonPrime::MelonPrimePatchState::ExpandStageMatrixStatus::WaitingForLoad,
        "partial candidate waits for a later validation");
    Check(ram.writeCount == 0, "partial candidate never writes cells");
    Check(ram.fullValidationAttempts == 0, "partial readiness skips the full guard");

    ram.codeReady = true;
    MelonPrime::ExpandStageMatrix_ApplyIfLoadedForTesting(
        state, memory, true, false, 0u);
    Check(ram.writeCount == 0, "retry cooldown delays the next guest read");
    MelonPrime::ExpandStageMatrix_ApplyIfLoadedForTesting(
        state, memory, true, false, 0u);
    Check(
        state.expandStageMatrix.status
            == MelonPrime::MelonPrimePatchState::ExpandStageMatrixStatus::Verified,
        "delayed complete candidate eventually verifies");
    Check(ram.fullValidationAttempts == 1, "complete candidate runs one full guard");
    Check(ram.writeCount == 5, "base cells apply after full verification only");
}

void RunPermanentMismatchBackoff()
{
    FakeRam ram;
    ram.codeReady = false;
    MelonPrime::MelonPrimePatchState state;
    auto memory = Bind(ram);

    constexpr int kFrames = 240;
    for (int frame = 0; frame < kFrames; ++frame)
    {
        MelonPrime::ExpandStageMatrix_ApplyIfLoadedForTesting(
            state, memory, true, false, 0u);
    }

    Check(ram.fullValidationAttempts == 0,
        "permanent readiness mismatch never runs the full guard");
    Check(ram.writeCount == 0,
        "permanent readiness mismatch never writes cells");
    Check(ram.totalReads < kFrames * 10,
        "permanent readiness mismatch is bounded below full-guard-per-frame cost");

    FakeRam strictRam;
    strictRam.strictReady = false;
    MelonPrime::MelonPrimePatchState strictState;
    auto strictMemory = Bind(strictRam);
    for (int frame = 0; frame < kFrames; ++frame)
    {
        MelonPrime::ExpandStageMatrix_ApplyIfLoadedForTesting(
            strictState, strictMemory, true, false, 0u);
    }
    Check(strictRam.fullValidationAttempts > 0,
        "a cheap-ready but strict-bad candidate is retried");
    Check(strictRam.fullValidationAttempts < kFrames / 4,
        "strict mismatch uses bounded exponential backoff");
    Check(strictRam.writeCount == 0,
        "strict mismatch never writes cells");
    Check(strictRam.totalReads < kFrames * 20,
        "strict mismatch is not a full guard on every frame");
}

void RunAllRomGroupsAndLifecycle()
{
    for (uint8_t romGroup = 0u; romGroup < 7u; ++romGroup)
    {
        FakeRam ram;
        ram.romGroupIndex = romGroup;
        MelonPrime::MelonPrimePatchState state;
        auto memory = Bind(ram);

        MelonPrime::ExpandStageMatrix_ApplyIfLoadedForTesting(
            state, memory, true, true, romGroup);
        Check(
            state.expandStageMatrix.status
                == MelonPrime::MelonPrimePatchState::ExpandStageMatrixStatus::Verified,
            "all seven ROM groups verify through the production guard");
        Check(ram.writeCount == 14, "all seven ROM groups apply base and extra cells");

        MelonPrime::ExpandStageMatrix_InvalidatePatch(state);
        Check(state.expandStageMatrix.pendingRestore,
            "lifecycle invalidation remembers an applied matrix");
        MelonPrime::ExpandStageMatrix_ApplyIfLoadedForTesting(
            state, memory, false, false, romGroup);
        Check(ram.writeCount == 28,
            "lifecycle invalidation validates before restoring all cells");
        Check(!state.expandStageMatrix.pendingRestore,
            "lifecycle restore clears the pending flag");

        MelonPrime::ExpandStageMatrix_ApplyIfLoadedForTesting(
            state, memory, true, false, romGroup);
        Check(ram.writeCount == 33,
            "config reconciliation reapplies only the requested base cells");
        Check(ram.fullValidationAttempts == 2,
            "lifecycle path performs one initial and one invalidated full guard");

        MelonPrime::ExpandStageMatrix_ResetPatchState(state);
        Check(
            state.expandStageMatrix.status
                == MelonPrime::MelonPrimePatchState::ExpandStageMatrixStatus::Unknown,
            "reset clears the validation status");
        Check(state.expandStageMatrix.romGroupIndex == 0xFFu,
            "reset clears the ROM group identity");
    }
}

} // namespace

int main()
{
    RunDelayedCompleteLoad();
    RunPermanentMismatchBackoff();
    RunAllRomGroupsAndLifecycle();

    if (gFailures != 0)
    {
        std::printf("stage-matrix-validation: %d failures\n", gFailures);
        return 1;
    }
    std::printf("stage-matrix-validation: delayed-load, bounded-retry, seven-ROM, lifecycle PASS\n");
    return 0;
}
