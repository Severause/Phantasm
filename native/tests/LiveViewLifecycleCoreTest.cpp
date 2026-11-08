// Phantasm - native self-tests: LiveViewLifecycleCore (when the live actor view recollects, waits or ends,
// and when a live view has its GPU bundle).
//
// Author: Severause

#include "LiveViewLifecycleCore.h"
#include "TestHarness.h"

#include <string_view>

namespace Phantasm::Tests
{
    void RunLiveViewLifecycleCoreTests()
    {
        namespace LVL = Phantasm::LiveViewLifecycleCore;
        using LVL::Recollect;
        using std::chrono::milliseconds;

        // 1. A loaded actor is collected, however long it was away before
        Check(LVL::DecideRecollect(true, false, true, milliseconds(0)) == Recollect::Collect,
              "lifecycle: a loaded actor is collected");
        Check(LVL::DecideRecollect(true, false, true, milliseconds(60000)) == Recollect::Collect,
              "lifecycle: a 3D back after a long absence is collected");

        // 2. Missing or dead ends the view at once
        Check(LVL::DecideRecollect(false, false, false, milliseconds(0)) == Recollect::EndGone,
              "lifecycle: a missing actor ends the view");
        Check(LVL::DecideRecollect(true, true, true, milliseconds(0)) == Recollect::EndGone,
              "lifecycle: a dead actor ends the view, 3D or not");

        // 3. Alive with no 3D: wait out the start's budget, then end as not loaded
        Check(LVL::DecideRecollect(true, false, false, milliseconds(0)) == Recollect::Wait,
              "lifecycle: the first miss of a live actor's 3D waits");
        Check(LVL::DecideRecollect(true, false, false, LVL::kAbsentBudget - milliseconds(1)) == Recollect::Wait,
              "lifecycle: a 3D missing just under the budget still waits");
        Check(LVL::DecideRecollect(true, false, false, LVL::kAbsentBudget) == Recollect::EndNotLoaded,
              "lifecycle: a 3D missing for the whole budget ends the view as not loaded");

        // 4. The running mirror waits exactly as long as the start retries
        Check(LVL::kAbsentBudget == milliseconds(3600), "lifecycle: the absence budget is the start's 30 x 120 ms");

        // 4b. Why a view whose actor cannot be drawn ends
        const auto same = [](const char* a, const char* b) { return std::string_view(a) == b; };
        Check(same(LVL::GoneReason(false, false), "missing") && same(LVL::GoneReason(false, true), "missing"),
              "lifecycle: no actor ends the view missing");
        Check(same(LVL::GoneReason(true, true), "dead"), "lifecycle: a dead actor ends the view dead");
        Check(same(LVL::GoneReason(true, false), "unrenderable"),
              "lifecycle: a living actor that cannot be drawn ends the view unrenderable");
        Check(LVL::ForgetsKeptLook(true, true, 0x0001A69Fu) && !LVL::ForgetsKeptLook(true, false, 0xFF000801u),
              "lifecycle: a found actor's look is forgotten only when they are dead");
        Check(LVL::ForgetsKeptLook(false, false, 0xFF000801u) && !LVL::ForgetsKeptLook(false, false, 0x0001A69Fu),
              "lifecycle: a missing runtime actor's look is forgotten, a missing plugin actor's is kept");

        // 4c. Failed recollects: a few in a row on one root end the view, a new root or a success starts again
        {
            LVL::RecollectFailures f;
            bool gaveUp = false;
            for (int i = 1; i < LVL::kRecollectFailLimit; ++i) {
                if (f.NoteFailure(0x10)) gaveUp = true;
            }
            Check(!gaveUp && f.Count() == LVL::kRecollectFailLimit - 1,
                  "lifecycle: failed recollects under the limit keep the view");
            Check(f.NoteFailure(0x10), "lifecycle: the limit of failed recollects on one root ends the view");
            Check(!f.NoteFailure(0x20) && f.Count() == 1, "lifecycle: a failure on a new root starts the count again");
            f.Reset();
            Check(f.Count() == 0 && !f.NoteFailure(0x20) && f.Count() == 1,
                  "lifecycle: a successful collect starts the count again");
            Check(LVL::kRecollectFailLimit > 1, "lifecycle: one failed recollect never ends the view");
        }

        // 5. The GPU bundle: shaders first, then the device objects, and a refusing device is left alone a while
        using LVL::GpuBundle;
        using LVL::ShaderBuild;
        const milliseconds never(0);
        Check(LVL::DecideGpuBundle(false, ShaderBuild::NotStarted, false, never) == GpuBundle::Pending,
              "gpu: shaders not started yet draw nothing this frame");
        Check(LVL::DecideGpuBundle(false, ShaderBuild::Compiling, false, never) == GpuBundle::Pending,
              "gpu: shaders still compiling draw nothing this frame");
        Check(LVL::DecideGpuBundle(false, ShaderBuild::Ready, false, never) == GpuBundle::Build,
              "gpu: compiled shaders build the device objects");
        Check(LVL::DecideGpuBundle(true, ShaderBuild::Ready, false, never) == GpuBundle::Ready,
              "gpu: a bundle on this device draws");
        Check(LVL::DecideGpuBundle(false, ShaderBuild::Failed, false, never) == GpuBundle::Failed,
              "gpu: a failed compile makes the views unavailable");
        Check(LVL::DecideGpuBundle(false, ShaderBuild::Ready, true, milliseconds(0)) == GpuBundle::Failed,
              "gpu: a device that just refused is not asked again at once");
        Check(LVL::DecideGpuBundle(true, ShaderBuild::Ready, true, LVL::kGpuRetryDelay - milliseconds(1)) ==
                  GpuBundle::Failed,
              "gpu: a refusal of the live targets holds over a built bundle until the delay passes");
        Check(LVL::DecideGpuBundle(false, ShaderBuild::Ready, true, LVL::kGpuRetryDelay) == GpuBundle::Build,
              "gpu: after the delay the device is asked again");
        Check(LVL::DecideGpuBundle(true, ShaderBuild::Ready, true, LVL::kGpuRetryDelay) == GpuBundle::Ready,
              "gpu: after the delay a built bundle draws again");
        Check(LVL::DecideGpuBundle(false, ShaderBuild::Failed, true, LVL::kGpuRetryDelay * 2) == GpuBundle::Failed,
              "gpu: a failed compile is final whatever the device does");
        Check(LVL::DecideGpuFrame(true, ShaderBuild::Ready, true, milliseconds(0), true) == GpuBundle::Ready,
              "gpu: a view holding the bundle and its targets goes on drawing through another view's refusal");
        Check(LVL::DecideGpuFrame(true, ShaderBuild::Ready, true, milliseconds(0), false) == GpuBundle::Failed,
              "gpu: a view that needs new targets inside the window is refused");
        Check(LVL::DecideGpuFrame(false, ShaderBuild::Ready, true, milliseconds(0), true) == GpuBundle::Failed,
              "gpu: so is one that needs the bundle built");
        Check(LVL::DecideGpuFrame(true, ShaderBuild::Failed, false, never, true) == GpuBundle::Failed,
              "gpu: a failed compile ends even a view holding its targets");
        Check(LVL::DecideGpuFrame(false, ShaderBuild::Ready, false, never, false) == GpuBundle::Build,
              "gpu: outside a window a frame decides as a start does");

        // 6. The item view's particle shaders: their own stage, which only ever turns the particles off
        using LVL::FxStage;
        Check(LVL::DecideFxStage(false, ShaderBuild::NotStarted, false) == FxStage::Pending &&
                  LVL::DecideFxStage(false, ShaderBuild::Compiling, false) == FxStage::Pending,
              "fx shaders: not compiled yet, the frame draws without particles and the compile goes on");
        Check(LVL::DecideFxStage(false, ShaderBuild::Ready, false) == FxStage::Build,
              "fx shaders: compiled, the device objects are built");
        Check(LVL::DecideFxStage(true, ShaderBuild::Ready, false) == FxStage::Ready,
              "fx shaders: built on this device, particles draw");
        Check(LVL::DecideFxStage(false, ShaderBuild::Failed, false) == FxStage::Off &&
                  LVL::DecideFxStage(false, ShaderBuild::Ready, true) == FxStage::Off,
              "fx shaders: a failed compile or a refusing device turns the particles off");
        // Their failure leaves the bundle's decision as it was: the two stages share no state.
        Check(LVL::DecideGpuBundle(true, ShaderBuild::Ready, false, never) == GpuBundle::Ready &&
                  LVL::DecideFxStage(false, ShaderBuild::Failed, false) == FxStage::Off,
              "fx shaders: failed particle shaders leave the live views' bundle drawing");
    }
}
