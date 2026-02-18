#pragma once

// === C++ Standard Library ===
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// === Engine ===
#include "Core/Threading/IThread.h"
#include "Core/Threading/IThreadFactory.h"
#include "Core/Memory/FixedBlockPool.h"
#include "Core/Error/Result.h"
#include <Threadin>

namespace Drama::Core::Threading
{
    class JobSystem final
    {
    public:
        enum class JobPriority : uint8_t
        {
            High = 0,
            Normal = 1,
            Low = 2,
        };

        using JobRawFunc = void(*)(void*);

        struct Job final
        {
            std::string name{};
            std::function<void()> func{};
            JobRawFunc rawFunc = nullptr;
            void* rawContext = nullptr;
            JobPriority priority = JobPriority::Normal;
            struct DependencyNode final
            {
                std::shared_future<void> future{};
                DependencyNode* next = nullptr;
                bool isReady = false;
            };
            DependencyNode* dependencyHead = nullptr;

            // NOTE:
            //  - 共有ポインタをやめて、Job が “その場” に居続ける前提で promise を値で持つ
            //  - キューが Job* を持つので、Job の move/copy を原則ゼロにできる
            std::optional<std::promise<void>> promise{};
        };

        JobSystem() noexcept = default;

        ~JobSystem() noexcept
        {
            // 1) 破棄時に取り残しを避ける
            shutdown();
        }

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        Core::Error::Result initialize(
            IThreadFactory& factory,
            uint32_t requestedWorkerCount = 0,
            uint32_t maxWorkerCount = 64,
            uint32_t maxJobsInFlight = 4096,
            uint32_t maxDependencyNodes = 0) noexcept
        {
            // 1) 二重初期化防止
            if (m_isInitialized.load(std::memory_order_relaxed))
            {
                return Core::Error::Result::ok();
            }

            // 2) ワーカー数決定（0なら自動）
            uint32_t workerCount = requestedWorkerCount;
            if (workerCount == 0)
            {
                const uint32_t hc = std::thread::hardware_concurrency();
                workerCount = (hc != 0) ? hc : 4;
            }
            if (workerCount > maxWorkerCount)
            {
                workerCount = maxWorkerCount;
            }
            if (workerCount == 0)
            {
                workerCount = 1;
            }

            // 3) ジョブプール初期化（ここで “置き場” を確保して、以降の enqueue でのヒープ確保を減らす）
            {
                const Core::Error::Result pr = pool_init(m_jobPool, maxJobsInFlight);
                if (!pr)
                {
                    return pr;
                }
            }

            // 4) 依存ノードプール初期化（0なら maxJobsInFlight * 4 を採用）
            uint32_t dependencyCapacity = maxDependencyNodes;
            if (dependencyCapacity == 0)
            {
                constexpr uint32_t k_perJobDeps = 4;
                const uint64_t calc = static_cast<uint64_t>(maxJobsInFlight) * k_perJobDeps;
                dependencyCapacity = (calc > UINT32_MAX)
                    ? UINT32_MAX
                    : static_cast<uint32_t>(calc);
            }
            {
                const Core::Error::Result dr = pool_init(m_dependencyPool, dependencyCapacity);
                if (!dr)
                {
                    pool_destroy(m_jobPool);
                    return dr;
                }
            }

            // 5) 状態初期化
            m_stopRequested.store(false, std::memory_order_relaxed);
            m_inFlight.store(0, std::memory_order_relaxed);
            m_dependencyEpoch.store(0, std::memory_order_relaxed);

            // 6) キュー/スレッド確保
            m_high.ready.clear();
            m_high.blocked.clear();
            m_normal.ready.clear();
            m_normal.blocked.clear();
            m_low.ready.clear();
            m_low.blocked.clear();

            m_workers.clear();
            m_workers.resize(workerCount);

            // 7) ワーカースレッド生成
            for (uint32_t i = 0; i < workerCount; ++i)
            {
                const std::string workerName = make_worker_name(i);

                ThreadDesc desc{};
                desc.name = workerName;
                desc.stackSizeBytes = 0;
                desc.priority = 0;
                desc.affinityMask = 0;

                std::unique_ptr<IThread> th{};
                const auto r = factory.create_thread(desc, &JobSystem::worker_entry, this, th);
                if (!r)
                {
                    shutdown_internal(true);
                    return r;
                }

                m_workers[i] = std::move(th);
            }

            m_isInitialized.store(true, std::memory_order_relaxed);
            return Core::Error::Result::ok();
        }

        void shutdown() noexcept
        {
            shutdown_internal(false);
        }

        Core::Error::Result enqueue_job(
            std::string jobName,
            std::function<void()> job,
            std::shared_future<void>& outFuture,
            JobPriority priority = JobPriority::Normal,
            const std::vector<std::shared_future<void>>& dependencies = {}) noexcept
        {
            return enqueue_job_internal(
                std::move(jobName),
                std::move(job),
                nullptr,
                nullptr,
                &outFuture,
                priority,
                dependencies);
        }

        Core::Error::Result enqueue_job(
            std::string jobName,
            std::function<void()> job,
            JobPriority priority = JobPriority::Normal,
            const std::vector<std::shared_future<void>>& dependencies = {}) noexcept
        {
            return enqueue_job_internal(
                std::move(jobName),
                std::move(job),
                nullptr,
                nullptr,
                nullptr,
                priority,
                dependencies);
        }

        Core::Error::Result enqueue_job_raw(
            std::string jobName,
            JobRawFunc func,
            void* context,
            std::shared_future<void>& outFuture,
            JobPriority priority = JobPriority::Normal,
            const std::vector<std::shared_future<void>>& dependencies = {}) noexcept
        {
            return enqueue_job_internal(
                std::move(jobName),
                {},
                func,
                context,
                &outFuture,
                priority,
                dependencies);
        }

        Core::Error::Result enqueue_job_raw(
            std::string jobName,
            JobRawFunc func,
            void* context,
            JobPriority priority = JobPriority::Normal,
            const std::vector<std::shared_future<void>>& dependencies = {}) noexcept
        {
            return enqueue_job_internal(
                std::move(jobName),
                {},
                func,
                context,
                nullptr,
                priority,
                dependencies);
        }

        Core::Error::Result enqueue_batch_job(
            const std::string& batchName,
            std::vector<std::function<void()>> jobs,
            std::shared_future<void>& outFuture,
            JobPriority priority = JobPriority::Normal) noexcept
        {
            // 1) バッチは1ジョブとして登録
            return enqueue_job(
                batchName,
                [jobs = std::move(jobs)]()
                {
                    // 1) 逐次実行（必要なら将来ここを並列化）
                    for (const auto& f : jobs)
                    {
                        f();
                    }
                },
                outFuture,
                priority);
        }

        Core::Error::Result enqueue_batch_job(
            const std::string& batchName,
            std::vector<std::function<void()>> jobs,
            JobPriority priority = JobPriority::Normal) noexcept
        {
            // 1) バッチは1ジョブとして登録
            return enqueue_job(
                batchName,
                [jobs = std::move(jobs)]()
                {
                    // 1) 逐次実行（必要なら将来ここを並列化）
                    for (const auto& f : jobs)
                    {
                        f();
                    }
                },
                priority);
        }

        void wait_for_job(const std::shared_future<void>& job) noexcept
        {
            // 1) validなら待つ
            if (job.valid())
            {
                job.wait();
            }
        }

        void wait_for_all() noexcept
        {
            // 1) 停止中なら待たない
            if (!m_isInitialized.load(std::memory_order_relaxed))
            {
                return;
            }

            // 2) inFlight が 0 になるまで待つ（停止要求が来たら抜ける）
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]()
                {
                    return (m_inFlight.load(std::memory_order_relaxed) == 0)
                        || m_stopRequested.load(std::memory_order_relaxed);
                });
        }

        std::size_t queued_job_count() const noexcept
        {
            // 1) キューサイズ合算
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_high.ready.size() + m_high.blocked.size()
                + m_normal.ready.size() + m_normal.blocked.size()
                + m_low.ready.size() + m_low.blocked.size();
        }

        std::size_t worker_count() const noexcept
        {
            // 1) ワーカー数
            return m_workers.size();
        }

        void clear_queued_jobs() noexcept
        {
            // 1) 実行待ちだけ消す（実行中ジョブは対象外）
            std::lock_guard<std::mutex> lock(m_mutex);
            const std::size_t cleared =
                drain_queue_no_lock(m_high)
                + drain_queue_no_lock(m_normal)
                + drain_queue_no_lock(m_low);
            if (cleared > 0)
            {
                const auto dec = static_cast<uint32_t>(
                    (cleared > UINT32_MAX) ? UINT32_MAX : cleared);
                const uint32_t prev = m_inFlight.fetch_sub(dec, std::memory_order_relaxed);
                if (prev < dec)
                {
                    m_inFlight.store(0, std::memory_order_relaxed);
                }
                m_cv.notify_all();
            }
        }

    private:
        struct JobQueue final
        {
            std::vector<Job*> ready{};
            std::vector<Job*> blocked{};
        };

        void shutdown_internal(bool force) noexcept
        {
            // 1) 初期化されていなければ何もしない
            if (!force && !m_isInitialized.load(std::memory_order_relaxed))
            {
                return;
            }

            // 2) 停止フラグを立てる
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopRequested.store(true, std::memory_order_relaxed);
            }

            // 3) 待機中ワーカーを起こす
            m_cv.notify_all();

            // 4) スレッド側にも停止要求
            for (auto& th : m_workers)
            {
                if (th)
                {
                    th->request_stop();
                }
            }

            // 5) join
            for (auto& th : m_workers)
            {
                if (th)
                {
                    (void)th->join();
                    th.reset();
                }
            }

            // 6) キューに残ったジョブを回収（promise は破棄されるので wait は解除される）
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                drain_queue_no_lock(m_high);
                drain_queue_no_lock(m_normal);
                drain_queue_no_lock(m_low);

                // join 済みなので inFlight は強制的に 0 に戻す
                m_inFlight.store(0, std::memory_order_relaxed);
            }
            m_cv.notify_all();

            // 7) プール解放
            pool_destroy(m_jobPool);
            pool_destroy(m_dependencyPool);

            m_isInitialized.store(false, std::memory_order_relaxed);
        }

        Core::Error::Result enqueue_job_internal(
            std::string jobName,
            std::function<void()> job,
            JobRawFunc rawFunc,
            void* rawContext,
            std::shared_future<void>* outFuture,
            JobPriority priority,
            const std::vector<std::shared_future<void>>& dependencies) noexcept
        {
            // 1) 初期化チェック
            if (!m_isInitialized.load(std::memory_order_relaxed))
            {
                return Core::Error::Result::fail(
                    Core::Error::Facility::Core,
                    Core::Error::Code::InvalidState,
                    Core::Error::Severity::Error,
                    0,
                    "JobSystem is not initialized.");
            }

            // 2) Job をプールから確保（キューは Job* を持つ）
            Job* j = nullptr;
            {
                const Core::Error::Result ar = pool_alloc(m_jobPool, j);
                if (!ar)
                {
                    return ar;
                }
            }

            // 3) Job を組み立てる（必要なら promise/future を作る）
            j->name = std::move(jobName);
            if (rawFunc != nullptr)
            {
                j->func = {};
                j->rawFunc = rawFunc;
                j->rawContext = rawContext;
            }
            else
            {
                j->func = std::move(job);
                j->rawFunc = nullptr;
                j->rawContext = nullptr;
            }
            j->priority = priority;
            j->dependencyHead = nullptr;

            if (outFuture != nullptr)
            {
                j->promise.emplace();
                *outFuture = j->promise->get_future().share();
            }
            else
            {
                j->promise.reset();
            }

            // 4) 依存ノードをプールから確保
            for (const auto& dep : dependencies)
            {
                if (!dep.valid())
                {
                    continue;
                }

                Job::DependencyNode* node = nullptr;
                const Core::Error::Result dr = pool_alloc(m_dependencyPool, node);
                if (!dr)
                {
                    reset_job_fields(*j);
                    pool_free(m_jobPool, j);
                    return dr;
                }

                node->future = dep;
                node->next = j->dependencyHead;
                node->isReady = false;
                j->dependencyHead = node;
            }

            const bool ready = are_dependencies_ready_no_lock(*j);

            // 5) キューに積む（停止中なら回収してエラー）
            {
                std::lock_guard<std::mutex> lock(m_mutex);

                if (m_stopRequested.load(std::memory_order_relaxed))
                {
                    // 1) 組み立て済み Job を戻す
                    reset_job_fields(*j);
                    pool_free(m_jobPool, j);

                    return Core::Error::Result::fail(
                        Core::Error::Facility::Core,
                        Core::Error::Code::InvalidState,
                        Core::Error::Severity::Error,
                        0,
                        "JobSystem is stopping.");
                }

                push_job_no_lock(j, ready);
                m_inFlight.fetch_add(1, std::memory_order_relaxed);
            }

            // 6) ワーカーを起こす
            m_cv.notify_one();
            return Core::Error::Result::ok();
        }

        // ---------- FixedBlockPool API 吸収 ----------
        template<class Pool>
        static Core::Error::Result pool_init(Pool& pool, uint32_t capacity) noexcept
        {
            // 1) よくある候補を順に試す（存在するものだけコンパイルされる）
            if constexpr (requires(Pool p, uint32_t c) { p.initialize(c); })
            {
                return pool.initialize(capacity);
            }
            else if constexpr (requires(Pool p, uint32_t c) { p.init(c); })
            {
                return pool.init(capacity);
            }
            else if constexpr (requires(Pool p, uint32_t c) { p.create(c); })
            {
                return pool.create(capacity);
            }
            else
            {
                return Core::Error::Result::fail(
                    Core::Error::Facility::Core,
                    Core::Error::Code::Unsupported,
                    Core::Error::Severity::Error,
                    0,
                    "FixedBlockPool: init method not found.");
            }
        }

        template<class Pool, class T>
        static Core::Error::Result pool_alloc(Pool& pool, T*& outPtr) noexcept
        {
            // 1) allocate / alloc / try_allocate を吸収
            if constexpr (requires(Pool p, T * &o) { p.allocate(o); })
            {
                return pool.allocate(outPtr);
            }
            else if constexpr (requires(Pool p, T * &o) { p.alloc(o); })
            {
                return pool.alloc(outPtr);
            }
            else if constexpr (requires(Pool p, T * &o) { p.try_allocate(o); })
            {
                return pool.try_allocate(outPtr);
            }
            else if constexpr (requires(Pool p, T * &o) { p.try_alloc(o); })
            {
                return pool.try_alloc(outPtr);
            }
            else
            {
                return Core::Error::Result::fail(
                    Core::Error::Facility::Core,
                    Core::Error::Code::Unsupported,
                    Core::Error::Severity::Error,
                    0,
                    "FixedBlockPool: alloc method not found.");
            }
        }

        template<class Pool, class T>
        static void pool_free(Pool& pool, T* ptr) noexcept
        {
            // 1) free / deallocate / release を吸収
            if constexpr (requires(Pool p, T * x) { p.free(x); })
            {
                pool.free(ptr);
            }
            else if constexpr (requires(Pool p, T * x) { p.deallocate(x); })
            {
                pool.deallocate(ptr);
            }
            else if constexpr (requires(Pool p, T * x) { p.release(x); })
            {
                pool.release(ptr);
            }
            else
            {
                // 2) ここに来る設計はダメ（静かにリークする）
                //    API 名を合わせろ
                (void)pool;
                (void)ptr;
            }
        }

        template<class Pool>
        static void pool_destroy(Pool& pool) noexcept
        {
            // 1) destroy を吸収
            if constexpr (requires(Pool p) { p.destroy(); })
            {
                pool.destroy();
            }
        }

        static void set_promise_value_noexcept(std::promise<void>& promise) noexcept
        {
            try
            {
                promise.set_value();
            }
            catch (...)
            {
                // 共有状態が無い/二重完了などの異常は握りつぶす
            }
        }

        static void set_promise_exception_noexcept(
            std::promise<void>& promise,
            std::exception_ptr eptr) noexcept
        {
            try
            {
                promise.set_exception(std::move(eptr));
            }
            catch (...)
            {
                // 共有状態が無い/二重完了などの異常は握りつぶす
            }
        }

        static void execute_job(Job& job) noexcept
        {
            try
            {
                if (job.rawFunc != nullptr)
                {
                    job.rawFunc(job.rawContext);
                }
                else
                {
                    job.func();
                }
                if (job.promise)
                {
                    set_promise_value_noexcept(*job.promise);
                }
            }
            catch (...)
            {
                if (job.promise)
                {
                    set_promise_exception_noexcept(*job.promise, std::current_exception());
                }
            }
        }

        // ---------- Worker ----------
        static uint32_t worker_entry(StopToken token, void* user) noexcept
        {
            // 1) this を取得
            auto* self = static_cast<JobSystem*>(user);
            if (!self)
            {
                return 0;
            }

            // 2) ループ実行
            return self->worker_loop(token);
        }

        uint32_t worker_loop(StopToken token) noexcept
        {
            constexpr auto k_dependencyPollMin = std::chrono::milliseconds(1);
            constexpr auto k_dependencyPollMax = std::chrono::milliseconds(50);
            auto pollInterval = k_dependencyPollMin;
            uint64_t lastEpoch = m_dependencyEpoch.load(std::memory_order_relaxed);

            while (!token.stop_requested())
            {
                Job* job = nullptr;

                // 1) ジョブ取得（無ければ待つ）
                {
                    std::unique_lock<std::mutex> lock(m_mutex);

                    while (!token.stop_requested())
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
                            const uint64_t epoch = m_dependencyEpoch.load(std::memory_order_relaxed);
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

                            // 2) 依存待ちが長い場合はバックオフしてCPU使用率を下げる
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
                            // 3) 空なら通知待ち（enqueue/完了で起きる）
                            pollInterval = k_dependencyPollMin;
                            m_cv.wait(lock);
                        }
                    }
                }

                if (!job)
                {
                    continue;
                }

                // 2) 実行
                execute_job(*job);

                // 3) ジョブ回収（次回再利用できるようにフィールドを掃除）
                reset_job_fields(*job);
                pool_free(m_jobPool, job);

                // 4) inFlight 減算して通知（依存が解ける可能性がある）
                const uint32_t prev = m_inFlight.fetch_sub(1, std::memory_order_relaxed);
                m_dependencyEpoch.fetch_add(1, std::memory_order_relaxed);
                if (prev == 1)
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

        // ---------- Queue ----------
        void push_job_no_lock(Job* j, bool ready) noexcept
        {
            // 1) 優先度別に格納
            switch (j->priority)
            {
            case JobPriority::High:
            {
                (ready ? m_high.ready : m_high.blocked).push_back(j);
                break;
            }
            case JobPriority::Normal:
            {
                (ready ? m_normal.ready : m_normal.blocked).push_back(j);
                break;
            }
            case JobPriority::Low:
            default:
            {
                (ready ? m_low.ready : m_low.blocked).push_back(j);
                break;
            }
            }
        }

        bool try_pop_ready_job_no_lock(Job*& outJob) noexcept
        {
            // 1) 高→通常→低 の順で探す
            if (try_pop_from_ready_queue_no_lock(m_high.ready, outJob))
            {
                return true;
            }
            if (try_pop_from_ready_queue_no_lock(m_normal.ready, outJob))
            {
                return true;
            }
            if (try_pop_from_ready_queue_no_lock(m_low.ready, outJob))
            {
                return true;
            }

            return false;
        }

        bool try_pop_from_ready_queue_no_lock(std::vector<Job*>& q, Job*& outJob) noexcept
        {
            if (q.empty())
            {
                return false;
            }

            outJob = q.back();
            q.pop_back();
            return true;
        }

        void promote_ready_jobs_no_lock() noexcept
        {
            promote_ready_from_queue_no_lock(m_high);
            promote_ready_from_queue_no_lock(m_normal);
            promote_ready_from_queue_no_lock(m_low);
        }

        void promote_ready_from_queue_no_lock(JobQueue& q) noexcept
        {
            for (std::size_t i = 0; i < q.blocked.size();)
            {
                Job* cand = q.blocked[i];
                if (cand && are_dependencies_ready_no_lock(*cand))
                {
                    q.ready.push_back(cand);
                    q.blocked[i] = q.blocked.back();
                    q.blocked.pop_back();
                }
                else
                {
                    ++i;
                }
            }
        }

        bool has_ready_jobs_no_lock() const noexcept
        {
            return (!m_high.ready.empty() || !m_normal.ready.empty() || !m_low.ready.empty());
        }

        bool has_blocked_jobs_no_lock() const noexcept
        {
            return (!m_high.blocked.empty() || !m_normal.blocked.empty() || !m_low.blocked.empty());
        }

        bool has_queued_jobs_no_lock() const noexcept
        {
            return has_ready_jobs_no_lock() || has_blocked_jobs_no_lock();
        }

        bool are_dependencies_ready_no_lock(Job& job) noexcept
        {
            // 1) 依存が無ければ即実行
            if (job.dependencyHead == nullptr)
            {
                return true;
            }

            // 2) 依存futureが ready か確認
            for (auto* node = job.dependencyHead; node != nullptr; node = node->next)
            {
                if (node->isReady)
                {
                    continue;
                }

                if (node->future.valid())
                {
                    const auto st = node->future.wait_for(std::chrono::seconds(0));
                    if (st != std::future_status::ready)
                    {
                        return false;
                    }
                }
                node->isReady = true;
            }

            return true;
        }

        std::size_t drain_queue_no_lock(JobQueue& q) noexcept
        {
            // 1) 残ジョブをすべて回収
            std::size_t cleared = 0;
            for (Job* j : q.ready)
            {
                if (j)
                {
                    reset_job_fields(*j);
                    pool_free(m_jobPool, j);
                    ++cleared;
                }
            }
            for (Job* j : q.blocked)
            {
                if (j)
                {
                    reset_job_fields(*j);
                    pool_free(m_jobPool, j);
                    ++cleared;
                }
            }
            q.ready.clear();
            q.blocked.clear();
            return cleared;
        }

        void reset_job_fields(Job& j) noexcept
        {
            // 1) 再利用のために “重い要素” を空にする
            j.name.clear();
            j.func = {};
            j.rawFunc = nullptr;
            j.rawContext = nullptr;
            clear_dependencies(j);
            j.promise.reset();

            // 2) promise は次 enqueue で作り直す
        }

        void clear_dependencies(Job& j) noexcept
        {
            Job::DependencyNode* node = j.dependencyHead;
            while (node != nullptr)
            {
                Job::DependencyNode* next = node->next;
                pool_free(m_dependencyPool, node);
                node = next;
            }
            j.dependencyHead = nullptr;
        }

        static std::string make_worker_name(uint32_t index)
        {
            // 1) デバッグ用（初期化時だけなので簡易でOK）
            return "JobWorker_" + std::to_string(index);
        }

    private:
        mutable std::mutex m_mutex{};
        std::condition_variable m_cv{};

        std::atomic<bool> m_isInitialized{ false };
        std::atomic<bool> m_stopRequested{ false };
        std::atomic<uint32_t> m_inFlight{ 0 };
        std::atomic<uint64_t> m_dependencyEpoch{ 0 };

        std::vector<std::unique_ptr<IThread>> m_workers{};

        // 1) 優先度別キュー（ポインタだけ保持）
        JobQueue m_high{};
        JobQueue m_normal{};
        JobQueue m_low{};

        // 2) Job の置き場（FixedBlockPool）
        Core::Memory::FixedBlockPool<Job> m_jobPool{};
        Core::Memory::FixedBlockPool<Job::DependencyNode> m_dependencyPool{};
    };
}
