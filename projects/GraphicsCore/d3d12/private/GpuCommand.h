#pragma once
#include "stdafx.h"
#include <Pool.h>

#include <functional>
#include <mutex>

namespace Cue::GraphicsCore::DX12
{
    class CommandContext
    {
    public:
        CommandContext(ID3D12Device& device);
        virtual ~CommandContext() = default;
        Result initialize(D3D12_COMMAND_LIST_TYPE type);
        Result reset();
        Result close();
        ID3D12GraphicsCommandList* get_command_list() { return m_commandList.Get(); }
        ID3D12CommandAllocator* get_command_allocator() { return m_commandAllocator.Get(); }
        bool is_list_empty() const { return m_listEmpty; }

        /* === Commands === */
    private:
        Result create_command_allocator(D3D12_COMMAND_LIST_TYPE type);
        Result create_command_list(D3D12_COMMAND_LIST_TYPE type);
    protected:
        ID3D12Device& m_device;
        bool m_listEmpty = true;
        ComPtr<ID3D12GraphicsCommandList> m_commandList = nullptr;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator = nullptr;
    };

    class GraphicsCommandContext : public CommandContext
    {
    public:
        GraphicsCommandContext(ID3D12Device& device)
            :CommandContext(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
        }
        ~GraphicsCommandContext() = default;
    };

    class ComputeCommandContext : public CommandContext
    {
    public:
        ComputeCommandContext(ID3D12Device& device)
            :CommandContext(device, D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
        }
        ~ComputeCommandContext() = default;
    };

    class CopyCommandContext : public CommandContext
    {
    public:
        CopyCommandContext(ID3D12Device& device)
            :CommandContext(device, D3D12_COMMAND_LIST_TYPE_COPY)
        {
        }
        ~CopyCommandContext() = default;
    };

    class CommandPool final
    {
    public:
        /// @brief コンストラクタ
        CommandPool(ID3D12Device& device)
            : m_device(device)
        {
        }
        /// @brief デストラクタ
        ~CommandPool() = default;

        // Context取得
        Core::Pool<GraphicsCommandContext, std::function<void(GraphicsCommandContext&)>>::pooled_ptr get_graphics_context() noexcept
        {
            std::lock_guard<std::mutex> lock(m_graphicsContextPoolMutex);
            return m_graphicsContextPool.acquire();
        }

        Core::Pool<ComputeCommandContext, std::function<void(ComputeCommandContext&)>>::pooled_ptr get_compute_context() noexcept
        {
            std::lock_guard<std::mutex> lock(m_computeContextPoolMutex);
            return m_computeContextPool.acquire();
        }

        Core::Pool<CopyCommandContext, std::function<void(CopyCommandContext&)>>::pooled_ptr get_copy_context() noexcept
        {
            std::lock_guard<std::mutex> lock(m_copyContextPoolMutex);
            return m_copyContextPool.acquire();
        }
    private:
        ID3D12Device& m_device;

        Core::Pool<GraphicsCommandContext, std::function<void(GraphicsCommandContext&)>> m_graphicsContextPool{
            32,
            [](GraphicsCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_graphicsContextPoolMutex;

        Core::Pool<ComputeCommandContext, std::function<void(ComputeCommandContext&)>> m_computeContextPool{
            32,
            [](ComputeCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_computeContextPoolMutex;

        Core::Pool<CopyCommandContext, std::function<void(CopyCommandContext&)>> m_copyContextPool{
            32,
            [](CopyCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_copyContextPoolMutex;
    };

    class QueueContext
    {
    public:
        /// @brief コンストラクタ
        QueueContext(ID3D12Device& device)
            : m_device(device)
        {

        }
        /// @brief デストラクタ
        virtual ~QueueContext()
        {
            flush();
            if (m_fenceEvent)
            {
                CloseHandle(m_fenceEvent);
                m_fenceEvent = nullptr;
            }
            m_fence.Reset();
            m_commandQueue.Reset();
        }

        Result initialize(D3D12_COMMAND_LIST_TYPE type)
        {
            // フェンスの作成
            m_fence.Reset();
            m_fenceValue = 0;// 初期値0
            HRESULT hr = m_device.CreateFence(
                m_fenceValue,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&m_fence));
            if (FAILED(hr))
            {
                return Result::fail(
                    Core::Facility::Graphics,
                    Core::Code::CreationFailed,
                    Core::Severity::Error,
                    static_cast<uint32_t>(hr),
                    "Failed to create Fence.");
            }
            SetD3D12Name(m_fence.Get(), L"QueueContext Fence");
            m_fenceValue++;// 次回以降のシグナル用にインクリメントしておく
            // イベントハンドルの作成
            m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (m_fenceEvent == nullptr)
            {
                Result::fail(
                    Core::Facility::Graphics,
                    Core::Code::CreationFailed,
                    Core::Severity::Error,
                    GetLastError(),
                    "Failed to create Fence event handle.");
            }
            // コマンドキューの作成
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = type;
            hr = m_device.CreateCommandQueue(
                &queueDesc,
                IID_PPV_ARGS(&m_commandQueue));
            if (FAILED(hr))
            {
                Result::fail(
                    Core::Facility::Graphics,
                    Core::Code::CreationFailed,
                    Core::Severity::Error,
                    static_cast<uint32_t>(hr),
                    "Failed to create CommandQueue.");
            }
            SetD3D12Name(m_commandQueue.Get(), L"QueueContext CommandQueue");
            return Result::ok();
        }

        void execute(CommandContext* ctx)
        {
            if (ctx)
            {
                ID3D12CommandList* lists[] = { ctx->get_command_list() };
                m_commandQueue->ExecuteCommandLists(1, lists);
            }
            m_fenceValue++;
            // GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
            m_commandQueue->Signal(m_fence.Get(), m_fenceValue);
        }
        void flush()
        {
            const UINT64 fence = ++m_fenceValue;
            m_commandQueue->Signal(m_fence.Get(), fence);

            if (m_fence->GetCompletedValue() < fence)
            {
                m_fence->SetEventOnCompletion(fence, m_fenceEvent);
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }
        }
        void wait_fence()
        {
            // Fenceの値が指定したSignal値にたどり着いているか確認する
            // GetCompletedValueの初期値はFence作成時に渡した初期値
            if (!m_fenceValue) { return; }
            if (m_fence->GetCompletedValue() < m_fenceValue)
            {
                // 指定したSignalにたどり着いていないので、たどり着くまで待つようにイベントを設定する
                m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
                // イベント待つ
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }
        }

        ID3D12CommandQueue* get_command_queue() noexcept { return m_commandQueue.Get(); };
        ID3D12Fence* get_fence() noexcept { return m_fence.Get(); };
        uint64_t get_fence_value() noexcept { return m_fenceValue; };
    private:
        ID3D12Device& m_device;
        ComPtr<ID3D12CommandQueue> m_commandQueue = nullptr;
        ComPtr<ID3D12Fence> m_fence = nullptr;
        HANDLE m_fenceEvent = {};
        uint64_t m_fenceValue = {};
    };

    class GraphicsQueueContext final : public QueueContext
    {
    public:
        /// @brief コンストラクタ
        GraphicsQueueContext(ID3D12Device& device)
            : QueueContext(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
        }
        /// @brief デストラクタ
        ~GraphicsQueueContext() = default;
    };

    class ComputeQueueContext final : public QueueContext
    {
    public:
        /// @brief コンストラクタ
        ComputeQueueContext(ID3D12Device& device)
            : QueueContext(device, D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
        }
        /// @brief デストラクタ
        ~ComputeQueueContext() = default;
    };

    class CopyQueueContext final : public QueueContext
    {
    public:
        /// @brief コンストラクタ
        CopyQueueContext(ID3D12Device& device)
            : QueueContext(device, D3D12_COMMAND_LIST_TYPE_COPY)
        {
        }
        /// @brief デストラクタ
        ~CopyQueueContext() = default;
    };

    class QueuePool final
    {
        public:
        /// @brief コンストラクタ
        QueuePool(ID3D12Device& device)
            : m_device(device)
        {
            m_graphicsQueuePool.prewarm(k_graphicsQueueCount);
            m_computeQueuePool.prewarm(k_computeQueueCount);
            m_copyQueuePool.prewarm(k_copyQueueCount);
        }
        /// @brief デストラクタ
        ~QueuePool() = default;

        // Context取得
        Core::Pool<GraphicsQueueContext, std::function<void(GraphicsQueueContext&)>>::pooled_ptr get_graphics_queue() noexcept
        {
            std::lock_guard<std::mutex> lock(m_graphicsQueuePoolMutex);
            return m_graphicsQueuePool.acquire();
        }
        Core::Pool<ComputeQueueContext, std::function<void(ComputeQueueContext&)>>::pooled_ptr get_compute_queue() noexcept
        {
            std::lock_guard<std::mutex> lock(m_computeQueuePoolMutex);
            return m_computeQueuePool.acquire();
        }
        Core::Pool<CopyQueueContext, std::function<void(CopyQueueContext&)>>::pooled_ptr get_copy_queue() noexcept
        {
            std::lock_guard<std::mutex> lock(m_copyQueuePoolMutex);
            return m_copyQueuePool.acquire();
        }

        void flush_all()
        {
            {
                std::lock_guard<std::mutex> lock(m_graphicsQueuePoolMutex);
                for (size_t i = 0; i < m_graphicsQueuePool.total_allocated(); ++i)
                {
                    auto ctx = m_graphicsQueuePool.acquire();
                    ctx->flush();
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_computeQueuePoolMutex);
                for (size_t i = 0; i < m_computeQueuePool.total_allocated(); ++i)
                {
                    auto ctx = m_computeQueuePool.acquire();
                    ctx->flush();
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_copyQueuePoolMutex);
                for (size_t i = 0; i < m_copyQueuePool.total_allocated(); ++i)
                {
                    auto ctx = m_copyQueuePool.acquire();
                    ctx->flush();
                }
            }
        }
    private:
        ID3D12Device& m_device;
        // 各キューの数
        static const uint32_t k_graphicsQueueCount = 1;///> 
        static const uint32_t k_computeQueueCount = 4; ///>
        static const uint32_t k_copyQueueCount = 2;    ///>

        Core::Pool<GraphicsQueueContext, std::function<void(GraphicsQueueContext&)>> m_graphicsQueuePool{
            4,
            [](GraphicsQueueContext& ctx) {
                ctx.wait_fence();
            }
        };
        std::mutex m_graphicsQueuePoolMutex;

        Core::Pool<ComputeQueueContext, std::function<void(ComputeQueueContext&)>> m_computeQueuePool{
            4,
            [](ComputeQueueContext& ctx) {
                ctx.wait_fence();
            }
        };
        std::mutex m_computeQueuePoolMutex;

        Core::Pool<CopyQueueContext, std::function<void(CopyQueueContext&)>> m_copyQueuePool{
            4,
            [](CopyQueueContext& ctx) {
                ctx.wait_fence();
            }
        };
        std::mutex m_copyQueuePoolMutex;
    };
} // namespace Cue::GraphicsCore::DX12
