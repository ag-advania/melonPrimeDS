// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
melonPrime->Initialize();
// P-11: Set 0.5ms timer resolution for precision frame pacing.
// Must be called before frame loop. Without this, SDL_Delay(1) is about 15.6ms.
#ifdef _WIN32
MelonPrime::WinInternal::SetHighTimerResolution();
#endif
#endif // MELONPRIME_DS
