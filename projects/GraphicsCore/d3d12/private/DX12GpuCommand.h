#pragma once
#include "stdafx.h"
#include "DX12RenderDevice.h"
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
        DX12CommandContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        virtual ~DX12CommandContext() override = default;

        Result reset() override;
        Result close() override;
        ID3D12CommandAllocator* get_command_allocator() const noexcept
        {
            return m_commandAllocator.Get();
        }
        ID3D12GraphicsCommandList* get_command_list() const noexcept
        {
            return m_commandList.Get();
        }
        virtual CommandListType type() const = 0;
    protected:
        ComPtr<ID3D12GraphicsCommandList> m_commandList = nullptr;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator = nullptr;
    private:
        Result create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        Result create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
    public:
        // Commands
        virtual void begin_event(const char* name) = 0;
        virtual void end_event() = 0;

        virtual Result resource_barrier(const ResourceBarrierDesc& barrier) = 0;
        virtual Result resource_barriers(const ResourceBarrierDesc* barriers, size_t count) = 0;
    };

    class DX12GraphicsCommandContext final : public DX12CommandContext
    {
    public:
        DX12GraphicsCommandContext(ID3D12Device& device)
            : DX12CommandContext(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
        }
        ~DX12GraphicsCommandContext() override = default;
        CommandListType type() const override
        {
            return CommandListType::Graphics;
        }
    };

    class DX12ComputeCommandContext final : public DX12CommandContext
    {
    public:
        DX12ComputeCommandContext(ID3D12Device& device)
            : DX12CommandContext(device, D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
        }
        ~DX12ComputeCommandContext() override = default;
        CommandListType type() const override
        {
            return CommandListType::Compute;
        }
    };

    class DX12CopyCommandContext final : public DX12CommandContext
    {
    public:
        DX12CopyCommandContext(ID3D12Device& device)
            : DX12CommandContext(device, D3D12_COMMAND_LIST_TYPE_COPY)
        {
        }
        ~DX12CopyCommandContext() override = default;
        CommandListType type() const override
        {
            return CommandListType::Copy;
        }
    };

    using GraphicsCommandPooledPtr = Core::Pool<DX12GraphicsCommandContext, std::function<void(DX12GraphicsCommandContext&)>>::pooled_ptr;
    using ComputeCommandPooledPtr = Core::Pool<DX12ComputeCommandContext, std::function<void(DX12ComputeCommandContext&)>>::pooled_ptr;
    using CopyCommandPooledPtr = Core::Pool<DX12CopyCommandContext, std::function<void(DX12CopyCommandContext&)>>::pooled_ptr;

    class DX12CommandPool final : public ICommandPool
    {
    public:
        DX12CommandPool() = default;
        ~DX12CommandPool() override = default;

        Result initialize(DX12RenderDevice& device) override;

        Result acquire_context(CommandListType type, CommandContextLease& outContext) override;
    private:
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

    class DX12QueueContext : public IQueueContext
    {
    public:
        DX12QueueContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        virtual ~DX12QueueContext() override;
        virtual CommandListType type() const = 0;
        Result submit(ICommandContext& cmd) override;
        Result signal(QueueSyncPoint& outPoint) override;
        Result wait(const QueueSyncPoint& point) override;
        Result wait_for_last_signal();
        ID3D12CommandQueue* get_command_queue() const noexcept
        {
            return m_commandQueue.Get();
        }
    private:
        Result create_fence(ID3D12Device& device);
        Result create_fence_event();
        Result create_command_queue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
    protected:
        QueueSyncPoint m_lastSignalPoint{};
        ComPtr<ID3D12CommandQueue> m_commandQueue = nullptr;
        ComPtr<ID3D12Fence> m_fence = nullptr;
        HANDLE m_fenceEvent = {};
        uint64_t m_fenceValue = {};
    };

    class DX12GraphicsQueueContext final : public DX12QueueContext
    {
    public:
        DX12GraphicsQueueContext(ID3D12Device& device)
            : DX12QueueContext(device, D3D12_COMMAND_LIST_TYPE_DIRECT)
        {
        }
        ~DX12GraphicsQueueContext() override = default;
        CommandListType type() const override
        {
            return CommandListType::Graphics;
        }
    };

    class DX12ComputeQueueContext final : public DX12QueueContext
    {
    public:
        DX12ComputeQueueContext(ID3D12Device& device)
            : DX12QueueContext(device, D3D12_COMMAND_LIST_TYPE_COMPUTE)
        {
        }
        ~DX12ComputeQueueContext() override = default;
        CommandListType type() const override
        {
            return CommandListType::Compute;
        }
    };

    class DX12CopyQueueContext final : public DX12QueueContext
    {
    public:
        DX12CopyQueueContext(ID3D12Device& device)
            : DX12QueueContext(device, D3D12_COMMAND_LIST_TYPE_COPY)
        {
        }
        ~DX12CopyQueueContext() override = default;
        CommandListType type() const override
        {
            return CommandListType::Copy;
        }
    };

    using GraphicsQueuePooledPtr = Core::Pool<DX12GraphicsQueueContext, std::function<void(DX12GraphicsQueueContext&)>>::pooled_ptr;
    using ComputeQueuePooledPtr = Core::Pool<DX12ComputeQueueContext, std::function<void(DX12ComputeQueueContext&)>>::pooled_ptr;
    using CopyQueuePooledPtr = Core::Pool<DX12CopyQueueContext, std::function<void(DX12CopyQueueContext&)>>::pooled_ptr;

    class DX12QueuePool final : public IQueuePool
    {
        public:
        DX12QueuePool() = default;
        ~DX12QueuePool() override = default;
        Result initialize(IRenderDevice& device) override;

        Result acquire_queue(CommandListType type, QueueContextLease& outQueue) override;
    private:
        // 各キューの数
        static const uint32_t k_graphicsQueueCount = 1;///> 
        static const uint32_t k_computeQueueCount = 4; ///>
        static const uint32_t k_copyQueueCount = 2;    ///>

        Core::Pool<DX12GraphicsQueueContext, std::function<void(DX12GraphicsQueueContext&)>> m_graphicsQueuePool{
            4,
            [](DX12GraphicsQueueContext& ctx) {
                ctx.wait_for_last_signal();
            }
        };
        std::mutex m_graphicsQueuePoolMutex;

        Core::Pool<DX12ComputeQueueContext, std::function<void(DX12ComputeQueueContext&)>> m_computeQueuePool{
            4,
            [](DX12ComputeQueueContext& ctx) {
                ctx.wait_for_last_signal();
            }
        };
        std::mutex m_computeQueuePoolMutex;

        Core::Pool<DX12CopyQueueContext, std::function<void(DX12CopyQueueContext&)>> m_copyQueuePool{
            4,
            [](DX12CopyQueueContext& ctx) {
                ctx.wait_for_last_signal();
            }
        };
        std::mutex m_copyQueuePoolMutex;
    };
} // namespace Cue::GraphicsCore::DX12
