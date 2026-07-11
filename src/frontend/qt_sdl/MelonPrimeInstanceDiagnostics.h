#ifndef MELONPRIME_INSTANCE_DIAGNOSTICS_H
#define MELONPRIME_INSTANCE_DIAGNOSTICS_H

class EmuInstance;

namespace MelonPrime::InstanceDiagnostics {

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
void LogLifecycle(EmuInstance* emu, const void* core, const char* event) noexcept;
void LogOwnedStates(EmuInstance* emu, const void* hook, const void* patch, const void* hud) noexcept;
void LogInputSubscription(EmuInstance* emu, const void* subscription,
                          unsigned long long generation, bool active) noexcept;
bool CheckGuiThread(EmuInstance* emu, const char* operation) noexcept;
bool CheckEmuThread(EmuInstance* emu, const char* operation) noexcept;
#else
inline void LogLifecycle(EmuInstance*, const void*, const char*) noexcept {}
inline void LogOwnedStates(EmuInstance*, const void*, const void*, const void*) noexcept {}
inline void LogInputSubscription(EmuInstance*, const void*, unsigned long long, bool) noexcept {}
inline bool CheckGuiThread(EmuInstance*, const char*) noexcept { return true; }
inline bool CheckEmuThread(EmuInstance*, const char*) noexcept { return true; }
#endif

} // namespace MelonPrime::InstanceDiagnostics

#endif // MELONPRIME_INSTANCE_DIAGNOSTICS_H
