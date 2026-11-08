// Phantasm - native self-tests: ItemCollectCore (which objects of an item model the item view skips, one rule for
// the collector and the private parse's count).
//
// Author: Severause

#include "ItemCollectCore.h"
#include "TestHarness.h"

namespace Phantasm::Tests
{
    namespace
    {
        namespace IC = Phantasm::ItemCollectCore;
        using IC::Skip;

        bool Same(const IC::Tier& a, const IC::Tier& b)
        {
            return a.honorHidden == b.honorHidden && a.permissive == b.permissive &&
                   a.keepAnimatedEffects == b.keepAnimatedEffects;
        }
    }

    void RunItemCollectCoreTests()
    {
        const IC::Tier first = IC::kTiers[0], second = IC::kTiers[1], permissive = IC::kTiers[2];
        Check(first.honorHidden && !first.permissive && !second.honorHidden && !second.permissive &&
                  permissive.permissive && !permissive.honorHidden,
              "item collect: hides honoured, then ignored, then the permissive tier");
        Check(Same(IC::kCountTier, second),
              "item collect: the rescue's count is the second tier's rule, effects not kept");

        // Objects: a hidden branch only while hides are honoured; markers and blood shells in every tier.
        IC::ObjectFacts hidden;
        hidden.hidden = true;
        Check(IC::ObjectSkip(hidden, first) == Skip::kHidden && IC::ObjectSkip(hidden, second) == Skip::kNone &&
                  IC::ObjectSkip(hidden, IC::kCountTier) == Skip::kNone,
              "item collect: a hidden object is skipped only while hides are honoured");
        IC::ObjectFacts marker, blood;
        marker.editorMarker = true;
        blood.bloodShell    = true;
        Check(IC::ObjectSkip(marker, permissive) == Skip::kEditorMarker &&
                  IC::ObjectSkip(blood, permissive) == Skip::kBloodShell &&
                  IC::ObjectSkip(blood, IC::kCountTier) == Skip::kBloodShell,
              "item collect: editor markers and blood shells are skipped in every tier, the count's included");

        // The bound bow's three pieces (LoreRim's boundbow.nif): the lit bow at material alpha 0, the flame shell (an
        // effect shader at alpha 0) and a lit refraction pass at alpha 1.
        IC::GeomFacts bow;
        bow.triShape      = true;
        bow.materialAlpha = 0.0f;
        IC::GeomFacts flames = bow;
        flames.effectShader  = true;
        flames.blended       = true;
        IC::GeomFacts refraction;
        refraction.triShape   = true;
        refraction.refraction = true;
        Check(IC::GeomSkip(bow, first) == Skip::kInvisible && IC::GeomSkip(flames, first) == Skip::kEffect &&
                  IC::GeomSkip(refraction, first) == Skip::kRefraction,
              "item collect: the bound bow's pieces all skip in the first tier");
        Check(IC::GeomSkip(bow, permissive) == Skip::kNone && IC::GeomSkip(flames, permissive) == Skip::kNone &&
                  IC::GeomSkip(refraction, permissive) == Skip::kRefraction,
              "item collect: the permissive tier draws the bow and its flames, never the refraction pass");
        Check(IC::GeomSkip(bow, IC::kCountTier) != Skip::kNone && IC::GeomSkip(flames, IC::kCountTier) != Skip::kNone &&
                  IC::GeomSkip(refraction, IC::kCountTier) == Skip::kRefraction,
              "item collect: the count agrees: none of them is real geometry");

        // Glow-only meshes: a glow map with no authored diffuse is an aura, in every tier.
        IC::GeomFacts aura;
        aura.glowMap    = true;
        aura.hasDiffuse = false;
        IC::GeomFacts glowingSurface = aura;
        glowingSurface.hasDiffuse    = true;
        Check(IC::GeomSkip(aura, permissive) == Skip::kGlowOnly && IC::GeomSkip(aura, IC::kCountTier) == Skip::kGlowOnly &&
                  IC::GeomSkip(glowingSurface, first) == Skip::kNone,
              "item collect: a glow-only aura is skipped everywhere; a glowing surface with a diffuse is drawn");

        // Effect shells: kept only where the view animates them, or by the permissive tier.
        IC::Tier animating = first;
        animating.keepAnimatedEffects = true;
        IC::GeomFacts flame;
        flame.effectShader = true;
        flame.blended      = true;
        flame.triShape     = true;
        flame.triangles    = 2;   // a flame card is a quad too
        Check(IC::GeomSkip(flame, first) == Skip::kDecorQuad && IC::GeomSkip(flame, animating) == Skip::kNone,
              "item collect: an animated flame card stays, though it is a blended quad");
        IC::GeomFacts blob;
        blob.blended   = true;
        blob.triShape  = true;
        blob.triangles = 2;
        IC::GeomFacts unknown = blob;
        unknown.triangles     = 0;
        Check(IC::GeomSkip(blob, animating) == Skip::kDecorQuad && IC::GeomSkip(blob, permissive) == Skip::kNone &&
                  IC::GeomSkip(unknown, first) == Skip::kNone,
              "item collect: a lit shadow blob stays out unless permissive; an unknown triangle count is not a quad");
        IC::GeomFacts nonVisual;
        nonVisual.nonVisual = true;
        Check(IC::GeomSkip(nonVisual, permissive) == Skip::kNonVisual, "item collect: non-visual shapes never draw");
        Check(IC::IsDecorQuad(true, true, 1) && !IC::IsDecorQuad(false, true, 2) && !IC::IsDecorQuad(true, false, 2) &&
                  !IC::IsDecorQuad(true, true, 3),
              "item collect: a decor quad is a blended tri-shape of one or two triangles");
    }
}
