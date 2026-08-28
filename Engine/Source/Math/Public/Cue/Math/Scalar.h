#pragma once

#include <Cue/Foundation/Result.h>

namespace cue::math
{
/// @brief 絶対誤差と相対誤差を用途ごとに明示する検証済み比較条件
class Tolerance final
{
  public:
    /// @brief 非負かつ有限な絶対誤差と相対誤差からToleranceを生成する
    /// @param a_emergencyHandler Error生成失敗時の非所有終了境界
    /// @param a_absolute 許容する絶対誤差
    /// @param a_relative 比較値のScaleに対して許容する相対誤差
    [[nodiscard]] static cue::Result<Tolerance> create(cue::EmergencyHandler &a_emergencyHandler,
                                                       float a_absolute, float a_relative) noexcept;

    /// @brief 許容する絶対誤差を返す
    [[nodiscard]] float absolute() const noexcept;

    /// @brief 比較値のScaleに対して許容する相対誤差を返す
    [[nodiscard]] float relative() const noexcept;

  private:
    /// @brief 検証済みの絶対誤差と相対誤差を保持する
    Tolerance(float a_absolute, float a_relative) noexcept;

    float m_absolute;
    float m_relative;
};

/// @brief 単精度円周率を返す
[[nodiscard]] constexpr float pi() noexcept
{
    return 3.14159265358979323846F;
}

/// @brief 単精度値がNaNでもInfinityでもない場合にtrueを返す
[[nodiscard]] bool is_finite(float a_value) noexcept;

/// @brief 絶対誤差と相対誤差の両方を考慮して有限な単精度値を比較する
[[nodiscard]] bool is_near(float a_left, float a_right, const Tolerance &a_tolerance) noexcept;
} // namespace cue::math
