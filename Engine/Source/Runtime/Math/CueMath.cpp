#include "Math_pch.h"
#include "CueMath.h"

namespace Cue::Math
{
    [[nodiscard]]
    uint32_t round_up_to_multiple(uint32_t a_value, uint32_t a_step) noexcept
    {
        uint32_t out = 0;

        // - 0 の倍数への切り上げは定義できないため停止する
        if (a_step == 0)
        {
            CUE_ASSERT_FORMAT(false, "Invalid step: %u. Step must be greater than 0.", a_step);
        }

        // - 既に倍数ならそのまま
        const uint32_t r = a_value % a_step;
        if (r == 0)
        {
            out = a_value;
            return out;
        }

        // 次の倍数へ（オーバーフロー検出）
        const uint32_t add = a_step - r;
        constexpr uint32_t maxValue = (std::numeric_limits<uint32_t>::max)();
        // a_value + add > maxValue ならオーバーフローする
        if (a_value > (maxValue - add))
        {
            CUE_ASSERT_FORMAT(false, "Overflow detected: value %u + add %u exceeds max uint32_t %u.", a_value, add, maxValue);
        }

        out = a_value + add;
        return out;
    }

    [[nodiscard]]
    uint64_t round_up_to_multiple(uint64_t a_value, uint64_t a_step) noexcept
    {
        uint64_t out = 0;

        // - 0 の倍数への切り上げは定義できないため停止する
        if (a_step == 0)
        {
            CUE_ASSERT_FORMAT(false, "Invalid step: %llu. Step must be greater than 0.", static_cast<unsigned long long>(a_step));
        }

        // - 既に倍数ならそのまま
        const uint64_t r = a_value % a_step;
        if (r == 0)
        {
            out = a_value;
            return out;
        }

        // 次の倍数へ（オーバーフロー検出）
        const uint64_t add = a_step - r;
        constexpr uint64_t maxValue = (std::numeric_limits<uint64_t>::max)();
        // a_value + add > maxValue ならオーバーフローする
        if (a_value > (maxValue - add))
        {
            CUE_ASSERT_FORMAT(
                false,
                "Overflow detected: value %llu + add %llu exceeds max uint64_t %llu.",
                static_cast<unsigned long long>(a_value),
                static_cast<unsigned long long>(add),
                static_cast<unsigned long long>(maxValue));
        }

        out = a_value + add;
        return out;
    }

    [[nodiscard]] float4x4 scale_matrix(float3 a_scale) noexcept
    {
        // - 各軸のスケールを対角成分に設定する
        float4x4 matrix = float4x4::identity();
        matrix.values[0][0] = a_scale.x;
        matrix.values[1][1] = a_scale.y;
        matrix.values[2][2] = a_scale.z;
        return matrix;
    }

    [[nodiscard]] float4x4 x_axis_matrix(float a_radian) noexcept
    {
        // - X軸回転行列を構築する
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
        // - Y軸回転行列を構築する
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
        // - Z軸回転行列を構築する
        float4x4 matrix = float4x4::identity();
        const float c = std::cos(a_radian);
        const float s = std::sin(a_radian);
        matrix.values[0][0] = c;
        matrix.values[0][1] = s;
        matrix.values[1][0] = -s;
        matrix.values[1][1] = c;
        return matrix;
    }

    [[nodiscard]] float4x4 xyz_rotate_matrix(float3 a_rotationRadians) noexcept
    {
        // - 各軸回転を合成する
        return x_axis_matrix(a_rotationRadians.x) *
            y_axis_matrix(a_rotationRadians.y) *
            z_axis_matrix(a_rotationRadians.z);
    }

    [[nodiscard]] float4x4 quaternion_matrix(
        Quaternion a_rotation) noexcept
    {
        const Quaternion rotation = Quaternion::normalize(a_rotation);
        const float xx = rotation.x * rotation.x;
        const float yy = rotation.y * rotation.y;
        const float zz = rotation.z * rotation.z;
        const float xy = rotation.x * rotation.y;
        const float xz = rotation.x * rotation.z;
        const float yz = rotation.y * rotation.z;
        const float wx = rotation.w * rotation.x;
        const float wy = rotation.w * rotation.y;
        const float wz = rotation.w * rotation.z;

        float4x4 matrix = float4x4::identity();
        matrix.values[0][0] = 1.0f - 2.0f * (yy + zz);
        matrix.values[0][1] = 2.0f * (xy + wz);
        matrix.values[0][2] = 2.0f * (xz - wy);
        matrix.values[1][0] = 2.0f * (xy - wz);
        matrix.values[1][1] = 1.0f - 2.0f * (xx + zz);
        matrix.values[1][2] = 2.0f * (yz + wx);
        matrix.values[2][0] = 2.0f * (xz + wy);
        matrix.values[2][1] = 2.0f * (yz - wx);
        matrix.values[2][2] = 1.0f - 2.0f * (xx + yy);
        return matrix;
    }

    [[nodiscard]] Quaternion quaternion_from_euler_xyz(
        float3 a_rotationRadians) noexcept
    {
        const float halfX = a_rotationRadians.x * 0.5f;
        const float halfY = a_rotationRadians.y * 0.5f;
        const float halfZ = a_rotationRadians.z * 0.5f;
        const float sx = std::sin(halfX);
        const float cx = std::cos(halfX);
        const float sy = std::sin(halfY);
        const float cy = std::cos(halfY);
        const float sz = std::sin(halfZ);
        const float cz = std::cos(halfZ);

        Quaternion rotation(
            sx * cy * cz + cx * sy * sz,
            cx * sy * cz - sx * cy * sz,
            cx * cy * sz + sx * sy * cz,
            cx * cy * cz - sx * sy * sz);
        return rotation.normalize();
    }

    [[nodiscard]] float3 quaternion_to_euler_xyz(
        Quaternion a_rotation) noexcept
    {
        const float4x4 matrix = quaternion_matrix(a_rotation);
        const float sinY = -matrix.values[0][2];
        const float clampedSinY =
            sinY < -1.0f ? -1.0f : (sinY > 1.0f ? 1.0f : sinY);
        const float y = std::asin(clampedSinY);
        const float cosY = std::cos(y);

        if (std::abs(cosY) > 0.000001f)
        {
            return float3(
                std::atan2(matrix.values[1][2], matrix.values[2][2]),
                y,
                std::atan2(matrix.values[0][1], matrix.values[0][0]));
        }

        return float3(
            0.0f,
            y,
            std::atan2(-matrix.values[1][0], matrix.values[1][1]));
    }

    [[nodiscard]] float4x4 translate_matrix(float3 a_translation) noexcept
    {
        // - 単位行列に平行移動成分を設定する
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
        // - 画面座標への変換行列を構築する
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
        // - 透視投影行列を構築する
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
        // - 正射影行列を構築する
        float4x4 matrix = float4x4::identity();
        matrix.values[0][0] = 2.0f / (a_right - a_left);
        matrix.values[1][1] = 2.0f / (a_top - a_bottom);
        matrix.values[2][2] = 1.0f / (a_farClip - a_nearClip);
        matrix.values[3][0] = (a_left + a_right) / (a_left - a_right);
        matrix.values[3][1] = (a_top + a_bottom) / (a_bottom - a_top);
        matrix.values[3][2] = a_nearClip / (a_nearClip - a_farClip);
        return matrix;
    }

    [[nodiscard]] float4x4 make_affine_matrix(
        float3 a_scale,
        float3 a_rotateRadians,
        float3 a_translate) noexcept
    {
        // - スケール、回転、平行移動を合成してアフィン変換行列を構築する
        return scale_matrix(a_scale) * xyz_rotate_matrix(a_rotateRadians) *
            translate_matrix(a_translate);
    }

    [[nodiscard]] float4x4 make_affine_matrix(
        float3 a_scale,
        Quaternion a_rotation,
        float3 a_translate) noexcept
    {
        return scale_matrix(a_scale) * quaternion_matrix(a_rotation) *
            translate_matrix(a_translate);
    }

    [[nodiscard]] Quaternion make_axis_angle_quaternion(const float3& a_axis, float a_angleRadians) noexcept
    {
        const float halfAngle = a_angleRadians * 0.5f;
        const float sinHalfAngle = std::sin(halfAngle);
        Quaternion rotation(a_axis.x * sinHalfAngle, a_axis.y * sinHalfAngle, a_axis.z * sinHalfAngle,
            std::cos(halfAngle));
        return rotation.normalize();
    }

    [[nodiscard]] float3 rotate_vector(const Quaternion& a_rotation, const float3& a_vector) noexcept
    {
        const Quaternion rotation = Quaternion::normalize(a_rotation);
        const Quaternion vector(a_vector.x, a_vector.y, a_vector.z, 0.0f);
        const Quaternion result = rotation * vector * Quaternion::inverse(rotation);
        return float3(result.x, result.y, result.z);
    }
}
