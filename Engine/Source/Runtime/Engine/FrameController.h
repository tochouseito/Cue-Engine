#pragma once

/// ************************************************************************************
/// フレームコントローラー
/// ************************************************************************************
///   Fixed        : 順序と安定性優先。ゲーム本体向き。
///   Mailbox      : 最新表示と低遅延優先。古いフレームを捨ててもよい用途向き
///   Backpressure : 単純さと過剰先行防止優先。デバッグや安全側の実装向き

// === Core include ===
#include <Threading/IThread.h>
#include <Threading/IThreadFactory.h>
#include <Time/FrameCounter.h>
#include <Time/IClock.h>

// === C++ includes ===
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace Cue
{
    using updateFunc = std::function<void(uint64_t, uint32_t)>;
    using renderFunc = std::function<void(uint64_t, uint32_t)>;
    using presentFunc = std::function<void(uint64_t, uint32_t)>;
    using safePointFunc = std::function<void()>;

    enum class ControllerMode : uint32_t
    {
        Fixed,
        Mailbox,
        Backpressure,
    };

    struct FrameControllerDesc final
    {
        FrameControllerDesc(const uint32_t& a_bufferCount) : bufferCount(a_bufferCount)
        {}
        const uint32_t& bufferCount;
        uint32_t maxFps = 60;
        ControllerMode mode = ControllerMode::Fixed;
    };

    class FrameJob final
    {
    public:
        using jobFunc = std::function<void(uint64_t, uint32_t)>;

        /// @brief スレッド開始
        bool start(Core::Threading::IThreadFactory& a_factory, const Core::Time::IClock& a_clock, const char* a_name,
            jobFunc a_func);

        /// @brief 実行要求投入
        void kick(uint64_t frameNo, uint32_t index);

        /// @brief 完了フレーム取得
        uint64_t get_finished_frame() const;

        /// @brief 直近実行時間取得
        double get_last_elapsed_ms() const;

        /// @brief キューと実行中処理を含めて完全に空なら true
        bool is_idle() const;

        /// @brief 停止
        void stop();

    private:
        struct Request final
        {
            uint64_t frameNo = 0;
            uint32_t index = 0;
        };

        static uint32_t thread_entry(Core::Threading::StopToken a_token, void* a_user) noexcept;

        uint32_t thread_loop(Core::Threading::StopToken a_token) noexcept;

        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::unique_ptr<Core::Threading::IThread> m_thread;
        std::deque<Request> m_queue;
        jobFunc m_func;
        const Core::Time::IClock* m_clock = nullptr;
        double m_lastElapsedMs = 0.0;
        uint64_t m_finishedFrame = UINT64_MAX;
        bool m_isExecuting = false;
        bool m_exit = false;
    };

    class FrameController final
    {
    public:
        /// @brief 生成
        FrameController(const FrameControllerDesc& config, Core::Threading::IThreadFactory& a_threadFactory,
            const Core::Time::IClock& a_clock, Core::Time::IWaiter& a_waiter,
            const updateFunc& a_updateFunc, const renderFunc& a_renderFunc,
            const presentFunc& a_presentFunc, const safePointFunc& a_safePointFunc)
            : m_desc(config), m_threadFactory(a_threadFactory), m_clock(a_clock), m_waiter(a_waiter),
            m_frameCounter(a_clock, a_waiter), m_updateFunc(a_updateFunc), m_renderFunc(a_renderFunc),
            m_presentFunc(a_presentFunc), m_safePointFunc(a_safePointFunc)
        {
            // 初期化はメンバ初期化リストで完結させる
        }

        /// @brief 破棄
        ~FrameController();

        /// @brief 1 ステップ進行
        void step();
        /// @brief リサイズ要求反映
        void poll_resize_request();
        /// @brief 実行中フレームを吐き切る
        void synchronize();

        /// @brief frame counter 取得
        Core::Time::FrameCounter& frame_counter() noexcept
        {
            return m_frameCounter;
        }

        /// @brief 総フレーム数取得
        uint64_t total_frame() const noexcept
        {
            return m_frameCounter.total_frames();
        }
        /// @brief update index 取得
        uint32_t update_index() const noexcept
        {
            return m_updateIndex;
        }
        /// @brief render index 取得
        uint32_t render_index() const noexcept
        {
            return m_renderIndex;
        }
        /// @brief present index 取得
        uint32_t present_index() const noexcept
        {
            return m_presentIndex;
        }
        /// @brief update 実行時間取得
        double update_elapsed_ms() const noexcept;
        /// @brief render 実行時間取得
        double render_elapsed_ms() const noexcept;
        /// @brief step 全体の実行時間取得
        double step_elapsed_ms() const noexcept;
        /// @brief present 実行時間取得
        double present_elapsed_ms() const noexcept;
        /// @brief frame counter tick 実行時間取得
        double counter_tick_elapsed_ms() const noexcept;
        /// @brief 直近 present までに step した回数取得
        uint32_t steps_per_present() const noexcept;

    private:
        class ControllerState
        {
        public:
            virtual ~ControllerState() = default;
            virtual void reset() noexcept = 0;
            virtual bool step(FrameController& a_controller) = 0;
        };

        class FixedState final : public ControllerState
        {
        public:
            void reset() noexcept override;
            bool step(FrameController& a_controller) override;

        private:
            uint64_t m_produceFrame = 0;
            uint64_t m_renderFrame = 0;
            uint64_t m_totalFrame = 0;
        };

        class MailboxState final : public ControllerState
        {
        public:
            void reset() noexcept override;
            bool step(FrameController& a_controller) override;

        private:
            uint64_t m_produceFrame = 0;
            uint64_t m_lastPresentedFrame = 0;
            bool m_hasPresented = false;
        };

        class BackpressureState final : public ControllerState
        {
        public:
            void reset() noexcept override;
            bool step(FrameController& a_controller) override;

        private:
            uint64_t m_currentFrame = 0;
            bool m_isInFlight = false;
        };

        class SingleBufferState final : public ControllerState
        {
        public:
            void reset() noexcept override;
            bool step(FrameController& a_controller) override;

        private:
            uint64_t m_currentFrame = 0;
        };

        /// @brief パイプライン起動
        bool start_pipeline();

        /// @brief ジョブ停止
        void stop_jobs();

        /// @brief 各段 index 計算
        void compute_indices(uint64_t frameNo, uint32_t bufferCount, uint32_t& updateIndex, uint32_t& renderIndex,
            uint32_t& presentIndex);

        /// @brief present 実行
        void present_frame(uint64_t frameNo);

        /// @brief 次フレーム向けリサイズ反映
        void apply_resize_for_next_frame(uint64_t nextFrameNo);

        /// @brief safe point で保留中のリサイズを反映
        void apply_pending_resize(uint64_t nextFrameNo, bool a_shouldFillBuffers);

        /// @brief バッファ初期充填
        void fill_buffers(uint64_t frameNo);

        /// @brief 設定から使用する状態を選ぶ
        ControllerState& select_state() noexcept;

        /// @brief 指定フレームのジョブが完了済みかを返す
        static bool is_frame_finished(uint64_t a_finishedFrame, uint64_t a_frameNo) noexcept;

        FrameControllerDesc m_desc;
        Core::Threading::IThreadFactory& m_threadFactory;
        const Core::Time::IClock& m_clock;
        Core::Time::IWaiter& m_waiter;
        Core::Time::FrameCounter m_frameCounter;
        uint32_t m_backBufferBase = 0;
        std::atomic<bool> m_resizePending{ false };
        updateFunc m_updateFunc;
        renderFunc m_renderFunc;
        presentFunc m_presentFunc;
        safePointFunc m_safePointFunc;
        FrameJob m_updateJob;
        FrameJob m_renderJob;
        FixedState m_fixedState{};
        MailboxState m_mailboxState{};
        BackpressureState m_backpressureState{};
        SingleBufferState m_singleState{};
        ControllerState* m_activeState = nullptr;
        uint64_t m_maxLead = 0;
        bool m_started = false;
        bool m_finished = false;
        double m_updateElapsedMs = 0.0;
        double m_renderElapsedMs = 0.0;
        double m_stepElapsedMs = 0.0;
        double m_presentElapsedMs = 0.0;
        double m_counterTickElapsedMs = 0.0;
        uint32_t m_stepsSincePresent = 0;
        uint32_t m_stepsPerPresent = 0;
        uint32_t m_updateIndex = 0;
        uint32_t m_renderIndex = 0;
        uint32_t m_presentIndex = 0;
    };
} // namespace Cue

