#pragma once
#include "GraphicsCommon.h"
#include "ResourceHandle.h"
#include <functional>

namespace Cue::GraphicsCore
{
    struct QueueSyncPoint final
    {
        CommandListType queueType = CommandListType::Graphics;
        uint64_t value = 0;
    };

    struct ResourceBarrierDesc final
    {
        ResourceKind kind = ResourceKind::Buffer;
        uint32_t index = 0;
        uint32_t generation = 0;
        ResourceState before = ResourceState::Common;
        ResourceState after = ResourceState::Common;
    };

    using NativeCommandList = void*; // 透過型ハンドル（実際の型はバックエンドごとに異なる）

    class ICommandContext
    {
    public:
        ICommandContext() = default;
        // コピー禁止
        ICommandContext(const ICommandContext&) = delete;
        ICommandContext& operator=(const ICommandContext&) = delete;
        // ムーブは許可
        ICommandContext(ICommandContext&&) = default;
        ICommandContext& operator=(ICommandContext&&) = default;
        virtual ~ICommandContext() = default;

        virtual Result setup() = 0;
        virtual Result reset() = 0;
        virtual Result close() = 0;
        virtual CommandListType type() const = 0;
        virtual NativeCommandList native_command_list() const = 0;

        bool is_list_empty() const
        {
            return m_listEmpty;
        }
    protected:
        bool m_listEmpty = true; // コマンドリストが空かどうか
    public:
        // Commands
        virtual void begin_event(const char* name) = 0;
        virtual void end_event() = 0;

        virtual Result resource_barrier(const ResourceBarrierDesc& barrier) = 0;
        virtual Result resource_barriers(const ResourceBarrierDesc* barriers, size_t count) = 0;
        virtual Result clear_render_target(TextureHandle handle, const float clearColor[4]) = 0;
        virtual Result set_viewport_scissor(uint32_t width, uint32_t height) = 0;
        virtual Result set_render_targets(const ViewHandle* renderTargetViews, uint32_t renderTargetCount, ViewHandle depthStencilView) = 0;
    };

    class IQueueContext
    {
    public:
        IQueueContext() = default;
        // コピー禁止
        IQueueContext(const IQueueContext&) = delete;
        IQueueContext& operator=(const IQueueContext&) = delete;
        // ムーブは許可
        IQueueContext(IQueueContext&&) = default;
        IQueueContext& operator=(IQueueContext&&) = default;
        virtual ~IQueueContext() = default;

        virtual CommandListType type() const = 0;
        virtual Result submit(ICommandContext& cmd) = 0;
        virtual Result signal(QueueSyncPoint& outPoint) = 0;
        virtual Result wait(const IQueueContext& producerQueue, const QueueSyncPoint& point) = 0;
        virtual bool is_complete(const QueueSyncPoint& point) const = 0;
    };

    using CommandContextLease = std::unique_ptr<ICommandContext, std::function<void(ICommandContext*)>>;
    using QueueContextLease = std::unique_ptr<IQueueContext, std::function<void(IQueueContext*)>>;

    class IRenderDevice
    {
    public:
        IRenderDevice() = default;
        // コピー禁止
        IRenderDevice(const IRenderDevice&) = delete;
        IRenderDevice& operator=(const IRenderDevice&) = delete;
        // ムーブは許可
        IRenderDevice(IRenderDevice&&) = default;
        IRenderDevice& operator=(IRenderDevice&&) = default;
        virtual ~IRenderDevice() = default;
        virtual Result initialize(bool enableDebugLayer = false) = 0;

    };

    class ICommandPool
    {
    public:
        ICommandPool() = default;

        virtual ~ICommandPool() = default;
        virtual Result initialize() = 0;
        virtual Result acquire_context(CommandListType type, CommandContextLease& outContext) = 0;
        virtual Result retire_context(CommandContextLease&& context, IQueueContext& queueContext, const QueueSyncPoint& completionPoint) = 0;
    };

    class IQueuePool
    {
    public:
        IQueuePool() = default;
        // コピー禁止
        IQueuePool(const IQueuePool&) = delete;
        IQueuePool& operator=(const IQueuePool&) = delete;
        // ムーブは許可
        IQueuePool(IQueuePool&&) = default;
        IQueuePool& operator=(IQueuePool&&) = default;
        virtual ~IQueuePool() = default;
        virtual Result initialize() = 0;
        virtual Result acquire_queue(CommandListType type, QueueContextLease& outQueue) = 0;
    };

} // namespace Cue::GraphicsCore
