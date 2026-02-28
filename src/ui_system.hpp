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

        auto get_id() const noexcept -> uint32_t { return m_id; }

        go::vf2 position = {0.0f, 0.0f};
        go::vf2 size = {100.0f, 30.0f};
        bool visible = true;
        bool enabled = true;
        vk::FontId font = {};

    protected:
        uint32_t m_id;
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

        std::wstring text;
        go::vu4 color = {255, 255, 255, 255};
        UILabelAlign align = UILabelAlign::Left;
    };

    // ===== UIButton =====

    class UIButton : public UIElement
    {
    public:
        UIButton() = default;

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;

        std::wstring label;
        go::vu4 color_normal  = {60, 60, 80, 255};
        go::vu4 color_hover   = {80, 80, 110, 255};
        go::vu4 color_pressed = {40, 40, 60, 255};
        go::vu4 text_color    = {255, 255, 255, 255};
        std::function<void()> on_click;

    private:
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

        std::wstring text;
        go::vu4 bg_color     = {30, 30, 45, 255};
        go::vu4 text_color   = {255, 255, 255, 255};
        go::vu4 cursor_color = {255, 255, 255, 200};
        go::vu4 border_color = {80, 80, 110, 255};
        std::function<void(const std::wstring&)> on_change;

    private:
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

        std::wstring text;
        go::vu4 bg_color     = {30, 30, 45, 255};
        go::vu4 text_color   = {255, 255, 255, 255};
        go::vu4 cursor_color = {255, 255, 255, 200};
        go::vu4 border_color = {80, 80, 110, 255};
        std::function<void(const std::wstring&)> on_change;

    private:
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

        float value = 0.0f;
        float min_val = 0.0f;
        float max_val = 1.0f;
        go::vu4 track_color  = {40, 40, 60, 255};
        go::vu4 handle_color = {100, 100, 160, 255};
        std::function<void(float)> on_change;

    private:
        bool m_dragging = false;
    };

    // ===== UICheckbox =====

    class UICheckbox : public UIElement
    {
    public:
        UICheckbox();

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;

        bool checked = false;
        std::wstring label;
        go::vu4 box_color    = {60, 60, 80, 255};
        go::vu4 check_color  = {100, 200, 100, 255};
        go::vu4 text_color   = {255, 255, 255, 255};
        std::function<void(bool)> on_change;
    };

    // ===== UIDropdown =====

    class UIDropdown : public UIElement
    {
    public:
        UIDropdown() = default;

        auto on_event(UIEvent& ev) -> bool override;
        auto draw(DrawContext& ctx) -> void override;
        auto is_open() const -> bool { return m_open; }
        auto clear_interaction_state() -> void override;

        std::vector<std::wstring> items;
        int selected = -1;
        go::vu4 bg_color      = {50, 50, 70, 255};
        go::vu4 hover_color   = {70, 70, 100, 255};
        go::vu4 text_color    = {255, 255, 255, 255};
        std::function<void(int)> on_change;

    private:
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

        Layout layout = Layout::Free;
        float padding = 4.0f;
        float spacing = 4.0f;
        bool scrollable = false;
        go::vf2 scroll_offset = {0.0f, 0.0f};
        bool draw_background = false;
        go::vu4 bg_color = {0, 0, 0, 0};
        bool draw_border = false;
        go::vu4 border_color = {100, 100, 130, 255};

    private:
        friend class UISystem;
        auto apply_layout() -> void;
        auto content_height() const -> float;
        auto max_scroll_y() const -> float;
        auto clamp_scroll() -> void;
        auto clear_interaction_state_recursive(uint32_t keep_id) -> void;
        std::vector<std::unique_ptr<UIElement>> m_children;
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
        };

        auto panel_count(const UIPanel& panel) const -> size_t;
        auto ensure_panel_buffers(size_t count) -> bool;
        auto ensure_panel_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font) -> vk::TextBuffer*;
        auto ensure_panel_clipped_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font, const VkRect2D& scissor) -> vk::TextBuffer*;
        auto ensure_panel_dropdown_text_buffer(PanelDrawBuffers& panel_buf, vk::FontId font) -> vk::TextBuffer*;
        auto build_panel_buffers(UIPanel& panel, const go::vf2& panel_abs_pos, const VkRect2D& parent_clip, size_t& panel_id) -> void;

        UIPanel m_root;
        std::vector<PanelDrawBuffers> m_panel_buffers;
        size_t m_used_panel_buffers = 0;
        vk::FontId m_font;
        go::vf2 m_mouse_pos = {0.0f, 0.0f};
        bool m_initialized = false;
    };
}
