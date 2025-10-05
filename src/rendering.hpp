#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h> // AMD VULKAN ALLOCATION LIB

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

    auto init(GLFWwindow* window) noexcept -> bool;
    auto destroy() noexcept -> void;
}