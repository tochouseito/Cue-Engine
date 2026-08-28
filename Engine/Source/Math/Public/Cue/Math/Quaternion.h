#pragma once

#include <Cue/Math/Angle.h>
#include <Cue/Math/Matrix.h>

namespace cue::math
{
/// @brief x、y、zのVector部とwのScalar部を保持するQuaternion値型
struct Quaternion final
{
    /// Vector部のX成分
    float x = 0.0F;
    /// Vector部のY成分
    float y = 0.0F;
    /// Vector部のZ成分
    float z = 0.0F;
    /// Scalar部
    float w = 1.0F;
};

/// @brief Quaternionを組込みfloatの完全一致規則で比較する
[[nodiscard]] bool operator==(Quaternion a_left, Quaternion a_right) noexcept;
/// @brief Quaternionが完全一致しない場合にtrueを返す
[[nodiscard]] bool operator!=(Quaternion a_left, Quaternion a_right) noexcept;
/// @brief Quaternionの全Componentが有限な場合にtrueを返す
[[nodiscard]] bool is_finite(Quaternion a_value) noexcept;
/// @brief Quaternionの長さをOverflowとUnderflowを避けて計算する
[[nodiscard]] float length(Quaternion a_value) noexcept;
/// @brief Quaternionの全Componentを指定Tolerance内で比較する
[[nodiscard]] bool is_near(Quaternion a_left, Quaternion a_right,
                           const Tolerance &a_tolerance) noexcept;
/// @brief 符号が反転したQuaternionも同じ回転として指定Tolerance内で比較する
[[nodiscard]] bool is_same_rotation(Quaternion a_left, Quaternion a_right,
                                    const Tolerance &a_tolerance) noexcept;
/// @brief Quaternionが有限かつ指定Tolerance内で単位長の場合にtrueを返す
[[nodiscard]] bool is_unit_rotation(Quaternion a_value,
                                    const Tolerance &a_tolerance) noexcept;
/// @brief 有限で十分な長さを持つQuaternionを正規化する
[[nodiscard]] cue::Result<Quaternion> normalize(cue::EmergencyHandler &a_emergencyHandler,
                                                 Quaternion a_value,
                                                 const Tolerance &a_tolerance) noexcept;
/// @brief 有限で十分な長さを持つQuaternionの逆Quaternionを返す
[[nodiscard]] cue::Result<Quaternion> inverse(cue::EmergencyHandler &a_emergencyHandler,
                                               Quaternion a_value,
                                               const Tolerance &a_tolerance) noexcept;
/// @brief 有限で十分な長さを持つAxisとRadian角から単位Quaternionを生成する
[[nodiscard]] cue::Result<Quaternion> from_axis_angle(
    cue::EmergencyHandler &a_emergencyHandler, Vector3 a_axis, Radians a_angle,
    const Tolerance &a_tolerance) noexcept;
/// @brief 単位Quaternionを行Vector規約の回転Matrix3へ変換する
[[nodiscard]] cue::Result<Matrix3> to_matrix3(cue::EmergencyHandler &a_emergencyHandler,
                                               Quaternion a_rotation,
                                               const Tolerance &a_tolerance) noexcept;
/// @brief 単位Quaternionを行Vector規約の回転Matrix4へ変換する
[[nodiscard]] cue::Result<Matrix4> to_matrix4(cue::EmergencyHandler &a_emergencyHandler,
                                               Quaternion a_rotation,
                                               const Tolerance &a_tolerance) noexcept;
/// @brief 単位QuaternionでVector3を回転する
[[nodiscard]] cue::Result<Vector3> rotate(cue::EmergencyHandler &a_emergencyHandler,
                                           Vector3 a_value, Quaternion a_rotation,
                                           const Tolerance &a_tolerance) noexcept;
/// @brief first、secondの時間順に単位Quaternion回転を合成する
[[nodiscard]] cue::Result<Quaternion> compose_rotation(
    cue::EmergencyHandler &a_emergencyHandler, Quaternion a_first, Quaternion a_second,
    const Tolerance &a_tolerance) noexcept;
} // namespace cue::math
