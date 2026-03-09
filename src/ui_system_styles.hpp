#pragma once

#include <vector>
#include <vulkan/vulkan_core.h>

#include "gomath.hpp"
#include "rendering_overlay.hpp"

namespace engi::ui2
{
    inline constexpr auto color_darkgrey = go::vu4{64, 64, 64, 255};
    inline constexpr auto color_lightgrey = go::vu4{192, 192, 192, 255};
    inline constexpr auto color_white = go::vu4{255, 255, 255, 255};

    struct UILabelStyle
    {
        go::vu4 text_color = color_white;
        vk::FontId font = {};
    };

    struct UIButtonStyle
    {
        bool draw_background = true;
        bool draw_border = true;
        go::vu4 bg_color = color_darkgrey;
        go::vu4 border_color = color_lightgrey;
        go::vu4 text_color = color_white;

        go::vu4 hover_bg_color = color_lightgrey;
        go::vu4 hover_border_color = color_lightgrey;
        go::vu4 hover_text_color = color_white;

        go::vu4 pressed_bg_color = color_lightgrey;
        go::vu4 pressed_border_color = color_white;
        go::vu4 pressed_text_color = color_white;

        vk::FontId font = {};
    };

    struct UIPanelStyle
    {
        bool draw_background = false;
        bool draw_border = false;
        go::vu4 bg_color = color_darkgrey;
        go::vu4 border_color = color_lightgrey;
    };

    struct UITextInputStyle
    {
        bool draw_background = true;
        bool draw_border = true;
        go::vu4 bg_color = color_darkgrey;
        go::vu4 border_color = color_lightgrey;
        go::vu4 text_color = color_white;
        go::vu4 active_bg_color = color_darkgrey;
        go::vu4 active_border_color = color_white;
        go::vu4 active_text_color = color_white;
        go::vu4 cursor_color = color_white;
        vk::FontId font = {};
    };

    struct UITextAreaStyle
    {
        bool draw_background = true;
        bool draw_border = true;
        go::vu4 bg_color = color_darkgrey;
        go::vu4 border_color = color_lightgrey;
        go::vu4 text_color = color_white;
        go::vu4 active_bg_color = color_darkgrey;
        go::vu4 active_border_color = color_white;
        go::vu4 active_text_color = color_white;
        go::vu4 cursor_color = color_white;
        vk::FontId font = {};
    };

    struct UISliderStyle
    {
        go::vu4 track_color = color_lightgrey;
        go::vu4 handle_bg_color = color_darkgrey;
        go::vu4 handle_border_color = color_white;
        bool draw_handle_border = true;
    };

    struct UICheckboxStyle
    {
        go::vu4 box_color = color_darkgrey;
        go::vu4 border_color = color_lightgrey;
        go::vu4 check_color = color_lightgrey;
        go::vu4 text_color = color_white;
        vk::FontId font = {};
    };

    struct UIDropdownStyle
    {
        bool draw_background = true;
        bool draw_border = true;
        go::vu4 bg_color = color_darkgrey;
        go::vu4 border_color = color_lightgrey;
        go::vu4 hover_color = color_lightgrey;
        go::vu4 text_color = color_white;
        vk::FontId font = {};
    };

    struct UIStyleSheet
    {
        std::vector<UILabelStyle> label;
        std::vector<UIButtonStyle> button;
        std::vector<UITextInputStyle> text_input;
        std::vector<UITextAreaStyle> text_area;
        std::vector<UISliderStyle> slider;
        std::vector<UICheckboxStyle> checkbox;
        std::vector<UIDropdownStyle> dropdown;
        //std::vector<UIExpandablePanelStyle> expandable_panel;
        std::vector<UIPanelStyle> panel;
    };
}