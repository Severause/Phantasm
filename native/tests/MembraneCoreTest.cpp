// Phantasm - native self-tests: MembraneCore (an effect shader's membrane on an actor).

#include "TestHarness.h"

#include "MembraneCore.h"

#include <cmath>
#include <limits>

namespace Phantasm::Tests
{
    namespace
    {
        namespace MC = Phantasm::MembraneCore;

        bool Near(float a, float b, float tol = 0.001f) { return std::fabs(a - b) <= tol; }

        // GhostFXShader as LoreRim's winner has it (spookier ghosts.esp over Skyrim.esm 03B6CB).
        MC::ShaderFacts GhostFx()
        {
            MC::ShaderFacts f;
            f.edge           = { 84.0f / 255.0f, 125.0f / 255.0f, 188.0f / 255.0f };
            f.edgeFalloff    = 2.0f;
            f.edgePersistent = 0.3f;
            f.fillKeyScale[2] = 1.5f;
            f.fillKeyTime[2]  = 0.5f;
            f.fillPersistent = 0.2f;
            f.hasFillTexture = true;
            f.projectedUV    = true;
            return f;
        }
    }

    void RunMembraneCoreTests()
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();

        {
            const auto l = MC::Persistent(GhostFx(), true);
            Check(l.on && l.projectedUV, "membrane: a ghost's shader draws, its fill laid on by height");
            Check(Near(l.rimStrength, 0.3f * MC::kRimGain) && Near(l.falloff, 2.0f) && Near(l.rim.b, 188.0f / 255.0f),
                  "membrane: the rim at the persistent edge ratio, its colour and falloff");
            Check(Near(l.fillStrength, 0.2f * MC::kFillGain) && Near(l.fill.r, 0.0f),
                  "membrane: the fill at the persistent ratio; a black fill colour leaves the texture's own");
            Check(Near(l.baseAlpha, MC::kGhostBaseAlpha), "membrane: a ghost's pieces are drawn faint under it");
            const auto other = MC::Persistent(GhostFx(), false);
            Check(!other.on && Near(other.baseAlpha, 1.0f), "membrane: anyone else's lasting shader is not drawn");
        }
        {
            auto f            = GhostFx();
            f.edgeSubtractive = true;
            Check(!MC::Persistent(f, true).on, "membrane: a subtractive edge is not drawn");
            f.edgeSubtractive = false;
            f.greyscale       = true;
            Check(!MC::Persistent(f, true).on, "membrane: a palette shader is not drawn");
        }
        {
            auto f     = GhostFx();
            f.membrane = false;
            const auto l = MC::Persistent(f, true);
            Check(!l.on && Near(l.baseAlpha, 1.0f), "membrane: a shader whose membrane is off draws nothing, a ghost solid");
        }
        {
            auto f           = GhostFx();
            f.edgePersistent = 0.0f;
            f.hasFillTexture = false;
            Check(!MC::Persistent(f, true).on, "membrane: no rim and no fill texture draws nothing");
            f.edgePersistent = nan;
            f.edgeFalloff    = nan;
            f.hasFillTexture = true;
            const auto l     = MC::Persistent(f, true);
            Check(l.on && Near(l.rimStrength, 0.0f) && Near(l.falloff, 1.0f), "membrane: values that are not finite read as none");
        }
        {
            MC::ShaderFacts f;
            f.fillKeys[0]     = { 1.0f, 0.0f, 0.0f };
            f.fillKeys[1]     = { 0.0f, 1.0f, 0.0f };
            f.fillKeyTime[1]  = 0.7f;
            f.fillKeyScale[1] = 0.5f;
            const auto fill   = MC::PersistentFill(f);
            Check(Near(fill.g, 0.5f) && Near(fill.r, 0.0f), "membrane: the persistent fill is the latest key, scaled");
        }
        {
            Check(Near(MC::ProjectedV(-75.0f, -75.0f, 150.0f), 0.0f) && Near(MC::ProjectedV(75.0f, -75.0f, 150.0f), 1.0f) &&
                      Near(MC::ProjectedV(0.0f, -75.0f, 150.0f), 0.5f),
                  "membrane: v runs from the feet to the head");
            Check(Near(MC::ProjectedV(500.0f, -75.0f, 150.0f), 1.0f) && Near(MC::ProjectedV(0.0f, 0.0f, 0.0f), 0.0f),
                  "membrane: v is clamped, and 0 with no height");
            Check(Near(MC::ProjectedU(1.0f, 0.0f), 0.0f) && Near(MC::ProjectedU(-1.0f, 0.0f), 1.0f) &&
                      Near(MC::ProjectedU(0.0f, 1.0f), 0.5f) && Near(MC::ProjectedU(0.0f, -1.0f), 0.5f),
                  "membrane: u is the angle about the figure, both halves folded onto one");
        }
    }
}
