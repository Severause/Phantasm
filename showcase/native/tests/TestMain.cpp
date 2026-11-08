// Phantasm Showcase - self-tests: the runner. Add a unit's Run<Unit>Tests() here.

#include "TestHarness.h"

namespace Showcase::Tests
{
    void RunTextTests();
    void RunViewCoreTests();
    void RunAnimPathTests();
    void RunImageTests();
    void RunTurnTests();
    void RunItemTests();
    void RunTryOnTests();
    void RunPeopleTests();
    void RunHostTests();
    void RunPanelTests();
}

int main()
{
    using namespace Showcase::Tests;
    RunTextTests();
    RunViewCoreTests();
    RunAnimPathTests();
    RunImageTests();
    RunTurnTests();
    RunItemTests();
    RunTryOnTests();
    RunPeopleTests();
    RunHostTests();
    RunPanelTests();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
