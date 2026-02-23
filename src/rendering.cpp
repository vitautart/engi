#include "gomath.hpp"
#include <cstddef>
#include <cstdint>
#define VMA_IMPLEMENTATION // should be before aby includes

#include <algorithm>
#include <deque>
#include <optional>
#include <print>
#include <cassert>
#include <vector>
#include <set>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#if defined(__linux)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#define GLFW_EXPOSE_NATIVE_X11
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <rendercommon.hpp>
#include <rendering.hpp>

#define ENGI_SWAP_V(o1, field, o2) {auto m = o1.field; o1.field = o2.field; o2.field = m;}

namespace RenderingConfig
{
    constexpr VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_4_BIT;
    constexpr size_t frames = engi::vk::frame_count();
}

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
    std::vector<engi::vk::Image> color_images;
    std::vector<engi::vk::Image> resolve_images;
    std::vector<engi::vk::Image> depth_images;
    std::vector<VkSemaphore> image_semaphores;
    std::vector<VkSemaphore> command_semaphores;
    std::vector<VkFence> command_fences;
    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> main_cmd_buffers;
    std::vector<VkCommandBuffer> color_cmd_buffers;
    std::vector<VkCommandBuffer> present_cmd_buffers;
    std::vector<VkCommandBuffer> work_cmd_buffers;
    std::vector<VkBufferMemoryBarrier2> buffer_barriers;
    std::vector<VkImageMemoryBarrier2> image_barriers;
    std::vector<VkMemoryBarrier2> memory_barriers;
    uint32_t image_id = 0;
    uint32_t frame_id = 0;
    std::vector<engi::vk::Buffer> buffers_to_delete[RenderingConfig::frames] = {};
};

struct RenderingInstace
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice p_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
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
        VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    };
}

auto constexpr static get_depth_format() noexcept
{
    return std::array
    {
        VkFormat::VK_FORMAT_D32_SFLOAT,
        VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT
        //VkFormat::VK_FORMAT_D24_UNORM_S8_UINT
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
        .pNext = &validationFeatures,
        //.pNext = nullptr,
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

static auto create_device_and_queues() -> bool
{
    if (!choose_queue_families())
        return false;

    auto queue_infos = create_queue_infos({ ins.gfx_queue, ins.present_queue });

    VkPhysicalDeviceVulkan12Features vulkan12_features =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = nullptr,
        .descriptorIndexing = VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE,
    };

    VkPhysicalDeviceSynchronization2Features synchro2 =
    {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = &vulkan12_features,
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

    vkGetDeviceQueue(ins.device, ins.gfx_queue.family, ins.gfx_queue.id, &ins.gfx_queue.queue);
    vkGetDeviceQueue(ins.device, ins.present_queue.family, ins.present_queue.id, &ins.present_queue.queue);

    return true;
}

static auto create_allocator() -> bool
{
    VmaAllocatorCreateInfo info = 
    {
        .flags = 0,
        .physicalDevice = ins.p_device,
        .device = ins.device,
        /// Preferred size of a single `VkDeviceMemory` block to be allocated from large heaps > 1 GiB. Optional.
        /** Set to 0 to use default, which is currently 256 MiB. */
        .preferredLargeHeapBlockSize = 0,
        .pAllocationCallbacks = nullptr,
        .pDeviceMemoryCallbacks = nullptr,
        //.frameInUseCount = 2,
        .pHeapSizeLimit = nullptr,
        .pVulkanFunctions = nullptr,
        //.pRecordSettings = nullptr,
        .instance = ins.instance,
        .vulkanApiVersion = VK_API_VERSION_1_2
    };

    auto result = vmaCreateAllocator(&info, &ins.allocator);
    VK_CHECK_RETURN(result, "[ERROR] Allocator creation failed: {}");

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

static auto create_swapchain_images() -> bool
{
    using namespace engi::vk;
    std::vector<VkImage> swapchain_images;
    engi::vkenum(vkGetSwapchainImagesKHR, swapchain_images, ins.device, ins.win.swapchain);

    VkImageCreateInfo image_info = 
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VkImageType::VK_IMAGE_TYPE_2D,
        .extent = { ins.win.extent.width, ins.win.extent.height, 1}, 
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = RenderingConfig::samples,
        .tiling = VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
        .sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    VkImageViewCreateInfo view_info = 
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .components = engi::get_std_rgba_comp_mapping(),
        .subresourceRange = 
        {
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    for (auto sw_image : swapchain_images)
    {
        view_info.image = VK_NULL_HANDLE; // will be added inside create method
        view_info.format = ins.color_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
 
        auto image = Image::create(sw_image, view_info);
        RES_CHECK_RETURN(image, "[ERROR] Couldn't create resolve images for swapchanin: {}");
        ins.win.resolve_images.emplace_back(std::move(image.value()));
    }

    for (auto _ : swapchain_images)
    {
        image_info.format = ins.color_format;
        image_info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        view_info.image = VK_NULL_HANDLE; // will be added inside create method
        view_info.format = ins.color_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
       
        auto image = Image::create(image_info, view_info);
        RES_CHECK_RETURN(image, "[ERROR] Couldn't create color images for swapchanin: {}");
        ins.win.color_images.emplace_back(std::move(image.value()));
    }

    for (auto _ : swapchain_images)
    {
        image_info.format = ins.depth_format;
        image_info.usage = VkImageUsageFlagBits::VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        view_info.image = VK_NULL_HANDLE; // will be added inside create method
        view_info.format = ins.depth_format;
        view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        auto image = Image::create(image_info, view_info);
        RES_CHECK_RETURN(image, "[ERROR] Couldn't create depth images for swapchanin: {}");
        ins.win.depth_images.emplace_back(std::move(image.value()));
    }

    return true;
}

static auto create_sync_data() -> bool
{
    VkSemaphoreCreateInfo info_sem = 
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0
    };

    VkFenceCreateInfo info_fen = 
    {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    for (size_t i = 0; i < RenderingConfig::frames; i++)
    {
        VkSemaphore item;
        auto result = vkCreateSemaphore(ins.device, &info_sem, nullptr, &item);
        VK_CHECK_RETURN(result, "[ERROR] Semaphore creation failed: {}");
        ins.win.image_semaphores.push_back(item);
    }

    for (const auto& _ : ins.win.resolve_images)
    {
        VkSemaphore item;
        auto result = vkCreateSemaphore(ins.device, &info_sem, nullptr, &item);
        VK_CHECK_RETURN(result, "[ERROR] Semaphore creation failed: {}");
        ins.win.command_semaphores.push_back(item);
    }

    for (size_t i = 0; i < RenderingConfig::frames; i++)
    {
        VkFence item;
        auto result = vkCreateFence(ins.device, &info_fen, nullptr, &item);
        VK_CHECK_RETURN(result, "[ERROR] Fence creation failed: {}");
        ins.win.command_fences.push_back(item);
    }
    return true;
}

static auto create_cmd_pool() -> bool
{    
    VkCommandPoolCreateInfo poolInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ins.gfx_queue.family
    };
    auto result = vkCreateCommandPool(ins.device, &poolInfo, nullptr, &ins.win.cmd_pool);
    VK_CHECK_RETURN(result, "[ERROR] Command buffer pool creation failed: {}");

    return true;
}

static auto create_main_cmd_buffers() -> bool
{
    VkCommandBufferAllocateInfo info = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = ins.win.cmd_pool,
        .level = VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = RenderingConfig::frames
    };

    std::vector<VkCommandBuffer> commandBuffers; commandBuffers.resize(info.commandBufferCount);
    auto result = vkAllocateCommandBuffers(ins.device, &info, commandBuffers.data());
    VK_CHECK_RETURN(result, "[ERROR] Command buffer allocation failed: {}");
    ins.win.main_cmd_buffers = std::move(commandBuffers);

    return true;
}

static auto record_transit_cmd_buffers() -> void;

static auto create_transit_cmd_buffers() -> bool
{
    VkCommandBufferAllocateInfo info = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = ins.win.cmd_pool,
        .level = VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = uint32_t(ins.win.resolve_images.size())
    };
    {
        std::vector<VkCommandBuffer> commandBuffers; commandBuffers.resize(info.commandBufferCount);
        auto result = vkAllocateCommandBuffers(ins.device, &info, commandBuffers.data());
        VK_CHECK_RETURN(result, "[ERROR] Command buffer allocation failed: {}");
        ins.win.color_cmd_buffers = std::move(commandBuffers);
    }
    {
        std::vector<VkCommandBuffer> commandBuffers; commandBuffers.resize(info.commandBufferCount);
        auto result = vkAllocateCommandBuffers(ins.device, &info, commandBuffers.data());
        VK_CHECK_RETURN(result, "[ERROR] Command buffer allocation failed: {}");
        ins.win.present_cmd_buffers = std::move(commandBuffers);
    }

    record_transit_cmd_buffers();
    return true;
}

static auto system_resource_initialization() -> void
{
    auto cmd = engi::vk::one_time_submit_start();
    
    // Prepare barrier structures
    VkImageSubresourceRange color_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    
    VkImageSubresourceRange depth_range = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    
    VkImageMemoryBarrier2 color_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange = color_range,
    };
    
    VkImageMemoryBarrier2 depth_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | 
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange = depth_range,
    };
    
    // Build barrier array for all MSAA images
    std::vector<VkImageMemoryBarrier2> barriers;
    barriers.reserve(ins.win.color_images.size() * 2); // color + depth for each
    
    for (size_t i = 0; i < ins.win.color_images.size(); i++) {
        // Add color attachment barrier
        color_barrier.image = ins.win.color_images[i].image();
        barriers.push_back(color_barrier);
        
        // Add depth attachment barrier
        depth_barrier.image = ins.win.depth_images[i].image();
        barriers.push_back(depth_barrier);
    }

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
        .pImageMemoryBarriers = barriers.data(),
    };
    
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);

    engi::vk::one_time_submit_end(cmd);
}

// Record these command buffers - they'll be executed EVERY frame
static auto record_transit_cmd_buffers() -> void
{
    VkCommandBufferBeginInfo cbBeginInfo = { 
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 
    };
    
    VkImageSubresourceRange common_range = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    
    VkImageMemoryBarrier2 resolve_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        //.srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, // to avoid sync errors
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange = common_range,
    };
    
    for (size_t i = 0; i < ins.win.color_cmd_buffers.size(); i++) 
    {
        resolve_barrier.image = ins.win.resolve_images[i].image();
        
        auto cb = ins.win.color_cmd_buffers[i];
        vkResetCommandBuffer(cb, 0);
        vkBeginCommandBuffer(cb, &cbBeginInfo);
        
        VkDependencyInfo dependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &resolve_barrier,
        };
        
        vkCmdPipelineBarrier2(cb, &dependencyInfo);
        vkEndCommandBuffer(cb);
    }
    
    // Present transition (unchanged)
    VkImageMemoryBarrier2 present_barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
        .dstAccessMask = VK_ACCESS_2_NONE,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .subresourceRange = common_range,
    };
    
    for (size_t i = 0; i < ins.win.present_cmd_buffers.size(); i++) 
    {
        present_barrier.image = ins.win.resolve_images[i].image();
        
        auto cb = ins.win.present_cmd_buffers[i];
        vkResetCommandBuffer(cb, 0);
        vkBeginCommandBuffer(cb, &cbBeginInfo);
        
        VkDependencyInfo dependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &present_barrier,
        };
        
        vkCmdPipelineBarrier2(cb, &dependencyInfo);
        vkEndCommandBuffer(cb);
    }
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

auto engi::vk::init(GLFWwindow* window) noexcept -> bool
{
    if (!create_instance()) { destroy(); return false; }
    if (!create_debugger()) { destroy(); return false; }
    if (!choose_physical_device()) { destroy(); return false; }
    if (!create_device_and_queues()) { destroy(); return false; }
    if (!create_allocator()) { destroy(); return false; }

    print_device_info();

    if (!create_surface(window)) { destroy(); return false; }
    if (!create_swapchain(window)) { destroy(); return false; }
    if (!create_swapchain_images()) { destroy(); return false; }
    if (!create_sync_data()) { destroy(); return false; }
    if (!create_cmd_pool()) { destroy(); return false; }
    if (!create_main_cmd_buffers()) { destroy(); return false; }
    if (!create_transit_cmd_buffers()) { destroy(); return false; }
    system_resource_initialization();

    return true;
}

auto engi::vk::one_time_submit_start() -> VkCommandBuffer
{
    VkCommandBufferAllocateInfo allocInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = ins.win.cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(ins.device, &allocInfo, &commandBuffer);
    
    // Begin recording
    VkCommandBufferBeginInfo beginInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    
    return commandBuffer;
}

auto engi::vk::one_time_submit_end(VkCommandBuffer cmd) -> void
{
    vkEndCommandBuffer(cmd);
    
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    
    vkQueueSubmit(ins.gfx_queue.queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(ins.gfx_queue.queue);
    vkFreeCommandBuffers(ins.device, ins.win.cmd_pool, 1, &cmd);
}

auto engi::vk::wait_frame() -> uint32_t
{
    const auto cmd_fence = ins.win.command_fences[ins.win.frame_id];
    vkWaitForFences(ins.device, 1, &cmd_fence, VK_TRUE, UINT64_MAX);
    auto result = vkResetFences(ins.device, 1, &cmd_fence);

    // Clean up buffers that were marked for deletion in this frame
    ins.win.buffers_to_delete[ins.win.frame_id].clear();

    // Clear and push dummy cmd for later submit
    // This dummy cmd will be used later after aquire (with aquired image id), 
    // to push actuall color_cmd_buffer for image transition.
    ins.win.work_cmd_buffers.clear();
    ins.win.work_cmd_buffers.push_back(VK_NULL_HANDLE);
    //ins.win.work_cmd_buffers.push_back(ins.win.color_cmd_buffers[ins.win.image_id]);

    return ins.win.frame_id;
}

auto engi::vk::acquire() -> AcquireResult
{
    const auto img_semaphore = ins.win.image_semaphores[ins.win.frame_id];
    
    auto result = vkAcquireNextImageKHR(ins.device, ins.win.swapchain, 
            UINT64_MAX, img_semaphore, nullptr, &ins.win.image_id);

    return 
    {
        .image = ins.win.image_id,
        .result = result,
    };
}

auto engi::vk::current_frame_id() -> uint32_t
{
    return ins.win.frame_id;
}

auto engi::vk::cmd_start() -> VkCommandBuffer
{
    if (ins.win.work_cmd_buffers.empty())
    {
        std::println("[WARNING] Command buffer recording was started without wait_frame being called first");
    }
    auto cmd = ins.win.main_cmd_buffers[ins.win.frame_id];
    VkCommandBufferBeginInfo info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkResetCommandBuffer(cmd, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
    vkBeginCommandBuffer(cmd, &info);

    return cmd;
}

auto engi::vk::draw_start(VkCommandBuffer cmd, go::vf4 srgba_bg) -> void
{   
    draw_start(cmd, { {0, 0}, ins.win.extent }, srgba_bg);
}

auto engi::vk::draw_start(VkCommandBuffer cmd, const VkRect2D& view, go::vf4 srgba_bg) -> void
{   
    VkViewport viewport = 
    {
        .x = (float)view.offset.x,
        .y = (float)view.offset.y,
        .width = (float)view.extent.width,
        .height = (float)view.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    auto lcolor = go::srgba_to_linear(srgba_bg);
    VkRenderingAttachmentInfo colorAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = ins.win.color_images[ins.win.image_id].view(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VkResolveModeFlagBits::VK_RESOLVE_MODE_AVERAGE_BIT,
        .resolveImageView = ins.win.resolve_images[ins.win.image_id].view(), 
        .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = {lcolor[0], lcolor[1], lcolor[2], lcolor[3]} }
    };
    VkRenderingAttachmentInfo depthAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = ins.win.depth_images[ins.win.image_id].view(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VkResolveModeFlagBits::VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE, 
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VkAttachmentLoadOp::VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VkAttachmentStoreOp::VK_ATTACHMENT_STORE_OP_DONT_CARE,
        //.clearValue = { .depthStencil = {.depth = 1.0f, .stencil = 0} } // default-Z
        .clearValue = { .depthStencil = {.depth = 0.0f, .stencil = 0} } // reverse-Z
    };

    VkRenderingInfo renderingInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0, // TODO: maybe here can be something usefull
        .renderArea = view,
        .layerCount = 1, 
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment, 
        .pStencilAttachment = nullptr
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &view);
}

// Generic draw_start: control depth, multisampling and load/store ops.
auto engi::vk::draw_start(
    VkCommandBuffer cmd,
    const VkRect2D& view,
    go::vf4 srgba_bg,
    bool enable_depth,
    bool enable_msaa,
    VkAttachmentLoadOp colorLoadOp,
    VkAttachmentStoreOp colorStoreOp,
    VkAttachmentLoadOp depthLoadOp,
    VkAttachmentStoreOp depthStoreOp) -> void
{
    VkImageView colorView = VK_NULL_HANDLE;
    VkImageView resolveView = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;

    if (enable_msaa)
    {
        colorView = ins.win.color_images[ins.win.image_id].view();
        resolveView = ins.win.resolve_images[ins.win.image_id].view();
    }
    else
    {
        colorView = ins.win.resolve_images[ins.win.image_id].view();
        resolveView = VK_NULL_HANDLE;
    }

    if (enable_depth)
        depthView = ins.win.depth_images[ins.win.image_id].view();
    else
        depthView = VK_NULL_HANDLE;

    draw_start(cmd, view, srgba_bg,
               colorView, depthView, resolveView, enable_msaa,
               colorLoadOp, colorStoreOp, depthLoadOp, depthStoreOp);
}

// Explicit image-view draw_start: use provided image views directly.
auto engi::vk::draw_start(
    VkCommandBuffer cmd,
    const VkRect2D& view,
    go::vf4 srgba_bg,
    VkImageView color_view,
    VkImageView depth_view,
    VkImageView resolve_view,
    bool enable_msaa,
    VkAttachmentLoadOp colorLoadOp,
    VkAttachmentStoreOp colorStoreOp,
    VkAttachmentLoadOp depthLoadOp,
    VkAttachmentStoreOp depthStoreOp) -> void
{
    VkViewport viewport = 
    {
        .x = (float)view.offset.x,
        .y = (float)view.offset.y,
        .width = (float)view.extent.width,
        .height = (float)view.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    auto lcolor = go::srgba_to_linear(srgba_bg);
    VkRenderingAttachmentInfo colorAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = color_view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = (enable_msaa && resolve_view != VK_NULL_HANDLE) ? VkResolveModeFlagBits::VK_RESOLVE_MODE_AVERAGE_BIT : VkResolveModeFlagBits::VK_RESOLVE_MODE_NONE,
        .resolveImageView = (enable_msaa ? resolve_view : VK_NULL_HANDLE), 
        .resolveImageLayout = (enable_msaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED),
        .loadOp = colorLoadOp,
        .storeOp = colorStoreOp,
        .clearValue = { .color = {lcolor[0], lcolor[1], lcolor[2], lcolor[3]} }
    };

    VkRenderingAttachmentInfo depthAttachment = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VkResolveModeFlagBits::VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE, 
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = depthLoadOp,
        .storeOp = depthStoreOp,
        .clearValue = { .depthStencil = {.depth = 0.0f, .stencil = 0} } // reverse-Z
    };

    VkRenderingInfo renderingInfo = 
    {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = view,
        .layerCount = 1, 
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = depth_view != VK_NULL_HANDLE ? &depthAttachment : nullptr, 
        .pStencilAttachment = nullptr
    };

    vkCmdBeginRendering(cmd, &renderingInfo);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &view);
}
    
auto engi::vk::view_set(VkCommandBuffer cmd, const VkRect2D& view) -> void
{
    VkViewport viewport = 
    {
        .x = (float)view.offset.x,
        .y = (float)view.offset.y,
        .width = (float)view.extent.width,
        .height = (float)view.extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &view);
}

auto engi::vk::draw_end(VkCommandBuffer cmd) -> void
{
    vkCmdEndRendering(cmd);
}

auto engi::vk::cmd_end() -> void
{
    auto cmd = ins.win.main_cmd_buffers[ins.win.frame_id];
    vkEndCommandBuffer(cmd);
    ins.win.work_cmd_buffers.push_back(cmd);
}

auto engi::vk::submit() -> bool
{
    if (ins.win.work_cmd_buffers.empty())
    {
        std::println("[WARNING] Submit was called without wait_frame being called first");
        return false; // nothing to submit, skip sync and present
    }
    const auto cmd_semaphore = ins.win.command_semaphores[ins.win.image_id];
    const auto cmd_fence = ins.win.command_fences[ins.win.frame_id];
    const auto img_semaphore = ins.win.image_semaphores[ins.win.frame_id];

    ins.win.work_cmd_buffers[0] = ins.win.color_cmd_buffers[ins.win.image_id];
    ins.win.work_cmd_buffers.push_back(ins.win.present_cmd_buffers[ins.win.image_id]);

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    VkSubmitInfo submit_info =
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &img_semaphore,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = (uint32_t)ins.win.work_cmd_buffers.size(),
        .pCommandBuffers = ins.win.work_cmd_buffers.data(),
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &cmd_semaphore
    };
    auto result = vkQueueSubmit(ins.gfx_queue.queue, 1, &submit_info, cmd_fence);

    VkPresentInfoKHR present_info = 
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &cmd_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &ins.win.swapchain,
        .pImageIndices = &ins.win.image_id
    };
    result = vkQueuePresentKHR(ins.present_queue.queue, &present_info);

    ins.win.frame_id = (ins.win.frame_id + 1) % RenderingConfig::frames;
    ins.win.work_cmd_buffers.clear();

    return (result == VK_SUBOPTIMAL_KHR) || (result == VK_SUCCESS);
}

auto engi::vk::add_barrier(const VkBufferMemoryBarrier2& barrier) -> void
{
    ins.win.buffer_barriers.push_back(barrier);
}

auto engi::vk::add_barrier(const VkImageMemoryBarrier2& barrier) -> void
{
    ins.win.image_barriers.push_back(barrier);
}

auto engi::vk::add_barrier(const VkMemoryBarrier2& barrier) -> void
{
    ins.win.memory_barriers.push_back(barrier);
}

auto engi::vk::add_vertex_buffer_write_barrier(VkBuffer buffer) -> void
{
    VkBufferMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };

    ins.win.buffer_barriers.push_back(barrier);
}

auto engi::vk::add_index_buffer_write_barrier(VkBuffer buffer) -> void
{
    VkBufferMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };

    ins.win.buffer_barriers.push_back(barrier);
}

auto engi::vk::delete_later(Buffer&& buffer, uint32_t frame_id) -> void
{
    assert(frame_id < RenderingConfig::frames);
    ins.win.buffers_to_delete[frame_id].push_back(std::move(buffer));
}

auto engi::vk::cmd_sync_barriers(VkCommandBuffer cmd) -> void
{
    VkDependencyInfo dependency_info = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = (uint32_t)ins.win.memory_barriers.size(),
        .pMemoryBarriers = ins.win.memory_barriers.empty() ? nullptr : ins.win.memory_barriers.data(),
        .bufferMemoryBarrierCount = (uint32_t)ins.win.buffer_barriers.size(),
        .pBufferMemoryBarriers = ins.win.buffer_barriers.empty() ? nullptr : ins.win.buffer_barriers.data(),
        .imageMemoryBarrierCount = (uint32_t)ins.win.image_barriers.size(),
        .pImageMemoryBarriers = ins.win.image_barriers.empty() ? nullptr : ins.win.image_barriers.data()
    };
    
    vkCmdPipelineBarrier2(cmd, &dependency_info);
    
    // Clear barrier storage after sync
    ins.win.buffer_barriers.clear();
    ins.win.image_barriers.clear();
    ins.win.memory_barriers.clear();
}

auto engi::vk::instance() noexcept -> VkInstance
{
    return ins.instance;
}

auto engi::vk::device() noexcept -> VkDevice
{
    return ins.device;
}

auto engi::vk::color_format() noexcept -> VkFormat
{
    return ins.color_format;
}

auto engi::vk::depth_format() noexcept -> VkFormat
{
    return ins.depth_format;
}

auto engi::vk::sample_count() noexcept -> VkSampleCountFlagBits
{
    return RenderingConfig::samples;
}

auto engi::vk::wait() noexcept -> void
{
    if (ins.device)
        vkDeviceWaitIdle(ins.device);
}

auto engi::vk::destroy() noexcept -> void
{
    // Wait for all GPU operations to complete before destroying resources
    if (ins.device)
        vkDeviceWaitIdle(ins.device);

    if (auto& data = ins.win.color_cmd_buffers; !data.empty())
        vkFreeCommandBuffers(ins.device, ins.win.cmd_pool, data.size(), data.data());
    ins.win.color_cmd_buffers.clear();

    if (auto& data = ins.win.present_cmd_buffers; !data.empty())
        vkFreeCommandBuffers(ins.device, ins.win.cmd_pool, data.size(), data.data());
    ins.win.present_cmd_buffers.clear();

    if (auto& data = ins.win.main_cmd_buffers; !data.empty())
        vkFreeCommandBuffers(ins.device, ins.win.cmd_pool, data.size(), data.data());
    ins.win.main_cmd_buffers.clear();
     
    if (ins.win.cmd_pool)
        vkDestroyCommandPool(ins.device, ins.win.cmd_pool, nullptr);

    for (auto semaphore : ins.win.image_semaphores)
        vkDestroySemaphore(ins.device, semaphore, nullptr);
    ins.win.image_semaphores.clear();

    for (auto semaphore : ins.win.command_semaphores)
        vkDestroySemaphore(ins.device, semaphore, nullptr);
    ins.win.command_semaphores.clear();

    for (auto fence : ins.win.command_fences)
        vkDestroyFence(ins.device, fence, nullptr);
    ins.win.command_fences.clear();

    ins.win.color_images.clear();
    ins.win.depth_images.clear();
    ins.win.resolve_images.clear();

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

    if (ins.allocator)
    {
        vmaDestroyAllocator(ins.allocator);
        ins.allocator = VK_NULL_HANDLE;
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

engi::vk::Image::Image(Image&& other) noexcept : Image{}
{
    swap(other);
}

auto engi::vk::Image::operator=(this Image& lhs, Image&& rhs) noexcept -> Image&
{
    lhs.swap(rhs); return lhs;
}


auto engi::vk::Image::swap(this Image& lhs, Image& rhs) noexcept -> void
{
    static_assert(offsetof(Image, m_image) == 0, "Swap needs to be updated");
    static_assert(offsetof(Image, m_view) == 8, "Swap needs to be updated");
    static_assert(offsetof(Image, m_format) == 16, "Swap needs to be updated");
    static_assert(sizeof(Image) == (offsetof(Image, m_memory) + sizeof(VmaAllocation)), "Swap needs to be updated");
    ENGI_SWAP_V(lhs, m_image, rhs);
    ENGI_SWAP_V(lhs, m_view, rhs);
    ENGI_SWAP_V(lhs, m_format, rhs);
    ENGI_SWAP_V(lhs, m_memory, rhs);
}

auto engi::vk::Image::create(const VkImageCreateInfo& image_info, const VkImageViewCreateInfo& view_info) -> std::expected<Image, VkResult>
{
    Image output;
    VmaAllocation allocation;
    VmaAllocationInfo alloc_info;
    VmaAllocationCreateInfo alloc_create_info = { .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT, .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO };

    auto result = vmaCreateImage(ins.allocator, &image_info, &alloc_create_info, &output.m_image, &output.m_memory, &alloc_info);
    if (result != VK_SUCCESS) return std::unexpected(result);

    VkImageViewCreateInfo view_info_copy = view_info;
    view_info_copy.image = output.m_image;
    result = vkCreateImageView(ins.device, &view_info_copy, nullptr, &output.m_view);
    if (result != VK_SUCCESS) return std::unexpected(result);

    output.m_format = image_info.format;

    return output;
}

auto engi::vk::Image::create(VkImage vk_image, const VkImageViewCreateInfo& view_info) -> std::expected<Image, VkResult>
{
    Image output;
    VkImageViewCreateInfo view_info_copy = view_info;
    view_info_copy.image = vk_image;
    
    auto result = vkCreateImageView(ins.device, &view_info_copy, nullptr, &output.m_view);
    if (result != VK_SUCCESS) return std::unexpected(result);

    output.m_format = view_info.format;
    output.m_memory = VK_NULL_HANDLE;
    output.m_image = vk_image;

    return output;
}

engi::vk::Image::~Image()
{
    if (m_view)
        vkDestroyImageView(ins.device, m_view, nullptr);
    if (m_image && m_memory)
        vmaDestroyImage(ins.allocator, m_image, m_memory);
    m_view = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
}

engi::vk::Buffer::Buffer(Buffer&& other) noexcept : Buffer{}
{
    swap(other);
}

auto engi::vk::Buffer::operator=(this Buffer& lhs, Buffer&& rhs) noexcept -> Buffer&
{
    lhs.swap(rhs); return lhs;
}

auto engi::vk::Buffer::swap(this Buffer& lhs, Buffer& rhs) noexcept -> void
{
    static_assert(offsetof(Buffer, m_buffer) == 0, "Swap needs to be updated");
    static_assert(offsetof(Buffer, m_ptr) == 8, "Swap needs to be updated");
    static_assert(offsetof(Buffer, m_size) == 16, "Swap needs to be updated");
    static_assert(sizeof(Buffer) == (offsetof(Buffer, m_memory) + sizeof(VmaAllocation)), "Swap needs to be updated");
    ENGI_SWAP_V(lhs, m_buffer, rhs);
    ENGI_SWAP_V(lhs, m_ptr, rhs);
    ENGI_SWAP_V(lhs, m_size, rhs);
    ENGI_SWAP_V(lhs, m_memory, rhs);
}

auto engi::vk::Buffer::create_cpu(const VkBufferCreateInfo& info) noexcept -> std::expected<Buffer, VkResult>
{
    Buffer out;
    VmaAllocationInfo alloc_info;
    VmaAllocationCreateInfo alloc_create_info = 
    {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
        .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO
    };
    auto result = vmaCreateBuffer(ins.allocator, &info, &alloc_create_info, &out.m_buffer, &out.m_memory, &alloc_info);
    if (result != VK_SUCCESS) return std::unexpected(result);

    out.m_ptr = alloc_info.pMappedData;
    out.m_size = info.size;
    return out;
}

auto engi::vk::Buffer::create_gpu(const VkBufferCreateInfo& info) noexcept -> std::expected<Buffer, VkResult>
{
    Buffer out;
    VmaAllocationInfo alloc_info;
    VmaAllocationCreateInfo alloc_reate_info = 
    {
        .flags = 0,
        .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    };
    auto result = vmaCreateBuffer(ins.allocator, &info, &alloc_reate_info, &out.m_buffer, &out.m_memory, &alloc_info);
    if (result != VK_SUCCESS) return std::unexpected(result);

    out.m_ptr = nullptr;
    out.m_size = info.size;

    return out;
}

// https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
/*auto engi::vk::Buffer::create_gpu_mapped(const VkBufferCreateInfo& info) noexcept -> std::expected<Buffer, VkResult>
{
    Buffer out;
    VmaAllocationInfo alloc_info;
    VmaAllocationCreateInfo alloc_create_info = 
    {
        .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT & 
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT &
            VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
        .usage = VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO
    };
    auto result = vmaCreateBuffer(ins.allocator, &info, &alloc_create_info, &out.m_buffer, &out.m_memory, &alloc_info);
    if (result != VK_SUCCESS) return std::unexpected(result);

    out.m_ptr = alloc_info.pMappedData;
    out.m_size = info.size;

    return out;
}*/

engi::vk::Buffer::~Buffer() noexcept
{
    if (m_buffer && m_memory)
            vmaDestroyBuffer(ins.allocator, m_buffer, m_memory);
}

auto engi::vk::Buffer::write(const void* src, VkDeviceSize src_size, VkDeviceSize dst_offset) noexcept -> void
{
    if (m_ptr != nullptr)
        memcpy((char*)m_ptr + dst_offset, src, src_size);
    else
        std::println("[WARNING] Trying to write to non-mapped buffer!");
}