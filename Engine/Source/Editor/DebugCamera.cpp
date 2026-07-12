#include "DebugCamera.h"

// === ImGui includes ===
#include <imgui.h>

// === C++ includes ===
#include <algorithm>
#include <cmath>

namespace Cue::Editor
{
    DebugCamera::DebugCamera() noexcept
    {
        rebuild_render_view(1, 1);
    }

    void DebugCamera::update(const DebugCameraViewport& a_viewport)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool canStartControl = a_viewport.isHovered || a_viewport.isFocused;
        if (canStartControl && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            m_isControlling = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
        {
            m_isControlling = false;
        }

        if (canStartControl && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        {
            m_isPanning = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            m_isPanning = false;
        }

        if (m_isControlling)
        {
            // DebugCamera は Euler 角を保持せず、mouse delta から作った増分 Quaternion
            // を合成する。
            const Math::Quaternion yawDelta =
                make_axis_angle_quaternion(Math::float3::unit_y(), io.MouseDelta.x * m_rotateSpeed);
            const Math::Quaternion pitchDelta =
                make_axis_angle_quaternion(Math::float3::unit_x(), io.MouseDelta.y * m_rotateSpeed);
            m_orientation = Math::Quaternion::normalize(yawDelta * m_orientation * pitchDelta);

            const float deltaTime = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
            const float speed = m_moveSpeed * (io.KeyShift ? m_fastMoveMultiplier : 1.0f) * deltaTime;
            const Math::float3 forward = rotate_vector(m_orientation, Math::float3::unit_z());
            const Math::float3 right = rotate_vector(m_orientation, Math::float3::unit_x());
            const Math::float3 up = rotate_vector(m_orientation, Math::float3::unit_y());

            Math::float3 movement = Math::float3::zero();
            if (ImGui::IsKeyDown(ImGuiKey_W))
            {
                movement += forward;
            }
            if (ImGui::IsKeyDown(ImGuiKey_S))
            {
                movement -= forward;
            }
            if (ImGui::IsKeyDown(ImGuiKey_D))
            {
                movement += right;
            }
            if (ImGui::IsKeyDown(ImGuiKey_A))
            {
                movement -= right;
            }
            if (ImGui::IsKeyDown(ImGuiKey_E))
            {
                movement += up;
            }
            if (ImGui::IsKeyDown(ImGuiKey_Q))
            {
                movement -= up;
            }
            if (!movement.is_zero())
            {
                m_position += Math::float3::normalize(movement) * speed;
            }
        }

        if (m_isPanning)
        {
            const Math::float3 right = rotate_vector(m_orientation, Math::float3::unit_x());
            const Math::float3 up = rotate_vector(m_orientation, Math::float3::unit_y());
            m_position -= right * (io.MouseDelta.x * m_panSpeed);
            m_position += up * (io.MouseDelta.y * m_panSpeed);
        }

        if (a_viewport.isHovered && io.MouseWheel != 0.0f)
        {
            const float speed = m_scrollSpeed * (io.KeyShift ? m_fastMoveMultiplier : 1.0f);
            const Math::float3 forward = rotate_vector(m_orientation, Math::float3::unit_z());
            m_position += forward * (io.MouseWheel * speed);
        }

        rebuild_render_view(std::max(a_viewport.width, 1u), std::max(a_viewport.height, 1u));
    }

    void DebugCamera::rebuild_render_view(uint32_t a_width, uint32_t a_height) noexcept
    {
        const float aspectRatio = static_cast<float>(std::max(a_width, 1u)) / static_cast<float>(std::max(a_height, 1u));
        const Math::float4x4 worldMatrix = Math::make_affine_matrix(Math::float3::one(), m_orientation, m_position);

        m_renderView.view = Math::float4x4::inverse(worldMatrix);
        m_renderView.projection = Math::perspective_fov_matrix(m_fovYRadians, aspectRatio, m_nearZ, m_farZ);
        m_renderView.position = m_position;
        m_renderView.width = a_width;
        m_renderView.height = a_height;
        m_renderView.nearZ = m_nearZ;
        m_renderView.farZ = m_farZ;
    }
} // namespace Cue::Editor
