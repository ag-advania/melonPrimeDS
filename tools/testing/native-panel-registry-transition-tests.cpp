/*
    Contract model for the native panel registry transition boundary.

    The production Vulkan/DX12 walks copy matching panel addresses while the
    registry mutex is held, then call Quiesce after unlock. GUI destruction is
    serialized by the prepareVideoBackendTransition() barrier. This test keeps
    that lifetime rule executable without Qt or a renderer SDK and exercises
    two instances while one panel closes during the other instance's wait.
*/

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct Panel
{
    int instance = 0;
    std::atomic<int> prepares{0};
};

class RegistryModel
{
public:
    void Publish(Panel* panel)
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        registry_.push_back(panel);
    }

    void PrepareForInstance(int instance)
    {
        std::vector<Panel*> snapshot;
        {
            std::lock_guard<std::mutex> lock(registryMutex_);
            for (Panel* panel : registry_) {
                if (panel->instance == instance)
                    snapshot.push_back(panel);
            }
        }

        {
            std::lock_guard<std::mutex> lock(barrierMutex_);
            transitionInFlight_ = true;
        }
        barrierCv_.notify_all();

        // Represents the potentially blocking presenter Quiesce call after
        // the process-global registry lock has already been released.
        for (Panel* panel : snapshot) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            panel->prepares.fetch_add(1, std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lock(barrierMutex_);
            transitionInFlight_ = false;
        }
        barrierCv_.notify_all();
    }

    void DestroyAfterGuiBarrier(Panel* panel)
    {
        std::unique_lock<std::mutex> lock(barrierMutex_);
        barrierCv_.wait(lock, [&] { return !transitionInFlight_; });
        lock.unlock();

        std::lock_guard<std::mutex> registryLock(registryMutex_);
        registry_.erase(
            std::remove(registry_.begin(), registry_.end(), panel),
            registry_.end());
    }

private:
    std::mutex registryMutex_;
    std::vector<Panel*> registry_;
    std::mutex barrierMutex_;
    std::condition_variable barrierCv_;
    bool transitionInFlight_ = false;
};

} // namespace

int main()
{
    RegistryModel registry;
    Panel instanceA{1};
    Panel instanceB{2};
    registry.Publish(&instanceA);
    registry.Publish(&instanceB);

    std::thread transition([&] { registry.PrepareForInstance(1); });

    // Wait until the emulation-thread transition has taken its snapshot and
    // entered the blocking section, then request GUI-owned close of B. B's
    // close is allowed to proceed only after A's transition barrier completes.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::thread closeB([&] { registry.DestroyAfterGuiBarrier(&instanceB); });

    transition.join();
    closeB.join();

    if (instanceA.prepares.load(std::memory_order_relaxed) != 1
        || instanceB.prepares.load(std::memory_order_relaxed) != 0) {
        std::fprintf(stderr, "FAIL native panel registry transition model\n");
        return 1;
    }

    std::puts("PASS: native panel registry snapshot/lifetime barrier model");
    return 0;
}
