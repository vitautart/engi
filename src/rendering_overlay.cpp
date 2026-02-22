#include "rendering_overlay.hpp"
#include "gomath.hpp"

#include <cassert>
#include <print>
#include <vector>

namespace engi::vk
{
    // ===== TextBuffer =====

    auto TextBuffer::create(FontId font) -> std::expected<TextBuffer, VkResult>
    {
        TextBuffer out;
        out.m_font_id = font;

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
        const auto atlas = m_font_id.ptr;
        const auto line_height = static_cast<go::f32>(atlas->get_line_height());

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

            const Glyph* glyph = atlas->get_glyph(static_cast<int>(ch));
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

            const go::u32 image_id = static_cast<go::u32>(glyph->image_id) + static_cast<go::u32>(m_font_id.image_offset);

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

    // ===== GeometryBuffer2D =====

    auto GeometryBuffer2D::create() -> std::expected<GeometryBuffer2D, VkResult>
    {
        GeometryBuffer2D out;
        constexpr VkDeviceSize initial_vertex_count = 1024;
        constexpr VkDeviceSize initial_index_count = 1024 * 3;

        auto vb = DynamicBuffer::create(initial_vertex_count * sizeof(Vertex2D), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (!vb) return std::unexpected(vb.error());
        out.m_vertex_gpu = std::move(vb.value());

        auto ib = DynamicBuffer::create(initial_index_count * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        if (!ib) return std::unexpected(ib.error());
        out.m_index_gpu = std::move(ib.value());

        return out;
    }

    auto GeometryBuffer2D::clear() noexcept -> void
    {
        m_vertices.clear();
        m_indices.clear();
        m_index_count = 0;
    }

    auto GeometryBuffer2D::add_rect(const go::vf2& pos, const go::vf2& size, const go::vu4& color) -> void
    {
        const go::u32 col = go::packUnorm4x8(color);
        const uint32_t first_vtx = static_cast<uint32_t>(m_vertices.size());

        m_vertices.push_back({ {pos[0], pos[1]}, {0, 0}, col, 0 });
        m_vertices.push_back({ {pos[0] + size[0], pos[1]}, {1, 0}, col, 0 });
        m_vertices.push_back({ {pos[0] + size[0], pos[1] + size[1]}, {1, 1}, col, 0 });
        m_vertices.push_back({ {pos[0], pos[1] + size[1]}, {0, 1}, col, 0 });

        m_indices.push_back(first_vtx + 0);
        m_indices.push_back(first_vtx + 1);
        m_indices.push_back(first_vtx + 2);
        m_indices.push_back(first_vtx + 0);
        m_indices.push_back(first_vtx + 2);
        m_indices.push_back(first_vtx + 3);

        m_index_count = static_cast<uint32_t>(m_indices.size());
    }

    auto GeometryBuffer2D::upload(VkCommandBuffer cmd) noexcept -> std::expected<void, VkResult>
    {
        if (m_indices.empty())
        {
            m_index_count = 0;
            return {};
        }

        auto res_v = m_vertex_gpu.write_to_gpu(cmd, m_vertices.data(), m_vertices.size() * sizeof(Vertex2D));
        if (!res_v) return res_v;

        auto res_i = m_index_gpu.write_to_gpu(cmd, m_indices.data(), m_indices.size() * sizeof(uint32_t));
        if (!res_i) return res_i;

        return {};
    }

    auto GeometryBuffer2D::vertex_buffer() const noexcept -> VkBuffer { return m_vertex_gpu.buffer(); }
    auto GeometryBuffer2D::index_buffer() const noexcept -> VkBuffer { return m_index_gpu.buffer(); }
    auto GeometryBuffer2D::index_count() const noexcept -> uint32_t { return m_index_count; }

    // ===== GeometryBuffer2DWire =====

    auto GeometryBuffer2DWire::create() -> std::expected<GeometryBuffer2DWire, VkResult>
    {
        GeometryBuffer2DWire out;
        constexpr VkDeviceSize initial_vertex_count = 1024;
        constexpr VkDeviceSize initial_index_count = 1024 * 2;

        auto vb = DynamicBuffer::create(initial_vertex_count * sizeof(Vertex2DWire), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        if (!vb) return std::unexpected(vb.error());
        out.m_vertex_gpu = std::move(vb.value());

        auto ib = DynamicBuffer::create(initial_index_count * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        if (!ib) return std::unexpected(ib.error());
        out.m_index_gpu = std::move(ib.value());

        return out;
    }

    auto GeometryBuffer2DWire::clear() noexcept -> void
    {
        m_vertices.clear();
        m_indices.clear();
        m_index_count = 0;
    }

    auto GeometryBuffer2DWire::add_rect(const go::vf2& pos, const go::vf2& size, const go::vu4& color) -> void
    {
        const go::u32 col = go::packUnorm4x8(color);
        const uint32_t first_vtx = static_cast<uint32_t>(m_vertices.size());

        m_vertices.push_back({ {pos[0], pos[1]}, col });
        m_vertices.push_back({ {pos[0] + size[0], pos[1]}, col });
        m_vertices.push_back({ {pos[0] + size[0], pos[1] + size[1]}, col });
        m_vertices.push_back({ {pos[0], pos[1] + size[1]}, col });

        m_indices.push_back(first_vtx + 0); m_indices.push_back(first_vtx + 1);
        m_indices.push_back(first_vtx + 1); m_indices.push_back(first_vtx + 2);
        m_indices.push_back(first_vtx + 2); m_indices.push_back(first_vtx + 3);
        m_indices.push_back(first_vtx + 3); m_indices.push_back(first_vtx + 0);

        m_index_count = static_cast<uint32_t>(m_indices.size());
    }

    auto GeometryBuffer2DWire::upload(VkCommandBuffer cmd) noexcept -> std::expected<void, VkResult>
    {
        if (m_indices.empty())
        {
            m_index_count = 0;
            return {};
        }

        auto res_v = m_vertex_gpu.write_to_gpu(cmd, m_vertices.data(), m_vertices.size() * sizeof(Vertex2DWire));
        if (!res_v) return res_v;

        auto res_i = m_index_gpu.write_to_gpu(cmd, m_indices.data(), m_indices.size() * sizeof(uint32_t));
        if (!res_i) return res_i;

        return {};
    }

    auto GeometryBuffer2DWire::vertex_buffer() const noexcept -> VkBuffer { return m_vertex_gpu.buffer(); }
    auto GeometryBuffer2DWire::index_buffer() const noexcept -> VkBuffer { return m_index_gpu.buffer(); }
    auto GeometryBuffer2DWire::index_count() const noexcept -> uint32_t { return m_index_count; }


    // ===== RenderingOverlay =====

    RenderingOverlay::RenderingOverlay(RenderingOverlay&& other) noexcept
        : m_font_sampler(other.m_font_sampler)
        , m_desc_pool(other.m_desc_pool)
        , m_desc_set(other.m_desc_set)
        , m_layout(std::move(other.m_layout))
        , m_pipeline_text(std::move(other.m_pipeline_text))
        , m_pipeline_2d(std::move(other.m_pipeline_2d))
        , m_pipeline_2d_wire(std::move(other.m_pipeline_2d_wire))
        , m_fonts(std::move(other.m_fonts))
        , m_initialized(other.m_initialized)
    {
        other.m_font_sampler = VK_NULL_HANDLE;
        other.m_desc_pool = VK_NULL_HANDLE;
        other.m_desc_set = VK_NULL_HANDLE;
        other.m_initialized = false;
    }

    auto RenderingOverlay::operator=(RenderingOverlay&& other) noexcept -> RenderingOverlay&
    {
        if (this != &other)
        {
            destroy();

            m_font_sampler = other.m_font_sampler;
            m_desc_pool = other.m_desc_pool;
            m_desc_set = other.m_desc_set;
            m_layout = std::move(other.m_layout);
            m_pipeline_text = std::move(other.m_pipeline_text);
            m_pipeline_2d = std::move(other.m_pipeline_2d);
            m_pipeline_2d_wire = std::move(other.m_pipeline_2d_wire);
            m_fonts = std::move(other.m_fonts);
            m_initialized = other.m_initialized;

            other.m_font_sampler = VK_NULL_HANDLE;
            other.m_desc_pool = VK_NULL_HANDLE;
            other.m_desc_set = VK_NULL_HANDLE;
            other.m_initialized = false;
        }
        return *this;
    }

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
        m_pipeline_text = {};
        m_pipeline_2d = {};
        m_pipeline_2d_wire = {};
        m_initialized = false;
    }

    auto RenderingOverlay::add_font(IFontAtlas* atlas) -> FontId
    {
        size_t offset = 0;
        for (auto* f : m_fonts)
        {
            offset += f->image_count();
        }
        m_fonts.push_back(atlas);
        return { .ptr = atlas, .image_offset = offset };
    }

    auto RenderingOverlay::init() noexcept -> bool
    {
        if (m_initialized)
            return true;

        size_t total_image_count = 0;
        for (auto* f : m_fonts)
        {
            total_image_count += f->image_count();
        }

        auto dev = device();

        // Layout: set 0 - combined image sampler, plus push constants for viewport params
        auto layout_res = LayoutBuilder()
            .set(false)
            .add(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, static_cast<uint32_t>(total_image_count))
            .push_const(0, sizeof(PushConstants), VK_SHADER_STAGE_VERTEX_BIT)
            .build();

        if (!layout_res)
        {
            std::println("[ERROR] RenderingOverlay: layout creation failed: {}", (int)layout_res.error());
            return false;
        }
        m_layout = std::move(layout_res.value());

        // --- Text Pipeline ---
        {
            VkVertexInputBindingDescription binding
            {
                .binding = 0,
                .stride = static_cast<uint32_t>(sizeof(CharVertex)),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };

            VkVertexInputAttributeDescription attrs[] = {
                { 0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(CharVertex, pos)) },
                { 1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(CharVertex, uv)) },
                { 2, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(offsetof(CharVertex, col)) },
                { 3, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(offsetof(CharVertex, image)) }
            };

            auto pipe_res = PipelineBuilder()
                .vertex_shader_from_file("shaders/text_vert.spv")
                .fragment_shader_from_file("shaders/text_frag.spv")
                .color_format(color_format())
                .depth_format(depth_format())
                .samples(sample_count())
                .add(binding)
                .add(attrs[0]).add(attrs[1]).add(attrs[2]).add(attrs[3])
                .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
                .polygon_mode(VK_POLYGON_MODE_FILL)
                .cull_mode(VK_CULL_MODE_NONE)
                .front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
                .depth_test(false)
                .depth_write(false)
                .depth_compare_op(VK_COMPARE_OP_ALWAYS)
                .alpha_blending()
                .build(m_layout.get());

            if (!pipe_res) return false;
            m_pipeline_text = std::move(pipe_res.value());
        }

        // --- 2D Pipeline (Filled) ---
        {
            VkVertexInputBindingDescription binding
            {
                .binding = 0,
                .stride = static_cast<uint32_t>(sizeof(Vertex2D)),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };

            VkVertexInputAttributeDescription attrs[] = {
                { 0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex2D, pos)) },
                { 1, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex2D, uv)) },
                { 2, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(offsetof(Vertex2D, col)) },
                { 3, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(offsetof(Vertex2D, image)) }
            };

            auto pipe_res = PipelineBuilder()
                .vertex_shader_from_file("shaders/rect_vert.spv")
                .fragment_shader_from_file("shaders/rect_frag.spv")
                .color_format(color_format())
                .depth_format(depth_format())
                .samples(sample_count())
                .add(binding)
                .add(attrs[0]).add(attrs[1]).add(attrs[2]).add(attrs[3])
                .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
                .polygon_mode(VK_POLYGON_MODE_FILL)
                .cull_mode(VK_CULL_MODE_NONE)
                .front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
                .depth_test(false)
                .depth_write(false)
                .alpha_blending()
                .build(m_layout.get());

            if (!pipe_res) return false;
            m_pipeline_2d = std::move(pipe_res.value());
        }

        // --- 2D Wire Pipeline ---
        {
            VkVertexInputBindingDescription binding
            {
                .binding = 0,
                .stride = static_cast<uint32_t>(sizeof(Vertex2DWire)),
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };

            VkVertexInputAttributeDescription attrs[] = {
                { 0, 0, VK_FORMAT_R32G32_SFLOAT, static_cast<uint32_t>(offsetof(Vertex2DWire, pos)) },
                { 1, 0, VK_FORMAT_R32_UINT, static_cast<uint32_t>(offsetof(Vertex2DWire, col)) }
            };

            auto pipe_res = PipelineBuilder()
                .vertex_shader_from_file("shaders/rect_wire_vert.spv")
                .fragment_shader_from_file("shaders/rect_wire_frag.spv")
                .color_format(color_format())
                .depth_format(depth_format())
                .samples(sample_count())
                .add(binding)
                .add(attrs[0]).add(attrs[1])
                .topology(VK_PRIMITIVE_TOPOLOGY_LINE_LIST)
                .polygon_mode(VK_POLYGON_MODE_LINE)
                .cull_mode(VK_CULL_MODE_NONE)
                .front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
                .depth_test(false)
                .depth_write(false)
                .alpha_blending()
                .line_width(1.0f)
                .build(m_layout.get());

            if (!pipe_res) return false;
            m_pipeline_2d_wire = std::move(pipe_res.value());
        }

        // Sampler
        VkSamplerCreateInfo sampler_info
        {
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .unnormalizedCoordinates = VK_TRUE
        };

        if (vkCreateSampler(dev, &sampler_info, nullptr, &m_font_sampler) != VK_SUCCESS)
        {
            destroy();
            return false;
        }

        // Descriptor pool and set
        if (total_image_count > 0)
        {
            VkDescriptorPoolSize pool_size { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(total_image_count) };
            VkDescriptorPoolCreateInfo pool_info { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &pool_size };

            if (vkCreateDescriptorPool(dev, &pool_info, nullptr, &m_desc_pool) != VK_SUCCESS)
            {
                destroy();
                return false;
            }

            VkDescriptorSetLayout set_layout = m_layout.descriptor_layouts()[0];
            VkDescriptorSetAllocateInfo alloc_info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, m_desc_pool, 1, &set_layout };

            if (vkAllocateDescriptorSets(dev, &alloc_info, &m_desc_set) != VK_SUCCESS)
            {
                destroy();
                return false;
            }

            std::vector<VkDescriptorImageInfo> image_infos;
            for (auto* atlas : m_fonts)
            {
                for (size_t i = 0; i < atlas->image_count(); ++i)
                {
                    image_infos.push_back({ m_font_sampler, atlas->image_view(i), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
                }
            }

            VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_desc_set, 0, 0, static_cast<uint32_t>(image_infos.size()), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, image_infos.data() };
            vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);
        }

        m_initialized = true;
        return true;
    }

    auto RenderingOverlay::start_text_draw(VkCommandBuffer cmd) noexcept -> void
    {
        if (!m_initialized) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_text.get());
        if (m_desc_set != VK_NULL_HANDLE)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout.get(), 0, 1, &m_desc_set, 0, nullptr);
    }

    auto RenderingOverlay::draw(VkCommandBuffer cmd, const TextBuffer& buffer, const VkRect2D& view) noexcept -> void
    {
        if (!m_initialized || buffer.vertex_count() == 0) return;

        PushConstants pc {
            .view_size_over_2 = { view.extent.width * 0.5f, view.extent.height * 0.5f },
            .view_2_over_size = { 2.0f / view.extent.width, 2.0f / view.extent.height },
            .screen_pos = { (float)view.offset.x, (float)view.offset.y },
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
        if (!m_initialized || buffer.vertex_count() == 0) return;
        
        PushConstants pc {
            .view_size_over_2 = { view.extent.width * 0.5f, view.extent.height * 0.5f },
            .view_2_over_size = { 2.0f / view.extent.width, 2.0f / view.extent.height },
            .screen_pos = { (float)view.offset.x, (float)view.offset.y },
            .color = go::packUnorm4x8(color),
            .color_strength = 1.0f
        };
        vkCmdPushConstants(cmd, m_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        VkBuffer vb = buffer.vertex_buffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdDraw(cmd, buffer.vertex_count(), 1, 0, 0);
    }

    auto RenderingOverlay::start_draw_2d(VkCommandBuffer cmd) noexcept -> void
    {
        if (!m_initialized) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_2d.get());
    }

    auto RenderingOverlay::draw(VkCommandBuffer cmd, const GeometryBuffer2D& buffer, const VkRect2D& view) noexcept -> void
    {
        if (!m_initialized || buffer.index_count() == 0) return;

        PushConstants pc {
            .view_size_over_2 = { view.extent.width * 0.5f, view.extent.height * 0.5f },
            .view_2_over_size = { 2.0f / view.extent.width, 2.0f / view.extent.height },
            .screen_pos = { (float)view.offset.x, (float)view.offset.y },
            .color = 0xffffffffu,
            .color_strength = 0.0f
        };
        vkCmdPushConstants(cmd, m_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        VkBuffer vb = buffer.vertex_buffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, buffer.index_buffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, buffer.index_count(), 1, 0, 0, 0);
    }

    auto RenderingOverlay::start_draw_2d_wire(VkCommandBuffer cmd) noexcept -> void
    {
        if (!m_initialized) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_2d_wire.get());
    }

    auto RenderingOverlay::draw(VkCommandBuffer cmd, const GeometryBuffer2DWire& buffer, const VkRect2D& view) noexcept -> void
    {
        if (!m_initialized || buffer.index_count() == 0) return;

        PushConstants pc {
            .view_size_over_2 = { view.extent.width * 0.5f, view.extent.height * 0.5f },
            .view_2_over_size = { 2.0f / view.extent.width, 2.0f / view.extent.height },
            .screen_pos = { (float)view.offset.x, (float)view.offset.y },
            .color = 0xffffffffu,
            .color_strength = 0.0f
        };
        vkCmdPushConstants(cmd, m_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

        VkBuffer vb = buffer.vertex_buffer();
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &offset);
        vkCmdBindIndexBuffer(cmd, buffer.index_buffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, buffer.index_count(), 1, 0, 0, 0);
    }
}

