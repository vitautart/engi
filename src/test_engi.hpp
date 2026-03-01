#pragma once

#include <app.hpp>
#include <dynamic_buffer.hpp>
#include <font_atlas.hpp>
#include <gomath.hpp>
#include <layout.hpp>
#include <pipeline.hpp>
#include <rendering_overlay.hpp>
#include <static_buffer.hpp>
#include <ui_system.hpp>

#include <chrono>
#include <memory>

namespace engi
{
class TestEngi final : public App
{
public:
    static auto create() noexcept -> std::unique_ptr<TestEngi>;

private:
    auto on_frame(VkCommandBuffer cmd, uint32_t frame_id) -> void override;
    auto on_shutdown() -> void override;

    auto on_text_input(unsigned int codepoint) -> void override;
    auto on_key_press(int key, int scancode, int action, int mods) -> void override;
    auto on_mouse_press(int button, int action, int mods) -> void override;
    auto on_mouse_move(double x, double y) -> void override;
    auto on_scroll(double xoffset, double yoffset) -> void override;

    auto init(VkCommandBuffer cmd, uint32_t frame_id) -> void;
    auto render(VkCommandBuffer cmd) -> void;
    auto cleanup() -> void;

private:
    static constexpr bool ENABLE_UI_MSAA = true;

    bool m_initialized = false;
    vk::Pipeline m_pipeline;
    vk::Layout m_layout;
    vk::StaticBuffer m_vertex_buffer;
    vk::StaticBuffer m_index_buffer;
    vk::DynamicBuffer m_vertex_buffer_dynamic;
    uint32_t m_index_count = 0;
    std::chrono::high_resolution_clock::time_point m_start_time = std::chrono::high_resolution_clock::now();

    vk::FontMonoAtlas m_font_atlas;
    vk::RenderingOverlay m_overlay;

    ui::UISystem m_ui;
    ui::UILabel* m_counter_label = nullptr;
    int m_click_count = 0;
};
}
