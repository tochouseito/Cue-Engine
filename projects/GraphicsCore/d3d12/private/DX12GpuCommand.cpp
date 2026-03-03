#include "DX12GpuCommand.h"

namespace Cue::GraphicsCore::DX12
{
    DX12CommandContext::DX12CommandContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // 1) コマンドアロケータの作成
        create_command_allocator(device, type);

        // 2) コマンドリストの作成
        create_command_list(device, type);
    }
    Result DX12CommandContext::reset()
    {
        if (!m_commandAllocator || !m_commandList)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "CommandAllocator or CommandList is null.");
        }

        // 1) コマンドアロケータのリセット
        HRESULT hr = m_commandAllocator->Reset();
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to reset CommandAllocator.");
        }

        // 2) コマンドリストのリセット
        hr = m_commandList->Reset(
            m_commandAllocator.Get(),
            nullptr);
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to reset CommandList.");
        }

        m_listEmpty = true;

        return Result::ok();
    }
    Result DX12CommandContext::close()
    {
        if (!m_commandList)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "CommandList is null.");
        }

        // コマンドリストのクローズ
        HRESULT hr = m_commandList->Close();
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to close CommandList.");
        }

        return Result::ok();
    }
    Result DX12CommandContext::create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // コマンドアロケータの作成
        HRESULT hr = device.CreateCommandAllocator(
            type,
            IID_PPV_ARGS(&m_commandAllocator));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandAllocator.");
        }
        SetD3D12Name(m_commandAllocator.Get(), L"CommandContext CommandAllocator");
        return Result::ok();
    }
    Result DX12CommandContext::create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // 1) コマンドリストの作成
        HRESULT hr = device.CreateCommandList(
            0,
            type,
            m_commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&m_commandList));
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to create CommandList.");
        }

        // 2) オブジェクトに名前を付ける
        SetD3D12Name(m_commandList.Get(), L"CommandContext CommandList");

        // 3) コマンドリストは生成直後にオープン状態になるのでクローズしておく
        m_commandList->Close();

        return Result::ok();
    }
    DX12CommandPool::DX12CommandPool(DX12RenderDevice& device)
        : m_graphicsContextPool(
            32,
            [](DX12GraphicsCommandContext& ctx)
            {
                ctx.reset();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12GraphicsCommandContext>(*d3d12Device);
            })
        , m_computeContextPool(
            32,
            [](DX12ComputeCommandContext& ctx)
            {
                ctx.reset();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12ComputeCommandContext>(*d3d12Device);
            })
        , m_copyContextPool(
            32,
            [](DX12CopyCommandContext& ctx)
            {
                ctx.reset();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12CopyCommandContext>(*d3d12Device);
            })
    {
    }
    Result DX12CommandPool::initialize()
    {
        m_graphicsContextPool.prewarm(1);
        m_computeContextPool.prewarm(1);
        m_copyContextPool.prewarm(1);
        return Result::ok();
    }
    Result DX12CommandPool::acquire_context(CommandListType type, CommandContextLease& outContext)
    {
        switch (type)
        {
        case CommandListType::Graphics:
        {
            auto pooled = m_graphicsContextPool.acquire();
            outContext = CommandContextLease(
                pooled.release(),
                [this](ICommandContext* raw) { m_graphicsContextPool.recycle(static_cast<DX12GraphicsCommandContext*>(raw)); });
            return Result::ok();
        }
        case CommandListType::Compute:
        {
            auto pooled = m_computeContextPool.acquire();
            outContext = CommandContextLease(
                pooled.release(),
                [this](ICommandContext* raw) { m_computeContextPool.recycle(static_cast<DX12ComputeCommandContext*>(raw)); });
            return Result::ok();
        }
        case CommandListType::Copy:
        {
            auto pooled = m_copyContextPool.acquire();
            outContext = CommandContextLease(
                pooled.release(),
                [this](ICommandContext* raw) { m_copyContextPool.recycle(static_cast<DX12CopyCommandContext*>(raw)); });
            return Result::ok();
        }
        default:
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Unsupported command list type");
        }
    }
    DX12QueueContext::DX12QueueContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        create_fence(device);
        create_fence_event();
        create_command_queue(device, type);
    }
    DX12QueueContext::~DX12QueueContext()
    {
        if (m_commandQueue && m_fence)
        {
            wait(m_lastSignalPoint);
        }
        if (m_fenceEvent)
        {
            CloseHandle(m_fenceEvent);
            m_fenceEvent = nullptr;
        }
        m_fence.Reset();
        m_commandQueue.Reset();
    }
    Result DX12QueueContext::submit(ICommandContext& cmd)
    {
        if (cmd.type() != type() || !m_commandQueue)
        {
            return Result::fail(
                Facility::GraphicsCore,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Command list type does not match queue type or command queue is not initialized.");
        }

        DX12CommandContext& dx12Cmd = static_cast<DX12CommandContext&>(cmd);
        ID3D12GraphicsCommandList* commandList = dx12Cmd.get_command_list();
        if (commandList == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Command list is null.");
        }

        ID3D12CommandList* lists[] = { commandList };
        m_commandQueue->ExecuteCommandLists(1, lists);
        return Result::ok();
    }
    Result DX12QueueContext::signal(QueueSyncPoint& outPoint)
    {
        // 1) submit 済み作業の完了点を外へ渡せるよう、フェンス値を進めて返す。
        if (!m_commandQueue || !m_fence)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Queue or fence is not initialized.");
        }

        const UINT64 fence = ++m_fenceValue;
        const HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fence);
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to signal queue fence.");
        }

        outPoint.queueType = type();
        outPoint.value = fence;
        return Result::ok();
    }
    Result DX12QueueContext::wait(const QueueSyncPoint& point)
    {
        // 1) runtime が束ねているキュー表から待機元 fence を解決して GPU 側 wait を発行する。
        if (!m_commandQueue)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Command queue is null.");
        }
        if (point.value == 0)
        {
            return Result::fail(
                Facility::GraphicsCore,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Queue sync point is invalid.");
        }

        
        if (!m_fence || !m_fenceEvent)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Fence or fence event is not initialized.");
        }
        // Fenceの値が指定したSignal値にたどり着いているか確認する
        // GetCompletedValueの初期値はFence作成時に渡した初期値
        if (!m_fenceValue)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Fence value is not initialized.");
        }
        if (m_fence->GetCompletedValue() < m_fenceValue)
        {
            // 指定したSignalにたどり着いていないので、たどり着くまで待つようにイベントを設定する
            m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
            // イベント待つ
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        return Result::ok();
    }
    Result DX12QueueContext::wait_for_last_signal()
    {
        if (m_lastSignalPoint.value != 0)
        {
            return wait(m_lastSignalPoint);
        }
        return Result::ok();
    }
    Result DX12QueueContext::create_fence(ID3D12Device& device)
    {
        // フェンスの作成
        m_fence.Reset();
        m_fenceValue = 0;// 初期値0
        HRESULT hr = device.CreateFence(
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
        return Result::ok();
    }
    Result DX12QueueContext::create_fence_event()
    {
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
        return Result::ok();
    }
    Result DX12QueueContext::create_command_queue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = type;
        HRESULT hr = device.CreateCommandQueue(
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
    DX12QueuePool::DX12QueuePool(DX12RenderDevice& device)
        : m_graphicsQueuePool(
            4,
            [](DX12GraphicsQueueContext& ctx)
            {
                ctx.wait_for_last_signal();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12GraphicsQueueContext>(*d3d12Device);
            })
        , m_computeQueuePool(
            4,
            [](DX12ComputeQueueContext& ctx)
            {
                ctx.wait_for_last_signal();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12ComputeQueueContext>(*d3d12Device);
            })
        , m_copyQueuePool(
            4,
            [](DX12CopyQueueContext& ctx)
            {
                ctx.wait_for_last_signal();
            },
            [d3d12Device = device.get_d3d12_device()]()
            {
                return std::make_unique<DX12CopyQueueContext>(*d3d12Device);
            })
    {
    }
    Result DX12QueuePool::initialize()
    {
        m_graphicsQueuePool.prewarm(k_graphicsQueueCount);
        m_computeQueuePool.prewarm(k_computeQueueCount);
        m_copyQueuePool.prewarm(k_copyQueueCount);
        return Result::ok();
    }
    Result DX12QueuePool::acquire_queue(CommandListType type, QueueContextLease& outQueue)
    {
        switch (type)
        {
        case CommandListType::Graphics:
        {
            auto pooled = m_graphicsQueuePool.acquire();
            outQueue = QueueContextLease(
                pooled.release(),
                [this](IQueueContext* raw) { m_graphicsQueuePool.recycle(static_cast<DX12GraphicsQueueContext*>(raw)); });
            return Result::ok();
        }
        case CommandListType::Compute:
        {
            auto pooled = m_computeQueuePool.acquire();
            outQueue = QueueContextLease(
                pooled.release(),
                [this](IQueueContext* raw) { m_computeQueuePool.recycle(static_cast<DX12ComputeQueueContext*>(raw)); });
            return Result::ok();
        }
        case CommandListType::Copy:
        {
            auto pooled = m_copyQueuePool.acquire();
            outQueue = QueueContextLease(
                pooled.release(),
                [this](IQueueContext* raw) { m_copyQueuePool.recycle(static_cast<DX12CopyQueueContext*>(raw)); });
            return Result::ok();
        }
        default:
            return Result::fail(Facility::Graphics, Code::InvalidArg, Severity::Warning, 0, "Unsupported queue type");
        }
    }
} // namespace Cue::GraphicsCore::DX12
