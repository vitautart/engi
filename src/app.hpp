#pragma once

#include <memory>

typedef struct GLFWwindow GLFWwindow;

namespace engi
{
class App
{
struct Deinitalizer
{
    GLFWwindow* m_glfw_window;
    ~Deinitalizer() noexcept;
};
public:
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
    static auto fnScrollExterior(GLFWwindow* window, double xoffset, double yoffset) -> void;
    static auto fnResizeExterior(GLFWwindow* window, int width,int height) -> void;

    auto fn_text_input_interior(unsigned int codepoint) -> void;
    auto fn_key_press_interior(int key, int scancode, int action, int mods) -> void;
    auto fn_mouse_press_interior(int button, int action, int mods) -> void;
    auto fn_mouse_move_interior(double x, double y) -> void;
    auto fn_scroll_interior(double xoffset, double yoffset) -> void;
    auto fn_resize_interior(int width,int height) -> void;

private:
    GLFWwindow* m_glfw_window;
    Deinitalizer m_deinititalizer;
    // Add rest of RAII members below Deinitalizer
    // To ensure proper destruction order
};
}
