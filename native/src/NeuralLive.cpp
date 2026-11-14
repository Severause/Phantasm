#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "NeuralLive.h"

#include "NeuralStills.h"

#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>

namespace Phantasm::NeuralLive
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        enum class State : int
        {
            kOff,         // nothing attached: the next live frame attaches
            kAttaching,   // the worker is importing the textures and making the feature
            kReady,       // frames go through NR
            kDetaching,   // the worker is letting the objects go
            kFailed,      // NR could not run live: frames stay as drawn for the session
        };
        std::atomic<State> g_state{ State::kOff };

        // The attached view's D3D11 side. Written by the render thread while kOff (attach), read by it while kReady,
        // released by the worker while kDetaching: g_mx keeps a frame and the move to kDetaching apart.
        struct Objects
        {
            ComPtr<ID3D11Texture2D>          in, depth, out;
            ComPtr<ID3D11RenderTargetView>   inRtv, depthRtv, offRtv;
            ComPtr<ID3D11ShaderResourceView> outSrv;
            ComPtr<ID3D11Fence>              fence;
            ID3D11Texture2D*                 offscreen = nullptr;   // compare only: the offscreen offRtv writes
            std::uint32_t                    w = 0, h = 0, view = 0, fid = 0;
            std::uint64_t                    value = 0;   // the fence's last value set or queued
            bool                             reset = true;
        } g_obj;
        std::mutex g_mx;

        // Steady milliseconds of the attached view's last frame: another view takes over only once it has stopped
        // (kOwnerGoneMs without a frame) or ended (OnViewEnded sets 0).
        std::atomic<std::int64_t> g_lastFrameMs{ 0 };
        std::atomic<bool>         g_detachPosted{ false };
        // The view the objects are (being) attached for, and the view a detach was posted to make room for: such a
        // view's frames are published as drawn meanwhile, and Process asks for another until one has gone through NR.
        std::atomic<std::uint32_t> g_holder{ 0 };
        std::atomic<std::uint32_t> g_next{ 0 };
        constexpr std::int64_t    kOwnerGoneMs = 1000;
        constexpr std::int64_t    kIdleDetachMs = 30000;

        // The scaling shaders, built once per device. Render thread.
        ID3D11Device*               g_shaderDevice = nullptr;
        ComPtr<ID3D11VertexShader>  g_vs;
        ComPtr<ID3D11PixelShader>   g_psColor, g_psDepth, g_psUp;
        ComPtr<ID3D11SamplerState>  g_linear;

        constexpr const char* kShaders = R"(
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID)
{
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}
Texture2D<float4> tColor : register(t0);
Texture2D<float>  tDepth : register(t0);
SamplerState      sLinear : register(s0);
float4 PSColor(VSOut i) : SV_Target { return float4(tColor.SampleLevel(sLinear, i.uv, 0).rgb, 1); }
float  PSDepth(VSOut i) : SV_Target
{
    float2 size;
    tDepth.GetDimensions(size.x, size.y);
    return tDepth.Load(int3(min(i.uv * size, size - 1), 0));
}
float4 PSUp(VSOut i) : SV_Target { return float4(tColor.SampleLevel(sLinear, i.uv, 0).rgb, 1); }
)";

        std::int64_t NowMs()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        void Fail(const char* why)
        {
            SKSE::log::warn("Phantasm NR live: {} - live views stay as drawn", why);
            g_state = State::kFailed;
        }

        bool BuildShaders(ID3D11Device* device)
        {
            if (g_shaderDevice == device && g_vs) return true;
            g_vs.Reset();
            g_psColor.Reset();
            g_psDepth.Reset();
            g_psUp.Reset();
            g_linear.Reset();
            const auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& blob) {
                ComPtr<ID3DBlob> errors;
                if (SUCCEEDED(D3DCompile(kShaders, std::strlen(kShaders), "PhantasmNeuralLive", nullptr, nullptr, entry, target,
                                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
                    return true;
                }
                SKSE::log::error("Phantasm NR live: shader {} did not compile: {}", entry,
                                 errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
                return false;
            };
            ComPtr<ID3DBlob> vs, pc, pd, pu;
            if (!compile("VS", "vs_5_0", vs) || !compile("PSColor", "ps_5_0", pc) || !compile("PSDepth", "ps_5_0", pd) ||
                !compile("PSUp", "ps_5_0", pu)) {
                return false;
            }
            D3D11_SAMPLER_DESC sd{};
            sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.MaxLOD   = D3D11_FLOAT32_MAX;
            if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs)) ||
                FAILED(device->CreatePixelShader(pc->GetBufferPointer(), pc->GetBufferSize(), nullptr, &g_psColor)) ||
                FAILED(device->CreatePixelShader(pd->GetBufferPointer(), pd->GetBufferSize(), nullptr, &g_psDepth)) ||
                FAILED(device->CreatePixelShader(pu->GetBufferPointer(), pu->GetBufferSize(), nullptr, &g_psUp)) ||
                FAILED(device->CreateSamplerState(&sd, &g_linear))) {
                g_vs.Reset();
                return false;
            }
            g_shaderDevice = device;
            return true;
        }

        // A texture NR's Vulkan device imports: shared by NT handle. Render thread.
        bool SharedTexture(ID3D11Device* device, DXGI_FORMAT format, std::uint32_t w, std::uint32_t h,
                           ComPtr<ID3D11Texture2D>& tex, HANDLE& handle)
        {
            D3D11_TEXTURE2D_DESC td{};
            td.Width            = w;
            td.Height           = h;
            td.MipLevels        = 1;
            td.ArraySize        = 1;
            td.Format           = format;
            td.SampleDesc.Count = 1;
            td.Usage            = D3D11_USAGE_DEFAULT;
            td.BindFlags        = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            td.MiscFlags        = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            if (FAILED(device->CreateTexture2D(&td, nullptr, &tex))) return false;
            ComPtr<IDXGIResource1> dxgi;
            return SUCCEEDED(tex.As(&dxgi)) &&
                   SUCCEEDED(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                                                      &handle));
        }

        // Makes the view's objects and hands them to the worker to attach. Render thread, state kOff.
        void Attach(ID3D11Device* device, ID3D11Texture2D* offscreen, std::uint32_t w, std::uint32_t h,
                    std::uint32_t view, std::uint32_t fid)
        {
            if (!BuildShaders(device)) return Fail("the scaling shaders did not build");
            ComPtr<ID3D11Device5> device5;
            if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device5)))) return Fail("the game's device has no fences (D3D11.4)");
            Objects o;
            HANDLE  hIn = nullptr, hDepth = nullptr, hOut = nullptr, hFence = nullptr;
            const auto closeAll = [&]() {
                for (HANDLE h2 : { hIn, hDepth, hOut, hFence }) {
                    if (h2) ::CloseHandle(h2);
                }
            };
            if (!SharedTexture(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, o.in, hIn) ||
                !SharedTexture(device, DXGI_FORMAT_R32_FLOAT, w, h, o.depth, hDepth) ||
                !SharedTexture(device, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, o.out, hOut) ||
                FAILED(device->CreateRenderTargetView(o.in.Get(), nullptr, &o.inRtv)) ||
                FAILED(device->CreateRenderTargetView(o.depth.Get(), nullptr, &o.depthRtv)) ||
                FAILED(device->CreateShaderResourceView(o.out.Get(), nullptr, &o.outSrv)) ||
                FAILED(device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&o.fence))) ||
                FAILED(o.fence->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &hFence))) {
                closeAll();
                return Fail("the game's device refused the shared textures or fence");
            }
            D3D11_RENDER_TARGET_VIEW_DESC rvd{};
            rvd.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;   // NR's answer is display-encoded already
            rvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            if (FAILED(device->CreateRenderTargetView(offscreen, &rvd, &o.offRtv))) {
                closeAll();
                return Fail("the offscreen took no UNORM render target view");
            }
            o.offscreen = offscreen;
            o.w         = w;
            o.h         = h;
            o.view      = view;
            o.fid       = fid;
            {
                std::lock_guard lock(g_mx);
                g_obj = std::move(o);
            }
            g_holder      = view;
            g_next        = 0;
            g_state       = State::kAttaching;
            g_lastFrameMs = NowMs();
            NeuralStills::Post([w, h, hIn, hDepth, hOut, hFence](bool started) {
                const auto* api = NeuralStills::Live();
                char        why[512]{};
                const bool  ok  = started && api && api->attach(w, h, hIn, hDepth, hOut, hFence, why, sizeof(why));
                for (HANDLE h2 : { hIn, hDepth, hOut, hFence }) ::CloseHandle(h2);
                if (!ok) {
                    {
                        std::lock_guard lock(g_mx);
                        g_obj = Objects{};
                    }
                    SKSE::log::warn("Phantasm NR live: not attached: {}", started ? why : "NR did not start");
                    g_holder = 0;
                    // NR that did not start may yet (its switch, the followed mod's, a library named later): only an
                    // attach NR refused, or a start that failed for good, ends live NR for the session.
                    g_state = started || NeuralStills::StartFailed() ? State::kFailed : State::kOff;
                    return;
                }
                SKSE::log::info("Phantasm NR live: attached at {}x{}", w, h);
                g_state = State::kReady;
            });
        }

        // Lets the attached view go, on the worker. From kReady (or kFailed after a frame failed).
        void PostDetach(State after)
        {
            if (g_detachPosted.exchange(true)) return;
            NeuralStills::Post([after](bool) {
                {
                    std::lock_guard lock(g_mx);
                    const State s = g_state.load();
                    if (s != State::kReady && s != State::kFailed) {
                        g_detachPosted = false;
                        return;
                    }
                    g_state = State::kDetaching;
                }
                if (const auto* api = NeuralStills::Live()) api->detach();
                {
                    std::lock_guard lock(g_mx);
                    g_obj = Objects{};
                }
                g_holder       = 0;
                g_detachPosted = false;
                g_state        = after;
                SKSE::log::info("Phantasm NR live: detached");
            });
        }

        void Draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, std::uint32_t w, std::uint32_t h,
                  ID3D11PixelShader* ps, ID3D11ShaderResourceView* src)
        {
            D3D11_VIEWPORT vp{};
            vp.Width    = static_cast<float>(w);
            vp.Height   = static_cast<float>(h);
            vp.MaxDepth = 1.0f;
            context->OMSetRenderTargets(1, &target, nullptr);
            context->RSSetViewports(1, &vp);
            context->PSSetShader(ps, nullptr, 0);
            context->PSSetShaderResources(0, 1, &src);
            context->Draw(3, 0);
            ID3D11ShaderResourceView* none = nullptr;
            context->PSSetShaderResources(0, 1, &none);
        }
    }

    bool Process(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* offscreen,
                 ID3D11ShaderResourceView* offscreenSrv, ID3D11Texture2D* depth, ID3D11Texture2D* depthCopy,
                 ID3D11ShaderResourceView* depthCopySrv, std::uint32_t offW, std::uint32_t offH, std::uint32_t view,
                 std::uint32_t fid)
    {
        if (!NeuralStills::LiveOn() || !NeuralStills::Enabled() || !device || !context || !offscreen || !offscreenSrv || !offW ||
            !offH) {
            return false;
        }
        const float scale = NeuralStills::LiveScale();
        // NR is on its way for this view: its frame is published as drawn and another is asked for.
        const auto pending = [view]() { return view != 0 && (g_holder.load() == view || g_next.load() == view); };
        const State state = g_state.load();
        if (state == State::kFailed) return false;
        if (state == State::kAttaching || state == State::kDetaching) return pending();
        const std::uint32_t w   = std::max(64u, static_cast<std::uint32_t>(std::lround(offW * scale)));
        const std::uint32_t h   = std::max(64u, static_cast<std::uint32_t>(std::lround(offH * scale)));
        const std::int64_t  now = NowMs();
        if (state == State::kOff) {
            if (g_detachPosted) return pending();
            Attach(device, offscreen, w, h, view, fid);
            return g_state.load() != State::kFailed;
        }

        std::unique_lock lock(g_mx, std::try_to_lock);
        if (!lock.owns_lock() || g_state.load() != State::kReady) return pending();   // the worker is letting it go
        Objects& o = g_obj;
        if (o.view != view) {
            // Another view: it takes over once the attached one has ended or stopped drawing.
            if (now - g_lastFrameMs.load() < kOwnerGoneMs) return false;
            if (o.w != w || o.h != h) {
                g_next = view;
                lock.unlock();
                PostDetach(State::kOff);
                return true;
            }
            // The same size: the objects serve this view as they are, from this frame on.
            o.view   = view;
            o.reset  = true;
            g_holder = view;
        }
        if (o.w != w || o.h != h) {
            g_next = view;
            lock.unlock();
            PostDetach(State::kOff);
            return true;
        }
        if (o.offscreen != offscreen) {
            // The offscreen was rebuilt at the same size.
            D3D11_RENDER_TARGET_VIEW_DESC rvd{};
            rvd.Format        = DXGI_FORMAT_R8G8B8A8_UNORM;
            rvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            o.offRtv.Reset();
            if (FAILED(device->CreateRenderTargetView(offscreen, &rvd, &o.offRtv))) return false;
            o.offscreen = offscreen;
            o.reset     = true;
        }
        if (o.fid != fid) {
            o.fid   = fid;
            o.reset = true;
        }
        const auto* api = NeuralStills::Live();
        ComPtr<ID3D11DeviceContext4> context4;
        if (!api || FAILED(context->QueryInterface(IID_PPV_ARGS(&context4)))) return false;

        // The frame scaled into NR's input, with its depth (0 near; a missing copy reads as far).
        context->RSSetState(nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
        context->OMSetDepthStencilState(nullptr, 0);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(g_vs.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
        ID3D11SamplerState* sampler = g_linear.Get();
        context->PSSetSamplers(0, 1, &sampler);
        Draw(context, o.inRtv.Get(), w, h, g_psColor.Get(), offscreenSrv);
        if (depth && depthCopy && depthCopySrv) {
            context->OMSetRenderTargets(0, nullptr, nullptr);
            context->CopyResource(depthCopy, depth);
            Draw(context, o.depthRtv.Get(), w, h, g_psDepth.Get(), depthCopySrv);
        } else {
            const float farDepth[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
            context->ClearRenderTargetView(o.depthRtv.Get(), farDepth);
        }
        context->OMSetRenderTargets(0, nullptr, nullptr);

        // NR on its own queue between two fence values; the game's GPU waits for the second only once it is queued.
        const std::uint64_t ready = ++o.value;
        context4->Signal(o.fence.Get(), ready);
        context->Flush();
        const PhantasmNR_Settings settings = NeuralStills::CurrentSettings();
        char                      why[512]{};
        const auto                t0 = std::chrono::steady_clock::now();
        if (!api->evaluate(ready, ready + 1, o.reset ? 1 : 0, &settings, why, sizeof(why))) {
            SKSE::log::warn("Phantasm NR live: a frame was not queued: {} - live views stay as drawn", why);
            lock.unlock();
            PostDetach(State::kFailed);
            return false;
        }
        o.value = ready + 1;
        context4->Wait(o.fence.Get(), o.value);
        Draw(context, o.offRtv.Get(), offW, offH, g_psUp.Get(), o.outSrv.Get());
        context->OMSetRenderTargets(0, nullptr, nullptr);

        static std::atomic<int> s_logged{ 0 };
        if (s_logged.fetch_add(1) < 3 || o.reset) {
            SKSE::log::info("Phantasm NR live: frame {}x{} -> {}x{} queued in {:.2f} ms{}", offW, offH, w, h,
                            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(),
                            o.reset ? " (history started)" : "");
        }
        o.reset = false;
        g_lastFrameMs = now;
        return false;
    }

    void OnViewEnded(std::uint32_t view)
    {
        if (view != 0 && g_holder.load() == view) g_lastFrameMs = 0;
    }

    void OnWorkerIdle()
    {
        if (g_state.load() == State::kReady && NowMs() - g_lastFrameMs.load() >= kIdleDetachMs) PostDetach(State::kOff);
    }
}
