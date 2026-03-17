#pragma once

#include <cstddef>
#include "IClock.h"

namespace Cue::Core::Time
{
    class Timer final
    {
    public:
        explicit Timer(const IClock& clock) noexcept
            : m_clock(&clock)
        {
            // 1) 初期化
            reset();
        }

        void reset() noexcept
        {
            // 1) 状態初期化
            m_running = false;
            m_elapsed = Math::TimeSpan::zero();
            m_start = Math::TimeSpan::zero();
            m_last = m_clock->now_ns();
        }

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

        bool is_running() const noexcept
        {
            // 1) 動作中フラグを返す
            return m_running;
        }

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

        double elapsed_seconds() const noexcept
        {
            // 1) Tickを取得
            const Math::TimeSpan ticks = elapsed_ticks();

            // 2) double秒へ
            return ticks.s_f64();
        }

        // フレーム計測用：前回呼び出しからの差分Tick（動作中/停止中に関係なく計測）
        Math::TimeSpan lap_ticks() noexcept
        {
            // 1) 現在Tickを取得
            const Math::TimeSpan nowTick = m_clock->now_ns();

            // 2) 前回との差分を計算して更新
            const Math::TimeSpan dt = nowTick - m_last;
            m_last = nowTick;

            return dt;
        }

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
