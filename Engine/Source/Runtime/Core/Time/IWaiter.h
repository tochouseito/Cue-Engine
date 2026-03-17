#pragma once
#include <cstdint>
#include <TimeUnit.h>

namespace Cue::Core::Time
{
    class IWaiter
    {
    public:
        virtual ~IWaiter() noexcept = default;

        virtual void sleep_for(Math::TimeSpan duration) noexcept = 0;
        virtual void sleep_until(Math::TimeSpan targetTick) noexcept = 0;
        virtual void relax() noexcept = 0;
    };
}
