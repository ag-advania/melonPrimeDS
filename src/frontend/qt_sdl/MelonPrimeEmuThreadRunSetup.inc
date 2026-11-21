// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
melonPrime->Initialize();
// Windows high-timer-resolution request intentionally disabled at startup.
// SetHighTimerResolution() stays implemented for optional future use.
#endif // MELONPRIME_DS
