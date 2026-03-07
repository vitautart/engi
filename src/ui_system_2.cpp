#include "ui_system_2.hpp"
#include "gomath.hpp"
#include "rendering.hpp"
#include "rendering_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <vulkan/vulkan_core.h>

namespace engi::ui2
{
    // ===== Element ID Generator =====

    static uint32_t s_next_id = 1;

    auto next_element_id() -> uint32_t
    {
        return s_next_id++;
    }

    // ===== Geometry Helpers =====

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
        return {{x0, y0}, {std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)}};
    }

    static auto to_rect(const go::vf2& pos, const go::vf2& size) -> VkRect2D
    {
        return {
            .offset = {
                .x = static_cast<int32_t>(std::max(0.0f, pos[0])),
                .y = static_cast<int32_t>(std::max(0.0f, pos[1]))
            },
            .extent = {
                .width  = static_cast<uint32_t>(std::max(0.0f, size[0])),
                .height = static_cast<uint32_t>(std::max(0.0f, size[1]))
            }
        };
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
        return {
            .offset = {.x = x0, .y = y0},
            .extent = {
                .width  = static_cast<uint32_t>(std::max(0, x1 - x0)),
                .height = static_cast<uint32_t>(std::max(0, y1 - y0))
            }
        };
    }

    // ===== Font Helpers =====

    static auto effective_text_buffer(UIElement& el, DrawContext& ctx, size_t passId) -> vk::TextBuffer*
    {
        if (ctx.passes[passId].text.empty())
        {
            // add new text buffer for this font
            auto new_text_buff = vk::TextBuffer::create(el.get_font());
            if (!new_text_buff)
                return nullptr;
            ctx.passes[passId].text.emplace_back(std::move(new_text_buff.value()));
            return &ctx.passes[passId].text.back();
        }

        for(auto& text_buff : ctx.passes[passId].text)
        {
            if (text_buff.font_index() == el.get_font().font_index)
                return &text_buff;
            else
            {
                // add new text buffer for this font
                auto new_text_buff = vk::TextBuffer::create(el.get_font());
                if (!new_text_buff)
                    return nullptr;
                ctx.passes[passId].text.emplace_back(std::move(new_text_buff.value()));
                return &ctx.passes[passId].text.back();
            };
        }
        return nullptr;
    }

    static auto effective_text_buffer(UIElement& el, DrawContext& ctx, size_t passId, VkRect2D rect) -> vk::TextBuffer*
    {
        // add new text buffer for this font
        auto new_text_buff = vk::TextBuffer::create(el.get_font());
        if (!new_text_buff)
            return nullptr;
        ctx.passes[passId].texts.emplace_back(std::move(new_text_buff.value()), rect);
        return &ctx.passes[passId].texts.back().first;
    }

    static auto effective_solid_buffer(DrawContext& ctx, size_t passId) -> vk::GeometryBuffer2D*
    {
        if (ctx.passes[passId].solid.has_value())
        {
            return &ctx.passes[passId].solid.value();
        }
        else
        {
            auto new_solid_buff = vk::GeometryBuffer2D::create();
            if (!new_solid_buff)
                return nullptr;
            ctx.passes[passId].solid = std::move(new_solid_buff.value());
            return &ctx.passes[passId].solid.value();
        }
    }

    static auto effective_solid_buffer(DrawContext& ctx, size_t passId, VkRect2D rect) -> vk::GeometryBuffer2D*
    {
        // add new solid buffer
        auto new_solid_buff = vk::GeometryBuffer2D::create();
        if (!new_solid_buff)
            return nullptr;
        ctx.passes[passId].solids.emplace_back(std::move(new_solid_buff.value()), rect);
        return &ctx.passes[passId].solids.back().first;
    }

    static auto effective_wire_buffer(DrawContext& ctx, size_t passId) -> vk::GeometryBuffer2DWire*
    {
        if (ctx.passes[passId].wire.has_value())
        {
            return &ctx.passes[passId].wire.value();
        }
        else
        {
            auto new_wire_buff = vk::GeometryBuffer2DWire::create();
            if (!new_wire_buff)
                return nullptr;
            ctx.passes[passId].wire = std::move(new_wire_buff.value());
            return &ctx.passes[passId].wire.value();
        }
    }

    static auto effective_wire_buffer(DrawContext& ctx, size_t passId, VkRect2D rect) -> vk::GeometryBuffer2DWire*
    {
        // add new wire buffer
        auto new_wire_buff = vk::GeometryBuffer2DWire::create();
        if (!new_wire_buff)
            return nullptr;
        ctx.passes[passId].wires.emplace_back(std::move(new_wire_buff.value()), rect);
        return &ctx.passes[passId].wires.back().first;
    }

    static auto clear_draw_context(DrawContext& ctx) -> void
    {
        for (auto& pass : ctx.passes)
        {
            if (pass.solid.has_value())
                pass.solid->clear();
            for (auto& text_buff : pass.text)
                text_buff.clear();
            for (auto& text_buff : pass.texts)
                text_buff.first.clear();
            if (pass.wire.has_value())
                pass.wire->clear();
            for (auto& solid_buff : pass.solids)
                solid_buff.first.clear();
            for (auto& wire_buff : pass.wires)
                wire_buff.first.clear();
        }
    }

    static auto upload_draw_context(DrawContext& ctx, VkCommandBuffer cmd) -> void
    {
        for (auto& pass : ctx.passes)
        {
            if (pass.solid.has_value())
            {
                auto res = pass.solid->upload(cmd);
                if (res)
                {
                    vk::add_vertex_buffer_write_barrier(pass.solid->vertex_buffer());
                    vk::add_index_buffer_write_barrier(pass.solid->index_buffer());
                }
            }
            for (auto& text_buff : pass.text)
            {
                auto res = text_buff.upload(cmd);
                if (res)
                {
                    vk::add_vertex_buffer_write_barrier(text_buff.vertex_buffer());
                }
            }
            for (auto& text_buff : pass.texts)
            {
                auto res = text_buff.first.upload(cmd);
                if (res)
                {
                    vk::add_vertex_buffer_write_barrier(text_buff.first.vertex_buffer());
                }
            }
            if (pass.wire.has_value())
            {
                auto res = pass.wire->upload(cmd);
                if (res)
                {
                    vk::add_vertex_buffer_write_barrier(pass.wire->vertex_buffer());
                    vk::add_index_buffer_write_barrier(pass.wire->index_buffer());
                }
            }
            for (auto& solid_buff : pass.solids)
            {
                auto res = solid_buff.first.upload(cmd);
                if (res)
                {
                    vk::add_vertex_buffer_write_barrier(solid_buff.first.vertex_buffer());
                    vk::add_index_buffer_write_barrier(solid_buff.first.index_buffer());
                }
            }
            for (auto& wire_buff : pass.wires)
            {
                auto res = wire_buff.first.upload(cmd);
                if (res)
                {
                    vk::add_vertex_buffer_write_barrier(wire_buff.first.vertex_buffer());
                    vk::add_index_buffer_write_barrier(wire_buff.first.index_buffer());
                }
            }
        }
    }


    // ===== Text Measurement Helpers =====

    static auto glyph_advance(const vk::IFontAtlas* font_atlas, wchar_t ch) -> float
    {
        if (!font_atlas)
            return 10.0f;
        if (auto glyph = font_atlas->get_glyph(static_cast<int>(ch)); glyph)
            return static_cast<float>(glyph->advance);
        auto one = std::wstring_view{&ch, 1};
        return static_cast<float>(font_atlas->calculate_line_width(one));
    }

    static auto measure_prefix_width(std::wstring_view text, uint32_t cursor, const vk::IFontAtlas* font_atlas) -> float
    {
        auto width = 0.0f;
        auto end = std::min(cursor, static_cast<uint32_t>(text.size()));
        for (uint32_t i = 0; i < end; i++)
        {
            if (text[i] == L'\n')
                break;
            width += glyph_advance(font_atlas, text[i]);
        }
        return width;
    }

    static auto find_cursor_in_line(std::wstring_view line, float local_x, const vk::IFontAtlas* font_atlas) -> uint32_t
    {
        if (local_x <= 0.0f)
            return 0;
        auto pen_x = 0.0f;
        for (uint32_t i = 0; i < static_cast<uint32_t>(line.size()); i++)
        {
            auto advance = glyph_advance(font_atlas, line[i]);
            if (local_x < pen_x + advance * 0.5f)
                return i;
            pen_x += advance;
        }
        return static_cast<uint32_t>(line.size());
    }

    static auto find_cursor_in_multiline_text(std::wstring_view text, const go::vf2& local_pos, const vk::IFontAtlas* font_atlas) -> uint32_t
    {
        auto line_h = font_atlas ? static_cast<float>(font_atlas->get_line_height()) : 16.0f;
        auto text_x = local_pos[0] - 4.0f;
        auto text_y = std::max(0.0f, local_pos[1] - 4.0f);
        auto target_line = static_cast<uint32_t>(std::floor(text_y / std::max(1.0f, line_h)));

        auto line_start = uint32_t{0};
        auto current_line = uint32_t{0};
        for (uint32_t i = 0; i <= static_cast<uint32_t>(text.size()); i++)
        {
            auto is_line_end = i == static_cast<uint32_t>(text.size()) || text[i] == L'\n';
            if (!is_line_end)
                continue;
            if (current_line == target_line)
            {
                auto line = std::wstring_view{text.data() + line_start, i - line_start};
                return line_start + find_cursor_in_line(line, text_x, font_atlas);
            }
            if (i == static_cast<uint32_t>(text.size()))
                return static_cast<uint32_t>(text.size());
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
        for (uint32_t i = 0; i < std::min(cursor, static_cast<uint32_t>(text.size())); i++)
        {
            if (text[i] == L'\n') { x = 0.0f; y += line_h; }
            else { x += glyph_advance(font_atlas, text[i]); }
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
            if (ch == L'\n') { max_w = std::max(max_w, line_w); line_w = 0.0f; lines++; }
            else { line_w += glyph_advance(font_atlas, ch); }
        }
        max_w = std::max(max_w, line_w);
        return {max_w, static_cast<float>(lines - 1) * line_h + cap_h};
    }

    static auto centered_single_line_text_pos(
        const go::vf2& rect_pos, const go::vf2& rect_size,
        std::wstring_view text, const vk::IFontAtlas* font_atlas) -> go::vf2
    {
        auto font_h = font_atlas ? static_cast<float>(font_atlas->get_cap_height()) : 12.0f;
        auto text_w = font_atlas ? static_cast<float>(font_atlas->calculate_line_width(text)) : 0.0f;
        return {
            std::floor(rect_pos[0] + (rect_size[0] - text_w) * 0.5f),
            std::floor(rect_pos[1] + (rect_size[1] + font_h) * 0.5f)
        };
    }

    // ===== Clipped Text Drawing Helper =====

    /*static auto draw_clipped_text(
        DrawContext& ctx, UIElement& el,
        const go::vf2& abs_pos, const go::vf2& el_size,
        std::wstring_view text, const go::vf2& text_pos, const go::vu4& color) -> bool
    {
        auto font = effective_font(el, ctx);
        if (!ctx.resolve_clipped_text_buffer || !font.ptr)
            return false;

        auto content_fb_pos = abs_pos + ctx.clip_pos;
        auto [clip_pos, clip_size] = clip_rect(content_fb_pos, el_size, ctx.clip_pos, ctx.clip_size);
        if (clip_size[0] <= 0.0f || clip_size[1] <= 0.0f)
            return false;

        auto scissor = to_rect(clip_pos, clip_size);
        auto* clipped_text = ctx.resolve_clipped_text_buffer(font, scissor);
        if (!clipped_text)
            return false;

        clipped_text->add(text, text_pos, color);
        return true;
    }*/

    // ===== Scroll Cursor Tracking Helper =====

    static auto update_scroll_for_cursor(float cursor_px, float inner_extent, float max_scroll, float& scroll) -> void
    {
        constexpr auto margin = 2.0f;
        auto max_visible = std::max(margin, inner_extent - margin - 2.0f);
        auto view = cursor_px - scroll;
        if (view > max_visible)
            scroll = cursor_px - max_visible;
        else if (view < margin)
            scroll = cursor_px - margin;
        scroll = std::clamp(scroll, 0.0f, max_scroll);
    }

    static constexpr auto k_panel_scrollbar_width = 4.0f;
    static constexpr auto k_panel_scrollbar_min_thumb_height = 12.0f;
    static constexpr auto k_text_edit_padding = 4.0f;

    static auto should_draw_background(bool draw_background, const go::vu4& color) -> bool
    {
        return draw_background && color[3] > 0;
    }

    static auto draw_element_background(DrawContext& ctx, const go::vf2& pos, const go::vf2& size, const go::vu4& color, bool draw_bg, size_t pass_id) -> void
    {
        if (should_draw_background(draw_bg, color))
        {
            auto buff = effective_solid_buffer(ctx, pass_id);
            if (buff)
                buff->add_rect(pos, size, color);
        }
    }

    static auto draw_element_background(DrawContext& ctx, const go::vf2& pos, const go::vf2& size, const go::vu4& color, bool draw_bg, size_t pass_id, VkRect2D scissor) -> void
    {
        if (should_draw_background(draw_bg, color))
        {
            auto buff = effective_solid_buffer(ctx, pass_id, scissor);
            if (buff)
                buff->add_rect(pos, size, color);
        }
    }

    static auto draw_element_border(DrawContext& ctx, const go::vf2& pos, const go::vf2& size, const go::vu4& color, bool draw_border, size_t pass_id) -> void
    {
        if (draw_border && color[3] > 0)
        {
            auto buff = effective_wire_buffer(ctx, pass_id);
            if (buff)
                buff->add_rect(pos, size, color);
        }
    }

    static auto draw_element_border(DrawContext& ctx, const go::vf2& pos, const go::vf2& size, const go::vu4& color, bool draw_border, size_t pass_id, VkRect2D scissor) -> void
    {
        if (draw_border && color[3] > 0)
        {
            auto buff = effective_wire_buffer(ctx, pass_id, scissor);
            if (buff)
                buff->add_rect(pos, size, color);
        }
    }

    // ===== UIElement =====

    UIElement::UIElement()
        : m_id(next_element_id())
    {
    }

    auto UIElement::mark_dirty() -> void
    {
        m_dirty = true;
        if (m_parent)
            m_parent->mark_dirty();
    }

    auto UIElement::set_position(go::vf2 pos) -> void
    {
        if (m_position[0] != pos[0] || m_position[1] != pos[1])
        {
            m_position = pos;
            mark_dirty();
        }
    }

    auto UIElement::set_size(go::vf2 sz) -> void
    {
        if (m_size[0] != sz[0] || m_size[1] != sz[1])
        {
            m_size = sz;
            mark_dirty();
        }
    }

    auto UIElement::set_visible(bool v) -> void
    {
        if (m_visible != v)
        {
            m_visible = v;
            mark_dirty();
        }
    }

    auto UIElement::set_enabled(bool e) -> void
    {
        if (m_enabled != e)
        {
            m_enabled = e;
            mark_dirty();
        }
    }

    auto UIElement::set_font(vk::FontId f) -> void
    {
        m_font = f;
        mark_dirty();
    }

    // ===== UILabel Setters =====

    auto UILabel::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.label.size())
            return;
        m_style = style_sheet.label[index];
        mark_dirty();
    }

    auto UILabel::set_text(std::wstring t) -> void { m_text = std::move(t); mark_dirty(); }
    auto UILabel::set_color(go::vu4 c) -> void { m_style.text_color = c; mark_dirty(); }
    auto UILabel::set_align(UILabelAlign a) -> void { if (m_align != a) { m_align = a; mark_dirty(); } }

    // ===== UIButton Setters =====

    auto UIButton::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.button.size())
            return;
        m_style = style_sheet.button[index];
        mark_dirty();
    }

    auto UIButton::set_label(std::wstring t) -> void { m_label = std::move(t); mark_dirty(); }
    auto UIButton::set_text_color(go::vu4 c) -> void { m_style.text_color = c; mark_dirty(); }
    auto UIButton::set_draw_background(bool v) -> void { if (m_style.draw_background != v) { m_style.draw_background = v; mark_dirty(); } }
    auto UIButton::set_bg_color(go::vu4 c) -> void { m_style.bg_color = c; mark_dirty(); }
    auto UIButton::set_draw_border(bool v) -> void { if (m_style.draw_border != v) { m_style.draw_border = v; mark_dirty(); } }
    auto UIButton::set_border_color(go::vu4 c) -> void { m_style.border_color = c; mark_dirty(); }
    auto UIButton::set_hover_bg_color(go::vu4 c) -> void { m_style.hover_bg_color = c; mark_dirty(); }
    auto UIButton::set_hover_border_color(go::vu4 c) -> void { m_style.hover_border_color = c; mark_dirty(); }
    auto UIButton::set_hover_text_color(go::vu4 c) -> void { m_style.hover_text_color = c; mark_dirty(); }
    auto UIButton::set_pressed_bg_color(go::vu4 c) -> void { m_style.pressed_bg_color = c; mark_dirty(); }
    auto UIButton::set_pressed_border_color(go::vu4 c) -> void { m_style.pressed_border_color = c; mark_dirty(); }
    auto UIButton::set_pressed_text_color(go::vu4 c) -> void { m_style.pressed_text_color = c; mark_dirty(); }

    // ===== UITextInput Setters =====

    auto UITextInput::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.text_input.size())
            return;
        m_style = style_sheet.text_input[index];
        mark_dirty();
    }

    auto UITextInput::set_text(std::wstring t) -> void { m_text = std::move(t); m_cursor = std::min(m_cursor, static_cast<uint32_t>(m_text.size())); mark_dirty(); }
    auto UITextInput::set_text_color(go::vu4 c) -> void { m_style.text_color = c; mark_dirty(); }
    auto UITextInput::set_draw_background(bool v) -> void { if (m_style.draw_background != v) { m_style.draw_background = v; mark_dirty(); } }
    auto UITextInput::set_bg_color(go::vu4 c) -> void { m_style.bg_color = c; mark_dirty(); }
    auto UITextInput::set_draw_border(bool v) -> void { if (m_style.draw_border != v) { m_style.draw_border = v; mark_dirty(); } }
    auto UITextInput::set_border_color(go::vu4 c) -> void { m_style.border_color = c; mark_dirty(); }
    auto UITextInput::set_active_bg_color(go::vu4 c) -> void { m_style.active_bg_color = c; mark_dirty(); }
    auto UITextInput::set_active_border_color(go::vu4 c) -> void { m_style.active_border_color = c; mark_dirty(); }
    auto UITextInput::set_active_text_color(go::vu4 c) -> void { m_style.active_text_color = c; mark_dirty(); }
    auto UITextInput::set_cursor_color(go::vu4 c) -> void { m_style.cursor_color = c; mark_dirty(); }

    // ===== UITextArea Setters =====

    auto UITextArea::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.text_area.size())
            return;
        m_style = style_sheet.text_area[index];
        mark_dirty();
    }

    auto UITextArea::set_text(std::wstring t) -> void { m_text = std::move(t); m_cursor = std::min(m_cursor, static_cast<uint32_t>(m_text.size())); mark_dirty(); }
    auto UITextArea::set_text_color(go::vu4 c) -> void { m_style.text_color = c; mark_dirty(); }
    auto UITextArea::set_draw_background(bool v) -> void { if (m_style.draw_background != v) { m_style.draw_background = v; mark_dirty(); } }
    auto UITextArea::set_bg_color(go::vu4 c) -> void { m_style.bg_color = c; mark_dirty(); }
    auto UITextArea::set_draw_border(bool v) -> void { if (m_style.draw_border != v) { m_style.draw_border = v; mark_dirty(); } }
    auto UITextArea::set_border_color(go::vu4 c) -> void { m_style.border_color = c; mark_dirty(); }
    auto UITextArea::set_active_bg_color(go::vu4 c) -> void { m_style.active_bg_color = c; mark_dirty(); }
    auto UITextArea::set_active_border_color(go::vu4 c) -> void { m_style.active_border_color = c; mark_dirty(); }
    auto UITextArea::set_active_text_color(go::vu4 c) -> void { m_style.active_text_color = c; mark_dirty(); }
    auto UITextArea::set_cursor_color(go::vu4 c) -> void { m_style.cursor_color = c; mark_dirty(); }

    // ===== UISlider Setters =====

    auto UISlider::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.slider.size())
            return;
        m_style = style_sheet.slider[index];
        mark_dirty();
    }

    auto UISlider::set_value(float v) -> void { if (m_value != v) { m_value = v; mark_dirty(); } }
    auto UISlider::set_min_val(float v) -> void { if (m_min_val != v) { m_min_val = v; mark_dirty(); } }
    auto UISlider::set_max_val(float v) -> void { if (m_max_val != v) { m_max_val = v; mark_dirty(); } }
    auto UISlider::set_track_color(go::vu4 c) -> void { m_style.track_color = c; mark_dirty(); }
    auto UISlider::set_handle_bg_color(go::vu4 c) -> void { m_style.handle_bg_color = c; mark_dirty(); }
    auto UISlider::set_handle_border_color(go::vu4 c) -> void { m_style.handle_border_color = c; mark_dirty(); }
    auto UISlider::set_draw_handle_border(bool v) -> void { if (m_style.draw_handle_border != v) { m_style.draw_handle_border = v; mark_dirty(); } }

    // ===== UICheckbox Setters =====

    auto UICheckbox::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.checkbox.size())
            return;
        m_style = style_sheet.checkbox[index];
        mark_dirty();
    }

    auto UICheckbox::set_checked(bool v) -> void { if (m_checked != v) { m_checked = v; mark_dirty(); } }
    auto UICheckbox::set_label(std::wstring t) -> void { m_label = std::move(t); mark_dirty(); }
    auto UICheckbox::set_box_color(go::vu4 c) -> void { m_style.box_color = c; mark_dirty(); }
    auto UICheckbox::set_border_color(go::vu4 c) -> void { m_style.border_color = c; mark_dirty(); }
    auto UICheckbox::set_check_color(go::vu4 c) -> void { m_style.check_color = c; mark_dirty(); }
    auto UICheckbox::set_text_color(go::vu4 c) -> void { m_style.text_color = c; mark_dirty(); }

    // ===== UIDropdown Setters =====

    auto UIDropdown::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.dropdown.size())
            return;
        m_style = style_sheet.dropdown[index];
        mark_dirty();
    }

    auto UIDropdown::set_items(std::vector<std::wstring> items) -> void { m_items = std::move(items); mark_dirty(); }
    auto UIDropdown::set_selected(int idx) -> void { if (m_selected != idx) { m_selected = idx; mark_dirty(); } }
    auto UIDropdown::set_hover_color(go::vu4 c) -> void { m_style.hover_color = c; mark_dirty(); }
    auto UIDropdown::set_text_color(go::vu4 c) -> void { m_style.text_color = c; mark_dirty(); }
    auto UIDropdown::set_draw_background(bool v) -> void { if (m_style.draw_background != v) { m_style.draw_background = v; mark_dirty(); } }
    auto UIDropdown::set_bg_color(go::vu4 c) -> void { m_style.bg_color = c; mark_dirty(); }
    auto UIDropdown::set_draw_border(bool v) -> void { if (m_style.draw_border != v) { m_style.draw_border = v; mark_dirty(); } }
    auto UIDropdown::set_border_color(go::vu4 c) -> void { m_style.border_color = c; mark_dirty(); }

    // ===== UIPanel Setters =====

    auto UIPanel::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.panel.size())
            return;
        m_style = style_sheet.panel[index];
        mark_dirty();
    }

    auto UIPanel::set_layout(Layout l) -> void { if (m_layout != l) { m_layout = l; mark_dirty(); } }
    auto UIPanel::set_padding(float p) -> void { if (m_padding != p) { m_padding = p; mark_dirty(); } }
    auto UIPanel::set_spacing(float s) -> void { if (m_spacing != s) { m_spacing = s; mark_dirty(); } }
    auto UIPanel::set_scroll_offset(go::vf2 off) -> void
    {
        if (m_scroll_offset[0] != off[0] || m_scroll_offset[1] != off[1])
        {
            m_scroll_offset = off;
            mark_dirty();
        }
    }
    auto UIPanel::set_draw_background(bool v) -> void { if (m_style.draw_background != v) { m_style.draw_background = v; mark_dirty(); } }
    auto UIPanel::set_bg_color(go::vu4 c) -> void { m_style.bg_color = c; mark_dirty(); }
    auto UIPanel::set_draw_border(bool v) -> void { if (m_style.draw_border != v) { m_style.draw_border = v; mark_dirty(); } }
    auto UIPanel::set_border_color(go::vu4 c) -> void { m_style.border_color = c; mark_dirty(); }

    // ===== UIExpandablePanel Setters =====
/*
    auto UIExpandablePanel::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.expandable_panel.size())
            return;
        m_style = style_sheet.expandable_panel[index];
        mark_dirty();
    }

    auto UIExpandablePanel::set_header(std::wstring text) -> void { m_header = std::move(text); mark_dirty(); }
    auto UIExpandablePanel::set_expanded(bool expanded) -> void { if (m_expanded != expanded) { m_expanded = expanded; mark_dirty(); } }
    auto UIExpandablePanel::set_padding(float p) -> void { if (m_padding != p) { m_padding = p; mark_dirty(); } }
    auto UIExpandablePanel::set_spacing(float s) -> void { if (m_spacing != s) { m_spacing = s; mark_dirty(); } }
    auto UIExpandablePanel::set_header_height(float h) -> void
    {
        auto clamped_h = std::max(1.0f, h);
        if (m_header_height != clamped_h)
        {
            m_header_height = clamped_h;
            if (m_folded_height <= 0.0f)
                m_folded_height = m_header_height;
            mark_dirty();
        }
    }
    auto UIExpandablePanel::set_expanded_height(float h) -> void
    {
        auto clamped_h = std::max(1.0f, h);
        if (m_expanded_height != clamped_h)
        {
            m_expanded_height = clamped_h;
            mark_dirty();
        }
    }
    auto UIExpandablePanel::set_folded_height(float h) -> void
    {
        auto clamped_h = std::max(1.0f, h);
        if (m_folded_height != clamped_h)
        {
            m_folded_height = clamped_h;
            mark_dirty();
        }
    }
    auto UIExpandablePanel::set_header_bg_color(go::vu4 c) -> void { m_style.header_bg_color = c; mark_dirty(); }
    auto UIExpandablePanel::set_text_color(go::vu4 c) -> void { m_style.text_color = c; mark_dirty(); }
    auto UIExpandablePanel::set_draw_background(bool v) -> void { if (m_style.draw_background != v) { m_style.draw_background = v; mark_dirty(); } }
    auto UIExpandablePanel::set_bg_color(go::vu4 c) -> void { m_style.bg_color = c; mark_dirty(); }
    auto UIExpandablePanel::set_draw_border(bool v) -> void { if (m_style.draw_border != v) { m_style.draw_border = v; mark_dirty(); } }
    auto UIExpandablePanel::set_border_color(go::vu4 c) -> void { m_style.border_color = c; mark_dirty(); }
*/
    // ===== UILabel =====

    auto UILabel::on_event(UIEvent&) -> bool
    {
        return false;
    }

    auto UILabel::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;

        auto text_buf = effective_text_buffer(*this, ctx, 0);
        if (!text_buf) return;

        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;
        auto font_h = static_cast<float>(m_font.ptr->get_x_height());
        auto text_w = static_cast<float>(m_font.ptr->calculate_line_width(m_text));
        auto text_x = abs_pos[0];
        if (m_align == UILabelAlign::Center)
            text_x = std::floor(abs_pos[0] + (m_size[0] - text_w) * 0.5f);
        else if (m_align == UILabelAlign::Right)
            text_x = std::floor(abs_pos[0] + m_size[0] - text_w);
        auto text_y = std::floor(abs_pos[1] + (m_size[1] + font_h) * 0.5f);
        text_buf->add(m_text, {text_x, text_y}, m_style.text_color);
    }

    // ===== UIButton =====

    UIButton::UIButton()
    {
        m_style = Style{};
    }

    auto UIButton::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0, 0}, m_size);

        if (ev.type == EventType::MouseMove)
        {
            if (m_hovered != inside) { m_hovered = inside; mark_dirty(); }
            return false;
        }
        if (ev.type == EventType::MousePress && ev.button == 0 && inside)
        {
            m_pressed = true;
            mark_dirty();
            ev.consumed = true;
            return true;
        }
        if (ev.type == EventType::MouseRelease && ev.button == 0)
        {
            if (m_pressed && inside && on_click)
                on_click();
            if (m_pressed) { m_pressed = false; mark_dirty(); }
            return inside;
        }
        return false;
    }

    auto UIButton::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;
        auto text_buf = effective_text_buffer(*this, ctx, 0);
        if (!text_buf) return;

        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;

        auto bg_color = m_style.bg_color;
        auto border_color = m_style.border_color;
        auto text_color = m_style.text_color;
        if (m_pressed)
        {
            bg_color = m_style.pressed_bg_color;
            border_color = m_style.pressed_border_color;
            text_color = m_style.pressed_text_color;
        }
        else if (m_hovered)
        {
            bg_color = m_style.hover_bg_color;
            border_color = m_style.hover_border_color;
            text_color = m_style.hover_text_color;
        }

        draw_element_background(ctx, abs_pos, m_size, bg_color, m_style.draw_background, 0);
        draw_element_border(ctx, abs_pos, m_size, border_color, m_style.draw_border, 0);

        auto text_pos = centered_single_line_text_pos(abs_pos, m_size, m_label, m_font.ptr);
        if (text_buf)
            text_buf->add(m_label, text_pos, text_color);
    }

    // ===== UITextInput =====

    UITextInput::UITextInput()
    {
        set_draw_background(true);
        set_bg_color(color_darkgrey);
        set_draw_border(true);
        set_border_color(color_lightgrey);
    }

    auto UITextInput::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled) return false;

        m_cursor = std::min(m_cursor, static_cast<uint32_t>(m_text.size()));
        m_scroll_x = std::max(0.0f, m_scroll_x);

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0, 0}, m_size);

        if (ev.type == EventType::MousePress && ev.button == 0)
        {
            auto was_focused = m_focused;
            m_focused = inside;
            if (inside)
            {
                ev.active_interaction_id = m_id;
                m_cursor = find_cursor_in_line(m_text, local[0] - 4.0f + m_scroll_x, nullptr);
                ev.consumed = true;
            }
            if (was_focused != m_focused) mark_dirty();
            return inside;
        }

        if (!m_focused) return false;

        if (ev.type == EventType::TextInput)
        {
            m_text.insert(m_text.begin() + m_cursor, static_cast<wchar_t>(ev.codepoint));
            m_cursor++;
            if (on_change) on_change(m_text);
            mark_dirty();
            ev.consumed = true;
            return true;
        }

        if (ev.type == EventType::KeyPress)
        {
            if (ev.key == keys::Backspace && m_cursor > 0)
            {
                m_cursor--;
                m_text.erase(m_cursor, 1);
                if (on_change) on_change(m_text);
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Delete && m_cursor < m_text.size())
            {
                m_text.erase(m_cursor, 1);
                if (on_change) on_change(m_text);
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Left && m_cursor > 0)
            {
                m_cursor--;
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Right && m_cursor < m_text.size())
            {
                m_cursor++;
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Home)
            {
                m_cursor = 0;
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::End)
            {
                m_cursor = static_cast<uint32_t>(m_text.size());
                mark_dirty();
                ev.consumed = true;
                return true;
            }
        }
        return false;
    }

    auto UITextInput::clear_interaction_state() -> void
    {
        if (m_focused) { m_focused = false; mark_dirty(); }
    }

    auto UITextInput::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;
        m_cursor = std::min(m_cursor, static_cast<uint32_t>(m_text.size()));
        //auto* font_atlas = effective_font_atlas(*this, ctx);
        auto* font_atlas = m_font.ptr;
        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;

        auto bg_color = m_focused ? m_style.active_bg_color : m_style.bg_color;
        auto border_color = m_focused ? m_style.active_border_color : m_style.border_color;
        auto text_color = m_focused ? m_style.active_text_color : m_style.text_color;

        draw_element_background(ctx, abs_pos, m_size, bg_color, m_style.draw_background, 0);
        draw_element_border(ctx, abs_pos, m_size, border_color, m_style.draw_border, 0);

        auto cap_h = font_atlas ? static_cast<float>(font_atlas->get_cap_height()) : 12.0f;
        auto inner_w = std::max(0.0f, m_size[0] - k_text_edit_padding * 2.0f);
        auto cursor_px = measure_prefix_width(m_text, m_cursor, font_atlas);
        auto text_w = measure_prefix_width(m_text, static_cast<uint32_t>(m_text.size()), font_atlas);
        auto max_scroll_x = std::max(0.0f, text_w - inner_w);

        update_scroll_for_cursor(cursor_px, inner_w, max_scroll_x, m_scroll_x);

        auto text_x = abs_pos[0] + k_text_edit_padding - m_scroll_x;
        auto text_y = std::floor(abs_pos[1] + (m_size[1] + cap_h) * 0.5f);

        //if (!draw_clipped_text(ctx, *this, abs_pos, m_size, m_text, {text_x, text_y}, text_color))
        {
            // TODO: this will not work if we move whole panel somewhere in abs positions,
            // in that case we need to redo this part.
            VkRect2D scissors = 
            {
                ctx.viewport.offset.x + static_cast<int32_t>(abs_pos[0]), 
                ctx.viewport.offset.y + static_cast<int32_t>(abs_pos[1]),
                static_cast<uint32_t>(m_size[0]),
                static_cast<uint32_t>(m_size[1])
            };
            if (auto* text_buf = effective_text_buffer(*this, ctx, 0, scissors); text_buf)
                text_buf->add(m_text, {text_x, text_y}, text_color);
        }

        if (m_focused)
        {
            if (auto solid_buff = effective_solid_buffer(ctx, 0); solid_buff)
            {
                auto cursor_x = text_x + measure_prefix_width(m_text, m_cursor, font_atlas);
                solid_buff->add_rect({cursor_x, text_y - cap_h}, {2.0f, cap_h}, m_style.cursor_color);
            }
        }
    }

    // ===== UITextArea =====

    UITextArea::UITextArea()
    {
        m_size = {200.0f, 100.0f};
        set_draw_background(true);
        set_bg_color(color_darkgrey);
        set_draw_border(true);
        set_border_color(color_lightgrey);
    }

    auto UITextArea::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled) return false;

        m_cursor = std::min(m_cursor, static_cast<uint32_t>(m_text.size()));
        m_scroll_x = std::max(0.0f, m_scroll_x);
        m_scroll_y = std::max(0.0f, m_scroll_y);

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0, 0}, m_size);

        if (ev.type == EventType::Scroll && inside)
        {
            m_scroll_x = std::max(0.0f, m_scroll_x - ev.scroll_dx * 20.0f);
            m_scroll_y = std::max(0.0f, m_scroll_y - ev.scroll_dy * 20.0f);
            mark_dirty();
            ev.consumed = true;
            return true;
        }

        if (ev.type == EventType::MousePress && ev.button == 0)
        {
            auto was_focused = m_focused;
            m_focused = inside;
            if (inside)
            {
                ev.active_interaction_id = m_id;
                auto content_local = go::vf2{local[0] + m_scroll_x, local[1] + m_scroll_y};
                m_cursor = find_cursor_in_multiline_text(m_text, content_local, nullptr);
                ev.consumed = true;
            }
            if (was_focused != m_focused || inside) mark_dirty();
            return inside;
        }

        if (!m_focused) return false;

        if (ev.type == EventType::TextInput)
        {
            m_text.insert(m_text.begin() + m_cursor, static_cast<wchar_t>(ev.codepoint));
            m_cursor++;
            if (on_change) on_change(m_text);
            mark_dirty();
            ev.consumed = true;
            return true;
        }

        if (ev.type == EventType::KeyPress)
        {
            if (ev.key == keys::Enter)
            {
                m_text.insert(m_text.begin() + m_cursor, L'\n');
                m_cursor++;
                if (on_change) on_change(m_text);
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Backspace && m_cursor > 0)
            {
                m_cursor--;
                m_text.erase(m_cursor, 1);
                if (on_change) on_change(m_text);
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Delete && m_cursor < m_text.size())
            {
                m_text.erase(m_cursor, 1);
                if (on_change) on_change(m_text);
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Left && m_cursor > 0)
            {
                m_cursor--;
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Right && m_cursor < m_text.size())
            {
                m_cursor++;
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::Home)
            {
                m_cursor = 0;
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            if (ev.key == keys::End)
            {
                m_cursor = static_cast<uint32_t>(m_text.size());
                mark_dirty();
                ev.consumed = true;
                return true;
            }
        }
        return false;
    }

    auto UITextArea::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;
        m_cursor = std::min(m_cursor, static_cast<uint32_t>(m_text.size()));
        m_scroll_x = std::max(0.0f, m_scroll_x);
        m_scroll_y = std::max(0.0f, m_scroll_y);
        //auto* font_atlas = effective_font_atlas(*this, ctx);
        auto* font_atlas = m_font.ptr;
        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;

        auto bg_color = m_focused ? m_style.active_bg_color : m_style.bg_color;
        auto border_color = m_focused ? m_style.active_border_color : m_style.border_color;
        auto text_color = m_focused ? m_style.active_text_color : m_style.text_color;

        draw_element_background(ctx, abs_pos, m_size, bg_color, m_style.draw_background, 0);
        draw_element_border(ctx, abs_pos, m_size, border_color, m_style.draw_border, 0);

        auto font_h = font_atlas ? static_cast<float>(font_atlas->get_x_height()) : 12.0f;
        auto cap_h = font_atlas ? static_cast<float>(font_atlas->get_cap_height()) : 12.0f;
        auto inner_w = std::max(0.0f, m_size[0] - k_text_edit_padding * 2.0f);
        auto inner_h = std::max(0.0f, m_size[1] - k_text_edit_padding * 2.0f);

        auto cursor_pos = multiline_cursor_position(m_text, m_cursor, font_atlas);
        auto content_sz = multiline_content_size(m_text, font_atlas);

        update_scroll_for_cursor(cursor_pos[0], inner_w, std::max(0.0f, content_sz[0] - inner_w), m_scroll_x);
        update_scroll_for_cursor(cursor_pos[1], std::max(0.0f, inner_h - cap_h), std::max(0.0f, content_sz[1] - inner_h), m_scroll_y);

        auto text_x = abs_pos[0] + k_text_edit_padding - m_scroll_x;
        auto text_y = std::floor(abs_pos[1] + k_text_edit_padding + font_h - m_scroll_y);

        //if (!draw_clipped_text(ctx, *this, abs_pos, m_size, m_text, {text_x, text_y}, text_color))
        {
            // TODO: this will not work if we move whole panel somewhere in abs positions,
            // in that case we need to redo this part.
            VkRect2D scissors = 
            {
                ctx.viewport.offset.x + static_cast<int32_t>(abs_pos[0]), 
                ctx.viewport.offset.y + static_cast<int32_t>(abs_pos[1]),
                static_cast<uint32_t>(m_size[0]),
                static_cast<uint32_t>(m_size[1])
            };
            if (auto* text_buf = effective_text_buffer(*this, ctx, 0, scissors); text_buf)
                text_buf->add(m_text, {text_x, text_y}, text_color);
        }

        if (m_focused)
        {
            if (auto solid_buff = effective_solid_buffer(ctx, 0); solid_buff)
            {
                auto cursor_x = text_x + cursor_pos[0];
                auto cursor_baseline = text_y + cursor_pos[1];
                solid_buff->add_rect({cursor_x, cursor_baseline - cap_h}, {2.0f, cap_h}, m_style.cursor_color);
            }
        }
    }

    auto UITextArea::clear_interaction_state() -> void
    {
        if (m_focused) { m_focused = false; mark_dirty(); }
    }

    // ===== UISlider =====

    auto UISlider::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0, 0}, m_size);

        if (ev.type == EventType::MousePress && ev.button == 0 && inside)
        {
            m_dragging = true;
            auto ratio = std::clamp(local[0] / m_size[0], 0.0f, 1.0f);
            m_value = m_min_val + ratio * (m_max_val - m_min_val);
            if (on_change) on_change(m_value);
            mark_dirty();
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
            auto ratio = std::clamp(local[0] / m_size[0], 0.0f, 1.0f);
            m_value = m_min_val + ratio * (m_max_val - m_min_val);
            if (on_change) on_change(m_value);
            mark_dirty();
            ev.consumed = true;
            return true;
        }
        return false;
    }

    auto UISlider::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;
        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;

        auto track_height = 6.0f;
        auto track_y = abs_pos[1] + (m_size[1] - track_height) * 0.5f;
        auto solid_buff = effective_solid_buffer(ctx, 0);
        solid_buff->add_rect({abs_pos[0], track_y}, {m_size[0], track_height}, m_style.track_color);

        auto range = m_max_val - m_min_val;
        auto ratio = range > 0.0f ? (m_value - m_min_val) / range : 0.0f;
        auto handle_w = 12.0f;
        auto handle_x = abs_pos[0] + ratio * (m_size[0] - handle_w);
        solid_buff->add_rect({handle_x, abs_pos[1]}, {handle_w, m_size[1]}, m_style.handle_bg_color);
        if (m_style.draw_handle_border)
        {
            auto wire_buff = effective_wire_buffer(ctx, 0);
            wire_buff->add_rect({handle_x, abs_pos[1]}, {handle_w, m_size[1]}, m_style.handle_border_color);
        }
    }

    // ===== UICheckbox =====

    UICheckbox::UICheckbox()
    {
        m_size = {20.0f, 20.0f};
        set_border_color(color_lightgrey);
    }

    auto UICheckbox::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0, 0}, {m_size[1], m_size[1]});

        if (ev.type == EventType::MousePress && ev.button == 0 && inside)
        {
            m_checked = !m_checked;
            if (on_change) on_change(m_checked);
            mark_dirty();
            ev.consumed = true;
            return true;
        }
        return false;
    }

    auto UICheckbox::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;
        auto* text_buf = effective_text_buffer(*this, ctx, 0);
        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;
        auto box_sz = go::vf2{m_size[1], m_size[1]};

        auto solid_buff = effective_solid_buffer(ctx, 0);
        auto wire_buff = effective_wire_buffer(ctx, 0);

        solid_buff->add_rect(abs_pos, box_sz, m_style.box_color);
        wire_buff->add_rect(abs_pos, box_sz, m_style.border_color);

        if (m_checked)
        {
            auto inset = 4.0f;
            solid_buff->add_rect(
                {abs_pos[0] + inset, abs_pos[1] + inset},
                {box_sz[0] - inset * 2.0f, box_sz[1] - inset * 2.0f},
                m_style.check_color
            );
        }

        if (!m_label.empty())
        {
            auto font_h = static_cast<float>(m_font.ptr->get_x_height());
            auto text_x = abs_pos[0] + box_sz[0] + 6.0f;
            auto text_y = std::floor(abs_pos[1] + (box_sz[1] + font_h) * 0.5f);
            if (text_buf)
                text_buf->add(m_label, {text_x, text_y}, m_style.text_color);
        }
    }

    // ===== UIDropdown =====

    UIDropdown::UIDropdown()
    {
        set_draw_background(true);
        set_bg_color(color_darkgrey);
        set_draw_border(true);
        set_border_color(color_lightgrey);
    }

    auto UIDropdown::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside_header = point_in_rect(local, {0, 0}, m_size);

        if (ev.type == EventType::MouseMove && m_open)
        {
            auto prev = m_hovered_item;
            m_hovered_item = -1;
            auto item_height = m_size[1];
            for (int i = 0; i < static_cast<int>(m_items.size()); i++)
            {
                auto item_y = m_size[1] + static_cast<float>(i) * item_height;
                if (point_in_rect(local, {0, item_y}, {m_size[0], item_height}))
                {
                    m_hovered_item = i;
                    break;
                }
            }
            if (prev != m_hovered_item) mark_dirty();
            return true;
        }

        if (ev.type == EventType::MousePress && ev.button == 0)
        {
            if (m_open)
            {
                if (m_hovered_item >= 0)
                {
                    m_selected = m_hovered_item;
                    if (on_change) on_change(m_selected);
                }
                m_open = false;
                mark_dirty();
                ev.consumed = true;
                return true;
            }
            else if (inside_header)
            {
                m_open = true;
                mark_dirty();
                ev.active_interaction_id = m_id;
                ev.consumed = true;
                return true;
            }
            else
            {
                if (m_open) { m_open = false; mark_dirty(); }
            }
        }
        return false;
    }

    auto UIDropdown::clear_interaction_state() -> void
    {
        if (m_open || m_hovered_item != -1)
        {
            m_open = false;
            m_hovered_item = -1;
            mark_dirty();
        }
    }

    auto UIDropdown::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;

        //auto* font_atlas = effective_font_atlas(*this, ctx);
        auto* font_atlas = m_font.ptr;
        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;

        if (m_open)
        {
            auto* text_buf = effective_text_buffer(*this, ctx, 1);
            auto* solid_buf = effective_solid_buffer(ctx, 1);

            if (solid_buf && should_draw_background(m_style.draw_background, get_bg_color()))
                solid_buf->add_rect(abs_pos, m_size, get_bg_color());

            if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size()))
            {
                auto text_pos = centered_single_line_text_pos(abs_pos, m_size, m_items[m_selected], font_atlas);
                if (text_buf) text_buf->add(m_items[m_selected], text_pos, m_style.text_color);
            }
            
            if (solid_buf)
            {
                auto arrow_center = go::vf2{abs_pos[0] + m_size[0] - 12.0f, abs_pos[1] + m_size[1] * 0.5f};
                auto side = std::max(6.0f, std::min(10.0f, m_size[1] * 0.45f));
                auto half_w = side * 0.5f;
                auto half_h = side * 0.5f * std::sqrt(3.0f) * 0.5f;

                auto p0 = go::vf2{arrow_center[0] - half_w, arrow_center[1] + half_h};
                auto p1 = go::vf2{arrow_center[0] + half_w, arrow_center[1] + half_h};
                auto p2 = go::vf2{arrow_center[0], arrow_center[1] - half_h};
                solid_buf->add_triangle(p0, p1, p2, m_style.text_color);
            }

            auto item_height = m_size[1];
            for (int i = 0; i < static_cast<int>(m_items.size()); i++)
            {
                auto iy = abs_pos[1] + m_size[1] + static_cast<float>(i) * item_height;
                if (solid_buf)
                {
                    auto col = (i == m_hovered_item) ? m_style.hover_color : m_style.bg_color;
                    solid_buf->add_rect({abs_pos[0], iy}, {m_size[0], item_height}, col);
                }

                if (text_buf)
                {
                    auto item_pos = centered_single_line_text_pos({abs_pos[0], iy}, {m_size[0], item_height}, m_items[i], font_atlas);
                    text_buf->add(m_items[i], item_pos, m_style.text_color);
                }
            }
            auto open_size = go::vf2{m_size[0], m_size[1] * (static_cast<float>(m_items.size()) + 1.0f)};
            draw_element_border(ctx, abs_pos, open_size, get_border_color(), m_style.draw_border, 1);
        }
        else 
        {
            auto* solid_buf = effective_solid_buffer(ctx, 0);
            auto* text_buf = effective_text_buffer(*this, ctx, 0);

            if (solid_buf && should_draw_background(m_style.draw_background, get_bg_color()))
                solid_buf->add_rect(abs_pos, m_size, get_bg_color());

            draw_element_border(ctx, abs_pos, m_size, get_border_color(), m_style.draw_border, 0);

            if (m_selected >= 0 && m_selected < static_cast<int>(m_items.size()))
            {
                auto text_pos = centered_single_line_text_pos(abs_pos, m_size, m_items[m_selected], font_atlas);
                if (text_buf)
                    text_buf->add(m_items[m_selected], text_pos, m_style.text_color);
            }

            if (solid_buf)
            {
                auto arrow_center = go::vf2{abs_pos[0] + m_size[0] - 12.0f, abs_pos[1] + m_size[1] * 0.5f};
                auto side = std::max(6.0f, std::min(10.0f, m_size[1] * 0.45f));
                auto half_w = side * 0.5f;
                auto half_h = side * 0.5f * std::sqrt(3.0f) * 0.5f;

                auto p0 = go::vf2{arrow_center[0] - half_w, arrow_center[1] - half_h};
                auto p1 = go::vf2{arrow_center[0] + half_w, arrow_center[1] - half_h};
                auto p2 = go::vf2{arrow_center[0], arrow_center[1] + half_h};
                solid_buf->add_triangle(p0, p1, p2, m_style.text_color);
            }
        }
    }

    // ===== UIPanel =====
/*
    UIExpandablePanel::UIExpandablePanel()
    {
        set_bg_color(color_darkgrey);
        set_border_color(color_lightgrey);
    }

    auto UIExpandablePanel::add(std::unique_ptr<UIElement> element) -> UIElement*
    {
        auto ptr = element.get();
        ptr->m_parent = this;
        m_children.push_back(std::move(element));
        mark_dirty();
        return ptr;
    }

    auto UIExpandablePanel::apply_layout() -> void
    {
        sync_height_with_state();
        clamp_scroll();
        auto cursor = go::vf2{m_padding, m_header_height + m_padding};
        for (auto& child : m_children)
        {
            if (!child->m_visible)
                continue;
            child->set_position(cursor);
            cursor[1] += child->m_size[1] + m_spacing;
        }
    }

    auto UIExpandablePanel::content_height() const -> float
    {
        auto max_bottom = m_header_height + m_padding;
        for (const auto& child : m_children)
        {
            if (!child->m_visible)
                continue;
            max_bottom = std::max(max_bottom, child->m_position[1] + child->m_size[1]);
        }
        return max_bottom + m_padding;
    }

    auto UIExpandablePanel::max_scroll_y() const -> float
    {
        if (!m_expanded)
            return 0.0f;
        auto content_view_h = std::max(0.0f, m_size[1] - m_header_height);
        auto full_content_h = std::max(0.0f, content_height() - m_header_height);
        return std::max(0.0f, full_content_h - content_view_h);
    }

    auto UIExpandablePanel::clamp_scroll() -> void
    {
        m_scroll_y = std::clamp(m_scroll_y, 0.0f, max_scroll_y());
    }

    auto UIExpandablePanel::sync_height_with_state() -> void
    {
        if (m_folded_height <= 0.0f)
            m_folded_height = std::max(1.0f, m_header_height);

        if (m_expanded_height <= 0.0f)
            m_expanded_height = std::max(m_folded_height, m_size[1]);

        m_folded_height = std::max(1.0f, m_folded_height);
        m_expanded_height = std::max(m_folded_height, m_expanded_height);

        auto target_h = m_expanded ? m_expanded_height : m_folded_height;
        m_size[1] = target_h;
    }

    auto UIExpandablePanel::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled)
            return false;

        apply_layout();

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0.0f, 0.0f}, m_size);
        auto inside_header = point_in_rect(local, {0.0f, 0.0f}, {m_size[0], m_header_height});

        if (ev.type == EventType::MousePress && ev.button == 0 && inside_header)
        {
            m_expanded = !m_expanded;
            sync_height_with_state();
            clamp_scroll();
            ev.active_interaction_id = m_id;
            ev.consumed = true;
            mark_dirty();
            return true;
        }

        if (!m_expanded)
        {
            if (inside && (ev.type == EventType::MousePress || ev.type == EventType::MouseRelease))
            {
                ev.consumed = true;
                return true;
            }
            return false;
        }

        auto content_inside = point_in_rect(local, {0.0f, m_header_height}, {m_size[0], std::max(0.0f, m_size[1] - m_header_height)});
        if (ev.type == EventType::Scroll && content_inside)
        {
            auto max_scroll = max_scroll_y();
            if (max_scroll > 0.0f)
            {
                m_scroll_y = std::clamp(m_scroll_y - ev.scroll_dy * 20.0f, 0.0f, max_scroll);
                mark_dirty();
                ev.consumed = true;
                return true;
            }
        }

        auto child_ev = ev;
        child_ev.mouse_pos = {local[0], local[1] + m_scroll_y};

        auto is_pointer_event =
            ev.type == EventType::MouseMove ||
            ev.type == EventType::MousePress ||
            ev.type == EventType::MouseRelease ||
            ev.type == EventType::Scroll;

        if (is_pointer_event)
        {
            for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
            {
                if (!(*it)->m_visible)
                    continue;
                if ((*it)->element_type() != ElementType::Dropdown)
                    continue;
                auto* dropdown = static_cast<UIDropdown*>(it->get());
                if (!dropdown->is_open())
                    continue;
                if ((*it)->on_event(child_ev))
                {
                    ev.consumed = child_ev.consumed;
                    ev.active_interaction_id = child_ev.active_interaction_id;
                    return true;
                }
            }
        }

        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        {
            if ((*it)->on_event(child_ev))
            {
                ev.consumed = child_ev.consumed;
                ev.active_interaction_id = child_ev.active_interaction_id;
                return true;
            }
        }

        if (inside && (ev.type == EventType::MousePress || ev.type == EventType::MouseRelease))
        {
            ev.consumed = true;
            return true;
        }
        return false;
    }

    auto UIExpandablePanel::clear_interaction_state_recursive(uint32_t keep_id) -> void
    {
        for (auto& child : m_children)
        {
            if (!child)
                continue;
            if (child->get_id() != keep_id)
                child->clear_interaction_state();
            if (child->element_type() == ElementType::Panel)
                static_cast<UIPanel*>(child.get())->clear_interaction_state_recursive(keep_id);
            if (child->element_type() == ElementType::ExpandablePanel)
                static_cast<UIExpandablePanel*>(child.get())->clear_interaction_state_recursive(keep_id);
        }
    }

    auto UIExpandablePanel::draw(DrawContext& ctx) -> void
    {
        if (!m_visible)
            return;

        apply_layout();

        auto* text_buf = effective_text_buffer(*this, ctx);
        auto* font_atlas = effective_font_atlas(*this, ctx);
        auto abs_pos = ctx.origin + m_position;
        auto parent_clip_pos = ctx.clip_pos;
        auto parent_clip_size = ctx.clip_size;
        auto [new_clip_pos, new_clip_size] = clip_rect(abs_pos, m_size, parent_clip_pos, parent_clip_size);
        if (new_clip_size[0] <= 0.0f || new_clip_size[1] <= 0.0f)
            return;

        ctx.geo.add_rect(abs_pos, {m_size[0], m_header_height}, m_style.header_bg_color);

        auto triangle_center = go::vf2{abs_pos[0] + m_padding + 5.0f, abs_pos[1] + m_header_height * 0.5f};
        auto side = std::max(6.0f, std::min(10.0f, m_header_height * 0.45f));
        auto half_w = side * 0.5f;
        auto half_h = side * 0.5f * std::sqrt(3.0f) * 0.5f;

        if (m_expanded)
        {
            auto p0 = go::vf2{triangle_center[0] - half_w, triangle_center[1] - half_h};
            auto p1 = go::vf2{triangle_center[0] + half_w, triangle_center[1] - half_h};
            auto p2 = go::vf2{triangle_center[0], triangle_center[1] + half_h};
            ctx.geo.add_triangle(p0, p1, p2, m_style.text_color);
        }
        else
        {
            auto p0 = go::vf2{triangle_center[0] - half_h, triangle_center[1] - half_w};
            auto p1 = go::vf2{triangle_center[0] - half_h, triangle_center[1] + half_w};
            auto p2 = go::vf2{triangle_center[0] + half_h, triangle_center[1]};
            ctx.geo.add_triangle(p0, p1, p2, m_style.text_color);
        }

        if (!m_header.empty() && text_buf)
        {
            auto text_rect_pos = go::vf2{abs_pos[0] + m_padding + 12.0f, abs_pos[1]};
            auto text_rect_size = go::vf2{std::max(0.0f, m_size[0] - m_padding - 12.0f), m_header_height};
            auto text_pos = centered_single_line_text_pos(text_rect_pos, text_rect_size, m_header, font_atlas);
            text_buf->add(m_header, text_pos, m_style.text_color);
        }

        if (m_expanded && should_draw_background(m_style.draw_background, get_bg_color()) && m_size[1] > m_header_height)
        {
            ctx.geo.add_rect(
                {abs_pos[0], abs_pos[1] + m_header_height},
                {m_size[0], std::max(0.0f, m_size[1] - m_header_height)},
                get_bg_color()
            );
        }

        draw_element_border(ctx, abs_pos, m_size, get_border_color(), m_style.draw_border);

        if (!m_expanded)
            return;

        auto content_abs_pos = go::vf2{abs_pos[0], abs_pos[1] + m_header_height};
        auto content_abs_size = go::vf2{m_size[0], std::max(0.0f, m_size[1] - m_header_height)};
        auto [content_clip_pos, content_clip_size] = clip_rect(content_abs_pos, content_abs_size, new_clip_pos, new_clip_size);
        if (content_clip_size[0] <= 0.0f || content_clip_size[1] <= 0.0f)
            return;

        auto content_scissor = to_rect(content_clip_pos, content_clip_size);

        auto* clipped_geo = ctx.resolve_clipped_geo_buffer ? ctx.resolve_clipped_geo_buffer(content_scissor) : nullptr;
        auto* clipped_wire = ctx.resolve_clipped_wire_buffer ? ctx.resolve_clipped_wire_buffer(content_scissor) : nullptr;

        auto* child_geo = clipped_geo ? clipped_geo : &ctx.geo;
        auto* child_wire = clipped_wire ? clipped_wire : &ctx.wire;

        auto child_text_resolve = [&ctx, content_scissor](vk::FontId font) -> vk::TextBuffer*
        {
            if (ctx.resolve_clipped_text_buffer)
                return ctx.resolve_clipped_text_buffer(font, content_scissor);
            if (ctx.resolve_text_buffer)
                return ctx.resolve_text_buffer(font);
            return nullptr;
        };

        auto child_clipped_text_resolve = [&ctx](vk::FontId font, const VkRect2D& scissor) -> vk::TextBuffer*
        {
            if (!ctx.resolve_clipped_text_buffer)
                return nullptr;
            return ctx.resolve_clipped_text_buffer(font, scissor);
        };

        auto child_clipped_geo_resolve = [&ctx](const VkRect2D& scissor) -> vk::GeometryBuffer2D*
        {
            if (!ctx.resolve_clipped_geo_buffer)
                return nullptr;
            return ctx.resolve_clipped_geo_buffer(scissor);
        };

        auto child_clipped_wire_resolve = [&ctx](const VkRect2D& scissor) -> vk::GeometryBuffer2DWire*
        {
            if (!ctx.resolve_clipped_wire_buffer)
                return nullptr;
            return ctx.resolve_clipped_wire_buffer(scissor);
        };

        auto child_ctx = DrawContext{
            .geo = *child_geo,
            .wire = *child_wire,
            .resolve_text_buffer = child_text_resolve,
            .resolve_clipped_text_buffer = child_clipped_text_resolve,
            .resolve_clipped_geo_buffer = child_clipped_geo_resolve,
            .resolve_clipped_wire_buffer = child_clipped_wire_resolve,
            .default_font = ctx.default_font,
            .origin = {abs_pos[0], abs_pos[1] - m_scroll_y},
            .clip_pos = content_clip_pos,
            .clip_size = content_clip_size
        };

        for (auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            if (child->element_type() == ElementType::Dropdown)
                continue;
            child->draw(child_ctx);
        }

        for (auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            if (child->element_type() != ElementType::Dropdown)
                continue;
            child->draw(child_ctx);
        }

        auto max_scroll = max_scroll_y();
        if (max_scroll > 0.0f)
        {
            auto content_view_h = std::max(0.0f, m_size[1] - m_header_height);
            auto full_content_h = std::max(0.0f, content_height() - m_header_height);
            auto thumb_h = std::max(k_panel_scrollbar_min_thumb_height, content_view_h * (content_view_h / full_content_h));
            auto travel = std::max(0.0f, content_view_h - thumb_h);
            auto t = std::clamp(m_scroll_y / max_scroll, 0.0f, 1.0f);
            ctx.geo.add_rect(
                {abs_pos[0] + m_size[0] - k_panel_scrollbar_width, abs_pos[1] + m_header_height + travel * t},
                {k_panel_scrollbar_width, thumb_h},
                color_lightgrey
            );
        }

        ctx.clip_pos = parent_clip_pos;
        ctx.clip_size = parent_clip_size;
    }*/

    auto UIPanel::add(std::unique_ptr<UIElement> element) -> UIElement*
    {
        if (m_scrollable)
        {
            if (m_children.empty())
            {
                auto scroll_panel = std::make_unique<UIPanel>(false);
                //scroll_panel->set_size(m_size - go::vf2{get_padding(), get_padding()});
                scroll_panel->set_size(m_size);
                scroll_panel->set_position({0, 0});
                scroll_panel->set_draw_background(false);
                scroll_panel->set_draw_border(false);
                scroll_panel->set_padding(get_padding());
                scroll_panel->set_spacing(get_spacing());
                scroll_panel->m_parent = this;
                scroll_panel->set_layout(get_layout());
                m_children.push_back(std::move(scroll_panel));
            }

            auto* scroll_panel = static_cast<UIPanel*>(m_children.back().get());
            auto ptr = element.get();
            ptr->m_parent = scroll_panel;
            scroll_panel->add(std::move(element));
            mark_dirty();
            return ptr;
        }
        else
        {
            element->m_parent = this;
            m_children.push_back(std::move(element));
            mark_dirty();
            return m_children.back().get();
        }
    }

    auto UIPanel::apply_layout() -> void
    {
        if (m_layout == Layout::Free || m_scrollable)
            return;

        auto cursor = go::vf2{m_padding, m_padding};
        auto max_another_size = 0.0f;
        for (auto& child : m_children)
        {
            if (!child->m_visible) continue;
            child->set_position(cursor);
            if (m_layout == Layout::Horizontal)
            {
                cursor[0] += child->m_size[0] + m_spacing;
                max_another_size = std::max(max_another_size, child->m_size[1]);
            }
            else
            {
                cursor[1] += child->m_size[1] + m_spacing;
                max_another_size = std::max(max_another_size, child->m_size[0]);
            }
        }

        if (m_layout == Layout::Horizontal)
            m_size = 
            { 
                cursor[0] - m_spacing + m_padding, 
                std::max(max_another_size + m_padding * 2.0f, m_size[1])
            };
        else
            m_size = 
            { 
                std::max(max_another_size + m_padding * 2.0f, m_size[0]), 
                cursor[1] - m_spacing + m_padding 
            };
    }

    auto UIPanel::content_height() const -> float
    {
        auto max_bottom = m_padding;
        for (const auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            max_bottom = std::max(max_bottom, child->m_position[1] + child->m_size[1]);
        }
        return max_bottom + m_padding;
    }

    auto UIPanel::max_scroll_y() const -> float
    {
        if (!m_scrollable)
            return 0.0f;
        return std::max(0.0f, content_height() - m_size[1]);
    }

    auto UIPanel::clamp_scroll() -> void
    {
        auto max_scroll = max_scroll_y();
        m_scroll_offset[0] = 0.0f;
        m_scroll_offset[1] = std::clamp(m_scroll_offset[1], -max_scroll, 0.0f);
    }

    auto UIPanel::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled) return false;

        apply_layout();
        clamp_scroll();

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0, 0}, m_size);

        if (ev.type == EventType::Scroll && m_scrollable && inside)
        {
            m_scroll_offset[1] += ev.scroll_dy * 20.0f;
            clamp_scroll();
            mark_dirty();
            ev.consumed = true;
            return true;
        }

        auto child_ev = ev;
        child_ev.mouse_pos = {local[0] - m_scroll_offset[0], local[1] - m_scroll_offset[1]};

        auto is_pointer_event =
            ev.type == EventType::MouseMove ||
            ev.type == EventType::MousePress ||
            ev.type == EventType::MouseRelease ||
            ev.type == EventType::Scroll;

        // Open dropdowns get first pass
        if (is_pointer_event)
        {
            for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
            {
                if (!(*it)->is_visible()) continue;
                if ((*it)->element_type() != ElementType::Dropdown) continue;
                auto* dropdown = static_cast<UIDropdown*>(it->get());
                if (!dropdown->is_open()) continue;
                if ((*it)->on_event(child_ev))
                {
                    ev.consumed = child_ev.consumed;
                    ev.active_interaction_id = child_ev.active_interaction_id;
                    return true;
                }
            }
        }

        for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        {
            if ((*it)->on_event(child_ev))
            {
                ev.consumed = child_ev.consumed;
                ev.active_interaction_id = child_ev.active_interaction_id;
                return true;
            }
        }

        if (inside && (ev.type == EventType::MousePress || ev.type == EventType::MouseRelease))
        {
            ev.consumed = true;
            return true;
        }
        return false;
    }

    auto UIPanel::clear_interaction_state_recursive(uint32_t keep_id) -> void
    {
        for (auto& child : m_children)
        {
            if (!child) continue;
            if (child->get_id() != keep_id)
                child->clear_interaction_state();
            if (child->element_type() == ElementType::Panel)
                static_cast<UIPanel*>(child.get())->clear_interaction_state_recursive(keep_id);
            //if (child->element_type() == ElementType::ExpandablePanel)
            //    static_cast<UIExpandablePanel*>(child.get())->clear_interaction_state_recursive(keep_id);
        }
    }

    auto UIPanel::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;

        apply_layout();
        clamp_scroll();

        clear_draw_context(ctx);

        draw_element_background(ctx, go::vf2{0, 0}, m_size, get_bg_color(), m_style.draw_background, 0);

        auto* child_ctx = &ctx;

        if (m_scrollable)
        {
            auto max_scroll = max_scroll_y();
            if (max_scroll > 0.0f)
            {
                auto solid_buff = effective_solid_buffer(ctx, 2);
                if (solid_buff)
                {
                    auto content_h = content_height();
                    auto thumb_h = std::max(k_panel_scrollbar_min_thumb_height, m_size[1] * (m_size[1] / content_h));
                    auto travel = std::max(0.0f, m_size[1] - thumb_h);
                    auto t = std::clamp(max_scroll > 0.0f ? (-m_scroll_offset[1] / max_scroll) : 0.0f, 0.0f, 1.0f);
                    go::vf2 scroll_pos = {m_size[0] - k_panel_scrollbar_width, travel * t};
                    go::vf2 scroll_size = {k_panel_scrollbar_width, thumb_h};
                    solid_buff->add_rect(scroll_pos, scroll_size, color_lightgrey);
                }
            }
        }

        for (auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            if (child->element_type() == ElementType::Panel)
            {
                auto panel = static_cast<UIPanel*>(child.get());
                auto& panel_ctx = panel->get_draw_context();
                // TODO: need to clip this against parent ctx
                panel_ctx.viewport = to_rect(panel->m_position + m_position + m_scroll_offset, panel->m_size);
                panel_ctx.scissors = ctx.viewport;
                panel->update(panel_ctx);
            }
            else
                child->update(*child_ctx);
        }

        draw_element_border(ctx, go::vf2{0, 0}, m_size, get_border_color(), m_style.draw_border, 2);
    }

    auto UIPanel::upload(VkCommandBuffer cmd) -> void
    {
        if (!m_visible) return;

        upload_draw_context(get_draw_context(), cmd);

        for (auto& child : m_children)
        {
            if (child->is_visible() && child->element_type() == ElementType::Panel)
            {
                auto panel = static_cast<UIPanel*>(child.get());
                panel->upload(cmd);
            }
        }
    }

    auto UIPanel::draw(VkCommandBuffer cmd, vk::RenderingOverlay& overlay) -> void
    {
        if (!m_visible) return;

        /*VkRect2D viewport =
        {
            .offset = {static_cast<int32_t>(m_position[0]), static_cast<int32_t>(m_position[1])},
            .extent = {static_cast<uint32_t>(m_size[0]), static_cast<uint32_t>(m_size[1])}
        };*/

        auto draw_pass = [](const DrawContext::Pass& pass, VkCommandBuffer cmd, 
            vk::RenderingOverlay& overlay, const VkRect2D& viewport, const VkRect2D& scissors_main)
        {
            vk::view_set(cmd, viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissors_main);
            overlay.start_draw_2d(cmd);
            if (pass.solid) overlay.draw(cmd, pass.solid.value(), viewport);
            for (const auto& [buff, scissors] : pass.solids)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vkCmdSetScissor(cmd, 0, 1, &intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }

            //vkCmdSetScissor(cmd, 0, 1, &viewport);
            vkCmdSetScissor(cmd, 0, 1, &scissors_main);
            overlay.start_text_draw(cmd);
            for (auto& buff : pass.text)
                overlay.draw(cmd, buff, viewport);
            for (const auto& [buff, scissors] : pass.texts)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vkCmdSetScissor(cmd, 0, 1, &intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }

            vkCmdSetScissor(cmd, 0, 1, &scissors_main);
            overlay.start_draw_2d_wire(cmd);
            if (pass.wire) overlay.draw(cmd, pass.wire.value(), viewport);
            for (const auto& [buff, scissors] : pass.wires)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vkCmdSetScissor(cmd, 0, 1, &intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }
        };

        draw_pass(m_draw_ctx.passes[0], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);
        for (auto& child : m_children)
        {
            if (child->is_visible() && child->element_type() == ElementType::Panel)
                static_cast<UIPanel*>(child.get())->draw(cmd, overlay);
        }

        draw_pass(m_draw_ctx.passes[1], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);
        draw_pass(m_draw_ctx.passes[2], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);
    }

    // ===== UISystem =====

    /*auto UISystem::panel_count(const UIPanel& panel) const -> size_t
    {
        auto count = size_t{1};
        for (const auto& child : panel.children())
        {
            if (!child->is_visible()) continue;
            if (child->element_type() == ElementType::Panel)
                count += panel_count(static_cast<const UIPanel&>(*child));
        }
        return count;
    }

    auto UISystem::ensure_panel_buffers(size_t count) -> bool
    {
        if (m_panel_buffers.size() >= count)
            return true;

        while (m_panel_buffers.size() < count)
        {
            auto geo_res = vk::GeometryBuffer2D::create();
            auto wire_res = vk::GeometryBuffer2DWire::create();
            auto dropdown_geo_res = vk::GeometryBuffer2D::create();
            auto dropdown_wire_res = vk::GeometryBuffer2DWire::create();
            auto scrollbar_geo_res = vk::GeometryBuffer2D::create();

            if (!geo_res || !wire_res || !dropdown_geo_res || !dropdown_wire_res || !scrollbar_geo_res)
            {
                std::println("[ERROR] UISystem: failed to create panel buffers");
                return false;
            }

            m_panel_buffers.push_back(PanelDrawBuffers{
                .geo = std::move(geo_res.value()),
                .wire = std::move(wire_res.value()),
                .dropdown_geo = std::move(dropdown_geo_res.value()),
                .dropdown_wire = std::move(dropdown_wire_res.value()),
                .scrollbar_geo = std::move(scrollbar_geo_res.value())
            });
        }
        return true;
    }

    auto UISystem::ensure_panel_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font) -> vk::TextBuffer*
    {
        if (!font.ptr)
            return nullptr;
        auto font_index = static_cast<size_t>(font.font_index);
        if (panel_buf.text.size() <= font_index)
            panel_buf.text.resize(font_index + 1);
        auto& entry = panel_buf.text[font_index];
        if (!entry.has_value())
        {
            auto res = vk::TextBuffer::create(font);
            if (!res) { std::println("[ERROR] UISystem: failed to create TextBuffer"); return nullptr; }
            entry = std::move(res.value());
        }
        return &entry.value();
    }

    auto UISystem::ensure_panel_clipped_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font, const VkRect2D& scissor) -> vk::TextBuffer*
    {
        if (!font.ptr)
            return nullptr;
        auto entry_id = panel_buf.clipped_text_used++;
        if (panel_buf.clipped_text.size() <= entry_id)
            panel_buf.clipped_text.emplace_back();
        auto& entry = panel_buf.clipped_text[entry_id];
        entry.scissor = scissor;
        if (!entry.text.has_value() || entry.text->font_index() != font.font_index)
        {
            auto res = vk::TextBuffer::create(font);
            if (!res) { std::println("[ERROR] UISystem: failed to create clipped TextBuffer"); return nullptr; }
            entry.text = std::move(res.value());
        }
        return &entry.text.value();
    }

    auto UISystem::ensure_panel_clipped_geo_buffer(PanelDrawBuffers& panel_buf, const VkRect2D& scissor) -> vk::GeometryBuffer2D*
    {
        auto entry_id = panel_buf.clipped_geo_used++;
        if (panel_buf.clipped_geo.size() <= entry_id)
            panel_buf.clipped_geo.emplace_back();
        auto& entry = panel_buf.clipped_geo[entry_id];
        entry.scissor = scissor;
        if (!entry.geo.has_value())
        {
            auto res = vk::GeometryBuffer2D::create();
            if (!res)
            {
                std::println("[ERROR] UISystem: failed to create clipped GeometryBuffer2D");
                return nullptr;
            }
            entry.geo = std::move(res.value());
        }
        return &entry.geo.value();
    }

    auto UISystem::ensure_panel_clipped_wire_buffer(PanelDrawBuffers& panel_buf, const VkRect2D& scissor) -> vk::GeometryBuffer2DWire*
    {
        if (panel_buf.clipped_geo_used == 0)
            return nullptr;
        auto entry_id = panel_buf.clipped_geo_used - 1;
        if (panel_buf.clipped_geo.size() <= entry_id)
            return nullptr;
        auto& entry = panel_buf.clipped_geo[entry_id];
        entry.scissor = scissor;
        if (!entry.wire.has_value())
        {
            auto res = vk::GeometryBuffer2DWire::create();
            if (!res)
            {
                std::println("[ERROR] UISystem: failed to create clipped GeometryBuffer2DWire");
                return nullptr;
            }
            entry.wire = std::move(res.value());
        }
        return &entry.wire.value();
    }

    auto UISystem::ensure_panel_dropdown_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font) -> vk::TextBuffer*
    {
        if (!font.ptr)
            return nullptr;
        auto font_index = static_cast<size_t>(font.font_index);
        if (panel_buf.dropdown_text.size() <= font_index)
            panel_buf.dropdown_text.resize(font_index + 1);
        auto& entry = panel_buf.dropdown_text[font_index];
        if (!entry.has_value())
        {
            auto res = vk::TextBuffer::create(font);
            if (!res) { std::println("[ERROR] UISystem: failed to create dropdown TextBuffer"); return nullptr; }
            entry = std::move(res.value());
        }
        return &entry.value();
    }

    auto UISystem::skip_panel_ids(const UIPanel& panel, size_t& panel_id) -> void
    {
        panel_id++;
        for (const auto& child : panel.children())
        {
            if (!child->is_visible()) continue;
            if (child->element_type() == ElementType::Panel)
                skip_panel_ids(static_cast<const UIPanel&>(*child), panel_id);
        }
    }

    auto UISystem::build_panel_buffers(UIPanel& panel, const go::vf2& panel_abs_pos, const VkRect2D& parent_clip, size_t& panel_id) -> void
    {
        panel.apply_layout();
        panel.clamp_scroll();

        auto panel_rect = to_rect(panel_abs_pos, panel.m_size);
        auto panel_clip = rect_intersection(parent_clip, panel_rect);

        auto& panel_buf = m_panel_buffers[panel_id];

        // Skip rebuild if panel content is clean and position hasn't moved
        auto pos_changed = panel_buf.last_abs_pos[0] != panel_abs_pos[0] || panel_buf.last_abs_pos[1] != panel_abs_pos[1];
        if (!panel.is_dirty() && !pos_changed)
        {
            panel_buf.needs_upload = false;
            skip_panel_ids(panel, panel_id);
            return;
        }

        panel_buf.last_abs_pos = panel_abs_pos;
        panel_buf.needs_upload = true;
        panel_id++;

        panel_buf.view = panel_clip;
        panel_buf.geo.clear();
        panel_buf.wire.clear();
        panel_buf.clipped_text_used = 0;
        panel_buf.clipped_geo_used = 0;
        panel_buf.dropdown_geo.clear();
        panel_buf.dropdown_wire.clear();
        panel_buf.scrollbar_geo.clear();
        for (auto& tb : panel_buf.text)
            if (tb.has_value()) tb->clear();
        for (auto& tb : panel_buf.dropdown_text)
            if (tb.has_value()) tb->clear();
        for (auto& ct : panel_buf.clipped_text)
            if (ct.text.has_value()) ct.text->clear();
        for (auto& cg : panel_buf.clipped_geo)
        {
            if (cg.geo.has_value()) cg.geo->clear();
            if (cg.wire.has_value()) cg.wire->clear();
        }

        if (panel_clip.extent.width == 0 || panel_clip.extent.height == 0)
        {
            panel.clear_dirty();
            return;
        }

        auto clip_offset = go::vf2{static_cast<float>(panel_clip.offset.x), static_cast<float>(panel_clip.offset.y)};
        auto child_origin = panel_abs_pos + panel.m_scroll_offset;

        auto panel_ctx = DrawContext{
            .geo = panel_buf.geo,
            .wire = panel_buf.wire,
            .resolve_text_buffer = [this, &panel_buf](vk::FontId font) -> vk::TextBuffer* {
                return ensure_panel_text_buffer(panel_buf, font);
            },
            .resolve_clipped_text_buffer = [this, &panel_buf](vk::FontId font, const VkRect2D& scissor) -> vk::TextBuffer* {
                return ensure_panel_clipped_text_buffer(panel_buf, font, scissor);
            },
            .resolve_clipped_geo_buffer = [this, &panel_buf](const VkRect2D& scissor) -> vk::GeometryBuffer2D* {
                return ensure_panel_clipped_geo_buffer(panel_buf, scissor);
            },
            .resolve_clipped_wire_buffer = [this, &panel_buf](const VkRect2D& scissor) -> vk::GeometryBuffer2DWire* {
                return ensure_panel_clipped_wire_buffer(panel_buf, scissor);
            },
            .default_font = m_font,
            .origin = child_origin - clip_offset,
            .clip_pos = clip_offset,
            .clip_size = {static_cast<float>(panel_clip.extent.width), static_cast<float>(panel_clip.extent.height)}
        };

        auto dropdown_ctx = DrawContext{
            .geo = panel_buf.dropdown_geo,
            .wire = panel_buf.dropdown_wire,
            .resolve_text_buffer = [this, &panel_buf](vk::FontId font) -> vk::TextBuffer* {
                return ensure_panel_dropdown_text_buffer(panel_buf, font);
            },
            .resolve_clipped_text_buffer = [](vk::FontId, const VkRect2D&) -> vk::TextBuffer* {
                return nullptr;
            },
            .resolve_clipped_geo_buffer = [](const VkRect2D&) -> vk::GeometryBuffer2D* {
                return nullptr;
            },
            .resolve_clipped_wire_buffer = [](const VkRect2D&) -> vk::GeometryBuffer2DWire* {
                return nullptr;
            },
            .default_font = m_font,
            .origin = child_origin - clip_offset,
            .clip_pos = clip_offset,
            .clip_size = {static_cast<float>(panel_clip.extent.width), static_cast<float>(panel_clip.extent.height)}
        };

        if (should_draw_background(panel.m_style.draw_background, panel.get_bg_color()))
            panel_buf.geo.add_rect(panel_abs_pos - clip_offset, panel.m_size, panel.get_bg_color());
        if (panel.get_draw_border() && panel.get_border_color()[3] > 0)
            panel_buf.wire.add_rect(panel_abs_pos - clip_offset, panel.m_size, panel.get_border_color());

        // Draw non-panel, non-dropdown children
        for (auto& child : panel.children())
        {
            if (!child->is_visible()) continue;
            if (child->element_type() == ElementType::Panel)
            {
                auto* child_panel = static_cast<UIPanel*>(child.get());
                build_panel_buffers(*child_panel, child_origin + child_panel->m_position, panel_clip, panel_id);
            }
            else if (child->element_type() != ElementType::Dropdown)
            {
                child->draw(panel_ctx);
            }
        }

        // Draw dropdowns on top
        for (auto& child : panel.children())
        {
            if (!child->is_visible()) continue;
            if (child->element_type() == ElementType::Dropdown)
                child->draw(dropdown_ctx);
        }

        // Draw scrollbar
        if (panel.m_scrollable)
        {
            auto max_scroll = panel.max_scroll_y();
            if (max_scroll > 0.0f)
            {
                auto content_h = panel.content_height();
                auto thumb_h = std::max(k_panel_scrollbar_min_thumb_height, panel.m_size[1] * (panel.m_size[1] / content_h));
                auto travel = std::max(0.0f, panel.m_size[1] - thumb_h);
                auto t = std::clamp(max_scroll > 0.0f ? (-panel.m_scroll_offset[1] / max_scroll) : 0.0f, 0.0f, 1.0f);
                panel_buf.scrollbar_geo.add_rect(
                    go::vf2{panel_abs_pos[0] + panel.m_size[0] - k_panel_scrollbar_width, panel_abs_pos[1] + travel * t} - clip_offset,
                    {k_panel_scrollbar_width, thumb_h},
                    color_lightgrey
                );
            }
        }

        panel.clear_dirty();
    }*/

    auto UISystem::init() -> bool
    {
        m_initialized = true;
        return true;
    }

    auto UISystem::process_mouse_move(float x, float y) -> void
    {
        m_mouse_pos = {x, y};
        auto ev = UIEvent{.type = EventType::MouseMove, .mouse_pos = m_mouse_pos};
        m_root.on_event(ev);
    }

    auto UISystem::process_mouse_press(int button, int action) -> void
    {
        auto ev = UIEvent{
            .type = (action == 1) ? EventType::MousePress : EventType::MouseRelease,
            .mouse_pos = m_mouse_pos,
            .button = button
        };
        m_root.on_event(ev);
        if (button == 0 && action == 1)
            m_root.clear_interaction_state_recursive(ev.active_interaction_id);
    }

    auto UISystem::process_key(int key, int action, int mods) -> void
    {
        if (action == 2) return;
        auto ev = UIEvent{
            .type = (action == 1 || action == 2) ? EventType::KeyPress : EventType::KeyRelease,
            .mouse_pos = m_mouse_pos,
            .key = key,
            .mods = mods
        };
        m_root.on_event(ev);
    }

    auto UISystem::process_text_input(unsigned int codepoint) -> void
    {
        auto ev = UIEvent{.type = EventType::TextInput, .mouse_pos = m_mouse_pos, .codepoint = codepoint};
        m_root.on_event(ev);
    }

    auto UISystem::process_scroll(float dx, float dy) -> void
    {
        auto ev = UIEvent{
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
        m_root.set_position({0, 0});
        m_root.set_size({static_cast<float>(viewport.extent.width), static_cast<float>(viewport.extent.height)});
        auto& root_ctx = m_root.get_draw_context();
        root_ctx.viewport = viewport;
        root_ctx.scissors = viewport;
        m_root.update(root_ctx);
        m_root.upload(cmd);
        vk::cmd_sync_barriers(cmd);
    }

    auto UISystem::draw(VkCommandBuffer cmd, vk::RenderingOverlay& overlay, const VkRect2D& viewport) -> void
    {
        if (!m_initialized) return;
        vk::view_set(cmd, viewport);
        m_root.draw(cmd, overlay);
    }
}
