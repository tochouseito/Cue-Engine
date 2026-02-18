#pragma once
#include <Time/IWaiter.h>
#include <Time/IClock.h>

namespace Cue::Platform::Win
{
    class WinWaiter final : public Core::Time::IWaiter
    {
    public:
        explicit WinWaiter(Core::Time::IClock* clock) noexcept;
        ~WinWaiter() noexcept override;

        void sleep_for(Math::TimeSpan duration) noexcept override;
        void sleep_until(Math::TimeSpan targetTick) noexcept override;
        void relax() noexcept override;
    private:
        void sleep_for_coarse_ms(uint32_t ms) noexcept;
    private:
        Core::Time::IClock* m_clock = nullptr; // 非所有（WinQpcClockを想定）
        HANDLE m_timer = nullptr;
    };
}
