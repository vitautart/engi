#pragma once

#include "gomath.hpp"
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

    auto acquire() -> AcquireResult;
    auto cmd_start() -> VkCommandBuffer;
    auto add_barrier(const VkBufferMemoryBarrier2& barrier) -> void;
    auto add_barrier(const VkImageMemoryBarrier2& barrier) -> void;
    auto add_barrier(const VkMemoryBarrier2& barrier) -> void;
    auto add_vertex_buffer_write_barrier(VkBuffer buffer) -> void;
    auto add_index_buffer_write_barrier(VkBuffer buffer) -> void;
    auto delete_later(Buffer&& buffer, uint32_t frame_id) -> void;
    auto cmd_sync_barriers() -> void;
    // TODO: maybe divide view scope on view and rendering, 
    // view can render certain areas, but rendering is just like launching main beginrender stuff
    // need to check if we can set clear collor with commands
    auto view_start(VkCommandBuffer cmd, const VkRect2D& view, go::vf4 srgba_bg) -> void;
    auto view_end(VkCommandBuffer cmd) -> void;
    auto cmd_end() -> void;
    auto submit() -> bool;
    auto wait() noexcept -> void;

    auto destroy() noexcept -> void;

    auto instance() noexcept -> VkInstance;
    auto device() noexcept -> VkDevice;
    auto color_format() noexcept -> VkFormat;
    auto depth_format() noexcept -> VkFormat;
    consteval auto frame_count() noexcept -> uint32_t { return 2; }
}