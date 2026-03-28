#pragma once

// === RHI includes ===
#include <RHICommon.h>

// === C++ includes ===
#include <mutex>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "DX12RenderDevice.h"

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

        /// --- GPU プロファイリング用のイベントマーカー ---
        void begin_event(const char* name) override;
        void end_event() override;
    private:
        Result create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        Result create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
    private:
        ComPtr<ID3D12GraphicsCommandList> m_commandList = nullptr;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator = nullptr;
        CommandListType m_type = CommandListType::Graphics;
    };

    class DX12CommandPool final : public ICommandPool
    {
    public:
        DX12CommandPool(DX12RenderDevice& renderDevice)
            : m_renderDevice(renderDevice),
            m_graphicsContextPool(
                32,
                [](DX12GpuCommandContext& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandContext>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
                })
            , m_computeContextPool(
                32,
                [](DX12GpuCommandContext& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandContext>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
                })
            , m_copyContextPool(
                32,
                [](DX12GpuCommandContext& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandContext>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_COPY);
                })
        {
        }
        ~DX12CommandPool() override = default;

        Result get_command_context(CommandListType type, CommandContextLease& outContext);
        Result return_command_context(CommandContextLease& context);
    private:
        DX12RenderDevice& m_renderDevice;
        Core::Pool<DX12GpuCommandContext, std::function<void(DX12GpuCommandContext&)>> m_graphicsContextPool;
        std::mutex m_graphicsPoolMutex;
        Core::Pool<DX12GpuCommandContext, std::function<void(DX12GpuCommandContext&)>> m_computeContextPool;
        std::mutex m_computePoolMutex;
        Core::Pool<DX12GpuCommandContext, std::function<void(DX12GpuCommandContext&)>> m_copyContextPool;
        std::mutex m_copyPoolMutex;
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

        // --- 取得 ---
        ID3D12CommandQueue* command_queue() const { return m_commandQueue.Get(); }
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

    class DX12QueuePool final : public IQueuePool
    {
    public:
        DX12QueuePool(DX12RenderDevice& renderDevice)
            : m_renderDevice(renderDevice),
            m_graphicsQueuePool(
                1,
                [](DX12GpuCommandQueue& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandQueue>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_DIRECT);
                })
            , m_computeQueuePool(
                4,
                [](DX12GpuCommandQueue& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandQueue>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
                })
            , m_copyQueuePool(
                4,
                [](DX12GpuCommandQueue& ctx)
                {
                    (void)ctx;
                },
                [d3d12Device = renderDevice.get_d3d12_device()]()
                {
                    return std::make_unique<DX12GpuCommandQueue>(*d3d12Device, D3D12_COMMAND_LIST_TYPE_COPY);
                })
        {
        }
        ~DX12QueuePool() override = default;
         Result get_queue_context(CommandListType type, QueueContextLease& outContext);
         Result return_queue_context(QueueContextLease& context);
    private:
        DX12RenderDevice& m_renderDevice;
        Core::Pool<DX12GpuCommandQueue, std::function<void(DX12GpuCommandQueue&)>> m_graphicsQueuePool;
        std::mutex m_graphicsPoolMutex;
        Core::Pool<DX12GpuCommandQueue, std::function<void(DX12GpuCommandQueue&)>> m_computeQueuePool;
        std::mutex m_computePoolMutex;
        Core::Pool<DX12GpuCommandQueue, std::function<void(DX12GpuCommandQueue&)>> m_copyQueuePool;
        std::mutex m_copyPoolMutex;
    };
}
