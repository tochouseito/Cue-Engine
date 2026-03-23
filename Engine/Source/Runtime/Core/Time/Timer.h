#pragma once

// === C++ includes ===
#include <cstddef>

// === Core includes ===
#include "IClock.h"

namespace Cue::Core::Time
{
    /// @brief クロックを使って経過時間を計測するタイマーです。
    class Timer final
    {
    public:
        /// @brief クロック参照でタイマーを初期化します。
        /// @param a_clock 計測に使うクロックです。
        explicit Timer(const IClock& a_clock) noexcept
            : m_clock(&a_clock)
        {
            // 1) 初期化
            reset();
        }

        /// @brief 計測状態を初期化します。
        void reset() noexcept
        {
            // 1) 状態初期化
            m_running = false;
            m_elapsed = Math::TimeSpan::zero();
            m_start = Math::TimeSpan::zero();
            m_last = m_clock->now_ns();
        }

        /// @brief タイマー計測を開始します。
        void start() noexcept
        {
            // 1) すでに動いているなら何もしない
            if (m_running)
            {
                return;
            }

            // 2) 開始Tickを記録
            m_start = m_clock->now_ns();
            m_running = true;
        }

        /// @brief タイマー計測を停止します。
        void stop() noexcept
        {
            // 1) 動いていないなら何もしない
            if (!m_running)
            {
                return;
            }

            // 2) 経過を加算して停止
            const Math::TimeSpan nowTick = m_clock->now_ns();
            m_elapsed += (nowTick - m_start);
            m_running = false;
        }

        /// @brief 現在動作中かを返します。
        /// @return 動作中なら `true` です。
        bool is_running() const noexcept
        {
            // 1) 動作中フラグを返す
            return m_running;
        }

        /// @brief 経過時間を tick 単位で返します。
        /// @return 積算経過時間です。
        Math::TimeSpan elapsed_ticks() const noexcept
        {
            // 1) 既に積算した分を基準にする
            Math::TimeSpan total = m_elapsed;

            // 2) 動作中なら現在までの差分を足す
            if (m_running)
            {
                const Math::TimeSpan nowTick = m_clock->now_ns();
                total += (nowTick - m_start);
            }

            return total;
        }

        /// @brief 経過時間を秒で返します。
        /// @return 積算経過秒です。
        double elapsed_seconds() const noexcept
        {
            // 1) Tickを取得
            const Math::TimeSpan ticks = elapsed_ticks();

            // 2) double秒へ
            return ticks.s_f64();
        }

        /// @brief 前回呼び出しからの差分 tick を返します。
        /// @return 差分時間です。
        Math::TimeSpan lap_ticks() noexcept
        {
            // 1) 現在Tickを取得
            const Math::TimeSpan nowTick = m_clock->now_ns();

            // 2) 前回との差分を計算して更新
            const Math::TimeSpan dt = nowTick - m_last;
            m_last = nowTick;

            return dt;
        }

        /// @brief 前回呼び出しからの差分秒を返します。
        /// @return 差分秒です。
        double lap_seconds() noexcept
        {
            // 1) 差分Tickを得る
            const Math::TimeSpan dt = lap_ticks();

            // 2) double秒へ
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
