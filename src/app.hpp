#pragma once

#include <memory>

typedef struct GLFWwindow GLFWwindow;

namespace engi
{
class App
{
public:
    ~App() noexcept;

    auto run () noexcept -> void;
    static auto create() noexcept -> std::unique_ptr<App>;
private:
    explicit App() = default;
    explicit App(const App&) = delete;
    explicit App(App&&) = delete;
private:
    static auto fnTextInputExterior(GLFWwindow* window, unsigned int codepoint) -> void;
    static auto fnKeyPressExterior(GLFWwindow* window, int key, int scancode, int action, int mods) -> void;
    static auto fnMousePressExterior(GLFWwindow* window, int button, int action, int mods) -> void;
    static auto fnMouseMoveExterior(GLFWwindow* window, double x, double y) -> void;
    static auto fnScrollExterior(GLFWwindow* window, double xOffset, double yOffset) -> void;
    static auto fnResizeExterior(GLFWwindow* window, int width,int height) -> void;

    auto fnTextInputInterior(unsigned int codepoint) -> void;
    auto fnKeyPressInterior(int key, int scancode, int action, int mods) -> void;
    auto fnMousePressInterior(int button, int action, int mods) -> void;
    auto fnMouseMoveInterior(double x, double y) -> void;
    auto fnScrollInterior(double xOffset, double yOffset) -> void;
    auto fnResizeInterior(int width,int height) -> void;

private:
    GLFWwindow* mGLFWWindow;
};
}
