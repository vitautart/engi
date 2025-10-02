#include <rendercommon.hpp>
#include <rendering.hpp>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
#include <print>
#include <cassert>
#include <vector>
#include <GLFW/glfw3.h>

#if defined(__linux)
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#define GLFW_EXPOSE_NATIVE_X11
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

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

struct RenderingInstace
{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice p_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugger = VK_NULL_HANDLE;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
};

static auto iget() -> RenderingInstace&
{
    static RenderingInstace inst;
    return inst;
}

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

    auto result = vkCreateInstance(&info, nullptr, &iget().instance);
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
    auto fn = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(iget().instance, "vkCreateDebugUtilsMessengerEXT");
    auto result = fn(iget().instance, &info, nullptr, &iget().debugger);
    VK_CHECK_RETURN(result, "[ERROR] Debugger creation failed: {}");

    std::println("[INFO] Debugger created successfully");
    return true;
}

static auto choose_physical_device() -> bool
{
    std::vector<VkPhysicalDevice> devices;
    engi::vkenum(vkEnumeratePhysicalDevices, devices, iget().instance);

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
                iget().depth_format = format;
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

    iget().p_device = device;

    return true;
}

static auto choose_queue_families() -> bool
{
    return true;
}

static auto create_device() -> bool
{
    return true;
}

}

auto engi::Rendering::init() noexcept -> bool
{
    if (!create_instance()) { destroy(); return false; }
    if (!create_debugger()) { destroy(); return false; }
    if (!choose_physical_device()) { destroy(); return false; }

    return true;
}

auto engi::Rendering::destroy() noexcept -> void
{
    if (iget().device)
    {
        vkDestroyDevice(iget().device, nullptr);
        iget().device = VK_NULL_HANDLE;
    }

    if (iget().debugger)
    {
        auto fn =(PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(iget().instance, "vkDestroyDebugUtilsMessengerEXT");
        fn(iget().instance, iget().debugger, nullptr);
        iget().debugger = VK_NULL_HANDLE;
    }

    if (iget().instance)
    {
        vkDestroyInstance(iget().instance, nullptr);
        iget().instance = VK_NULL_HANDLE;
    }
}