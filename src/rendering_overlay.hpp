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
    struct CharVertex
    {
        go::vf2 pos;
        go::vf2 uv;   // unnormalized atlas coordinates (pixels)
        go::u32 col;  // packed sRGBA (unorm4x8)
        go::u32 image;
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

        static auto create(const IFontAtlas* atlas) -> std::expected<TextBuffer, VkResult>;

        auto clear() noexcept -> void;

        auto add(std::wstring_view text, const go::vf2& pos, const go::vu4& color) -> void;

        auto upload(VkCommandBuffer cmd) noexcept -> std::expected<void, VkResult>;

        auto vertex_buffer() const noexcept -> VkBuffer;
        auto vertex_count() const noexcept -> uint32_t;

    private:
        std::vector<CharVertex> m_vertices;
        DynamicBuffer m_gpu_buffer;
        uint32_t m_vertex_count = 0;
        const IFontAtlas* m_atlas = nullptr; // non-owning reference
    };

    class RenderingOverlay
    {
    public:
        RenderingOverlay() = default;
        RenderingOverlay(const RenderingOverlay&) = delete;
        auto operator=(const RenderingOverlay&) -> RenderingOverlay& = delete;
        RenderingOverlay(RenderingOverlay&& other) noexcept
            : m_font_sampler(other.m_font_sampler)
            , m_desc_pool(other.m_desc_pool)
            , m_desc_set(other.m_desc_set)
            , m_layout(std::move(other.m_layout))
            , m_pipeline(std::move(other.m_pipeline))
            , m_initialized(other.m_initialized)
        {
            other.m_font_sampler = VK_NULL_HANDLE;
            other.m_desc_pool = VK_NULL_HANDLE;
            other.m_desc_set = VK_NULL_HANDLE;
            other.m_initialized = false;
        }

        auto operator=(RenderingOverlay&& other) noexcept -> RenderingOverlay&
        {
            if (this != &other)
            {
                destroy();

                m_font_sampler = other.m_font_sampler;
                m_desc_pool = other.m_desc_pool;
                m_desc_set = other.m_desc_set;
                m_layout = std::move(other.m_layout);
                m_pipeline = std::move(other.m_pipeline);
                m_initialized = other.m_initialized;

                other.m_font_sampler = VK_NULL_HANDLE;
                other.m_desc_pool = VK_NULL_HANDLE;
                other.m_desc_set = VK_NULL_HANDLE;
                other.m_initialized = false;
            }
            return *this;
        }
        ~RenderingOverlay();

        // Initializes pipeline, layout, sampler and descriptor set for given font atlas.
        auto init(const IFontAtlas& atlas) noexcept -> bool;

        // Draws text buffer inside given viewport rectangle.
        auto start_text_draw(VkCommandBuffer cmd) noexcept -> void;
        auto draw(VkCommandBuffer cmd, const TextBuffer& buffer, const VkRect2D& view) noexcept -> void;
        auto draw(VkCommandBuffer cmd, const TextBuffer& buffer, const VkRect2D& view, const go::vu4& color) noexcept -> void;

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
        Pipeline m_pipeline;
        bool m_initialized = false;
    };
}

