#include "FrameController.h"
#include <CueAssert.h>
#include <Time/Timer.h>
#include <limits>

namespace Cue
{
    bool FrameJob::start(Core::Threading::IThreadFactory& a_factory, const Core::Time::IClock& a_clock,
        const char* a_name, Core::Threading::ThreadApartmentModel a_apartmentModel, jobFunc a_func)
    {
        // 実行関数をメンバへ保持する
        // 要求受付ループのスレッドを開始する
        m_func = std::move(a_func);
        m_clock = &a_clock;
        m_exit = false;
        m_lastElapsedMs = 0.0;
        m_finishedFrame = std::numeric_limits<uint64_t>::max();
        m_isExecuting = false;
        m_queue.clear();

        Core::Threading::ThreadDesc desc{};
        if (a_name != nullptr)
        {
            desc.name = a_name;
        }
        desc.apartmentModel = a_apartmentModel;

        std::unique_ptr<Core::Threading::IThread> thread{};
        const auto result = a_factory.create_thread(desc, &FrameJob::thread_entry, this, thread);
        if (!result)
        {
            return false;
        }

        m_thread = std::move(thread);
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
    double FrameJob::get_last_elapsed_ms() const
    {
        // 実行時間の読み取りも同じロックで保護する
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastElapsedMs;
    }
    bool FrameJob::is_idle() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty() && !m_isExecuting;
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
                // 実行中の job を保持したまま thread を破棄すると次回起動時に競合するため、終了失敗を隠しません
                const Result result = m_thread->join();
                CUE_ASSERT_FORMAT(success(result), "Failed to join frame job thread: {}", result.message.data());
            }
            m_thread.reset();
        }
    }
    uint32_t FrameJob::thread_entry(Core::Threading::StopToken a_token, void* a_user) noexcept
    {
        // void* を安全に復元してインスタンスを得る
        // 共通のループ処理に委譲する
        auto* self = static_cast<FrameJob*>(a_user);
        if (!self)
        {
            return 0;
        }
        return self->thread_loop(a_token);
    }
    uint32_t FrameJob::thread_loop(Core::Threading::StopToken a_token) noexcept
    {
        // 条件変数で待機する
        // 停止要求を優先して安全に終了する
        uint64_t currentFrame = 0;
        while (!a_token.stop_requested())
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&]() { return m_exit || !m_queue.empty() || a_token.stop_requested(); });

            if (m_exit || a_token.stop_requested())
            {
                break;
            }

            const Request req = m_queue.front();
            m_queue.pop_front();
            m_isExecuting = true;
            currentFrame = req.frameNo;
            const uint32_t index = req.index;
            lock.unlock();

            double elapsedMs = 0.0;
            if (m_clock != nullptr)
            {
                Core::Time::Timer timer(*m_clock);
                timer.start();
                m_func(currentFrame, index);
                timer.stop();
                elapsedMs = timer.elapsed_ticks().ms_f64();
            }
            else
            {
                m_func(currentFrame, index);
            }

            lock.lock();
            m_lastElapsedMs = elapsedMs;
            m_finishedFrame = currentFrame;
            m_isExecuting = false;
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
        Core::Time::Timer stepTimer(m_clock);
        stepTimer.start();
        if (m_finished)
        {
            stepTimer.stop();
            m_stepElapsedMs = stepTimer.elapsed_ticks().ms_f64();
            return;
        }

        if (!m_started)
        {
            if (!start_pipeline())
            {
                m_finished = true;
                stepTimer.stop();
                m_stepElapsedMs = stepTimer.elapsed_ticks().ms_f64();
                return;
            }
        }

        ++m_stepsSincePresent;

        CUE_ASSERT_MSG(m_activeState != nullptr, "FrameController state is null.");
        const bool isRunning = m_activeState->step(*this);

        if (!isRunning)
        {
            stop_jobs();
            m_finished = true;
        }

        stepTimer.stop();
        m_stepElapsedMs = stepTimer.elapsed_ticks().ms_f64();
    }
    void FrameController::poll_resize_request()
    {
        // 反映要求フラグを立てる
        // セーフポイントで適用できるよう記録だけ行う
        m_resizePending.store(true, std::memory_order_relaxed);
    }
    void FrameController::synchronize()
    {
        if (!m_started || m_desc.bufferCount <= 1)
        {
            return;
        }

        for (;;)
        {
            if (m_updateJob.is_idle() && m_renderJob.is_idle())
            {
                return;
            }

            m_waiter.relax();
        }
    }
    bool FrameController::start_pipeline()
    {
        // 設定値を検証する
        // 初期バッファを設定して開始状態を作る
        CUE_ASSERT_MSG((m_desc.bufferCount >= 1), "bufferCount < 1");
        CUE_ASSERT_MSG(static_cast<bool>(m_updateFunc), "updateFunc is null.");
        CUE_ASSERT_MSG(static_cast<bool>(m_renderFunc), "renderFunc is null.");
        CUE_ASSERT_MSG(static_cast<bool>(m_presentFunc), "presentFunc is null.");
        m_frameCounter.set_max_fps(m_desc.maxFps);
        m_frameCounter.set_max_lead(m_desc.bufferCount - 1);
        m_maxLead = static_cast<uint64_t>(m_desc.bufferCount - 1);
        m_backBufferBase = 0;
        m_fixedState.reset();
        m_mailboxState.reset();
        m_backpressureState.reset();
        m_singleState.reset();
        m_activeState = &select_state();
        m_updateElapsedMs = 0.0;
        m_renderElapsedMs = 0.0;
        m_stepElapsedMs = 0.0;
        m_presentElapsedMs = 0.0;
        m_counterTickElapsedMs = 0.0;
        m_stepsSincePresent = 0;
        m_stepsPerPresent = 0;

        if (m_desc.bufferCount > 1)
        {
            fill_buffers(0);
        }
        else
        {
            m_started = true;
            return true;
        }

        if (!m_updateJob.start(m_threadFactory, m_clock, "UpdateJob",
            Core::Threading::ThreadApartmentModel::MultiThreaded,
            [this](uint64_t frameNo, uint32_t index) { m_updateFunc(frameNo, index); }))
        {
            CUE_ASSERT_MSG(false, "UpdateJob の開始に失敗しました。");
            return false;
        }

        if (!m_renderJob.start(m_threadFactory, m_clock, "RenderJob",
            Core::Threading::ThreadApartmentModel::MultiThreaded,
            [this](uint64_t frameNo, uint32_t index) { m_renderFunc(frameNo, index); }))
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
    double FrameController::update_elapsed_ms() const noexcept
    {
        if (m_desc.bufferCount == 1)
        {
            return m_updateElapsedMs;
        }

        return m_updateJob.get_last_elapsed_ms();
    }
    double FrameController::render_elapsed_ms() const noexcept
    {
        if (m_desc.bufferCount == 1)
        {
            return m_renderElapsedMs;
        }

        return m_renderJob.get_last_elapsed_ms();
    }
    double FrameController::step_elapsed_ms() const noexcept
    {
        return m_stepElapsedMs;
    }
    double FrameController::present_elapsed_ms() const noexcept
    {
        return m_presentElapsedMs;
    }
    double FrameController::counter_tick_elapsed_ms() const noexcept
    {
        return m_counterTickElapsedMs;
    }
    uint32_t FrameController::steps_per_present() const noexcept
    {
        return m_stepsPerPresent;
    }
    void FrameController::compute_indices(uint64_t frameNo, uint32_t bufferCount, uint32_t& updateIndex,
        uint32_t& renderIndex, uint32_t& presentIndex)
    {
        if (bufferCount == 1)
        {
            updateIndex = 0;
            renderIndex = 0;
            presentIndex = 0;
            return;
        }

        const uint64_t baseFrame = frameNo + m_backBufferBase;
        const uint32_t frameIndex = static_cast<uint32_t>(baseFrame % bufferCount);
        updateIndex = frameIndex;
        renderIndex = frameIndex;
        presentIndex = frameIndex;
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
        compute_indices(frameNo, m_desc.bufferCount, updateIndex, renderIndex, presentIndex);
        (void)updateIndex;
        (void)renderIndex;

        Core::Time::Timer presentTimer(m_clock);
        presentTimer.start();
        m_presentFunc(frameNo, presentIndex);
        presentTimer.stop();
        m_presentElapsedMs = presentTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer counterTimer(m_clock);
        counterTimer.start();
        m_frameCounter.tick();
        counterTimer.stop();
        m_counterTickElapsedMs = counterTimer.elapsed_ticks().ms_f64();

        m_stepsPerPresent = m_stepsSincePresent;
        m_stepsSincePresent = 0;
    }
    void FrameController::apply_resize_for_next_frame(uint64_t nextFrameNo)
    {
        // リサイズ後にインデックスが揃うよう基準を合わせる
        // 次フレームの基準位置を更新する
        const uint32_t bufferCount = m_desc.bufferCount;
        const uint32_t mod = static_cast<uint32_t>(nextFrameNo % bufferCount);
        m_backBufferBase = (bufferCount - mod) % bufferCount;
    }
    void FrameController::apply_pending_resize(uint64_t nextFrameNo, bool a_shouldFillBuffers)
    {
        if (m_safePointFunc)
        {
            m_safePointFunc();
        }

        apply_resize_for_next_frame(nextFrameNo);
        if (a_shouldFillBuffers)
        {
            fill_buffers(nextFrameNo);
        }

        m_resizePending.store(false, std::memory_order_relaxed);
    }
    void FrameController::fill_buffers(uint64_t frameNo)
    {
        // 全バッファを走査して更新する
        // 初回 Present で欠けが出ないよう順に埋める
        for (uint32_t i = 0; i < m_desc.bufferCount; ++i)
        {
            m_updateFunc(frameNo, i);
        }
    }
    FrameController::ControllerState& FrameController::select_state() noexcept
    {
        if (m_desc.bufferCount == 1)
        {
            return m_singleState;
        }

        switch (m_desc.mode)
        {
        case ControllerMode::Fixed:
            return m_fixedState;
        case ControllerMode::Mailbox:
            return m_mailboxState;
        case ControllerMode::Backpressure:
            return m_backpressureState;
        }

        return m_fixedState;
    }
    void FrameController::SingleBufferState::reset() noexcept
    {
        m_currentFrame = 0;
    }
    bool FrameController::SingleBufferState::step(FrameController& a_controller)
    {
        // リサイズ要求があれば次フレームの基準だけ合わせる
        // Update -> Render -> Present を順番に処理する
        if (a_controller.m_resizePending.load(std::memory_order_relaxed))
        {
            a_controller.apply_pending_resize(m_currentFrame, false);
        }

        uint32_t updateIndex = 0;
        uint32_t renderIndex = 0;
        uint32_t presentIndex = 0;
        a_controller.compute_indices(m_currentFrame, a_controller.m_desc.bufferCount, updateIndex, renderIndex,
            presentIndex);
        (void)presentIndex;

        Core::Time::Timer updateTimer(a_controller.m_clock);
        updateTimer.start();
        a_controller.m_updateFunc(m_currentFrame, updateIndex);
        updateTimer.stop();
        a_controller.m_updateElapsedMs = updateTimer.elapsed_ticks().ms_f64();

        Core::Time::Timer renderTimer(a_controller.m_clock);
        renderTimer.start();
        a_controller.m_renderFunc(m_currentFrame, renderIndex);
        renderTimer.stop();
        a_controller.m_renderElapsedMs = renderTimer.elapsed_ticks().ms_f64();
        a_controller.present_frame(m_currentFrame);
        ++m_currentFrame;

        return true;
    }
    void FrameController::FixedState::reset() noexcept
    {
        m_produceFrame = 0;
        m_renderFrame = 0;
        m_totalFrame = 0;
    }
    bool FrameController::FixedState::step(FrameController& a_controller)
    {
        // 安全にリサイズできるかを先に確認する
        // 先行上限内なら Update/Render をキックする
        // 完了済みなら Present して進める
        if (a_controller.m_resizePending.load(std::memory_order_relaxed) && m_produceFrame == m_totalFrame &&
            m_renderFrame == m_totalFrame)
        {
            a_controller.apply_pending_resize(m_totalFrame, true);
        }

        const bool canProduce = !a_controller.m_resizePending.load(std::memory_order_relaxed);
        if (canProduce && (m_produceFrame - m_totalFrame) < a_controller.m_maxLead)
        {
            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
            a_controller.compute_indices(m_produceFrame, a_controller.m_desc.bufferCount, updateIndex, renderIndex,
                presentIndex);
            (void)presentIndex;

            a_controller.m_updateJob.kick(m_produceFrame, updateIndex);
            ++m_produceFrame;
        }

        const uint64_t updateFinished = a_controller.m_updateJob.get_finished_frame();
        if (m_renderFrame == m_totalFrame && m_renderFrame < m_produceFrame &&
            FrameController::is_frame_finished(updateFinished, m_renderFrame))
        {
            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
            a_controller.compute_indices(m_renderFrame, a_controller.m_desc.bufferCount, updateIndex, renderIndex,
                presentIndex);
            (void)updateIndex;
            (void)presentIndex;

            a_controller.m_renderJob.kick(m_renderFrame, renderIndex);
            ++m_renderFrame;
        }

        const bool canPresent =
            FrameController::is_frame_finished(a_controller.m_renderJob.get_finished_frame(), m_totalFrame);
        if (canPresent)
        {
            a_controller.present_frame(m_totalFrame);
            ++m_totalFrame;

            if (a_controller.m_resizePending.load(std::memory_order_relaxed) && m_produceFrame == m_totalFrame &&
                m_renderFrame == m_totalFrame)
            {
                a_controller.apply_pending_resize(m_totalFrame, true);
            }
        }
        else
        {
            a_controller.m_waiter.relax();
        }

        return true;
    }
    void FrameController::MailboxState::reset() noexcept
    {
        m_produceFrame = 0;
        m_lastPresentedFrame = 0;
        m_hasPresented = false;
    }
    bool FrameController::MailboxState::step(FrameController& a_controller)
    {
        // 初回のリサイズ適用タイミングを安全側に寄せる
        // 先行上限内で Update/Render をキックする
        // Present とリサイズ反映を進める
        if (a_controller.m_resizePending.load(std::memory_order_relaxed) && !m_hasPresented && m_produceFrame == 0)
        {
            a_controller.apply_pending_resize(0, true);
        }

        const uint64_t presentBase = m_hasPresented ? m_lastPresentedFrame : 0;
        const bool canProduce = !a_controller.m_resizePending.load(std::memory_order_relaxed);
        if (canProduce && (m_produceFrame - presentBase) < a_controller.m_maxLead)
        {
            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
            a_controller.compute_indices(m_produceFrame, a_controller.m_desc.bufferCount, updateIndex, renderIndex,
                presentIndex);
            (void)presentIndex;

            a_controller.m_updateJob.kick(m_produceFrame, updateIndex);
            a_controller.m_renderJob.kick(m_produceFrame, renderIndex);
            ++m_produceFrame;
        }

        const uint64_t updateFinished = a_controller.m_updateJob.get_finished_frame();
        const uint64_t renderFinished = a_controller.m_renderJob.get_finished_frame();
        if (updateFinished == std::numeric_limits<uint64_t>::max() ||
            renderFinished == std::numeric_limits<uint64_t>::max())
        {
            a_controller.m_waiter.relax();
            return true;
        }
        const uint64_t readyFrame = (updateFinished < renderFinished) ? updateFinished : renderFinished;

        bool didPresent = false;
        if (!m_hasPresented || readyFrame > m_lastPresentedFrame)
        {
            a_controller.present_frame(readyFrame);
            m_lastPresentedFrame = readyFrame;
            m_hasPresented = true;
            didPresent = true;
        }

        bool didResize = false;
        if (a_controller.m_resizePending.load(std::memory_order_relaxed) && m_hasPresented)
        {
            const bool noInFlight = (m_lastPresentedFrame + 1) == m_produceFrame;
            const bool workersDone = updateFinished >= m_lastPresentedFrame && renderFinished >= m_lastPresentedFrame;
            if (noInFlight && workersDone)
            {
                a_controller.apply_pending_resize(m_lastPresentedFrame + 1, true);
                didResize = true;
            }
        }

        if (!didPresent && !didResize)
        {
            a_controller.m_waiter.relax();
        }

        return true;
    }
    void FrameController::BackpressureState::reset() noexcept
    {
        m_currentFrame = 0;
        m_isInFlight = false;
    }
    bool FrameController::BackpressureState::step(FrameController& a_controller)
    {
        // キック条件を確認する
        // 完了済みなら Present して次へ進む
        if (!m_isInFlight)
        {
            if (a_controller.m_resizePending.load(std::memory_order_relaxed))
            {
                a_controller.apply_pending_resize(m_currentFrame, true);
            }

            uint32_t updateIndex = 0;
            uint32_t renderIndex = 0;
            uint32_t presentIndex = 0;
            a_controller.compute_indices(m_currentFrame, a_controller.m_desc.bufferCount, updateIndex, renderIndex,
                presentIndex);
            (void)presentIndex;

            a_controller.m_updateJob.kick(m_currentFrame, updateIndex);
            a_controller.m_renderJob.kick(m_currentFrame, renderIndex);
            m_isInFlight = true;
        }

        const bool canPresent =
            FrameController::is_frame_finished(a_controller.m_updateJob.get_finished_frame(), m_currentFrame) &&
            FrameController::is_frame_finished(a_controller.m_renderJob.get_finished_frame(), m_currentFrame);
        if (canPresent)
        {
            a_controller.present_frame(m_currentFrame);
            ++m_currentFrame;
            m_isInFlight = false;
        }
        else
        {
            a_controller.m_waiter.relax();
        }

        return true;
    }
    bool FrameController::is_frame_finished(uint64_t a_finishedFrame, uint64_t a_frameNo) noexcept
    {
        return a_finishedFrame != std::numeric_limits<uint64_t>::max() && a_finishedFrame >= a_frameNo;
    }
} // namespace Cue
