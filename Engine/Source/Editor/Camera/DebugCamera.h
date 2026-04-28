#pragma once

// === Engine includes ===
#include <GpuData/ViewProjection.h>

// === C++ includes ===
#include <algorithm>
#include <cmath>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class DebugCamera final
    {
    public:
        struct Ray final
        {
            Math::float3 origin = Math::float3::zero();
            Math::float3 direction = Math::float3(0.0f, 0.0f, -1.0f);
        };

        DebugCamera() = default;
        ~DebugCamera() = default;

        void set_aspect(float a_aspectRatio) noexcept
        {
            if (a_aspectRatio > 0.0f)
            {
                m_aspectRatio = a_aspectRatio;
            }
        }

        void update(bool a_isActive) noexcept
        {
            if (!a_isActive)
            {
                return;
            }

            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            {
                m_previousMousePos = io.MousePos;
            }

            const ImVec2 mouseDelta(
                io.MousePos.x - m_previousMousePos.x,
                io.MousePos.y - m_previousMousePos.y);
            m_previousMousePos = io.MousePos;

            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                m_rotation.x = std::clamp(
                    m_rotation.x + mouseDelta.y * m_mouseSensitivity *
                    Math::k_pi / 180.0f,
                    -k_pitchLimit,
                    k_pitchLimit);
                m_rotation.y += mouseDelta.x * m_mouseSensitivity *
                    Math::k_pi / 180.0f;
            }

            const Math::float3 right = right_axis();
            const Math::float3 up = up_axis();
            const Math::float3 forward = forward_axis();

            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                m_position -= right * mouseDelta.x * m_mouseSensitivity;
                m_position += up * mouseDelta.y * m_mouseSensitivity;
            }

            if (io.MouseWheel != 0.0f)
            {
                m_position += forward * (-io.MouseWheel) * m_moveSpeed;
            }
        }

        [[nodiscard]] GpuData::ViewProjectionGpu view_projection() const noexcept
        {
            GpuData::ViewProjectionGpu viewProjection{};
            const Math::float4x4 worldMatrix = Math::make_affine_matrix(
                Math::float3(1.0f, 1.0f, 1.0f),
                m_rotation,
                m_position);
            viewProjection.view = Math::float4x4::inverse(worldMatrix);
            viewProjection.projection = Math::perspective_fov_matrix(
                m_fovY * Math::k_pi / 180.0f,
                m_aspectRatio,
                m_nearZ,
                m_farZ);

            return viewProjection;
        }

        [[nodiscard]] Ray pick_ray(
            float a_normalizedX,
            float a_normalizedY) const noexcept
        {
            const float tanHalfFov =
                std::tan((m_fovY * Math::k_pi / 180.0f) * 0.5f);
            const float viewX =
                (a_normalizedX * 2.0f - 1.0f) * m_aspectRatio * tanHalfFov;
            const float viewY = (1.0f - a_normalizedY * 2.0f) * tanHalfFov;
            Math::float3 direction =
                right_axis() * viewX +
                up_axis() * viewY +
                forward_axis();
            direction.normalize();

            return Ray{ m_position, direction };
        }

    private:
        [[nodiscard]] Math::float3 transform_direction(
            const Math::float3& a_direction) const noexcept
        {
            const Math::float4x4 rotationMatrix =
                Math::xyz_rotate_matrix(m_rotation);
            return Math::float3(
                a_direction.x * rotationMatrix.values[0][0] +
                    a_direction.y * rotationMatrix.values[1][0] +
                    a_direction.z * rotationMatrix.values[2][0],
                a_direction.x * rotationMatrix.values[0][1] +
                    a_direction.y * rotationMatrix.values[1][1] +
                    a_direction.z * rotationMatrix.values[2][1],
                a_direction.x * rotationMatrix.values[0][2] +
                    a_direction.y * rotationMatrix.values[1][2] +
                    a_direction.z * rotationMatrix.values[2][2]);
        }

        [[nodiscard]] Math::float3 right_axis() const noexcept
        {
            return transform_direction(Math::float3(1.0f, 0.0f, 0.0f));
        }

        [[nodiscard]] Math::float3 up_axis() const noexcept
        {
            return transform_direction(Math::float3(0.0f, 1.0f, 0.0f));
        }

        [[nodiscard]] Math::float3 forward_axis() const noexcept
        {
            return transform_direction(Math::float3(0.0f, 0.0f, -1.0f));
        }

    private:
        static constexpr float k_pitchLimit = 89.0f * Math::k_pi / 180.0f;

        Math::float3 m_position = Math::float3(0.0f, 0.0f, -30.0f);
        Math::float3 m_rotation = Math::float3(0.0f, 0.0f, 0.0f);
        ImVec2 m_previousMousePos = ImVec2(0.0f, 0.0f);
        float m_aspectRatio = 16.0f / 9.0f;
        float m_fovY = 60.0f;
        float m_nearZ = 0.1f;
        float m_farZ = 1000.0f;
        float m_mouseSensitivity = 0.1f;
        float m_moveSpeed = 0.1f;
    };
}
