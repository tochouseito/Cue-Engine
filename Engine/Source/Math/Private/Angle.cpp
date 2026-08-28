#include <Cue/Math/Angle.h>

namespace cue::math
{
/// @brief RadianをDegreeへ明示的に変換する
Degrees to_degrees(Radians a_angle) noexcept
{
    return Degrees(a_angle.value * (180.0F / pi()));
}

/// @brief DegreeをRadianへ明示的に変換する
Radians to_radians(Degrees a_angle) noexcept
{
    return Radians(a_angle.value * (pi() / 180.0F));
}

/// @brief Radian値を組込みfloatの完全一致規則で比較する
bool operator==(Radians a_left, Radians a_right) noexcept
{
    return a_left.value == a_right.value;
}

/// @brief Radian値が完全一致しない場合にtrueを返す
bool operator!=(Radians a_left, Radians a_right) noexcept
{
    return !(a_left == a_right);
}

/// @brief Degree値を組込みfloatの完全一致規則で比較する
bool operator==(Degrees a_left, Degrees a_right) noexcept
{
    return a_left.value == a_right.value;
}

/// @brief Degree値が完全一致しない場合にtrueを返す
bool operator!=(Degrees a_left, Degrees a_right) noexcept
{
    return !(a_left == a_right);
}

/// @brief Radian値を指定Tolerance内で比較する
bool is_near(Radians a_left, Radians a_right, const Tolerance &a_tolerance) noexcept
{
    return is_near(a_left.value, a_right.value, a_tolerance);
}

/// @brief Degree値を指定Tolerance内で比較する
bool is_near(Degrees a_left, Degrees a_right, const Tolerance &a_tolerance) noexcept
{
    return is_near(a_left.value, a_right.value, a_tolerance);
}
} // namespace cue::math
