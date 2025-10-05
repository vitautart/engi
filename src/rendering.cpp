#include <algorithm>
#include <optional>
#include <rendercommon.hpp>
#include <rendering.hpp>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <print>
#include <cassert>
#include <vector>
#include <set>
#include <GLFW/glfw3.h>

#if defined(__linux)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#define GLFW_EXPOSE_NATIVE_X11
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace engi
{
    template<typename T, typename Fn>
    auto find_if(const std::vector<T>& t, Fn fn) -> std::optional<T>
    {
        if (auto it = std::ranges::find_if(t, fn); it != std::end(t)) return *it;
        return std::nullopt;
    }
    template<typename T, typename Fn>
    auto find_if_id(const std::vector<T>& t, Fn fn) -> std::optional<uint32_t>
    {
        if (auto it = std::ranges::find_if(t, fn); it != std::end(t)) return (uint32_t)std::distance(std::begin(t), it);
        return std::nullopt;
    }
}

namespace 
{
#ifdef ENGI_RENDER_DEBUG
constexpr bool IS_DEBUG_RENDERING = true;
#else
constexpr bool IS_DEBUG_RENDERING = false;
#endif

static VKAPI_ATTR VkBool32 VKAPI_CALL 
debug_cb(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, 
        const VkDebugUtilsMessengerCallbackDataEXT* data, void* user_data) noexcept
{
    std::println("[VALIDATION] {}", data->pMessage);
    bool do_assert = severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if (do_assert) assert(false);
    return VK_FALSE;
}

struct RenderingQueue 
{
    uint32_t family = 0;
    uint32_t id = 0;
    VkQueue queue = VK_NULL_HANDLE;
};

struct RenderingWindow
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
};

struct RenderingInstace
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice p_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugger = VK_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkFormat color_format = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR color_space = VK_COLOR_SPACE_MAX_ENUM_KHR;
    RenderingQueue gfx_queue = {};
    RenderingQueue present_queue = {};
    RenderingWindow win = {};
};

static auto get_rendering_instance() -> RenderingInstace&
{
    static RenderingInstace inst;
    return inst;
}
#define ins get_rendering_instance()

auto constexpr static get_layers() noexcept
{
    std::array<const char*, IS_DEBUG_RENDERING ? 1 : 0> data;

    if constexpr (IS_DEBUG_RENDERING)
        data[0] = "VK_LAYER_KHRONOS_validation";
    return data; 
}

auto constexpr static get_instance_extensions() noexcept
{
    return std::array
    {
        "VK_KHR_surface", 
        "VK_KHR_xlib_surface",
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME
    };
}

auto constexpr static get_device_extensions() noexcept
{
    return  std::array
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
}

auto constexpr static get_depth_format() noexcept
{
    return std::array
    {
        VkFormat::VK_FORMAT_D32_SFLOAT,
        VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT,
        VkFormat::VK_FORMAT_D24_UNORM_S8_UINT
    };
}

auto check_present_support(VkPhysicalDevice device, int family_queue_id) -> bool
{
    if constexpr(__linux)
    {
        auto display = glfwGetX11Display();
        auto visual = XDefaultVisual(display, XDefaultScreen(display));
        return vkGetPhysicalDeviceXlibPresentationSupportKHR(device, 
                family_queue_id, display, visual->visualid) == VK_TRUE;
    }
    else // Not supported
    {
        assert(false);
        return false;
    }
}

static auto create_instance() -> bool
{
    const auto layers = get_layers();
    const auto extensions_instance = get_instance_extensions();

    VkApplicationInfo app_info = 
    {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "HomeCAD",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "HomeCAD ENGINE",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_4
    };

    std::println("[INFO] Vulkan API version: {}", app_info.apiVersion);

    VkValidationFeatureEnableEXT enableValidationFeatures[] = 
    {
        VkValidationFeatureEnableEXT::VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
    };
    VkValidationFeaturesEXT validationFeatures = 
    {
        .sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext = nullptr,
        .enabledValidationFeatureCount = sizeof(enableValidationFeatures) / sizeof(enableValidationFeatures[0]),
        .pEnabledValidationFeatures = enableValidationFeatures,
        .disabledValidationFeatureCount = 0,
        .pDisabledValidationFeatures = nullptr
    };
    VkInstanceCreateInfo info = 
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        // TO REMOVE SYNCHRONIZATION DEBUG, COMMENT THIS OUT
        //.pNext = &validationFeatures,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = (uint32_t)(layers.size()),
        .ppEnabledLayerNames = layers.empty() ? nullptr : layers.data(),
        .enabledExtensionCount = (uint32_t)extensions_instance.size(),
        .ppEnabledExtensionNames = extensions_instance.data()
    };

    auto result = vkCreateInstance(&info, nullptr, &ins.instance);
    VK_CHECK_RETURN(result, "[ERROR] Instance creation failed: {}");

    return true;
}

static auto create_debugger() -> bool
{
    if constexpr (!IS_DEBUG_RENDERING)
        return true;

    VkDebugUtilsMessengerCreateInfoEXT info = 
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext = nullptr,
        .flags = 0,
        .messageSeverity = 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_cb,
        .pUserData = nullptr
    };
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(ins.instance, "vkCreateDebugUtilsMessengerEXT");
    auto result = fn(ins.instance, &info, nullptr, &ins.debugger);
    VK_CHECK_RETURN(result, "[ERROR] Debugger creation failed: {}");

    std::println("[INFO] Debugger created successfully");
    return true;
}

static auto choose_physical_device() -> bool
{
    std::vector<VkPhysicalDevice> devices;
    engi::vkenum(vkEnumeratePhysicalDevices, devices, ins.instance);

    using DeviceCost = std::pair<VkPhysicalDevice, float>; 
    std::vector<DeviceCost> filtered;
    
    auto get_device_score = [](VkPhysicalDevice iDevice) noexcept -> float
    {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(iDevice, &props);

        float result = 0.0f;

        result += props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1.0f : 0;
        result += props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 0.5f : 0;

        return result;
    };

    auto is_present_supported = [](VkPhysicalDevice iDevice) -> bool
    {
        std::vector<VkQueueFamilyProperties> queue_props;
        engi::vkenum(vkGetPhysicalDeviceQueueFamilyProperties, queue_props, iDevice);

        for (size_t i = 0; i < queue_props.size(); i++)
            if (check_present_support(iDevice, i))
                return true;

        return false;
    };

    for (auto device : devices)
        if (is_present_supported(device))
            filtered.emplace_back(device, get_device_score(device));

    if (filtered.empty())
    {
        std::println("[ERROR] Couldn't find appropriate physical device");
        return false;
    }

    std::sort(filtered.begin(), filtered.end(), [](DeviceCost lhs, DeviceCost rhs) { return lhs.second > rhs.second; });

    auto device = filtered[0].first;

    auto choose_depth_format = [](VkPhysicalDevice device)
    {
        for (const auto& format : get_depth_format())
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(device, format, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            {
                ins.depth_format = format;
                return true;
            }
        }
        return false;
    };

    if (!choose_depth_format(device))
    {
        std::println("[ERROR] Couldn't find needed depth format");
        return false;
    }

    ins.p_device = device;

    return true;
}

static auto choose_queue_families() -> bool
{
    std::vector<VkQueueFamilyProperties> props;
    engi::vkenum(vkGetPhysicalDeviceQueueFamilyProperties, props, ins.p_device);

    auto opt_family = engi::find_if_id(props, [](VkQueueFamilyProperties p) 
    { 
        return p.queueFlags & VK_QUEUE_GRAPHICS_BIT;
    });
    OPT_CHECK_RETURN(opt_family, "[ERROR] Can't find gfx queue");
    ins.gfx_queue = { .family = *opt_family, .id = 0 };

    if (check_present_support(ins.p_device, ins.gfx_queue.family))
    {
        ins.present_queue = { .family = ins.gfx_queue.family, .id = 0 };
        return true;
    }

    auto find_present_family = [&props] -> std::optional<uint32_t>
    {
        for (uint32_t i = 0; i < props.size(); i++)
            if (check_present_support(ins.p_device, i))
                return i;
        return std::nullopt;
    };
    opt_family = find_present_family();
    OPT_CHECK_RETURN(opt_family, "[ERROR] Can't find present queue");
    ins.present_queue = { .family = *opt_family, .id = 0 };

    return true;
}

static auto create_queue_infos(const std::vector<RenderingQueue>& queues) -> std::vector<VkDeviceQueueCreateInfo>
{
    static const float priorities[2] = {1.0f, 1.0f};

    std::vector<VkDeviceQueueCreateInfo> infos;
    std::set<std::pair<uint32_t, uint32_t>> unique_queue; 

    for (auto queue : queues)
        unique_queue.insert({queue.family, queue.id});

    for (auto queue : unique_queue)
    {
        auto family = queue.first;
        auto it = std::find_if(infos.begin(), infos.end(), [family](const VkDeviceQueueCreateInfo& info)
                {return info.queueFamilyIndex == family;});
        if (it != infos.end())
            it->queueCount++;
        else
            infos.push_back({
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr, .flags = 0,
            .queueFamilyIndex = family,
            .queueCount = 1,
            .pQueuePriorities = priorities});
    }
    return infos;
}

static auto create_device() -> bool
{
    if (!choose_queue_families())
        return false;

    auto queue_infos = create_queue_infos({ ins.gfx_queue, ins.present_queue });

    VkPhysicalDeviceSynchronization2Features synchro2 =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = nullptr,
        .synchronization2 = true
    };
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeature = 
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .pNext = &synchro2,
        .dynamicRendering = true
    };
    VkPhysicalDeviceFeatures2 features2 = 
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &dynamicRenderingFeature,
        .features = 
        {
            //.robustBufferAccess;
            //.fullDrawIndexUint32;
            //.imageCubeArray;
            //.independentBlend;
            //.geometryShader;
            //.tessellationShader;
            //.sampleRateShading;
            //.dualSrcBlend;
            //.logicOp;
            .multiDrawIndirect = VK_TRUE,
            //.drawIndirectFirstInstance;
            //.depthClamp;
            //.depthBiasClamp;
            .fillModeNonSolid = VK_TRUE,
            //.depthBounds;
            .wideLines = VK_TRUE,
            //.largePoints;
            //.alphaToOne;
            //.multiViewport;
            .samplerAnisotropy = VK_TRUE,
            //.textureCompressionETC2;
            //.textureCompressionASTC_LDR;
            //.textureCompressionBC;
            //.occlusionQueryPrecise;
            //.pipelineStatisticsQuery;
            .vertexPipelineStoresAndAtomics = VK_TRUE,
            //.fragmentStoresAndAtomics;
            //.shaderTessellationAndGeometryPointSize;
            //.shaderImageGatherExtended;
            //.shaderStorageImageExtendedFormats;
            //.shaderStorageImageMultisample;
            //.shaderStorageImageReadWithoutFormat;
            //.shaderStorageImageWriteWithoutFormat;
            //.shaderUniformBufferArrayDynamicIndexing;
            //.shaderSampledImageArrayDynamicIndexing;
            //.shaderStorageBufferArrayDynamicIndexing;
            //.shaderStorageImageArrayDynamicIndexing;
            //.shaderClipDistance;
            //.shaderCullDistance;
            //.shaderFloat64;
            //.shaderInt64;
            //.shaderInt16;
            //.shaderResourceResidency;
            //.shaderResourceMinLod;
            //.sparseBinding;
            //.sparseResidencyBuffer;
            //.sparseResidencyImage2D;
            //.sparseResidencyImage3D;
            //.sparseResidency2Samples;
            //.sparseResidency4Samples;
            //.sparseResidency8Samples;
            //.sparseResidency16Samples;
            //.sparseResidencyAliased;
            //.variableMultisampleRate;
            //.inheritedQueries;
        }
    };
    auto extensions = get_device_extensions();
    auto layers = get_layers();
    VkDeviceCreateInfo deviceInfo =
    {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features2, 
        .flags = 0,
        .queueCreateInfoCount = (uint32_t)(queue_infos.size()),
        .pQueueCreateInfos = queue_infos.data(),
        .enabledLayerCount = (uint32_t)(layers.size()),
        .ppEnabledLayerNames = layers.empty() ? nullptr : layers.data(),
        .enabledExtensionCount = (uint32_t)extensions.size(),
        .ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data(),
        .pEnabledFeatures =  nullptr//&features
    };

    auto result = vkCreateDevice(ins.p_device, &deviceInfo, nullptr, &ins.device);
    VK_CHECK_RETURN(result, "[ERROR] Device creation failed: {}");

    return true;
}

static auto create_surface(GLFWwindow* glfw_window) -> bool
{
    if constexpr (__linux)
    {
        auto display = glfwGetX11Display();
        auto window =  glfwGetX11Window(glfw_window);
        
        VkXlibSurfaceCreateInfoKHR info = 
        {
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .dpy = display,
            .window = window
        }; 
        auto result = vkCreateXlibSurfaceKHR(ins.instance, &info, nullptr, &ins.win.surface);
        VK_CHECK_RETURN(result, "[ERROR] Surface creation failed: {}");

        return true;
    }
    else
    {
        assert(false);
    }
}

auto create_swapchain(GLFWwindow* window) -> bool
{
    std::vector<VkSurfaceFormatKHR> formats;
    engi::vkenum(vkGetPhysicalDeviceSurfaceFormatsKHR, formats, ins.p_device, ins.win.surface);
    auto opt_format = engi::find_if(formats, [](VkSurfaceFormatKHR f) 
    { 
        return f.format == VK_FORMAT_B8G8R8A8_SRGB && 
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    OPT_CHECK_RETURN(opt_format, "[ERROR] Couldn't find needed surface format");
    ins.color_format = opt_format->format;
    ins.color_space = opt_format->colorSpace;

    std::vector<VkPresentModeKHR> modes;
    engi::vkenum(vkGetPhysicalDeviceSurfacePresentModesKHR, modes, ins.p_device, ins.win.surface);
    auto opt_mode = engi::find_if(modes, [](VkPresentModeKHR m) 
    {
        return m == VK_PRESENT_MODE_MAILBOX_KHR 
            || m == VK_PRESENT_MODE_FIFO_KHR;
    });
    OPT_CHECK_RETURN(opt_mode, "[ERROR] Couldn't find needed present mode");

    VkSurfaceCapabilitiesKHR cap;
    auto result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ins.p_device, ins.win.surface, &cap);
    VK_CHECK_RETURN(result, "[ERROR] Couldn't get data from device: {}");

    auto get_extent = [](const VkSurfaceCapabilitiesKHR& cap, GLFWwindow* window) -> std::optional<VkExtent2D>
    {
        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width == 0 || height == 0)
            return std::nullopt;

        if (cap.currentExtent.width != UINT32_MAX)
            return cap.currentExtent;

        return VkExtent2D
        { 
            .width = std::clamp(uint32_t(width), cap.minImageExtent.width, cap.maxImageExtent.width),
            .height = std::clamp(uint32_t(height), cap.minImageExtent.height, cap.maxImageExtent.height)
        };
    };

    auto opt_extent = get_extent(cap, window);
    OPT_CHECK_RETURN(opt_extent, "[ERROR] Couldn't find max extents");
    ins.win.extent = opt_extent.value();

    bool gfx_present_same = ins.gfx_queue.family == ins.present_queue.family;
    auto indices = std::array{ins.gfx_queue.family, ins.present_queue.family};

    VkSwapchainCreateInfoKHR info = 
    {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = ins.win.surface,
        .minImageCount = std::clamp(cap.minImageCount+1, cap.minImageCount,
            cap.maxImageCount > 0 ? cap.maxImageCount : 256),
        .imageFormat = ins.color_format,
        .imageColorSpace = ins.color_space,
        .imageExtent = ins.win.extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = gfx_present_same ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT,
        .queueFamilyIndexCount = gfx_present_same ? 0u : uint32_t(indices.size()),
        .pQueueFamilyIndices = gfx_present_same ? nullptr : indices.data(),
        .preTransform = cap.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = opt_mode.value(),
        .clipped = VK_TRUE,
        //.oldSwapchain = mSwapchain.swapchain // NOT SURE ABOUT THIS
        .oldSwapchain = VK_NULL_HANDLE
    };

    result = vkCreateSwapchainKHR(ins.device, &info, nullptr, &ins.win.swapchain);
    VK_CHECK_RETURN(result, "[ERROR] Swapchain creation failed: {}");

    return true;
}

auto print_device_info() -> void
{
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(ins.p_device, &props);
    std::println("------------------------------------------------------------");
    std::println("DEVICE: {} \tVERSION: {}", props.deviceName, props.driverVersion);
    std::println("GRAPHIC QUEUE:\tFAMILY: {} \tID: {}", ins.gfx_queue.family, ins.gfx_queue.id);
    std::println("PRESENT QUEUE:\tFAMILY: {} \tID: {}\n", ins.present_queue.family, ins.present_queue.id);
    std::println("------------------------------------------------------------");
}

}

auto engi::Rendering::init(GLFWwindow* window) noexcept -> bool
{
    if (!create_instance()) { destroy(); return false; }
    if (!create_debugger()) { destroy(); return false; }
    if (!choose_physical_device()) { destroy(); return false; }
    if (!create_device()) { destroy(); return false; }

    print_device_info();

    if (!create_surface(window)) { destroy(); return false; }
    if (!create_swapchain(window)) { destroy(); return false; }

    return true;
}

auto engi::Rendering::destroy() noexcept -> void
{
    if (ins.win.swapchain)
    {
        vkDestroySwapchainKHR(ins.device, ins.win.swapchain, nullptr);
        ins.win.swapchain = VK_NULL_HANDLE;
    }

    if (ins.win.surface)
    {
        vkDestroySurfaceKHR(ins.instance, ins.win.surface, nullptr);
        ins.win.surface = VK_NULL_HANDLE;
    }

    if (ins.device)
    {
        vkDestroyDevice(ins.device, nullptr);
        ins.device = VK_NULL_HANDLE;
    }

    if (ins.debugger)
    {
        auto fn =(PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(ins.instance, "vkDestroyDebugUtilsMessengerEXT");
        fn(ins.instance, ins.debugger, nullptr);
        ins.debugger = VK_NULL_HANDLE;
    }

    if (ins.instance)
    {
        vkDestroyInstance(ins.instance, nullptr);
        ins.instance = VK_NULL_HANDLE;
    }
}