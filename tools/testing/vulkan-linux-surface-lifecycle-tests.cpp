/*
    GPU-free model tests for the Linux Vulkan native-surface contract.

    These tests intentionally model only the lifecycle decisions owned by
    ScreenPanelVulkan/VulkanPresenter. They do not claim that a compositor or
    a Vulkan driver is present; those remain runtime coverage items.
*/

#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace
{

enum class Result
{
    Success,
    SurfaceLost,
    DeviceLost,
};

enum class Phase
{
    Hidden,
    ShowRequested,
    SurfaceCreated,
    PresenterBound,
    PresentAllowed,
    Invalid,
};

class LifecycleModel
{
public:
    void requestShow()
    {
        if (PhaseValue == Phase::Hidden || PhaseValue == Phase::Invalid)
            PhaseValue = Phase::ShowRequested;
    }

    void surfaceCreated(std::uint64_t wid)
    {
        if (PhaseValue != Phase::ShowRequested)
            throw std::logic_error("SurfaceCreated before ShowRequested");
        ++GenerationValue;
        WindowId = wid;
        PhaseValue = Phase::SurfaceCreated;
    }

    void bindPresenter()
    {
        if (PhaseValue != Phase::SurfaceCreated)
            throw std::logic_error("presenter bound without a current surface");
        PresenterGeneration = GenerationValue;
        PhaseValue = Phase::PresenterBound;
    }

    void allowPresent()
    {
        if (PhaseValue != Phase::PresenterBound
            || PresenterGeneration != GenerationValue)
        {
            throw std::logic_error("present allowed before current-generation bind");
        }
        PhaseValue = Phase::PresentAllowed;
    }

    void hide()
    {
        ++GenerationValue;
        PresenterGeneration = 0;
        PhaseValue = Phase::Invalid;
    }

    void queueResult(Result result)
    {
        if (result == Result::SurfaceLost)
        {
            RebindRequested = true;
            Fatal = false;
        }
        else if (result == Result::DeviceLost)
        {
            Fatal = true;
        }
    }

    void retireForRebind()
    {
        if (!RebindRequested)
            throw std::logic_error("retired without a surface-rebind request");
        PresenterGeneration = 0;
        RebindRequested = false;
        PhaseValue = Phase::SurfaceCreated;
    }

    [[nodiscard]] Phase phase() const noexcept { return PhaseValue; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return GenerationValue; }
    [[nodiscard]] std::uint64_t presenterGeneration() const noexcept
    {
        return PresenterGeneration;
    }
    [[nodiscard]] std::uint64_t windowId() const noexcept { return WindowId; }
    [[nodiscard]] bool rebindRequested() const noexcept { return RebindRequested; }
    [[nodiscard]] bool fatal() const noexcept { return Fatal; }

private:
    Phase PhaseValue = Phase::Hidden;
    std::uint64_t GenerationValue = 0;
    std::uint64_t PresenterGeneration = 0;
    std::uint64_t WindowId = 0;
    bool RebindRequested = false;
    bool Fatal = false;
};

void Require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void TestStartupOrder()
{
    LifecycleModel model;
    Require(model.phase() == Phase::Hidden, "startup must begin hidden");
    model.requestShow();
    Require(model.phase() == Phase::ShowRequested, "show request was not recorded");
    model.surfaceCreated(0xA);
    Require(model.generation() == 1, "first surface must be generation 1");
    model.bindPresenter();
    model.allowPresent();
    Require(model.phase() == Phase::PresentAllowed, "present was not enabled after bind");
}

void TestHideShowNeverReusesGeneration()
{
    LifecycleModel model;
    model.requestShow();
    model.surfaceCreated(0xA);
    model.bindPresenter();
    model.allowPresent();
    model.hide();
    Require(model.phase() == Phase::Invalid, "hide must invalidate the surface");
    const std::uint64_t invalidGeneration = model.generation();
    model.requestShow();
    model.surfaceCreated(0xA);
    Require(model.windowId() == 0xA, "the test must cover an unchanged WId");
    Require(model.generation() > invalidGeneration, "show must publish a new generation");
    model.bindPresenter();
    Require(model.presenterGeneration() == model.generation(),
        "old presenter generation was reused");
}

void TestSurfaceLossIsRecoverable()
{
    LifecycleModel model;
    model.requestShow();
    model.surfaceCreated(0xB);
    model.bindPresenter();
    model.allowPresent();
    model.queueResult(Result::SurfaceLost);
    Require(model.rebindRequested(), "surface loss must request a rebind");
    Require(!model.fatal(), "surface loss must not latch fatal failure");
    model.retireForRebind();
    model.bindPresenter();
    model.allowPresent();
    Require(model.phase() == Phase::PresentAllowed,
        "presenter must become usable after a surface rebind");

    model.queueResult(Result::DeviceLost);
    Require(model.fatal(), "device loss must remain fatal");
}

} // namespace

int main()
{
    try
    {
        TestStartupOrder();
        TestHideShowNeverReusesGeneration();
        TestSurfaceLossIsRecoverable();
        std::cout << "vulkan-linux-surface-lifecycle-tests: PASS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "vulkan-linux-surface-lifecycle-tests: FAIL: "
                  << error.what() << '\n';
        return 1;
    }
}
