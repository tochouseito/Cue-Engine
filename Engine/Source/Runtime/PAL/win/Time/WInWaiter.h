#pragma once

// === Base Includes ===
#include "Result.h"

// === Core Includes ===
#include <Time/IClock.h>
#include <Time/IWaiter.h>

// === Windows API Includes ===
#include "stdafx.h"

namespace Cue::PAL::Win
{
    class WinWaiter : public Core::Time::IWaiter
    {
    public:
        // 暗黙変換禁止
        explicit WinWaiter(Core::Time::IClock& clock) noexcept;
        ~WinWaiter() noexcept override;

        // --- sleep 操作 ---
        void sleep_for(Math::TimeSpan duration) noexcept override;
        void sleep_until(Math::TimeSpan targetTick) noexcept override;
        void relax() noexcept override;
    private:
        void sleep_for_coarse_ms(uint32_t ms) noexcept;
    private:
        Core::Time::IClock& m_clock;
        HANDLE m_timer = nullptr;
    };
}
