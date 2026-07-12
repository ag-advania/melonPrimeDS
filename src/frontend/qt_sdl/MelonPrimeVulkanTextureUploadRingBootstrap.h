#pragma once

#include <QString>

namespace MelonPrime::Vulkan
{

// MELONPRIME_VULKAN_TEXTURE_UPLOAD_RING_BOOTSTRAP_V1
int RunTextureUploadRingHarness(const QString& outputPath, int iterations = 3);

} // namespace MelonPrime::Vulkan
