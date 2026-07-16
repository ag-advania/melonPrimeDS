// FrameQueue upstream-vs-Desktop differential harness (S75-6).

#include <cstdio>
#include <optional>

#include "DesktopFrameLifetimeTracker.h"
#include "SapphireGenerated/SapphireFrameQueueCore.h"
#include "VulkanReference/FrameQueue.h"

using namespace MelonDSAndroid;

namespace
{

FrameQueuePolicy defaultPolicy()
{
    return {};
}

bool framesMatch(const Frame* left, const Frame* right)
{
    if (left == nullptr || right == nullptr)
        return left == right;
    return left->frameId == right->frameId;
}

bool test_complete_lifetime_render_present_parity()
{
    SapphireFrameQueueCore core;
    DesktopFrameLifetimeTracker lifetime;
    FrameQueue wrapper;

    const FrameQueuePolicy policy = defaultPolicy();

    Frame* coreRender = core.getRenderFrame(policy);
    if (coreRender == nullptr)
        return false;
    lifetime.onRenderAcquired(coreRender, core);
    lifetime.onPushRendered(coreRender, core);
    core.pushRenderedFrame(coreRender, policy);
    Frame* corePresent = core.getPresentCandidate(policy, std::nullopt);

    Frame* wrapperRender = wrapper.getRenderFrame(policy);
    if (wrapperRender == nullptr)
        return false;
    wrapper.validateRenderFrame(wrapperRender, 256, 192, FrameBackend::VulkanImage);
    wrapper.pushRenderedFrame(wrapperRender, policy);
    Frame* wrapperPresent = wrapper.getPresentCandidate(policy, std::nullopt);

    return framesMatch(coreRender, wrapperRender)
        && framesMatch(corePresent, wrapperPresent);
}

bool test_wrapper_defers_present_candidate_when_reference_active()
{
    FrameQueue wrapper;
    const FrameQueuePolicy policy = defaultPolicy();

    Frame* render = wrapper.getRenderFrame(policy);
    if (render == nullptr)
        return false;
    wrapper.validateRenderFrame(render, 256, 192, FrameBackend::VulkanImage);
    wrapper.pushRenderedFrame(render, policy);

    Frame* firstCandidate = wrapper.getPresentCandidate(policy, std::nullopt);
    if (firstCandidate == nullptr)
        return false;

    Frame* secondCandidate = wrapper.getPresentCandidate(policy, std::nullopt);
    return secondCandidate == nullptr;
}

bool test_core_and_wrapper_agree_when_present_queue_empty()
{
    SapphireFrameQueueCore core;
    FrameQueue wrapper;
    const FrameQueuePolicy policy = defaultPolicy();

    Frame* corePresent = core.getPresentFrame(policy, std::nullopt);
    Frame* wrapperPresent = wrapper.getPresentFrame(policy, std::nullopt);
    return corePresent == nullptr && wrapperPresent == nullptr;
}

} // namespace

int main()
{
    if (!test_complete_lifetime_render_present_parity())
    {
        std::fprintf(stderr, "FAIL: test_complete_lifetime_render_present_parity\n");
        return 1;
    }
    if (!test_wrapper_defers_present_candidate_when_reference_active())
    {
        std::fprintf(stderr, "FAIL: test_wrapper_defers_present_candidate_when_reference_active\n");
        return 1;
    }
    if (!test_core_and_wrapper_agree_when_present_queue_empty())
    {
        std::fprintf(stderr, "FAIL: test_core_and_wrapper_agree_when_present_queue_empty\n");
        return 1;
    }
    std::fprintf(stdout, "FrameQueue differential harness OK\n");
    return 0;
}
