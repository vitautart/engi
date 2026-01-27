#include "pipeline.hpp"

#include <rendering.hpp>

#include <array>

namespace engi::vk
{

// Shader configuration
auto Pipeline::shaders(const ShaderSet& shader_set) -> Pipeline&
{
    m_shader_set = shader_set;
    return *this;
}

// Render target configuration
auto Pipeline::color_format(VkFormat format) -> Pipeline&
{
    m_color_format = format;
    return *this;
}

auto Pipeline::depth_format(VkFormat format) -> Pipeline&
{
    m_depth_format = format;
    return *this;
}

auto Pipeline::samples(VkSampleCountFlagBits samples) -> Pipeline&
{
    m_samples = samples;
    return *this;
}

// Input assembly
auto Pipeline::topology(VkPrimitiveTopology topology) -> Pipeline&
{
    m_topology = topology;
    return *this;
}

// Rasterization
auto Pipeline::polygon_mode(VkPolygonMode mode) -> Pipeline&
{
    m_polygon_mode = mode;
    return *this;
}

auto Pipeline::cull_mode(VkCullModeFlags mode) -> Pipeline&
{
    m_cull_mode = mode;
    return *this;
}

auto Pipeline::front_face(VkFrontFace face) -> Pipeline&
{
    m_front_face = face;
    return *this;
}

auto Pipeline::line_width(float width) -> Pipeline&
{
    m_line_width = width;
    return *this;
}

// Depth testing
auto Pipeline::depth_test(bool enable) -> Pipeline&
{
    m_depth_test = enable;
    return *this;
}

auto Pipeline::depth_write(bool enable) -> Pipeline&
{
    m_depth_write = enable;
    return *this;
}

auto Pipeline::depth_compare_op(VkCompareOp op) -> Pipeline&
{
    m_depth_compare_op = op;
    return *this;
}

// Blending
auto Pipeline::blend_attachment(const VkPipelineColorBlendAttachmentState& attachment) -> Pipeline&
{
    m_blend_attachments.push_back(attachment);
    return *this;
}

auto Pipeline::alpha_blending() -> Pipeline&
{
    VkPipelineColorBlendAttachmentState blend
    {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    m_blend_attachments.push_back(blend);
    return *this;
}

auto Pipeline::additive_blending() -> Pipeline&
{
    VkPipelineColorBlendAttachmentState blend
    {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    m_blend_attachments.push_back(blend);
    return *this;
}

auto Pipeline::no_blending() -> Pipeline&
{
    VkPipelineColorBlendAttachmentState blend
    {
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    m_blend_attachments.push_back(blend);
    return *this;
}

// Build method
auto Pipeline::build(VkPipelineLayout layout) -> std::expected<VkPipeline, VkResult>
{
    // Validate required components
    if (!m_shader_set || m_shader_set->vertex_shader == VK_NULL_HANDLE)
    {
        return std::unexpected(VK_ERROR_INITIALIZATION_FAILED);
    }
    
    // If no blend attachments specified, add default one
    if (m_blend_attachments.empty())
    {
        no_blending();
    }
    
    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> shader_stages;
    
    if (m_shader_set->vertex_shader != VK_NULL_HANDLE)
    {
        shader_stages.push_back(VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = m_shader_set->vertex_shader,
            .pName = "main",
            .pSpecializationInfo = nullptr
        });
    }
    
    if (m_shader_set->fragment_shader != VK_NULL_HANDLE)
    {
        shader_stages.push_back(VkPipelineShaderStageCreateInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = m_shader_set->fragment_shader,
            .pName = "main",
            .pSpecializationInfo = nullptr
        });
    }
    
    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertex_input
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(m_shader_set->vertex_bindings.size()),
        .pVertexBindingDescriptions = m_shader_set->vertex_bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(m_shader_set->vertex_attributes.size()),
        .pVertexAttributeDescriptions = m_shader_set->vertex_attributes.data()
    };
    
    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo input_assembly
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .topology = m_topology,
        .primitiveRestartEnable = VK_FALSE
    };
    
    // Viewport state (dynamic)
    VkPipelineViewportStateCreateInfo viewport_state
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr
    };
    
    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterization
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = m_polygon_mode,
        .cullMode = m_cull_mode,
        .frontFace = m_front_face,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = m_line_width
    };
    
    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisample
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .rasterizationSamples = m_samples,
        .sampleShadingEnable = VK_FALSE,
        .minSampleShading = 1.0f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };
    
    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depth_stencil
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .depthTestEnable = m_depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = m_depth_write ? VK_TRUE : VK_FALSE,
        .depthCompareOp = m_depth_compare_op,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f
    };
    
    // Color blend state
    VkPipelineColorBlendStateCreateInfo color_blend
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<uint32_t>(m_blend_attachments.size()),
        .pAttachments = m_blend_attachments.data(),
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
    };
    
    // Dynamic states
    VkDynamicState dynamic_states[] = 
    {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamic_state
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states
    };

    std::array<VkFormat, 1> color_formats = { m_color_format };
    // Rendering info
    VkPipelineRenderingCreateInfo rendering_info
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .pNext = nullptr,
        .viewMask = 0,
        .colorAttachmentCount = static_cast<uint32_t>(color_formats.size()),
        .pColorAttachmentFormats = color_formats.data(),
        .depthAttachmentFormat = m_depth_format,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };
    
    // Create graphics pipeline
    VkGraphicsPipelineCreateInfo pipeline_info
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .flags = 0,
        .stageCount = static_cast<uint32_t>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blend,
        .pDynamicState = &dynamic_state,
        .layout = layout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    
    VkPipeline pipeline;
    VkResult result = vkCreateGraphicsPipelines(vk::device(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline);
    
    if (result != VK_SUCCESS)
    {
        return std::unexpected(result);
    }
    
    return pipeline;
}

} // namespace engi::vk