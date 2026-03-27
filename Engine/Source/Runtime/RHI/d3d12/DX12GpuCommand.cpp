#include "DX12GpuCommand.h"

namespace Cue::RHI::DX12
{
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
        return Result();
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
}
