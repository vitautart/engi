#include "GLFW/glfw3.h"
#include <app.hpp>
#include <print>
#include <rendering.hpp>
#include <vulkan/vulkan_core.h>

#define TO_APP_WINDOW(window) reinterpret_cast<App*>(glfwGetWindowUserPointer(window))

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
    on_text_input(codepoint);
}

auto engi::App::fn_key_press_interior(int key, int scancode, int action, int mods) -> void
{
    on_key_press(key, scancode, action, mods);
}

auto engi::App::fn_mouse_press_interior(int button, int action, int mods) -> void
{
    on_mouse_press(button, action, mods);
}

auto engi::App::fn_mouse_move_interior(double x, double y) -> void
{
    on_mouse_move(x, y);
}

auto engi::App::fn_scroll_interior(double xoffset, double yoffset) -> void
{
    on_scroll(xoffset, yoffset);
}

auto engi::App::fn_resize_interior(int width,int height) -> void
{
    on_resize(width, height);
}

auto engi::App::initialize_window_and_renderer(const char* title, int width, int height, bool resizable) noexcept -> bool
{
    if (!glfwInit())
    {
        std::println("[ERROR] Can't initialize glfw");
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);
    m_glfw_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    m_deinititalizer.m_glfw_window = m_glfw_window;
    if (!m_glfw_window)
    {
        std::println("[ERROR] Can't create window");
        return false;
    }
    glfwSetCharCallback(m_glfw_window, fnTextInputExterior);
    glfwSetKeyCallback(m_glfw_window, fnKeyPressExterior);
    glfwSetMouseButtonCallback(m_glfw_window, fnMousePressExterior);
    glfwSetCursorPosCallback(m_glfw_window, fnMouseMoveExterior);
    glfwSetScrollCallback(m_glfw_window, fnScrollExterior);
    glfwSetWindowSizeCallback(m_glfw_window, fnResizeExterior);
    glfwSetWindowUserPointer(m_glfw_window, this);

    if (!engi::vk::init(m_glfw_window))
    {
        std::println("[ERROR] Can't init rendering systen");
        return false;
    }

    return true;
}

auto engi::App::run() noexcept -> void
{
    std::println("[INFO] Starting app...");
    while(!glfwWindowShouldClose(m_glfw_window))
    {
        if (m_resize_pending)
        {
            if (engi::vk::resize(m_glfw_window))
                m_resize_pending = false;
            else
            {
                glfwWaitEvents();
                continue;
            }
        }

        // Acquire next swapchain image
        auto frame = engi::vk::wait_frame();
        auto acquire_result = engi::vk::acquire();
        if (acquire_result.result == VK_ERROR_OUT_OF_DATE_KHR || acquire_result.result == VK_SUBOPTIMAL_KHR)
        {
            m_resize_pending = true;
            continue;
        }
        if (acquire_result.result != VK_SUCCESS)
        {
            std::println("[ERROR] Failed to acquire swapchain image");
            break;
        }

        // Start recording commands
        auto cmd = engi::vk::cmd_start();

        on_frame(cmd, frame);

        engi::vk::cmd_end();

        // Submit and present
        if (!engi::vk::submit())
        {
            m_resize_pending = true;
            continue;
        }

        glfwWaitEvents();
    }
    std::println("[INFO] Waiting for GPU to finish...");
    engi::vk::wait();
    std::println("[INFO] Closing app...");
    on_shutdown();
}

auto engi::App::window() const noexcept -> GLFWwindow*
{
    return m_glfw_window;
}

auto engi::App::on_frame(VkCommandBuffer, uint32_t) -> void
{
}

auto engi::App::on_shutdown() -> void
{
}

auto engi::App::on_text_input(unsigned int) -> void
{
}

auto engi::App::on_key_press(int key, int, int action, int) -> void
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(m_glfw_window, true);
}

auto engi::App::on_mouse_press(int, int, int) -> void
{
}

auto engi::App::on_mouse_move(double, double) -> void
{
}

auto engi::App::on_scroll(double, double) -> void
{
}

auto engi::App::on_resize(int width, int height) -> void
{
    if (width > 0 && height > 0)
        m_resize_pending = true;
}