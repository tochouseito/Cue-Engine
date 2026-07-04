#pragma once

/// ************************************************************************************
/// Editor / Effect viewer 用の床グリッド描画パス
/// ************************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === Engine includes ===
#include "DrawSystem/DrawResources.h"

// === C++ includes ===
#include <cstdint>
#include <string>

namespace Cue::DrawSystem
{
    class GridPass final : public RHI::FrameGraphPass
    {
    public:
        explicit GridPass(DrawResources& a_drawResources);
        GridPass(DrawResources& a_drawResources, std::string a_passName, std::string a_renderTargetName);
        ~GridPass() override;

        GridPass(const GridPass&) = delete;
        GridPass& operator=(const GridPass&) = delete;
        GridPass(GridPass&&) = delete;
        GridPass& operator=(GridPass&&) = delete;

        [[nodiscard]] const char* name() const noexcept override;
        [[nodiscard]] RHI::CommandListType type() const noexcept override;
        [[nodiscard]] Result setup(RHI::FrameGraphBuilder& a_builder) override;
        [[nodiscard]] Result describe_resources(RHI::FrameGraphBuilder& a_builder) override;
        void execute(RHI::FrameGraphContext& a_context) override;

    private:
        DrawResources& m_drawResources;
        std::string m_passName{};
        std::string m_renderTargetName{};
        std::string m_renderTargetRtvName{};
        RHI::TextureHandle m_renderTargetHandle{};
        RHI::ViewHandle m_renderTargetRtvHandle{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_vertexShader{};
        RHI::ShaderBlobHandle m_pixelShader{};
        RHI::PipelineStateHandle m_pipelineState{};

        static constexpr uint32_t k_gridLineCount = 82;
    };
} // namespace Cue::DrawSystem
