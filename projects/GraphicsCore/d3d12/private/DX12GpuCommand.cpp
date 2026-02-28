#include "DX12GpuCommand.h"

namespace Cue::GraphicsCore::DX12
{
    namespace
    {
        [[nodiscard]] CommandListType to_command_list_type(D3D12_COMMAND_LIST_TYPE type) noexcept
        {
            // 1) DX12 のキュー種別を FrameGraph 共通の列挙へ写像する。
            switch (type)
            {
            case D3D12_COMMAND_LIST_TYPE_DIRECT:
                return CommandListType::Graphics;
            case D3D12_COMMAND_LIST_TYPE_COMPUTE:
                return CommandListType::Compute;
            case D3D12_COMMAND_LIST_TYPE_COPY:
                return CommandListType::Copy;
            default:
                return CommandListType::Graphics;
            }
        }

        [[nodiscard]] size_t to_queue_index(CommandListType type) noexcept
        {
            // 1) 固定長配列へアクセスするため共通列挙を添字へ変換する。
            switch (type)
            {
            case CommandListType::Graphics:
                return 0;
            case CommandListType::Compute:
                return 1;
            case CommandListType::Copy:
                return 2;
            default:
                return 0;
            }
        }
    }

    Result DX12CommandContext::initialize(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
    {
        m_device = device;
        m_nativeType = type;
        m_type = to_command_list_type(type);
        if (m_device == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Device is null.");
        }

        // 1) 初期化済みなら再生成は不要
        if (m_commandAllocator && m_commandList)
        {
            return Result::ok();
        }

        Result r;
        // 2) コマンドアロケータの作成
        r = create_command_allocator(type);
        if (!r)
        {
            return r;
        }
        // 3) コマンドリストの作成
        r = create_command_list(type);
        if (!r)
        {
            return r;
        }
        return Result::ok();
    }
    Result DX12CommandContext::reset()
    {
        if (!m_commandAllocator || !m_commandList)
        {
            return Result::ok();
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
            return Result::ok();
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
    void DX12CommandContext::begin_event(const char* name)
    {
        // 1) PIX/マーカー連携は未実装のため、現状は no-op とする。
        (void)name;
    }
    void DX12CommandContext::end_event()
    {
        // 1) PIX/マーカー連携は未実装のため、現状は no-op とする。
    }
    Result DX12CommandContext::resource_barrier(const ResourceBarrierDesc& barrier)
    {
        // 1) 実リソース解決層が未接続のため、現状は Unsupported を返す。
        (void)barrier;
        return Result::fail(
            Facility::GraphicsCore,
            Code::Unsupported,
            Severity::Warning,
            0,
            "DX12 resource_barrier is not connected to concrete resources yet.");
    }
    Result DX12CommandContext::resource_barriers(const ResourceBarrierDesc* barriers, size_t count)
    {
        // 1) 入力の整合性だけ確認し、現状は Unsupported を返す。
        if ((barriers == nullptr) && (count > 0))
        {
            return Result::fail(
                Facility::GraphicsCore,
                Code::InvalidArg,
                Severity::Error,
                0,
                "barriers is null.");
        }
        return Result::fail(
            Facility::GraphicsCore,
            Code::Unsupported,
            Severity::Warning,
            0,
            "DX12 resource_barriers is not connected to concrete resources yet.");
    }
    Result DX12CommandContext::create_command_allocator(D3D12_COMMAND_LIST_TYPE type)
    {
        if (m_device == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Device is null.");
        }

        // コマンドアロケータの作成
        HRESULT hr = m_device->CreateCommandAllocator(
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
    Result DX12CommandContext::create_command_list(D3D12_COMMAND_LIST_TYPE type)
    {
        if (m_device == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Device is null.");
        }

        // 1) コマンドリストの作成
        HRESULT hr = m_device->CreateCommandList(
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
        SetD3D12Name(m_commandList.Get(), L"CommandContext CommandList");

        // 2) コマンドリストは生成直後にオープン状態になるのでクローズしておく
        m_commandList->Close();
        return Result::ok();
    }

    Result QueueContext::submit(ICommandContext& cmd)
    {
        // 1) runtime が返す DX12 コマンドコンテキストだけを受け取る前提で送信する。
        if (m_commandQueue == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Command queue is null.");
        }
        if (cmd.type() != m_type)
        {
            return Result::fail(
                Facility::GraphicsCore,
                Code::InvalidArg,
                Severity::Error,
                0,
                "Command list type does not match queue type.");
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
    Result QueueContext::signal(QueueSyncPoint& outPoint)
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

        outPoint.queueType = m_type;
        outPoint.value = fence;
        return Result::ok();
    }
    Result QueueContext::wait(const QueueSyncPoint& point)
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
        if (m_queueTable == nullptr)
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                0,
                "Queue table is not bound.");
        }

        QueueContext* sourceQueue = (*m_queueTable)[to_queue_index(point.queueType)];
        if ((sourceQueue == nullptr) || (sourceQueue->get_fence() == nullptr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::NotFound,
                Severity::Error,
                0,
                "Source queue fence is not available.");
        }

        const HRESULT hr = m_commandQueue->Wait(sourceQueue->get_fence(), point.value);
        if (FAILED(hr))
        {
            return Result::fail(
                Facility::Graphics,
                Code::InvalidState,
                Severity::Error,
                static_cast<uint32_t>(hr),
                "Failed to wait for source queue fence.");
        }

        return Result::ok();
    }

    Dx12FrameGraphRuntime::~Dx12FrameGraphRuntime()
    {
        reset();
    }
    Result Dx12FrameGraphRuntime::initialize()
    {
        // 1) FrameGraph 実行中は固定の QueueContext を保持し、QueueSyncPoint の解決先を安定化する。
        m_graphicsQueue = m_queuePool.get_graphics_pool();
        m_computeQueue = m_queuePool.get_compute_pool();
        m_copyQueue = m_queuePool.get_copy_pool();
        if (!m_graphicsQueue || !m_computeQueue || !m_copyQueue)
        {
            reset();
            return Result::fail(
                Facility::Graphics,
                Code::CreationFailed,
                Severity::Error,
                0,
                "Failed to acquire DX12 frame-graph queues.");
        }

        m_queueTable[0] = m_graphicsQueue.get();
        m_queueTable[1] = m_computeQueue.get();
        m_queueTable[2] = m_copyQueue.get();
        m_graphicsQueue->bind_queue_table(&m_queueTable);
        m_computeQueue->bind_queue_table(&m_queueTable);
        m_copyQueue->bind_queue_table(&m_queueTable);
        return Result::ok();
    }
    void Dx12FrameGraphRuntime::reset() noexcept
    {
        // 1) コマンドを先に pool へ返し、その後で queue を返して依存順を保つ。
        m_commandSlots.clear();
        m_queueTable = { nullptr, nullptr, nullptr };
        m_graphicsQueue.reset();
        m_computeQueue.reset();
        m_copyQueue.reset();
    }
    IQueueContext* Dx12FrameGraphRuntime::get_queue_context(CommandListType queueType)
    {
        // 1) execute 中に固定保持している queue をそのまま返し、pass 間同期を安定させる。
        switch (queueType)
        {
        case CommandListType::Graphics:
            return m_graphicsQueue.get();
        case CommandListType::Compute:
            return m_computeQueue.get();
        case CommandListType::Copy:
            return m_copyQueue.get();
        default:
            return nullptr;
        }
    }
    ICommandContext* Dx12FrameGraphRuntime::acquire_pass_command_context(CommandListType queueType, size_t passIndex)
    {
        // 1) pass ごとに borrowed command context を保持し、execute 完了まで pool へ返さない。
        if (passIndex >= m_commandSlots.size())
        {
            m_commandSlots.resize(passIndex + 1);
        }

        CommandSlot& slot = m_commandSlots[passIndex];
        if (slot.raw != nullptr)
        {
            if (slot.type != queueType)
            {
                return nullptr;
            }
            return slot.raw;
        }

        slot.type = queueType;
        switch (queueType)
        {
        case CommandListType::Graphics:
            slot.graphics = m_commandPool.get_graphics_context();
            slot.raw = slot.graphics.get();
            break;
        case CommandListType::Compute:
            slot.compute = m_commandPool.get_compute_context();
            slot.raw = slot.compute.get();
            break;
        case CommandListType::Copy:
            slot.copy = m_commandPool.get_copy_context();
            slot.raw = slot.copy.get();
            break;
        default:
            slot.raw = nullptr;
            break;
        }

        return slot.raw;
    }
} // namespace Cue::GraphicsCore::DX12
