#pragma once

/// ****************************************************************************
/// Editor 用 DebugCamera
/// ****************************************************************************

// === Math includes ===
#include <CueMath.h>

// === Engine includes ===
#include <DrawSystem/RenderView.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::Editor
{
    struct DebugCameraViewport final
    {
        uint32_t width = 1;
        uint32_t height = 1;
        bool isHovered = false;
        bool isFocused = false;
    };

    /// @brief Editor が描画確認に使う自由視点 camera。
    class DebugCamera final
    {
    public:
        DebugCamera() noexcept;

        /// @brief ImGui 入力を読み、DebugCamera の視点を更新する。
        void update(const DebugCameraViewport& a_viewport);

        /// @brief DrawSystem に渡す描画視点を返す。
        [[nodiscard]] const DrawSystem::RenderView& render_view() const noexcept
        {
            return m_renderView;
        }

    private:
        /// @brief 現在の姿勢から RenderView を再構築する。
        void rebuild_render_view(uint32_t a_width, uint32_t a_height) noexcept;

        Math::float3 m_position{ 0.0f, 0.0f, -6.0f };
        Math::Quaternion m_orientation = Math::Quaternion::identity();
        DrawSystem::RenderView m_renderView{};

        float m_fovYRadians = 1.0471975512f;
        float m_nearZ = 0.1f;
        float m_farZ = 1000.0f;
        float m_moveSpeed = 5.0f;
        float m_fastMoveMultiplier = 4.0f;
        float m_rotateSpeed = 0.0035f;
        bool m_isControlling = false;
    };
} // namespace Cue::Editor
