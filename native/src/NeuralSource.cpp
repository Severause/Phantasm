#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "NeuralSource.h"

#include <cstdint>
#include <system_error>

namespace Phantasm::NeuralSource
{
    namespace Src = NeuralSourceCore;

    std::wstring Widen(std::string_view utf8)
    {
        if (utf8.empty()) return {};
        const int    n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring w(static_cast<std::size_t>(n), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), w.data(), n);
        return w;
    }

    std::string Narrow(std::wstring_view wide)
    {
        if (wide.empty()) return {};
        const int   n = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(n), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::filesystem::path ModuleFile(void* module)
    {
        std::wstring buf(MAX_PATH, L'\0');
        for (;;) {
            const DWORD n = ::GetModuleFileNameW(static_cast<HMODULE>(module), buf.data(), static_cast<DWORD>(buf.size()));
            if (n == 0) return {};
            if (n < buf.size()) {
                buf.resize(n);
                break;
            }
            buf.resize(buf.size() * 2);
        }
        return std::filesystem::path(buf);
    }

    std::vector<Src::Found> Discover(const std::filesystem::path& game)
    {
        std::vector<Src::Found> found;
        const std::wstring      library = Widen(Src::kLibrary), stem = Widen(Src::kLibraryStem);
        if (const HMODULE loaded = ::GetModuleHandleW(library.c_str())) {
            if (const auto file = ModuleFile(loaded); !file.empty()) {
                found.push_back({ Narrow(file.wstring()), Src::Place::Loaded, 0 });
            }
        }
        if (game.empty()) return found;
        // Files down to maxDepth folders below root; skipData leaves root's own Data folder out.
        const auto walk = [&](const std::filesystem::path& root, Src::Place place, int maxDepth, bool skipData) {
            std::error_code                                     ec;
            std::filesystem::recursive_directory_iterator       it(root, std::filesystem::directory_options::skip_permission_denied, ec);
            const std::filesystem::recursive_directory_iterator end;
            for (; !ec && it != end; it.increment(ec)) {
                const auto      name = it->path().filename();
                std::error_code ignored;
                if (it->is_directory(ignored)) {
                    if (it.depth() >= maxDepth || (skipData && it.depth() == 0 && _wcsicmp(name.c_str(), L"Data") == 0)) {
                        it.disable_recursion_pending();
                    }
                } else if (_wcsnicmp(name.c_str(), stem.c_str(), stem.size()) == 0 && Src::IsLibraryName(Narrow(name.wstring()))) {
                    found.push_back({ Narrow(it->path().wstring()), place, it.depth() });
                }
            }
        };
        walk(game / L"Data" / L"SKSE" / L"Plugins", Src::Place::Plugins, 3, false);
        walk(game / L"Data" / L"Shaders", Src::Place::Shaders, 3, false);
        walk(game / L"Data" / L"UpscalerBasePlugin", Src::Place::Game, 1, false);
        walk(game, Src::Place::Game, 2, true);
        Src::Rank(found);
        return found;
    }

    std::filesystem::path RealPath(const std::filesystem::path& file)
    {
        const HANDLE h = ::CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return {};
        constexpr DWORD kNameFlags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
        std::wstring    real(1024, L'\0');
        DWORD           n = ::GetFinalPathNameByHandleW(h, real.data(), static_cast<DWORD>(real.size()), kNameFlags);
        if (n >= real.size()) {
            real.resize(static_cast<std::size_t>(n) + 1);
            n = ::GetFinalPathNameByHandleW(h, real.data(), static_cast<DWORD>(real.size()), kNameFlags);
        }
        ::CloseHandle(h);
        if (n == 0 || n >= real.size()) return {};
        real.resize(n);
        // "\\?\C:\..." as a plain path ("\\?\UNC\server\share" as "\\server\share"): a command line and NVIDIA's
        // loader take that form.
        if (real.rfind(L"\\\\?\\UNC\\", 0) == 0) real = L"\\\\" + real.substr(8);
        else if (real.rfind(L"\\\\?\\", 0) == 0) real = real.substr(4);
        return std::filesystem::path(real);
    }

    namespace
    {
        // Where the private copies are kept: off the documents folder, which a cloud drive may sync.
        std::filesystem::path CopiesFolder(const std::filesystem::path& fallback)
        {
            std::wstring local(MAX_PATH, L'\0');
            if (const DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(), static_cast<DWORD>(local.size()));
                len > 0 && len < local.size()) {
                local.resize(len);
                return std::filesystem::path(local) / L"Phantasm" / L"NR";
            }
            return fallback.empty() ? std::filesystem::path{} : fallback / L"NR";
        }
    }

    std::filesystem::path PrivateCopy(const std::filesystem::path& file, const std::filesystem::path& fallback,
                                      std::string& why, std::string& note)
    {
        note.clear();
        // The file's real place names the copy: under a mod manager the game's path to it is virtual.
        const auto                real = RealPath(file);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (real.empty() || !::GetFileAttributesExW(real.c_str(), GetFileExInfoStandard, &info)) {
            why = "the library file is not there";
            return {};
        }
        const std::uint64_t size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
        const std::uint64_t time = (std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;

        const auto base = CopiesFolder(fallback);
        if (base.empty()) {
            why = "there is nowhere to keep a private copy";
            return {};
        }
        const auto      folder = base / Src::CopyName(Narrow(real.wstring()), size, time);
        const auto      target = folder / real.filename();
        std::error_code ec;
        if (std::filesystem::file_size(target, ec) != size || ec) {
            std::filesystem::remove(target, ec);
            std::filesystem::create_directories(folder, ec);
            // A real copy: Windows gives a hard link the module already loaded from the file's other name.
            if (!::CopyFileW(real.c_str(), target.c_str(), FALSE)) {
                why = "no private copy could be made (error " + std::to_string(::GetLastError()) + ")";
                return {};
            }
            note = "private copy made at '" + Narrow(target.wstring()) + "' (" + std::to_string(size >> 20) + " MB)";
        }
        return target;
    }

    void PruneCopies(const std::filesystem::path& fallback, const std::filesystem::path& keep)
    {
        const auto base = CopiesFolder(fallback);
        if (base.empty()) return;
        std::error_code ec;
        for (std::filesystem::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code ignored;
            if (keep.empty() || it->path().filename() != keep.filename()) std::filesystem::remove_all(it->path(), ignored);
        }
    }
}
