#pragma once

namespace MelonPrime
{

// Install Windows unhandled-exception filter (minidump + stack trace).
// No-op on non-Windows platforms.
void installWindowsCrashHandler();

} // namespace MelonPrime
