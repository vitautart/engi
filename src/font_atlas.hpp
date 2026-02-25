#pragma once

#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <filesystem>
#include <string_view>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "rendering.hpp"

namespace engi::vk
{
    struct Glyph
    {
        int16_t x0, y0, x1, y1;  // pixel coords in bitmap
        int16_t u0, v0, u1, v1;  // texcoord in bitmap
        int16_t advance;
        int16_t image_id;        // array layer in atlas image
    };
    struct CharRange
    {
        int first_char;
        int char_count;
    };

    class IFontAtlas
    {
    public:
        virtual ~IFontAtlas() = default;
        virtual auto calculate_line_width(std::wstring_view text) const -> uint32_t = 0;
        virtual auto calculate_multiline_size(std::wstring_view text, uint32_t line_height) const -> go::vu2 = 0;
        virtual auto get_cap_height() const noexcept -> int = 0;
        virtual auto get_x_height() const noexcept -> int = 0;
        virtual auto get_glyph(int codepoint) const noexcept -> const Glyph* = 0;
        virtual auto get_line_height() const noexcept -> int = 0;
        virtual auto image_view() const noexcept -> VkImageView = 0;
        virtual auto image() const noexcept -> VkImage = 0;
        // number of array layers in atlas image
        virtual auto layer_count() const noexcept -> uint32_t = 0;
    };

    class FontMonoAtlas : public IFontAtlas
    {
    public:
        FontMonoAtlas() = default;
        FontMonoAtlas(const FontMonoAtlas&) = delete;
        FontMonoAtlas(FontMonoAtlas&&) noexcept = default;
        auto operator=(FontMonoAtlas&&) noexcept -> FontMonoAtlas& = default;
        ~FontMonoAtlas() = default;

        static auto create(
            VkCommandBuffer cmd,
            const std::filesystem::path& font_path,
            int line_height,
            uint32_t bitmap_width,
            uint32_t bitmap_height,
            std::span<CharRange> char_ranges = {}
        ) -> std::expected<FontMonoAtlas, VkResult>;

        static auto create(
            VkCommandBuffer cmd,
            const std::filesystem::path& font_path,
            int line_height,
            uint32_t bitmap_width,
            uint32_t bitmap_height,
            Buffer& staging_buffer,
            std::span<CharRange> char_ranges = {}
        ) -> std::expected<FontMonoAtlas, VkResult>;

        // IFontAtlas interface
        auto image_view() const noexcept -> VkImageView override { return m_atlas.view(); }
        auto image() const noexcept -> VkImage override { return m_atlas.image(); }
        auto layer_count() const noexcept -> uint32_t override { return m_layer_count; }
        auto get_line_height() const noexcept -> int override { return m_line_height; }
        auto get_glyph(int codepoint) const noexcept -> const Glyph* override;
        auto calculate_line_width(std::wstring_view text) const -> uint32_t override;
        auto calculate_multiline_size(std::wstring_view text, uint32_t line_height) const -> go::vu2 override;
        auto get_cap_height() const noexcept -> int override { return m_cap_height; }
        auto get_x_height() const noexcept -> int override { return m_x_height; }

        // FontMonoAtlas interface
        auto get_advance() const noexcept -> int { return m_advance; }

    private:
        Image m_atlas;
        uint32_t m_layer_count = 0;
        int m_line_height = 0;
        int m_advance = 0; // common advance for mono font
        int m_cap_height = 0;
        int m_x_height = 0;
        std::map<int, Glyph> m_glyph_map;
    };
}
