#include "rendering_overlay.hpp"
#include "gomath.hpp"

#include <cassert>
#include <print>

namespace engi::vk
{
    // ===== TextBuffer =====

    auto TextBuffer::create(const IFontAtlas* atlas) -> std::expected<TextBuffer, VkResult>
    {
        TextBuffer out;
        out.m_atlas = atlas;

        // Start with space for 1024 characters (6 vertices per char)
        constexpr VkDeviceSize initial_vertex_count = 1024 * 6;
        constexpr VkDeviceSize initial_size = initial_vertex_count * sizeof(CharVertex);

        auto db = DynamicBuffer::create(initial_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (!db)
        {
            std::println("[ERROR] TextBuffer DynamicBuffer creation failed: {}", (int)db.error());
            return std::unexpected(db.error());
        }
        out.m_gpu_buffer = std::move(db.value());

        return out;
    }

    auto TextBuffer::clear() noexcept -> void
    {
        m_vertices.clear();
        m_vertex_count = 0;
    }

    auto TextBuffer::add(std::wstring_view text, const go::vf2& pos, const go::vu4& color) -> void
    {
        go::f32 cursor_x = pos[0];
        go::f32 cursor_y = pos[1];

        const auto line_height = static_cast<go::f32>(m_atlas->get_line_height());

        // TODO: srgb to linear conversion?
        const go::u32 coloru32 = go::packUnorm4x8(color);

        for (wchar_t ch : text)
        {
            if (ch == L'\n')
            {
                cursor_x = pos[0];
                cursor_y += line_height;
                continue;
            }

            const Glyph* glyph = m_atlas->get_glyph(static_cast<int>(ch));
            if (!glyph)
                continue;

            const go::f32 x0 = cursor_x + static_cast<go::f32>(glyph->x0);
            const go::f32 y0 = cursor_y + static_cast<go::f32>(glyph->y0);
            const go::f32 x1 = cursor_x + static_cast<go::f32>(glyph->x1);
            const go::f32 y1 = cursor_y + static_cast<go::f32>(glyph->y1);

            const go::f32 u0 = static_cast<go::f32>(glyph->u0);
            const go::f32 v0 = static_cast<go::f32>(glyph->v0);
            const go::f32 u1 = static_cast<go::f32>(glyph->u1);
            const go::f32 v1 = static_cast<go::f32>(glyph->v1);

            const go::u32 image_id = static_cast<go::u32>(glyph->image_id);

            // Two triangles (non-indexed)
            CharVertex vtx[6] =
            {
                { {x0, y0}, {u0, v0}, coloru32, image_id },
                { {x1, y0}, {u1, v0}, coloru32, image_id },
                { {x1, y1}, {u1, v1}, coloru32, image_id },

                { {x0, y0}, {u0, v0}, coloru32, image_id },
                { {x1, y1}, {u1, v1}, coloru32, image_id },
                { {x0, y1}, {u0, v1}, coloru32, image_id },
            };

            m_vertices.insert(m_vertices.end(), std::begin(vtx), std::end(vtx));
            cursor_x += static_cast<go::f32>(glyph->advance);
        }

        m_vertex_count = static_cast<uint32_t>(m_vertices.size());
    }

    auto TextBuffer::upload(VkCommandBuffer cmd) noexcept -> std::expected<void, VkResult>
    {
        if (m_vertices.empty())
        {
            m_vertex_count = 0;
            return {};
        }

        const VkDeviceSize size_bytes = static_cast<VkDeviceSize>(m_vertices.size() * sizeof(CharVertex));
        auto res = m_gpu_buffer.write_to_gpu(cmd, m_vertices.data(), size_bytes);
        if (!res)
            return res;

        return {};
    }

    auto TextBuffer::vertex_buffer() const noexcept -> VkBuffer
    {
        return m_gpu_buffer.buffer();
    }

    auto TextBuffer::vertex_count() const noexcept -> uint32_t
    {
        return m_vertex_count;
    }

    // ===== RenderingOverlay =====

    RenderingOverlay::~RenderingOverlay()
    {
        destroy();
    }

    auto RenderingOverlay::destroy() noexcept -> void
    {
        auto dev = device();
        if (m_desc_pool)
        {
            vkDestroyDescriptorPool(dev, m_desc_pool, nullptr);
            m_desc_pool = VK_NULL_HANDLE;
        }
        if (m_font_sampler)
        {
            vkDestroySampler(dev, m_font_sampler, nullptr);
            m_font_sampler = VK_NULL_HANDLE;
        }

        m_desc_set = VK_NULL_HANDLE;
        m_layout = {};
        m_pipeline = {};
        m_initialized = false;
    }

    auto RenderingOverlay::init(const IFontAtlas& atlas) noexcept -> bool
    {
        if (m_initialized)
            return true;

        auto dev = device();

        // Layout: set 0 - combined image sampler, plus push constants for viewport params
        auto layout_res = LayoutBuilder()
            .set()
            .add(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)
            .push_const(0, sizeof(PushConstants), VK_SHADER_STAGE_VERTEX_BIT)
            .build();

        if (!layout_res)
        {
            std::println("[ERROR] RenderingOverlay: layout creation failed: {}", (int)layout_res.error());
            return false;
        }
        m_layout = std::move(layout_res.value());

        // Pipeline
        VkVertexInputBindingDescription binding
        {
            .binding = 0,
            .stride = static_cast<uint32_t>(sizeof(CharVertex)),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        };

        VkVertexInputAttributeDescription attr_pos
        {
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = static_cast<uint32_t>(offsetof(CharVertex, pos))
        };

        VkVertexInputAttributeDescription attr_uv
        {
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32_SFLOAT,
            .offset = static_cast<uint32_t>(offsetof(CharVertex, uv))
        };

        VkVertexInputAttributeDescription attr_col
        {
            .location = 2,
            .binding = 0,
            .format = VK_FORMAT_R32_UINT,
            .offset = static_cast<uint32_t>(offsetof(CharVertex, col))
        };

        VkVertexInputAttributeDescription attr_img
        {
            .location = 3,
            .binding = 0,
            .format = VK_FORMAT_R32_UINT,
            .offset = static_cast<uint32_t>(offsetof(CharVertex, image))
        };

        auto pipe_res = PipelineBuilder()
            .vertex_shader_from_file("shaders/text_vert.spv")
            .fragment_shader_from_file("shaders/text_frag.spv")
            .color_format(color_format())
            .depth_format(depth_format())
            .samples(sample_count())
            .add(binding)
            .add(attr_pos)
            .add(attr_uv)
            .add(attr_col)
            .add(attr_img)
            .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .polygon_mode(VK_POLYGON_MODE_FILL)
            .cull_mode(VK_CULL_MODE_NONE)
            .front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .depth_test(false)
            .depth_write(false)
            .depth_compare_op(VK_COMPARE_OP_ALWAYS)
            .alpha_blending()
            .build(m_layout.get());

        if (!pipe_res)
        {
            std::println("[ERROR] RenderingOverlay: pipeline creation failed: {}", (int)pipe_res.error());
            return false;
        }
        m_pipeline = std::move(pipe_res.value());

        // Sampler
        VkSamplerCreateInfo sampler_info
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .mipLodBias = 0.0f,
            .anisotropyEnable = VK_FALSE,
            .maxAnisotropy = 1.0f,
            .compareEnable = VK_FALSE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
            .unnormalizedCoordinates = VK_TRUE
        };

        if (vkCreateSampler(dev, &sampler_info, nullptr, &m_font_sampler) != VK_SUCCESS)
        {
            std::println("[ERROR] RenderingOverlay: sampler creation failed");
            destroy();
            return false;
        }

        // Descriptor pool and set
        VkDescriptorPoolSize pool_size
        {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1
        };

        VkDescriptorPoolCreateInfo pool_info
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &pool_size
        };

        if (vkCreateDescriptorPool(dev, &pool_info, nullptr, &m_desc_pool) != VK_SUCCESS)
        {
            std::println("[ERROR] RenderingOverlay: descriptor pool creation failed");
            destroy();
            return false;
        }

        VkDescriptorSetLayout set_layout = m_layout.descriptor_layouts().empty()
            ? VK_NULL_HANDLE
            : m_layout.descriptor_layouts()[0];

        assert(set_layout != VK_NULL_HANDLE);

        VkDescriptorSetAllocateInfo alloc_info
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = m_desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &set_layout
        };

        if (vkAllocateDescriptorSets(dev, &alloc_info, &m_desc_set) != VK_SUCCESS)
        {
            std::println("[ERROR] RenderingOverlay: descriptor set allocation failed");
            destroy();
            return false;
        }

        VkDescriptorImageInfo image_info
        {
            .sampler = m_font_sampler,
            .imageView = atlas.image_view(),
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };

        VkWriteDescriptorSet write
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = m_desc_set,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &image_info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr
        };

        vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

        m_initialized = true;
        return true;
    }

    auto RenderingOverlay::start_text_draw(VkCommandBuffer cmd) noexcept -> void
    {
        if (!m_initialized)
            return;
        assert(cmd != VK_NULL_HANDLE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.get());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout.get(), 0, 1, &m_desc_set, 0, nullptr);
    }

    auto RenderingOverlay::draw(VkCommandBuffer cmd, const TextBuffer& buffer, const VkRect2D& view) noexcept -> void
    {
        if (!m_initialized)
            return;
        if (buffer.vertex_count() == 0)
            return;
        assert(cmd != VK_NULL_HANDLE);

        PushConstants pc
        {
            .view_size_over_2 = { 
                static_cast<float>(view.extent.width) * 0.5f, 
                static_cast<float>(view.extent.height) * 0.5f 
            },
            .view_2_over_size = { 
                2.0f / static_cast<float>(view.extent.width), 
                2.0f / static_cast<float>(view.extent.height) 
            },
            .screen_pos = { static_cast<float>(view.offset.x), static_cast<float>(view.offset.y) },
            .color = 0xffffffffu,
            .color_strength = 0.0f
        };

        vkCmdPushConstants(cmd, m_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        VkBuffer vb = buffer.vertex_buffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdDraw(cmd, buffer.vertex_count(), 1, 0, 0);
    }

    auto RenderingOverlay::draw(VkCommandBuffer cmd, const TextBuffer& buffer, const VkRect2D& view, const go::vu4& color) noexcept -> void
    {
        if (!m_initialized)
            return;
        if (buffer.vertex_count() == 0)
            return;
        assert(cmd != VK_NULL_HANDLE);
        
        PushConstants pc
        {
            .view_size_over_2 = { 
                static_cast<float>(view.extent.width) * 0.5f, 
                static_cast<float>(view.extent.height) * 0.5f 
            },
            .view_2_over_size = { 
                2.0f / static_cast<float>(view.extent.width), 
                2.0f / static_cast<float>(view.extent.height) 
            },
            .screen_pos = { static_cast<float>(view.offset.x), static_cast<float>(view.offset.y) },
            .color = go::packUnorm4x8(color),
            .color_strength = 1.0f
        };

        vkCmdPushConstants(cmd, m_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        VkBuffer vb = buffer.vertex_buffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdDraw(cmd, buffer.vertex_count(), 1, 0, 0);
    }
}

