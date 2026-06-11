// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
// P-46: Clear flag INSIDE the mutex after draining.
// Clearing inside the lock ensures sendMessage cannot enqueue between the
// drain and the clear, which would otherwise cause that message to be
// missed until the flag is set again. Relaxed is sufficient here because
// the subsequent mutex unlock provides the necessary release ordering.
msgPending.store(false, std::memory_order_relaxed);
#endif
