// Pure state-model tests for the Linux XI2 reset-fence contract.

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <vector>

namespace {

constexpr std::size_t kMaxXiEventsPerChunk = 64;

struct ResetFenceModel
{
    std::size_t processed = 0;
    std::size_t requestedAfter = 0;
    std::size_t appliedAfter = 0;
    std::size_t generation = 0;
    bool resetPending = false;
    std::vector<std::size_t> eventGeneration;

    void requestReset() noexcept
    {
        requestedAfter = processed;
        resetPending = true;
    }

    void run(std::size_t eventCount, std::size_t requestAfter)
    {
        while (processed < eventCount) {
            const std::size_t chunkEnd = std::min(
                eventCount, processed + kMaxXiEventsPerChunk);
            while (processed < chunkEnd) {
                eventGeneration.push_back(generation);
                ++processed;
                if (processed == requestAfter)
                    requestReset();
            }

            // The production filter drains the eventfd only after poll says
            // it is readable, then starts the next bounded X-event chunk.
            if (resetPending) {
                resetPending = false;
                ++generation;
                appliedAfter = processed;
            }
        }
    }
};

bool CheckBoundedFence(std::size_t eventCount, std::size_t requestAfter)
{
    ResetFenceModel model;
    model.run(eventCount, requestAfter);
    if (model.resetPending || model.generation != 1)
        return false;
    if (model.appliedAfter < requestAfter
        || model.appliedAfter - requestAfter > kMaxXiEventsPerChunk)
        return false;
    for (std::size_t i = model.appliedAfter;
         i < model.eventGeneration.size(); ++i) {
        if (model.eventGeneration[i] != 1)
            return false;
    }
    return true;
}

} // namespace

int main()
{
    // Request between event A and event B: the active bounded chunk finishes,
    // then the fence is applied before the next chunk.
    if (!CheckBoundedFence(kMaxXiEventsPerChunk + 1, 1))
        return 1;

    // A long XPending flood cannot postpone the reset beyond one chunk.
    if (!CheckBoundedFence(10000, 100))
        return 1;

    // A request at the exact chunk edge is applied before the following event.
    if (!CheckBoundedFence(kMaxXiEventsPerChunk + 1, kMaxXiEventsPerChunk))
        return 1;

    std::cout << "linux-reset-fence-tests: PASS\n";
    return 0;
}
