#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanTextureDecodeBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_TEXTURE_DECODE_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Decodes all seven Nintendo DS texture formats into the RGB6A5 payload used
// by the Vulkan texture path. It validates exact memory footprints, wrapped
// VRAM access, content hashes and 512-byte dirty-page invalidation.
int RunTextureDecodeDirtyHashHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
