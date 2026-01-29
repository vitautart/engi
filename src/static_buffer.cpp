#include <cassert>

#include <static_buffer.hpp>

namespace engi::vk
{
    auto StaticBuffer::create(VkDeviceSize size, VkBufferUsageFlags usage) -> std::expected<StaticBuffer, VkResult>
    {
        StaticBuffer out;

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = nullptr;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = nullptr;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        bufferInfo.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        auto b = Buffer::create_gpu(bufferInfo);
        if (!b)
            return std::unexpected(b.error());

        out.m_buffer = std::move(b.value());
        return out;
    }

    auto StaticBuffer::write_to_gpu(VkCommandBuffer cmd, const void* src, VkDeviceSize src_size) noexcept -> std::expected<Buffer, VkResult>
    {
        assert(src_size <= m_buffer.size());
        assert(src_size > 0);
        assert(cmd != VK_NULL_HANDLE);

        // Create staging buffer (CPU accessible)
        VkBufferCreateInfo stagingBufferInfo = {};
        stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufferInfo.pNext = nullptr;
        stagingBufferInfo.flags = 0;
        stagingBufferInfo.size = src_size;
        stagingBufferInfo.queueFamilyIndexCount = 0;
        stagingBufferInfo.pQueueFamilyIndices = nullptr;
        stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        auto stagingBuffer = Buffer::create_cpu(stagingBufferInfo);
        if (!stagingBuffer)
            return std::unexpected(stagingBuffer.error());

        // Write data into staging buffer
        stagingBuffer.value().write(src, src_size, 0);

        // Record copy command
        VkBufferCopy region = {};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = src_size;

        vkCmdCopyBuffer(cmd, stagingBuffer.value().buffer(), m_buffer.buffer(), 1, &region);

        // Return staging buffer - caller MUST keep it alive until GPU transfer completes!
        return stagingBuffer;
    }
}
