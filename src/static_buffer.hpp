#pragma once

#include <expected>
#include <vulkan/vulkan.h>

#include <rendering.hpp>

namespace engi::vk
{
    struct StaticBuffer
    {
    public:
        StaticBuffer() = default;
        StaticBuffer(const StaticBuffer&) = delete;
        StaticBuffer(StaticBuffer&&) noexcept = default;
        auto operator=(StaticBuffer&&) noexcept -> StaticBuffer& = default;
        ~StaticBuffer() = default;

        // create gpu buffer of given size
        static auto create(VkDeviceSize size, VkBufferUsageFlags usage) -> std::expected<StaticBuffer, VkResult>;

        // write data into gpu buffer using staging buffer and command buffer
        // returns the staging buffer which MUST be kept alive until GPU transfer completes
        // caller is responsible for managing staging buffer lifetime (typically until frame submission + sync)
        auto write_to_gpu(VkCommandBuffer cmd, const void* src, VkDeviceSize src_size) noexcept -> std::expected<Buffer, VkResult>;

        auto buffer() const noexcept -> const VkBuffer& { return m_buffer.buffer(); }
        auto size() const noexcept -> VkDeviceSize { return m_buffer.size(); }

    private:
        Buffer m_buffer = {};
    };
}
