// Engine-free tests for NeuralLayerCore (keeping other programs' Vulkan layers out of NR's instance).

#include "TestHarness.h"

#include "NeuralLayerCore.h"

namespace Phantasm::Tests
{
    void RunNeuralLayerCoreTests()
    {
        namespace NL = Phantasm::NeuralLayerCore;

        Check(NL::KeepsLayersOut(NL::Version(1, 3, 234)), "neural layers: the first loader with the switch keeps them out");
        Check(NL::KeepsLayersOut(NL::Version(1, 4, 357)) && NL::KeepsLayersOut(NL::Version(2, 0, 0)),
              "neural layers: so does every later loader");
        Check(!NL::KeepsLayersOut(NL::Version(1, 3, 233)) && !NL::KeepsLayersOut(NL::Version(1, 2, 999)) &&
                  !NL::KeepsLayersOut(NL::Version(1, 0, 0)),
              "neural layers: an older loader does not read the switch");
        Check(NL::KeepsLayersOut((1u << 29) | NL::Version(1, 3, 234)) && !NL::KeepsLayersOut((7u << 29) | NL::Version(1, 3, 0)),
              "neural layers: the variant bits do not count");
        Check(NL::kDisableVariable == L"VK_LOADER_LAYERS_DISABLE" && NL::kDisableImplicit == L"~implicit~",
              "neural layers: the loader's own names");
    }
}
