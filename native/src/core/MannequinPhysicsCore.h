#pragma once

// Phantasm - MannequinPhysicsCore: the actor view's clip player reads the installed physics mods' setup, so
// the bones it moves and how they move follow what the game shows (MannequinRenderer.cpp), engine-free
// (native/build.ps1 -Tests).
//  - CBPC: its master configs (which bone is which alias, under which conditions) and bounce configs (the alias's
//    settings, at body weight 0 and 100), and the jiggle step. The step follows CBPCSSE's Thing::update (MIT,
//    Licenses/CBPCSSE-MIT.txt): a point that lags the bone's parent on a linear + quadratic spring, clamped per
//    parent axis, moves the bone by the lag and turns it by a second, rotational lag; a breast bone's gravity follows
//    the spine and its swing the body armour's class. Collisions and the armour push-ups are left out.
//  - HDT-SMP / FSMP: a physics XML's bones (a mass above 0 is simulated, 0 follows the animation) and how far its
//    constraints let each one swing, which become a MannequinSpringCore tip spring.
//
// Author: Severause

#include "AsciiText.h"
#include "MannequinSpringCore.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Phantasm::MannequinPhysicsCore
{
    using MannequinSpringCore::Vec3;

    inline std::string Lower(std::string_view s) { return AsciiText::Lowered(s); }

    inline std::string_view Trim(std::string_view s) { return AsciiText::Trim(s); }

    inline bool ParseFloat(std::string_view s, float& out)
    {
        const std::string t(Trim(s));
        if (t.empty()) return false;
        char*        end = nullptr;
        const double v   = std::strtod(t.c_str(), &end);
        if (end == t.c_str() || !std::isfinite(v)) return false;
        out = static_cast<float>(v);
        return true;
    }

    // Splits text into lines, each with any '#' comment cut off and trimmed.
    inline std::vector<std::string_view> ConfigLines(std::string_view text)
    {
        std::vector<std::string_view> out;
        while (!text.empty()) {
            const auto       nl   = text.find('\n');
            std::string_view line = text.substr(0, nl);
            text                  = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
            if (const auto hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
            line = Trim(line);
            if (!line.empty()) out.push_back(line);
        }
        return out;
    }

    // ── CBPC conditions ─────────────────────────────────────────────────────

    /// One function of a condition, `NOT IsFemale()` or `IsRaceFormId(Skyrim.esm|013746)`: fn and arg as written.
    struct CbpcAtom
    {
        std::string fn, arg;
        bool        negate = false;
    };

    /// A condition as CBPC reads it: AND first, then OR inside each part. Empty = always true.
    struct CbpcCondition
    {
        std::vector<std::vector<CbpcAtom>> allOf;   // every entry must hold; an entry holds when any of its atoms does
    };

    inline std::vector<std::string_view> SplitWord(std::string_view s, std::string_view word)
    {
        std::vector<std::string_view> out;
        std::size_t                   start = 0;
        for (std::size_t i = 0; i + word.size() <= s.size();) {
            if (s.compare(i, word.size(), word) == 0) {
                out.push_back(s.substr(start, i - start));
                i += word.size();
                start = i;
            } else {
                ++i;
            }
        }
        out.push_back(s.substr(start));
        return out;
    }

    inline CbpcCondition ParseCbpcCondition(std::string_view text)
    {
        CbpcCondition c;
        text = Trim(text);
        if (text.empty()) return c;
        for (auto andPart : SplitWord(text, "AND")) {
            std::vector<CbpcAtom> any;
            for (auto orPart : SplitWord(andPart, "OR")) {
                std::string_view p = Trim(orPart);
                if (p.empty()) continue;
                CbpcAtom a;
                if (p.size() > 4 && Lower(p.substr(0, 4)) == "not ") {
                    a.negate = true;
                    p        = Trim(p.substr(4));
                }
                const auto open  = p.find('(');
                const auto close = p.rfind(')');
                a.fn  = Lower(Trim(p.substr(0, open)));
                if (open != std::string_view::npos && close != std::string_view::npos && close > open) {
                    a.arg = std::string(Trim(p.substr(open + 1, close - open - 1)));
                }
                any.push_back(std::move(a));
            }
            if (!any.empty()) c.allOf.push_back(std::move(any));
        }
        return c;
    }

    /// The condition against an actor: atom answers one function (fn lower-cased; false for one it does not know).
    inline bool EvalCbpcCondition(const CbpcCondition& c, const std::function<bool(const CbpcAtom&)>& atom)
    {
        for (const auto& any : c.allOf) {
            bool held = false;
            for (const auto& a : any) {
                if (atom(a) != a.negate) {
                    held = true;
                    break;
                }
            }
            if (!held) return false;
        }
        return true;
    }

    // ── CBPC config files ───────────────────────────────────────────────────

    enum class CbpcConfigFile : std::uint8_t { kNone, kMaster, kBounce };

    /// Which CBPC config a file in Data/SKSE/Plugins is, by its name (any case), as CBPC finds them:
    /// CBPCMasterConfig*.txt / .ini, CBPConfig*.txt / .ini.
    inline CbpcConfigFile CbpcConfigFileKind(std::wstring_view fileName)
    {
        std::wstring n(fileName);
        for (auto& c : n) c = static_cast<wchar_t>(std::towlower(c));
        const auto ends = [&](std::wstring_view s) { return n.size() >= s.size() && n.compare(n.size() - s.size(), s.size(), s) == 0; };
        if (!ends(L".txt") && !ends(L".ini")) return CbpcConfigFile::kNone;
        if (n.rfind(L"cbpcmasterconfig", 0) == 0) return CbpcConfigFile::kMaster;
        if (n.rfind(L"cbpconfig", 0) == 0) return CbpcConfigFile::kBounce;
        return CbpcConfigFile::kNone;
    }

    // ── CBPC master config: bone -> alias ───────────────────────────────────

    struct CbpcBoneMap
    {
        std::string   bone, alias;
        CbpcCondition condition;
    };

    struct CbpcMaster
    {
        std::vector<CbpcBoneMap> bones;   // a later file's line for the same bone replaces the earlier one
        bool                     malePhysics = false;
    };

    /// Adds one CBPCMasterConfig*.txt to m (files are read in name order).
    inline void ParseCbpcMaster(std::string_view text, CbpcMaster& m)
    {
        bool inMap = false;
        for (auto line : ConfigLines(text)) {
            if (line.front() == '[') {
                inMap = Lower(line).rfind("[configmap]", 0) == 0;
                continue;
            }
            if (line == "<" || line == ">") continue;
            const auto eq = line.find('=');
            if (eq == std::string_view::npos) continue;
            const std::string_view key = Trim(line.substr(0, eq));
            const std::string_view rest = line.substr(eq + 1);
            if (!inMap) {
                if (Lower(key) == "malephysics") {
                    float v = 0.0f;
                    if (ParseFloat(rest, v)) m.malePhysics = v != 0.0f;
                }
                continue;
            }
            const auto       eq2   = rest.find('=');
            CbpcBoneMap      e;
            e.bone  = std::string(key);
            e.alias = std::string(Trim(rest.substr(0, eq2)));
            if (eq2 != std::string_view::npos) e.condition = ParseCbpcCondition(rest.substr(eq2 + 1));
            if (e.bone.empty() || e.alias.empty()) continue;
            const auto lb = Lower(e.bone);
            const auto it = std::find_if(m.bones.begin(), m.bones.end(), [&](const CbpcBoneMap& b) { return Lower(b.bone) == lb; });
            if (it != m.bones.end()) *it = std::move(e);
            else m.bones.push_back(std::move(e));
        }
    }

    /// The bones CBPC leaves alone on a male body unless MalePhysics is on.
    inline bool IsFemaleBodyBone(std::string_view bone)
    {
        const auto n = Lower(bone);
        for (std::string_view s : { "breast", "butt", "belly", "thigh", "calf" }) {
            if (n.find(s) != std::string::npos) return true;
        }
        return false;
    }

    // ── CBPC bounce config: alias -> settings ───────────────────────────────

    /// One alias's settings as written (keys lower-cased): the value at body weight 100 and at weight 0.
    struct CbpcEntry
    {
        std::unordered_map<std::string, float> at100, at0;
    };

    struct CbpcBounceFile
    {
        bool                                        conditioned = false;
        CbpcCondition                               condition;
        int                                         priority = 0;
        std::unordered_map<std::string, CbpcEntry> aliases;   // by lower-cased alias
    };

    /// Reads one CBPConfig*.txt: `Alias.key v100 [v0]` lines, plus `Conditions =` / `Priority =`.
    inline CbpcBounceFile ParseCbpcBounce(std::string_view text)
    {
        CbpcBounceFile f;
        for (auto line : ConfigLines(text)) {
            const auto eq = line.find('=');
            if (eq != std::string_view::npos) {
                const auto key = Lower(Trim(line.substr(0, eq)));
                if (key == "conditions") {
                    f.conditioned = true;
                    f.condition   = ParseCbpcCondition(line.substr(eq + 1));
                } else if (key == "priority") {
                    float v = 0.0f;
                    if (ParseFloat(line.substr(eq + 1), v)) f.priority = static_cast<int>(v);
                }
                continue;
            }
            const auto dot = line.find('.');
            if (dot == std::string_view::npos || dot == 0) continue;
            const auto alias = Lower(Trim(line.substr(0, dot)));
            std::string_view rest = line.substr(dot + 1);
            std::vector<std::string_view> tok;
            while (!rest.empty()) {
                rest = Trim(rest);
                const auto sp = rest.find_first_of(" \t");
                tok.push_back(rest.substr(0, sp));
                if (sp == std::string_view::npos) break;
                rest = rest.substr(sp);
            }
            float v100 = 0.0f;
            if (tok.size() < 2 || !ParseFloat(tok[1], v100)) continue;
            float v0 = v100;
            if (tok.size() > 2) ParseFloat(tok[2], v0);
            auto& e = f.aliases[alias];
            const auto k = Lower(tok[0]);
            e.at100[k] = v100;
            e.at0[k]   = v0;
        }
        return f;
    }

    /// Adds an unconditioned bounce file to the merged one, key by key (files are read in name order).
    inline void MergeCbpcBounce(CbpcBounceFile& into, const CbpcBounceFile& f)
    {
        for (const auto& [alias, entry] : f.aliases) {
            auto& to = into.aliases[alias];
            for (const auto& [k, v] : entry.at100) to.at100[k] = v;
            for (const auto& [k, v] : entry.at0) to.at0[k] = v;
        }
    }

    /// The conditioned bounce file an actor takes, as CBPC orders them: the highest priority, then the most AND
    /// parts, the first whose condition holds; null for none.
    template <class Holds>
    inline const CbpcBounceFile* ChooseCbpcBounce(const std::vector<CbpcBounceFile>& files, Holds&& holds)
    {
        const CbpcBounceFile* chosen = nullptr;
        for (const auto& f : files) {
            const bool better = !chosen || f.priority > chosen->priority ||
                                (f.priority == chosen->priority && f.condition.allOf.size() > chosen->condition.allOf.size());
            if (better && holds(f.condition)) chosen = &f;
        }
        return chosen;
    }

    /// Whether a conditioned file's entry counts for its alias, CBPC's IsConfigActuallyAllocated: a stiffness or
    /// damping key at 0.001 or more, a collision bound moved off its +-100, or an amplitude of 0.001 or more, which
    /// CBPC seeds at 1 for every alias of a conditioned file. So only an entry that sets its amplitude to 0 and
    /// nothing else lets the alias fall back to the unconditioned files.
    inline bool CbpcEntryAllocated(const CbpcEntry& e)
    {
        for (const auto* m : { &e.at100, &e.at0 }) {
            for (const auto& [k, v] : *m) {
                if ((k.rfind("stiffness", 0) == 0 || k.rfind("damping", 0) == 0) && v >= 0.001f) return true;
                if (k.rfind("collision", 0) == 0 && k.find("offset") != std::string::npos) {
                    const float def = k.find("max") != std::string::npos ? 100.0f : -100.0f;
                    if (std::fabs(v - def) >= 0.001f) return true;
                }
            }
            const auto amp = m->find("amplitude");
            if (amp == m->end() || amp->second >= 0.001f) return true;
        }
        return false;
    }

    /// An alias's settings, as CBPC picks them: with a conditioned file chosen, that file's entry, an alias it does
    /// not name taking CBPC's defaults there (which move nothing: null), and the merged unconditioned ones only for
    /// an entry CbpcEntryAllocated refuses; with none chosen, the merged ones. Null when nothing moves it.
    inline const CbpcEntry* CbpcEntryFor(std::string_view aliasLower, const CbpcBounceFile* chosen, const CbpcBounceFile& base)
    {
        const std::string key(aliasLower);
        if (chosen) {
            const auto it = chosen->aliases.find(key);
            if (it == chosen->aliases.end()) return nullptr;
            if (CbpcEntryAllocated(it->second)) return &it->second;
        }
        const auto it = base.aliases.find(key);
        return it == base.aliases.end() ? nullptr : &it->second;
    }

    /// The settings one alias resolves to at a body weight, with CBPC's defaults and fallbacks.
    struct CbpcParams
    {
        Vec3  stiffness, stiffness2, damping;            // per parent axis
        Vec3  stiffnessRot, stiffness2Rot, dampingRot;   // per rotation axis X, Y, Z
        Vec3  maxOff, minOff;                            // the lag's bounds per parent axis (game units)
        Vec3  maxOffRot, minOffRot;                      // the rotational lag's bounds per rotation axis
        float timeTick = 4.0f, timeTickRot = 4.0f;       // physics step (ms)
        float timeStep = 1.0f, timeStepRot = 1.0f;
        Vec3  linear, rotational;                        // how much of the lag moves and turns the bone
        float linRot[3][3]{};                            // [linear axis][rotation axis]
        float spreadLin[3][3]{};                         // the lag a clamp cuts on axis [from], given to [to]
        float spreadRot[3][3]{};                         // the same per rotation axis
        float gravityBias = 0.0f, gravityCorrection = 0.0f;
        float gravityInverted = 0.0f, gravityInvStart = 0.0f, gravityInvEnd = 0.0f;
        float forceMultiplier = 1.0f, amplitude = 1.0f;
        float clothedAmplitude = 1.0f, lightAmplitude = 1.0f, heavyAmplitude = 1.0f;   // a breast bone's, by armour
        float armorAmplitude = 1.0f;   // the one of the three the actor's body armour picks (CbpcArmorAmplitude), set by the caller
    };

    namespace detail
    {
        inline float Get(const std::unordered_map<std::string, float>& m, const char* k, float def = 0.0f)
        {
            const auto it = m.find(k);
            return it == m.end() ? def : it->second;
        }

        inline CbpcParams ResolveOne(const std::unordered_map<std::string, float>& m)
        {
            CbpcParams p;
            const auto g = [&](const char* k, float def = 0.0f) { return Get(m, k, def); };
            const auto axes = [&](const char* x, const char* y, const char* z) { return Vec3{ g(x), g(y), g(z) }; };
            const auto none = [](Vec3 v) { return v.x < 0.001f && v.y < 0.001f && v.z < 0.001f; };

            const float s = g("stiffness"), s2 = g("stiffness2"), d = g("damping");
            p.stiffness     = axes("stiffnessx", "stiffnessy", "stiffnessz");
            p.stiffnessRot  = axes("stiffnessxrot", "stiffnessyrot", "stiffnesszrot");
            p.stiffness2    = axes("stiffness2x", "stiffness2y", "stiffness2z");
            p.stiffness2Rot = axes("stiffness2xrot", "stiffness2yrot", "stiffness2zrot");
            p.damping       = axes("dampingx", "dampingy", "dampingz");
            p.dampingRot    = axes("dampingxrot", "dampingyrot", "dampingzrot");
            if (s >= 0.001f && none(p.stiffness)) p.stiffness = { s, s, s };
            if (s >= 0.001f && none(p.stiffnessRot)) p.stiffnessRot = { s, s, s };
            if (s2 >= 0.001f && none(p.stiffness2)) p.stiffness2 = { s2, s2, s2 };
            if (s2 >= 0.001f && none(p.stiffness2Rot)) p.stiffness2Rot = { s2, s2, s2 };
            if (d >= 0.001f && none(p.damping)) p.damping = { d, d, d };
            if (none(p.dampingRot)) p.dampingRot = p.damping;

            const float mo = g("maxoffset");
            if (mo >= 0.01f) {
                p.maxOff = p.maxOffRot = { mo, mo, mo };
                p.minOff = p.minOffRot = { -mo, -mo, -mo };
            } else {
                // CBPC seeds the linear bounds at +-5 before reading a file.
                p.maxOff    = { g("xmaxoffset", 5.0f), g("ymaxoffset", 5.0f), g("zmaxoffset", 5.0f) };
                p.minOff    = { g("xminoffset", -5.0f), g("yminoffset", -5.0f), g("zminoffset", -5.0f) };
                p.maxOffRot = axes("xmaxoffsetrot", "ymaxoffsetrot", "zmaxoffsetrot");
                p.minOffRot = axes("xminoffsetrot", "yminoffsetrot", "zminoffsetrot");
                if (none(p.maxOffRot) && none(p.minOffRot)) {
                    // Rotation X/Y/Z follows linear Z/X/Y (linear X turns about Y, Y about Z, Z about X).
                    p.maxOffRot = { p.maxOff.z, p.maxOff.x, p.maxOff.y };
                    p.minOffRot = { p.minOff.z, p.minOff.x, p.minOff.y };
                }
            }
            p.timeTick    = (std::max)(1.0f, g("timetick", 4.0f));
            p.timeTickRot = g("timetickrot");
            if (p.timeTickRot < 1.0f) p.timeTickRot = p.timeTick;
            p.timeStep    = g("timestep", 1.0f);
            p.timeStepRot = g("timesteprot");
            if (p.timeStepRot < 0.001f) p.timeStepRot = p.timeStep;
            p.linear      = axes("linearx", "lineary", "linearz");
            const float rg = g("rotational");
            p.rotational  = { rg >= 0.0001f ? rg : g("rotationalx"), g("rotationaly"), g("rotationalz") };
            const char* ax = "xyz";
            for (int a = 0; a < 3; ++a) {
                for (int r = 0; r < 3; ++r) {
                    const std::string key = std::string("linear") + ax[a] + "rotation" + ax[r];
                    const bool        def = (a == 0 && r == 1) || (a == 1 && r == 2) || (a == 2 && r == 0);
                    p.linRot[a][r]        = g(key.c_str(), def ? 1.0f : 0.0f);
                    if (a == r) continue;
                    p.spreadLin[a][r] = g((std::string("linear") + ax[a] + "spreadforce" + ax[r]).c_str());
                    p.spreadRot[a][r] = g((std::string("rotation") + ax[a] + "spreadforce" + ax[r]).c_str());
                }
            }
            p.gravityBias       = g("gravitybias");
            p.gravityCorrection = g("gravitycorrection");
            p.gravityInverted   = g("gravityinvertedcorrection");
            p.gravityInvStart   = g("gravityinvertedcorrectionstart");
            p.gravityInvEnd     = g("gravityinvertedcorrectionend");
            p.forceMultiplier   = g("forcemultipler", 1.0f);
            p.amplitude         = g("amplitude", 1.0f);
            p.clothedAmplitude  = g("breastclothedamplitude", 1.0f);
            p.lightAmplitude    = g("breastlightarmoredamplitude", 1.0f);
            p.heavyAmplitude    = g("breastheavyarmoredamplitude", 1.0f);
            return p;
        }

        inline float Lerp(float a0, float a100, float w) { return a0 + (a100 - a0) * w; }
        inline Vec3  Lerp(Vec3 a0, Vec3 a100, float w) { return { Lerp(a0.x, a100.x, w), Lerp(a0.y, a100.y, w), Lerp(a0.z, a100.z, w) }; }
    }

    /// The alias's settings at body weight `weight` (0-100), each value between its weight-0 and weight-100 values.
    inline CbpcParams ResolveCbpc(const CbpcEntry& e, float weight)
    {
        const CbpcParams a = detail::ResolveOne(e.at0), b = detail::ResolveOne(e.at100);
        const float      w = std::clamp(weight, 0.0f, 100.0f) / 100.0f;
        using detail::Lerp;
        CbpcParams p;
        p.stiffness = Lerp(a.stiffness, b.stiffness, w);
        p.stiffness2 = Lerp(a.stiffness2, b.stiffness2, w);
        p.damping = Lerp(a.damping, b.damping, w);
        p.stiffnessRot = Lerp(a.stiffnessRot, b.stiffnessRot, w);
        p.stiffness2Rot = Lerp(a.stiffness2Rot, b.stiffness2Rot, w);
        p.dampingRot = Lerp(a.dampingRot, b.dampingRot, w);
        p.maxOff = Lerp(a.maxOff, b.maxOff, w);
        p.minOff = Lerp(a.minOff, b.minOff, w);
        p.maxOffRot = Lerp(a.maxOffRot, b.maxOffRot, w);
        p.minOffRot = Lerp(a.minOffRot, b.minOffRot, w);
        p.timeTick = Lerp(a.timeTick, b.timeTick, w);
        p.timeTickRot = Lerp(a.timeTickRot, b.timeTickRot, w);
        p.timeStep = Lerp(a.timeStep, b.timeStep, w);
        p.timeStepRot = Lerp(a.timeStepRot, b.timeStepRot, w);
        p.linear = Lerp(a.linear, b.linear, w);
        p.rotational = Lerp(a.rotational, b.rotational, w);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                p.linRot[i][j]    = Lerp(a.linRot[i][j], b.linRot[i][j], w);
                p.spreadLin[i][j] = Lerp(a.spreadLin[i][j], b.spreadLin[i][j], w);
                p.spreadRot[i][j] = Lerp(a.spreadRot[i][j], b.spreadRot[i][j], w);
            }
        }
        p.gravityBias = Lerp(a.gravityBias, b.gravityBias, w);
        p.gravityCorrection = Lerp(a.gravityCorrection, b.gravityCorrection, w);
        p.gravityInverted = Lerp(a.gravityInverted, b.gravityInverted, w);
        p.gravityInvStart = Lerp(a.gravityInvStart, b.gravityInvStart, w);
        p.gravityInvEnd = Lerp(a.gravityInvEnd, b.gravityInvEnd, w);
        p.forceMultiplier = Lerp(a.forceMultiplier, b.forceMultiplier, w);
        p.amplitude = Lerp(a.amplitude, b.amplitude, w);
        p.clothedAmplitude = Lerp(a.clothedAmplitude, b.clothedAmplitude, w);
        p.lightAmplitude = Lerp(a.lightAmplitude, b.lightAmplitude, w);
        p.heavyAmplitude = Lerp(a.heavyAmplitude, b.heavyAmplitude, w);
        return p;
    }

    /// CBPC's breast bones (any case, by the master config's bone name): their gravity follows NPC Spine2 and their
    /// swing the body armour. Side 'L' or 'R' for a name holding "L Breast" / "R Breast" (whose armour can override
    /// its class with CBPC's per-side keywords), else 0.
    inline bool CbpcIsBreastBone(std::string_view bone) { return Lower(bone).find("breast") != std::string::npos; }
    inline char CbpcBreastSide(std::string_view bone)
    {
        const auto n = Lower(bone);
        if (n.find("l breast") != std::string::npos) return 'L';
        if (n.find("r breast") != std::string::npos) return 'R';
        return 0;
    }

    /// The reference bone CBPC reads a breast bone's gravity from (found by name on the actor's 3D).
    inline constexpr std::string_view kCbpcBreastGravityBone = "NPC Spine2 [Spn2]";

    /// The class CBPC gives the armour in the body slot, for one breast bone.
    enum class CbpcArmor : std::uint8_t { kNone, kClothing, kLight, kHeavy };

    /// What the body-slot armour carries: CBPC's own keywords for the bone's side (CBPCAsNakedL, CBPCAsHeavyL, ...;
    /// all false for a bone of neither side) and the game's ArmorHeavy / ArmorLight / ArmorClothing.
    struct CbpcArmorFacts
    {
        bool asNaked = false, asHeavy = false, asLight = false, asClothing = false;
        bool heavy = false, light = false, clothing = false;
    };

    /// CBPC's rule: AsNaked wins outright, any other As keyword replaces the game's keywords, then heavy before light
    /// before clothing. No armour in the slot is kNone.
    inline CbpcArmor CbpcArmorFor(const CbpcArmorFacts& f)
    {
        if (f.asNaked) return CbpcArmor::kNone;
        bool heavy = f.asHeavy, light = f.asLight, clothing = f.asClothing;
        if (!heavy && !light && !clothing) {
            heavy    = f.heavy;
            light    = f.light;
            clothing = f.clothing;
        }
        if (heavy) return CbpcArmor::kHeavy;
        if (light) return CbpcArmor::kLight;
        if (clothing) return CbpcArmor::kClothing;
        return CbpcArmor::kNone;
    }

    /// The amplitude a breast bone's settings give for the armour class (1 for none).
    inline float CbpcArmorAmplitude(const CbpcParams& p, CbpcArmor a)
    {
        switch (a) {
        case CbpcArmor::kHeavy:    return p.heavyAmplitude;
        case CbpcArmor::kLight:    return p.lightAmplitude;
        case CbpcArmor::kClothing: return p.clothedAmplitude;
        default:                   return 1.0f;
        }
    }

    /// Whether the settings can move the bone at all: some span to lag in, and some share of the lag to show (an
    /// amplitude, the armour's included, of 0 shows none). Ask it once armorAmplitude is set.
    inline bool CbpcMoves(const CbpcParams& p)
    {
        if (std::fabs(p.amplitude * p.armorAmplitude) < 1e-5f) return false;
        const auto span = [](Vec3 lo, Vec3 hi) { return hi.x - lo.x > 1e-4f || hi.y - lo.y > 1e-4f || hi.z - lo.z > 1e-4f; };
        const bool lin  = span(p.minOff, p.maxOff) && (std::fabs(p.linear.x) + std::fabs(p.linear.y) + std::fabs(p.linear.z)) > 1e-5f;
        const bool rot  = span(p.minOffRot, p.maxOffRot) &&
                         (std::fabs(p.rotational.x) + std::fabs(p.rotational.y) + std::fabs(p.rotational.z)) > 1e-5f;
        return lin || rot;
    }

    // ── CBPC step ───────────────────────────────────────────────────────────

    /// Where the lagging points are (relative to the caller's reference point) and how fast they move (world axes).
    struct CbpcState
    {
        Vec3 pos, posRot, vel, velRot;
        bool primed = false;
    };

    /// What the step does to the bone: a move along the parent's axes, and a turn in the bone's own frame given as
    /// CBPC's Euler angles (radians): heading about Y, attitude about Z, bank about X.
    struct CbpcPose
    {
        Vec3  offset;
        float heading = 0.0f, attitude = 0.0f, bank = 0.0f;
    };

    /// CBPC skips a frame gap longer than this (seconds) and starts again from rest.
    inline constexpr float kCbpcResetGap = 0.2f;

    namespace detail
    {
        inline float Sgn(float v) { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }
        inline float Comp(Vec3 v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
        inline void  SetComp(Vec3& v, int i, float x) { (i == 0 ? v.x : (i == 1 ? v.y : v.z)) = x; }
        inline bool  Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    }

    namespace detail
    {
        // The rest lowers by gravityCorrection, which turns to gravityInverted as the reference (the parent, or
        // Spine2 for a breast bone; upZ the world Z of its Z axis) turns upside down: the drop in world Z.
        inline float GravityCorrection(const CbpcParams& p, float upZ)
        {
            float ratio = (upZ + 1.0f) * 0.5f;
            if (std::fabs(p.gravityInvEnd - p.gravityInvStart) > 1e-6f) {
                ratio = (ratio - p.gravityInvStart) / (p.gravityInvEnd - p.gravityInvStart);
            } else {
                ratio = 1.0f;
            }
            ratio = std::clamp(ratio, 0.0f, 1.0f);
            return ratio * p.gravityCorrection + (1.0f - ratio) * p.gravityInverted;
        }

        // The bone's pose from the two clamped lags (in the parent's axes).
        inline CbpcPose PoseFrom(const CbpcParams& p, Vec3 ld, Vec3 ldr)
        {
            CbpcPose    pose;
            const float amp  = p.amplitude * p.armorAmplitude;
            pose.offset      = { ld.x * p.linear.x * amp, ld.y * p.linear.y * amp, ld.z * p.linear.z * amp };
            const float amp2 = p.amplitude * p.amplitude * p.armorAmplitude;
            float       rot[3];
            for (int r = 0; r < 3; ++r) {
                rot[r] = Comp(p.rotational, r) * amp2 * (ldr.x * p.linRot[0][r] + ldr.y * p.linRot[1][r] + ldr.z * p.linRot[2][r]);
            }
            pose.heading  = rot[1];
            pose.attitude = rot[2];
            pose.bank     = rot[0];
            return pose;
        }
    }

    /// The pose CBPC holds a bone in at rest (only gravity's correction), for a parent with unit axes ax/ay/az. The
    /// game's own CBPC has already written it into a captured bone, so the view applies a step's pose against it.
    /// gravityAxis: the unit Z axis whose world Z sets the gravity (Spine2's for a breast bone); null for az.
    inline CbpcPose CbpcRest(const CbpcParams& p, Vec3 ax, Vec3 ay, Vec3 az, const Vec3* gravityAxis = nullptr)
    {
        using MannequinSpringCore::Dot;
        const Vec3 g{ 0.0f, 0.0f, detail::GravityCorrection(p, gravityAxis ? gravityAxis->z : az.z) };
        const Vec3 lgc{ Dot(g, ax), Dot(g, ay), Dot(g, az) };
        return detail::PoseFrom(p, lgc, lgc);
    }

    /// One frame of CBPC's jiggle: target is the bone's parent position, ax/ay/az the parent's unit axes in the world
    /// (all relative to one reference point, as the state is), dt the frame's real time, gravityAxis as for
    /// CbpcRest. A first step, a long gap or a diverging state rest the bone. Returns whether it is still moving;
    /// pose says what to do to the bone.
    inline bool CbpcStep(CbpcState& s, Vec3 target, Vec3 ax, Vec3 ay, Vec3 az, const CbpcParams& p, float dt, CbpcPose& pose,
                         const Vec3* gravityAxis = nullptr)
    {
        using namespace detail;
        using MannequinSpringCore::Dot;
        using MannequinSpringCore::Length;
        const auto toLocal = [&](Vec3 w) { return Vec3{ Dot(w, ax), Dot(w, ay), Dot(w, az) }; };
        const auto toWorld = [&](Vec3 l) { return ax * l.x + ay * l.y + az * l.z; };

        const float gc  = GravityCorrection(p, gravityAxis ? gravityAxis->z : az.z);
        const Vec3  lgc = toLocal({ 0.0f, 0.0f, gc });
        const auto restPose = [&]() {
            s.pos = s.posRot = target;
            s.vel = s.velRot = {};
            s.primed         = true;
            pose             = PoseFrom(p, lgc, lgc);
            return false;
        };
        if (!s.primed || !(dt > 0.0f) || dt > kCbpcResetGap) return restPose();
        const Vec3 d0 = (target - s.pos) * p.forceMultiplier;
        if (std::fabs(d0.x) > 250.0f || std::fabs(d0.y) > 250.0f || std::fabs(d0.z) > 250.0f) return restPose();

        const float deltaT = std::clamp(dt * 1000.0f, 8.0f, 64.0f);
        const Vec3  fgb    = toLocal({ 0.0f, 0.0f, p.gravityBias });

        // Runs one lag (a spring on each parent axis, its settings picked per axis by `axisOf`) for the frame.
        const auto lag = [&](Vec3& pos, Vec3& vel, float tick, float step, Vec3 k, Vec3 k2, Vec3 damp, Vec3 lo, Vec3 hi,
                             const float spread[3][3], const int axisOf[3]) {
            Vec3 diff = toLocal((target - pos) * p.forceMultiplier) * (tick / deltaT);
            Vec3 force;
            for (int i = 0; i < 3; ++i) {
                const float di = Comp(diff, i);
                const int   a  = axisOf[i];
                SetComp(force, i, di * Comp(k, a) + di * di * Sgn(di) * Comp(k2, a) - Comp(fgb, i));
            }
            Vec3 v = toLocal(vel), moved;
            float left = deltaT;
            int   n    = 0;
            do {
                for (int i = 0; i < 3; ++i) {
                    const float vi = Comp(v, i);
                    SetComp(v, i, vi + Comp(force, i) * step - vi * Comp(damp, axisOf[i]) * step);
                }
                moved = moved + v * step;
                left -= tick;
            } while (left >= tick && ++n < 256);
            vel = toWorld(v);
            const Vec3 np = pos + toWorld(moved);
            Vec3       ld = toLocal(np - target);
            const Vec3 before = ld;
            const auto clampAll = [&]() {
                for (int i = 0; i < 3; ++i) SetComp(ld, i, std::clamp(Comp(ld, i), Comp(lo, axisOf[i]), (std::max)(Comp(lo, axisOf[i]), Comp(hi, axisOf[i]))));
            };
            clampAll();
            const Vec3 cut = before - ld;
            for (int to = 0; to < 3; ++to) {
                float add = 0.0f;
                for (int from = 0; from < 3; ++from) {
                    if (from != to) add += Comp(cut, from) * spread[axisOf[from]][axisOf[to]];
                }
                SetComp(ld, to, Comp(ld, to) + add);
            }
            clampAll();
            ld  = ld + lgc;
            pos = toWorld(ld) + target - Vec3{ 0.0f, 0.0f, gc };
            return ld;
        };

        static constexpr int kLinearAxes[3] = { 0, 1, 2 };
        static constexpr int kRotAxes[3]    = { 1, 2, 0 };   // linear X turns about Y, Y about Z, Z about X
        const Vec3 ld  = lag(s.pos, s.vel, p.timeTick, p.timeStep, p.stiffness, p.stiffness2, p.damping, p.minOff, p.maxOff,
                             p.spreadLin, kLinearAxes);
        const Vec3 ldr = lag(s.posRot, s.velRot, p.timeTickRot, p.timeStepRot, p.stiffnessRot, p.stiffness2Rot,
                             p.dampingRot, p.minOffRot, p.maxOffRot, p.spreadRot, kRotAxes);
        if (!Finite(s.pos) || !Finite(s.posRot) || !Finite(s.vel) || !Finite(s.velRot)) return restPose();

        pose = PoseFrom(p, ld, ldr);
        return Length(s.vel) > 0.002f || Length(s.velRot) > 0.002f;
    }

    /// The rotation CBPC builds from its Euler angles, as rows and columns of a column-vector matrix (NiMatrix33's
    /// layout): heading about Y, attitude about Z, bank about X.
    inline void EulerMatrix(float heading, float attitude, float bank, float m[3][3])
    {
        const float ch = std::cos(heading), sh = std::sin(heading);
        const float ca = std::cos(attitude), sa = std::sin(attitude);
        const float cb = std::cos(bank), sb = std::sin(bank);
        m[0][0] = ch * ca;
        m[0][1] = sh * sb - ch * sa * cb;
        m[0][2] = ch * sa * sb + sh * cb;
        m[1][0] = sa;
        m[1][1] = ca * cb;
        m[1][2] = -ca * sb;
        m[2][0] = -sh * ca;
        m[2][1] = sh * sa * cb + ch * sb;
        m[2][2] = -sh * sa * sb + ch * cb;
    }

    // ── A small XML reader (enough for SMP's files) ─────────────────────────

    struct XmlNode
    {
        std::string                                      name;   // lower-cased
        std::vector<std::pair<std::string, std::string>> attrs;  // names lower-cased
        std::string                                      text;
        std::vector<XmlNode>                             children;

        const std::string* Attr(std::string_view n) const
        {
            for (const auto& [k, v] : attrs) {
                if (k == n) return &v;
            }
            return nullptr;
        }
        const XmlNode* Child(std::string_view n) const
        {
            for (const auto& c : children) {
                if (c.name == n) return &c;
            }
            return nullptr;
        }
    };

    /// Parses an XML document into a tree under a nameless root; false on a malformed one. Comments, declarations
    /// and CDATA are skipped; entities are left as written; a stray close tag at document level is ignored.
    inline bool ParseXml(std::string_view s, XmlNode& root)
    {
        root = {};
        std::vector<XmlNode*> stack{ &root };
        std::size_t           i = 0;
        const auto            skipTo = [&](std::string_view end) {
            const auto at = s.find(end, i);
            i             = at == std::string_view::npos ? s.size() : at + end.size();
        };
        while (i < s.size()) {
            if (s[i] != '<') {
                const auto lt = s.find('<', i);
                const auto t  = s.substr(i, lt == std::string_view::npos ? std::string_view::npos : lt - i);
                stack.back()->text += std::string(t);
                i = lt == std::string_view::npos ? s.size() : lt;
                continue;
            }
            if (s.compare(i, 4, "<!--") == 0) { skipTo("-->"); continue; }
            if (s.compare(i, 9, "<![CDATA[") == 0) { skipTo("]]>"); continue; }
            if (s.compare(i, 2, "<?") == 0) { skipTo("?>"); continue; }
            if (s.compare(i, 2, "<!") == 0) { skipTo(">"); continue; }
            if (s.compare(i, 2, "</") == 0) {
                const auto gt = s.find('>', i);
                if (gt == std::string_view::npos) return false;
                const auto name = Lower(Trim(s.substr(i + 2, gt - i - 2)));
                // A close tag with nothing open is skipped, as SMP's reader never reads past its root: shipped
                // armours end `</system>undefined</xml>`.
                if (stack.size() >= 2) {
                    if (stack.back()->name != name) return false;
                    stack.pop_back();
                }
                i = gt + 1;
                continue;
            }
            ++i;
            std::size_t j = i;
            while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j])) && s[j] != '>' && s[j] != '/') ++j;
            XmlNode n;
            n.name = Lower(s.substr(i, j - i));
            if (n.name.empty()) return false;
            i            = j;
            bool selfEnd = false;
            for (;;) {
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                if (i >= s.size()) return false;
                if (s[i] == '>') { ++i; break; }
                if (s[i] == '/') {
                    if (i + 1 < s.size() && s[i + 1] == '>') { selfEnd = true; i += 2; break; }
                    return false;
                }
                std::size_t k = i;
                while (k < s.size() && s[k] != '=' && s[k] != '>' && !std::isspace(static_cast<unsigned char>(s[k]))) ++k;
                const auto an = Lower(s.substr(i, k - i));
                i             = k;
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                if (i >= s.size() || s[i] != '=') {
                    n.attrs.emplace_back(an, "");
                    continue;
                }
                ++i;
                while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) return false;
                const char q  = s[i];
                const auto ce = s.find(q, i + 1);
                if (ce == std::string_view::npos) return false;
                n.attrs.emplace_back(an, std::string(s.substr(i + 1, ce - i - 1)));
                i = ce + 1;
            }
            if (stack.size() > 64) return false;
            stack.back()->children.push_back(std::move(n));
            if (!selfEnd) stack.push_back(&stack.back()->children.back());
        }
        return stack.size() == 1;
    }

    // ── HDT-SMP physics XML ─────────────────────────────────────────────────

    /// One bone an SMP file names: simulated (mass above 0) or following the animation, with how far it may swing.
    struct SmpBone
    {
        bool  dynamic        = false;
        float angularDamping = 0.0f;
        float cone           = -1.0f;   // radians; -1 = no constraint names it
    };

    struct SmpPhysics
    {
        std::unordered_map<std::string, SmpBone> bones;   // by lower-cased bone name
        std::size_t fromConstraints = 0;   // bones only a constraint names (SMP creates them from the bone-default)
        std::size_t repeated        = 0;   // <bone> declarations of a name already created, which SMP ignores
    };

    /// A live node's name as FSMP left it. FSMP renames each bone it merges into the skeleton
    /// `hdtSSEPhysics_AutoRename_Armor_<hex id> <name>` (or `_Head_`), in any case, with any number of hex digits, and
    /// twice for a node merged twice; a bone the skeleton already had keeps its own name.
    struct SmpNodeName
    {
        std::string              key;      // the name an SMP XML gives the bone, lower-cased
        std::vector<std::string> pieces;   // the rename ids of the pieces it was merged for ("armor_1a"), outermost first
    };

    inline SmpNodeName SplitSmpNodeName(std::string_view name)
    {
        SmpNodeName                out;
        const std::string          lower = Lower(name);
        std::string_view           n(lower);
        constexpr std::string_view kPrefix = "hdtssephysics_autorename_";
        while (n.rfind(kPrefix, 0) == 0) {
            const std::string_view id   = n.substr(kPrefix.size());
            std::string_view       rest = id;
            if (rest.rfind("armor_", 0) == 0) rest.remove_prefix(6);
            else if (rest.rfind("head_", 0) == 0) rest.remove_prefix(5);
            else break;
            std::size_t hex = 0;
            while (hex < rest.size() && std::isxdigit(static_cast<unsigned char>(rest[hex]))) ++hex;
            if (hex == 0 || hex >= rest.size() || rest[hex] != ' ') break;
            out.pieces.emplace_back(id.substr(0, static_cast<std::size_t>(rest.data() - id.data()) + hex));
            n = rest.substr(hex + 1);
        }
        out.key = std::string(n);
        return out;
    }

    /// The name an SMP XML gives a bone, from its live node's name (SplitSmpNodeName). Only the SMP lookup uses this:
    /// CBPC finds its bones by their skeleton names.
    inline std::string SmpBoneKey(std::string_view name) { return SplitSmpNodeName(name).key; }

    /// The tighter of two swing limits, -1 meaning none.
    inline float TighterCone(float a, float b) noexcept
    {
        if (a < 0.0f) return b;
        if (b < 0.0f) return a;
        return (std::min)(a, b);
    }

    /// A bone several SMP files name: simulated if any of them simulates it, as far as the tightest lets it.
    inline void MergeSmpBone(SmpBone& into, const SmpBone& b) noexcept
    {
        if (b.dynamic && !into.dynamic) into = b;
        else if (b.dynamic) into.cone = TighterCone(into.cone, b.cone);
    }

    /// The SMP bones of what an actor wears, by the file that names them. FSMP simulates each piece from its own
    /// file, and two pieces' files can name a bone alike (Bone01, HDT_BS01 2) while FSMP keeps their nodes apart
    /// under each piece's rename id. So a node renamed for a piece whose files are known takes the bone from those
    /// files only; a renamed node of an unknown piece only a name one file alone names; a skeleton bone (no rename)
    /// the merge of every file that names it (MergeSmpBone).
    class SmpBoneIndex
    {
    public:
        enum class Found : std::uint8_t { kNone, kBone, kAmbiguous };

        /// Adds a file (once, however often it is added); returns its index.
        std::size_t AddFile(std::shared_ptr<const SmpPhysics> file)
        {
            for (std::size_t i = 0; i < m_files.size(); ++i) {
                if (m_files[i] == file) return i;
            }
            for (const auto& [name, b] : file->bones) {
                auto [it, fresh] = m_merged.emplace(name, b);
                if (!fresh) MergeSmpBone(it->second, b);
                ++m_namedBy[name];
            }
            m_files.push_back(std::move(file));
            return m_files.size() - 1;
        }

        /// Notes that the piece with rename id `piece` (SplitSmpNodeName) is simulated from file `file`.
        void NotePieceFile(const std::string& piece, std::size_t file)
        {
            auto& files = m_pieces[piece];
            if (file < m_files.size() && std::find(files.begin(), files.end(), file) == files.end()) files.push_back(file);
        }

        /// The bone for a live node's name, into out. kAmbiguous: a renamed node of an unknown piece whose name
        /// several files give, which is left still.
        Found Find(std::string_view liveName, SmpBone& out) const
        {
            const SmpNodeName n = SplitSmpNodeName(liveName);
            for (const auto& id : n.pieces) {
                const auto p = m_pieces.find(id);
                if (p == m_pieces.end()) continue;
                bool found = false;
                for (const std::size_t f : p->second) {
                    const auto it = m_files[f]->bones.find(n.key);
                    if (it == m_files[f]->bones.end()) continue;
                    if (!found) out = it->second;
                    else MergeSmpBone(out, it->second);
                    found = true;
                }
                return found ? Found::kBone : Found::kNone;
            }
            const auto it = m_merged.find(n.key);
            if (it == m_merged.end()) return Found::kNone;
            if (!n.pieces.empty() && m_namedBy.at(n.key) > 1) return Found::kAmbiguous;
            out = it->second;
            return Found::kBone;
        }

        std::size_t Files() const noexcept { return m_files.size(); }
        std::size_t Bones() const noexcept { return m_merged.size(); }   // distinct names

    private:
        std::vector<std::shared_ptr<const SmpPhysics>>            m_files;
        std::unordered_map<std::string, std::vector<std::size_t>> m_pieces;   // by rename id
        std::unordered_map<std::string, SmpBone>                  m_merged;
        std::unordered_map<std::string, std::uint32_t>            m_namedBy;  // how many files name each bone
    };

    /// SMP's shape mapping (SKSE/Plugins/hdtSkinnedMeshConfigs/defaultBBPs.xml): the physics file for a mesh shape
    /// that names none itself (an SMP body, a tail, ears), by lower-cased shape name. Adds to `out` (a later file's
    /// mapping replaces an earlier one); false for a file that is not one.
    inline bool ParseSmpDefaultBbps(std::string_view text, std::unordered_map<std::string, std::string>& out)
    {
        XmlNode root;
        if (!ParseXml(text, root)) return false;
        const XmlNode* top = root.Child("default-bbps");
        if (!top) return false;
        for (const auto& n : top->children) {
            if (n.name != "map") continue;
            const auto* shape = n.Attr("shape");
            const auto* file  = n.Attr("file");
            if (!shape || !file || Trim(*shape).empty() || Trim(*file).empty()) continue;
            out[Lower(Trim(*shape))] = std::string(Trim(*file));
        }
        return true;
    }

    /// A bone with no constraint swings this far at most (radians).
    inline constexpr float kSmpUnconstrainedCone = 0.6f;

    namespace detail
    {
        struct SmpBoneDefaults
        {
            float mass = 0.0f, angularDamping = 0.0f;
        };
        struct SmpConstraintDefaults
        {
            Vec3  angLo{ 1.0f, 1.0f, 1.0f }, angHi{ -1.0f, -1.0f, -1.0f };   // SMP's unset limits: every axis free
            float swing1 = 0.0f, swing2 = 0.0f;
            bool  haveLimits = false;
        };

        // SMP's number reader takes the first comma as the decimal point. CBPC's does not, so ParseFloat, which
        // both read through, leaves it alone.
        inline bool SmpFloat(std::string_view s, float& out)
        {
            std::string t(s);
            if (const auto comma = t.find(','); comma != std::string::npos) t[comma] = '.';
            return ParseFloat(t, out);
        }
        inline float TextFloat(const XmlNode* n, float def)
        {
            float v = def;
            if (n) SmpFloat(n->text, v);
            return v;
        }
        inline Vec3 AttrVec(const XmlNode* n, Vec3 def)
        {
            if (!n) return def;
            Vec3 v = def;
            if (const auto* x = n->Attr("x")) SmpFloat(*x, v.x);
            if (const auto* y = n->Attr("y")) SmpFloat(*y, v.y);
            if (const auto* z = n->Attr("z")) SmpFloat(*z, v.z);
            return v;
        }
        inline void ApplyBone(const XmlNode& n, SmpBoneDefaults& d)
        {
            d.mass           = TextFloat(n.Child("mass"), d.mass);
            d.angularDamping = TextFloat(n.Child("angulardamping"), d.angularDamping);
        }
        inline void ApplyGeneric(const XmlNode& n, SmpConstraintDefaults& d)
        {
            const XmlNode* lo = n.Child("angularlowerlimit");
            const XmlNode* hi = n.Child("angularupperlimit");
            if (lo || hi) d.haveLimits = true;
            d.angLo = AttrVec(lo, d.angLo);
            d.angHi = AttrVec(hi, d.angHi);
        }
        // SMP's reader takes coneLimit and limitZ for swingSpan1, planeLimit and limitY for swingSpan2, the last one
        // written winning, never below 0.
        inline void ApplyConeTwist(const XmlNode& n, SmpConstraintDefaults& d)
        {
            for (const auto& c : n.children) {
                float* to = nullptr;
                if (c.name == "swingspan1" || c.name == "conelimit" || c.name == "limitz") to = &d.swing1;
                else if (c.name == "swingspan2" || c.name == "planelimit" || c.name == "limity") to = &d.swing2;
                if (!to) continue;
                d.haveLimits = true;
                float v = *to;
                if (SmpFloat(c.text, v)) *to = (std::max)(0.0f, v);
            }
        }
        // A generic constraint swings as far as the mean of its two widest half-spans. An axis whose lower limit is
        // above its upper one is free (Bullet's rule, which SMP's schema repeats) and counts as kSmpUnconstrainedCone.
        inline float GenericCone(const SmpConstraintDefaults& d)
        {
            const auto half = [](float lo, float hi) { return lo > hi ? kSmpUnconstrainedCone : (hi - lo) * 0.5f; };
            float      h[3] = { half(d.angLo.x, d.angHi.x), half(d.angLo.y, d.angHi.y), half(d.angLo.z, d.angHi.z) };
            std::sort(std::begin(h), std::end(h));
            return (std::max)(0.0f, (h[1] + h[2]) * 0.5f);
        }
    }

    /// Reads an SMP physics XML: <bone> / <bone-default> (named templates and `extends` included; a bone's first
    /// declaration wins) and the generic, cone-twist and stiff-spring constraints, in <constraint-group> too (a body
    /// no <bone> declared comes from the bone-default in force there). Adds to `out`; false for a file that is not one.
    inline bool ParseSmp(std::string_view text, SmpPhysics& out)
    {
        using namespace detail;
        XmlNode root;
        if (!ParseXml(text, root)) return false;
        const XmlNode* sys = root.Child("system");
        if (!sys) return false;

        SmpBoneDefaults                                        boneDef;
        std::map<std::string, SmpBoneDefaults>                 boneTemplates;
        std::map<std::string, SmpConstraintDefaults>           genericDefs, coneDefs, springDefs;
        SmpConstraintDefaults                                  genericDef, coneDef, springDef;
        std::unordered_map<std::string, SmpBoneDefaults>       bones;
        struct Constraint { std::string a, b; float cone; };
        std::vector<Constraint>                                constraints;

        const auto lowerAttr = [](const XmlNode& n, std::string_view k) {
            const auto* v = n.Attr(k);
            return v ? Lower(Trim(*v)) : std::string{};
        };
        const auto defaultFor = [&](auto& unnamed, auto& named, const XmlNode& n, auto apply) {
            const std::string name = lowerAttr(n, "name"), ext = lowerAttr(n, "extends");
            auto base = unnamed;
            if (!ext.empty()) {
                if (const auto it = named.find(ext); it != named.end()) base = it->second;
            }
            apply(n, base);
            if (name.empty()) unnamed = base;
            else named[name] = base;
        };
        const auto pickTemplate = [&](const auto& unnamed, const auto& named, const XmlNode& n) {
            const std::string t = lowerAttr(n, "template");
            if (!t.empty()) {
                if (const auto it = named.find(t); it != named.end()) return it->second;
            }
            return unnamed;
        };

        std::function<void(const XmlNode&)> walk = [&](const XmlNode& parent) {
            for (const auto& n : parent.children) {
                if (n.name == "bone-default") {
                    defaultFor(boneDef, boneTemplates, n, ApplyBone);
                } else if (n.name == "bone") {
                    const std::string name = lowerAttr(n, "name");
                    if (name.empty()) continue;
                    auto d = pickTemplate(boneDef, boneTemplates, n);
                    ApplyBone(n, d);
                    if (!bones.try_emplace(name, d).second) ++out.repeated;   // SMP keeps the first
                } else if (n.name == "generic-constraint-default") {
                    defaultFor(genericDef, genericDefs, n, ApplyGeneric);
                } else if (n.name == "conetwist-constraint-default") {
                    defaultFor(coneDef, coneDefs, n, ApplyConeTwist);
                } else if (n.name == "stiffspring-constraint-default") {
                    defaultFor(springDef, springDefs, n, [](const XmlNode&, SmpConstraintDefaults&) {});
                } else if (n.name == "generic-constraint" || n.name == "conetwist-constraint" || n.name == "stiffspring-constraint") {
                    Constraint c{ lowerAttr(n, "bodya"), lowerAttr(n, "bodyb"), kSmpUnconstrainedCone };
                    // A body no <bone> has declared yet is created here from the unnamed bone-default in force, as
                    // SMP creates it from the node of that name; a later <bone> of the name is then ignored.
                    for (const std::string* body : { &c.a, &c.b }) {
                        if (!body->empty() && bones.try_emplace(*body, boneDef).second) ++out.fromConstraints;
                    }
                    if (n.name == "generic-constraint") {
                        auto d = pickTemplate(genericDef, genericDefs, n);
                        ApplyGeneric(n, d);
                        c.cone = d.haveLimits ? GenericCone(d) : kSmpUnconstrainedCone;
                    } else if (n.name == "conetwist-constraint") {
                        auto d = pickTemplate(coneDef, coneDefs, n);
                        ApplyConeTwist(n, d);
                        c.cone = d.haveLimits ? (std::max)(0.0f, (d.swing1 + d.swing2) * 0.5f) : kSmpUnconstrainedCone;
                    }
                    if (!c.a.empty() || !c.b.empty()) constraints.push_back(std::move(c));
                } else if (n.name == "constraint-group") {
                    walk(n);
                }
            }
        };
        walk(*sys);

        for (const auto& [name, d] : bones) {
            SmpBone b;
            b.dynamic        = d.mass > 0.0f;
            b.angularDamping = d.angularDamping;
            out.bones[name]  = b;
        }
        // The swing a constraint allows belongs to its simulated body (bodyA by convention); the tightest one wins.
        for (const auto& c : constraints) {
            auto ia = out.bones.find(c.a);
            auto ib = out.bones.find(c.b);
            auto it = (ia != out.bones.end() && ia->second.dynamic) ? ia
                    : (ib != out.bones.end() && ib->second.dynamic) ? ib : out.bones.end();
            if (it == out.bones.end()) continue;
            it->second.cone = TighterCone(it->second.cone, c.cone);
        }
        return true;
    }

    /// The tip spring an SMP-simulated bone swings on: its constraint's reach, the rest ours (the view has no
    /// gravity or collision, so the pull back to the pose stands in for both), damped harder as SMP damps it.
    inline MannequinSpringCore::SpringParams SmpSpringParams(const SmpBone& b)
    {
        const float cone = std::clamp(b.cone < 0.0f ? kSmpUnconstrainedCone : b.cone, 0.03f, 1.0f);
        const float keep = 1.0f - std::clamp(b.angularDamping, 0.0f, 0.95f);   // Bullet keeps (1 - d) per second
        return { 50.0f, 4.0f - 4.0f * std::log(keep), cone };
    }
}
