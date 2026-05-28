// Timer の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <cstddef>

// === Core includes ===
#include "IClock.h"

namespace Cue::Core::Time
{
    /// @brief クロックを使って経過時間を計測するタイマー
    class Timer final
    {
    public:
        /// @brief クロック参照でタイマーを初期化する
        /// @param a_clock 計測に使うクロック
        explicit Timer(const IClock& a_clock) noexcept
            : m_clock(&a_clock)
        {
            // - 初期化
            reset();
        }

        /// @brief 計測状態を初期化する
        void reset() noexcept
        {
            // - 状態初期化
            m_running = false;
            m_elapsed = Math::TimeSpan::zero();
            m_start = Math::TimeSpan::zero();
            m_last = m_clock->now_ns();
        }

        /// @brief タイマー計測を開始し
        void start() noexcept
        {
            // - すでに動いているなら何もしない
            if (m_running)
            {
                return;
            }

            // - 開始Tickを記録
            m_start = m_clock->now_ns();
            m_running = true;
        }

        /// @brief タイマー計測を停止し
        void stop() noexcept
        {
            // - 動いていないなら何もしない
            if (!m_running)
            {
                return;
            }

            // - 経過を加算して停止
            const Math::TimeSpan nowTick = m_clock->now_ns();
            m_elapsed += (nowTick - m_start);
            m_running = false;
        }

        /// @brief 現在動作中かを返す
        /// @return 動作中なら `true` 
        bool is_running() const noexcept
        {
            // - 動作中フラグを返す
            return m_running;
        }

        /// @brief 経過時間を tick 単位で返す
        /// @return 積算経過時間
        Math::TimeSpan elapsed_ticks() const noexcept
        {
            // - 既に積算した分を基準にする
            Math::TimeSpan total = m_elapsed;

            // - 動作中なら現在までの差分を足す
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
            // - Tickを取得
            const Math::TimeSpan ticks = elapsed_ticks();

            // - double秒へ
            return ticks.s_f64();
        }

        /// @brief 前回呼び出しからの差分 tick を返す
        /// @return 差分時間
        Math::TimeSpan lap_ticks() noexcept
        {
            // - 現在Tickを取得
            const Math::TimeSpan nowTick = m_clock->now_ns();

            // - 前回との差分を計算して更新
            const Math::TimeSpan dt = nowTick - m_last;
            m_last = nowTick;

            return dt;
        }

        /// @brief 前回呼び出しからの差分秒を返す
        /// @return 差分秒
        double lap_seconds() noexcept
        {
            // - 差分Tickを得る
            const Math::TimeSpan dt = lap_ticks();

            // - double秒へ
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
