#include <cassert>

#include <uniform_buffer.hpp>

namespace engi::vk
{
    auto UniformBuffer::create(uint32_t binding, VkShaderStageFlags stageFlags, VkDeviceSize size) -> std::expected<UniformBuffer, VkResult>
    {
        UniformBuffer out;
        out.m_binding = binding;
        out.m_stage_flags = stageFlags;
        out.m_size = size;

        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.pNext = nullptr;
        bufferInfo.flags = 0;
        bufferInfo.size = size;
        bufferInfo.queueFamilyIndexCount = 0;
        bufferInfo.pQueueFamilyIndices = nullptr;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        //bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        for (size_t i = 0; i < FRAMES; ++i)
        {
            auto b = Buffer::create_cpu(bufferInfo);
            if (!b)
                return std::unexpected(b.error());

            out.m_buffers[i] = std::move(b.value());
            out.m_buf_infos[i] = VkDescriptorBufferInfo{ .buffer = out.m_buffers[i].buffer(), .offset = 0, .range = out.m_size };
        }

        return out;
    }
    auto UniformBuffer::write(uint32_t frame_index, const void* src, VkDeviceSize src_size) noexcept -> void
    {
        assert(frame_index < FRAMES);
        assert(src_size <= m_size);
        assert(src_size > 0);
        m_buffers[frame_index].write(src, src_size, 0);
        m_buf_infos[frame_index].range = src_size;
    }

    auto UniformBuffer::fill_write_descriptor(uint32_t frame_index, VkWriteDescriptorSet& out_write, VkDescriptorBufferInfo& out_info) const noexcept -> void
    {
        assert(frame_index < FRAMES);
        out_info = m_buf_infos[frame_index];

        out_write = {};
        out_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        out_write.pNext = nullptr;
        out_write.dstSet = VK_NULL_HANDLE; // ignored for push descriptors
        out_write.dstBinding = m_binding;
        out_write.dstArrayElement = 0;
        out_write.descriptorCount = 1;
        out_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        out_write.pBufferInfo = &out_info;
        out_write.pImageInfo = nullptr;
        out_write.pTexelBufferView = nullptr;
    }
}
