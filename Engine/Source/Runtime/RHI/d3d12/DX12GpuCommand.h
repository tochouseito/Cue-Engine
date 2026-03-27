#pragma once

// === RHI includes ===
#include <RHICommon.h>

// === DirectX 12 includes ===
#include "stdafx.h"

namespace Cue::RHI::DX12
{
    class DX12GpuCommandContext final : public ICommandContext
    {
    public:
        DX12GpuCommandContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        // コピー禁止
        DX12GpuCommandContext(const DX12GpuCommandContext&) = delete;
        DX12GpuCommandContext& operator=(const DX12GpuCommandContext&) = delete;
        // ムーブは許可
        DX12GpuCommandContext(DX12GpuCommandContext&&) = default;
        DX12GpuCommandContext& operator=(DX12GpuCommandContext&&) = default;
        ~DX12GpuCommandContext() override = default;

        Result reset() override;
        Result close() override;
        CommandListType type() const override;

        // --- 取得 ---
        ID3D12GraphicsCommandList* d3d12_command_list() const { return m_commandList.Get(); }
        ID3D12CommandAllocator* d3d12_command_allocator() const { return m_commandAllocator.Get(); }
    private:
        Result create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        Result create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
    private:
        ComPtr<ID3D12GraphicsCommandList> m_commandList = nullptr;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator = nullptr;
        CommandListType m_type = CommandListType::Graphics;
    };

    class DX12GpuCommandQueue final : public IQueueContext
    {
    public:
        DX12GpuCommandQueue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        // コピー禁止
        DX12GpuCommandQueue(const DX12GpuCommandQueue&) = delete;
        DX12GpuCommandQueue& operator=(const DX12GpuCommandQueue&) = delete;
        // ムーブは許可
        DX12GpuCommandQueue(DX12GpuCommandQueue&&) = default;
        DX12GpuCommandQueue& operator=(DX12GpuCommandQueue&&) = default;
        ~DX12GpuCommandQueue() override = default;

        CommandListType type() const override;
        Result submit(std::vector<ICommandContext*>& contexts) override;
        Result signal() override;
        Result wait() override;
        Result wait_for_queue(IQueueContext& queue) override;
    private:
        Result create_fence(ID3D12Device& device);
        Result create_fence_event();
        Result create_command_queue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
    private:
        ComPtr<ID3D12CommandQueue> m_commandQueue = nullptr;
        ComPtr<ID3D12Fence> m_fence = nullptr;
        HANDLE m_fenceEvent = {};
        uint64_t m_fenceValue = {};
        CommandListType m_type = CommandListType::Graphics;
    };
}
