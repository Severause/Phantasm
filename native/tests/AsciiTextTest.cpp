// Phantasm - native self-tests: AsciiText (ASCII case folding).
//
// Author: Severause

#include "AsciiText.h"
#include "TestHarness.h"

#include <cctype>

namespace Phantasm::Tests
{
    void RunAsciiTextTests()
    {
        namespace AT = Phantasm::AsciiText;

        bool sameAsCLocale = true;
        for (int c = 0; c < 256; ++c) {
            const auto ch = static_cast<char>(c);
            const auto c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            sameAsCLocale = sameAsCLocale && AT::Lower(ch) == c0;
        }
        Check(sameAsCLocale, "ascii: every byte folds as std::tolower does in the C locale");
        Check(AT::Lowered("Textures\\Actors\\FaceGen_HEAD.dds") == "textures\\actors\\facegen_head.dds",
              "ascii: a path folds to lower case");
        Check(AT::Lowered("\xC3\x89t\xC3\xA9") == "\xC3\x89t\xC3\xA9", "ascii: UTF-8 bytes are left as they are");
        std::string s = "MagicNode R";
        AT::LowerInPlace(s);
        Check(s == "magicnode r", "ascii: folds in place");

        Check(AT::EqualsI("NPC Head [Head]", "npc head [head]") && !AT::EqualsI("abc", "abcd"),
              "ascii: equal in any case, never at another length");
        Check(AT::StartsWithI("AnimObjectA", "animobject") && !AT::StartsWithI("Anim", "AnimObject"),
              "ascii: a prefix in any case");
        Check(AT::EndsWithI("mt_idle.HKX", ".hkx") && !AT::EndsWithI("hkx", ".hkx"), "ascii: a suffix in any case");
        Check(AT::ContainsI("BloodFX_Spray", "bloodfx") && AT::ContainsI("x", "") && !AT::ContainsI("blood", "bloodfx"),
              "ascii: a part anywhere in any case, the empty part always");
    }
}
