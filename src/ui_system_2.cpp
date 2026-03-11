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

    auto cast_drawable(const UIElement* el) -> const UIDrawableElement*
    {
        switch (el->element_type())
        {
            case ElementType::Panel:
            case ElementType::ExpandablePanel:
                return static_cast<UIDrawableElement*>(const_cast<UIElement*>(el));
            default:
                return nullptr;
        }
    }

    auto cast_drawable(UIElement* el) -> UIDrawableElement*
    {
        switch (el->element_type())
        {
            case ElementType::Panel:
            case ElementType::ExpandablePanel:
                return static_cast<UIDrawableElement*>(el);
            default:
                return nullptr;
        }
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
                .x = static_cast<int32_t>(pos[0]),
                .y = static_cast<int32_t>(pos[1])
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

    static auto effective_text_buffer(vk::FontId font, DrawContext& ctx, size_t passId) -> vk::TextBuffer*
    {
        if (ctx.passes[passId].text.empty())
        {
            // add new text buffer for this font
            auto new_text_buff = vk::TextBuffer::create(font);
            if (!new_text_buff)
                return nullptr;
            ctx.passes[passId].text.emplace_back(std::move(new_text_buff.value()));
            return &ctx.passes[passId].text.back();
        }

        for(auto& text_buff : ctx.passes[passId].text)
        {
            if (text_buff.font_index() == font.font_index)
                return &text_buff;
            else
            {
                // add new text buffer for this font
                auto new_text_buff = vk::TextBuffer::create(font);
                if (!new_text_buff)
                    return nullptr;
                ctx.passes[passId].text.emplace_back(std::move(new_text_buff.value()));
                return &ctx.passes[passId].text.back();
            };
        }
        return nullptr;
    }

    static auto effective_text_buffer(vk::FontId font, DrawContext& ctx, size_t passId, VkRect2D rect) -> vk::TextBuffer*
    {
        // add new text buffer for this font
        auto new_text_buff = vk::TextBuffer::create(font);
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
    static constexpr auto k_panel_scrollbar_height = 4.0f;
    static constexpr auto k_panel_scrollbar_min_thumb_height = 12.0f;
    static constexpr auto k_panel_scrollbar_min_thumb_width = 12.0f;
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

    UIScrollablePanel::UIScrollablePanel()
    {
        m_layout = Layout::Vertical;
    }

    auto UIScrollablePanel::ensure_content_panel() -> UIAutoPanel*
    {
        if (m_content_panel)
            return m_content_panel;

        auto proxy_el = std::make_unique<UIAutoPanel>();
        proxy_el->set_position({0.0f, 0.0f});
        proxy_el->set_draw_background(false);
        proxy_el->set_draw_border(false);
        proxy_el->set_layout(m_layout);
        proxy_el->set_padding(m_padding);
        proxy_el->set_spacing(m_spacing);
        auto* proxy_ptr = proxy_el.get();
        UIPanel::add(std::move(proxy_el));
        m_content_panel = proxy_ptr;
        return m_content_panel;
    }

    auto UIScrollablePanel::sync_content_panel() -> void
    {
        auto* proxy = ensure_content_panel();
        if (!proxy)
            return;
        proxy->set_position({0.0f, 0.0f});
        proxy->set_layout(m_layout);
        proxy->set_padding(m_padding);
        proxy->set_spacing(m_spacing);
    }

    auto UIPanel::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        if (index >= style_sheet.panel.size())
            return;
        m_style = style_sheet.panel[index];
        mark_dirty();
    }

    auto UIPanel::set_layout(Layout l) -> void
    {
        if (!allow_layout(l))
            return;
        if (m_layout != l)
        {
            m_layout = l;
            mark_dirty();
        }
    }

    auto UIPanel::set_padding(float p) -> void
    {
        if (m_padding != p)
        {
            m_padding = p;
            mark_dirty();
        }
    }

    auto UIPanel::set_spacing(float s) -> void
    {
        if (m_spacing != s)
        {
            m_spacing = s;
            mark_dirty();
        }
    }
    auto UIPanel::set_draw_background(bool v) -> void { if (m_style.draw_background != v) { m_style.draw_background = v; mark_dirty(); } }
    auto UIPanel::set_bg_color(go::vu4 c) -> void { m_style.bg_color = c; mark_dirty(); }
    auto UIPanel::set_draw_border(bool v) -> void { if (m_style.draw_border != v) { m_style.draw_border = v; mark_dirty(); } }
    auto UIPanel::set_border_color(go::vu4 c) -> void { m_style.border_color = c; mark_dirty(); }

    // ===== UIExpandablePanel Setters =====

    auto UIExpandablePanel::applyStyleSheet(const UIStyleSheet& style_sheet, size_t index) -> void
    {
        UIPanel::applyStyleSheet(style_sheet, index);

        if (index < style_sheet.expandable_panel.size())
            m_style = style_sheet.expandable_panel[index];

        mark_dirty();
    }

    auto UIExpandablePanel::set_header(std::wstring text) -> void
    {
        m_header = std::move(text);
        mark_dirty();
    }

    auto UIExpandablePanel::set_expanded(bool expanded) -> void
    {
        if (m_expanded == expanded)
            return;

        if (!expanded)
            m_expanded_height = std::max(m_header_height, m_size[1]);

        m_expanded = expanded;
        sync_height_with_state();
        clamp_scroll();
        mark_dirty();
    }

    auto UIExpandablePanel::set_header_height(float h) -> void
    {
        auto new_h = std::max(1.0f, h);
        if (m_header_height == new_h)
            return;
        m_header_height = new_h;
        sync_height_with_state();
        clamp_scroll();
        mark_dirty();
    }

    auto UIExpandablePanel::set_header_bg_color(go::vu4 c) -> void
    {
        m_style.header_bg_color = c;
        mark_dirty();
    }

    auto UIExpandablePanel::set_text_color(go::vu4 c) -> void
    {
        m_style.text_color = c;
        mark_dirty();
    }

    // ===== UILabel =====

    auto UILabel::on_event(UIEvent&) -> bool
    {
        return false;
    }

    auto UILabel::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;

        auto text_buf = effective_text_buffer(m_style.font, ctx, 0);
        if (!text_buf) return;

        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;
        auto font_h = static_cast<float>(m_style.font.ptr->get_x_height());
        auto text_w = static_cast<float>(m_style.font.ptr->calculate_line_width(m_text));
        auto text_x = abs_pos[0];
        if (m_align == UILabelAlign::Center)
            text_x = std::floor(abs_pos[0] + (m_size[0] - text_w) * 0.5f);
        else if (m_align == UILabelAlign::Right)
            text_x = std::floor(abs_pos[0] + m_size[0] - text_w);
        auto text_y = std::floor(abs_pos[1] + (m_size[1] + font_h) * 0.5f);
        text_buf->add(m_text, {text_x, text_y}, m_style.text_color);
    }

    // ===== UIButton =====

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
        auto text_buf = effective_text_buffer(m_style.font, ctx, 0);
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

        auto text_pos = centered_single_line_text_pos(abs_pos, m_size, m_label, m_style.font.ptr);
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
        auto* font_atlas = m_style.font.ptr;
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
            if (auto* text_buf = effective_text_buffer(m_style.font, ctx, 0, scissors); text_buf)
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
        auto* font_atlas = m_style.font.ptr;
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
            if (auto* text_buf = effective_text_buffer(m_style.font, ctx, 0, scissors); text_buf)
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
        auto* text_buf = effective_text_buffer(m_style.font, ctx, 0);
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
            auto font_h = static_cast<float>(m_style.font.ptr->get_x_height());
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
        auto* font_atlas = m_style.font.ptr;
        //auto abs_pos = ctx.origin + m_position;
        auto abs_pos = m_position;

        if (m_open)
        {
            auto* text_buf = effective_text_buffer(m_style.font, ctx, 1);
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
            auto* text_buf = effective_text_buffer(m_style.font, ctx, 0);

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

    // ===== UIExpandablePanel =====

    UIExpandablePanel::UIExpandablePanel()
    {
        set_layout(Layout::Vertical);
        set_draw_background(true);
        set_bg_color(color_darkgrey);
        set_draw_border(true);
        set_border_color(color_lightgrey);
        m_expanded_height = std::max(m_header_height, m_size[1]);
    }

    auto UIExpandablePanel::sync_height_with_state() -> void
    {
        m_header_height = std::max(1.0f, m_header_height);
        m_expanded_height = std::max(m_header_height, m_expanded_height);

        if (m_expanded)
        {
            if (m_size[1] > m_header_height)
                m_expanded_height = std::max(m_header_height, m_size[1]);
            m_size[1] = m_expanded_height;
        }
        else
        {
            if (m_size[1] > m_header_height)
                m_expanded_height = std::max(m_header_height, m_size[1]);
            m_size[1] = m_header_height;
        }
    }

    auto UIExpandablePanel::on_event(UIEvent& ev) -> bool
    {
        sync_content_panel();
        sync_height_with_state();
        clamp_scroll();

        if (!m_visible || !m_enabled)
            return false;

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0.0f, 0.0f}, m_size);
        auto inside_header = point_in_rect(local, {0.0f, 0.0f}, {m_size[0], m_header_height});

        if (ev.type == EventType::MousePress && ev.button == 0 && inside_header)
        {
            set_expanded(!m_expanded);
            ev.active_interaction_id = m_id;
            ev.consumed = true;
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

        auto content_h = std::max(0.0f, m_size[1] - m_header_height);
        auto content_inside = point_in_rect(local, {0.0f, m_header_height}, {m_size[0], content_h});

        if (ev.type == EventType::Scroll && content_inside)
        {
            m_scroll_offset[1] += ev.scroll_dy * 20.0f;
            clamp_scroll();
            mark_dirty();
            ev.consumed = true;
            return true;
        }

        auto child_ev = ev;
        child_ev.mouse_pos = {
            local[0] - m_scroll_offset[0],
            local[1] - m_header_height - m_scroll_offset[1]
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
                if (!(*it)->is_visible())
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

    auto UIExpandablePanel::update(DrawContext& ctx) -> void
    {
        sync_content_panel();
        sync_height_with_state();
        clamp_scroll();

        if (!m_visible)
            return;

        clear_draw_context(ctx);

        auto* header_solid = effective_solid_buffer(ctx, 0);
        auto* header_text = effective_text_buffer(m_style.font, ctx, 0);

        if (header_solid && m_style.header_bg_color[3] > 0)
            header_solid->add_rect({0.0f, 0.0f}, {m_size[0], m_header_height}, m_style.header_bg_color);

        if (header_solid)
        {
            auto arrow_center = go::vf2{8.0f, m_header_height * 0.5f};
            auto side = std::max(6.0f, std::min(10.0f, m_header_height * 0.45f));
            auto half_w = side * 0.5f;
            auto half_h = side * 0.5f * std::sqrt(3.0f) * 0.5f;

            if (m_expanded)
            {
                auto p0 = go::vf2{arrow_center[0] - half_w, arrow_center[1] - half_h};
                auto p1 = go::vf2{arrow_center[0] + half_w, arrow_center[1] - half_h};
                auto p2 = go::vf2{arrow_center[0], arrow_center[1] + half_h};
                header_solid->add_triangle(p0, p1, p2, m_style.text_color);
            }
            else
            {
                auto p0 = go::vf2{arrow_center[0] - half_h, arrow_center[1] - half_w};
                auto p1 = go::vf2{arrow_center[0] - half_h, arrow_center[1] + half_w};
                auto p2 = go::vf2{arrow_center[0] + half_h, arrow_center[1]};
                header_solid->add_triangle(p0, p1, p2, m_style.text_color);
            }
        }

        if (header_text && !m_header.empty() && m_style.font.ptr)
        {
            auto text_pos = centered_single_line_text_pos({0.0f, 0.0f}, {m_size[0], m_header_height}, m_header, m_style.font.ptr);
            header_text->add(m_header, text_pos, m_style.text_color);
        }

        if (!m_expanded)
        {
            for (auto& child : m_children)
            {
                if (auto panel = cast_drawable(child.get()); panel != nullptr)
                    clear_draw_context(panel->get_draw_context());
            }

            draw_element_border(ctx, {0.0f, 0.0f}, m_size, get_border_color(), get_draw_border(), 2);
            return;
        }

        if (m_expanded)
        {
            auto panel_abs_pos = go::vf2{
                static_cast<float>(ctx.viewport.offset.x),
                static_cast<float>(ctx.viewport.offset.y)
            };
            auto content_pos = go::vf2{0.0f, m_header_height};
            auto content_size = go::vf2{m_size[0], std::max(0.0f, m_size[1] - m_header_height)};
            auto content_scissors = rect_intersection(ctx.scissors, to_rect(panel_abs_pos + content_pos, content_size));

            draw_element_background(ctx, content_pos, content_size, get_bg_color(), get_draw_background(), 0);

            for (auto& child : m_children)
            {
                if (!child->is_visible())
                    continue;

                if (auto panel = cast_drawable(child.get()); panel != nullptr)
                {
                    auto& panel_ctx = panel->get_draw_context();
                    panel_ctx.viewport = to_rect(panel_abs_pos + panel->get_position() + m_scroll_offset + content_pos, panel->get_size());
                    panel_ctx.scissors = content_scissors;
                    panel->update(panel_ctx);
                }
                else
                {
                    child->update(ctx);
                }
            }

            auto max_scroll = max_scroll_offset();
            auto content = m_content_panel ? m_content_panel->get_size() : go::vf2{0.0f, 0.0f};
            if (max_scroll[1] > 0.0f)
            {
                auto* scrollbar = effective_solid_buffer(ctx, 2);
                if (scrollbar)
                {
                    auto content_h = content[1];
                    auto thumb_h = std::max(k_panel_scrollbar_min_thumb_height, content_size[1] * (content_size[1] / content_h));
                    thumb_h = std::min(thumb_h, content_size[1]);
                    auto travel = std::max(0.0f, content_size[1] - thumb_h);
                    auto t = std::clamp((-m_scroll_offset[1] / max_scroll[1]), 0.0f, 1.0f);
                    scrollbar->add_rect({m_size[0] - k_panel_scrollbar_width, m_header_height + travel * t}, {k_panel_scrollbar_width, thumb_h}, color_lightgrey);
                }
            }
        }

        draw_element_border(ctx, {0.0f, 0.0f}, m_size, get_border_color(), get_draw_border(), 2);
    }

    auto UIPanel::add(std::unique_ptr<UIElement> element) -> UIElement*
    {
        element->m_parent = this;
        m_children.push_back(std::move(element));
        mark_dirty();
        return m_children.back().get();
    }

    auto UIScrollablePanel::add(std::unique_ptr<UIElement> element) -> UIElement*
    {
        auto* proxy = ensure_content_panel();
        sync_content_panel();

        auto* ptr = element.get();
        proxy->add(std::move(element));
        mark_dirty();
        return ptr;
    }

    auto UIScrollablePanel::set_scroll_offset(go::vf2 off) -> void
    {
        if (m_scroll_offset[0] != off[0] || m_scroll_offset[1] != off[1])
        {
            m_scroll_offset = off;
            clamp_scroll();
            mark_dirty();
        }
    }

    auto UIPanel::apply_layout() -> void
    {
        if (m_layout == Layout::Horizontal || m_layout == Layout::Vertical)
        {
            auto cursor = go::vf2{m_padding, m_padding};
            for (auto& child : m_children)
            {
                if (!child->m_visible)
                    continue;
                child->set_position(cursor);
                if (m_layout == Layout::Horizontal)
                    cursor[0] += child->m_size[0] + m_spacing;
                else
                    cursor[1] += child->m_size[1] + m_spacing;
            }
        }
    }

    auto UIAutoPanel::apply_layout() -> void
    {
        UIPanel::apply_layout();
        m_size = content_size();
    }

    auto UIAutoPanel::update(DrawContext& ctx) -> void
    {
        UIPanel::update(ctx);
    }

    auto UIPanel::content_size() const -> go::vf2
    {
        auto max_right = m_padding;
        auto max_bottom = m_padding;
        for (const auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            max_right = std::max(max_right, child->m_position[0] + child->m_size[0]);
            max_bottom = std::max(max_bottom, child->m_position[1] + child->m_size[1]);
        }
        return {
            max_right + m_padding,
            max_bottom + m_padding
        };
    }

    auto UIScrollablePanel::max_scroll_offset() const -> go::vf2
    {
        auto* proxy = m_content_panel;
        if (!proxy)
            return {0.0f, 0.0f};

        auto content = proxy->get_size();
        auto view = content_viewport_size();
        auto max_x = 0.0f;
        auto max_y = 0.0f;
        if (m_layout == Layout::Horizontal)
            max_x = std::max(0.0f, content[0] - std::max(0.0f, view[0]));
        else if (m_layout == Layout::Vertical)
            max_y = std::max(0.0f, content[1] - std::max(0.0f, view[1]));
        return {max_x, max_y};
    }

    auto UIScrollablePanel::clamp_scroll() -> void
    {
        auto max_scroll = max_scroll_offset();
        m_scroll_offset[0] = std::clamp(m_scroll_offset[0], -max_scroll[0], 0.0f);
        m_scroll_offset[1] = std::clamp(m_scroll_offset[1], -max_scroll[1], 0.0f);
    }

    auto UIScrollablePanel::on_event(UIEvent& ev) -> bool
    {
        sync_content_panel();
        clamp_scroll();

        if (!m_visible || !m_enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0, 0}, m_size);
        auto content_offset = content_viewport_offset();
        auto content_size = content_viewport_size();
        auto inside_content = point_in_rect(local, content_offset, content_size);

        if (ev.type == EventType::Scroll && inside_content)
        {
            if (m_layout == Layout::Horizontal)
            {
                auto delta = std::abs(ev.scroll_dx) > std::abs(ev.scroll_dy) ? ev.scroll_dx : ev.scroll_dy;
                m_scroll_offset[0] += delta * 20.0f;
            }
            else
            {
                m_scroll_offset[1] += ev.scroll_dy * 20.0f;
            }
            clamp_scroll();
            mark_dirty();
            ev.consumed = true;
            return true;
        }

        auto child_ev = ev;
        child_ev.mouse_pos = {local[0] - m_scroll_offset[0], local[1] - m_scroll_offset[1]};
    child_ev.mouse_pos[0] -= content_offset[0];
    child_ev.mouse_pos[1] -= content_offset[1];

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

    auto UIPanel::on_event(UIEvent& ev) -> bool
    {
        if (!m_visible || !m_enabled) return false;

        apply_layout();

        auto local = go::vf2{ev.mouse_pos[0] - m_position[0], ev.mouse_pos[1] - m_position[1]};
        auto inside = point_in_rect(local, {0, 0}, m_size);

        auto child_ev = ev;
        child_ev.mouse_pos = local;

        auto is_pointer_event =
            ev.type == EventType::MouseMove ||
            ev.type == EventType::MousePress ||
            ev.type == EventType::MouseRelease ||
            ev.type == EventType::Scroll;

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
            if (child->element_type() == ElementType::ExpandablePanel)
                static_cast<UIPanel*>(child.get())->clear_interaction_state_recursive(keep_id);
        }
    }

    auto UIPanel::update(DrawContext& ctx) -> void
    {
        if (!m_visible) return;

        apply_layout();

        clear_draw_context(ctx);

        auto panel_abs_pos = go::vf2{
            static_cast<float>(ctx.viewport.offset.x),
            static_cast<float>(ctx.viewport.offset.y)
        };

        draw_element_background(ctx, go::vf2{0, 0}, m_size, get_bg_color(), m_style.draw_background, 0);

        auto* child_ctx = &ctx;

        for (auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            if (auto panel = cast_drawable(child.get()); panel != nullptr)
            {
                auto& panel_ctx = panel->get_draw_context();
                panel_ctx.viewport = to_rect(panel_abs_pos + panel->m_position, panel->m_size);
                panel_ctx.scissors = ctx.scissors;
                panel->update(panel_ctx);
            }
            else
                child->update(*child_ctx);
        }

        draw_element_border(ctx, go::vf2{0, 0}, m_size, get_border_color(), m_style.draw_border, 2);
    }

    auto UIScrollablePanel::update(DrawContext& ctx) -> void
    {
        sync_content_panel();
        clamp_scroll();

        if (!m_visible) return;

        clear_draw_context(ctx);
        draw_element_background(ctx, go::vf2{0, 0}, m_size, get_bg_color(), m_style.draw_background, 0);

        auto panel_abs_pos = go::vf2{
            static_cast<float>(ctx.viewport.offset.x),
            static_cast<float>(ctx.viewport.offset.y)
        };

        auto content_offset = content_viewport_offset();
        auto content_size = content_viewport_size();
        auto content_scissors = rect_intersection(ctx.scissors, to_rect(panel_abs_pos + content_offset, content_size));

        for (auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            if (auto panel = cast_drawable(child.get()); panel != nullptr)
            {
                auto& panel_ctx = panel->get_draw_context();
                panel_ctx.viewport = to_rect(panel_abs_pos + panel->get_position() + m_scroll_offset + content_offset, panel->get_size());
                panel_ctx.scissors = content_scissors;
                panel->update(panel_ctx);
            }
            else
                child->update(ctx);
        }

        draw_element_border(ctx, go::vf2{0, 0}, m_size, get_border_color(), m_style.draw_border, 2);


        auto max_scroll = max_scroll_offset();
        auto content = m_content_panel ? m_content_panel->get_size() : go::vf2{0.0f, 0.0f};

        if (max_scroll[1] > 0.0f)
        {
            auto solid_buff = effective_solid_buffer(ctx, 2);
            if (solid_buff)
            {
                auto content_h = content[1];
                auto view_h = std::max(0.0f, content_size[1]);
                auto thumb_h = std::max(k_panel_scrollbar_min_thumb_height, view_h * (view_h / content_h));
                thumb_h = std::min(thumb_h, view_h);
                auto travel = std::max(0.0f, view_h - thumb_h);
                auto t = std::clamp(max_scroll[1] > 0.0f ? (-m_scroll_offset[1] / max_scroll[1]) : 0.0f, 0.0f, 1.0f);
                go::vf2 scroll_pos = {content_offset[0] + content_size[0] - k_panel_scrollbar_width, content_offset[1] + travel * t};
                go::vf2 scroll_size = {k_panel_scrollbar_width, thumb_h};
                solid_buff->add_rect(scroll_pos, scroll_size, color_lightgrey);
            }
        }

        if (max_scroll[0] > 0.0f)
        {
            auto solid_buff = effective_solid_buffer(ctx, 2);
            if (solid_buff)
            {
                auto content_w = content[0];
                auto view_w = std::max(0.0f, content_size[0]);
                auto thumb_w = std::max(k_panel_scrollbar_min_thumb_width, view_w * (view_w / content_w));
                thumb_w = std::min(thumb_w, view_w);
                auto travel = std::max(0.0f, view_w - thumb_w);
                auto t = std::clamp(max_scroll[0] > 0.0f ? (-m_scroll_offset[0] / max_scroll[0]) : 0.0f, 0.0f, 1.0f);
                go::vf2 scroll_pos = {content_offset[0] + travel * t, content_offset[1] + content_size[1] - k_panel_scrollbar_height};
                go::vf2 scroll_size = {thumb_w, k_panel_scrollbar_height};
                solid_buff->add_rect(scroll_pos, scroll_size, color_lightgrey);
            }
        }
    }

    auto UIExpandablePanel::content_viewport_offset() const noexcept -> go::vf2
    {
        return {0.0f, m_header_height};
    }

    auto UIExpandablePanel::content_viewport_size() const noexcept -> go::vf2
    {
        if (!m_expanded)
            return {std::max(0.0f, m_size[0]), 0.0f};
        return {
            std::max(0.0f, m_size[0]),
            std::max(0.0f, m_size[1] - m_header_height)
        };
    }

    auto UIPanel::upload(VkCommandBuffer cmd) -> void
    {
        if (!m_visible) return;

        upload_draw_context(get_draw_context(), cmd);

        for (auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            if (auto panel = cast_drawable(child.get()); panel != nullptr)
                panel->upload(cmd);
        }
    }

    auto UIPanel::draw(VkCommandBuffer cmd, vk::RenderingOverlay& overlay) -> void
    {
        if (!m_visible) return;

        auto draw_pass = [](const DrawContext::Pass& pass, VkCommandBuffer cmd, 
            vk::RenderingOverlay& overlay, const VkRect2D& viewport, const VkRect2D& scissors_main)
        {
            vk::viewport_set(cmd, viewport);
            vk::scissors_set(cmd, scissors_main);
            overlay.start_draw_2d(cmd);
            if (pass.solid) overlay.draw(cmd, pass.solid.value(), viewport);
            for (const auto& [buff, scissors] : pass.solids)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vk::scissors_set(cmd, intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }

            vk::scissors_set(cmd, scissors_main);
            overlay.start_text_draw(cmd);
            for (auto& buff : pass.text)
                overlay.draw(cmd, buff, viewport);
            for (const auto& [buff, scissors] : pass.texts)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vk::scissors_set(cmd, intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }

            vk::scissors_set(cmd, scissors_main);
            overlay.start_draw_2d_wire(cmd);
            if (pass.wire) overlay.draw(cmd, pass.wire.value(), viewport);
            for (const auto& [buff, scissors] : pass.wires)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vk::scissors_set(cmd, intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }
        };

        draw_pass(m_draw_ctx.passes[0], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);
        for (auto& child : m_children)
        {
            if (!child->is_visible())
                continue;
            if (auto panel = cast_drawable(child.get()); panel != nullptr)
                panel->draw(cmd, overlay);
        }

        draw_pass(m_draw_ctx.passes[1], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);
        draw_pass(m_draw_ctx.passes[2], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);
    }

    auto UIExpandablePanel::draw(VkCommandBuffer cmd, vk::RenderingOverlay& overlay) -> void
    {
        if (!m_visible)
            return;

        auto draw_pass = [](const DrawContext::Pass& pass, VkCommandBuffer cmd,
            vk::RenderingOverlay& overlay, const VkRect2D& viewport, const VkRect2D& scissors_main)
        {
            vk::viewport_set(cmd, viewport);
            vk::scissors_set(cmd, scissors_main);
            overlay.start_draw_2d(cmd);
            if (pass.solid)
                overlay.draw(cmd, pass.solid.value(), viewport);
            for (const auto& [buff, scissors] : pass.solids)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vk::scissors_set(cmd, intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }

            vk::scissors_set(cmd, scissors_main);
            overlay.start_text_draw(cmd);
            for (auto& buff : pass.text)
                overlay.draw(cmd, buff, viewport);
            for (const auto& [buff, scissors] : pass.texts)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vk::scissors_set(cmd, intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }

            vk::scissors_set(cmd, scissors_main);
            overlay.start_draw_2d_wire(cmd);
            if (pass.wire)
                overlay.draw(cmd, pass.wire.value(), viewport);
            for (const auto& [buff, scissors] : pass.wires)
            {
                auto intersected_scissor = rect_intersection(scissors_main, scissors);
                vk::scissors_set(cmd, intersected_scissor);
                overlay.draw(cmd, buff, viewport);
            }
        };

        draw_pass(m_draw_ctx.passes[0], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);

        if (m_expanded)
        {
            for (auto& child : m_children)
            {
                if (!child->is_visible())
                    continue;
                if (auto panel = cast_drawable(child.get()); panel != nullptr)
                    panel->draw(cmd, overlay);
            }
        }

        draw_pass(m_draw_ctx.passes[1], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);
        draw_pass(m_draw_ctx.passes[2], cmd, overlay, m_draw_ctx.viewport, m_draw_ctx.scissors);
    }

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
