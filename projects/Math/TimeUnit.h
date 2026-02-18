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

        // 変換
        static int64_t to_nanoseconds(int64_t value, TimeUnit unit) noexcept
        {
            switch (unit)
            {
            case TimeUnit::seconds:
                return value * 1'000'000'000LL;
            case TimeUnit::milliseconds:
                return value * 1'000'000LL;
            case TimeUnit::microseconds:
                return value * 1'000LL;
            case TimeUnit::nanoseconds:
                return value;
            default:
                return 0; // 異常系
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
                return 0; // 不明な単位は0とみなす
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
