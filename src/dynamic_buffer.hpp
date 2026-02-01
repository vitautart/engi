#pragma once

#include <array>
#include <expected>
#include <vulkan/vulkan.h>

#include <rendering.hpp>

namespace engi::vk
{
    struct DynamicBuffer
    {
    public:
        DynamicBuffer() = default;
        DynamicBuffer(const DynamicBuffer&) = delete;
        DynamicBuffer(DynamicBuffer&&) noexcept = default;
        auto operator=(const DynamicBuffer&) -> DynamicBuffer& = delete;
        auto operator=(DynamicBuffer&&) noexcept -> DynamicBuffer& = default;
        ~DynamicBuffer() = default;

        static auto create(VkDeviceSize initial_size, VkBufferUsageFlags usage) -> std::expected<DynamicBuffer, VkResult>;

        auto write_to_gpu(VkCommandBuffer cmd, const void* src, VkDeviceSize src_size) noexcept -> std::expected<void, VkResult>;

        auto buffer() const noexcept -> VkBuffer;
        auto size() const noexcept -> VkDeviceSize;

    private:
        static constexpr size_t FRAMES = static_cast<size_t>(frame_count());
        std::array<Buffer, FRAMES> m_gpu_buffers = {};
        std::array<Buffer, FRAMES> m_staging_buffers = {};
        std::array<VkDeviceSize, FRAMES> m_allocated_sizes = {};
        std::array<VkDeviceSize, FRAMES> m_written_sizes = {};
        VkBufferUsageFlags m_usage = 0;
    };
}
