#include <cassert>
#include <print>

#include <dynamic_buffer.hpp>

namespace engi::vk
{
    auto DynamicBuffer::create(VkDeviceSize initial_size, VkBufferUsageFlags usage) -> std::expected<DynamicBuffer, VkResult>
    {
        DynamicBuffer out;
        out.m_usage = usage;

        VkBufferCreateInfo gpuInfo = {};
        gpuInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        gpuInfo.size = initial_size;
        gpuInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        gpuInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBufferCreateInfo stagingInfo = {};
        stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingInfo.size = initial_size;
        stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        for (size_t i = 0; i < FRAMES; ++i)
        {
            auto gpu = Buffer::create_gpu(gpuInfo);
            if (!gpu)
            {
                std::println("[ERROR] DynamicBuffer GPU creation failed: {}", (int)gpu.error());
                return std::unexpected(gpu.error());
            }

            auto staging = Buffer::create_cpu(stagingInfo);
            if (!staging)
            {
                std::println("[ERROR] DynamicBuffer staging creation failed: {}", (int)staging.error());
                return std::unexpected(staging.error());
            }

            out.m_gpu_buffers[i] = std::move(gpu.value());
            out.m_staging_buffers[i] = std::move(staging.value());
            out.m_allocated_sizes[i] = initial_size;
            out.m_written_sizes[i] = 0;
        }

        return out;
    }

    auto DynamicBuffer::write_to_gpu(VkCommandBuffer cmd, const void* src, VkDeviceSize src_size) noexcept -> std::expected<void, VkResult>
    {
        assert(src_size > 0);
        assert(cmd != VK_NULL_HANDLE);

        m_current_buffer_id = (m_current_buffer_id + 1) % FRAMES;

        const auto fi = m_current_buffer_id;
        assert(fi < FRAMES);

        if (src_size > m_allocated_sizes[fi])
        {
            const auto new_size = (m_allocated_sizes[fi] * 2 > src_size) ? m_allocated_sizes[fi] * 2 : src_size;

            VkBufferCreateInfo gpuInfo = {};
            gpuInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            gpuInfo.size = new_size;
            gpuInfo.usage = m_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            gpuInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VkBufferCreateInfo stagingInfo = {};
            stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size = new_size;
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            auto new_gpu = Buffer::create_gpu(gpuInfo);
            if (!new_gpu)
                return std::unexpected(new_gpu.error());

            auto new_staging = Buffer::create_cpu(stagingInfo);
            if (!new_staging)
                return std::unexpected(new_staging.error());

            delete_later(std::move(m_gpu_buffers[fi]), fi);
            m_gpu_buffers[fi] = std::move(new_gpu.value());
            m_staging_buffers[fi] = std::move(new_staging.value());
            m_allocated_sizes[fi] = new_size;
        }

        m_staging_buffers[fi].write(src, src_size, 0);
        m_written_sizes[fi] = src_size;

        VkBufferCopy region = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = src_size
        };
        vkCmdCopyBuffer(cmd, m_staging_buffers[fi].buffer(), m_gpu_buffers[fi].buffer(), 1, &region);

        return {};
    }

    auto DynamicBuffer::buffer() const noexcept -> VkBuffer
    {
        return m_gpu_buffers[m_current_buffer_id].buffer();
    }

    auto DynamicBuffer::size() const noexcept -> VkDeviceSize
    {
        return m_written_sizes[m_current_buffer_id];
    }
}
