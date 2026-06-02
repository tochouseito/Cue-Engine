#pragma once

/// *********************************************************************************
/// Windows QPC クロック
/// *********************************************************************************

// === C++ includes ===
#include <cstdint>

// === Core includes ===
#include <Time/IClock.h>

// === Windows API includes ===
#include "WinCommon.h"

namespace Cue::PAL::Win
{
    /// @brief Query Performance Counter ベースのクロック
    class WinQpcClock : public Core::Time::IClock
    {
    public:
        WinQpcClock() noexcept = default;
        ~WinQpcClock() override = default;

        /// @brief ナノ秒単位の現在時刻を返す
        [[nodiscard]] Math::TimeSpan now_ns() const noexcept override;

    private:
        [[nodiscard]] static std::int64_t query_frequency_hz() noexcept;
        [[nodiscard]] static std::int64_t ticks_to_ns(std::int64_t a_ticks, std::int64_t a_frequency) noexcept;
    };
}
