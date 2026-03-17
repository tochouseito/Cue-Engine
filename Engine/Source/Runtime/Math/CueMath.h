#pragma once

/*
行ベクトル row-vector
行優先 row-major
*/

#define NOMINMAX // Windows.h の min/max マクロを無効化

#include <cstdint>
#include <limits>
#include <numbers>
#include "TimeUnit.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4.h"

namespace Cue::Math
{
    static constexpr float pi = std::numbers::pi_v<float>;// 円周率

    /// @brief 値を指定された倍数に切り上げます。
    /// @param value 切り上げる値。
    /// @param step 切り上げ先の倍数。0の場合は無効です。
    /// @return 指定された倍数に切り上げられた値。
    [[nodiscard]] static constexpr uint32_t round_up_to_multiple(uint32_t value, uint32_t step) noexcept;

    [[nodiscard]] float4x4 scale_matrix(float3 scale) noexcept;

    [[nodiscard]] float4x4 x_axis_matrix(float radian) noexcept;

    [[nodiscard]] float4x4 y_axis_matrix(float radian) noexcept;

    [[nodiscard]] float4x4 z_axis_matrix(float radian) noexcept;

    [[nodiscard]] float4x4 xyz_rotate_matrix(float3 rotation) noexcept;

    [[nodiscard]] float4x4 translate_matrix(float3 translation) noexcept;

    [[nodiscard]] float4x4 viewport_matrix(float left, float top, float width, float height, float minDepth, float maxDepth) noexcept;

    [[nodiscard]] float4x4 perspective_fov_matrix(float fovY, float aspectRatio, float nearClip, float farClip) noexcept;

    [[nodiscard]] float4x4 orthographic_matrix(float left, float top, float right, float bottom, float nearClip, float farClip) noexcept;

    [[nodiscard]] float4x4 make_affine_matrix(float3 scale, float3 rotate, float3 translate)noexcept;
}
