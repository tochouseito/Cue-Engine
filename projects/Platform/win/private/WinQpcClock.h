#pragma once
#include <cstdint>
#include <Time/IClock.h>

namespace Cue::Platform::Win
{
    class WinQpcClock final : public Core::Time::IClock
    {
    public:
        WinQpcClock() = default;
        ~WinQpcClock() override = default;

        // ナノ秒単位の現在時刻を取得する
        [[nodiscard]] std::int64_t now_ns() const noexcept override;

    private:
        [[nodiscard]] static std::int64_t query_frequency_hz() noexcept;

        [[nodiscard]] static std::int64_t ticks_to_ns(const std::int64_t ticks, const std::int64_t freq) noexcept;
    };
}
