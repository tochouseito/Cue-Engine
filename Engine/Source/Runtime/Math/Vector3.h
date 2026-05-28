#pragma once

/// *********************************************************************************
/// 3次元ベクトル
/// *********************************************************************************

// === Math includes ===
#include "MathStructAllowedList.h"

// === C++ includes ===
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace Cue::Math
{
    /// @brief 3次元ベクトル
    template <AllowedVector T>
    struct Vector3 final
    {
        /// @brief 別名アクセス
        union
        {
            struct
            {
                T x;
                T y;
                T z;
            };
            struct
            {
                T r;
                T g;
                T b;
            };
            T v[3];
        };

        // --- コンストラクタ ---
        /// @brief デフォルトコンストラクタ
        constexpr Vector3() noexcept
            : x(static_cast<T>(0))
            , y(static_cast<T>(0))
            , z(static_cast<T>(0))
        {}

        /// @brief 引数付きコンストラクタ
        constexpr Vector3(T a_x, T a_y, T a_z) noexcept
            : x(a_x)
            , y(a_y)
            , z(a_z)
        {}

        // --- 判定/初期化 ---
        /// @brief ゼロベクトルか判定
        constexpr bool is_zero() const noexcept
        {
            return x == static_cast<T>(0) &&
                y == static_cast<T>(0) &&
                z == static_cast<T>(0);
        }

        /// @brief ゼロ初期化
        constexpr void initialize() noexcept
        {
            x = static_cast<T>(0);
            y = static_cast<T>(0);
            z = static_cast<T>(0);
        }

        // --- 変換演算子 ---
        /// @brief bool型への変換（ゼロでなければtrue）
        explicit constexpr operator bool() const noexcept
        {
            return !is_zero();
        }

        // --- 配列アクセス ---
        /// @brief 配列アクセス（読み書き）0:x(=r),1:y(=g),2:z(=b)
        constexpr T& operator[](std::size_t a_index) noexcept
        {
#ifdef _DEBUG
            assert(a_index < 3);
#endif
            return v[a_index];
        }

        /// @brief 配列アクセス（読み取り専用）0:x(=r),1:y(=g),2:z(=b)
        constexpr const T& operator[](std::size_t a_index) const noexcept
        {
#ifdef _DEBUG
            assert(a_index < 3);
#endif
            return v[a_index];
        }

        // --- 符号演算子 ---
        /// @brief 単項プラス
        constexpr Vector3 operator+() const noexcept
        {
            return *this;
        }

        /// @brief 単項マイナス
        constexpr Vector3 operator-() const noexcept
        {
            return { -x, -y, -z };
        }

        // --- 二項演算子 ---
        /// @brief 加算
        constexpr Vector3 operator+(const Vector3& a_other) const noexcept
        {
            return { x + a_other.x, y + a_other.y, z + a_other.z };
        }

        /// @brief 減算
        constexpr Vector3 operator-(const Vector3& a_other) const noexcept
        {
            return { x - a_other.x, y - a_other.y, z - a_other.z };
        }

        /// @brief スカラー乗算
        constexpr Vector3 operator*(T a_scalar) const noexcept
        {
            return { x * a_scalar, y * a_scalar, z * a_scalar };
        }

        /// @brief スカラー除算
        constexpr Vector3 operator/(T a_scalar) const noexcept
        {
            return { x / a_scalar, y / a_scalar, z / a_scalar };
        }

        // --- 複合代入演算子 ---
        /// @brief 加算代入
        constexpr Vector3& operator+=(const Vector3& a_other) noexcept
        {
            x += a_other.x;
            y += a_other.y;
            z += a_other.z;
            return *this;
        }

        /// @brief 減算代入
        constexpr Vector3& operator-=(const Vector3& a_other) noexcept
        {
            x -= a_other.x;
            y -= a_other.y;
            z -= a_other.z;
            return *this;
        }

        /// @brief 乗算代入
        constexpr Vector3& operator*=(T a_scalar) noexcept
        {
            x *= a_scalar;
            y *= a_scalar;
            z *= a_scalar;
            return *this;
        }

        /// @brief 除算代入
        constexpr Vector3& operator/=(T a_scalar) noexcept
        {
            x /= a_scalar;
            y /= a_scalar;
            z /= a_scalar;
            return *this;
        }

        // --- インクリメント/デクリメント ---
        /// @brief 前置インクリメント
        constexpr Vector3& operator++() noexcept
        {
            ++x;
            ++y;
            ++z;
            return *this;
        }

        /// @brief 後置インクリメント
        constexpr Vector3 operator++(int) noexcept
        {
            Vector3 temp = *this;
            ++(*this);
            return temp;
        }

        /// @brief 前置デクリメント
        constexpr Vector3& operator--() noexcept
        {
            --x;
            --y;
            --z;
            return *this;
        }

        /// @brief 後置デクリメント
        constexpr Vector3 operator--(int) noexcept
        {
            Vector3 temp = *this;
            --(*this);
            return temp;
        }

        // --- 比較演算子 ---
        /// @brief 等価（全成分一致）
        constexpr bool operator==(const Vector3& a_other) const noexcept
        {
            return x == a_other.x &&
                y == a_other.y &&
                z == a_other.z;
        }

        /// @brief 非等価
        constexpr bool operator!=(const Vector3& a_other) const noexcept
        {
            return !(*this == a_other);
        }

        /// @brief 小なり（全成分）
        constexpr bool operator<(const Vector3& a_other) const noexcept
        {
            return (x < a_other.x) &&
                (y < a_other.y) &&
                (z < a_other.z);
        }

        /// @brief 小なりイコール（全成分）
        constexpr bool operator<=(const Vector3& a_other) const noexcept
        {
            return (x <= a_other.x) &&
                (y <= a_other.y) &&
                (z <= a_other.z);
        }

        /// @brief 大なり（全成分）
        constexpr bool operator>(const Vector3& a_other) const noexcept
        {
            return (x > a_other.x) &&
                (y > a_other.y) &&
                (z > a_other.z);
        }

        /// @brief 大なりイコール（全成分）
        constexpr bool operator>=(const Vector3& a_other) const noexcept
        {
            return (x >= a_other.x) &&
                (y >= a_other.y) &&
                (z >= a_other.z);
        }

        // --- 計算メンバ関数 ---
        /// @brief 長さ
        T length() const noexcept
        {
            return static_cast<T>(std::sqrt(x * x + y * y + z * z));
        }

        /// @brief 長さの二乗
        constexpr T length_sq() const noexcept
        {
            return x * x + y * y + z * z;
        }

        /// @brief 正規化
        Vector3& normalize() noexcept
        {
            const T len = length();
            if (len != static_cast<T>(0))
            {
                x /= len;
                y /= len;
                z /= len;
            }
            return *this;
        }

        /// @brief 内積
        constexpr T dot(const Vector3& a_other) const noexcept
        {
            return x * a_other.x + y * a_other.y + z * a_other.z;
        }

        /// @brief 外積
        constexpr Vector3 cross(const Vector3& a_other) const noexcept
        {
            return {
                y * a_other.z - z * a_other.y,
                z * a_other.x - x * a_other.z,
                x * a_other.y - y * a_other.x
            };
        }

        // --- Epsilon 比較 ---
        /// @brief Epsilonを用いた等価判定（全成分）
        constexpr bool equals_epsilon(const Vector3& a_other, T a_epsilon) const noexcept
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                auto abs_value = [](T a_value)
                    {
                        return a_value >= T(0) ? a_value : -a_value;
                    };
                return abs_value(x - a_other.x) <= a_epsilon &&
                    abs_value(y - a_other.y) <= a_epsilon &&
                    abs_value(z - a_other.z) <= a_epsilon;
            }
            else
            {
                return (*this == a_other);
            }
        }

        /// @brief 既定Epsilonでの等価判定（浮動小数点は 10*epsilon）
        constexpr bool equals_epsilon(const Vector3& a_other) const noexcept
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                return equals_epsilon(a_other, T(10) * std::numeric_limits<T>::epsilon());
            }
            else
            {
                return (*this == a_other);
            }
        }

        // --- ユーティリティ ---
        /// @brief スカラー×ベクトル（左掛け）
        friend constexpr Vector3 operator*(T a_scalar, const Vector3& a_value) noexcept
        {
            return a_value * a_scalar;
        }

        // --- 静的メンバ関数 ---
        /// @brief 零ベクトル取得
        static constexpr Vector3 zero() noexcept
        {
            return { static_cast<T>(0), static_cast<T>(0), static_cast<T>(0) };
        }

        /// @brief 単位ベクトル取得
        static constexpr Vector3 one() noexcept
        {
            return { static_cast<T>(1), static_cast<T>(1), static_cast<T>(1) };
        }

        /// @brief 単位Xベクトル取得
        static constexpr Vector3 unit_x() noexcept
        {
            return { static_cast<T>(1), static_cast<T>(0), static_cast<T>(0) };
        }

        /// @brief 単位Yベクトル取得
        static constexpr Vector3 unit_y() noexcept
        {
            return { static_cast<T>(0), static_cast<T>(1), static_cast<T>(0) };
        }

        /// @brief 単位Zベクトル取得
        static constexpr Vector3 unit_z() noexcept
        {
            return { static_cast<T>(0), static_cast<T>(0), static_cast<T>(1) };
        }

        /// @brief 最大値ベクトル取得
        static constexpr Vector3 max_value() noexcept
        {
            return { (std::numeric_limits<T>::max)(),
                (std::numeric_limits<T>::max)(),
                (std::numeric_limits<T>::max)() };
        }

        /// @brief 最小値ベクトル取得
        static constexpr Vector3 min_value() noexcept
        {
            return { std::numeric_limits<T>::lowest(),
                std::numeric_limits<T>::lowest(),
                std::numeric_limits<T>::lowest() };
        }

        /// @brief 正規化済みベクトル取得
        static Vector3 normalize(const Vector3& a_value) noexcept
        {
            Vector3 result = a_value;
            result.normalize();
            return result;
        }

        /// @brief 内積計算
        static constexpr T dot(const Vector3& a_left, const Vector3& a_right) noexcept
        {
            return a_left.dot(a_right);
        }

        /// @brief 外積計算
        static constexpr Vector3 cross(const Vector3& a_left, const Vector3& a_right) noexcept
        {
            return a_left.cross(a_right);
        }

        /// @brief 線形補間
        static constexpr Vector3 lerp(const Vector3& a_start, const Vector3& a_end, float a_t) noexcept
        {
            return a_start + (a_end - a_start) * static_cast<T>(a_t);
        }
    };

    using int3 = Vector3<int>;
    using uint3 = Vector3<unsigned int>;
    using uint32T3 = Vector3<std::uint32_t>;
    using float3 = Vector3<float>;
    using double3 = Vector3<double>;
} // Cue::Math 名前空間
