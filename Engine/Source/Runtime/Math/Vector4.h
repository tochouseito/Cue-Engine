#pragma once

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
    /// @brief 4次元ベクトル
    template <AllowedVector T>
    struct Vector4 final
    {
        /// @brief 別名アクセス
        union
        {
            struct
            {
                T x;
                T y;
                T z;
                T w;
            };
            struct
            {
                T r;
                T g;
                T b;
                T a;
            };
            T v[4];
        };

        // --- コンストラクタ ---
        /// @brief デフォルトコンストラクタ
        constexpr Vector4() noexcept
            : x(static_cast<T>(0))
            , y(static_cast<T>(0))
            , z(static_cast<T>(0))
            , w(static_cast<T>(0))
        {}

        /// @brief 引数付きコンストラクタ
        constexpr Vector4(T a_x, T a_y, T a_z, T a_w) noexcept
            : x(a_x)
            , y(a_y)
            , z(a_z)
            , w(a_w)
        {}

        // --- 判定/初期化 ---
        /// @brief ゼロベクトルか判定
        constexpr bool is_zero() const noexcept
        {
            // 1) 全成分が 0 かどうかで判定する
            return x == static_cast<T>(0) &&
                y == static_cast<T>(0) &&
                z == static_cast<T>(0) &&
                w == static_cast<T>(0);
        }

        /// @brief ゼロ初期化
        constexpr void initialize() noexcept
        {
            // 1) 全成分を 0 に初期化する
            x = static_cast<T>(0);
            y = static_cast<T>(0);
            z = static_cast<T>(0);
            w = static_cast<T>(0);
        }

        // --- 変換演算子 ---
        /// @brief bool型への変換（ゼロでなければtrue）
        explicit constexpr operator bool() const noexcept
        {
            // 1) ゼロベクトル判定を再利用する
            return !is_zero();
        }

        // --- 配列アクセス ---
        /// @brief 配列アクセス（読み書き）0:x(=r),1:y(=g),2:z(=b),3:w(=a)
        constexpr T& operator[](std::size_t a_index) noexcept
        {
            // 1) デバッグ時のみ範囲外アクセスを検出する
#ifdef _DEBUG
            assert(a_index < 4);
#endif
            // 2) 配列としてアクセスする
            return v[a_index];
        }

        /// @brief 配列アクセス（読み取り専用）0:x(=r),1:y(=g),2:z(=b),3:w(=a)
        constexpr const T& operator[](std::size_t a_index) const noexcept
        {
            // 1) デバッグ時のみ範囲外アクセスを検出する
#ifdef _DEBUG
            assert(a_index < 4);
#endif
            // 2) 配列としてアクセスする
            return v[a_index];
        }

        // --- 符号演算子 ---
        /// @brief 単項プラス
        constexpr Vector4 operator+() const noexcept
        {
            // 1) 自分自身を返す
            return *this;
        }

        /// @brief 単項マイナス
        constexpr Vector4 operator-() const noexcept
        {
            // 1) 各成分の符号を反転させる
            return { -x, -y, -z, -w };
        }

        // --- 二項演算子 ---
        /// @brief 加算
        constexpr Vector4 operator+(const Vector4& a_other) const noexcept
        {
            // 1) 成分ごとの加算結果を返す
            return { x + a_other.x, y + a_other.y, z + a_other.z, w + a_other.w };
        }

        /// @brief 減算
        constexpr Vector4 operator-(const Vector4& a_other) const noexcept
        {
            // 1) 成分ごとの減算結果を返す
            return { x - a_other.x, y - a_other.y, z - a_other.z, w - a_other.w };
        }

        /// @brief スカラー乗算
        constexpr Vector4 operator*(T a_scalar) const noexcept
        {
            // 1) 成分ごとにスカラーを掛ける
            return { x * a_scalar, y * a_scalar, z * a_scalar, w * a_scalar };
        }

        /// @brief スカラー除算
        constexpr Vector4 operator/(T a_scalar) const noexcept
        {
            // 1) 成分ごとにスカラーで割る
            return { x / a_scalar, y / a_scalar, z / a_scalar, w / a_scalar };
        }

        // --- 複合代入演算子 ---
        /// @brief 加算代入
        constexpr Vector4& operator+=(const Vector4& a_other) noexcept
        {
            // 1) 成分を加算して自身に反映する
            x += a_other.x;
            y += a_other.y;
            z += a_other.z;
            w += a_other.w;
            return *this;
        }

        /// @brief 減算代入
        constexpr Vector4& operator-=(const Vector4& a_other) noexcept
        {
            // 1) 成分を減算して自身に反映する
            x -= a_other.x;
            y -= a_other.y;
            z -= a_other.z;
            w -= a_other.w;
            return *this;
        }

        /// @brief 乗算代入
        constexpr Vector4& operator*=(T a_scalar) noexcept
        {
            // 1) 成分ごとにスカラーを掛ける
            x *= a_scalar;
            y *= a_scalar;
            z *= a_scalar;
            w *= a_scalar;
            return *this;
        }

        /// @brief 除算代入
        constexpr Vector4& operator/=(T a_scalar) noexcept
        {
            // 1) 成分ごとにスカラーで割る
            x /= a_scalar;
            y /= a_scalar;
            z /= a_scalar;
            w /= a_scalar;
            return *this;
        }

        // --- インクリメント/デクリメント ---
        /// @brief 前置インクリメント
        constexpr Vector4& operator++() noexcept
        {
            // 1) 全成分を加算して更新する
            ++x;
            ++y;
            ++z;
            ++w;
            return *this;
        }

        /// @brief 後置インクリメント
        constexpr Vector4 operator++(int) noexcept
        {
            // 1) 変更前のコピーを保持する
            Vector4 temp = *this;
            // 2) 前置版で更新する
            ++(*this);
            return temp;
        }

        /// @brief 前置デクリメント
        constexpr Vector4& operator--() noexcept
        {
            // 1) 全成分を減算して更新する
            --x;
            --y;
            --z;
            --w;
            return *this;
        }

        /// @brief 後置デクリメント
        constexpr Vector4 operator--(int) noexcept
        {
            // 1) 変更前のコピーを保持する
            Vector4 temp = *this;
            // 2) 前置版で更新する
            --(*this);
            return temp;
        }

        // --- 比較演算子 ---
        /// @brief 等価（全成分一致）
        constexpr bool operator==(const Vector4& a_other) const noexcept
        {
            // 1) 全成分が一致するか判定する
            return x == a_other.x &&
                y == a_other.y &&
                z == a_other.z &&
                w == a_other.w;
        }

        /// @brief 非等価
        constexpr bool operator!=(const Vector4& a_other) const noexcept
        {
            // 1) 等価判定の否定を返す
            return !(*this == a_other);
        }

        /// @brief 小なり（全成分）
        constexpr bool operator<(const Vector4& a_other) const noexcept
        {
            // 1) 全成分での大小関係を確認する
            return (x < a_other.x) &&
                (y < a_other.y) &&
                (z < a_other.z) &&
                (w < a_other.w);
        }

        /// @brief 小なりイコール（全成分）
        constexpr bool operator<=(const Vector4& a_other) const noexcept
        {
            // 1) 全成分での大小関係を確認する
            return (x <= a_other.x) &&
                (y <= a_other.y) &&
                (z <= a_other.z) &&
                (w <= a_other.w);
        }

        /// @brief 大なり（全成分）
        constexpr bool operator>(const Vector4& a_other) const noexcept
        {
            // 1) 全成分での大小関係を確認する
            return (x > a_other.x) &&
                (y > a_other.y) &&
                (z > a_other.z) &&
                (w > a_other.w);
        }

        /// @brief 大なりイコール（全成分）
        constexpr bool operator>=(const Vector4& a_other) const noexcept
        {
            // 1) 全成分での大小関係を確認する
            return (x >= a_other.x) &&
                (y >= a_other.y) &&
                (z >= a_other.z) &&
                (w >= a_other.w);
        }

        // --- 計算メンバ関数 ---
        /// @brief 長さ
        T length() const noexcept
        {
            // 1) 4次元の長さを計算する
            return static_cast<T>(std::sqrt(x * x + y * y + z * z + w * w));
        }

        /// @brief 長さの二乗
        constexpr T length_sq() const noexcept
        {
            // 1) 4次元の長さの二乗を返す
            return x * x + y * y + z * z + w * w;
        }

        /// @brief 正規化
        Vector4& normalize() noexcept
        {
            // 1) 長さを取得してゼロ除算を避ける
            const T len = length();
            if (len != static_cast<T>(0))
            {
                // 2) 長さで割って正規化する
                x /= len;
                y /= len;
                z /= len;
                w /= len;
            }
            return *this;
        }

        /// @brief 内積
        constexpr T dot(const Vector4& a_other) const noexcept
        {
            // 1) 4次元の内積を計算する
            return x * a_other.x +
                y * a_other.y +
                z * a_other.z +
                w * a_other.w;
        }

        /// @brief 外積（4次元ベクトルの外積は定義されていないため、3次元ベクトルとして計算し、w成分は0に設定）
        constexpr Vector4 cross(const Vector4& a_other) const noexcept
        {
            // 1) 3次元の外積成分を計算する
            return {
                y * a_other.z - z * a_other.y,
                z * a_other.x - x * a_other.z,
                x * a_other.y - y * a_other.x,
                static_cast<T>(0)
            };
        }

        // --- Epsilon 比較 ---
        /// @brief Epsilonを用いた等価判定（全成分）
        constexpr bool equals_epsilon(const Vector4& a_other, T a_epsilon) const noexcept
        {
            // 1) 浮動小数点なら許容誤差で比較する
            if constexpr (std::is_floating_point_v<T>)
            {
                auto abs_value = [](T a_value)
                    {
                        return a_value >= T(0) ? a_value : -a_value;
                    };
                return abs_value(x - a_other.x) <= a_epsilon &&
                    abs_value(y - a_other.y) <= a_epsilon &&
                    abs_value(z - a_other.z) <= a_epsilon &&
                    abs_value(w - a_other.w) <= a_epsilon;
            }
            // 2) それ以外は完全一致で判定する
            else
            {
                return (*this == a_other);
            }
        }

        /// @brief 既定Epsilonでの等価判定（浮動小数点は 10*epsilon）
        constexpr bool equals_epsilon(const Vector4& a_other) const noexcept
        {
            // 1) 浮動小数点のみ既定値で比較する
            if constexpr (std::is_floating_point_v<T>)
            {
                return equals_epsilon(a_other, T(10) * std::numeric_limits<T>::epsilon());
            }
            // 2) それ以外は完全一致で判定する
            else
            {
                return (*this == a_other);
            }
        }

        // --- ユーティリティ ---
        /// @brief スカラー×ベクトル（左掛け）
        friend constexpr Vector4 operator*(T a_scalar, const Vector4& a_value) noexcept
        {
            // 1) 右辺の乗算を再利用する
            return a_value * a_scalar;
        }

        // --- 静的メンバ関数 ---
        /// @brief 零ベクトル取得
        static constexpr Vector4 zero() noexcept
        {
            // 1) 全成分 0 のベクトルを返す
            return { static_cast<T>(0), static_cast<T>(0), static_cast<T>(0), static_cast<T>(0) };
        }

        /// @brief 単位ベクトル取得
        static constexpr Vector4 one() noexcept
        {
            // 1) 全成分 1 のベクトルを返す
            return { static_cast<T>(1), static_cast<T>(1), static_cast<T>(1), static_cast<T>(1) };
        }

        /// @brief 単位Xベクトル取得
        static constexpr Vector4 unit_x() noexcept
        {
            // 1) X 方向の単位ベクトルを返す
            return { static_cast<T>(1), static_cast<T>(0), static_cast<T>(0), static_cast<T>(0) };
        }

        /// @brief 単位Yベクトル取得
        static constexpr Vector4 unit_y() noexcept
        {
            // 1) Y 方向の単位ベクトルを返す
            return { static_cast<T>(0), static_cast<T>(1), static_cast<T>(0), static_cast<T>(0) };
        }

        /// @brief 単位Zベクトル取得
        static constexpr Vector4 unit_z() noexcept
        {
            // 1) Z 方向の単位ベクトルを返す
            return { static_cast<T>(0), static_cast<T>(0), static_cast<T>(1), static_cast<T>(0) };
        }

        /// @brief 単位Wベクトル取得
        static constexpr Vector4 unit_w() noexcept
        {
            // 1) W 方向の単位ベクトルを返す
            return { static_cast<T>(0), static_cast<T>(0), static_cast<T>(0), static_cast<T>(1) };
        }

        /// @brief 最大値ベクトル取得
        static constexpr Vector4 max_value() noexcept
        {
            // 1) 最大値の成分で構成したベクトルを返す
            return { (std::numeric_limits<T>::max)(),
                (std::numeric_limits<T>::max)(),
                (std::numeric_limits<T>::max)(),
                (std::numeric_limits<T>::max)() };
        }

        /// @brief 最小値ベクトル取得
        static constexpr Vector4 min_value() noexcept
        {
            // 1) 最小値の成分で構成したベクトルを返す
            return { std::numeric_limits<T>::lowest(),
                std::numeric_limits<T>::lowest(),
                std::numeric_limits<T>::lowest(),
                std::numeric_limits<T>::lowest() };
        }

        /// @brief 正規化済みベクトル取得
        static Vector4 normalize(const Vector4& a_value) noexcept
        {
            // 1) コピーして破壊的変更を避ける
            Vector4 result = a_value;
            result.normalize();
            return result;
        }

        /// @brief 内積計算
        static constexpr T dot(const Vector4& a_left, const Vector4& a_right) noexcept
        {
            // 1) メンバ関数の内積を再利用する
            return a_left.dot(a_right);
        }

        /// @brief 外積計算（4次元ベクトルの外積は定義されていないため、3次元ベクトルとして計算し、w成分は0に設定）
        static constexpr Vector4 cross(const Vector4& a_left, const Vector4& a_right) noexcept
        {
            // 1) メンバ関数の外積を再利用する
            return a_left.cross(a_right);
        }
    };

    using int4 = Vector4<int>;
    using uint4 = Vector4<unsigned int>;
    using uint32T4 = Vector4<std::uint32_t>;
    using float4 = Vector4<float>;
    using double4 = Vector4<double>;
} // Cue::Math 名前空間
