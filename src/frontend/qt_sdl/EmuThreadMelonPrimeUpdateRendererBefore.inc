// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
bool vsyncFlag = cfg.GetBool("Screen.VSync");
emuInstance->setVSyncGL(vsyncFlag);
#endif // MELONPRIME_DS
