#pragma once

#include <expected>
#include <map>
#include <string_view>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "rendering.hpp"

namespace engi::vk
{
    struct MonoGlyph
    {
        int16_t x0, y0, x1, y1;  // pixel coords in bitmap
        int16_t u0, v0, u1, v1;  // texcoord in bitmap
    };

    class FontMonoAtlas
    {
    public:
        FontMonoAtlas() = default;
        FontMonoAtlas(const FontMonoAtlas&) = delete;
        FontMonoAtlas(FontMonoAtlas&&) noexcept = default;
        auto operator=(FontMonoAtlas&&) noexcept -> FontMonoAtlas& = default;
        ~FontMonoAtlas() = default;

        static auto create(
            VkCommandBuffer cmd,
            const char* font_path,
            int line_height,
            uint32_t bitmap_width,
            uint32_t bitmap_height
        ) -> std::expected<FontMonoAtlas, VkResult>;

        auto image_view() const noexcept -> VkImageView { return m_atlas.view(); }
        auto image() const noexcept -> VkImage { return m_atlas.image(); }
        auto height() const noexcept -> int { return m_height; }
        auto line_height() const noexcept -> int { return m_line_height; }

        auto get_glyph(int codepoint) const noexcept -> const MonoGlyph*;
        auto calculate_width(std::wstring_view text) -> uint32_t;
        auto get_advance() const noexcept -> int { return m_advance; }
        auto get_cap_height() const noexcept -> int { return m_cap_height; }
        auto get_x_height() const noexcept -> int { return m_x_height; }

    private:
        Image m_atlas;
        int m_height = 0;
        int m_line_height = 0;
        int m_advance = 0; // common advance for mono font
        int m_cap_height = 0;
        int m_x_height = 0;
        std::map<wchar_t, MonoGlyph> m_glyph_map;
    };
}
