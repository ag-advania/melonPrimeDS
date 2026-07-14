#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanVertexUploadBootstrap is owned by the Vulkan build gate"
#endif

// MELONPRIME_VULKAN_VERTEX_UPLOAD_BOOTSTRAP_V1

class QString;

namespace MelonPrime::Vulkan
{

// Builds deterministic DS triangle, quad, line and degenerate polygon inputs,
// packs them with the OpenGL-compatible 28-byte Vulkan layout, uploads the
// vertex/index/edge/polygon payload through a device-local buffer, reads it
// back, and verifies exact bytes. Game rendering remains on Software.
int RunVertexUploadBootstrapHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
