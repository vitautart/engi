#include "ui_system.hpp"

#include <algorithm>
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
        auto abs_pos = ctx.origin + position;
        auto font_h = ctx.font ? static_cast<float>(ctx.font->get_x_height()) : 12.0f;
        auto text_y = abs_pos[1] + (size[1] + font_h) * 0.5f; // baseline: center x-height inside element
        ctx.text.add(text, {abs_pos[0], text_y}, color);
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
        auto abs_pos = ctx.origin + position;

        auto& col = m_pressed ? color_pressed : (m_hovered ? color_hover : color_normal);
        ctx.geo.add_rect(abs_pos, size, col);
        ctx.wire.add_rect(abs_pos, size, go::vu4{100, 100, 130, 255});

        auto font_h = ctx.font ? static_cast<float>(ctx.font->get_x_height()) : 12.0f;
        auto text_x = abs_pos[0] + 6.0f;
        auto text_y = abs_pos[1] + (size[1] + font_h) * 0.5f; // baseline centered using x-height
        ctx.text.add(label, {text_x, text_y}, text_color);
    }

    // ===== UITextInput =====

    auto UITextInput::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::MousePress && ev.button == 0)
        {
            m_focused = inside;
            if (inside)
            {
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
        auto abs_pos = ctx.origin + position;

        ctx.geo.add_rect(abs_pos, size, bg_color);
        ctx.wire.add_rect(abs_pos, size, m_focused ? go::vu4{120, 120, 200, 255} : border_color);

        auto font_h = ctx.font ? static_cast<float>(ctx.font->get_x_height()) : 12.0f;
        auto advance = ctx.font ? static_cast<float>(ctx.font->get_line_height()) : 10.0f;
        auto text_x = abs_pos[0] + 4.0f;
        auto text_y = abs_pos[1] + (size[1] + font_h) * 0.5f; // baseline centered using x-height
        ctx.text.add(text, {text_x, text_y}, text_color);

        if (m_focused)
        {
            auto cursor_x = text_x + static_cast<float>(m_cursor) * advance;
            ctx.geo.add_rect(
                go::vf2{cursor_x, abs_pos[1] + 3.0f},
                go::vf2{2.0f, size[1] - 6.0f},
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

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::MousePress && ev.button == 0)
        {
            m_focused = inside;
            if (inside)
            {
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
        auto abs_pos = ctx.origin + position;

        ctx.geo.add_rect(abs_pos, size, bg_color);
        ctx.wire.add_rect(abs_pos, size, m_focused ? go::vu4{120, 120, 200, 255} : border_color);

        auto font_h = ctx.font ? static_cast<float>(ctx.font->get_x_height()) : 12.0f;
        auto line_h = ctx.font ? static_cast<float>(ctx.font->get_line_height()) : 16.0f;
        auto advance = ctx.font ? static_cast<float>(ctx.font->get_line_height()) : 10.0f;
        auto text_x = abs_pos[0] + 4.0f;
        // baseline for first line: top padding + x-height so that glyphs sit within padding
        auto text_y = abs_pos[1] + 4.0f + font_h;
        ctx.text.add(text, {text_x, text_y}, text_color);

        if (m_focused)
        {
            // Count lines and columns up to cursor
            uint32_t line = 0;
            uint32_t col = 0;
            for (uint32_t i = 0; i < m_cursor && i < text.size(); i++)
            {
                if (text[i] == L'\n')
                {
                    line++;
                    col = 0;
                }
                else
                {
                    col++;
                }
            }
            auto cursor_x = text_x + static_cast<float>(col) * advance;
            auto cursor_y = abs_pos[1] + 3.0f + static_cast<float>(line) * line_h;
            ctx.geo.add_rect(
                go::vf2{cursor_x, cursor_y},
                go::vf2{2.0f, line_h},
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
            auto font_h = ctx.font ? static_cast<float>(ctx.font->get_x_height()) : 12.0f;
            auto text_x = abs_pos[0] + box_sz[0] + 6.0f;
            auto text_y = abs_pos[1] + (box_sz[1] + font_h) * 0.5f; // baseline centered using x-height
            ctx.text.add(label, {text_x, text_y}, text_color);
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
        auto abs_pos = ctx.origin + position;

        auto font_h = ctx.font ? static_cast<float>(ctx.font->get_x_height()) : 12.0f;

        ctx.geo.add_rect(abs_pos, size, bg_color);
        ctx.wire.add_rect(abs_pos, size, go::vu4{100, 100, 130, 255});

        if (selected >= 0 && selected < static_cast<int>(items.size()))
        {
            auto text_y = abs_pos[1] + (size[1] + font_h) * 0.5f;
            ctx.text.add(items[selected], {abs_pos[0] + 6.0f, text_y}, text_color);
        }

        // Draw arrow indicator
        auto arrow_x = abs_pos[0] + size[0] - 16.0f;
        auto arrow_y = abs_pos[1] + (size[1] + font_h) * 0.5f;
        ctx.text.add(m_open ? L"\x25B2" : L"\x25BC", {arrow_x, arrow_y}, text_color);

        if (m_open)
        {
            auto item_height = size[1];
            for (int i = 0; i < static_cast<int>(items.size()); i++)
            {
                auto iy = abs_pos[1] + size[1] + static_cast<float>(i) * item_height;
                auto& col = (i == m_hovered_item) ? hover_color : bg_color;
                ctx.geo.add_rect(go::vf2{abs_pos[0], iy}, go::vf2{size[0], item_height}, col);
                ctx.wire.add_rect(go::vf2{abs_pos[0], iy}, go::vf2{size[0], item_height}, go::vu4{80, 80, 110, 255});
                auto item_y = iy + (item_height + font_h) * 0.5f;
                ctx.text.add(items[i], go::vf2{abs_pos[0] + 6.0f, item_y}, text_color);
            }
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

    auto UIPanel::on_event(UIEvent& ev) -> bool
    {
        if (!visible || !enabled) return false;

        apply_layout();

        auto local = go::vf2{ev.mouse_pos[0] - position[0], ev.mouse_pos[1] - position[1]};
        auto inside = point_in_rect(local, {0, 0}, size);

        if (ev.type == EventType::Scroll && scrollable && inside)
        {
            scroll_offset[1] += ev.scroll_dy * 20.0f;
            ev.consumed = true;
            return true;
        }

        // Translate mouse into children space (accounting for scroll)
        auto child_ev = ev;
        child_ev.mouse_pos = go::vf2{
            local[0] - scroll_offset[0],
            local[1] - scroll_offset[1]
        };

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

            auto text_res = vk::TextBuffer::create(m_font);
            if (!text_res)
            {
                std::println("[ERROR] UISystem: failed to create panel TextBuffer");
                return false;
            }

            m_panel_buffers.push_back(
                PanelDrawBuffers
                {
                    .geo = std::move(geo_res.value()),
                    .wire = std::move(wire_res.value()),
                    .text = std::move(text_res.value())
                }
            );
        }

        return true;
    }

    auto UISystem::build_panel_buffers(UIPanel& panel, const go::vf2& panel_abs_pos, const VkRect2D& parent_clip, size_t& panel_id) -> void
    {
        panel.apply_layout();

        auto panel_rect = to_rect(panel_abs_pos, panel.size);
        auto panel_clip = rect_intersection(parent_clip, panel_rect);

        auto& panel_buf = m_panel_buffers[panel_id++];
        panel_buf.view = panel_clip;
        panel_buf.geo.clear();
        panel_buf.wire.clear();
        panel_buf.text.clear();

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
            .text = panel_buf.text,
            .font = m_font.ptr,
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
                child->draw(panel_ctx);
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

            if (panel_buf.text.vertex_count() > 0)
            {
                if (auto r = panel_buf.text.upload(cmd); r)
                {
                    vk::add_vertex_buffer_write_barrier(panel_buf.text.vertex_buffer());
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

            overlay.start_draw_2d(cmd);
            overlay.draw(cmd, panel_buf.geo, panel_buf.view);

            overlay.start_draw_2d_wire(cmd);
            overlay.draw(cmd, panel_buf.wire, panel_buf.view);

            overlay.start_text_draw(cmd);
            overlay.draw(cmd, panel_buf.text, panel_buf.view);
        }

        vk::view_set(cmd, viewport);
    }
}
