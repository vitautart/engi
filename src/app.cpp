#include "GLFW/glfw3.h"
#include <app.hpp>
#include <memory>
#include <print>
#include <rendering.hpp>
#include <pipeline.hpp>
#include <layout.hpp>
#include <static_buffer.hpp>
#include <gomath.hpp>
#include <gocamera.hpp>
#include <chrono>
#include <vulkan/vulkan_core.h>

#define TO_APP_WINDOW(window) reinterpret_cast<App*>(glfwGetWindowUserPointer(window))

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
    uint32_t g_index_count = sizeof(CUBE_INDICES) / sizeof(CUBE_INDICES[0]);
    auto g_start_time = std::chrono::high_resolution_clock::now();

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
            .samples(VK_SAMPLE_COUNT_2_BIT)
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
        engi::vk::cmd_sync_barriers();

        // Keep staging buffers alive until frame is done
        engi::vk::delete_later(std::move(vb_staging.value()), frame_id);
        engi::vk::delete_later(std::move(ib_staging.value()), frame_id);

        g_initialized = true;
        std::println("[INFO] Cube rendering initialized");
    }

    auto render(VkCommandBuffer cmd) -> void
    {
        auto now = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(now - g_start_time).count();
        
        // Create rotation matrices
        float angle = elapsed * 0.5f; // Slow rotation speed
        go::mf4 rotation_x = go::rot4({1, 0, 0}, angle * 0.7f);
        go::mf4 rotation_y = go::rot4({0, 1, 0}, angle);
        go::mf4 rotation_z = go::rot4({0, 0, 1}, angle * 0.3f);
        
        go::mf4 rotation = rotation_y * rotation_x * rotation_z;
        
        // Translation and projection
        go::mf4 translation = go::translate(go::vf3{0, 0, -5});
        go::mf4 view = translation * rotation;
        
        go::mf4 proj = go::persp_proj_rh<float>(
            1.5708f,  // 90 degrees FOV
            800.0f,
            600.0f,
            0.01f,
            100.0f
        );
        go::Camera3D camera(
            {-5, 0, 0},
            {0, 0, 0},
            {0, 1, 0},
            {800.0f, 600.0f},
            true
        );
        
        //go::mf4 mvp = camera.get_proj() * camera.get_view();
        go::mf4 mvp = proj * view;
        
        PushConstant pc{mvp};
        
        // Bind pipeline and buffers
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeline.get());
        
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &g_vertex_buffer.buffer(), &offset);
        vkCmdBindIndexBuffer(cmd, g_index_buffer.buffer(), 0, VK_INDEX_TYPE_UINT16);
        
        // Push constants
        vkCmdPushConstants(
            cmd,
            g_layout.get(),
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(PushConstant),
            &pc
        );
        
        // Draw
        vkCmdDrawIndexed(cmd, g_index_count, 1, 0, 0, 0);
    }

    auto cleanup() -> void
    {
        if (!g_initialized)
            return;
        
        g_vertex_buffer = {};
        g_index_buffer = {};
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
}

auto engi::App::fn_key_press_interior(int key, int scancode, int action, int mods) -> void
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(m_glfw_window, true);
    }
}

auto engi::App::fn_mouse_press_interior(int button, int action, int mods) -> void
{
    double x, y;
    glfwGetCursorPos(m_glfw_window, &x, &y);
}

auto engi::App::fn_mouse_move_interior(double x, double y) -> void
{
}

auto engi::App::fn_scroll_interior(double xoffset, double yoffset) -> void
{
    double x, y;
    glfwGetCursorPos(m_glfw_window, &x, &y);
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
        auto acquire_result = engi::vk::acquire();
        if (acquire_result.result != VK_SUCCESS)
        {
            std::println("[ERROR] Failed to acquire swapchain image");
            break;
        }

        // Start recording commands
        auto cmd = engi::vk::cmd_start();

        // Initialize cube rendering (first frame only)
        TestCube::init(cmd, acquire_result.id);

        // Setup rendering area (full screen)
        VkRect2D view_rect{
            .offset = {0, 0},
            .extent = {800, 600}
        };
        engi::vk::view_start(cmd, view_rect, {34.0f/255.f, 34.0f/255.f, 59.0f/255.f, 1.0f});

        // Render the cube
        TestCube::render(cmd);

        // End rendering
        engi::vk::view_end(cmd);
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