// Phantasm - native self-tests: ShaderContractCore (the C++ records against the live views' HLSL). Compiles the
// renderer's own shader text, read from MannequinRenderer.cpp (PHANTASM_RENDERER_SOURCE), as the renderer does.
//
// Author: Severause

#include "ShaderContractCore.h"
#include "TestHarness.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace Phantasm::Tests
{
    namespace
    {
        namespace SC = Phantasm::ShaderContractCore;

        // The text of `decl`'s raw string literal in src: its R"( ... )" pieces joined as the compiler joins them.
        std::string RawStringOf(const std::string& src, std::string_view decl)
        {
            auto p = src.find(decl);
            if (p == std::string::npos) return {};
            p = src.find("R\"(", p);
            if (p == std::string::npos) return {};
            p += 3;
            std::string out;
            for (;;) {
                const auto e = src.find(")\"", p);
                if (e == std::string::npos) return {};
                out.append(src, p, e - p);
                auto q = e + 2;
                while (q < src.size() && std::isspace(static_cast<unsigned char>(src[q]))) ++q;
                if (src.compare(q, 3, "R\"(") != 0) return out;
                p = q + 3;
            }
        }

        std::string ReadRendererSource()
        {
            std::ifstream f(PHANTASM_RENDERER_SOURCE, std::ios::binary);
            std::string   s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            std::string   out;
            out.reserve(s.size());
            for (const char c : s) {
                if (c != '\r') out.push_back(c);
            }
            return out;
        }

        // Compiles entry from source with bones as kMaxBones' definition; empty on a compile error.
        std::vector<std::uint8_t> Compile(const std::string& source, const char* entry, const char* target,
                                          const char* bones = SC::kMaxBonesText)
        {
            const D3D_SHADER_MACRO macros[] = { { SC::kMaxBonesMacro, bones }, { nullptr, nullptr } };
            Microsoft::WRL::ComPtr<ID3DBlob> blob, errors;
            if (FAILED(D3DCompile(source.data(), source.size(), "MannequinShader", macros, nullptr, entry, target,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
                std::printf("      %s: %s\n", entry,
                            errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no error text");
                return {};
            }
            const auto* b = static_cast<const std::uint8_t*>(blob->GetBufferPointer());
            return { b, b + blob->GetBufferSize() };
        }

        std::string Mismatch(const std::vector<std::uint8_t>& code, SC::Needs needs)
        {
            return SC::CheckShader(code.data(), code.size(), needs);
        }
    }

    void RunShaderContractCoreTests()
    {
        Check(SC::kMaxBones == SC::ParseDecimal(SC::kMaxBonesText) && SC::ParseDecimal("42") == 42,
              "shader contract: kMaxBones is the macro's text");

        // The comparison itself.
        const std::vector<SC::Member> two = { { "a", 0, 16 }, { "b", 16, 64 } };
        SC::Declared                  d;
        d.present   = true;
        d.bindPoint = 0;
        d.size      = 80;
        d.members   = two;
        Check(SC::BufferMismatch("cb", 0, 80, two, d).empty(), "shader contract: the same layout matches");
        Check(!SC::BufferMismatch("cb", 1, 80, two, d).empty(), "shader contract: another register differs");
        Check(!SC::BufferMismatch("cb", 0, 96, two, d).empty(), "shader contract: another size differs");
        auto moved       = d;
        moved.members[1] = { "b", 32, 64 };
        Check(SC::BufferMismatch("cb", 0, 80, two, moved).find("b is at 32") != std::string::npos,
              "shader contract: a moved member is named with both offsets");
        auto renamed       = d;
        renamed.members[0] = { "c", 0, 16 };
        Check(!SC::BufferMismatch("cb", 0, 80, two, renamed).empty(), "shader contract: a renamed member differs");
        auto fewer = d;
        fewer.members.pop_back();
        Check(!SC::BufferMismatch("cb", 0, 80, two, fewer).empty(), "shader contract: a missing member differs");
        Check(SC::CheckShader(nullptr, 0, {}) == "the bytecode does not reflect",
              "shader contract: no bytecode is a difference");

        // The renderer's own shaders, compiled as CompileLiveShaders and CompileFxShaders compile them.
        const std::string src  = ReadRendererSource();
        const std::string live = RawStringOf(src, "constexpr const char* kShaderHLSL = ");
        const std::string fx   = RawStringOf(src, "constexpr const char* kFxShaderHLSL = ");
        Check(!live.empty() && !fx.empty(), "shader contract: the renderer's shader text is read");
        if (live.empty() || fx.empty()) return;
        const std::string withFx = live + fx;

        const auto vsMain = Compile(live, "VS_Main", "vs_5_0");
        Check(!vsMain.empty() && Mismatch(vsMain, { .perObject = true, .bones = true }).empty(),
              "shader contract: VS_Main reads cbPerObject and cbBones as written");
        const auto psMain = Compile(live, "PS_Main", "ps_5_0");
        Check(!psMain.empty() && Mismatch(psMain, { .perObject = true }).empty(),
              "shader contract: PS_Main reads cbPerObject as written");
        bool others = true;
        for (const auto& [entry, target] : { std::pair{ "PS_ShadowClip", "ps_5_0" }, std::pair{ "VS_Floor", "vs_5_0" },
                                             std::pair{ "PS_Floor", "ps_5_0" } }) {
            const auto code = Compile(live, entry, target);
            const auto why  = code.empty() ? std::string("no code") : Mismatch(code, {});
            if (!why.empty()) std::printf("      %s: %s\n", entry, why.c_str());
            others = others && why.empty();
        }
        Check(others, "shader contract: the shadow clip and the floor read what they declare as written");
        const auto vsFx = Compile(withFx, "VS_Fx", "vs_5_0");
        Check(!vsFx.empty() && Mismatch(vsFx, { .perObject = true, .fxVertices = true }).empty(),
              "shader contract: VS_Fx reads cbPerObject and the particle vertices as written");
        bool fxOthers = true;
        for (const auto& [entry, target] : { std::pair{ "PS_Fx", "ps_5_0" }, std::pair{ "VS_FullTri", "vs_5_0" },
                                             std::pair{ "PS_FxComposite", "ps_5_0" } }) {
            const auto code = Compile(withFx, entry, target);
            const auto why  = code.empty() ? std::string("no code") : Mismatch(code, {});
            if (!why.empty()) std::printf("      %s: %s\n", entry, why.c_str());
            fxOthers = fxOthers && why.empty();
        }
        Check(fxOthers, "shader contract: the particle pixel shaders read what they declare as written");

        // A drift on either side is caught.
        const auto shortPalette = Compile(live, "VS_Main", "vs_5_0", "64");
        Check(!shortPalette.empty() && Mismatch(shortPalette, { .bones = true }).find("cbBones") == 0,
              "shader contract: a palette of another size is caught");
        std::string inserted = live;
        const auto  at       = inserted.find("    float4   floorParams;");
        if (at != std::string::npos) inserted.insert(at, "    float4   extraLane;\n");
        const auto  shifted =
            at != std::string::npos ? Compile(inserted, "VS_Main", "vs_5_0") : std::vector<std::uint8_t>{};
        Check(!shifted.empty() && !Mismatch(shifted, { .perObject = true }).empty(),
              "shader contract: a member added on the HLSL side is caught");
        Check(!vsMain.empty() && !Mismatch(vsMain, { .fxVertices = true }).empty(),
              "shader contract: a needed record the code does not declare is caught");
    }
}
