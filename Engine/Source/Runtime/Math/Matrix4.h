#pragma once
#include "MathStructAllowedList.h"

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
        float m_values[4][4] = {};

        /// @brief 単位行列で初期化
        constexpr void initialize_identity() noexcept
        {
            // 1) 対角のみ 1、それ以外は 0 にする
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    m_values[row][col] = (row == col) ? T{ 1 } : T{ 0 };
                }
            }
        }

        /// @brief 単位行列生成
        [[nodiscard]] static constexpr Matrix4 identity() noexcept
        {
            // 1) 単位行列で初期化した値を返す
            Matrix4 result;
            result.initialize_identity();
            return result;
        }

        /// @brief ゼロ行列で初期化
        constexpr void initialize_zero() noexcept
        {
            // 1) 全要素を 0 にする
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    m_values[row][col] = T{ 0 };
                }
            }
        }

        /// @brief ゼロ行列生成
        [[nodiscard]] static constexpr Matrix4 zero() noexcept
        {
            // 1) ゼロ行列で初期化した値を返す
            Matrix4 result;
            result.initialize_zero();
            return result;
        }

        /// @brief 行列の転置（破壊的）
        constexpr void transpose() noexcept
        {
            // 1) 対角を跨ぐ要素を入れ替える
            for (int row = 0; row < 4; ++row)
            {
                for (int col = row + 1; col < 4; ++col)
                {
                    std::swap(m_values[row][col], m_values[col][row]);
                }
            }
        }

        /// @brief 転置（非破壊）
        [[nodiscard]] static constexpr Matrix4 transpose(const Matrix4& value) noexcept
        {
            // 1) 転置した結果を新しい行列として作る
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.m_values[row][col] = value.m_values[col][row];
                }
            }
            return result;
        }

        /// @brief 行列積（result = this * other）
        [[nodiscard]] constexpr Matrix4 multiply(const Matrix4& other) const noexcept
        {
            // 1) 行列積の行ごとの計算を行う
            Matrix4 result{};
            for (int row = 0; row < 4; ++row)
            {
                for (int k = 0; k < 4; ++k)
                {
                    const T aik = static_cast<T>(m_values[row][k]);
                    // 2) 行の要素を列へ加算する
                    result.m_values[row][0] += aik * other.m_values[k][0];
                    result.m_values[row][1] += aik * other.m_values[k][1];
                    result.m_values[row][2] += aik * other.m_values[k][2];
                    result.m_values[row][3] += aik * other.m_values[k][3];
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Matrix4 operator*(const Matrix4& other) const noexcept
        {
            // 1) 行列積の実装を再利用する
            return multiply(other);
        }

        // --- スカラー演算 ---

        [[nodiscard]] constexpr Matrix4 operator+(const Matrix4& other) const noexcept
        {
            // 1) 成分ごとの加算結果を返す
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.m_values[row][col] = m_values[row][col] + other.m_values[row][col];
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Matrix4 operator-(const Matrix4& other) const noexcept
        {
            // 1) 成分ごとの減算結果を返す
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.m_values[row][col] = m_values[row][col] - other.m_values[row][col];
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Matrix4 operator*(T scalar) const noexcept
        {
            // 1) 成分ごとにスカラーを掛ける
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.m_values[row][col] = m_values[row][col] * scalar;
                }
            }
            return result;
        }

        [[nodiscard]] constexpr Matrix4 operator-() const noexcept
        {
            // 1) 全成分の符号を反転する
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.m_values[row][col] = -m_values[row][col];
                }
            }
            return result;
        }

        // --- 比較（浮動小数向けの相対＋絶対誤差） ---

        [[nodiscard]] constexpr bool nearly_equal(const Matrix4& other,
            T absEps = T(1e-6),
            T relEps = T(1e-5)) const noexcept
        {
            // 1) 各成分の誤差が許容範囲か判定する
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    const T a = m_values[row][col];
                    const T b = other.m_values[row][col];
                    const T diff = (a > b) ? (a - b) : (b - a);
                    const T scale = std::max<T>(std::abs(a), std::abs(b));
                    if (diff > absEps + relEps * scale)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] constexpr bool operator==(const Matrix4& other) const noexcept
        {
            // 1) 型に応じた既定epsilonで近似比較する
            return nearly_equal(other,
                T(10) * std::numeric_limits<T>::epsilon(),
                T(100) * std::numeric_limits<T>::epsilon());
        }

        [[nodiscard]] constexpr bool operator!=(const Matrix4& other) const noexcept
        {
            // 1) 等価判定の否定を返す
            return !(*this == other);
        }

        // --- 逆行列（浮動小数型にのみ提供） ---

        template <class U = T, std::enable_if_t<std::is_floating_point_v<U>, int> = 0>
        void inverse() noexcept
        {
            // 1) 掃き出し法で逆行列を求める
            const int size = 4;
            U sweep[size][size * 2]{};

            for (int row = 0; row < size; ++row)
            {
                for (int col = 0; col < size; ++col)
                {
                    sweep[row][col] = static_cast<U>(m_values[row][col]);
                    sweep[row][col + size] = (row == col) ? U{ 1 } : U{ 0 };
                }
            }

            for (int k = 0; k < size; ++k)
            {
                // 2) ピボット選択（部分ピボット）
                U maxValue = std::abs(sweep[k][k]);
                int maxIndex = k;
                for (int row = k + 1; row < size; ++row)
                {
                    const U value = std::abs(sweep[row][k]);
                    if (value > maxValue)
                    {
                        maxValue = value;
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
                    m_values[row][col] = static_cast<T>(sweep[row][col + size]);
                }
            }
        }

        template <class U = T, std::enable_if_t<std::is_floating_point_v<U>, int> = 0>
        [[nodiscard]] static Matrix4 inverse(const Matrix4& value) noexcept
        {
            // 1) コピーして破壊的な逆行列を避ける
            Matrix4 result = value;
            result.inverse();
            return result;
        }

        // --- 配列との相互変換（row-major: out[row*4+col]） ---

        constexpr void to_array16(T out[16]) const noexcept
        {
            // 1) row-major で配列へ書き出す
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    out[row * 4 + col] = m_values[row][col];
                }
            }
        }

        [[nodiscard]] static constexpr Matrix4 from_array16(const T in[16]) noexcept
        {
            // 1) row-major の配列から行列を構成する
            Matrix4 result;
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    result.m_values[row][col] = in[row * 4 + col];
                }
            }
            return result;
        }

        // --- 検算：inv が mat の逆行列か（?mat*inv - I?∞ を評価） ---
        template <class U = T, std::enable_if_t<std::is_floating_point_v<U>, int> = 0>
        [[nodiscard]] static bool check_inverse(const Matrix4& mat,
            const Matrix4& inv,
            U tol = U(1e-9)) noexcept
        {
            // 1) mat * inv が単位行列に近いかを評価する
            U maxAbsErr = U{ 0 };
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    U sum = U{ 0 };
                    for (int k = 0; k < 4; ++k)
                    {
                        sum += static_cast<U>(mat.m_values[row][k]) *
                            static_cast<U>(inv.m_values[k][col]);
                    }

                    const U ideal = (row == col) ? U{ 1 } : U{ 0 };
                    const U err = std::abs(ideal - sum);
                    if (err > maxAbsErr)
                    {
                        maxAbsErr = err;
                    }
                }
            }
            return maxAbsErr <= tol;
        }
    };

    using float4x4 = Matrix4<float>;
    using double4x4 = Matrix4<double>;
} // Cue::Math 名前空間
