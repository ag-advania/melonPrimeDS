// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
emuInstance->setVSyncGL(vsyncFlag);
MelonPrime::RendererTransitionProfile::Mark("emu-render-settings");
#endif // MELONPRIME_DS
