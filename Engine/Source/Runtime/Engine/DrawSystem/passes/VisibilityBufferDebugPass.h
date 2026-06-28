#pragma once

#include <FrameGraph.h>

#include "DrawSystem/RenderDebugView.h"

namespace Cue::DrawSystem {

class VisibilityBufferDebugPass final : public RHI::FrameGraphPass {
public:
  explicit VisibilityBufferDebugPass(const RenderDebugView &debugView)
      : m_debugView(debugView) {}

  const char *name() const noexcept override { return "VisibilityBufferDebug"; }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    (void)a_frameIndex;
    return m_debugView == RenderDebugView::VisibilityObjectId ||
           m_debugView == RenderDebugView::VisibilityPrimitiveId;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_texture("FinalColor", m_color);
    if (!result) {
      return result;
    }
    result = builder.render(&m_color, 1);
    if (!result) {
      return result;
    }
    result = builder.get_view("FinalColorRTV", m_colorRtv);
    if (!result) {
      return result;
    }

    result = builder.get_texture("VisibilityBuffer", m_visibility);
    if (!result) {
      return result;
    }
    result = builder.get_view("VisibilityBufferSRV", m_visibilitySrv);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "VisibilityBufferDebugRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::DescriptorTableSRV,
         RHI::ShaderVisibility::Pixel, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         0});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc vsDesc{};
    vsDesc.name = "VisibilityBufferDebugVS";
    vsDesc.filePath = "Shaders/D3D12/VisibilityBufferDebug.hlsl";
    vsDesc.entryPoint = "vs_main";
    vsDesc.targetProfile = "vs_6_0";
    result = builder.create_shader_blob(vsDesc, m_vertexShader);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc psDesc{};
    psDesc.name = "VisibilityBufferDebugPS";
    psDesc.filePath = "Shaders/D3D12/VisibilityBufferDebug.hlsl";
    psDesc.entryPoint = "ps_main";
    psDesc.targetProfile = "ps_6_0";
    result = builder.create_shader_blob(psDesc, m_pixelShader);
    if (!result) {
      return result;
    }

    RHI::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "VisibilityBufferDebugPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.vsHandle = m_vertexShader;
    pipelineDesc.psHandle = m_pixelShader;
    pipelineDesc.inputElements = {};
    pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
    pipelineDesc.depthStencilState.depthEnable = false;
    pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::Zero;
    pipelineDesc.blendMode = {RHI::BlendMode::None};
    pipelineDesc.rtvFormats = {RHI::ColorFormat::R8G8B8A8_UNORM};
    return builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_texture(
        m_visibility, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_texture(m_color, RHI::ResourceAccessType::Write,
                               RHI::ResourceState::RenderTarget,
                               RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->set_render_targets(&m_colorRtv, 1, {});
    commandContext->set_viewport_scissor(context.width(), context.height());
    commandContext->set_graphics_pipeline(m_pipeline);
    commandContext->set_primitive_topology(
        RHI::PrimitiveTopologyType::Triangle);
    commandContext->set_graphics_descriptor_table(0, m_visibilitySrv);
    commandContext->set_32bit_constant(1, static_cast<uint32_t>(m_debugView));
    commandContext->draw_instanced(3, 1, 0, 0);
  }

private:
  const RenderDebugView &m_debugView;
  RHI::TextureHandle m_color{};
  RHI::TextureHandle m_visibility{};
  RHI::ViewHandle m_colorRtv{};
  RHI::ViewHandle m_visibilitySrv{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_vertexShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

} // namespace Cue::DrawSystem
