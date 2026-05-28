// StaticMeshPoolTypes の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include <RHICommon.h>

// === Core includes ===
#include <Native/EngineNativeStruct.h>

namespace Cue::DrawSystem
{
    using RHI::BufferHandle;
    using RHI::StaticMeshHandle;
    using RHI::ViewHandle;

    struct StaticMeshPoolDesc final
    {
        uint32_t maxVertexCount = 256u * 1024u; // プール内の最大頂点数
        uint32_t maxIndexCount = 768u * 1024u; // プール内の最大インデックス数
        uint32_t maxMeshCount = 4u * 1024u; // プール内の最大メッシュ数
        uint32_t positionStagingSize = 1u * 1024u * 1024u; // Position stream 用の常設 staging サイズ
        uint32_t uvStagingSize = 512u * 1024u; // UV stream 用の常設 staging サイズ
        uint32_t normalStagingSize = 1u * 1024u * 1024u; // Normal stream 用の常設 staging サイズ
        uint32_t influenceStagingSize = 1u * 1024u * 1024u; // Skin influence stream 用の常設 staging サイズ
        uint32_t indexStagingSize = 1u * 1024u * 1024u; // Index stream 用の常設 staging サイズ
        uint32_t meshRangeStagingCount = 256u; // MeshRange 用の常設 staging 要素数
        std::string_view positionName = "StaticMeshPool.Position";
        std::string_view uvName = "StaticMeshPool.Uv";
        std::string_view normalName = "StaticMeshPool.Normal";
        std::string_view influenceName = "StaticMeshPool.SkinInfluence";
        std::string_view indexName = "StaticMeshPool.Index";
        std::string_view meshRangeName = "StaticMeshPool.MeshRange";
        std::string_view meshRangeSrvName = "StaticMeshPool.MeshRangeSRV";
    };

    struct StaticMeshRange final
    {
        uint32_t indexCount = 0;
        uint32_t startIndex = 0;
        int32_t baseVertex = 0;
        uint32_t padding = 0;
    };

    struct StaticMeshPoolBindings final
    {
        BufferHandle positionBuffer = {};
        BufferHandle uvBuffer = {};
        BufferHandle normalBuffer = {};
        BufferHandle influenceBuffer = {};
        BufferHandle indexBuffer = {};
        BufferHandle meshRangeBuffer = {};
        ViewHandle meshRangeSrv = {};
    };

    class IStaticMeshPool
    {
    public:
        IStaticMeshPool() = default;
        // コピー禁止
        IStaticMeshPool(const IStaticMeshPool&) = delete;
        IStaticMeshPool& operator=(const IStaticMeshPool&) = delete;
        // ムーブは許可
        IStaticMeshPool(IStaticMeshPool&&) = default;
        IStaticMeshPool& operator=(IStaticMeshPool&&) = default;
        virtual ~IStaticMeshPool() = default;

        // --- Mesh の割り当てと解放 ---
        virtual Result allocate_mesh(const Core::Native::MeshData& meshData, StaticMeshHandle& outHandle) = 0;
        virtual Result free_mesh(StaticMeshHandle handle) = 0;
        virtual Result get_mesh_id(StaticMeshHandle handle, uint32_t& outMeshId) const = 0;
        virtual Result get_mesh_range(uint32_t meshId, StaticMeshRange& outMeshRange) const = 0;
        virtual Result has_skin_influence(uint32_t meshId, bool& outHasSkinInfluence) const = 0;
        virtual Result get_bindings(StaticMeshPoolBindings& outBindings) const = 0;
    };
}
