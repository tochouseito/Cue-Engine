#include "DebugCamera.h"

// === C++ includes ===
#include <algorithm>
#include <cmath>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] Math::float4x4 look_at_lh(
            const Math::float3& eye,
            const Math::float3& target,
            const Math::float3& up) noexcept
        {
            const Math::float3 zAxis = Math::float3::normalize(target - eye);
            const Math::float3 xAxis =
                Math::float3::normalize(Math::float3::cross(up, zAxis));
            const Math::float3 yAxis = Math::float3::cross(zAxis, xAxis);

            Math::float4x4 view = Math::float4x4::identity();
            view.values[0][0] = xAxis.x;
            view.values[0][1] = yAxis.x;
            view.values[0][2] = zAxis.x;
            view.values[1][0] = xAxis.y;
            view.values[1][1] = yAxis.y;
            view.values[1][2] = zAxis.y;
            view.values[2][0] = xAxis.z;
            view.values[2][1] = yAxis.z;
            view.values[2][2] = zAxis.z;
            view.values[3][0] = -Math::float3::dot(xAxis, eye);
            view.values[3][1] = -Math::float3::dot(yAxis, eye);
            view.values[3][2] = -Math::float3::dot(zAxis, eye);
            return view;
        }
    }

    Math::float3 DebugCamera::forward() const noexcept
    {
        const float cosPitch = std::cos(m_pitch);
        return Math::float3::normalize(Math::float3(
            std::sin(m_yaw) * cosPitch,
            std::sin(m_pitch),
            std::cos(m_yaw) * cosPitch));
    }

    Math::float3 DebugCamera::right() const noexcept
    {
        return Math::float3::normalize(
            Math::float3::cross(Math::float3::unit_y(), forward()));
    }

    void DebugCamera::update(const Input& input) noexcept
    {
        if (input.lookActive)
        {
            m_yaw += input.mouseDeltaX * m_mouseSensitivity;
            m_pitch -= input.mouseDeltaY * m_mouseSensitivity;
            constexpr float k_pitchLimit =
                89.0f * DebugCameraConstants::k_pi / 180.0f;
            m_pitch = std::clamp(m_pitch, -k_pitchLimit, k_pitchLimit);
        }

        Math::float3 move = Math::float3::zero();
        const Math::float3 f = forward();
        const Math::float3 r = right();
        if (input.moveForward)
        {
            move += f;
        }
        if (input.moveBackward)
        {
            move -= f;
        }
        if (input.moveRight)
        {
            move += r;
        }
        if (input.moveLeft)
        {
            move -= r;
        }
        if (input.moveUp)
        {
            move += Math::float3::unit_y();
        }
        if (input.moveDown)
        {
            move -= Math::float3::unit_y();
        }

        if (!move.is_zero())
        {
            move.normalize();
            const float speed = input.fast ? m_fastMoveSpeed : m_moveSpeed;
            m_position += move * (speed * input.deltaSeconds);
        }
    }

    GpuData::ViewProjectionGpu DebugCamera::make_view_projection(
        uint32_t renderWidth,
        uint32_t renderHeight) const noexcept
    {
        const float aspectRatio = renderHeight == 0
            ? 1.0f
            : static_cast<float>(renderWidth) / static_cast<float>(renderHeight);

        GpuData::ViewProjectionGpu viewProjection{};
        viewProjection.view = look_at_lh(
            m_position,
            m_position + forward(),
            Math::float3::unit_y());
        viewProjection.projection = Math::perspective_fov_matrix(
            m_fovYRadians,
            aspectRatio,
            m_nearZ,
            m_farZ);
        return viewProjection;
    }
}
