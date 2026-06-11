// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
// P-46: Atomic fast-path - skip mutex entirely when no messages pending.
// sendMessage() stores true (release) before enqueue; we load (acquire)
// here so any enqueued message is visible if we see true.
// On 99%+ of frames this returns immediately, saving the uncontended
// QMutex lock/unlock overhead (~20-50 cyc/frame).
if (!msgPending.load(std::memory_order_acquire))
    return;
#endif
