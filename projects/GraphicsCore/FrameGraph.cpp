#include "FrameGraph.h"

namespace Cue::GraphicsCore
{
    FrameGraph::FrameGraph()
    {
    }
    FrameGraph::~FrameGraph()
    {
    }
    void FrameGraph::add_pass(FrameGraphPass& pass)
    {
        // 1) パス情報を記録して後続の依存解決に使う
        if (m_buildCacheValid && !m_forceRebuild)
        {
            return;
        }
        PassNode node{};
        node.m_pass = &pass;
        node.m_type = pass.type();
        m_passes.push_back(std::move(node));
    }
    Result FrameGraph::build()
    {
        // 1) パスの setup を順に実行して宣言を集める
        // 2) 必要に応じて依存解決と実行順を確定する
        // 3) 毎フレーム外部リソース差し替えを行う
        if (!m_buildCacheValid || m_forceRebuild)
        {
            for (auto& pass : m_passes)
            {
                pass.m_accesses.clear();
                pass.m_dependencies.clear();
            }

            for (uint32_t i = 0; i < m_passes.size(); ++i)
            {
                FrameGraphBuilder builder(i);
                m_passes[i].m_pass->setup(builder);
            }

            Result result = build_dependencies();
            if (!result)
            {
                m_buildCacheValid = false;
                return result;
            }
            result = build_execution_order();
            if (!result)
            {
                m_buildCacheValid = false;
                return result;
            }

            m_buildCacheValid = true;
            m_forceRebuild = false;
        }

        for (uint32_t i = 0; i < m_passes.size(); ++i)
        {
            FrameGraphBuilder builder(i);
        }

        return Result::ok();
    }
    Result FrameGraph::execute()
    {
        return Result();
    }
    Result FrameGraph::build_dependencies()
    {
        return Result();
    }
    Result FrameGraph::build_execution_order()
    {
        return Result();
    }
}
