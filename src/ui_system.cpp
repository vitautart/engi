#include "ui_system.hpp"

#include <algorithm>
#include <cmath>
#include <print>

namespace engi::ui
{
    // ===== Element ID Generator =====

    static uint32_t s_next_id = 1;

    auto next_element_id() -> uint32_t
    {
        return s_next_id++;
    }

    // ===== Helpers =====

    static auto point_in_rect(const go::vf2& point, const go::vf2& rect_pos, const go::vf2& rect_size) -> bool
    {
        return point[0] >= rect_pos[0] && point[0] <= rect_pos[0] + rect_size[0] &&
               point[1] >= rect_pos[1] && point[1] <= rect_pos[1] + rect_size[1];
    }

    static auto clip_rect(const go::vf2& a_pos, const go::vf2& a_size,
                           const go::vf2& b_pos, const go::vf2& b_size) -> std::pair<go::vf2, go::vf2>
    {
        auto x0 = std::max(a_pos[0], b_pos[0]);
        auto y0 = std::max(a_pos[1], b_pos[1]);
        auto x1 = std::min(a_pos[0] + a_size[0], b_pos[0] + b_size[0]);
        auto y1 = std::min(a_pos[1] + a_size[1], b_pos[1] + b_size[1]);
        auto w = std::max(0.0f, x1 - x0);
        auto h = std::max(0.0f, y1 - y0);
        return {{x0, y0}, {w, h}};
    }

    static auto to_rect(const go::vf2& pos, const go::vf2& size) -> VkRect2D
    {
        auto out = VkRect2D{};
        out.offset.x = static_cast<int32_t>(std::max(0.0f, pos[0]));
        out.offset.y = static_cast<int32_t>(std::max(0.0f, pos[1]));
        out.extent.width = static_cast<uint32_t>(std::max(0.0f, size[0]));
        out.extent.height = static_cast<uint32_t>(std::max(0.0f, size[1]));
        return out;
    }

    static auto rect_intersection(const VkRect2D& a, const VkRect2D& b) -> VkRect2D
    {
        auto ax1 = a.offset.x + static_cast<int32_t>(a.extent.width);
        auto ay1 = a.offset.y + static_cast<int32_t>(a.extent.height);
        auto bx1 = b.offset.x + static_cast<int32_t>(b.extent.width);
        auto by1 = b.offset.y + static_cast<int32_t>(b.extent.height);

        auto x0 = std::max(a.offset.x, b.offset.x);
        auto y0 = std::max(a.offset.y, b.offset.y);
        auto x1 = std::min(ax1, bx1);
        auto y1 = std::min(ay1, by1);

        auto out = VkRect2D{};
        out.offset.x = x0;
        out.offset.y = y0;
        out.extent.width = static_cast<uint32_t>(std::max(0, x1 - x0));
        out.extent.height = static_cast<uint32_t>(std::max(0, y1 - y0));
        return out;
    }

    static auto effective_font(const UIElement& el, const DrawContext& ctx) -> vk::FontId
    {
        if (el.font.ptr)
        {
            return el.font;
        }
        return ctx.default_font;
    }

    static auto effective_font_atlas(const UIElement& el, const DrawContext& ctx) -> const vk::IFontAtlas*
    {
        return effective_font(el, ctx).ptr;
    }

    static auto effective_text_buffer(UIElement& el, DrawContext& ctx) -> vk::TextBuffer*
    {
        if (!ctx.resolve_text_buffer)
        {
            return nullptr;
        }
        auto font = effective_font(el, ctx);
        if (!font.ptr)
        {
            return nullptr;
        }
        return ctx.resolve_text_buffer(font);
    }

    static auto glyph_advance(const vk::IFontAtlas* font_atlas, wchar_t ch) -> float
    {
        if (!font_atlas)
        {
            return 10.0f;
        }

        if (auto glyph = font_atlas->get_glyph(static_cast<int>(ch)); glyph)
        {
            return static_cast<float>(glyph->advance);
        }

        auto one = std::wstring_view{&ch, 1};
        return static_cast<float>(font_atlas->calculate_line_width(one));
    }

    static auto measure_prefix_width(std::wstring_view text, uint32_t cursor, const vk::IFontAtlas* font_atlas) -> float
    {
        auto width = 0.0f;
        auto end = std::min(cursor, static_cast<uint32_t>(text.size()));
        for (uint32_t i = 0; i < end; i++)
        {
            auto ch = text[i];
            if (ch == L'\n')
            {
                break;
            }
            width += glyph_advance(font_atlas, ch);
        }
        return width;
    }

    static auto find_cursor_in_line(std::wstring_view line, float local_x, const vk::IFontAtlas* font_atlas) -> uint32_t
    {
        if (local_x <= 0.0f)
        {
            return 0;
        }

        auto pen_x = 0.0f;
        for (uint32_t i = 0; i < static_cast<uint32_t>(line.size()); i++)
        {
            auto advance = glyph_advance(font_atlas, line[i]);
            auto split = pen_x + advance * 0.5f;
            if (local_x < split)
            {
                return i;
            }
            pen_x += advance;
        }

        return static_cast<uint32_t>(line.size());
    }

    static auto find_cursor_in_multiline_text(std::wstring_view text, const go::vf2& local_pos, const vk::IFontAtlas* font_atlas) -> uint32_t
    {
        auto line_h = font_atlas ? static_cast<float>(font_atlas->get_line_height()) : 16.0f;
        auto text_x = local_pos[0] - 4.0f;
        auto text_y = local_pos[1] - 4.0f;

        if (text_y < 0.0f)
        {
            text_y = 0.0f;
        }

        auto target_line = static_cast<uint32_t>(std::floor(text_y / std::max(1.0f, line_h)));

        auto line_start = uint32_t{0};
        auto current_line = uint32_t{0};

        for (uint32_t i = 0; i <= static_cast<uint32_t>(text.size()); i++)
        {
            auto is_line_end = i == static_cast<uint32_t>(text.size()) || text[i] == L'\n';
            if (!is_line_end)
            {
                continue;
            }

            if (current_line == target_line)
            {
                auto line = std::wstring_view{text.data() + line_start, i - line_start};
                auto in_line = find_cursor_in_line(line, text_x, font_atlas);
                return line_start + in_line;
            }

            if (i == static_cast<uint32_t>(text.size()))
            {
                return static_cast<uint32_t>(text.size());
            }

            current_line++;
            line_start = i + 1;
        }

        return static_cast<uint32_t>(text.size());
    }

    static auto multiline_cursor_position(std::wstring_view text, uint32_t cursor, const vk::IFontAtlas* font_atlas) -> go::vf2
    {
        auto line_h = font_atlas ? static_cast<float>(font_atlas->get_line_height()) : 16.0f;
        auto x = 0.0f;
        auto y = 0.0f;
        auto end = std::min(cursor, static_cast<uint32_t>(text.size()));

        for (uint32_t i = 0; i < end; i++)
        {
            if (text[i] == L'\n')
            {
                x = 0.0f;
                y += line_h;
            }
            else
            {
                x += glyph_advance(font_atlas, text[i]);
            }
        }

        return {x, y};
    }

    static auto multiline_content_size(std::wstring_view text, const vk::IFontAtlas* font_atlas) -> go::vf2
    {
        auto line_h = font_atlas ? static_cast<float>(font_atlas->get_line_height()) : 16.0f;
        auto cap_h = font_atlas ? static_cast<float>(font_atlas->get_cap_height()) : 12.0f;
        auto line_w = 0.0f;
        auto max_w = 0.0f;
        auto lines = 1u;

        for (auto ch : text)
        {
            if (ch == L'\n')
            {
                max_w = std::max(max_w, line_w);
                line_w = 0.0f;
                lines++;
            }
            else
            {
                line_w += glyph_advance(font_atlas, ch);
            }
        }
        max_w = std::max(max_w, line_w);

        auto content_h = static_cast<float>(lines - 1) * line_h + cap_h;
        return {max_w, content_h};
    }

    static auto centered_single_line_text_pos(
        const go::vf2& rect_pos,
        const go::vf2& rect_size,
        std::wstring_view text,
        const vk::IFontAtlas* font_atlas
    ) -> go::vf2
    {
        auto font_h = font_atlas ? static_cast<float>(font_atlas->get_cap_height()) : 12.0f;
        auto text_w = font_atlas ? static_cast<float>(font_atlas->calculate_line_width(text)) : 0.0f;
        auto text_x = std::floor(rect_pos[0] + (rect_size[0] - text_w) * 0.5f);
        auto text_y = std::floor(rect_pos[1] + (rect_size[1] + font_h) * 0.5f);
        return {text_x, text_y};
    }

    static constexpr auto k_panel_scrollbar_width = 4.0f;
    static constexpr auto k_panel_scrollbar_min_thumb_height = 12.0f;
    static constexpr auto k_text_edit_padding = 4.0f;

    // ===== UIElement =====

    UIElement::UIElement()
        : m_id(next_element_id())
    {
    }

    // ===== UILabel =====

    auto UILabel::on_event(UIEvent&) -> bool
    {
        return false;
    }

    auto UILabel::draw(DrawContext& ctx) -> void
    {
        if (!visible) return;
        auto* text_buf = effective_text_buffer(*this, ctx);
        auto* font_atlas = effective_font_atlas(*this, ctx);
        if (!text_buf || !font_atlas) return;

        auto abs_pos = ctx.origin + position;
        auto font_h = static_cast<float>(font_atlas->get_x_height());
        auto text_w = static_cast<float>(font_atlas->calculate_line_width(text));
        auto text_x = abs_pos[0];
        if (align == UILabelAlign::Center)
        {
            text_x = std::floor(abs_pos[0] + (size[0] - text_w) * 0.5f);
        }
        else if (align == UILabelAlign::Right)
        {
            text_x = std::floor(abs_pos[0] + size[0] - text_w);
        }
        auto text_y = std::floor(abs_pos[1] + (size[1] + font_h) * 0.5f); // baseline: center x-height inside element
        text_buf->add(text, {text_x, text_y}, color);
    }

    // ===== UIButton =====

    auto UIButton::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::MouseMove)
        {
            m_hovered = inside;
            return false;
        }

        if (ev.type == EventType::MousePress && ev.button == 0 && inside)
        {
            m_pressed = true;
            ev.consumed = true;
            return true;
        }

        if (ev.type == EventType::MouseRelease && ev.button == 0)
        {
            if (m_pressed && inside && on_click)
                on_click();
            m_pressed = false;
            return inside;
        }

        return false;
    }

    auto UIButton::draw(DrawContext& ctx) -> void
    {
        if (!visible) return;
        auto* text_buf = effective_text_buffer(*this, ctx);
        auto* font_atlas = effective_font_atlas(*this, ctx);
        auto abs_pos = ctx.origin + position;

        auto& col = m_pressed ? color_pressed : (m_hovered ? color_hover : color_normal);
        ctx.geo.add_rect(abs_pos, size, col);
        ctx.wire.add_rect(abs_pos, size, go::vu4{100, 100, 130, 255});

        auto text_pos = centered_single_line_text_pos(abs_pos, size, label, font_atlas);
        if (text_buf)
        {
            text_buf->add(label, text_pos, text_color);
        }
    }

    // ===== UITextInput =====

    auto UITextInput::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        m_cursor = std::min(m_cursor, static_cast<uint32_t>(text.size()));
        m_scroll_x = std::max(0.0f, m_scroll_x);

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::MousePress && ev.button == 0)
        {
            m_focused = inside;
            if (inside)
            {
                auto x = local[0] - 4.0f + m_scroll_x;
                m_cursor = find_cursor_in_line(text, x, nullptr);
                ev.consumed = true;
                return true;
            }
            return false;
        }

        if (!m_focused) return false;

        if (ev.type == EventType::TextInput)
        {
            text.insert(text.begin() + m_cursor, static_cast<wchar_t>(ev.codepoint));
            m_cursor++;
            if (on_change) on_change(text);
            ev.consumed = true;
            return true;
        }

        if (ev.type == EventType::KeyPress)
        {
            // GLFW key codes
            constexpr int KEY_BACKSPACE = 259;
            constexpr int KEY_DELETE = 261;
            constexpr int KEY_LEFT = 263;
            constexpr int KEY_RIGHT = 262;
            constexpr int KEY_HOME = 268;
            constexpr int KEY_END = 269;

            if (ev.key == KEY_BACKSPACE && m_cursor > 0)
            {
                m_cursor--;
                text.erase(m_cursor, 1);
                if (on_change) on_change(text);
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_DELETE && m_cursor < text.size())
            {
                text.erase(m_cursor, 1);
                if (on_change) on_change(text);
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_LEFT && m_cursor > 0)
            {
                m_cursor--;
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_RIGHT && m_cursor < text.size())
            {
                m_cursor++;
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_HOME)
            {
                m_cursor = 0;
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_END)
            {
                m_cursor = static_cast<uint32_t>(text.size());
                ev.consumed = true;
                return true;
            }
        }

        return false;
    }

    auto UITextInput::draw(DrawContext& ctx) -> void
    {
        if (!visible) return;
        m_cursor = std::min(m_cursor, static_cast<uint32_t>(text.size()));
        auto* font_atlas = effective_font_atlas(*this, ctx);
        auto font = effective_font(*this, ctx);
        auto abs_pos = ctx.origin + position;

        ctx.geo.add_rect(abs_pos, size, bg_color);
        ctx.wire.add_rect(abs_pos, size, m_focused ? go::vu4{120, 120, 200, 255} : border_color);

        auto cap_h = font_atlas ? static_cast<float>(font_atlas->get_cap_height()) : 12.0f;
        auto inner_w = std::max(0.0f, size[0] - k_text_edit_padding * 2.0f);
        auto cursor_px = measure_prefix_width(text, m_cursor, font_atlas);
        auto text_w = measure_prefix_width(text, static_cast<uint32_t>(text.size()), font_atlas);
        auto max_scroll_x = std::max(0.0f, text_w - inner_w);

        auto cursor_margin = 2.0f;
        auto min_visible_x = cursor_margin;
        auto max_visible_x = std::max(min_visible_x, inner_w - cursor_margin - 2.0f);
        auto cursor_view_x = cursor_px - m_scroll_x;
        if (cursor_view_x > max_visible_x)
        {
            m_scroll_x = cursor_px - max_visible_x;
        }
        else if (cursor_view_x < min_visible_x)
        {
            m_scroll_x = cursor_px - min_visible_x;
        }
        m_scroll_x = std::clamp(m_scroll_x, 0.0f, max_scroll_x);

        auto text_x = abs_pos[0] + k_text_edit_padding - m_scroll_x;
        auto text_y = std::floor(abs_pos[1] + (size[1] + cap_h) * 0.5f);

        auto text_drawn = false;
        if (ctx.resolve_clipped_text_buffer && font.ptr)
        {
            auto content_pos = abs_pos;
            auto content_size = size;

            auto content_fb_pos = content_pos + ctx.clip_pos;
            auto [clip_pos, clip_size] = clip_rect(content_fb_pos, content_size, ctx.clip_pos, ctx.clip_size);
            if (clip_size[0] > 0.0f && clip_size[1] > 0.0f)
            {
                auto scissor = to_rect(clip_pos, clip_size);
                if (auto* clipped_text = ctx.resolve_clipped_text_buffer(font, scissor); clipped_text)
                {
                    clipped_text->add(text, {text_x, text_y}, text_color);
                    text_drawn = true;
                }
            }
        }

        if (!text_drawn)
        {
            if (auto* text_buf = effective_text_buffer(*this, ctx); text_buf)
            {
                text_buf->add(text, {text_x, text_y}, text_color);
            }
        }

        if (m_focused)
        {
            auto cursor_x = text_x + measure_prefix_width(text, m_cursor, font_atlas);
            ctx.geo.add_rect(
                go::vf2{cursor_x, text_y - cap_h},
                go::vf2{2.0f, cap_h},
                cursor_color
            );
        }
    }

    // ===== UITextArea =====

    UITextArea::UITextArea()
    {
        size = {200.0f, 100.0f};
    }

    auto UITextArea::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        m_cursor = std::min(m_cursor, static_cast<uint32_t>(text.size()));
        m_scroll_x = std::max(0.0f, m_scroll_x);
        m_scroll_y = std::max(0.0f, m_scroll_y);

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::Scroll && inside)
        {
            m_scroll_x = std::max(0.0f, m_scroll_x - ev.scroll_dx * 20.0f);
            m_scroll_y = std::max(0.0f, m_scroll_y - ev.scroll_dy * 20.0f);
            ev.consumed = true;
            return true;
        }

        if (ev.type == EventType::MousePress && ev.button == 0)
        {
            m_focused = inside;
            if (inside)
            {
                auto content_local = go::vf2{local[0] + m_scroll_x, local[1] + m_scroll_y};
                m_cursor = find_cursor_in_multiline_text(text, content_local, nullptr);
                ev.consumed = true;
                return true;
            }
            return false;
        }

        if (!m_focused) return false;

        if (ev.type == EventType::TextInput)
        {
            text.insert(text.begin() + m_cursor, static_cast<wchar_t>(ev.codepoint));
            m_cursor++;
            if (on_change) on_change(text);
            ev.consumed = true;
            return true;
        }

        if (ev.type == EventType::KeyPress)
        {
            constexpr int KEY_BACKSPACE = 259;
            constexpr int KEY_DELETE = 261;
            constexpr int KEY_LEFT = 263;
            constexpr int KEY_RIGHT = 262;
            constexpr int KEY_ENTER = 257;
            constexpr int KEY_HOME = 268;
            constexpr int KEY_END = 269;

            if (ev.key == KEY_ENTER)
            {
                text.insert(text.begin() + m_cursor, L'\n');
                m_cursor++;
                if (on_change) on_change(text);
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_BACKSPACE && m_cursor > 0)
            {
                m_cursor--;
                text.erase(m_cursor, 1);
                if (on_change) on_change(text);
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_DELETE && m_cursor < text.size())
            {
                text.erase(m_cursor, 1);
                if (on_change) on_change(text);
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_LEFT && m_cursor > 0)
            {
                m_cursor--;
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_RIGHT && m_cursor < text.size())
            {
                m_cursor++;
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_HOME)
            {
                m_cursor = 0;
                ev.consumed = true;
                return true;
            }
            if (ev.key == KEY_END)
            {
                m_cursor = static_cast<uint32_t>(text.size());
                ev.consumed = true;
                return true;
            }
        }

        return false;
    }

    auto UITextArea::draw(DrawContext& ctx) -> void
    {
        if (!visible) return;
        m_cursor = std::min(m_cursor, static_cast<uint32_t>(text.size()));
        m_scroll_x = std::max(0.0f, m_scroll_x);
        m_scroll_y = std::max(0.0f, m_scroll_y);
        auto* font_atlas = effective_font_atlas(*this, ctx);
        auto font = effective_font(*this, ctx);
        auto abs_pos = ctx.origin + position;

        ctx.geo.add_rect(abs_pos, size, bg_color);
        ctx.wire.add_rect(abs_pos, size, m_focused ? go::vu4{120, 120, 200, 255} : border_color);

        auto font_h = font_atlas ? static_cast<float>(font_atlas->get_x_height()) : 12.0f;
        auto cap_h = font_atlas ? static_cast<float>(font_atlas->get_cap_height()) : 12.0f;
        auto line_h = font_atlas ? static_cast<float>(font_atlas->get_line_height()) : 16.0f;
        auto inner_w = std::max(0.0f, size[0] - k_text_edit_padding * 2.0f);
        auto inner_h = std::max(0.0f, size[1] - k_text_edit_padding * 2.0f);

        auto cursor_pos = multiline_cursor_position(text, m_cursor, font_atlas);
        auto content_size = multiline_content_size(text, font_atlas);

        auto max_scroll_x = std::max(0.0f, content_size[0] - inner_w);
        auto max_scroll_y = std::max(0.0f, content_size[1] - inner_h);

        auto cursor_margin = 2.0f;
        auto min_visible_x = cursor_margin;
        auto max_visible_x = std::max(min_visible_x, inner_w - cursor_margin - 2.0f);
        auto cursor_view_x = cursor_pos[0] - m_scroll_x;
        if (cursor_view_x > max_visible_x)
        {
            m_scroll_x = cursor_pos[0] - max_visible_x;
        }
        else if (cursor_view_x < min_visible_x)
        {
            m_scroll_x = cursor_pos[0] - min_visible_x;
        }

        auto min_visible_y = cursor_margin;
        auto max_visible_y = std::max(min_visible_y, inner_h - cursor_margin - cap_h);
        auto cursor_view_y = cursor_pos[1] - m_scroll_y;
        if (cursor_view_y > max_visible_y)
        {
            m_scroll_y = cursor_pos[1] - max_visible_y;
        }
        else if (cursor_view_y < min_visible_y)
        {
            m_scroll_y = cursor_pos[1] - min_visible_y;
        }

        m_scroll_x = std::clamp(m_scroll_x, 0.0f, max_scroll_x);
        m_scroll_y = std::clamp(m_scroll_y, 0.0f, max_scroll_y);

        auto text_x = abs_pos[0] + k_text_edit_padding - m_scroll_x;
        // baseline for first line: top padding + x-height so that glyphs sit within padding
        auto text_y = std::floor(abs_pos[1] + k_text_edit_padding + font_h - m_scroll_y);

        auto text_drawn = false;
        if (ctx.resolve_clipped_text_buffer && font.ptr)
        {
            auto content_pos = abs_pos;
            auto content_size = size;

            auto content_fb_pos = content_pos + ctx.clip_pos;
            auto [clip_pos, clip_size] = clip_rect(content_fb_pos, content_size, ctx.clip_pos, ctx.clip_size);
            if (clip_size[0] > 0.0f && clip_size[1] > 0.0f)
            {
                auto scissor = to_rect(clip_pos, clip_size);
                if (auto* clipped_text = ctx.resolve_clipped_text_buffer(font, scissor); clipped_text)
                {
                    clipped_text->add(text, {text_x, text_y}, text_color);
                    text_drawn = true;
                }
            }
        }

        if (!text_drawn)
        {
            if (auto* text_buf = effective_text_buffer(*this, ctx); text_buf)
            {
                text_buf->add(text, {text_x, text_y}, text_color);
            }
        }

        if (m_focused)
        {
            auto cursor_x = text_x + cursor_pos[0];
            auto cursor_baseline = text_y + cursor_pos[1];

            ctx.geo.add_rect(
                go::vf2{cursor_x, cursor_baseline - cap_h},
                go::vf2{2.0f, cap_h},
                cursor_color
            );
        }
    }

    // ===== UISlider =====

    auto UISlider::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::MousePress && ev.button == 0 && inside)
        {
            m_dragging = true;
            auto ratio = std::clamp(local[0] / size[0], 0.0f, 1.0f);
            value = min_val + ratio * (max_val - min_val);
            if (on_change) on_change(value);
            ev.consumed = true;
            return true;
        }

        if (ev.type == EventType::MouseRelease && ev.button == 0)
        {
            m_dragging = false;
            return false;
        }

        if (ev.type == EventType::MouseMove && m_dragging)
        {
            auto ratio = std::clamp(local[0] / size[0], 0.0f, 1.0f);
            value = min_val + ratio * (max_val - min_val);
            if (on_change) on_change(value);
            ev.consumed = true;
            return true;
        }

        return false;
    }

    auto UISlider::draw(DrawContext& ctx) -> void
    {
        if (!visible) return;
        auto abs_pos = ctx.origin + position;

        auto track_height = 6.0f;
        auto track_y = abs_pos[1] + (size[1] - track_height) * 0.5f;
        ctx.geo.add_rect(go::vf2{abs_pos[0], track_y}, go::vf2{size[0], track_height}, track_color);

        auto range = max_val - min_val;
        auto ratio = range > 0.0f ? (value - min_val) / range : 0.0f;
        auto handle_w = 12.0f;
        auto handle_x = abs_pos[0] + ratio * (size[0] - handle_w);
        ctx.geo.add_rect(go::vf2{handle_x, abs_pos[1]}, go::vf2{handle_w, size[1]}, handle_color);
        ctx.wire.add_rect(go::vf2{handle_x, abs_pos[1]}, go::vf2{handle_w, size[1]}, go::vu4{140, 140, 200, 255});
    }

    // ===== UICheckbox =====

    UICheckbox::UICheckbox()
    {
        size = {20.0f, 20.0f};
    }

    auto UICheckbox::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto box_size = go::vf2{size[1], size[1]};
        auto inside = point_in_rect(local, {0, 0}, box_size);

        if (ev.type == EventType::MousePress && ev.button == 0 && inside)
        {
            checked = !checked;
            if (on_change) on_change(checked);
            ev.consumed = true;
            return true;
        }

        return false;
    }

    auto UICheckbox::draw(DrawContext& ctx) -> void
    {
        if (!visible) return;
        auto* text_buf = effective_text_buffer(*this, ctx);
        auto* font_atlas = effective_font_atlas(*this, ctx);
        auto abs_pos = ctx.origin + position;
        auto box_sz = go::vf2{size[1], size[1]};

        ctx.geo.add_rect(abs_pos, box_sz, box_color);
        ctx.wire.add_rect(abs_pos, box_sz, go::vu4{100, 100, 130, 255});

        if (checked)
        {
            auto inset = 4.0f;
            ctx.geo.add_rect(
                go::vf2{abs_pos[0] + inset, abs_pos[1] + inset},
                go::vf2{box_sz[0] - inset * 2.0f, box_sz[1] - inset * 2.0f},
                check_color
            );
        }

        if (!label.empty())
        {
            auto font_h = font_atlas ? static_cast<float>(font_atlas->get_x_height()) : 12.0f;
            auto text_x = abs_pos[0] + box_sz[0] + 6.0f;
            auto text_y = std::floor(abs_pos[1] + (box_sz[1] + font_h) * 0.5f); // baseline centered using x-height
            if (text_buf)
            {
                text_buf->add(label, {text_x, text_y}, text_color);
            }
        }
    }

    // ===== UIDropdown =====

    auto UIDropdown::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside_header = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::MouseMove && m_open)
        {
            m_hovered_item = -1;
            auto item_height = size[1];
            for (int i = 0; i < static_cast<int>(items.size()); i++)
            {
                auto item_y = size[1] + static_cast<float>(i) * item_height;
                if (point_in_rect(local, {0, item_y}, {size[0], item_height}))
                {
                    m_hovered_item = i;
                    break;
                }
            }
            return true;
        }

        if (ev.type == EventType::MousePress && ev.button == 0)
        {
            if (m_open)
            {
                if (m_hovered_item >= 0)
                {
                    selected = m_hovered_item;
                    if (on_change) on_change(selected);
                }
                m_open = false;
                ev.consumed = true;
                return true;
            }
            else if (inside_header)
            {
                m_open = true;
                ev.consumed = true;
                return true;
            }
            else
            {
                m_open = false;
            }
        }

        return false;
    }

    auto UIDropdown::draw(DrawContext& ctx) -> void
    {
        if (!visible) return;
        auto* text_buf = effective_text_buffer(*this, ctx);
        auto* font_atlas = effective_font_atlas(*this, ctx);
        auto abs_pos = ctx.origin + position;

        ctx.geo.add_rect(abs_pos, size, bg_color);
        if (!m_open)
        {
            ctx.wire.add_rect(abs_pos, size, go::vu4{100, 100, 130, 255});
        }

        if (selected >= 0 && selected < static_cast<int>(items.size()))
        {
            auto text_pos = centered_single_line_text_pos(abs_pos, size, items[selected], font_atlas);
            if (text_buf)
            {
                text_buf->add(items[selected], text_pos, text_color);
            }
        }

        auto arrow_center = go::vf2{abs_pos[0] + size[0] - 12.0f, abs_pos[1] + size[1] * 0.5f};
        auto side = std::max(6.0f, std::min(10.0f, size[1] * 0.45f));
        auto half_w = side * 0.5f;
        auto half_h = side * 0.5f * std::sqrt(3.0f) * 0.5f;

        if (m_open)
        {
            auto p0 = go::vf2{arrow_center[0] - half_w, arrow_center[1] + half_h};
            auto p1 = go::vf2{arrow_center[0] + half_w, arrow_center[1] + half_h};
            auto p2 = go::vf2{arrow_center[0], arrow_center[1] - half_h};
            ctx.geo.add_triangle(p0, p1, p2, text_color);
        }
        else
        {
            auto p0 = go::vf2{arrow_center[0] - half_w, arrow_center[1] - half_h};
            auto p1 = go::vf2{arrow_center[0] + half_w, arrow_center[1] - half_h};
            auto p2 = go::vf2{arrow_center[0], arrow_center[1] + half_h};
            ctx.geo.add_triangle(p0, p1, p2, text_color);
        }

        if (m_open)
        {
            auto item_height = size[1];
            for (int i = 0; i < static_cast<int>(items.size()); i++)
            {
                auto iy = abs_pos[1] + size[1] + static_cast<float>(i) * item_height;
                auto& col = (i == m_hovered_item) ? hover_color : bg_color;
                ctx.geo.add_rect(go::vf2{abs_pos[0], iy}, go::vf2{size[0], item_height}, col);
                auto item_pos = centered_single_line_text_pos(
                    go::vf2{abs_pos[0], iy},
                    go::vf2{size[0], item_height},
                    items[i],
                    font_atlas
                );
                if (text_buf)
                {
                    text_buf->add(items[i], item_pos, text_color);
                }
            }

            auto open_size = go::vf2{size[0], size[1] * (static_cast<float>(items.size()) + 1.0f)};
            ctx.wire.add_rect(abs_pos, open_size, go::vu4{100, 100, 130, 255});
        }
    }

    // ===== UIPanel =====

    auto UIPanel::add(std::unique_ptr<UIElement> element) -> UIElement*
    {
        auto ptr = element.get();
        m_children.push_back(std::move(element));
        return ptr;
    }

    auto UIPanel::apply_layout() -> void
    {
        if (layout == Layout::Free)
            return;

        auto cursor = go::vf2{padding, padding};

        for (auto& child : m_children)
        {
            if (!child->visible) continue;

            child->position = cursor;

            if (layout == Layout::Horizontal)
            {
                cursor[0] += child->size[0] + spacing;
            }
            else
            {
                cursor[1] += child->size[1] + spacing;
            }
        }
    }

    auto UIPanel::content_height() const -> float
    {
        auto max_bottom = padding;
        for (const auto& child : m_children)
        {
            if (!child->visible)
            {
                continue;
            }
            max_bottom = std::max(max_bottom, child->position[1] + child->size[1]);
        }
        return max_bottom + padding;
    }

    auto UIPanel::max_scroll_y() const -> float
    {
        if (!scrollable)
        {
            return 0.0f;
        }
        return std::max(0.0f, content_height() - size[1]);
    }

    auto UIPanel::clamp_scroll() -> void
    {
        auto max_scroll = max_scroll_y();
        scroll_offset[0] = 0.0f;
        scroll_offset[1] = std::clamp(scroll_offset[1], -max_scroll, 0.0f);
    }

    auto UIPanel::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        apply_layout();
        clamp_scroll();

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::Scroll && scrollable && inside)
        {
            scroll_offset[1] += ev.scroll_dy * 20.0f;
            clamp_scroll();
            ev.consumed = true;
            return true;
        }

        // Translate mouse into children space (accounting for scroll)
        auto child_ev = ev;
        child_ev.mouse_pos = go::vf2{
            local[0] - scroll_offset[0],
            local[1] - scroll_offset[1]
        };

        auto is_pointer_event =
            ev.type == EventType::MouseMove ||
            ev.type == EventType::MousePress ||
            ev.type == EventType::MouseRelease ||
            ev.type == EventType::Scroll;

        if (is_pointer_event)
        {
            for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
            {
                if (!(*it)->visible)
                {
                    continue;
                }

                auto* dropdown = dynamic_cast<UIDropdown*>(it->get());
                if (!dropdown || !dropdown->is_open())
                {
                    continue;
                }

                if ((*it)->on_event(child_ev))
                {
                    ev.consumed = child_ev.consumed;
                    return true;
                }
            }
        }

        // Propagate in reverse order (top-most child = last in list gets first chance)
        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        {
            if ((*it)->on_event(child_ev))
            {
                ev.consumed = child_ev.consumed;
                return true;
            }
        }

        // Panel itself consumes clicks inside its bounds
        if (inside && (ev.type == EventType::MousePress || ev.type == EventType::MouseRelease))
        {
            ev.consumed = true;
            return true;
        }

        return false;
    }

    auto UIPanel::draw(DrawContext& ctx) -> void
    {
        if (!visible) return;

        apply_layout();
        clamp_scroll();

        auto abs_pos = ctx.origin + position;

        // Save parent clip
        auto parent_clip_pos = ctx.clip_pos;
        auto parent_clip_size = ctx.clip_size;

        // Compute clipped region as intersection of parent clip and this panel
        auto [new_clip_pos, new_clip_size] = clip_rect(abs_pos, size, parent_clip_pos, parent_clip_size);

        if (new_clip_size[0] <= 0.0f || new_clip_size[1] <= 0.0f)
            return;

        // Draw background
        if (draw_background || bg_color[3] > 0)
        {
            ctx.geo.add_rect(abs_pos, size, bg_color);
        }

        if (draw_border && border_color[3] > 0)
        {
            ctx.wire.add_rect(abs_pos, size, border_color);
        }

        // Set clip for children
        ctx.clip_pos = new_clip_pos;
        ctx.clip_size = new_clip_size;

        // Draw children with scroll offset applied to origin
        auto child_origin = go::vf2{abs_pos[0] + scroll_offset[0], abs_pos[1] + scroll_offset[1]};
        auto saved_origin = ctx.origin;
        ctx.origin = child_origin;

        for (auto& child : m_children)
        {
            child->draw(ctx);
        }

        if (scrollable)
        {
            auto max_scroll = max_scroll_y();
            if (max_scroll > 0.0f)
            {
                auto content_h = content_height();
                auto thumb_h = std::max(k_panel_scrollbar_min_thumb_height, size[1] * (size[1] / content_h));
                auto travel = std::max(0.0f, size[1] - thumb_h);
                auto t = max_scroll > 0.0f ? (-scroll_offset[1] / max_scroll) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                auto thumb_y = abs_pos[1] + travel * t;
                auto thumb_x = abs_pos[0] + size[0] - k_panel_scrollbar_width;

                ctx.geo.add_rect(
                    go::vf2{thumb_x, thumb_y},
                    go::vf2{k_panel_scrollbar_width, thumb_h},
                    go::vu4{150, 150, 190, 190}
                );
            }
        }

        // Restore
        ctx.origin = saved_origin;
        ctx.clip_pos = parent_clip_pos;
        ctx.clip_size = parent_clip_size;
    }

    auto UISystem::panel_count(const UIPanel& panel) const -> size_t
    {
        auto count = size_t{1};
        for (const auto& child : panel.children())
        {
            if (!child->visible) continue;
            if (auto* child_panel = dynamic_cast<const UIPanel*>(child.get()))
            {
                count += panel_count(*child_panel);
            }
        }
        return count;
    }

    auto UISystem::ensure_panel_buffers(size_t count) -> bool
    {
        if (m_panel_buffers.size() >= count)
        {
            return true;
        }

        while (m_panel_buffers.size() < count)
        {
            auto geo_res = vk::GeometryBuffer2D::create();
            if (!geo_res)
            {
                std::println("[ERROR] UISystem: failed to create panel GeometryBuffer2D");
                return false;
            }

            auto wire_res = vk::GeometryBuffer2DWire::create();
            if (!wire_res)
            {
                std::println("[ERROR] UISystem: failed to create panel GeometryBuffer2DWire");
                return false;
            }

            auto dropdown_geo_res = vk::GeometryBuffer2D::create();
            if (!dropdown_geo_res)
            {
                std::println("[ERROR] UISystem: failed to create panel dropdown GeometryBuffer2D");
                return false;
            }

            auto dropdown_wire_res = vk::GeometryBuffer2DWire::create();
            if (!dropdown_wire_res)
            {
                std::println("[ERROR] UISystem: failed to create panel dropdown GeometryBuffer2DWire");
                return false;
            }

            auto scrollbar_geo_res = vk::GeometryBuffer2D::create();
            if (!scrollbar_geo_res)
            {
                std::println("[ERROR] UISystem: failed to create panel scrollbar GeometryBuffer2D");
                return false;
            }

            m_panel_buffers.push_back(
                PanelDrawBuffers
                {
                    .geo = std::move(geo_res.value()),
                    .wire = std::move(wire_res.value()),
                    .dropdown_geo = std::move(dropdown_geo_res.value()),
                    .dropdown_wire = std::move(dropdown_wire_res.value()),
                    .scrollbar_geo = std::move(scrollbar_geo_res.value())
                }
            );
        }

        return true;
    }

    auto UISystem::ensure_panel_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font) -> vk::TextBuffer*
    {
        if (!font.ptr)
        {
            return nullptr;
        }

        auto font_index = static_cast<size_t>(font.font_index);
        if (panel_buf.text.size() <= font_index)
        {
            panel_buf.text.resize(font_index + 1);
        }

        auto& text_entry = panel_buf.text[font_index];
        if (!text_entry.has_value())
        {
            auto text_res = vk::TextBuffer::create(font);
            if (!text_res)
            {
                std::println("[ERROR] UISystem: failed to create panel TextBuffer for font {}", font.font_index);
                return nullptr;
            }
            text_entry = std::move(text_res.value());
        }

        return &text_entry.value();
    }

    auto UISystem::ensure_panel_clipped_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font, const VkRect2D& scissor) -> vk::TextBuffer*
    {
        if (!font.ptr)
        {
            return nullptr;
        }

        auto entry_id = panel_buf.clipped_text_used++;
        if (panel_buf.clipped_text.size() <= entry_id)
        {
            panel_buf.clipped_text.emplace_back();
        }

        auto& entry = panel_buf.clipped_text[entry_id];
        entry.scissor = scissor;

        if (!entry.text.has_value() || entry.text->font_index() != font.font_index)
        {
            auto text_res = vk::TextBuffer::create(font);
            if (!text_res)
            {
                std::println("[ERROR] UISystem: failed to create clipped TextBuffer for font {}", font.font_index);
                return nullptr;
            }
            entry.text = std::move(text_res.value());
        }

        return &entry.text.value();
    }

    auto UISystem::ensure_panel_dropdown_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font) -> vk::TextBuffer*
    {
        if (!font.ptr)
        {
            return nullptr;
        }

        auto font_index = static_cast<size_t>(font.font_index);
        if (panel_buf.dropdown_text.size() <= font_index)
        {
            panel_buf.dropdown_text.resize(font_index + 1);
        }

        auto& text_entry = panel_buf.dropdown_text[font_index];
        if (!text_entry.has_value())
        {
            auto text_res = vk::TextBuffer::create(font);
            if (!text_res)
            {
                std::println("[ERROR] UISystem: failed to create panel dropdown TextBuffer for font {}", font.font_index);
                return nullptr;
            }
            text_entry = std::move(text_res.value());
        }

        return &text_entry.value();
    }

    auto UISystem::build_panel_buffers(UIPanel& panel, const go::vf2& panel_abs_pos, const VkRect2D& parent_clip, size_t& panel_id) -> void
    {
        panel.apply_layout();
        panel.clamp_scroll();

        auto panel_rect = to_rect(panel_abs_pos, panel.size);
        auto panel_clip = rect_intersection(parent_clip, panel_rect);

        auto& panel_buf = m_panel_buffers[panel_id++];
        panel_buf.view = panel_clip;
        panel_buf.geo.clear();
        panel_buf.wire.clear();
        panel_buf.clipped_text_used = 0;
        panel_buf.dropdown_geo.clear();
        panel_buf.dropdown_wire.clear();
        panel_buf.scrollbar_geo.clear();
        for (auto& text_buf : panel_buf.text)
        {
            if (text_buf.has_value())
            {
                text_buf->clear();
            }
        }
        for (auto& text_buf : panel_buf.dropdown_text)
        {
            if (text_buf.has_value())
            {
                text_buf->clear();
            }
        }
        for (auto& clipped : panel_buf.clipped_text)
        {
            if (clipped.text.has_value())
            {
                clipped.text->clear();
            }
        }

        if (panel_clip.extent.width == 0 || panel_clip.extent.height == 0)
        {
            return;
        }

        auto clip_offset = go::vf2{static_cast<float>(panel_clip.offset.x), static_cast<float>(panel_clip.offset.y)};
        auto child_origin = panel_abs_pos + panel.scroll_offset;

        DrawContext panel_ctx
        {
            .geo = panel_buf.geo,
            .wire = panel_buf.wire,
            .resolve_text_buffer = [this, &panel_buf](vk::FontId font) -> vk::TextBuffer*
            {
                return ensure_panel_text_buffer(panel_buf, font);
            },
            .resolve_clipped_text_buffer = [this, &panel_buf](vk::FontId font, const VkRect2D& scissor) -> vk::TextBuffer*
            {
                return ensure_panel_clipped_text_buffer(panel_buf, font, scissor);
            },
            .default_font = m_font,
            .origin = child_origin - clip_offset,
            .clip_pos = clip_offset,
            .clip_size = {static_cast<float>(panel_clip.extent.width), static_cast<float>(panel_clip.extent.height)}
        };

        DrawContext dropdown_ctx
        {
            .geo = panel_buf.dropdown_geo,
            .wire = panel_buf.dropdown_wire,
            .resolve_text_buffer = [this, &panel_buf](vk::FontId font) -> vk::TextBuffer*
            {
                return ensure_panel_dropdown_text_buffer(panel_buf, font);
            },
            .resolve_clipped_text_buffer = [](vk::FontId, const VkRect2D&) -> vk::TextBuffer*
            {
                return nullptr;
            },
            .default_font = m_font,
            .origin = child_origin - clip_offset,
            .clip_pos = clip_offset,
            .clip_size = {static_cast<float>(panel_clip.extent.width), static_cast<float>(panel_clip.extent.height)}
        };

        if (panel.draw_background || panel.bg_color[3] > 0)
        {
            panel_buf.geo.add_rect(panel_abs_pos - clip_offset, panel.size, panel.bg_color);
        }

        if (panel.draw_border && panel.border_color[3] > 0)
        {
            panel_buf.wire.add_rect(panel_abs_pos - clip_offset, panel.size, panel.border_color);
        }

        for (auto& child : panel.children())
        {
            if (!child->visible) continue;

            if (auto* child_panel = dynamic_cast<UIPanel*>(child.get()))
            {
                auto child_panel_abs = child_origin + child_panel->position;
                build_panel_buffers(*child_panel, child_panel_abs, panel_clip, panel_id);
            }
            else
            {
                if (dynamic_cast<UIDropdown*>(child.get()))
                {
                    continue;
                }
                child->draw(panel_ctx);
            }
        }

        for (auto& child : panel.children())
        {
            if (!child->visible) continue;

            if (dynamic_cast<UIDropdown*>(child.get()))
            {
                child->draw(dropdown_ctx);
            }
        }

        if (panel.scrollable)
        {
            auto max_scroll = panel.max_scroll_y();
            if (max_scroll > 0.0f)
            {
                auto content_h = panel.content_height();
                auto thumb_h = std::max(k_panel_scrollbar_min_thumb_height, panel.size[1] * (panel.size[1] / content_h));
                auto travel = std::max(0.0f, panel.size[1] - thumb_h);
                auto t = max_scroll > 0.0f ? (-panel.scroll_offset[1] / max_scroll) : 0.0f;
                t = std::clamp(t, 0.0f, 1.0f);
                auto thumb_y = panel_abs_pos[1] + travel * t;
                auto thumb_x = panel_abs_pos[0] + panel.size[0] - k_panel_scrollbar_width;

                panel_buf.scrollbar_geo.add_rect(
                    go::vf2{thumb_x, thumb_y} - clip_offset,
                    go::vf2{k_panel_scrollbar_width, thumb_h},
                    go::vu4{150, 150, 190, 190}
                );
            }
        }
    }

    // ===== UISystem =====

    auto UISystem::init(vk::FontId font) -> bool
    {
        m_font = font;
        if (!ensure_panel_buffers(1))
        {
            return false;
        }
        m_initialized = true;
        return true;
    }

    auto UISystem::process_mouse_move(float x, float y) -> void
    {
        m_mouse_pos = {x, y};
        UIEvent ev
        {
            .type = EventType::MouseMove,
            .mouse_pos = m_mouse_pos
        };
        m_root.on_event(ev);
    }

    auto UISystem::process_mouse_press(int button, int action) -> void
    {
        UIEvent ev
        {
            .type = (action == 1) ? EventType::MousePress : EventType::MouseRelease,
            .mouse_pos = m_mouse_pos,
            .button = button
        };
        m_root.on_event(ev);
    }

    auto UISystem::process_key(int key, int action, int mods) -> void
    {
        if (action == 2) return; // ignore repeats for now... or treat as press
        UIEvent ev
        {
            .type = (action == 1 || action == 2) ? EventType::KeyPress : EventType::KeyRelease,
            .mouse_pos = m_mouse_pos,
            .key = key,
            .mods = mods
        };
        m_root.on_event(ev);
    }

    auto UISystem::process_text_input(unsigned int codepoint) -> void
    {
        UIEvent ev
        {
            .type = EventType::TextInput,
            .mouse_pos = m_mouse_pos,
            .codepoint = codepoint
        };
        m_root.on_event(ev);
    }

    auto UISystem::process_scroll(float dx, float dy) -> void
    {
        UIEvent ev
        {
            .type = EventType::Scroll,
            .mouse_pos = m_mouse_pos,
            .scroll_dx = dx,
            .scroll_dy = dy
        };
        m_root.on_event(ev);
    }

    auto UISystem::sync(VkCommandBuffer cmd, const VkRect2D& viewport) -> void
    {
        if (!m_initialized) return;

        m_root.position = {static_cast<float>(viewport.offset.x), static_cast<float>(viewport.offset.y)};
        m_root.size = {static_cast<float>(viewport.extent.width), static_cast<float>(viewport.extent.height)};

        auto needed_panel_buffers = panel_count(m_root);
        if (!ensure_panel_buffers(needed_panel_buffers))
        {
            return;
        }

        m_used_panel_buffers = 0;
        build_panel_buffers(m_root, m_root.position, viewport, m_used_panel_buffers);

        for (size_t i = 0; i < m_used_panel_buffers; i++)
        {
            auto& panel_buf = m_panel_buffers[i];

            if (panel_buf.geo.index_count() > 0)
            {
                if (auto r = panel_buf.geo.upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(panel_buf.geo.vertex_buffer());
                    vk::add_index_buffer_write_barrier(panel_buf.geo.index_buffer());
                }
            }

            if (panel_buf.wire.index_count() > 0)
            {
                if (auto r = panel_buf.wire.upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(panel_buf.wire.vertex_buffer());
                    vk::add_index_buffer_write_barrier(panel_buf.wire.index_buffer());
                }
            }

            if (panel_buf.dropdown_geo.index_count() > 0)
            {
                if (auto r = panel_buf.dropdown_geo.upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(panel_buf.dropdown_geo.vertex_buffer());
                    vk::add_index_buffer_write_barrier(panel_buf.dropdown_geo.index_buffer());
                }
            }

            if (panel_buf.dropdown_wire.index_count() > 0)
            {
                if (auto r = panel_buf.dropdown_wire.upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(panel_buf.dropdown_wire.vertex_buffer());
                    vk::add_index_buffer_write_barrier(panel_buf.dropdown_wire.index_buffer());
                }
            }

            if (panel_buf.scrollbar_geo.index_count() > 0)
            {
                if (auto r = panel_buf.scrollbar_geo.upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(panel_buf.scrollbar_geo.vertex_buffer());
                    vk::add_index_buffer_write_barrier(panel_buf.scrollbar_geo.index_buffer());
                }
            }

            for (auto& text_buf : panel_buf.text)
            {
                if (!text_buf.has_value() || text_buf->vertex_count() == 0)
                {
                    continue;
                }

                if (auto r = text_buf->upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(text_buf->vertex_buffer());
                }
            }

            for (auto& text_buf : panel_buf.dropdown_text)
            {
                if (!text_buf.has_value() || text_buf->vertex_count() == 0)
                {
                    continue;
                }

                if (auto r = text_buf->upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(text_buf->vertex_buffer());
                }
            }

            for (size_t clipped_id = 0; clipped_id < panel_buf.clipped_text_used; clipped_id++)
            {
                auto& clipped = panel_buf.clipped_text[clipped_id];
                if (!clipped.text.has_value() || clipped.text->vertex_count() == 0)
                {
                    continue;
                }

                if (auto r = clipped.text->upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(clipped.text->vertex_buffer());
                }
            }
        }

        vk::cmd_sync_barriers(cmd);
    }

    auto UISystem::draw(VkCommandBuffer cmd, vk::RenderingOverlay& overlay, const VkRect2D& viewport) -> void
    {
        if (!m_initialized) return;

        for (size_t i = 0; i < m_used_panel_buffers; i++)
        {
            auto& panel_buf = m_panel_buffers[i];
            if (panel_buf.view.extent.width == 0 || panel_buf.view.extent.height == 0) continue;

            vk::view_set(cmd, panel_buf.view);
            vkCmdSetScissor(cmd, 0, 1, &panel_buf.view);

            overlay.start_draw_2d(cmd);
            overlay.draw(cmd, panel_buf.geo, panel_buf.view);

            overlay.start_draw_2d_wire(cmd);
            overlay.draw(cmd, panel_buf.wire, panel_buf.view);

            overlay.start_text_draw(cmd);
            for (const auto& text_buf : panel_buf.text)
            {
                if (!text_buf.has_value() || text_buf->vertex_count() == 0)
                {
                    continue;
                }
                overlay.draw(cmd, text_buf.value(), panel_buf.view);
            }

            for (size_t clipped_id = 0; clipped_id < panel_buf.clipped_text_used; clipped_id++)
            {
                const auto& clipped = panel_buf.clipped_text[clipped_id];
                if (!clipped.text.has_value() || clipped.text->vertex_count() == 0)
                {
                    continue;
                }

                vkCmdSetScissor(cmd, 0, 1, &clipped.scissor);
                overlay.draw(cmd, clipped.text.value(), panel_buf.view);
            }

            vkCmdSetScissor(cmd, 0, 1, &panel_buf.view);

            overlay.start_draw_2d(cmd);
            overlay.draw(cmd, panel_buf.dropdown_geo, panel_buf.view);

            overlay.start_draw_2d_wire(cmd);
            overlay.draw(cmd, panel_buf.dropdown_wire, panel_buf.view);

            overlay.start_text_draw(cmd);
            for (const auto& text_buf : panel_buf.dropdown_text)
            {
                if (!text_buf.has_value() || text_buf->vertex_count() == 0)
                {
                    continue;
                }
                overlay.draw(cmd, text_buf.value(), panel_buf.view);
            }

            vkCmdSetScissor(cmd, 0, 1, &panel_buf.view);

            overlay.start_draw_2d(cmd);
            overlay.draw(cmd, panel_buf.scrollbar_geo, panel_buf.view);

            vkCmdSetScissor(cmd, 0, 1, &panel_buf.view);
        }

        vk::view_set(cmd, viewport);
    }
}
