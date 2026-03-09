#include "math_pch.h"
#include "CueMath.h"

namespace Cue::Math
{
    [[nodiscard]]
    constexpr uint32_t round_up_to_multiple(uint32_t value, uint32_t step) noexcept
    {
        uint32_t out = 0;

        // 1) step == 0 は無効
        if(step <= 0)
        {
            Assert::cue_assert(
                false,
                "Invalid step: {}. Step must be greater than 0.",
                step);
        }

        // 2) 既に倍数ならそのまま
        const uint32_t r = value % step;
        if (r == 0)
        {
            out = value;
            return out;
        }

        // 3-0) 次の倍数へ（オーバーフロー検出）
        const uint32_t add = step - r;
        const uint32_t maxv = (std::numeric_limits<uint32_t>::max)();
        // 3-1) value + add > maxv ならオーバーフローする
        if (value > (maxv - add))
        {
            Assert::cue_assert(
                false,
                "Overflow detected: value {} + add {} exceeds max uint32_t {}.",
                value, add, maxv);
        }

        out = value + add;
        return out;
    }

    [[nodiscard]] float4x4 scale_matrix(float3 scale) noexcept
    {
        // 1) 各軸のスケールを対角成分に設定する
        float4x4 matrix = float4x4::identity();
        matrix.m_values[0][0] = scale.m_x;
        matrix.m_values[1][1] = scale.m_y;
        matrix.m_values[2][2] = scale.m_z;
        return matrix;
    }

    [[nodiscard]] float4x4 x_axis_matrix(float radian) noexcept
    {
        // 1) X軸回転行列を構築する
        float4x4 matrix = float4x4::identity();
        const float c = std::cos(radian);
        const float s = std::sin(radian);
        matrix.m_values[1][1] = c;
        matrix.m_values[1][2] = s;
        matrix.m_values[2][1] = -s;
        matrix.m_values[2][2] = c;
        return matrix;
    }

    [[nodiscard]] float4x4 y_axis_matrix(float radian) noexcept
    {
        // 1) Y軸回転行列を構築する
        float4x4 matrix = float4x4::identity();
        const float c = std::cos(radian);
        const float s = std::sin(radian);
        matrix.m_values[0][0] = c;
        matrix.m_values[0][2] = -s;
        matrix.m_values[2][0] = s;
        matrix.m_values[2][2] = c;
        return matrix;
    }

    [[nodiscard]] float4x4 z_axis_matrix(float radian) noexcept
    {
        // 1) Z軸回転行列を構築する
        float4x4 matrix = float4x4::identity();
        const float c = std::cos(radian);
        const float s = std::sin(radian);
        matrix.m_values[0][0] = c;
        matrix.m_values[0][1] = s;
        matrix.m_values[1][0] = -s;
        matrix.m_values[1][1] = c;
        return matrix;
    }

    [[nodiscard]] float4x4 xyz_rotate_matrix(float3 rotation) noexcept
    {
        // 1) 各軸回転を合成する
        return x_axis_matrix(rotation.m_x) * y_axis_matrix(rotation.m_y) * z_axis_matrix(rotation.m_z);
    }

    [[nodiscard]] float4x4 translate_matrix(float3 translation) noexcept
    {
        // 1) 単位行列に平行移動成分を設定する
        float4x4 matrix = float4x4::identity();
        matrix.m_values[3][0] = translation.m_x;
        matrix.m_values[3][1] = translation.m_y;
        matrix.m_values[3][2] = translation.m_z;
        return matrix;
    }

    [[nodiscard]] float4x4 viewport_matrix(float left, float top, float width, float height, float minDepth, float maxDepth) noexcept
    {
        // 1) 画面座標への変換行列を構築する
        float4x4 matrix = float4x4::identity();
        matrix.m_values[0][0] = width / 2.0f;
        matrix.m_values[1][1] = -height / 2.0f; // Y軸反転
        matrix.m_values[2][2] = maxDepth - minDepth;
        matrix.m_values[3][0] = left + width / 2.0f;
        matrix.m_values[3][1] = top + height / 2.0f;
        matrix.m_values[3][2] = minDepth;
        return matrix;
    }

    [[nodiscard]] float4x4 perspective_fov_matrix(float fovY, float aspectRatio, float nearClip, float farClip) noexcept
    {
        // 1) 透視投影行列を構築する
        float4x4 matrix = float4x4::zero();
        const float f = std::tan(fovY / 2.0f);
        matrix.m_values[0][0] = 1.0f / (aspectRatio * f);
        matrix.m_values[1][1] = 1.0f / f;
        matrix.m_values[2][2] = (farClip + nearClip) / (farClip - nearClip);
        matrix.m_values[2][3] = 1.0f;
        matrix.m_values[3][2] = -(2.0f * farClip * nearClip) / (farClip - nearClip);
        return matrix;
    }

    [[nodiscard]] float4x4 orthographic_matrix(float left, float top, float right, float bottom, float nearClip, float farClip) noexcept
    {
        // 1) 正射影行列を構築する
        float4x4 matrix = float4x4::identity();
        matrix.m_values[0][0] = 2.0f / (right - left);
        matrix.m_values[1][1] = 2.0f / (top - bottom);
        matrix.m_values[2][2] = 1.0f / (farClip - nearClip);
        matrix.m_values[3][0] = (left + right) / (left - right);
        matrix.m_values[3][1] = (top + bottom) / (bottom - top);
        matrix.m_values[3][2] = nearClip / (nearClip - farClip);
        return matrix;
    }

    [[nodiscard]] float4x4 make_affine_matrix(float3 scale, float3 rotate, float3 translate) noexcept
    {
        // 1) スケール、回転、平行移動を合成してアフィン変換行列を構築する
        return scale_matrix(scale) * xyz_rotate_matrix(rotate) * translate_matrix(translate);
    }
}
