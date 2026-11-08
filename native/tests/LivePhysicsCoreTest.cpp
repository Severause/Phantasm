// Phantasm - native self-tests: LivePhysicsCore (the live simulation's chains, resets, state frame and the game's
// own physics, and the armour addon an attached piece's node names).
//
// Author: Severause

#include "LivePhysicsCore.h"
#include "TestHarness.h"

#include <cmath>
#include <limits>

namespace Phantasm::Tests
{
    namespace
    {
        namespace LP = Phantasm::LivePhysicsCore;
        namespace RP = Phantasm::RestPoseCore;

        RP::Xf At(float x, float y, float z, float s = 1.0f)
        {
            RP::Xf t;
            t.t[0] = x;
            t.t[1] = y;
            t.t[2] = z;
            t.s    = s;
            return t;
        }

        RP::Xf TurnX(float deg, float x, float y, float z)
        {
            const float r = deg * 3.14159265f / 180.0f;
            RP::Xf      t = At(x, y, z);
            t.rot[1][1] = std::cos(r);
            t.rot[1][2] = -std::sin(r);
            t.rot[2][1] = std::sin(r);
            t.rot[2][2] = std::cos(r);
            return t;
        }
    }

    void RunLivePhysicsCoreTests()
    {
        // 1. Written over local: the engine's own world is not, a swing is, far from the origin too
        {
            const RP::Xf parent = TurnX(30.0f, 13844.75f, -66957.19f, 5760.2f);
            const RP::Xf local  = At(0.0f, 4.0f, -6.0f);
            const RP::Xf engine = RP::Compose(parent, local);
            Check(!LP::WrittenOverLocal(engine, parent, local), "live physics: the engine's world is parent x local");
            RP::Xf nudged = engine;
            nudged.t[0] += 0.02f;
            Check(!LP::WrittenOverLocal(nudged, parent, local), "live physics: float noise 70000 units out is not a write");
            const RP::Xf swung = RP::Compose(parent, RP::Compose(TurnX(12.0f, 0, 0, 0), local));
            Check(LP::WrittenOverLocal(swung, parent, local), "live physics: a bone swung on its parent is a write");
            RP::Xf bad = engine;
            bad.t[1]   = std::numeric_limits<float>::quiet_NaN();
            Check(LP::WrittenOverLocal(bad, parent, local), "live physics: a non-finite world keeps the live one");
        }

        // 2. Chains: parents first, what hangs under a simulated node carried, the rest left live
        {
            // 0 head (live), 1 hair root (simulated), 2 hair mid (simulated), 3 hair tip (named by no file),
            // 4 strap under the head (live), 5 a second strand (simulated) on the head, 6 its tip; listed out of order.
            const std::vector<int>          parent{ -1, 0, 1, 2, 0, 0, 5 };
            const std::vector<std::uint8_t> sim{ 0, 1, 1, 0, 0, 1, 0 };
            const auto                      plan = LP::PlanChains(parent, sim);
            Check(plan.role[0] == LP::NodeRole::kLive && plan.role[4] == LP::NodeRole::kLive,
                  "live physics: nodes above or beside the chains stay live");
            Check(plan.role[1] == LP::NodeRole::kSimulated && plan.role[2] == LP::NodeRole::kSimulated &&
                      plan.role[5] == LP::NodeRole::kSimulated,
                  "live physics: the named bones are simulated");
            Check(plan.role[3] == LP::NodeRole::kCarried && plan.role[6] == LP::NodeRole::kCarried,
                  "live physics: a bone under a simulated one is carried");
            Check(plan.chains == 2, "live physics: two chains, one per top simulated bone");
            Check(plan.order.size() == 5, "live physics: only the moving nodes are posed");
            std::vector<int> at(parent.size(), -1);
            for (std::size_t k = 0; k < plan.order.size(); ++k) at[static_cast<std::size_t>(plan.order[k])] = static_cast<int>(k);
            Check(at[1] < at[2] && at[2] < at[3] && at[5] < at[6], "live physics: a parent is posed before its children");
            const std::vector<int> loop{ 1, 0 };
            const auto             cut = LP::PlanChains(loop, { 1, 0 });
            Check(cut.role.size() == 2, "live physics: a parent loop is cut, not followed forever");
        }

        // 3. When the springs start again
        {
            Check(LP::PlanStep(-1.0f, 0.0f).reset, "live physics: the first tick starts at rest");
            const auto s = LP::PlanStep(1.0f / 60.0f, 7.0f);
            Check(!s.reset && std::fabs(s.dt - 1.0f / 60.0f) < 1e-6f, "live physics: a tick of running steps by its time");
            Check(LP::PlanStep(0.5f, 0.0f).reset, "live physics: a long gap (a menu, a hitch) starts again");
            Check(LP::PlanStep(1.0f / 60.0f, 5000.0f).reset, "live physics: a teleport starts again");
            Check(LP::PlanStep(1.0f / 60.0f, std::numeric_limits<float>::quiet_NaN()).reset,
                  "live physics: an unreadable move starts again");
            Check(!LP::PlanStep(0.0f, 0.0f).reset && LP::PlanStep(0.0f, 0.0f).dt == 0.0f, "live physics: no time, pose only");
        }

        // 4. The state's origin: a rebase moves nothing
        {
            Check(!LP::NeedsRebase(100.0f) && LP::NeedsRebase(3000.0f) && LP::NeedsRebase(std::numeric_limits<float>::infinity()),
                  "live physics: the origin follows only past the rebase distance");
            namespace MS = Phantasm::MannequinSpringCore;
            namespace MP = Phantasm::MannequinPhysicsCore;
            MS::SpringState st;
            MP::CbpcState   cs;
            st.tip    = { 2500.0f, 10.0f, 120.0f };
            cs.pos    = { 2500.0f, 0.0f, 100.0f };
            cs.posRot = { 2501.0f, 0.0f, 100.0f };
            st.vel    = { 3.0f, 0.0f, 0.0f };
            LP::Rebase(st, cs, { 2500.0f, 0.0f, 0.0f });
            Check(std::fabs(st.tip.x) < 1e-3f && st.tip.z == 120.0f && cs.pos.x == 0.0f && cs.posRot.x == 1.0f && st.vel.x == 3.0f,
                  "live physics: a rebase shifts positions and keeps speeds");
            st.primed = cs.primed = true;
            LP::ResetSpring(st, cs);
            Check(!st.primed && !cs.primed, "live physics: a reset puts both springs at rest");
        }

        // 5. CBPC: the game's own while its local moves, the view's once it has held still
        {
            LP::LocalWatch w;
            const RP::Xf   rest = At(0.0f, 5.0f, 0.0f);
            Check(LP::GameMovesByLocal(w, rest), "live physics: an unwatched bone is left to the game's CBPC");
            bool game = true;
            for (int i = 0; i < LP::kCbpcQuietTicks; ++i) game = LP::GameMovesByLocal(w, rest);
            Check(!game, "live physics: a local held still is taken over");
            Check(!LP::GameMovesByLocal(w, At(0.0f, 5.6f, 0.0f)) && !LP::GameMovesByLocal(w, At(0.0f, 5.6f, 0.0f)),
                  "live physics: one tick's move, then stillness, keeps the view's spring");
            bool back = false;
            for (int i = 0; i < LP::kCbpcMovingTicks; ++i) back = LP::GameMovesByLocal(w, At(0.0f, 6.0f + 0.5f * i, 0.0f));
            Check(back, "live physics: a local moving tick after tick is the game's again");
            Check(LP::GameMovesByLocal(w, At(0.0f, 7.5f, 0.0f)), "live physics: the game keeps it while it moves");
        }

        // 6. Carrying a spring between the players
        {
            Check(LP::CarriesState(true, true) && !LP::CarriesState(false, true) && !LP::CarriesState(true, false),
                  "live physics: only a moving spring of the same kind carries");
            Check(LP::StateFresh(0.016f) && !LP::StateFresh(1.0f) && !LP::StateFresh(-1.0f),
                  "live physics: only a state stepped within the reset gap carries");
        }

        // 7. The armour addon behind an attached node's name
        {
            const auto a = LP::ParseAttachedArmorNode(" (FE1B0A00)[0]/ (FE1B0A08) [31%]");
            Check(a.ok && a.addon == 0xFE1B0A00u && a.armor == 0xFE1B0A08u, "armor node: ids without names");
            const auto b = LP::ParseAttachedArmorNode("Cloak (Hooded) AA (0004a3f1)[2]/Cloak (Fur) (00012345) [100%]");
            Check(b.ok && b.addon == 0x0004A3F1u && b.armor == 0x00012345u, "armor node: names holding parentheses");
            const auto c = LP::ParseAttachedArmorNode("RoyalElvenGlovesAA (FE796807)[0]/");
            Check(c.ok && c.addon == 0xFE796807u && c.armor == 0, "armor node: the addon alone");
            Check(!LP::ParseAttachedArmorNode("NPC Spine2 [Spn2]").ok, "armor node: a bone is not one");
            Check(!LP::ParseAttachedArmorNode("Thing (FE1B0A0)[0]").ok, "armor node: seven digits are not an id");
            Check(!LP::ParseAttachedArmorNode("Thing (FE1B0A0G)[0]").ok, "armor node: a non-hex digit is not an id");
            Check(!LP::ParseAttachedArmorNode("(FE1B0A00)").ok, "armor node: an id with no slot is not one");
        }
    }
}
