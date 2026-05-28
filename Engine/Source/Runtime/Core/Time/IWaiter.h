#pragma once

/// *********************************************************************************
/// 待機インターフェース
/// *********************************************************************************

// === Math includes ===
#include <TimeUnit.h>

namespace Cue::Core::Time
{
    /// @brief 待機処理を抽象化するインターフェース
    class IWaiter
    {
    public:
        virtual ~IWaiter() noexcept = default;

        /// @brief 指定時間だけ待機
        virtual void sleep_for(Math::TimeSpan a_duration) noexcept = 0;
        /// @brief 指定時刻まで待機
        virtual void sleep_until(Math::TimeSpan a_targetTick) noexcept = 0;
        /// @brief 短時間の待機緩和処理を行う
        virtual void relax() noexcept = 0;
    };
}
