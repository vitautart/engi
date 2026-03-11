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
        {
            .text_color = {255, 255, 255, 255},
            .font = font_id
        }
    };
    style_sheet.button = {
        {
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
            .pressed_text_color = {255, 255, 255, 255},
            .font = font_id
        }
    };
    style_sheet.text_input = {
        {
            .draw_background = true,
            .draw_border = true,
            .bg_color = {110, 20, 20, 255},
            .border_color = {255, 0, 0, 255},
            .text_color = {255, 255, 255, 255},
            .active_bg_color = {140, 30, 30, 255},
            .active_border_color = {255, 255, 255, 255},
            .active_text_color = {255, 255, 255, 255},
            .cursor_color = {255, 255, 255, 255},
            .font = font_id
        }
    };
    style_sheet.text_area = {
        {
            .draw_background = true,
            .draw_border = true,
            .bg_color = {110, 20, 20, 255},
            .border_color = {255, 0, 0, 255},
            .text_color = {255, 255, 255, 255},
            .active_bg_color = {140, 30, 30, 255},
            .active_border_color = {255, 255, 255, 255},
            .active_text_color = {255, 255, 255, 255},
            .cursor_color = {255, 255, 255, 255},
            .font = font_id
        }
    };
    style_sheet.slider = {
        {
            .track_color = {255, 255, 255, 255},
            .handle_bg_color = {170, 40, 40, 255},
            .handle_border_color = {255, 0, 0, 255},
            .draw_handle_border = true
        }
    };
    style_sheet.checkbox = {
        {
            .box_color = {150, 30, 30, 255},
            .border_color = {255, 0, 0, 255},
            .check_color = {255, 255, 255, 255},
            .text_color = {255, 255, 255, 255},
            .font = font_id
        }
    };
    style_sheet.dropdown = {
        {
            .draw_background = true,
            .draw_border = true,
            .bg_color = {150, 30, 30, 255},
            .border_color = {255, 0, 0, 255},
            .hover_color = {210, 70, 70, 255},
            .text_color = {255, 255, 255, 255},
            .font = font_id
        }
    };
    style_sheet.expandable_panel = {
        {
            .header_bg_color = {170, 40, 40, 255},
            .text_color = {255, 255, 255, 255},
            .font = font_id
        }
    };
    style_sheet.panel = {
        {
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
    title->applyStyleSheet(style_sheet, 0);

    auto panel = root.add_new<ui::UIScrollablePanel>();
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
    btn1->applyStyleSheet(style_sheet, 0);
    btn1->on_click = [](){ std::println("[UI] Button 1 clicked"); };

    auto btn3 = panel->add_new<ui::UIButton>();
    btn3->set_label(L"Button 3");
    btn3->set_size({200.0f, 24.0f});
    btn3->applyStyleSheet(style_sheet, 0);
    btn3->on_click = [](){ std::println("[UI] Button 3 clicked"); };

    auto btn4 = panel->add_new<ui::UIButton>();
    btn4->set_label(L"Button 4");
    btn4->set_size({200.0f, 24.0f});
    btn4->applyStyleSheet(style_sheet, 0);
    btn4->on_click = [](){ std::println("[UI] Button 4 clicked"); };

    auto input = panel->add_new<ui::UITextInput>();
    input->set_size({200.0f, 24.0f});
    input->set_text(L"Type here");
    input->applyStyleSheet(style_sheet, 0);
    input->on_change = [](const std::wstring& text)
    { std::println("[UI] Input text changed"); };

    auto drop = panel->add_new<ui::UIDropdown>();
    drop->set_size({200.0f, 24.0f});
    drop->set_items({L"Option 1", L"Option 2", L"Option 3"});
    drop->set_selected(0);
    drop->applyStyleSheet(style_sheet, 0);
    drop->on_change = [](int idx)
    { std::println("[UI] Dropdown selected index: {}", idx); };

    auto btn2 = panel->add_new<ui::UIButton>();
    btn2->set_label(L"Button 2");
    btn2->set_size({200.0f, 24.0f});
    btn2->applyStyleSheet(style_sheet, 0);
    btn2->on_click = [](){ std::println("[UI] Button 2 clicked"); };

    auto check = panel->add_new<ui::UICheckbox>();
    check->set_size({200.0f, 24.0f});
    check->set_label(L"Check me");
    check->set_checked(true);
    check->applyStyleSheet(style_sheet, 0);
    check->on_change = [](bool checked)
    { std::println("[UI] Checkbox checked: {}", checked); };

    auto hpanel = root.add_new<ui::UIScrollablePanel>();
    hpanel->set_size({220.0f, 42.0f});
    hpanel->applyStyleSheet(style_sheet, 0);
    hpanel->set_layout(ui::Layout::Horizontal);
    hpanel->set_padding(6.0f);
    hpanel->set_spacing(6.0f);
    hpanel->set_draw_background(true);
    hpanel->set_draw_border(true);

    auto hbtn1 = hpanel->add_new<ui::UIButton>();
    hbtn1->set_label(L"H Button 1");
    hbtn1->set_size({110.0f, 24.0f});
    hbtn1->applyStyleSheet(style_sheet, 0);
    hbtn1->on_click = [](){ std::println("[UI] H Button 1 clicked"); };

    auto hbtn2 = hpanel->add_new<ui::UIButton>();
    hbtn2->set_label(L"H Button 2");
    hbtn2->set_size({110.0f, 24.0f});
    hbtn2->applyStyleSheet(style_sheet, 0);
    hbtn2->on_click = [](){ std::println("[UI] H Button 2 clicked"); };

    auto hbtn3 = hpanel->add_new<ui::UIButton>();
    hbtn3->set_label(L"H Button 3");
    hbtn3->set_size({110.0f, 24.0f});
    hbtn3->applyStyleSheet(style_sheet, 0);
    hbtn3->on_click = [](){ std::println("[UI] H Button 3 clicked"); };

    auto hbtn4 = hpanel->add_new<ui::UIButton>();
    hbtn4->set_label(L"H Button 4");
    hbtn4->set_size({110.0f, 24.0f});
    hbtn4->applyStyleSheet(style_sheet, 0);
    hbtn4->on_click = [](){ std::println("[UI] H Button 4 clicked"); };

    auto hbtn5 = hpanel->add_new<ui::UIButton>();
    hbtn5->set_label(L"H Button 5");
    hbtn5->set_size({110.0f, 24.0f});
    hbtn5->applyStyleSheet(style_sheet, 0);
    hbtn5->on_click = [](){ std::println("[UI] H Button 5 clicked"); };

    auto auto_hpanel = root.add_new<ui::UIAutoPanel>();
    auto_hpanel->applyStyleSheet(style_sheet, 0);
    auto_hpanel->set_layout(ui::Layout::Horizontal);
    auto_hpanel->set_padding(8.0f);
    auto_hpanel->set_spacing(8.0f);
    auto_hpanel->set_draw_background(true);
    auto_hpanel->set_draw_border(true);

    auto auto_hbtn1 = auto_hpanel->add_new<ui::UIButton>();
    auto_hbtn1->set_label(L"Auto H1");
    auto_hbtn1->set_size({90.0f, 24.0f});
    auto_hbtn1->applyStyleSheet(style_sheet, 0);
    auto_hbtn1->on_click = [](){ std::println("[UI] Auto H1 clicked"); };

    auto auto_hbtn2 = auto_hpanel->add_new<ui::UIButton>();
    auto_hbtn2->set_label(L"Auto H2");
    auto_hbtn2->set_size({90.0f, 24.0f});
    auto_hbtn2->applyStyleSheet(style_sheet, 0);
    auto_hbtn2->on_click = [](){ std::println("[UI] Auto H2 clicked"); };

    auto expandable = root.add_new<ui::UIExpandablePanel>();
    expandable->set_size({220.0f, 220.0f});
    expandable->set_header(L"Expandable");
    expandable->set_expanded(true);
    expandable->set_header_height(24.0f);
    expandable->set_padding(8.0f);
    expandable->set_spacing(8.0f);
    expandable->set_draw_background(true);
    expandable->set_draw_border(true);
    expandable->set_bg_color({90, 15, 15, 220});
    expandable->set_border_color({255, 0, 0, 255});
    expandable->applyStyleSheet(style_sheet, 0);

    auto exp_btn1 = expandable->add_new<ui::UIButton>();
    exp_btn1->set_label(L"Expand Btn 1");
    exp_btn1->set_size({200.0f, 24.0f});
    exp_btn1->applyStyleSheet(style_sheet, 0);
    exp_btn1->on_click = [](){ std::println("[UI] Expand Btn 1 clicked"); };

    auto exp_btn2 = expandable->add_new<ui::UIButton>();
    exp_btn2->set_label(L"Expand Btn 2");
    exp_btn2->set_size({200.0f, 24.0f});
    exp_btn2->applyStyleSheet(style_sheet, 0);
    exp_btn2->on_click = [](){ std::println("[UI] Expand Btn 2 clicked"); };

    auto exp_input = expandable->add_new<ui::UITextInput>();
    exp_input->set_size({200.0f, 24.0f});
    exp_input->set_text(L"Expandable input");
    exp_input->applyStyleSheet(style_sheet, 0);
    exp_input->on_change = [](const std::wstring&)
    {
        std::println("[UI] Expandable input changed");
    };

    auto exp_drop = expandable->add_new<ui::UIDropdown>();
    exp_drop->set_size({200.0f, 24.0f});
    exp_drop->set_items({L"Ex Option 1", L"Ex Option 2", L"Ex Option 3"});
    exp_drop->set_selected(0);
    exp_drop->applyStyleSheet(style_sheet, 0);
    exp_drop->on_change = [](int idx)
    {
        std::println("[UI] Expandable dropdown selected index: {}", idx);
    };

    auto exp_auto_panel = expandable->add_new<ui::UIAutoPanel>();
    exp_auto_panel->applyStyleSheet(style_sheet, 0);
    exp_auto_panel->set_layout(ui::Layout::Vertical);
    exp_auto_panel->set_padding(6.0f);
    exp_auto_panel->set_spacing(6.0f);
    exp_auto_panel->set_draw_background(true);
    exp_auto_panel->set_draw_border(true);

    auto exp_auto_input = exp_auto_panel->add_new<ui::UITextInput>();
    exp_auto_input->set_size({200.0f, 24.0f});
    exp_auto_input->set_text(L"Auto panel input");
    exp_auto_input->applyStyleSheet(style_sheet, 0);
    exp_auto_input->on_change = [](const std::wstring&)
    {
        std::println("[UI] Expandable auto input changed");
    };

    auto exp_auto_btn = exp_auto_panel->add_new<ui::UIButton>();
    exp_auto_btn->set_label(L"Auto Panel Button");
    exp_auto_btn->set_size({200.0f, 24.0f});
    exp_auto_btn->applyStyleSheet(style_sheet, 0);
    exp_auto_btn->on_click = []()
    {
        std::println("[UI] Expandable auto button clicked");
    };

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
