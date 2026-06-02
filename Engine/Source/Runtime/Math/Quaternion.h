// Quaternion の役割と公開要素を定義する

#pragma once

// === Math includes === 
#include "MathStructAllowedList.h"

// === C++ includes ===
#include <cmath>
#include <limits>

namespace Cue::Math
{
    /// @brief クォータニオン構造体（w + xi + yj + zk）
    struct Quaternion final
    {
        float x;
        float y;
        float z;
        float w;

        // --- コンストラクタ/初期化 ---
        /// @brief デフォルトは単位（0,0,0,1）
        constexpr Quaternion(float a_x = 0.0f, float a_y = 0.0f, float a_z = 0.0f, float a_w = 1.0f) noexcept
            : x(a_x)
            , y(a_y)
            , z(a_z)
            , w(a_w)
        {}

        /// @brief 単位に初期化
        void initialize() noexcept
        {
            // - 単位クォータニオンに戻す
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
            w = 1.0f;
        }

        // --- 四則/積 ---
        /// @brief 和
        Quaternion operator+(const Quaternion& other) const noexcept
        {
            // - 成分ごとの加算結果を返す
            return { x + other.x, y + other.y, z + other.z, w + other.w };
        }

        /// @brief 差
        Quaternion operator-(const Quaternion& other) const noexcept
        {
            // - 成分ごとの減算結果を返す
            return { x - other.x, y - other.y, z - other.z, w - other.w };
        }

        /// @brief スカラー乗算
        Quaternion operator*(float scalar) const noexcept
        {
            // - 成分ごとにスカラーを掛ける
            return { x * scalar, y * scalar, z * scalar, w * scalar };
        }

        /// @brief スカラー除算（0割は単位を返す）
        Quaternion operator/(float scalar) const noexcept
        {
            // - 0割は単位クォータニオンでフォールバックする
            if (scalar == 0.0f)
            {
                return identity();
            }
            return { x / scalar, y / scalar, z / scalar, w / scalar };
        }

        /// @brief 積（this * other）
        Quaternion multiply(const Quaternion& other) const noexcept
        {
            // - ハミルトン積で合成する
            return {
                w * other.x + x * other.w + y * other.z - z * other.y,
                w * other.y - x * other.z + y * other.w + z * other.x,
                w * other.z + x * other.y - y * other.x + z * other.w,
                w * other.w - x * other.x - y * other.y - z * other.z
            };
        }

        /// @brief 積（演算子）
        Quaternion operator*(const Quaternion& other) const noexcept
        {
            // - 積の実装を再利用する
            return multiply(other);
        }

        // --- 基本演算 ---
        /// @brief 共役（その場で反転）
        void conjugate() noexcept
        {
            // - ベクトル成分の符号を反転する
            x = -x;
            y = -y;
            z = -z;
            // m_w はそのまま
        }

        /// @brief ノルム
        float norm() const noexcept
        {
            // - 成分の二乗和からノルムを計算する
            return std::sqrt(x * x + y * y + z * z + w * w);
        }

        /// @brief 内積
        float dot(const Quaternion& other) const noexcept
        {
            // - 成分ごとの積和を計算する
            return x * other.x + y * other.y + z * other.z + w * other.w;
        }

        /// @brief 正規化（その場）
        Quaternion normalize() noexcept
        {
            // - ノルムで割って単位化する
            const float n = norm();
            if (n == 0.0f)
            {
                initialize();
            }
            else
            {
                x /= n;
                y /= n;
                z /= n;
                w /= n;
            }
            return *this;
        }

        /// @brief 逆（その場）
        void inverse() noexcept
        {
            // - 共役とノルムから逆クォータニオンを求める
            const Quaternion conj = conjugate_copy(*this);
            const float n = norm();
            const float n2 = n * n;
            if (n2 == 0.0f)
            {
                initialize();
            }
            else
            {
                x = conj.x / n2;
                y = conj.y / n2;
                z = conj.z / n2;
                w = conj.w / n2;
            }
        }

    public:
        // --- 静的ユーティリティ ---
        /// @brief 線形補間（正規化付き）
        static Quaternion lerp(const Quaternion& start, const Quaternion& end, float t) noexcept
        {
            // - 線形補間して正規化する
            Quaternion result = start * (1.0f - t) + end * t;
            return result.normalize();
        }

        /// @brief 球面線形補間（大角・小角に自動対応）
        static Quaternion slerp(const Quaternion& start, const Quaternion& end, float t) noexcept
        {
            // - 内積と符号で最短経路を確保する
            float dot_value = start.dot(end);
            Quaternion end_adjusted = end;
            if (dot_value < 0.0f)
            {
                end_adjusted = Quaternion{ -end.x, -end.y, -end.z, -end.w };
                dot_value = -dot_value;
            }

            // - ほぼ同方向なら線形補間で十分
            const float threshold = 0.9995f;
            if (dot_value > threshold)
            {
                return lerp(start, end_adjusted, t);
            }

            // - 角度を使って球面補間する
            const float theta0 = std::acos(dot_value);
            const float theta = theta0 * t;
            const float s0 = std::cos(theta) - dot_value * std::sin(theta) / std::sin(theta0);
            const float s1 = std::sin(theta) / std::sin(theta0);
            Quaternion result{
                s0 * start.x + s1 * end_adjusted.x,
                s0 * start.y + s1 * end_adjusted.y,
                s0 * start.z + s1 * end_adjusted.z,
                s0 * start.w + s1 * end_adjusted.w
            };
            return result.normalize();
        }

        /// @brief 単位
        static Quaternion identity() noexcept
        {
            // - 単位クォータニオンを返す
            return { 0.0f, 0.0f, 0.0f, 1.0f };
        }

        /// @brief 共役（コピー版）
        static Quaternion conjugate_copy(const Quaternion& value) noexcept
        {
            // - ベクトル成分だけ反転したコピーを返す
            return { -value.x, -value.y, -value.z, value.w };
        }

        /// @brief 正規化（コピー版）
        static Quaternion normalize(const Quaternion& value) noexcept
        {
            // - ノルムが 0 のときは単位を返す
            const float n = std::sqrt(value.x * value.x +
                value.y * value.y +
                value.z * value.z +
                value.w * value.w);
            if (n == 0.0f)
            {
                return identity();
            }
            return { value.x / n, value.y / n, value.z / n, value.w / n };
        }

        /// @brief 逆（コピー版）
        static Quaternion inverse(const Quaternion& value) noexcept
        {
            // - 共役とノルムから逆クォータニオンを求める
            const Quaternion conj = conjugate_copy(value);
            const float n = std::sqrt(value.x * value.x +
                value.y * value.y +
                value.z * value.z +
                value.w * value.w);
            const float n2 = n * n;
            if (n2 == 0.0f)
            {
                return identity();
            }
            return { conj.x / n2, conj.y / n2, conj.z / n2, conj.w / n2 };
        }

        // --- Epsilon 比較 ---
        /// @brief 既定Epsilonでの等価（|a-b|<=ε を全成分で）
        static bool equals_epsilon(const Quaternion& left, const Quaternion& right) noexcept
        {
            // - 既定の許容誤差で比較する
            constexpr float epsilon = 10.0f * std::numeric_limits<float>::epsilon();
            auto abs_value = [](float value)
                {
                    return value >= 0.0f ? value : -value;
                };
            return abs_value(left.x - right.x) <= epsilon &&
                abs_value(left.y - right.y) <= epsilon &&
                abs_value(left.z - right.z) <= epsilon &&
                abs_value(left.w - right.w) <= epsilon;
        }

        /// @brief Epsilon指定の等価
        static bool equals_epsilon(const Quaternion& left, const Quaternion& right, float epsilon) noexcept
        {
            // - 指定の許容誤差で比較する
            auto abs_value = [](float value)
                {
                    return value >= 0.0f ? value : -value;
                };
            return abs_value(left.x - right.x) <= epsilon &&
                abs_value(left.y - right.y) <= epsilon &&
                abs_value(left.z - right.z) <= epsilon &&
                abs_value(left.w - right.w) <= epsilon;
        }
    };
}
