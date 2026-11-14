// NR's work (PhantasmNRInProc.dll's): NVIDIA DLSS NR through NVIDIA's loader, the NGX core of
// the graphics driver, or through the feature library's own entry points when that loader does not take the copy
// (see PhantasmNR.h). Both are libraries on the player's machine, loaded here and called by their export names
// (Ngx.h): no code of NVIDIA's is built in. Stills run for Phantasm's NR worker and wait on their own fence; a live
// view's frames are queued for the render thread and never wait. The queue and NGX are each behind a lock (Vulkan
// queues and NGX calls are not free-threaded); neither lock is held across a wait.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <vulkan/vulkan.h>
#include <softpub.h>
#include <wintrust.h>

#include "NeuralLayerCore.h"
#include "Ngx.h"
#include "PhantasmNR.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    // NGX names an application by a project id when NVIDIA issued none (Ngx::kCustomEngine).
    constexpr const char* kProjectId = "6f1e2b74-3c1a-4e0b-9d51-2f7a8c4e9b10";
    // The application id a library started without NVIDIA's loader is given: the one that loader falls back to for
    // an application NVIDIA issued no id (its log's cmsid).
    constexpr unsigned long long kDirectAppId = 0x876232C;

    PhantasmNR_LogFn g_log = nullptr;

    void Log(const char* fmt, ...)
    {
        if (!g_log) return;
        char    buf[1024];
        va_list a;
        va_start(a, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, a);
        va_end(a);
        g_log(buf);
    }

    void SetError(char* error, std::uint32_t cap, const char* fmt, ...)
    {
        if (!error || !cap) return;
        va_list a;
        va_start(a, fmt);
        std::vsnprintf(error, cap, fmt, a);
        va_end(a);
    }

    // NGX's own log, cut to NR's lines: at this level it also lists every other feature it looks for.
    void __cdecl NgxLog(const char* message, int, int)
    {
        if (!g_log || !message) return;
        std::string line(message);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        std::string lower(line);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.find("dlssnr") == std::string::npos) return;
        g_log(("NGX: " + line).c_str());
    }

    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkGetDeviceProcAddr   gdpa = nullptr;
#define PNR_VK(name) PFN_##name name = nullptr;
    PNR_VK(vkCreateInstance) PNR_VK(vkDestroyInstance) PNR_VK(vkEnumeratePhysicalDevices)
    PNR_VK(vkGetPhysicalDeviceProperties) PNR_VK(vkGetPhysicalDeviceProperties2)
    PNR_VK(vkGetPhysicalDeviceQueueFamilyProperties) PNR_VK(vkGetPhysicalDeviceMemoryProperties)
    PNR_VK(vkEnumerateDeviceExtensionProperties) PNR_VK(vkCreateDevice) PNR_VK(vkGetPhysicalDeviceFeatures2)
    PNR_VK(vkDestroyDevice) PNR_VK(vkGetDeviceQueue) PNR_VK(vkCreateCommandPool) PNR_VK(vkDestroyCommandPool)
    PNR_VK(vkAllocateCommandBuffers) PNR_VK(vkBeginCommandBuffer) PNR_VK(vkEndCommandBuffer)
    PNR_VK(vkResetCommandBuffer) PNR_VK(vkQueueSubmit) PNR_VK(vkQueueWaitIdle) PNR_VK(vkDeviceWaitIdle)
    PNR_VK(vkCreateImage) PNR_VK(vkDestroyImage) PNR_VK(vkGetImageMemoryRequirements) PNR_VK(vkAllocateMemory)
    PNR_VK(vkFreeMemory) PNR_VK(vkBindImageMemory) PNR_VK(vkCreateImageView) PNR_VK(vkDestroyImageView)
    PNR_VK(vkCreateBuffer) PNR_VK(vkDestroyBuffer) PNR_VK(vkGetBufferMemoryRequirements) PNR_VK(vkBindBufferMemory)
    PNR_VK(vkMapMemory) PNR_VK(vkCmdPipelineBarrier) PNR_VK(vkCmdCopyBufferToImage) PNR_VK(vkCmdCopyImageToBuffer)
    PNR_VK(vkCmdClearColorImage) PNR_VK(vkCreateFence) PNR_VK(vkDestroyFence) PNR_VK(vkWaitForFences)
    PNR_VK(vkResetFences) PNR_VK(vkCreateSemaphore) PNR_VK(vkDestroySemaphore)
    PNR_VK(vkGetMemoryWin32HandlePropertiesKHR) PNR_VK(vkImportSemaphoreWin32HandleKHR)
#undef PNR_VK

    HMODULE                          g_vulkan = nullptr;
    VkInstance                       g_inst   = VK_NULL_HANDLE;
    VkPhysicalDevice                 g_pd     = VK_NULL_HANDLE;
    VkDevice                         g_dev    = VK_NULL_HANDLE;
    std::uint32_t                    g_family = 0;
    VkQueue                          g_queue  = VK_NULL_HANDLE;
    VkCommandPool                    g_pool   = VK_NULL_HANDLE;   // the stills' (worker thread)
    VkCommandBuffer                  g_cmd    = VK_NULL_HANDLE;
    VkFence                          g_fence  = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties g_mem{};
    bool                             g_ngx    = false;
    bool                             g_interop = false;   // the device takes the game's D3D11 textures and fence
    Ngx::Parameter*             g_params = nullptr;  // the stills'
    std::wstring                     g_featureFolder;
    const wchar_t*                   g_paths[1]{};

    std::mutex g_queueMx;   // vkQueueSubmit / vkQueueWaitIdle
    std::mutex g_ngxMx;     // NGX create, evaluate, release

    // While one lives, the Vulkan loader leaves every implicit layer out of an instance made in this process
    // (NeuralLayerCore). The variable is the whole process's, so the value it had comes back when this goes.
    struct LayersOut
    {
        bool         on  = false;
        bool         had = false;
        std::wstring before;

        explicit LayersOut(bool wanted) : on(wanted)
        {
            if (!on) return;
            namespace NL = Phantasm::NeuralLayerCore;
            const DWORD n = ::GetEnvironmentVariableW(NL::kDisableVariable.data(), nullptr, 0);
            if (n > 0) {
                before.resize(n);
                const DWORD got = ::GetEnvironmentVariableW(NL::kDisableVariable.data(), before.data(), n);
                had             = got > 0 && got < n;
                before.resize(had ? got : 0);
            }
            ::SetEnvironmentVariableW(NL::kDisableVariable.data(), NL::kDisableImplicit.data());
        }
        ~LayersOut()
        {
            if (on) ::SetEnvironmentVariableW(Phantasm::NeuralLayerCore::kDisableVariable.data(), had ? before.c_str() : nullptr);
        }
        LayersOut(const LayersOut&)            = delete;
        LayersOut& operator=(const LayersOut&) = delete;
    };

    std::uint32_t MemType(std::uint32_t bits, VkMemoryPropertyFlags want)
    {
        for (std::uint32_t i = 0; i < g_mem.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) && (g_mem.memoryTypes[i].propertyFlags & want) == want) return i;
        }
        return UINT32_MAX;
    }

    struct Img
    {
        VkImage        image = VK_NULL_HANDLE;
        VkDeviceMemory mem   = VK_NULL_HANDLE;
        VkImageView    view  = VK_NULL_HANDLE;
        VkFormat       format{};

        void Release()
        {
            if (view) vkDestroyImageView(g_dev, view, nullptr);
            if (image) vkDestroyImage(g_dev, image, nullptr);
            if (mem) vkFreeMemory(g_dev, mem, nullptr);
            *this = {};
        }
    };

    struct Buf
    {
        VkBuffer       buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        void*          map = nullptr;

        void Release()
        {
            if (buf) vkDestroyBuffer(g_dev, buf, nullptr);
            if (mem) vkFreeMemory(g_dev, mem, nullptr);   // unmaps
            *this = {};
        }
    };

    constexpr VkImageUsageFlags kImageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    bool MakeView(Img& im)
    {
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image            = im.image;
        vi.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vi.format           = im.format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        return vkCreateImageView(g_dev, &vi, nullptr, &im.view) == VK_SUCCESS;
    }

    bool MakeImage(Img& im, VkFormat fmt, std::uint32_t w, std::uint32_t h)
    {
        im.format = fmt;
        VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = fmt;
        ci.extent        = { w, h, 1 };
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = kImageUsage;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(g_dev, &ci, nullptr, &im.image) != VK_SUCCESS) return false;
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(g_dev, im.image, &mr);
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = MemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(g_dev, &ai, nullptr, &im.mem) != VK_SUCCESS) return false;
        if (vkBindImageMemory(g_dev, im.image, im.mem, 0) != VK_SUCCESS) return false;
        return MakeView(im);
    }

    // A D3D11 texture shared by NT handle, as a Vulkan image on the same memory. The handle stays the caller's.
    bool ImportImage(Img& im, HANDLE handle, VkFormat fmt, std::uint32_t w, std::uint32_t h)
    {
        im.format = fmt;
        constexpr auto                  kType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        VkExternalMemoryImageCreateInfo ext{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
        ext.handleTypes = kType;
        VkImageCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ci.pNext         = &ext;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.format        = fmt;
        ci.extent        = { w, h, 1 };
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.usage         = kImageUsage;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(g_dev, &ci, nullptr, &im.image) != VK_SUCCESS) return false;
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(g_dev, im.image, &mr);
        VkMemoryWin32HandlePropertiesKHR hp{ VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR };
        if (vkGetMemoryWin32HandlePropertiesKHR(g_dev, kType, handle, &hp) != VK_SUCCESS) hp.memoryTypeBits = ~0u;
        std::uint32_t bits = mr.memoryTypeBits & hp.memoryTypeBits;
        if (!bits) bits = mr.memoryTypeBits;
        VkMemoryDedicatedAllocateInfo   dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
        dedicated.image = im.image;
        VkImportMemoryWin32HandleInfoKHR import{ VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR };
        import.pNext      = &dedicated;
        import.handleType = kType;
        import.handle     = handle;
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.pNext           = &import;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = MemType(bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = MemType(bits, 0);
        if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(g_dev, &ai, nullptr, &im.mem) != VK_SUCCESS) return false;
        if (vkBindImageMemory(g_dev, im.image, im.mem, 0) != VK_SUCCESS) return false;
        return MakeView(im);
    }

    bool MakeBuffer(Buf& b, VkDeviceSize size)
    {
        VkBufferCreateInfo ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        ci.size  = size;
        ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (vkCreateBuffer(g_dev, &ci, nullptr, &b.buf) != VK_SUCCESS) return false;
        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(g_dev, b.buf, &mr);
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = MemType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(g_dev, &ai, nullptr, &b.mem) != VK_SUCCESS) return false;
        if (vkBindBufferMemory(g_dev, b.buf, b.mem, 0) != VK_SUCCESS) return false;
        return vkMapMemory(g_dev, b.mem, 0, size, 0, &b.map) == VK_SUCCESS;
    }

    void Barrier(VkCommandBuffer cmd, VkImage img, VkImageLayout from, VkImageLayout to,
                 std::uint32_t srcFamily = VK_QUEUE_FAMILY_IGNORED, std::uint32_t dstFamily = VK_QUEUE_FAMILY_IGNORED)
    {
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask       = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.oldLayout           = from;
        b.newLayout           = to;
        b.srcQueueFamilyIndex = srcFamily;
        b.dstQueueFamilyIndex = dstFamily;
        b.image               = img;
        b.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &b);
    }

    void Begin(VkCommandBuffer cmd)
    {
        vkResetCommandBuffer(cmd, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);
    }

    // Submits cmd and waits for it on fence, the queue lock held only for the submit.
    bool SubmitAndWait(VkCommandBuffer cmd, VkFence fence)
    {
        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cmd;
        vkResetFences(g_dev, 1, &fence);
        {
            std::lock_guard lock(g_queueMx);
            if (vkQueueSubmit(g_queue, 1, &si, fence) != VK_SUCCESS) return false;
        }
        return vkWaitForFences(g_dev, 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    }

    void Begin() { Begin(g_cmd); }
    bool Submit() { return SubmitAndWait(g_cmd, g_fence); }

    void SetSubrects(Ngx::Parameter* q, std::uint32_t w, std::uint32_t h)
    {
        for (const char* base : { "Color", "Output", "Depth", "MVec" }) {
            const std::string k = std::string("DLSSNR.") + base + "Subrect";
            q->Set((k + "BaseX").c_str(), 0);
            q->Set((k + "BaseY").c_str(), 0);
            q->Set((k + "Width").c_str(), int(w));
            q->Set((k + "Height").c_str(), int(h));
        }
    }

    void SetEffect(Ngx::Parameter* q, const PhantasmNR_Settings& s, bool reset)
    {
        q->Set(Ngx::Name::kMotionScaleX, 1.0f);
        q->Set(Ngx::Name::kMotionScaleY, 1.0f);
        q->Set(Ngx::Name::kEnabled, 1);
        q->Set(Ngx::Name::kDepthInverted, 0);
        q->Set(Ngx::Name::kReset, reset ? 1 : 0);
        q->Set(Ngx::Name::kAutoMask, 1);
        q->Set(Ngx::Name::kStyle, std::clamp(s.model, 0, 2));
        q->Set(Ngx::Name::kIntensity, s.intensity);
        q->Set(Ngx::Name::kTone, s.tone);
        q->Set(Ngx::Name::kStructure, s.structure);
        q->Set(Ngx::Name::kSkin, s.skin);
        q->Set(Ngx::Name::kUiCorrection, 0);
        q->Set(Ngx::Name::kJitterX, 0.0f);
        q->Set(Ngx::Name::kJitterY, 0.0f);
    }

    // ---- NVIDIA's loader: the driver's NGX core ----------------------------------------------------------------

    // The NGX core is a library of the graphics driver (_nvngx.dll; nvngx.dll on old drivers). It finds a feature
    // library, checks NVIDIA's signature on it and runs it, and it makes the parameter blocks. The driver records
    // its folder in the registry; the Windows system folder is where older drivers put it. Those places are
    // writable by administrators only, the trust the driver itself has. It stays loaded once found.
    struct Core
    {
        HMODULE                   module             = nullptr;
        Ngx::InstanceExtensionsFn instanceExtensions = nullptr;
        Ngx::DeviceExtensionsFn   deviceExtensions   = nullptr;
        Ngx::CoreInitFn           init               = nullptr;
        Ngx::ParametersFn         capabilities       = nullptr;
        Ngx::ParametersFn         allocateParameters = nullptr;
        Ngx::DestroyParametersFn  destroyParameters  = nullptr;
        Ngx::CreateFn             create             = nullptr;
        Ngx::EvaluateFn           evaluate           = nullptr;
        Ngx::ReleaseFn            release            = nullptr;
        Ngx::CoreShutdownFn       shutdown           = nullptr;
    } g_core;

    std::wstring RegistryText(const wchar_t* key, const wchar_t* value)
    {
        wchar_t buf[1024]{};
        DWORD   bytes = sizeof(buf) - sizeof(wchar_t);
        if (::RegGetValueW(HKEY_LOCAL_MACHINE, key, value, RRF_RT_REG_SZ, nullptr, buf, &bytes) != ERROR_SUCCESS) return {};
        return buf;
    }

    // Loads the core and finds its entry points. False and why when the machine has none NR can use.
    bool LoadCore(char* error, std::uint32_t cap)
    {
        if (g_core.module) return true;
        std::vector<std::wstring> folders;
        folders.push_back(RegistryText(L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm\\NGXCore", L"NGXPath"));
        folders.push_back(RegistryText(L"SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", L"FullPath"));
        wchar_t system[MAX_PATH]{};
        if (::GetSystemDirectoryW(system, MAX_PATH)) folders.emplace_back(system);
        HMODULE module = nullptr;
        for (const auto& folder : folders) {
            if (folder.empty()) continue;
            for (const wchar_t* name : { L"_nvngx.dll", L"nvngx.dll" }) {
                const std::wstring file = folder + L"\\" + name;
                if (::GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
                module = ::LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
                if (module) break;
            }
            if (module) break;
        }
        if (!module) {
            SetError(error, cap, "NVIDIA's NGX (_nvngx.dll) was not found: NR needs an NVIDIA graphics driver");
            return false;
        }
        Core core;
        core.module = module;
#define PNR_CORE(field, type, name) core.field = reinterpret_cast<Ngx::type>(::GetProcAddress(module, name));
        PNR_CORE(instanceExtensions, InstanceExtensionsFn, "NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements")
        PNR_CORE(deviceExtensions, DeviceExtensionsFn, "NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements")
        PNR_CORE(init, CoreInitFn, "NVSDK_NGX_VULKAN_Init_ProjectID_Ext")
        PNR_CORE(capabilities, ParametersFn, "NVSDK_NGX_VULKAN_GetCapabilityParameters")
        PNR_CORE(allocateParameters, ParametersFn, "NVSDK_NGX_VULKAN_AllocateParameters")
        PNR_CORE(destroyParameters, DestroyParametersFn, "NVSDK_NGX_VULKAN_DestroyParameters")
        PNR_CORE(create, CreateFn, "NVSDK_NGX_VULKAN_CreateFeature1")
        PNR_CORE(evaluate, EvaluateFn, "NVSDK_NGX_VULKAN_EvaluateFeature")
        PNR_CORE(release, ReleaseFn, "NVSDK_NGX_VULKAN_ReleaseFeature")
        PNR_CORE(shutdown, CoreShutdownFn, "NVSDK_NGX_VULKAN_Shutdown1")
#undef PNR_CORE
        if (!core.instanceExtensions || !core.deviceExtensions || !core.init || !core.capabilities || !core.allocateParameters ||
            !core.destroyParameters || !core.create || !core.evaluate || !core.release || !core.shutdown) {
            ::FreeLibrary(module);
            SetError(error, cap, "this graphics driver's NGX lacks an entry point NR needs (a driver too old for it)");
            return false;
        }
        g_core = core;
        return true;
    }

    // ---- a copy NVIDIA's loader does not take ------------------------------------------------------------------

    // The feature library's own Vulkan entry points, the ones NVIDIA's loader calls. An unsigned copy (a community
    // build for an older GPU, a pre-release one) is refused by that loader, and one under another file name is never
    // found by it, so it is loaded and called here. The library asks Windows for the file name of the module calling
    // it and takes only NVIDIA's loader: its imports of GetModuleFileName are pointed at CallerNameW / CallerNameA
    // while it is loaded. Parameter blocks stay the loader's (Core::allocateParameters), which runs without the
    // feature.
    struct Direct
    {
        HMODULE    module   = nullptr;
        bool       started  = false;   // Init answered: every feature call goes to the library itself
        bool       nvidias  = false;   // the file carries a valid signature
        bool       faulted  = false;   // a call into it faulted: nothing more is asked of it
        Ngx::LibraryInitFn init     = nullptr;
        Ngx::CreateFn      create   = nullptr;
        Ngx::EvaluateFn    evaluate = nullptr;
        Ngx::ReleaseFn     release  = nullptr;
        Ngx::ShutdownFn    shutdown = nullptr;

        struct Patch
        {
            void** slot     = nullptr;
            void*  original = nullptr;
        };
        std::vector<Patch> patches;
    } g_direct;

    HMODULE g_self = nullptr;   // this library's own module: the one NVIDIA's library sees calling it
    DWORD(WINAPI* g_realNameW)(HMODULE, LPWSTR, DWORD) = nullptr;
    DWORD(WINAPI* g_realNameA)(HMODULE, LPSTR, DWORD)  = nullptr;

    // NVIDIA's loader as this process has it, for the library's question about us.
    HMODULE LoaderModule() { return g_core.module; }

    DWORD WINAPI CallerNameW(HMODULE module, LPWSTR name, DWORD cap)
    {
        if (module && module == g_self) {
            if (HMODULE loader = LoaderModule()) return g_realNameW(loader, name, cap);
        }
        return g_realNameW(module, name, cap);
    }

    DWORD WINAPI CallerNameA(HMODULE module, LPSTR name, DWORD cap)
    {
        if (module && module == g_self) {
            if (HMODULE loader = LoaderModule()) return g_realNameA(loader, name, cap);
        }
        return g_realNameA(module, name, cap);
    }

    // Points the library's imports of GetModuleFileNameW / A at the two above. The count patched.
    int PatchCallerName(HMODULE library)
    {
        auto*       base = reinterpret_cast<std::uint8_t*>(library);
        const auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress || dir.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) return 0;
        for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; ++d) {
            if (!d->OriginalFirstThunk || !d->FirstThunk) continue;
            if (_stricmp(reinterpret_cast<const char*>(base + d->Name), "KERNEL32.dll") != 0) continue;
            auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + d->OriginalFirstThunk);
            auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + d->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots) {
                if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
                const char* fn   = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
                void*       ours = nullptr;
                void**      real = nullptr;
                if (std::strcmp(fn, "GetModuleFileNameW") == 0) {
                    ours = reinterpret_cast<void*>(&CallerNameW);
                    real = reinterpret_cast<void**>(&g_realNameW);
                } else if (std::strcmp(fn, "GetModuleFileNameA") == 0) {
                    ours = reinterpret_cast<void*>(&CallerNameA);
                    real = reinterpret_cast<void**>(&g_realNameA);
                } else {
                    continue;
                }
                auto** slot = reinterpret_cast<void**>(&slots->u1.Function);
                DWORD  old  = 0;
                if (*slot == ours || !::VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) continue;
                *real = *slot;
                g_direct.patches.push_back({ slot, *slot });
                *slot = ours;
                ::VirtualProtect(slot, sizeof(void*), old, &old);
            }
        }
        return int(g_direct.patches.size());
    }

    // True when Windows finds a valid signature in the file. Such a copy is NVIDIA's own: under NVIDIA's file name it
    // runs through NVIDIA's loader or not at all, since that loader refusing it means it was not built for this GPU or
    // driver.
    bool IsSigned(const std::wstring& file)
    {
        WINTRUST_FILE_INFO fi{};
        fi.cbStruct      = sizeof(fi);
        fi.pcwszFilePath = file.c_str();
        WINTRUST_DATA wd{};
        wd.cbStruct            = sizeof(wd);
        wd.dwUIChoice          = WTD_UI_NONE;
        wd.fdwRevocationChecks = WTD_REVOKE_NONE;
        wd.dwUnionChoice       = WTD_CHOICE_FILE;
        wd.pFile               = &fi;
        wd.dwStateAction       = WTD_STATEACTION_VERIFY;
        wd.dwProvFlags         = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;   // never the network
        GUID       action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        const LONG r      = ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
        wd.dwStateAction  = WTD_STATEACTION_CLOSE;
        ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &wd);
        return r == ERROR_SUCCESS;
    }

    // A copy built for another GPU can fault inside its own start or its feature's creation. That is a refusal
    // here, never the game's crash: false when the call faulted.
    bool GuardedInit(const wchar_t* dataFolder, Ngx::Result* r)
    {
        __try {
            *r = g_direct.init(kDirectAppId, dataFolder, g_inst, g_pd, g_dev, gipa, gdpa, Ngx::kInterfaceVersion, nullptr);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool GuardedCreate(VkCommandBuffer cmd, int id, Ngx::Parameter* q, Ngx::Handle** out,
                       Ngx::Result* r)
    {
        __try {
            *r = g_direct.create(g_dev, cmd, id, q, out);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void StopDirect()
    {
        if (g_direct.faulted) {
            // What the library left running is unknown: it stays loaded, its imports as they are.
            g_direct = Direct{};
            return;
        }
        if (g_direct.started && g_direct.shutdown && g_dev) g_direct.shutdown(g_dev);
        for (auto it = g_direct.patches.rbegin(); it != g_direct.patches.rend(); ++it) {
            DWORD old = 0;
            if (!::VirtualProtect(it->slot, sizeof(void*), PAGE_READWRITE, &old)) continue;
            *it->slot = it->original;
            ::VirtualProtect(it->slot, sizeof(void*), old, &old);
        }
        if (g_direct.module) ::FreeLibrary(g_direct.module);
        g_direct = Direct{};
    }

    // Loads the library itself and starts it on the device. False and why when it cannot.
    bool StartDirect(const std::wstring& file, const wchar_t* dataFolder, char* error, std::uint32_t cap)
    {
        ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCWSTR>(&StartDirect), &g_self);
        g_direct.module = ::LoadLibraryExW(file.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!g_direct.module || !g_self) {
            SetError(error, cap, "the library would not load (error %lu)", ::GetLastError());
            StopDirect();
            return false;
        }
#define PNR_FN(field, type, name) g_direct.field = reinterpret_cast<Ngx::type>(::GetProcAddress(g_direct.module, name));
        PNR_FN(init, LibraryInitFn, "NVSDK_NGX_VULKAN_Init_Ext2")
        PNR_FN(create, CreateFn, "NVSDK_NGX_VULKAN_CreateFeature1")
        PNR_FN(evaluate, EvaluateFn, "NVSDK_NGX_VULKAN_EvaluateFeature")
        PNR_FN(release, ReleaseFn, "NVSDK_NGX_VULKAN_ReleaseFeature")
        PNR_FN(shutdown, ShutdownFn, "NVSDK_NGX_VULKAN_Shutdown1")
#undef PNR_FN
        if (!g_direct.init || !g_direct.create || !g_direct.evaluate || !g_direct.release || !g_direct.shutdown) {
            SetError(error, cap, "the library lacks its Vulkan entry points");
            StopDirect();
            return false;
        }
        if (!PatchCallerName(g_direct.module)) {
            SetError(error, cap, "the library asks for its caller in a way this build does not know");
            StopDirect();
            return false;
        }
        Ngx::Result r = Ngx::kFail;
        if (!GuardedInit(dataFolder, &r)) {
            g_direct.faulted = true;
            SetError(error, cap, "the directly loaded library faulted while starting (a copy built for another GPU?)");
            StopDirect();
            return false;
        }
        if (Ngx::Failed(r)) {
            SetError(error, cap, "the directly loaded library did not start on this GPU (0x%08X)", unsigned(r));
            StopDirect();
            return false;
        }
        g_direct.started = true;
        return true;
    }

    Ngx::Result CreateFeature(VkCommandBuffer cmd, Ngx::Parameter* q, std::uint32_t w, std::uint32_t h,
                                   Ngx::Handle** out)
    {
        q->Set(Ngx::Name::kWidth, w);
        q->Set(Ngx::Name::kHeight, h);
        q->Set(Ngx::Name::kRenderPreset, 0);
        q->Set(Ngx::Name::kDepthInverted, 0);
        q->Set(Ngx::Name::kEnabled, 1);
        q->Set(Ngx::Name::kCreationNodeMask, 1u);
        q->Set(Ngx::Name::kVisibilityNodeMask, 1u);
        std::lock_guard lock(g_ngxMx);
        const int       id = Ngx::kNeuralRendering;
        if (!g_direct.started) return g_core.create(g_dev, cmd, id, q, out);
        Ngx::Result r = Ngx::kFail;
        if (g_direct.faulted) return r;
        if (!GuardedCreate(cmd, id, q, out, &r)) {
            g_direct.faulted = true;
            *out             = nullptr;
            Log("the directly loaded library faulted making its feature: NR stays off");
            return Ngx::kFail;
        }
        return r;
    }

    Ngx::Result Evaluate(VkCommandBuffer cmd, Ngx::Handle* feature, Ngx::Parameter* q)
    {
        std::lock_guard lock(g_ngxMx);
        if (!g_direct.started) return g_core.evaluate(cmd, feature, q, nullptr);
        return g_direct.faulted ? Ngx::kFail : g_direct.evaluate(cmd, feature, q, nullptr);
    }

    void ReleaseFeature(Ngx::Handle*& feature)
    {
        if (!feature) return;
        std::lock_guard lock(g_ngxMx);
        if (!g_direct.started) g_core.release(feature);
        else if (!g_direct.faulted) g_direct.release(feature);
        feature = nullptr;
    }

    // ---- stills ------------------------------------------------------------------------------------------------

    // What NR keeps for one still size: its feature and the images it reads and writes.
    struct Sized
    {
        std::uint32_t     w = 0, h = 0;
        Ngx::Handle* feature = nullptr;
        Img               color, output, depth, motion;
        Buf               upload, download;

        void Release()
        {
            if (g_fence) vkWaitForFences(g_dev, 1, &g_fence, VK_TRUE, UINT64_MAX);
            ReleaseFeature(feature);
            color.Release();
            output.Release();
            depth.Release();
            motion.Release();
            upload.Release();
            download.Release();
            w = h = 0;
        }
    } g_sized;

    std::uint16_t ToHalf(float f)
    {
        std::uint32_t x;
        std::memcpy(&x, &f, 4);
        const std::uint32_t sign = (x >> 16) & 0x8000;
        const int           e    = int((x >> 23) & 0xff) - 127 + 15;
        const std::uint32_t m    = x & 0x7fffff;
        if (e <= 0) return std::uint16_t(sign);
        if (e >= 31) return std::uint16_t(sign | 0x7c00);
        return std::uint16_t(sign | (std::uint32_t(e) << 10) | (m >> 13));
    }

    float FromHalf(std::uint16_t hv)
    {
        const std::uint32_t sign = std::uint32_t(hv & 0x8000) << 16;
        const std::uint32_t e    = (hv >> 10) & 0x1f;
        const std::uint32_t m    = hv & 0x3ff;
        std::uint32_t       x;
        if (e == 0) x = sign;
        else if (e == 31) x = sign | 0x7f800000 | (m << 13);
        else x = sign | ((e - 15 + 127) << 23) | (m << 13);
        float f;
        std::memcpy(&f, &x, 4);
        return f;
    }

    bool BuildSized(std::uint32_t w, std::uint32_t h, char* error, std::uint32_t cap)
    {
        if (g_sized.feature && g_sized.w == w && g_sized.h == h) return true;
        g_sized.Release();
        if (!MakeImage(g_sized.color, VK_FORMAT_R16G16B16A16_SFLOAT, w, h) ||
            !MakeImage(g_sized.output, VK_FORMAT_R16G16B16A16_SFLOAT, w, h) ||
            !MakeImage(g_sized.depth, VK_FORMAT_R32_SFLOAT, w, h) || !MakeImage(g_sized.motion, VK_FORMAT_R16G16_SFLOAT, w, h) ||
            !MakeBuffer(g_sized.upload, VkDeviceSize(w) * h * 12) || !MakeBuffer(g_sized.download, VkDeviceSize(w) * h * 8)) {
            SetError(error, cap, "could not make %ux%u images", w, h);
            g_sized.Release();
            return false;
        }
        Begin();
        for (Img* im : { &g_sized.color, &g_sized.output, &g_sized.depth, &g_sized.motion }) {
            Barrier(g_cmd, im->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }
        const VkClearColorValue       zero{};
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdClearColorImage(g_cmd, g_sized.motion.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        vkCmdClearColorImage(g_cmd, g_sized.output.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        for (Img* im : { &g_sized.color, &g_sized.output, &g_sized.depth, &g_sized.motion }) {
            Barrier(g_cmd, im->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        if (!Submit()) {
            SetError(error, cap, "the Vulkan queue refused the image setup");
            g_sized.Release();
            return false;
        }
        Begin();
        const Ngx::Result r = CreateFeature(g_cmd, g_params, w, h, &g_sized.feature);
        const bool submitted = Submit();
        if (Ngx::Failed(r) || !g_sized.feature || !submitted) {
            SetError(error, cap, "NR feature for %ux%u: 0x%08X", w, h, unsigned(r));
            g_sized.feature = nullptr;
            g_sized.Release();
            return false;
        }
        g_sized.w = w;
        g_sized.h = h;
        Log("NR feature made for a %ux%u still", w, h);
        return true;
    }

    // ---- live view ---------------------------------------------------------------------------------------------

    // A live view's frames: the game's textures and fence as Vulkan objects, a motion image of zeros, the feature and
    // a ring of command buffers, so a frame never waits for one the GPU is still running.
    constexpr int kLiveRing = 3;
    struct Live
    {
        bool                 ready = false;
        std::uint32_t        w = 0, h = 0;
        Img                  color, depth, output, motion;
        VkSemaphore          fence = VK_NULL_HANDLE;   // the D3D11 fence (timeline)
        VkCommandPool        pool  = VK_NULL_HANDLE;
        VkCommandBuffer      cmd[kLiveRing]{};
        VkFence              done[kLiveRing]{};
        int                  next = 0;
        Ngx::Parameter* params  = nullptr;
        Ngx::Handle*    feature = nullptr;
        Ngx::Resource res[4]{};   // colour, output, depth, motion
    } g_live;
    std::mutex g_liveMx;   // LiveEvaluate (render thread) against LiveAttach / LiveDetach (worker)

    void ReleaseLive()
    {
        if (g_dev) {
            std::lock_guard lock(g_queueMx);
            vkQueueWaitIdle(g_queue);
        }
        ReleaseFeature(g_live.feature);
        if (g_live.params) {
            std::lock_guard lock(g_ngxMx);
            g_core.destroyParameters(g_live.params);
        }
        g_live.params = nullptr;
        for (int i = 0; i < kLiveRing; ++i) {
            if (g_live.done[i]) vkDestroyFence(g_dev, g_live.done[i], nullptr);
        }
        if (g_live.pool) vkDestroyCommandPool(g_dev, g_live.pool, nullptr);
        if (g_live.fence) vkDestroySemaphore(g_dev, g_live.fence, nullptr);
        g_live.color.Release();
        g_live.depth.Release();
        g_live.output.Release();
        g_live.motion.Release();
        g_live = Live{};
    }

    void Teardown()
    {
        {
            std::lock_guard lock(g_liveMx);
            if (g_dev) ReleaseLive();
        }
        g_sized.Release();
        if (g_params) g_core.destroyParameters(g_params);
        g_params = nullptr;
        StopDirect();
        if (g_ngx) {
            std::uint32_t written = 0;   // see Ngx::CoreShutdownFn
            g_core.shutdown(g_dev, &written);
        }
        g_ngx = false;
        if (g_fence) vkDestroyFence(g_dev, g_fence, nullptr);
        g_fence = VK_NULL_HANDLE;
        if (g_pool) vkDestroyCommandPool(g_dev, g_pool, nullptr);
        g_pool = VK_NULL_HANDLE;
        g_cmd  = VK_NULL_HANDLE;
        if (g_dev) vkDestroyDevice(g_dev, nullptr);
        g_dev = VK_NULL_HANDLE;
        if (g_inst) vkDestroyInstance(g_inst, nullptr);
        g_inst = VK_NULL_HANDLE;
        g_pd   = VK_NULL_HANDLE;
    }
}

extern "C" int PhantasmNR_Start(const wchar_t* libraryFile, const wchar_t* dataFolder,
                                                      std::uint32_t luidLow, std::int32_t luidHigh, std::uint32_t flags,
                                                      PhantasmNR_LogFn log, std::uint32_t* how, char* error,
                                                      std::uint32_t errorCap)
{
    g_log = log;
    if (g_ngx) {
        if (how) *how = !g_direct.started ? PhantasmNR_HowLoader : g_direct.nvidias ? PhantasmNR_HowDirectSigned : PhantasmNR_HowDirect;
        return 1;
    }
    if (!libraryFile || !*libraryFile || !dataFolder) {
        SetError(error, errorCap, "no NR library");
        return 0;
    }
    const std::wstring file(libraryFile);
    const auto         slash = file.find_last_of(L"\\/");
    // NVIDIA's loader finds a library by its own name only. A copy under another name is loaded directly, and the
    // loader is pointed at a folder that holds none, so it cannot take a different copy from beside it.
    const bool standard = _wcsicmp(file.c_str() + (slash == std::wstring::npos ? 0 : slash + 1), L"nvngx_dlssnr.dll") == 0;
    g_featureFolder     = !standard ? std::wstring(dataFolder) : slash == std::wstring::npos ? std::wstring(L".") : file.substr(0, slash);
    g_paths[0]          = g_featureFolder.c_str();

    // Until Start returns: the instance and the device are made, and NGX started, with no layer of another program's.
    const bool      noLayers = (flags & PhantasmNR_FlagNoLayers) != 0;
    const LayersOut layersOut(noLayers);

    if (!g_vulkan) g_vulkan = ::LoadLibraryW(L"vulkan-1.dll");
    if (!g_vulkan) {
        SetError(error, errorCap, "vulkan-1.dll is not installed");
        return 0;
    }
    gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(::GetProcAddress(g_vulkan, "vkGetInstanceProcAddr"));
    if (!gipa) {
        SetError(error, errorCap, "vulkan-1.dll has no vkGetInstanceProcAddr");
        return 0;
    }
    if (noLayers) {
        // A loader that does not read the switch would load every layer here: it is asked its version first.
        const auto    version = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gipa(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
        std::uint32_t v       = 0;
        if (!version || version(&v) != VK_SUCCESS || !Phantasm::NeuralLayerCore::KeepsLayersOut(v)) {
            SetError(error, errorCap,
                     "this PC's Vulkan loader (vulkan-1.dll %u.%u.%u) cannot leave other programs' Vulkan layers out of "
                     "the game; 1.3.234 and later can (a current graphics driver installs one)",
                     VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
            return 0;
        }
    }
#define PNR_GI(name) name = reinterpret_cast<PFN_##name>(gipa(g_inst, #name));
    PNR_GI(vkCreateInstance)

    if (!LoadCore(error, errorCap)) return 0;

    Ngx::CommonInfo fci{};
    fci.paths.paths          = g_paths;
    fci.paths.count          = 1;
    fci.logging.callback     = &NgxLog;
    fci.logging.level        = Ngx::kLogOn;
    fci.logging.onlyCallback = true;

    Ngx::Discovery di{};
    di.version                             = Ngx::kInterfaceVersion;
    di.feature                             = Ngx::kNeuralRendering;
    di.identifier.type                     = Ngx::kNamedByProject;
    di.identifier.by.project.id            = kProjectId;
    di.identifier.by.project.engine        = Ngx::kCustomEngine;
    di.identifier.by.project.engineVersion = "1.0";
    di.dataFolder                          = dataFolder;
    di.info                                = &fci;

    std::uint32_t          nInst    = 0;
    VkExtensionProperties* instExts = nullptr;
    g_core.instanceExtensions(&di, &nInst, &instExts);
    std::vector<const char*> iexts;
    for (std::uint32_t i = 0; i < nInst; ++i) iexts.push_back(instExts[i].extensionName);

    VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    app.pApplicationName = "PhantasmNR";
    app.apiVersion       = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo        = &app;
    ici.enabledExtensionCount   = std::uint32_t(iexts.size());
    ici.ppEnabledExtensionNames = iexts.data();
    if (vkCreateInstance(&ici, nullptr, &g_inst) != VK_SUCCESS) {
        SetError(error, errorCap, "vkCreateInstance failed");
        return 0;
    }
    PNR_GI(vkDestroyInstance) PNR_GI(vkEnumeratePhysicalDevices) PNR_GI(vkGetPhysicalDeviceProperties)
    PNR_GI(vkGetPhysicalDeviceProperties2) PNR_GI(vkGetPhysicalDeviceQueueFamilyProperties)
    PNR_GI(vkGetPhysicalDeviceMemoryProperties) PNR_GI(vkEnumerateDeviceExtensionProperties) PNR_GI(vkCreateDevice)
    PNR_GI(vkGetPhysicalDeviceFeatures2)
#undef PNR_GI
    gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(g_inst, "vkGetDeviceProcAddr"));

    // The game's GPU, by its adapter LUID: a live view's textures can only be shared on the GPU that made them.
    std::uint32_t n = 0;
    vkEnumeratePhysicalDevices(g_inst, &n, nullptr);
    std::vector<VkPhysicalDevice> pds(n);
    vkEnumeratePhysicalDevices(g_inst, &n, pds.data());
    const bool wantLuid = luidLow != 0 || luidHigh != 0;
    for (auto pd : pds) {
        VkPhysicalDeviceIDProperties ids{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
        VkPhysicalDeviceProperties2  pp{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        pp.pNext = &ids;
        vkGetPhysicalDeviceProperties2(pd, &pp);
        if (pp.properties.vendorID != 0x10DE) continue;
        if (wantLuid) {
            LUID l{};
            std::memcpy(&l, ids.deviceLUID, sizeof(l));
            if (!ids.deviceLUIDValid || l.LowPart != luidLow || l.HighPart != luidHigh) continue;
        }
        g_pd = pd;
        Log("NR on %s", pp.properties.deviceName);
        break;
    }
    if (!g_pd) {
        SetError(error, errorCap, wantLuid ? "the game's GPU is not an NVIDIA GPU Vulkan can see" : "no NVIDIA GPU");
        Teardown();
        return 0;
    }
    vkGetPhysicalDeviceMemoryProperties(g_pd, &g_mem);

    std::uint32_t          nDev    = 0;
    VkExtensionProperties* devExts = nullptr;
    const Ngx::Result        listed = g_core.deviceExtensions(g_inst, g_pd, &di, &nDev, &devExts);
    std::vector<std::string> want;
    if (!Ngx::Failed(listed) && devExts) {
        for (std::uint32_t i = 0; i < nDev; ++i) want.emplace_back(devExts[i].extensionName);
    }
    if (want.empty()) {
        // The core lists a library's extensions only when it will run that library itself. For one run directly
        // (unsigned, or under another file name) these are what an NR library asks of its device: the core's own
        // answer for the builds it does take. One the device lacks is left out below.
        for (const char* e : { "VK_NVX_binary_import", "VK_NVX_image_view_handle", "VK_KHR_buffer_device_address", "VK_KHR_push_descriptor" }) {
            want.emplace_back(e);
        }
    }
    // A live view's sharing with D3D11; stills need neither.
    for (const char* e : { "VK_KHR_external_memory_win32", "VK_KHR_external_semaphore_win32" }) want.emplace_back(e);
    std::uint32_t nAvail = 0;
    vkEnumerateDeviceExtensionProperties(g_pd, nullptr, &nAvail, nullptr);
    std::vector<VkExtensionProperties> avail(nAvail);
    vkEnumerateDeviceExtensionProperties(g_pd, nullptr, &nAvail, avail.data());
    std::vector<const char*> dexts;
    bool                     extBda = false;
    int                      interop = 0;
    for (const auto& w : want) {
        const auto it = std::find_if(avail.begin(), avail.end(), [&](const VkExtensionProperties& p) { return w == p.extensionName; });
        if (it == avail.end()) {
            Log("NR wants the Vulkan extension %s, which this driver lacks", w.c_str());
            continue;
        }
        dexts.push_back(it->extensionName);
        extBda |= w == "VK_EXT_buffer_device_address";
        interop += w == "VK_KHR_external_memory_win32" || w == "VK_KHR_external_semaphore_win32";
    }
    g_interop = interop == 2;

    std::uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g_pd, &nq, nullptr);
    std::vector<VkQueueFamilyProperties> qf(nq);
    vkGetPhysicalDeviceQueueFamilyProperties(g_pd, &nq, qf.data());
    std::uint32_t family = UINT32_MAX;
    for (std::uint32_t i = 0; i < nq && family == UINT32_MAX; ++i) {
        const VkQueueFlags both = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((qf[i].queueFlags & both) == both) family = i;
    }
    if (family == UINT32_MAX) {
        SetError(error, errorCap, "no graphics queue");
        Teardown();
        return 0;
    }
    g_family = family;

    // Every feature the device has: NR's needs (timeline semaphores, descriptor indexing, device addresses) among them.
    VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceVulkan12Features f12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan11Features f11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceFeatures2        f2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    f2.pNext  = &f11;
    f11.pNext = &f12;
    f12.pNext = &f13;
    vkGetPhysicalDeviceFeatures2(g_pd, &f2);
    VkPhysicalDeviceBufferDeviceAddressFeaturesEXT fbda{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_EXT };
    if (extBda) {
        // The EXT extension and the core feature cannot both be on.
        f12.bufferDeviceAddress              = VK_FALSE;
        f12.bufferDeviceAddressCaptureReplay = VK_FALSE;
        f12.bufferDeviceAddressMultiDevice   = VK_FALSE;
        fbda.bufferDeviceAddress             = VK_TRUE;
        f13.pNext                            = &fbda;
    }
    const float             priority = 1.0f;
    VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
    qci.queueFamilyIndex = family;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.pNext                   = &f2;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = std::uint32_t(dexts.size());
    dci.ppEnabledExtensionNames = dexts.data();
    if (const VkResult vr = vkCreateDevice(g_pd, &dci, nullptr, &g_dev); vr != VK_SUCCESS) {
        SetError(error, errorCap, "vkCreateDevice failed (%d)", int(vr));
        g_dev = VK_NULL_HANDLE;
        Teardown();
        return 0;
    }
#define PNR_GD(name) name = reinterpret_cast<PFN_##name>(gdpa(g_dev, #name));
    PNR_GD(vkDestroyDevice) PNR_GD(vkGetDeviceQueue) PNR_GD(vkCreateCommandPool) PNR_GD(vkDestroyCommandPool)
    PNR_GD(vkAllocateCommandBuffers) PNR_GD(vkBeginCommandBuffer) PNR_GD(vkEndCommandBuffer) PNR_GD(vkResetCommandBuffer)
    PNR_GD(vkQueueSubmit) PNR_GD(vkQueueWaitIdle) PNR_GD(vkDeviceWaitIdle) PNR_GD(vkCreateImage) PNR_GD(vkDestroyImage)
    PNR_GD(vkGetImageMemoryRequirements) PNR_GD(vkAllocateMemory) PNR_GD(vkFreeMemory) PNR_GD(vkBindImageMemory)
    PNR_GD(vkCreateImageView) PNR_GD(vkDestroyImageView) PNR_GD(vkCreateBuffer) PNR_GD(vkDestroyBuffer)
    PNR_GD(vkGetBufferMemoryRequirements) PNR_GD(vkBindBufferMemory) PNR_GD(vkMapMemory) PNR_GD(vkCmdPipelineBarrier)
    PNR_GD(vkCmdCopyBufferToImage) PNR_GD(vkCmdCopyImageToBuffer) PNR_GD(vkCmdClearColorImage) PNR_GD(vkCreateFence)
    PNR_GD(vkDestroyFence) PNR_GD(vkWaitForFences) PNR_GD(vkResetFences) PNR_GD(vkCreateSemaphore)
    PNR_GD(vkDestroySemaphore) PNR_GD(vkGetMemoryWin32HandlePropertiesKHR) PNR_GD(vkImportSemaphoreWin32HandleKHR)
#undef PNR_GD
    g_interop = g_interop && vkGetMemoryWin32HandlePropertiesKHR && vkImportSemaphoreWin32HandleKHR;
    vkGetDeviceQueue(g_dev, family, 0, &g_queue);
    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = family;
    VkFenceCreateInfo fci2{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci2.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // a still size's release waits on it before any still has run
    if (vkCreateCommandPool(g_dev, &pci, nullptr, &g_pool) != VK_SUCCESS ||
        vkCreateFence(g_dev, &fci2, nullptr, &g_fence) != VK_SUCCESS) {
        SetError(error, errorCap, "could not make the command pool");
        Teardown();
        return 0;
    }
    VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool        = g_pool;
    cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(g_dev, &cai, &g_cmd);

    const Ngx::Result r = g_core.init(kProjectId, Ngx::kCustomEngine, "1.0", dataFolder, g_inst, g_pd, g_dev, gipa, gdpa,
                                      Ngx::kInterfaceVersion, &fci);
    if (Ngx::Failed(r)) {
        SetError(error, errorCap, "NGX did not start (0x%08X)", unsigned(r));
        Teardown();
        return 0;
    }
    g_ngx = true;

    Ngx::Parameter* caps      = nullptr;
    int                  available = 0;
    if (standard) {
        if (Ngx::Failed(g_core.capabilities(&caps)) || !caps ||
            Ngx::Failed(caps->Get(Ngx::Name::kAvailable, &available))) {
            available = 0;
        }
        if (caps) g_core.destroyParameters(caps);
    }
    if (!available) {
        // NVIDIA's loader takes only an NVIDIA-signed library built for this GPU, under its own name.
        const bool there   = ::GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES;
        const bool nvidias = there && IsSigned(file);
        bool       ok      = false;
        if (!there) {
            SetError(error, errorCap, "the library file is not there");
        } else if (standard && nvidias) {
            SetError(error, errorCap, "NVIDIA's loader does not run this signed nvngx_dlssnr.dll here (a copy made for "
                                      "another GPU, or a driver too old for it)");
        } else if (!nvidias && !(flags & PhantasmNR_FlagUnsigned)) {
            SetError(error, errorCap, "the library is unsigned, and unsigned copies are turned off");
        } else {
            ok               = StartDirect(file, dataFolder, error, errorCap);
            g_direct.nvidias = ok && nvidias;
        }
        if (!ok) {
            Teardown();
            return 0;
        }
    }
    if (how) *how = !g_direct.started ? PhantasmNR_HowLoader : g_direct.nvidias ? PhantasmNR_HowDirectSigned : PhantasmNR_HowDirect;
    g_core.allocateParameters(&g_params);
    if (!g_params) {
        SetError(error, errorCap, "NGX gave no parameter block");
        Teardown();
        return 0;
    }
    if (!g_interop) Log("this driver cannot share textures with D3D11: live views stay as drawn");
    return 1;
}

extern "C" int PhantasmNR_Process(const std::uint8_t* rgbaIn, const float* depth, std::uint32_t width,
                                                        std::uint32_t height, const PhantasmNR_Settings* settings,
                                                        std::uint8_t* rgbaOut, char* error, std::uint32_t errorCap)
{
    if (!g_ngx || !g_params) {
        SetError(error, errorCap, "NR is not started");
        return 0;
    }
    if (!rgbaIn || !rgbaOut || !settings || settings->size < sizeof(PhantasmNR_Settings) || !width || !height) {
        SetError(error, errorCap, "bad arguments");
        return 0;
    }
    if (!BuildSized(width, height, error, errorCap)) return 0;

    const std::size_t n      = std::size_t(width) * height;
    auto*             colour = static_cast<std::uint16_t*>(g_sized.upload.map);
    for (std::size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) colour[i * 4 + c] = ToHalf(rgbaIn[i * 4 + c] / 255.0f);
        colour[i * 4 + 3] = ToHalf(1.0f);
    }
    auto* depthUp = reinterpret_cast<float*>(static_cast<std::uint8_t*>(g_sized.upload.map) + n * 8);
    for (std::size_t i = 0; i < n; ++i) depthUp[i] = depth ? depth[i] : 1.0f;

    Begin();
    Barrier(g_cmd, g_sized.color.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    Barrier(g_cmd, g_sized.depth.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy bc{};
    bc.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    bc.imageExtent      = { width, height, 1 };
    vkCmdCopyBufferToImage(g_cmd, g_sized.upload.buf, g_sized.color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bc);
    bc.bufferOffset = n * 8;
    vkCmdCopyBufferToImage(g_cmd, g_sized.upload.buf, g_sized.depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bc);
    Barrier(g_cmd, g_sized.color.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    Barrier(g_cmd, g_sized.depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    if (!Submit()) {
        SetError(error, errorCap, "the Vulkan queue refused the upload");
        return 0;
    }

    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    Ngx::Resource cRes = Ngx::ImageResource(g_sized.color.view, g_sized.color.image, range,
                                                                         g_sized.color.format, width, height, false);
    Ngx::Resource oRes = Ngx::ImageResource(g_sized.output.view, g_sized.output.image, range,
                                                                         g_sized.output.format, width, height, true);
    Ngx::Resource dRes = Ngx::ImageResource(g_sized.depth.view, g_sized.depth.image, range,
                                                                         g_sized.depth.format, width, height, false);
    Ngx::Resource mRes = Ngx::ImageResource(g_sized.motion.view, g_sized.motion.image, range,
                                                                         g_sized.motion.format, width, height, false);
    const int passes = std::clamp(settings->passes, 1, 8);
    for (int p = 0; p < passes; ++p) {
        Ngx::Parameter* q = g_params;
        q->Set(Ngx::Name::kColor, static_cast<void*>(&cRes));
        q->Set(Ngx::Name::kOutput, static_cast<void*>(&oRes));
        q->Set(Ngx::Name::kDepth, static_cast<void*>(&dRes));
        q->Set(Ngx::Name::kMotion, static_cast<void*>(&mRes));
        SetSubrects(q, width, height);
        // A still: the same frame, no motion; the first pass starts a fresh history.
        SetEffect(q, *settings, p == 0);
        Begin();
        const Ngx::Result r = Evaluate(g_cmd, g_sized.feature, q);
        if (!Submit() || Ngx::Failed(r)) {
            SetError(error, errorCap, "NR evaluation failed (0x%08X)", unsigned(r));
            return 0;
        }
    }

    Begin();
    Barrier(g_cmd, g_sized.output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy dc{};
    dc.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    dc.imageExtent      = { width, height, 1 };
    vkCmdCopyImageToBuffer(g_cmd, g_sized.output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_sized.download.buf, 1, &dc);
    Barrier(g_cmd, g_sized.output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    if (!Submit()) {
        SetError(error, errorCap, "the Vulkan queue refused the read-back");
        return 0;
    }
    const auto* px = static_cast<const std::uint16_t*>(g_sized.download.map);
    for (std::size_t i = 0; i < n; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float f      = std::clamp(FromHalf(px[i * 4 + c]), 0.0f, 1.0f);
            rgbaOut[i * 4 + c] = static_cast<std::uint8_t>(f * 255.0f + 0.5f);
        }
        rgbaOut[i * 4 + 3] = rgbaIn[i * 4 + 3];
    }
    return 1;
}

extern "C" void PhantasmNR_Trim()
{
    g_sized.Release();
}

extern "C" int PhantasmNR_LiveAttach(std::uint32_t width, std::uint32_t height, void* colorIn,
                                                           void* depthIn, void* colorOut, void* fence, char* error,
                                                           std::uint32_t errorCap)
{
    std::lock_guard lock(g_liveMx);
    if (g_live.ready) ReleaseLive();
    if (!g_ngx || !g_interop) {
        SetError(error, errorCap, g_ngx ? "this driver cannot share textures with D3D11" : "NR is not started");
        return 0;
    }
    if (!width || !height || !colorIn || !depthIn || !colorOut || !fence) {
        SetError(error, errorCap, "bad arguments");
        return 0;
    }
    const auto fail = [&](const char* what) {
        SetError(error, errorCap, "%s", what);
        ReleaseLive();
        return 0;
    };
    if (!ImportImage(g_live.color, colorIn, VK_FORMAT_R16G16B16A16_SFLOAT, width, height)) return fail("the colour texture would not import");
    if (!ImportImage(g_live.depth, depthIn, VK_FORMAT_R32_SFLOAT, width, height)) return fail("the depth texture would not import");
    if (!ImportImage(g_live.output, colorOut, VK_FORMAT_R16G16B16A16_SFLOAT, width, height)) return fail("the output texture would not import");
    if (!MakeImage(g_live.motion, VK_FORMAT_R16G16_SFLOAT, width, height)) return fail("the motion image was refused");

    VkSemaphoreTypeCreateInfo tci{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    sci.pNext = &tci;
    if (vkCreateSemaphore(g_dev, &sci, nullptr, &g_live.fence) != VK_SUCCESS) return fail("the semaphore was refused");
    VkImportSemaphoreWin32HandleInfoKHR isi{ VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR };
    isi.semaphore  = g_live.fence;
    isi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE_BIT;
    isi.handle     = static_cast<HANDLE>(fence);
    if (vkImportSemaphoreWin32HandleKHR(g_dev, &isi) != VK_SUCCESS) return fail("the D3D11 fence would not import");

    VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = g_family;
    if (vkCreateCommandPool(g_dev, &pci, nullptr, &g_live.pool) != VK_SUCCESS) return fail("the command pool was refused");
    VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    cai.commandPool        = g_live.pool;
    cai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = kLiveRing;
    if (vkAllocateCommandBuffers(g_dev, &cai, g_live.cmd) != VK_SUCCESS) return fail("the command buffers were refused");
    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (auto& f : g_live.done) {
        if (vkCreateFence(g_dev, &fci, nullptr, &f) != VK_SUCCESS) return fail("a fence was refused");
    }
    {
        std::lock_guard ngx(g_ngxMx);
        g_core.allocateParameters(&g_live.params);
    }
    if (!g_live.params) return fail("NGX gave no parameter block");

    // First use: every image to GENERAL (the game has written nothing yet), the motion image zeroed, the feature made.
    VkCommandBuffer cmd = g_live.cmd[0];
    Begin(cmd);
    for (Img* im : { &g_live.color, &g_live.depth, &g_live.output, &g_live.motion }) {
        Barrier(cmd, im->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    }
    const VkClearColorValue       zero{};
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    vkCmdClearColorImage(cmd, g_live.motion.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    const Ngx::Result r = CreateFeature(cmd, g_live.params, width, height, &g_live.feature);
    if (!SubmitAndWait(cmd, g_live.done[0]) || Ngx::Failed(r) || !g_live.feature) {
        g_live.feature = nullptr;
        SetError(error, errorCap, "NR feature for %ux%u: 0x%08X", width, height, unsigned(r));
        ReleaseLive();
        return 0;
    }
    g_live.res[0] = Ngx::ImageResource(g_live.color.view, g_live.color.image, range, g_live.color.format, width, height, false);
    g_live.res[1] = Ngx::ImageResource(g_live.output.view, g_live.output.image, range, g_live.output.format, width, height, true);
    g_live.res[2] = Ngx::ImageResource(g_live.depth.view, g_live.depth.image, range, g_live.depth.format, width, height, false);
    g_live.res[3] = Ngx::ImageResource(g_live.motion.view, g_live.motion.image, range, g_live.motion.format, width, height, false);
    g_live.w     = width;
    g_live.h     = height;
    g_live.next  = 0;
    g_live.ready = true;
    Log("NR live view attached at %ux%u", width, height);
    return 1;
}

extern "C" int PhantasmNR_LiveEvaluate(std::uint64_t waitValue, std::uint64_t signalValue, int reset,
                                                             const PhantasmNR_Settings* settings, char* error,
                                                             std::uint32_t errorCap)
{
    std::lock_guard lock(g_liveMx);
    if (!g_live.ready || !settings || settings->size < sizeof(PhantasmNR_Settings)) {
        SetError(error, errorCap, "no live view attached");
        return 0;
    }
    const int       slot = g_live.next;
    VkFence         done = g_live.done[slot];
    VkCommandBuffer cmd  = g_live.cmd[slot];
    // Three frames back: long finished unless the GPU is far behind, and then waiting is what keeps up.
    if (vkWaitForFences(g_dev, 1, &done, VK_TRUE, 2'000'000'000ull) != VK_SUCCESS) {
        SetError(error, errorCap, "an earlier live frame never finished");
        return 0;
    }
    g_live.next = (slot + 1) % kLiveRing;

    Begin(cmd);
    // The game's textures come from D3D11: acquired from outside the queue family, the output released back to it.
    Barrier(cmd, g_live.color.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL, g_family);
    Barrier(cmd, g_live.depth.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL, g_family);
    Barrier(cmd, g_live.output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, VK_QUEUE_FAMILY_EXTERNAL, g_family);
    Ngx::Parameter* q = g_live.params;
    q->Set(Ngx::Name::kColor, static_cast<void*>(&g_live.res[0]));
    q->Set(Ngx::Name::kOutput, static_cast<void*>(&g_live.res[1]));
    q->Set(Ngx::Name::kDepth, static_cast<void*>(&g_live.res[2]));
    q->Set(Ngx::Name::kMotion, static_cast<void*>(&g_live.res[3]));
    SetSubrects(q, g_live.w, g_live.h);
    SetEffect(q, *settings, reset != 0);
    const Ngx::Result r = Evaluate(cmd, g_live.feature, q);
    Barrier(cmd, g_live.output.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL, g_family, VK_QUEUE_FAMILY_EXTERNAL);
    vkEndCommandBuffer(cmd);
    if (Ngx::Failed(r)) {
        SetError(error, errorCap, "NR evaluation failed (0x%08X)", unsigned(r));
        return 0;   // nothing submitted: the fence is not touched
    }

    VkTimelineSemaphoreSubmitInfo ts{ VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
    ts.waitSemaphoreValueCount   = 1;
    ts.pWaitSemaphoreValues      = &waitValue;
    ts.signalSemaphoreValueCount = 1;
    ts.pSignalSemaphoreValues    = &signalValue;
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo               si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.pNext                = &ts;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &g_live.fence;
    si.pWaitDstStageMask    = &stage;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &g_live.fence;
    vkResetFences(g_dev, 1, &done);
    VkResult vr;
    {
        std::lock_guard q2(g_queueMx);
        vr = vkQueueSubmit(g_queue, 1, &si, done);
    }
    if (vr != VK_SUCCESS) {
        SetError(error, errorCap, "the Vulkan queue refused the frame (%d)", int(vr));
        return 0;
    }
    return 1;
}

extern "C" void PhantasmNR_LiveDetach()
{
    std::lock_guard lock(g_liveMx);
    if (g_dev) ReleaseLive();
}

extern "C" void PhantasmNR_Shutdown()
{
    Teardown();
}
