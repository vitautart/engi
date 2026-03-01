#pragma once

#include <cstdint>

typedef struct GLFWwindow GLFWwindow;
typedef struct VkCommandBuffer_T* VkCommandBuffer;

namespace engi
{
class App
{
    struct Deinitalizer
    {
        GLFWwindow* m_glfw_window = nullptr;
        ~Deinitalizer() noexcept;
    };
public:
    virtual ~App() = default;
    auto run () noexcept -> void;
protected:
    App() = default;
    App(const App&) = delete;
    App(App&&) = delete;
    auto operator=(const App&) -> App& = delete;
    auto operator=(App&&) -> App& = delete;

    auto initialize_window_and_renderer(const char* title, int width, int height, bool resizable) noexcept -> bool;
    auto window() const noexcept -> GLFWwindow*;

    virtual auto on_frame(VkCommandBuffer cmd, uint32_t frame_id) -> void;
    virtual auto on_shutdown() -> void;
    virtual auto on_text_input(unsigned int codepoint) -> void;
    virtual auto on_key_press(int key, int scancode, int action, int mods) -> void;
    virtual auto on_mouse_press(int button, int action, int mods) -> void;
    virtual auto on_mouse_move(double x, double y) -> void;
    virtual auto on_scroll(double xoffset, double yoffset) -> void;
    virtual auto on_resize(int width,int height) -> void;

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
    GLFWwindow* m_glfw_window = nullptr;
    bool m_resize_pending = false;
    Deinitalizer m_deinititalizer{};
};
}
