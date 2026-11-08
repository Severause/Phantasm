#pragma once

// Phantasm - ShaderContractCore: the records the C++ side writes for the live views' HLSL (kShaderHLSL and
// kFxShaderHLSL in MannequinRenderer.cpp) and the check that the compiled shaders read them at the same places:
// each constant buffer's register, size and members by name, offset and size, and the particle vertices' register
// and stride, read back from the bytecode (D3DReflect) and compared with offsetof. The renderer checks every
// compile and refuses the views on a difference; ShaderContractCoreTest.cpp compiles the renderer's own shader text.
//
// Author: Severause

#include <DirectXMath.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Phantasm::ShaderContractCore
{
    /// The bone palette's slots (cbBones, VS b1): a skin with more bones keeps the bind-pose placement. The HLSL
    /// sizes boneMatrices from kMaxBonesMacro, defined as kMaxBonesText at every compile.
    inline constexpr char kMaxBonesMacro[] = "PHANTASM_MAX_BONES";
    inline constexpr char kMaxBonesText[]  = "128";

    constexpr std::uint32_t ParseDecimal(const char* s) noexcept
    {
        std::uint32_t v = 0;
        for (; *s; ++s) v = v * 10 + static_cast<std::uint32_t>(*s - '0');
        return v;
    }
    inline constexpr std::uint32_t kMaxBones = ParseDecimal(kMaxBonesText);   // kMaxBonesText is a plain decimal
    static_assert(ParseDecimal("128") == 128 && kMaxBones > 0);

    // ── Per-object constant buffer payload (cbPerObject, b0) ──
    // 5 float4x4 + 28 float4; must match the HLSL cbuffer field for field (CheckShader).
    struct CbPayload
    {
        DirectX::XMMATRIX worldViewProj;
        DirectX::XMMATRIX worldRot;          // rotation for normal transforms (3x3 used;
                                             // dyn-shape pieces substitute the actor
                                             // root frame — see FillPiecePayload)
        DirectX::XMMATRIX worldMat;          // FULL model->world (VS world-pos / shadow)
        DirectX::XMMATRIX lightViewProj;     // world -> key-light clip (shadow map)
        DirectX::XMMATRIX fillLightViewProj; // world -> fill-light clip (2nd shadow map)
        DirectX::XMFLOAT4 lightDirWorld;     // key light direction (toward the light)
        DirectX::XMFLOAT4 fillLightDir;      // fill light from opposite side
        DirectX::XMFLOAT4 rimLightDir;       // rim light from behind / above
        DirectX::XMFLOAT4 keyLightColor;     // .rgb=color, .a=intensity
        DirectX::XMFLOAT4 fillLightColor;
        DirectX::XMFLOAT4 rimLightColor;
        // .xyz=the camera in draw space (world from s_drawOrigin, as the VS's world positions), .w=the eye's
        // clip-space z (MakeFrameBase)
        DirectX::XMFLOAT4 cameraPosWorld;
        DirectX::XMFLOAT4 specularParams;    // x=intensity, y=power (shininess), z=subsurfaceAmount, w=metal reflection
        // x=ambient, y=useTexture, z=an effect piece's palette colour row base (EffectPieceLook), w=lightingMode
        // (0, or 2 flat-lit)
        DirectX::XMFLOAT4 tintParams;
        DirectX::XMFLOAT4 skinTint;          // rgb=mesh tint (skin OR hair), a=applyTint
        // x=useVertexColor, y=useNormalMap, z=alpha test (AlphaTestCode), w=useHalfLambert
        DirectX::XMFLOAT4 colorMode;
        // x=skinned (cbBones), y=haveSubsurfaceMap(_sk@t4), z=haveSpecMask(t5), w=emit alpha (EmitAlphaMode)
        DirectX::XMFLOAT4 pieceFlags;
        // specColorTint: the material's specularColor, a = what it tints
        // (RendererMaterialCore::SpecTintCode). emissiveAndFlags: rgb = a
        // lighting piece's emissiveColor × emissiveMult (capped, EmissiveChannel)
        // or an effect piece's colour multiplier, w = glow texture bound at t2.
        DirectX::XMFLOAT4 specColorTint;
        DirectX::XMFLOAT4 emissiveAndFlags;
        // x=haveEnvCube, y=envScale, z=haveFaceTint, w=haveDetail (t7).
        DirectX::XMFLOAT4 envParams;
        // x=emissive route (RendererMaterialCore::EmissiveRoute), y=envMaskBound, z=normal-alpha gloss, w=env masked
        // by gloss.
        DirectX::XMFLOAT4 miscParams;
        // x=an effect piece fades by its falloff (effectFalloff), y=depth bias (PS_Main and PS_Floor), z=an effect
        // piece takes its vertex rgb, w=an effect piece is a soft effect (drops a pixel of no alpha).
        DirectX::XMFLOAT4 shadowParams;
        DirectX::XMFLOAT4 uvXform;           // material UV offset (xy) / scale (zw); (0,0) scale = identity
        // TruePBR: x=isPBR, y=haveRMAOS(t5); z=an effect piece's palette colour, w=its palette alpha (palette at t1).
        DirectX::XMFLOAT4 pbrParams;
        // x=emitted-alpha multiplier (overlay / decal / blend pass), y=back-light map at t7, z=hair material,
        // w=depth pull (game units).
        DirectX::XMFLOAT4 materialParams;
        // The lighting preset's ambient lobes (w=1 = use them).
        DirectX::XMFLOAT4 ambientSky;
        DirectX::XMFLOAT4 ambientGround;
        // Two looks under one name: the actor view's floor (VS_Floor / PS_Floor, DrawMirrorFloor), or an item
        // particle system's colour and palette flags (PS_Fx, DrawFxParticles); see the HLSL.
        DirectX::XMFLOAT4 floorParams;
        DirectX::XMFLOAT4 floorLook;
        DirectX::XMFLOAT4 floorLook2;
        // An effect piece's falloff (BSEffectShaderMaterial, read under shadowParams.x): x=start angle, y=stop angle
        // (cosines), z=start opacity, w=stop opacity.
        DirectX::XMFLOAT4 effectFalloff;
        // An effect piece's soft edge: x=the material's soft falloff depth (0: none, or the scene's depth is not
        // bound), y and z=the projection's _33 and _43 (MakeFrameBase), which turn a depth into a view distance.
        DirectX::XMFLOAT4 effectSoft;
        // The item view's metal: x=1 for its reflection model (0: the actor views'), y=a metal's reflectance head-on.
        DirectX::XMFLOAT4 metalLook;
    };
    inline constexpr std::uint32_t kCbPerObjectBytes =
        sizeof(DirectX::XMMATRIX) * 5 + sizeof(DirectX::XMFLOAT4) * 28;
    static_assert(kCbPerObjectBytes % 16 == 0, "a constant buffer is a multiple of 16 bytes");
    static_assert(sizeof(CbPayload) == kCbPerObjectBytes, "CbPayload is 5 float4x4 and 28 float4, unpadded");

    /// One particle vertex of the item view (FxVertex, the structured buffer VS_Fx reads at t10).
    struct FxVertexCpu
    {
        DirectX::XMFLOAT3 pos;
        float             pad0;
        DirectX::XMFLOAT4 color;
        DirectX::XMFLOAT2 uv;
        DirectX::XMFLOAT2 pad1;
    };
    static_assert(sizeof(FxVertexCpu) == 48);

    /// The registers the renderer binds them at.
    inline constexpr std::uint32_t kPerObjectSlot  = 0;    // b0
    inline constexpr std::uint32_t kBonesSlot      = 1;    // b1
    inline constexpr std::uint32_t kFxVerticesSlot = 10;   // t10

    /// One member of a constant buffer: its name, byte offset and size.
    struct Member
    {
        std::string   name;
        std::uint32_t offset = 0;
        std::uint32_t size   = 0;
    };

#define PHANTASM_CB_MEMBER(m) \
    Member { #m, static_cast<std::uint32_t>(offsetof(CbPayload, m)), static_cast<std::uint32_t>(sizeof(CbPayload::m)) }

    /// cbPerObject as the C++ side writes it, in declaration order.
    inline std::vector<Member> PerObjectMembers()
    {
        return { PHANTASM_CB_MEMBER(worldViewProj),    PHANTASM_CB_MEMBER(worldRot),
                 PHANTASM_CB_MEMBER(worldMat),         PHANTASM_CB_MEMBER(lightViewProj),
                 PHANTASM_CB_MEMBER(fillLightViewProj), PHANTASM_CB_MEMBER(lightDirWorld),
                 PHANTASM_CB_MEMBER(fillLightDir),     PHANTASM_CB_MEMBER(rimLightDir),
                 PHANTASM_CB_MEMBER(keyLightColor),    PHANTASM_CB_MEMBER(fillLightColor),
                 PHANTASM_CB_MEMBER(rimLightColor),    PHANTASM_CB_MEMBER(cameraPosWorld),
                 PHANTASM_CB_MEMBER(specularParams),   PHANTASM_CB_MEMBER(tintParams),
                 PHANTASM_CB_MEMBER(skinTint),         PHANTASM_CB_MEMBER(colorMode),
                 PHANTASM_CB_MEMBER(pieceFlags),       PHANTASM_CB_MEMBER(specColorTint),
                 PHANTASM_CB_MEMBER(emissiveAndFlags), PHANTASM_CB_MEMBER(envParams),
                 PHANTASM_CB_MEMBER(miscParams),       PHANTASM_CB_MEMBER(shadowParams),
                 PHANTASM_CB_MEMBER(uvXform),          PHANTASM_CB_MEMBER(pbrParams),
                 PHANTASM_CB_MEMBER(materialParams),   PHANTASM_CB_MEMBER(ambientSky),
                 PHANTASM_CB_MEMBER(ambientGround),    PHANTASM_CB_MEMBER(floorParams),
                 PHANTASM_CB_MEMBER(floorLook),        PHANTASM_CB_MEMBER(floorLook2),
                 PHANTASM_CB_MEMBER(effectFalloff),    PHANTASM_CB_MEMBER(effectSoft),
                 PHANTASM_CB_MEMBER(metalLook) };
    }

#undef PHANTASM_CB_MEMBER

    /// cbBones as the C++ side writes it: kMaxBones float4x4.
    inline std::vector<Member> BoneMembers()
    {
        return { Member{ "boneMatrices", 0, kMaxBones * static_cast<std::uint32_t>(sizeof(DirectX::XMFLOAT4X4)) } };
    }

    /// A resource as compiled code declares it: whether it does, its register, its size (a constant buffer's bytes,
    /// a structured buffer's stride) and a constant buffer's members in order.
    struct Declared
    {
        bool                present   = false;
        std::uint32_t       bindPoint = 0;
        std::uint32_t       size      = 0;
        std::vector<Member> members;
    };

    /// Why a constant buffer the code declares differs from what the C++ side writes ("" when it matches): its
    /// register, its size, then its members in order by name, offset and size.
    inline std::string BufferMismatch(std::string_view what, std::uint32_t slot, std::uint32_t bytes,
                                      const std::vector<Member>& expected, const Declared& d)
    {
        const auto num = [](std::uint32_t v) { return std::to_string(v); };
        std::string w(what);
        if (d.bindPoint != slot) return w + " is at register " + num(d.bindPoint) + ", bound at " + num(slot);
        if (d.size != bytes) return w + " is " + num(d.size) + " bytes, written as " + num(bytes);
        if (d.members.size() != expected.size()) {
            return w + " has " + num(static_cast<std::uint32_t>(d.members.size())) + " members, written as " +
                   num(static_cast<std::uint32_t>(expected.size()));
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const Member& e = expected[i];
            const Member& a = d.members[i];
            if (a.name != e.name) return w + " member " + num(static_cast<std::uint32_t>(i)) + " is " + a.name +
                                         ", written as " + e.name;
            if (a.offset != e.offset || a.size != e.size) {
                return w + "." + e.name + " is at " + num(a.offset) + " (" + num(a.size) + " bytes), written at " +
                       num(e.offset) + " (" + num(e.size) + " bytes)";
            }
        }
        return {};
    }

    /// Reads `name` from compiled code's reflection; not present when the code does not use it.
    inline Declared ReflectResource(ID3D11ShaderReflection* r, const char* name)
    {
        Declared                     d;
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        if (!r || FAILED(r->GetResourceBindingDescByName(name, &bind))) return d;
        d.present   = true;
        d.bindPoint = bind.BindPoint;
        if (bind.Type == D3D_SIT_STRUCTURED) {
            d.size = bind.NumSamples;   // a structured buffer's stride
            return d;
        }
        if (bind.Type != D3D_SIT_CBUFFER) return d;
        auto*                    cb = r->GetConstantBufferByName(name);
        D3D11_SHADER_BUFFER_DESC desc{};
        if (!cb || FAILED(cb->GetDesc(&desc))) return d;
        d.size = desc.Size;
        for (UINT i = 0; i < desc.Variables; ++i) {
            auto*                      v = cb->GetVariableByIndex(i);
            D3D11_SHADER_VARIABLE_DESC vd{};
            if (!v || FAILED(v->GetDesc(&vd))) continue;
            d.members.push_back({ vd.Name ? vd.Name : "", vd.StartOffset, vd.Size });
        }
        return d;
    }

    /// What an entry point must declare: the per-object buffer, the bone palette, the particle vertices.
    struct Needs
    {
        bool perObject  = false;
        bool bones      = false;
        bool fxVertices = false;
    };

    /// Why compiled code reads the C++ records differently ("" when it reads them as written). Every record the
    /// code declares is checked; one in `needs` it does not declare is a difference too (a renamed buffer).
    inline std::string CheckShader(const void* code, std::size_t bytes, Needs needs)
    {
        Microsoft::WRL::ComPtr<ID3D11ShaderReflection> r;
        if (!code || bytes == 0 || FAILED(D3DReflect(code, bytes, __uuidof(ID3D11ShaderReflection),
                                                     reinterpret_cast<void**>(r.GetAddressOf())))) {
            return "the bytecode does not reflect";
        }
        const Declared perObject = ReflectResource(r.Get(), "cbPerObject");
        if (needs.perObject && !perObject.present) return "cbPerObject is not declared";
        if (perObject.present) {
            auto why = BufferMismatch("cbPerObject", kPerObjectSlot, kCbPerObjectBytes, PerObjectMembers(), perObject);
            if (!why.empty()) return why;
        }
        const Declared bones = ReflectResource(r.Get(), "cbBones");
        if (needs.bones && !bones.present) return "cbBones is not declared";
        if (bones.present) {
            auto why = BufferMismatch("cbBones", kBonesSlot,
                                      kMaxBones * static_cast<std::uint32_t>(sizeof(DirectX::XMFLOAT4X4)),
                                      BoneMembers(), bones);
            if (!why.empty()) return why;
        }
        const Declared fx = ReflectResource(r.Get(), "fxVertices");
        if (needs.fxVertices && !fx.present) return "fxVertices is not declared";
        if (fx.present) {
            if (fx.bindPoint != kFxVerticesSlot) {
                return "fxVertices is at register " + std::to_string(fx.bindPoint) + ", bound at " +
                       std::to_string(kFxVerticesSlot);
            }
            if (fx.size != sizeof(FxVertexCpu)) {
                return "fxVertices' stride is " + std::to_string(fx.size) + " bytes, written as " +
                       std::to_string(sizeof(FxVertexCpu));
            }
        }
        return {};
    }
}
