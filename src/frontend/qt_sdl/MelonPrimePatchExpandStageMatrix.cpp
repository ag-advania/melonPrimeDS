#ifdef MELONPRIME_DS

#include "MelonPrimePatchExpandStageMatrix.h"
#include "MelonPrimePatchState.h"
#include "Config.h"
#include "MelonPrimeDef.h"
#include "NDS.h"

namespace MelonPrime {

// ---- Stage matrix guard data (StageMatrixLoadedGuard-v55, all versions) ----
//
// Matrix formula:  addr = matrixBase + row * 0x0D + column
//   column = setupMode - 2  (Battle=0, Bounty=1, Capture=2, Defender=3,
//                             Node=4, PrimeHunter=5, Survival=6)
//
// Guard uses a strict 3-point check:
//   1. 10 fixed u32s at (matrixBase - 0x28)  match kMatrixPreludeWords
//   2. First 32 bytes at matrixBase           match kMatrixPrefixSignature
//   3. countRecompute / compatibilityCheck prologues and their matrixBase
//      literals match the expected values for this ROM version

struct MatrixVersionInfo {
    uint32_t matrixBase;
    uint32_t countRecomputeFunc;
    uint32_t compatibilityCheckFunc;
    uint32_t countMatrixLiteralAddr;
    uint32_t checkMatrixLiteralAddr;
    uint32_t expectedCountPrologue;
    uint32_t expectedCheckPrologue;
};

// ROM group order: JP1_0=0, JP1_1=1, US1_0=2, US1_1=3, EU1_0=4, EU1_1=5, KR1_0=6
static constexpr MatrixVersionInfo kMatrixVersions[7] = {
    {0x02145678u, 0x021377C0u, 0x0213831Cu, 0x0213783Cu, 0x02138370u, 0xE92D40F0u, 0xE92D4070u}, // JP1_0
    {0x02145638u, 0x02137780u, 0x021382DCu, 0x021377FCu, 0x02138330u, 0xE92D40F0u, 0xE92D4070u}, // JP1_1
    {0x02143384u, 0x02135588u, 0x021360E4u, 0x02135604u, 0x02136138u, 0xE92D40F0u, 0xE92D4070u}, // US1_0
    {0x02143E8Cu, 0x0213604Cu, 0x02136BA8u, 0x021360C8u, 0x02136BFCu, 0xE92D40F0u, 0xE92D4070u}, // US1_1
    {0x02143F74u, 0x02136164u, 0x02136CC0u, 0x021361E0u, 0x02136D14u, 0xE92D40F0u, 0xE92D4070u}, // EU1_0
    {0x02143F18u, 0x021360D8u, 0x02136C34u, 0x02136154u, 0x02136C88u, 0xE92D40F0u, 0xE92D4070u}, // EU1_1
    {0x02136898u, 0x0212B3F8u, 0x0212A9CCu, 0x0212B468u, 0x0212AA1Cu, 0xE92D40F8u, 0xE92D4070u}, // KR1_0
};

// 10 fixed u32s immediately before the matrix (at matrixBase - 0x28).
// Identical across all versions.
static constexpr uint32_t kMatrixPreludeWords[10] = {
    0x00000014u, 0x00000019u, 0x0000001Eu, 0x00000028u, 0x00000032u,
    0x0000003Cu, 0x00000046u, 0x00000050u, 0x0000005Au, 0x00000064u,
};

// First 32 bytes of the matrix.  Patch cells all lie beyond offset 0x20 so
// this signature remains valid even after the patch is applied.
static constexpr uint8_t kMatrixPrefixSignature[32] = {
    0x01u, 0x00u, 0x00u, 0x01u, 0x01u, 0x01u, 0x01u, 0x01u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x01u,
    0x01u, 0x01u, 0x01u, 0x01u, 0x01u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x01u, 0x01u, 0x01u,
};

// Stage/mode cells split into two groups.
// Base: stable, broadly useful additions.
// Extra: additional stages selectable via a second config key.
struct MatrixCell { uint8_t row, column; };

static constexpr MatrixCell kBaseCells[5] = {
    { 3, 3}, // Defender   — High Ground
    {17, 3}, // Defender   — Elder Passage
    {18, 1}, // Bounty     — Fuel Stack
    {22, 2}, // Capture    — Celestial Gateway
    {23, 1}, // Bounty     — Alinos Gateway
};

static constexpr MatrixCell kExtraCells[9] = {
    { 7, 0}, // Battle     — Transfer Lock wide
    { 7, 4}, // Node       — Transfer Lock wide
    { 7, 5}, // PrimeHunter— Transfer Lock wide
    { 7, 6}, // Survival   — Transfer Lock wide
    { 8, 3}, // Defender   — Transfer Lock
    {10, 3}, // Defender   — Compressor Room
    {11, 3}, // Defender   — Incubator
    {18, 3}, // Defender   — Fuel Stack
    {21, 3}, // Defender   — Head Shot
};

static constexpr uint32_t MatrixAddr(const MatrixVersionInfo& info, const MatrixCell& cell) noexcept
{
    return info.matrixBase + static_cast<uint32_t>(cell.row) * 0x0Du + cell.column;
}

// ---- Guard ----

static bool StageMatrixLoadedStrict(melonDS::NDS* nds, const MatrixVersionInfo& info)
{
    // 1. Prelude: 10 fixed u32s at matrixBase - 0x28
    const uint32_t preludeBase = info.matrixBase - 0x28u;
    for (int i = 0; i < 10; ++i) {
        if (nds->ARM9Read32(preludeBase + static_cast<uint32_t>(i * 4)) != kMatrixPreludeWords[i])
            return false;
    }
    // 2. Prefix: first 32 bytes of matrix
    for (int i = 0; i < 32; ++i) {
        if (nds->ARM9Read8(info.matrixBase + static_cast<uint32_t>(i)) != kMatrixPrefixSignature[i])
            return false;
    }
    // 3. Function literals and prologues
    if (nds->ARM9Read32(info.countRecomputeFunc)    != info.expectedCountPrologue)  return false;
    if (nds->ARM9Read32(info.compatibilityCheckFunc) != info.expectedCheckPrologue) return false;
    if (nds->ARM9Read32(info.countMatrixLiteralAddr) != info.matrixBase)            return false;
    if (nds->ARM9Read32(info.checkMatrixLiteralAddr) != info.matrixBase)            return false;
    return true;
}

// ---- Public API ----

void ExpandStageMatrix_ApplyIfLoaded(MelonPrimePatchState& state, melonDS::NDS* nds, Config::Table& cfg, uint8_t romGroupIndex)
{
    const bool enabled = cfg.GetBool(MelonPrime::CfgKey::ExpandStageMatrix);

    if (!state.expandStageMatrixPendingRestore && !enabled) return;
    if (romGroupIndex >= 7) return;

    const MatrixVersionInfo& info = kMatrixVersions[romGroupIndex];
    if (!StageMatrixLoadedStrict(nds, info)) return;

    if (state.expandStageMatrixPendingRestore) {
        // Write the correct state for every cell based on current settings.
        // Handles: parent off, extra off, or any combination changing on save.
        state.expandStageMatrixPendingRestore = false;
        const bool extraEnabled = enabled && cfg.GetBool(MelonPrime::CfgKey::ExpandStageMatrixExtra);
        for (const auto& cell : kBaseCells)
            nds->ARM9Write8(MatrixAddr(info, cell), enabled ? 0x01u : 0x00u);
        for (const auto& cell : kExtraCells)
            nds->ARM9Write8(MatrixAddr(info, cell), extraEnabled ? 0x01u : 0x00u);
        return;
    }

    // Normal per-frame apply path.
    for (const auto& cell : kBaseCells)
        nds->ARM9Write8(MatrixAddr(info, cell), 0x01u);

    if (cfg.GetBool(MelonPrime::CfgKey::ExpandStageMatrixExtra)) {
        for (const auto& cell : kExtraCells)
            nds->ARM9Write8(MatrixAddr(info, cell), 0x01u);
    }
}

void ExpandStageMatrix_InvalidatePatch(MelonPrimePatchState& state)
{
    state.expandStageMatrixPendingRestore = true;
}

void ExpandStageMatrix_ResetPatchState(MelonPrimePatchState& state)
{
    state.expandStageMatrixPendingRestore = false;
}

} // namespace MelonPrime

#endif // MELONPRIME_DS
