#pragma once

// === C++ Standard Library ===
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

// === Engine ===
#include <Threading/IThread.h>
#include <Threading/IThreadFactory.h>
#include <Time/FrameCounter.h>

namespace Cue
{
    using UpdateFunc = std::function<void(uint64_t, uint32_t)>;
    using RenderFunc = std::function<void(uint64_t, uint32_t)>;
    using PresentFunc = std::function<void(uint64_t, uint32_t)>;

    enum class ControllerMode
    {
        Fixed,
        Mailbox,
        Backpressure,
    };

    struct FrameControllerDesc final
    {
        uint32_t m_bufferCount = 1;
        uint32_t m_maxFps = 60;
        ControllerMode m_mode = ControllerMode::Fixed;
    };

    class FrameJob final
    {
    public:
        using JobFunc = std::function<void(uint64_t, uint32_t)>;
        using ThreadFactory = Core::Threading::IThreadFactory;
        using Thread = Core::Threading::IThread;
        using StopToken = Core::Threading::StopToken;

        bool start(ThreadFactory& factory, const char* name, JobFunc func);

        void kick(uint64_t frameNo, uint32_t index);

        uint64_t get_finished_frame() const;

        void stop();

    private:
        struct Request final
        {
            uint64_t m_frameNo = 0;
            uint32_t m_index = 0;
        };

        static uint32_t thread_entry(StopToken token, void* user) noexcept;

        uint32_t thread_loop(StopToken token) noexcept;

        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::unique_ptr<Thread> m_thread;
        std::deque<Request> m_queue;
        JobFunc m_func;
        uint64_t m_finishedFrame = 0;
        bool m_exit = false;
    };

    class FrameController final
    {
        using Clock = Core::Time::IClock;
        using IWaiter = Core::Time::IWaiter;
        using ThreadFactory = Core::Threading::IThreadFactory;
    public:
        FrameController(const FrameControllerDesc& config, ThreadFactory& threadFactory, const Clock& clock, IWaiter& waiter,
            const UpdateFunc& updateFunc, const RenderFunc& renderFunc, const PresentFunc& presentFunc)
            : m_config(config)
            , m_threadFactory(threadFactory)
            , m_waiter(waiter)
            , m_frameCounter(clock, waiter)
            , m_updateFunc(updateFunc)
            , m_renderFunc(renderFunc)
            , m_presentFunc(presentFunc)
        {
            // 1) 初期化はメンバ初期化リストで完結させる
        }

        ~FrameController();

        void step();
        void poll_resize_request();

        Core::Time::FrameCounter& frame_counter() noexcept
        {
            return m_frameCounter;
        }

        uint64_t total_frame() const noexcept
        {
            return m_frameCounter.total_frames();
        }
        uint32_t update_index() const noexcept
        {
            return m_updateIndex;
        }
        uint32_t render_index() const noexcept
        {
            return m_renderIndex;
        }
        uint32_t present_index() const noexcept
        {
            return m_presentIndex;
        }

    private:
        struct FixedState final
        {
            uint64_t m_produceFrame = 0;
            uint64_t m_totalFrame = 0;
        };

        struct MailboxState final
        {
            uint64_t m_produceFrame = 0;
            uint64_t m_lastPresentedFrame = 0;
            bool m_hasPresented = false;
        };

        struct BackpressureState final
        {
            uint64_t m_currentFrame = 0;
            bool m_inFlight = false;
        };

        struct SingleBufferState final
        {
            uint64_t m_currentFrame = 0;
        };

        bool start_pipeline();
        void stop_jobs();

        void compute_indices(uint64_t frameNo, uint32_t bufferCount, uint32_t& updateIndex,
            uint32_t& renderIndex, uint32_t& presentIndex);

        void present_frame(uint64_t frameNo);

        void apply_resize_for_next_frame(uint64_t nextFrameNo);

        void fill_buffers(uint64_t frameNo);

        bool step_single_buffer();

        bool step_fixed();

        bool step_mailbox();

        bool step_backpressure();

        FrameControllerDesc m_config;
        Core::Threading::IThreadFactory& m_threadFactory;
        Core::Time::IWaiter& m_waiter;
        Core::Time::FrameCounter m_frameCounter;
        uint32_t m_backBufferBase = 0;
        std::atomic<bool> m_resizePending{ false };
        UpdateFunc m_updateFunc;
        RenderFunc m_renderFunc;
        PresentFunc m_presentFunc;
        FrameJob m_updateJob;
        FrameJob m_renderJob;
        FixedState m_fixedState{};
        MailboxState m_mailboxState{};
        BackpressureState m_backpressureState{};
        SingleBufferState m_singleState{};
        uint64_t m_maxLead = 0;
        bool m_started = false;
        bool m_finished = false;
        uint32_t m_updateIndex = 0;
        uint32_t m_renderIndex = 0;
        uint32_t m_presentIndex = 0;
    };
}
