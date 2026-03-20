#pragma once

// === Base Includes ===
#include <Result.h>

// === Core Includes ===
#include <Time/IClock.h>

// === Windows API Includes ===
#include "stdafx.h"

namespace Cue::PAL::Win
{
    class WinQpcClock : public Core::Time::IClock
    {
    public:
        WinQpcClock() noexcept = default;
        ~WinQpcClock() override = default;

        // ナノ秒単位の現在時刻を取得する
        [[nodiscard]] Math::TimeSpan now_ns() const noexcept override;
    private:
        [[nodiscard]] static std::int64_t query_frequency_hz() noexcept;
        [[nodiscard]] static std::int64_t ticks_to_ns(const std::int64_t ticks, const std::int64_t freq) noexcept;
    };
}
