// Phantasm - native self-tests: NifParticleCore (reading a NIF's particle systems) and ParticleSimCore
// (running one), over small NIFs built here block by block (the managed and unsupported ones with the numbers of
// real LoreRim meshes).
//
// Author: Severause

#include "NifParticleCore.h"
#include "ParticleSimCore.h"
#include "TestHarness.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace Phantasm::Tests
{
    namespace
    {
        namespace NPC = Phantasm::NifParticleCore;
        namespace PSC = Phantasm::ParticleSimCore;

        // A Skyrim NIF (SE, or LE with bsVersion 83) assembled in memory: types, strings and blocks, written with the
        // real header layout.
        struct NifBuilder
        {
            std::vector<std::string> types, strings;
            struct Block
            {
                std::uint16_t             type = 0;
                std::vector<std::uint8_t> bytes;
            };
            std::vector<Block> blocks;
            std::uint32_t      bsVersion = 100;   // 83 writes a Skyrim LE header

            std::int32_t Str(const std::string& s)
            {
                for (std::size_t i = 0; i < strings.size(); ++i)
                    if (strings[i] == s) return static_cast<std::int32_t>(i);
                strings.push_back(s);
                return static_cast<std::int32_t>(strings.size() - 1);
            }
            Block& Add(const std::string& type)
            {
                std::uint16_t t = 0;
                for (; t < types.size() && types[t] != type; ++t) {}
                if (t == types.size()) types.push_back(type);
                blocks.push_back({ t, {} });
                return blocks.back();
            }
            template <class T>
            static void Put(std::vector<std::uint8_t>& b, T v)
            {
                const auto* p = reinterpret_cast<const std::uint8_t*>(&v);
                b.insert(b.end(), p, p + sizeof(T));
            }
            static void Floats(std::vector<std::uint8_t>& b, std::initializer_list<float> fs)
            {
                for (float f : fs) Put(b, f);
            }
            std::vector<std::uint8_t> Build() const
            {
                std::vector<std::uint8_t> b;
                const char*               head = "Gamebryo File Format, Version 20.2.0.7\n";
                b.insert(b.end(), head, head + std::strlen(head));
                Put<std::uint32_t>(b, 0x14020007);
                Put<std::uint8_t>(b, 1);
                Put<std::uint32_t>(b, 12);
                Put<std::uint32_t>(b, static_cast<std::uint32_t>(blocks.size()));
                Put<std::uint32_t>(b, bsVersion);
                for (int i = 0; i < 3; ++i) Put<std::uint8_t>(b, 0);
                Put<std::uint16_t>(b, static_cast<std::uint16_t>(types.size()));
                for (const auto& t : types) {
                    Put<std::uint32_t>(b, static_cast<std::uint32_t>(t.size()));
                    b.insert(b.end(), t.begin(), t.end());
                }
                for (const auto& bl : blocks) Put<std::uint16_t>(b, bl.type);
                for (const auto& bl : blocks) Put<std::uint32_t>(b, static_cast<std::uint32_t>(bl.bytes.size()));
                Put<std::uint32_t>(b, static_cast<std::uint32_t>(strings.size()));
                Put<std::uint32_t>(b, 64);
                for (const auto& s : strings) {
                    Put<std::uint32_t>(b, static_cast<std::uint32_t>(s.size()));
                    b.insert(b.end(), s.begin(), s.end());
                }
                Put<std::uint32_t>(b, 0);
                for (const auto& bl : blocks) b.insert(b.end(), bl.bytes.begin(), bl.bytes.end());
                return b;
            }
        };

        void Modifier(NifBuilder& n, std::vector<std::uint8_t>& b, const char* name)
        {
            NifBuilder::Put<std::int32_t>(b, n.Str(name));
            NifBuilder::Put<std::uint32_t>(b, 1000);
            NifBuilder::Put<std::int32_t>(b, 1);
            NifBuilder::Put<std::uint8_t>(b, 1);
        }

        // 0 emitter node, 1 the system, 2 a box emitter, 3 colour, 4 scale, 5 the emitter controller, 6/7 its rising
        // birth rate (10 to 30 a second over 2 s), 8 planar gravity, 9 the data with two flipbook frames.
        // multiTarget: the controller is a BSPSysMultiTargetEmitterCtlr (NiPSysEmitterCtlr's fields, then the most
        // emitters and the master system). legacy: an LE file, its system laid out as NiGeometry (as Campfire's torch,
        // with one material).
        std::vector<std::uint8_t> SampleNif(bool multiTarget = false, bool legacy = false)
        {
            NifBuilder n;
            using B = NifBuilder;
            if (legacy) n.bsVersion = 83;
            {
                auto& b = n.Add("NiNode").bytes;
                B::Put<std::int32_t>(b, n.Str("Fire-Emitter"));
            }
            {
                auto& b = n.Add("NiParticleSystem").bytes;
                B::Put<std::int32_t>(b, n.Str("Fire"));
                B::Put<std::uint32_t>(b, 0);   // extra data
                B::Put<std::int32_t>(b, 5);    // controller
                B::Put<std::uint32_t>(b, 0);   // flags
                for (int i = 0; i < 13; ++i) B::Put<float>(b, i == 3 || i == 7 || i == 11 || i == 12 ? 1.0f : 0.0f);
                B::Put<std::int32_t>(b, -1);   // collision
                if (legacy) {
                    B::Put<std::int32_t>(b, 9);    // data
                    B::Put<std::int32_t>(b, -1);   // skin instance
                    B::Put<std::uint32_t>(b, 1);   // one material: name, extra data
                    B::Put<std::int32_t>(b, n.Str("Material"));
                    B::Put<std::int32_t>(b, -1);
                    B::Put<std::int32_t>(b, 0);    // active material
                    B::Put<std::uint8_t>(b, 1);    // needs update
                    B::Put<std::int32_t>(b, -1);   // shader
                    B::Put<std::int32_t>(b, -1);   // alpha
                    B::Put<std::uint64_t>(b, 0);   // far/near
                } else {
                    B::Floats(b, { 0, 0, 0, 10 });
                    B::Put<std::int32_t>(b, -1);
                    B::Put<std::int32_t>(b, -1);
                    B::Put<std::int32_t>(b, -1);
                    B::Put<std::uint64_t>(b, 0);
                    B::Put<std::uint64_t>(b, 0);
                    B::Put<std::int32_t>(b, 9);    // data
                }
                B::Put<std::uint8_t>(b, 0);    // world space
                B::Put<std::uint32_t>(b, 4);
                for (std::int32_t m : { 2, 3, 4, 8 }) B::Put<std::int32_t>(b, m);
            }
            {
                auto& b = n.Add("NiPSysBoxEmitter").bytes;
                Modifier(n, b, "Emit");
                B::Floats(b, { 10, 0, 0, 0, 0, 0, 1, 0.5f, 0.25f, 1, 5, 0, 1, 0 });
                B::Put<std::int32_t>(b, 0);
                B::Floats(b, { 2, 2, 2 });
            }
            {
                auto& b = n.Add("BSPSysSimpleColorModifier").bytes;
                Modifier(n, b, "Color");
                B::Floats(b, { 0.2f, 0.8f, 0.1f, 0.4f, 0.6f, 0.9f });
                B::Floats(b, { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0 });
            }
            {
                auto& b = n.Add("BSPSysScaleModifier").bytes;
                Modifier(n, b, "Scale");
                B::Put<std::uint32_t>(b, 2);
                B::Floats(b, { 0.5f, 1.5f });
            }
            {
                auto& b = n.Add(multiTarget ? "BSPSysMultiTargetEmitterCtlr" : "NiPSysEmitterCtlr").bytes;
                B::Put<std::int32_t>(b, -1);
                B::Put<std::uint16_t>(b, 0x48);
                B::Floats(b, { 1, 0, 0, 2 });
                B::Put<std::int32_t>(b, 1);
                B::Put<std::int32_t>(b, 6);
                B::Put<std::int32_t>(b, n.Str("Emit"));
                B::Put<std::int32_t>(b, -1);
                if (multiTarget) {
                    B::Put<std::uint16_t>(b, 8);
                    B::Put<std::int32_t>(b, -1);
                }
            }
            {
                auto& b = n.Add("NiFloatInterpolator").bytes;
                B::Put<float>(b, 3.4028234e38f);
                B::Put<std::int32_t>(b, 7);
            }
            {
                auto& b = n.Add("NiFloatData").bytes;
                B::Put<std::uint32_t>(b, 2);
                B::Put<std::uint32_t>(b, 1);
                B::Floats(b, { 0, 10, 2, 30 });
            }
            {
                auto& b = n.Add("NiPSysGravityModifier").bytes;
                Modifier(n, b, "Gravity");
                B::Put<std::int32_t>(b, 0);
                B::Floats(b, { 0, 0, 1, 0, 5 });
                B::Put<std::uint32_t>(b, 0);
                B::Floats(b, { 0, 1 });
                B::Put<std::uint8_t>(b, 1);
            }
            {
                auto& b = n.Add("NiPSysData").bytes;
                b.resize(47, 0);
                B::Put<std::uint32_t>(b, 2);
                B::Floats(b, { 0, 0.5f, 0, 1, 0.5f, 0.5f, 0, 1 });
                b.resize(b.size() + 19, 0);
            }
            return n.Build();
        }

        bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

        // A NiParticleSystem block with one modifier and the controller chain at `controller`.
        void ParticleSystem(NifBuilder& n, const char* name, std::int32_t controller, std::int32_t modifier)
        {
            using B = NifBuilder;
            auto& b = n.Add("NiParticleSystem").bytes;
            B::Put<std::int32_t>(b, n.Str(name));
            B::Put<std::uint32_t>(b, 0);
            B::Put<std::int32_t>(b, controller);
            B::Put<std::uint32_t>(b, 0x8000E);
            for (int i = 0; i < 13; ++i) B::Put<float>(b, i == 3 || i == 7 || i == 11 || i == 12 ? 1.0f : 0.0f);
            B::Put<std::int32_t>(b, -1);
            B::Floats(b, { 0, 0, 0, 10 });
            B::Put<std::int32_t>(b, -1);
            B::Put<std::int32_t>(b, -1);
            B::Put<std::int32_t>(b, -1);
            B::Put<std::uint64_t>(b, 0);
            B::Put<std::uint64_t>(b, 0);
            B::Put<std::int32_t>(b, -1);   // no data
            B::Put<std::uint8_t>(b, 0);
            B::Put<std::uint32_t>(b, 1);
            B::Put<std::int32_t>(b, modifier);
        }

        // A sequence's two controlled blocks for an emitter controller: the birth rate and the emitter's visibility.
        struct SeqSpec
        {
            const char*  name;
            std::uint32_t cycle;
            float        stop;
            std::int32_t rateInterp, visInterp;
            std::int32_t controller;   // -1: matched by name, as a file whose blocks name no controller
        };
        void ControllerSequence(NifBuilder& n, const SeqSpec& q, const char* system, const char* emitter)
        {
            using B = NifBuilder;
            auto& b = n.Add("NiControllerSequence").bytes;
            B::Put<std::int32_t>(b, n.Str(q.name));
            B::Put<std::uint32_t>(b, 2);
            B::Put<std::uint32_t>(b, 1);
            for (int k = 0; k < 2; ++k) {
                B::Put<std::int32_t>(b, k == 0 ? q.rateInterp : q.visInterp);
                B::Put<std::int32_t>(b, q.controller);
                B::Put<std::uint8_t>(b, 0);
                B::Put<std::int32_t>(b, n.Str(system));
                B::Put<std::int32_t>(b, -1);
                B::Put<std::int32_t>(b, n.Str("NiPSysEmitterCtlr"));
                B::Put<std::int32_t>(b, n.Str(emitter));
                B::Put<std::int32_t>(b, n.Str(k == 0 ? "BirthRate" : "EmitterActive"));
            }
            B::Floats(b, { 1.0f });
            B::Put<std::int32_t>(b, -1);
            B::Put<std::uint32_t>(b, q.cycle);
            B::Floats(b, { 1.0f, 0.0f, q.stop });
            B::Put<std::int32_t>(b, 0);
            B::Put<std::int32_t>(b, -1);
            B::Put<std::uint16_t>(b, 0);
        }

        // A manager-driven emitter as LoreRim's Riilungolz staff (CrystalAsorbcastpointfx.nif) has it: the
        // controller's interpolators are blends holding the "no value" sentinels, and three sequences drive it,
        // mBegin (clamp, 0.0667 s), mLoop (loop, 0.8333 s, 30 a second) and mEnd (clamp, the rate falls to 0).
        // kBeehive: Rally's beehive01.nif instead: AmIdle (clamp, rate 30, emitter off) and Burning (loop, on), its
        // blocks naming no controller. kHeld: the staff's sequences, but the blends hold values of their own (12 a
        // second, emitter off) instead of the sentinels.
        enum class Managed
        {
            kSequences,
            kBeehive,
            kHeld,
        };
        std::vector<std::uint8_t> ManagedNif(Managed kind)
        {
            const bool beehive = kind == Managed::kBeehive;
            NifBuilder n;
            using B = NifBuilder;
            {
                auto& b = n.Add("NiNode").bytes;   // 0
                B::Put<std::int32_t>(b, n.Str("PCloud03-Emitter"));
            }
            ParticleSystem(n, "PCloud03", 2, 3);   // 1
            {
                auto& b = n.Add("NiPSysEmitterCtlr").bytes;   // 2
                B::Put<std::int32_t>(b, -1);
                B::Put<std::uint16_t>(b, 0x68);
                B::Floats(b, { 1.0f, 35.029144f, 0.0f, 13.866667f });
                B::Put<std::int32_t>(b, 1);
                B::Put<std::int32_t>(b, 4);
                B::Put<std::int32_t>(b, n.Str("NiPSysCylinderEmitter:0"));
                B::Put<std::int32_t>(b, 5);
            }
            {
                auto& b = n.Add("NiPSysCylinderEmitter").bytes;   // 3
                Modifier(n, b, "NiPSysCylinderEmitter:0");
                B::Floats(b, { 10, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 0, 1, 0 });
                B::Put<std::int32_t>(b, 0);
                B::Floats(b, { 3, 4 });
            }
            if (kind == Managed::kHeld) {
                n.Add("NiBlendFloatInterpolator").bytes = { 0x01, 0x02, 0, 0, 0, 0, 0x00, 0x00, 0x40, 0x41 };   // 4: 12
                n.Add("NiBlendBoolInterpolator").bytes  = { 0x01, 0x02, 0, 0, 0, 0, 0x00 };                     // 5: off
            } else {
                n.Add("NiBlendFloatInterpolator").bytes = { 0x01, 0x02, 0, 0, 0, 0, 0xFF, 0xFF, 0x7F, 0xFF };   // 4
                n.Add("NiBlendBoolInterpolator").bytes  = { 0x01, 0x02, 0, 0, 0, 0, 0x02 };                     // 5
            }
            // A float interpolator holding `value`, or reading the NiFloatData that follows it.
            const auto floatInterp = [&](float value, std::int32_t data) {
                auto& b = n.Add("NiFloatInterpolator").bytes;
                B::Put<float>(b, value);
                B::Put<std::int32_t>(b, data);
            };
            const auto boolInterp = [&](std::uint8_t value) {
                auto& b = n.Add("NiBoolInterpolator").bytes;
                B::Put<std::uint8_t>(b, value);
                B::Put<std::int32_t>(b, -1);
            };
            if (beehive) {
                floatInterp(30.0f, -1);   // 6 AmIdle's rate
                boolInterp(0);            // 7 AmIdle: off
                floatInterp(30.0f, -1);   // 8 Burning's rate
                boolInterp(1);            // 9 Burning: on
                ControllerSequence(n, { "AmIdle", 2, 0.033333f, 6, 7, -1 }, "PCloud03", "NiPSysCylinderEmitter:0");
                ControllerSequence(n, { "Burning", 0, 3.966666f, 8, 9, -1 }, "PCloud03", "NiPSysCylinderEmitter:0");
                return n.Build();
            }
            floatInterp(30.0f, -1);           // 6 mBegin's rate
            boolInterp(1);                    // 7
            floatInterp(-3.4028235e38f, 9);   // 8 mLoop's rate, from its data
            {
                auto& b = n.Add("NiFloatData").bytes;   // 9: quadratic keys (time, value, forward, backward)
                B::Put<std::uint32_t>(b, 2);
                B::Put<std::uint32_t>(b, 2);
                B::Floats(b, { 0.0f, 30.0f, 0.0f, 0.0f, 0.8333333f, 30.0f, 0.0f, 0.0f });
            }
            boolInterp(1);                    // 10
            floatInterp(0.0f, -1);            // 11 mEnd's rate
            boolInterp(0);                    // 12
            ControllerSequence(n, { "mBegin", 2, 0.0666667f, 6, 7, 2 }, "PCloud03", "NiPSysCylinderEmitter:0");
            ControllerSequence(n, { "mLoop", 0, 0.8333333f, 8, 10, 2 }, "PCloud03", "NiPSysCylinderEmitter:0");
            ControllerSequence(n, { "mEnd", 2, 1.0333433f, 11, 12, 2 }, "PCloud03", "NiPSysCylinderEmitter:0");
            return n.Build();
        }

        // The systems the reader names but the renderer cannot run, with LoreRim's numbers: Animated Armoury's ice whip
        // (IcicleWhipParticleOld.nif) emits 'PBig' from the vertices of the skinned blade 'BlizzariaWhip_Ice_D' along
        // their normals, and the Phoenix Glave's 'pStreaks' is a strip system.
        std::vector<std::uint8_t> UnsupportedNif()
        {
            NifBuilder n;
            using B = NifBuilder;
            {
                auto& b = n.Add("BSTriShape").bytes;   // 0
                B::Put<std::int32_t>(b, n.Str("BlizzariaWhip_Ice_D"));
            }
            ParticleSystem(n, "PBig", -1, 2);   // 1
            {
                auto& b = n.Add("NiPSysMeshEmitter").bytes;   // 2
                Modifier(n, b, "BigMeshi");
                B::Floats(b, { 1, 2, 0, 3.14f, 0, 6.28f, 1, 1, 1, 1, 24, 2.4f, 3, 0.26f });
                B::Put<std::uint32_t>(b, 1);
                B::Put<std::int32_t>(b, 0);
                B::Put<std::uint32_t>(b, 0);   // velocity: the normals
                B::Put<std::uint32_t>(b, 0);   // emission: the vertices
                B::Floats(b, { 1, 0, 0 });
            }
            {
                auto& b = n.Add("BSStripParticleSystem").bytes;   // 3
                B::Put<std::int32_t>(b, n.Str("pStreaks"));
                b.resize(169, 0);
            }
            return n.Build();
        }
    }

    void RunNifParticleCoreTests()
    {
        const auto bytes   = SampleNif();
        const auto systems = NPC::Parse(bytes.data(), bytes.size());
        Check(systems.size() == 1, "nif particles: one system is read");
        if (systems.size() != 1) return;
        const auto& s = systems[0];
        Check(s.name == "Fire" && !s.worldSpace && s.hasEmitter && s.emitter.object == "Fire-Emitter" &&
                  s.emitter.shape == NPC::EmitterShape::kBox && Near(s.emitter.speed, 10.0f) &&
                  Near(s.emitter.radius, 5.0f) && Near(s.emitter.dims[0], 2.0f),
              "nif particles: the emitter, its node and its shape");
        Check(s.color.on && Near(s.color.c2Start, 0.9f) && Near(s.color.colors[1][1], 1.0f) && s.scales.size() == 2 &&
                  s.gravity.size() == 1 && Near(s.gravity[0].strength, 5.0f) && s.gravity[0].worldAligned &&
                  s.gravity[0].name == "Gravity",
              "nif particles: the colour, scale and gravity modifiers");
        Check(s.frames.size() == 2 && Near(s.frames[1][0], 0.5f) && Near(s.frames[1][1], 0.5f),
              "nif particles: the flipbook frames are read from the system's data");
        Check(Near(s.birthRate.Eval(0.0f), 10.0f) && Near(s.birthRate.Eval(1.0f), 20.0f) &&
                  Near(s.birthRate.Eval(5.0f), 30.0f) && Near(s.emitTime.Scaled(2.5f), 0.5f),
              "nif particles: the birth rate follows its keys over the controller's loop");

        Check(NPC::Parse(bytes.data(), 40).empty() && NPC::Parse(bytes.data(), bytes.size() - 20).size() <= 1,
              "nif particles: a short buffer reads nothing or what it can, without running off the end");
        auto wrong = bytes;
        wrong[std::strlen("Gamebryo File Format, Version 20.2.0.7\n") + 5] = 11;   // user version 11
        Check(NPC::Parse(wrong.data(), wrong.size()).empty(), "nif particles: only Skyrim NIFs are read");
        {
            NPC::FileVerdict read = NPC::FileVerdict::kUnreadable, other = read, cut = NPC::FileVerdict::kRead;
            NPC::Parse(bytes.data(), bytes.size(), &read);
            NPC::Parse(wrong.data(), wrong.size(), &other);
            NPC::Parse(bytes.data(), 40, &cut);
            Check(read == NPC::FileVerdict::kRead && other == NPC::FileVerdict::kNotSkyrim &&
                      cut == NPC::FileVerdict::kUnreadable,
                  "nif particles: the file's verdict tells another game's NIF from a broken one");
            Check(s.unsupported == NPC::Unsupported::kNone && s.sequence.empty(),
                  "nif particles: a plain system is supported and driven by its own controller");
        }
        {
            const auto multi = SampleNif(true);
            const auto ms    = NPC::Parse(multi.data(), multi.size());
            Check(ms.size() == 1 && Near(ms[0].birthRate.Eval(1.0f), 20.0f),
                  "nif particles: a multi-target emitter controller gives the birth rate as the plain one does");
        }
        {
            const auto       le = SampleNif(false, true);
            NPC::FileVerdict v  = NPC::FileVerdict::kUnreadable;
            const auto       ls = NPC::Parse(le.data(), le.size(), &v);
            Check(v == NPC::FileVerdict::kRead && ls.size() == 1 && ls[0].unsupported == NPC::Unsupported::kNone &&
                      ls[0].emitter.object == "Fire-Emitter" && ls[0].frames.size() == 2 && ls[0].gravity.size() == 1 &&
                      Near(ls[0].birthRate.Eval(1.0f), 20.0f),
                  "nif particles: an LE NIF's system reads as the SE one does, through its NiGeometry layout");
        }

        // Manager-driven emitters: the blends hold no value, so the sequence the view shows gives the rate.
        {
            const auto managed = ManagedNif(Managed::kSequences);
            const auto ms      = NPC::Parse(managed.data(), managed.size());
            Check(ms.size() == 1 && ms[0].unsupported == NPC::Unsupported::kNone && ms[0].sequence == "mLoop",
                  "nif particles: a manager-driven emitter plays the looping sequence between a begin and an end");
            if (ms.size() == 1) {
                const auto& m = ms[0];
                Check(Near(m.birthRate.Eval(0.4f), 30.0f) && m.visible.Eval(0.4f) && !m.emitTime.clamp &&
                          Near(m.emitTime.stop, 0.8333f) && Near(m.emitTime.Scaled(1.0f), 1.0f - 0.8333f) &&
                          Near(m.emitTime.phase, 0.0f),
                      "nif particles: its rate (30 a second) and visibility, on the sequence's time, not the "
                      "controller's");
            }
            const auto hive = ManagedNif(Managed::kBeehive);
            const auto hs   = NPC::Parse(hive.data(), hive.size());
            Check(hs.size() == 1 && hs[0].sequence == "AmIdle" && !hs[0].visible.Eval(0.0f) && hs[0].emitTime.clamp,
                  "nif particles: an idle sequence wins (a beehive at rest does not smoke), matched by name when the "
                  "file names no controller");
            const auto held = ManagedNif(Managed::kHeld);
            const auto hd   = NPC::Parse(held.data(), held.size());
            Check(hd.size() == 1 && hd[0].sequence.empty() && Near(hd[0].birthRate.Eval(0.4f), 12.0f) &&
                      hd[0].birthRate.keys.empty() && !hd[0].visible.Eval(0.4f) && Near(hd[0].emitTime.stop, 13.8667f) &&
                      Near(hd[0].emitTime.phase, 35.029f),
                  "nif particles: blends holding values of their own give the rate and visibility, on the "
                  "controller's time, whatever the sequences say");
        }
        {
            NPC::Sequence begin{ "mBegin", 2 }, loop{ "mLoop", 0 }, end{ "mEnd", 2 }, idle{ "amidle", 2 };
            Check(NPC::ChooseSequence({ &begin, &loop, &end }) == 1 && NPC::ChooseSequence({ &begin, &end }) == 0 &&
                      NPC::ChooseSequence({ &loop, &idle }) == 1 && NPC::ChooseSequence({}) == -1,
                  "nif particles: the sequence shown: an idle by name in any case, else the first loop, else the first");
        }

        // Systems read but not run, each with why.
        {
            const auto un = UnsupportedNif();
            const auto us = NPC::Parse(un.data(), un.size());
            const NPC::System* whip   = nullptr;
            const NPC::System* streak = nullptr;
            for (const auto& x : us) {
                if (x.name == "PBig") whip = &x;
                if (x.name == "pStreaks") streak = &x;
            }
            Check(whip && whip->unsupported == NPC::Unsupported::kMeshEmitter &&
                      whip->emitter.shape == NPC::EmitterShape::kMesh && whip->emitter.mesh.meshes.size() == 1 &&
                      whip->emitter.mesh.meshes[0] == "BlizzariaWhip_Ice_D" && whip->emitter.mesh.velocityType == 0 &&
                      whip->emitter.mesh.emissionType == 0 && Near(whip->emitter.mesh.axis[0], 1.0f) &&
                      Near(whip->emitter.radius, 24.0f) && Near(whip->emitter.life, 3.0f),
                  "nif particles: a mesh emitter is read to its last field and marked as not simulated");
            Check(streak && streak->unsupported == NPC::Unsupported::kStripSystem,
                  "nif particles: a strip system is named and marked as not simulated");
        }

        // The simulation: emits at the rate over the first second, ages out, rises, and colours and sizes by life.
        PSC::Sim sim(s, 1234);
        PSC::Env env;
        env.maxParticles = 64;
        for (int i = 0; i < 30; ++i) sim.Step(1.0f / 30.0f, env);
        Check(sim.Live() >= 11 && sim.Live() <= 17, "particle sim: about fifteen born in the first second");
        std::vector<PSC::Drawn> drawn;
        sim.Collect(drawn);
        bool rising = !drawn.empty();
        for (const auto& d : drawn) rising = rising && d.pos[2] > -1.01f && d.halfSize > 0.0f;
        Check(rising, "particle sim: particles move up out of the emitter and have a size");
        for (int i = 0; i < 120; ++i) sim.Step(1.0f / 30.0f, env);
        Check(sim.Live() <= 32, "particle sim: particles die at the end of their life");

        float c[4];
        sim.ColorAt(0.05f, c);
        Check(Near(c[0], 1.0f) && Near(c[3], 0.25f), "particle sim: the first colour, faded in");
        sim.ColorAt(0.5f, c);
        Check(Near(c[1], 1.0f) && Near(c[3], 1.0f), "particle sim: the second colour, held");
        sim.ColorAt(0.95f, c);
        Check(Near(c[2], 1.0f) && Near(c[3], 0.0f), "particle sim: the third colour, faded out with its own alpha");
        Check(Near(sim.ScaleAt(0.0f), 0.5f) && Near(sim.ScaleAt(0.5f), 1.0f) && Near(sim.ScaleAt(1.0f), 1.5f),
              "particle sim: the size follows the scale curve");

        PSC::Sim capped(s, 99);
        PSC::Env few;
        few.maxParticles = 3;
        for (int i = 0; i < 60; ++i) capped.Step(1.0f / 30.0f, few);
        Check(capped.Live() <= 3, "particle sim: never more than the system has room for");

        // A local-space system's particles go with its node: moved 100 along x, turned a quarter about z, doubled.
        {
            PSC::Sim carried(s, 4321);
            PSC::Env at;
            at.maxParticles = 16;
            for (int i = 0; i < 15; ++i) carried.Step(1.0f / 30.0f, at);
            std::vector<PSC::Drawn> before, after;
            carried.Collect(before);
            PSC::Frame from, to;
            to.pos[0] = 100.0f;
            to.x[0] = 0.0f, to.x[1] = 1.0f;    // x -> y
            to.y[0] = -1.0f, to.y[1] = 0.0f;   // y -> -x
            to.scale = 2.0f;
            carried.Carry(from, to);
            carried.Collect(after);
            bool moved = !before.empty() && before.size() == after.size();
            for (std::size_t i = 0; moved && i < before.size(); ++i) {
                const auto& b = before[i];
                const auto& a = after[i];
                moved = Near(a.pos[0], 100.0f - b.pos[1] * 2.0f) && Near(a.pos[1], b.pos[0] * 2.0f) &&
                        Near(a.pos[2], b.pos[2] * 2.0f) && Near(a.halfSize, b.halfSize * 2.0f);
            }
            Check(moved, "particle sim: a carried particle keeps its place, size and scale in its node's frame");
            PSC::Frame flat, doubled;
            flat.scale    = 0.0f;
            doubled.scale = 2.0f;
            carried.Carry(flat, doubled);
            std::vector<PSC::Drawn> again;
            carried.Collect(again);
            bool kept = !again.empty() && again.size() == after.size();
            for (std::size_t i = 0; kept && i < again.size(); ++i) {
                kept = Near(again[i].pos[0], after[i].pos[0]) && Near(again[i].pos[1], after[i].pos[1]) &&
                       Near(again[i].pos[2], after[i].pos[2]) && Near(again[i].halfSize, after[i].halfSize);
            }
            Check(kept, "particle sim: a carry from a degenerate frame keeps the place and the size");
        }

        // Node transforms, as the Dragonbane streak authors its lag bone (2.1u off its parent; the skin binds it
        // 19.9u off, the trail at full stretch).
        {
            NifBuilder n;
            using B             = NifBuilder;
            const auto nodeBlock = [&](const char* type, const char* name, float tx, float ty, float scale) {
                auto& b = n.Add(type).bytes;
                B::Put<std::int32_t>(b, n.Str(name));
                B::Put<std::uint32_t>(b, 0);    // extra data
                B::Put<std::int32_t>(b, -1);    // controller
                B::Put<std::uint32_t>(b, 14);   // flags
                B::Floats(b, { tx, ty, 0.0f, 1, 0, 0, 0, 1, 0, 0, 0, 1, scale });
                B::Put<std::int32_t>(b, -1);    // collision
                B::Put<std::uint32_t>(b, 0);    // children
                B::Put<std::uint32_t>(b, 0);    // effects
            };
            nodeBlock("BSFadeNode", "FXDragonsBaneSwordStreak.nif", 0.0f, 0.0f, 1.0f);
            nodeBlock("NiNode", "LagBone", 2.1f, 0.0f, 1.0f);
            {
                auto& b = n.Add("NiAlphaProperty").bytes;   // not a node
                B::Put<std::int32_t>(b, -1);
                b.resize(14, 0);
            }
            nodeBlock("NiNode", "LagBone01", 1.84f, 0.5f, 0.75f);
            const auto  nif   = n.Build();
            auto        v     = NPC::FileVerdict::kUnreadable;
            const auto  nodes = NPC::ParseNodeLocals(nif.data(), nif.size(), &v);
            Check(v == NPC::FileVerdict::kRead && nodes.size() == 3, "nif nodes: every node block is read, others not");
            if (nodes.size() == 3) {
                Check(nodes[1].name == "LagBone" && std::fabs(nodes[1].translate[0] - 2.1f) < 1e-6f &&
                          nodes[1].rotate[0] == 1.0f && nodes[1].rotate[4] == 1.0f && nodes[1].scale == 1.0f,
                      "nif nodes: a node's authored offset, rotation and scale");
                Check(nodes[2].name == "LagBone01" && std::fabs(nodes[2].translate[1] - 0.5f) < 1e-6f &&
                          nodes[2].scale == 0.75f,
                      "nif nodes: past a block of another type");
            }
            const std::uint8_t junk[80]{};
            v = NPC::FileVerdict::kRead;
            Check(NPC::ParseNodeLocals(junk, sizeof junk, &v).empty() && v != NPC::FileVerdict::kRead,
                  "nif nodes: not a NIF reads nothing");
        }

        // A sequence's emitter tracks over the block's values, as Sparks' mIdle sets its particles' size.
        {
            NPC::System s;
            s.name           = "PCloudPowerHand";
            s.emitter.radius = 0.75f;
            s.emitter.life   = 1.0f;
            s.emitter.speed  = 2.0f;
            s.gravity.push_back({});
            s.gravity.back().name     = "NiPSysGravityModifier:9";
            s.gravity.back().strength = 1.0f;
            s.gravity.push_back({});
            s.gravity.back().name     = "NiPSysGravityModifier:2";
            s.gravity.back().strength = 3.0f;
            NPC::Sequence q;
            q.time.start = 0.0f;
            const auto block = [&](const char* node, const char* type, const char* id, std::int32_t interp) {
                NPC::Sequence::Block b;
                b.node           = node;
                b.controllerType = type;
                b.controllerId   = id;
                b.interpolator   = interp;
                q.blocks.push_back(b);
            };
            block("PCloudPowerHand", "NiPSysEmitterInitialRadiusCtlr", "NiPSysBoxEmitter:0", 1);
            block("PCloudPowerHand", "NiPSysEmitterLifeSpanCtlr", "NiPSysBoxEmitter:0", 2);
            block("PCloudPowerHand", "NiPSysEmitterSpeedCtlr", "NiPSysBoxEmitter:0", 3);
            block("PCloudPowerHand", "NiPSysGravityStrengthCtlr", "NiPSysGravityModifier:9", 4);
            block("PCloudPowerCore", "NiPSysEmitterInitialRadiusCtlr", "NiPSysBoxEmitter:0", 5);
            block("PCloudPowerHand", "NiPSysEmitterLifeSpanCtlr", "NiPSysBoxEmitter:0", 6);
            const auto read = [](std::int32_t interp, NPC::Curve& c) {
                switch (interp) {
                case 1: c.keys = { { 0.0f, 5.0f }, { 9.67f, 5.0f } }; break;
                case 2: c.keys = { { 0.0f, 0.2f } }; break;
                case 3: c.constant = 9.0f; break;
                case 4: c.constant = 90.0f; break;
                case 5: c.constant = 12.0f; break;
                case 6: c.constant = -3.402823466e+38f; break;   // the "no value" sentinel
                default: break;
                }
            };
            NPC::ApplySequenceEmitterValues(q, s, read);
            Check(s.emitter.radius == 5.0f && std::fabs(s.emitter.life - 0.2f) < 1e-6f && s.emitter.speed == 9.0f,
                  "sequence emitter: size, life and speed come from the sequence's tracks");
            Check(s.gravity.front().strength == 90.0f && s.gravity.back().strength == 3.0f,
                  "sequence emitter: a gravity strength by its modifier's name, another modifier's kept");
            Check(s.emitter.radius == 5.0f, "sequence emitter: another system's track is left alone");

            NPC::Sequence bad;
            bad.time.start = 0.0f;
            for (const char* type : { "NiPSysEmitterInitialRadiusCtlr", "NiPSysEmitterLifeSpanCtlr" }) {
                NPC::Sequence::Block b;
                b.node           = "PCloudPowerHand";
                b.controllerType = type;
                b.controllerId   = "NiPSysBoxEmitter:0";
                b.interpolator   = 7;
                bad.blocks.push_back(b);
            }
            NPC::Sequence zeroLife   = bad;
            zeroLife.blocks.erase(zeroLife.blocks.begin());
            zeroLife.blocks[0].interpolator = 8;
            const auto readBad = [](std::int32_t interp, NPC::Curve& c) {
                c.constant = interp == 7 ? -2.0f : 0.0f;
            };
            NPC::ApplySequenceEmitterValues(bad, s, readBad);
            NPC::ApplySequenceEmitterValues(zeroLife, s, readBad);
            Check(s.emitter.radius == 5.0f && std::fabs(s.emitter.life - 0.2f) < 1e-6f,
                  "sequence emitter: a negative size and a life of 0 or less are refused");
        }
    }
}
