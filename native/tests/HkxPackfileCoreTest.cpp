// Phantasm - native self-tests: HkxPackfileCore (an .hkx packfile read into its memory image), over packfiles built
// here with the layout Skyrim SE's animation files use (checked against an animation pack's hk_2010.2.0-r1 files).
//
// Author: Severause

#include "HkxPackfileCore.h"
#include "TestHarness.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace Phantasm::Tests
{
    namespace
    {
        namespace HPC = Phantasm::HkxPackfileCore;

        void Put32(std::vector<std::uint8_t>& b, std::size_t at, std::int32_t v)
        {
            if (b.size() < at + 4) b.resize(at + 4, 0);
            std::memcpy(b.data() + at, &v, 4);
        }

        // A packfile of three sections (__classnames__, __types__, __data__) as Skyrim SE writes one.
        struct PackBuilder
        {
            std::vector<std::string>                             classes;
            std::vector<std::uint8_t>                            data = std::vector<std::uint8_t>(64, 0xAB);
            std::vector<std::pair<std::int32_t, std::int32_t>>   locals;
            std::vector<std::array<std::int32_t, 3>>             globals, virtuals;   // virtuals: at, section, class index
            std::uint8_t                                         ptrSize = 8, little = 1, reuse = 0, emptyBase = 1;
            std::string                                          version{ HPC::kVersion };
            std::uint32_t                                        magic0  = HPC::kMagic0;

            std::vector<std::uint8_t> Build(std::vector<std::int32_t>* classOffsets = nullptr) const
            {
                std::vector<std::uint8_t> f(HPC::kHeaderBytes + 3 * HPC::kSectionBytes, 0);
                Put32(f, 0, static_cast<std::int32_t>(magic0));
                Put32(f, 4, static_cast<std::int32_t>(HPC::kMagic1));
                Put32(f, 12, 8);
                f[16] = ptrSize;
                f[17] = little;
                f[18] = reuse;
                f[19] = emptyBase;
                Put32(f, 20, 3);
                std::memcpy(f.data() + 40, version.data(), (std::min)(version.size(), std::size_t{ 15 }));

                // __classnames__: sig, tag, name, NUL per class.
                const std::size_t namesStart = f.size();
                std::vector<std::int32_t> offs;
                for (const auto& c : classes) {
                    Put32(f, f.size(), 0x12345678);
                    f.push_back(0x09);
                    offs.push_back(static_cast<std::int32_t>(f.size() - namesStart));
                    f.insert(f.end(), c.begin(), c.end());
                    f.push_back(0);
                }
                const std::int32_t namesLen = static_cast<std::int32_t>(f.size() - namesStart);
                const std::size_t  typesStart = f.size();

                const std::size_t dataStart = f.size();
                f.insert(f.end(), data.begin(), data.end());
                const std::int32_t local = static_cast<std::int32_t>(data.size());
                for (const auto& [at, to] : locals) {
                    Put32(f, f.size(), at);
                    Put32(f, f.size(), to);
                }
                Put32(f, f.size(), -1);   // padding
                Put32(f, f.size(), -1);
                const std::int32_t global = static_cast<std::int32_t>(f.size() - dataStart);
                for (const auto& g : globals) {
                    for (const auto v : g) Put32(f, f.size(), v);
                }
                const std::int32_t virt = static_cast<std::int32_t>(f.size() - dataStart);
                for (const auto& v : virtuals) {
                    Put32(f, f.size(), v[0]);
                    Put32(f, f.size(), v[1]);
                    Put32(f, f.size(), v[2] >= 0 && v[2] < static_cast<std::int32_t>(offs.size()) ? offs[static_cast<std::size_t>(v[2])] : v[2]);
                }
                Put32(f, f.size(), -1);
                Put32(f, f.size(), -1);
                Put32(f, f.size(), -1);
                const std::int32_t end = static_cast<std::int32_t>(f.size() - dataStart);

                const auto section = [&](std::size_t i, const char* name, std::size_t start, std::int32_t loc,
                                         std::int32_t glob, std::int32_t vir, std::int32_t e) {
                    const std::size_t h = HPC::kHeaderBytes + i * HPC::kSectionBytes;
                    std::memcpy(f.data() + h, name, std::strlen(name));
                    Put32(f, h + 20, static_cast<std::int32_t>(start));
                    Put32(f, h + 24, loc);
                    Put32(f, h + 28, glob);
                    Put32(f, h + 32, vir);
                    Put32(f, h + 36, e);
                    Put32(f, h + 40, e);
                    Put32(f, h + 44, e);
                };
                section(0, "__classnames__", namesStart, namesLen, namesLen, namesLen, namesLen);
                section(1, "__types__", typesStart, 0, 0, 0, 0);
                section(2, "__data__", dataStart, local, global, virt, end);
                if (classOffsets) *classOffsets = offs;
                return f;
            }
        };

        PackBuilder Typical()
        {
            PackBuilder b;
            b.classes = { "hkRootLevelContainer", "hkaSplineCompressedAnimation", "hkaAnimationBinding" };
            b.locals  = { { 0, 16 } };
            b.globals = { { 48, 2, 32 } };   // the binding's animation pointer at 48 -> the animation at 32
            b.virtuals = { { 0, 0, 0 }, { 32, 0, 1 }, { 40, 0, 2 } };
            return b;
        }
    }

    void RunHkxPackfileCoreTests()
    {
        {
            const auto      f = Typical().Build();
            HPC::Image      img;
            const auto      v = HPC::Read(f.data(), f.size(), img);
            Check(v == HPC::Verdict::kRead, "hkx: a Skyrim SE packfile reads");
            Check(img.data.size() == 64 && img.data[5] == 0xAB, "hkx: the data section's bytes, up to its fixup tables");
            Check(img.pointers.size() == 2, "hkx: a local and a global fixup each set a pointer; padding entries are skipped");
            Check(HPC::PointerAt(img, 0) == 16 && HPC::PointerAt(img, 48) == 32, "hkx: each pointer names its target offset");
            Check(HPC::PointerAt(img, 8) == -1, "hkx: no fixup at an offset reads as no pointer");
            Check(HPC::InertClipClass("hkRootLevelContainer") && HPC::InertClipClass("hkMemoryResourceContainer"),
                  "hkx: the root and an exporter's resource container stay as plain data");
            Check(!HPC::InertClipClass("hkaAnimationContainer") && !HPC::InertClipClass("hkaAnimationBinding") &&
                      !HPC::InertClipClass("hkaSplineCompressedAnimation") && !HPC::InertClipClass("hkbBehaviorGraph"),
                  "hkx: the sampled classes and other classes are not inert");
            Check(!HPC::InertClipClass("hkmemoryresourcecontainer") && !HPC::InertClipClass(""),
                  "hkx: inert classes match exactly, as Havok class names do");
            Check(img.objects.size() == 3, "hkx: one object per virtual fixup");
            const auto anims = HPC::ObjectsOf(img, "hkaSplineCompressedAnimation");
            Check(anims.size() == 1 && anims[0] == 32, "hkx: an object is found by its class name");
            Check(HPC::ObjectsOf(img, "hkaspline").empty(), "hkx: class names match exactly");
        }
        {
            HPC::Image img;
            auto       b = Typical();
            b.magic0     = 0xCAFEBABE;
            auto f       = b.Build();
            Check(HPC::Read(f.data(), f.size(), img) == HPC::Verdict::kNotHkx, "hkx: another file is not a packfile");
            Check(HPC::Read(f.data(), 10, img) == HPC::Verdict::kNotHkx, "hkx: a file shorter than a header is not one");
            Check(HPC::Read(nullptr, 0, img) == HPC::Verdict::kNotHkx, "hkx: no bytes are not a packfile");
        }
        {
            HPC::Image img;
            for (int which = 0; which < 5; ++which) {
                auto b = Typical();
                if (which == 0) b.ptrSize = 4;
                if (which == 1) b.little = 0;
                if (which == 2) b.reuse = 1;
                if (which == 3) b.emptyBase = 0;
                if (which == 4) b.version = "hk_2014.1.0-r1";
                const auto f = b.Build();
                Check(HPC::Read(f.data(), f.size(), img) == HPC::Verdict::kOtherLayout,
                      "hkx: an Oldrim, big-endian, padded or other-version packfile is another layout");
            }
        }
        {
            HPC::Image img;
            auto       b = Typical();
            b.globals    = { { 48, 0, 32 } };   // into __classnames__
            auto f       = b.Build();
            Check(HPC::Read(f.data(), f.size(), img) == HPC::Verdict::kMalformed,
                  "hkx: a pointer into another section than the data is refused");
            b         = Typical();
            b.locals  = { { 60, 16 } };   // 8 bytes from 60 run past the 64-byte data
            f         = b.Build();
            Check(HPC::Read(f.data(), f.size(), img) == HPC::Verdict::kMalformed, "hkx: a pointer that runs past the data is refused");
            b         = Typical();
            b.locals  = { { 0, 64 } };
            f         = b.Build();
            Check(HPC::Read(f.data(), f.size(), img) == HPC::Verdict::kMalformed, "hkx: a pointer past the data's end is refused");
            b          = Typical();
            b.virtuals = { { 0, 0, 999 } };
            f          = b.Build();
            Check(HPC::Read(f.data(), f.size(), img) == HPC::Verdict::kMalformed, "hkx: a class name outside its section is refused");
            f = Typical().Build();
            Check(HPC::Read(f.data(), f.size() - 20, img) == HPC::Verdict::kMalformed, "hkx: a cut-off file is refused");
            Check(img.data.empty() && img.objects.empty(), "hkx: a refused read leaves the image empty");
        }
    }
}
