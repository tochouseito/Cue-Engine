#include "DX12GpuCommand.h"

namespace Cue::RHI::DX12
{
    namespace
    {
        constexpr UINT k_eventMetadataAnsi = 1u;
    }

    DX12GpuCommandContext::DX12GpuCommandContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // コマンドアロケータの作成
        create_command_allocator(device, type);
        // コマンドリストの作成
        create_command_list(device, type);

        m_type = convert_command_list_type(type);
    }
    Result DX12GpuCommandContext::reset()
    {
        if (!m_commandAllocator || !m_commandList)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "CommandAllocator or CommandList is not initialized.");
        }

        // コマンドアロケータのリセット
        HRESULT hr = m_commandAllocator->Reset();
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to reset CommandAllocator.");
        }

        // コマンドリストのリセット
        hr = m_commandList->Reset(
            m_commandAllocator.Get(),
            nullptr);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to reset CommandList.");
        }

        return Result::ok();
    }
    Result DX12GpuCommandContext::close()
    {
        if (!m_commandList)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "CommandList is not initialized.");
        }

        // コマンドリストのクローズ
        HRESULT hr = m_commandList->Close();
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to close CommandList.");
        }

        return Result::ok();
    }
    CommandListType DX12GpuCommandContext::type() const
    {
        return m_type;
    }
    void DX12GpuCommandContext::begin_event(const char* name)
    {
        // コマンドリスト未初期化時はイベント記録を行えないため、何もせず戻る。
        if (m_commandList == nullptr)
        {
            return;
        }

        // 空名はデバッグ時の識別性を落とすため、既定名に置き換える。
        const char* eventName = name;
        if (eventName == nullptr || eventName[0] == '\0')
        {
            eventName = "UnnamedEvent";
        }

        // metadata と size を文字列形式に合わせて指定し、デバッグレイヤーの破損判定を回避する。
        const UINT eventNameBytes = static_cast<UINT>((std::char_traits<char>::length(eventName) + 1) * sizeof(eventName[0]));
        m_commandList->BeginEvent(k_eventMetadataAnsi, eventName, eventNameBytes);
    }
    void DX12GpuCommandContext::end_event()
    {
        // コマンドリスト未初期化時は end marker を積めないため、何もせず戻る。
        if (m_commandList == nullptr)
        {
            return;
        }

        // begin_event で積んだスコープを閉じ、GPU キャプチャ上のパス範囲を確定する。
        m_commandList->EndEvent();
    }
    Result DX12GpuCommandContext::create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // コマンドアロケータの作成
        HRESULT hr = device.CreateCommandAllocator(
            type,
            IID_PPV_ARGS(&m_commandAllocator));
        if (FAILED(hr))
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create CommandAllocator.");
        }
        set_d3d12_name(m_commandAllocator.Get(), L"CommandContext CommandAllocator");

        return Result::ok();
    }
    Result DX12GpuCommandContext::create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        // コマンドリストの作成
        HRESULT hr = device.CreateCommandList(
            0,
            type,
            m_commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&m_commandList));
        if (FAILED(hr))
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create CommandList.");
        }

        // オブジェクトに名前を付ける
        set_d3d12_name(m_commandList.Get(), L"CommandContext CommandList");

        // コマンドリストは生成直後にオープン状態になるのでクローズしておく
        m_commandList->Close();

        return Result::ok();
    }
    DX12GpuCommandQueue::DX12GpuCommandQueue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        create_fence(device);
        create_fence_event();
        create_command_queue(device, type);
    }
    CommandListType DX12GpuCommandQueue::type() const
    {
        return m_type;
    }
    Result DX12GpuCommandQueue::submit(std::vector<ICommandContext*>& contexts)
    {
        std::vector<ID3D12CommandList*> commandLists;
        for (ICommandContext* context : contexts)
        {
            if (context == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context is null.");
            }
            if (context->type() != m_type)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command context type does not match the queue type.");
            }

            DX12GpuCommandContext& dx12Cmd = static_cast<DX12GpuCommandContext&>(*context);
            ID3D12CommandList* commandList = dx12Cmd.d3d12_command_list();
            if (commandList == nullptr)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Command list is null.");
            }
            commandLists.push_back(commandList);
        }

        m_commandQueue->ExecuteCommandLists(1, commandLists.data());
        return Result::ok();
    }
    Result DX12GpuCommandQueue::signal()
    {
        // submit 済み作業の完了点を外へ渡せるよう、フェンス値を進めて返す。
        if (!m_commandQueue || !m_fence)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "CommandQueue or Fence is not initialized.");
        }

        const UINT64 fence = ++m_fenceValue;
        const HRESULT hr = m_commandQueue->Signal(m_fence.Get(), fence);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to signal CommandQueue.");
        }

        return Result::ok();
    }
    Result DX12GpuCommandQueue::wait()
    {
        // 自前 fence の完了だけを監視し、再利用前の CPU 同期待機に使う。
        if (!m_fence || !m_fenceEvent)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "Fence or Fence event is not initialized.");
        }
        if (m_fence->GetCompletedValue() < m_fenceValue)
        {
            // 完了通知イベントを張り、指定値まで到達するまで待機する。
            m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
            WaitForSingleObject(m_fenceEvent, INFINITE);
        }
        return Result::ok();
    }
    Result DX12GpuCommandQueue::wait_for_queue(IQueueContext& queue)
    {
        DX12GpuCommandQueue& dx12Queue = static_cast<DX12GpuCommandQueue&>(queue);
        if (!m_fence || !dx12Queue.m_fence)
        {
            return Result::fail(
                Code::InternalError,
                Severity::Fatal,
                "Fence is not initialized.");
        }
        const HRESULT hr = m_commandQueue->Wait(dx12Queue.m_fence.Get(), dx12Queue.m_fenceValue);
        if (FAILED(hr))
        {
            return Result::fail(
                PAL::Win::convert_hresult_code(hr),
                Severity::Error,
                "Failed to wait for another queue.");
        }

        return Result::ok();
    }
    Result DX12GpuCommandQueue::create_fence(ID3D12Device& device)
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
                Code::CreateFailed,
                Severity::Error,
                "Failed to create Fence.");
        }
        set_d3d12_name(m_fence.Get(), L"QueueContext Fence");
        return Result::ok();
    }
    Result DX12GpuCommandQueue::create_fence_event()
    {
        // イベントハンドルの作成
        m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_fenceEvent == nullptr)
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create Fence event.");
        }
        return Result::ok();
    }
    Result DX12GpuCommandQueue::create_command_queue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = type;
        HRESULT hr = device.CreateCommandQueue(
            &queueDesc,
            IID_PPV_ARGS(&m_commandQueue));
        if (FAILED(hr))
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create CommandQueue.");
        }
        set_d3d12_name(m_commandQueue.Get(), L"QueueContext CommandQueue");
        return Result::ok();
    }
    Result DX12CommandPool::get_command_context(CommandListType type, CommandContextLease& outContext)
    {
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            // 1) swapchain backbuffer を触る graphics command list は再利用せず、毎回新規生成して参照履歴を持ち越しません。
            auto context = std::make_unique<DX12GpuCommandContext>(
                *m_renderDevice.get_d3d12_device(),
                D3D12_COMMAND_LIST_TYPE_DIRECT);
            outContext = CommandContextLease(
                context.release(),
                [](ICommandContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            std::lock_guard lock(m_computePoolMutex);
            // コンピュートコマンドコンテキストをプールから取得
                auto context = m_computeContextPool.acquire();
                outContext = CommandContextLease(
                    context.release(),
                    [](ICommandContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            std::lock_guard lock(m_copyPoolMutex);
            // コピーコマンドコンテキストをプールから取得
                auto context = m_copyContextPool.acquire();
                outContext = CommandContextLease(
                    context.release(),
                    [](ICommandContext* raw) {delete raw; });
        }
            break;
        default:
            CUE_ASSERT_MSG(false, "Invalid command list type.");
            break;
        }
        return Result::ok();
    }
    Result DX12CommandPool::return_command_context(CommandContextLease& context)
    {
        CommandListType type = context->type();
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            // 1) graphics command list は再利用せず破棄して、swapchain buffer の参照状態を完全にリセットします。
            delete context.release();
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            // コンピュートコマンドコンテキストをプールへ返却
            std::lock_guard lock(m_computePoolMutex);
            m_computeContextPool.recycle(static_cast<DX12GpuCommandContext*>(context.release()));
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            // コピーコマンドコンテキストをプールへ返却
            std::lock_guard lock(m_copyPoolMutex);
            m_copyContextPool.recycle(static_cast<DX12GpuCommandContext*>(context.release()));
        }
            break;
        default:
            CUE_ASSERT_MSG(false, "Invalid command list type.");
            break;
        }
        return Result::ok();
    }
    Result DX12QueuePool::get_queue_context(CommandListType type, QueueContextLease& outContext)
    {
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            // 1) SwapChain と同じ graphics queue を必ず単独利用にして、複数 queue へ分岐しないようにします。
            std::unique_lock lock(m_graphicsPoolMutex);
            m_graphicsPoolCv.wait(lock, [this]()
                {
                    return !m_graphicsQueueCheckedOut;
                });
            auto context = m_graphicsQueuePool.acquire();
            m_graphicsQueueCheckedOut = true;
            outContext = QueueContextLease(
                context.release(),
                [](IQueueContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            std::lock_guard lock(m_computePoolMutex);
            // コンピュートコマンドキューコンテキストをプールから取得
                auto context = m_computeQueuePool.acquire();
                outContext = QueueContextLease(
                    context.release(),
                    [](IQueueContext* raw) {delete raw; });
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            std::lock_guard lock(m_copyPoolMutex);
            // コピーコマンドキューコンテキストをプールから取得
                auto context = m_copyQueuePool.acquire();
                outContext = QueueContextLease(
                    context.release(),
                    [](IQueueContext* raw) {delete raw; });
        }
            break;
        default:
            break;
        }
        return Result::ok();
    }
    Result DX12QueuePool::return_queue_context(QueueContextLease& context)
    {
        CommandListType type = context->type();
        switch (type)
        {
        case Cue::RHI::CommandListType::Graphics:
        {
            // 1) graphics queue を返却して待機中の実行を再開し、swapchain 作業を 1 本へ直列化します。
            std::lock_guard lock(m_graphicsPoolMutex);
            m_graphicsQueuePool.recycle(static_cast<DX12GpuCommandQueue*>(context.release()));
            m_graphicsQueueCheckedOut = false;
        }
            m_graphicsPoolCv.notify_one();
            break;
        case Cue::RHI::CommandListType::Compute:
        {
            // コンピュートコマンドキューコンテキストをプールへ返却
            std::lock_guard lock(m_computePoolMutex);
            m_computeQueuePool.recycle(static_cast<DX12GpuCommandQueue*>(context.release()));
        }
            break;
        case Cue::RHI::CommandListType::Copy:
        {
            // コピーコマンドキューコンテキストをプールへ返却
            std::lock_guard lock(m_copyPoolMutex);
            m_copyQueuePool.recycle(static_cast<DX12GpuCommandQueue*>(context.release()));
        }
            break;
        default:
            break;
        }
        return Result::ok();
    }
}
