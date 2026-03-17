#pragma once

// c++ 標準ライブラリ include
#include <cstdint>

// core 関連 include
#include "IClock.h"
#include "IWaiter.h"
#include "Timer.h"
#include <TimeUnit.h>

namespace Cue::Core::Time
{
    /// @brief フレームカウンター
    class FrameCounter final
    {
    public:
        explicit FrameCounter(const IClock& clock, IWaiter& waiter) noexcept
            : m_clock(&clock)
            , m_timer(clock)
            , m_waiter(&waiter)
        {
            // 1) 初期化
            m_timer.reset();
        }

        ~FrameCounter() noexcept = default;

        // --------------------
        // 主 api
        // --------------------
        void tick() noexcept
        {
            // 1) 初回のみ：基準点だけ作る
            if (m_initialized == false)
            {
                m_timer.reset();
                m_capBaseTick = m_clock->now_ns();
                m_initialized = true;
                return;
            }

            // 2) fps 制限待機
            if (m_maxFps > 0)
            {
                cap_fps_();
            }

            // 3) 待機込み delta 計測
            //    lap_seconds() で内部基準点を更新
            m_deltaTime = m_timer.lap_seconds();
            if (m_deltaTime > 0.0)
            {
                m_fps = 1.0 / m_deltaTime;
            }
            else
            {
                m_fps = 0.0;
            }

            // 4) 統計更新
            m_totalFrames += 1;
            m_produceFrame += 1;

            // 5) 次回 cap 判定基準 tick 更新
            m_capBaseTick = m_clock->now_ns();
        }

        double delta_time() const noexcept
        {
            return m_deltaTime;
        }

        double fps() const noexcept
        {
            return m_fps;
        }

        void set_max_fps(std::uint32_t maxFps) noexcept
        {
            m_maxFps = maxFps;
        }

        void set_max_lead(std::uint32_t maxLead) noexcept
        {
            m_maxLead = maxLead;
        }

        std::uint32_t max_lead() const noexcept
        {
            return m_maxLead;
        }

        std::uint64_t total_frames() const noexcept
        {
            return m_totalFrames;
        }

        std::uint64_t produce_frame() const noexcept
        {
            return m_produceFrame;
        }

    private:
        void cap_fps_() noexcept
        {
            // 1) cap 無効時は終了
            if (m_maxFps == 0)
            {
                return;
            }

            // 2) 1 フレーム分 ns 計算
            const Math::TimeSpan frameNs = { static_cast<int64_t>((1'000'000'000.0 / static_cast<double>(m_maxFps)) + 0.5), Math::TimeUnit::nanoseconds };

            // 3) 低 fps 帯用スピン予算計算
            //    60 fps 付近では約 1 ms を最終スピンへ使用
            constexpr int64_t minSpinNs = 250'000LL;
            constexpr int64_t maxSpinNs = 1'000'000LL;
            int64_t spinBudgetNs = frameNs.nano() / 8;
            if (spinBudgetNs < minSpinNs)
            {
                spinBudgetNs = minSpinNs;
            }
            else if (spinBudgetNs > maxSpinNs)
            {
                spinBudgetNs = maxSpinNs;
            }
            const Math::TimeSpan spinNs = { spinBudgetNs, Math::TimeUnit::nanoseconds };

            // 4) 今フレーム目標時刻計算
            const Math::TimeSpan now0 = m_clock->now_ns();
            const Math::TimeSpan targetTick = m_capBaseTick + frameNs;

            // 5) 遅延時は待機省略
            if (now0 >= targetTick)
            {
                return;
            }

            // 6) 高 fps 帯は sleep を省略
            //    2 ms 以下はフルスピンへ切替
            constexpr int64_t fullSpinThresholdNs = 2'000'000LL;
            if (frameNs.nano() > fullSpinThresholdNs)
            {
                const Math::TimeSpan sleepUntilNs = targetTick - spinNs;
                if (sleepUntilNs > now0)
                {
                    m_waiter->sleep_until(sleepUntilNs);
                }
            }

            // 7) 目標時刻まで短スピン
            while (m_clock->now_ns() < targetTick)
            {
                m_waiter->relax();
            }
        }

    private:
        const IClock* m_clock = nullptr;
        IWaiter* m_waiter = nullptr;
        Timer m_timer;

        bool m_initialized = false;

        // fps cap 判定基準 tick
        Math::TimeSpan m_capBaseTick = Math::TimeSpan::zero();

        double m_deltaTime = 0.0;
        double m_fps = 0;

        std::uint32_t m_maxFps = 60;
        std::uint32_t m_maxLead = 0; // 2枚→1, 3枚→2

        std::uint64_t m_totalFrames = 0;
        std::uint64_t m_produceFrame = 0;
    };
} // 名前空間 cue::core::time
