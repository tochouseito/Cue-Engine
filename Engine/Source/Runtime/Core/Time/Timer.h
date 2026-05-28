#pragma once

/// *********************************************************************************
/// タイマー
/// *********************************************************************************

// === Math includes ===
#include <TimeUnit.h>

// === Core includes ===
#include "IClock.h"

// === C++ includes ===
#include <cstddef>

namespace Cue::Core::Time
{
    /// @brief クロックを使って経過時間を計測するタイマー
    class Timer final
    {
    public:
        /// @brief クロック参照でタイマーを初期化
        /// @param a_clock 計測に使うクロック
        explicit Timer(const IClock& a_clock) noexcept
            : m_clock(&a_clock)
        {
            reset();
        }

        /// @brief 計測状態を初期化
        void reset() noexcept
        {
            m_running = false;
            m_elapsed = Math::TimeSpan::zero();
            m_start = Math::TimeSpan::zero();
            m_last = m_clock->now_ns();
        }

        /// @brief タイマー計測を開始
        void start() noexcept
        {
            if (m_running)
            {
                return;
            }

            m_start = m_clock->now_ns();
            m_running = true;
        }

        /// @brief タイマー計測を停止
        void stop() noexcept
        {
            if (!m_running)
            {
                return;
            }

            const Math::TimeSpan nowTick = m_clock->now_ns();
            m_elapsed += (nowTick - m_start);
            m_running = false;
        }

        /// @brief 現在動作中かを返す
        /// @return 動作中なら `true`
        bool is_running() const noexcept
        {
            return m_running;
        }

        /// @brief 経過時間を tick 単位で返す
        /// @return 積算経過時間
        Math::TimeSpan elapsed_ticks() const noexcept
        {
            Math::TimeSpan total = m_elapsed;

            if (m_running)
            {
                const Math::TimeSpan nowTick = m_clock->now_ns();
                total += (nowTick - m_start);
            }

            return total;
        }

        /// @brief 経過時間を秒で返す
        /// @return 積算経過秒
        double elapsed_seconds() const noexcept
        {
            const Math::TimeSpan ticks = elapsed_ticks();

            return ticks.s_f64();
        }

        /// @brief 前回呼び出しからの差分 tick を返す
        /// @return 差分時間
        Math::TimeSpan lap_ticks() noexcept
        {
            const Math::TimeSpan nowTick = m_clock->now_ns();

            const Math::TimeSpan dt = nowTick - m_last;
            m_last = nowTick;

            return dt;
        }

        /// @brief 前回呼び出しからの差分秒を返す
        /// @return 差分秒
        double lap_seconds() noexcept
        {
            const Math::TimeSpan dt = lap_ticks();

            return dt.s_f64();
        }

    private:
        const IClock* m_clock = nullptr;

        bool m_running = false;
        Math::TimeSpan m_start{};
        Math::TimeSpan m_elapsed{};

        Math::TimeSpan m_last{}; // lap用
    };
}
