#include "WinQpcClock.h"

namespace Cue::PAL::Win
{
    Math::TimeSpan WinQpcClock::now_ns() const noexcept
    {
        // QPC取得
        LARGE_INTEGER counter{};
        const BOOL ok = ::QueryPerformanceCounter(&counter);
        if (ok == FALSE)
        {
            return Math::TimeSpan::zero();
        }

        // ticks->ns
        int64_t ns = ticks_to_ns(static_cast<std::int64_t>(counter.QuadPart), query_frequency_hz());
        return Math::TimeSpan{ ns, Math::TimeUnit::nanoseconds };
    }

    std::int64_t WinQpcClock::query_frequency_hz() noexcept
    {
        // キャッシュ
        static std::int64_t cachedFrequency = []() noexcept -> std::int64_t
            {
                LARGE_INTEGER frequency{};
                const BOOL ok = ::QueryPerformanceFrequency(&frequency);
                if (ok == FALSE)
                {
                    return 0;
                }
                return static_cast<std::int64_t>(frequency.QuadPart);
            }();

        return cachedFrequency;
    }

    std::int64_t WinQpcClock::ticks_to_ns(std::int64_t a_ticks, std::int64_t a_frequency) noexcept
    {
        // 異常系（割り算事故防止）
        if (a_frequency <= 0)
        {
            return 0;
        }

        // オーバーフロー回避：秒 + 余りで換算
        constexpr std::int64_t kNsPerSec = 1000000000LL;

        const std::int64_t sec = a_ticks / a_frequency;
        const std::int64_t rem = a_ticks % a_frequency;

        // rem*kNsPerSec を 64bit で計算してナノ秒へ変換する
        const std::int64_t ns = (sec * kNsPerSec) + ((rem * kNsPerSec) / a_frequency);
        return ns;
    }
}
