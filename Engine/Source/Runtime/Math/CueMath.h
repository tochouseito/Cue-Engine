#pragma once

// 1) 本モジュールは行ベクトル row-vector を前提にします。
// 2) 行列は行優先 row-major を前提にします。

#define NOMINMAX // Windows.h の min/max マクロを無効化

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <numbers>

// === Math includes ===
#include "TimeUnit.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4.h"

namespace Cue::Math
{
    static constexpr float k_pi = std::numbers::pi_v<float>; // 円周率

    /// @brief 値を指定された倍数に切り上げます。
    /// @param a_value 切り上げる値。
    /// @param a_step 切り上げ先の倍数。0の場合は無効です。
    /// @return 指定された倍数に切り上げられた値。
    [[nodiscard]]  uint32_t round_up_to_multiple(uint32_t a_value, uint32_t a_step) noexcept;

    /// @brief スケール行列を構築します。
    [[nodiscard]] float4x4 scale_matrix(float3 a_scale) noexcept;

    /// @brief X 軸回転行列を構築します。
    [[nodiscard]] float4x4 x_axis_matrix(float a_radian) noexcept;

    /// @brief Y 軸回転行列を構築します。
    [[nodiscard]] float4x4 y_axis_matrix(float a_radian) noexcept;

    /// @brief Z 軸回転行列を構築します。
    [[nodiscard]] float4x4 z_axis_matrix(float a_radian) noexcept;

    /// @brief XYZ 順の回転行列を構築します。
    [[nodiscard]] float4x4 xyz_rotate_matrix(float3 a_rotation) noexcept;

    /// @brief 平行移動行列を構築します。
    [[nodiscard]] float4x4 translate_matrix(float3 a_translation) noexcept;

    /// @brief ビューポート変換行列を構築します。
    [[nodiscard]] float4x4 viewport_matrix(
        float a_left,
        float a_top,
        float a_width,
        float a_height,
        float a_minDepth,
        float a_maxDepth) noexcept;

    /// @brief 透視投影行列を構築します。
    [[nodiscard]] float4x4 perspective_fov_matrix(
        float a_fovY,
        float a_aspectRatio,
        float a_nearClip,
        float a_farClip) noexcept;

    /// @brief 正射影行列を構築します。
    [[nodiscard]] float4x4 orthographic_matrix(
        float a_left,
        float a_top,
        float a_right,
        float a_bottom,
        float a_nearClip,
        float a_farClip) noexcept;

    /// @brief スケール、回転、平行移動を合成したアフィン行列を構築します。
    [[nodiscard]] float4x4 make_affine_matrix(float3 a_scale, float3 a_rotate, float3 a_translate) noexcept;
}
