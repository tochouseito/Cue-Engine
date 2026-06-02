#include "WinWaiter.h"

namespace Cue::PAL::Win
{
    namespace
    {
        constexpr int64_t ns_to_100ns_ceil(int64_t a_nanoseconds) noexcept
        {
            // 0以下は0
            if (a_nanoseconds <= 0)
            {
                return 0;
            }

            // 100ns単位へ切り上げ（(ns + 99) / 100）
            return (a_nanoseconds + 99) / 100;
        }

        constexpr uint32_t ns_to_ms_ceil_u32(int64_t a_nanoseconds) noexcept
        {
            // 0以下は0
            if (a_nanoseconds <= 0)
            {
                return 0;
            }

            // msへ切り上げ（(ns + 999,999) / 1,000,000）
            const int64_t ms64 = (a_nanoseconds + 999'999) / 1'000'000;

            // uint32範囲に丸め（Sleepはuint32で十分）
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

    WinWaiter::WinWaiter(Core::Time::IClock& a_clock) noexcept
        : m_clock(a_clock)
    {
        // タイマーオブジェクト作成
        m_timer = ::CreateWaitableTimerExW(
            nullptr,
            nullptr,
            CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);
    }

    WinWaiter::~WinWaiter() noexcept
    {
        // ハンドル破棄
        if (m_timer != nullptr)
        {
            ::CloseHandle(m_timer);
            m_timer = nullptr;
        }
    }

    void WinWaiter::sleep_for(Math::TimeSpan a_duration) noexcept
    {
        // 負値/0は無視
        if (a_duration.value <= 0)
        {
            return;
        }

        // WaitableTimer があるなら ns を直接（100ns単位へ切り上げ）
        // ※ここがキモ：1ms未満でもブロックできるスピン不要
        if (m_timer != nullptr)
        {
            const int64_t hundredNs = ns_to_100ns_ceil(a_duration.nano());
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

        // フォールバック：Sleep(ms) しかないので ms に切り上げて必ず「それ以上」寝る
        const uint32_t milliseconds = ns_to_ms_ceil_u32(a_duration.nano());
        if (milliseconds == 0)
        {
            // 1ms未満でも Sleep(1) に丸める（std::this_thread の挙動に寄せる＝少なくとも寝る）
            ::Sleep(1);
            return;
        }

        sleep_for_coarse_ms(milliseconds);
    }

    void WinWaiter::sleep_until(Math::TimeSpan a_targetTick) noexcept
    {
        // 現在時刻
        const Math::TimeSpan now = m_clock.now_ns();

        // 既に過ぎているなら終わり
        if (a_targetTick <= now)
        {
            return;
        }

        // 残りを1回だけ寝る（ループ禁止）
        const int64_t remaining = a_targetTick.nano() - now.nano();
        sleep_for(Math::TimeSpan{ remaining, Math::TimeUnit::nanoseconds });
    }

    void WinWaiter::relax() noexcept
    {
        // MSVC x86/x64
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
        _mm_pause();

        // Clang/GCC x86/x64
#elif (defined(__clang__) || defined(__GNUC__)) && (defined(__i386__) || defined(__x86_64__))
        __builtin_ia32_pause();

        // ARM (Androidなど)
#elif defined(__aarch64__) || defined(__arm__)
        // ARM の "yield" 相当コンパイラ/環境で差があるので asm で固定
        __asm__ __volatile__("yield");

        // フォールバック
#else
    // 何もしない（最悪これでも動く）
#endif
    }

    void WinWaiter::sleep_for_coarse_ms(uint32_t a_milliseconds) noexcept
    {
        // 0は何もしない
        if (a_milliseconds == 0)
        {
            return;
        }

        // WaitableTimer があるならそちら（相対時間・100ns単位）
        if (m_timer != nullptr)
        {
            LARGE_INTEGER due{};
            const int64_t hundredNs = -static_cast<int64_t>(a_milliseconds) * 10'000LL; // negative = relative
            due.QuadPart = hundredNs;

            const BOOL okSet = ::SetWaitableTimer(m_timer, &due, 0, nullptr, nullptr, FALSE);
            if (okSet != FALSE)
            {
                (void)::WaitForSingleObject(m_timer, INFINITE);
                return;
            }
        }

        // フォールバック
        ::Sleep(a_milliseconds);
    }

}
