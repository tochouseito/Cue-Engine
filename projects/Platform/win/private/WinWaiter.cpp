#include "win_pch.h"
#include "WinWaiter.h"

#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
#include <intrin.h>
#endif

namespace Cue::Platform::Win
{
    namespace
    {
        constexpr int64_t ns_to_100ns_ceil(int64_t ns) noexcept
        {
            // 1) 0以下は0
            if (ns <= 0)
            {
                return 0;
            }

            // 2) 100ns単位へ切り上げ（(ns + 99) / 100）
            return (ns + 99) / 100;
        }

        constexpr uint32_t ns_to_ms_ceil_u32(int64_t ns) noexcept
        {
            // 1) 0以下は0
            if (ns <= 0)
            {
                return 0;
            }

            // 2) msへ切り上げ（(ns + 999,999) / 1,000,000）
            const int64_t ms64 = (ns + 999'999) / 1'000'000;

            // 3) uint32範囲に丸め（Sleepはuint32で十分）
            if (ms64 <= 0)
            {
                return 0;
            }
            if (ms64 > static_cast<int64_t>(UINT32_MAX))
            {
                return UINT32_MAX;
            }
            return static_cast<uint32_t>(ms64);
        }
    }

    WinWaiter::WinWaiter(Core::Time::IClock* clock) noexcept
        : m_clock(clock)
    {
        // 1) タイマーオブジェクト作成
        m_timer = ::CreateWaitableTimerExW(
            nullptr,
            nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);
    }
    WinWaiter::~WinWaiter() noexcept
    {
        // 1) ハンドル破棄
        if (m_timer != nullptr)
        {
            ::CloseHandle(m_timer);
            m_timer = nullptr;
        }
    }
    void WinWaiter::sleep_for(Math::TimeSpan duration) noexcept
    {
        // 1) 負値/0は無視
        if (duration.value <= 0)
        {
            return;
        }

        // 2) WaitableTimer があるなら ns を直接（100ns単位へ切り上げ）
        //    ※ここがキモ：1ms未満でもブロックできる。スピン不要。
        if (m_timer != nullptr)
        {
            const int64_t hundredNs = ns_to_100ns_ceil(duration.nano());
            if (hundredNs > 0)
            {
                LARGE_INTEGER due{};
                due.QuadPart = -hundredNs; // negative = relative

                const BOOL okSet = ::SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE);
                if (okSet != FALSE)
                {
                    (void)::WaitForSingleObject(m_timer, INFINITE);
                    return;
                }
            }
        }

        // 3) フォールバック：Sleep(ms) しかないので ms に切り上げて必ず「それ以上」寝る
        const uint32_t ms = ns_to_ms_ceil_u32(duration.nano());
        if (ms == 0)
        {
            // 1ms未満でも Sleep(1) に丸める（std::this_thread の挙動に寄せる＝少なくとも寝る）
            ::Sleep(1);
            return;
        }

        sleep_for_coarse_ms(ms);
    }
    void WinWaiter::sleep_until(Math::TimeSpan targetTick) noexcept
    {
        // 1) clockがないのは設計ミスだが、例外禁止なので何もしない
        if (m_clock == nullptr)
        {
            return;
        }

        // 2) 現在時刻
        const Math::TimeSpan now = m_clock->now_ns();

        // 3) 既に過ぎているなら終わり
        if (targetTick <= now)
        {
            return;
        }

        // 4) 残りを1回だけ寝る（ループ禁止）
        const int64_t remaining = targetTick.nano() - now.nano();
        sleep_for(Math::TimeSpan{ remaining, Math::TimeUnit::nanoseconds });
    }
    void WinWaiter::relax() noexcept
    {
        // 1) MSVC x86/x64
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
        _mm_pause();

        // 2) Clang/GCC x86/x64
#elif (defined(__clang__) || defined(__GNUC__)) && (defined(__i386__) || defined(__x86_64__))
        __builtin_ia32_pause();

        // 3) ARM (Androidなど)
#elif defined(__aarch64__) || defined(__arm__)
    // ARM の "yield" 相当。コンパイラ/環境で差があるので asm で固定。
        __asm__ __volatile__("yield");

        // 4) フォールバック
#else
    // 何もしない（最悪これでも動く）
#endif
    }

    void WinWaiter::sleep_for_coarse_ms(uint32_t ms) noexcept
    {
        // 1) 0は何もしない
        if (ms == 0)
        {
            return;
        }

        // 2) WaitableTimer があるならそちら（相対時間・100ns単位）
        if (m_timer != nullptr)
        {
            LARGE_INTEGER due{};
            const int64_t hundredNs = -static_cast<int64_t>(ms) * 10'000LL; // negative = relative
            due.QuadPart = hundredNs;

            const BOOL okSet = ::SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE);
            if (okSet != FALSE)
            {
                (void)::WaitForSingleObject(m_timer, INFINITE);
                return;
            }
        }

        // 3) フォールバック
        ::Sleep(ms);
    }
}
