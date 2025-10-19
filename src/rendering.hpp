#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h> // AMD VULKAN ALLOCATION LIB
#include <vulkan/vulkan_core.h>

typedef struct GLFWwindow GLFWwindow;

namespace engi::Rendering 
{
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VmaAllocation memory = VK_NULL_HANDLE;

        auto create(const VkImageCreateInfo& image_info, const VkImageViewCreateInfo& view_info) -> VkResult;
        auto create(VkImage vk_image, const VkImageViewCreateInfo& view_info) -> VkResult;
        auto destroy() -> void;
    };

    struct AcquireResult
    {
        uint32_t id;
        uint32_t image;
        VkResult result;
        VkImageView color_view;
        VkImageView depth_view;
        VkImageView resolve_view;
    };

    auto init(GLFWwindow* window) noexcept -> bool;
    auto draw_start() -> AcquireResult;
    auto cmd_start() -> VkCommandBuffer;
    auto cmd_end() -> void;
    auto draw_end() -> bool;
    auto destroy() noexcept -> void;
    auto instance() noexcept -> VkInstance;
    auto device() noexcept -> VkDevice;
}