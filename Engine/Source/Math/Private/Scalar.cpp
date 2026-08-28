#include <Cue/Math/Scalar.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace cue::math
{
/// @brief 検証済みの絶対誤差と相対誤差を保持する
Tolerance::Tolerance(float a_absolute, float a_relative) noexcept
    : m_absolute(a_absolute), m_relative(a_relative)
{
}

/// @brief 非負かつ有限な絶対誤差と相対誤差からToleranceを生成する
cue::Result<Tolerance> Tolerance::create(cue::EmergencyHandler &a_emergencyHandler,
                                         float a_absolute, float a_relative) noexcept
{
    if (!is_finite(a_absolute) || !is_finite(a_relative))
    {
        auto errorCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Math", 1);
        auto error = cue::Error::create(a_emergencyHandler, std::move(errorCode),
                                        "Tolerance values must be finite");
        return cue::Result<Tolerance>::failure(std::move(error));
    }

    if (a_absolute < 0.0F || a_relative < 0.0F)
    {
        auto errorCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Math", 2);
        auto error = cue::Error::create(a_emergencyHandler, std::move(errorCode),
                                        "Tolerance values must be non-negative");
        return cue::Result<Tolerance>::failure(std::move(error));
    }

    auto tolerance = Tolerance(a_absolute, a_relative);
    return cue::Result<Tolerance>::success(std::move(tolerance));
}

/// @brief 許容する絶対誤差を返す
float Tolerance::absolute() const noexcept
{
    return m_absolute;
}

/// @brief 比較値のScaleに対して許容する相対誤差を返す
float Tolerance::relative() const noexcept
{
    return m_relative;
}

/// @brief 単精度値がNaNでもInfinityでもない場合にtrueを返す
bool is_finite(float a_value) noexcept
{
    return std::isfinite(a_value);
}

/// @brief 絶対誤差と相対誤差の両方を考慮して有限な単精度値を比較する
bool is_near(float a_left, float a_right, const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_left) || !is_finite(a_right))
    {
        return false;
    }

    const auto left = static_cast<double>(a_left);
    const auto right = static_cast<double>(a_right);
    const auto difference = std::abs(left - right);
    const auto scale = std::max(std::abs(left), std::abs(right));
    const auto allowedDifference =
        std::max(static_cast<double>(a_tolerance.absolute()),
                 static_cast<double>(a_tolerance.relative()) * scale);
    return difference <= allowedDifference;
}
} // namespace cue::math
