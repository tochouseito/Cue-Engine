#pragma once

/// *********************************************************************************
/// 4x4 行列
/// *********************************************************************************

// === Math includes ===
#include "MathStructAllowedList.h"

// === C++ includes ===
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Cue::Math
{
    /// @brief 4x4行列（row-major: m[row][col]）
    template <AllowedVector T>
    struct Matrix4 final
    {
        float values[4][4] = {};

        /// @brief 単位行列で初期化
        constexpr void initialize_identity() noexcept
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    values[row][col] = (row == col) ? T{ 1 } : T{ 0 };
                }
            }
        }

        /// @brief 単位行列生成
        [[nodiscard]] static constexpr Matrix4 identity() noexcept
        {
            Matrix4 result;
            result.initialize_identity();
            return result;
        }

        /// @brief ゼロ行列で初期化
        constexpr void initialize_zero() noexcept
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    values[row][col] = T{ 0 };
                }
            }
        }

        /// @brief ゼロ行列生成
        [[nodiscard]] static constexpr Matrix4 zero() noexcept
        {
            Matrix4 result;
            result.initialize_zero();
            return result;
        }

        /// @brief 行列の転置（破壊的）
        constexpr void transpose() noexcept
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = row + 1; col < 4; ++col)
                {
                    std::swap(values[row][col], values[col][row]);
                }
            }
        }

        /// @brief 転置（非破壊）
        [[nodiscard]] static constexpr Matrix4 transpose(const Matrix4& a_value) noexcept
        {
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.values[row][col] = a_value.values[col][row];
                }
            }
            return result;
        }

        /// @brief 行列積（result = this * a_other）
        [[nodiscard]] constexpr Matrix4 multiply(const Matrix4& a_other) const noexcept
        {
            Matrix4 result{};
            for (int row = 0; row < 4; ++row)
            {
                for (int k = 0; k < 4; ++k)
                {
                    const T aik = static_cast<T>(values[row][k]);
                    result.values[row][0] += aik * a_other.values[k][0];
                    result.values[row][1] += aik * a_other.values[k][1];
                    result.values[row][2] += aik * a_other.values[k][2];
                    result.values[row][3] += aik * a_other.values[k][3];
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Matrix4 operator*(const Matrix4& a_other) const noexcept
        {
            return multiply(a_other);
        }

        // --- スカラー演算 ---

        [[nodiscard]] constexpr Matrix4 operator+(const Matrix4& a_other) const noexcept
        {
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.values[row][col] = values[row][col] + a_other.values[row][col];
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Matrix4 operator-(const Matrix4& a_other) const noexcept
        {
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.values[row][col] = values[row][col] - a_other.values[row][col];
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Matrix4 operator*(T a_scalar) const noexcept
        {
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.values[row][col] = values[row][col] * a_scalar;
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Matrix4 operator-() const noexcept
        {
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.values[row][col] = -values[row][col];
                }
            }
            return result;
        }

        // --- 比較（浮動小数向けの相対＋絶対誤差） ---

        [[nodiscard]] constexpr bool nearly_equal(const Matrix4& a_other,
            T a_absEps = T(1e-6),
            T a_relEps = T(1e-5)) const noexcept
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    const T a = values[row][col];
                    const T b = a_other.values[row][col];
                    const T diff = (a > b) ? (a - b) : (b - a);
                    const T scale = std::max<T>(std::abs(a), std::abs(b));
                    if (diff > a_absEps + a_relEps * scale)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool operator==(const Matrix4& a_other) const noexcept
        {
            return nearly_equal(a_other,
                T(10) * std::numeric_limits<T>::epsilon(),
                T(100) * std::numeric_limits<T>::epsilon());
        }

        [[nodiscard]] constexpr bool operator!=(const Matrix4& a_other) const noexcept
        {
            return !(*this == a_other);
        }

        // --- 逆行列（浮動小数型にのみ提供） ---

        template <class U = T, std::enable_if_t<std::is_floating_point_v<U>, int> = 0>
        void inverse() noexcept
        {
            const int size = 4;
            U sweep[size][size * 2]{};

            for (int row = 0; row < size; ++row)
            {
                for (int col = 0; col < size; ++col)
                {
                    sweep[row][col] = static_cast<U>(values[row][col]);
                    sweep[row][col + size] = (row == col) ? U{ 1 } : U{ 0 };
                }
            }

            for (int k = 0; k < size; ++k)
            {
                U maxValue = std::abs(sweep[k][k]);
                int maxIndex = k;
                for (int row = k + 1; row < size; ++row)
                {
                    const U pivotValue = std::abs(sweep[row][k]);
                    if (pivotValue > maxValue)
                    {
                        maxValue = pivotValue;
                        maxIndex = row;
                    }
                }
                if (maxValue <= U(1e-12))
                {
                    initialize_identity();
                    return;
                }

                if (k != maxIndex)
                {
                    for (int col = 0; col < size * 2; ++col)
                    {
                        std::swap(sweep[k][col], sweep[maxIndex][col]);
                    }
                }

                const U pivot = sweep[k][k];
                for (int col = 0; col < size * 2; ++col)
                {
                    sweep[k][col] /= pivot;
                }

                for (int row = 0; row < size; ++row)
                {
                    if (row != k)
                    {
                        const U factor = sweep[row][k];
                        if (factor != U{ 0 })
                        {
                            for (int col = 0; col < size * 2; ++col)
                            {
                                sweep[row][col] -= sweep[k][col] * factor;
                            }
                        }
                    }
                }
            }

            for (int row = 0; row < size; ++row)
            {
                for (int col = 0; col < size; ++col)
                {
                    values[row][col] = static_cast<T>(sweep[row][col + size]);
                }
            }
        }

        template <class U = T, std::enable_if_t<std::is_floating_point_v<U>, int> = 0>
        [[nodiscard]] static Matrix4 inverse(const Matrix4& a_value) noexcept
        {
            Matrix4 result = a_value;
            result.inverse();
            return result;
        }

        // --- 配列との相互変換（row-major: a_out[row*4+col]） ---

        constexpr void to_array16(T a_out[16]) const noexcept
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    a_out[row * 4 + col] = values[row][col];
                }
            }
        }

        [[nodiscard]] static constexpr Matrix4 from_array16(const T a_in[16]) noexcept
        {
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.values[row][col] = a_in[row * 4 + col];
                }
            }
            return result;
        }

        // --- 検算：a_inv が a_mat の逆行列かを評価 ---
        template <class U = T, std::enable_if_t<std::is_floating_point_v<U>, int> = 0>
        [[nodiscard]] static bool check_inverse(const Matrix4& a_mat,
            const Matrix4& a_inv,
            U a_tol = U(1e-9)) noexcept
        {
            U maxAbsErr = U{ 0 };
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    U sum = U{ 0 };
                    for (int k = 0; k < 4; ++k)
                    {
                        sum += static_cast<U>(a_mat.values[row][k]) *
                            static_cast<U>(a_inv.values[k][col]);
                    }

                    const U ideal = (row == col) ? U{ 1 } : U{ 0 };
                    const U err = std::abs(ideal - sum);
                    if (err > maxAbsErr)
                    {
                        maxAbsErr = err;
                    }
                }
            }
            return maxAbsErr <= a_tol;
        }
    };

    using float4x4 = Matrix4<float>;
    using double4x4 = Matrix4<double>;
} // Cue::Math 名前空間
