#pragma once

// Phantasm - RestPoseCore: the classic pose's skeleton at rest (BuildRestPose in MannequinRenderer.cpp), engine-free
// (native/build.ps1 -Tests). The classic doll skins each skinned piece the way the engine does, palette_i =
// skinToBone_i x restWorld_i, with the skeleton at rest in place of its live pose: the race skeleton NIF's rotation
// and translation for every node animation drives, so a seated or sneaking NPC stands, and the live node's own scale,
// which animation never writes (the female hands' 0.8517, a race skeleton's proportions, RaceMenu and NiOverride).
//
// Author: Severause

#include "AsciiText.h"

#include <cctype>
#include <cmath>
#include <string_view>
#include <vector>

namespace Phantasm::RestPoseCore
{
    /// A node transform as NiTransform holds it: p' = s * (rot * p) + t, rot[row][col].
    struct Xf
    {
        float rot[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
        float t[3]      = { 0, 0, 0 };
        float s         = 1.0f;
    };

    struct P3
    {
        float x = 0, y = 0, z = 0;
    };

    /// The world of a node whose parent's world is `parent` and whose local is `local` (NiAVObject::UpdateWorldData).
    inline Xf Compose(const Xf& parent, const Xf& local) noexcept
    {
        Xf out;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                out.rot[r][c] = parent.rot[r][0] * local.rot[0][c] + parent.rot[r][1] * local.rot[1][c] +
                                parent.rot[r][2] * local.rot[2][c];
            }
            out.t[r] = parent.t[r] + parent.s * (parent.rot[r][0] * local.t[0] + parent.rot[r][1] * local.t[1] +
                                                 parent.rot[r][2] * local.t[2]);
        }
        out.s = parent.s * local.s;
        return out;
    }

    inline P3 Apply(const Xf& x, P3 p) noexcept
    {
        const float v[3] = { p.x, p.y, p.z };
        float       o[3];
        for (int r = 0; r < 3; ++r) o[r] = x.s * (x.rot[r][0] * v[0] + x.rot[r][1] * v[1] + x.rot[r][2] * v[2]) + x.t[r];
        return { o[0], o[1], o[2] };
    }

    /// Apply and Inverse state the palette rule in the tests (a skinToBone is the inverse of a bone's bind;
    /// palette_i = Compose(restWorld_i, skinToBone_i)); the renderer composes the palette as XMMATRIX.
    /// The inverse of a transform whose rotation is orthonormal and whose scale is not zero.
    inline Xf Inverse(const Xf& x) noexcept
    {
        Xf          out;
        const float inv = 1.0f / x.s;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) out.rot[r][c] = x.rot[c][r];
        }
        for (int r = 0; r < 3; ++r) {
            out.t[r] = -inv * (out.rot[r][0] * x.t[0] + out.rot[r][1] * x.t[1] + out.rot[r][2] * x.t[2]);
        }
        out.s = inv;
        return out;
    }

    inline bool IsFinite(const Xf& x) noexcept
    {
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(x.rot[r][c])) return false;
            }
            if (!std::isfinite(x.t[r])) return false;
        }
        return std::isfinite(x.s);
    }

    /// A node's local scale as the rest pose takes it: a mod that hides a node at scale 0 keeps it hidden; only a
    /// scale that is not finite, or negative, reads as 1.
    inline float UsableScale(float s) noexcept
    {
        return std::isfinite(s) && s >= 0.0f ? s : 1.0f;
    }

    namespace detail
    {
        inline bool StartsWithNoCase(std::string_view s, std::string_view prefix) noexcept
        {
            return AsciiText::StartsWithI(s, prefix);
        }
    }

    /// Whether a node is a skeleton helper animation never drives: XPMSE's and the race skeletons' "CME " nodes
    /// (which carry a race's proportions and the RaceMenu body sliders; FKDRS adds ":N"-suffixed copies), "NPC"
    /// (RaceMenu's height), and an unnamed node (animation binds bones by name; a race skeleton keeps one above the
    /// pelvis). Its whole live local is its rest local. Any case.
    inline bool IsUndrivenHelperNode(std::string_view name) noexcept
    {
        return name.empty() || detail::StartsWithNoCase(name, "CME ") ||
               (name.size() == 3 && detail::StartsWithNoCase(name, "NPC"));
    }

    /// Whether a bone name is the head bone, any case: humanoid skeletons name it "NPC Head [Head]", creature
    /// skeletons (falmer, bear, riekling, dragon) "NPC Head".
    inline bool IsHeadBoneKey(std::string_view name) noexcept
    {
        const auto is = [&](std::string_view head) {
            return name.size() == head.size() && detail::StartsWithNoCase(name, head);
        };
        return is("npc head [head]") || is("npc head");
    }

    enum class LocalSource
    {
        kLive,                 // the node's whole live local (a helper animation never drives)
        kRestWithLiveScale,    // the skeleton NIF's rotation and translation, the live node's scale
        kNone,                 // no rest local: not in the skeleton NIF (a bone an armour or SMP adds)
    };

    /// Where a node's rest local comes from. A node in the skeleton NIF that is not a helper may be driven by the
    /// behaviour graph, so its live rotation and translation are the pose of the moment (a seated NPC's COM is lower):
    /// only its scale is taken live. A node outside the NIF has no rest local of its own; the caller places a skin
    /// bone of that kind from its bind pose instead.
    inline LocalSource ChooseLocalSource(bool inSkeletonNif, bool undrivenHelper) noexcept
    {
        if (undrivenHelper) return LocalSource::kLive;
        return inSkeletonNif ? LocalSource::kRestWithLiveScale : LocalSource::kNone;
    }

    /// The rest local from the node's skeleton NIF local and its live local (kNone gives the identity).
    inline Xf RestLocal(const Xf& nifLocal, const Xf& liveLocal, LocalSource source) noexcept
    {
        Xf out;
        switch (source) {
        case LocalSource::kLive:
            out   = liveLocal;
            out.s = UsableScale(liveLocal.s);
            break;
        case LocalSource::kRestWithLiveScale:
            out   = nifLocal;
            out.s = UsableScale(liveLocal.s);
            break;
        case LocalSource::kNone:
            break;
        }
        return out;
    }

    /// An accumulated scale past this reads as a broken node, not a skeleton.
    inline constexpr float kMaxRestScale = 50.0f;

    /// The race skeleton NIF's node tree: each node's local and its parent's index (-1: a child of the NIF root).
    struct NifSkeleton
    {
        std::vector<Xf>  local;
        std::vector<int> parent;
    };

    /// The rest local of a flattened-tree bone relative to its parent entry, by their NIF nodes (`node`, `ancestor`;
    /// -1 = none): the NIF locals of the nodes strictly between them, composed over the bone's own RestLocal. A race
    /// skeleton keeps nodes there the engine does not list as bones of their own (FKDRS's ":N" helpers, an unnamed
    /// node), and they carry its proportions. With no NIF ancestor to meet (none given, or not an ancestor), the
    /// bone's own RestLocal; a helper's (kLive) is its own live local, which the engine composed itself.
    inline Xf TreeEntryRestLocal(const NifSkeleton& nif, int node, int ancestor, const Xf& live, LocalSource source)
    {
        const int n = static_cast<int>(nif.local.size());
        const Xf  own = RestLocal(node >= 0 && node < n ? nif.local[node] : Xf{}, live, source);
        if (source != LocalSource::kRestWithLiveScale || node < 0 || node >= n || ancestor < 0) return own;
        Xf between;
        int guard = 0;
        for (int i = nif.parent[node]; i >= 0 && i < n && guard++ < n; i = nif.parent[i]) {
            if (i == ancestor) return Compose(between, own);
            between = Compose(nif.local[i], between);
        }
        return own;
    }

    /// One bone of a flattened bone tree (BSFlattenedBoneTree::BoneEntry): its parent entry (-1: the tree node),
    /// its live local, its NIF node (-1: none), whether its name is a helper's, and whether the entry is the tree
    /// node itself.
    struct TreeEntry
    {
        int  parent     = -1;
        Xf   live;
        int  nif        = -1;
        bool helper     = false;
        bool isTreeNode = false;
    };

    /// Places a flattened tree's bones at rest by the entries' own parent links (the engine's, not the scene
    /// graph's), in any order: worlds[i] is entry i's rest world where placed[i] is 1, 0 where it is not (a bone
    /// with no rest local, under one, under a parent index out of range or in a cycle, or a world that is not
    /// finite or is scaled past kMaxRestScale). `treeWorld` is the tree node's rest world and `treeNif` its NIF node.
    inline void PlaceTreeEntries(const NifSkeleton& nif, const std::vector<TreeEntry>& entries, const Xf& treeWorld,
                                 int treeNif, std::vector<Xf>& worlds, std::vector<signed char>& placed)
    {
        const int n = static_cast<int>(entries.size());
        worlds.assign(entries.size(), Xf{});
        placed.assign(entries.size(), -1);   // -1 not yet, 2 on the stack (a cycle), then 0 / 1
        std::vector<int> chain;
        for (int start = 0; start < n; ++start) {
            // Up to the first resolved ancestor, then back down: no recursion, a cycle ends unplaced.
            chain.clear();
            int i = start;
            while (i >= 0 && i < n && placed[i] == -1) {
                placed[i] = 2;
                chain.push_back(i);
                i = entries[i].isTreeNode ? -1 : entries[i].parent;
            }
            // The top of the chain hangs under a resolved entry, the tree, an index out of range or the chain itself
            // (still 2 or already 0 when reached): only the first two can place it.
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                const int        k = *it;
                const TreeEntry& e = entries[k];
                placed[k]          = 0;
                if (e.isTreeNode) {
                    worlds[k] = treeWorld;
                    placed[k] = 1;
                    continue;
                }
                const int p = e.parent;
                if (p >= n || (p >= 0 && placed[p] != 1)) continue;
                const Xf& parentWorld = p < 0 ? treeWorld : worlds[p];
                const int parentNif   = p < 0 ? treeNif : (entries[p].isTreeNode ? treeNif : entries[p].nif);
                const LocalSource source = ChooseLocalSource(e.nif >= 0, e.helper);
                if (source == LocalSource::kNone) continue;
                const Xf world = Compose(parentWorld, TreeEntryRestLocal(nif, e.nif, parentNif, e.live, source));
                if (!IsFinite(world) || !(world.s <= kMaxRestScale)) continue;
                worlds[k] = world;
                placed[k] = 1;
            }
        }
    }

    /// A rest world farther than this from the bind pose the mesh was authored on is not trusted: kRestBindSlack
    /// units plus kRestBindPerUnit for each unit the bind sits above the actor root, so the scale of a tall race or a
    /// RaceMenu height (1.5 moves the head 60u) passes and a skeleton NIF that is not the live skeleton does not.
    inline constexpr float kRestBindSlack   = 40.0f;
    inline constexpr float kRestBindPerUnit = 0.5f;

    inline bool RestAgreesWithBind(float restToBindDistance, float bindHeightAboveRoot) noexcept
    {
        const float h = std::fabs(bindHeightAboveRoot);
        if (!std::isfinite(h)) return false;
        return restToBindDistance < kRestBindSlack + kRestBindPerUnit * h;   // false for NaN
    }
}
