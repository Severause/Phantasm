// Settings panel: see SettingsPanel.h.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include "SettingsPanel.h"

#include "Settings.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

// After CommonLib, which refuses a <windows.h> included before it (the header includes one), and after <atomic>,
// <filesystem> and <format>, which the header uses and does not include.
#include <SKSEMenuFramework.h>

namespace Phantasm::SettingsPanel
{
    namespace
    {
        namespace IG     = ImGuiMCP;
        namespace Budget = LiveViewBudgetCore;
        using Clock = std::chrono::steady_clock;

        constexpr const char* kSection = "Phantasm";
        constexpr const char* kItem    = "Settings";
        // The ImGui the framework's header describes (its structs and enum values).
        constexpr std::string_view kImGuiVersion = "1.90.";
        // Every export the panel's calls reach: the header's ImGui wrappers call them without a null check.
        constexpr std::array kExports = {
            "AddSectionItem", "igGetVersion", "igPushID_Str", "igPopID", "igTextWrappedV", "igTextDisabledV",
            "igPushTextWrapPos", "igPopTextWrapPos", "igPushStyleColor_U32", "igPopStyleColor", "igSeparatorText",
            "igCheckbox", "igSliderFloat", "igSliderInt", "igBeginCombo", "igSelectable_Bool", "igEndCombo",
            "igCollapsingHeader_TreeNodeFlags", "igInputText", "igSmallButton", "igSameLine", "igIsItemActive",
            "igIsItemDeactivatedAfterEdit",
        };

        // Settings::GetStatus takes a lock and builds strings: asked this often, and at the draw after a change.
        constexpr auto kStatusEvery = std::chrono::milliseconds(500);
        // A panel not drawn for this long has just come up: a widget held when it was last drawn is not held now.
        constexpr auto kCameUp = std::chrono::seconds(1);

        constexpr IG::ImU32 kWarningColour = IM_COL32(0xe8, 0xb4, 0x54, 0xff);

        // The sliders' ends where SettingsCore names none: those NeuralStillCore's Clamp functions keep.
        // Settings::Apply clamps whatever a slider gives.
        constexpr int kModelMax  = 2;
        constexpr int kPassesMin = 1;
        constexpr int kPassesMax = 8;

        struct BudgetChoice
        {
            Budget::Mode mode;
            const char*  label;
        };
        constexpr std::array kBudgets = {
            BudgetChoice{ Budget::Mode::kSmooth, "Smooth" },
            BudgetChoice{ Budget::Mode::kBalanced, "Balanced" },
            BudgetChoice{ Budget::Mode::kLight, "Light" },
        };

        // The draw's own state: the framework's render thread alone reads and writes it.
        Settings::Values  g_values;             // the working copy the widgets change
        char              g_folder[520]{};      // the Library field's text: g_values.folder unless the field is held
        bool              g_editing = false;    // a slider or the Library field was held in the last draw
        Settings::Status  g_status;
        bool              g_statusDue = true;   // read g_status again at the next draw
        Clock::time_point g_statusAt{};
        Clock::time_point g_lastDraw{};
        // The Magelight page's key being caught: after Change, the first key that goes down. Read with
        // GetAsyncKeyState, so the panel needs none of the framework's ImGui key exports.
        bool                  g_catching = false;
        std::array<bool, 256> g_heldAtStart{};   // keys down when the catch began: not taken until let go once

        void FolderToField()
        {
            const std::size_t n = std::min(g_values.folder.size(), sizeof(g_folder) - 1);
            std::memcpy(g_folder, g_values.folder.data(), n);
            g_folder[n] = '\0';
        }

        void TakeInForce()
        {
            g_values = Settings::Get();
            FolderToField();
        }

        /// Puts the working copy in force and into Phantasm.ini, when it differs from what is in force: Apply reads
        /// and writes the file and logs.
        void Commit()
        {
            if (g_values == Settings::Get()) return;
            Settings::Apply(g_values);   // why a write failed is Status::saveError, read at the next draw
            g_statusDue = true;
        }

        /// After a slider or the Library field: held, the draw keeps the working copy; let go after a change, the
        /// copy goes in force. Not on every frame of a drag: see Commit.
        void AfterEdit()
        {
            if (IG::IsItemActive()) g_editing = true;
            if (IG::IsItemDeactivatedAfterEdit()) Commit();
        }

        /// A dim line, wrapped. `text` is never a format: it may hold a path.
        void Note(const char* text)
        {
            IG::PushTextWrapPos(0.0f);
            IG::TextDisabled("%s", text);
            IG::PopTextWrapPos();
        }

        void Warning(const char* text)
        {
            IG::PushStyleColor(IG::ImGuiCol_Text, kWarningColour);
            IG::TextWrapped("%s", text);
            IG::PopStyleColor();
        }

        void Strength(const char* label, float& value)
        {
            IG::SliderFloat(label, &value, 0.0f, 1.0f, "%.2f", IG::ImGuiSliderFlags_AlwaysClamp);
            AfterEdit();
        }

        void DrawStatus()
        {
            const auto& s = g_status;
            IG::TextWrapped("Phantasm %s", s.version.c_str());
            if (!s.neuralText.empty()) IG::TextWrapped("%s", s.neuralText.c_str());
            if (!s.library.empty()) IG::TextWrapped("Library: %s", s.library.c_str());
            if (!s.followed.empty()) IG::TextWrapped("Settings from: %s", s.followed.c_str());
            if (!s.ownLibrary) {
                Warning("Neural rendering's own library, PhantasmNRInProc.dll, is not installed in SKSE/Plugins/Phantasm.");
            }
            if (s.restartNeeded) IG::TextWrapped("%s", "Some changes take effect the next time the game starts.");
            if (!s.saveError.empty()) {
                Warning(("Not saved: " + s.saveError).c_str());
                Note("The change holds for this session.");
            }
        }

        bool KeyDown(int virtualKey) { return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0; }

        /// GetAsyncKeyState reads the keyboard whatever window has it: a key typed into another program is not
        /// a key for the page.
        bool GameHasKeyboard()
        {
            DWORD      process = 0;
            const HWND front = ::GetForegroundWindow();
            return front && ::GetWindowThreadProcessId(front, &process) != 0 && process == ::GetCurrentProcessId();
        }

        void BeginCatch()
        {
            Settings::TakePageKey(0);   // what the last key was refused for no longer stands
            g_statusDue = true;
            g_catching  = true;
            for (int vk = 0; vk < 256; ++vk) g_heldAtStart[static_cast<std::size_t>(vk)] = KeyDown(vk);
        }

        /// One draw of the catch. The first key newly down ends it: Escape with the key as it was, any other as
        /// the page's key when it can open a page (Settings::TakePageKey; Status::keyError when it cannot).
        void PollCatch()
        {
            if (!GameHasKeyboard()) return;
            // A click ends the catch: the next key is then meant for whatever was clicked.
            for (const int button : { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON }) {
                auto& held = g_heldAtStart[static_cast<std::size_t>(button)];
                if (!KeyDown(button)) {
                    held = false;
                } else if (!held) {
                    g_catching = false;
                    return;
                }
            }
            // From 8: the codes below it are the mouse's buttons.
            for (int vk = 0x08; vk < 0xFF; ++vk) {
                auto& held = g_heldAtStart[static_cast<std::size_t>(vk)];
                if (!KeyDown(vk)) {
                    held = false;
                    continue;
                }
                if (held) continue;
                // Shift, Ctrl, Alt or a Windows key alone: the key itself is still to come.
                if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || (vk >= VK_LSHIFT && vk <= VK_RMENU) ||
                    vk == VK_LWIN || vk == VK_RWIN) {
                    continue;
                }
                g_catching = false;
                if (vk != VK_ESCAPE) {
                    Settings::TakePageKey(static_cast<std::uint32_t>(vk));
                    g_statusDue = true;
                }
                return;
            }
        }

        /// The key that opens the Magelight page, where there is such a page.
        void DrawPageKey()
        {
            if (!g_status.pageReady) return;
            IG::SeparatorText("Magelight page");
            if (g_catching) {
                IG::TextWrapped("%s", "Press the key that should open the page. Escape keeps the one it has.");
                PollCatch();
            } else {
                IG::TextWrapped("Opens with: %s", g_status.pageKeyName.c_str());
                IG::SameLine();
                if (IG::SmallButton("Change")) BeginCatch();
                IG::SameLine();
                if (IG::SmallButton("None##pageKey") && g_values.pageKey != 0) {
                    g_values.pageKey = 0;
                    Commit();
                }
            }
            if (!g_status.keyError.empty()) Warning(g_status.keyError.c_str());
        }

        void DrawNeural()
        {
            IG::SeparatorText("Neural rendering");
            Note("Experimental. It needs an nvngx_dlssnr.dll from a neural rendering mod.");

            bool wanted = SettingsCore::NeuralWanted(g_values);
            if (IG::Checkbox("Neural rendering##wanted", &wanted)) {
                g_values.enabled = wanted;
                // The switch on with no source named is still off, so it takes the copy a neural rendering mod
                // installed.
                if (wanted && !SettingsCore::SourceNamed(g_values.folder)) {
                    g_values.folder = "auto";
                    FolderToField();
                }
                Commit();
            }
            if (IG::Checkbox("Also on live views", &g_values.live)) Commit();

            float percent = g_values.liveScale * 100.0f;
            if (IG::SliderFloat("Working size", &percent, SettingsCore::kLiveScaleMin * 100.0f,
                                SettingsCore::kLiveScaleMax * 100.0f, "%.0f%%", IG::ImGuiSliderFlags_AlwaysClamp)) {
                g_values.liveScale = percent / 100.0f;
            }
            AfterEdit();
            Note("The size neural rendering works at, against a live view's picture.");
        }

        void DrawLiveViews()
        {
            IG::SeparatorText("Live views");

            const auto  mode    = Budget::ParseMode(g_values.budget);
            const char* current = kBudgets.front().label;
            for (const auto& choice : kBudgets) {
                if (choice.mode == mode) current = choice.label;
            }
            if (IG::BeginCombo("Budget", current)) {
                for (const auto& choice : kBudgets) {
                    if (IG::Selectable(choice.label, choice.mode == mode)) {
                        g_values.budget = Budget::ModeName(choice.mode);
                        Commit();
                    }
                }
                IG::EndCombo();
            }

            float ms = static_cast<float>(g_values.budgetMs);
            if (IG::SliderFloat("Budget in milliseconds", &ms, 0.0f, static_cast<float>(SettingsCore::kBudgetMsMax),
                                "%.1f", IG::ImGuiSliderFlags_AlwaysClamp)) {
                // Below the least a budget may be there is only 0, the mode's own. Tenths, as the slider shows them.
                g_values.budgetMs = ms < SettingsCore::kBudgetMsMin ? 0.0 : std::round(ms * 10.0) / 10.0;
            }
            AfterEdit();
            static const std::string note =
                std::format("0 takes the mode's own budget; otherwise {} to {} milliseconds.",
                            SettingsCore::kBudgetMsMin, SettingsCore::kBudgetMsMax);
            Note(note.c_str());
        }

        void DrawAdvanced()
        {
            if (!IG::CollapsingHeader("Advanced")) return;

            if (IG::Checkbox("Take my neural rendering mod's settings", &g_values.follow)) Commit();
            Note("Its switch, style and strengths. The ones below apply to anything it does not set.");
            if (IG::Checkbox("Start as soon as that mod turns its own on", &g_values.prestart)) Commit();

            IG::SliderInt("Style", &g_values.model, 0, kModelMax, "%d", IG::ImGuiSliderFlags_AlwaysClamp);
            AfterEdit();
            Strength("Intensity", g_values.intensity);
            Strength("Tone", g_values.tone);
            Strength("Structure", g_values.structure);
            Strength("Skin", g_values.skin);
            IG::SliderInt("Passes", &g_values.passes, kPassesMin, kPassesMax, "%d", IG::ImGuiSliderFlags_AlwaysClamp);
            AfterEdit();

            if (IG::Checkbox("Allow unsigned copies", &g_values.unsignedCopies)) Commit();

            IG::InputText("Library", g_folder, sizeof(g_folder));
            if (IG::IsItemDeactivatedAfterEdit()) g_values.folder = g_folder;
            AfterEdit();
            IG::SameLine();
            if (IG::SmallButton("auto")) {
                g_values.folder = "auto";
                FolderToField();
                Commit();
            }
            Note("auto, or a folder or a library file. Empty: no source, so neural rendering is off.");

            Note(("Settings file: " + g_status.file).c_str());
        }

        void __stdcall Render()
        {
            const auto now    = Clock::now();
            const bool cameUp = now - g_lastDraw >= kCameUp;
            g_lastDraw        = now;
            // What is in force, unless a widget is held: the other settings page may have changed it since the last
            // draw, and Apply clamps what this one gave it.
            if (cameUp || !g_editing) TakeInForce();
            g_editing = false;
            if (cameUp) g_catching = false;
            if (cameUp || g_statusDue || now - g_statusAt >= kStatusEvery) {
                g_status    = Settings::GetStatus();
                g_statusAt  = now;
                g_statusDue = false;
            }

            IG::PushID("PhantasmSettings");
            DrawStatus();
            DrawNeural();
            DrawLiveViews();
            DrawPageKey();
            DrawAdvanced();
            IG::PopID();
        }

        /// The framework's DLL has every export the panel calls and the ImGui its header describes.
        bool FrameworkUsable(HMODULE framework)
        {
            for (const char* name : kExports) {
                if (!::GetProcAddress(framework, name)) {
                    SKSE::log::warn("settings panel: SKSE Menu Framework has no {} - no settings panel", name);
                    return false;
                }
            }
            const char*            version = IG::GetVersion();
            const std::string_view imgui   = version ? version : "";
            if (!imgui.starts_with(kImGuiVersion)) {
                SKSE::log::warn("settings panel: SKSE Menu Framework runs ImGui {}, the panel was made for {}x - no "
                                "settings panel",
                                imgui, kImGuiVersion);
                return false;
            }
            SKSE::log::info("settings panel: SKSE Menu Framework {} (ImGui {})",
                            SKSEMenuFramework::GetMenuFrameworkVersion(), imgui);
            return true;
        }
    }

    void Initialize()
    {
        const auto framework = ::GetModuleHandleW(L"SKSEMenuFramework.dll");
        if (!framework) {
            SKSE::log::info("settings panel: SKSE Menu Framework is not installed - no settings panel");
            return;
        }
        if (!FrameworkUsable(framework)) return;
        SKSEMenuFramework::SetSection(kSection);
        SKSEMenuFramework::AddSectionItem(kItem, &Render);
        SKSE::log::info("settings panel: the panel is in the Mod Control Panel, {} > {}", kSection, kItem);
    }
}
