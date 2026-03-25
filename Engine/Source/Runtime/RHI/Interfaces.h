#pragma once

// === Base includes ===
#include <Result.h>

namespace Cue::RHI
{
    /// @brief レンダーデバイスの共通インターフェースです。
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

        /// @brief レンダーデバイスを初期化します。
        virtual Result initialize(bool a_enableDebugLayer = false) = 0;
    };

    class GpuResource
    {
    public:
        GpuResource() = default;
        // コピー禁止
        GpuResource(const GpuResource&) = delete;
        GpuResource& operator=(const GpuResource&) = delete;
        // ムーブは許可
        GpuResource(GpuResource&&) = default;
        GpuResource& operator=(GpuResource&&) = default;
        virtual ~GpuResource() = default;
    };

    /// @brief コマンドコンテキストの共通インターフェースです。
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

    /// @brief キューコンテキストの共通インターフェースです。
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

    /// @brief コマンドプールの共通インターフェースです。
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

    /// @brief キュープールの共通インターフェースです。
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
