// Phantasm - native self-tests: LastCaptureCore (which collects the actor view keeps as an NPC's last
// look, when it shows one, and how many it keeps).
//
// Author: Severause

#include "LastCaptureCore.h"
#include "TestHarness.h"

#include <cstdint>

namespace Phantasm::Tests
{
    void RunLastCaptureCoreTests()
    {
        namespace LC = Phantasm::LastCaptureCore;

        Check(LC::KeepsCollect(false, false, false), "last look: a complete collect is kept");
        Check(!LC::KeepsCollect(true, false, false), "last look: a collect still owed textures is not kept");
        Check(!LC::KeepsCollect(false, true, false), "last look: a collect that skipped pieces is not kept");
        Check(!LC::KeepsCollect(false, false, true), "last look: a try-on's unsaved clothes are not kept");

        Check(LC::KeepsAtLeave(true, true, false), "last look: a complete look is kept when the NPC leaves");
        Check(!LC::KeepsAtLeave(false, true, false),
              "last look: an incomplete look never replaces a kept one when the NPC leaves");
        Check(LC::KeepsAtLeave(false, false, false),
              "last look: an incomplete look is kept when the NPC leaves with none kept yet");
        Check(!LC::KeepsAtLeave(true, false, true) && !LC::KeepsAtLeave(false, false, true),
              "last look: a try-on is never kept when the NPC leaves");

        Check(LC::ServesKept(true, true, false), "last look: shown for a living NPC who cannot be captured");
        Check(!LC::ServesKept(false, true, false), "last look: nothing to show without one");
        Check(!LC::ServesKept(true, true, true) && !LC::ServesKept(true, false, false),
              "last look: never for a dead or missing NPC");

        const LC::LookKey classic{ 7, false, true }, captured{ 7, true, true }, bare{ 7, false, false };
        Check(!(classic == captured) && !(classic == bare) && classic == LC::LookKey{ 7, false, true },
              "look key: one NPC drawn in another pose or weapons choice is another look");
        const LC::LookKeyHash h;
        Check(h(classic) != h(captured) && h(classic) != h(bare) && h(classic) != h(LC::LookKey{ 8, false, true }),
              "look key: the hash tells the pose, the weapons and the NPC apart");
        LC::Recency<LC::LookKey> rk;
        rk.Touch(classic);
        rk.Touch(captured);
        rk.Erase(classic);
        Check(rk.Order().size() == 1 && rk.Order().front() == captured, "look key: one look of an NPC drops alone");

        LC::Recency<std::uint32_t> r;
        Check(r.Touch(1, 3).empty() && r.Touch(2, 3).empty() && r.Touch(3, 3).empty(), "recency: up to the capacity, nothing drops");
        const auto dropped = r.Touch(4, 3);
        Check(dropped.size() == 1 && dropped[0] == 1 && r.Order().front() == 4, "recency: past it, the oldest drops");
        Check(r.Touch(2, 3).empty() && r.Order().front() == 2 && r.Order().size() == 3, "recency: keeping one again moves it first");
        r.Erase(3);
        Check(r.Order().size() == 2, "recency: an erased key leaves");
        r.Clear();
        Check(r.Order().empty(), "recency: cleared");

        // Sizes
        Check(LC::TextureBytes(4, 4, 1, 1, 32, false) == 64, "budget: a 4x4 RGBA texture is 64 bytes");
        Check(LC::TextureBytes(1024, 1024, 1, 1, 8, true) == 1048576, "budget: a 1024 BC7 level is 1 MiB");
        Check(LC::TextureBytes(1024, 1024, 11, 1, 8, true) == 1398128, "budget: its mip chain adds about a third");
        Check(LC::TextureBytes(2, 2, 1, 1, 4, true) == 8, "budget: a block-compressed level is at least one block");
        Check(LC::TextureBytes(16, 16, 1, 6, 32, false) == 6144, "budget: a cube counts its six faces");

        // The budget
        using Res = LC::Resources<std::uint32_t>;
        const std::uint64_t mb = 1ull << 20;
        Check(LC::KeepWithin<std::uint32_t>({ Res{ { 1, 600 * mb } } }) == 1, "budget: the newest is kept whatever it costs");
        Check(LC::KeepWithin<std::uint32_t>({ Res{ { 1, 300 * mb } }, Res{ { 2, 300 * mb } } }) == 1,
              "budget: an older look past the budget drops");
        Check(LC::KeepWithin<std::uint32_t>({ Res{ { 1, 300 * mb } }, Res{ { 1, 300 * mb }, { 3, 100 * mb } } }) == 2,
              "budget: a shared texture counts once");
        Check(LC::KeepWithin<std::uint32_t>({ Res{ { 1, 100 * mb } }, Res{ { 2, 100 * mb } }, Res{ { 3, 400 * mb } },
                                              Res{ { 4, 10 * mb } } }) == 2,
              "budget: the looks kept are the newest ones, the cut is where the budget runs out");
        Check(LC::KeepWithin<std::uint32_t>({}) == 0, "budget: nothing kept, nothing to keep");
        Check(LC::TotalBytes<std::uint32_t>({ Res{ { 1, 5 }, { 2, 7 } }, Res{ { 2, 7 }, { 3, 1 } } }) == 13,
              "budget: the total counts each resource once");
    }
}
