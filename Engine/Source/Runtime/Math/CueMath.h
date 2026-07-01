#pragma once

/// *********************************************************************************
/// 数学機能集約ヘッダ
/// *********************************************************************************

// 行ベクトル row-vector 前提
// 行列は行優先 row-major 前提

#define NOMINMAX // Windows.h の min/max マクロを無効化

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Math includes ===
#include "TimeUnit.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4.h"
#include "Quaternion.h"

// === C++ includes ===
#include <cstdint>
#include <limits>
#include <numbers>

namespace Cue::Math
{
    /// @brief 値を指定された倍数に切り上げ
    /// @param a_value 切り上げる値
    /// @param a_step 切り上げ先の倍数。0 の場合は無効
    /// @return 指定された倍数に切り上げられた値
    [[nodiscard]] uint32_t round_up_to_multiple(uint32_t a_value, uint32_t a_step) noexcept;

    /// @brief 値を指定された倍数に切り上げ
    /// @param a_value 切り上げる値
    /// @param a_step 切り上げ先の倍数。0 の場合は無効
    /// @return 指定された倍数に切り上げられた値
    [[nodiscard]] uint64_t round_up_to_multiple(uint64_t a_value, uint64_t a_step) noexcept;

    /// @brief スケール行列を構築する
    [[nodiscard]] float4x4 scale_matrix(float3 a_scale) noexcept;

    /// @brief X 軸回転行列を構築する
    [[nodiscard]] float4x4 x_axis_matrix(float a_radian) noexcept;

    /// @brief Y 軸回転行列を構築する
    [[nodiscard]] float4x4 y_axis_matrix(float a_radian) noexcept;

    /// @brief Z 軸回転行列を構築する
    [[nodiscard]] float4x4 z_axis_matrix(float a_radian) noexcept;

    /// @brief XYZ 順の回転行列を構築する
    /// @param a_rotationRadians 弧度法の Euler 回転
    [[nodiscard]] float4x4 xyz_rotate_matrix(float3 a_rotationRadians) noexcept;

    /// @brief クォータニオンから回転行列を構築する
    [[nodiscard]] float4x4 quaternion_matrix(
        Quaternion a_rotation) noexcept;

    /// @brief XYZ 順の Euler 回転からクォータニオンを構築する
    /// @param a_rotationRadians 弧度法の Euler 回転
    [[nodiscard]] Quaternion quaternion_from_euler_xyz(
        float3 a_rotationRadians) noexcept;

    /// @brief クォータニオンから XYZ 順の Euler 回転を取得する
    /// @return 弧度法の Euler 回転
    [[nodiscard]] float3 quaternion_to_euler_xyz(
        Quaternion a_rotation) noexcept;

    /// @brief 平行移動行列を構築する
    [[nodiscard]] float4x4 translate_matrix(float3 a_translation) noexcept;

    /// @brief ビューポート変換行列を構築する
    [[nodiscard]] float4x4 viewport_matrix(
        float a_left,
        float a_top,
        float a_width,
        float a_height,
        float a_minDepth,
        float a_maxDepth) noexcept;

    /// @brief 透視投影行列を構築する
    [[nodiscard]] float4x4 perspective_fov_matrix(
        float a_fovY,
        float a_aspectRatio,
        float a_nearClip,
        float a_farClip) noexcept;

    /// @brief 正射影行列を構築する
    [[nodiscard]] float4x4 orthographic_matrix(
        float a_left,
        float a_top,
        float a_right,
        float a_bottom,
        float a_nearClip,
        float a_farClip) noexcept;

    /// @brief スケール、回転、平行移動を合成したアフィン行列を構築する
    /// @param a_rotateRadians 弧度法の Euler 回転
    [[nodiscard]] float4x4 make_affine_matrix(
        float3 a_scale,
        float3 a_rotateRadians,
        float3 a_translate) noexcept;

    /// @brief スケール、クォータニオン回転、平行移動を合成したアフィン行列を構築する
    [[nodiscard]] float4x4 make_affine_matrix(
        float3 a_scale,
        Quaternion a_rotation,
        float3 a_translate) noexcept;

    /// @brief 軸と角度からクォータニオンを構築する
    /// @param a_axis 回転軸
    /// @param a_angleRadians 回転角（ラジアン）
    /// @return クォータニオン
    [[nodiscard]] Quaternion make_axis_angle_quaternion(
        const float3& a_axis, float a_angleRadians) noexcept;

    /// @brief クォータニオンでベクトルを回転する
    /// @param a_rotation 回転を表すクォータニオン
    /// @param a_vector 回転させるベクトル
    /// @return 回転後のベクトル
    [[nodiscard]] float3 rotate_vector(
        const Quaternion& a_rotation, const float3& a_vector) noexcept;
}
