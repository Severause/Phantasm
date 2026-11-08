#pragma once

// Phantasm - FaceMorphCore: the actor view's own face (MannequinRenderer.cpp). It reads a head part's
// expression .tri (FRTRI003), takes the game's current expression off a face and puts a chosen one on, and blinks
// on its own rhythm while the game does not animate the face. Engine-free (native/build.ps1 -Tests).
//
// Author: Severause

#include "AsciiText.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Phantasm::FaceMorphCore
{
    /// A differential morph: per vertex, x y z deltas scaled by `scale` (game units).
    struct Morph
    {
        std::string               name;
        float                     scale = 0.0f;
        std::vector<std::int16_t> deltas;   // 3 per vertex
    };

    struct Tri
    {
        std::uint32_t      vertexCount = 0;
        std::vector<Morph> morphs;

        /// The morph of that name, any case; null when the file has none.
        const Morph* Find(std::string_view name) const noexcept
        {
            for (const auto& m : morphs) {
                if (AsciiText::EqualsI(m.name, name)) return &m;
            }
            return nullptr;
        }
    };

    /// Reads an FRTRI003 file: a header of 14 counts after the magic (vertices, triangles, quads, two unknowns,
    /// UVs, flags, morphs, modifiers, modifier vertices, four unknowns), the base vertices and modifier vertices,
    /// the triangles and quads, with UVs the UVs and their triangles, then each morph (a length-prefixed name, a
    /// scale and an int16 x y z per vertex). Modifier morphs (absolute, for a few vertices) are skipped. False, with
    /// `why`, when the file is short, not a .tri, or a count runs past its end.
    inline bool ParseTri(const std::uint8_t* data, std::size_t size, Tri& out, std::string* why = nullptr)
    {
        out = Tri{};
        const auto fail = [&](const char* w) {
            if (why) *why = w;
            out = Tri{};
            return false;
        };
        std::size_t off = 0;
        const auto  have = [&](std::size_t n) { return n <= size && off <= size - n; };
        const auto  u32  = [&](std::uint32_t& v) {
            if (!have(4)) return false;
            std::memcpy(&v, data + off, 4);
            off += 4;
            return true;
        };
        const auto skip = [&](std::uint64_t n) {
            if (n > size || !have(static_cast<std::size_t>(n))) return false;
            off += static_cast<std::size_t>(n);
            return true;
        };
        if (!data || size < 8 + 14 * 4 || std::memcmp(data, "FRTRI003", 8) != 0) return fail("not an FRTRI003 file");
        off = 8;
        std::uint32_t h[14]{};
        for (auto& v : h) u32(v);
        const std::uint32_t V = h[0], F = h[1], Q = h[2], UV = h[5], M = h[7], Mod = h[8], ModV = h[9];
        if (V == 0 || V > 200000 || M > 4096) return fail("implausible counts");
        if (!skip(std::uint64_t(V) * 12) || !skip(std::uint64_t(ModV) * 12) || !skip(std::uint64_t(F) * 12) ||
            !skip(std::uint64_t(Q) * 16)) {
            return fail("geometry runs past the end");
        }
        if (UV && (!skip(std::uint64_t(UV) * 8) || !skip(std::uint64_t(F) * 12))) return fail("UVs run past the end");
        out.vertexCount = V;
        out.morphs.reserve(M);
        for (std::uint32_t i = 0; i < M; ++i) {
            std::uint32_t n = 0;
            if (!u32(n) || n > 1024 || !have(n)) return fail("a morph name runs past the end");
            Morph m;
            m.name.assign(reinterpret_cast<const char*>(data + off), n);
            m.name.erase(std::find(m.name.begin(), m.name.end(), '\0'), m.name.end());
            off += n;
            if (!have(4)) return fail("a morph scale runs past the end");
            std::memcpy(&m.scale, data + off, 4);
            off += 4;
            const std::uint64_t bytes = std::uint64_t(V) * 6;
            if (bytes > size || !have(static_cast<std::size_t>(bytes))) return fail("a morph runs past the end");
            m.deltas.resize(std::size_t(V) * 3);
            std::memcpy(m.deltas.data(), data + off, static_cast<std::size_t>(bytes));
            off += static_cast<std::size_t>(bytes);
            if (!std::isfinite(m.scale)) m.scale = 0.0f;
            out.morphs.push_back(std::move(m));
        }
        (void)Mod;   // modifier morphs (absolute positions for a few vertices) are not used
        return true;
    }

    struct P4
    {
        float x = 0, y = 0, z = 0, w = 0;
    };

    /// out = base + the sum of each morph's deltas at its weight (a negative weight takes a morph off). A morph
    /// with a vertex count other than base's is skipped; out keeps base's w.
    inline void Apply(const std::vector<P4>& base, const std::vector<std::pair<const Morph*, float>>& weights,
                      std::vector<P4>& out)
    {
        out = base;
        for (const auto& [m, w] : weights) {
            if (!m || w == 0.0f || !(w == w) || m->deltas.size() != base.size() * 3) continue;
            const float s = m->scale * w;
            for (std::size_t i = 0; i < base.size(); ++i) {
                out[i].x += s * m->deltas[i * 3 + 0];
                out[i].y += s * m->deltas[i * 3 + 1];
                out[i].z += s * m->deltas[i * 3 + 2];
            }
        }
    }

    // ── The game's own weights ──

    /// The names of the engine's expression, modifier and phoneme slots, in BSFaceGenKeyframeMultiple's order
    /// (the morph names in a head part's .tri).
    inline constexpr std::array<std::string_view, 17> kExpressionNames{
        "DialogueAnger", "DialogueFear", "DialogueHappy", "DialogueSad", "DialogueSurprise", "DialoguePuzzled",
        "DialogueDisgusted", "MoodNeutral", "MoodAnger", "MoodFear", "MoodHappy", "MoodSad", "MoodSurprise",
        "MoodPuzzled", "MoodDisgusted", "CombatAnger", "CombatShout"
    };
    inline constexpr std::array<std::string_view, 14> kModifierNames{
        "BlinkLeft", "BlinkRight", "BrowDownLeft", "BrowDownRight", "BrowInLeft", "BrowInRight", "BrowUpLeft",
        "BrowUpRight", "LookDown", "LookLeft", "LookRight", "LookUp", "SquintLeft", "SquintRight"
    };
    inline constexpr std::array<std::string_view, 16> kPhonemeNames{
        "Aah", "BigAah", "BMP", "ChJSh", "DST", "Eee", "Eh", "FV", "I", "K", "N", "Oh", "OohQ", "R", "Th", "W"
    };

    /// The game's face weights at one moment (each 0-1, by slot; a short list is padded with zeros).
    struct GameWeights
    {
        std::vector<float> expressions, modifiers, phonemes;
    };

    /// The weights that take the game's face off a face: every slot negated (`all`), or only the two blinks (to
    /// keep the game's expression and blink on the view's own rhythm instead).
    inline std::vector<std::pair<const Morph*, float>> TakeOff(const Tri& tri, const GameWeights& g, bool all)
    {
        std::vector<std::pair<const Morph*, float>> out;
        const auto add = [&](const auto& names, const std::vector<float>& values, std::size_t limit) {
            for (std::size_t i = 0; i < names.size() && i < values.size() && i < limit; ++i) {
                const float v = values[i];
                if (!(v > 0.0f)) continue;
                if (const Morph* m = tri.Find(names[i])) out.emplace_back(m, -std::min(v, 1.0f));
            }
        };
        if (all) {
            add(kExpressionNames, g.expressions, kExpressionNames.size());
            add(kModifierNames, g.modifiers, kModifierNames.size());
            add(kPhonemeNames, g.phonemes, kPhonemeNames.size());
        } else {
            add(kModifierNames, g.modifiers, 2);   // BlinkLeft, BlinkRight
        }
        return out;
    }

    // ── The chosen expression ──

    enum class Expression : int
    {
        kAsInGame = 0,
        kNeutral,
        kHappy,
        kSad,
        kAngry,
        kAfraid,
        kSurprised,
        kPuzzled,
        kDisgusted,
        kCount
    };

    /// The expression hint's value for each expression ("" is as in game), indexed by Expression.
    inline constexpr std::array<std::string_view, static_cast<std::size_t>(Expression::kCount)> kExpressionIds{
        "", "neutral", "happy", "sad", "angry", "afraid", "surprised", "puzzled", "disgusted"
    };

    inline std::string_view ExpressionId(Expression e) noexcept
    {
        const auto i = static_cast<std::size_t>(e);
        return i < kExpressionIds.size() ? kExpressionIds[i] : kExpressionIds[0];
    }

    /// The expression an id names, any case; anything else is as in game.
    inline Expression ParseExpression(std::string_view text) noexcept
    {
        for (std::size_t i = 1; i < kExpressionIds.size(); ++i) {
            if (AsciiText::EqualsI(text, kExpressionIds[i])) return static_cast<Expression>(i);
        }
        return Expression::kAsInGame;
    }

    /// The .tri morph that shows an expression (the game's mood morphs), or "" for as in game and neutral.
    inline std::string_view MoodMorph(Expression e) noexcept
    {
        switch (e) {
        case Expression::kHappy:     return "MoodHappy";
        case Expression::kSad:       return "MoodSad";
        case Expression::kAngry:     return "MoodAnger";
        case Expression::kAfraid:    return "MoodFear";
        case Expression::kSurprised: return "MoodSurprise";
        case Expression::kPuzzled:   return "MoodPuzzled";
        case Expression::kDisgusted: return "MoodDisgusted";
        default:                     return "";
        }
    }

    /// How strongly a chosen mood shows: a clear expression without the caricature of a full one.
    inline constexpr float kMoodWeight = 0.85f;

    // ── Blinking ──

    /// The view's blink: a close in 70 ms, a 50 ms hold and an opening in 130 ms, every 2.5-6 s, now and then
    /// twice in a row, on a rhythm of its own per NPC (seeded) so two faces never blink together.
    class Blinker
    {
    public:
        static constexpr double kClose = 0.07, kHold = 0.05, kOpen = 0.13;

        Blinker(std::uint32_t seed = 1) noexcept : m_state(seed ? seed : 1) {}

        /// The blink's weight (0 open, 1 shut) at `now` seconds; the first call starts the rhythm.
        float Weight(double now) noexcept
        {
            if (!m_started) {
                m_started = true;
                m_at      = now + 0.6 + Uniform() * 2.0;
            }
            if (now < m_at) return 0.0f;
            const double t = now - m_at;
            if (t >= kClose + kHold + kOpen) {
                m_at = now + (m_double ? 0.18 : 2.5 + Uniform() * 3.5);
                m_double = !m_double && Uniform() < 0.15;
                return 0.0f;
            }
            if (t < kClose) return Ease(static_cast<float>(t / kClose));
            if (t < kClose + kHold) return 1.0f;
            return 1.0f - Ease(static_cast<float>((t - kClose - kHold) / kOpen));
        }

    private:
        static float Ease(float t) noexcept
        {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
        double Uniform() noexcept   // 0-1, xorshift32
        {
            m_state ^= m_state << 13;
            m_state ^= m_state >> 17;
            m_state ^= m_state << 5;
            return (m_state & 0xFFFFFF) / double(0x1000000);
        }

        std::uint32_t m_state;
        bool          m_started = false;
        bool          m_double  = false;
        double        m_at      = 0.0;
    };
}
