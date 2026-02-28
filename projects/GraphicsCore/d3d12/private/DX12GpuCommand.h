#pragma once
#include "stdafx.h"
#include <FrameGraph.h>
#include <Pool.h>

#include <array>
#include <functional>
#include <mutex>

namespace Cue::GraphicsCore::DX12
{
    class DX12CommandContext : public ICommandContext
    {
    public:
        DX12CommandContext() = default;
        virtual ~DX12CommandContext() = default;
        Result initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);
        ID3D12GraphicsCommandList* get_command_list() { return m_commandList.Get(); }
        ID3D12CommandAllocator* get_command_allocator() { return m_commandAllocator.Get(); }
        Result reset() override;
        Result close() override;
        CommandListType type() const override { return m_type; }
        void begin_event(const char* name) override;
        void end_event() override;
        Result resource_barrier(const ResourceBarrierDesc& barrier) override;
        Result resource_barriers(const ResourceBarrierDesc* barriers, size_t count) override;
    private:
        Result create_command_allocator(D3D12_COMMAND_LIST_TYPE type);
        Result create_command_list(D3D12_COMMAND_LIST_TYPE type);
    protected:
        ID3D12Device* m_device = nullptr;
        D3D12_COMMAND_LIST_TYPE m_nativeType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        CommandListType m_type = CommandListType::Graphics;
        ComPtr<ID3D12GraphicsCommandList> m_commandList = nullptr;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator = nullptr;
    };

    class DX12GraphicsCommandContext : public DX12CommandContext
    {
    public:
        DX12GraphicsCommandContext() = default;
        Result initialize(ID3D12Device* device) { return DX12CommandContext::initialize(device, D3D12_COMMAND_LIST_TYPE_DIRECT); }
        ~DX12GraphicsCommandContext() = default;
    };

    class DX12ComputeCommandContext : public DX12CommandContext
    {
    public:
        DX12ComputeCommandContext() = default;
        Result initialize(ID3D12Device* device) { return DX12CommandContext::initialize(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); }
        ~DX12ComputeCommandContext() = default;
    };

    class DX12CopyCommandContext : public DX12CommandContext
    {
    public:
        DX12CopyCommandContext() = default;
        Result initialize(ID3D12Device* device) { return DX12CommandContext::initialize(device, D3D12_COMMAND_LIST_TYPE_COPY); }
        ~DX12CopyCommandContext() = default;
    };

    using GraphicsCommandPooledPtr = Core::Pool<DX12GraphicsCommandContext, std::function<void(DX12GraphicsCommandContext&)>>::pooled_ptr;
    using ComputeCommandPooledPtr = Core::Pool<DX12ComputeCommandContext, std::function<void(DX12ComputeCommandContext&)>>::pooled_ptr;
    using CopyCommandPooledPtr = Core::Pool<DX12CopyCommandContext, std::function<void(DX12CopyCommandContext&)>>::pooled_ptr;

    class CommandPool final
    {
    public:
        /// @brief コンストラクタ
        CommandPool(ID3D12Device* device)
            : m_device(device)
        {
        }
        /// @brief デストラクタ
        ~CommandPool() = default;

        // Context取得
        GraphicsCommandPooledPtr get_graphics_context() noexcept
        {
            std::lock_guard<std::mutex> lock(m_graphicsContextPoolMutex);
            auto ctx = m_graphicsContextPool.acquire();
            if (!ctx->initialize(m_device))
            {
                return {};
            }
            return ctx;
        }

        ComputeCommandPooledPtr get_compute_context() noexcept
        {
            std::lock_guard<std::mutex> lock(m_computeContextPoolMutex);
            auto ctx = m_computeContextPool.acquire();
            if (!ctx->initialize(m_device))
            {
                return {};
            }
            return ctx;
        }

        CopyCommandPooledPtr get_copy_context() noexcept
        {
            std::lock_guard<std::mutex> lock(m_copyContextPoolMutex);
            auto ctx = m_copyContextPool.acquire();
            if (!ctx->initialize(m_device))
            {
                return {};
            }
            return ctx;
        }
    private:
        ID3D12Device* m_device = nullptr;

        Core::Pool<DX12GraphicsCommandContext, std::function<void(DX12GraphicsCommandContext&)>> m_graphicsContextPool{
            32,
            [](DX12GraphicsCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_graphicsContextPoolMutex;

        Core::Pool<DX12ComputeCommandContext, std::function<void(DX12ComputeCommandContext&)>> m_computeContextPool{
            32,
            [](DX12ComputeCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_computeContextPoolMutex;

        Core::Pool<DX12CopyCommandContext, std::function<void(DX12CopyCommandContext&)>> m_copyContextPool{
            32,
            [](DX12CopyCommandContext& ctx) {
                ctx.reset();
            }
        };
        std::mutex m_copyContextPoolMutex;
    };

    class QueueContext : public IQueueContext
    {
    public:
        /// @brief コンストラクタ
        QueueContext() = default;
        /// @brief デストラクタ
        virtual ~QueueContext()
        {
            if (m_commandQueue && m_fence)
            {
                wait_fence();
            }
            if (m_fenceEvent)
            {
                CloseHandle(m_fenceEvent);
                m_fenceEvent = nullptr;
            }
            m_fence.Reset();
            m_commandQueue.Reset();
        }

        Result initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
        {
            m_device = device;
            m_nativeType = type;
            switch (type)
            {
            case D3D12_COMMAND_LIST_TYPE_DIRECT:
                m_type = CommandListType::Graphics;
                break;
            case D3D12_COMMAND_LIST_TYPE_COMPUTE:
                m_type = CommandListType::Compute;
                break;
            case D3D12_COMMAND_LIST_TYPE_COPY:
                m_type = CommandListType::Copy;
                break;
            default:
                m_type = CommandListType::Graphics;
                break;
            }
            if (m_device == nullptr)
            {
                return Result::fail(
                    Facility::Graphics,
                    Code::InvalidArg,
                    Severity::Error,
                    0,
                    "Device is null.");
            }

            if (m_commandQueue && m_fence && m_fenceEvent)
            {
                return Result::ok();
            }

            // フェンスの作成
            m_fence.Reset();
            m_fenceValue = 0;// 初期値0
            HRESULT hr = m_device->CreateFence(
                m_fenceValue,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(&m_fence));
            if (FAILED(hr))
            {
                return Result::fail(
                    Facility::Graphics,
                    Code::CreationFailed,
                    Severity::Error,
                    static_cast<uint32_t>(hr),
                    "Failed to create Fence.");
            }
            SetD3D12Name(m_fence.Get(), L"QueueContext Fence");

            // イベントハンドルの作成
            m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (m_fenceEvent == nullptr)
            {
                return Result::fail(
                    Facility::Graphics,
                    Code::CreationFailed,
                    Severity::Error,
                    GetLastError(),
                    "Failed to create Fence event handle.");
            }
            // コマンドキューの作成
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = type;
            hr = m_device->CreateCommandQueue(
                &queueDesc,
                IID_PPV_ARGS(&m_commandQueue));
            if (FAILED(hr))
            {
                return Result::fail(
                    Facility::Graphics,
                    Code::CreationFailed,
                    Severity::Error,
                    static_cast<uint32_t>(hr),
                    "Failed to create CommandQueue.");
            }
            SetD3D12Name(m_commandQueue.Get(), L"QueueContext CommandQueue");
            return Result::ok();
        }

        CommandListType type() const override { return m_type; }
        Result submit(ICommandContext& cmd) override;
        Result signal(QueueSyncPoint& outPoint) override;
        Result wait(const QueueSyncPoint& point) override;
        void wait_fence()
        {
            if (!m_fence || !m_fenceEvent)
            {
                return;
            }
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
        void bind_queue_table(const std::array<QueueContext*, 3>* queueTable) noexcept
        {
            m_queueTable = queueTable;
        }

        ID3D12CommandQueue* get_command_queue() noexcept { return m_commandQueue.Get(); };
        ID3D12Fence* get_fence() noexcept { return m_fence.Get(); };
        uint64_t get_fence_value() noexcept { return m_fenceValue; };
    private:
        ID3D12Device* m_device = nullptr;
        D3D12_COMMAND_LIST_TYPE m_nativeType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        CommandListType m_type = CommandListType::Graphics;
        ComPtr<ID3D12CommandQueue> m_commandQueue = nullptr;
        ComPtr<ID3D12Fence> m_fence = nullptr;
        HANDLE m_fenceEvent = {};
        uint64_t m_fenceValue = {};
        const std::array<QueueContext*, 3>* m_queueTable = nullptr;
    };

    class GraphicsQueueContext final : public QueueContext
    {
    public:
        /// @brief コンストラクタ
        GraphicsQueueContext() = default;
        Result initialize(ID3D12Device* device) { return QueueContext::initialize(device, D3D12_COMMAND_LIST_TYPE_DIRECT); }
        /// @brief デストラクタ
        ~GraphicsQueueContext() = default;
    };

    class ComputeQueueContext final : public QueueContext
    {
    public:
        /// @brief コンストラクタ
        ComputeQueueContext() = default;
        Result initialize(ID3D12Device* device) { return QueueContext::initialize(device, D3D12_COMMAND_LIST_TYPE_COMPUTE); }
        /// @brief デストラクタ
        ~ComputeQueueContext() = default;
    };

    class CopyQueueContext final : public QueueContext
    {
    public:
        /// @brief コンストラクタ
        CopyQueueContext() = default;
        Result initialize(ID3D12Device* device) { return QueueContext::initialize(device, D3D12_COMMAND_LIST_TYPE_COPY); }
        /// @brief デストラクタ
        ~CopyQueueContext() = default;
    };

    using GraphicsQueuePooledPtr = Core::Pool<GraphicsQueueContext, std::function<void(GraphicsQueueContext&)>>::pooled_ptr;
    using ComputeQueuePooledPtr = Core::Pool<ComputeQueueContext, std::function<void(ComputeQueueContext&)>>::pooled_ptr;
    using CopyQueuePooledPtr = Core::Pool<CopyQueueContext, std::function<void(CopyQueueContext&)>>::pooled_ptr;

    class QueuePool final
    {
        public:
        /// @brief コンストラクタ
        QueuePool(ID3D12Device* device)
            : m_device(device)
        {
            m_graphicsQueuePool.prewarm(k_graphicsQueueCount);
            m_computeQueuePool.prewarm(k_computeQueueCount);
            m_copyQueuePool.prewarm(k_copyQueueCount);
        }
        /// @brief デストラクタ
        ~QueuePool() = default;

        // Context取得
        GraphicsQueuePooledPtr get_graphics_pool() noexcept
        {
            std::lock_guard<std::mutex> lock(m_graphicsQueuePoolMutex);
            auto ctx = m_graphicsQueuePool.acquire();
            if (!ctx->initialize(m_device))
            {
                return {};
            }
            return ctx;
        }
        ComputeQueuePooledPtr get_compute_pool() noexcept
        {
            std::lock_guard<std::mutex> lock(m_computeQueuePoolMutex);
            auto ctx = m_computeQueuePool.acquire();
            if (!ctx->initialize(m_device))
            {
                return {};
            }
            return ctx;
        }
        CopyQueuePooledPtr get_copy_pool() noexcept
        {
            std::lock_guard<std::mutex> lock(m_copyQueuePoolMutex);
            auto ctx = m_copyQueuePool.acquire();
            if (!ctx->initialize(m_device))
            {
                return {};
            }
            return ctx;
        }

        void flush_all()
        {
            {
                std::lock_guard<std::mutex> lock(m_graphicsQueuePoolMutex);
                for (size_t i = 0; i < m_graphicsQueuePool.total_allocated(); ++i)
                {
                    auto ctx = m_graphicsQueuePool.acquire();
                    QueueSyncPoint point{};
                    (void)ctx->signal(point);
                    ctx->wait_fence();
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_computeQueuePoolMutex);
                for (size_t i = 0; i < m_computeQueuePool.total_allocated(); ++i)
                {
                    auto ctx = m_computeQueuePool.acquire();
                    QueueSyncPoint point{};
                    (void)ctx->signal(point);
                    ctx->wait_fence();
                }
            }
            {
                std::lock_guard<std::mutex> lock(m_copyQueuePoolMutex);
                for (size_t i = 0; i < m_copyQueuePool.total_allocated(); ++i)
                {
                    auto ctx = m_copyQueuePool.acquire();
                    QueueSyncPoint point{};
                    (void)ctx->signal(point);
                    ctx->wait_fence();
                }
            }
        }
    private:
        ID3D12Device* m_device = nullptr;
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

    class Dx12FrameGraphRuntime final : public IFrameGraphRuntime
    {
    public:
        Dx12FrameGraphRuntime(CommandPool& commandPool, QueuePool& queuePool) noexcept
            : m_commandPool(commandPool)
            , m_queuePool(queuePool)
        {
        }
        ~Dx12FrameGraphRuntime() override;

        [[nodiscard]] Result initialize();
        void reset() noexcept;

        [[nodiscard]] IQueueContext* get_queue_context(CommandListType queueType) override;
        [[nodiscard]] ICommandContext* acquire_pass_command_context(CommandListType queueType, size_t passIndex) override;
    private:
        struct CommandSlot final
        {
            CommandListType type = CommandListType::Graphics;
            DX12CommandContext* raw = nullptr;
            GraphicsCommandPooledPtr graphics{};
            ComputeCommandPooledPtr compute{};
            CopyCommandPooledPtr copy{};
        };
    private:
        CommandPool& m_commandPool;
        QueuePool& m_queuePool;
        std::vector<CommandSlot> m_commandSlots;
        GraphicsQueuePooledPtr m_graphicsQueue{};
        ComputeQueuePooledPtr m_computeQueue{};
        CopyQueuePooledPtr m_copyQueue{};
        std::array<QueueContext*, 3> m_queueTable{ nullptr, nullptr, nullptr };
    };
} // namespace Cue::GraphicsCore::DX12
