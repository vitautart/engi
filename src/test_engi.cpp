//#include "ui_system.hpp"
#include <test_engi.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <print>
#include <string>

#include <gocamera.hpp>
#include <rendering.hpp>
#include <vulkan/vulkan_core.h>

namespace
{
using namespace engi;

struct Vertex
{
    go::vf3 pos;
    go::vf3 color;
};

struct PushConstant
{
    go::mf4 mvp;
};

constexpr Vertex CUBE_VERTICES[] = {
    {{-1, -1,  1}, {1, 0, 0}},
    {{ 1, -1,  1}, {1, 0, 0}},
    {{ 1,  1,  1}, {1, 0, 0}},
    {{-1,  1,  1}, {1, 0, 0}},
    {{-1, -1, -1}, {0, 1, 0}},
    {{ 1, -1, -1}, {0, 1, 0}},
    {{ 1,  1, -1}, {0, 1, 0}},
    {{-1,  1, -1}, {0, 1, 0}},
    {{-1,  1,  1}, {0, 0, 1}},
    {{ 1,  1,  1}, {0, 0, 1}},
    {{ 1,  1, -1}, {0, 0, 1}},
    {{-1,  1, -1}, {0, 0, 1}},
    {{-1, -1,  1}, {1, 1, 0}},
    {{ 1, -1,  1}, {1, 1, 0}},
    {{ 1, -1, -1}, {1, 1, 0}},
    {{-1, -1, -1}, {1, 1, 0}},
    {{ 1, -1,  1}, {1, 0, 1}},
    {{ 1, -1, -1}, {1, 0, 1}},
    {{ 1,  1, -1}, {1, 0, 1}},
    {{ 1,  1,  1}, {1, 0, 1}},
    {{-1, -1,  1}, {0, 1, 1}},
    {{-1, -1, -1}, {0, 1, 1}},
    {{-1,  1, -1}, {0, 1, 1}},
    {{-1,  1,  1}, {0, 1, 1}},
};

constexpr uint16_t CUBE_INDICES[] = {
    0, 1, 2,  0, 2, 3,
    4, 6, 5,  4, 7, 6,
    8, 9, 10, 8, 10, 11,
    12, 14, 13, 12, 15, 14,
    16, 17, 18, 16, 18, 19,
    20, 22, 21, 20, 23, 22
};

auto hue_to_rgb(float h) noexcept -> go::vf3
{
    auto r = std::abs(h * 6.0f - 3.0f) - 1.0f;
    auto g = 2.0f - std::abs(h * 6.0f - 2.0f);
    auto b = 2.0f - std::abs(h * 6.0f - 4.0f);
    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
    return {r, g, b};
}
}

auto engi::TestEngi::create() noexcept -> std::unique_ptr<TestEngi>
{
    auto app = std::unique_ptr<TestEngi>(new TestEngi);
    if (!app)
    {
        std::println("[ERROR] Can't create app");
        return nullptr;
    }

    if (!app->initialize_window_and_renderer("TestEngi", 800, 600, false))
        return nullptr;

    app->m_index_count = sizeof(CUBE_INDICES) / sizeof(CUBE_INDICES[0]);
    return app;
}

auto engi::TestEngi::on_frame(VkCommandBuffer cmd, uint32_t frame_id) -> void
{
    init(cmd, frame_id);
    render(cmd);
}

auto engi::TestEngi::on_shutdown() -> void
{
    cleanup();
}

auto engi::TestEngi::on_text_input(unsigned int codepoint) -> void
{
    m_ui.process_text_input(codepoint);
}

auto engi::TestEngi::on_key_press(int key, int scancode, int action, int mods) -> void
{
    App::on_key_press(key, scancode, action, mods);
    m_ui.process_key(key, action, mods);
}

auto engi::TestEngi::on_mouse_press(int button, int action, int) -> void
{
    m_ui.process_mouse_press(button, action);
}

auto engi::TestEngi::on_mouse_move(double x, double y) -> void
{
    m_ui.process_mouse_move(static_cast<float>(x), static_cast<float>(y));
}

auto engi::TestEngi::on_scroll(double xoffset, double yoffset) -> void
{
    m_ui.process_scroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
}

auto engi::TestEngi::init(VkCommandBuffer cmd, uint32_t frame_id) -> void
{
    if (m_initialized)
        return;

    auto layout_result = vk::LayoutBuilder()
        .push_const(0, sizeof(PushConstant), VK_SHADER_STAGE_VERTEX_BIT)
        .build();
    if (!layout_result)
    {
        std::println("[ERROR] Failed to create cube pipeline layout");
        return;
    }
    m_layout = std::move(layout_result.value());

    auto binding = VkVertexInputBindingDescription{
        .binding = 0,
        .stride = sizeof(Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

    auto attributes = std::array<VkVertexInputAttributeDescription, 2>{
        VkVertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, pos)
        },
        VkVertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = VK_FORMAT_R32G32B32_SFLOAT,
            .offset = offsetof(Vertex, color)
        }
    };

    auto pipeline_result = vk::PipelineBuilder()
        .vertex_shader_from_file("shaders/cube_vert.spv")
        .fragment_shader_from_file("shaders/cube_frag.spv")
        .color_format(vk::color_format())
        .depth_format(vk::depth_format())
        .samples(vk::sample_count())
        .add(binding)
        .add(attributes[0])
        .add(attributes[1])
        .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .polygon_mode(VK_POLYGON_MODE_FILL)
        .cull_mode(VK_CULL_MODE_BACK_BIT)
        .front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .depth_test(true)
        .depth_write(true)
        .depth_compare_op(VK_COMPARE_OP_GREATER)
        .no_blending()
        .build(m_layout.get());
    if (!pipeline_result)
    {
        std::println("[ERROR] Failed to create cube pipeline");
        return;
    }
    m_pipeline = std::move(pipeline_result.value());

    auto vb_result = vk::StaticBuffer::create(
        sizeof(CUBE_VERTICES),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    if (!vb_result)
    {
        std::println("[ERROR] Failed to create cube vertex buffer");
        return;
    }
    m_vertex_buffer = std::move(vb_result.value());

    auto ib_result = vk::StaticBuffer::create(
        sizeof(CUBE_INDICES),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    if (!ib_result)
    {
        std::println("[ERROR] Failed to create cube index buffer");
        return;
    }
    m_index_buffer = std::move(ib_result.value());

    auto vb_staging = m_vertex_buffer.write_to_gpu(cmd, CUBE_VERTICES, sizeof(CUBE_VERTICES));
    if (!vb_staging)
    {
        std::println("[ERROR] Failed to upload vertex buffer");
        return;
    }

    auto ib_staging = m_index_buffer.write_to_gpu(cmd, CUBE_INDICES, sizeof(CUBE_INDICES));
    if (!ib_staging)
    {
        std::println("[ERROR] Failed to upload index buffer");
        return;
    }

    vk::add_vertex_buffer_write_barrier(m_vertex_buffer.buffer());
    vk::add_index_buffer_write_barrier(m_index_buffer.buffer());
    vk::cmd_sync_barriers(cmd);

    vk::delete_later(std::move(vb_staging.value()), frame_id);
    vk::delete_later(std::move(ib_staging.value()), frame_id);

    auto dyn_vb_result = vk::DynamicBuffer::create(
        sizeof(CUBE_VERTICES),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    if (!dyn_vb_result)
    {
        std::println("[ERROR] Failed to create cube2 dynamic vertex buffer");
        return;
    }
    m_vertex_buffer_dynamic = std::move(dyn_vb_result.value());

    m_initialized = true;
    std::println("[INFO] Cube rendering initialized");

    auto font_res = vk::FontMonoAtlas::create(
        cmd,
        std::filesystem::path("resources/fonts/iosevka-fixed-regular.ttf"),
        24,
        512u,
        512u
    );
    if (!font_res)
    {
        std::println("[WARNING] Failed to create font atlas");
        return;
    }

    m_font_atlas = std::move(font_res.value());

    auto font_id = m_overlay.add_font(&m_font_atlas);
    if (!m_overlay.init(ENABLE_UI_MSAA))
    {
        std::println("[WARNING] Failed to init rendering overlay");
        return;
    }

    //if (!m_ui.init(font_id))
    if (!m_ui.init())
        return;

    ui::UIStyleSheet style_sheet;
    style_sheet.label = {
        ui::UILabel::Style{
            .text_color = {255, 255, 255, 255}
        }
    };
    style_sheet.button = {
        ui::UIButton::Style{
            .draw_background = true,
            .draw_border = true,
            .bg_color = {170, 40, 40, 255},
            .border_color = {255, 0, 0, 255},
            .text_color = {255, 255, 255, 255},
            .hover_bg_color = {210, 70, 70, 255},
            .hover_border_color = {255, 0, 0, 255},
            .hover_text_color = {255, 255, 255, 255},
            .pressed_bg_color = {140, 20, 20, 255},
            .pressed_border_color = {255, 255, 255, 255},
            .pressed_text_color = {255, 255, 255, 255}
        }
    };
    style_sheet.text_input = {
        ui::UITextInput::Style{
            .draw_background = true,
            .draw_border = true,
            .bg_color = {110, 20, 20, 255},
            .border_color = {255, 0, 0, 255},
            .text_color = {255, 255, 255, 255},
            .active_bg_color = {140, 30, 30, 255},
            .active_border_color = {255, 255, 255, 255},
            .active_text_color = {255, 255, 255, 255},
            .cursor_color = {255, 255, 255, 255}
        }
    };
    style_sheet.text_area = {
        ui::UITextArea::Style{
            .draw_background = true,
            .draw_border = true,
            .bg_color = {110, 20, 20, 255},
            .border_color = {255, 0, 0, 255},
            .text_color = {255, 255, 255, 255},
            .active_bg_color = {140, 30, 30, 255},
            .active_border_color = {255, 255, 255, 255},
            .active_text_color = {255, 255, 255, 255},
            .cursor_color = {255, 255, 255, 255}
        }
    };
    style_sheet.slider = {
        ui::UISlider::Style{
            .track_color = {255, 255, 255, 255},
            .handle_bg_color = {170, 40, 40, 255},
            .handle_border_color = {255, 0, 0, 255},
            .draw_handle_border = true
        }
    };
    style_sheet.checkbox = {
        ui::UICheckbox::Style{
            .box_color = {150, 30, 30, 255},
            .border_color = {255, 0, 0, 255},
            .check_color = {255, 255, 255, 255},
            .text_color = {255, 255, 255, 255}
        }
    };
    style_sheet.dropdown = {
        ui::UIDropdown::Style{
            .draw_background = true,
            .draw_border = true,
            .bg_color = {150, 30, 30, 255},
            .border_color = {255, 0, 0, 255},
            .hover_color = {210, 70, 70, 255},
            .text_color = {255, 255, 255, 255}
        }
    };
    /*style_sheet.expandable_panel = {
        ui::UIExpandablePanel::Style{
            .draw_background = true,
            .draw_border = true,
            .bg_color = {90, 15, 15, 255},
            .border_color = {255, 0, 0, 255},
            .header_bg_color = {170, 40, 40, 255},
            .text_color = {255, 255, 255, 255}
        }
    };*/
    style_sheet.panel = {
        ui::UIPanel::Style{
            .draw_background = true,
            .draw_border = true,
            .bg_color = {90, 15, 15, 220},
            .border_color = {255, 0, 0, 255}
        }
    };

    auto& root = m_ui.root();
    root.applyStyleSheet(style_sheet, 0);
    root.set_layout(ui::Layout::Vertical);
    root.set_padding(10.0f);
    root.set_spacing(8.0f);
    root.set_draw_background(false);
    root.set_draw_border(false);

    auto* title = root.add_new<ui::UILabel>();
    title->set_text(L"UI controls test");
    title->set_size({220.0f, 20.0f});
    title->set_font(font_id);
    title->applyStyleSheet(style_sheet, 0);

    auto panel = root.add_new<ui::UIPanel>();
    panel->set_size({220.0f, 200.0f});
    panel->applyStyleSheet(style_sheet, 0);
    panel->set_layout(ui::Layout::Vertical);
    panel->set_padding(8.0f);
    panel->set_spacing(8.0f);
    panel->set_draw_background(true);
    panel->set_draw_border(true);

    auto btn1 = panel->add_new<ui::UIButton>();
    btn1->set_label(L"Button 1");
    btn1->set_size({200.0f, 24.0f});
    btn1->set_font(font_id);
    btn1->applyStyleSheet(style_sheet, 0);
    btn1->on_click = [](){ std::println("[UI] Button 1 clicked"); };

    auto input = panel->add_new<ui::UITextInput>();
    input->set_size({200.0f, 24.0f});
    input->set_font(font_id);
    input->set_text(L"Type here");
    input->applyStyleSheet(style_sheet, 0);
    input->on_change = [](const std::wstring& text)
    { std::println("[UI] Input text changed"); };

    auto drop = panel->add_new<ui::UIDropdown>();
    drop->set_size({200.0f, 24.0f});
    drop->set_font(font_id);
    drop->set_items({L"Option 1", L"Option 2", L"Option 3"});
    drop->set_selected(0);
    drop->applyStyleSheet(style_sheet, 0);
    drop->on_change = [](int idx)
    { std::println("[UI] Dropdown selected index: {}", idx); };

    auto btn2 = panel->add_new<ui::UIButton>();
    btn2->set_label(L"Button 2");
    btn2->set_size({200.0f, 24.0f});
    btn2->set_font(font_id);
    btn2->applyStyleSheet(style_sheet, 0);
    btn2->on_click = [](){ std::println("[UI] Button 2 clicked"); };

    auto check = panel->add_new<ui::UICheckbox>();
    check->set_size({200.0f, 24.0f});
    check->set_font(font_id);
    check->set_label(L"Check me");
    check->set_checked(true);
    check->applyStyleSheet(style_sheet, 0);
    check->on_change = [](bool checked)
    { std::println("[UI] Checkbox checked: {}", checked); };


    /*auto* align_left = root.add_new<ui::UILabel>();
    align_left->set_text(L"Left aligned label");
    align_left->set_size({220.0f, 18.0f});
    align_left->set_align(ui::UILabelAlign::Left);

    auto* align_center = root.add_new<ui::UILabel>();
    align_center->set_text(L"Centered label");
    align_center->set_size({220.0f, 18.0f});
    align_center->set_align(ui::UILabelAlign::Center);

    auto* align_right = root.add_new<ui::UILabel>();
    align_right->set_text(L"Right aligned label");
    align_right->set_size({220.0f, 18.0f});
    align_right->set_align(ui::UILabelAlign::Right);

    auto* main_panel = root.add_new<ui::UIPanel>();
    main_panel->set_size({220.0f, 116.0f});
    main_panel->set_layout(ui::Layout::Horizontal);
    main_panel->set_padding(4.0f);
    main_panel->set_spacing(8.0f);
    main_panel->set_draw_background(true);
    main_panel->set_draw_border(true);

    auto* left_panel = main_panel->add_new<ui::UIPanel>();
    left_panel->set_size({100.0f, 100.0f});
    left_panel->set_layout(ui::Layout::Vertical);
    left_panel->set_padding(4.0f);
    left_panel->set_spacing(4.0f);
    left_panel->set_draw_background(true);
    left_panel->set_draw_border(true);
    left_panel->set_scrollable(true);

    auto* left_label = left_panel->add_new<ui::UILabel>();
    left_label->set_text(L"Left");
    left_label->set_size({80.0f, 16.0f});
    left_label->set_align(ui::UILabelAlign::Center);

    auto* btn_hello = left_panel->add_new<ui::UIButton>();
    btn_hello->set_label(L"Hello");
    btn_hello->set_size({92.0f, 20.0f});
    btn_hello->on_click = [](){ std::println("[UI] Hello click"); };

    m_counter_label = root.add_new<ui::UILabel>();
    m_counter_label->set_text(L"Clicks: 0");
    m_counter_label->set_size({220.0f, 18.0f});

    auto* btn_count = left_panel->add_new<ui::UIButton>();
    btn_count->set_label(L"Count");
    btn_count->set_size({92.0f, 20.0f});
    btn_count->on_click = [this]()
    {
        m_click_count++;
        m_counter_label->set_text(L"Clicks: " + std::to_wstring(m_click_count));
    };

    auto* left_slider = left_panel->add_new<ui::UISlider>();
    left_slider->set_size({92.0f, 16.0f});
    left_slider->set_value(0.35f);
    left_slider->on_change = [](float v){ std::println("[UI] Left slider: {:.2f}", v); };

    auto* left_more_btn = left_panel->add_new<ui::UIButton>();
    left_more_btn->set_label(L"More");
    left_more_btn->set_size({88.0f, 20.0f});
    left_more_btn->on_click = [](){ std::println("[UI] Left more"); };

    auto* right_panel = main_panel->add_new<ui::UIPanel>();
    right_panel->set_size({100.0f, 100.0f});
    right_panel->set_layout(ui::Layout::Vertical);
    right_panel->set_padding(4.0f);
    right_panel->set_spacing(4.0f);
    right_panel->set_draw_background(true);
    right_panel->set_draw_border(true);
    right_panel->set_scrollable(true);

    auto* right_label = right_panel->add_new<ui::UILabel>();
    right_label->set_text(L"Right");
    right_label->set_size({80.0f, 16.0f});
    right_label->set_align(ui::UILabelAlign::Center);

    auto* check = right_panel->add_new<ui::UICheckbox>();
    check->set_label(L"On");
    check->set_size({92.0f, 18.0f});
    check->set_checked(true);
    check->on_change = [](bool v){ std::println("[UI] Checkbox: {}", v); };

    auto* input = right_panel->add_new<ui::UITextInput>();
    input->set_size({92.0f, 24.0f});
    input->set_text(L"Input");
    input->on_change = [](const std::wstring& text)
    {
        std::println("[UI] Input size: {}", text.size());
    };

    auto* dropdown = right_panel->add_new<ui::UIDropdown>();
    dropdown->set_size({92.0f, 20.0f});
    dropdown->set_items({L"One", L"Two", L"Three"});
    dropdown->set_selected(0);
    dropdown->on_change = [](int idx){ std::println("[UI] Dropdown: {}", idx); };

    auto* dd_btn1 = right_panel->add_new<ui::UIButton>();
    dd_btn1->set_label(L"DD Btn 1");
    dd_btn1->set_size({92.0f, 20.0f});
    dd_btn1->on_click = [](){ std::println("[UI] DD Btn 1"); };

    auto* dd_btn2 = right_panel->add_new<ui::UIButton>();
    dd_btn2->set_label(L"DD Btn 2");
    dd_btn2->set_size({92.0f, 20.0f});
    dd_btn2->on_click = [](){ std::println("[UI] DD Btn 2"); };

    auto* dd_btn3 = right_panel->add_new<ui::UIButton>();
    dd_btn3->set_label(L"DD Btn 3");
    dd_btn3->set_size({92.0f, 20.0f});
    dd_btn3->on_click = [](){ std::println("[UI] DD Btn 3"); };

    auto* expandable = root.add_new<ui::UIExpandablePanel>();
    expandable->set_size({220.0f, 126.0f});
    expandable->set_header(L"Advanced controls");
    expandable->set_expanded(true);
    expandable->set_header_height(24.0f);
    expandable->set_folded_height(24.0f);
    expandable->set_expanded_height(126.0f);
    expandable->set_padding(6.0f);
    expandable->set_spacing(4.0f);
    expandable->set_draw_background(true);
    expandable->set_draw_border(true);

    auto* ex_label = expandable->add_new<ui::UILabel>();
    ex_label->set_text(L"Inside expandable");
    ex_label->set_size({208.0f, 16.0f});

    auto* ex_button = expandable->add_new<ui::UIButton>();
    ex_button->set_label(L"Action");
    ex_button->set_size({208.0f, 20.0f});
    ex_button->on_click = [](){ std::println("[UI] Expandable action"); };

    auto* ex_slider = expandable->add_new<ui::UISlider>();
    ex_slider->set_size({208.0f, 16.0f});
    ex_slider->set_value(0.5f);
    ex_slider->on_change = [](float v){ std::println("[UI] Expandable slider: {:.2f}", v); };

    auto* ex_dropdown = expandable->add_new<ui::UIDropdown>();
    ex_dropdown->set_size({208.0f, 20.0f});
    ex_dropdown->set_items({L"Alpha", L"Beta", L"Gamma"});
    ex_dropdown->set_selected(0);
    ex_dropdown->on_change = [](int idx){ std::println("[UI] Expandable dropdown: {}", idx); };

    auto* ex_nested_panel = expandable->add_new<ui::UIPanel>();
    ex_nested_panel->set_size({208.0f, 48.0f});
    ex_nested_panel->set_layout(ui::Layout::Horizontal);
    ex_nested_panel->set_padding(4.0f);
    ex_nested_panel->set_spacing(4.0f);
    ex_nested_panel->set_draw_background(true);
    ex_nested_panel->set_draw_border(true);

    auto* ex_nested_btn = ex_nested_panel->add_new<ui::UIButton>();
    ex_nested_btn->set_label(L"Nested");
    ex_nested_btn->set_size({96.0f, 20.0f});
    ex_nested_btn->on_click = [](){ std::println("[UI] Expandable nested button"); };

    auto* ex_nested_check = ex_nested_panel->add_new<ui::UICheckbox>();
    ex_nested_check->set_size({96.0f, 20.0f});
    ex_nested_check->set_label(L"Flag");
    ex_nested_check->set_checked(false);
    ex_nested_check->on_change = [](bool v){ std::println("[UI] Expandable nested check: {}", v); };

    auto* text_area = root.add_new<ui::UITextArea>();
    text_area->set_size({220.0f, 90.0f});
    text_area->set_text(L"TextArea\nType here");
    text_area->on_change = [](const std::wstring& text)
    {
        std::println("[UI] TextArea size: {}", text.size());
    };*/

    std::println("[INFO] UI system initialized");
}

auto engi::TestEngi::render(VkCommandBuffer cmd) -> void
{
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration<float>(now - m_start_time).count();

    auto fb_extent = vk::extent();
    if (fb_extent.width == 0 || fb_extent.height == 0)
        return;

    auto camera = go::Camera3D(
        {0, 0, 0},
        {1, 0, 0},
        {0, 1, 0},
        {static_cast<float>(fb_extent.width), static_cast<float>(fb_extent.height)},
        true
    );
    auto proj = camera.get_proj();
    auto view = camera.get_view();

    auto angle = elapsed * 0.5f;
    auto rotation_x = go::rot4({1, 0, 0}, angle * 0.7f);
    auto rotation_y = go::rot4({0, 1, 0}, angle);
    auto rotation_z = go::rot4({0, 0, 1}, angle * 0.3f);
    auto rotation = rotation_y * rotation_x * rotation_z;
    auto model_left = go::translate(go::vf3{5, 0, -2}) * rotation;
    auto model_right = go::translate(go::vf3{5, 0, 2}) * rotation;
    auto mvp_left = proj * view * model_left;
    auto mvp_right = proj * view * model_right;

    auto dyn_vertices = std::array<Vertex, std::size(CUBE_VERTICES)>{};
    constexpr auto hue_speed = 0.35f;
    for (size_t i = 0; i < std::size(CUBE_VERTICES); ++i)
    {
        auto face = static_cast<uint32_t>(i / 4);
        auto hue = std::fmod(elapsed * hue_speed + face / 6.0f, 1.0f);
        if (hue < 0.0f)
            hue += 1.0f;
        auto base = hue_to_rgb(hue);
        auto pulse = 0.6f + 0.4f * (0.5f + 0.5f * std::sin(elapsed * 2.0f + face));
        dyn_vertices[i] = {
            CUBE_VERTICES[i].pos,
            base * pulse
        };
    }

    auto wr = m_vertex_buffer_dynamic.write_to_gpu(cmd, dyn_vertices.data(), sizeof(dyn_vertices));
    if (!wr)
    {
        std::println("[ERROR] Failed to upload cube2 vertex data");
        return;
    }
    vk::add_vertex_buffer_write_barrier(m_vertex_buffer_dynamic.buffer());
    vk::cmd_sync_barriers(cmd);

    auto full_rect = VkRect2D{ .offset = {0, 0}, .extent = fb_extent };
    auto clear_color = go::vf4{34.0f / 255.f, 34.0f / 255.f, 59.0f / 255.f, 1.0f};
    m_ui.sync(cmd, full_rect);

    vk::draw_start(cmd, full_rect, clear_color);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.get());
    vkCmdBindIndexBuffer(cmd, m_index_buffer.buffer(), 0, VK_INDEX_TYPE_UINT16);

    auto offset = VkDeviceSize{0};
    auto pc_left = PushConstant{mvp_left};
    vkCmdPushConstants(cmd, m_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstant), &pc_left);
    auto vb_static = m_vertex_buffer.buffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb_static, &offset);
    vkCmdDrawIndexed(cmd, m_index_count, 1, 0, 0, 0);

    auto pc_right = PushConstant{mvp_right};
    vkCmdPushConstants(cmd, m_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstant), &pc_right);
    auto vb_dynamic = m_vertex_buffer_dynamic.buffer();
    vkCmdBindVertexBuffers(cmd, 0, 1, &vb_dynamic, &offset);
    vkCmdDrawIndexed(cmd, m_index_count, 1, 0, 0, 0);

    vk::draw_end(cmd);

    vk::draw_start(cmd, full_rect, clear_color, false, ENABLE_UI_MSAA,
        VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
        VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE);
    m_ui.draw(cmd, m_overlay, full_rect);
    vk::draw_end(cmd);
}

auto engi::TestEngi::cleanup() -> void
{
    if (!m_initialized)
        return;

    m_counter_label = nullptr;
    m_ui = {};
    m_overlay = {};
    m_font_atlas = {};

    m_vertex_buffer = {};
    m_index_buffer = {};
    m_vertex_buffer_dynamic = {};
    m_pipeline = {};
    m_layout = {};
    m_initialized = false;
}
