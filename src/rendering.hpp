#pragma once

typedef struct GLFWwindow GLFWwindow;

namespace engi::Rendering {
    auto init(GLFWwindow* window) noexcept -> bool;
    auto destroy() noexcept -> void;
}