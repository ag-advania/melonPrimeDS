/*
    Contract model for the native panel registry transition boundary.

    The production Vulkan/DX12 walks copy matching panel addresses while the
    registry mutex is held, then call Quiesce after unlock. GUI destruction is
    serialized by the prepareVideoBackendTransition() barrier. This test keeps
    that lifetime rule executable without Qt or a renderer SDK and exercises
    two instances while both a matching panel and another-instance panel close
    during the blocking transition.
*/

#include <algorithm>
#include <atomic>
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
    std::atomic_bool destroyed{false};
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
            blockingSectionEntered_ = true;
            allowTransitionFinish_ = false;
        }
        barrierCv_.notify_all();

        // Represents the potentially blocking presenter Quiesce call after
        // the process-global registry lock has already been released. The
        // test releases this wait only after both GUI close requests are
        // known to be waiting on the same transition barrier.
        {
            std::unique_lock<std::mutex> lock(barrierMutex_);
            barrierCv_.wait(lock, [&] { return allowTransitionFinish_; });
        }

        for (Panel* panel : snapshot) {
            if (panel->destroyed.load(std::memory_order_acquire))
                useAfterDestroy_.store(true, std::memory_order_release);
            panel->prepares.fetch_add(1, std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lock(barrierMutex_);
            transitionInFlight_ = false;
            blockingSectionEntered_ = false;
        }
        barrierCv_.notify_all();
    }

    void WaitForBlockingTransition()
    {
        std::unique_lock<std::mutex> lock(barrierMutex_);
        barrierCv_.wait(lock, [&] { return blockingSectionEntered_; });
    }

    void WaitForCloseRequests(std::size_t expected)
    {
        std::unique_lock<std::mutex> lock(barrierMutex_);
        barrierCv_.wait(lock, [&] { return closeRequests_ >= expected; });
    }

    void ReleaseTransition()
    {
        {
            std::lock_guard<std::mutex> lock(barrierMutex_);
            allowTransitionFinish_ = true;
        }
        barrierCv_.notify_all();
    }

    void DestroyAfterGuiBarrier(Panel* panel)
    {
        std::unique_lock<std::mutex> lock(barrierMutex_);
        ++closeRequests_;
        barrierCv_.notify_all();
        barrierCv_.wait(lock, [&] { return !transitionInFlight_; });
        lock.unlock();

        std::lock_guard<std::mutex> registryLock(registryMutex_);
        registry_.erase(
            std::remove(registry_.begin(), registry_.end(), panel),
            registry_.end());
        panel->destroyed.store(true, std::memory_order_release);
    }

    bool SawUseAfterDestroy() const
    {
        return useAfterDestroy_.load(std::memory_order_acquire);
    }

    std::size_t Size()
    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        return registry_.size();
    }

private:
    std::mutex registryMutex_;
    std::vector<Panel*> registry_;
    std::mutex barrierMutex_;
    std::condition_variable barrierCv_;
    bool transitionInFlight_ = false;
    bool blockingSectionEntered_ = false;
    bool allowTransitionFinish_ = false;
    std::size_t closeRequests_ = 0;
    std::atomic_bool useAfterDestroy_{false};
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
    registry.WaitForBlockingTransition();

    // The transition snapshot contains A. Request GUI-owned close of both A
    // (the matching snapshot entry) and B (another instance) while the
    // emulation thread is blocked in the post-unlock Quiesce model. Both close
    // requests must be observed before releasing the transition; no timing
    // sleep is used to establish this ordering.
    std::thread closeA([&] { registry.DestroyAfterGuiBarrier(&instanceA); });
    std::thread closeB([&] { registry.DestroyAfterGuiBarrier(&instanceB); });
    registry.WaitForCloseRequests(2);

    registry.ReleaseTransition();
    transition.join();
    closeA.join();
    closeB.join();

    if (instanceA.prepares.load(std::memory_order_relaxed) != 1
        || instanceB.prepares.load(std::memory_order_relaxed) != 0
        || !instanceA.destroyed.load(std::memory_order_acquire)
        || !instanceB.destroyed.load(std::memory_order_acquire)
        || registry.SawUseAfterDestroy()
        || registry.Size() != 0) {
        std::fprintf(stderr, "FAIL native panel registry transition model\n");
        return 1;
    }

    std::puts("PASS: native panel registry snapshot/lifetime barrier model");
    return 0;
}
