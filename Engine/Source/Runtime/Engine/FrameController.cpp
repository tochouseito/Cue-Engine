#include "FrameController.h"
#include <CueAssert.h>

namespace Cue
{
    bool FrameJob::start(ThreadFactory& factory, const char* name, JobFunc func)
    {
        // 実行関数をメンバへ保持する
        // 要求受付ループのスレッドを開始する
        m_func = std::move(func);
        m_exit = false;
        m_finishedFrame = 0;
        m_queue.clear();

        Core::Threading::ThreadDesc desc{};
        if (name != nullptr)
        {
            desc.name = name;
        }

        std::unique_ptr<Thread> th{};
        const auto result = factory.create_thread(desc, &FrameJob::thread_entry, this, th);
        if (!result)
        {
            return false;
        }

        m_thread = std::move(th);
        return true;
    }
    void FrameJob::kick(uint64_t frameNo, uint32_t index)
    {
        // 要求をキュー末尾へ追加する
        // 待機中スレッドを起こして遅延を抑える
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(Request{ frameNo, index });
        }
        m_cv.notify_one();
    }
    uint64_t FrameJob::get_finished_frame() const
    {
        // キュー操作をミューテックスで排他する
        // 進行判定に使う完了フレームを返す
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_finishedFrame;
    }
    void FrameJob::stop()
    {
        // 終了フラグを立ててループ停止を要求する
        // join して後処理中の競合を防ぐ
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_exit = true;
        }
        m_cv.notify_all();

        if (m_thread)
        {
            m_thread->request_stop();
            if (m_thread->joinable())
            {
                (void)m_thread->join();
            }
            m_thread.reset();
        }
    }
    uint32_t FrameJob::thread_entry(StopToken token, void* user) noexcept
    {
        // void* を安全に復元してインスタンスを得る
        // 共通のループ処理に委譲する
        auto* self = static_cast<FrameJob*>(user);
        if (!self)
        {
            return 0;
        }
        return self->thread_loop(token);
    }
    uint32_t FrameJob::thread_loop(StopToken token) noexcept
    {
        // 条件変数で待機する
        // 停止要求を優先して安全に終了する
        uint64_t currentFrame = 0;
        while (!token.stop_requested())
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&]()
                {
                    return m_exit || !m_queue.empty() || token.stop_requested();
                });

            if (m_exit || token.stop_requested())
            {
                break;
            }

            const Request req = m_queue.front();
            m_queue.pop_front();
            currentFrame = req.m_frameNo;
            const uint32_t index = req.m_index;
            lock.unlock();

            m_func(currentFrame, index);

            lock.lock();
            m_finishedFrame = currentFrame;
            lock.unlock();
            m_cv.notify_all();
        }

        return 0;
    }
    FrameController::~FrameController()
    {
        // スレッドが残らないよう先に停止する
        stop_jobs();
    }
    void FrameController::step()
    {
        // 初回のみ初期化して状態を確定させる
        // モードに応じて 1 ステップ進める
        // 終了条件を満たしたらジョブを止める
        if (m_finished)
        {
            return;
        }

        if (!m_started)
        {
            if (!start_pipeline())
            {
                m_finished = true;
                return;
            }
        }

        bool isRunning = true;
        if (m_desc.m_bufferCount == 1)
        {
            isRunning = step_single_buffer();
        }
        else
        {
            switch (m_desc.m_mode)
            {
            case ControllerMode::Fixed:
                isRunning = step_fixed();
                break;
            case ControllerMode::Mailbox:
                isRunning = step_mailbox();
                break;
            case ControllerMode::Backpressure:
                isRunning = step_backpressure();
                break;
            }
        }

        if (!isRunning)
        {
            stop_jobs();
            m_finished = true;
        }
    }
    void FrameController::poll_resize_request()
    {
        // 反映要求フラグを立てる
        // セーフポイントで適用できるよう記録だけ行う
        m_resizePending.store(true, std::memory_order_relaxed);
    }
    bool FrameController::start_pipeline()
    {
        // 設定値を検証する
        // 初期バッファを設定して開始状態を作る
        CUE_ASSERT_MSG((m_desc.m_bufferCount >= 1), "bufferCount < 1");
        CUE_ASSERT_MSG(static_cast<bool>(m_updateFunc), "updateFunc is null.");
        CUE_ASSERT_MSG(static_cast<bool>(m_renderFunc), "renderFunc is null.");
        CUE_ASSERT_MSG(static_cast<bool>(m_presentFunc), "presentFunc is null.");
        m_frameCounter.set_max_fps(m_desc.m_maxFps);
        m_frameCounter.set_max_lead(m_desc.m_bufferCount - 1);
        m_maxLead = static_cast<uint64_t>(m_desc.m_bufferCount - 1);
        m_backBufferBase = 0;
        m_fixedState = FixedState{};
        m_mailboxState = MailboxState{};
        m_backpressureState = BackpressureState{};
        m_singleState = SingleBufferState{};

        if (m_desc.m_bufferCount > 1)
        {
            fill_buffers(0);
        }
        else
        {
            m_started = true;
            return true;
        }

        if (!m_updateJob.start(m_threadFactory, "UpdateJob", [this](uint64_t frameNo, uint32_t index)
            {
                m_updateFunc(frameNo, index);
            }))
        {
            CUE_ASSERT_MSG(false, "UpdateJob の開始に失敗しました。");
            return false;
        }

        if (!m_renderJob.start(m_threadFactory, "RenderJob", [this](uint64_t frameNo, uint32_t index)
            {
                m_renderFunc(frameNo, index);
            }))
        {
            CUE_ASSERT_MSG(false, "RenderJob の開始に失敗しました。");
            m_updateJob.stop();
            return false;
        }

        m_started = true;
        return true;
    }
    void FrameController::stop_jobs()
    {
        // スレッド停止を先に行い競合を避ける
        // 起動状態を更新して再起動判定に使う
        m_updateJob.stop();
        m_renderJob.stop();
        m_started = false;
    }
    void FrameController::compute_indices(uint64_t frameNo, uint32_t bufferCount, uint32_t& updateIndex, uint32_t& renderIndex, uint32_t& presentIndex)
    {
        // 単一バッファは固定で 0 を返す
        // presentIndex を算出する
        // update/render のインデックスをオフセットで算出する
        if (bufferCount == 1)
        {
            updateIndex = 0;
            renderIndex = 0;
            presentIndex = 0;
            return;
        }

        const uint64_t baseFrame = frameNo + m_backBufferBase;
        presentIndex = static_cast<uint32_t>(baseFrame % bufferCount);
        renderIndex = (presentIndex + bufferCount - 2) % bufferCount;
        updateIndex = (presentIndex + bufferCount - 1) % bufferCount;
        m_updateIndex = updateIndex;
        m_renderIndex = renderIndex;
        m_presentIndex = presentIndex;
    }
    void FrameController::present_frame(uint64_t frameNo)
    {
        // 必要なインデックスをまとめて算出する
        // Present 後に FPS 制御を行う
        uint32_t updateIndex = 0;
        uint32_t renderIndex = 0;
        uint32_t presentIndex = 0;
        compute_indices(frameNo, m_desc.m_bufferCount, updateIndex, renderIndex, presentIndex);
        (void)updateIndex;
        (void)renderIndex;

        m_presentFunc(frameNo, presentIndex);
        m_frameCounter.tick();
    }
    void FrameController::apply_resize_for_next_frame(uint64_t nextFrameNo)
    {
        // リサイズ後にインデックスが揃うよう基準を合わせる
        // 次フレームの基準位置を更新する
        const uint32_t bufferCount = m_desc.m_bufferCount;
        const uint32_t mod = static_cast<uint32_t>(nextFrameNo % bufferCount);
        m_backBufferBase = (bufferCount - mod) % bufferCount;
    }
    void FrameController::fill_buffers(uint64_t frameNo)
    {
        // 全バッファを走査して更新する
        // 初回 Present で欠けが出ないよう順に埋める
        for (uint32_t i = 0; i < m_desc.m_bufferCount; ++i)
        {
            m_updateFunc(frameNo, i);
        }
    }
    bool FrameController::step_single_buffer()
    {
        // リサイズ要求があれば次フレームの基準だけ合わせる
        // Update -> Render -> Present を順番に処理する
        if (m_resizePending.load(std::memory_order_relaxed))
        {
            apply_resize_for_next_frame(m_singleState.m_currentFrame);
            m_resizePending.store(false, std::memory_order_relaxed);
        }

        uint32_t updateIndex = 0;
        uint32_t renderIndex = 0;
        uint32_t presentIndex = 0;
        compute_indices(m_singleState.m_currentFrame, m_desc.m_bufferCount, updateIndex, renderIndex, presentIndex);
        (void)presentIndex;

        m_updateFunc(m_singleState.m_currentFrame, updateIndex);
        m_renderFunc(m_singleState.m_currentFrame, renderIndex);
        present_frame(m_singleState.m_currentFrame);
        ++m_singleState.m_currentFrame;

        return true;
    }
    bool FrameController::step_fixed()
    {
        // 安全にリサイズできるかを先に確認する
        // 先行上限内なら Update/Render をキックする
        // 完了済みなら Present して進める
        if (m_resizePending.load(std::memory_order_relaxed) &&
            m_fixedState.m_produceFrame == m_fixedState.m_totalFrame)
        {
            apply_resize_for_next_frame(m_fixedState.m_totalFrame);
            fill_buffers(m_fixedState.m_totalFrame);
            m_resizePending.store(false, std::memory_order_relaxed);
        }

        const bool canProduce = !m_resizePending.load(std::memory_order_relaxed);
        if (canProduce && (m_fixedState.m_produceFrame - m_fixedState.m_totalFrame) < m_maxLead)
        {
            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
            compute_indices(m_fixedState.m_produceFrame, m_desc.m_bufferCount, updateIndex, renderIndex, presentIndex);
            (void)presentIndex;

            m_updateJob.kick(m_fixedState.m_produceFrame, updateIndex);
            m_renderJob.kick(m_fixedState.m_produceFrame, renderIndex);
            ++m_fixedState.m_produceFrame;
        }

        const bool canPresent = m_updateJob.get_finished_frame() >= m_fixedState.m_totalFrame &&
            m_renderJob.get_finished_frame() >= m_fixedState.m_totalFrame;
        if (canPresent)
        {
            present_frame(m_fixedState.m_totalFrame);
            ++m_fixedState.m_totalFrame;

            if (m_resizePending.load(std::memory_order_relaxed) &&
                m_fixedState.m_produceFrame == m_fixedState.m_totalFrame)
            {
                apply_resize_for_next_frame(m_fixedState.m_totalFrame);
                fill_buffers(m_fixedState.m_totalFrame);
                m_resizePending.store(false, std::memory_order_relaxed);
            }
        }
        else
        {
            m_waiter.relax();
        }

        return true;
    }
    bool FrameController::step_mailbox()
    {
        // 初回のリサイズ適用タイミングを安全側に寄せる
        // 先行上限内で Update/Render をキックする
        // Present とリサイズ反映を進める
        if (m_resizePending.load(std::memory_order_relaxed) && !m_mailboxState.m_hasPresented &&
            m_mailboxState.m_produceFrame == 0)
        {
            apply_resize_for_next_frame(0);
            fill_buffers(0);
            m_resizePending.store(false, std::memory_order_relaxed);
        }

        const uint64_t presentBase = m_mailboxState.m_hasPresented ? m_mailboxState.m_lastPresentedFrame : 0;
        const bool canProduce = !m_resizePending.load(std::memory_order_relaxed);
        if (canProduce && (m_mailboxState.m_produceFrame - presentBase) < m_maxLead)
        {
            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
            compute_indices(m_mailboxState.m_produceFrame, m_desc.m_bufferCount, updateIndex, renderIndex, presentIndex);
            (void)presentIndex;

            m_updateJob.kick(m_mailboxState.m_produceFrame, updateIndex);
            m_renderJob.kick(m_mailboxState.m_produceFrame, renderIndex);
            ++m_mailboxState.m_produceFrame;
        }

        const uint64_t updateFinished = m_updateJob.get_finished_frame();
        const uint64_t renderFinished = m_renderJob.get_finished_frame();
        const uint64_t readyFrame = (updateFinished < renderFinished) ? updateFinished : renderFinished;

        bool didPresent = false;
        if (!m_mailboxState.m_hasPresented || readyFrame > m_mailboxState.m_lastPresentedFrame)
        {
            present_frame(readyFrame);
            m_mailboxState.m_lastPresentedFrame = readyFrame;
            m_mailboxState.m_hasPresented = true;
            didPresent = true;
        }

        bool didResize = false;
        if (m_resizePending.load(std::memory_order_relaxed) && m_mailboxState.m_hasPresented)
        {
            const bool noInFlight = (m_mailboxState.m_lastPresentedFrame + 1) == m_mailboxState.m_produceFrame;
            const bool workersDone = updateFinished >= m_mailboxState.m_lastPresentedFrame &&
                renderFinished >= m_mailboxState.m_lastPresentedFrame;
            if (noInFlight && workersDone)
            {
                apply_resize_for_next_frame(m_mailboxState.m_lastPresentedFrame + 1);
                fill_buffers(m_mailboxState.m_lastPresentedFrame + 1);
                m_resizePending.store(false, std::memory_order_relaxed);
                didResize = true;
            }
        }

        if (!didPresent && !didResize)
        {
            m_waiter.relax();
        }

        return true;
    }
    bool FrameController::step_backpressure()
    {
        // キック条件を確認する
        // 完了済みなら Present して次へ進む
        if (!m_backpressureState.m_inFlight)
        {
            if (m_resizePending.load(std::memory_order_relaxed))
            {
                apply_resize_for_next_frame(m_backpressureState.m_currentFrame);
                fill_buffers(m_backpressureState.m_currentFrame);
                m_resizePending.store(false, std::memory_order_relaxed);
            }

            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
            compute_indices(m_backpressureState.m_currentFrame, m_desc.m_bufferCount,
                updateIndex, renderIndex, presentIndex);
            (void)presentIndex;

            m_updateJob.kick(m_backpressureState.m_currentFrame, updateIndex);
            m_renderJob.kick(m_backpressureState.m_currentFrame, renderIndex);
            m_backpressureState.m_inFlight = true;
        }

        const bool canPresent = m_updateJob.get_finished_frame() >= m_backpressureState.m_currentFrame &&
            m_renderJob.get_finished_frame() >= m_backpressureState.m_currentFrame;
        if (canPresent)
        {
            present_frame(m_backpressureState.m_currentFrame);
            ++m_backpressureState.m_currentFrame;
            m_backpressureState.m_inFlight = false;
        }
        else
        {
            m_waiter.relax();
        }

        return true;
    }
}
