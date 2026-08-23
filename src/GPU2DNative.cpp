/*
    Copyright 2016-2026 melonDS team
*/

#include "GPU2DNative.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "GPU.h"
#include "Platform.h"

namespace melonDS::GPU2DNative
{

namespace
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
std::atomic<bool> ExactValidationSavestateReady{false};
#endif
std::atomic<u64> NextRendererEpoch{1};
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
std::atomic<u64> NativeOwnedBlocksSkipped{0};
std::atomic<u64> NativeOwnedHostReupload{0};
#endif

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
u32 CaptureAddressBlockMask(
    u32 offsetCode, u32 sizeCode, u32 firstLine) noexcept
{
    const u32 width = CaptureWidthForSize(sizeCode);
    const u32 height = CaptureHeightForSize(sizeCode);
    u32 mask = 0u;
    for (u32 line = firstLine; line < height; ++line)
    {
        for (u32 x = 0u; x < width; x += 2u)
        {
            const u32 address = WrapLCDCByte(
                CaptureOffsetBytes(offsetCode)
                + line * width * 2u + x * 2u);
            mask |= 1u << (address / CapturePhysicalBlockBytes);
        }
    }
    return mask;
}
#endif

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
std::atomic<u32>& ForcedPresentationStallRemaining() noexcept
{
    static std::atomic<u32> remaining = [] {
        const char* value = std::getenv(
            "MELONPRIME_TEST_GPU2D_PRESENTATION_STALL_FRAMES");
        if (!value || value[0] == '\0')
            return 0u;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end == value || *end != '\0')
            return 0u;
        return static_cast<u32>(std::min(parsed, 600ul));
    }();
    return remaining;
}
#endif
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
void RecordNativeOwnedCaptureCopySkipped() noexcept
{
    NativeOwnedBlocksSkipped.fetch_add(1u, std::memory_order_relaxed);
}

void RecordNativeOwnedHostReupload() noexcept
{
    NativeOwnedHostReupload.fetch_add(1u, std::memory_order_relaxed);
}

NativeCaptureHostCopyDiagnostics GetNativeCaptureHostCopyDiagnostics() noexcept
{
    return {
        NativeOwnedBlocksSkipped.load(std::memory_order_relaxed),
        NativeOwnedHostReupload.load(std::memory_order_relaxed),
    };
}
#endif

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
bool ConsumeForcedPresentationStallFrame() noexcept
{
    std::atomic<u32>& remaining = ForcedPresentationStallRemaining();
    u32 current = remaining.load(std::memory_order_relaxed);
    while (current != 0u
        && !remaining.compare_exchange_weak(
            current, current - 1u,
            std::memory_order_acq_rel, std::memory_order_relaxed))
    {
    }
    return current != 0u;
}
#endif

u64 AllocateRendererEpoch() noexcept
{
    u64 epoch = NextRendererEpoch.fetch_add(1u, std::memory_order_relaxed);
    if (epoch != 0u)
        return epoch;
    // The wraparound path is practically unreachable, but zero is reserved
    // as the uninitialized identity in the frontend visibility state.
    epoch = NextRendererEpoch.fetch_add(1u, std::memory_order_relaxed);
    return epoch == 0u ? 1u : epoch;
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
bool ExactValidationEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_GPU2D_EXACT_VALIDATE");
        if (!value || value[0] == '\0')
            value = std::getenv("MELONPRIME_GPU2D_EXACT");
        if (!value)
            return false;
        return value[0] == '1' || value[0] == 'y' || value[0] == 'Y'
            || value[0] == 't' || value[0] == 'T';
    }();
    const char* diagnosticState = std::getenv("MELONPRIME_TEST_SAVESTATE");
    const char* physicalABState =
        std::getenv("MELONPRIME_PHYSICAL_AB_SAVESTATE_PATH");
    const bool waitForSavestate =
        (diagnosticState && diagnosticState[0] != '\0')
        || (physicalABState && physicalABState[0] != '\0');
    return enabled
        && (!waitForSavestate
            || ExactValidationSavestateReady.load(std::memory_order_acquire));
}

bool StageDiagnosticsEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_GPU2D_STAGE_DIAGNOSTICS");
        if (!value || value[0] == '\0')
            value = std::getenv("MELONPRIME_GPU2D_EXACT_VALIDATE");
        if (!value || value[0] == '\0')
            value = std::getenv("MELONPRIME_GPU2D_EXACT");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

bool DirectOutputDiagnosticsEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_GPU2D_STAGE_DIRECT");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

bool DropDiscontinuousSavestateFrameEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv(
            "MELONPRIME_GPU2D_DROP_DISCONTINUOUS_SAVESTATE_FRAME");
        if (!value || value[0] == '\0')
            return true;
        if (value[0] == '0' && value[1] == '\0')
            return false;
        return value[0] == '1' || value[0] == 'y' || value[0] == 'Y'
            || value[0] == 't' || value[0] == 'T';
    }();
    return enabled;
}

void NotifySavestateLoaded() noexcept
{
    ExactValidationSavestateReady.store(true, std::memory_order_release);
}

u64 HashWords(const u32* words, std::size_t count, u64 seed) noexcept
{
    if (!words)
        return seed;

    u64 hash = seed;
    for (std::size_t i = 0; i < count; ++i)
    {
        u32 word = words[i];
        for (u32 byte = 0; byte < sizeof(word); ++byte)
        {
            hash ^= static_cast<u8>(word & 0xFFu);
            hash *= 1099511628211ull;
            word >>= 8u;
        }
    }
    return hash;
}

BlankClass ClassifyNativePixels(const u32* pixels, std::size_t count) noexcept
{
    if (!pixels || count == 0)
        return BlankClass::NonBlank;

    bool allBlack = true;
    bool allWhite = true;
    for (std::size_t i = 0; i < count; ++i)
    {
        const u32 color = pixels[i] & 0x003F3F3Fu;
        allBlack = allBlack && color == 0u;
        allWhite = allWhite && color == 0x003F3F3Fu;
        if (!allBlack && !allWhite)
            return BlankClass::NonBlank;
    }
    return allBlack ? BlankClass::AllBlack
        : (allWhite ? BlankClass::AllWhite : BlankClass::NonBlank);
}

const char* BlankClassName(BlankClass value) noexcept
{
    switch (value)
    {
    case BlankClass::AllBlack: return "ALL_BLACK";
    case BlankClass::AllWhite: return "ALL_WHITE";
    case BlankClass::Unknown: return "UNKNOWN";
    default: return "NONBLANK";
    }
}

u64 HashStructuredScreen(const u32* structured, u32 screen) noexcept
{
    if (!structured || screen >= 2u)
        return 0u;

    const u32 screenBase = screen * 4u * ScreenPixelCount;
    u64 hash = 1469598103934665603ull;
    for (u32 plane = 0; plane < 4u; ++plane)
    {
        hash = HashWords(
            structured + screenBase + plane * ScreenPixelCount,
            ScreenPixelCount, hash);
    }
    return HashWords(
        structured + StructuredLineMetaBase + screen * ScreenHeight,
        ScreenHeight, hash);
}

BlankClass ClassifyStructuredScreen(const u32* structured, u32 screen) noexcept
{
    if (!structured || screen >= 2u)
        return BlankClass::NonBlank;

    const u32 screenBase = screen * 4u * ScreenPixelCount;
    const u32 belowBase = screenBase;
    const u32 controlBase = screenBase + 2u * ScreenPixelCount;
    bool allBlack = true;
    bool allWhite = true;
    for (u32 pixel = 0; pixel < ScreenPixelCount; ++pixel)
    {
        const u32 control = structured[controlBase + pixel];
        // A 3D slot is resolved only by Stage B.  Do not call a Stage A
        // plane that intentionally awaits 3D a false all-black/all-white
        // frame.
        if (((control >> 24u) & 0x40u) != 0u)
            return BlankClass::NonBlank;
        const u32 color = structured[belowBase + pixel] & 0x003F3F3Fu;
        allBlack = allBlack && color == 0u;
        allWhite = allWhite && color == 0x003F3F3Fu;
        if (!allBlack && !allWhite)
            return BlankClass::NonBlank;
    }
    return allBlack ? BlankClass::AllBlack
        : (allWhite ? BlankClass::AllWhite : BlankClass::NonBlank);
}

namespace
{

void LogBlankState(
    const char* backend,
    const char* stage,
    u64 emulatedFrame,
    u64 recordedFrame,
    u64 rendererSerial,
    u64 generation,
    u32 slot,
    const FrameInput& input,
    u32 screen,
    const char* source,
    BlankClass actual,
    BlankClass expected) noexcept
{
    Platform::Log(
        Platform::LogLevel::Info,
        "[GPU2DStage] blank_state backend=%s stage=%s emulated=%llu recorded=%llu "
        "renderer_serial=%llu generation=%llu slot=%u ScreenSwap=%u ScreensEnabled=%u "
        "physical_screen=%u source=%s actual=%s expected=%s\n",
        backend, stage,
        static_cast<unsigned long long>(emulatedFrame),
        static_cast<unsigned long long>(recordedFrame),
        static_cast<unsigned long long>(rendererSerial),
        static_cast<unsigned long long>(generation), slot,
        input.ScreenSwap, input.ScreensEnabled, screen, source,
        BlankClassName(actual), BlankClassName(expected));

    for (u32 line : {0u, 96u, 191u})
    {
        const u32 engine = input.ScreenSource[screen * ScreenHeight + line] & 1u;
        const LineState& state = input.Lines[engine * ScreenHeight + line];
        const u32 displayMode = (state.DispCnt >> 16u) & 0x3u;
        Platform::Log(
            Platform::LogLevel::Info,
            "[GPU2DStage] blank_line backend=%s stage=%s physical_screen=%u line=%u "
            "ScreenSource=%u engine=%u DispCnt=0x%08X display_mode=%u "
            "UnitEnabled=%u ForcedBlank=%u LayerEnable=0x%08X OBJEnable=0x%08X "
            "MasterBrightness=%u LCDVRAMMap=0x%08X CaptureEnable=%u CaptureCnt=0x%08X\n",
            backend, stage, screen, line,
            input.ScreenSource[screen * ScreenHeight + line], engine,
            state.DispCnt, displayMode, state.UnitEnabled, state.ForcedBlank,
            state.LayerEnable, state.OBJEnable, state.MasterBrightness,
            state.LCDVRAMMap, state.CaptureEnable, state.CaptureCnt);

        const auto hashBytes = [](const u8* bytes, std::size_t count) {
            u64 hash = 1469598103934665603ull;
            if (!bytes)
                return hash;
            for (std::size_t i = 0; i < count; ++i)
            {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return hash;
        };
        const u32 bgOffset = engine == 0u ? 0u : 32u;
        const u32 objOffset = engine == 0u ? 0u : 16u;
        const u32 bgBase = line * NativeCaptureBGMappingStride + bgOffset;
        const u32 objBase = line * NativeCaptureOBJMappingStride + objOffset;
        const u32 bgCount = engine == 0u ? 32u : 8u;
        const u32 objCount = engine == 0u ? 16u : 8u;
        const u32 bg0 = bgCount > 0u
            ? input.NativeCaptureBGMapping[bgBase] : 0u;
        const u32 bg1 = bgCount > 1u
            ? input.NativeCaptureBGMapping[bgBase + 1u] : 0u;
        const u32 bg2 = bgCount > 2u
            ? input.NativeCaptureBGMapping[bgBase + 2u] : 0u;
        const u32 bg3 = bgCount > 3u
            ? input.NativeCaptureBGMapping[bgBase + 3u] : 0u;
        const u32 obj0 = objCount > 0u
            ? input.NativeCaptureOBJMapping[objBase] : 0u;
        const u32 obj1 = objCount > 1u
            ? input.NativeCaptureOBJMapping[objBase + 1u] : 0u;
        const u32 obj2 = objCount > 2u
            ? input.NativeCaptureOBJMapping[objBase + 2u] : 0u;
        const u32 obj3 = objCount > 3u
            ? input.NativeCaptureOBJMapping[objBase + 3u] : 0u;
        const MemorySnapshot& memory = input.Engine[engine];
        const u64 bgHash = hashBytes(
            memory.BGVRAM.data(), std::min<std::size_t>(memory.BGSize, 1024u));
        const u64 objHash = hashBytes(
            memory.OBJVRAM.data(), std::min<std::size_t>(memory.OBJSize, 1024u));
        const u64 bank2Hash = hashBytes(
            input.LCDVRAM.data() + 2u * 128u * 1024u, 1024u);
        const u64 bank3Hash = hashBytes(
            input.LCDVRAM.data() + 3u * 128u * 1024u, 1024u);
        u16 lcd2Word0 = 0u;
        u16 lcd2Word1 = 0u;
        u16 lcd2Word2 = 0u;
        u16 lcd2Word3 = 0u;
        u16 lcd3Word0 = 0u;
        u16 lcd3Word1 = 0u;
        u16 lcd3Word2 = 0u;
        u16 lcd3Word3 = 0u;
        std::memcpy(&lcd2Word0, input.LCDVRAM.data() + 2u * 128u * 1024u + 0u, sizeof(u16));
        std::memcpy(&lcd2Word1, input.LCDVRAM.data() + 2u * 128u * 1024u + 2u, sizeof(u16));
        std::memcpy(&lcd2Word2, input.LCDVRAM.data() + 2u * 128u * 1024u + 4u, sizeof(u16));
        std::memcpy(&lcd2Word3, input.LCDVRAM.data() + 2u * 128u * 1024u + 6u, sizeof(u16));
        std::memcpy(&lcd3Word0, input.LCDVRAM.data() + 3u * 128u * 1024u + 0u, sizeof(u16));
        std::memcpy(&lcd3Word1, input.LCDVRAM.data() + 3u * 128u * 1024u + 2u, sizeof(u16));
        std::memcpy(&lcd3Word2, input.LCDVRAM.data() + 3u * 128u * 1024u + 4u, sizeof(u16));
        std::memcpy(&lcd3Word3, input.LCDVRAM.data() + 3u * 128u * 1024u + 6u, sizeof(u16));
        const CaptureBlockProvenance& bank2Owner =
            input.LCDVRAMProvenance[2u * CapturePhysicalBlocksPerBank];
        const CaptureBlockProvenance& bank3Owner =
            input.LCDVRAMProvenance[3u * CapturePhysicalBlocksPerBank];
        Platform::Log(
            Platform::LogLevel::Info,
            "[GPU2DStage] mapped_debug backend=%s stage=%s emulated=%llu "
            "screen=%u line=%u engine=%u BGSize=%u OBJSize=%u "
            "bgMask=%08X,%08X,%08X,%08X objMask=%08X,%08X,%08X,%08X "
            "bgHash=%016llX objHash=%016llX "
            "lcd2Hash=%016llX lcd2_words=%04X,%04X,%04X,%04X "
            "lcd3Hash=%016llX lcd3_words=%04X,%04X,%04X,%04X "
            "lcd2Owner=%s lcd3Owner=%s\n",
            backend, stage,
            static_cast<unsigned long long>(emulatedFrame), screen, line, engine,
            memory.BGSize, memory.OBJSize,
            bg0, bg1, bg2, bg3, obj0, obj1, obj2, obj3,
            static_cast<unsigned long long>(bgHash),
            static_cast<unsigned long long>(objHash),
            static_cast<unsigned long long>(bank2Hash),
            lcd2Word0, lcd2Word1, lcd2Word2, lcd2Word3,
            static_cast<unsigned long long>(bank3Hash),
            lcd3Word0, lcd3Word1, lcd3Word2, lcd3Word3,
            CaptureOwnerName(bank2Owner.Owner),
            CaptureOwnerName(bank3Owner.Owner));
    }
}

void LogStageLine(
    const char* backend,
    const char* stage,
    u64 emulatedFrame,
    u64 recordedFrame,
    u64 rendererSerial,
    u64 generation,
    u32 slot,
    u32 screenSwap,
    u32 screensEnabled,
    const char* source,
    u64 topHash,
    u64 bottomHash,
    BlankClass top,
    BlankClass bottom) noexcept
{
    Platform::Log(
        Platform::LogLevel::Info,
        "[GPU2DStage] backend=%s stage=%s emulated=%llu recorded=%llu "
        "renderer_serial=%llu generation=%llu slot=%u "
        "ScreenSwap=%u ScreensEnabled=%u source=%s "
        "%s_top_hash=%016llX %s_bottom_hash=%016llX "
        "top=%s bottom=%s\n",
        backend, stage,
        static_cast<unsigned long long>(emulatedFrame),
        static_cast<unsigned long long>(recordedFrame),
        static_cast<unsigned long long>(rendererSerial),
        static_cast<unsigned long long>(generation), slot, screenSwap,
        screensEnabled, source,
        stage[0] == 'A' ? "logical" : "resolved",
        static_cast<unsigned long long>(topHash),
        stage[0] == 'A' ? "logical" : "resolved",
        static_cast<unsigned long long>(bottomHash),
        BlankClassName(top), BlankClassName(bottom));
}

} // namespace

void LogStageSnapshot(
    const char* backend,
    u64 emulatedFrame,
    u64 recordedFrame,
    u64 rendererSerial,
    u64 generation,
    u32 slot,
    const FrameInput& input,
    const u32* structured,
    const u32* actualTop,
    const u32* actualBottom,
    const char* resolvedSource,
    const u32* expectedTop,
    const u32* expectedBottom) noexcept
{
    if (!StageDiagnosticsEnabled())
        return;

    if (structured)
    {
        const BlankClass top = ClassifyStructuredScreen(structured, 0u);
        const BlankClass bottom = ClassifyStructuredScreen(structured, 1u);
        LogStageLine(
            backend, "A", emulatedFrame, recordedFrame, rendererSerial,
            generation, slot, input.ScreenSwap, input.ScreensEnabled,
            "structured",
            HashStructuredScreen(structured, 0u),
            HashStructuredScreen(structured, 1u), top, bottom);
        if (top != BlankClass::NonBlank)
            LogBlankState(backend, "A", emulatedFrame, recordedFrame, rendererSerial,
                generation, slot, input, 0u, "structured", top,
                expectedTop ? ClassifyNativePixels(expectedTop, ScreenPixelCount)
                    : BlankClass::Unknown);
        if (bottom != BlankClass::NonBlank)
            LogBlankState(backend, "A", emulatedFrame, recordedFrame, rendererSerial,
                generation, slot, input, 1u, "structured", bottom,
                expectedBottom ? ClassifyNativePixels(expectedBottom, ScreenPixelCount)
                    : BlankClass::Unknown);
    }

    if (actualTop && actualBottom)
    {
        const BlankClass top = ClassifyNativePixels(actualTop, ScreenPixelCount);
        const BlankClass bottom = ClassifyNativePixels(actualBottom, ScreenPixelCount);
        LogStageLine(
            backend, "B", emulatedFrame, recordedFrame, rendererSerial,
            generation, slot, input.ScreenSwap, input.ScreensEnabled,
            resolvedSource,
            HashWords(actualTop, ScreenPixelCount),
            HashWords(actualBottom, ScreenPixelCount), top, bottom);
        // Stage B is the developer readback of the actual native output that
        // the presenter consumes: either the composed buffer or the direct
        // output image. Keep an explicit final-composition marker separate
        // from the older resolved_* names so a Stage A logical hash cannot be
        // mistaken for proof of the presented image.
        Platform::Log(
            Platform::LogLevel::Info,
            "[GPU2DStage] backend=%s FinalComposedTopHash=%016llX "
            "FinalComposedBottomHash=%016llX source=%s "
            "emulated=%llu generation=%llu renderer_serial=%llu\n",
            backend,
            static_cast<unsigned long long>(HashWords(actualTop, ScreenPixelCount)),
            static_cast<unsigned long long>(HashWords(actualBottom, ScreenPixelCount)),
            resolvedSource,
            static_cast<unsigned long long>(emulatedFrame),
            static_cast<unsigned long long>(generation),
            static_cast<unsigned long long>(rendererSerial));
        if (top != BlankClass::NonBlank)
            LogBlankState(backend, "B", emulatedFrame, recordedFrame, rendererSerial,
                generation, slot, input, 0u, resolvedSource, top,
                expectedTop ? ClassifyNativePixels(expectedTop, ScreenPixelCount)
                    : BlankClass::Unknown);
        if (bottom != BlankClass::NonBlank)
            LogBlankState(backend, "B", emulatedFrame, recordedFrame, rendererSerial,
                generation, slot, input, 1u, resolvedSource, bottom,
                expectedBottom ? ClassifyNativePixels(expectedBottom, ScreenPixelCount)
                    : BlankClass::Unknown);
    }
}

void LogPresentedIdentity(
    const char* backend,
    u64 emulatedFrame,
    u64 rendererSerial,
    u64 generation,
    u64 epoch,
    u32 slot) noexcept
{
    if (!StageDiagnosticsEnabled())
        return;
    Platform::Log(
        Platform::LogLevel::Info,
        "[GPU2DStage] backend=%s stage=C emulated=%llu "
        "presented_renderer_serial=%llu presented_generation=%llu "
        "presented_epoch=%llu presented_slot=%u\n",
        backend, static_cast<unsigned long long>(emulatedFrame),
        static_cast<unsigned long long>(rendererSerial),
        static_cast<unsigned long long>(generation),
        static_cast<unsigned long long>(epoch), slot);
}

void LogSemanticIdentity(
    const char* backend,
    u64 emulatedFrame,
    u64 captureGeneration,
    u64 epoch,
    bool published,
    bool forcedPresentationStall,
    bool mirrorFullResync,
    u32 publishedSlot) noexcept
{
    if (!StageDiagnosticsEnabled())
        return;
    Platform::Log(
        Platform::LogLevel::Info,
        "[GPU2DStage] backend=%s stage=semantic emulated=%llu "
        "capture_generation=%llu mirror_last_semantic_frame=%llu "
        "mirror_capture_generation=%llu semantic_epoch=%llu publication=%s "
        "presentation_stall=%s mirror_full_resync=%u published_slot=%u\n",
        backend, static_cast<unsigned long long>(emulatedFrame),
        static_cast<unsigned long long>(captureGeneration),
        static_cast<unsigned long long>(emulatedFrame),
        static_cast<unsigned long long>(captureGeneration),
        static_cast<unsigned long long>(epoch),
        published ? "visible" : "semantic_only",
        forcedPresentationStall ? "forced" : "none",
        mirrorFullResync ? 1u : 0u, publishedSlot);
}
#endif

namespace
{
void ClearFrameInput(FrameInput& input) noexcept
{
    // FrameInput is intentionally a trivially-copyable ABI aggregate. Avoid
    // `Input = {}` here: its multi-megabyte temporary can exceed the Windows
    // thread stack before the recorder has even started a frame.
    std::memset(&input, 0, sizeof(input));
}

void MarkDirtyRange(FrameInput& input, u32 offset, u32 size) noexcept
{
    if (size == 0u)
        return;
    if (input.DirtyRangeCount != 0u)
    {
        DirtyRange& previous = input.DirtyRanges[input.DirtyRangeCount - 1u];
        if (previous.Offset + previous.Size == offset)
        {
            previous.Size += size;
            return;
        }
    }
    if (input.DirtyRangeCount >= MaxDirtyRanges)
    {
        input.DirtyRangeCount = 1u;
        input.DirtyRanges[0] = {
            0u, static_cast<u32>(PackedFrameBytes())};
        return;
    }
    input.DirtyRanges[input.DirtyRangeCount++] = {offset, size};
}

u64 NowNanoseconds() noexcept
{
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void CopyChangedBlocks(
    FrameInput& input,
    u8* destination,
    const u8* source,
    u32 size,
    u32 packedOffset) noexcept
{
    if (!destination || !source || size == 0u)
        return;
    for (u32 offset = 0; offset < size; offset += DirtyBlockBytes)
    {
        const u32 blockSize = std::min(DirtyBlockBytes, size - offset);
        ++input.Recorder.BlocksScanned;
        input.Recorder.BytesScanned += blockSize;
        if (std::memcmp(destination + offset, source + offset, blockSize) == 0)
            continue;
        std::memcpy(destination + offset, source + offset, blockSize);
        ++input.Recorder.BlocksCopied;
        input.Recorder.BytesCopied += blockSize;
        MarkDirtyRange(input, packedOffset + offset, blockSize);
    }
}

void CopyCoherentLCDVRAMBlocks(
    FrameInput& input,
    u32 bank,
    u8* destination,
    const u8* source) noexcept
{
    if (bank >= CapturePhysicalBanks || !destination || !source)
        return;

    for (u32 physicalBlock = 0;
        physicalBlock < CapturePhysicalBlocksPerBank;
        ++physicalBlock)
    {
        const CaptureBlockProvenance& provenance = input.LCDVRAMProvenance[
            bank * CapturePhysicalBlocksPerBank + physicalBlock];
        if (IsNativeCaptureOwner(provenance.Owner))
        {
            // The persistent native mirror is authoritative for this
            // physical block. Copying CPU VRAM here would replay the stale
            // pre-capture snapshot over it on a frame rollover/resync.
            continue;
        }

        const u32 offset = physicalBlock * CapturePhysicalBlockBytes;
        CopyChangedBlocks(
            input,
            destination + offset,
            source + offset,
            CapturePhysicalBlockBytes,
            PackedLCDVRAMBase * sizeof(u32)
                + bank * 128u * 1024u + offset);
    }
}

void CopyLineState(LineState& destination, const GPU2D& source, u32 renderXPos) noexcept
{
    destination = {};
    destination.DispCnt = source.DispCnt;
    destination.LayerEnable = source.LayerEnable;
    destination.OBJEnable = source.OBJEnable;
    destination.ForcedBlank = source.ForcedBlank;
    for (u32 i = 0; i < 4u; ++i)
    {
        destination.BGCnt[i] = source.BGCnt[i];
        destination.BGXPos[i] = source.BGXPos[i];
        destination.BGYPos[i] = source.BGYPos[i];
        destination.Win0Coords[i] = source.Win0Coords[i];
        destination.Win1Coords[i] = source.Win1Coords[i];
        destination.WinCnt[i] = source.WinCnt[i];
    }
    for (u32 i = 0; i < 2u; ++i)
    {
        destination.BGXRefInternal[i] = source.BGXRefInternal[i];
        destination.BGYRefInternal[i] = source.BGYRefInternal[i];
        destination.BGRotA[i] = source.BGRotA[i];
        destination.BGRotB[i] = source.BGRotB[i];
        destination.BGRotC[i] = source.BGRotC[i];
        destination.BGRotD[i] = source.BGRotD[i];
        destination.BGMosaicSize[i] = source.BGMosaicSize[i];
        destination.OBJMosaicSize[i] = source.OBJMosaicSize[i];
    }
    destination.Win0Active = source.Win0Active;
    destination.Win1Active = source.Win1Active;
    destination.BGMosaicLine = source.BGMosaicLine;
    destination.OBJMosaicLine = source.OBJMosaicLine;
    destination.BlendCnt = source.BlendCnt;
    destination.BlendAlpha = source.BlendAlpha;
    destination.EVA = source.EVA;
    destination.EVB = source.EVB;
    destination.EVY = source.EVY;
    destination.RenderXPos = renderXPos & 0x1FFu;

    // Keep the same packed window representation as the OpenGL scanline
    // configuration.  The active bit is the vertical latch; the horizontal
    // bit is consumed locally here, so a shader does not need to mutate state
    // while evaluating a pixel.
    destination.WinRegs = (source.DispCnt & 0xE000u) != 0u
        ? source.WinCnt[2]
        : 0xFFu;
    destination.WinRegs |= (source.DispCnt & (1u << 15u))
        ? static_cast<u32>(source.WinCnt[3]) << 8u : 0xFF00u;
    destination.WinRegs |= (source.DispCnt & (1u << 14u))
        ? static_cast<u32>(source.WinCnt[1]) << 16u : 0xFF0000u;
    destination.WinRegs |= (source.DispCnt & (1u << 13u))
        ? static_cast<u32>(source.WinCnt[0]) << 24u : 0xFF000000u;

    destination.WinPos = {256u, 256u, 256u, 256u};
    destination.WinMask = 0;
    if ((source.DispCnt & (1u << 13u)) && (source.Win0Active & 0x1u))
    {
        const u32 x0 = source.Win0Coords[0];
        const u32 x1 = source.Win0Coords[1];
        if (x0 <= x1)
        {
            destination.WinPos[0] = x0;
            destination.WinPos[1] = x1;
            if (source.Win0Active == 0x3u)
                destination.WinMask |= 1u << 0u;
            destination.WinMask |= 1u << 1u;
        }
        else
        {
            destination.WinPos[0] = x1;
            destination.WinPos[1] = x0;
            if (source.Win0Active == 0x3u)
                destination.WinMask |= 1u << 0u;
            destination.WinMask |= 1u << 2u;
        }
    }
    if ((source.DispCnt & (1u << 14u)) && (source.Win1Active & 0x1u))
    {
        const u32 x0 = source.Win1Coords[0];
        const u32 x1 = source.Win1Coords[1];
        if (x0 <= x1)
        {
            destination.WinPos[2] = x0;
            destination.WinPos[3] = x1;
            if (source.Win1Active == 0x3u)
                destination.WinMask |= 1u << 3u;
            destination.WinMask |= 1u << 4u;
        }
        else
        {
            destination.WinPos[2] = x1;
            destination.WinPos[3] = x0;
            if (source.Win1Active == 0x3u)
                destination.WinMask |= 1u << 3u;
            destination.WinMask |= 1u << 5u;
        }
    }
}

bool HasDirtyOverlap(const FrameInput& input, u32 begin, u32 end) noexcept
{
    for (u32 i = 0; i < input.DirtyRangeCount; ++i)
    {
        const DirtyRange& range = input.DirtyRanges[i];
        const u32 rangeEnd = range.Offset + range.Size;
        if (range.Offset < end && rangeEnd > begin)
            return true;
    }
    return false;
}

struct MappedReadContext
{
    FrameInput* Input = nullptr;
    u32 Line = 0u;
    u32 Engine = 0u;
    u32 Section = 0u;
    bool NativeMappingReady = false;
    std::array<u8, CapturePhysicalBanks> NativeCaptureWrittenBlocks{};
    u32 NativeCaptureWrittenBankMask = 0u;
};

u32 CaptureWrittenBankMask(
    const std::array<u8, CapturePhysicalBanks>& writtenBlocks) noexcept
{
    u32 bankMask = 0u;
    for (u32 bank = 0u; bank < CapturePhysicalBanks; ++bank)
    {
        if (writtenBlocks[bank] != 0u)
            bankMask |= 1u << bank;
    }
    return bankMask;
}

bool IsCaptureMappedSection(u32 section) noexcept
{
    // Sections 0/1 are BG/OBJ logical VRAM. Extended palettes have no
    // native capture overlay in this ABI and therefore remain ordinary CPU
    // mirrors.
    return section == 0u || section == 1u;
}

u32 NativeCaptureMaskForRead(
    const MappedReadContext& context,
    const melonDS::GPU& gpu,
    u32 mappedBankMask,
    u32 address) noexcept
{
    if (!context.Input || !context.NativeMappingReady
        || !IsCaptureMappedSection(context.Section)
        || context.Line >= ScreenHeight)
    {
        return 0u;
    }

    u32 nativeMask = 0u;
    u32 captureBanks = mappedBankMask
        & ((1u << CapturePhysicalBanks) - 1u);
    while (captureBanks != 0u)
    {
        const u32 bank = static_cast<u32>(__builtin_ctz(captureBanks));
        captureBanks &= captureBanks - 1u;
        const u32 physicalAddress = address & gpu.VRAMMask[bank];
        const u32 physicalBlock =
            physicalAddress / CapturePhysicalBlockBytes;
        if (IsNativeCaptureOwner(
                gpu.GetRenderer().GetCaptureBlockProvenance(
                    bank, physicalBlock).Owner))
        {
            nativeMask |= 1u << bank;
        }
    }
    return nativeMask;
}

u32 NativeCaptureWrittenMaskForRead(
    const MappedReadContext& context,
    const melonDS::GPU& gpu,
    u32 mappedBankMask,
    u32 address) noexcept
{
    if (!context.Input || !context.NativeMappingReady
        || context.NativeCaptureWrittenBankMask == 0u
        || !IsCaptureMappedSection(context.Section))
    {
        return 0u;
    }

    u32 writtenMask = 0u;
    u32 captureBanks = mappedBankMask
        & ((1u << CapturePhysicalBanks) - 1u);
    while (captureBanks != 0u)
    {
        const u32 bank = static_cast<u32>(__builtin_ctz(captureBanks));
        captureBanks &= captureBanks - 1u;
        const u32 physicalAddress = address & gpu.VRAMMask[bank];
        const u32 physicalBlock =
            physicalAddress / CapturePhysicalBlockBytes;
        if ((context.NativeCaptureWrittenBlocks[bank]
                & (1u << physicalBlock)) != 0u)
        {
            writtenMask |= 1u << bank;
        }
    }
    return writtenMask;
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
bool ProofMaterializeMappedCaptureEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv(
            "MELONPRIME_GPU2D_PROOF_MATERIALIZE_MAPPED_CAPTURE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

u64 HashPhysicalCaptureBlock(
    const melonDS::GPU& gpu, u32 bank, u32 physicalBlock) noexcept
{
    if (bank >= CapturePhysicalBanks
        || physicalBlock >= CapturePhysicalBlocksPerBank)
    {
        return 0u;
    }

    u64 hash = 1469598103934665603ull;
    const u8* source = gpu.VRAM[bank]
        + static_cast<std::size_t>(physicalBlock) * CapturePhysicalBlockBytes;
    for (u32 byte = 0u; byte < CapturePhysicalBlockBytes; ++byte)
    {
        hash ^= source[byte];
        hash *= 1099511628211ull;
    }
    return hash;
}

void RecordMappedCaptureViolation(
    FrameInput& input,
    const MappedReadContext& context,
    const melonDS::GPU& gpu,
    u32 logicalAddress,
    u32 mappingIndex,
    u32 bank,
    u32 physicalAddress,
    u32 physicalBlock,
    const CaptureBlockProvenance& owner,
    bool materialized) noexcept
{
    for (u32 index = 0u; index < input.MappedCaptureViolationCount; ++index)
    {
        const MappedCaptureViolation& current =
            input.MappedCaptureViolations[index];
        if (current.Line == context.Line
            && current.Engine == context.Engine
            && current.Section == context.Section
            && current.LogicalAddress == logicalAddress
            && current.MappingIndex == mappingIndex
            && current.Bank == bank
            && current.PhysicalBlock == physicalBlock
            && current.Materialized == (materialized ? 1u : 0u))
        {
            return;
        }
    }

    if (input.MappedCaptureViolationCount >= MaxMappedCaptureViolations)
    {
        input.MappedCaptureViolationOverflow = 1u;
        return;
    }

    MappedCaptureViolation& violation =
        input.MappedCaptureViolations[input.MappedCaptureViolationCount++];
    violation.Frame = input.Generation.Frame;
    violation.Line = context.Line;
    violation.Engine = context.Engine;
    violation.Section = context.Section;
    violation.LogicalAddress = logicalAddress;
    violation.MappingIndex = mappingIndex;
    violation.Bank = bank;
    violation.PhysicalAddress = physicalAddress;
    violation.PhysicalBlock = physicalBlock;
    violation.Owner = owner.Owner;
    violation.OwnerSemanticFrame = owner.SemanticFrame;
    violation.OwnerCaptureGeneration = owner.CaptureGeneration;
    violation.CpuHash = HashPhysicalCaptureBlock(gpu, bank, physicalBlock);
    violation.NativeHash = 0u;
    violation.Materialized = materialized ? 1u : 0u;
}
#endif

template <u32 MappingBytes>
u64 ReadMappedWordSlow(
    const MappedReadContext& context,
    const melonDS::GPU& gpu,
    const u32* mappings,
    u32 mappingCount,
    u32 address) noexcept
{
    u64 value = 0;
    const Renderer& renderer = gpu.GetRenderer();
    for (u32 byteOffset = 0u; byteOffset < sizeof(u64); ++byteOffset)
    {
        const u32 logicalAddress = address + byteOffset;
        const u32 mappingIndex = logicalAddress / MappingBytes;
        if (mappingIndex >= mappingCount)
            continue;

        const u32 bankMask = mappings[mappingIndex];
        u8 result = 0u;
        for (u32 bank = 0u; bank < 9u; ++bank)
        {
            if ((bankMask & (1u << bank)) == 0u)
                continue;

            const u32 physicalAddress = logicalAddress & gpu.VRAMMask[bank];
            const u32 physicalBlock = physicalAddress / CapturePhysicalBlockBytes;
            bool skipCpuRead = false;
            if (bank < CapturePhysicalBanks
                && IsCaptureMappedSection(context.Section))
            {
                // Copy the provenance before the developer proof path can
                // materialize the block. The renderer's successful readback
                // transition mutates its live owner in place; diagnostics
                // must retain the owner that authorized the attempted CPU
                // read, not report the post-materialization state.
                const CaptureBlockProvenance owner =
                    renderer.GetCaptureBlockProvenance(bank, physicalBlock);
                if (IsNativeCaptureOwner(owner.Owner))
                {
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
                    bool materialized = false;
                    if (ProofMaterializeMappedCaptureEnabled())
                    {
                        ++context.Input->Recorder.NativeOwnedMappedCpuRead;
                        const_cast<melonDS::GPU&>(gpu)
                            .MaterializeVRAMCaptureBlockForGPU2DProof(
                                (bank << 2u) | physicalBlock);
                        const CaptureBlockProvenance& after =
                            gpu.GetRenderer().GetCaptureBlockProvenance(
                                bank, physicalBlock);
                        materialized = !IsNativeCaptureOwner(after.Owner);
                        if (materialized)
                        {
                            ++context.Input->Recorder
                                .NativeOwnedMappedCpuMaterialized;
                        }
                        RecordMappedCaptureViolation(
                            *context.Input, context, gpu, logicalAddress,
                            mappingIndex, bank, physicalAddress, physicalBlock,
                            owner, materialized);
                        skipCpuRead = !materialized;
                    }
                    else
                    {
                        // Normal shipping/developer execution never reads
                        // stale CPU VRAM for a native-owned capture. The
                        // shader's native mirror overlay supplies this bank.
                        skipCpuRead = true;
                    }
#else
                    // Native capture is GPU-resident in shipping builds.
                    // Never turn it into a host readback just to flatten BG/OBJ.
                    skipCpuRead = true;
#endif
                }
                else if ((context.NativeCaptureWrittenBlocks[bank]
                    & (1u << physicalBlock)) != 0u)
                {
                    // The current native semantic frame has already written
                    // this physical block, but provenance publication is a
                    // post-submit event. Do not flatten stale CPU VRAM into
                    // the line that will consume the GPU-resident write.
                    skipCpuRead = true;
                }
            }
            if (!skipCpuRead)
                result |= gpu.VRAM[bank][physicalAddress];
        }
        value |= static_cast<u64>(result) << (byteOffset * 8u);
    }
    return value;
}

template <u32 MappingBytes>
u64 ReadMappedWord(
    const MappedReadContext& context,
    const melonDS::GPU& gpu,
    const u32* mappings,
    u32 mappingCount,
    u32 address) noexcept
{
    if (context.Input)
        ++context.Input->Recorder.MappedReadWordCalls;

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const bool proofMaterialize = ProofMaterializeMappedCaptureEnabled();
#else
    constexpr bool proofMaterialize = false;
#endif
    const u32 mappingIndex = address / MappingBytes;
    const u32 mappingOffset = address % MappingBytes;
    const bool singleMapping = mappingIndex < mappingCount
        && mappingOffset + sizeof(u64) <= MappingBytes
        && mappings != nullptr;
    const u32 mappedBankMask = singleMapping
        ? mappings[mappingIndex] & 0x1FFu : 0u;

    // Capture-disabled frames are the overwhelmingly common case and perform
    // more than one hundred thousand mapped words at 1x.  Preserve the direct
    // flatten shape unless either the line-time mapping or the current-frame
    // write-ahead state can contain a native GPU owner.  The bank summary is
    // computed once per snapshot context, not once per word.
    if (!proofMaterialize && singleMapping
        && (!context.Input
            || ((context.Input->NativeCaptureOverlayAny
                    & NativeCaptureOverlayAnyMask) == 0u
                && context.NativeCaptureWrittenBankMask == 0u)))
    {
        if (context.Input)
            ++context.Input->Recorder.MappedReadFastPathCalls;
        u32 bankMask = mappedBankMask;
        u64 value = 0u;
        while (bankMask != 0u)
        {
            const u32 bank = static_cast<u32>(__builtin_ctz(bankMask));
            bankMask &= bankMask - 1u;
            u64 bankValue = 0u;
            std::memcpy(
                &bankValue,
                gpu.VRAM[bank] + (address & gpu.VRAMMask[bank]),
                sizeof(bankValue));
            value |= bankValue;
        }
        return value;
    }

    // The shader mapping row is sampled at a different temporal boundary from
    // this host snapshot. Resolve the fast read from the live renderer owner
    // plus current-frame write-ahead state, exactly like ReadMappedWordSlow,
    // rather than replaying a row owner that a later CPU/DMA write superseded.
    const u32 nativeMask = singleMapping
        ? NativeCaptureMaskForRead(context, gpu, mappedBankMask, address)
            | NativeCaptureWrittenMaskForRead(
                context, gpu, mappedBankMask, address)
        : 0u;

    const bool physicalReadFits = [&] {
        if (!singleMapping)
            return false;
        const u32 bankMask = mappedBankMask;
        for (u32 bank = 0u; bank < 9u; ++bank)
        {
            if ((bankMask & (1u << bank)) == 0u)
                continue;
            const u32 physicalAddress = address & gpu.VRAMMask[bank];
            if (static_cast<u64>(physicalAddress) + sizeof(u64)
                > static_cast<u64>(gpu.VRAMMask[bank]) + 1u)
            {
                return false;
            }
        }
        return true;
    }();

    // The proof switch deliberately takes the old ownership-aware path so a
    // diagnostic run can materialize a native block and report the attempted
    // host read. Shipping and ordinary developer runs use the single mapping
    // lookup plus one active-bank scan below.
    if (!singleMapping || !physicalReadFits || proofMaterialize
        || (IsCaptureMappedSection(context.Section)
            && !context.NativeMappingReady))
    {
        if (context.Input)
            ++context.Input->Recorder.MappedReadSlowPathCalls;
        return ReadMappedWordSlow<MappingBytes>(
            context, gpu, mappings, mappingCount, address);
    }

    if (context.Input)
        ++context.Input->Recorder.MappedReadFastPathCalls;

    u32 bankMask = mappedBankMask;
    bankMask &= ~nativeMask;
    u64 value = 0u;
    while (bankMask != 0u)
    {
        const u32 bank = static_cast<u32>(__builtin_ctz(bankMask));
        bankMask &= bankMask - 1u;
        u64 bankValue = 0u;
        const u32 physicalAddress = address & gpu.VRAMMask[bank];
        std::memcpy(
            &bankValue,
            gpu.VRAM[bank] + physicalAddress,
            sizeof(bankValue));
        value |= bankValue;
    }
    return value;
}

template <u32 MappingBytes>
void CopyMappedVRAMBlocks(
    FrameInput& input,
    u8* destination,
    u32 size,
    const u32* mappings,
    u32 mappingCount,
    const melonDS::GPU& gpu,
    u32 packedOffset,
    const MappedReadContext& context) noexcept
{
    const bool noNativeCapture = !context.Input
        || ((context.Input->NativeCaptureOverlayAny
                & NativeCaptureOverlayAnyMask) == 0u
            && context.NativeCaptureWrittenBankMask == 0u);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const bool directFlatten = !ProofMaterializeMappedCaptureEnabled()
        && noNativeCapture;
#else
    const bool directFlatten = noNativeCapture;
#endif
    static constexpr std::array<u8, DirtyBlockBytes> zeroBlock{};
    std::array<u8, DirtyBlockBytes> source{};
    for (u32 offset = 0; offset < size; offset += DirtyBlockBytes)
    {
        const u32 blockSize = std::min(DirtyBlockBytes, size - offset);
        const u8* flattened = source.data();
        const u32 mappingIndex = offset / MappingBytes;
        const bool directBlock = directFlatten && mappings
            && mappingIndex < mappingCount
            && offset % MappingBytes + blockSize <= MappingBytes;
        if (directBlock)
        {
            // Dirty blocks and all physical VRAM banks are 512-byte aligned,
            // so a block cannot straddle a physical wrap boundary. Flatten a
            // complete block here instead of repeating mapping lookup and bank
            // selection for each of its 64 words. This is byte-for-byte the
            // same OR operation as ReadMappedWord's capture-free fast path.
            const u32 wordCount = blockSize / sizeof(u64);
            input.Recorder.MappedReadWordCalls += wordCount;
            input.Recorder.MappedReadFastPathCalls += wordCount;
            u32 bankMask = mappings[mappingIndex] & 0x1FFu;
            if (bankMask == 0u)
            {
                flattened = zeroBlock.data();
            }
            else
            {
                const u32 firstBank = static_cast<u32>(__builtin_ctz(bankMask));
                bankMask &= bankMask - 1u;
                const u8* firstBankSource = gpu.VRAM[firstBank]
                    + (offset & gpu.VRAMMask[firstBank]);
                if (bankMask == 0u)
                {
                    // Most DS mappings have exactly one owner. Compare the
                    // persistent snapshot directly with that bank and copy
                    // only on change, avoiding a throwaway 512-byte staging
                    // copy for the common unchanged block.
                    flattened = firstBankSource;
                }
                else
                {
                    std::memcpy(source.data(), firstBankSource, blockSize);
                }
                while (bankMask != 0u)
                {
                    const u32 bank = static_cast<u32>(__builtin_ctz(bankMask));
                    bankMask &= bankMask - 1u;
                    const u8* bankSource = gpu.VRAM[bank]
                        + (offset & gpu.VRAMMask[bank]);
                    for (u32 blockOffset = 0u;
                        blockOffset < blockSize;
                        blockOffset += sizeof(u64))
                    {
                        u64 destinationWord = 0u;
                        u64 bankWord = 0u;
                        std::memcpy(
                            &destinationWord,
                            source.data() + blockOffset,
                            sizeof(destinationWord));
                        std::memcpy(
                            &bankWord,
                            bankSource + blockOffset,
                            sizeof(bankWord));
                        destinationWord |= bankWord;
                        std::memcpy(
                            source.data() + blockOffset,
                            &destinationWord,
                            sizeof(destinationWord));
                    }
                }
            }
        }
        else
        {
            source.fill(0u);
            for (u32 blockOffset = 0;
                blockOffset < blockSize;
                blockOffset += sizeof(u64))
            {
                const u64 value = ReadMappedWord<MappingBytes>(
                    context, gpu, mappings, mappingCount, offset + blockOffset);
                std::memcpy(source.data() + blockOffset, &value, sizeof(value));
            }
        }

        ++input.Recorder.BlocksScanned;
        input.Recorder.BytesScanned += blockSize;
        if (std::memcmp(destination + offset, flattened, blockSize) == 0)
            continue;
        std::memcpy(destination + offset, flattened, blockSize);
        ++input.Recorder.BlocksCopied;
        input.Recorder.BytesCopied += blockSize;
        MarkDirtyRange(input, packedOffset + offset, blockSize);
    }
}

u64 HashTimelineBlock(const u8* source) noexcept
{
    // FNV-1a is sufficient here because the hash is only a lookup hint; every
    // hit is verified with a full memcmp before an existing version is reused.
    u64 hash = 1469598103934665603ull;
    for (u32 i = 0; i < DirtyBlockBytes; ++i)
    {
        hash ^= source[i];
        hash *= 1099511628211ull;
    }
    return hash == 0u ? 1u : hash;
}

u64 HashTimelineWords(const u32* source, u32 wordCount) noexcept
{
    u64 hash = 1469598103934665603ull;
    for (u32 i = 0; i < wordCount; ++i)
    {
        const u32 word = source[i];
        for (u32 byte = 0; byte < sizeof(word); ++byte)
        {
            hash ^= (word >> (byte * 8u)) & 0xFFu;
            hash *= 1099511628211ull;
        }
    }
    return hash == 0u ? 1u : hash;
}

u32 AppendTimelineDelta(FrameInput& input, const u8* source) noexcept
{
    if (!source)
    {
        input.TimelineOverflow = 1u;
        return 0u;
    }

    const u64 hash = HashTimelineBlock(source);
    constexpr u32 hashMask = TimelineHashTableSize - 1u;
    u32 slot = static_cast<u32>(hash) & hashMask;
    u32 emptySlot = TimelineHashTableSize;
    for (u32 probe = 0; probe < TimelineHashTableSize; ++probe)
    {
        const u64 storedHash = input.TimelineHashKeys[slot];
        if (storedHash == 0u)
        {
            emptySlot = slot;
            break;
        }
        if (storedHash == hash)
        {
            const u32 version = input.TimelineHashVersions[slot];
            if (version != 0u
                && std::memcmp(
                    input.TimelinePayload.data()
                        + static_cast<std::size_t>(version - 1u) * DirtyBlockBytes,
                    source,
                    DirtyBlockBytes)
                    == 0)
            {
                // A high-churn DMA/remap can expose the same block contents
                // many times. Reusing the immutable payload keeps the dense
                // per-line index lossless without consuming one delta per
                // observation.
                ++input.TimelineMutationSerial;
                return version;
            }
        }
        slot = (slot + 1u) & hashMask;
    }

    if (input.TimelineDeltaCount >= MaxMemoryDeltas)
    {
        input.TimelineOverflow = 1u;
        return 0u;
    }

    const u32 version = ++input.TimelineDeltaCount;
    std::memcpy(
        input.TimelinePayload.data()
            + static_cast<std::size_t>(version - 1u) * DirtyBlockBytes,
        source,
        DirtyBlockBytes);
    MarkDirtyRange(
        input,
        PackedTimelinePayloadBase * sizeof(u32)
            + (version - 1u) * DirtyBlockBytes,
        DirtyBlockBytes);
    if (emptySlot != TimelineHashTableSize)
    {
        input.TimelineHashKeys[emptySlot] = hash;
        input.TimelineHashVersions[emptySlot] = version;
    }
    ++input.TimelineMutationSerial;
    return version;
}

template <u32 MappingBytes>
void CaptureMappedMemoryBlocks(
    FrameInput& input,
    std::array<u32, TimelineBlockCount>& currentVersions,
    u8* current,
    u32 size,
    const u32* mappings,
    u32 mappingCount,
    const melonDS::GPU& gpu,
    u32 blockBase,
    const MappedReadContext& context) noexcept
{
    std::array<u8, DirtyBlockBytes> source{};
    for (u32 offset = 0; offset < size; offset += DirtyBlockBytes)
    {
        source.fill(0u);
        const u32 blockSize = std::min(DirtyBlockBytes, size - offset);
        for (u32 blockOffset = 0; blockOffset < blockSize; blockOffset += sizeof(u64))
        {
            const u64 value = ReadMappedWord<MappingBytes>(
                context, gpu, mappings, mappingCount, offset + blockOffset);
            std::memcpy(source.data() + blockOffset, &value, sizeof(value));
        }

        ++input.Recorder.BlocksScanned;
        input.Recorder.BytesScanned += blockSize;
        if (std::memcmp(current + offset, source.data(), blockSize) == 0)
            continue;

        const u32 block = blockBase + offset / DirtyBlockBytes;
        if (block >= currentVersions.size())
        {
            input.TimelineOverflow = 1u;
            continue;
        }
        const u32 version = AppendTimelineDelta(input, source.data());
        if (version == 0u)
            continue;
        std::memcpy(current + offset, source.data(), blockSize);
        ++input.Recorder.BlocksCopied;
        input.Recorder.BytesCopied += blockSize;
        currentVersions[block] = version;
    }
}

template <u32 MappingBytes>
void CaptureMappedPhysicalMemoryBlock(
    FrameInput& input,
    std::array<u32, TimelineBlockCount>& currentVersions,
    u8* current,
    u32 size,
    const u32* mappings,
    u32 mappingCount,
    const melonDS::GPU& gpu,
    u32 blockBase,
    u32 bank,
    u32 physicalBlock,
    const MappedReadContext& context) noexcept
{
    if (!current || !mappings || bank >= 9u)
        return;

    const u32 bankSize = gpu.VRAMMask[bank] + 1u;
    const u32 physicalOffset = physicalBlock * DirtyBlockBytes;
    if (physicalOffset >= bankSize)
        return;

    std::array<u8, DirtyBlockBytes> source{};
    for (u32 mappingIndex = 0; mappingIndex < mappingCount; ++mappingIndex)
    {
        if ((mappings[mappingIndex] & (1u << bank)) == 0u)
        {
            continue;
        }

        const u32 offset = mappingIndex * MappingBytes;
        if (offset >= size)
            continue;
        const u32 blockSize = std::min(MappingBytes, size - offset);
        const u32 relative = (physicalOffset - (offset & gpu.VRAMMask[bank]))
            & gpu.VRAMMask[bank];
        if (relative + DirtyBlockBytes > blockSize
            || (relative % DirtyBlockBytes) != 0u)
            continue;

        source.fill(0u);
        for (u32 blockOffset = 0; blockOffset < DirtyBlockBytes; blockOffset += sizeof(u64))
        {
            const u64 value = ReadMappedWord<MappingBytes>(
                context, gpu, mappings, mappingCount,
                offset + relative + blockOffset);
            std::memcpy(source.data() + blockOffset, &value, sizeof(value));
        }

        const u32 logicalBlockOffset = relative;
        const u32 logicalBlock = blockBase + (offset + logicalBlockOffset) / DirtyBlockBytes;
        ++input.Recorder.BlocksScanned;
        input.Recorder.BytesScanned += DirtyBlockBytes;
        if (logicalBlock >= currentVersions.size()
            || std::memcmp(current + offset + logicalBlockOffset,
                source.data(), DirtyBlockBytes) == 0)
        {
            continue;
        }
        const u32 version = AppendTimelineDelta(input, source.data());
        if (version == 0u)
            continue;
        std::memcpy(current + offset + logicalBlockOffset, source.data(), DirtyBlockBytes);
        ++input.Recorder.BlocksCopied;
        input.Recorder.BytesCopied += DirtyBlockBytes;
        currentVersions[logicalBlock] = version;
    }
}

void CaptureDirectMemoryBlocks(
    FrameInput& input,
    std::array<u32, TimelineBlockCount>& currentVersions,
    u8* current,
    const u8* source,
    u32 size,
    u32 blockBase) noexcept
{
    std::array<u8, DirtyBlockBytes> block{};
    for (u32 offset = 0; offset < size; offset += DirtyBlockBytes)
    {
        const u32 blockSize = std::min(DirtyBlockBytes, size - offset);
        block.fill(0u);
        std::memcpy(block.data(), source + offset, blockSize);
        ++input.Recorder.BlocksScanned;
        input.Recorder.BytesScanned += blockSize;
        if (std::memcmp(current + offset, block.data(), blockSize) == 0)
            continue;

        const u32 blockIndex = blockBase + offset / DirtyBlockBytes;
        if (blockIndex >= currentVersions.size())
        {
            input.TimelineOverflow = 1u;
            continue;
        }
        const u32 version = AppendTimelineDelta(input, block.data());
        if (version == 0u)
            continue;
        std::memcpy(current + offset, block.data(), blockSize);
        ++input.Recorder.BlocksCopied;
        input.Recorder.BytesCopied += blockSize;
        currentVersions[blockIndex] = version;
    }
}

void CaptureDirectMemoryBlockImpl(
    FrameInput& input,
    std::array<u32, TimelineBlockCount>& currentVersions,
    u8* current,
    const u8* source,
    u32 size,
    u32 blockBase,
    u32 block) noexcept
{
    if (!current || !source || block * DirtyBlockBytes >= size)
        return;

    std::array<u8, DirtyBlockBytes> contents{};
    const u32 offset = block * DirtyBlockBytes;
    const u32 blockSize = std::min(DirtyBlockBytes, size - offset);
    ++input.Recorder.BlocksScanned;
    input.Recorder.BytesScanned += blockSize;
    std::memcpy(contents.data(), source + offset, blockSize);
    const u32 blockIndex = blockBase + block;
    if (blockIndex >= currentVersions.size()
        || std::memcmp(current + offset, contents.data(), blockSize) == 0)
    {
        return;
    }
    const u32 version = AppendTimelineDelta(input, contents.data());
    if (version == 0u)
        return;
    std::memcpy(current + offset, contents.data(), blockSize);
    ++input.Recorder.BlocksCopied;
    input.Recorder.BytesCopied += blockSize;
    currentVersions[blockIndex] = version;
}
}

FrameRecorder::FrameRecorder(const melonDS::GPU& gpu) noexcept
    : GPU(gpu)
{
}

void FrameRecorder::Reset() noexcept
{
    ClearFrameInput(Input);
    Valid = false;
    LineSeen.fill(false);
    EngineLineCount[0] = 0;
    EngineLineCount[1] = 0;
    // MemorySnapshot contains the full private BG/OBJ mirrors. Value-assigning
    // either element materializes an ~800 KiB temporary on the Windows thread
    // stack, which makes the developer purity command overflow before the
    // recorder runs. The aggregate is trivially copyable, so clear it in place.
    std::memset(CurrentEngine, 0, sizeof(CurrentEngine));
    CurrentPalette.fill(0u);
    CurrentOAM.fill(0u);
    CurrentDisplayFIFO.fill(0u);
    CurrentLCDVRAM.fill(0u);
    CurrentTimelineVersion.fill(0u);
    NativeCaptureWrittenBlocks.fill(0u);
    NativeCaptureMappingBuilt.fill(false);
    NativeCaptureMappingLines.fill(ScreenHeight);
    NativeCaptureMappingSources = {};
    NativeCaptureMappingWrittenBlocks = {};
    NativeCaptureMappingProvenanceSerial = {};
    LastTimelineMutationSerial = 0u;
    LastSpriteTimelineMutationSerial = 0u;
    MemoryBaselineReady = false;
    SpriteLatchSeen.fill(false);
    PendingEngineAOBJ.fill(0u);
    PendingEngineBOBJ.fill(0u);
    PendingOAM.fill(0u);
    PendingNativeCaptureSpriteOBJMapping.fill(0u);
    PendingSpriteLatchReady = false;
    LastJournalSequence = 0u;
    CaptureStartLine = CaptureStartLineNone;
    CaptureStateCnt = 0u;
    CaptureStateEnabled = false;
    CaptureAddressLogCount = 0u;
    CaptureAddressLogOverflow = 0u;
    CaptureAddressLog.fill(CaptureAddressDiagnostic{});
    RecorderStartNs = 0u;
}

void FrameRecorder::BeginFrame(u64 frame) noexcept
{
    const FrameGeneration previousGeneration = Input.Generation;
    const bool hadPreviousFrame = Valid;
    if (!Valid)
        ClearFrameInput(Input);
    else
    {
        // Retain the coherent memory mirrors so changed blocks can be copied
        // into both the CPU frame and the backend's device-resident mirror.
        std::fill(Input.Lines.begin(), Input.Lines.end(), LineState{});
        std::fill(Input.ScreenSource.begin(), Input.ScreenSource.end(), 0u);
        std::fill(Input.TimelineRowIds.begin(), Input.TimelineRowIds.end(), 0xFFFFFFFFu);
        std::fill(Input.SpriteTimelineRowIds.begin(), Input.SpriteTimelineRowIds.end(), 0xFFFFFFFFu);
        Input.DirtyRangeCount = 0u;
    }
    Input.Recorder = {};
    Input.Generation.Frame = frame;
    if (hadPreviousFrame)
    {
        Input.Generation.ContentGeneration = previousGeneration.ContentGeneration;
        Input.Generation.VRAMGeneration = previousGeneration.VRAMGeneration;
        Input.Generation.CaptureGeneration = previousGeneration.CaptureGeneration;
        Input.Generation.NativeCaptureMappingGeneration =
            previousGeneration.NativeCaptureMappingGeneration;
    }
    else
    {
        // A newly created recorder has no device mirror history.  The first
        // slot use still performs a full upload, while the non-zero seed makes
        // a later slot generation comparison unambiguous after the first frame.
        Input.Generation.ContentGeneration = 1u;
        Input.Generation.VRAMGeneration = 1u;
        Input.Generation.CaptureGeneration = 1u;
        Input.Generation.NativeCaptureMappingGeneration = 1u;
    }
    Input.CaptureCnt = GPU.CaptureCnt;
    Input.CaptureEnable = GPU.CaptureEnable ? 1u : 0u;
    Input.NativeCaptureOverlayAny = 0u;
    Input.ScreenSwap = GPU.ScreenSwap ? 1u : 0u;
    Input.ScreensEnabled = GPU.ScreensEnabled ? 1u : 0u;
    Input.LCDVRAMMap = GPU.VRAMMap_LCDC;
    RefreshCaptureProvenance();
    MarkDirtyRange(Input, 0u, PackedHeaderWords * sizeof(u32));
    MarkDirtyRange(Input, PackedHeaderWords * sizeof(u32),
        PackedLinesWords * sizeof(u32));
    MarkDirtyRange(Input, PackedRouteBase * sizeof(u32),
        PackedRouteWords * sizeof(u32));
    Input.TimelineDeltaCount = 0u;
    Input.TimelineOverflow = 0u;
    std::fill(Input.TimelineRowIds.begin(), Input.TimelineRowIds.end(), 0xFFFFFFFFu);
    std::fill(Input.SpriteTimelineRowIds.begin(), Input.SpriteTimelineRowIds.end(), 0xFFFFFFFFu);
    std::fill(Input.TimelineHashKeys.begin(), Input.TimelineHashKeys.end(), 0u);
    std::fill(Input.TimelineHashVersions.begin(), Input.TimelineHashVersions.end(), 0u);
    std::fill(Input.TimelineRowHashKeys.begin(), Input.TimelineRowHashKeys.end(), 0u);
    std::fill(Input.TimelineRowHashRows.begin(), Input.TimelineRowHashRows.end(), 0xFFFFFFFFu);
    std::fill(Input.SpriteTimelineRowHashKeys.begin(),
        Input.SpriteTimelineRowHashKeys.end(), 0u);
    std::fill(Input.SpriteTimelineRowHashRows.begin(),
        Input.SpriteTimelineRowHashRows.end(), 0xFFFFFFFFu);
    MemoryBaselineReady = false;
    NativeCaptureWrittenBlocks.fill(0u);
    NativeCaptureMappingBuilt.fill(false);
    NativeCaptureMappingLines.fill(ScreenHeight);
    NativeCaptureMappingSources = {};
    NativeCaptureMappingWrittenBlocks = {};
    NativeCaptureMappingProvenanceSerial = {};
    CurrentTimelineVersion.fill(0u);
    Input.TimelineRowCount = 0u;
    Input.SpriteTimelineRowCount = 0u;
    Input.TimelineMutationSerial = 0u;
    LastTimelineMutationSerial = 0u;
    LastSpriteTimelineMutationSerial = 0u;
    LastJournalSequence = GPU.GetGPU2DWriteJournalSequence();
    LineSeen.fill(false);
    SpriteLatchSeen.fill(false);
    EngineLineCount[0] = 0;
    EngineLineCount[1] = 0;
    Valid = false;
    CaptureStartLine = CaptureStartLineNone;
    CaptureStateCnt = 0u;
    CaptureStateEnabled = false;
    CaptureAddressLogCount = 0u;
    CaptureAddressLogOverflow = 0u;
    CaptureAddressLog.fill(CaptureAddressDiagnostic{});
    RecorderStartNs = NowNanoseconds();
}

void FrameRecorder::RefreshCaptureProvenance() noexcept
{
    // A byte difference between CPU VRAM and a retained recorder snapshot
    // cannot determine capture authority. Native-owned capture state may
    // legitimately be newer than CPU VRAM. Authority changes are event-driven,
    // never inferred from memcmp.
    const Renderer& renderer = GPU.GetRenderer();
    for (u32 bank = 0; bank < CapturePhysicalBanks; ++bank)
    {
        for (u32 physicalBlock = 0;
            physicalBlock < CapturePhysicalBlocksPerBank;
            ++physicalBlock)
        {
            Input.LCDVRAMProvenance[
                bank * CapturePhysicalBlocksPerBank + physicalBlock] =
                renderer.GetCaptureBlockProvenance(bank, physicalBlock);
        }
    }
}

void FrameRecorder::MarkInputCaptureBlockCpuCoherent(
    u32 bank,
    u32 physicalBlock) noexcept
{
    if (bank >= CapturePhysicalBanks
        || physicalBlock >= CapturePhysicalBlocksPerBank)
    {
        return;
    }

    CaptureBlockProvenance& provenance = Input.LCDVRAMProvenance[
        bank * CapturePhysicalBlocksPerBank + physicalBlock];
    provenance = {};
    provenance.Owner = CaptureOwner::CpuCoherent;
}

void FrameRecorder::CaptureNativeMappingForLine(
    u32 line, bool spriteLatch) noexcept
{
    if (line >= ScreenHeight)
        return;

    const u32 mode = spriteLatch ? 1u : 0u;
    std::array<u32, 64> source{};
    std::copy_n(GPU.VRAMMap_ABG, 32u, source.begin());
    std::copy_n(GPU.VRAMMap_AOBJ, 16u, source.begin() + 32u);
    std::copy_n(GPU.VRAMMap_BBG, 8u, source.begin() + 48u);
    std::copy_n(GPU.VRAMMap_BOBJ, 8u, source.begin() + 56u);
    if (NativeCaptureMappingBuilt[mode]
        && NativeCaptureMappingLines[mode] == line
        && NativeCaptureMappingSources[mode] == source
        && NativeCaptureMappingWrittenBlocks[mode]
            == NativeCaptureWrittenBlocks
        && NativeCaptureMappingProvenanceSerial[mode]
            == GPU.GetRenderer().GetCaptureProvenanceSerial())
    {
        return;
    }

    NativeCaptureMappingBuilt[mode] = true;
    NativeCaptureMappingLines[mode] = line;
    NativeCaptureMappingSources[mode] = source;
    NativeCaptureMappingWrittenBlocks[mode] = NativeCaptureWrittenBlocks;
    NativeCaptureMappingProvenanceSerial[mode] =
        GPU.GetRenderer().GetCaptureProvenanceSerial();
    ++Input.Recorder.NativeMappingBuildCalls;

    const Renderer& renderer = GPU.GetRenderer();
    const auto noteNativeCaptureOverlay = [&](u32 mappingValue) {
        if ((mappingValue & (NativeCaptureBankMask
                | NativeCaptureOverlayPresent)) == 0u
            || (Input.NativeCaptureOverlayAny
                & NativeCaptureOverlayAnyMask) != 0u)
        {
            return;
        }

        Input.NativeCaptureOverlayAny |= NativeCaptureOverlayAnyMask;
        // BeginFrame() dirties the initial zero value before scanline
        // mapping is built.  The first native-owned mapping can appear later
        // in the frame, so the packed header must be dirtied at the same
        // temporal boundary or a reused GPU slot will retain header[15] == 0
        // and bypass the otherwise valid per-entry ownership masks.
        MarkDirtyRange(Input, 15u * sizeof(u32), sizeof(u32));
    };
    const auto nativeMaskForEntry = [&](const u32* mappings, u32 count) {
        std::array<u32, NativeCaptureBGMappingStride> result{};
        const u32 limitedCount = std::min<u32>(
            count, static_cast<u32>(result.size()));
        bool any = false;
        for (u32 mappingIndex = 0u; mappingIndex < limitedCount; ++mappingIndex)
        {
            const u32 bankMask = mappings[mappingIndex];
            u32 nativeMask = 0u;
            const u32 logicalAddress = mappingIndex * 16u * 1024u;
            for (u32 bank = 0u; bank < CapturePhysicalBanks; ++bank)
            {
                if ((bankMask & (1u << bank)) == 0u)
                    continue;
                const u32 physicalAddress =
                    logicalAddress & GPU.VRAMMask[bank];
                const u32 physicalBlock =
                    physicalAddress / CapturePhysicalBlockBytes;
                if (IsNativeCaptureOwner(
                        renderer.GetCaptureBlockProvenance(
                            bank, physicalBlock).Owner)
                    || (NativeCaptureWrittenBlocks[bank]
                        & (1u << physicalBlock)) != 0u)
                {
                    nativeMask |= 1u << bank;
                }
            }
            result[mappingIndex] = nativeMask & NativeCaptureBankMask;
            any = any || result[mappingIndex] != 0u;
        }
        if (limitedCount != 0u && any)
            result[0] |= NativeCaptureOverlayPresent;
        return result;
    };

    bool mappingChanged = false;
    const auto writeMappingRow = [&](u32* destination,
                                     u32 count,
                                     const u32* mappings,
                                     u32 packedOffset) {
        const auto masks = nativeMaskForEntry(mappings, count);
        if (std::memcmp(destination, masks.data(), count * sizeof(u32)) == 0)
            return false;
        std::memcpy(destination, masks.data(), count * sizeof(u32));
        ++Input.Recorder.NativeMappingRowsUploaded;
        Input.Recorder.NativeMappingBytesUploaded += count * sizeof(u32);
        MarkDirtyRange(
            Input, packedOffset * sizeof(u32), count * sizeof(u32));
        return true;
    };

    const u32* bgMappings[2] = {GPU.VRAMMap_ABG, GPU.VRAMMap_BBG};
    const u32 bgCounts[2] = {32u, 8u};
    const u32* objMappings[2] = {GPU.VRAMMap_AOBJ, GPU.VRAMMap_BOBJ};
    const u32 objCounts[2] = {16u, 8u};

    if (!spriteLatch)
    {
        const u32 bgBase = line * NativeCaptureBGMappingStride;
        u32* bgRow = Input.NativeCaptureBGMapping.data() + bgBase;
        for (u32 engine = 0u; engine < 2u; ++engine)
        {
            const u32 offset = engine == 0u ? 0u : 32u;
            mappingChanged = writeMappingRow(
                bgRow + offset, bgCounts[engine], bgMappings[engine],
                PackedNativeCaptureBGMappingBase
                    + line * NativeCaptureBGMappingStride + offset)
                || mappingChanged;
            noteNativeCaptureOverlay(bgRow[offset]);
            if ((bgRow[offset] & NativeCaptureOverlayPresent) != 0u)
                ++Input.Recorder.BGOverlaySlowPath;
            else
                ++Input.Recorder.BGOverlayFastPath;
        }
        const u32 objBase = line * NativeCaptureOBJMappingStride;
        u32* objRow = Input.NativeCaptureOBJMapping.data() + objBase;
        for (u32 engine = 0u; engine < 2u; ++engine)
        {
            const u32 offset = engine == 0u ? 0u : 16u;
            mappingChanged = writeMappingRow(
                objRow + offset, objCounts[engine], objMappings[engine],
                PackedNativeCaptureOBJMappingBase
                    + line * NativeCaptureOBJMappingStride + offset)
                || mappingChanged;
            noteNativeCaptureOverlay(objRow[offset]);
            if ((objRow[offset] & NativeCaptureOverlayPresent) != 0u)
                ++Input.Recorder.OBJOverlaySlowPath;
            else
                ++Input.Recorder.OBJOverlayFastPath;
        }
        if (mappingChanged)
            ++Input.Generation.NativeCaptureMappingGeneration;
        return;
    }

    u32* spriteRow = Input.NativeCaptureSpriteOBJMapping.data()
        + line * NativeCaptureOBJMappingStride;
    for (u32 engine = 0u; engine < 2u; ++engine)
    {
        const u32 offset = engine == 0u ? 0u : 16u;
        mappingChanged = writeMappingRow(
            spriteRow + offset, objCounts[engine], objMappings[engine],
            PackedNativeCaptureSpriteOBJMappingBase
                + line * NativeCaptureOBJMappingStride + offset)
            || mappingChanged;
        noteNativeCaptureOverlay(spriteRow[offset]);
        if ((spriteRow[offset] & NativeCaptureOverlayPresent) != 0u)
            ++Input.Recorder.OBJOverlaySlowPath;
        else
            ++Input.Recorder.OBJOverlayFastPath;
    }
    if (line == 0u)
    {
        std::memcpy(
            PendingNativeCaptureSpriteOBJMapping.data(),
            spriteRow,
            PendingNativeCaptureSpriteOBJMapping.size() * sizeof(u32));
    }
    if (mappingChanged)
        ++Input.Generation.NativeCaptureMappingGeneration;
}

void FrameRecorder::CommitNativeCaptureWriteAheadForLine(u32 line) noexcept
{
    if (line >= ScreenHeight
        || !GPU.GetRenderer().UsesNativeGPU2DProducerForFrame())
    {
        return;
    }

    const LineState& state = Input.Lines[line];
    if (state.CaptureEnable == 0u)
        return;

    const u32 bank = (state.CaptureCnt >> 16u) & 3u;
    if (bank >= CapturePhysicalBanks
        || (state.LCDVRAMMap & (1u << bank)) == 0u)
    {
        return;
    }

    const u32 sizeCode = (state.CaptureCnt >> 20u) & 3u;
    const u32 width = CaptureWidthForSize(sizeCode);
    const u32 height = CaptureHeightForSize(sizeCode);
    if (line >= height)
        return;

    const u32 first = WrapLCDCByte(
        CaptureOffsetBytes((state.CaptureCnt >> 18u) & 3u)
        + line * width * 2u);
    const u32 last = WrapLCDCByte(first + width * 2u - 1u);
    NativeCaptureWrittenBlocks[bank] |= static_cast<u8>(
        1u << (first / CapturePhysicalBlockBytes));
    NativeCaptureWrittenBlocks[bank] |= static_cast<u8>(
        1u << (last / CapturePhysicalBlockBytes));
}

void FrameRecorder::ApplyPendingNativeSpriteMapping() noexcept
{
    u32* destination = Input.NativeCaptureSpriteOBJMapping.data();
    if (std::memcmp(
            destination,
            PendingNativeCaptureSpriteOBJMapping.data(),
            PendingNativeCaptureSpriteOBJMapping.size() * sizeof(u32)) == 0)
    {
        return;
    }
    std::memcpy(
        destination,
        PendingNativeCaptureSpriteOBJMapping.data(),
        PendingNativeCaptureSpriteOBJMapping.size() * sizeof(u32));
    MarkDirtyRange(
        Input,
        PackedNativeCaptureSpriteOBJMappingBase * sizeof(u32),
        PendingNativeCaptureSpriteOBJMapping.size() * sizeof(u32));
}

void FrameRecorder::CaptureAllMappedMemoryForLine(u32 line) noexcept
{
    CaptureMappedMemoryForLine(
        line, 0u, 0u, CurrentEngine[0].BGVRAM.data(), CurrentEngine[0].BGSize,
        GPU.VRAMMap_ABG, 32u, 16u * 1024u,
        TimelineEngineBaseBlock + 0u);
    CaptureMappedMemoryForLine(
        line, 0u, 1u, CurrentEngine[0].OBJVRAM.data(), CurrentEngine[0].OBJSize,
        GPU.VRAMMap_AOBJ, 16u, 16u * 1024u,
        TimelineEngineBaseBlock + TimelineEngineBGBlocks);
    CaptureMappedMemoryForLine(
        line, 0u, 2u, CurrentEngine[0].BGExtendedPalette.data(),
        CurrentEngine[0].BGExtendedPaletteSize, GPU.VRAMMap_ABGExtPal, 4u,
        8u * 1024u,
        TimelineEngineBaseBlock + TimelineEngineBGBlocks + TimelineEngineOBJBlocks);
    CaptureMappedMemoryForLine(
        line, 0u, 3u, CurrentEngine[0].OBJExtendedPalette.data(),
        CurrentEngine[0].OBJExtendedPaletteSize, &GPU.VRAMMap_AOBJExtPal, 1u,
        8u * 1024u,
        TimelineEngineBaseBlock + TimelineEngineBGBlocks + TimelineEngineOBJBlocks
            + TimelineEngineBGExtBlocks);

    const u32 engine1Base = TimelineEngineBaseBlock + TimelineEngineBlocks;
    CaptureMappedMemoryForLine(
        line, 1u, 0u, CurrentEngine[1].BGVRAM.data(), CurrentEngine[1].BGSize,
        GPU.VRAMMap_BBG, 8u, 16u * 1024u, engine1Base + 0u);
    CaptureMappedMemoryForLine(
        line, 1u, 1u, CurrentEngine[1].OBJVRAM.data(), CurrentEngine[1].OBJSize,
        GPU.VRAMMap_BOBJ, 8u, 16u * 1024u,
        engine1Base + TimelineEngineBGBlocks);
    CaptureMappedMemoryForLine(
        line, 1u, 2u, CurrentEngine[1].BGExtendedPalette.data(),
        CurrentEngine[1].BGExtendedPaletteSize, GPU.VRAMMap_BBGExtPal, 4u,
        8u * 1024u,
        engine1Base + TimelineEngineBGBlocks + TimelineEngineOBJBlocks);
    CaptureMappedMemoryForLine(
        line, 1u, 3u, CurrentEngine[1].OBJExtendedPalette.data(),
        CurrentEngine[1].OBJExtendedPaletteSize, &GPU.VRAMMap_BOBJExtPal, 1u,
        8u * 1024u,
        engine1Base + TimelineEngineBGBlocks + TimelineEngineOBJBlocks
            + TimelineEngineBGExtBlocks);

    CaptureDirectMemoryForLine(
        GPU.Palette, CurrentPalette.data(),
        static_cast<u32>(CurrentPalette.size()), TimelinePaletteBaseBlock);
    CaptureDirectMemoryForLine(
        GPU.OAM, CurrentOAM.data(),
        static_cast<u32>(CurrentOAM.size()), TimelineOAMBaseBlock);
    CaptureDirectMemoryForLine(
        reinterpret_cast<const u8*>(GPU.DispFIFOBuffer),
        reinterpret_cast<u8*>(CurrentDisplayFIFO.data()),
        static_cast<u32>(CurrentDisplayFIFO.size() * sizeof(u16)),
        TimelineFIFOBaseBlock);
    CaptureCoherentLCDVRAMForLine();
}

void FrameRecorder::CaptureCoherentLCDVRAMForLine() noexcept
{
    for (u32 bank = 0; bank < CapturePhysicalBanks; ++bank)
    {
        for (u32 physicalBlock = 0;
            physicalBlock < CapturePhysicalBlocksPerBank;
            ++physicalBlock)
        {
            const CaptureBlockProvenance& provenance = Input.LCDVRAMProvenance[
                bank * CapturePhysicalBlocksPerBank + physicalBlock];
            if (IsNativeCaptureOwner(provenance.Owner))
                continue;

            const u32 offset = physicalBlock * CapturePhysicalBlockBytes;
            CaptureDirectMemoryForLine(
                GPU.VRAM[bank] + offset,
                CurrentLCDVRAM.data()
                    + static_cast<std::size_t>(bank) * 128u * 1024u + offset,
                CapturePhysicalBlockBytes,
                TimelineLCDVRAMBaseBlock
                    + bank * (128u * 1024u / DirtyBlockBytes)
                    + physicalBlock * CaptureDirtyBlocksPerPhysicalBlock);
        }
    }
}

void FrameRecorder::CaptureJournalWritesForLine(u32 line) noexcept
{
    bool overflow = false;
    const u32 count = GPU.ReadGPU2DWriteJournal(
        LastJournalSequence,
        JournalScratch.data(),
        GPU2DWriteJournalCapacity,
        overflow);
    if (overflow)
    {
        // A bounded ring can lose the exact list under a DMA burst. Preserve
        // correctness with one exceptional full private refresh; shared dirty
        // ownership is still untouched.
        CaptureAllMappedMemoryForLine(line);
        LastJournalSequence = GPU.GetGPU2DWriteJournalSequence();
        return;
    }

    bool mappingChanged = false;
    for (u32 i = 0; i < count; ++i)
    {
        if (static_cast<GPU2DWriteKind>(JournalScratch[i].Kind)
            == GPU2DWriteKind::Mapping)
        {
            mappingChanged = true;
            break;
        }
    }
    if (mappingChanged)
    {
        // Remapping changes the logical view without changing physical bytes.
        // Rebuild the private view once for this boundary, then process no
        // individual events from the same batch a second time.
        CaptureNativeMappingForLine(line, false);
        CaptureAllMappedMemoryForLine(line);
        LastJournalSequence = GPU.GetGPU2DWriteJournalSequence();
        return;
    }

    for (u32 i = 0; i < count; ++i)
    {
        const GPU2DWriteJournalEntry& entry = JournalScratch[i];
        switch (static_cast<GPU2DWriteKind>(entry.Kind))
        {
        case GPU2DWriteKind::VRAM:
        case GPU2DWriteKind::CaptureSync:
            CaptureMappedPhysicalBlock(
                line, 0u, 0u, CurrentEngine[0].BGVRAM.data(), CurrentEngine[0].BGSize,
                GPU.VRAMMap_ABG, 32u, 16u * 1024u,
                TimelineEngineBaseBlock, entry.Bank, entry.Block);
            CaptureMappedPhysicalBlock(
                line, 0u, 1u, CurrentEngine[0].OBJVRAM.data(), CurrentEngine[0].OBJSize,
                GPU.VRAMMap_AOBJ, 16u, 16u * 1024u,
                TimelineEngineBaseBlock + TimelineEngineBGBlocks,
                entry.Bank, entry.Block);
            CaptureMappedPhysicalBlock(
                line, 0u, 2u, CurrentEngine[0].BGExtendedPalette.data(),
                CurrentEngine[0].BGExtendedPaletteSize, GPU.VRAMMap_ABGExtPal, 4u,
                8u * 1024u,
                TimelineEngineBaseBlock + TimelineEngineBGBlocks + TimelineEngineOBJBlocks,
                entry.Bank, entry.Block);
            CaptureMappedPhysicalBlock(
                line, 0u, 3u, CurrentEngine[0].OBJExtendedPalette.data(),
                CurrentEngine[0].OBJExtendedPaletteSize, &GPU.VRAMMap_AOBJExtPal, 1u,
                8u * 1024u,
                TimelineEngineBaseBlock + TimelineEngineBGBlocks + TimelineEngineOBJBlocks
                    + TimelineEngineBGExtBlocks,
                entry.Bank, entry.Block);

            {
                const u32 engine1Base = TimelineEngineBaseBlock + TimelineEngineBlocks;
                CaptureMappedPhysicalBlock(
                    line, 1u, 0u, CurrentEngine[1].BGVRAM.data(), CurrentEngine[1].BGSize,
                    GPU.VRAMMap_BBG, 8u, 16u * 1024u,
                    engine1Base, entry.Bank, entry.Block);
                CaptureMappedPhysicalBlock(
                    line, 1u, 1u, CurrentEngine[1].OBJVRAM.data(), CurrentEngine[1].OBJSize,
                    GPU.VRAMMap_BOBJ, 8u, 16u * 1024u,
                    engine1Base + TimelineEngineBGBlocks,
                    entry.Bank, entry.Block);
                CaptureMappedPhysicalBlock(
                    line, 1u, 2u, CurrentEngine[1].BGExtendedPalette.data(),
                    CurrentEngine[1].BGExtendedPaletteSize, GPU.VRAMMap_BBGExtPal, 4u,
                    8u * 1024u,
                    engine1Base + TimelineEngineBGBlocks + TimelineEngineOBJBlocks,
                    entry.Bank, entry.Block);
                CaptureMappedPhysicalBlock(
                    line, 1u, 3u, CurrentEngine[1].OBJExtendedPalette.data(),
                    CurrentEngine[1].OBJExtendedPaletteSize, &GPU.VRAMMap_BOBJExtPal, 1u,
                    8u * 1024u,
                    engine1Base + TimelineEngineBGBlocks + TimelineEngineOBJBlocks
                        + TimelineEngineBGExtBlocks,
                    entry.Bank, entry.Block);
            }

            if (entry.Bank < 4u)
            {
                CaptureDirectMemoryBlockImpl(
                    Input, CurrentTimelineVersion,
                    CurrentLCDVRAM.data()
                        + static_cast<std::size_t>(entry.Bank) * 128u * 1024u,
                    GPU.VRAM[entry.Bank], 128u * 1024u,
                    TimelineLCDVRAMBaseBlock
                        + entry.Bank * (128u * 1024u / DirtyBlockBytes),
                    entry.Block);
                const u32 physicalBlock =
                    entry.Block / CaptureDirtyBlocksPerPhysicalBlock;
                const CaptureBlockProvenance rendererProvenance =
                    GPU.GetRenderer().GetCaptureBlockProvenance(
                        entry.Bank, physicalBlock);
                const bool softwareOracleWrite =
                    ExactValidationEnabled()
                    && IsNativeCaptureOwner(rendererProvenance.Owner);
                if (!softwareOracleWrite)
                {
                    MarkInputCaptureBlockCpuCoherent(
                        entry.Bank, physicalBlock);
                }
                // Exact validation runs the Software renderer as an oracle.
                // Its Display Capture writes are observational data for the
                // frame timeline, not an emulated CPU/DMA authority event.
                // Keep the native renderer owner intact so the persistent
                // native mirror is never reuploaded from the oracle snapshot.
            }
            break;

        case GPU2DWriteKind::Palette:
            CaptureDirectMemoryBlockImpl(
                Input, CurrentTimelineVersion, CurrentPalette.data(), GPU.Palette,
                static_cast<u32>(CurrentPalette.size()), TimelinePaletteBaseBlock,
                entry.Block);
            break;

        case GPU2DWriteKind::OAM:
            CaptureDirectMemoryBlockImpl(
                Input, CurrentTimelineVersion, CurrentOAM.data(), GPU.OAM,
                static_cast<u32>(CurrentOAM.size()), TimelineOAMBaseBlock,
                entry.Block);
            break;

        case GPU2DWriteKind::FIFO:
            CaptureDirectMemoryBlockImpl(
                Input, CurrentTimelineVersion,
                reinterpret_cast<u8*>(CurrentDisplayFIFO.data()),
                reinterpret_cast<const u8*>(GPU.DispFIFOBuffer),
                static_cast<u32>(CurrentDisplayFIFO.size() * sizeof(u16)),
                TimelineFIFOBaseBlock, entry.Block);
            break;

        case GPU2DWriteKind::Mapping:
            // Handled by the batch pre-pass above.
            break;
        }
    }
    LastJournalSequence = GPU.GetGPU2DWriteJournalSequence();
}

void FrameRecorder::CaptureMemoryForLine(u32 line) noexcept
{
    if (line >= ScreenHeight)
        return;

    // ReadMappedWord uses the per-line native mask as its fast-path input.
    // Establish it before the frame-start snapshot or journal delta is read.
    CaptureNativeMappingForLine(line, false);

    if (!MemoryBaselineReady || line == 0u)
    {
        // The baseline is captured before line 0 is evaluated. This is the
        // only full snapshot; every later line carries only changed blocks.
        SnapshotEngine(0u, line);
        SnapshotEngine(1u, line);
        CopyChangedBlocks(
            Input, Input.Palette.data(), GPU.Palette,
            static_cast<u32>(Input.Palette.size()), PackedPaletteBase * sizeof(u32));
        CopyChangedBlocks(
            Input, Input.OAM.data(), GPU.OAM,
            static_cast<u32>(Input.OAM.size()), PackedOAMBase * sizeof(u32));
        CopyChangedBlocks(
            Input,
            reinterpret_cast<u8*>(Input.DisplayFIFO.data()),
            reinterpret_cast<const u8*>(GPU.DispFIFOBuffer),
            static_cast<u32>(Input.DisplayFIFO.size() * sizeof(u16)),
            PackedFIFOBase * sizeof(u32));
        for (u32 bank = 0; bank < CapturePhysicalBanks; ++bank)
        {
            CopyCoherentLCDVRAMBlocks(
                Input,
                bank,
                Input.LCDVRAM.data() +
                    static_cast<std::size_t>(bank) * 128u * 1024u,
                GPU.VRAM[bank]);
        }

        CurrentEngine[0] = Input.Engine[0];
        CurrentEngine[1] = Input.Engine[1];
        std::memcpy(CurrentPalette.data(), Input.Palette.data(), CurrentPalette.size());
        std::memcpy(CurrentOAM.data(), Input.OAM.data(), CurrentOAM.size());
        std::memcpy(
            CurrentDisplayFIFO.data(), Input.DisplayFIFO.data(),
            CurrentDisplayFIFO.size() * sizeof(u16));
        std::memcpy(CurrentLCDVRAM.data(), Input.LCDVRAM.data(), CurrentLCDVRAM.size());
        CurrentTimelineVersion.fill(0u);
        MemoryBaselineReady = true;
        LastJournalSequence = GPU.GetGPU2DWriteJournalSequence();
    }
    else
    {
        CaptureJournalWritesForLine(line);
    }

    if (line < ScreenHeight)
    {
        FillTimelineLine(line);
        if (line == 0u)
        {
            if (PendingSpriteLatchReady)
                ApplyPendingSpriteLatch();
            else
            {
                // A renderer reset or a first frame can legitimately arrive
                // without the preceding VCOUNT 262 hook. In that case the
                // current line-0 memory is the only available latch and is
                // already represented by the frame-start versions.
                SpriteLatchSeen[0] = true;
                FillSpriteTimelineLine(0u);
                CaptureNativeMappingForLine(0u, true);
            }
        }
    }
}

void FrameRecorder::RecordSoftwareCaptureLine(u64 nanoseconds) noexcept
{
    ++Input.Recorder.CaptureCPU2DLines;
    Input.Recorder.CaptureCPU2DNs += nanoseconds;
}

void FrameRecorder::CaptureSpriteLatchForLine(u32 line) noexcept
{
    if (line >= ScreenHeight)
        return;
    if (!MemoryBaselineReady)
        CaptureMemoryForLine(0u);

    // This is deliberately the only place where the OBJ/OAM preparation
    // snapshot is latched. GPU::StartHBlank calls DrawSprites(line+1) after
    // DrawScanline(line), matching the hardware one-line-ahead latch. The
    // write journal makes this boundary cheap when the source memory is
    // unchanged; the palette and OBJ extended palette remain on the ordinary
    // line timeline.
    CaptureJournalWritesForLine(line);
    CaptureNativeMappingForLine(line, true);
    FillSpriteTimelineLine(line);
    SpriteLatchSeen[line] = true;

    if (line == 0u)
    {
        // VCOUNT 262 prepares line 0 for the next frame. Keep raw mapped
        // bytes so BeginFrame can rebuild a timeline row after it snapshots
        // the new frame's ordinary line-0 memory.
        std::memcpy(PendingEngineAOBJ.data(), CurrentEngine[0].OBJVRAM.data(),
            PendingEngineAOBJ.size());
        std::memcpy(PendingEngineBOBJ.data(), CurrentEngine[1].OBJVRAM.data(),
            PendingEngineBOBJ.size());
        std::memcpy(PendingOAM.data(), CurrentOAM.data(), PendingOAM.size());
        PendingSpriteLatchReady = true;
    }
}

void FrameRecorder::FillSpriteTimelineLine(u32 line) noexcept
{
    if (line >= ScreenHeight)
        return;

    const u64 dedupStartNs = NowNanoseconds();
    constexpr u32 invalidRow = 0xFFFFFFFFu;
    const u32 oldRow = Input.SpriteTimelineRowIds[line];
    u32 row = invalidRow;
    u64 rowHash = 0u;
    u32 emptyHashSlot = TimelineHashTableSize;
    bool hashMatched = false;
    if (line != 0u
        && Input.TimelineMutationSerial == LastSpriteTimelineMutationSerial
        && Input.SpriteTimelineRowIds[line - 1u] != invalidRow)
    {
        row = Input.SpriteTimelineRowIds[line - 1u];
    }
    else
    {
        std::array<u32, SpriteTimelineBlockCount> current{};
        for (u32 block = 0; block < SpriteTimelineOAMBlocks; ++block)
            current[block] = CurrentTimelineVersion[TimelineOAMBaseBlock + block];
        for (u32 engine = 0; engine < 2u; ++engine)
        {
            const u32 sourceBase = TimelineEngineBaseBlock
                + engine * TimelineEngineBlocks + TimelineEngineBGBlocks;
            const u32 destinationBase = SpriteTimelineOAMBlocks
                + engine * SpriteTimelineEngineOBJBlocks;
            for (u32 block = 0; block < SpriteTimelineEngineOBJBlocks; ++block)
            {
                current[destinationBase + block] =
                    CurrentTimelineVersion[sourceBase + block];
            }
        }

        rowHash = HashTimelineWords(current.data(), SpriteTimelineBlockCount);
        constexpr u32 hashMask = TimelineHashTableSize - 1u;
        u32 slot = static_cast<u32>(rowHash) & hashMask;
        for (u32 probe = 0; probe < TimelineHashTableSize; ++probe)
        {
            const u64 storedHash = Input.SpriteTimelineRowHashKeys[slot];
            if (storedHash == 0u)
            {
                emptyHashSlot = slot;
                break;
            }
            if (storedHash == rowHash)
            {
                const u32 candidate = Input.SpriteTimelineRowHashRows[slot];
                if (candidate < Input.SpriteTimelineRowCount
                    && std::memcmp(
                        Input.SpriteTimelineRows.data()
                            + static_cast<std::size_t>(candidate) * SpriteTimelineBlockCount,
                        current.data(),
                        SpriteTimelineBlockCount * sizeof(u32)) == 0)
                {
                    row = candidate;
                    hashMatched = true;
                    break;
                }
            }
            slot = (slot + 1u) & hashMask;
        }
        if (row == invalidRow)
        {
            if (Input.SpriteTimelineRowCount >= ScreenHeight)
            {
                Input.TimelineOverflow = 1u;
                row = oldRow == invalidRow ? 0u : oldRow;
            }
            else
            {
                row = Input.SpriteTimelineRowCount++;
                u32* destination = Input.SpriteTimelineRows.data()
                    + static_cast<std::size_t>(row) * SpriteTimelineBlockCount;
                std::memcpy(
                    destination, current.data(), SpriteTimelineBlockCount * sizeof(u32));
                MarkDirtyRange(
                    Input,
                    PackedSpriteTimelineRowsBase * sizeof(u32)
                        + row * SpriteTimelineBlockCount * sizeof(u32),
                    SpriteTimelineBlockCount * sizeof(u32));
            }
        }
        if (!hashMatched && row != invalidRow
            && emptyHashSlot != TimelineHashTableSize)
        {
            Input.SpriteTimelineRowHashKeys[emptyHashSlot] = rowHash;
            Input.SpriteTimelineRowHashRows[emptyHashSlot] = row;
        }
    }
    Input.SpriteTimelineRowIds[line] = row;
    if (oldRow != row)
    {
        MarkDirtyRange(
            Input,
            PackedSpriteTimelineBase * sizeof(u32) + line * sizeof(u32),
            sizeof(u32));
    }
    LastSpriteTimelineMutationSerial = Input.TimelineMutationSerial;
    Input.Recorder.SpriteTimelineRowDedupNs += NowNanoseconds() - dedupStartNs;
}

void FrameRecorder::ApplyPendingSpriteLatch() noexcept
{
    if (!PendingSpriteLatchReady)
        return;

    auto apply = [&](u8* current, const u8* pending, u32 size, u32 blockBase) {
        std::array<u8, DirtyBlockBytes> block{};
        for (u32 offset = 0; offset < size; offset += DirtyBlockBytes)
        {
            const u32 blockSize = std::min(DirtyBlockBytes, size - offset);
            block.fill(0u);
            std::memcpy(block.data(), pending + offset, blockSize);
            if (std::memcmp(current + offset, block.data(), blockSize) == 0)
                continue;
            const u32 version = AppendTimelineDelta(Input, block.data());
            if (version == 0u)
                continue;
            std::memcpy(current + offset, block.data(), blockSize);
            CurrentTimelineVersion[blockBase + offset / DirtyBlockBytes] = version;
        }
    };

    apply(CurrentEngine[0].OBJVRAM.data(), PendingEngineAOBJ.data(),
        static_cast<u32>(PendingEngineAOBJ.size()),
        TimelineEngineBaseBlock + TimelineEngineBGBlocks);
    apply(CurrentEngine[1].OBJVRAM.data(), PendingEngineBOBJ.data(),
        static_cast<u32>(PendingEngineBOBJ.size()),
        TimelineEngineBaseBlock + TimelineEngineBlocks + TimelineEngineBGBlocks);
    apply(CurrentOAM.data(), PendingOAM.data(), static_cast<u32>(PendingOAM.size()),
        TimelineOAMBaseBlock);
    SpriteLatchSeen[0] = true;
    FillSpriteTimelineLine(0u);
    ApplyPendingNativeSpriteMapping();
    PendingSpriteLatchReady = false;
}

void FrameRecorder::CaptureMappedMemoryForLine(
    u32 line,
    u32 engine,
    u32 section,
    u8* current,
    u32 size,
    const u32* mappings,
    u32 mappingCount,
    u32 mappingBytes,
    u32 blockBase) noexcept
{
    const MappedReadContext context{
        &Input, line, engine, section, true, NativeCaptureWrittenBlocks,
        CaptureWrittenBankMask(NativeCaptureWrittenBlocks)};
    if (mappingBytes == 16u * 1024u)
    {
        CaptureMappedMemoryBlocks<16u * 1024u>(
            Input, CurrentTimelineVersion, current, size, mappings, mappingCount,
            GPU, blockBase, context);
    }
    else
    {
        CaptureMappedMemoryBlocks<8u * 1024u>(
            Input, CurrentTimelineVersion, current, size, mappings, mappingCount,
            GPU, blockBase, context);
    }
}

void FrameRecorder::CaptureMappedPhysicalBlock(
    u32 line,
    u32 engine,
    u32 section,
    u8* current,
    u32 size,
    const u32* mappings,
    u32 mappingCount,
    u32 mappingBytes,
    u32 blockBase,
    u32 bank,
    u32 physicalBlock) noexcept
{
    const MappedReadContext context{
        &Input, line, engine, section, true, NativeCaptureWrittenBlocks,
        CaptureWrittenBankMask(NativeCaptureWrittenBlocks)};
    if (mappingBytes == 16u * 1024u)
    {
        CaptureMappedPhysicalMemoryBlock<16u * 1024u>(
            Input, CurrentTimelineVersion, current, size, mappings, mappingCount,
            GPU, blockBase, bank, physicalBlock, context);
    }
    else
    {
        CaptureMappedPhysicalMemoryBlock<8u * 1024u>(
            Input, CurrentTimelineVersion, current, size, mappings, mappingCount,
            GPU, blockBase, bank, physicalBlock, context);
    }
}

void FrameRecorder::CaptureDirectMemoryForLine(
    const u8* source,
    u8* current,
    u32 size,
    u32 blockBase) noexcept
{
    CaptureDirectMemoryBlocks(
        Input, CurrentTimelineVersion, current, source, size, blockBase);
}

void FrameRecorder::CaptureDirectMemoryBlock(
    const u8* source,
    u8* current,
    u32 size,
    u32 blockBase,
    u32 block) noexcept
{
    CaptureDirectMemoryBlockImpl(
        Input, CurrentTimelineVersion, current, source, size, blockBase, block);
}

void FrameRecorder::FillTimelineLine(u32 line) noexcept
{
    if (line >= ScreenHeight)
        return;

    const u64 dedupStartNs = NowNanoseconds();
    constexpr u32 invalidRow = 0xFFFFFFFFu;
    const u32 oldRow = Input.TimelineRowIds[line];
    u32 row = invalidRow;
    u64 rowHash = 0u;
    u32 emptyHashSlot = TimelineHashTableSize;
    bool hashMatched = false;
    if (line != 0u
        && Input.TimelineMutationSerial == LastTimelineMutationSerial
        && Input.TimelineRowIds[line - 1u] != invalidRow)
    {
        row = Input.TimelineRowIds[line - 1u];
    }
    else
    {
        rowHash = HashTimelineWords(CurrentTimelineVersion.data(), TimelineBlockCount);
        constexpr u32 hashMask = TimelineHashTableSize - 1u;
        u32 slot = static_cast<u32>(rowHash) & hashMask;
        for (u32 probe = 0; probe < TimelineHashTableSize; ++probe)
        {
            const u64 storedHash = Input.TimelineRowHashKeys[slot];
            if (storedHash == 0u)
            {
                emptyHashSlot = slot;
                break;
            }
            if (storedHash == rowHash)
            {
                const u32 candidate = Input.TimelineRowHashRows[slot];
                if (candidate < Input.TimelineRowCount
                    && std::memcmp(
                        Input.TimelineRows.data()
                            + static_cast<std::size_t>(candidate) * TimelineBlockCount,
                        CurrentTimelineVersion.data(),
                        TimelineBlockCount * sizeof(u32)) == 0)
                {
                    row = candidate;
                    hashMatched = true;
                    break;
                }
            }
            slot = (slot + 1u) & hashMask;
        }
        if (row == invalidRow)
        {
            if (Input.TimelineRowCount >= ScreenHeight)
            {
                Input.TimelineOverflow = 1u;
                row = oldRow == invalidRow ? 0u : oldRow;
            }
            else
            {
                row = Input.TimelineRowCount++;
                std::memcpy(
                    Input.TimelineRows.data()
                        + static_cast<std::size_t>(row) * TimelineBlockCount,
                    CurrentTimelineVersion.data(),
                    TimelineBlockCount * sizeof(u32));
                MarkDirtyRange(
                    Input,
                    PackedTimelineRowsBase * sizeof(u32)
                        + row * TimelineBlockCount * sizeof(u32),
                    TimelineBlockCount * sizeof(u32));
            }
        }
        if (!hashMatched && row != invalidRow
            && emptyHashSlot != TimelineHashTableSize)
        {
            Input.TimelineRowHashKeys[emptyHashSlot] = rowHash;
            Input.TimelineRowHashRows[emptyHashSlot] = row;
        }
    }
    Input.TimelineRowIds[line] = row;
    if (oldRow != row)
    {
        MarkDirtyRange(
            Input,
            PackedTimelineBase * sizeof(u32) + line * sizeof(u32),
            sizeof(u32));
    }
    LastTimelineMutationSerial = Input.TimelineMutationSerial;
    Input.Recorder.TimelineRowDedupNs += NowNanoseconds() - dedupStartNs;
}

void FrameRecorder::CaptureLine(
    u32 engine,
    const melonDS::GPU2D& gpu2D,
    u32 line,
    bool screenSwap) noexcept
{
    if (engine >= 2u || line >= ScreenHeight)
        return;

    const std::size_t lineIndex = static_cast<std::size_t>(engine) * ScreenHeight + line;
    if (!LineSeen[lineIndex])
    {
        LineSeen[lineIndex] = true;
        ++EngineLineCount[engine];
    }
    CopyLineState(
        Input.Lines[engine * ScreenHeight + line],
        gpu2D,
        GPU.GPU3D.GetRenderXPos());
    LineState& state = Input.Lines[engine * ScreenHeight + line];
    state.UnitEnabled = gpu2D.Enabled ? 1u : 0u;
    state.MasterBrightness = engine == 0u ? GPU.MasterBrightnessA : GPU.MasterBrightnessB;
    state.CaptureCnt = GPU.CaptureCnt;
    state.CaptureEnable = GPU.CaptureEnable ? 1u : 0u;
    state.ScreensEnabled = GPU.ScreensEnabled ? 1u : 0u;
    state.ScreenSwap = screenSwap ? 1u : 0u;
    state.LCDVRAMMap = GPU.VRAMMap_LCDC;
    if (engine == 0u)
    {
        const bool captureEnabled = GPU.CaptureEnable;
        const u32 captureCnt = GPU.CaptureCnt;
        const bool captureStarted = captureEnabled
            && (!CaptureStateEnabled || CaptureStateCnt != captureCnt);
        if (captureStarted)
        {
            CaptureStartLine = line;
            BeginCaptureAddressDiagnostic(line, captureCnt);
        }
        else if (!captureEnabled && CaptureStartLine == CaptureStartLineNone)
        {
            // No capture has started in this frame.  Keep this explicit so
            // the high byte never accidentally exposes a stale frame's line.
            CaptureStartLine = CaptureStartLineNone;
        }
        CaptureStateEnabled = captureEnabled;
        CaptureStateCnt = captureCnt;
        if (captureEnabled)
            RecordCaptureAddressLine(line, captureCnt);
    }
    state.SpriteLatchValid = (SpriteLatchSeen[line] ? SpriteLatchValidMask : 0u)
        | ((CaptureStartLine & CaptureStartLineMask) << CaptureStartLineShift);
    if (engine == 0u)
    {
        Input.CaptureEnable |= state.CaptureEnable;
        // CaptureMemoryForLine() runs before CaptureLine(), so it cannot see a
        // new command or LCDC remap latched at this line boundary. Re-evaluate
        // the mapping after the line state has been recorded; the helper only
        // includes writes from earlier lines, matching the native shader's
        // render-then-capture ordering.
        CaptureNativeMappingForLine(line, false);
    }

    // This is the emulation-time LCD assignment.  Present-time POWCNT1 is not
    // authoritative when a title changes routing within a frame.
    const u32 screenA = screenSwap ? 0u : 1u;
    const u32 screenB = screenA ^ 1u;
    Input.ScreenSource[screenA * ScreenHeight + line] = 0u;
    Input.ScreenSource[screenB * ScreenHeight + line] = 1u;
}

void FrameRecorder::CaptureCaptureStateForLine(u32 line) noexcept
{
    if (line >= ScreenHeight || !LineSeen[line])
        return;

    // CaptureLine() runs at line start, but DISPCAPCNT can be written by an
    // emulated register event later in the same line. The native capture
    // shader must sample the state at the actual capture write boundary.
    LineState& state = Input.Lines[line];
    const bool captureEnabled = GPU.CaptureEnable;
    const u32 captureCnt = GPU.CaptureCnt;
    const bool captureStarted = captureEnabled
        && (!CaptureStateEnabled || CaptureStateCnt != captureCnt);
    state.CaptureCnt = captureCnt;
    state.CaptureEnable = captureEnabled ? 1u : 0u;
    state.ScreensEnabled = GPU.ScreensEnabled ? 1u : 0u;
    state.LCDVRAMMap = GPU.VRAMMap_LCDC;

    if (captureStarted)
    {
        CaptureStartLine = line;
        BeginCaptureAddressDiagnostic(line, captureCnt);
    }
    else if (!captureEnabled && CaptureStartLine == CaptureStartLineNone)
    {
        CaptureStartLine = CaptureStartLineNone;
    }
    CaptureStateEnabled = captureEnabled;
    CaptureStateCnt = captureCnt;
    state.SpriteLatchValid = (SpriteLatchSeen[line] ? SpriteLatchValidMask : 0u)
        | ((CaptureStartLine & CaptureStartLineMask) << CaptureStartLineShift);
    Input.CaptureEnable |= state.CaptureEnable;
    if (captureEnabled)
        RecordCaptureAddressLine(line, captureCnt);

    // A late DISPCAPCNT/LCDC change is the same temporal boundary as the
    // ordinary CaptureLine() path. Keep the current-frame write-ahead owner
    // timeline in sync even though the native producer does not execute the
    // CPU DoCapture() body.
    CaptureNativeMappingForLine(line, false);
    CommitNativeCaptureWriteAheadForLine(line);

    // BeginFrame() already marks the complete packed header and line-state
    // ranges dirty before this late line-boundary sample. Do not add a new
    // per-line upload range here: this helper is an oracle-side state refresh,
    // not a request for an additional native upload.
}

void FrameRecorder::BeginCaptureAddressDiagnostic(
    u32 line, u32 captureCnt) noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (!StageDiagnosticsEnabled())
        return;

    if (CaptureAddressLogCount >= MaxCaptureAddressDiagnostics)
    {
        ++CaptureAddressLogOverflow;
        return;
    }

    CaptureAddressDiagnostic& diagnostic =
        CaptureAddressLog[CaptureAddressLogCount++];
    diagnostic = {};
    diagnostic.Frame = Input.Generation.Frame;
    diagnostic.Line = line;
    diagnostic.CaptureCnt = captureCnt;
    diagnostic.Bank = (captureCnt >> 16u) & 3u;
    diagnostic.SizeCode = (captureCnt >> 20u) & 3u;
    diagnostic.DstOffsetCode = (captureCnt >> 18u) & 3u;
    diagnostic.DstHalfwordBase = CaptureOffsetHalfwords(
        diagnostic.DstOffsetCode);
    diagnostic.DstByteBase = CaptureOffsetBytes(diagnostic.DstOffsetCode);
    diagnostic.SourceBOffsetCode = (captureCnt >> 26u) & 3u;
    diagnostic.ExpectedBlockMask = CaptureAddressBlockMask(
        diagnostic.DstOffsetCode, diagnostic.SizeCode, line);
    diagnostic.ProvenanceExpectedFirstByte =
        (diagnostic.DstByteBase / CapturePhysicalBlockBytes)
        * CapturePhysicalBlockBytes;
#else
    (void)line;
    (void)captureCnt;
#endif
}

void FrameRecorder::RecordCaptureAddressLine(
    u32 line, u32 captureCnt) noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (!StageDiagnosticsEnabled())
        return;

    if (line >= ScreenHeight || !GPU.CaptureEnable)
        return;

    const u32 bank = (captureCnt >> 16u) & 3u;
    const u32 sizeCode = (captureCnt >> 20u) & 3u;
    const u32 offsetCode = (captureCnt >> 18u) & 3u;
    if (CaptureAddressLogCount == 0u
        || CaptureAddressLog[CaptureAddressLogCount - 1u].Frame
            != Input.Generation.Frame
        || CaptureAddressLog[CaptureAddressLogCount - 1u].CaptureCnt
            != captureCnt)
    {
        BeginCaptureAddressDiagnostic(line, captureCnt);
    }
    if (CaptureAddressLogCount == 0u)
        return;

    CaptureAddressDiagnostic& diagnostic =
        CaptureAddressLog[CaptureAddressLogCount - 1u];
    if (diagnostic.Frame != Input.Generation.Frame
        || diagnostic.CaptureCnt != captureCnt)
    {
        return;
    }
    if (diagnostic.LastTrackedLine == line)
        return;
    diagnostic.LastTrackedLine = line;

    const u32 width = CaptureWidthForSize(sizeCode);
    const u32 height = CaptureHeightForSize(sizeCode);
    if (line >= height)
        return;

    const u32 rawLineAddress = CaptureOffsetBytes(offsetCode)
        + line * width * 2u;
    const u32 rawLastAddress = rawLineAddress + width * 2u - 2u;
    diagnostic.WrapCount = rawLastAddress / LCDCBankBytes;
    for (u32 x = 0u; x < width; x += 2u)
    {
        const u32 byteAddress = WrapLCDCByte(
            rawLineAddress + x * 2u);
        const u32 lastByteAddress = byteAddress + 1u;
        if (byteAddress > LCDCBankByteMask
            || lastByteAddress > LCDCBankByteMask)
        {
            ++diagnostic.OutsideBank;
            continue;
        }
        if (diagnostic.FirstByte == 0xFFFFFFFFu)
            diagnostic.FirstByte = byteAddress;
        diagnostic.LastByte = lastByteAddress;
        diagnostic.ActualBlockMask |= 1u << (
            byteAddress / CapturePhysicalBlockBytes);

        for (const u32 sourceX : {0u, 255u})
        {
            const u32 softwareSource = WrapLCDCHalfword(
                line * 256u + sourceX
                + CaptureOffsetHalfwords(diagnostic.SourceBOffsetCode))
                << 1u;
            const u32 nativeSource = WrapLCDCByte(
                line * 512u + sourceX * 2u
                + CaptureOffsetBytes(diagnostic.SourceBOffsetCode));
            if (softwareSource != nativeSource)
                diagnostic.SourceBAddressMismatch = 1u;
        }
    }

    if (bank >= CapturePhysicalBanks)
        diagnostic.NeighborBankCorruption = 1u;
#else
    (void)line;
    (void)captureCnt;
#endif
}

void FrameRecorder::FinalizeCaptureAddressDiagnostics() noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (!StageDiagnosticsEnabled())
        return;

    for (u32 index = 0u; index < CaptureAddressLogCount; ++index)
    {
        CaptureAddressDiagnostic& diagnostic = CaptureAddressLog[index];
        diagnostic.DestinationAddressMismatch =
            diagnostic.ExpectedBlockMask != diagnostic.ActualBlockMask
            || diagnostic.FirstByte > LCDCBankByteMask
            || diagnostic.LastByte > LCDCBankByteMask;
        diagnostic.NeighborBankCorruption |= diagnostic.OutsideBank != 0u;

        const u32 firstBlock = diagnostic.DstByteBase
            / CapturePhysicalBlockBytes;
        const CaptureBlockProvenance& owner = GPU.GetRenderer()
            .GetCaptureBlockProvenance(diagnostic.Bank, firstBlock);
        diagnostic.ProvenanceAddressMismatch =
            IsNativeCaptureOwner(owner.Owner)
            && diagnostic.DstByteBase
                != diagnostic.ProvenanceExpectedFirstByte;
        if (diagnostic.ProvenanceAddressMismatch != 0u)
            assert(diagnostic.ProvenanceAddressMismatch == 0u);

        Platform::Log(
            Platform::LogLevel::Info,
            "[GPU2DCaptureAddress] frame=%llu line=%u CaptureCnt=0x%08X "
            "bank=%u size=%u dstOffsetCode=%u dstHalfwordBase=0x%04X "
            "dstByteBase=0x%05X sourceBOffsetCode=%u firstByte=0x%05X "
            "lastByte=0x%05X wrapCount=%u expectedBlocks=0x%X "
            "actualBlocks=0x%X destinationAddressMismatch=%u "
            "sourceBAddressMismatch=%u outsideBank=%u "
            "neighborBankCorruption=%u provenanceOwner=%s "
            "provenanceExpectedFirstByte=0x%05X provenanceAddressMismatch=%u\n",
            static_cast<unsigned long long>(diagnostic.Frame),
            diagnostic.Line, diagnostic.CaptureCnt, diagnostic.Bank,
            diagnostic.SizeCode, diagnostic.DstOffsetCode,
            diagnostic.DstHalfwordBase, diagnostic.DstByteBase,
            diagnostic.SourceBOffsetCode, diagnostic.FirstByte,
            diagnostic.LastByte, diagnostic.WrapCount,
            diagnostic.ExpectedBlockMask, diagnostic.ActualBlockMask,
            diagnostic.DestinationAddressMismatch,
            diagnostic.SourceBAddressMismatch, diagnostic.OutsideBank,
            diagnostic.NeighborBankCorruption, CaptureOwnerName(owner.Owner),
            diagnostic.ProvenanceExpectedFirstByte,
            diagnostic.ProvenanceAddressMismatch);
    }
    if (CaptureAddressLogOverflow != 0u)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[GPU2DCaptureAddress] frame=%llu diagnosticOverflow=%u\n",
            static_cast<unsigned long long>(Input.Generation.Frame),
            CaptureAddressLogOverflow);
    }
#endif
}

void FrameRecorder::FinalizeMappedCaptureDiagnostics() noexcept
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    for (u32 index = 0u; index < Input.MappedCaptureViolationCount; ++index)
    {
        const MappedCaptureViolation& violation =
            Input.MappedCaptureViolations[index];
        Platform::Log(
            Platform::LogLevel::Error,
            "[GPU2DMappedCaptureViolation] frame=%llu line=%u engine=%u "
            "section=%s logicalAddress=0x%X mappingIndex=%u bank=%u "
            "physicalAddress=0x%X physicalBlock=%u owner=%s "
            "ownerSemanticFrame=%llu ownerCaptureGeneration=%llu "
            "cpuHash=0x%llX nativeHash=0x%llX materialized=%u\n",
            static_cast<unsigned long long>(violation.Frame), violation.Line,
            violation.Engine, violation.Section == 0u ? "BG" : "OBJ",
            violation.LogicalAddress, violation.MappingIndex, violation.Bank,
            violation.PhysicalAddress, violation.PhysicalBlock,
            CaptureOwnerName(violation.Owner),
            static_cast<unsigned long long>(violation.OwnerSemanticFrame),
            static_cast<unsigned long long>(violation.OwnerCaptureGeneration),
            static_cast<unsigned long long>(violation.CpuHash),
            static_cast<unsigned long long>(violation.NativeHash),
            violation.Materialized);
    }
    if (Input.MappedCaptureViolationOverflow != 0u)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "[GPU2DMappedCaptureViolation] frame=%llu diagnosticOverflow=%u\n",
            static_cast<unsigned long long>(Input.Generation.Frame),
            Input.MappedCaptureViolationOverflow);
    }
#endif
}

void FrameRecorder::SnapshotEngine(u32 engine, u32 line) noexcept
{
    if (engine >= 2u)
        return;

    MemorySnapshot& destination = Input.Engine[engine];

    const bool engineA = engine == 0u;
    const u32 bgSize = engineA ? 512u * 1024u : 128u * 1024u;
    const u32 objSize = engineA ? 256u * 1024u : 128u * 1024u;
    const u32* bgMappings = engineA ? GPU.VRAMMap_ABG : GPU.VRAMMap_BBG;
    const u32 bgMappingCount = engineA ? 32u : 8u;
    const u32* objMappings = engineA ? GPU.VRAMMap_AOBJ : GPU.VRAMMap_BOBJ;
    const u32 objMappingCount = engineA ? 16u : 8u;
    const u32* bgExtMappings = engineA ? GPU.VRAMMap_ABGExtPal : GPU.VRAMMap_BBGExtPal;
    const u32 bgExtMappingCount = 4u;
    const u32* objExtMappings = engineA
        ? &GPU.VRAMMap_AOBJExtPal : &GPU.VRAMMap_BOBJExtPal;

    destination.BGSize = bgSize;
    destination.OBJSize = objSize;
    destination.BGExtendedPaletteSize = 32u * 1024u;
    destination.OBJExtendedPaletteSize = 8u * 1024u;
    const u32 engineBase = PackedEngineBase + engine * PackedEngineWords;
    const u32 nativeCaptureWrittenBankMask =
        CaptureWrittenBankMask(NativeCaptureWrittenBlocks);
    const MappedReadContext bgContext{
        &Input, line, engine, 0u, true, NativeCaptureWrittenBlocks,
        nativeCaptureWrittenBankMask};
    const MappedReadContext objContext{
        &Input, line, engine, 1u, true, NativeCaptureWrittenBlocks,
        nativeCaptureWrittenBankMask};
    const MappedReadContext bgExtContext{
        &Input, line, engine, 2u, true, NativeCaptureWrittenBlocks,
        nativeCaptureWrittenBankMask};
    const MappedReadContext objExtContext{
        &Input, line, engine, 3u, true, NativeCaptureWrittenBlocks,
        nativeCaptureWrittenBankMask};
    CopyMappedVRAMBlocks<16u * 1024u>(
        Input, destination.BGVRAM.data(), destination.BGSize,
        bgMappings, bgMappingCount, GPU,
        engineBase * sizeof(u32), bgContext);
    CopyMappedVRAMBlocks<16u * 1024u>(
        Input, destination.OBJVRAM.data(), destination.OBJSize,
        objMappings, objMappingCount, GPU,
        (engineBase + PackedBGWords) * sizeof(u32), objContext);
    CopyMappedVRAMBlocks<8u * 1024u>(
        Input, destination.BGExtendedPalette.data(),
        destination.BGExtendedPaletteSize, bgExtMappings, bgExtMappingCount,
        GPU, (engineBase + PackedBGWords + PackedOBJWords) * sizeof(u32),
        bgExtContext);
    CopyMappedVRAMBlocks<8u * 1024u>(
        Input, destination.OBJExtendedPalette.data(),
        destination.OBJExtendedPaletteSize, objExtMappings, 1u, GPU,
        (engineBase + PackedBGWords + PackedOBJWords
            + PackedBGExtendedPaletteWords) * sizeof(u32), objExtContext);
}

void FrameRecorder::FinalizeMemory() noexcept
{
    if (RecorderStartNs != 0u)
    {
        Input.Recorder.GPU2DRecorderNs += NowNanoseconds() - RecorderStartNs;
        RecorderStartNs = 0u;
    }
    FinalizeCaptureAddressDiagnostics();
    FinalizeMappedCaptureDiagnostics();
    if (EngineLineCount[0] != ScreenHeight || EngineLineCount[1] != ScreenHeight)
        return;
    if (!MemoryBaselineReady)
        CaptureMemoryForLine(0u);
    if (HasDirtyOverlap(
            Input,
            PackedEngineBase * sizeof(u32),
            PackedPaletteBase * sizeof(u32)))
    {
        ++Input.Generation.VRAMGeneration;
    }
    if (HasDirtyOverlap(
            Input,
            PackedPaletteBase * sizeof(u32),
            PackedLCDVRAMBase * sizeof(u32)))
    {
        ++Input.Generation.ContentGeneration;
    }
    if (HasDirtyOverlap(
            Input,
            PackedLCDVRAMBase * sizeof(u32),
            PackedRouteBase * sizeof(u32)))
    {
        ++Input.Generation.CaptureGeneration;
    }
    Input.CaptureCnt = GPU.CaptureCnt;
    Input.CaptureEnable |= GPU.CaptureEnable ? 1u : 0u;
    Input.ScreenSwap = GPU.ScreenSwap ? 1u : 0u;
    Input.ScreensEnabled = GPU.ScreensEnabled ? 1u : 0u;
    Valid = Input.TimelineOverflow == 0u;
}

} // namespace melonDS::GPU2DNative
