#pragma once

// === C++ includes ===
#include <cstdint>

namespace Cue::Math
{
    enum class TimeUnit : uint8_t
    {
        seconds,
        milliseconds,
        microseconds,
        nanoseconds,
    };

    struct TimeSpan
    {
        int64_t value;
        TimeUnit unit;

        static TimeSpan zero() noexcept
        {
            return { 0, TimeUnit::nanoseconds };
        }

        // --------------------
        // 整数変換
        // --------------------
        int64_t nano() const noexcept
        {
            return unit_cast(*this, TimeUnit::nanoseconds);
        }

        int64_t ms() const noexcept
        {
            return unit_cast(*this, TimeUnit::milliseconds);
        }

        int64_t us() const noexcept
        {
            return unit_cast(*this, TimeUnit::microseconds);
        }

        int64_t s() const noexcept
        {
            return unit_cast(*this, TimeUnit::seconds);
        }

        // --------------------
        // 浮動小数変換
        // --------------------
        double nano_f64() const noexcept
        {
            // 1) 対象単位へ浮動小数で変換
            return unit_cast_f64(*this, TimeUnit::nanoseconds);
        }

        double ms_f64() const noexcept
        {
            // 1) 対象単位へ浮動小数で変換
            return unit_cast_f64(*this, TimeUnit::milliseconds);
        }

        double us_f64() const noexcept
        {
            // 1) 対象単位へ浮動小数で変換
            return unit_cast_f64(*this, TimeUnit::microseconds);
        }

        double s_f64() const noexcept
        {
            // 1) 対象単位へ浮動小数で変換
            return unit_cast_f64(*this, TimeUnit::seconds);
        }

        // --------------------
        // 整数変換補助
        // --------------------
        static int64_t to_nanoseconds(int64_t a_value, TimeUnit a_unit) noexcept
        {
            switch (a_unit)
            {
            case TimeUnit::seconds:
                return a_value * 1'000'000'000LL;
            case TimeUnit::milliseconds:
                return a_value * 1'000'000LL;
            case TimeUnit::microseconds:
                return a_value * 1'000LL;
            case TimeUnit::nanoseconds:
                return a_value;
            default:
                return 0;
            }
        }

        static int64_t unit_cast(TimeSpan a_value, TimeUnit a_targetUnit) noexcept
        {
            const int64_t ns = to_nanoseconds(a_value.value, a_value.unit);
            switch (a_targetUnit)
            {
            case TimeUnit::seconds:
                return ns / 1'000'000'000LL;
            case TimeUnit::milliseconds:
                return ns / 1'000'000LL;
            case TimeUnit::microseconds:
                return ns / 1'000LL;
            case TimeUnit::nanoseconds:
                return ns;
            default:
                return 0;
            }
        }

        // --------------------
        // 浮動小数変換補助
        // 1) 整数 ns 経由の overflow 回避
        // 2) 秒へ落としてから目的単位へ変換
        // --------------------
        static double to_seconds_f64(int64_t a_value, TimeUnit a_unit) noexcept
        {
            switch (a_unit)
            {
            case TimeUnit::seconds:
                return static_cast<double>(a_value);
            case TimeUnit::milliseconds:
                return static_cast<double>(a_value) * 1e-3;
            case TimeUnit::microseconds:
                return static_cast<double>(a_value) * 1e-6;
            case TimeUnit::nanoseconds:
                return static_cast<double>(a_value) * 1e-9;
            default:
                return 0.0;
            }
        }

        static double unit_cast_f64(TimeSpan a_value, TimeUnit a_targetUnit) noexcept
        {
            // 1) まず秒(double)へ
            const double sec = to_seconds_f64(a_value.value, a_value.unit);

            // 2) 秒から目的単位へ
            switch (a_targetUnit)
            {
            case TimeUnit::seconds:
                return sec;
            case TimeUnit::milliseconds:
                return sec * 1e3;
            case TimeUnit::microseconds:
                return sec * 1e6;
            case TimeUnit::nanoseconds:
                return sec * 1e9;
            default:
                return 0.0;
            }
        }

        // 比較
        bool operator==(const TimeSpan& a_other) const noexcept
        {
            // 1) 単位が同じなら値を直接比較
            if (unit == a_other.unit)
            {
                return value == a_other.value;
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して比較
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(a_other.value, a_other.unit);
            return thisNs == otherNs;
        }
        bool operator==(const int64_t a_other) const noexcept
        {
            // 1) 単位がナノ秒なら直接比較
            if (unit == TimeUnit::nanoseconds)
            {
                return value == a_other;
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して比較
            const int64_t thisNs = to_nanoseconds(value, unit);
            return thisNs == a_other;
        }
        bool operator!=(const TimeSpan& a_other) const noexcept
        {
            return !(*this == a_other);
        }
        bool operator<(const TimeSpan& a_other) const noexcept
        {
            // 1) 単位が同じなら値を直接比較
            if (unit == a_other.unit)
            {
                return value < a_other.value;
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して比較
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(a_other.value, a_other.unit);
            return thisNs < otherNs;
        }
        bool operator<=(const TimeSpan& a_other) const noexcept
        {
            return (*this < a_other) || (*this == a_other);
        }
        bool operator>(const TimeSpan& a_other) const noexcept
        {
            return !(*this <= a_other);
        }
        bool operator>=(const TimeSpan& a_other) const noexcept
        {
            return !(*this < a_other);
        }

        // 加算・減算
        TimeSpan operator+(const TimeSpan& a_other) const noexcept
        {
            // 1) 単位が同じなら値を直接加算
            if (unit == a_other.unit)
            {
                return { value + a_other.value, unit };
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して加算
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(a_other.value, a_other.unit);
            return { thisNs + otherNs, TimeUnit::nanoseconds };
        }

        TimeSpan operator+(int64_t a_other) const noexcept
        {
            // 1) 単位がナノ秒なら直接加算
            if (unit == TimeUnit::nanoseconds)
            {
                return { value + a_other, unit };
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して加算
            const int64_t thisNs = to_nanoseconds(value, unit);
            return { thisNs + a_other, TimeUnit::nanoseconds };
        }

        TimeSpan operator-(const TimeSpan& a_other) const noexcept
        {
            // 1) 単位が同じなら値を直接減算
            if (unit == a_other.unit)
            {
                return { value - a_other.value, unit };
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して減算
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(a_other.value, a_other.unit);
            return { thisNs - otherNs, TimeUnit::nanoseconds };
        }

        TimeSpan operator-(int64_t a_other) const noexcept
        {
            // 1) 単位がナノ秒なら直接減算
            if (unit == TimeUnit::nanoseconds)
            {
                return { value - a_other, unit };
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して減算
            const int64_t thisNs = to_nanoseconds(value, unit);
            return { thisNs - a_other, TimeUnit::nanoseconds };
        }

        // 乗算
        TimeSpan operator*(const TimeSpan& a_other) const noexcept
        {
            // 1) 単位が同じなら値を直接乗算
            if (unit == a_other.unit)
            {
                return { value * a_other.value, unit };
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して乗算
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(a_other.value, a_other.unit);
            return { thisNs * otherNs, TimeUnit::nanoseconds };
        }

        // スカラー倍
        TimeSpan operator*(int64_t a_scalar) const noexcept
        {
            return { value * a_scalar, unit };
        }

        // 除算
        TimeSpan operator/(const TimeSpan& a_other) const noexcept
        {
            // 1) 単位が同じなら値を直接除算
            if (unit == a_other.unit)
            {
                return { value / a_other.value, unit };
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して除算
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(a_other.value, a_other.unit);
            return { thisNs / otherNs, TimeUnit::nanoseconds };
        }

        // スカラー除算
        TimeSpan operator/(int64_t a_scalar) const noexcept
        {
            return { value / a_scalar, unit };
        }

        // 代入演算子
        TimeSpan& operator+=(const TimeSpan& a_other) noexcept
        {
            *this = *this + a_other;
            return *this;
        }
        TimeSpan& operator-=(const TimeSpan& a_other) noexcept
        {
            *this = *this - a_other;
            return *this;
        }
    };
}
