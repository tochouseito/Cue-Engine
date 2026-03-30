#pragma once

// === RHI includes ===
#include "RHICommon.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ViewManager.h"
#include "PipelineManager.h"
#include "StaticMeshPool.h"

namespace Cue::RHI
{
    class FrameGraph;

    class FrameGraphBuilder final
    {
    public:

    private:

    };

    class FrameGraphContext final
    {
    public:

    };

    class FrameGraphPass
    {
    public:

    };

    /// @brief フレーム単位の描画依存関係を保持するプレースホルダーです。
    class FrameGraph final
    {
    public:
        FrameGraph(IBufferManager* a_bufferManager, ITextureManager* a_textureManager, IViewManager* a_viewManager, IPipelineManager* a_pipelineManager, IStaticMeshPool* a_staticMeshPool)
            : m_bufferManager(a_bufferManager)
            , m_textureManager(a_textureManager)
            , m_viewManager(a_viewManager)
            , m_pipelineManager(a_pipelineManager)
            , m_staticMeshPool(a_staticMeshPool)
        {
        }
        // コピー禁止
        FrameGraph(const FrameGraph&) = delete;
        FrameGraph& operator=(const FrameGraph&) = delete;
        // ムーブ禁止
        FrameGraph(FrameGraph&&) = delete;
        FrameGraph& operator=(FrameGraph&&) = delete;
        ~FrameGraph() = default;

        /// @brief パスの追加

        /// @brief 描画依存関係を構築します。
        Result build();

        /// @brief パスの実行
        Result execute();
    private:
        IBufferManager* m_bufferManager = nullptr;
        ITextureManager* m_textureManager = nullptr;
        IViewManager* m_viewManager = nullptr;
        IPipelineManager* m_pipelineManager = nullptr;
        IStaticMeshPool* m_staticMeshPool = nullptr;
    };
}
