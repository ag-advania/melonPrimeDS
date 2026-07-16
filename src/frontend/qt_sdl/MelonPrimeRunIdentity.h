#pragma once

#include <cstdint>

namespace MelonPrime
{

void initRunIdentity();
[[nodiscard]] std::uint64_t runId() noexcept;
[[nodiscard]] const char* binarySha256Hex() noexcept;

} // namespace MelonPrime
