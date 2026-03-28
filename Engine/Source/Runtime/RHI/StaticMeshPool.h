#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    struct StaticMeshPoolDesc final
    {
        uint32_t maxVertexCount = 16u * 1024u * 1024u; // プール内の最大頂点数
        std::string_view positionBufferName = "StaticMeshPool.Position";
        std::string_view uvBufferName = "StaticMeshPool.Uv";
        std::string_view normalBufferName = "StaticMeshPool.Normal";
        std::string_view indexBufferName = "StaticMeshPool.Index";
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
    };
}
