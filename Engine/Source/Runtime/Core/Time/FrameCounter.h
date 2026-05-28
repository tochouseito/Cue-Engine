#pragma once

/// *********************************************************************************
/// フレームカウンター
/// *********************************************************************************

// === C++ includes ===
#include <cstdint>

// === Core includes ===
#include "IClock.h"
#include "IWaiter.h"

// === Math includes ===
#include <TimeUnit.h>

namespace Cue::Core::Time
{
    /// @brief フレームカウンター
    class FrameCounter final
    {
    public:
        /// @brief クロックと待機器でフレームカウンターを初期化
        explicit FrameCounter(const IClock& a_clock, IWaiter& a_waiter) noexcept
            : m_clock(&a_clock)
            , m_waiter(&a_waiter)
        {
            // 初回 tick で基準時刻を作る
        }

        ~FrameCounter() noexcept = default;

        // --- 主 API ---
        /// @brief 1 フレーム分の統計を更新
        void tick() noexcept
        {
            const Math::TimeSpan tickNow = m_clock->now_ns();

            if (m_initialized == false)
            {
                m_lastTick = tickNow;
                m_initialized = true;
                return;
            }

            const Math::TimeSpan previousTick = m_lastTick;

            Math::TimeSpan frameEndTick = tickNow;
            if (m_maxFps > 0)
            {
                frameEndTick = cap_fps_(previousTick, tickNow);
            }

            const Math::TimeSpan deltaTicks = frameEndTick - previousTick;
            m_lastTick = frameEndTick;
            m_deltaTime = deltaTicks.s_f64();
            if (m_deltaTime > 0.0)
            {
                m_fps = 1.0 / m_deltaTime;
            }
            else
            {
                m_fps = 0.0;
            }

            m_totalFrames += 1;
            m_produceFrame += 1;
        }

        /// @brief 前フレームからの経過秒を返す
        double delta_time() const noexcept
        {
            return m_deltaTime;
        }

        /// @brief 計測基準を未初期化状態へ戻す
        void reset() noexcept
        {
            m_initialized = false;
            m_lastTick = Math::TimeSpan::zero();
            m_deltaTime = 0.0;
            m_fps = 0.0;
            m_totalFrames = 0;
            m_produceFrame = 0;
        }

        /// @brief 現在の fps を返す
        double fps() const noexcept
        {
            return m_fps;
        }

        /// @brief 上限 fps を設定
        void set_max_fps(std::uint32_t a_maxFps) noexcept
        {
            m_maxFps = a_maxFps;
        }

        /// @brief 最大先行フレーム数を設定
        void set_max_lead(std::uint32_t a_maxLead) noexcept
        {
            m_maxLead = a_maxLead;
        }

        /// @brief 最大先行フレーム数を返す
        std::uint32_t max_lead() const noexcept
        {
            return m_maxLead;
        }

        /// @brief 総フレーム数を返す
        std::uint64_t total_frames() const noexcept
        {
            return m_totalFrames;
        }

        /// @brief 生成済みフレーム数を返す
        std::uint64_t produce_frame() const noexcept
        {
            return m_produceFrame;
        }

    private:
        Math::TimeSpan cap_fps_(Math::TimeSpan a_baseTick, Math::TimeSpan a_now) noexcept
        {
            if (m_maxFps == 0)
            {
                return a_now;
            }

            const Math::TimeSpan frameNs = { static_cast<int64_t>((1'000'000'000.0 / static_cast<double>(m_maxFps)) + 0.5), Math::TimeUnit::nanoseconds };

            // 60 fps 付近では約 1 ms を最終スピンへ使用する
            constexpr int64_t k_minSpinNs = 250'000LL;
            constexpr int64_t k_maxSpinNs = 1'000'000LL;
            int64_t spinBudgetNs = frameNs.nano() / 8;
            if (spinBudgetNs < k_minSpinNs)
            {
                spinBudgetNs = k_minSpinNs;
            }
            else if (spinBudgetNs > k_maxSpinNs)
            {
                spinBudgetNs = k_maxSpinNs;
            }
            const Math::TimeSpan spinNs = { spinBudgetNs, Math::TimeUnit::nanoseconds };

            const Math::TimeSpan targetTick = a_baseTick + frameNs;

            if (a_now >= targetTick)
            {
                return a_now;
            }

            // 2 ms 以下の高 fps 帯では sleep を省略してフルスピンへ切り替える
            constexpr int64_t k_fullSpinThresholdNs = 2'000'000LL;
            if (frameNs.nano() > k_fullSpinThresholdNs)
            {
                const Math::TimeSpan sleepUntilNs = targetTick - spinNs;
                if (sleepUntilNs > a_now)
                {
                    m_waiter->sleep_until(sleepUntilNs);
                }
            }

            while (m_clock->now_ns() < targetTick)
            {
                m_waiter->relax();
            }

            return m_clock->now_ns();
        }

    private:
        const IClock* m_clock = nullptr;
        IWaiter* m_waiter = nullptr;

        bool m_initialized = false;

        // fps / delta 計測用の前回 tick 入口
        Math::TimeSpan m_lastTick = Math::TimeSpan::zero();

        double m_deltaTime = 0.0;
        double m_fps = 0;

        std::uint32_t m_maxFps = 60;
        std::uint32_t m_maxLead = 0; // 2枚→1, 3枚→2

        std::uint64_t m_totalFrames = 0;
        std::uint64_t m_produceFrame = 0;
    };
} // 名前空間 cue::core::time
