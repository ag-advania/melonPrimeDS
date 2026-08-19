// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
// P-46: Atomic fast-path - skip mutex entirely when no messages pending.
// sendMessage() stores true (release) before enqueue; we load (acquire)
// here so any enqueued message is visible if we see true.
// On 99%+ of frames this returns immediately, saving the uncontended
// QMutex lock/unlock overhead (~20-50 cyc/frame). Developer-only diagnostic
// state loading still needs this post-frame boundary when the queue is empty;
// the shipping path keeps the original zero-work fast return.
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
if (!msgPending.load(std::memory_order_acquire))
{
    tryLoadDiagnosticSavestate();
    return;
}
#else
if (!msgPending.load(std::memory_order_acquire))
    return;
#endif
#endif
