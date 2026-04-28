#pragma once

// === Math includes ===
#include <CueMath.h>

namespace Cue::GameCore::NavMath
{
    inline constexpr float k_epsilon = 0.00001f;

    /// @brief 3D 座標を XZ 平面の 2D 座標へ投影します。
    [[nodiscard]] Math::float2 project_xz(
        const Math::float3& a_position) noexcept;

    /// @brief 2D ベクトルの外積 Z 成分を返します。
    [[nodiscard]] float cross_2d(
        const Math::float2& a_left,
        const Math::float2& a_right) noexcept;

    /// @brief 3D 座標間の距離の二乗を返します。
    [[nodiscard]] float distance_sq(
        const Math::float3& a_left,
        const Math::float3& a_right) noexcept;

    /// @brief 3D 座標間の距離を返します。
    [[nodiscard]] float distance(
        const Math::float3& a_left,
        const Math::float3& a_right) noexcept;

    /// @brief 点が XZ 平面上で三角形に含まれるか判定します。
    [[nodiscard]] bool point_in_triangle_xz(
        const Math::float3& a_point,
        const Math::float3& a_a,
        const Math::float3& a_b,
        const Math::float3& a_c,
        float a_epsilon = k_epsilon) noexcept;

    /// @brief XZ 座標から三角形平面上の Y 座標を計算します。
    [[nodiscard]] bool triangle_height_at_xz(
        const Math::float3& a_point,
        const Math::float3& a_a,
        const Math::float3& a_b,
        const Math::float3& a_c,
        float& a_outHeight) noexcept;

    /// @brief 線分上の最近点を返します。
    [[nodiscard]] Math::float3 closest_point_on_segment(
        const Math::float3& a_point,
        const Math::float3& a_a,
        const Math::float3& a_b) noexcept;

    /// @brief 三角形上の最近点を返します。
    [[nodiscard]] Math::float3 closest_point_on_triangle(
        const Math::float3& a_point,
        const Math::float3& a_a,
        const Math::float3& a_b,
        const Math::float3& a_c) noexcept;
}
