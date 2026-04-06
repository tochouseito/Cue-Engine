#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    struct StaticMeshPoolDesc final
    {
        uint32_t maxVertexCount = 256u * 1024u; // プール内の最大頂点数
        uint32_t maxIndexCount = 768u * 1024u; // プール内の最大インデックス数
        uint32_t maxMeshCount = 4u * 1024u; // プール内の最大メッシュ数
        std::string_view positionBufferName = "StaticMeshPool.Position";
        std::string_view uvBufferName = "StaticMeshPool.Uv";
        std::string_view normalBufferName = "StaticMeshPool.Normal";
        std::string_view indexBufferName = "StaticMeshPool.Index";
        std::string_view meshRangeBufferName = "StaticMeshPool.MeshRange";
        std::string_view meshRangeViewName = "StaticMeshPool.MeshRangeSRV";
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
        virtual Result get_bindings(StaticMeshPoolBindings& outBindings) const = 0;
    };
}
