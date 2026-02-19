#pragma once

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
        // Integer conversions (truncate)
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
        // Floating-point conversions (no truncate)
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
        // Integer cast helpers (truncate)
        // --------------------
        static int64_t to_nanoseconds(int64_t v, TimeUnit u) noexcept
        {
            switch (u)
            {
            case TimeUnit::seconds:
                return v * 1'000'000'000LL;
            case TimeUnit::milliseconds:
                return v * 1'000'000LL;
            case TimeUnit::microseconds:
                return v * 1'000LL;
            case TimeUnit::nanoseconds:
                return v;
            default:
                return 0;
            }
        }

        static int64_t unit_cast(TimeSpan v, TimeUnit targetUnit) noexcept
        {
            const int64_t ns = to_nanoseconds(v.value, v.unit);
            switch (targetUnit)
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
        // Floating cast helpers (no truncate)
        //   ※整数の ns へ一旦変換するとオーバーフローし得るので、
        //     直接スケールして秒へ落としてから目的単位へ変換する。
        // --------------------
        static double to_seconds_f64(int64_t v, TimeUnit u) noexcept
        {
            switch (u)
            {
            case TimeUnit::seconds:
                return static_cast<double>(v);
            case TimeUnit::milliseconds:
                return static_cast<double>(v) * 1e-3;
            case TimeUnit::microseconds:
                return static_cast<double>(v) * 1e-6;
            case TimeUnit::nanoseconds:
                return static_cast<double>(v) * 1e-9;
            default:
                return 0.0;
            }
        }

        static double unit_cast_f64(TimeSpan v, TimeUnit targetUnit) noexcept
        {
            // 1) まず秒(double)へ
            const double sec = to_seconds_f64(v.value, v.unit);

            // 2) 秒から目的単位へ
            switch (targetUnit)
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
        bool operator==(const TimeSpan& other) const noexcept
        {
            // 1) 単位が同じなら値を直接比較
            if (unit == other.unit)
            {
                return value == other.value;
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して比較
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(other.value, other.unit);
            return thisNs == otherNs;
        }
        bool operator==(const int64_t other) const noexcept
        {
            // 1) 単位がナノ秒なら直接比較
            if (unit == TimeUnit::nanoseconds)
            {
                return value == other;
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して比較
            const int64_t thisNs = to_nanoseconds(value, unit);
            return thisNs == other;
        }
        bool operator!=(const TimeSpan& other) const noexcept
        {
            return !(*this == other);
        }
        bool operator<(const TimeSpan& other) const noexcept
        {
            // 1) 単位が同じなら値を直接比較
            if (unit == other.unit)
            {
                return value < other.value;
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して比較
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(other.value, other.unit);
            return thisNs < otherNs;
        }
        bool operator<=(const TimeSpan& other) const noexcept
            {
                return (*this < other) || (*this == other);
        }
        bool operator>(const TimeSpan& other) const noexcept
        {
            return !(*this <= other);
        }
        bool operator>=(const TimeSpan& other) const noexcept
        {
            return !(*this < other);
        }

        // 加算・減算
        TimeSpan operator+(const TimeSpan& other) const noexcept
        {
            // 1) 単位が同じなら値を直接加算
            if (unit == other.unit)
            {
                return { value + other.value, unit };
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して加算
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(other.value, other.unit);
            return { thisNs + otherNs, TimeUnit::nanoseconds };
        }
        
        TimeSpan operator-(const TimeSpan& other) const noexcept
        {
            // 1) 単位が同じなら値を直接減算
            if (unit == other.unit)
            {
                return { value - other.value, unit };
            }
            // 2) 単位が違うなら、両方をナノ秒に換算して減算
            const int64_t thisNs = to_nanoseconds(value, unit);
            const int64_t otherNs = to_nanoseconds(other.value, other.unit);
            return { thisNs - otherNs, TimeUnit::nanoseconds };
        }

        // スカラー倍
        TimeSpan operator*(int64_t scalar) const noexcept
        {
            return { value * scalar, unit };
        }

        // スカラー除算
        TimeSpan operator/(int64_t scalar) const noexcept
        {
            return { value / scalar, unit };
        }

        // 代入演算子
        TimeSpan& operator+=(const TimeSpan& other) noexcept
        {
            *this = *this + other;
            return *this;
        }
        TimeSpan& operator-=(const TimeSpan& other) noexcept
        {
            *this = *this - other;
            return *this;
        }
    };
}
