#pragma once
#include "MathStructAllowedList.h"

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
                T m_x;
                T m_y;
                T m_z;
            };
            struct
            {
                T m_r;
                T m_g;
                T m_b;
            };
            T m_v[3];
        };

        /*================ コンストラクタ ================*/
        /// @brief デフォルトコンストラクタ
        constexpr Vector3() noexcept
            : m_x(static_cast<T>(0))
            , m_y(static_cast<T>(0))
            , m_z(static_cast<T>(0))
        {}

        /// @brief 引数付きコンストラクタ
        constexpr Vector3(T x, T y, T z) noexcept
            : m_x(x)
            , m_y(y)
            , m_z(z)
        {}

        /*================ 判定/初期化 ================*/
        /// @brief ゼロベクトルか判定
        constexpr bool is_zero() const noexcept
        {
            // 1) 全成分が 0 かどうかで判定する
            return m_x == static_cast<T>(0) &&
                m_y == static_cast<T>(0) &&
                m_z == static_cast<T>(0);
        }

        /// @brief ゼロ初期化
        constexpr void initialize() noexcept
        {
            // 1) 全成分を 0 に初期化する
            m_x = static_cast<T>(0);
            m_y = static_cast<T>(0);
            m_z = static_cast<T>(0);
        }

        /*================ 変換演算子 ================*/
        /// @brief bool型への変換（ゼロでなければtrue）
        explicit constexpr operator bool() const noexcept
        {
            // 1) ゼロベクトル判定を再利用する
            return !is_zero();
        }

        /*================ 配列アクセス ================*/
        /// @brief 配列アクセス（読み書き）0:x(=r),1:y(=g),2:z(=b)
        constexpr T& operator[](std::size_t i) noexcept
        {
            // 1) デバッグ時のみ範囲外アクセスを検出する
#ifdef _DEBUG
            assert(i < 3);
#endif
            // 2) 配列としてアクセスする
            return m_v[i];
        }

        /// @brief 配列アクセス（読み取り専用）0:x(=r),1:y(=g),2:z(=b)
        constexpr const T& operator[](std::size_t i) const noexcept
        {
            // 1) デバッグ時のみ範囲外アクセスを検出する
#ifdef _DEBUG
            assert(i < 3);
#endif
            // 2) 配列としてアクセスする
            return m_v[i];
        }

        /*================ 符号演算子 ================*/
        /// @brief 単項プラス
        constexpr Vector3 operator+() const noexcept
        {
            // 1) 自分自身を返す
            return *this;
        }

        /// @brief 単項マイナス
        constexpr Vector3 operator-() const noexcept
        {
            // 1) 各成分の符号を反転させる
            return { -m_x, -m_y, -m_z };
        }

        /*================ 二項演算子 ================*/
        /// @brief 加算
        constexpr Vector3 operator+(const Vector3& other) const noexcept
        {
            // 1) 成分ごとの加算結果を返す
            return { m_x + other.m_x, m_y + other.m_y, m_z + other.m_z };
        }

        /// @brief 減算
        constexpr Vector3 operator-(const Vector3& other) const noexcept
        {
            // 1) 成分ごとの減算結果を返す
            return { m_x - other.m_x, m_y - other.m_y, m_z - other.m_z };
        }

        /// @brief スカラー乗算
        constexpr Vector3 operator*(T scalar) const noexcept
        {
            // 1) 成分ごとにスカラーを掛ける
            return { m_x * scalar, m_y * scalar, m_z * scalar };
        }

        /// @brief スカラー除算
        constexpr Vector3 operator/(T scalar) const noexcept
        {
            // 1) 成分ごとにスカラーで割る
            return { m_x / scalar, m_y / scalar, m_z / scalar };
        }

        /*================ 複合代入演算子 ================*/
        /// @brief 加算代入
        constexpr Vector3& operator+=(const Vector3& other) noexcept
        {
            // 1) 成分を加算して自身に反映する
            m_x += other.m_x;
            m_y += other.m_y;
            m_z += other.m_z;
            return *this;
        }

        /// @brief 減算代入
        constexpr Vector3& operator-=(const Vector3& other) noexcept
        {
            // 1) 成分を減算して自身に反映する
            m_x -= other.m_x;
            m_y -= other.m_y;
            m_z -= other.m_z;
            return *this;
        }

        /// @brief 乗算代入
        constexpr Vector3& operator*=(T scalar) noexcept
        {
            // 1) 成分ごとにスカラーを掛ける
            m_x *= scalar;
            m_y *= scalar;
            m_z *= scalar;
            return *this;
        }

        /// @brief 除算代入
        constexpr Vector3& operator/=(T scalar) noexcept
        {
            // 1) 成分ごとにスカラーで割る
            m_x /= scalar;
            m_y /= scalar;
            m_z /= scalar;
            return *this;
        }

        /*================ インクリメント/デクリメント ================*/
        /// @brief 前置インクリメント
        constexpr Vector3& operator++() noexcept
        {
            // 1) 全成分を加算して更新する
            ++m_x;
            ++m_y;
            ++m_z;
            return *this;
        }

        /// @brief 後置インクリメント
        constexpr Vector3 operator++(int) noexcept
        {
            // 1) 変更前のコピーを保持する
            Vector3 temp = *this;
            // 2) 前置版で更新する
            ++(*this);
            return temp;
        }

        /// @brief 前置デクリメント
        constexpr Vector3& operator--() noexcept
        {
            // 1) 全成分を減算して更新する
            --m_x;
            --m_y;
            --m_z;
            return *this;
        }

        /// @brief 後置デクリメント
        constexpr Vector3 operator--(int) noexcept
        {
            // 1) 変更前のコピーを保持する
            Vector3 temp = *this;
            // 2) 前置版で更新する
            --(*this);
            return temp;
        }

        /*================ 比較演算子 ================*/
        /// @brief 等価（全成分一致）
        constexpr bool operator==(const Vector3& other) const noexcept
        {
            // 1) 全成分が一致するか判定する
            return m_x == other.m_x &&
                m_y == other.m_y &&
                m_z == other.m_z;
        }

        /// @brief 非等価
        constexpr bool operator!=(const Vector3& other) const noexcept
        {
            // 1) 等価判定の否定を返す
            return !(*this == other);
        }

        /// @brief 小なり（全成分）
        constexpr bool operator<(const Vector3& other) const noexcept
        {
            // 1) 全成分での大小関係を確認する
            return (m_x < other.m_x) &&
                (m_y < other.m_y) &&
                (m_z < other.m_z);
        }

        /// @brief 小なりイコール（全成分）
        constexpr bool operator<=(const Vector3& other) const noexcept
        {
            // 1) 全成分での大小関係を確認する
            return (m_x <= other.m_x) &&
                (m_y <= other.m_y) &&
                (m_z <= other.m_z);
        }

        /// @brief 大なり（全成分）
        constexpr bool operator>(const Vector3& other) const noexcept
        {
            // 1) 全成分での大小関係を確認する
            return (m_x > other.m_x) &&
                (m_y > other.m_y) &&
                (m_z > other.m_z);
        }

        /// @brief 大なりイコール（全成分）
        constexpr bool operator>=(const Vector3& other) const noexcept
        {
            // 1) 全成分での大小関係を確認する
            return (m_x >= other.m_x) &&
                (m_y >= other.m_y) &&
                (m_z >= other.m_z);
        }

        /*================ 計算メンバ関数 ================*/
        /// @brief 長さ
        T length() const noexcept
        {
            // 1) 3次元の長さを計算する
            return static_cast<T>(std::sqrt(m_x * m_x + m_y * m_y + m_z * m_z));
        }

        /// @brief 長さの二乗
        constexpr T length_sq() const noexcept
        {
            // 1) 3次元の長さの二乗を返す
            return m_x * m_x + m_y * m_y + m_z * m_z;
        }

        /// @brief 正規化
        Vector3& normalize() noexcept
        {
            // 1) 長さを取得してゼロ除算を避ける
            const T len = length();
            if (len != static_cast<T>(0))
            {
                // 2) 長さで割って正規化する
                m_x /= len;
                m_y /= len;
                m_z /= len;
            }
            return *this;
        }

        /// @brief 内積
        constexpr T dot(const Vector3& other) const noexcept
        {
            // 1) 3次元の内積を計算する
            return m_x * other.m_x + m_y * other.m_y + m_z * other.m_z;
        }

        /// @brief 外積
        constexpr Vector3 cross(const Vector3& other) const noexcept
        {
            // 1) 3次元の外積を計算する
            return {
                m_y * other.m_z - m_z * other.m_y,
                m_z * other.m_x - m_x * other.m_z,
                m_x * other.m_y - m_y * other.m_x
            };
        }

        /*================ Epsilon 比較 ================*/
        /// @brief Epsilonを用いた等価判定（全成分）
        constexpr bool equals_epsilon(const Vector3& other, T epsilon) const noexcept
        {
            // 1) 浮動小数点なら許容誤差で比較する
            if constexpr (std::is_floating_point_v<T>)
            {
                auto abs_value = [](T value)
                    {
                        return value >= T(0) ? value : -value;
                    };
                return abs_value(m_x - other.m_x) <= epsilon &&
                    abs_value(m_y - other.m_y) <= epsilon &&
                    abs_value(m_z - other.m_z) <= epsilon;
            }
            // 2) それ以外は完全一致で判定する
            else
            {
                return (*this == other);
            }
        }

        /// @brief 既定Epsilonでの等価判定（浮動小数点は 10*epsilon）
        constexpr bool equals_epsilon(const Vector3& other) const noexcept
        {
            // 1) 浮動小数点のみ既定値で比較する
            if constexpr (std::is_floating_point_v<T>)
            {
                return equals_epsilon(other, T(10) * std::numeric_limits<T>::epsilon());
            }
            // 2) それ以外は完全一致で判定する
            else
            {
                return (*this == other);
            }
        }

        /*================ ユーティリティ ================*/
        /// @brief スカラー×ベクトル（左掛け）
        friend constexpr Vector3 operator*(T scalar, const Vector3& value) noexcept
        {
            // 1) 右辺の乗算を再利用する
            return value * scalar;
        }

        /*================ 静的メンバ関数 ================*/
        /// @brief 零ベクトル取得
        static constexpr Vector3 zero() noexcept
        {
            // 1) 全成分 0 のベクトルを返す
            return { static_cast<T>(0), static_cast<T>(0), static_cast<T>(0) };
        }

        /// @brief 単位ベクトル取得
        static constexpr Vector3 one() noexcept
        {
            // 1) 全成分 1 のベクトルを返す
            return { static_cast<T>(1), static_cast<T>(1), static_cast<T>(1) };
        }

        /// @brief 単位Xベクトル取得
        static constexpr Vector3 unit_x() noexcept
        {
            // 1) X 方向の単位ベクトルを返す
            return { static_cast<T>(1), static_cast<T>(0), static_cast<T>(0) };
        }

        /// @brief 単位Yベクトル取得
        static constexpr Vector3 unit_y() noexcept
        {
            // 1) Y 方向の単位ベクトルを返す
            return { static_cast<T>(0), static_cast<T>(1), static_cast<T>(0) };
        }

        /// @brief 単位Zベクトル取得
        static constexpr Vector3 unit_z() noexcept
        {
            // 1) Z 方向の単位ベクトルを返す
            return { static_cast<T>(0), static_cast<T>(0), static_cast<T>(1) };
        }

        /// @brief 最大値ベクトル取得
        static constexpr Vector3 max_value() noexcept
        {
            // 1) 最大値の成分で構成したベクトルを返す
            return { (std::numeric_limits<T>::max)(),
                (std::numeric_limits<T>::max)(),
                (std::numeric_limits<T>::max)() };
        }

        /// @brief 最小値ベクトル取得
        static constexpr Vector3 min_value() noexcept
        {
            // 1) 最小値の成分で構成したベクトルを返す
            return { std::numeric_limits<T>::lowest(),
                std::numeric_limits<T>::lowest(),
                std::numeric_limits<T>::lowest() };
        }

        /// @brief 正規化済みベクトル取得
        static Vector3 normalize(const Vector3& value) noexcept
        {
            // 1) コピーして破壊的変更を避ける
            Vector3 result = value;
            result.normalize();
            return result;
        }

        /// @brief 内積計算
        static constexpr T dot(const Vector3& left, const Vector3& right) noexcept
        {
            // 1) メンバ関数の内積を再利用する
            return left.dot(right);
        }

        /// @brief 外積計算
        static constexpr Vector3 cross(const Vector3& left, const Vector3& right) noexcept
        {
            // 1) メンバ関数の外積を再利用する
            return left.cross(right);
        }

        /// @brief 線形補間
        static constexpr Vector3 lerp(const Vector3& start, const Vector3& end, float t) noexcept
        {
            // 1) 線形補間で 3 次元の値を求める
            return start + (end - start) * static_cast<T>(t);
        }
    };

    using int3 = Vector3<int>;
    using uint3 = Vector3<unsigned int>;
    using uint32T3 = Vector3<std::uint32_t>;
    using float3 = Vector3<float>;
    using double3 = Vector3<double>;
} // Cue::Math 名前空間
