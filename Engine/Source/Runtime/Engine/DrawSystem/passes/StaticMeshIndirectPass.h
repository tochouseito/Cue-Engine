#pragma once

/// ************************************************************************************
/// StaticMesh の indirect forward 描画パス
/// ************************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/DrawResources.h"
#include "DrawSystem/MeshPool.h"

namespace Cue::DrawSystem
{
    /// @brief MeshPool と CPU batching 結果を使って StaticMesh を indirect draw する
    class StaticMeshIndirectPass final : public RHI::FrameGraphPass
    {
    public:
        StaticMeshIndirectPass(
            DrawResources& a_drawResources,
            MeshPool& a_meshPool,
            DrawFrameState& a_drawFrameState);
        ~StaticMeshIndirectPass() override;

        StaticMeshIndirectPass(const StaticMeshIndirectPass&) = delete;
        StaticMeshIndirectPass& operator=(const StaticMeshIndirectPass&) = delete;
        StaticMeshIndirectPass(StaticMeshIndirectPass&&) = delete;
        StaticMeshIndirectPass& operator=(StaticMeshIndirectPass&&) = delete;

        /// @brief FrameGraph 上の pass 名
        [[nodiscard]] const char* name() const noexcept override;

        /// @brief StaticMesh 描画は graphics queue で実行する
        [[nodiscard]] RHI::CommandListType type() const noexcept override;

        /// @brief shader、pipeline、render target 参照を構築する
        [[nodiscard]] Result setup(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief MeshPool と FinalColor の使用状態を宣言する
        [[nodiscard]] Result describe_resources(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief フレームごとの indirect command を使って StaticMesh を描画する
        void execute(RHI::FrameGraphContext& a_context) override;

    private:
        DrawResources& m_drawResources; // DrawSystem 共通 buffer と batch upload 結果
        MeshPool& m_meshPool;           // vertex/index stream と MeshRange buffer の管理元
        DrawFrameState& m_drawFrameState; // フレームごとの CPU batching 結果

        RHI::TextureHandle m_finalColorHandle{};     // StaticMesh の描画先 color texture
        RHI::ViewHandle m_finalColorRtvHandle{};     // StaticMesh の描画先 RTV
        MeshPoolBindings m_meshPoolBindings{};       // MeshPool が保持する vertex/index buffer 群
        RHI::RootSignatureHandle m_rootSignature{};  // StaticMesh indirect 用 root signature
        RHI::ShaderBlobHandle m_vertexShader{};      // StaticMesh indirect vertex shader
        RHI::ShaderBlobHandle m_pixelShader{};       // StaticMesh indirect pixel shader
        RHI::PipelineStateHandle m_pipelineState{};  // StaticMesh indirect pipeline
    };
} // namespace Cue::DrawSystem
