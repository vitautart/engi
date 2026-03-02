#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gomath.hpp"
#include "rendering_overlay.hpp"

namespace engi::ui
{
    // ===== GLFW Key Constants =====

    namespace keys
    {
        constexpr int Backspace = 259;
        constexpr int Delete    = 261;
        constexpr int Right     = 262;
        constexpr int Left      = 263;
        constexpr int Enter     = 257;
        constexpr int Home      = 268;
        constexpr int End       = 269;
    }

    // ===== Event Types =====

    enum class EventType
    {
        MouseMove,
        MousePress,
        MouseRelease,
        Scroll,
        KeyPress,
        KeyRelease,
        TextInput
    };

    struct UIEvent
    {
        EventType type;
        go::vf2 mouse_pos;
        int button = 0;
        int key = 0;
        int mods = 0;
        float scroll_dx = 0.0f;
        float scroll_dy = 0.0f;
        unsigned int codepoint = 0;
        uint32_t active_interaction_id = 0;
        bool consumed = false;
    };

    // ===== Layout =====

    enum class Layout
    {
        Free,
        Horizontal,
        Vertical
    };

    // ===== Element Type =====

    enum class ElementType
    {
        Label,
        Button,
        TextInput,
        TextArea,
        Slider,
        Checkbox,
        Dropdown,
        Panel
    };

    // ===== Draw Context =====

    struct DrawContext
    {
        vk::GeometryBuffer2D& geo;
        vk::GeometryBuffer2DWire& wire;
        std::function<vk::TextBuffer*(vk::FontId)> resolve_text_buffer;
        std::function<vk::TextBuffer*(vk::FontId, const VkRect2D&)> resolve_clipped_text_buffer;
        vk::FontId default_font;
        go::vf2 origin;
        go::vf2 clip_pos;
        go::vf2 clip_size;
    };

    // ===== Element ID Generator =====

    auto next_element_id() -> uint32_t;

    // ===== UIElement (abstract base) =====

    class UIPanel;

    class UIElement
    {
    public:
        UIElement();
        virtual ~UIElement() = default;
        UIElement(const UIElement&) = delete;
        auto operator=(const UIElement&) -> UIElement& = delete;
        UIElement(UIElement&&) noexcept = default;
        auto operator=(UIElement&&) noexcept -> UIElement& = default;

        virtual auto on_event(UIEvent& ev) -> bool = 0;
        virtual auto draw(DrawContext& ctx) -> void = 0;
        virtual auto clear_interaction_state() -> void {}
        virtual auto element_type() const -> ElementType = 0;

        auto get_id() const noexcept -> uint32_t { return m_id; }

        auto set_position(go::vf2 pos) -> void;
        auto get_position() const noexcept -> go::vf2 { return m_position; }

        auto set_size(go::vf2 sz) -> void;
        auto get_size() const noexcept -> go::vf2 { return m_size; }

        auto set_visible(bool v) -> void;
        auto is_visible() const noexcept -> bool { return m_visible; }

        auto set_enabled(bool e) -> void;
        auto is_enabled() const noexcept -> bool { return m_enabled; }

        auto set_font(vk::FontId f) -> void;
        auto get_font() const noexcept -> vk::FontId { return m_font; }

        auto is_dirty() const noexcept -> bool { return m_dirty; }
        auto mark_dirty() -> void;
        auto clear_dirty() -> void { m_dirty = false; }

    protected:
        uint32_t m_id;
        go::vf2 m_position = {0.0f, 0.0f};
        go::vf2 m_size = {100.0f, 30.0f};
        bool m_visible = true;
        bool m_enabled = true;
        vk::FontId m_font = {};
        bool m_dirty = true;
        UIPanel* m_parent = nullptr;

        friend class UIPanel;
    };

    // ===== UILabel =====

    enum class UILabelAlign
    {
        Left,
        Center,
        Right
    };

    class UILabel : public UIElement
    {
    public:
        UILabel() = default;

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto element_type() const -> ElementType override { return ElementType::Label; }

        auto set_text(std::wstring t) -> void;
        auto get_text() const -> const std::wstring& { return m_text; }

        auto set_color(go::vu4 c) -> void;
        auto get_color() const noexcept -> go::vu4 { return m_color; }

        auto set_align(UILabelAlign a) -> void;
        auto get_align() const noexcept -> UILabelAlign { return m_align; }

    private:
        std::wstring m_text;
        go::vu4 m_color = {255, 255, 255, 255};
        UILabelAlign m_align = UILabelAlign::Left;
    };

    // ===== UIButton =====

    class UIButton : public UIElement
    {
    public:
        UIButton() = default;

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto element_type() const -> ElementType override { return ElementType::Button; }

        auto set_label(std::wstring t) -> void;
        auto get_label() const -> const std::wstring& { return m_label; }

        auto set_color_normal(go::vu4 c) -> void;
        auto get_color_normal() const noexcept -> go::vu4 { return m_color_normal; }

        auto set_color_hover(go::vu4 c) -> void;
        auto get_color_hover() const noexcept -> go::vu4 { return m_color_hover; }

        auto set_color_pressed(go::vu4 c) -> void;
        auto get_color_pressed() const noexcept -> go::vu4 { return m_color_pressed; }

        auto set_text_color(go::vu4 c) -> void;
        auto get_text_color() const noexcept -> go::vu4 { return m_text_color; }

        std::function<void()> on_click;

    private:
        std::wstring m_label;
        go::vu4 m_color_normal  = {60, 60, 80, 255};
        go::vu4 m_color_hover   = {80, 80, 110, 255};
        go::vu4 m_color_pressed = {40, 40, 60, 255};
        go::vu4 m_text_color    = {255, 255, 255, 255};
        bool m_hovered = false;
        bool m_pressed = false;
    };

    // ===== UITextInput (single-line) =====

    class UITextInput : public UIElement
    {
    public:
        UITextInput() = default;

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto clear_interaction_state() -> void override;
        auto element_type() const -> ElementType override { return ElementType::TextInput; }

        auto set_text(std::wstring t) -> void;
        auto get_text() const -> const std::wstring& { return m_text; }

        auto set_bg_color(go::vu4 c) -> void;
        auto get_bg_color() const noexcept -> go::vu4 { return m_bg_color; }

        auto set_text_color(go::vu4 c) -> void;
        auto get_text_color() const noexcept -> go::vu4 { return m_text_color; }

        auto set_cursor_color(go::vu4 c) -> void;
        auto get_cursor_color() const noexcept -> go::vu4 { return m_cursor_color; }

        auto set_border_color(go::vu4 c) -> void;
        auto get_border_color() const noexcept -> go::vu4 { return m_border_color; }

        std::function<void(const std::wstring&)> on_change;

    private:
        std::wstring m_text;
        go::vu4 m_bg_color     = {30, 30, 45, 255};
        go::vu4 m_text_color   = {255, 255, 255, 255};
        go::vu4 m_cursor_color = {255, 255, 255, 200};
        go::vu4 m_border_color = {80, 80, 110, 255};
        bool m_focused = false;
        uint32_t m_cursor = 0;
        float m_scroll_x = 0.0f;
    };

    // ===== UITextArea (multi-line) =====

    class UITextArea : public UIElement
    {
    public:
        UITextArea();

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto clear_interaction_state() -> void override;
        auto element_type() const -> ElementType override { return ElementType::TextArea; }

        auto set_text(std::wstring t) -> void;
        auto get_text() const -> const std::wstring& { return m_text; }

        auto set_bg_color(go::vu4 c) -> void;
        auto get_bg_color() const noexcept -> go::vu4 { return m_bg_color; }

        auto set_text_color(go::vu4 c) -> void;
        auto get_text_color() const noexcept -> go::vu4 { return m_text_color; }

        auto set_cursor_color(go::vu4 c) -> void;
        auto get_cursor_color() const noexcept -> go::vu4 { return m_cursor_color; }

        auto set_border_color(go::vu4 c) -> void;
        auto get_border_color() const noexcept -> go::vu4 { return m_border_color; }

        std::function<void(const std::wstring&)> on_change;

    private:
        std::wstring m_text;
        go::vu4 m_bg_color     = {30, 30, 45, 255};
        go::vu4 m_text_color   = {255, 255, 255, 255};
        go::vu4 m_cursor_color = {255, 255, 255, 200};
        go::vu4 m_border_color = {80, 80, 110, 255};
        bool m_focused = false;
        uint32_t m_cursor = 0;
        float m_scroll_x = 0.0f;
        float m_scroll_y = 0.0f;
    };

    // ===== UISlider =====

    class UISlider : public UIElement
    {
    public:
        UISlider() = default;

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto element_type() const -> ElementType override { return ElementType::Slider; }

        auto set_value(float v) -> void;
        auto get_value() const noexcept -> float { return m_value; }

        auto set_min_val(float v) -> void;
        auto get_min_val() const noexcept -> float { return m_min_val; }

        auto set_max_val(float v) -> void;
        auto get_max_val() const noexcept -> float { return m_max_val; }

        auto set_track_color(go::vu4 c) -> void;
        auto get_track_color() const noexcept -> go::vu4 { return m_track_color; }

        auto set_handle_color(go::vu4 c) -> void;
        auto get_handle_color() const noexcept -> go::vu4 { return m_handle_color; }

        std::function<void(float)> on_change;

    private:
        float m_value = 0.0f;
        float m_min_val = 0.0f;
        float m_max_val = 1.0f;
        go::vu4 m_track_color  = {40, 40, 60, 255};
        go::vu4 m_handle_color = {100, 100, 160, 255};
        bool m_dragging = false;
    };

    // ===== UICheckbox =====

    class UICheckbox : public UIElement
    {
    public:
        UICheckbox();

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto element_type() const -> ElementType override { return ElementType::Checkbox; }

        auto set_checked(bool v) -> void;
        auto is_checked() const noexcept -> bool { return m_checked; }

        auto set_label(std::wstring t) -> void;
        auto get_label() const -> const std::wstring& { return m_label; }

        auto set_box_color(go::vu4 c) -> void;
        auto get_box_color() const noexcept -> go::vu4 { return m_box_color; }

        auto set_check_color(go::vu4 c) -> void;
        auto get_check_color() const noexcept -> go::vu4 { return m_check_color; }

        auto set_text_color(go::vu4 c) -> void;
        auto get_text_color() const noexcept -> go::vu4 { return m_text_color; }

        std::function<void(bool)> on_change;

    private:
        bool m_checked = false;
        std::wstring m_label;
        go::vu4 m_box_color    = {60, 60, 80, 255};
        go::vu4 m_check_color  = {100, 200, 100, 255};
        go::vu4 m_text_color   = {255, 255, 255, 255};
    };

    // ===== UIDropdown =====

    class UIDropdown : public UIElement
    {
    public:
        UIDropdown() = default;

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto is_open() const noexcept -> bool { return m_open; }
        auto clear_interaction_state() -> void override;
        auto element_type() const -> ElementType override { return ElementType::Dropdown; }

        auto set_items(std::vector<std::wstring> items) -> void;
        auto get_items() const -> const std::vector<std::wstring>& { return m_items; }

        auto set_selected(int idx) -> void;
        auto get_selected() const noexcept -> int { return m_selected; }

        auto set_bg_color(go::vu4 c) -> void;
        auto get_bg_color() const noexcept -> go::vu4 { return m_bg_color; }

        auto set_hover_color(go::vu4 c) -> void;
        auto get_hover_color() const noexcept -> go::vu4 { return m_hover_color; }

        auto set_text_color(go::vu4 c) -> void;
        auto get_text_color() const noexcept -> go::vu4 { return m_text_color; }

        std::function<void(int)> on_change;

    private:
        std::vector<std::wstring> m_items;
        int m_selected = -1;
        go::vu4 m_bg_color      = {50, 50, 70, 255};
        go::vu4 m_hover_color   = {70, 70, 100, 255};
        go::vu4 m_text_color    = {255, 255, 255, 255};
        bool m_open = false;
        int m_hovered_item = -1;
    };

    // ===== UIPanel (aggregate element) =====

    class UIPanel : public UIElement
    {
    public:
        UIPanel() = default;

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto element_type() const -> ElementType override { return ElementType::Panel; }

        auto add(std::unique_ptr<UIElement> element) -> UIElement*;

        template<typename T, typename... Args>
        auto add_new(Args&&... args) -> T*
        {
            auto el = std::make_unique<T>(std::forward<Args>(args)...);
            auto ptr = el.get();
            add(std::move(el));
            return ptr;
        }

        auto children() -> std::vector<std::unique_ptr<UIElement>>& { return m_children; }
        auto children() const -> const std::vector<std::unique_ptr<UIElement>>& { return m_children; }

        auto set_layout(Layout l) -> void;
        auto get_layout() const noexcept -> Layout { return m_layout; }

        auto set_padding(float p) -> void;
        auto get_padding() const noexcept -> float { return m_padding; }

        auto set_spacing(float s) -> void;
        auto get_spacing() const noexcept -> float { return m_spacing; }

        auto set_scrollable(bool s) -> void;
        auto is_scrollable() const noexcept -> bool { return m_scrollable; }

        auto set_scroll_offset(go::vf2 off) -> void;
        auto get_scroll_offset() const noexcept -> go::vf2 { return m_scroll_offset; }

        auto set_draw_background(bool v) -> void;
        auto get_draw_background() const noexcept -> bool { return m_draw_background; }

        auto set_bg_color(go::vu4 c) -> void;
        auto get_bg_color() const noexcept -> go::vu4 { return m_bg_color; }

        auto set_draw_border(bool v) -> void;
        auto get_draw_border() const noexcept -> bool { return m_draw_border; }

        auto set_border_color(go::vu4 c) -> void;
        auto get_border_color() const noexcept -> go::vu4 { return m_border_color; }

    private:
        friend class UISystem;
        auto apply_layout() -> void;
        auto content_height() const -> float;
        auto max_scroll_y() const -> float;
        auto clamp_scroll() -> void;
        auto clear_interaction_state_recursive(uint32_t keep_id) -> void;

        std::vector<std::unique_ptr<UIElement>> m_children;
        Layout m_layout = Layout::Free;
        float m_padding = 4.0f;
        float m_spacing = 4.0f;
        bool m_scrollable = false;
        go::vf2 m_scroll_offset = {0.0f, 0.0f};
        bool m_draw_background = false;
        go::vu4 m_bg_color = {0, 0, 0, 0};
        bool m_draw_border = false;
        go::vu4 m_border_color = {100, 100, 130, 255};
    };

    // ===== UISystem =====

    class UISystem
    {
    public:
        UISystem() = default;
        UISystem(const UISystem&) = delete;
        auto operator=(const UISystem&) -> UISystem& = delete;
        UISystem(UISystem&&) noexcept = default;
        auto operator=(UISystem&&) noexcept -> UISystem& = default;

        auto init(vk::FontId font) -> bool;

        auto root() -> UIPanel& { return m_root; }
        auto root() const -> const UIPanel& { return m_root; }

        auto process_mouse_move(float x, float y) -> void;
        auto process_mouse_press(int button, int action) -> void;
        auto process_key(int key, int action, int mods) -> void;
        auto process_text_input(unsigned int codepoint) -> void;
        auto process_scroll(float dx, float dy) -> void;

        auto sync(VkCommandBuffer cmd, const VkRect2D& viewport) -> void;
        auto draw(VkCommandBuffer cmd, vk::RenderingOverlay& overlay, const VkRect2D& viewport) -> void;

    private:
        struct PanelDrawBuffers
        {
            struct ClippedTextDraw
            {
                std::optional<vk::TextBuffer> text;
                VkRect2D scissor = {};
            };

            vk::GeometryBuffer2D geo;
            vk::GeometryBuffer2DWire wire;
            std::vector<std::optional<vk::TextBuffer>> text;
            std::vector<ClippedTextDraw> clipped_text;
            size_t clipped_text_used = 0;

            vk::GeometryBuffer2D dropdown_geo;
            vk::GeometryBuffer2DWire dropdown_wire;
            std::vector<std::optional<vk::TextBuffer>> dropdown_text;

            vk::GeometryBuffer2D scrollbar_geo;
            VkRect2D view = {};
            bool needs_upload = false;
            go::vf2 last_abs_pos = {-1.0f, -1.0f};
        };

        auto panel_count(const UIPanel& panel) const -> size_t;
        auto ensure_panel_buffers(size_t count) -> bool;
        auto ensure_panel_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font) -> vk::TextBuffer*;
        auto ensure_panel_clipped_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font, const VkRect2D& scissor) -> vk::TextBuffer*;
        auto ensure_panel_dropdown_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font) -> vk::TextBuffer*;
        auto build_panel_buffers(UIPanel& panel, const go::vf2& panel_abs_pos, const VkRect2D& parent_clip, size_t& panel_id) -> void;
        auto skip_panel_ids(const UIPanel& panel, size_t& panel_id) -> void;

        UIPanel m_root;
        std::vector<PanelDrawBuffers> m_panel_buffers;
        size_t m_used_panel_buffers = 0;
        vk::FontId m_font;
        go::vf2 m_mouse_pos = {0.0f, 0.0f};
        bool m_initialized = false;
    };
}
