#pragma once

// Phantasm - native self-tests: the shared plain-assert harness (no framework). One counter pair for the
// whole exe; each unit's test file adds a Run<Unit>Tests() that main (TestMain.cpp) calls.
//
// Author: Severause

#include <cstdio>

namespace Phantasm::Tests
{
    inline int g_failures = 0;
    inline int g_checks = 0;

    inline void Check(bool ok, const char* what)
    {
        ++g_checks;
        if (!ok) { ++g_failures; std::printf("FAIL  %s\n", what); }
        else std::printf("ok    %s\n", what);
    }
}
