// Engine-free tests for NeuralSettingsCore (the NR settings read from the player's neural rendering mod).

#include "TestHarness.h"

#include "NeuralSettingsCore.h"

namespace Phantasm::Tests
{
    void RunNeuralSettingsCoreTests()
    {
        namespace NC = Phantasm::NeuralSettingsCore;

        // ── Which mod a library belongs to ──
        Check(NC::ModOf("C:\\Game\\Data\\UpscalerBasePlugin\\nvngx_dlssnr.dll") == NC::Mod::SkyrimUpscaler &&
                  NC::ModOf("c:/game/upscalerbaseplugin/NVNGX_DLSSNR.DLL") == NC::Mod::SkyrimUpscaler,
              "neural settings: the Upscaler's folder names the Skyrim Upscaler, whatever the case and slashes");
        Check(NC::ModOf("C:\\Game\\Data\\SKSE\\Plugins\\TheosRenderPipeline\\NVIDIA\\nvngx_dlssnr.dll") == NC::Mod::Theos,
              "neural settings: Theo's folder names Theo's Render Pipeline");
        Check(NC::ModOf("C:\\Game\\Data\\Shaders\\Upscaling\\Streamline\\nvngx_dlssnr.dll") == NC::Mod::OpenShaders,
              "neural settings: the shaders' Streamline folder names Open Shaders");
        Check(NC::ModOf("C:\\Game\\Data\\SKSE\\Plugins\\TheosRenderPipeline\\NVIDIA\\nvngx_dlssnr_int8.dll") == NC::Mod::Theos,
              "neural settings: a library under Theo's own name is Theo's too");
        Check(NC::ModOf("D:\\Other Game\\nvngx_dlssnr.dll") == NC::Mod::None, "neural settings: another folder follows no mod");

        // ── An ini value ──
        const char* ini = "\xEF\xBB\xBF[Other]\nmDLSSNRIntensity = 0.1\n[DLSS NR]\n#mDLSSNRIntensity = 0.2\n"
                          "mDLSSNRIntensity = 0.75 ; a note\r\n mEnableDLSSNR=TRUE\n[Next]\nmDLSSNRStyle = 1\n";
        Check(NC::IniValue(ini, "dlss nr", "MDLSSNRINTENSITY") == std::optional<std::string>("0.75"),
              "neural settings: a key is read from its own section, names in any case, comments left out");
        Check(!NC::IniValue(ini, "DLSS NR", "mDLSSNRStyle"), "neural settings: a key of another section is not there");
        Check(NC::Bool(" Yes ") == std::optional<bool>(true) && NC::Bool("0") == std::optional<bool>(false) && !NC::Bool("maybe"),
              "neural settings: a switch reads true, false or nothing");
        Check(!NC::Number("1.0x") && !NC::Number("nan") && NC::Number(" 0.5 ") == std::optional<float>(0.5f),
              "neural settings: a number is all of its text and finite");

        // ── Skyrim Upscaler ──
        {
            const auto f = NC::ParseSkyrimUpscaler(
                "[DLSS NR]\nmEnableDLSSNR = true\nmDLSSNRStyle = 2\nmDLSSNRIntensity = 0.800000\n"
                "mDLSSNRLocalToneStrength = 0.500000\nmDLSSNRLocalStructureStrength = 0.250000\n"
                "mDLSSNRSkinStructureStrength = -1\nmDLSSNRUseAutoMask = false\n");
            Check(f.enabled == std::optional<bool>(true) && f.style == std::optional<int>(2) && f.intensity == std::optional<float>(0.8f) &&
                      f.tone == std::optional<float>(0.5f) && f.structure == std::optional<float>(0.25f) &&
                      f.skin == std::optional<float>(-1.0f),
                  "neural settings: the Skyrim Upscaler's file is read");
            const auto s = NC::Merge({}, f);
            Check(s.model == 2 && s.intensity == 0.8f && s.tone == 0.5f && s.structure == 0.25f && s.skin == 0.25f,
                  "neural settings: a negative skin strength takes the structure's");
            Check(!NC::ParseSkyrimUpscaler("[Settings]\nmUpscaleType = 1\n").Any(),
                  "neural settings: a file with no NR section sets nothing");
        }

        // ── Theo's Render Pipeline ──
        {
            const char* defaults = "[SourceDLSSG]\nNeuralRenderingEnabled=false\nNRPasses=1\n";
            const char* user     = "[SourceDLSSG]\nNeuralRenderingEnabled=true\nNRStyle=7\nNRIntensity=1.5\nNRLocalTone=0.4\n";
            const auto  f        = NC::ParseTheos(defaults, user);
            Check(f.enabled == std::optional<bool>(true) && f.style == std::optional<int>(7) && f.tone == std::optional<float>(0.4f) &&
                      !f.structure && !f.skin,
                  "neural settings: Theo's user file wins over the shipped one, a key in neither is not set");
            Check(NC::ParseTheos(defaults, "").enabled == std::optional<bool>(false),
                  "neural settings: with no user file Theo's shipped values stand");
            Check(NC::TheosRuntime("[Experimental]\nNeuralRenderingRuntimePath=Automatic\n", "").empty() &&
                      NC::TheosRuntime("[SourceDLSSG]\nNRStyle=1\n", "").empty(),
                  "neural settings: Theo's Automatic, or no key, names no library");
            Check(NC::TheosRuntime("[Experimental]\nNeuralRenderingRuntimePath=Automatic\n",
                                   "[Experimental]\nNeuralRenderingRuntimePath = TheosRenderPipeline/NVIDIA/nvngx_dlssnr_int8.dll\n") ==
                          "nvngx_dlssnr_int8.dll" &&
                      NC::TheosRuntime("[Experimental]\nNeuralRenderingRuntimePath=\"nvngx_dlssnr_310.9.16.dll\"\n", "") ==
                          "nvngx_dlssnr_310.9.16.dll",
                  "neural settings: Theo's named library is read, the user file's first, as a file name");
            Check(NC::TheosRuntime("[Experimental]\nNeuralRenderingRuntimePath=nvngx_dlssnr_310.9.16.trpk\n", "").empty(),
                  "neural settings: a value that names no library is none");
            NeuralStillCore::Settings base;
            base.structure = 0.3f;
            const auto s = NC::Merge(base, f);
            Check(s.model == 2 && s.intensity == 1.0f && s.tone == 0.4f && s.structure == 0.3f,
                  "neural settings: values are brought into Phantasm's ranges, and what the mod leaves keeps the ini's");
        }

        // ── Open Shaders ──
        {
            const char* json = R"({ "Menu": { "neuralRenderingEnabled": false, "note": "a } brace \" in a string" },
                "Upscaling": { "qualityMode": 1, "neuralRenderingEnabled": true,
                               "neuralRenderingTuning": { "materialStrength": { "intensity": 9 }, "intensity": 0.6,
                                                          "style": 1, "localToneStrength": 0.7,
                                                          "localStructureStrength": 0.2, "skinStructureStrength": 0.9,
                                                          "useAutoMask": true },
                               "sharpnessDLSS": 0.0 } })";
            const auto  f    = NC::ParseOpenShaders(json);
            Check(f.enabled == std::optional<bool>(true) && f.style == std::optional<int>(1) && f.intensity == std::optional<float>(0.6f) &&
                      f.tone == std::optional<float>(0.7f) && f.structure == std::optional<float>(0.2f) &&
                      f.skin == std::optional<float>(0.9f),
                  "neural settings: Open Shaders' Upscaling block is read, a nested or another block's key is not taken");
            Check(!NC::ParseOpenShaders(R"({ "Upscaling": { "qualityMode": 1 } })").Any() && !NC::ParseOpenShaders("not json").Any() &&
                      !NC::ParseOpenShaders(R"({ "Upscaling": { "neuralRenderingTuning": )").Any(),
                  "neural settings: a file without the keys, or cut short, sets nothing");
            Check(NC::JsonMember(R"({ "a": "x,y}", "b": [1, {"c": 2}], "d": 3 })", "d") == std::optional<std::string_view>("3"),
                  "neural settings: a member is found past strings and nested values");
        }
    }
}
