#pragma once

#include <vulkan/vulkan.h>
#include <expected>
#include <optional>
#include <vector>
#include <vulkan/vulkan_core.h>

namespace engi::vk
{

struct ShaderSet
{
    VkShaderModule vertex_shader = VK_NULL_HANDLE;
    VkShaderModule fragment_shader = VK_NULL_HANDLE;
    std::vector<VkVertexInputBindingDescription> vertex_bindings;
    std::vector<VkVertexInputAttributeDescription> vertex_attributes;
};

class Pipeline
{
public:
    Pipeline() = default;
    ~Pipeline() = default;

    // Builder methods
    auto shaders(const ShaderSet& shader_set) -> Pipeline&;
    
    auto color_format(VkFormat format) -> Pipeline&;
    auto depth_format(VkFormat format) -> Pipeline&;
    auto samples(VkSampleCountFlagBits samples) -> Pipeline&;
    
    auto topology(VkPrimitiveTopology topology) -> Pipeline&;
    auto polygon_mode(VkPolygonMode mode) -> Pipeline&;
    auto cull_mode(VkCullModeFlags mode) -> Pipeline&;
    auto front_face(VkFrontFace face) -> Pipeline&;
    auto line_width(float width) -> Pipeline&;
    
    auto depth_test(bool enable) -> Pipeline&;
    auto depth_write(bool enable) -> Pipeline&;
    auto depth_compare_op(VkCompareOp op) -> Pipeline&;
    
    auto blend_attachment(const VkPipelineColorBlendAttachmentState& attachment) -> Pipeline&;
    auto alpha_blending() -> Pipeline&;
    auto additive_blending() -> Pipeline&;
    auto no_blending() -> Pipeline&;
    
    // Build the pipeline
    auto build(VkPipelineLayout layout) -> std::expected<VkPipeline, VkResult>;

private:
    // Shader set
    std::optional<ShaderSet> m_shader_set;
    
    // Render target formats
    VkFormat m_color_format = VK_FORMAT_UNDEFINED;
    VkFormat m_depth_format = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits m_samples = VK_SAMPLE_COUNT_1_BIT;
    
    // Input assembly
    VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    
    // Rasterization
    VkPolygonMode m_polygon_mode = VK_POLYGON_MODE_FILL;
    VkCullModeFlags m_cull_mode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace m_front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    float m_line_width = 1.0f;
    
    // Depth stencil
    bool m_depth_test = true;
    bool m_depth_write = true;
    VkCompareOp m_depth_compare_op = VK_COMPARE_OP_LESS;
    
    // Color blending
    std::vector<VkPipelineColorBlendAttachmentState> m_blend_attachments;
};

} // namespace engi::vk