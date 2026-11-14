// Engine-free tests for NeuralSourceCore (which NR library NR runs from).

#include "TestHarness.h"

#include "NeuralSourceCore.h"

#include <string>
#include <vector>

namespace Phantasm::Tests
{
    void RunNeuralSourceCoreTests()
    {
        namespace NS = Phantasm::NeuralSourceCore;

        Check(NS::ParseFolder("").mode == NS::Mode::Off, "neural source: an empty Folder is off");
        Check(NS::ParseFolder("  OFF ").mode == NS::Mode::Off, "neural source: off is off");
        Check(NS::ParseFolder("0").mode == NS::Mode::Off && NS::ParseFolder("none").mode == NS::Mode::Off,
              "neural source: 0 and none are off");
        Check(NS::ParseFolder(" Auto ").mode == NS::Mode::Auto, "neural source: auto, in any case");

        const auto folder = NS::ParseFolder("  \"D:\\Games\\Some Game\\\"  ");
        Check(folder.mode == NS::Mode::Folder && folder.folder == "D:\\Games\\Some Game",
              "neural source: a folder loses its quotes and trailing slash");
        const auto file = NS::ParseFolder("D:/Games/Some Game/NVNGX_DLSSNR.DLL");
        Check(file.mode == NS::Mode::Folder && file.folder == "D:/Games/Some Game" && file.file == "NVNGX_DLSSNR.DLL",
              "neural source: a path to the library names its folder");
        Check(folder.file == NS::kLibrary, "neural source: a folder holds the library under NVIDIA's name");
        const auto renamed = NS::ParseFolder("D:\\Mods\\NVIDIA\\nvngx_dlssnr_int8.dll");
        Check(renamed.mode == NS::Mode::Folder && renamed.folder == "D:\\Mods\\NVIDIA" && renamed.file == "nvngx_dlssnr_int8.dll",
              "neural source: a path to a library under a mod's own name names that file");
        Check(NS::ParseFolder("C:\\").folder == "C:\\", "neural source: a drive's root keeps its slash");
        Check(NS::ParseFolder("D:\\my_nvngx_dlssnr.dll").folder == "D:\\my_nvngx_dlssnr.dll",
              "neural source: only the library's own name is cut off");

        std::vector<NS::Found> found{
            { "C:\\Game\\nvngx_dlssnr.dll", NS::Place::Game, 0 },
            { "C:\\Game\\Data\\Shaders\\Upscaling\\Streamline\\nvngx_dlssnr.dll", NS::Place::Shaders, 2 },
            { "C:\\Game\\Data\\SKSE\\Plugins\\B\\NVIDIA\\nvngx_dlssnr.dll", NS::Place::Plugins, 2 },
            { "C:\\Game\\Data\\SKSE\\Plugins\\A\\nvngx_dlssnr.dll", NS::Place::Plugins, 1 },
            { "c:/game/data/skse/plugins/b/nvidia/NVNGX_DLSSNR.dll", NS::Place::Loaded, 0 },
        };
        NS::Rank(found);
        Check(found.size() == 4, "neural source: one entry per file, whatever its case and slashes");
        Check(found[0].place == NS::Place::Loaded, "neural source: the copy a mod has loaded is tried first");
        Check(found[1].file == "C:\\Game\\Data\\SKSE\\Plugins\\A\\nvngx_dlssnr.dll",
              "neural source: then the plugins' copies, the shallowest first");
        Check(found[2].place == NS::Place::Shaders && found[3].place == NS::Place::Game,
              "neural source: then the shaders' copy, then the game folder's");

        // ── A library's names ──
        Check(NS::IsLibraryName("nvngx_dlssnr.dll") && NS::IsLibraryName("NVNGX_DLSSNR_INT8.DLL") &&
                  NS::IsLibraryName("nvngx_dlssnr_310.9.16.dll"),
              "neural source: NVIDIA's name and a mod's own names are a library's");
        Check(!NS::IsLibraryName("nvngx_dlss.dll") && !NS::IsLibraryName("my_nvngx_dlssnr.dll") &&
                  !NS::IsLibraryName("nvngx_dlssnr_310.9.16.trpk") && !NS::IsLibraryName("nvngx_dlssnr_310.9.16_fast.runtime.bin") &&
                  !NS::IsLibraryName("nvngx_dlssnr"),
              "neural source: another library, a kernel pack and a runtime copy are not");
        Check(NS::FileName("C:\\a/b\\c.dll") == "c.dll" && NS::FileName("c.dll") == "c.dll", "neural source: a path's file name");
        Check(NS::NameVersion("nvngx_dlssnr_310.9.16.dll") == std::vector<int>{ 310, 9, 16 } &&
                  NS::NameVersion("NVNGX_DLSSNR_311.DLL") == std::vector<int>{ 311 },
              "neural source: a name's version is read");
        Check(NS::NameVersion("nvngx_dlssnr.dll").empty() && NS::NameVersion("nvngx_dlssnr_int8.dll").empty() &&
                  NS::NameVersion("nvngx_dlssnr_310..9.dll").empty() && NS::NameVersion("nvngx_dlssnr_310.9..dll").empty() &&
                  NS::NameVersion("nvngx_dlssnr310.dll").empty(),
              "neural source: a name with no version, or a broken one, has none");

        // ── The GPU ──
        Check(NS::GpuSeries("NVIDIA GeForce RTX 5080") == 50 && NS::GpuSeries("NVIDIA GeForce RTX 3060 Ti") == 30 &&
                  NS::GpuSeries("nvidia geforce rtx 4070 Laptop GPU") == 40 && NS::GpuSeries("NVIDIA GeForce RTX 2080 SUPER") == 20,
              "neural source: a GeForce RTX card's generation is read from its name");
        Check(NS::GpuSeries("NVIDIA RTX A4000") == 0 && NS::GpuSeries("NVIDIA GeForce GTX 1080") == 0 &&
                  NS::GpuSeries("AMD Radeon RX 7900") == 0 && NS::GpuSeries("NVIDIA GeForce RTX 50") == 0 && NS::GpuSeries("") == 0,
              "neural source: any other card's is unknown");
        Check(!NS::Suits("nvngx_dlssnr_int8.dll", 40) && NS::Suits("NVNGX_DLSSNR_INT8.dll", 30) && NS::Suits("nvngx_dlssnr_int8.dll", 50),
              "neural source: the INT8 build is left out on RTX 40 only");
        Check(NS::Suits("nvngx_dlssnr_310.9.16.dll", 50) && NS::Suits("nvngx_dlssnr_310.9.16.dll", 60) &&
                  !NS::Suits("nvngx_dlssnr_310.9.16.dll", 40) && !NS::Suits("nvngx_dlssnr_310.9.16.dll", 20),
              "neural source: an NVIDIA build under its version runs unpatched on RTX 50 and later only");
        Check(NS::Suits("nvngx_dlssnr.dll", 20) && NS::Suits("nvngx_dlssnr.dll", 40) && NS::Suits("nvngx_dlssnr_int8.dll", 0) &&
                  NS::Suits("nvngx_dlssnr_310.9.16.dll", 0),
              "neural source: NVIDIA's name suits every card, and every name an unknown card");

        // ── Several libraries in one folder ──
        const auto theos = [] {
            return std::vector<NS::Found>{
                { "C:\\G\\Data\\SKSE\\Plugins\\Theos\\NVIDIA\\nvngx_dlssnr_int8.dll", NS::Place::Plugins, 2 },
                { "C:\\G\\Data\\SKSE\\Plugins\\Theos\\NVIDIA\\nvngx_dlssnr_310.9.16.dll", NS::Place::Plugins, 2 },
                { "C:\\G\\Data\\SKSE\\Plugins\\Theos\\NVIDIA\\nvngx_dlssnr.dll", NS::Place::Plugins, 2 },
                { "C:\\G\\Data\\SKSE\\Plugins\\Theos\\NVIDIA\\nvngx_dlssnr_310.10.2.dll", NS::Place::Plugins, 2 },
                { "C:\\G\\Data\\SKSE\\Plugins\\Other\\NVIDIA\\nvngx_dlssnr_int8.dll", NS::Place::Plugins, 2 },
            };
        };
        const auto names = [](const std::vector<NS::Found>& list) {
            std::string out;
            for (const auto& f : list) {
                if (f.file.find("\\Theos\\") != std::string::npos) out += std::string(NS::FileName(f.file)) + " ";
            }
            return out;
        };
        {
            auto list = theos();
            NS::Rank(list);
            Check(list[0].file.find("\\Other\\") != std::string::npos, "neural source: folders keep their order by path");
            Check(names(list) == "nvngx_dlssnr.dll nvngx_dlssnr_310.10.2.dll nvngx_dlssnr_310.9.16.dll nvngx_dlssnr_int8.dll ",
                  "neural source: in a folder NVIDIA's name is tried first, then the others by name");
        }
        {
            auto list = theos();
            NS::Rank(list, { {}, 50 });
            Check(names(list) == "nvngx_dlssnr_310.10.2.dll nvngx_dlssnr_310.9.16.dll nvngx_dlssnr.dll nvngx_dlssnr_int8.dll ",
                  "neural source: on RTX 50 the NVIDIA builds under a version go first, the newest ahead");
        }
        {
            auto list = theos();
            NS::Rank(list, { "c:/g/data/skse/plugins/theos/nvidia/NVNGX_DLSSNR_INT8.DLL", 50 });
            Check(names(list) == "nvngx_dlssnr_int8.dll nvngx_dlssnr_310.10.2.dll nvngx_dlssnr_310.9.16.dll nvngx_dlssnr.dll " &&
                      list[0].file.find("\\Other\\") != std::string::npos,
                  "neural source: the file the mod is set to run from heads its own folder, and no other's");
        }

        const auto name = NS::CopyName("C:\\Mods\\X\\nvngx_dlssnr.dll", 276934144, 1000);
        Check(name.size() == 16, "neural source: a copy's folder name is 16 hex digits");
        Check(name == NS::CopyName("c:/mods/x/NVNGX_DLSSNR.DLL", 276934144, 1000),
              "neural source: the same file gives the same name, whatever its case and slashes");
        Check(name != NS::CopyName("C:\\Mods\\X\\nvngx_dlssnr.dll", 276934145, 1000) &&
                  name != NS::CopyName("C:\\Mods\\X\\nvngx_dlssnr.dll", 276934144, 1001) &&
                  name != NS::CopyName("C:\\Mods\\Y\\nvngx_dlssnr.dll", 276934144, 1000),
              "neural source: another size, write time or path gives another name");
    }
}
