#pragma once

#include <vulkan/vulkan.h>
#include <expected>
#include <vector>

namespace engi::vk
{

class LayoutBuilder;

// Move-only RAII wrapper for VkPipelineLayout and its descriptor set layouts
class Layout
{
public:
    Layout() = default;
    explicit Layout(VkPipelineLayout pipeline, std::vector<VkDescriptorSetLayout>&& descLayouts);
    ~Layout();

    Layout(const Layout&) = delete;
    Layout& operator=(const Layout&) = delete;

    Layout(Layout&& other) noexcept;
    Layout& operator=(Layout&& other) noexcept;

    VkPipelineLayout get() const { return m_pipeline; }
    const std::vector<VkDescriptorSetLayout>& descriptor_layouts() const { return m_desc_layouts; }

private:
    VkPipelineLayout m_pipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> m_desc_layouts;
};

class LayoutBuilder
{
public:
    LayoutBuilder() = default;
    ~LayoutBuilder() = default;

    // Start a new descriptor set layout (subsequent add() calls append to the current set)
    auto set() -> LayoutBuilder&;

    // Add a binding to the current descriptor set: binding, type, stageFlags, descriptorCount
    auto add(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t descriptorCount = 1) -> LayoutBuilder&;

    // Add a push constant range
    auto push_const(uint32_t offset, uint32_t size, VkShaderStageFlags stageFlags) -> LayoutBuilder&;

    // Build pipeline layout and descriptor set layouts
    auto build() -> std::expected<Layout, VkResult>;

private:
    std::vector<std::vector<VkDescriptorSetLayoutBinding>> m_sets;
    std::vector<VkPushConstantRange> m_push_constants;
};

} // namespace engi::vk
