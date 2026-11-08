#pragma once

// Phantasm Showcase - PanelCore: the ImGui panel's rules (ImGuiPanel.h): the picture's box, when the view is resized,
// how a drag and the wheel turn and zoom, and who it shows first. Engine-free.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Showcase::PanelCore
{
    /// The picture's width over its height: a standing figure, as the pages' stages.
    inline constexpr float kAspect = 0.75f;
    /// The smallest picture drawn, in pixels of height; below it the panel shows no picture.
    inline constexpr float kMinHeight = 160.0f;

    struct Box
    {
        float w = 0, h = 0;
    };

    /// The largest box of kAspect inside availW x availH; an empty box when it would be under kMinHeight high or the
    /// sizes are not numbers.
    inline Box Fit(float availW, float availH)
    {
        if (!std::isfinite(availW) || !std::isfinite(availH) || availW <= 0 || availH <= 0) return {};
        float h = std::min(availH, availW / kAspect);
        if (h < kMinHeight) return {};
        return { std::floor(h * kAspect), std::floor(h) };
    }

    /// A size the view is opened or resized at.
    struct Size
    {
        std::uint32_t w = 0, h = 0;
        bool operator==(const Size&) const = default;
    };

    /// Resizing rebuilds the view's offscreen, so a box that changes every frame (a window being dragged larger) is
    /// followed only once it has held still for kSettleMs, and a change under kSlack of a side is not followed at all.
    class ResizeGate
    {
    public:
        static constexpr double kSettleMs = 250.0;
        static constexpr double kSlack = 0.05;

        /// The size to use now for a box of `want` at time nowMs (any steady clock). The first box is taken at once.
        Size Update(Size want, double nowMs)
        {
            if (!want.w || !want.h) return m_size;
            if (!m_size.w || !m_size.h) {
                m_size = want;
                m_pending = {};
                return m_size;
            }
            if (Near(want.w, m_size.w) && Near(want.h, m_size.h)) {
                m_pending = {};
                return m_size;
            }
            if (!(want == m_pending)) {
                m_pending = want;
                m_since = nowMs;
            } else if (nowMs - m_since >= kSettleMs) {
                m_size = want;
                m_pending = {};
            }
            return m_size;
        }

        /// Forgets the size: the next box is taken at once (a view opened again).
        void Reset() { *this = {}; }

    private:
        static bool Near(std::uint32_t a, std::uint32_t b)
        {
            return std::abs(double(a) - double(b)) <= kSlack * double(std::max(a, b));
        }

        Size   m_size;
        Size   m_pending;
        double m_since = 0;
    };

    /// Degrees of yaw per pixel of a drag, and the wheel's deltaY per notch: the pages' numbers (LiveView.tsx), so a view
    /// turns and zooms the same in either.
    inline constexpr float kYawPerPixel = 0.5f;
    inline constexpr float kWheelPerNotch = 100.0f;

    /// Orbit's dx (degrees of yaw) and dy (pixels of pitch) for a mouse move of (mx, my) pixels.
    inline std::array<float, 2> Orbit(float mx, float my) { return { mx * kYawPerPixel, my }; }

    /// Zoom's dz for ImGui's MouseWheel (notches, up positive): a browser's deltaY runs the other way.
    inline float Zoom(float notches) { return -notches * kWheelPerNotch; }

    /// Whom the panel shows when nobody is chosen yet: the actor under the crosshair when the list has them, else
    /// the first listed (the player, PeopleCore's order), else nobody (0).
    inline std::uint32_t DefaultPick(const std::vector<std::uint32_t>& listed, std::uint32_t crosshair)
    {
        if (crosshair && std::find(listed.begin(), listed.end(), crosshair) != listed.end()) return crosshair;
        return listed.empty() ? 0 : listed.front();
    }

    /// What the stage says for a view's Waiting or Ended reason, or a refused open's: the page's words
    /// (ui/src/lib/labels.ts), but for "no-image", which the panel's hostless view gets only from a Phantasm before 1.0.0
    /// without Magelight, or from Magelight's CPU renderer.
    inline std::string Note(std::string_view reason)
    {
        static constexpr std::array<std::array<std::string_view, 2>, 15> kNotes = { {
            { "refused", "Phantasm refused the view." },
            { "busy", "Waiting: more views are open than Phantasm draws at once." },
            { "game-load", "A game load ended the view." },
            { "missing", "They aren't in the game anymore." },
            { "dead", "They are dead." },
            { "unrenderable", "Phantasm can't draw them." },
            { "not-loaded", "Too far away to draw." },
            { "collect-failed", "Phantasm couldn't read their 3D." },
            { "no-image", "Phantasm drew no picture: without Magelight it needs Phantasm 1.0.0." },
            { "gpu-unavailable", "The graphics card refused. Try again in a moment." },
            { "faulted", "The view stopped after an error." },
            { "tooMany", "Too many views are open." },
            { "badSize", "The panel is too small." },
            { "absent", "Phantasm is not installed, so nothing can be drawn." },
            { "old", "This Phantasm is too old for the showcase. Update Phantasm." },
        } };
        for (const auto& [key, text] : kNotes) {
            if (key == reason) return std::string(text);
        }
        return "The picture isn't available (" + std::string(reason) + ").";
    }

    /// How many "no-image" ends in a row the panel opens the view again after: the first can be Magelight before its
    /// first in-game frame, which passes; more is a setup that never draws.
    inline constexpr int kNoImageTries = 2;

    struct Choice
    {
        std::string_view value;   // the hint value
        std::string_view label;
    };

    /// The pages' lighting and expression choices (ui/src/lib/labels.ts), as the hints take them.
    inline constexpr std::array<Choice, 6> kLights = { {
        { "studio", "Studio" }, { "daylight", "Daylight" }, { "hearth", "Hearth" }, { "torchlight", "Torchlight" },
        { "moonlight", "Moonlight" }, { "here", "As here" },
    } };
    inline constexpr std::array<Choice, 9> kExpressions = { {
        { "", "As in game" }, { "neutral", "Neutral" }, { "happy", "Happy" }, { "sad", "Sad" }, { "angry", "Angry" },
        { "afraid", "Afraid" }, { "surprised", "Surprised" }, { "puzzled", "Puzzled" }, { "disgusted", "Disgusted" },
    } };
}
