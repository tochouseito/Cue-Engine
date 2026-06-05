#pragma once

/// ************************************************************************************
/// RHI共通
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>
#include <CueAssert.h>

// === Math includes ===
#include <CueMath.h>

// === Core includes ===
#include <Native/Handle.h>
#include <IO/Logger.h>
#include <Container/Registry.h>
#include <Container/RingBuffer.h>
#include <Container/Pool.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <vector>
#include <string>
#include <string_view>
#include <functional>

namespace Cue::RHI
{
    class IQueueContext;

    enum class IndexFormat : uint8_t;
    enum class ColorFormat : uint8_t;

    struct BufferTag {};
    struct TextureTag {};
    struct ViewTag {};
    struct PipelineTag {};
    struct RootSignatureTag {};
    struct ShaderBlobTag {};
    struct MeshTag {};

    using BufferHandle = Core::Handle<BufferTag>;
    using TextureHandle = Core::Handle<TextureTag>;
    using ViewHandle = Core::Handle<ViewTag>;
    using PipelineStateHandle = Core::Handle<PipelineTag>;
    using RootSignatureHandle = Core::Handle<RootSignatureTag>;
    using ShaderBlobHandle = Core::Handle<ShaderBlobTag>;
    using MeshHandle = Core::Handle<MeshTag>;

    struct GpuMemoryUsage
    {
        uint64_t budgetBytes = 0;
        uint64_t currentUsageBytes = 0;
        uint64_t availableForReservationBytes = 0;
        uint64_t currentReservationBytes = 0;
    };

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
        VertexBuffer,
        IndexBuffer,
        IndirectArgument,
        DepthWrite,
        Present
    };

    inline const char* resource_state_to_string(ResourceState state) noexcept
    {
        switch (state)
        {
        case ResourceState::Common: return "Common";
        case ResourceState::CopySource: return "CopySource";
        case ResourceState::CopyDest: return "CopyDest";
        case ResourceState::RenderTarget: return "RenderTarget";
        case ResourceState::UnorderedAccess: return "UnorderedAccess";
        case ResourceState::ShaderResource: return "ShaderResource";
        case ResourceState::VertexBuffer: return "VertexBuffer";
        case ResourceState::IndexBuffer: return "IndexBuffer";
        case ResourceState::IndirectArgument: return "IndirectArgument";
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

    /// @brief レンダーデバイスの共通インターフェース
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

        /// @brief レンダーデバイスを初期化する
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
        BufferHandle srcBufferHandle = {};
        uint32_t srcUploadResourceIndex = 0;
        uint64_t srcByteOffset = 0;
        BufferHandle dstBufferHandle = {};
        uint32_t dstDefaultResourceIndex = 0;
        uint64_t dstByteOffset = 0;
        uint64_t byteSize = 0;
    };

    struct TextureToBufferCopyRegion final
    {
        TextureHandle srcTextureHandle = {};
        uint32_t srcX = 0;
        uint32_t srcY = 0;
        uint32_t width = 1;
        uint32_t height = 1;
        ColorFormat format;
        BufferHandle dstBufferHandle = {};
        uint32_t dstReadbackResourceIndex = 0;
        uint64_t dstByteOffset = 0;
    };

    struct BufferToReadbackCopyRegion final
    {
        // GPU default heap の buffer 内容を CPU 可視 readback heap へコピーする。
        // GPU が作った debug/statistics 値を ImGui やログへ出す用途で使う。
        BufferHandle srcBufferHandle = {};
        uint32_t srcDefaultResourceIndex = 0;
        uint64_t srcByteOffset = 0;
        BufferHandle dstBufferHandle = {};
        uint32_t dstReadbackResourceIndex = 0;
        uint64_t dstByteOffset = 0;
        uint64_t byteSize = 0;
    };

    /// @brief コマンドコンテキストの共通インターフェース
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
        virtual bool supports_timestamps() const = 0;
        virtual Result write_timestamp(uint32_t queryIndex) = 0;
        virtual Result resolve_timestamps(uint32_t firstQueryIndex, uint32_t queryCount) = 0;
        virtual Result read_timestamp(uint32_t queryIndex, uint64_t& outValue) const = 0;
        virtual void set_pending_fence(IQueueContext* a_queue, uint64_t a_fenceValue) = 0;
        virtual bool is_pending_fence_complete() const = 0;
        virtual Result wait_for_pending_fence() = 0;

        // --- GPU プロファイリング用のイベントマーカー ---
        virtual void begin_event(const char* name) = 0;
        virtual void end_event() = 0;

        // --- Commaonds ---
        virtual Result resource_barrier(BufferHandle handle, const ResourceBarrierDesc desc) = 0;
        virtual Result resource_barrier(TextureHandle handle, const ResourceBarrierDesc desc) = 0;
        virtual Result copy_buffer_region(const BufferCopyRegion& region) = 0;
        virtual Result copy_buffer_region_to_readback(const BufferToReadbackCopyRegion& region) = 0;
        virtual Result copy_texture_region_to_buffer(const TextureToBufferCopyRegion& region) = 0;
        virtual Result clear_render_target(ViewHandle handle, const float clearColor[4]) = 0;
        virtual Result clear_depth_stencil(ViewHandle handle, float depth, uint8_t stencil) = 0;
        virtual Result clear_unordered_access_uint(ViewHandle handle, const uint32_t clearValues[4]) = 0;
        virtual Result set_viewport_scissor(uint32_t width, uint32_t height) = 0;
        virtual Result set_viewport_scissor(
            uint32_t x,
            uint32_t y,
            uint32_t width,
            uint32_t height) = 0;
        virtual Result set_primitive_topology(PrimitiveTopologyType topology) = 0;
        virtual Result set_vertex_buffer(uint32_t slot, BufferHandle handle) = 0;
        virtual Result set_index_buffer(BufferHandle handle, IndexFormat format) = 0;
        virtual Result set_graphics_pipeline(PipelineStateHandle handle) = 0;
        virtual Result set_compute_pipeline(PipelineStateHandle handle) = 0;
        virtual Result set_32bit_constant(uint32_t rootParameterIndex, uint32_t value) = 0;
        virtual Result set_cbv(uint32_t rootParameterIndex, BufferHandle handle) = 0;
        virtual Result set_srv(uint32_t rootParameterIndex, BufferHandle handle) = 0;
        virtual Result set_uav(uint32_t rootParameterIndex, BufferHandle handle) = 0;
        virtual Result set_graphics_descriptor_table(uint32_t rootParameterIndex, ViewHandle handle) = 0;
        virtual Result set_compute_descriptor_table(uint32_t rootParameterIndex, ViewHandle handle) = 0;
        virtual Result set_graphics_texture_table(uint32_t rootParameterIndex) = 0;
        virtual Result dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
        virtual Result set_render_targets(const ViewHandle* renderTargetViews, uint32_t renderTargetCount, ViewHandle depthStencilView) = 0;
        virtual Result draw_instanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation) = 0;
        virtual Result draw_indexed_instanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation) = 0;
        virtual Result execute_indexed_indirect(BufferHandle commandBufferHandle, BufferHandle commandCountBufferHandle, uint32_t maxCommandCount) = 0;
    };

    /// @brief キューコンテキストの共通インターフェース
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
        virtual Result signal(uint64_t* outFenceValue = nullptr) = 0;
        virtual Result wait() = 0;
        virtual Result wait_for_fence(uint64_t fenceValue) = 0;
        virtual bool is_fence_complete(uint64_t fenceValue) const = 0;
        virtual Result wait_for_queue(IQueueContext& queue) = 0;
        virtual Result get_timestamp_frequency(uint64_t& outFrequency) const = 0;
    };

    using commandContextLease = std::unique_ptr<ICommandContext, std::function<void(ICommandContext*)>>;
    using queueContextLease = std::unique_ptr<IQueueContext, std::function<void(IQueueContext*)>>;
    using queueContextPtr = IQueueContext*;

    /// @brief コマンドプールの共通インターフェース
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

        /// @brief コマンドコンテキストをプールから取得する
        virtual Result get_command_context(CommandListType type, commandContextLease& outContext) = 0;
        /// @brief コマンドコンテキストをプールへ返却し
        virtual Result return_command_context(commandContextLease& context) = 0;
    };

    /// @brief キュープールの共通インターフェース
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

        /// @brief キューコンテキストをプールから取得する
        virtual Result get_queue_context(CommandListType type, queueContextLease& outContext) = 0;
        /// @brief キューコンテキストをプールへ返却し
        virtual Result return_queue_context(queueContextLease& context) = 0;
        /// @brief graphics キューへ待機させ
        virtual Result wait_for_graphics_queue() = 0;
        /// @brief present キューの取得
        virtual queueContextPtr get_present_queue_context() = 0;
    };

    enum class ColorFormat : uint8_t
    {
        R8G8B8A8_UNORM,
        R8G8B8A8_UNORM_SRGB,
        BC6H_UF16,
        BC7_UNORM,
        BC7_UNORM_SRGB,
        R32_UINT,
        D24_UNorm_S8_UInt,
        R24_UNorm_X8_Typeless
    };

    inline const char* color_format_to_string(ColorFormat format) noexcept
    {
        switch (format)
        {
        case ColorFormat::R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case ColorFormat::R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
        case ColorFormat::BC6H_UF16: return "BC6H_UF16";
        case ColorFormat::BC7_UNORM: return "BC7_UNORM";
        case ColorFormat::BC7_UNORM_SRGB: return "BC7_UNORM_SRGB";
        case ColorFormat::R32_UINT: return "R32_UINT";
        case ColorFormat::D24_UNorm_S8_UInt: return "D24_UNorm_S8_UInt";
        case ColorFormat::R24_UNorm_X8_Typeless:
            return "R24_UNorm_X8_Typeless";
        default: return "Unknown";
        }
    }

    inline uint32_t color_format_byte_size(ColorFormat format) noexcept
    {
        switch (format)
        {
        case ColorFormat::R8G8B8A8_UNORM:
        case ColorFormat::R8G8B8A8_UNORM_SRGB:
        case ColorFormat::R32_UINT:
            return 4;
        case ColorFormat::BC6H_UF16:
        case ColorFormat::BC7_UNORM:
        case ColorFormat::BC7_UNORM_SRGB:
            return 0;
        default:
            return 0;
        }
    }

    enum class BufferKind : uint8_t
    {
        Texture,
        Buffer,
    };

    enum class BufferType : uint8_t
    {
        Vertex,
        Index,
        Constant,
        Structured,
        UnorderedAccess,
        Raw,
        Readback,
        Unknown,
    };

    inline const char* buffer_type_to_string(BufferType type) noexcept
    {
        switch (type)
        {
        case BufferType::Vertex: return "Vertex";
        case BufferType::Index: return "Index";
        case BufferType::Constant: return "Constant";
        case BufferType::Structured: return "Structured";
        case BufferType::UnorderedAccess: return "UnorderedAccess";
        case BufferType::Raw: return "Raw";
        case BufferType::Readback: return "Readback";
        case BufferType::Unknown: return "Unknown";
        default: return "Unknown";
        }
    }

    enum class ViewType : uint8_t
    {
        ConstantBuffer,
        ShaderResourceBuffer,
        ShaderResourceRawBuffer,
        UnorderedAccessBuffer,
        UnorderedAccessRawBuffer,
        ShaderResourceTexture2D,
        ShaderResourceTextureCube,
        UnorderedAccessTexture2D,
        RenderTarget,
        DepthStencil,
    };

    enum class IndexFormat : uint8_t
    {
        UInt16,
        UInt32,
    };

    inline const char* index_format_to_string(IndexFormat format) noexcept
    {
        switch (format)
        {
        case IndexFormat::UInt16: return "UInt16";
        case IndexFormat::UInt32: return "UInt32";
        default: return "Unknown";
        }
    }
}
