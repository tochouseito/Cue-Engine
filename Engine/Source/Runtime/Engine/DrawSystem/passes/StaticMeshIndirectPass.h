#pragma once

/// ************************************************************************************
/// StaticMesh の indirect forward 描画パス
/// ************************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/DrawSceneResources.h"
#include "DrawSystem/DrawViewResources.h"
#include "DrawSystem/DrawVisibilityResources.h"
#include "DrawSystem/MeshPool.h"

// === C++ includes ===
#include <string>

namespace Cue::DrawSystem
{
    /// @brief MeshPool と CPU batching 結果を使って StaticMesh を indirect draw する
    class StaticMeshIndirectPass final : public RHI::FrameGraphPass
    {
    public:
        StaticMeshIndirectPass(
            DrawSceneResources& a_sceneResources,
            DrawViewResources& a_viewResources,
            DrawVisibilityResources& a_visibilityResources,
            MeshPool& a_meshPool,
            DrawFrameState& a_drawFrameState);
        StaticMeshIndirectPass(
            DrawSceneResources& a_sceneResources,
            DrawViewResources& a_viewResources,
            DrawVisibilityResources& a_visibilityResources,
            MeshPool& a_meshPool,
            DrawFrameState& a_drawFrameState,
            std::string a_passName,
            std::string a_renderTargetName,
            std::string a_depthStencilName);
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

        /// @brief MeshPool と描画先 color の使用状態を宣言する
        [[nodiscard]] Result describe_resources(RHI::FrameGraphBuilder& a_builder) override;

        /// @brief フレームごとの indirect command を使って StaticMesh を描画する
        void execute(RHI::FrameGraphContext& a_context) override;

    private:
        DrawSceneResources& m_sceneResources; // 全 View で共有する DrawScene 入力
        DrawViewResources& m_viewResources; // camera 行列を持つ描画 View
        DrawVisibilityResources& m_visibilityResources; // culling と indirect draw の View 固有出力
        MeshPool& m_meshPool;           // vertex/index stream と MeshRange buffer の管理元
        DrawFrameState& m_drawFrameState; // フレームごとの CPU batching 結果
        std::string m_passName{}; // FrameGraph 上の pass 名
        std::string m_renderTargetName{}; // StaticMesh の描画先 texture 名
        std::string m_renderTargetRtvName{}; // StaticMesh の描画先 RTV 名
        std::string m_depthStencilName{}; // StaticMesh の深度 texture 名
        std::string m_depthStencilDsvName{}; // StaticMesh の深度 DSV 名

        RHI::TextureHandle m_renderTargetHandle{};   // StaticMesh の描画先 color texture
        RHI::ViewHandle m_renderTargetRtvHandle{};   // StaticMesh の描画先 RTV
        RHI::TextureHandle m_depthStencilHandle{};   // StaticMesh の描画先 depth texture
        RHI::ViewHandle m_depthStencilDsvHandle{};   // StaticMesh の描画先 DSV
        MeshPoolBindings m_meshPoolBindings{};       // MeshPool が保持する vertex/index buffer 群
        RHI::RootSignatureHandle m_rootSignature{};  // StaticMesh indirect 用 root signature
        RHI::ShaderBlobHandle m_vertexShader{};      // StaticMesh indirect vertex shader
        RHI::ShaderBlobHandle m_pixelShader{};       // StaticMesh indirect pixel shader
        RHI::PipelineStateHandle m_pipelineState{};  // StaticMesh indirect pipeline

        static constexpr Math::float4 k_clearColor = Math::float4::from_rgba8(63, 63, 63, 255);
    };
} // namespace Cue::DrawSystem
