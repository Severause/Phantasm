// Engine-free tests for SettingsCore (Phantasm.ini read as values and written back into its own text).

#include "TestHarness.h"

#include "SettingsCore.h"

#include <string>

namespace Phantasm::Tests
{
    void RunSettingsCoreTests()
    {
        namespace SC = Phantasm::SettingsCore;

        const std::string file =
            "; Phantasm settings\r\n"
            "\r\n"
            "[LiveViews]\r\n"
            "; how much of a frame\r\n"
            "Budget = balanced\r\n"
            "BudgetMs = 0\r\n"
            "\r\n"
            "[NeuralRendering]\r\n"
            "; the switch\r\n"
            "Enabled = 0\r\n"
            "Folder = auto   ; found by itself\r\n"
            "Unsigned = 1\r\n"
            "Follow = 1\r\n"
            "Model = 2\r\n"
            "Intensity = 0.5\r\n"
            "Passes = 6\r\n"
            "LiveViews = 1\r\n"
            "LiveScale = 0.75\r\n"
            "\r\n"
            "[SettingsPage]\r\n"
            "Key = 68\r\n";

        // reading
        {
            const auto p = SC::Parse(file);
            const auto& v = p.values;
            Check(p.warnings.empty() && p.sawLiveViews, "settings: a clean file reads with no warning");
            Check(v.budget == "balanced" && v.budgetMs == 0.0, "settings: the live views' budget is read");
            Check(!v.enabled && v.folder == "auto" && v.unsignedCopies && v.follow, "settings: the switch and the source, a comment after a value left off");
            Check(v.model == 2 && v.intensity == 0.5f && v.passes == 6 && v.live && v.liveScale == 0.75f, "settings: the numbers are read");
            Check(v.tone == 1.0f && v.prestart, "settings: a key the file does not name keeps its default");
            Check(v.pageKey == 68, "settings: the page's key is read");
            Check(!SC::NeuralWanted(v), "settings: off with the switch off, whatever the folder");
        }
        {
            const auto p = SC::Parse("[NeuralRendering]\nFolder = auto\n");
            Check(p.values.enabled && SC::NeuralWanted(p.values), "settings: a file from before the switch is on by its folder alone");
            Check(!p.sawLiveViews, "settings: a file with no [LiveViews] says so");
            Check(!SC::NeuralWanted(SC::Parse("").values), "settings: no file, no neural rendering");
            Check(!SC::NeuralWanted(SC::Parse("[NeuralRendering]\nEnabled = 1\nFolder = none\n").values) &&
                      !SC::SourceNamed("off") && !SC::SourceNamed(" 0 ") && SC::SourceNamed("auto") && SC::SourceNamed("D:\\NR"),
                  "settings: a Folder that names no source is off, whatever the switch says");
        }
        {
            const auto p = SC::Parse("[NeuralRendering]\nFolder = \"D:\\Mods #1\\NR;x\"  ; mine\n");
            Check(p.values.folder == "D:\\Mods #1\\NR;x", "settings: a value in quotes keeps its ';' and '#', and loses the quotes");
            const auto bare = SC::Parse("[NeuralRendering]\nFolder = D:\\NR ; mine\nPasses = 6;six\n");
            Check(bare.values.folder == "D:\\NR" && bare.values.passes == 6, "settings: outside quotes a ';' starts a comment, with or without a space");
        }
        {
            const auto p = SC::Parse("\xEF\xBB\xBF[liveviews] ; a comment\nBUDGET=light\nBudgetMs = lots\nStray = 1\n[Other]\nBudget = smooth\n");
            Check(p.values.budget == "light" && p.sawLiveViews, "settings: a byte order mark, a comment after a header and any letter case are read");
            Check(p.warnings.size() == 1 && p.values.budgetMs == 0.0, "settings: a value that is no number is one warning and keeps the default");
            const auto twice = SC::Parse("[LiveViews]\nBudget = light\nBudget = balanced\n");
            Check(twice.values.budget == "balanced", "settings: of a key named twice the last wins");
            const auto loose = SC::Parse("Budget = light\n");
            Check(loose.values.budget == "smooth", "settings: a key under no section is ignored");
        }

        // ranges
        {
            SC::Values v;
            v.budget = "Frantic";
            v.budgetMs = 0.2;
            v.model = 9;
            v.intensity = -1.0f;
            v.passes = 0;
            v.liveScale = 3.0f;
            v.pageKey = 4000;
            v.folder = "  auto  ";
            SC::Clamp(v);
            Check(v.budget == "smooth" && v.budgetMs == SC::kBudgetMsMin, "settings: an unknown mode is smooth, a small budget the smallest");
            Check(v.model == 2 && v.intensity == 0.0f && v.passes == 1 && v.liveScale == SC::kLiveScaleMax, "settings: numbers are brought inside their ranges");
            Check(v.pageKey == 0 && v.folder == "auto", "settings: a key that is no scan code is none, the folder is trimmed");
            v.folder = " \"D:\\NR\" ";
            SC::Clamp(v);
            Check(v.folder == "D:\\NR", "settings: quotes around a folder are not part of it");
            v.folder = "a\"b;c";
            SC::Clamp(v);
            Check(v.folder == "ab;c", "settings: nor is a quote inside one");
            const auto once = SC::Rewrite("[NeuralRendering]\nFolder = auto\n", v);
            Check(SC::Parse(once).values.folder == "ab;c" && SC::Rewrite(once, v) == once,
                  "settings: ... so it reads back as itself and a second save changes nothing");
            v.pageKey = 0x01;
            SC::Clamp(v);
            Check(v.pageKey == 0, "settings: Escape cannot open the page, so it is no key");
            v.budgetMs = -3.0;
            SC::Clamp(v);
            Check(v.budgetMs == 0.0, "settings: no budget of its own is 0");
        }

        // the page's key
        {
            Check(SC::Values{}.pageKey == 0x46 && SC::KeyFixedName(SC::Values{}.pageKey) == "Scroll Lock",
                  "settings: the page opens with Scroll Lock unless the file says otherwise");
            Check(SC::KeyUsable(0x46) && SC::KeyUsable(0x57) && SC::KeyUsable(0xD2) && SC::KeyUsable(0x1E),
                  "settings: a lock key, a function key, a navigation key and a letter can open the page");
            Check(!SC::KeyUsable(0) && !SC::KeyUsable(0x100) && !SC::KeyUsable(0x01) && !SC::KeyUsable(0x1C) &&
                      !SC::KeyUsable(0x9C) && !SC::KeyUsable(0x2A) && !SC::KeyUsable(0x9D) && !SC::KeyUsable(0xB8) &&
                      !SC::KeyUsable(0xDB),
                  "settings: no key, Escape, Enter and the modifiers alone cannot");
            Check(SC::KeyFromScan(0x46) == 0x46 && SC::KeyFromScan(0x57) == 0x57, "settings: a plain scan code is its own DirectInput code");
            Check(SC::KeyFromScan(0xE052) == 0xD2 && SC::KeyFromScan(0xE047) == 0xC7 && SC::KeyFromScan(0xE01D) == 0x9D,
                  "settings: an extended scan code is the DirectInput code with its high bit");
            Check(SC::KeyFromScan(0) == 0 && SC::KeyFromScan(0xE11D) == 0 && SC::KeyFromScan(0xE000) == 0 && SC::KeyFromScan(0x80) == 0,
                  "settings: no scan code, Pause's 0xE1 code and a code that is no key are none");
            Check(SC::KeyFixedName(0x3B) == "F1" && SC::KeyFixedName(0x44) == "F10" && SC::KeyFixedName(0x58) == "F12" &&
                      SC::KeyFixedName(0xC5) == "Pause" && SC::KeyFixedName(0xD1) == "Page Down" && SC::KeyFixedName(0x47) == "Numpad 7",
                  "settings: the keys every layout names alike have their names");
            Check(SC::KeyFixedName(0x1E).empty() && SC::KeyFixedName(0x02).empty() && SC::KeyFixedName(0).empty(),
                  "settings: a letter, a digit and no key have none: the layout names those");
        }

        // writing back
        {
            auto v = SC::Parse(file).values;
            Check(SC::Rewrite(file, v) == file, "settings: nothing changed, nothing rewritten");

            v.enabled = true;
            v.liveScale = 0.5f;
            v.budget = "light";
            const auto out = SC::Rewrite(file, v);
            Check(out.find("Enabled = 1\r\n") != std::string::npos && out.find("LiveScale = 0.5\r\n") != std::string::npos &&
                      out.find("Budget = light\r\n") != std::string::npos,
                  "settings: a changed value is written on its own line");
            Check(out.find("; the switch\r\nEnabled = 1") != std::string::npos && out.find("; how much of a frame\r\nBudget = light") != std::string::npos,
                  "settings: the line stays under its comment");
            Check(out.find("Folder = auto   ; found by itself\r\n") != std::string::npos && out.find("Model = 2\r\n") != std::string::npos,
                  "settings: an unchanged line is left exactly as it was");
            Check(out.find('\n') != std::string::npos && out.find("\r\n") != std::string::npos && out.back() == '\n' &&
                      out.find("\n\n") == std::string::npos,
                  "settings: the line ends stay CRLF");
            auto again = SC::Parse(out).values;
            Check(again == v, "settings: what is written reads back as the same values");

            v.folder = "D:\\NR";
            const auto pathed = SC::Rewrite(out, v);
            Check(pathed.find("Folder = D:\\NR  ; found by itself\r\n") != std::string::npos, "settings: a changed value keeps the comment after it");

            v.folder = "D:\\Mods #1\\NR";
            const auto quoted = SC::Rewrite(pathed, v);
            Check(quoted.find("Folder = \"D:\\Mods #1\\NR\"  ; found by itself\r\n") != std::string::npos,
                  "settings: a path with a '#' in it is written in quotes");
            Check(SC::Parse(quoted).values == v && SC::Rewrite(quoted, v) == quoted,
                  "settings: ... reads back as itself, and a second save changes nothing");
        }
        {
            // A key and a section the file lacks.
            SC::Values v = SC::Parse(file).values;
            v.tone = 0.25f;
            const auto out = SC::Rewrite(file, v);
            Check(out.find("LiveScale = 0.75\r\nTone = 0.25\r\n\r\n[SettingsPage]") != std::string::npos,
                  "settings: a key the file lacks goes at the end of its section");
            Check(SC::Parse(out).values == v, "settings: ... and reads back");

            const std::string bare = "[LiveViews]\nBudget = smooth\n";
            SC::Values        w = SC::Parse(bare).values;
            w.enabled = true;
            w.folder = "auto";
            const auto grown = SC::Rewrite(bare, w);
            Check(grown.find("\r\n") == std::string::npos && grown.starts_with("[LiveViews]\nBudget = smooth\n"),
                  "settings: a file with plain line ends keeps them, and what it had");
            Check(grown == "[LiveViews]\nBudget = smooth\n\n[NeuralRendering]\nFolder = auto\n",
                  "settings: a section the file lacks is added at its end, with only what its silence would not mean");
            Check(SC::Parse(grown).values == w, "settings: ... and reads back");

            SC::Values fresh;
            fresh.enabled = true;
            fresh.folder = "auto";
            const auto made = SC::Rewrite("", fresh);
            Check(made == "[NeuralRendering]\r\nFolder = auto\r\n" && SC::Parse(made).values == fresh,
                  "settings: no file at all gets the one line that is not a default");
        }
        {
            const std::string twice = "\xEF\xBB\xBF[LiveViews]\nbudget=light\nBudget = light\n";
            SC::Values        v = SC::Parse(twice).values;
            v.budget = "balanced";
            const auto out = SC::Rewrite(twice, v);
            Check(out.starts_with("\xEF\xBB\xBF[LiveViews]\nbudget= balanced\nBudget = balanced\n"),
                  "settings: every line of a key named twice is changed, the byte order mark and the key as written kept");
            SC::Values same = SC::Parse("[NeuralRendering]\nIntensity = 1\nUnsigned = 7\n").values;
            Check(SC::Rewrite("[NeuralRendering]\nIntensity = 1\nUnsigned = 7\n", same).starts_with("[NeuralRendering]\nIntensity = 1\nUnsigned = 7\n"),
                  "settings: a value that already says the same in other words is left alone");
        }
    }
}
