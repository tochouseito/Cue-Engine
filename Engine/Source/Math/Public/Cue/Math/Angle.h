#pragma once

#include <Cue/Math/Scalar.h>

namespace cue::math
{
/// @brief Radian単位を無次元Scalarから区別するAngle値
struct Radians final
{
    /// @brief Zero Radianを生成する
    constexpr Radians() noexcept = default;

    /// @brief 指定したRadian値を保持する
    explicit constexpr Radians(float a_value) noexcept : value(a_value)
    {
    }

    /// Radian単位の値
    float value = 0.0F;
};

/// @brief Degree単位を無次元Scalarから区別するAngle値
struct Degrees final
{
    /// @brief Zero Degreeを生成する
    constexpr Degrees() noexcept = default;

    /// @brief 指定したDegree値を保持する
    explicit constexpr Degrees(float a_value) noexcept : value(a_value)
    {
    }

    /// Degree単位の値
    float value = 0.0F;
};

/// @brief RadianをDegreeへ明示的に変換する
[[nodiscard]] Degrees to_degrees(Radians a_angle) noexcept;

/// @brief DegreeをRadianへ明示的に変換する
[[nodiscard]] Radians to_radians(Degrees a_angle) noexcept;

/// @brief Radian値を組込みfloatの完全一致規則で比較する
[[nodiscard]] bool operator==(Radians a_left, Radians a_right) noexcept;

/// @brief Radian値が完全一致しない場合にtrueを返す
[[nodiscard]] bool operator!=(Radians a_left, Radians a_right) noexcept;

/// @brief Degree値を組込みfloatの完全一致規則で比較する
[[nodiscard]] bool operator==(Degrees a_left, Degrees a_right) noexcept;

/// @brief Degree値が完全一致しない場合にtrueを返す
[[nodiscard]] bool operator!=(Degrees a_left, Degrees a_right) noexcept;

/// @brief Radian値を指定Tolerance内で比較する
[[nodiscard]] bool is_near(Radians a_left, Radians a_right,
                           const Tolerance &a_tolerance) noexcept;

/// @brief Degree値を指定Tolerance内で比較する
[[nodiscard]] bool is_near(Degrees a_left, Degrees a_right,
                           const Tolerance &a_tolerance) noexcept;
} // namespace cue::math
