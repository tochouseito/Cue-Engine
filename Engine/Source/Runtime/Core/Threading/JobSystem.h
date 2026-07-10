#pragma once

/// *********************************************************************************
/// 優先度付きジョブシステム
/// *********************************************************************************

// === Base includes ===
#include <CueAssert.h>
#include <CueResult.h>

// === Core includes ===
#include "IThread.h"
#include "IThreadFactory.h"

// === C++ includes ===
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Cue::Core::Threading
{
    /// @brief 優先度付きのジョブ実行基盤
    class JobSystem final
    {
    public:

        /// @brief 優先度
        enum class JobPriority : uint8_t
        {
            High = 0,
            Normal = 1,
            Low = 2,
        };

        /// @brief ジョブの生の実装関数
        using jobRawProc = void(*)(void*);

    private:

        /// @brief 依存関係ノード
        struct DependencyNode final
        {
            std::shared_future<void> future{}; // 依存先ジョブの完了を待つための future
            DependencyNode* next = nullptr; // 単方向リストの次ノード
            bool isReady = false; // 依存先ジョブが完了しているか
        };

        /// @brief 優先度ごとのジョブキュー
        struct Job final
        {
            std::string name{}; // Job 名
            std::function<void()> func{}; // ジョブの実行関数
            jobRawProc rawProc = nullptr; // 生のジョブ実装関数
            void* rawContext = nullptr; // 生のジョブ実装関数のコンテキスト
            JobPriority priority = JobPriority::Normal; // ジョブの優先度
            DependencyNode* dependencyHead = nullptr; // 依存関係の先頭ノード
            std::optional<std::promise<void>> promise{}; // ジョブ完了通知用の promise
        };

        /// @brief ジョブキュー
        struct JobQueue final
        {
            std::vector<Job*> ready{}; // 実行可能なジョブ
            std::vector<Job*> blocked{}; // 依存関係が解決されていないジョブ
        };

    public:
        /// @brief デフォルトコンストラクタ
        JobSystem() noexcept = default;

        /// @brief デストラクタ、未停止ならシャットダウン
        ~JobSystem() noexcept
        {
            shutdown();
        }

        // コピー禁止
        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        // ムーブ禁止
        JobSystem(JobSystem&&) = delete;
        JobSystem& operator=(JobSystem&&) = delete;

        /// @brief JobSystem の初期化
        /// @param a_factory スレッドファクトリ
        /// @param a_requestedWorkerCount 要求するワーカー数（0なら自動決定）
        /// @param a_maxWorkerCount ワーカー数の上限（0なら制限なし）
        /// @param a_maxJobsInFlight 同時に存在できるジョブの最大数（0なら制限なし）
        /// @param a_maxDependencyNodes 同時に存在できる依存関係ノードの最大数（0なら自動決定）
        /// @return CueResult::ok() なら初期化成功、そうでない場合は失敗
        Result initialize(
            IThreadFactory& a_factory,
            uint32_t a_requestedWorkerCount = 0,
            uint32_t a_maxWorkerCount = 64,
            uint32_t a_maxJobsInFlight = 4096,
            uint32_t a_maxDependencyNodes = 0) noexcept
        {
            // 二重初期化の防止
            if (m_isInitialized.load(std::memory_order_relaxed))
            {
                return Result::ok();
            }

            // ワーカー数の決定
            uint32_t workerCount = a_requestedWorkerCount;
            if (workerCount == 0)
            {
                const uint32_t hardwareCount = std::thread::hardware_concurrency();
                workerCount = hardwareCount != 0 ? hardwareCount : 4;
            }
            if (workerCount > a_maxWorkerCount)
            {
                workerCount = a_maxWorkerCount;
            }
            if (workerCount == 0)
            {
                workerCount = 1;
            }

            // 依存関係ノードの容量の決定
            uint32_t dependencyCapacity = a_maxDependencyNodes;
            if (dependencyCapacity == 0)
            {
                constexpr uint32_t k_defaultDepsPerJob = 4;
                const uint64_t total =
                    static_cast<uint64_t>(a_maxJobsInFlight) * k_defaultDepsPerJob;
                dependencyCapacity = total > std::numeric_limits<uint32_t>::max()
                    ? std::numeric_limits<uint32_t>::max()
                    : static_cast<uint32_t>(total);
            }

            // ストレージの初期化
            Result result = initialize_storage(a_maxJobsInFlight, dependencyCapacity);
            if (!result)
            {
                clear_storage();
                return result;
            }

            // ワーカースレッドの生成
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_high.ready.clear();
                m_high.blocked.clear();
                m_normal.ready.clear();
                m_normal.blocked.clear();
                m_low.ready.clear();
                m_low.blocked.clear();
            }

            // 状態の初期化
            m_stopRequested.store(false, std::memory_order_relaxed);
            m_inFlight.store(0, std::memory_order_relaxed);
            m_dependencyEpoch.store(0, std::memory_order_relaxed);

            // ワーカーリストの確保
            try
            {
                m_workers.clear();
                m_workers.resize(workerCount);
            }
            catch (...)
            {
                clear_storage();
                return Result::fail(
                    Code::OutOfMemory,
                    Severity::Error,
                    "Failed to allocate worker list.");
            }

            // ワーカーの生成
            for (uint32_t index = 0; index < workerCount; ++index)
            {
                ThreadDesc desc{};
                desc.name = make_worker_name(index);

                Result createResult = a_factory.create_thread(
                    desc,
                    &JobSystem::worker_entry,
                    this,
                    m_workers[index]);
                if (!createResult)
                {
                    shutdown_internal(true);
                    return createResult;
                }
            }

            m_isInitialized.store(true, std::memory_order_relaxed);
            return Result::ok();
        }

        /// @brief JobSystem のシャットダウン
        void shutdown() noexcept
        {
            shutdown_internal(false);
        }

        /// @brief ジョブのエンキュー
        /// @param a_jobName ジョブの名前（デバッグ用、UTF-8想定）
        /// @param a_job ジョブの実装関数
        /// @param a_outFuture ジョブの完了を待つための future への参照、ジョブ完了時に ready、不要な場合は nullptr
        /// @param a_priority ジョブの優先度
        /// @param a_dependencies ジョブの依存関係
        /// @return CueResult::ok() ならエンキュー成功、そうでない場合は失敗
        Result enqueue_job(
            std::string a_jobName,
            std::function<void()> a_job,
            std::shared_future<void>& a_outFuture,
            JobPriority a_priority = JobPriority::Normal,
            const std::vector<std::shared_future<void>>& a_dependencies = {}) noexcept
        {
            return enqueue_job_internal(
                std::move(a_jobName),
                std::move(a_job),
                nullptr,
                nullptr,
                &a_outFuture,
                a_priority,
                a_dependencies);
        }

        /// @brief ジョブのエンキュー（完了通知不要版）
        /// @param a_jobName ジョブの名前（デバッグ用、UTF-8想定）
        /// @param a_job ジョブの実装関数
        /// @param a_priority ジョブの優先度
        /// @param a_dependencies ジョブの依存関係
        /// @return CueResult::ok() ならエンキュー成功、そうでない場合は失敗
        Result enqueue_job(
            std::string a_jobName,
            std::function<void()> a_job,
            JobPriority a_priority = JobPriority::Normal,
            const std::vector<std::shared_future<void>>& a_dependencies = {}) noexcept
        {
            return enqueue_job_internal(
                std::move(a_jobName),
                std::move(a_job),
                nullptr,
                nullptr,
                nullptr,
                a_priority,
                a_dependencies);
        }

        /// @brief 生のジョブ実装関数をエンキュー、完了通知と依存関係は std::shared_future<void> で指定
        /// @param a_jobName ジョブの名前（デバッグ用、UTF-8想定）
        /// @param a_proc ジョブの実装関数
        /// @param a_context ジョブのコンテキスト
        /// @param a_outFuture ジョブの完了を待つための future への参照、ジョブ完了時に ready、不要な場合は nullptr
        /// @param a_priority ジョブの優先度
        /// @param a_dependencies ジョブの依存関係
        /// @return CueResult::ok() ならエンキュー成功、そうでない場合は失敗
        Result enqueue_job_raw(
            std::string a_jobName,
            jobRawProc a_proc,
            void* a_context,
            std::shared_future<void>& a_outFuture,
            JobPriority a_priority = JobPriority::Normal,
            const std::vector<std::shared_future<void>>& a_dependencies = {}) noexcept
        {
            return enqueue_job_internal(
                std::move(a_jobName),
                {},
                a_proc,
                a_context,
                &a_outFuture,
                a_priority,
                a_dependencies);
        }

        /// @brief 生のジョブ実装関数をエンキュー（完了通知不要版）、依存関係は std::shared_future<void> で指定
        /// @param a_jobName ジョブの名前（デバッグ用、UTF-8想定）
        /// @param a_proc ジョブの実装関数
        /// @param a_context ジョブのコンテキスト
        /// @param a_priority ジョブの優先度
        /// @param a_dependencies ジョブの依存関係
        /// @return CueResult::ok() ならエンキュー成功、そうでない場合は失敗
        Result enqueue_job_raw(
            std::string a_jobName,
            jobRawProc a_proc,
            void* a_context,
            JobPriority a_priority = JobPriority::Normal,
            const std::vector<std::shared_future<void>>& a_dependencies = {}) noexcept
        {
            return enqueue_job_internal(
                std::move(a_jobName),
                {},
                a_proc,
                a_context,
                nullptr,
                a_priority,
                a_dependencies);
        }

        /// @brief 複数のジョブをまとめてエンキュー、完了通知と依存関係は std::shared_future<void> で指定
        /// @param a_batchName バッチの名前（デバッグ用、UTF-8想定）
        /// @param a_jobs ジョブの実装関数のリスト
        /// @param a_outFuture ジョブの完了を待つための future への参照、ジョブ完了時に ready、不要な場合は nullptr
        /// @param a_priority ジョブの優先度
        /// @return CueResult::ok() ならエンキュー成功、そうでない場合は失敗
        Result enqueue_batch_job(
            const std::string& a_batchName,
            std::vector<std::function<void()>> a_jobs,
            std::shared_future<void>& a_outFuture,
            JobPriority a_priority = JobPriority::Normal) noexcept
        {
            return enqueue_job(
                a_batchName,
                [jobs = std::move(a_jobs)]()
                {
                    for (const auto& job : jobs)
                    {
                        job();
                    }
                },
                a_outFuture,
                a_priority);
        }

        /// @brief 複数のジョブをまとめてエンキュー（完了通知不要版）、依存関係は std::shared_future<void> で指定
        /// @param a_batchName バッチの名前（デバッグ用、UTF-8想定）
        /// @param a_jobs ジョブの実装関数のリスト
        /// @param a_priority ジョブの優先度 
        /// @return CueResult::ok() ならエンキュー成功、そうでない場合は失敗
        Result enqueue_batch_job(
            const std::string& a_batchName,
            std::vector<std::function<void()>> a_jobs,
            JobPriority a_priority = JobPriority::Normal) noexcept
        {
            return enqueue_job(
                a_batchName,
                [jobs = std::move(a_jobs)]()
                {
                    for (const auto& job : jobs)
                    {
                        job();
                    }
                },
                a_priority);
        }

        /// @brief ジョブの完了を待機
        /// @param a_job 待機するジョブの future、ジョブ完了時に ready
        void wait_for_job(const std::shared_future<void>& a_job) noexcept
        {
            if (a_job.valid())
            {
                a_job.wait();
            }
        }

        /// @brief すべてのジョブの完了を待機、新規ジョブのエンキューもブロック
        void wait_for_all() noexcept
        {
            if (!m_isInitialized.load(std::memory_order_relaxed))
            {
                return;
            }

            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]()
                {
                    return m_inFlight.load(std::memory_order_relaxed) == 0
                        || m_stopRequested.load(std::memory_order_relaxed);
                });
        }

        /// @brief キューに存在するジョブの数を返す、実行中のジョブは含めない
        /// @return キューに存在するジョブの数
        std::size_t queued_job_count() const noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_high.ready.size() + m_high.blocked.size()
                + m_normal.ready.size() + m_normal.blocked.size()
                + m_low.ready.size() + m_low.blocked.size();
        }
        
        /// @brief ワーカーの数を返す
        /// @return ワーカーの数
        std::size_t worker_count() const noexcept
        {
            return m_workers.size();
        }

        /// @brief キューに存在するすべてのジョブをクリア、実行中のジョブは停止させない
        void clear_queued_jobs() noexcept
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            const std::size_t cleared = drain_queue_no_lock(m_high)
                + drain_queue_no_lock(m_normal)
                + drain_queue_no_lock(m_low);
            if (cleared == 0)
            {
                return;
            }

            const uint32_t decrease = cleared > std::numeric_limits<uint32_t>::max()
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(cleared);
            const uint32_t previous =
                m_inFlight.fetch_sub(decrease, std::memory_order_relaxed);
            if (previous < decrease)
            {
                m_inFlight.store(0, std::memory_order_relaxed);
            }
            m_cv.notify_all();
        }

    private:

        /// @brief ストレージの初期化
        /// @param a_jobCapacity 同時に存在できるジョブの最大数
        /// @param a_dependencyCapacity 同時に存在できる依存関係ノードの最大数
        /// @return CueResult::ok() なら初期化成功、そうでない場合は失敗
        Result initialize_storage(
            uint32_t a_jobCapacity,
            uint32_t a_dependencyCapacity) noexcept
        {
            try
            {
                m_jobStorage.clear();
                m_jobStorage.reserve(a_jobCapacity);
                m_freeJobs.clear();
                m_freeJobs.reserve(a_jobCapacity);

                m_dependencyStorage.clear();
                m_dependencyStorage.reserve(a_dependencyCapacity);
                m_freeDependencies.clear();
                m_freeDependencies.reserve(a_dependencyCapacity);
            }
            catch (...)
            {
                return Result::fail(
                    Code::OutOfMemory,
                    Severity::Error,
                    "Failed to reserve job system storage.");
            }

            for (uint32_t index = 0; index < a_jobCapacity; ++index)
            {
                auto job = std::unique_ptr<Job>(new (std::nothrow) Job{});
                if (!job)
                {
                    return Result::fail(
                        Code::OutOfMemory,
                        Severity::Error,
                        "Failed to allocate job storage.");
                }

                m_freeJobs.push_back(job.get());
                m_jobStorage.push_back(std::move(job));
            }

            for (uint32_t index = 0; index < a_dependencyCapacity; ++index)
            {
                auto node = std::unique_ptr<DependencyNode>(new (std::nothrow) DependencyNode{});
                if (!node)
                {
                    return Result::fail(
                        Code::OutOfMemory,
                        Severity::Error,
                        "Failed to allocate dependency storage.");
                }

                m_freeDependencies.push_back(node.get());
                m_dependencyStorage.push_back(std::move(node));
            }

            return Result::ok();
        }

        /// @brief ストレージのクリア
        void clear_storage() noexcept
        {
            m_jobStorage.clear();
            m_freeJobs.clear();
            m_dependencyStorage.clear();
            m_freeDependencies.clear();
        }

        /// @brief ジョブのエンキューの内部実装、完了通知と依存関係は std::shared_future<void> で行う
        /// @param a_jobName ジョブの名前（デバッグ用、UTF-8想定）
        /// @param a_job ジョブの実装関数、生のジョブ実装関数を使用する場合は無視
        /// @param a_rawProc 生のジョブ実装関数、通常のジョブ実装関数を使用する場合は nullptr
        /// @param a_rawContext 生のジョブ実装関数のコンテキスト、通常のジョブ実装関数を使用する場合は nullptr
        /// @param a_outFuture ジョブの完了通知を受け取るための std::shared_future<void>
        /// @param a_priority ジョブの優先度
        /// @param a_dependencies ジョブの依存関係
        /// @return ジョブのエンキュー結果
        Result enqueue_job_internal(
            std::string a_jobName,
            std::function<void()> a_job,
            jobRawProc a_rawProc,
            void* a_rawContext,
            std::shared_future<void>* a_outFuture,
            JobPriority a_priority,
            const std::vector<std::shared_future<void>>& a_dependencies) noexcept
        {
            if (!m_isInitialized.load(std::memory_order_relaxed))
            {
                return Result::fail(
                    Code::InvalidState,
                    Severity::Error,
                    "JobSystem is not initialized.");
            }

            try
            {
                std::lock_guard<std::mutex> lock(m_mutex);

                Job* job = allocate_job_no_lock();
                if (job == nullptr)
                {
                    return Result::fail(
                        Code::OutOfMemory,
                        Severity::Error,
                        "No free job slot is available.");
                }

                job->name = std::move(a_jobName);
                job->func = std::move(a_job);
                job->rawProc = a_rawProc;
                job->rawContext = a_rawContext;
                job->priority = a_priority;
                job->dependencyHead = nullptr;

                if (a_outFuture != nullptr)
                {
                    job->promise.emplace();
                    *a_outFuture = job->promise->get_future().share();
                }
                else
                {
                    job->promise.reset();
                }

                for (const auto& dependency : a_dependencies)
                {
                    if (!dependency.valid())
                    {
                        continue;
                    }

                    DependencyNode* node = allocate_dependency_no_lock();
                    if (node == nullptr)
                    {
                        reset_job_fields(*job);
                        free_job_no_lock(job);
                        return Result::fail(
                            Code::OutOfMemory,
                            Severity::Error,
                            "No free dependency slot is available.");
                    }

                    node->future = dependency;
                    node->next = job->dependencyHead;
                    node->isReady = false;
                    job->dependencyHead = node;
                }

                if (m_stopRequested.load(std::memory_order_relaxed))
                {
                    reset_job_fields(*job);
                    free_job_no_lock(job);
                    return Result::fail(
                        Code::InvalidState,
                        Severity::Error,
                        "JobSystem is stopping.");
                }

                const bool isReady = are_dependencies_ready_no_lock(*job);
                push_job_no_lock(job, isReady);
                m_inFlight.fetch_add(1, std::memory_order_relaxed);
            }
            catch (...)
            {
                return Result::fail(
                    Code::OutOfMemory,
                    Severity::Error,
                    "Failed to allocate job payload.");
            }

            m_cv.notify_one();
            return Result::ok();
        }

        /// @brief JobSystem のシャットダウン内部処理
        /// @param a_force 未初期化状態でも停止処理を進める場合は true
        void shutdown_internal(bool a_force) noexcept
        {
            if (!a_force && !m_isInitialized.load(std::memory_order_relaxed))
            {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopRequested.store(true, std::memory_order_relaxed);
            }
            m_cv.notify_all();

            for (auto& worker : m_workers)
            {
                if (worker)
                {
                    worker->request_stop();
                }
            }

            for (auto& worker : m_workers)
            {
                if (worker)
                {
                    // worker を破棄する前に終了を確認し、未完了 thread を解放して競合しないようにする
                    const Result result = worker->join();
                    CUE_ASSERT_FORMAT(success(result), "Failed to join job worker thread: {}", result.message.data());
                    worker.reset();
                }
            }
            m_workers.clear();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                drain_queue_no_lock(m_high);
                drain_queue_no_lock(m_normal);
                drain_queue_no_lock(m_low);
                m_inFlight.store(0, std::memory_order_relaxed);
            }
            m_cv.notify_all();

            clear_storage();
            m_isInitialized.store(false, std::memory_order_relaxed);
        }

        /// @brief ワーカースレッドのエントリポイント
        /// @param a_token 停止要求を監視するトークン
        /// @param a_user JobSystem インスタンス
        /// @return スレッドの終了コード
        static uint32_t worker_entry(StopToken a_token, void* a_user) noexcept
        {
            JobSystem* self = static_cast<JobSystem*>(a_user);
            if (self == nullptr)
            {
                return 0;
            }

            return self->worker_loop(a_token);
        }

        /// @brief ワーカースレッドのメインループ
        /// @param a_token 停止要求を監視するトークン
        /// @return スレッドの終了コード
        uint32_t worker_loop(StopToken a_token) noexcept
        {
            constexpr auto k_dependencyPollMin = std::chrono::milliseconds(1);
            constexpr auto k_dependencyPollMax = std::chrono::milliseconds(50);

            auto pollInterval = k_dependencyPollMin;
            uint64_t lastEpoch = m_dependencyEpoch.load(std::memory_order_relaxed);

            while (!a_token.stop_requested())
            {
                Job* job = nullptr;

                {
                    std::unique_lock<std::mutex> lock(m_mutex);

                    while (!a_token.stop_requested())
                    {
                        if (m_stopRequested.load(std::memory_order_relaxed))
                        {
                            return 0;
                        }

                        if (try_pop_ready_job_no_lock(job))
                        {
                            pollInterval = k_dependencyPollMin;
                            break;
                        }

                        if (has_blocked_jobs_no_lock())
                        {
                            const uint64_t epoch =
                                m_dependencyEpoch.load(std::memory_order_relaxed);
                            if (epoch != lastEpoch)
                            {
                                promote_ready_jobs_no_lock();
                                lastEpoch = epoch;
                                if (try_pop_ready_job_no_lock(job))
                                {
                                    pollInterval = k_dependencyPollMin;
                                    break;
                                }
                            }

                            const auto status = m_cv.wait_for(lock, pollInterval);
                            if (status == std::cv_status::timeout)
                            {
                                promote_ready_jobs_no_lock();
                                if (try_pop_ready_job_no_lock(job))
                                {
                                    pollInterval = k_dependencyPollMin;
                                    break;
                                }

                                if (pollInterval < k_dependencyPollMax)
                                {
                                    pollInterval *= 2;
                                    if (pollInterval > k_dependencyPollMax)
                                    {
                                        pollInterval = k_dependencyPollMax;
                                    }
                                }
                                lastEpoch = m_dependencyEpoch.load(std::memory_order_relaxed);
                            }
                            else
                            {
                                pollInterval = k_dependencyPollMin;
                            }
                        }
                        else
                        {
                            pollInterval = k_dependencyPollMin;
                            m_cv.wait(lock);
                        }
                    }
                }

                if (job == nullptr)
                {
                    continue;
                }

                execute_job(*job);

                uint32_t previous = 0;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    reset_job_fields(*job);
                    free_job_no_lock(job);
                    previous = m_inFlight.fetch_sub(1, std::memory_order_relaxed);
                    m_dependencyEpoch.fetch_add(1, std::memory_order_relaxed);
                }

                if (previous == 1)
                {
                    m_cv.notify_all();
                }
                else
                {
                    m_cv.notify_one();
                }
            }

            return 0;
        }

        /// @brief ジョブを優先度別キューへ追加
        /// @param a_job 追加するジョブ
        /// @param a_isReady 依存関係が解決済みなら true
        void push_job_no_lock(Job* a_job, bool a_isReady) noexcept
        {
            switch (a_job->priority)
            {
            case JobPriority::High:
                (a_isReady ? m_high.ready : m_high.blocked).push_back(a_job);
                break;
            case JobPriority::Normal:
                (a_isReady ? m_normal.ready : m_normal.blocked).push_back(a_job);
                break;
            case JobPriority::Low:
            default:
                (a_isReady ? m_low.ready : m_low.blocked).push_back(a_job);
                break;
            }
        }

        /// @brief 実行可能なジョブを優先度順に取り出し
        /// @param a_outJob 取り出したジョブの格納先
        /// @return ジョブを取り出した場合は true
        bool try_pop_ready_job_no_lock(Job*& a_outJob) noexcept
        {
            return try_pop_from_ready_queue_no_lock(m_high.ready, a_outJob)
                || try_pop_from_ready_queue_no_lock(m_normal.ready, a_outJob)
                || try_pop_from_ready_queue_no_lock(m_low.ready, a_outJob);
        }

        /// @brief 実行可能キューからジョブを 1 つ取り出し
        /// @param a_queue 取り出し元の実行可能キュー
        /// @param a_outJob 取り出したジョブの格納先
        /// @return ジョブを取り出した場合は true
        static bool try_pop_from_ready_queue_no_lock(
            std::vector<Job*>& a_queue,
            Job*& a_outJob) noexcept
        {
            if (a_queue.empty())
            {
                return false;
            }

            a_outJob = a_queue.back();
            a_queue.pop_back();
            return true;
        }

        /// @brief 依存関係が解決済みのジョブを実行可能キューへ移動
        void promote_ready_jobs_no_lock() noexcept
        {
            promote_ready_from_queue_no_lock(m_high);
            promote_ready_from_queue_no_lock(m_normal);
            promote_ready_from_queue_no_lock(m_low);
        }

        /// @brief 指定キュー内の実行可能になったジョブを移動
        /// @param a_queue 昇格対象のジョブキュー
        void promote_ready_from_queue_no_lock(JobQueue& a_queue) noexcept
        {
            for (std::size_t index = 0; index < a_queue.blocked.size();)
            {
                Job* candidate = a_queue.blocked[index];
                if (candidate != nullptr && are_dependencies_ready_no_lock(*candidate))
                {
                    a_queue.ready.push_back(candidate);
                    a_queue.blocked[index] = a_queue.blocked.back();
                    a_queue.blocked.pop_back();
                }
                else
                {
                    ++index;
                }
            }
        }

        /// @brief 依存関係待ちのジョブが存在するかを返す
        /// @return 依存関係待ちのジョブがある場合は true
        bool has_blocked_jobs_no_lock() const noexcept
        {
            return !m_high.blocked.empty()
                || !m_normal.blocked.empty()
                || !m_low.blocked.empty();
        }

        /// @brief ジョブの依存関係がすべて解決済みかを返す
        /// @param a_job 判定するジョブ
        /// @return すべての依存関係が解決済みなら true
        bool are_dependencies_ready_no_lock(Job& a_job) noexcept
        {
            if (a_job.dependencyHead == nullptr)
            {
                return true;
            }

            for (DependencyNode* node = a_job.dependencyHead; node != nullptr; node = node->next)
            {
                if (node->isReady)
                {
                    continue;
                }

                if (node->future.valid())
                {
                    const auto status = node->future.wait_for(std::chrono::seconds(0));
                    if (status != std::future_status::ready)
                    {
                        return false;
                    }
                }
                node->isReady = true;
            }

            return true;
        }

        /// @brief 指定キュー内のジョブを解放
        /// @param a_queue クリアするジョブキュー
        /// @return 解放したジョブの数
        std::size_t drain_queue_no_lock(JobQueue& a_queue) noexcept
        {
            std::size_t cleared = 0;

            for (Job* job : a_queue.ready)
            {
                if (job == nullptr)
                {
                    continue;
                }

                reset_job_fields(*job);
                free_job_no_lock(job);
                ++cleared;
            }

            for (Job* job : a_queue.blocked)
            {
                if (job == nullptr)
                {
                    continue;
                }

                reset_job_fields(*job);
                free_job_no_lock(job);
                ++cleared;
            }

            a_queue.ready.clear();
            a_queue.blocked.clear();
            return cleared;
        }

        /// @brief ジョブの再利用前に保持フィールドを初期化
        /// @param a_job 初期化するジョブ
        void reset_job_fields(Job& a_job) noexcept
        {
            a_job.name.clear();
            a_job.func = {};
            a_job.rawProc = nullptr;
            a_job.rawContext = nullptr;
            clear_dependencies(a_job);
            a_job.promise.reset();
        }

        /// @brief ジョブに紐づく依存関係ノードを解放
        /// @param a_job 依存関係をクリアするジョブ
        void clear_dependencies(Job& a_job) noexcept
        {
            DependencyNode* node = a_job.dependencyHead;
            while (node != nullptr)
            {
                DependencyNode* next = node->next;
                node->future = {};
                node->next = nullptr;
                node->isReady = false;
                free_dependency_no_lock(node);
                node = next;
            }

            a_job.dependencyHead = nullptr;
        }

        /// @brief promise に完了を通知、例外は握りつぶす
        /// @param a_promise 完了通知先の promise
        static void set_promise_value_noexcept(std::promise<void>& a_promise) noexcept
        {
            try
            {
                a_promise.set_value();
            }
            catch (...)
            {
            }
        }

        /// @brief promise に例外を通知、例外は握りつぶす
        /// @param a_promise 例外通知先の promise
        /// @param a_exception 通知する例外
        static void set_promise_exception_noexcept(
            std::promise<void>& a_promise,
            std::exception_ptr a_exception) noexcept
        {
            try
            {
                a_promise.set_exception(std::move(a_exception));
            }
            catch (...)
            {
            }
        }

        /// @brief ジョブを実行し、完了または例外を通知
        /// @param a_job 実行するジョブ
        static void execute_job(Job& a_job) noexcept
        {
            try
            {
                if (a_job.rawProc != nullptr)
                {
                    a_job.rawProc(a_job.rawContext);
                }
                else if (a_job.func)
                {
                    a_job.func();
                }

                if (a_job.promise)
                {
                    set_promise_value_noexcept(*a_job.promise);
                }
            }
            catch (...)
            {
                if (a_job.promise)
                {
                    set_promise_exception_noexcept(
                        *a_job.promise,
                        std::current_exception());
                }
            }
        }

        /// @brief 空きジョブスロットを取得
        /// @return 取得したジョブ、空きがない場合は nullptr
        Job* allocate_job_no_lock() noexcept
        {
            if (m_freeJobs.empty())
            {
                return nullptr;
            }

            Job* job = m_freeJobs.back();
            m_freeJobs.pop_back();
            return job;
        }

        /// @brief ジョブスロットを空きリストへ戻す
        /// @param a_job 解放するジョブ
        void free_job_no_lock(Job* a_job) noexcept
        {
            if (a_job == nullptr)
            {
                return;
            }

            m_freeJobs.push_back(a_job);
        }

        /// @brief 空き依存関係ノードを取得
        /// @return 取得した依存関係ノード、空きがない場合は nullptr
        DependencyNode* allocate_dependency_no_lock() noexcept
        {
            if (m_freeDependencies.empty())
            {
                return nullptr;
            }

            DependencyNode* node = m_freeDependencies.back();
            m_freeDependencies.pop_back();
            return node;
        }

        /// @brief 依存関係ノードを空きリストへ戻す
        /// @param a_node 解放する依存関係ノード
        void free_dependency_no_lock(DependencyNode* a_node) noexcept
        {
            if (a_node == nullptr)
            {
                return;
            }

            m_freeDependencies.push_back(a_node);
        }

        /// @brief ワーカースレッド名を生成
        /// @param a_index ワーカーのインデックス
        /// @return ワーカースレッド名
        static std::string make_worker_name(uint32_t a_index)
        {
            return "JobWorker_" + std::to_string(a_index);
        }

    private:
        mutable std::mutex m_mutex{}; // ジョブキューとストレージを保護する mutex
        std::condition_variable m_cv{}; // ワーカー待機と完了待機の通知

        std::atomic<bool> m_isInitialized{ false }; // 初期化済みか
        std::atomic<bool> m_stopRequested{ false }; // 停止要求が出ているか
        std::atomic<uint32_t> m_inFlight{ 0 }; // 実行中またはキュー内のジョブ数
        std::atomic<uint64_t> m_dependencyEpoch{ 0 }; // 依存関係解決確認用の世代

        std::vector<std::unique_ptr<IThread>> m_workers{}; // ワーカースレッド

        JobQueue m_high{}; // 高優先度ジョブキュー
        JobQueue m_normal{}; // 通常優先度ジョブキュー
        JobQueue m_low{}; // 低優先度ジョブキュー

        std::vector<std::unique_ptr<Job>> m_jobStorage{}; // ジョブスロットの所有ストレージ
        std::vector<Job*> m_freeJobs{}; // 空きジョブスロット
        std::vector<std::unique_ptr<DependencyNode>> m_dependencyStorage{}; // 依存関係ノードの所有ストレージ
        std::vector<DependencyNode*> m_freeDependencies{}; // 空き依存関係ノード
    };
}
