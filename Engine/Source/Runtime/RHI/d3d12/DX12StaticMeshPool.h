#pragma once

// === RHI Includes ===
#include <StaticMeshPool.h>

// === C++ includes ===
#include <vector>
#include <unordered_map>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "DX12BufferManager.h"

namespace Cue::RHI::DX12
{
    struct StaticMeshRecord final
    {
    };

    class DX12StaticMeshPool final : public IStaticMeshPool
    {
    public:
        DX12StaticMeshPool(const StaticMeshPoolDesc& desc, DX12BufferManager& bufferManager);
        ~DX12StaticMeshPool() override = default;
        // --- Mesh の割り当てと解放 ---
        Result allocate_mesh(const Core::Native::MeshData& meshData, StaticMeshHandle& outHandle) override;
        Result free_mesh(StaticMeshHandle handle) override;
    private:
        DX12BufferManager& m_bufferManager; // バッファマネージャへの参照
        Core::Registry<StaticMeshTag, StaticMeshRecord> m_meshRegistry; // メッシュレコードのレジストリ
        std::unordered_map<Core::ResourceNameId, StaticMeshHandle> m_nameToHandlesMap; // 名前からハンドルへのマッピング
    };
}
