// This file is a unity-build fragment included only by EmuThread.cpp.
#ifdef MELONPRIME_DS
// P-46: Store true AFTER unlock so the enqueue is always visible to the
// emu thread when it sees true (release pairs with the acquire load in
// handleMessages). Storing before lock would allow emu to see true, lock
// an empty queue, clear the flag, and then lose the message.
msgPending.store(true, std::memory_order_release);

// Wake a paused run loop. The enqueue above happened under msgMutex, which the
// waiter holds across its emptiness check, so this cannot be missed: either the
// waiter has not checked yet and will find the queue non-empty, or it is
// already waiting and this wakes it.
msgQueueNotEmpty.wakeAll();
#endif
