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
    RetireRequested,
    Retiring,
    DestroySafe,
    Invalid,
};

class LifecycleModel
{
public:
    void requestShow()
    {
        if (PhaseValue == Phase::Hidden || PhaseValue == Phase::Invalid
            || PhaseValue == Phase::DestroySafe)
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
        if (PresenterGeneration != 0)
            throw std::logic_error("presenter bound while the old presenter is live");
        if (HadPresenter && PresenterGeneration == GenerationValue)
            throw std::logic_error("same-generation presenter rebind");
        if (HadPresenter)
            ++RebindCount;
        HadPresenter = true;
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

    void beginFrame()
    {
        if (PhaseValue != Phase::PresentAllowed)
            throw std::logic_error("frame began without a bound presenter");

        // This is the complete lifecycle critical section in the production
        // design: copy the current generation/snapshot, then release it before
        // any Vulkan operation begins.
        LifecycleLockHeld = true;
        ++SnapshotReads;
        LifecycleLockHeld = false;
        FrameActive = true;
    }

    void acquireNextImage()
    {
        requireVulkanOutsideLifecycleLock("vkAcquireNextImageKHR");
        FrameStage = Stage::Acquire;
    }

    void queueSubmit()
    {
        requireVulkanOutsideLifecycleLock("vkQueueSubmit");
        if (FrameStage != Stage::Acquire)
            throw std::logic_error("submit occurred before acquire");
        FrameStage = Stage::Submit;
    }

    void queuePresent()
    {
        requireVulkanOutsideLifecycleLock("vkQueuePresentKHR");
        if (FrameStage != Stage::Submit)
            throw std::logic_error("present occurred before submit");
        FrameStage = Stage::Present;
    }

    void endFrame()
    {
        if (!FrameActive || FrameStage != Stage::Present)
            throw std::logic_error("frame ended before present");
        FrameActive = false;
        FrameStage = Stage::None;
    }

    void requestRetire()
    {
        if (PhaseValue == Phase::RetireRequested || PhaseValue == Phase::Retiring
            || PhaseValue == Phase::DestroySafe)
        {
            return;
        }
        if (PresenterGeneration == 0 && !FrameActive)
        {
            PhaseValue = Phase::DestroySafe;
            return;
        }
        PhaseValue = Phase::RetireRequested;
    }

    [[nodiscard]] bool canBeginFrame() const noexcept
    {
        return PhaseValue == Phase::PresentAllowed && !LifecycleLockHeld;
    }

    void completeRetire()
    {
        if (PhaseValue != Phase::RetireRequested && PhaseValue != Phase::Retiring)
            throw std::logic_error("retire completed without a retire request");
        if (FrameActive)
            throw std::logic_error("destroy-safe reached while a frame lease is active");
        PhaseValue = Phase::Retiring;
        PresenterGeneration = 0;
        PhaseValue = Phase::DestroySafe;
        ++RetireCount;
    }

    void hide()
    {
        requestRetire();
        if (FrameActive)
            endFrame();
        if (PhaseValue == Phase::RetireRequested || PhaseValue == Phase::Retiring)
            completeRetire();
        ++GenerationValue;
        PhaseValue = Phase::Invalid;
    }

    void surfaceAboutToBeDestroyed()
    {
        requestRetire();
        ++GenerationValue;
    }

    void dispatchNestedLifecycleEvents()
    {
        dispatchLifecycleEvent(Event::Show);
    }

    void queueResult(Result result)
    {
        if (result == Result::SurfaceLost)
        {
            RebindRequested = true;
            Fatal = false;
            PhaseValue = Phase::RetireRequested;
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
        ++GenerationValue;
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
    [[nodiscard]] std::uint64_t rebindCount() const noexcept { return RebindCount; }
    [[nodiscard]] std::uint64_t retireCount() const noexcept { return RetireCount; }
    [[nodiscard]] std::uint64_t snapshotReads() const noexcept { return SnapshotReads; }
    [[nodiscard]] std::uint64_t nestedLifecycleEvents() const noexcept
    {
        return NestedLifecycleEvents;
    }
    [[nodiscard]] std::uint64_t lifecycleLockVulkanViolations() const noexcept
    {
        return LifecycleLockVulkanViolations;
    }

private:
    enum class Stage
    {
        None,
        Acquire,
        Submit,
        Present,
    };

    enum class Event
    {
        Show,
        PlatformSurface,
        WinIdChange,
    };

    void requireVulkanOutsideLifecycleLock(const char* operation)
    {
        if (LifecycleLockHeld)
        {
            ++LifecycleLockVulkanViolations;
            throw std::logic_error(operation);
        }
    }

    void dispatchLifecycleEvent(Event event)
    {
        if (HandlingLifecycleEvent)
        {
            ++NestedLifecycleEvents;
            return;
        }

        HandlingLifecycleEvent = true;
        requestRetire();
        if (PhaseValue == Phase::RetireRequested || PhaseValue == Phase::Retiring)
            completeRetire();
        ++GenerationValue;
        PhaseValue = Phase::Invalid;

        // QWidget::event(Show) can synchronously re-enter PlatformSurface and
        // WinIdChange. The guard must make that path finite and non-recursive.
        if (event == Event::Show)
        {
            dispatchLifecycleEvent(Event::PlatformSurface);
            dispatchLifecycleEvent(Event::WinIdChange);
        }
        HandlingLifecycleEvent = false;
    }

    Phase PhaseValue = Phase::Hidden;
    std::uint64_t GenerationValue = 0;
    std::uint64_t PresenterGeneration = 0;
    std::uint64_t WindowId = 0;
    std::uint64_t RebindCount = 0;
    std::uint64_t RetireCount = 0;
    std::uint64_t SnapshotReads = 0;
    std::uint64_t NestedLifecycleEvents = 0;
    std::uint64_t LifecycleLockVulkanViolations = 0;
    bool RebindRequested = false;
    bool Fatal = false;
    bool HadPresenter = false;
    bool FrameActive = false;
    bool LifecycleLockHeld = false;
    bool HandlingLifecycleEvent = false;
    Stage FrameStage = Stage::None;
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

void TestSteadyStateDoesNotRebind()
{
    LifecycleModel model;
    model.requestShow();
    model.surfaceCreated(0xA);
    model.bindPresenter();
    model.allowPresent();

    for (int frame = 0; frame < 1000; ++frame)
    {
        model.beginFrame();
        model.acquireNextImage();
        model.queueSubmit();
        model.queuePresent();
        model.endFrame();
    }

    Require(model.generation() == 1, "steady-state frames must not change generation");
    Require(model.rebindCount() == 0, "steady-state frames must not rebind the presenter");
    Require(model.snapshotReads() == 1000, "each frame must take one short lifecycle snapshot");
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
    Require(model.rebindCount() == 1, "hide/show must perform exactly one rebind");
}

void TestSurfaceAboutToBeDestroyedReachesDestroySafe()
{
    LifecycleModel model;
    model.requestShow();
    model.surfaceCreated(0xC);
    model.bindPresenter();
    model.allowPresent();

    model.beginFrame();
    model.requestRetire();
    Require(!model.canBeginFrame(), "retire request must stop new frame admission");
    model.surfaceAboutToBeDestroyed();
    model.acquireNextImage();
    model.queueSubmit();
    model.queuePresent();
    model.endFrame();
    model.completeRetire();

    Require(model.phase() == Phase::DestroySafe,
        "surface destruction must wait for the old frame lease to retire");
    Require(model.presenterGeneration() == 0,
        "destroy-safe must not retain the old presenter generation");
    Require(model.retireCount() == 1, "surface destruction must retire exactly once");
}

void TestNestedLifecycleEventsDoNotReenter()
{
    LifecycleModel model;
    model.requestShow();
    model.surfaceCreated(0xD);
    model.bindPresenter();
    model.allowPresent();
    model.dispatchNestedLifecycleEvents();

    Require(model.nestedLifecycleEvents() == 2,
        "nested PlatformSurface and WinIdChange events must be absorbed by the guard");
    Require(model.phase() == Phase::Invalid,
        "the outer lifecycle transition must leave the old surface invalidated");
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

void TestVulkanStagesAreOutsideLifecycleLock()
{
    LifecycleModel model;
    model.requestShow();
    model.surfaceCreated(0xE);
    model.bindPresenter();
    model.allowPresent();
    model.beginFrame();
    model.acquireNextImage();
    model.queueSubmit();
    model.queuePresent();
    model.endFrame();

    Require(model.lifecycleLockVulkanViolations() == 0,
        "Acquire/Submit/Present must not run under the lifecycle lock");
}

} // namespace

int main()
{
    try
    {
        TestStartupOrder();
        TestSteadyStateDoesNotRebind();
        TestHideShowNeverReusesGeneration();
        TestSurfaceAboutToBeDestroyedReachesDestroySafe();
        TestNestedLifecycleEventsDoNotReenter();
        TestSurfaceLossIsRecoverable();
        TestVulkanStagesAreOutsideLifecycleLock();
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
