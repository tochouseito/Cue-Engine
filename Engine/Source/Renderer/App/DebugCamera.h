#pragma once

/// ****************************************************************************
/// Renderer 検証用の自由移動カメラ
/// ****************************************************************************

// === Engine includes ===
#include <GpuData/ViewProjection.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::Renderer
{
    namespace DebugCameraConstants
    {
        constexpr float k_pi = 3.14159265358979323846f;
    }

    class DebugCamera final
    {
    public:
        struct Input final
        {
            bool moveForward = false;
            bool moveBackward = false;
            bool moveLeft = false;
            bool moveRight = false;
            bool moveUp = false;
            bool moveDown = false;
            bool fast = false;
            bool lookActive = false;
            float mouseDeltaX = 0.0f;
            float mouseDeltaY = 0.0f;
            float gamepadMoveX = 0.0f;
            float gamepadMoveY = 0.0f;
            float gamepadMoveVertical = 0.0f;
            float gamepadLookX = 0.0f;
            float gamepadLookY = 0.0f;
            float deltaSeconds = 0.0f;
        };

        DebugCamera() = default;

        void update(const Input& input) noexcept;

        [[nodiscard]] GpuData::ViewProjectionGpu make_view_projection(
            uint32_t renderWidth,
            uint32_t renderHeight) const noexcept;

    private:
        [[nodiscard]] Math::float3 forward() const noexcept;
        [[nodiscard]] Math::float3 right() const noexcept;

        Math::float3 m_position{ 0.0f, 0.05f, -2.0f };
        float m_yaw = 0.0f;
        float m_pitch = 0.0f;
        float m_fovYRadians = 60.0f * DebugCameraConstants::k_pi / 180.0f;
        float m_nearZ = 0.01f;
        float m_farZ = 100.0f;
        float m_moveSpeed = 1.5f;
        float m_fastMoveSpeed = 5.0f;
        float m_mouseSensitivity = 0.0025f;
        float m_gamepadLookSensitivity = 2.5f;
    };
}
