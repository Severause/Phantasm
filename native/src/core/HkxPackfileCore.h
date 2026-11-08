#pragma once

// Phantasm - HkxPackfileCore: a Havok packfile (an .hkx animation) read from its bytes into the memory image the
// engine would load it as: the __data__ section, the pointers its fixups set (each an offset in that section and the
// offset it points to), and every object's place and class, for the caller to give each its engine vtable. Only the
// layout Skyrim SE writes is read: hk_2010.2.0-r1, 8-byte little-endian pointers, no padding reuse, empty-base
// optimisation on. Engine-free; tested by HkxPackfileCoreTest.cpp.
//
// Author: Severause

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Phantasm::HkxPackfileCore
{
    inline constexpr std::uint32_t    kMagic0       = 0x57E0E057;
    inline constexpr std::uint32_t    kMagic1       = 0x10C0C010;
    inline constexpr std::string_view kVersion      = "hk_2010.2.0-r1";
    inline constexpr std::size_t      kHeaderBytes  = 64;
    inline constexpr std::size_t      kSectionBytes = 48;   // name[19] + NUL, then seven offsets
    inline constexpr std::size_t      kMaxBytes     = 32u << 20;
    inline constexpr std::size_t      kMaxSections  = 16;
    inline constexpr std::size_t      kMaxClassName = 128;

    /// A pointer the loader writes: at `at` in the data section, the address of `to` in the same section.
    struct Pointer
    {
        std::uint32_t at = 0, to = 0;
    };
    /// An object of the data section: where it starts and the class its virtual fixup names.
    struct Object
    {
        std::uint32_t at = 0;
        std::string   className;
    };
    struct Image
    {
        std::vector<std::uint8_t> data;   // the data section's bytes, up to its fixup tables
        std::vector<Pointer>      pointers;
        std::vector<Object>       objects;
    };

    enum class Verdict : std::uint8_t
    {
        kRead,
        kNotHkx,       // not a Havok packfile (a tagfile, an XML export, another file)
        kOtherLayout,  // a packfile of another Havok version or platform (LE / 32-bit / Oldrim)
        kMalformed,    // sizes or offsets that do not fit the file
    };

    inline const char* VerdictText(Verdict v) noexcept
    {
        switch (v) {
        case Verdict::kRead:        return "read";
        case Verdict::kNotHkx:      return "is not a Havok packfile";
        case Verdict::kOtherLayout: return "is not a Skyrim SE packfile (hk_2010.2.0-r1, 64-bit)";
        case Verdict::kMalformed:   return "is malformed";
        }
        return "?";
    }

    namespace Detail
    {
        inline std::int32_t I32(const std::uint8_t* p) noexcept
        {
            std::int32_t v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }
        inline std::uint32_t U32(const std::uint8_t* p) noexcept
        {
            std::uint32_t v;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        struct Section
        {
            std::string  name;
            std::int64_t start = 0;
            std::int32_t local = 0, global = 0, virt = 0, exports = 0, imports = 0, end = 0;
        };

        /// A NUL-terminated name of at most `cap` bytes at p, printable ASCII; "" when it is not one.
        inline std::string NameAt(const std::uint8_t* p, std::size_t avail, std::size_t cap)
        {
            const std::size_t lim = (std::min)(avail, cap);
            for (std::size_t i = 0; i < lim; ++i) {
                if (p[i] == 0) return std::string(reinterpret_cast<const char*>(p), i);
                if (p[i] < 0x20 || p[i] > 0x7E) return {};
            }
            return {};
        }
    }

    /// Reads bytes[0..n) into out (left empty unless kRead). Every pointer and object offset is checked against the
    /// data section; an entry of -1 is a fixup table's padding. A global fixup into another section than __data__ is
    /// refused (kMalformed): an animation file points only within its data.
    inline Verdict Read(const std::uint8_t* bytes, std::size_t n, Image& result)
    {
        using namespace Detail;
        result = Image{};
        Image out;
        if (!bytes || n < kHeaderBytes) return Verdict::kNotHkx;
        if (U32(bytes) != kMagic0 || U32(bytes + 4) != kMagic1) return Verdict::kNotHkx;
        if (n > kMaxBytes) return Verdict::kMalformed;
        const std::uint8_t ptrSize = bytes[16], little = bytes[17], reuse = bytes[18], emptyBase = bytes[19];
        const std::string  version = NameAt(bytes + 40, 16, 16);
        if (ptrSize != 8 || little != 1 || reuse != 0 || emptyBase != 1 || version != kVersion) return Verdict::kOtherLayout;
        const std::int32_t numSections = I32(bytes + 20);
        if (numSections < 1 || static_cast<std::size_t>(numSections) > kMaxSections ||
            kHeaderBytes + static_cast<std::size_t>(numSections) * kSectionBytes > n) {
            return Verdict::kMalformed;
        }

        std::vector<Section> secs(static_cast<std::size_t>(numSections));
        for (std::size_t i = 0; i < secs.size(); ++i) {
            const std::uint8_t* h = bytes + kHeaderBytes + i * kSectionBytes;
            Section&            s = secs[i];
            s.name    = NameAt(h, 20, 20);
            s.start   = I32(h + 20);
            s.local   = I32(h + 24);
            s.global  = I32(h + 28);
            s.virt    = I32(h + 32);
            s.exports = I32(h + 36);
            s.imports = I32(h + 40);
            s.end     = I32(h + 44);
            const bool ordered = s.start >= 0 && s.local >= 0 && s.local <= s.global && s.global <= s.virt &&
                                 s.virt <= s.exports && s.exports <= s.imports && s.imports <= s.end;
            if (!ordered || static_cast<std::uint64_t>(s.start) + static_cast<std::uint64_t>(s.end) > n) {
                return Verdict::kMalformed;
            }
        }
        const auto indexOf = [&](std::string_view name) -> std::int32_t {
            for (std::size_t i = 0; i < secs.size(); ++i) {
                if (secs[i].name == name) return static_cast<std::int32_t>(i);
            }
            return -1;
        };
        const std::int32_t dataIx = indexOf("__data__"), namesIx = indexOf("__classnames__");
        if (dataIx < 0 || namesIx < 0) return Verdict::kMalformed;
        const Section& data  = secs[static_cast<std::size_t>(dataIx)];
        const Section& names = secs[static_cast<std::size_t>(namesIx)];

        // The section's own bytes end where its fixup tables begin.
        const std::uint32_t dataBytes = static_cast<std::uint32_t>(data.local);
        const std::uint8_t* d         = bytes + data.start;
        out.data.assign(d, d + dataBytes);
        const auto pointerFits = [&](std::int32_t at) { return at >= 0 && static_cast<std::uint32_t>(at) + 8u <= dataBytes; };
        const auto targetFits  = [&](std::int32_t to) { return to >= 0 && static_cast<std::uint32_t>(to) < dataBytes; };

        for (std::int32_t o = data.local; o + 8 <= data.global; o += 8) {
            const std::int32_t at = I32(d + o), to = I32(d + o + 4);
            if (at == -1) continue;
            if (!pointerFits(at) || !targetFits(to)) return Verdict::kMalformed;
            out.pointers.push_back({ static_cast<std::uint32_t>(at), static_cast<std::uint32_t>(to) });
        }
        for (std::int32_t o = data.global; o + 12 <= data.virt; o += 12) {
            const std::int32_t at = I32(d + o), sec = I32(d + o + 4), to = I32(d + o + 8);
            if (at == -1) continue;
            if (sec != dataIx || !pointerFits(at) || !targetFits(to)) return Verdict::kMalformed;
            out.pointers.push_back({ static_cast<std::uint32_t>(at), static_cast<std::uint32_t>(to) });
        }
        const std::uint8_t* nameBase  = bytes + names.start;
        const std::uint32_t nameBytes = static_cast<std::uint32_t>(names.local);
        for (std::int32_t o = data.virt; o + 12 <= data.exports; o += 12) {
            const std::int32_t at = I32(d + o), sec = I32(d + o + 4), nameAt = I32(d + o + 8);
            if (at == -1) continue;
            if (sec != namesIx || at < 0 || static_cast<std::uint32_t>(at) >= dataBytes || nameAt < 0 ||
                static_cast<std::uint32_t>(nameAt) >= nameBytes) {
                return Verdict::kMalformed;
            }
            std::string cls = NameAt(nameBase + nameAt, nameBytes - static_cast<std::uint32_t>(nameAt), kMaxClassName);
            if (cls.empty()) return Verdict::kMalformed;
            out.objects.push_back({ static_cast<std::uint32_t>(at), std::move(cls) });
        }
        result = std::move(out);
        return Verdict::kRead;
    }

    /// The objects of class `cls` (exact, case-sensitive: Havok class names are).
    inline std::vector<std::uint32_t> ObjectsOf(const Image& img, std::string_view cls)
    {
        std::vector<std::uint32_t> at;
        for (const auto& o : img.objects) {
            if (o.className == cls) at.push_back(o.at);
        }
        return at;
    }

    /// Whether a placed clip may keep an object of class `cls` without an engine vtable: one nothing the sampler reads
    /// points into and nothing calls. The root container (it has no vtable), and the resource container some
    /// exporters write beside the animation container ("Resource Data", an hkMemoryResourceContainer).
    inline bool InertClipClass(std::string_view cls) noexcept
    {
        return cls == "hkRootLevelContainer" || cls == "hkMemoryResourceContainer";
    }

    /// The pointer the loader writes at `at`, as the offset it points to; -1 when no fixup sets one there.
    inline std::int64_t PointerAt(const Image& img, std::uint32_t at)
    {
        for (const auto& p : img.pointers) {
            if (p.at == at) return p.to;
        }
        return -1;
    }
}
