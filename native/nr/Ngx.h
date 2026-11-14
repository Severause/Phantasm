#pragma once

// The NGX interface PhantasmNRInProc.dll speaks, stated by Phantasm so it builds and runs without NVIDIA's SDK:
// the layouts the graphics driver's NGX core (_nvngx.dll) and an NR feature library (nvngx_dlssnr.dll) exchange with
// their caller, and the shapes of the entry points they export. PhantasmNR.cpp finds both libraries on the player's
// machine and reaches every entry point by its export name. The names of exports and parameters are the libraries'
// own; no code of NVIDIA's is here or in the library built from it.
//
// Every layout is for x64 Windows and is checked by the static_asserts at the end. Include <vulkan/vulkan.h> first.

#include <cstddef>
#include <cstdint>

struct ID3D11Resource;
struct ID3D12Resource;

namespace Ngx
{
    using Result = std::uint32_t;

    /// Every failure carries these top bits; the low ones say which.
    inline constexpr Result kFail = 0xBAD00000u;
    constexpr bool          Failed(Result r) noexcept { return (r & 0xFFF00000u) == kFail; }

    inline constexpr int kInterfaceVersion = 0x15;   // the version of this interface, told to both libraries
    inline constexpr int kNeuralRendering  = 18;     // the NR feature's number
    inline constexpr int kCustomEngine     = 0;      // an application on no engine NGX knows
    inline constexpr int kNamedByProject   = 1;      // Identifier::type: the application gives a project id
    inline constexpr int kLogOn            = 1;      // Logging::level
    inline constexpr int kImageView        = 0;      // Resource::type

    /// A feature the core or a library made.
    struct Handle;

    /// A block of named values, the libraries' own object: read and written through its virtual table only. The
    /// order of the declarations is that table's order and must stay.
    struct Parameter
    {
        virtual void Set(const char* name, unsigned long long value) = 0;
        virtual void Set(const char* name, float value)              = 0;
        virtual void Set(const char* name, double value)             = 0;
        virtual void Set(const char* name, unsigned int value)       = 0;
        virtual void Set(const char* name, int value)                = 0;
        virtual void Set(const char* name, ID3D11Resource* value)    = 0;
        virtual void Set(const char* name, ID3D12Resource* value)    = 0;
        virtual void Set(const char* name, void* value)              = 0;

        virtual Result Get(const char* name, unsigned long long* value) const = 0;
        virtual Result Get(const char* name, float* value) const              = 0;
        virtual Result Get(const char* name, double* value) const             = 0;
        virtual Result Get(const char* name, unsigned int* value) const       = 0;
        virtual Result Get(const char* name, int* value) const                = 0;
        virtual Result Get(const char* name, ID3D11Resource** value) const    = 0;
        virtual Result Get(const char* name, ID3D12Resource** value) const    = 0;
        virtual Result Get(const char* name, void** value) const              = 0;

        virtual void Reset() = 0;
    };

    using LogFn = void(__cdecl*)(const char* message, int level, int feature);

    /// The folders the core looks in for feature libraries, before its own.
    struct PathList
    {
        const wchar_t* const* paths;
        unsigned int          count;
    };

    struct Logging
    {
        LogFn callback;
        int   level;
        bool  onlyCallback;   // the core writes to no log of its own
    };

    struct CommonInfo
    {
        PathList paths;
        void*    reserved;
        Logging  logging;
    };

    struct Project
    {
        const char* id;
        int         engine;
        const char* engineVersion;
    };

    struct Identifier
    {
        int type;
        union
        {
            Project            project;
            unsigned long long application;
        } by;
    };

    /// What the core is asked about a feature before any device exists.
    struct Discovery
    {
        int               version;
        int               feature;
        Identifier        identifier;
        const wchar_t*    dataFolder;
        const CommonInfo* info;
    };

    struct ImageView
    {
        VkImageView             view;
        VkImage                 image;
        VkImageSubresourceRange range;
        VkFormat                format;
        unsigned int            width;
        unsigned int            height;
    };

    struct Buffer
    {
        VkBuffer     buffer;
        unsigned int bytes;
    };

    /// A Vulkan resource as a parameter's value (set as a void*; the block keeps the pointer, not a copy).
    struct Resource
    {
        union
        {
            ImageView image;
            Buffer    buffer;
        } of;
        int  type;
        bool written;
    };

    inline Resource ImageResource(VkImageView view, VkImage image, VkImageSubresourceRange range, VkFormat format,
                                  unsigned int width, unsigned int height, bool written)
    {
        Resource r{};
        r.type            = kImageView;
        r.of.image.view   = view;
        r.of.image.image  = image;
        r.of.image.range  = range;
        r.of.image.format = format;
        r.of.image.width  = width;
        r.of.image.height = height;
        r.written         = written;
        return r;
    }

    // ── entry points, by export name ──

    /// The core: NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements.
    using InstanceExtensionsFn = Result(__cdecl*)(const Discovery*, std::uint32_t* count, VkExtensionProperties** list);
    /// The core: NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements.
    using DeviceExtensionsFn = Result(__cdecl*)(VkInstance, VkPhysicalDevice, const Discovery*, std::uint32_t* count,
                                                VkExtensionProperties** list);
    /// The core: NVSDK_NGX_VULKAN_Init_ProjectID_Ext.
    using CoreInitFn = Result(__cdecl*)(const char* projectId, int engine, const char* engineVersion, const wchar_t* dataFolder,
                                        VkInstance, VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr,
                                        PFN_vkGetDeviceProcAddr, int version, const CommonInfo*);
    /// A feature library: NVSDK_NGX_VULKAN_Init_Ext2.
    using LibraryInitFn = Result(__cdecl*)(unsigned long long application, const wchar_t* dataFolder, VkInstance,
                                           VkPhysicalDevice, VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
                                           int version, const Parameter*);
    /// The core: NVSDK_NGX_VULKAN_AllocateParameters, NVSDK_NGX_VULKAN_GetCapabilityParameters.
    using ParametersFn = Result(__cdecl*)(Parameter**);
    /// The core: NVSDK_NGX_VULKAN_DestroyParameters.
    using DestroyParametersFn = Result(__cdecl*)(Parameter*);
    /// Both: NVSDK_NGX_VULKAN_CreateFeature1.
    using CreateFn = Result(__cdecl*)(VkDevice, VkCommandBuffer, int feature, Parameter*, Handle**);
    /// Both: NVSDK_NGX_VULKAN_EvaluateFeature. `progress` is a callback NR never gives.
    using EvaluateFn = Result(__cdecl*)(VkCommandBuffer, const Handle*, const Parameter*, void* progress);
    /// Both: NVSDK_NGX_VULKAN_ReleaseFeature.
    using ReleaseFn = Result(__cdecl*)(Handle*);
    /// A feature library: NVSDK_NGX_VULKAN_Shutdown1.
    using ShutdownFn = Result(__cdecl*)(VkDevice);
    /// The core: NVSDK_NGX_VULKAN_Shutdown1. The core writes four bytes through `written` (zero each time seen; what
    /// they say is not known) and faults on a null one, so it is always given somewhere to write.
    using CoreShutdownFn = Result(__cdecl*)(VkDevice, std::uint32_t* written);

    // ── parameter names ──
    // A library ignores a name it does not know, so a misspelt one fails silently.
    namespace Name
    {
        inline constexpr const char* kCreationNodeMask   = "CreationNodeMask";
        inline constexpr const char* kVisibilityNodeMask = "VisibilityNodeMask";
        inline constexpr const char* kJitterX            = "Jitter.Offset.X";
        inline constexpr const char* kJitterY            = "Jitter.Offset.Y";
        inline constexpr const char* kAvailable          = "DLSSNR.Available";   // the core's capability block

        inline constexpr const char* kWidth         = "DLSSNR.Width";
        inline constexpr const char* kHeight        = "DLSSNR.Height";
        inline constexpr const char* kRenderPreset  = "DLSSNR.Hint.Render.Preset";
        inline constexpr const char* kEnabled       = "DLSSNR.Enabled";
        inline constexpr const char* kDepthInverted = "DLSSNR.DepthInverted";
        inline constexpr const char* kColor         = "DLSSNR.Color";
        inline constexpr const char* kOutput        = "DLSSNR.Output";
        inline constexpr const char* kDepth         = "DLSSNR.Depth";
        inline constexpr const char* kMotion        = "DLSSNR.MVec";
        inline constexpr const char* kMotionScaleX  = "DLSSNR.MVecScaleX";
        inline constexpr const char* kMotionScaleY  = "DLSSNR.MVecScaleY";
        inline constexpr const char* kReset         = "DLSSNR.Reset";
        inline constexpr const char* kUiCorrection  = "DLSSNR.UICorrection";
        inline constexpr const char* kStyle         = "DLSSNR.Style";
        inline constexpr const char* kIntensity     = "DLSSNR.Intensity";
        inline constexpr const char* kTone          = "DLSSNR.LocalToneStrength";
        inline constexpr const char* kStructure     = "DLSSNR.LocalStructureStrength";
        inline constexpr const char* kSkin          = "DLSSNR.SkinStructureStrength";
        inline constexpr const char* kAutoMask      = "DLSSNR.UseAutoMask";
    }

    static_assert(sizeof(void*) == 8, "the layouts are x64's");
    static_assert(sizeof(PathList) == 16 && sizeof(Logging) == 16);
    static_assert(sizeof(CommonInfo) == 40 && offsetof(CommonInfo, logging) == 24);
    static_assert(sizeof(Identifier) == 32 && offsetof(Identifier, by) == 8);
    static_assert(sizeof(Discovery) == 56 && offsetof(Discovery, dataFolder) == 40 && offsetof(Discovery, info) == 48);
    static_assert(sizeof(ImageView) == 48 && offsetof(ImageView, format) == 36);
    static_assert(sizeof(Resource) == 56 && offsetof(Resource, type) == 48 && offsetof(Resource, written) == 52);
}
