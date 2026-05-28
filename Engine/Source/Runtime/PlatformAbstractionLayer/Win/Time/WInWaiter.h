#pragma once

/// *********************************************************************************
/// Windows 待機処理
/// *********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <Time/IClock.h>
#include <Time/IWaiter.h>

// === Windows API includes ===
#include "stdafx.h"

namespace Cue::PAL::Win
{
    /// @brief Windows の待機プリミティブを使う待機実装
    class WinWaiter : public Core::Time::IWaiter
    {
    public:
        /// @brief クロック参照で待機実装を初期化
        explicit WinWaiter(Core::Time::IClock& a_clock) noexcept;
        ~WinWaiter() noexcept override;

        // --- sleep 操作 ---
        void sleep_for(Math::TimeSpan a_duration) noexcept override;
        void sleep_until(Math::TimeSpan a_targetTick) noexcept override;
        void relax() noexcept override;

    private:
        void sleep_for_coarse_ms(uint32_t a_milliseconds) noexcept;

    private:
        Core::Time::IClock& m_clock;
        HANDLE m_timer = nullptr;
    };
}
