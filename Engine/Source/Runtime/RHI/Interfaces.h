#pragma once

// === Base includes ===
#include <Result.h>

// === C++ includes ===
#include <memory>
#include <functional>
#include <vector>

namespace Cue::RHI
{
    enum class CommandListType : uint8_t
    {
        Graphics,
        Compute,
        Copy
    };

    enum class ResourceState : uint8_t
    {
        Common,
        CopySource,
        CopyDest,
        RenderTarget,
        UnorderedAccess,
        ShaderResource,
        DepthWrite,
        Present
    };

    inline const char* resource_state_to_string(ResourceState state) noexcept
    {
        switch (state)
        {
        case ResourceState::Common: return "Common";
        case ResourceState::RenderTarget: return "RenderTarget";
        case ResourceState::UnorderedAccess: return "UnorderedAccess";
        case ResourceState::ShaderResource: return "ShaderResource";
        case ResourceState::DepthWrite: return "DepthWrite";
        case ResourceState::Present: return "Present";
        default: return "Unknown";
        }
    }

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
        virtual ResourceState current_state() const noexcept = 0;
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

        virtual Result reset() = 0;
        virtual Result close() = 0;
        virtual CommandListType type() const = 0;

        // --- GPU プロファイリング用のイベントマーカー ---
        virtual void begin_event(const char* name) = 0;
        virtual void end_event() = 0;
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

        virtual CommandListType type() const = 0;
        virtual Result submit(std::vector<ICommandContext*>& contexts) = 0;
        virtual Result signal() = 0;
        virtual Result wait() = 0;
        virtual Result wait_for_queue(IQueueContext& queue) = 0;
    };

    using CommandContextLease = std::unique_ptr<ICommandContext, std::function<void(ICommandContext*)>>;
    using QueueContextLease = std::unique_ptr<IQueueContext, std::function<void(IQueueContext*)>>;

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
