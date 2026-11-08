// Phantasm Showcase - ImGuiPanel: see ImGuiPanel.h.

#include "PCH.h"

#include "ImGuiPanel.h"

#include "PanelCore.h"
#include "People.h"
#include "ViewCore.h"
#include "Views.h"

#include <SKSEMenuFramework.h>

#include <charconv>
#include <condition_variable>
#include <d3d11.h>
#include <wrl/client.h>

namespace Showcase::ImGuiPanel
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        using PHANTASM_API::EventKind;
        using PHANTASM_API::ViewId;
        namespace IG = ImGuiMCP;

        constexpr const char* kSection = "Phantasm Showcase";
        // The ImGui the framework's header describes (its structs and key numbers).
        constexpr std::string_view kImGuiVersion = "1.90.";
        // Every export the panel's calls reach: the header's ImGui wrappers call them without a null check.
        constexpr std::array kExports = {
            "AddSectionItem", "AddWindow", "igBegin", "igEnd", "igSetNextWindowPos", "igSetNextWindowSize", "igGetIO",
            "igGetCurrentContext", "igGetVersion", "igGetFrameCount", "igGetCursorScreenPos", "igSetCursorScreenPos",
            "igGetWindowDrawList", "igGetContentRegionAvail", "igInvisibleButton", "igIsItemActive", "igIsItemHovered",
            "igSetItemKeyOwner", "igGetMousePos", "igGetMouseDragDelta", "igResetMouseDragDelta", "igCalcTextSize",
            "igSetNextItemWidth", "igBeginCombo", "igSelectable_Bool", "igEndCombo", "igTextWrappedV",
            "igTextDisabledV", "igPushID_Int", "igPopID", "igSameLine", "igButton", "igCheckbox",
            "igGetTextLineHeightWithSpacing", "igGetFrameHeightWithSpacing", "ImDrawList_AddRectFilledMultiColor",
            "ImDrawList_AddCallback", "ImDrawList_AddImage", "ImDrawList_AddText_FontPtr",
        };
        // A panel not drawn for this long (the menu closed, another section shown) has its view closed: a view nobody
        // closes keeps drawing during play. It is also what makes a picture safe to let go: by then ImGui has
        // rendered every list that named it.
        constexpr double kIdleCloseMs = 1000.0;
        // How long after Phantasm refused the view, or ended it for a reason that passes, the panel asks again.
        constexpr double kRetryMs = 2000.0;
        // The pages' stage (ui/src/styles/app.css .live), top to bottom, behind a picture with clear ground.
        const IG::ImU32 kStageTop = IM_COL32(0x1a, 0x1d, 0x23, 0xff);
        const IG::ImU32 kStageBottom = IM_COL32(0x0d, 0x0e, 0x11, 0xff);
        const IG::ImU32 kNoteColour = IM_COL32(0xb4, 0xb8, 0xc0, 0xff);

        double NowMs()
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        struct Person
        {
            std::uint32_t id = 0;
            std::string   label;
        };

        enum class State
        {
            Closed,
            Opening,
            Shown,
            Waiting,
            Ended,
        };

        // What the game thread tells the panel (OnViewEvent, the people task). Copied out by the draw; never held
        // across a call out.
        std::mutex                       g_dataLock;
        ComPtr<ID3D11ShaderResourceView> g_srv;   // the view's picture while it draws
        State                            g_state = State::Closed;
        ViewId                           g_stateView = 0;   // the view g_state is about
        std::uint32_t                    g_wantForm = 0;    // whom the view should show: a re-target keeps the view's id
        std::string                      g_reason;          // Waiting's and Ended's
        std::uint32_t                    g_endedForm = 0;
        std::vector<Person>              g_people;
        std::uint32_t                    g_crosshair = 0;
        std::uint32_t                    g_peopleSerial = 0;

        // The draw's own state: the render thread's, and the pump's or OnPreLoad's while they close the view. Held
        // for the whole draw, so nothing closes a view the draw is using; taken before Views' lock and g_dataLock.
        std::mutex g_renderLock;
        struct Choices
        {
            std::uint32_t target = 0;   // whom the panel shows; 0 until the people list arrives
            int           light = 0;    // PanelCore::kLights
            int           expression = 0;
            bool          floor = true;
            bool          weapons = false;
            bool          classicPose = false;
            bool          motion = true;
        } g_choices;
        // Every picture handed to ImGui in the current ImGui frame: the frame's lists are rendered after the section
        // and the window have both returned, so each is kept until the panel's first draw of a later frame, or its
        // idle close.
        std::vector<ComPtr<ID3D11ShaderResourceView>> g_frameSrvs;
        int                                           g_frame = -1;
        PanelCore::ResizeGate                         g_gate;
        bool                                          g_viewOpen = false;   // a view of the panel's may exist
        double                                        g_lastDraw = 0;
        double                                        g_retryAt = 0;
        double                                        g_smallSince = 0;   // when the box became too small, else 0
        int                                           g_noImageEnds = 0;  // "no-image" ends in a row
        std::string                                   g_refused;   // Views' why when Phantasm refused the view
        std::uint32_t                                 g_seenSerial = 0;
        // ImGuiIO's fields are read only where the header's layout of it is the framework's: its Ctx, which sits just
        // before the mouse fields the panel reads, must be the current context. Checked at the first draw.
        bool g_ioChecked = false;
        bool g_ioMatches = false;

        SKSEMenuFramework::Model::WindowInterface* g_window = nullptr;

        // The pump thread: asks for the people list (an SKSE task, never posted from inside Present) and closes a view
        // the panel stopped drawing.
        std::mutex              g_pumpLock;
        std::condition_variable g_pumpWake;
        bool                    g_wantPeople = false;

        // The premultiplied blend the picture is drawn with: Phantasm's offscreen is premultiplied, ImGui blends
        // straight alpha, which darkens every soft edge (hair, the silhouette). Built by the draw; used by ImGui's
        // render of the frame, on the same thread, after the draw returns.
        ComPtr<ID3D11DeviceContext> g_context;
        ComPtr<ID3D11BlendState>    g_premultiplied;
        ComPtr<ID3D11BlendState>    g_savedBlend;
        float                       g_savedFactor[4]{};
        UINT                        g_savedMask = 0;
        bool                        g_blendFailed = false;

        void RefreshPeople()
        {
            std::vector<Person> people;
            for (const auto& row : People::List()) {
                const auto hex = row.value("formId", std::string());
                std::uint32_t id = 0;
                std::from_chars(hex.data(), hex.data() + hex.size(), id, 16);
                if (!id) continue;
                std::string label = row.value("name", std::string());
                if (row.value("group", std::string()) == "follower") label += " (follower)";
                people.push_back({ id, std::move(label) });
            }
            const auto crosshair = People::CrosshairActor();
            std::lock_guard lock(g_dataLock);
            g_people = std::move(people);
            g_crosshair = crosshair;
            ++g_peopleSerial;
        }

        void RequestPeople()
        {
            {
                std::lock_guard lock(g_pumpLock);
                g_wantPeople = true;
            }
            g_pumpWake.notify_one();
        }

        /// Closes the panel's view (if Views still holds one) and forgets its state. Under g_renderLock. The frame's
        /// pictures are kept: a list ImGui has not rendered yet may name one.
        void CloseView()
        {
            Views::Close(ViewCore::kPanelSlot);
            g_viewOpen = false;
            g_gate.Reset();
            g_smallSince = 0;
            std::lock_guard lock(g_dataLock);
            g_srv.Reset();
            g_state = State::Closed;
            g_stateView = 0;
            g_reason.clear();
            g_endedForm = 0;
        }

        void Pump()
        {
            for (;;) {
                bool people = false;
                {
                    std::unique_lock lock(g_pumpLock);
                    g_pumpWake.wait_for(lock, std::chrono::milliseconds(500), [] { return g_wantPeople; });
                    people = std::exchange(g_wantPeople, false);
                }
                // SKSE holds its queue's lock for a whole drain: AddTask here, never from the frame's Present.
                if (people) {
                    if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask(&RefreshPeople);
                }
                std::lock_guard lock(g_renderLock);
                if ((g_viewOpen || !g_frameSrvs.empty()) && NowMs() - g_lastDraw >= kIdleCloseMs) {
                    if (g_viewOpen) SKSE::log::info("imgui: the panel is not shown - its view is closed");
                    CloseView();
                    g_frameSrvs.clear();
                }
            }
        }

        bool EnsurePremultipliedBlend(ID3D11ShaderResourceView* srv)
        {
            if (g_premultiplied) return true;
            if (g_blendFailed || !srv) return false;
            ComPtr<ID3D11Device> device;
            srv->GetDevice(&device);
            D3D11_BLEND_DESC desc{};
            auto& rt = desc.RenderTarget[0];
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D11_BLEND_ONE;
            rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            const HRESULT hr = device ? device->CreateBlendState(&desc, &g_premultiplied) : E_POINTER;
            if (FAILED(hr)) {
                SKSE::log::error("imgui: the blend state failed ({:#x}); soft edges draw darker", static_cast<std::uint32_t>(hr));
                g_blendFailed = true;
                return false;
            }
            device->GetImmediateContext(&g_context);
            return true;
        }

        // ImGui's dx11 renderer sets its blend state once per frame, so the picture's is set around it and put back.
        void BlendPremultiplied(const IG::ImDrawList*, const IG::ImDrawCmd*)
        {
            if (!g_context || !g_premultiplied) return;
            g_context->OMGetBlendState(g_savedBlend.ReleaseAndGetAddressOf(), g_savedFactor, &g_savedMask);
            g_context->OMSetBlendState(g_premultiplied.Get(), nullptr, 0xFFFFFFFF);
        }

        void BlendRestore(const IG::ImDrawList*, const IG::ImDrawCmd*)
        {
            if (!g_context) return;
            g_context->OMSetBlendState(g_savedBlend.Get(), g_savedFactor, g_savedMask);
            g_savedBlend.Reset();
        }

        /// True when ImGuiIO's fields may be read (see g_ioMatches). The first call logs a mismatch.
        bool IoMatches()
        {
            if (g_ioChecked) return g_ioMatches;
            const auto* io = IG::GetIO();
            const auto* context = IG::GetCurrentContext();
            if (!context) return false;   // no frame yet: asked again at the next draw
            g_ioChecked = true;
            g_ioMatches = io && io->Ctx == context;
            if (!g_ioMatches)
                SKSE::log::warn("imgui: the framework's ImGuiIO is not laid out as its header says - no wheel zoom, and "
                                "the window opens at a fixed size");
            return g_ioMatches;
        }

        Views::Hints HintsNow()
        {
            const auto& c = g_choices;
            return {
                { "pose", c.classicPose ? "classic" : "captured" },
                { "motion", c.motion ? "on" : "off" },
                { "lighting", std::string(PanelCore::kLights[c.light].value) },
                { "floor", c.floor ? "on" : "off" },
                { "weapons", c.weapons ? "on" : "off" },
                // "" removes the hint: the face the game gives them.
                { "expression", std::string(PanelCore::kExpressions[c.expression].value) },
            };
        }

        /// Whom the panel shows next. Under g_renderLock.
        void Choose(std::uint32_t id)
        {
            if (id == g_choices.target) return;
            g_choices.target = id;
            g_retryAt = 0;
            g_noImageEnds = 0;
            g_refused.clear();
            if (!id) CloseView();
            std::lock_guard lock(g_dataLock);
            g_wantForm = id;
            if (!id) return;
            // A re-target keeps the view's id and brings a new Shown; until then the old actor is not shown as the new
            // (OnViewEvent drops what is still on its way for them).
            g_srv.Reset();
            g_state = g_viewOpen ? State::Opening : State::Closed;
            g_reason.clear();
            g_endedForm = 0;
        }

        void KeepViewOpen(const PanelCore::Box& box, double now)
        {
            if (!g_choices.target || now < g_retryAt) return;
            const auto size = g_gate.Update({ static_cast<std::uint32_t>(box.w), static_cast<std::uint32_t>(box.h) }, now);
            std::string why;
            ViewId view = 0;
            if (!Views::OpenPanel(g_choices.target, size.w, size.h, HintsNow(), why, view)) {
                g_viewOpen = false;   // OpenSlot closed any view the slot held
                g_refused = why;
                g_retryAt = now + kRetryMs;
                return;
            }
            g_viewOpen = true;
            g_refused.clear();
            // A new view (the first, or one after the last ended): what the panel knows was about an older one.
            std::lock_guard lock(g_dataLock);
            if (g_stateView != view) {
                g_stateView = view;
                g_srv.Reset();
                g_state = State::Opening;
                g_reason.clear();
                g_endedForm = 0;
            }
        }

        std::string StageNote(State state, const std::string& reason)
        {
            if (!g_choices.target) return "Choose someone.";
            if (!g_refused.empty()) return PanelCore::Note(g_refused);
            if (state == State::Waiting || state == State::Ended) return PanelCore::Note(reason);
            return "Opening the view...";
        }

        void DrawStage(const PanelCore::Box& box, float availW, const ComPtr<ID3D11ShaderResourceView>& srv,
                       State state, const std::string& reason)
        {
            auto pos = IG::GetCursorScreenPos();
            pos.x += std::floor((availW - box.w) * 0.5f);
            IG::SetCursorScreenPos(pos);
            const IG::ImVec2 end{ pos.x + box.w, pos.y + box.h };
            auto* list = IG::GetWindowDrawList();
            IG::ImDrawListManager::AddRectFilledMultiColor(list, pos, end, kStageTop, kStageTop, kStageBottom, kStageBottom);

            IG::InvisibleButton("##phantasm-stage", IG::ImVec2(box.w, box.h));
            const bool held = IG::IsItemActive(), hovered = IG::IsItemHovered();
            IG::SetItemKeyOwner(IG::ImGuiKey_MouseWheelY, 0);   // the wheel zooms; the window does not scroll
            if (g_viewOpen && state == State::Shown) {
                if (held) {
                    const auto d = IG::GetMouseDragDelta(0, 0.0f);
                    IG::ResetMouseDragDelta(0);
                    if (d.x != 0 || d.y != 0) {
                        const auto o = PanelCore::Orbit(d.x, d.y);
                        Views::Orbit(ViewCore::kPanelSlot, o[0], o[1]);
                    }
                }
                const float wheel = IoMatches() ? IG::GetIO()->MouseWheel : 0.0f;
                if (hovered && wheel != 0) {
                    const auto mouse = IG::GetMousePos();
                    Views::Zoom(ViewCore::kPanelSlot, PanelCore::Zoom(wheel), (mouse.x - pos.x) / box.w,
                                (mouse.y - pos.y) / box.h);
                }
            }

            if (srv) {
                g_frameSrvs.push_back(srv);
                const bool premultiplied = EnsurePremultipliedBlend(srv.Get());
                if (premultiplied) IG::ImDrawListManager::AddCallback(list, &BlendPremultiplied, nullptr);
                IG::ImDrawListManager::AddImage(list, srv.Get(), pos, end, IG::ImVec2(0, 0), IG::ImVec2(1, 1), IM_COL32_WHITE);
                if (premultiplied) IG::ImDrawListManager::AddCallback(list, &BlendRestore, nullptr);
                return;
            }
            // The note, wrapped to the stage and centred in it.
            const auto  note = StageNote(state, reason);
            const float wrap = std::max(box.w - 32.0f, 64.0f);
            const auto  size = IG::CalcTextSize(note.c_str(), nullptr, false, wrap);
            IG::ImDrawListManager::AddText(list, nullptr, 0.0f,
                                           IG::ImVec2(pos.x + std::floor((box.w - size.x) * 0.5f),
                                                      pos.y + std::floor((box.h - size.y) * 0.5f)),
                                           kNoteColour, note.c_str(), nullptr, wrap);
        }

        template <std::size_t N>
        void ChoiceCombo(const char* label, const std::array<PanelCore::Choice, N>& choices, int& value)
        {
            IG::SetNextItemWidth(170.0f);
            const std::string current(choices[value].label);
            if (!IG::BeginCombo(label, current.c_str())) return;
            for (int i = 0; i < static_cast<int>(N); ++i) {
                const std::string text(choices[i].label);
                if (IG::Selectable(text.c_str(), i == value)) value = i;
            }
            IG::EndCombo();
        }

        void Draw(bool inWindow)
        {
            // A later ImGui frame: the lists that named the last frame's pictures have been rendered.
            if (const int frame = IG::GetFrameCount(); frame != g_frame) {
                g_frame = frame;
                g_frameSrvs.clear();
            }
            const double now = NowMs();
            if (now - g_lastDraw >= kIdleCloseMs) RequestPeople();   // the panel has just come up
            g_lastDraw = now;

            if (const auto why = Views::Unavailable(); !why.empty()) {
                IG::TextWrapped("%s", PanelCore::Note(why).c_str());
                return;
            }

            ComPtr<ID3D11ShaderResourceView> srv;
            State                            state;
            std::string                      reason;
            std::uint32_t                    endedForm;
            std::vector<Person>              people;
            std::uint32_t                    crosshair, serial;
            {
                std::lock_guard lock(g_dataLock);
                if (g_state == State::Shown) g_noImageEnds = 0;
                // An end that says nothing about the actor opens the view again after a while: a game load, and the
                // first "no-image" or two (Magelight before its first in-game frame); more is a setup that never draws.
                if (g_state == State::Ended && g_endedForm == g_choices.target &&
                    (g_reason == "game-load" ||
                     (g_reason == "no-image" && ++g_noImageEnds <= PanelCore::kNoImageTries))) {
                    g_state = State::Closed;
                    g_endedForm = 0;
                    g_retryAt = now + kRetryMs;
                }
                srv = g_srv;
                state = g_state;
                reason = g_reason;
                endedForm = g_endedForm;
                people = g_people;
                crosshair = g_crosshair;
                serial = g_peopleSerial;
            }

            // A fresh list: keep the choice while it is listed, else the crosshair's actor or the player.
            if (serial != g_seenSerial) {
                g_seenSerial = serial;
                std::vector<std::uint32_t> ids;
                for (const auto& p : people) ids.push_back(p.id);
                if (std::find(ids.begin(), ids.end(), g_choices.target) == ids.end())
                    Choose(PanelCore::DefaultPick(ids, crosshair));
            }

            std::string current = "Choose someone";
            for (const auto& p : people) {
                if (p.id == g_choices.target) current = p.label;
            }
            IG::SetNextItemWidth(260.0f);
            if (IG::BeginCombo("Who", current.c_str())) {
                for (const auto& p : people) {
                    IG::PushID(static_cast<int>(p.id));
                    if (IG::Selectable(p.label.c_str(), p.id == g_choices.target)) Choose(p.id);
                    IG::PopID();
                }
                IG::EndCombo();
            }
            IG::SameLine();
            if (IG::Button("Refresh")) RequestPeople();

            const bool windowOpen = g_window->IsOpen;
            // The chosen actor's view ended for a reason of theirs: not opened again, while the panel stays up, until
            // Try again or another choice.
            const bool ended = state == State::Ended && endedForm == g_choices.target;
            if (!inWindow && windowOpen) {
                IG::TextDisabled("The view is in its own window.");
            } else {
                // Room below the picture: the controls' rows in the panel, one line (a button row when ended) in the
                // window.
                const float below = !inWindow ? IG::GetFrameHeightWithSpacing() * 3.0f
                                    : ended  ? IG::GetFrameHeightWithSpacing()
                                             : IG::GetTextLineHeightWithSpacing();
                const auto avail = IG::GetContentRegionAvail();
                const auto box = PanelCore::Fit(avail.x, avail.y - below);
                if (box.h <= 0) {
                    // Nothing shows it, so it does not draw; a box that only flickers small keeps it.
                    if (!g_smallSince) g_smallSince = now;
                    if (g_viewOpen && !ended && now - g_smallSince >= PanelCore::ResizeGate::kSettleMs) CloseView();
                    IG::TextDisabled("Make the window larger to see the picture.");
                } else {
                    g_smallSince = 0;
                    if (!ended) KeepViewOpen(box, now);
                    DrawStage(box, avail.x, srv, state, reason);
                }
            }
            if (ended) {
                if (IG::Button("Try again")) {
                    g_noImageEnds = 0;
                    std::lock_guard lock(g_dataLock);
                    g_state = State::Closed;
                    g_endedForm = 0;
                }
                IG::SameLine();
            }

            if (inWindow) {
                IG::TextDisabled("Drag to turn, wheel to zoom.");
                return;
            }
            ChoiceCombo("Light", PanelCore::kLights, g_choices.light);
            IG::SameLine();
            ChoiceCombo("Expression", PanelCore::kExpressions, g_choices.expression);
            IG::Checkbox("Floor", &g_choices.floor);
            IG::SameLine();
            IG::Checkbox("Weapons", &g_choices.weapons);
            IG::SameLine();
            IG::Checkbox("Classic pose", &g_choices.classicPose);
            IG::SameLine();
            IG::Checkbox("Motion", &g_choices.motion);
            if (IG::Button(windowOpen ? "Back into the panel" : "Pop out")) g_window->IsOpen = !windowOpen;
            IG::SameLine();
            IG::TextDisabled("Drag to turn, wheel to zoom. Drawn by Phantasm.");
        }

        void __stdcall RenderSection()
        {
            std::lock_guard lock(g_renderLock);
            Draw(false);
        }

        void __stdcall RenderWindow()
        {
            std::lock_guard lock(g_renderLock);
            // First use: centred, three quarters of the screen high, about a figure's width.
            if (IoMatches()) {
                const auto screen = IG::GetIO()->DisplaySize;
                IG::SetNextWindowPos(IG::ImVec2(screen.x * 0.5f, screen.y * 0.5f), IG::ImGuiCond_FirstUseEver,
                                     IG::ImVec2(0.5f, 0.5f));
                IG::SetNextWindowSize(IG::ImVec2(std::floor(screen.y * 0.5f), std::floor(screen.y * 0.75f)),
                                      IG::ImGuiCond_FirstUseEver);
            } else {
                IG::SetNextWindowSize(IG::ImVec2(540, 800), IG::ImGuiCond_FirstUseEver);
            }
            bool open = true;
            if (IG::Begin("Phantasm##PhantasmShowcase", &open, IG::ImGuiWindowFlags_NoCollapse)) Draw(true);
            IG::End();
            if (!open) g_window->IsOpen = false;
        }

        /// The framework's DLL has every export the panel calls and the ImGui its header describes.
        bool FrameworkUsable(HMODULE module)
        {
            for (const char* name : kExports) {
                if (!::GetProcAddress(module, name)) {
                    SKSE::log::warn("imgui: SKSE Menu Framework has no {} - no ImGui panel", name);
                    return false;
                }
            }
            const char*            version = IG::GetVersion();
            const std::string_view imgui = version ? version : "";
            if (!imgui.starts_with(kImGuiVersion)) {
                SKSE::log::warn("imgui: SKSE Menu Framework runs ImGui {}, the panel was made for {}x - no ImGui panel",
                                imgui, kImGuiVersion);
                return false;
            }
            SKSE::log::info("imgui: SKSE Menu Framework {} (ImGui {})", SKSEMenuFramework::GetMenuFrameworkVersion(), imgui);
            return true;
        }
    }

    void Initialize()
    {
        const auto module = ::GetModuleHandleW(L"SKSEMenuFramework.dll");
        if (!module) {
            SKSE::log::info("imgui: SKSE Menu Framework is not installed - no ImGui panel");
            return;
        }
        if (!FrameworkUsable(module)) return;
        // The window first: the section's draw reads it. AddWindow's wrapper writes through what the export returns.
        g_window = SKSEMenuFramework::AddWindow(&RenderWindow, true);
        if (!g_window) return;   // not with the export checked above; the draw relies on it
        SKSEMenuFramework::SetSection(kSection);
        SKSEMenuFramework::AddSectionItem("Live view", &RenderSection);
        std::thread(&Pump).detach();
        SKSE::log::info("imgui: the panel is in the Mod Control Panel, {} > Live view", kSection);
    }

    void OnPreLoad()
    {
        // The pictures stay until the pump's idle close: a list ImGui has not rendered yet may name one.
        std::lock_guard lock(g_renderLock);
        CloseView();
    }

    void OnViewEvent(EventKind kind, ViewId view, std::uint32_t formId, ID3D11ShaderResourceView* srv,
                     std::string_view reason)
    {
        std::lock_guard lock(g_dataLock);
        // Still on its way for the actor before a re-target (the view keeps its id across one).
        if (kind != EventKind::Ended && formId != g_wantForm) return;
        switch (kind) {
        case EventKind::Shown:
            g_srv = srv;
            g_state = State::Shown;
            g_reason.clear();
            break;
        case EventKind::Waiting:
            g_srv.Reset();
            g_state = State::Waiting;
            g_reason = reason;
            break;
        case EventKind::Ended:
            g_srv.Reset();
            g_state = State::Ended;
            g_reason = reason;
            g_endedForm = formId;
            SKSE::log::info("imgui: the view of {:08X} ended ({})", formId, reason);
            break;
        default:
            return;
        }
        g_stateView = view;
    }
}
