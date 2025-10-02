#include "GLFW/glfw3.h"
#include <app.hpp>
#include <memory>
#include <print>

#define TO_APP_WINDOW(window) reinterpret_cast<App*>(glfwGetWindowUserPointer(window))

auto engi::App::fnTextInputExterior(GLFWwindow* window, unsigned int codepoint) -> void
{
    TO_APP_WINDOW(window)->fnTextInputInterior(codepoint);
}
auto engi::App::fnKeyPressExterior(GLFWwindow* window, int key, int scancode, int action, int mods) -> void
{
    TO_APP_WINDOW(window)->fnKeyPressInterior(key, scancode, action, mods);
}
auto engi::App::fnMousePressExterior(GLFWwindow* window, int button, int action, int mods) -> void
{
    TO_APP_WINDOW(window)->fnMousePressInterior(button, action, mods);
}
auto engi::App::fnMouseMoveExterior(GLFWwindow* window, double x, double y) -> void
{
    TO_APP_WINDOW(window)->fnMouseMoveInterior(x, y);
}
auto engi::App::fnScrollExterior(GLFWwindow* window, double xOffset, double yOffset) -> void
{
    TO_APP_WINDOW(window)->fnScrollInterior(xOffset, yOffset);
}
auto engi::App::fnResizeExterior(GLFWwindow* window, int width,int height) -> void
{
    TO_APP_WINDOW(window)->fnResizeInterior(width, height);
}

auto engi::App::fnTextInputInterior(unsigned int codepoint) -> void
{
}

auto engi::App::fnKeyPressInterior(int key, int scancode, int action, int mods) -> void
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(mGLFWWindow, true);
    }
}

auto engi::App::fnMousePressInterior(int button, int action, int mods) -> void
{
    double x, y;
    glfwGetCursorPos(mGLFWWindow, &x, &y);
}

auto engi::App::fnMouseMoveInterior(double x, double y) -> void
{
}

auto engi::App::fnScrollInterior(double xOffset, double yOffset) -> void
{
    double x, y;
    glfwGetCursorPos(mGLFWWindow, &x, &y);
}

auto engi::App::fnResizeInterior(int width,int height) -> void
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
        std::println("[ERROR] Can't initialize glfw\n");
        return  nullptr;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, false);
    app->mGLFWWindow = glfwCreateWindow(800, 600, "HomeCAD", NULL, NULL);
    if (!app->mGLFWWindow)
    {
        std::println("[ERROR] Can't create window\n");
        return nullptr;
    }
    glfwSetCharCallback(app->mGLFWWindow, fnTextInputExterior);
    glfwSetKeyCallback(app->mGLFWWindow, fnKeyPressExterior);
    glfwSetMouseButtonCallback(app->mGLFWWindow, fnMousePressExterior);
    glfwSetCursorPosCallback(app->mGLFWWindow, fnMouseMoveExterior);
    glfwSetScrollCallback(app->mGLFWWindow, fnScrollExterior);
    glfwSetWindowSizeCallback(app->mGLFWWindow, fnResizeExterior);
    glfwSetWindowUserPointer(app->mGLFWWindow, app.get());

    return app;
}


engi::App::~App() noexcept
{
    glfwDestroyWindow(mGLFWWindow);
    glfwTerminate();
}

auto engi::App::run() noexcept -> void
{
    std::println("[INFO] Starting app...");
    while(!glfwWindowShouldClose(mGLFWWindow))
    {
        glfwWaitEvents();
    }
    std::println("[INFO] Closing app...");
}