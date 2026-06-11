// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
// P-13: Late-poll state.
// storedFrametimeStep carries the previous frame's timing for the
// early limiter. This enables sleeping BEFORE input polling so raw
// input is sampled as close to RunFrame as possible.
double storedFrametimeStep = 1.0 / 60.0;
bool isFirstLimiterFrame = true;
// P-39: Once shaders finish compiling, NeedsShaderCompile() always returns
// false. Cache this to skip the virtual dispatch (~15-25 cyc) every frame.
bool shadersReady = false;
#endif
