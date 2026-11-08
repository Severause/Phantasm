// Phantasm Showcase - Underlay: see Underlay.h.

#include "PCH.h"

#include "Underlay.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#pragma comment(lib, "d3dcompiler.lib")

namespace Showcase::Underlay
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        constexpr const char* kMenuName = "PhantasmShowcaseUnderlay";

        // Above the HUD and the other menus a paused game may still draw, so nothing of theirs shows in a stage; the
        // page itself is PrismaUI's, drawn after every menu (Underlay.h).
        constexpr std::int8_t kDepth = 5;

        // The page's own look (ui/src/styles/app.css): the backdrop is .app's radial-gradient(120% 80% at 50% 0%,
        // #15171c 0%, #0b0c0e 60%) and a stage .live's radial-gradient(90% 70% at 50% 35%, #1a1d23 0%, #0d0e11 75%).
        // Colours are the CSS values as 0..1 sRGB-encoded numbers (the frame stores them encoded, as CSS mixes them).
        struct Gradient
        {
            float cx, cy, rx, ry;   // centre and radii, fractions of the box
            float inner[3], outer[3];
            float stop;             // where the outer colour is reached, a fraction of the radii
        };
        constexpr Gradient kBackdrop{ 0.5f, 0.0f, 1.2f, 0.8f, { 0x15 / 255.f, 0x17 / 255.f, 0x1c / 255.f },
                                      { 0x0b / 255.f, 0x0c / 255.f, 0x0e / 255.f }, 0.6f };
        constexpr Gradient kStage{ 0.5f, 0.35f, 0.9f, 0.7f, { 0x1a / 255.f, 0x1d / 255.f, 0x23 / 255.f },
                                   { 0x0d / 255.f, 0x0e / 255.f, 0x11 / 255.f }, 0.75f };

        constexpr const char* kShader = R"(
cbuffer Params : register(b0)
{
    float4 rect;     // x, y, w, h in target pixels
    float4 target;   // target w, h, corner radius, mode (0 gradient, 1 picture)
    float4 grad;     // gradient centre and radii, fractions of the rect
    float4 inner;    // rgb, a = the stop
    float4 outer;
};
Texture2D    pic  : register(t0);
SamplerState samp : register(s0);

struct VOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VOut VS(uint id : SV_VertexID)
{
    float2 c  = float2(id & 1, (id >> 1) & 1);
    float2 px = rect.xy + c * rect.zw;
    VOut o;
    o.pos = float4(px.x / target.x * 2.0 - 1.0, 1.0 - px.y / target.y * 2.0, 0.0, 1.0);
    o.uv  = c;
    return o;
}

// Premultiplied out: the rounded corner's coverage scales colour and alpha alike.
float4 PS(VOut i) : SV_Target
{
    float2 half_ = rect.zw * 0.5;
    float  r     = target.z;
    float2 q     = abs(i.uv * rect.zw - half_) - (half_ - r);
    float  d     = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float  cov   = saturate(0.5 - d);
    float4 col;
    if (target.w < 0.5) {
        float2 g = (i.uv - grad.xy) / max(grad.zw, 1e-4);
        float  t = saturate(length(g) / max(inner.a, 1e-4));
        col = float4(lerp(inner.rgb, outer.rgb, t), 1.0);
    } else {
        col = pic.Sample(samp, i.uv);
    }
    return col * cov;
}
)";

        struct Params
        {
            float rect[4];
            float target[4];
            float grad[4];
            float inner[4];
            float outer[4];
        };
        static_assert(sizeof(Params) % 16 == 0);

        struct Gpu
        {
            ComPtr<ID3D11Device>            device;
            ComPtr<ID3D11VertexShader>      vs;
            ComPtr<ID3D11PixelShader>       ps;
            ComPtr<ID3D11Buffer>            cb;
            ComPtr<ID3D11BlendState>        blend;
            ComPtr<ID3D11RasterizerState>   raster;
            ComPtr<ID3D11DepthStencilState> depth;
            ComPtr<ID3D11SamplerState>      sampler;
        };

        std::mutex g_mutex;
        bool       g_shown = false;
        float      g_pageW = 0, g_pageH = 0;
        std::vector<PageBox> g_boxes;
        std::map<std::string, ComPtr<ID3D11ShaderResourceView>, std::less<>> g_images;

        // Main thread (the draw) only.
        Gpu  g_gpu;
        bool g_gpuFailed = false;
        struct
        {
            std::uint64_t frames = 0;
            double        totalMs = 0, worstMs = 0;
        } g_cost;

        bool Fail(const char* what, HRESULT hr)
        {
            SKSE::log::error("underlay: {} failed ({:#x}); the views are not drawn under PrismaUI", what,
                             static_cast<std::uint32_t>(hr));
            g_gpuFailed = true;
            return false;
        }

        bool EnsureGpu(ID3D11Device* device)
        {
            if (g_gpuFailed) return false;
            if (g_gpu.device.Get() == device && g_gpu.ps) return true;
            g_gpu = {};
            g_gpu.device = device;
            ComPtr<ID3DBlob> vsb, psb, err;
            HRESULT hr = D3DCompile(kShader, std::strlen(kShader), "ShowcaseUnderlay", nullptr, nullptr, "VS", "vs_5_0",
                                    D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &err);
            if (FAILED(hr)) {
                if (err) SKSE::log::error("underlay: {}", static_cast<const char*>(err->GetBufferPointer()));
                return Fail("the vertex shader's compile", hr);
            }
            hr = D3DCompile(kShader, std::strlen(kShader), "ShowcaseUnderlay", nullptr, nullptr, "PS", "ps_5_0",
                            D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &err);
            if (FAILED(hr)) {
                if (err) SKSE::log::error("underlay: {}", static_cast<const char*>(err->GetBufferPointer()));
                return Fail("the pixel shader's compile", hr);
            }
            if (FAILED(hr = device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_gpu.vs)))
                return Fail("the vertex shader", hr);
            if (FAILED(hr = device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_gpu.ps)))
                return Fail("the pixel shader", hr);
            D3D11_BUFFER_DESC bd{};
            bd.ByteWidth = sizeof(Params);
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(hr = device->CreateBuffer(&bd, nullptr, &g_gpu.cb))) return Fail("the constant buffer", hr);
            D3D11_BLEND_DESC bl{};
            auto& rt = bl.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(hr = device->CreateBlendState(&bl, &g_gpu.blend))) return Fail("the blend state", hr);
            D3D11_RASTERIZER_DESC rd{};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.ScissorEnable = TRUE;
            rd.DepthClipEnable = TRUE;
            if (FAILED(hr = device->CreateRasterizerState(&rd, &g_gpu.raster))) return Fail("the rasterizer state", hr);
            D3D11_DEPTH_STENCIL_DESC dd{};
            dd.DepthEnable = FALSE;
            dd.StencilEnable = FALSE;
            if (FAILED(hr = device->CreateDepthStencilState(&dd, &g_gpu.depth))) return Fail("the depth state", hr);
            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            if (FAILED(hr = device->CreateSamplerState(&sd, &g_gpu.sampler))) return Fail("the sampler", hr);
            SKSE::log::info("underlay: ready");
            return true;
        }

        // Everything the draw changes, put back as it was: the frame goes on to draw other menus with it.
        struct SavedState
        {
            D3D11_PRIMITIVE_TOPOLOGY        topology{};
            ComPtr<ID3D11InputLayout>       layout;
            ComPtr<ID3D11VertexShader>      vs;
            ComPtr<ID3D11PixelShader>       ps;
            ComPtr<ID3D11GeometryShader>    gs;
            ComPtr<ID3D11HullShader>        hs;
            ComPtr<ID3D11DomainShader>      ds;
            ComPtr<ID3D11Buffer>            vsCb, psCb;
            ComPtr<ID3D11ShaderResourceView> psSrv;
            ComPtr<ID3D11SamplerState>      psSampler;
            ComPtr<ID3D11BlendState>        blend;
            float                           blendFactor[4]{};
            UINT                            sampleMask = 0;
            ComPtr<ID3D11DepthStencilState> depth;
            UINT                            stencilRef = 0;
            ComPtr<ID3D11RasterizerState>   raster;
            UINT                            viewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            D3D11_VIEWPORT                  viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            UINT                            scissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
            D3D11_RECT                      scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
            ComPtr<ID3D11DepthStencilView>  dsv;
            ID3D11RenderTargetView*         rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};   // AddRef'd by the Get

            ~SavedState()
            {
                for (auto* r : rtvs) {
                    if (r) r->Release();
                }
            }

            void Save(ID3D11DeviceContext* c)
            {
                c->IAGetPrimitiveTopology(&topology);
                c->IAGetInputLayout(&layout);
                c->VSGetShader(&vs, nullptr, nullptr);
                c->PSGetShader(&ps, nullptr, nullptr);
                c->GSGetShader(&gs, nullptr, nullptr);
                c->HSGetShader(&hs, nullptr, nullptr);
                c->DSGetShader(&ds, nullptr, nullptr);
                c->VSGetConstantBuffers(0, 1, &vsCb);
                c->PSGetConstantBuffers(0, 1, &psCb);
                c->PSGetShaderResources(0, 1, &psSrv);
                c->PSGetSamplers(0, 1, &psSampler);
                c->OMGetBlendState(&blend, blendFactor, &sampleMask);
                c->OMGetDepthStencilState(&depth, &stencilRef);
                c->RSGetState(&raster);
                c->RSGetViewports(&viewportCount, viewports);
                c->RSGetScissorRects(&scissorCount, scissors);
            }

            void Restore(ID3D11DeviceContext* c)
            {
                c->IASetPrimitiveTopology(topology);
                c->IASetInputLayout(layout.Get());
                c->VSSetShader(vs.Get(), nullptr, 0);
                c->PSSetShader(ps.Get(), nullptr, 0);
                c->GSSetShader(gs.Get(), nullptr, 0);
                c->HSSetShader(hs.Get(), nullptr, 0);
                c->DSSetShader(ds.Get(), nullptr, 0);
                ID3D11Buffer* vb = vsCb.Get();
                ID3D11Buffer* pb = psCb.Get();
                c->VSSetConstantBuffers(0, 1, &vb);
                c->PSSetConstantBuffers(0, 1, &pb);
                ID3D11ShaderResourceView* srv = psSrv.Get();
                c->PSSetShaderResources(0, 1, &srv);
                ID3D11SamplerState* smp = psSampler.Get();
                c->PSSetSamplers(0, 1, &smp);
                c->OMSetBlendState(blend.Get(), blendFactor, sampleMask);
                c->OMSetDepthStencilState(depth.Get(), stencilRef);
                c->RSSetState(raster.Get());
                c->RSSetViewports(viewportCount, viewports);
                c->RSSetScissorRects(scissorCount, scissors);
                c->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, dsv.Get());
            }
        };

        void Upload(ID3D11DeviceContext* c, const Params& p)
        {
            D3D11_MAPPED_SUBRESOURCE m{};
            if (FAILED(c->Map(g_gpu.cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
            std::memcpy(m.pData, &p, sizeof(p));
            c->Unmap(g_gpu.cb.Get(), 0);
        }

        void DrawQuad(ID3D11DeviceContext* c, const HostCore::Box& b, float tw, float th, const Gradient* g,
                      ID3D11ShaderResourceView* pic)
        {
            Params p{};
            p.rect[0] = b.rect.x;
            p.rect[1] = b.rect.y;
            p.rect[2] = b.rect.w;
            p.rect[3] = b.rect.h;
            p.target[0] = tw;
            p.target[1] = th;
            p.target[2] = b.radius;
            p.target[3] = pic ? 1.0f : 0.0f;
            if (g) {
                p.grad[0] = g->cx;
                p.grad[1] = g->cy;
                p.grad[2] = g->rx;
                p.grad[3] = g->ry;
                std::copy_n(g->inner, 3, p.inner);
                std::copy_n(g->outer, 3, p.outer);
                p.inner[3] = g->stop;
            }
            Upload(c, p);
            const D3D11_RECT sc{ static_cast<LONG>(std::floor(b.clip.x)), static_cast<LONG>(std::floor(b.clip.y)),
                                 static_cast<LONG>(std::ceil(b.clip.x + b.clip.w)),
                                 static_cast<LONG>(std::ceil(b.clip.y + b.clip.h)) };
            c->RSSetScissorRects(1, &sc);
            c->PSSetShaderResources(0, 1, &pic);
            c->Draw(4, 0);
        }

        void Draw()
        {
            std::vector<PageBox> boxes;
            std::map<std::string, ComPtr<ID3D11ShaderResourceView>, std::less<>> images;
            float pageW = 0, pageH = 0;
            {
                std::lock_guard lock(g_mutex);
                if (!g_shown) return;
                boxes = g_boxes;
                images = g_images;
                pageW = g_pageW;
                pageH = g_pageH;
            }
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (!renderer) return;
            auto* device = reinterpret_cast<ID3D11Device*>(renderer->GetRuntimeData().forwarder);
            auto* ctx = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);
            if (!device || !ctx || !EnsureGpu(device)) return;

            LARGE_INTEGER t0{}, t1{}, freq{};
            ::QueryPerformanceCounter(&t0);

            // Every bound target is put back, not only the first: OMSetRenderTargets unbinds the slots it is not given.
            SavedState saved;
            ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved.rtvs, &saved.dsv);
            ID3D11RenderTargetView* rtv = saved.rtvs[0];
            if (!rtv) return;
            float tw = 0, th = 0;
            {
                ComPtr<ID3D11Resource> res;
                rtv->GetResource(&res);
                ComPtr<ID3D11Texture2D> tex;
                if (!res || FAILED(res.As(&tex))) return;
                D3D11_TEXTURE2D_DESC td{};
                tex->GetDesc(&td);
                tw = static_cast<float>(td.Width);
                th = static_cast<float>(td.Height);
            }
            // Drawn into whatever target the UI render left bound (the page's boxes are scaled to it). An upscaler or
            // another menu's PostDisplay can leave a target other than the frame buffer bound: said once, with sizes.
            static std::atomic<bool> s_saidTarget{ false };
            const auto& fb = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER];
            if (fb.RTV && rtv != fb.RTV && !s_saidTarget.exchange(true)) {
                D3D11_TEXTURE2D_DESC fd{};
                if (fb.texture) fb.texture->GetDesc(&fd);
                SKSE::log::warn("underlay: the bound target ({}x{}) is not the frame buffer ({}x{}); drawing into the bound one",
                                tw, th, fd.Width, fd.Height);
            }
            saved.Save(ctx);

            ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            ctx->IASetInputLayout(nullptr);
            ctx->VSSetShader(g_gpu.vs.Get(), nullptr, 0);
            ctx->PSSetShader(g_gpu.ps.Get(), nullptr, 0);
            ctx->GSSetShader(nullptr, nullptr, 0);
            ctx->HSSetShader(nullptr, nullptr, 0);
            ctx->DSSetShader(nullptr, nullptr, 0);
            ID3D11Buffer* cb = g_gpu.cb.Get();
            ctx->VSSetConstantBuffers(0, 1, &cb);
            ctx->PSSetConstantBuffers(0, 1, &cb);
            ID3D11SamplerState* smp = g_gpu.sampler.Get();
            ctx->PSSetSamplers(0, 1, &smp);
            const float zero[4]{};
            ctx->OMSetBlendState(g_gpu.blend.Get(), zero, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(g_gpu.depth.Get(), 0);
            ctx->RSSetState(g_gpu.raster.Get());
            const D3D11_VIEWPORT vp{ 0, 0, tw, th, 0, 1 };
            ctx->RSSetViewports(1, &vp);
            ctx->OMSetRenderTargets(1, &rtv, nullptr);

            HostCore::Box screen{ { 0, 0, tw, th }, { 0, 0, tw, th }, 0 };
            DrawQuad(ctx, screen, tw, th, &kBackdrop, nullptr);
            for (const auto& pb : boxes) {
                HostCore::Box b;
                if (!HostCore::ToTarget(pb.box, pageW, pageH, tw, th, b)) continue;
                DrawQuad(ctx, b, tw, th, &kStage, nullptr);
                if (const auto it = images.find(pb.slot); it != images.end() && it->second)
                    DrawQuad(ctx, b, tw, th, nullptr, it->second.Get());
            }
            ID3D11ShaderResourceView* none = nullptr;
            ctx->PSSetShaderResources(0, 1, &none);
            saved.Restore(ctx);

            ::QueryPerformanceCounter(&t1);
            ::QueryPerformanceFrequency(&freq);
            const double ms = freq.QuadPart ? double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart) : 0.0;
            g_cost.frames++;
            g_cost.totalMs += ms;
            g_cost.worstMs = std::max(g_cost.worstMs, ms);
            if (g_cost.frames == 600) {
                SKSE::log::info("underlay: {} boxes, CPU {:.3f} ms a frame on average, {:.3f} ms at worst (600 frames, "
                                "target {}x{})",
                                boxes.size(), g_cost.totalMs / 600.0, g_cost.worstMs, tw, th);
                g_cost = {};
            }
        }

        class UnderlayMenu final : public RE::IMenu
        {
        public:
            UnderlayMenu()
            {
                depthPriority = kDepth;
                menuFlags.set(RE::UI_MENU_FLAGS::kCustomRendering, RE::UI_MENU_FLAGS::kRendersUnderPauseMenu,
                              RE::UI_MENU_FLAGS::kAdvancesUnderPauseMenu, RE::UI_MENU_FLAGS::kAllowSaving,
                              RE::UI_MENU_FLAGS::kDontHideCursorWhenTopmost);
                // Phantasm's blank movie (Interface/phantasm.swf): the engine expects a menu to have one.
                if (auto* sm = RE::BSScaleformManager::GetSingleton()) sm->LoadMovie(this, uiMovie, "phantasm");
            }

            static RE::IMenu* Create() { return new UnderlayMenu(); }

            // The page has the keyboard and the mouse (PrismaUI's focus); a user event passes on untouched.
            RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& msg) override
            {
                if (msg.type == RE::UI_MESSAGE_TYPE::kUserEvent) return RE::UI_MESSAGE_RESULTS::kPassOn;
                return RE::IMenu::ProcessMessage(msg);
            }

            void PostDisplay() override { Draw(); }
        };

        bool g_registered = false;
    }

    void Register()
    {
        if (g_registered) return;
        auto* ui = RE::UI::GetSingleton();
        if (!ui) return;
        ui->Register(kMenuName, UnderlayMenu::Create);
        g_registered = true;
        SKSE::log::info("underlay: menu '{}' registered", kMenuName);
    }

    void Show(bool on)
    {
        {
            std::lock_guard lock(g_mutex);
            g_shown = on;
            if (!on) {
                g_boxes.clear();
                g_images.clear();
            }
        }
        if (!g_registered) return;
        if (auto* q = RE::UIMessageQueue::GetSingleton())
            q->AddMessage(kMenuName, on ? RE::UI_MESSAGE_TYPE::kShow : RE::UI_MESSAGE_TYPE::kHide, nullptr);
    }

    void SetImage(const std::string& slot, ID3D11ShaderResourceView* srv)
    {
        std::lock_guard lock(g_mutex);
        g_images[slot] = srv;   // ComPtr: AddRef'd here, released when replaced or cleared
    }

    void ClearImage(const std::string& slot)
    {
        std::lock_guard lock(g_mutex);
        g_images.erase(slot);
    }

    void SetBoxes(float pageW, float pageH, std::vector<PageBox> boxes)
    {
        if (boxes.size() > HostCore::kMaxBoxes) boxes.resize(HostCore::kMaxBoxes);
        std::lock_guard lock(g_mutex);
        g_pageW = pageW;
        g_pageH = pageH;
        g_boxes = std::move(boxes);
    }
}
