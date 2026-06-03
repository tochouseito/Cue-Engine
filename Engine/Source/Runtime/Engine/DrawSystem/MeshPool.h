#pragma once

/// ****************************************************************************
/// MeshPool.h
/// ****************************************************************************

// === RHI includes ===
#include <RHICommon.h>
#include <BufferManager.h>
#include <ViewManager.h>

// === Math includes ===
#include <CueMath.h>

// === Core includes ===
#include <Native/EngineNativeStruct.h>

namespace Cue::DrawSystem
{
    using RHI::BufferHandle;
    using RHI::MeshHandle;
    using RHI::ViewHandle;
    using RHI::BufferCopyRegion;
    using RHI::BufferDesc;
    using RHI::BufferHandle;
    using RHI::BufferKind;
    using RHI::BufferType;
    using RHI::CommandListType;
    using RHI::ICommandContext;
    using RHI::CommandListType;
    using RHI::ICommandContext;
    using RHI::ResourceBarrierDesc;
    using RHI::ResourceState;
    using RHI::MeshHandle;
    using RHI::MeshTag;
    using RHI::UploadBufferView;
    using RHI::ViewDesc;
    using RHI::ViewHandle;
    using RHI::ViewType;
    using RHI::commandContextLease;
    using RHI::queueContextLease;

    /// @brief MeshPool の初期化パラメータ
    struct MeshPoolDesc final
    {
        uint32_t maxVertexCount = 4u * 1024u * 1024u; // プール内の最大頂点数
        uint32_t maxIndexCount = 4u * 1024u * 1024u; // プール内の最大インデックス数
        uint32_t maxMeshCount = 4u * 1024u; // プール内の最大メッシュ数
        uint32_t maxMeshletCount = 256u * 1024u; // プール内の最大 meshlet 数
        uint32_t positionStagingSize = 1u * 1024u * 1024u; // Position stream 用の常設 staging サイズ
        uint32_t uvStagingSize = 512u * 1024u; // UV stream 用の常設 staging サイズ
        uint32_t normalStagingSize = 1u * 1024u * 1024u; // Normal stream 用の常設 staging サイズ
        uint32_t indexStagingSize = 1u * 1024u * 1024u; // Index stream 用の常設 staging サイズ
        uint32_t meshletStagingSize = 512u * 1024u; // Meshlet metadata 用の常設 staging サイズ
        uint32_t meshRangeStagingCount = 256u; // MeshRange 用の常設 staging 要素数
        std::string_view positionName = "MeshPool.Position";
        std::string_view uvName = "MeshPool.Uv";
        std::string_view normalName = "MeshPool.Normal";
        std::string_view indexName = "MeshPool.Index";
        std::string_view meshletName = "MeshPool.Meshlet";
        std::string_view meshRangeName = "MeshPool.MeshRange";
        std::string_view meshRangeSrvName = "MeshPool.MeshRangeSRV";
    };

    /// @brief メッシュの描画範囲を表す構造体
    struct MeshRange final
    {
        uint32_t indexCount = 0;
        uint32_t startIndex = 0;
        int32_t baseVertex = 0;
        uint32_t meshletOffset = 0;
        uint32_t meshletCount = 0;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        uint32_t padding2 = 0;
    };

    struct MeshletGpu final
    {
        uint32_t startIndex = 0;
        uint32_t indexCount = 0;
        int32_t baseVertex = 0;
        uint32_t padding = 0;
        Math::float4 boundsCenterRadius = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
        Math::float4 coneApex = Math::float4(0.0f, 0.0f, 0.0f, 1.0f);
        Math::float4 coneAxisCutoff = Math::float4(0.0f, 0.0f, 0.0f, 0.0f);
    };

    /// @brief メッシュのバウンディング情報を表す構造体
    struct MeshBounds final
    {
        Math::float3 center = Math::float3::zero();
        float radius = 0.0f;
    };

    /// @brief MeshPool が管理する GPU リソースのハンドルをまとめた構造体
    struct MeshPoolBindings final
    {
        BufferHandle positionBuffer = {};
        BufferHandle uvBuffer = {};
        BufferHandle normalBuffer = {};
        BufferHandle indexBuffer = {};
        BufferHandle meshletBuffer = {};
        BufferHandle meshRangeBuffer = {};
        ViewHandle meshRangeSrv = {};
    };

    struct MeshRecord final
    {
        Core::ResourceNameId nameId = 0;
        uint32_t meshId = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint64_t positionByteOffset = 0;
        uint64_t positionByteSize = 0;
        uint64_t uvByteOffset = 0;
        uint64_t uvByteSize = 0;
        uint64_t normalByteOffset = 0;
        uint64_t normalByteSize = 0;
        uint64_t indexByteOffset = 0;
        uint64_t indexByteSize = 0;
        uint64_t meshletByteOffset = 0;
        uint64_t meshletByteSize = 0;
        uint32_t meshletCount = 0;
        MeshBounds bounds{};
        bool hasSkinInfluence = false;
    };

    struct FreeRange final
    {
        uint64_t byteOffset = 0;
        uint64_t byteSize = 0;
    };

    struct StreamState final
    {
        std::string debugName{};
        BufferType bufferType = BufferType::Unknown;
        BufferHandle defaultBufferHandle = {};
        BufferHandle stagingBufferHandle = {};
        std::vector<FreeRange> freeRanges{};
        Core::RingBuffer stagingRing{};
        std::byte* mappedStagingData = nullptr;
        uint64_t capacityInBytes = 0;
        uint64_t stagingCapacityInBytes = 0;
        uint32_t alignment = 1;
    };

    struct MeshRangeState final
    {
        std::string debugName{};
        BufferHandle defaultBufferHandle = {};
        BufferHandle stagingBufferHandle = {};
        ViewHandle srvHandle = {};
        Core::RingBuffer stagingRing{};
        std::byte* mappedStagingData = nullptr;
        uint64_t stagingCapacityInBytes = 0;
        uint32_t capacity = 0;
        std::vector<uint32_t> freeMeshIds{};
    };

    struct UploadAllocation final
    {
        BufferHandle bufferHandle = {};
        std::byte* mappedData = nullptr;
        uint64_t byteOffset = 0;
        uint64_t byteSize = 0;
        Core::RingBuffer::Allocation ringAllocation{};
        bool isTransient = false;

        [[nodiscard]] bool valid() const noexcept
        {
            return bufferHandle.valid() && mappedData != nullptr && byteSize > 0;
        }
    };

    class MeshPool
    {
    public:
        MeshPool(
            const MeshPoolDesc& desc,
            RHI::IBufferManager& bufferManager,
            RHI::IViewManager& viewManager,
            RHI::ICommandPool& commandPool,
            RHI::IQueuePool& queuePool);
        // コピー禁止
        MeshPool(const MeshPool&) = delete;
        MeshPool& operator=(const MeshPool&) = delete;
        // ムーブは許可
        MeshPool(MeshPool&&) = default;
        MeshPool& operator=(MeshPool&&) = default;
        ~MeshPool();

        // --- Mesh の割り当てと解放 ---
        Result allocate_mesh(const Core::Native::MeshData& meshData, MeshHandle& outHandle);
        Result free_mesh(MeshHandle handle);
        Result get_mesh_id(MeshHandle handle, uint32_t& outMeshId) const;
        Result get_mesh_range(uint32_t meshId, MeshRange& outMeshRange) const;
        Result get_mesh_bounds(uint32_t meshId, MeshBounds& outBounds) const;
        Result get_bindings(MeshPoolBindings& outBindings) const;
    private:
        Result initialize_streams(const MeshPoolDesc& desc);
        Result initialize_mesh_range_state(const MeshPoolDesc& desc);
        Result create_stream_state(
            std::string_view bufferName,
            BufferType bufferType,
            uint64_t totalBytes,
            uint64_t stagingBytes,
            uint32_t stride,
            uint32_t elementCount,
            uint32_t alignment,
            StreamState& outStreamState);
        Result create_upload_buffer(
            std::string_view bufferName,
            BufferType bufferType,
            uint64_t byteSize,
            BufferHandle& outBufferHandle,
            std::byte*& outMappedData);
        Result allocate_stream_range(
            StreamState& streamState,
            uint64_t byteSize,
            uint32_t alignment,
            uint64_t& outOffset);
        void release_stream_range(
            StreamState& streamState,
            uint64_t byteOffset,
            uint64_t byteSize);
        Result allocate_upload_range(
            StreamState& streamState,
            uint64_t byteSize,
            uint32_t alignment,
            UploadAllocation& outAllocation);
        Result allocate_upload_range(
            MeshRangeState& meshRangeState,
            uint64_t byteSize,
            uint32_t alignment,
            UploadAllocation& outAllocation);
        void release_upload_range(
            StreamState& streamState,
            UploadAllocation& allocation);
        void release_upload_range(
            MeshRangeState& meshRangeState,
            UploadAllocation& allocation);
        void write_upload_bytes(
            const UploadAllocation& allocation,
            const void* sourceData,
            uint64_t byteSize);
        Result copy_upload_regions(const std::vector<BufferCopyRegion>& regions);
        void destroy_stream_state(StreamState& streamState);
        Result allocate_mesh_id(uint32_t& outMeshId);
        void release_mesh_id(uint32_t meshId);
        void write_mesh_range(uint32_t meshId, const MeshRange& meshRange);
        Result upload_mesh_range(uint32_t meshId, const MeshRange& meshRange);
    private:
        RHI::IBufferManager& m_bufferManager;
        RHI::IViewManager& m_viewManager;
        RHI::ICommandPool& m_commandPool;
        RHI::IQueuePool& m_queuePool;
        Core::Registry<RHI::MeshTag, MeshRecord> m_meshRegistry;
        std::unordered_map<Core::ResourceNameId, MeshHandle> m_nameToHandlesMap;
        std::unordered_map<uint32_t, MeshHandle> m_meshIdToHandlesMap;
        StreamState m_positionStream{};
        StreamState m_uvStream{};
        StreamState m_normalStream{};
        StreamState m_influenceStream{};
        StreamState m_indexStream{};
        StreamState m_meshletStream{};
        MeshRangeState m_meshRangeState{};
        Result m_initResult = Result::ok();
    };
}
