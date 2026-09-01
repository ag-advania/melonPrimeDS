#ifndef MELONPRIME_INPUT_SUBSCRIPTION_H
#define MELONPRIME_INPUT_SUBSCRIPTION_H

#include <cstdint>
#include <atomic>
#include <mutex>

#include "MelonPrimeCompilerHints.h"

namespace MelonPrime {

// Per-emulator consumer cursor for the process-wide platform input collector.
// The collector owns physical OS state; this object owns everything that may
// differ between emulator instances or focus generations.
struct MelonPrimeInputSubscription {
    uint64_t instanceId = 0;
    uint64_t generation = 1;
    uint64_t focusGeneration = 0;
    int64_t lastReadX = 0;
    int64_t lastReadY = 0;
    uint64_t hotkeyDownSnapshot = 0;
    uint64_t hotkeyPrevious = 0;
    int64_t debugSumX = 0;
    int64_t debugSumY = 0;
    uint32_t debugFrames = 0;
    bool focused = false;
    std::atomic_bool activeOwner{false};
    bool cursorNeedsSync = true;
    // A process-wide Raw owner transfer may be initiated by another
    // EmuThread. That thread may publish this notification, but it must not
    // mutate the plain generation/cursor/baseline fields below the boundary.
    std::atomic_bool registrationResetPending{false};

    void Initialize(uint64_t id) noexcept { instanceId = id; }

    void RequestRegistrationReset() noexcept
    {
        registrationResetPending.store(true, std::memory_order_release);
    }

    // Owner-thread safe point. This is the sole cross-generation writer for
    // the subscription's plain consumer state after construction.
    [[nodiscard]] bool ConsumeRegistrationReset() noexcept
    {
        if (LIKELY(!registrationResetPending.load(std::memory_order_relaxed)))
            return false;
        if (!registrationResetPending.exchange(
                false, std::memory_order_acq_rel))
            return false;

        cursorNeedsSync = true;
        hotkeyPrevious = hotkeyDownSnapshot;
        ++generation;
        if (generation == 0)
            generation = 1;
        return true;
    }
};

class PlatformInputOwnerService {
public:
    static bool Update(MelonPrimeInputSubscription& subscription, bool eligible)
    {
        if (!eligible) {
            subscription.focused = false;
            if (OwnerAtomic().load(std::memory_order_acquire) != &subscription)
                return false;

            std::lock_guard<std::mutex> lock(Mutex());
            if (OwnerAtomic().load(std::memory_order_relaxed) == &subscription) {
                subscription.activeOwner.store(false, std::memory_order_release);
                OwnerAtomic().store(nullptr, std::memory_order_release);
            }
            return false;
        }

        subscription.focused = true;
        if (OwnerAtomic().load(std::memory_order_acquire) == &subscription)
            return true;

        std::lock_guard<std::mutex> lock(Mutex());
        auto* const owner = OwnerAtomic().load(std::memory_order_relaxed);
        if (owner != &subscription) {
            if (owner) {
                owner->RequestRegistrationReset();
                owner->activeOwner.store(false, std::memory_order_release);
            }
            subscription.cursorNeedsSync = true;
            subscription.hotkeyPrevious = subscription.hotkeyDownSnapshot;
            ++subscription.focusGeneration;
            ++subscription.generation;
            if (subscription.generation == 0)
                subscription.generation = 1;
            subscription.activeOwner.store(true, std::memory_order_release);
            OwnerAtomic().store(&subscription, std::memory_order_release);
        }
        return true;
    }

    // A raw-input registration change is an input-timeline boundary even when
    // the capture owner does not change. Keep the existing subscription
    // generation authoritative so button edges, wheel impulses, and aim
    // deltas can all reject events from the previous registration.
    static uint64_t BeginRegistrationGeneration(
        MelonPrimeInputSubscription& subscription)
    {
        // Reconfiguration is called by the active subscription's EmuThread.
        // Refuse a foreign-thread write rather than making a plain generation
        // field look synchronized merely because the service mutex is held.
        std::lock_guard<std::mutex> lock(Mutex());
        if (OwnerAtomic().load(std::memory_order_relaxed) != &subscription)
            return 0;
        ++subscription.generation;
        if (subscription.generation == 0)
            subscription.generation = 1;
        subscription.hotkeyPrevious = subscription.hotkeyDownSnapshot;
        return subscription.generation;
    }

    static void RequestRegistrationReset(
        MelonPrimeInputSubscription& subscription) noexcept
    {
        subscription.RequestRegistrationReset();
    }

    static void Release(MelonPrimeInputSubscription& subscription)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        if (OwnerAtomic().load(std::memory_order_relaxed) == &subscription)
            OwnerAtomic().store(nullptr, std::memory_order_release);
        subscription.activeOwner.store(false, std::memory_order_release);
        // Release may run on teardown or on a different instance's owner
        // path. Publish only the mailbox; the subscription owner consumes and
        // applies its plain cursor/baseline reset at its next safe point.
        subscription.RequestRegistrationReset();
    }

    static bool IsOwner(const MelonPrimeInputSubscription& subscription) noexcept
    {
        return OwnerAtomic().load(std::memory_order_acquire) == &subscription;
    }

private:
    static std::mutex& Mutex()
    {
        static std::mutex s_mutex; // process-service: serializes capture-owner changes
        return s_mutex;
    }

    static std::atomic<MelonPrimeInputSubscription*>& OwnerAtomic()
    {
        static std::atomic<MelonPrimeInputSubscription*> s_owner{nullptr}; // process-service: lock-free active capture owner authority
        return s_owner;
    }
};

} // namespace MelonPrime

#endif // MELONPRIME_INPUT_SUBSCRIPTION_H
