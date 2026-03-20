#pragma once

// === Base includes ===
#include <Result.h>

namespace Cue::RHI
{
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

        // 初期化
        virtual Result initialize(bool enableDebugLayer = false) = 0;
    };

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
    };

    class ICommandPool
    {
    public:
        ICommandPool() = default;
        // コピー禁止
        ICommandPool(const ICommandPool&) = delete;
        ICommandPool& operator=(const ICommandPool&) = delete;
        // ムーブは許可
        ICommandPool(ICommandPool&&) = default;
        ICommandPool& operator=(ICommandPool&&) = default;
        virtual ~ICommandPool() = default;
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
        
    };
}
