// Phantasm - native self-tests: the runner. Each unit's test file defines a Run<Unit>Tests() called from here.

#include "TestHarness.h"

#include <cstdio>

namespace Phantasm::Tests
{
    void RunActorCollectCoreTests();
    void RunAsciiTextTests();
    void RunCastingArtCoreTests();
    void RunCompanionCutoutCoreTests();
    void RunControllerKeysCoreTests();
    void RunFaceMorphCoreTests();
    void RunHkxPackfileCoreTests();
    void RunImageHostProbeCoreTests();
    void RunItemCollectCoreTests();
    void RunLastCaptureCoreTests();
    void RunLivePhysicsCoreTests();
    void RunLiveTextureCacheCoreTests();
    void RunLiveViewBudgetCoreTests();
    void RunLiveViewLifecycleCoreTests();
    void RunLiveViewSizingCoreTests();
    void RunFreezeCoreTests();
    void RunMannequinClipCoreTests();
    void RunMannequinPhysicsCoreTests();
    void RunMannequinSpringCoreTests();
    void RunMirrorCameraCoreTests();
    void RunMirrorLightingCoreTests();
    void RunNifParticleCoreTests();
    void RunPresentHandoffCoreTests();
    void RunRendererFxCoreTests();
    void RunRendererMaterialCoreTests();
    void RunRestPoseCoreTests();
    void RunShaderContractCoreTests();
    void RunStillShotCoreTests();
    void RunViewRegistryCoreTests();
}

int main()
{
    using namespace Phantasm::Tests;
    RunActorCollectCoreTests();
    RunAsciiTextTests();
    RunCastingArtCoreTests();
    RunCompanionCutoutCoreTests();
    RunControllerKeysCoreTests();
    RunFaceMorphCoreTests();
    RunHkxPackfileCoreTests();
    RunImageHostProbeCoreTests();
    RunItemCollectCoreTests();
    RunLastCaptureCoreTests();
    RunLivePhysicsCoreTests();
    RunLiveTextureCacheCoreTests();
    RunLiveViewBudgetCoreTests();
    RunLiveViewLifecycleCoreTests();
    RunLiveViewSizingCoreTests();
    RunFreezeCoreTests();
    RunMannequinClipCoreTests();
    RunMannequinPhysicsCoreTests();
    RunMannequinSpringCoreTests();
    RunMirrorCameraCoreTests();
    RunMirrorLightingCoreTests();
    RunNifParticleCoreTests();
    RunPresentHandoffCoreTests();
    RunRendererFxCoreTests();
    RunRendererMaterialCoreTests();
    RunRestPoseCoreTests();
    RunShaderContractCoreTests();
    RunStillShotCoreTests();
    RunViewRegistryCoreTests();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
