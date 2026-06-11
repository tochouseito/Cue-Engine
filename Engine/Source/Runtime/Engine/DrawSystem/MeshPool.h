#pragma once

/// ****************************************************************************
/// メッシュ統合プール
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
        uint32_t positionStagingSize = 1u * 1024u * 1024u; // Position stream 用の常設 staging サイズ
        uint32_t uvStagingSize = 512u * 1024u; // UV stream 用の常設 staging サイズ
        uint32_t normalStagingSize = 1u * 1024u * 1024u; // Normal stream 用の常設 staging サイズ
        uint32_t indexStagingSize = 1u * 1024u * 1024u; // Index stream 用の常設 staging サイズ
        uint32_t meshRangeStagingCount = 256u; // MeshRange 用の常設 staging 要素数
        std::string_view positionName = "MeshPool.Position";
        std::string_view uvName = "MeshPool.Uv";
        std::string_view normalName = "MeshPool.Normal";
        std::string_view indexName = "MeshPool.Index";
        std::string_view meshRangeName = "MeshPool.MeshRange";
        std::string_view meshRangeSrvName = "MeshPool.MeshRangeSRV";
    };
}
