#include "GLFW/glfw3.h"
#include <app.hpp>
#include <memory>
#include <print>
#include <rendering.hpp>

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
        glfwWaitEvents();
    }
    std::println("[INFO] Closing app...");
}