#include "WinQpcClock.h"

namespace Cue::PAL::Win
{
    Math::TimeSpan WinQpcClock::now_ns() const noexcept
    {
        // QPC取得
        LARGE_INTEGER c{};
        const BOOL ok = ::QueryPerformanceCounter(&c);
        if (ok == FALSE)
        {
            return Math::TimeSpan::zero();
        }

        // ticks->ns
        int64_t ns = ticks_to_ns(static_cast<std::int64_t>(c.QuadPart), query_frequency_hz());
        return Math::TimeSpan{ ns, Math::TimeUnit::nanoseconds };
    }
    std::int64_t WinQpcClock::query_frequency_hz() noexcept
    {
        // キャッシュ
        static std::int64_t cached = []() noexcept -> std::int64_t
            {
                LARGE_INTEGER f{};
                const BOOL ok = ::QueryPerformanceFrequency(&f);
                if (ok == FALSE)
                {
                    return 0;
                }
                return static_cast<std::int64_t>(f.QuadPart);
            }();

        return cached;
    }
    std::int64_t WinQpcClock::ticks_to_ns(const std::int64_t ticks, const std::int64_t freq) noexcept
    {
        // 異常系（割り算事故防止）
        if (freq <= 0)
        {
            return 0;
        }

        // オーバーフロー回避：秒 + 余りで換算
        constexpr std::int64_t kNsPerSec = 1000000000LL;

        const std::int64_t sec = ticks / freq;
        const std::int64_t rem = ticks % freq;

        // rem*kNsPerSec を 64bit で計算してナノ秒へ変換する
        const std::int64_t ns = (sec * kNsPerSec) + ((rem * kNsPerSec) / freq);
        return ns;
    }
}
