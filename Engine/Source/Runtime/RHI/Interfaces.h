#pragma once

// === Base includes ===
#include <Result.h>
#include <CueAssert.h>

// === Math includes ===
#include <CueMath.h>

// === Core includes ===
#include <Native/Handle.h>
#include <Native/EngineNativeStruct.h>
#include <IO/Logger.h>
#include <Container/Pool.h>
#include <Container/Registry.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <functional>

namespace Cue::RHI
{
    struct BufferTag {};
    struct TextureTag {};
    struct ViewTag {};
    struct PipelineTag {};
    struct RootSignatureTag {};
    struct ShaderBlobTag {};
    struct StaticMeshTag {};

    using bufferHandle = Core::Handle<BufferTag>;
    using textureHandle = Core::Handle<TextureTag>;
    using viewHandle = Core::Handle<ViewTag>;
    using pipelineStateHandle = Core::Handle<PipelineTag>;
    using rootSignatureHandle = Core::Handle<RootSignatureTag>;
    using shaderBlobHandle = Core::Handle<ShaderBlobTag>;
    using staticMeshHandle = Core::Handle<StaticMeshTag>;

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

    enum class PrimitiveTopologyType : uint8_t
    {
        Point,
        Line,
        Triangle,
    };

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

    struct ResourceBarrierDesc final
    {
        ResourceState before = ResourceState::Common;
        ResourceState after = ResourceState::Common;
    };

    struct BufferCopyRegion final
    {
        bufferHandle srcBufferHandle = {};
        uint32_t srcUploadResourceIndex = 0;
        uint64_t srcByteOffset = 0;
        bufferHandle dstBufferHandle = {};
        uint32_t dstDefaultResourceIndex = 0;
        uint64_t dstByteOffset = 0;
        uint64_t byteSize = 0;
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

        virtual Result setup(uint32_t frameIndex, uint32_t bufferCount) = 0;
        virtual Result reset() = 0;
        virtual Result close() = 0;
        virtual CommandListType type() const = 0;
        virtual void* native_command_list() const = 0;

        // --- GPU プロファイリング用のイベントマーカー ---
        virtual void begin_event(const char* name) = 0;
        virtual void end_event() = 0;

        // --- Commaonds ---
        virtual Result resource_barrier(bufferHandle handle, const ResourceBarrierDesc desc) = 0;
        virtual Result resource_barrier(textureHandle handle, const ResourceBarrierDesc desc) = 0;
        virtual Result copy_buffer_region(const BufferCopyRegion& region) = 0;
        virtual Result clear_render_target(viewHandle handle, const float clearColor[4]) = 0;
        virtual Result clear_depth_stencil(viewHandle handle, float depth, uint8_t stencil) = 0;
        virtual Result clear_unordered_access_uint(viewHandle handle, const uint32_t clearValues[4]) = 0;
        virtual Result set_viewport_scissor(uint32_t width, uint32_t height) = 0;
        virtual Result set_primitive_topology(PrimitiveTopologyType topology) = 0;
        virtual Result set_graphics_pipeline(pipelineStateHandle handle) = 0;
        virtual Result set_compute_pipeline(pipelineStateHandle handle) = 0;
        virtual Result set_32bit_constant(uint32_t rootParameterIndex, uint32_t value) = 0;
        virtual Result set_cbv(uint32_t rootParameterIndex, bufferHandle handle) = 0;
        virtual Result set_srv(uint32_t rootParameterIndex, bufferHandle handle) = 0;
        virtual Result set_uav(uint32_t rootParameterIndex, bufferHandle handle) = 0;
        virtual Result set_graphics_descriptor_table(uint32_t rootParameterIndex, viewHandle handle) = 0;
        virtual Result dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
        virtual Result set_render_targets(const viewHandle* renderTargetViews, uint32_t renderTargetCount, viewHandle depthStencilView) = 0;
        virtual Result draw_instanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation) = 0;
        virtual Result draw_indexed_instanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation) = 0;
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

    using commandContextLease = std::unique_ptr<ICommandContext, std::function<void(ICommandContext*)>>;
    using queueContextLease = std::unique_ptr<IQueueContext, std::function<void(IQueueContext*)>>;
    using queueContextPtr = IQueueContext*;

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

        /// @brief コマンドコンテキストをプールから取得します。
        virtual Result get_command_context(CommandListType type, commandContextLease& outContext) = 0;
        /// @brief コマンドコンテキストをプールへ返却します。
        virtual Result return_command_context(commandContextLease& context) = 0;
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

        /// @brief キューコンテキストをプールから取得します。
        virtual Result get_queue_context(CommandListType type, queueContextLease& outContext) = 0;
        /// @brief キューコンテキストをプールへ返却します。
        virtual Result return_queue_context(queueContextLease& context) = 0;
        /// @brief graphics キューへ待機させます。
        virtual Result wait_for_graphics_queue() = 0;
        /// @brief present キューの取得
        virtual queueContextPtr get_present_queue_context() = 0;
    };
}
