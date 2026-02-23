#pragma once

#include <expected>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

#include "gomath.hpp"
#include "font_atlas.hpp"
#include "dynamic_buffer.hpp"
#include "layout.hpp"
#include "pipeline.hpp"

namespace engi::vk
{
    struct FontId
    {
        const IFontAtlas* ptr = nullptr;
        size_t image_offset = 0;
    };

    struct CharVertex
    {
        go::vf2 pos;
        go::vf2 uv;   // unnormalized atlas coordinates (pixels)
        go::u32 col;  // packed sRGBA (unorm4x8)
        go::u32 image;
    };

    struct Vertex2D
    {
        go::vf2 pos;
        go::vf2 uv;    // not used for now
        go::u32 col;   // packed sRGBA (unorm4x8)
        go::u32 image; // not used for now
    };

    struct Vertex2DWire
    {
        go::vf2 pos;
        go::u32 col;   // packed sRGBA (unorm4x8)
    };

    class TextBuffer
    {
    public:
        TextBuffer() = default;
        TextBuffer(const TextBuffer&) = delete;
        auto operator=(const TextBuffer&) -> TextBuffer& = delete;
        TextBuffer(TextBuffer&&) noexcept = default;
        auto operator=(TextBuffer&&) noexcept -> TextBuffer& = default;
        ~TextBuffer() = default;

        static auto create(FontId font) -> std::expected<TextBuffer, VkResult>;

        auto clear() noexcept -> void;

        auto add(std::wstring_view text, const go::vf2& pos, const go::vu4& color) -> void;

        auto upload(VkCommandBuffer cmd) noexcept -> std::expected<void, VkResult>;

        auto vertex_buffer() const noexcept -> VkBuffer;
        auto vertex_count() const noexcept -> uint32_t;

    private:
        std::vector<CharVertex> m_vertices;
        DynamicBuffer m_gpu_buffer;
        uint32_t m_vertex_count = 0;
        FontId m_font_id;
    };

    class GeometryBuffer2D
    {
    public:
        GeometryBuffer2D() = default;
        GeometryBuffer2D(const GeometryBuffer2D&) = delete;
        auto operator=(const GeometryBuffer2D&) -> GeometryBuffer2D& = delete;
        GeometryBuffer2D(GeometryBuffer2D&&) noexcept = default;
        auto operator=(GeometryBuffer2D&&) noexcept -> GeometryBuffer2D& = default;
        ~GeometryBuffer2D() = default;

        static auto create() -> std::expected<GeometryBuffer2D, VkResult>;

        auto clear() noexcept -> void;

        auto add_rect(const go::vf2& pos, const go::vf2& size, const go::vu4& color) -> void;

        auto upload(VkCommandBuffer cmd) noexcept -> std::expected<void, VkResult>;

        auto vertex_buffer() const noexcept -> VkBuffer;
        auto index_buffer() const noexcept -> VkBuffer;
        auto index_count() const noexcept -> uint32_t;

    private:
        std::vector<Vertex2D> m_vertices;
        std::vector<uint32_t> m_indices;
        DynamicBuffer m_vertex_gpu;
        DynamicBuffer m_index_gpu;
        uint32_t m_index_count = 0;
    };

    class GeometryBuffer2DWire
    {
    public:
        GeometryBuffer2DWire() = default;
        GeometryBuffer2DWire(const GeometryBuffer2DWire&) = delete;
        auto operator=(const GeometryBuffer2DWire&) -> GeometryBuffer2DWire& = delete;
        GeometryBuffer2DWire(GeometryBuffer2DWire&&) noexcept = default;
        auto operator=(GeometryBuffer2DWire&&) noexcept -> GeometryBuffer2DWire& = default;
        ~GeometryBuffer2DWire() = default;

        static auto create() -> std::expected<GeometryBuffer2DWire, VkResult>;

        auto clear() noexcept -> void;

        auto add_rect(const go::vf2& pos, const go::vf2& size, const go::vu4& color) -> void;

        auto upload(VkCommandBuffer cmd) noexcept -> std::expected<void, VkResult>;

        auto vertex_buffer() const noexcept -> VkBuffer;
        auto index_buffer() const noexcept -> VkBuffer;
        auto index_count() const noexcept -> uint32_t;

    private:
        std::vector<Vertex2DWire> m_vertices;
        std::vector<uint32_t> m_indices;
        DynamicBuffer m_vertex_gpu;
        DynamicBuffer m_index_gpu;
        uint32_t m_index_count = 0;
    };


    class RenderingOverlay
    {
    public:
        RenderingOverlay() = default;
        RenderingOverlay(const RenderingOverlay&) = delete;
        auto operator=(const RenderingOverlay&) -> RenderingOverlay& = delete;
        RenderingOverlay(RenderingOverlay&& other) noexcept;
        auto operator=(RenderingOverlay&& other) noexcept -> RenderingOverlay&;
        ~RenderingOverlay();

        // Registers font and returns its ID. Must be called before init().
        auto add_font(IFontAtlas* atlas) -> FontId;

        // Initializes pipeline, layout, sampler and descriptor set for all added fonts.
        auto init(bool enable_msaa) noexcept -> bool;

        // Draws text buffer inside given viewport rectangle.
        auto start_text_draw(VkCommandBuffer cmd) noexcept -> void;
        auto draw(VkCommandBuffer cmd, const TextBuffer& buffer, const VkRect2D& view) noexcept -> void;
        auto draw(VkCommandBuffer cmd, const TextBuffer& buffer, const VkRect2D& view, const go::vu4& color) noexcept -> void;

        // Draws 2D geometry buffers.
        auto start_draw_2d(VkCommandBuffer cmd) noexcept -> void;
        auto draw(VkCommandBuffer cmd, const GeometryBuffer2D& buffer, const VkRect2D& view) noexcept -> void;

        auto start_draw_2d_wire(VkCommandBuffer cmd) noexcept -> void;
        auto draw(VkCommandBuffer cmd, const GeometryBuffer2DWire& buffer, const VkRect2D& view) noexcept -> void;

    private:
        struct PushConstants
        {
            go::vf2 view_size_over_2;
            go::vf2 view_2_over_size;
            go::vf2 screen_pos;
            uint32_t color;
            float color_strength;
        };

        auto destroy() noexcept -> void;

        VkSampler m_font_sampler = VK_NULL_HANDLE;
        VkDescriptorPool m_desc_pool = VK_NULL_HANDLE;
        VkDescriptorSet m_desc_set = VK_NULL_HANDLE;

        Layout m_layout;
        Pipeline m_pipeline_text;
        Pipeline m_pipeline_2d;
        Pipeline m_pipeline_2d_wire;
        std::vector<IFontAtlas*> m_fonts;
        bool m_initialized = false;
    };

}

