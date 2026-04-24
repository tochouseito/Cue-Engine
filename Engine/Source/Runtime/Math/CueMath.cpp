// === Math includes ===
#include "Math_pch.h"
#include "CueMath.h"

namespace Cue::Math
{
    [[nodiscard]]
    uint32_t round_up_to_multiple(uint32_t a_value, uint32_t a_step) noexcept
    {
        uint32_t out = 0;

        // 1) 0 の倍数への切り上げは定義できないため停止する
        if (a_step == 0)
        {
            CUE_ASSERTF(false, "Invalid step: %u. Step must be greater than 0.", a_step);
        }

        // 2) 既に倍数ならそのまま
        const uint32_t r = a_value % a_step;
        if (r == 0)
        {
            out = a_value;
            return out;
        }

        // 3-0) 次の倍数へ（オーバーフロー検出）
        const uint32_t add = a_step - r;
        const uint32_t maxValue = (std::numeric_limits<uint32_t>::max)();
        // 3-1) a_value + add > maxValue ならオーバーフローする
        if (a_value > (maxValue - add))
        {
            CUE_ASSERTF(false, "Overflow detected: value %u + add %u exceeds max uint32_t %u.", a_value, add, maxValue);
        }

        out = a_value + add;
        return out;
    }

    [[nodiscard]] float4x4 scale_matrix(float3 a_scale) noexcept
    {
        // 1) 各軸のスケールを対角成分に設定する
        float4x4 matrix = float4x4::identity();
        matrix.values[0][0] = a_scale.x;
        matrix.values[1][1] = a_scale.y;
        matrix.values[2][2] = a_scale.z;
        return matrix;
    }

    [[nodiscard]] float4x4 x_axis_matrix(float a_radian) noexcept
    {
        // 1) X軸回転行列を構築する
        float4x4 matrix = float4x4::identity();
        const float c = std::cos(a_radian);
        const float s = std::sin(a_radian);
        matrix.values[1][1] = c;
        matrix.values[1][2] = s;
        matrix.values[2][1] = -s;
        matrix.values[2][2] = c;
        return matrix;
    }

    [[nodiscard]] float4x4 y_axis_matrix(float a_radian) noexcept
    {
        // 1) Y軸回転行列を構築する
        float4x4 matrix = float4x4::identity();
        const float c = std::cos(a_radian);
        const float s = std::sin(a_radian);
        matrix.values[0][0] = c;
        matrix.values[0][2] = -s;
        matrix.values[2][0] = s;
        matrix.values[2][2] = c;
        return matrix;
    }

    [[nodiscard]] float4x4 z_axis_matrix(float a_radian) noexcept
    {
        // 1) Z軸回転行列を構築する
        float4x4 matrix = float4x4::identity();
        const float c = std::cos(a_radian);
        const float s = std::sin(a_radian);
        matrix.values[0][0] = c;
        matrix.values[0][1] = s;
        matrix.values[1][0] = -s;
        matrix.values[1][1] = c;
        return matrix;
    }

    [[nodiscard]] float4x4 xyz_rotate_matrix(float3 a_rotation) noexcept
    {
        // 1) 各軸回転を合成する
        return x_axis_matrix(a_rotation.x) * y_axis_matrix(a_rotation.y) * z_axis_matrix(a_rotation.z);
    }

    [[nodiscard]] float4x4 translate_matrix(float3 a_translation) noexcept
    {
        // 1) 単位行列に平行移動成分を設定する
        float4x4 matrix = float4x4::identity();
        matrix.values[3][0] = a_translation.x;
        matrix.values[3][1] = a_translation.y;
        matrix.values[3][2] = a_translation.z;
        return matrix;
    }

    [[nodiscard]] float4x4 viewport_matrix(
        float a_left,
        float a_top,
        float a_width,
        float a_height,
        float a_minDepth,
        float a_maxDepth) noexcept
    {
        // 1) 画面座標への変換行列を構築する
        float4x4 matrix = float4x4::identity();
        matrix.values[0][0] = a_width / 2.0f;
        matrix.values[1][1] = -a_height / 2.0f; // Y軸反転
        matrix.values[2][2] = a_maxDepth - a_minDepth;
        matrix.values[3][0] = a_left + a_width / 2.0f;
        matrix.values[3][1] = a_top + a_height / 2.0f;
        matrix.values[3][2] = a_minDepth;
        return matrix;
    }

    [[nodiscard]] float4x4 perspective_fov_matrix(
        float a_fovY,
        float a_aspectRatio,
        float a_nearClip,
        float a_farClip) noexcept
    {
        // 1) 透視投影行列を構築する
        float4x4 matrix = float4x4::zero();
        const float f = std::tan(a_fovY / 2.0f);
        matrix.values[0][0] = 1.0f / (a_aspectRatio * f);
        matrix.values[1][1] = 1.0f / f;
        matrix.values[2][2] = (a_farClip + a_nearClip) / (a_farClip - a_nearClip);
        matrix.values[2][3] = 1.0f;
        matrix.values[3][2] = -(2.0f * a_farClip * a_nearClip) / (a_farClip - a_nearClip);
        return matrix;
    }

    [[nodiscard]] float4x4 orthographic_matrix(
        float a_left,
        float a_top,
        float a_right,
        float a_bottom,
        float a_nearClip,
        float a_farClip) noexcept
    {
        // 1) 正射影行列を構築する
        float4x4 matrix = float4x4::identity();
        matrix.values[0][0] = 2.0f / (a_right - a_left);
        matrix.values[1][1] = 2.0f / (a_top - a_bottom);
        matrix.values[2][2] = 1.0f / (a_farClip - a_nearClip);
        matrix.values[3][0] = (a_left + a_right) / (a_left - a_right);
        matrix.values[3][1] = (a_top + a_bottom) / (a_bottom - a_top);
        matrix.values[3][2] = a_nearClip / (a_nearClip - a_farClip);
        return matrix;
    }

    [[nodiscard]] float4x4 make_affine_matrix(float3 a_scale, float3 a_rotate, float3 a_translate) noexcept
    {
        // 1) スケール、回転、平行移動を合成してアフィン変換行列を構築する
        return scale_matrix(a_scale) * xyz_rotate_matrix(a_rotate) * translate_matrix(a_translate);
    }
}
