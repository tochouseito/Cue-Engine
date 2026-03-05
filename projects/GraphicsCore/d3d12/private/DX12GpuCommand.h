#pragma once
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "GpuBuffer.h"
#include "DescriptorAllocator.h"
#include <FrameGraph.h>
#include <Pool.h>

#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace Cue::GraphicsCore::DX12
{
    class DX12BufferManager;
    class DX12TextureManager;
    class DX12QueueContext;

    class DX12CommandContext : public ICommandContext
    {
    public:
        DX12CommandContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        virtual ~DX12CommandContext() override;

        Result setup() override;
        Result reset() override;
        Result close() override;
        void bind_resources(DX12BufferManager& bufferManager, DX12TextureManager& textureManager, IViewManager& viewManager, DescriptorAllocator& descriptorAllocator) noexcept;
        ID3D12CommandAllocator* get_command_allocator() const noexcept
        {
            return m_commandAllocator.Get();
        }
        ID3D12GraphicsCommandList* get_command_list() const noexcept
        {
            return m_commandList.Get();
        }
        virtual CommandListType type() const = 0;
        virtual NativeCommandList native_command_list() const override
        {
            return reinterpret_cast<NativeCommandList>(m_commandList.Get());
        }
    protected:
        ComPtr<ID3D12GraphicsCommandList> m_commandList = nullptr;
        ComPtr<ID3D12CommandAllocator> m_commandAllocator = nullptr;
    private:
        Result create_command_allocator(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        Result create_command_list(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        [[nodiscard]] Result resolve_buffer_resource(BufferHandle handle, GpuBufferResource*& outBuffer) const;
        [[nodiscard]] Result resolve_texture_resource(TextureHandle handle, GpuTextureResource*& outTexture) const;
    public:
        // Commands
        virtual void begin_event(const char*) override {}
        virtual void end_event() override {}

        Result resource_barrier(const ResourceBarrierDesc& barrier) override;
        Result resource_barriers(const ResourceBarrierDesc* barriers, size_t count) override;
        Result clear_render_target(TextureHandle handle, const float clearColor[4]) override;
        Result set_viewport_scissor(uint32_t width, uint32_t height) override;
        Result set_render_targets(const ViewHandle* renderTargetViews, uint32_t renderTargetCount, ViewHandle depthStencilView) override;
    private:
        DX12BufferManager* m_bufferManager = nullptr;
        DX12TextureManager* m_textureManager = nullptr;
        IViewManager* m_viewManager = nullptr;
        DescriptorAllocator* m_descriptorAllocator = nullptr;
        DescriptorAllocator::TableID m_transientRtv = {};
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
        explicit DX12CommandPool(DX12RenderDevice& device);
        ~DX12CommandPool() override = default;

        Result initialize() override;

        void bind_resources(DX12BufferManager& bufferManager, DX12TextureManager& textureManager, IViewManager& viewManager, DescriptorAllocator& descriptorAllocator) noexcept;
        Result acquire_context(CommandListType type, CommandContextLease& outContext) override;
        Result retire_context(CommandContextLease&& context, IQueueContext& queueContext, const QueueSyncPoint& completionPoint) override;
    private:
        struct InFlightCommandContext final
        {
            std::unique_ptr<DX12CommandContext> context = nullptr;
            DX12QueueContext* queueContext = nullptr;
            QueueSyncPoint completionPoint = {};
        };

        void collect_completed_contexts(CommandListType type);
        void recycle_context(std::unique_ptr<DX12CommandContext> context);
    private:
        Core::Pool<DX12GraphicsCommandContext, std::function<void(DX12GraphicsCommandContext&)>> m_graphicsContextPool;
        std::mutex m_graphicsContextPoolMutex;

        Core::Pool<DX12ComputeCommandContext, std::function<void(DX12ComputeCommandContext&)>> m_computeContextPool;
        std::mutex m_computeContextPoolMutex;

        Core::Pool<DX12CopyCommandContext, std::function<void(DX12CopyCommandContext&)>> m_copyContextPool;
        std::mutex m_copyContextPoolMutex;
        std::vector<InFlightCommandContext> m_inFlightContexts;
        DX12BufferManager* m_bufferManager = nullptr;
        DX12TextureManager* m_textureManager = nullptr;
        IViewManager* m_viewManager = nullptr;
        DescriptorAllocator* m_descriptorAllocator = nullptr;
    };

    class DX12QueueContext : public IQueueContext
    {
    public:
        DX12QueueContext(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        virtual ~DX12QueueContext() override;
        virtual CommandListType type() const = 0;
        Result submit(ICommandContext& cmd) override;
        Result signal(QueueSyncPoint& outPoint) override;
        Result wait(const IQueueContext& producerQueue, const QueueSyncPoint& point) override;
        bool is_complete(const QueueSyncPoint& point) const override;
        Result wait_for_last_signal();
        ID3D12CommandQueue* get_command_queue() const noexcept
        {
            return m_commandQueue.Get();
        }
    private:
        Result create_fence(ID3D12Device& device);
        Result create_fence_event();
        Result create_command_queue(ID3D12Device& device, D3D12_COMMAND_LIST_TYPE type);
        Result wait_for_fence_value(uint64_t value);
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
        explicit DX12QueuePool(DX12RenderDevice& device);
        ~DX12QueuePool() override = default;
        Result initialize() override;
        Result acquire_queue(CommandListType type, QueueContextLease& outQueue) override;
    private:
        void recycle_graphics_queue(IQueueContext* raw) noexcept;
        void recycle_compute_queue(IQueueContext* raw) noexcept;
        void recycle_copy_queue(IQueueContext* raw) noexcept;

        // 各キューの数
        static const uint32_t k_graphicsQueueCount = 1;///> 
        static const uint32_t k_computeQueueCount = 4; ///>
        static const uint32_t k_copyQueueCount = 2;    ///>

        Core::Pool<DX12GraphicsQueueContext, std::function<void(DX12GraphicsQueueContext&)>> m_graphicsQueuePool;
        std::mutex m_graphicsQueuePoolMutex;
        std::condition_variable m_graphicsQueuePoolCv;
        uint32_t m_graphicsQueueInUseCount = 0;

        Core::Pool<DX12ComputeQueueContext, std::function<void(DX12ComputeQueueContext&)>> m_computeQueuePool;
        std::mutex m_computeQueuePoolMutex;
        std::condition_variable m_computeQueuePoolCv;
        uint32_t m_computeQueueInUseCount = 0;

        Core::Pool<DX12CopyQueueContext, std::function<void(DX12CopyQueueContext&)>> m_copyQueuePool;
        std::mutex m_copyQueuePoolMutex;
        std::condition_variable m_copyQueuePoolCv;
        uint32_t m_copyQueueInUseCount = 0;
    };
} // namespace Cue::GraphicsCore::DX12
