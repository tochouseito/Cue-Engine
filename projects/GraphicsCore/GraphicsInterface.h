#pragma once
#include "GraphicsCommon.h"
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

    class ICommandContext
    {
    public:
        ICommandContext() = default;
        virtual ~ICommandContext() = default;

        virtual Result reset() = 0;
        virtual Result close() = 0;
        virtual CommandListType type() const = 0;

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
    };

    class IQueueContext
    {
    public:
        IQueueContext() = default;
        virtual ~IQueueContext() = default;

        virtual CommandListType type() const = 0;
        virtual Result submit(ICommandContext& cmd) = 0;
        virtual Result signal(QueueSyncPoint& outPoint) = 0;
        virtual Result wait(const QueueSyncPoint& point) = 0;
    };

    using CommandContextLease = std::unique_ptr<ICommandContext, std::function<void(ICommandContext*)>>;
    using QueueContextLease = std::unique_ptr<IQueueContext, std::function<void(IQueueContext*)>>;

    class IRenderDevice
    {
    public:
        IRenderDevice() = default;
        virtual ~IRenderDevice() = default;
        virtual Result initialize(bool enableDebugLayer = false) = 0;

        virtual Result create_command_context(CommandListType type, ICommandContext& outContext) = 0;
        virtual Result create_command_queue(CommandListType type, IQueueContext& outQueue) = 0;
    };

    class ICommandPool
    {
    public:
        ICommandPool() = default;
        virtual ~ICommandPool() = default;
        virtual Result initialize() = 0;
        virtual Result acquire_context(CommandListType type, CommandContextLease& outContext) = 0;
    };

    class IQueuePool
    {
    public:
        IQueuePool() = default;
        virtual ~IQueuePool() = default;
        virtual Result initialize() = 0;
        virtual Result acquire_queue(CommandListType type, QueueContextLease& outQueue) = 0;
    };

} // namespace Cue::GraphicsCore
