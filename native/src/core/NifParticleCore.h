#pragma once

// The particle definitions of a Skyrim NIF (20.2.0.7, user version 12, BS version 100 for SE, 83 for LE, which SE
// and VR load too), read from the file's bytes:
// each NiParticleSystem's emitter, birth rate (its own track, or a sequence's when the model's NiControllerManager
// drives it), colour, scale, gravity, drag, rotation and flipbook, and the systems it reads but the renderer cannot
// run, with why. The renderer runs them itself (ParticleSimCore): the engine's particle update only runs from its own
// render path. Engine-free; tested by NifParticleCoreTest.cpp. Field layouts follow nif.xml and were checked block by
// block against vanilla, Embers XD and LoreRim meshes (each parse ends exactly at its block's declared size).

#include "AsciiText.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace Phantasm::NifParticleCore
{
    /// A float track of keys (time, value), linear between them, or a constant when there are none.
    struct Curve
    {
        std::vector<std::pair<float, float>> keys;
        float                                constant = 0.0f;

        float Eval(float t) const noexcept
        {
            if (keys.empty()) return constant;
            if (t <= keys.front().first) return keys.front().second;
            if (t >= keys.back().first) return keys.back().second;
            for (std::size_t i = 1; i < keys.size(); ++i) {
                if (t <= keys[i].first) {
                    const auto& a    = keys[i - 1];
                    const auto& b    = keys[i];
                    const float span = b.first - a.first;
                    return span > 1.0e-6f ? a.second + (b.second - a.second) * (t - a.first) / span : b.second;
                }
            }
            return keys.back().second;
        }
    };

    /// A bool track (the emitter's visibility): steps at its keys; true with none.
    struct BoolCurve
    {
        std::vector<std::pair<float, bool>> keys;

        bool Eval(float t) const noexcept
        {
            bool on = keys.empty() ? true : keys.front().second;
            for (const auto& k : keys) {
                if (k.first > t) break;
                on = k.second;
            }
            return on;
        }
    };

    /// A controller's time: frequency, phase, and the [start, stop] range it loops over (clamps, cycle type 2).
    struct ControllerTime
    {
        float frequency = 1.0f, phase = 0.0f, start = 0.0f, stop = 0.0f;
        bool  clamp = false;

        float Scaled(float t) const noexcept
        {
            const float s    = t * frequency + phase;
            const float span = stop - start;
            if (!(span > 1.0e-6f)) return start;
            if (clamp) return std::clamp(s, start, stop);
            float m = std::fmod(s - start, span);
            if (m < 0.0f) m += span;
            return start + m;
        }
    };

    enum class EmitterShape
    {
        kBox,       // dims: width (x), height (y), depth (z)
        kSphere,    // dims[0]: radius
        kCylinder,  // dims[0]: radius, dims[1]: height (along z)
        kMesh,      // NiPSysMeshEmitter: emits from the vertices, faces or edges of `MeshEmitter::meshes` (not simulated)
    };

    /// A NiPSysMeshEmitter's own fields (nif.xml), after the common emitter ones.
    struct MeshEmitter
    {
        std::vector<std::string> meshes;            // the emitter meshes' names
        std::uint32_t            velocityType = 0;  // 0 the mesh normals, 1 random, 2 the emission axis
        std::uint32_t            emissionType = 0;  // 0 vertices, 1 face centres, 2 edge centres, 3 faces, 4 edges
        std::array<float, 3>     axis{ 1, 0, 0 };
    };

    /// Why a system of the file is read but not run.
    enum class Unsupported
    {
        kNone,
        kMeshEmitter,    // its emitter is a mesh's surface, which needs the mesh's vertices in the sim
        kStripSystem,    // a BSStripParticleSystem: its particles join into ribbons, not quads
        kNoEmitter,      // no active emitter the reader knows
        kUnreadable,     // a block it needs did not read
    };

    inline const char* UnsupportedText(Unsupported u) noexcept
    {
        switch (u) {
        case Unsupported::kMeshEmitter: return "emit from a mesh (not simulated)";
        case Unsupported::kStripSystem: return "are a strip system (not simulated)";
        case Unsupported::kNoEmitter:   return "have no emitter the reader knows";
        case Unsupported::kUnreadable:  return "could not be read";
        default:                        return "are supported";
        }
    }

    /// What Parse made of the file as a whole.
    enum class FileVerdict
    {
        kRead,        // a Skyrim SE or LE NIF whose header read (it may hold no particles)
        kNotSkyrim,   // another version: Fallout, Oblivion, or not a NIF
        kUnreadable,  // truncated or malformed before its blocks
    };

    struct Emitter
    {
        EmitterShape         shape = EmitterShape::kBox;
        std::array<float, 3> dims{};
        float speed = 0, speedVar = 0, declination = 0, declinationVar = 0, planar = 0, planarVar = 0;
        std::array<float, 4> color{ 1, 1, 1, 1 };
        float                radius = 1, radiusVar = 0, life = 1, lifeVar = 0;
        std::string          object;   // the node it emits from; "" = the system itself (a mesh emitter: none)
        MeshEmitter          mesh;     // kMesh only
    };

    /// BSPSysSimpleColorModifier: colors[0] until c1End, to colors[1] by c1Start, colors[1] until c2End, to colors[2]
    /// by c2Start (each a fraction of the life); alpha also fades in until fadeIn and out from fadeOut.
    struct ColorFade
    {
        bool                                on = false;
        float                               fadeIn = 0, fadeOut = 1, c1End = 0, c1Start = 0, c2End = 0, c2Start = 0;
        std::array<std::array<float, 4>, 3> colors{};
    };

    struct Gravity
    {
        std::string          name;     // the modifier's own: the controller id a sequence's strength track names
        std::string          object;
        std::array<float, 3> axis{ 0, 0, 1 };
        float                strength = 0, decay = 0, turbulence = 0, turbulenceScale = 1;
        int                  forceType = 0;   // 0 planar (along the axis), 1 spherical (toward the object)
        bool                 worldAligned = false;
    };

    struct Drag
    {
        std::string          object;
        std::array<float, 3> axis{ 0, 0, 1 };
        float                percentage = 0;
    };

    struct Rotation
    {
        bool  on = false;
        float speed = 0, speedVar = 0, angle = 0, angleVar = 0;
        bool  randomSign = false;
    };

    /// BSPSysSubTexModifier: the flipbook frame runs from start to end over the particle's life.
    struct SubTex
    {
        bool  on = false;
        float start = 0, end = 0, loopStart = 0, frameCount = 0;
    };

    struct System
    {
        std::string          name;
        Unsupported          unsupported = Unsupported::kNone;   // not kNone: read, but not to be simulated
        bool                 worldSpace = false;
        bool                 hasEmitter = false;
        Emitter              emitter;
        Curve                birthRate;   // particles a second, against emitTime
        BoolCurve            visible;     // against emitTime
        // The emitter controller's time, or, when a sequence of the model's NiControllerManager drives it (blend
        // interpolators), that sequence's (`sequence` names it).
        ControllerTime       emitTime;
        std::string          sequence;
        ColorFade            color;
        std::vector<float>   scales;      // over the life, evenly spaced
        std::vector<Gravity> gravity;
        std::vector<Drag>    drag;
        Rotation             rotation;
        SubTex               subTex;
        // The texture's flipbook frames from the system's NiPSysData, each (u offset, u size, v offset, v size).
        std::vector<std::array<float, 4>> frames;
    };

    namespace detail
    {
        struct Reader
        {
            const std::uint8_t* p   = nullptr;
            std::size_t         n   = 0;
            std::size_t         o   = 0;
            bool                bad = false;

            bool Need(std::size_t k)
            {
                if (bad || o + k > n) {
                    bad = true;
                    return false;
                }
                return true;
            }
            template <class T>
            T Get()
            {
                T v{};
                if (Need(sizeof(T))) {
                    std::memcpy(&v, p + o, sizeof(T));
                    o += sizeof(T);
                }
                return v;
            }
            float         F() { return Get<float>(); }
            std::uint32_t U32() { return Get<std::uint32_t>(); }
            std::int32_t  I32() { return Get<std::int32_t>(); }
            std::uint16_t U16() { return Get<std::uint16_t>(); }
            std::uint8_t  U8() { return Get<std::uint8_t>(); }
            void          Skip(std::size_t k)
            {
                if (Need(k)) o += k;
            }
        };

        struct Block
        {
            std::string type;
            std::size_t offset = 0, size = 0;
        };

        // Reads a NiObjectNET's name (a string index) at the start of a block.
        inline std::string NameAt(const Reader& file, const std::vector<std::string>& strings, std::size_t offset)
        {
            Reader r{ file.p, file.n, offset };
            const std::int32_t i = r.I32();
            return !r.bad && i >= 0 && static_cast<std::size_t>(i) < strings.size() ? strings[i] : std::string{};
        }

        inline bool EqualsNoCase(const std::string& a, const char* b)
        {
            std::size_t i = 0;
            for (; i < a.size() && b[i]; ++i) {
                if (AsciiText::Lower(a[i]) != AsciiText::Lower(b[i])) return false;
            }
            return i == a.size() && !b[i];
        }

        struct Header
        {
            Reader                   file;   // the whole file (NameAt reads names through it)
            std::vector<std::string> strings;
            std::vector<Block>       blocks;
            bool                     legacy = false;   // BS version 83 (LE): NiParticleSystem is laid out as NiGeometry
        };

        // Reads a Skyrim NIF's header: its block types and sizes and its string table. False for a file it cannot
        // read, with `verdict` kNotSkyrim for another version; `verdict` kRead once the header is read.
        inline bool ReadHeader(const std::uint8_t* data, std::size_t size, Header& h, FileVerdict& verdict)
        {
            verdict = FileVerdict::kUnreadable;
            if (!data || size < 64) return false;
            const void* nl = std::memchr(data, '\n', std::min<std::size_t>(size, 128));
            if (!nl) return false;
            Reader r{ data, size, static_cast<std::size_t>(static_cast<const std::uint8_t*>(nl) - data) + 1 };
            const std::uint32_t version = r.U32();
            r.U8();   // endian
            const std::uint32_t userVersion = r.U32();
            const std::uint32_t numBlocks   = r.U32();
            const std::uint32_t bsVersion   = r.U32();
            if (r.bad) return false;
            if (version != 0x14020007 || userVersion != 12 || (bsVersion != 100 && bsVersion != 83)) {
                verdict = FileVerdict::kNotSkyrim;
                return false;
            }
            h.legacy = bsVersion == 83;
            if (numBlocks > 65535) return false;
            for (int i = 0; i < 3; ++i) r.Skip(r.U8());   // author, process script, export script
            const std::uint16_t      numTypes = r.U16();
            std::vector<std::string> types(numTypes);
            for (auto& t : types) {
                const std::uint32_t len = r.U32();
                if (!r.Need(len) || len > 256) return false;
                t.assign(reinterpret_cast<const char*>(data + r.o), len);
                r.o += len;
            }
            std::vector<std::uint16_t> typeOf(numBlocks);
            for (auto& t : typeOf) t = r.U16();
            std::vector<std::uint32_t> sizeOf(numBlocks);
            for (auto& sz : sizeOf) sz = r.U32();
            const std::uint32_t numStrings = r.U32();
            r.U32();   // the longest string
            if (r.bad || numStrings > 1u << 20) return false;
            h.strings.assign(numStrings, std::string{});
            for (auto& str : h.strings) {
                const std::uint32_t len = r.U32();
                if (!r.Need(len)) return false;
                str.assign(reinterpret_cast<const char*>(data + r.o), len);
                r.o += len;
            }
            r.Skip(std::size_t(r.U32()) * 4);   // groups
            if (r.bad) return false;
            h.blocks.assign(numBlocks, Block{});
            std::size_t at = r.o;
            for (std::uint32_t i = 0; i < numBlocks; ++i) {
                const std::uint16_t t = typeOf[i] & 0x7FFF;
                if (t >= types.size() || at + sizeOf[i] > size) return false;
                h.blocks[i] = { types[t], at, sizeOf[i] };
                at += sizeOf[i];
            }
            h.file  = r;
            verdict = FileVerdict::kRead;
            return true;
        }
    }

    /// A NiControllerSequence of the model's NiControllerManager, as far as the emitters need it.
    struct Sequence
    {
        std::string    name;
        std::uint32_t  cycle = 0;   // 0 loop, 1 reverse, 2 clamp
        ControllerTime time;
        struct Block
        {
            std::int32_t interpolator = -1, controller = -1;
            std::string  node, controllerType, controllerId, interpolatorId;
        };
        std::vector<Block> blocks;
    };

    /// Which of the sequences that drive an emitter the view plays, by index (or -1 with none): no sequence plays
    /// in the view, so it shows the model at rest: a sequence named Idle or AmIdle (the engine's idle state, e.g. a
    /// beehive's, whose emitters are off), else the first that loops (mLoop between a one-shot mBegin and mEnd),
    /// else the first.
    inline int ChooseSequence(const std::vector<const Sequence*>& candidates)
    {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (detail::EqualsNoCase(candidates[i]->name, "Idle") || detail::EqualsNoCase(candidates[i]->name, "AmIdle")) {
                return static_cast<int>(i);
            }
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i]->cycle == 0) return static_cast<int>(i);
        }
        return candidates.empty() ? -1 : 0;
    }

    /// The emitter values the chosen sequence holds for system s, over the emitter block's own: its particles' size,
    /// life and speed, and its gravity modifiers' strengths (a spell's hand art sets them per state: Sparks' idle
    /// particles are 5 units, its file's 0.75). Each is the track's value at the sequence's start; a track that reads
    /// no finite value keeps the block's. readFloat(interpolator, Curve&) reads a float track.
    template <class ReadFloat>
    void ApplySequenceEmitterValues(const Sequence& q, System& s, const ReadFloat& readFloat)
    {
        for (const auto& b : q.blocks) {
            if (b.node != s.name) continue;
            Curve c;
            c.constant = std::numeric_limits<float>::quiet_NaN();
            readFloat(b.interpolator, c);
            const float v = c.keys.empty() ? c.constant : c.Eval(q.time.start);
            if (!std::isfinite(v) || std::fabs(v) >= 1.0e30f) continue;
            if (b.controllerType == "NiPSysEmitterInitialRadiusCtlr") {
                if (v >= 0.0f) s.emitter.radius = v;
            } else if (b.controllerType == "NiPSysEmitterLifeSpanCtlr") {
                if (v > 0.0f) s.emitter.life = v;
            } else if (b.controllerType == "NiPSysEmitterSpeedCtlr") {
                s.emitter.speed = v;
            } else if (b.controllerType == "NiPSysGravityStrengthCtlr") {
                for (auto& g : s.gravity) {
                    if (g.name == b.controllerId) g.strength = v;
                }
            }
        }
    }

    /// The particle systems of a NIF's bytes; empty when it is not a Skyrim NIF or holds none. A system the view cannot
    /// run is returned by name with why (`unsupported`), so the renderer can say so; a block it cannot read marks
    /// only the system that uses it. `verdict` (optional) says what was made of the file.
    inline std::vector<System> Parse(const std::uint8_t* data, std::size_t size, FileVerdict* verdict = nullptr)
    {
        using namespace detail;
        std::vector<System> out;
        FileVerdict         dummy;
        FileVerdict&        fileVerdict = verdict ? *verdict : dummy;
        Header              h;
        if (!ReadHeader(data, size, h, fileVerdict)) return out;
        const Reader&                   r         = h.file;
        const std::vector<std::string>& strings   = h.strings;
        const std::vector<Block>&       blocks    = h.blocks;
        const std::uint32_t             numBlocks = static_cast<std::uint32_t>(blocks.size());

        const auto reader = [&](std::int32_t index) -> Reader {
            if (index < 0 || static_cast<std::size_t>(index) >= blocks.size()) return Reader{ data, 0, 0, true };
            const auto& b = blocks[index];
            return Reader{ data, b.offset + b.size, b.offset };
        };
        const auto str = [&](Reader& rr) {
            const std::int32_t i = rr.I32();
            return i >= 0 && static_cast<std::size_t>(i) < strings.size() ? strings[i] : std::string{};
        };
        const auto nodeName = [&](std::int32_t index) {
            return index >= 0 && static_cast<std::size_t>(index) < blocks.size()
                       ? NameAt(r, strings, blocks[index].offset)
                       : std::string{};
        };
        const auto typeIs = [&](std::int32_t index, const char* t) {
            return index >= 0 && static_cast<std::size_t>(index) < blocks.size() && blocks[index].type == t;
        };
        // NiPSysModifier: name, order, target, active.
        const auto modifierHead = [&](Reader& rr, std::string& name, bool& active) {
            name = str(rr);
            rr.U32();
            rr.I32();
            active = rr.U8() != 0;
        };
        const auto floatCurve = [&](std::int32_t interp, Curve& c) {
            if (!typeIs(interp, "NiFloatInterpolator")) return;
            Reader             ir   = reader(interp);
            const float        v    = ir.F();
            const std::int32_t dref = ir.I32();
            c.constant              = v;
            if (!typeIs(dref, "NiFloatData")) return;
            Reader              dr  = reader(dref);
            const std::uint32_t num = dr.U32();
            if (num == 0 || num > 4096) return;
            const std::uint32_t kind = dr.U32();   // 1 linear, 2 quadratic (+2 tangents), 3 TBC (+3), 5 constant
            for (std::uint32_t k = 0; k < num && !dr.bad; ++k) {
                const float t = dr.F(), val = dr.F();
                if (kind == 2) dr.Skip(8);
                else if (kind == 3) dr.Skip(12);
                c.keys.emplace_back(t, val);
            }
            if (dr.bad) c.keys.clear();
        };
        const auto boolCurve = [&](std::int32_t interp, BoolCurve& c) {
            if (!typeIs(interp, "NiBoolInterpolator") && !typeIs(interp, "NiBoolTimelineInterpolator")) return;
            Reader             ir   = reader(interp);
            const std::uint8_t v    = ir.U8();
            const std::int32_t dref = ir.I32();
            if (v <= 1) c.keys = { { 0.0f, v != 0 } };
            if (!typeIs(dref, "NiBoolData")) return;
            Reader              dr  = reader(dref);
            const std::uint32_t num = dr.U32();
            if (num == 0 || num > 4096) return;
            const std::uint32_t kind = dr.U32();
            c.keys.clear();
            for (std::uint32_t k = 0; k < num && !dr.bad; ++k) {
                const float        t   = dr.F();
                const std::uint8_t val = dr.U8();
                if (kind == 3) dr.Skip(12);
                c.keys.emplace_back(t, val != 0);
            }
            if (dr.bad) c.keys.clear();
        };
        // A blend interpolator's own value, the last field of its block (a float for NiBlendFloatInterpolator, a byte
        // for NiBlendBoolInterpolator): what the controller holds while no sequence plays, usually the "no value"
        // sentinel (-FLT_MAX, 2).
        const auto blendFloat = [&](std::int32_t interp, float& value) {
            if (!typeIs(interp, "NiBlendFloatInterpolator") || blocks[interp].size < 4) return false;
            Reader ir = reader(interp);
            ir.o      = blocks[interp].offset + blocks[interp].size - 4;
            value     = ir.F();
            return !ir.bad && std::isfinite(value) && std::fabs(value) < 1.0e30f;
        };
        const auto blendBool = [&](std::int32_t interp, bool& value) {
            if (!typeIs(interp, "NiBlendBoolInterpolator") || blocks[interp].size < 1) return false;
            const std::uint8_t b = data[blocks[interp].offset + blocks[interp].size - 1];
            value                = b == 1;
            return b <= 1;
        };

        // NiSequence: name, its controlled blocks (interpolator, controller, priority, then five strings: node,
        // property type, controller type, controller id, interpolator id); then NiControllerSequence: weight, text
        // keys, cycle type, frequency, start and stop time (checked on vanilla and LoreRim manager-driven emitters).
        std::vector<Sequence> sequences;
        for (std::uint32_t bi = 0; bi < numBlocks; ++bi) {
            if (blocks[bi].type != "NiControllerSequence") continue;
            Reader              sr = reader(static_cast<std::int32_t>(bi));
            Sequence            q;
            q.name                  = str(sr);
            const std::uint32_t num = sr.U32();
            sr.U32();   // array grow by
            if (sr.bad || num > 4096) continue;
            for (std::uint32_t k = 0; k < num && !sr.bad; ++k) {
                Sequence::Block b;
                b.interpolator   = sr.I32();
                b.controller     = sr.I32();
                sr.U8();   // priority
                b.node           = str(sr);
                str(sr);   // property type
                b.controllerType = str(sr);
                b.controllerId   = str(sr);
                b.interpolatorId = str(sr);
                q.blocks.push_back(std::move(b));
            }
            sr.F();     // weight
            sr.I32();   // text keys
            q.cycle          = sr.U32();
            q.time.frequency = sr.F();
            q.time.start     = sr.F();
            q.time.stop      = sr.F();
            q.time.clamp     = q.cycle == 2;
            if (!sr.bad) sequences.push_back(std::move(q));
        }
        const auto isEmitterCtlr = [](const std::string& t) {
            return t == "NiPSysEmitterCtlr" || t == "BSPSysMultiTargetEmitterCtlr";
        };

        for (std::uint32_t bi = 0; bi < numBlocks; ++bi) {
            if (blocks[bi].type == "BSStripParticleSystem") {
                System s;
                s.name        = NameAt(r, strings, blocks[bi].offset);
                s.unsupported = Unsupported::kStripSystem;
                out.push_back(std::move(s));
                continue;
            }
            if (blocks[bi].type != "NiParticleSystem") continue;
            Reader rr = reader(static_cast<std::int32_t>(bi));
            System s;
            s.name = str(rr);
            rr.Skip(std::size_t(rr.U32()) * 4);   // extra data
            const std::int32_t controller = rr.I32();
            rr.U32();                // flags
            rr.Skip(4 * (3 + 9 + 1));   // translation, rotation, scale
            rr.I32();                // collision
            std::int32_t dataRef = -1;
            if (h.legacy) {
                // NiGeometry's: data, skin instance, material data, then the shader and alpha properties.
                dataRef = rr.I32();
                rr.I32();                                         // skin instance
                const std::uint32_t numMaterials = rr.U32();
                if (numMaterials > 64) rr.bad = true;
                rr.Skip(std::size_t(numMaterials) * 8);           // material names and extra data
                rr.I32();                                         // active material
                rr.U8();                                          // material needs update
                rr.I32();                                         // shader property
                rr.I32();                                         // alpha property
                rr.Skip(8);                                       // far/near begin/end
            } else {
                rr.Skip(16);             // bounding sphere
                rr.I32();                // skin
                rr.I32();                // shader property
                rr.I32();                // alpha property
                rr.Skip(8);              // vertex desc
                rr.Skip(8);              // far/near begin/end
                dataRef = rr.I32();
            }
            s.worldSpace                 = rr.U8() != 0;
            const std::uint32_t numMods  = rr.U32();
            std::vector<std::int32_t> mods(rr.bad || numMods > 256 ? 0 : numMods);
            for (auto& m : mods) m = rr.I32();
            if (rr.bad || numMods > 256) {
                s.unsupported = Unsupported::kUnreadable;
                out.push_back(std::move(s));
                continue;
            }

            std::string emitterName;
            bool        failed = false;
            for (const std::int32_t m : mods) {
                if (m < 0 || static_cast<std::size_t>(m) >= blocks.size()) continue;
                const std::string& t  = blocks[m].type;
                Reader             mr = reader(m);
                std::string        name;
                bool               active = true;
                if (t == "NiPSysBoxEmitter" || t == "NiPSysSphereEmitter" || t == "NiPSysCylinderEmitter" ||
                    t == "NiPSysMeshEmitter") {
                    modifierHead(mr, name, active);
                    Emitter e;
                    e.speed          = mr.F();
                    e.speedVar       = mr.F();
                    e.declination    = mr.F();
                    e.declinationVar = mr.F();
                    e.planar         = mr.F();
                    e.planarVar      = mr.F();
                    for (auto& c : e.color) c = mr.F();
                    e.radius    = mr.F();
                    e.radiusVar = mr.F();
                    e.life      = mr.F();
                    e.lifeVar   = mr.F();
                    if (t == "NiPSysMeshEmitter") {
                        // NiPSysEmitter's fields are followed by the meshes, not a volume emitter's node.
                        e.shape                 = EmitterShape::kMesh;
                        const std::uint32_t num = mr.U32();
                        for (std::uint32_t k = 0; k < num && k < 64 && !mr.bad; ++k) {
                            e.mesh.meshes.push_back(nodeName(mr.I32()));
                        }
                        if (num > 64) mr.bad = true;
                        e.mesh.velocityType = mr.U32();
                        e.mesh.emissionType = mr.U32();
                        for (auto& a : e.mesh.axis) a = mr.F();
                    } else {
                        e.object = nodeName(mr.I32());
                        if (t == "NiPSysBoxEmitter") {
                            e.shape = EmitterShape::kBox;
                            for (auto& d : e.dims) d = mr.F();
                        } else if (t == "NiPSysSphereEmitter") {
                            e.shape   = EmitterShape::kSphere;
                            e.dims[0] = mr.F();
                        } else {
                            e.shape   = EmitterShape::kCylinder;
                            e.dims[0] = mr.F();
                            e.dims[1] = mr.F();
                        }
                    }
                    if (mr.bad) {
                        failed = true;
                        break;
                    }
                    if (active && !s.hasEmitter) {
                        s.hasEmitter = true;
                        s.emitter    = e;
                        emitterName  = name;
                    }
                } else if (t == "BSPSysSimpleColorModifier") {
                    modifierHead(mr, name, active);
                    ColorFade c;
                    c.on      = active;
                    c.fadeIn  = mr.F();
                    c.fadeOut = mr.F();
                    c.c1End   = mr.F();
                    c.c1Start = mr.F();
                    c.c2End   = mr.F();
                    c.c2Start = mr.F();
                    for (auto& col : c.colors)
                        for (auto& v : col) v = mr.F();
                    if (!mr.bad && active) s.color = c;
                } else if (t == "BSPSysScaleModifier") {
                    modifierHead(mr, name, active);
                    const std::uint32_t n = mr.U32();
                    if (n > 4096) continue;
                    std::vector<float> v(n);
                    for (auto& x : v) x = mr.F();
                    if (!mr.bad && active) s.scales = std::move(v);
                } else if (t == "NiPSysGravityModifier") {
                    modifierHead(mr, name, active);
                    Gravity g;
                    g.name   = name;
                    g.object = nodeName(mr.I32());
                    for (auto& a : g.axis) a = mr.F();
                    g.decay           = mr.F();
                    g.strength        = mr.F();
                    g.forceType       = static_cast<int>(mr.U32());
                    g.turbulence      = mr.F();
                    g.turbulenceScale = mr.F();
                    g.worldAligned    = mr.U8() != 0;
                    if (!mr.bad && active) s.gravity.push_back(g);
                } else if (t == "NiPSysDragModifier") {
                    modifierHead(mr, name, active);
                    Drag d;
                    d.object = nodeName(mr.I32());
                    for (auto& a : d.axis) a = mr.F();
                    d.percentage = mr.F();
                    if (!mr.bad && active) s.drag.push_back(d);
                } else if (t == "NiPSysRotationModifier") {
                    modifierHead(mr, name, active);
                    Rotation ro;
                    ro.on         = active;
                    ro.speed      = mr.F();
                    ro.speedVar   = mr.F();
                    ro.angle      = mr.F();
                    ro.angleVar   = mr.F();
                    ro.randomSign = mr.U8() != 0;
                    if (!mr.bad && active) s.rotation = ro;
                } else if (t == "BSPSysSubTexModifier") {
                    modifierHead(mr, name, active);
                    SubTex st;
                    st.on    = active;
                    st.start = static_cast<float>(mr.U32());
                    mr.F();   // start fudge
                    st.end       = mr.F();
                    st.loopStart = mr.F();
                    mr.F();   // loop start fudge
                    st.frameCount = mr.F();
                    if (!mr.bad && active) s.subTex = st;
                }
            }
            if (failed || !s.hasEmitter) {
                s.unsupported = failed ? Unsupported::kUnreadable : Unsupported::kNoEmitter;
                out.push_back(std::move(s));
                continue;
            }
            if (s.emitter.shape == EmitterShape::kMesh) s.unsupported = Unsupported::kMeshEmitter;

            // The flipbook frames: a Skyrim NiPSysData (SE or LE) stores no particles, so its frame count sits at
            // byte 47, the frames follow, and 19 bytes close the block (checked on vanilla, Embers XD and LE Campfire
            // data).
            if (typeIs(dataRef, "NiPSysData")) {
                const auto& db = blocks[dataRef];
                Reader      dr{ data, db.offset + db.size, db.offset + 47 };
                const std::uint32_t count = dr.U32();
                if (!dr.bad && count <= 256 && 47 + 4 + std::size_t(count) * 16 + 19 == db.size) {
                    s.frames.resize(count);
                    for (auto& f : s.frames)
                        for (auto& v : f) v = dr.F();
                    if (dr.bad) s.frames.clear();
                }
            }

            // The controllers: the emitter controller names the emitter and holds its birth rate and visibility.
            std::int32_t c     = controller;
            int          guard = 0;
            while (c >= 0 && static_cast<std::size_t>(c) < blocks.size() && guard++ < 64) {
                Reader              cr   = reader(c);
                const std::int32_t  next = cr.I32();
                const std::uint16_t fl   = cr.U16();
                ControllerTime      ct;
                ct.frequency = cr.F();
                ct.phase     = cr.F();
                ct.start     = cr.F();
                ct.stop      = cr.F();
                ct.clamp     = ((fl >> 1) & 3) == 2;
                cr.I32();   // target
                if (isEmitterCtlr(blocks[c].type)) {
                    const std::int32_t interp = cr.I32();
                    const std::string  mod    = str(cr);
                    const std::int32_t vis    = cr.I32();
                    if (!cr.bad && (mod == emitterName || emitterName.empty())) {
                        s.emitTime = ct;
                        floatCurve(interp, s.birthRate);
                        boolCurve(vis, s.visible);
                        // A manager-driven controller (blend interpolators): its blend's own value when it holds one,
                        // else the tracks of the sequence the view shows (ChooseSequence), on that sequence's time.
                        float      rate        = 0.0f;
                        bool       on          = true;
                        const bool blendedRate = typeIs(interp, "NiBlendFloatInterpolator");
                        const bool blendedVis  = typeIs(vis, "NiBlendBoolInterpolator");
                        const bool rateHeld    = blendedRate && blendFloat(interp, rate);
                        const bool visHeld     = blendedVis && blendBool(vis, on);
                        if (rateHeld) s.birthRate.constant = rate;
                        if (visHeld) s.visible.keys = { { 0.0f, on } };
                        if ((blendedRate && !rateHeld) || (blendedVis && !visHeld)) {
                            const auto drives = [&](const Sequence::Block& b) {
                                if (b.controller >= 0) return b.controller == c;
                                return isEmitterCtlr(b.controllerType) && b.node == s.name && b.controllerId == mod;
                            };
                            std::vector<const Sequence*> candidates;
                            for (const auto& q : sequences) {
                                if (std::any_of(q.blocks.begin(), q.blocks.end(), drives)) candidates.push_back(&q);
                            }
                            if (const int pick = ChooseSequence(candidates); pick >= 0) {
                                const Sequence& q = *candidates[static_cast<std::size_t>(pick)];
                                s.sequence        = q.name;
                                s.emitTime        = q.time;
                                for (const auto& b : q.blocks) {
                                    if (!drives(b)) continue;
                                    if (b.interpolatorId == "BirthRate" && blendedRate && !rateHeld) {
                                        floatCurve(b.interpolator, s.birthRate);
                                    } else if (b.interpolatorId == "EmitterActive" && blendedVis && !visHeld) {
                                        boolCurve(b.interpolator, s.visible);
                                    }
                                }
                                ApplySequenceEmitterValues(q, s, floatCurve);
                            }
                        }
                    }
                }
                if (cr.bad) break;
                c = next;
            }
            // A birth rate left at the "no value" sentinel with no keys emits nothing.
            if (s.birthRate.keys.empty() && !(std::fabs(s.birthRate.constant) < 1.0e30f)) s.birthRate.constant = 0.0f;
            out.push_back(std::move(s));
        }
        return out;
    }

    /// A node's local transform as the file authors it (NiAVObject): translation, rotation (rows, as NiMatrix3)
    /// and scale. What a controller moves at runtime (a lag bone's trail) is not in it.
    struct NodeLocal
    {
        std::string          name;
        std::array<float, 3> translate{};
        std::array<float, 9> rotate{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        float                scale = 1.0f;
    };

    /// Every node block's local transform (a block whose type ends in "Node"), in block order; empty when the file
    /// is not a Skyrim NIF. A node block that does not read is left out. `verdict` as Parse's.
    inline std::vector<NodeLocal> ParseNodeLocals(const std::uint8_t* data, std::size_t size,
                                                  FileVerdict* verdict = nullptr)
    {
        using namespace detail;
        std::vector<NodeLocal> out;
        FileVerdict            dummy;
        Header                 h;
        if (!ReadHeader(data, size, h, verdict ? *verdict : dummy)) return out;
        for (const auto& b : h.blocks) {
            if (b.type.size() < 4 || b.type.compare(b.type.size() - 4, 4, "Node") != 0) continue;
            Reader             rr{ data, b.offset + b.size, b.offset };
            const std::int32_t name = rr.I32();
            rr.Skip(std::size_t(rr.U32()) * 4);   // extra data
            rr.I32();                             // controller
            rr.U32();                             // flags
            NodeLocal n;
            for (auto& f : n.translate) f = rr.F();
            for (auto& f : n.rotate) f = rr.F();
            n.scale = rr.F();
            if (rr.bad) continue;
            n.name = name >= 0 && static_cast<std::size_t>(name) < h.strings.size() ? h.strings[name] : std::string{};
            out.push_back(std::move(n));
        }
        return out;
    }
}
