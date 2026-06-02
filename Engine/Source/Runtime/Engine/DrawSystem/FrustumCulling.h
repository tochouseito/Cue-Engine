// FrustumCulling の役割と公開要素を定義する

#pragma once

// === DrawSystem includes ===
#include "StaticMeshPoolTypes.h"

// === Engine includes ===
#include <GpuData/ViewProjection.h>

// === C++ includes ===
#include <algorithm>
#include <cmath>

namespace Cue::DrawSystem
{
    struct FrustumPlane final
    {
        Math::float3 normal = Math::float3::zero();
        float distance = 0.0f;
    };

    struct Frustum final
    {
        FrustumPlane planes[6]{};
    };

    [[nodiscard]] inline FrustumPlane normalize_plane(
        float a_x,
        float a_y,
        float a_z,
        float a_w) noexcept
    {
        FrustumPlane plane{};
        const float length = std::sqrt(a_x * a_x + a_y * a_y + a_z * a_z);
        if (length <= 0.000001f)
        {
            return plane;
        }

        const float invLength = 1.0f / length;
        plane.normal = Math::float3(a_x * invLength, a_y * invLength, a_z * invLength);
        plane.distance = a_w * invLength;
        return plane;
    }

    [[nodiscard]] inline Frustum make_frustum(
        const GpuData::ViewProjectionGpu& a_viewProjection) noexcept
    {
        const Math::float4x4 matrix =
            a_viewProjection.view * a_viewProjection.projection;

        Frustum frustum{};
        frustum.planes[0] = normalize_plane(
            matrix.values[0][3] + matrix.values[0][0],
            matrix.values[1][3] + matrix.values[1][0],
            matrix.values[2][3] + matrix.values[2][0],
            matrix.values[3][3] + matrix.values[3][0]);
        frustum.planes[1] = normalize_plane(
            matrix.values[0][3] - matrix.values[0][0],
            matrix.values[1][3] - matrix.values[1][0],
            matrix.values[2][3] - matrix.values[2][0],
            matrix.values[3][3] - matrix.values[3][0]);
        frustum.planes[2] = normalize_plane(
            matrix.values[0][3] + matrix.values[0][1],
            matrix.values[1][3] + matrix.values[1][1],
            matrix.values[2][3] + matrix.values[2][1],
            matrix.values[3][3] + matrix.values[3][1]);
        frustum.planes[3] = normalize_plane(
            matrix.values[0][3] - matrix.values[0][1],
            matrix.values[1][3] - matrix.values[1][1],
            matrix.values[2][3] - matrix.values[2][1],
            matrix.values[3][3] - matrix.values[3][1]);
        frustum.planes[4] = normalize_plane(
            matrix.values[0][3] + matrix.values[0][2],
            matrix.values[1][3] + matrix.values[1][2],
            matrix.values[2][3] + matrix.values[2][2],
            matrix.values[3][3] + matrix.values[3][2]);
        frustum.planes[5] = normalize_plane(
            matrix.values[0][3] - matrix.values[0][2],
            matrix.values[1][3] - matrix.values[1][2],
            matrix.values[2][3] - matrix.values[2][2],
            matrix.values[3][3] - matrix.values[3][2]);
        return frustum;
    }

    [[nodiscard]] inline Math::float3 transform_point(
        const Math::float3& a_point,
        const Math::float4x4& a_matrix) noexcept
    {
        return Math::float3(
            a_point.x * a_matrix.values[0][0] +
                a_point.y * a_matrix.values[1][0] +
                a_point.z * a_matrix.values[2][0] +
                a_matrix.values[3][0],
            a_point.x * a_matrix.values[0][1] +
                a_point.y * a_matrix.values[1][1] +
                a_point.z * a_matrix.values[2][1] +
                a_matrix.values[3][1],
            a_point.x * a_matrix.values[0][2] +
                a_point.y * a_matrix.values[1][2] +
                a_point.z * a_matrix.values[2][2] +
                a_matrix.values[3][2]);
    }

    [[nodiscard]] inline float max_axis_scale(
        const Math::float4x4& a_matrix) noexcept
    {
        const Math::float3 axisX(
            a_matrix.values[0][0],
            a_matrix.values[0][1],
            a_matrix.values[0][2]);
        const Math::float3 axisY(
            a_matrix.values[1][0],
            a_matrix.values[1][1],
            a_matrix.values[1][2]);
        const Math::float3 axisZ(
            a_matrix.values[2][0],
            a_matrix.values[2][1],
            a_matrix.values[2][2]);
        return (std::max)({ axisX.length(), axisY.length(), axisZ.length() });
    }

    [[nodiscard]] inline bool intersects_frustum(
        const Frustum& a_frustum,
        const StaticMeshBounds& a_localBounds,
        const Math::float4x4& a_worldMatrix) noexcept
    {
        const Math::float3 center =
            transform_point(a_localBounds.center, a_worldMatrix);
        const float radius = a_localBounds.radius * max_axis_scale(a_worldMatrix);

        for (const FrustumPlane& plane : a_frustum.planes)
        {
            const float distance =
                plane.normal.dot(center) + plane.distance;
            if (distance < -radius)
            {
                return false;
            }
        }

        return true;
    }
}
