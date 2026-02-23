#include "GLFW/glfw3.h"
#include <app.hpp>
#include <array>
#include <memory>
#include <print>
#include <rendering.hpp>
#include "rendering_overlay.hpp"
#include "ui_system.hpp"
#include <pipeline.hpp>
#include <layout.hpp>
#include <static_buffer.hpp>
#include <dynamic_buffer.hpp>
#include <gomath.hpp>
#include <gocamera.hpp>
#include <chrono>
#include <cmath>
#include <vulkan/vulkan_core.h>

#define TO_APP_WINDOW(window) reinterpret_cast<App*>(glfwGetWindowUserPointer(window))

namespace
{
    auto hue_to_rgb(float h) noexcept -> go::vf3
    {
        float r = std::abs(h * 6.0f - 3.0f) - 1.0f;
        float g = 2.0f - std::abs(h * 6.0f - 2.0f);
        float b = 2.0f - std::abs(h * 6.0f - 4.0f);
        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);
        return {r, g, b};
    }
}

// ===== CUBE TEST RENDERING (TEMPORARY) =====
namespace TestCube
{
    struct Vertex
    {
        go::vf3 pos;
        go::vf3 color;
    };

    // Cube vertices with per-face colors
    static constexpr Vertex CUBE_VERTICES[] = {
        // Front face (Red)
        {{-1, -1,  1}, {1, 0, 0}},
        {{ 1, -1,  1}, {1, 0, 0}},
        {{ 1,  1,  1}, {1, 0, 0}},
        {{-1,  1,  1}, {1, 0, 0}},
        // Back face (Green)
        {{-1, -1, -1}, {0, 1, 0}},
        {{ 1, -1, -1}, {0, 1, 0}},
        {{ 1,  1, -1}, {0, 1, 0}},
        {{-1,  1, -1}, {0, 1, 0}},
        // Top face (Blue)
        {{-1,  1,  1}, {0, 0, 1}},
        {{ 1,  1,  1}, {0, 0, 1}},
        {{ 1,  1, -1}, {0, 0, 1}},
        {{-1,  1, -1}, {0, 0, 1}},
        // Bottom face (Yellow)
        {{-1, -1,  1}, {1, 1, 0}},
        {{ 1, -1,  1}, {1, 1, 0}},
        {{ 1, -1, -1}, {1, 1, 0}},
        {{-1, -1, -1}, {1, 1, 0}},
        // Right face (Magenta)
        {{ 1, -1,  1}, {1, 0, 1}},
        {{ 1, -1, -1}, {1, 0, 1}},
        {{ 1,  1, -1}, {1, 0, 1}},
        {{ 1,  1,  1}, {1, 0, 1}},
        // Left face (Cyan)
        {{-1, -1,  1}, {0, 1, 1}},
        {{-1, -1, -1}, {0, 1, 1}},
        {{-1,  1, -1}, {0, 1, 1}},
        {{-1,  1,  1}, {0, 1, 1}},
    };

    static constexpr uint16_t CUBE_INDICES[] = {
        0, 1, 2,  0, 2, 3,      // Front
        4, 6, 5,  4, 7, 6,      // Back
        8, 9, 10, 8, 10, 11,    // Top
        12, 14, 13, 12, 15, 14, // Bottom
        16, 17, 18, 16, 18, 19, // Right
        20, 22, 21, 20, 23, 22  // Left
    };

    struct PushConstant
    {
        go::mf4 mvp;
    };

    // Global cube render state
    bool g_initialized = false;
    engi::vk::Pipeline g_pipeline;
    engi::vk::Layout g_layout;
    engi::vk::StaticBuffer g_vertex_buffer;
    engi::vk::StaticBuffer g_index_buffer;
    engi::vk::DynamicBuffer g_vertex_buffer_dynamic;
    uint32_t g_index_count = sizeof(CUBE_INDICES) / sizeof(CUBE_INDICES[0]);
    auto g_start_time = std::chrono::high_resolution_clock::now();

    // Text rendering test
    engi::vk::FontMonoAtlas g_font_atlas;
    engi::vk::RenderingOverlay g_overlay;
    engi::vk::TextBuffer g_text_buffer;
    engi::vk::GeometryBuffer2D g_geo_buffer;
    engi::vk::GeometryBuffer2DWire g_geo_wire_buffer;

    // UI system test
    engi::ui::UISystem g_ui;
    engi::ui::UILabel* g_counter_label = nullptr;
    int g_click_count = 0;

    auto init(VkCommandBuffer cmd, uint32_t frame_id) -> void
    {
        if (g_initialized)
            return;

        // Create layout with push constants
        auto layout_result = engi::vk::LayoutBuilder()
            .push_const(0, sizeof(PushConstant), VK_SHADER_STAGE_VERTEX_BIT)
            .build();
        
        if (!layout_result)
        {
            std::println("[ERROR] Failed to create cube pipeline layout");
            return;
        }
        g_layout = std::move(layout_result.value());

        // Create pipeline
        VkVertexInputBindingDescription binding{
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        };

        std::array<VkVertexInputAttributeDescription, 2> attributes{
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

        auto pipeline_result = engi::vk::PipelineBuilder()
            .vertex_shader_from_file("shaders/cube_vert.spv")
            .fragment_shader_from_file("shaders/cube_frag.spv")
            .color_format(engi::vk::color_format())
            .depth_format(engi::vk::depth_format())
            .samples(engi::vk::sample_count())
            .add(binding)
            .add(attributes[0])
            .add(attributes[1])
            .topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
            .polygon_mode(VK_POLYGON_MODE_FILL)
            .cull_mode(VK_CULL_MODE_BACK_BIT)
            .front_face(VK_FRONT_FACE_COUNTER_CLOCKWISE)
            .depth_test(true)
            .depth_write(true)
            //.depth_compare_op(VK_COMPARE_OP_LESS) // default-z
            .depth_compare_op(VK_COMPARE_OP_GREATER) // reverse-z
            .no_blending()
            .build(g_layout.get());
        
        if (!pipeline_result)
        {
            std::println("[ERROR] Failed to create cube pipeline");
            return;
        }
        g_pipeline = std::move(pipeline_result.value());

        // Create vertex buffer
        auto vb_result = engi::vk::StaticBuffer::create(
            sizeof(CUBE_VERTICES),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        );
        if (!vb_result)
        {
            std::println("[ERROR] Failed to create cube vertex buffer");
            return;
        }
        g_vertex_buffer = std::move(vb_result.value());

        // Create index buffer
        auto ib_result = engi::vk::StaticBuffer::create(
            sizeof(CUBE_INDICES),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        );
        if (!ib_result)
        {
            std::println("[ERROR] Failed to create cube index buffer");
            return;
        }
        g_index_buffer = std::move(ib_result.value());

        // Upload vertex and index data
        auto vb_staging = g_vertex_buffer.write_to_gpu(cmd, CUBE_VERTICES, sizeof(CUBE_VERTICES));
        if (!vb_staging)
        {
            std::println("[ERROR] Failed to upload vertex buffer");
            return;
        }

        auto ib_staging = g_index_buffer.write_to_gpu(cmd, CUBE_INDICES, sizeof(CUBE_INDICES));
        if (!ib_staging)
        {
            std::println("[ERROR] Failed to upload index buffer");
            return;
        }

        engi::vk::add_vertex_buffer_write_barrier(g_vertex_buffer.buffer());
        engi::vk::add_index_buffer_write_barrier(g_index_buffer.buffer());
        engi::vk::cmd_sync_barriers(cmd);

        // Keep staging buffers alive until frame is done
        engi::vk::delete_later(std::move(vb_staging.value()), frame_id);
        engi::vk::delete_later(std::move(ib_staging.value()), frame_id);

        auto dyn_vb_result = engi::vk::DynamicBuffer::create(
            sizeof(CUBE_VERTICES),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        );
        if (!dyn_vb_result)
        {
            std::println("[ERROR] Failed to create cube2 dynamic vertex buffer");
            return;
        }
        g_vertex_buffer_dynamic = std::move(dyn_vb_result.value());

        g_initialized = true;
        std::println("[INFO] Cube rendering initialized");

        // Initialize font atlas, overlay and a static text buffer
        auto font_res = engi::vk::FontMonoAtlas::create(
            cmd,
            std::filesystem::path("resources/fonts/iosevka-fixed-regular.ttf"),
            //16,
            32,
            512u,
            512u
        );
        if (!font_res)
        {
            std::println("[WARNING] Failed to create font atlas");
        }
        else
        {
            g_font_atlas = std::move(font_res.value());

            auto font_id = g_overlay.add_font(&g_font_atlas);
            if (!g_overlay.init(false))
                std::println("[WARNING] Failed to init rendering overlay");
            else
            {
                auto tb_res = engi::vk::TextBuffer::create(font_id);
                if (tb_res)
                {
                    g_text_buffer = std::move(tb_res.value());
                    g_text_buffer.clear();
                    g_text_buffer.add(L"Hello engi", go::vf2{10.0f, 20.0f}, go::vu4{255,255,255,255});
                    if (g_text_buffer.upload(cmd))
                        engi::vk::add_vertex_buffer_write_barrier(g_text_buffer.vertex_buffer());
                }

                auto gb_res = engi::vk::GeometryBuffer2D::create();
                if (gb_res)
                {
                    g_geo_buffer = std::move(gb_res.value());
                    g_geo_buffer.clear();
                    g_geo_buffer.add_rect(go::vf2{10.0f, 40.0f}, go::vf2{100.0f, 50.0f}, go::vu4{200, 50, 50, 255});
                    g_geo_buffer.add_rect(go::vf2{120.0f, 40.0f}, go::vf2{100.0f, 50.0f}, go::vu4{50, 200, 50, 255});
                    if (g_geo_buffer.upload(cmd))
                    {
                        engi::vk::add_vertex_buffer_write_barrier(g_geo_buffer.vertex_buffer());
                        engi::vk::add_index_buffer_write_barrier(g_geo_buffer.index_buffer());
                    }
                }

                auto gbw_res = engi::vk::GeometryBuffer2DWire::create();
                if (gbw_res)
                {
                    g_geo_wire_buffer = std::move(gbw_res.value());
                    g_geo_wire_buffer.clear();
                    g_geo_wire_buffer.add_rect(go::vf2{10.0f, 100.0f}, go::vf2{100.0f, 50.0f}, go::vu4{255, 255, 0, 255});
                    g_geo_wire_buffer.add_rect(go::vf2{120.0f, 100.0f}, go::vf2{100.0f, 50.0f}, go::vu4{0, 255, 255, 255});
                    if (g_geo_wire_buffer.upload(cmd))
                    {
                        engi::vk::add_vertex_buffer_write_barrier(g_geo_wire_buffer.vertex_buffer());
                        engi::vk::add_index_buffer_write_barrier(g_geo_wire_buffer.index_buffer());
                    }
                }

                engi::vk::cmd_sync_barriers(cmd);

                // Initialize UI system example
                if (g_ui.init(font_id))
                {
                    auto& root = g_ui.root();
                    root.layout = engi::ui::Layout::Vertical;
                    root.padding = 10.0f;
                    root.spacing = 8.0f;
                    root.bg_color = {20, 20, 35, 200};

                    auto* title = root.add_new<engi::ui::UILabel>();
                    title->text = L"UI System Demo";
                    title->color = {255, 220, 100, 255};
                    title->size = {200.0f, 24.0f};

                    auto* btn_hello = root.add_new<engi::ui::UIButton>();
                    btn_hello->label = L"Say Hello";
                    btn_hello->size = {150.0f, 32.0f};
                    btn_hello->on_click = [](){ std::println("[UI] Hello from button!"); };

                    g_counter_label = root.add_new<engi::ui::UILabel>();
                    g_counter_label->text = L"Clicks: 0";
                    g_counter_label->size = {200.0f, 24.0f};

                    auto* btn_count = root.add_new<engi::ui::UIButton>();
                    btn_count->label = L"Count Click";
                    btn_count->size = {150.0f, 32.0f};
                    btn_count->color_normal = {50, 80, 50, 255};
                    btn_count->color_hover = {70, 110, 70, 255};
                    btn_count->color_pressed = {30, 60, 30, 255};
                    btn_count->on_click = []()
                    {
                        g_click_count++;
                        g_counter_label->text = L"Clicks: " + std::to_wstring(g_click_count);
                    };

                    auto* slider = root.add_new<engi::ui::UISlider>();
                    slider->size = {200.0f, 24.0f};
                    slider->value = 0.5f;
                    slider->on_change = [](float v){ std::println("[UI] Slider: {:.2f}", v); };

                    std::println("[INFO] UI system initialized");
                }
            }
        }
    }

    auto render(VkCommandBuffer cmd) -> void
    {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - g_start_time).count();

        go::Camera3D camera(
            {0, 0, 0},
            {1, 0, 0},
            {0, 1, 0},
            {800.0f, 600.0f},
            true
        );
        go::mf4 proj = camera.get_proj();
        go::mf4 view = camera.get_view();

        float angle = elapsed * 0.5f;
        go::mf4 rotation_x = go::rot4({1, 0, 0}, angle * 0.7f);
        go::mf4 rotation_y = go::rot4({0, 1, 0}, angle);
        go::mf4 rotation_z = go::rot4({0, 0, 1}, angle * 0.3f);
        go::mf4 rotation = rotation_y * rotation_x * rotation_z;
        go::mf4 model_left = go::translate(go::vf3{5, 0, -2}) * rotation;
        go::mf4 model_right = go::translate(go::vf3{5, 0, 2}) * rotation;
        go::mf4 mvp_left = proj * view * model_left;
        go::mf4 mvp_right = proj * view * model_right;

        std::array<Vertex, std::size(CUBE_VERTICES)> dyn_vertices;
        constexpr float hue_speed = 0.35f;
        for (size_t i = 0; i < std::size(CUBE_VERTICES); ++i)
        {
            uint32_t face = static_cast<uint32_t>(i / 4);
            float hue = std::fmod(elapsed * hue_speed + face / 6.0f, 1.0f);
            if (hue < 0.0f)
                hue += 1.0f;
            go::vf3 base = hue_to_rgb(hue);
            float pulse = 0.6f + 0.4f * (0.5f + 0.5f * std::sin(elapsed * 2.0f + face));
            dyn_vertices[i] = {
                CUBE_VERTICES[i].pos,
                base * pulse
            };
        }
        auto wr = g_vertex_buffer_dynamic.write_to_gpu(cmd, dyn_vertices.data(), sizeof(dyn_vertices));
        if (!wr)
        {
            std::println("[ERROR] Failed to upload cube2 vertex data");
            return;
        }
        engi::vk::add_vertex_buffer_write_barrier(g_vertex_buffer_dynamic.buffer());
        engi::vk::cmd_sync_barriers(cmd);

        // Sync UI buffers (upload + barriers) before render pass
        VkRect2D full_rect = { .offset = {0, 0}, .extent = {800, 600} };
        go::vf4 clear_color = {34.0f/255.f, 34.0f/255.f, 59.0f/255.f, 1.0f};
        g_ui.sync(cmd, full_rect);

        engi::vk::draw_start(cmd, full_rect, clear_color);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeline.get());
        vkCmdBindIndexBuffer(cmd, g_index_buffer.buffer(), 0, VK_INDEX_TYPE_UINT16);

        VkDeviceSize offset = 0;
        PushConstant pc_left{mvp_left};
        vkCmdPushConstants(cmd, g_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstant), &pc_left);
        VkBuffer vb_static = g_vertex_buffer.buffer();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb_static, &offset);
        vkCmdDrawIndexed(cmd, g_index_count, 1, 0, 0, 0);

        PushConstant pc_right{mvp_right};
        vkCmdPushConstants(cmd, g_layout.get(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstant), &pc_right);
        VkBuffer vb_dynamic = g_vertex_buffer_dynamic.buffer();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb_dynamic, &offset);
        vkCmdDrawIndexed(cmd, g_index_count, 1, 0, 0, 0);

        //g_overlay.start_draw_2d(cmd);
        //g_overlay.draw(cmd, g_geo_buffer, full_rect);

        //g_overlay.start_draw_2d_wire(cmd);
        //g_overlay.draw(cmd, g_geo_wire_buffer, full_rect);

        //g_overlay.start_text_draw(cmd);
        //g_overlay.draw(cmd, g_text_buffer, full_rect);

        engi::vk::draw_end(cmd);

        engi::vk::draw_start(cmd, full_rect, clear_color, false, false, 
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE);
        g_ui.draw(cmd, g_overlay, full_rect);
        engi::vk::draw_end(cmd);
    }

    auto cleanup() -> void
    {
        if (!g_initialized)
            return;
        g_counter_label = nullptr;
        g_ui = {};
        g_geo_buffer = {};
        g_geo_wire_buffer = {};
        g_text_buffer = {};
        g_overlay = {};
        g_font_atlas = {};

        g_vertex_buffer = {};
        g_index_buffer = {};
        g_vertex_buffer_dynamic = {};
        g_pipeline = {};
        g_layout = {};
        g_initialized = false;
    }
}
// ===== END CUBE TEST RENDERING =====

engi::App::Deinitalizer::~Deinitalizer() noexcept
{
    vk::destroy();
    if (m_glfw_window)
    {
        glfwDestroyWindow(m_glfw_window);
        m_glfw_window = nullptr;
    }
    glfwTerminate();
}

auto engi::App::fnTextInputExterior(GLFWwindow* window, unsigned int codepoint) -> void
{
    TO_APP_WINDOW(window)->fn_text_input_interior(codepoint);
}
auto engi::App::fnKeyPressExterior(GLFWwindow* window, int key, int scancode, int action, int mods) -> void
{
    TO_APP_WINDOW(window)->fn_key_press_interior(key, scancode, action, mods);
}
auto engi::App::fnMousePressExterior(GLFWwindow* window, int button, int action, int mods) -> void
{
    TO_APP_WINDOW(window)->fn_mouse_press_interior(button, action, mods);
}
auto engi::App::fnMouseMoveExterior(GLFWwindow* window, double x, double y) -> void
{
    TO_APP_WINDOW(window)->fn_mouse_move_interior(x, y);
}
auto engi::App::fnScrollExterior(GLFWwindow* window, double xoffset, double yoffset) -> void
{
    TO_APP_WINDOW(window)->fn_scroll_interior(xoffset, yoffset);
}
auto engi::App::fnResizeExterior(GLFWwindow* window, int width,int height) -> void
{
    TO_APP_WINDOW(window)->fn_resize_interior(width, height);
}

auto engi::App::fn_text_input_interior(unsigned int codepoint) -> void
{
    TestCube::g_ui.process_text_input(codepoint);
}

auto engi::App::fn_key_press_interior(int key, int scancode, int action, int mods) -> void
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(m_glfw_window, true);
    }
    TestCube::g_ui.process_key(key, action, mods);
}

auto engi::App::fn_mouse_press_interior(int button, int action, int mods) -> void
{
    double x, y;
    glfwGetCursorPos(m_glfw_window, &x, &y);
    TestCube::g_ui.process_mouse_press(button, action);
}

auto engi::App::fn_mouse_move_interior(double x, double y) -> void
{
    TestCube::g_ui.process_mouse_move(static_cast<float>(x), static_cast<float>(y));
}

auto engi::App::fn_scroll_interior(double xoffset, double yoffset) -> void
{
    double x, y;
    glfwGetCursorPos(m_glfw_window, &x, &y);
    TestCube::g_ui.process_scroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
}

auto engi::App::fn_resize_interior(int width,int height) -> void
{
}

auto engi::App::create() noexcept -> std::unique_ptr<App>
{
    auto app = std::unique_ptr<App>(new App);
    if (!app) 
    {
        std::println("[ERROR] Can't create app\n");
        return nullptr;
    }

    if (!glfwInit())
    {
        std::println("[ERROR] Can't initialize glfw");
        return  nullptr;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, false);
    app->m_glfw_window = glfwCreateWindow(800, 600, "HomeCAD", NULL, NULL);
    app->m_deinititalizer.m_glfw_window = app->m_glfw_window;
    if (!app->m_glfw_window)
    {
        std::println("[ERROR] Can't create window");
        return nullptr;
    }
    glfwSetCharCallback(app->m_glfw_window, fnTextInputExterior);
    glfwSetKeyCallback(app->m_glfw_window, fnKeyPressExterior);
    glfwSetMouseButtonCallback(app->m_glfw_window, fnMousePressExterior);
    glfwSetCursorPosCallback(app->m_glfw_window, fnMouseMoveExterior);
    glfwSetScrollCallback(app->m_glfw_window, fnScrollExterior);
    glfwSetWindowSizeCallback(app->m_glfw_window, fnResizeExterior);
    glfwSetWindowUserPointer(app->m_glfw_window, app.get());

    if (!engi::vk::init(app->m_glfw_window))
    {
        std::println("[ERROR] Can't init rendering systen");
        return nullptr;
    }

    return app;
}

auto engi::App::run() noexcept -> void
{
    std::println("[INFO] Starting app...");
    while(!glfwWindowShouldClose(m_glfw_window))
    {
        // Acquire next swapchain image
        auto frame = engi::vk::wait_frame();
        auto acquire_result = engi::vk::acquire();
        if (acquire_result.result != VK_SUCCESS)
        {
            std::println("[ERROR] Failed to acquire swapchain image");
            break;
        }

        // Start recording commands
        auto cmd = engi::vk::cmd_start();

        // Initialize cube rendering (first frame only)
        TestCube::init(cmd, frame);

        TestCube::render(cmd);

        engi::vk::cmd_end();

        // Submit and present
        if (!engi::vk::submit())
        {
            std::println("[ERROR] Failed to submit commands");
            break;
        }

        glfwWaitEvents();
    }
    std::println("[INFO] Waiting for GPU to finish...");
    engi::vk::wait();
    std::println("[INFO] Closing app...");
    TestCube::cleanup();
}