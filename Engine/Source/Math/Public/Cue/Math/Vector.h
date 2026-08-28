#pragma once

#include <Cue/Math/Scalar.h>

namespace cue::math
{
/// @brief 二次元のScalar成分を連続配置で保持する値型
struct Vector2 final
{
    /// X成分
    float x = 0.0F;
    /// Y成分
    float y = 0.0F;
};

/// @brief 三次元のScalar成分を連続配置で保持する値型
struct Vector3 final
{
    /// X成分
    float x = 0.0F;
    /// Y成分
    float y = 0.0F;
    /// Z成分
    float z = 0.0F;
};

/// @brief 四次元のScalar成分を連続配置で保持する値型
struct Vector4 final
{
    /// X成分
    float x = 0.0F;
    /// Y成分
    float y = 0.0F;
    /// Z成分
    float z = 0.0F;
    /// W成分
    float w = 0.0F;
};

/// @brief Vector2の対応成分を加算する
[[nodiscard]] Vector2 operator+(Vector2 a_left, Vector2 a_right) noexcept;
/// @brief Vector2の対応成分を減算する
[[nodiscard]] Vector2 operator-(Vector2 a_left, Vector2 a_right) noexcept;
/// @brief Vector2の全成分の符号を反転する
[[nodiscard]] Vector2 operator-(Vector2 a_value) noexcept;
/// @brief Vector2の全成分へScalarを乗算する
[[nodiscard]] Vector2 operator*(Vector2 a_value, float a_scalar) noexcept;
/// @brief ScalarをVector2の全成分へ乗算する
[[nodiscard]] Vector2 operator*(float a_scalar, Vector2 a_value) noexcept;
/// @brief Vector2の全成分をScalarで除算する
[[nodiscard]] Vector2 operator/(Vector2 a_value, float a_scalar) noexcept;
/// @brief Vector2を組込みfloatの完全一致規則で比較する
[[nodiscard]] bool operator==(Vector2 a_left, Vector2 a_right) noexcept;
/// @brief Vector2が完全一致しない場合にtrueを返す
[[nodiscard]] bool operator!=(Vector2 a_left, Vector2 a_right) noexcept;

/// @brief Vector3の対応成分を加算する
[[nodiscard]] Vector3 operator+(Vector3 a_left, Vector3 a_right) noexcept;
/// @brief Vector3の対応成分を減算する
[[nodiscard]] Vector3 operator-(Vector3 a_left, Vector3 a_right) noexcept;
/// @brief Vector3の全成分の符号を反転する
[[nodiscard]] Vector3 operator-(Vector3 a_value) noexcept;
/// @brief Vector3の全成分へScalarを乗算する
[[nodiscard]] Vector3 operator*(Vector3 a_value, float a_scalar) noexcept;
/// @brief ScalarをVector3の全成分へ乗算する
[[nodiscard]] Vector3 operator*(float a_scalar, Vector3 a_value) noexcept;
/// @brief Vector3の全成分をScalarで除算する
[[nodiscard]] Vector3 operator/(Vector3 a_value, float a_scalar) noexcept;
/// @brief Vector3を組込みfloatの完全一致規則で比較する
[[nodiscard]] bool operator==(Vector3 a_left, Vector3 a_right) noexcept;
/// @brief Vector3が完全一致しない場合にtrueを返す
[[nodiscard]] bool operator!=(Vector3 a_left, Vector3 a_right) noexcept;

/// @brief Vector4の対応成分を加算する
[[nodiscard]] Vector4 operator+(Vector4 a_left, Vector4 a_right) noexcept;
/// @brief Vector4の対応成分を減算する
[[nodiscard]] Vector4 operator-(Vector4 a_left, Vector4 a_right) noexcept;
/// @brief Vector4の全成分の符号を反転する
[[nodiscard]] Vector4 operator-(Vector4 a_value) noexcept;
/// @brief Vector4の全成分へScalarを乗算する
[[nodiscard]] Vector4 operator*(Vector4 a_value, float a_scalar) noexcept;
/// @brief ScalarをVector4の全成分へ乗算する
[[nodiscard]] Vector4 operator*(float a_scalar, Vector4 a_value) noexcept;
/// @brief Vector4の全成分をScalarで除算する
[[nodiscard]] Vector4 operator/(Vector4 a_value, float a_scalar) noexcept;
/// @brief Vector4を組込みfloatの完全一致規則で比較する
[[nodiscard]] bool operator==(Vector4 a_left, Vector4 a_right) noexcept;
/// @brief Vector4が完全一致しない場合にtrueを返す
[[nodiscard]] bool operator!=(Vector4 a_left, Vector4 a_right) noexcept;

/// @brief Vector2の内積を返す
[[nodiscard]] float dot(Vector2 a_left, Vector2 a_right) noexcept;
/// @brief Vector3の内積を返す
[[nodiscard]] float dot(Vector3 a_left, Vector3 a_right) noexcept;
/// @brief Vector4の内積を返す
[[nodiscard]] float dot(Vector4 a_left, Vector4 a_right) noexcept;
/// @brief 右手則の代数定義に従うVector3の外積を返す
[[nodiscard]] Vector3 cross(Vector3 a_left, Vector3 a_right) noexcept;

/// @brief Vector2の長さをOverflowとUnderflowを避けて計算する
[[nodiscard]] float length(Vector2 a_value) noexcept;
/// @brief Vector3の長さをOverflowとUnderflowを避けて計算する
[[nodiscard]] float length(Vector3 a_value) noexcept;
/// @brief Vector4の長さをOverflowとUnderflowを避けて計算する
[[nodiscard]] float length(Vector4 a_value) noexcept;

/// @brief 有限なVector2の全成分を指定Tolerance内で比較する
[[nodiscard]] bool is_near(Vector2 a_left, Vector2 a_right,
                           const Tolerance &a_tolerance) noexcept;
/// @brief 有限なVector3の全成分を指定Tolerance内で比較する
[[nodiscard]] bool is_near(Vector3 a_left, Vector3 a_right,
                           const Tolerance &a_tolerance) noexcept;
/// @brief 有限なVector4の全成分を指定Tolerance内で比較する
[[nodiscard]] bool is_near(Vector4 a_left, Vector4 a_right,
                           const Tolerance &a_tolerance) noexcept;

/// @brief Vector2の全成分が有限な場合にtrueを返す
[[nodiscard]] bool is_finite(Vector2 a_value) noexcept;
/// @brief Vector3の全成分が有限な場合にtrueを返す
[[nodiscard]] bool is_finite(Vector3 a_value) noexcept;
/// @brief Vector4の全成分が有限な場合にtrueを返す
[[nodiscard]] bool is_finite(Vector4 a_value) noexcept;

/// @brief 有限で十分な長さを持つVector2を正規化する
[[nodiscard]] cue::Result<Vector2> normalize(cue::EmergencyHandler &a_emergencyHandler,
                                              Vector2 a_value,
                                              const Tolerance &a_tolerance) noexcept;
/// @brief 有限で十分な長さを持つVector3を正規化する
[[nodiscard]] cue::Result<Vector3> normalize(cue::EmergencyHandler &a_emergencyHandler,
                                              Vector3 a_value,
                                              const Tolerance &a_tolerance) noexcept;
/// @brief 有限で十分な長さを持つVector4を正規化する
[[nodiscard]] cue::Result<Vector4> normalize(cue::EmergencyHandler &a_emergencyHandler,
                                              Vector4 a_value,
                                              const Tolerance &a_tolerance) noexcept;
} // namespace cue::math
