#pragma once

#include <Cue/Math/Vector.h>

namespace cue::math
{
/// @brief 行Vector変換に使用するRow-major 3x3 Matrix値型
struct Matrix3 final
{
    /// Row-major順のMatrix要素
    float values[3][3] = {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
};

/// @brief 行Vector変換に使用するRow-major 4x4 Matrix値型
struct Matrix4 final
{
    /// Row-major順のMatrix要素
    float values[4][4] = {
        {1.0F, 0.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 0.0F, 1.0F},
    };
};

/// @brief 全要素がZeroのMatrix3を返す
[[nodiscard]] Matrix3 zero_matrix3() noexcept;
/// @brief 全要素がZeroのMatrix4を返す
[[nodiscard]] Matrix4 zero_matrix4() noexcept;
/// @brief Matrix3を組込みfloatの完全一致規則で比較する
[[nodiscard]] bool operator==(const Matrix3 &a_left, const Matrix3 &a_right) noexcept;
/// @brief Matrix3が完全一致しない場合にtrueを返す
[[nodiscard]] bool operator!=(const Matrix3 &a_left, const Matrix3 &a_right) noexcept;
/// @brief Matrix4を組込みfloatの完全一致規則で比較する
[[nodiscard]] bool operator==(const Matrix4 &a_left, const Matrix4 &a_right) noexcept;
/// @brief Matrix4が完全一致しない場合にtrueを返す
[[nodiscard]] bool operator!=(const Matrix4 &a_left, const Matrix4 &a_right) noexcept;
/// @brief Matrix3を時間順に合成する
[[nodiscard]] Matrix3 operator*(const Matrix3 &a_left, const Matrix3 &a_right) noexcept;
/// @brief Matrix4を時間順に合成する
[[nodiscard]] Matrix4 operator*(const Matrix4 &a_left, const Matrix4 &a_right) noexcept;
/// @brief 行VectorをMatrix3で変換する
[[nodiscard]] Vector3 operator*(Vector3 a_value, const Matrix3 &a_matrix) noexcept;
/// @brief Matrix3の全要素を指定Tolerance内で比較する
[[nodiscard]] bool is_near(const Matrix3 &a_left, const Matrix3 &a_right,
                           const Tolerance &a_tolerance) noexcept;
/// @brief Matrix4の全要素を指定Tolerance内で比較する
[[nodiscard]] bool is_near(const Matrix4 &a_left, const Matrix4 &a_right,
                           const Tolerance &a_tolerance) noexcept;
/// @brief Matrix3の全要素が有限な場合にtrueを返す
[[nodiscard]] bool is_finite(const Matrix3 &a_value) noexcept;
/// @brief Matrix4の全要素が有限な場合にtrueを返す
[[nodiscard]] bool is_finite(const Matrix4 &a_value) noexcept;
/// @brief Matrix4でPointを変換し、平行移動を適用する
[[nodiscard]] Vector3 transform_point(Vector3 a_value, const Matrix4 &a_matrix) noexcept;
/// @brief Matrix4でDirectionを変換し、平行移動を適用しない
[[nodiscard]] Vector3 transform_direction(Vector3 a_value, const Matrix4 &a_matrix) noexcept;
/// @brief 有限かつ非特異なMatrix3の逆Matrixを返す
[[nodiscard]] cue::Result<Matrix3> inverse(cue::EmergencyHandler &a_emergencyHandler,
                                            const Matrix3 &a_value,
                                            const Tolerance &a_tolerance) noexcept;
/// @brief 有限かつ非特異なMatrix4の逆Matrixを返す
[[nodiscard]] cue::Result<Matrix4> inverse(cue::EmergencyHandler &a_emergencyHandler,
                                            const Matrix4 &a_value,
                                            const Tolerance &a_tolerance) noexcept;
} // namespace cue::math
