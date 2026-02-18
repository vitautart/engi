#include "layout.hpp"

#include <rendering.hpp>
#include <utility>
#include <cassert>

namespace engi::vk
{

Layout::Layout(VkPipelineLayout pipeline, std::vector<VkDescriptorSetLayout>&& descLayouts)
    : m_pipeline(pipeline), m_desc_layouts(std::move(descLayouts))
{
}

Layout::~Layout()
{
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(vk::device(), m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }

    for (auto dl : m_desc_layouts)
    {
        if (dl != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(vk::device(), dl, nullptr);
    }
    m_desc_layouts.clear();
}

Layout::Layout(Layout&& other) noexcept : m_pipeline(VK_NULL_HANDLE)
{
    std::swap(m_pipeline, other.m_pipeline);
    m_desc_layouts.swap(other.m_desc_layouts);
}

Layout& Layout::operator=(Layout&& other) noexcept
{
    if (this != &other)
    {
        Layout tmp(std::move(other));
        std::swap(m_pipeline, tmp.m_pipeline);
        m_desc_layouts.swap(tmp.m_desc_layouts);
    }
    return *this;
}

auto LayoutBuilder::set(bool is_push_descriptor) -> LayoutBuilder&
{
    m_descriptor_set_layout_flags.push_back(is_push_descriptor ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT : 0);
    m_sets.emplace_back();
    return *this;
}

auto LayoutBuilder::add(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t descriptorCount) -> LayoutBuilder&
{
    if (m_sets.empty())
        m_sets.emplace_back();

    VkDescriptorSetLayoutBinding b{binding, type, descriptorCount, stageFlags, nullptr};
    m_sets.back().push_back(b);
    return *this;
}

auto LayoutBuilder::push_const(uint32_t offset, uint32_t size, VkShaderStageFlags stageFlags) -> LayoutBuilder&
{
    VkPushConstantRange r{stageFlags, offset, size};
    m_push_constants.push_back(r);
    return *this;
}

auto LayoutBuilder::build() -> std::expected<Layout, VkResult>
{
    assert(m_descriptor_set_layout_flags.size() == m_sets.size());
    std::vector<VkDescriptorSetLayout> created_layouts;
    created_layouts.reserve(m_sets.size());

    size_t flags_index = 0;
    for (const auto& set_bindings : m_sets)
    {
        VkDescriptorSetLayoutCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = m_descriptor_set_layout_flags[flags_index++],
            .bindingCount = static_cast<uint32_t>(set_bindings.size()),
            .pBindings = set_bindings.data()
        };

        VkDescriptorSetLayout desc_layout = VK_NULL_HANDLE;
        VkResult res = vkCreateDescriptorSetLayout(vk::device(), &info, nullptr, &desc_layout);
        if (res != VK_SUCCESS)
        {
            for (auto dl : created_layouts)
                if (dl != VK_NULL_HANDLE)
                    vkDestroyDescriptorSetLayout(vk::device(), dl, nullptr);
            return std::unexpected(res);
        }
        created_layouts.push_back(desc_layout);
    }

    VkPipelineLayoutCreateInfo layout_info{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .setLayoutCount = static_cast<uint32_t>(created_layouts.size()),
        .pSetLayouts = created_layouts.empty() ? nullptr : created_layouts.data(),
        .pushConstantRangeCount = static_cast<uint32_t>(m_push_constants.size()),
        .pPushConstantRanges = m_push_constants.empty() ? nullptr : m_push_constants.data()
    };

    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkResult res = vkCreatePipelineLayout(vk::device(), &layout_info, nullptr, &pipeline_layout);
    if (res != VK_SUCCESS)
    {
        for (auto dl : created_layouts)
            if (dl != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(vk::device(), dl, nullptr);
        return std::unexpected(res);
    }

    return Layout(pipeline_layout, std::move(created_layouts));
}

} // namespace engi::vk
