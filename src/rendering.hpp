#pragma once

#include <expected>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h> // AMD VULKAN ALLOCATION LIB
#include <vulkan/vulkan_core.h>

typedef struct GLFWwindow GLFWwindow;

namespace engi::vk 
{
    struct Image
    {
    public:
        explicit Image() = default;
        Image(const Image&) = delete;
        Image(Image&&) noexcept;
        auto operator=(this Image&, Image&&) noexcept -> Image&;
        ~Image();
        static auto create(const VkImageCreateInfo& image_info, const VkImageViewCreateInfo& view_info) -> std::expected<Image, VkResult>;
        static auto create(VkImage vk_image, const VkImageViewCreateInfo& view_info) -> std::expected<Image, VkResult>;
        auto view() const { return m_view; }
        auto image() const { return m_image; }

        auto swap(this Image&, Image&) noexcept -> void;

    private:
        VkImage m_image = VK_NULL_HANDLE;
        VkImageView m_view = VK_NULL_HANDLE;
        VkFormat m_format = VK_FORMAT_UNDEFINED;
        VmaAllocation m_memory = VK_NULL_HANDLE;
    };

    class Buffer
    {
    public: 
        Buffer() noexcept = default;
        Buffer(const Buffer&) = delete;
        Buffer(Buffer&&) noexcept;
        auto operator=(this Buffer&, Buffer&&) noexcept -> Buffer&;
        ~Buffer() noexcept;
        static auto create_cpu(const VkBufferCreateInfo& info) noexcept -> std::expected<Buffer, VkResult>;
        static auto create_gpu(const VkBufferCreateInfo& info) noexcept -> std::expected<Buffer, VkResult>;
        //static auto create_gpu_mapped(const VkBufferCreateInfo& info) noexcept -> std::expected<Buffer, VkResult>;

        auto buffer() const noexcept -> const VkBuffer& { return m_buffer; };
        auto size() const noexcept -> VkDeviceSize { return m_size; };
        auto write(const void* src, VkDeviceSize src_size, VkDeviceSize dst_offset) noexcept -> void;
        auto data() noexcept -> void* { return m_ptr; }

        auto swap(this Buffer&, Buffer&) noexcept -> void;
    private:
        VkBuffer m_buffer = VK_NULL_HANDLE;
        void* m_ptr = nullptr;
        VkDeviceSize m_size = 0;
        VmaAllocation m_memory = VK_NULL_HANDLE;
    };

    struct AcquireResult
    {
        uint32_t id;
        uint32_t image;
        VkResult result;
    };

    auto init(GLFWwindow* window) noexcept -> bool;

    auto draw_start() -> AcquireResult;
    auto cmd_start() -> VkCommandBuffer;
    auto view_start(VkCommandBuffer cmd, const VkRect2D& view) -> void;
    auto view_end(VkCommandBuffer cmd) -> void;
    auto cmd_end() -> void;
    auto draw_end() -> bool;

    auto destroy() noexcept -> void;

    auto instance() noexcept -> VkInstance;
    auto device() noexcept -> VkDevice;
}