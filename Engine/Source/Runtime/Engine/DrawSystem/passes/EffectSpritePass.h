#pragma once

/// ************************************************************************************
/// Effect sprite particle の billboard 描画パス
/// ************************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/DrawResources.h"

// === C++ includes ===
#include <string>

namespace Cue::DrawSystem
{
    class EffectSpritePass final : public RHI::FrameGraphPass
    {
    public:
        EffectSpritePass(DrawResources& a_drawResources, DrawFrameState& a_drawFrameState);
        EffectSpritePass(DrawResources& a_drawResources,
                         DrawFrameState& a_drawFrameState,
                         std::string a_passName,
                         std::string a_renderTargetName);
        ~EffectSpritePass() override;

        EffectSpritePass(const EffectSpritePass&) = delete;
        EffectSpritePass& operator=(const EffectSpritePass&) = delete;
        EffectSpritePass(EffectSpritePass&&) = delete;
        EffectSpritePass& operator=(EffectSpritePass&&) = delete;

        [[nodiscard]] const char* name() const noexcept override;
        [[nodiscard]] RHI::CommandListType type() const noexcept override;
        [[nodiscard]] Result setup(RHI::FrameGraphBuilder& a_builder) override;
        [[nodiscard]] Result describe_resources(RHI::FrameGraphBuilder& a_builder) override;
        void execute(RHI::FrameGraphContext& a_context) override;

    private:
        DrawResources& m_drawResources;
        DrawFrameState& m_drawFrameState;
        std::string m_passName{};
        std::string m_renderTargetName{};
        std::string m_renderTargetRtvName{};
        RHI::TextureHandle m_renderTargetHandle{};
        RHI::ViewHandle m_renderTargetRtvHandle{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_vertexShader{};
        RHI::ShaderBlobHandle m_pixelShader{};
        RHI::PipelineStateHandle m_pipelineState{};
    };
} // namespace Cue::DrawSystem
