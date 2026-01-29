#pragma once

#include <array>
#include <expected>
#include <vulkan/vulkan.h>

#include <rendering.hpp>

namespace engi::vk
{
    struct UniformBuffer
    {
    public:
        UniformBuffer() = default;
        UniformBuffer(const UniformBuffer&) = delete;
        UniformBuffer(UniformBuffer&&) noexcept = default;
        auto operator=(UniformBuffer&&) noexcept -> UniformBuffer& = default;
        ~UniformBuffer() = default;

        // create buffer of given size per-frame; cpu_mapped selects create_cpu vs create_gpu
        static auto create(uint32_t binding, VkShaderStageFlags stageFlags, VkDeviceSize size) -> std::expected<UniformBuffer, VkResult>;

        // write data into per-frame buffer
        auto write(uint32_t frame_index, const void* src, VkDeviceSize src_size) noexcept -> void;

        // prepare VkWriteDescriptorSet + VkDescriptorBufferInfo for push descriptor use
        auto fill_write_descriptor(uint32_t frame_index, VkWriteDescriptorSet& out_write, VkDescriptorBufferInfo& out_info) const noexcept -> void;

        auto descriptor_type() const noexcept -> VkDescriptorType { return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; }
        auto binding() const noexcept -> uint32_t { return m_binding; }

    private:
        static constexpr size_t FRAMES = static_cast<size_t>(frame_count());
        std::array<Buffer, FRAMES> m_buffers = {};
        std::array<VkDescriptorBufferInfo, FRAMES> m_buf_infos = {};

        uint32_t m_binding = 0;
        VkShaderStageFlags m_stage_flags = 0;
        VkDeviceSize m_size = 0;
    };
}
