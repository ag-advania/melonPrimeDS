#ifndef MELONPRIME_INPUT_SUBSCRIPTION_H
#define MELONPRIME_INPUT_SUBSCRIPTION_H

#include <cstdint>
#include <atomic>
#include <mutex>

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

    void Initialize(uint64_t id) noexcept { instanceId = id; }
};

class PlatformInputOwnerService {
public:
    static bool Update(MelonPrimeInputSubscription& subscription, bool eligible)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        auto*& owner = Owner();
        if (!eligible) {
            subscription.focused = false;
            if (owner == &subscription) {
                subscription.activeOwner = false;
                owner = nullptr;
            }
            return false;
        }

        subscription.focused = true;
        if (owner != &subscription) {
            if (owner)
                owner->activeOwner = false;
            owner = &subscription;
            subscription.activeOwner = true;
            subscription.cursorNeedsSync = true;
            subscription.hotkeyPrevious = subscription.hotkeyDownSnapshot;
            ++subscription.focusGeneration;
            ++subscription.generation;
            if (subscription.generation == 0)
                subscription.generation = 1;
        }
        return true;
    }

    static void Release(MelonPrimeInputSubscription& subscription)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        auto*& owner = Owner();
        if (owner == &subscription)
            owner = nullptr;
        subscription.activeOwner = false;
        subscription.focused = false;
        subscription.cursorNeedsSync = true;
    }

    static bool IsOwner(const MelonPrimeInputSubscription& subscription)
    {
        std::lock_guard<std::mutex> lock(Mutex());
        return Owner() == &subscription;
    }

private:
    static std::mutex& Mutex()
    {
        static std::mutex s_mutex; // process-service: serializes capture-owner changes
        return s_mutex;
    }

    static MelonPrimeInputSubscription*& Owner()
    {
        static MelonPrimeInputSubscription* s_owner = nullptr; // process-service: active capture owner
        return s_owner;
    }
};

} // namespace MelonPrime

#endif // MELONPRIME_INPUT_SUBSCRIPTION_H
