#pragma once

#include <FrameGraph.h>

#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/RenderDebugView.h"
#include "DrawSystem/RenderPath.h"

namespace Cue::DrawSystem {

class VisibilityBufferPass final : public RHI::FrameGraphPass {
public:
  VisibilityBufferPass(const DrawFrameState &drawFrameState,
                       const RenderPath &renderPath,
                       const RenderDebugView &debugView,
                       RHI::BufferHandle renderObjectBuffer,
                       RHI::BufferHandle transformBuffer,
                       RHI::BufferHandle viewProjectionBuffer,
                       RHI::BufferHandle visibleObjectCountBuffer,
                       uint32_t maxIndirectCommandCount)
      : m_drawFrameState(drawFrameState), m_renderPath(renderPath),
        m_debugView(debugView), m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_maxIndirectCommandCount(maxIndirectCommandCount) {}

  const char *name() const noexcept override { return "VisibilityBuffer"; }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    (void)a_frameIndex;
    return m_renderPath == RenderPath::VisibilityBuffer ||
           m_debugView != RenderDebugView::Forward;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    RHI::TextureDesc visibilityDesc{};
    visibilityDesc.name = "VisibilityBuffer";
    visibilityDesc.kind = RHI::TextureKind::RenderTarget;
    visibilityDesc.width = builder.width();
    visibilityDesc.height = builder.height();
    visibilityDesc.format = RHI::ColorFormat::R32G32_UINT;
    visibilityDesc.clearColor[0] = 0.0f;
    visibilityDesc.clearColor[1] = 0.0f;
    visibilityDesc.clearColor[2] = 0.0f;
    visibilityDesc.clearColor[3] = 0.0f;
    Result result = builder.create_texture(visibilityDesc, m_visibility);
    if (!result) {
      return result;
    }

    result = builder.render(&m_visibility, 1);
    if (!result) {
      return result;
    }

    RHI::ViewDesc visibilityRtvDesc{};
    visibilityRtvDesc.name = "VisibilityBufferRTV";
    visibilityRtvDesc.type = RHI::ViewType::RenderTarget;
    visibilityRtvDesc.bufferKind = RHI::BufferKind::Texture;
    visibilityRtvDesc.textureHandle = m_visibility;
    visibilityRtvDesc.colorFormat = RHI::ColorFormat::R32G32_UINT;
    result = builder.create_view(visibilityRtvDesc, m_visibilityRtv);
    if (!result) {
      return result;
    }

    RHI::ViewDesc visibilitySrvDesc{};
    visibilitySrvDesc.name = "VisibilityBufferSRV";
    visibilitySrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
    visibilitySrvDesc.bufferKind = RHI::BufferKind::Texture;
    visibilitySrvDesc.textureHandle = m_visibility;
    visibilitySrvDesc.colorFormat = RHI::ColorFormat::R32G32_UINT;
    visibilitySrvDesc.mipLevels = 1;
    result = builder.create_view(visibilitySrvDesc, m_visibilitySrv);
    if (!result) {
      return result;
    }

    RHI::TextureDesc depthDesc{};
    depthDesc.name = "VisibilityDepth";
    depthDesc.kind = RHI::TextureKind::DepthStencil;
    depthDesc.width = builder.width();
    depthDesc.height = builder.height();
    depthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
    depthDesc.clearDepth = 1.0f;
    depthDesc.clearStencil = 0;
    result = builder.create_texture(depthDesc, m_depth);
    if (!result) {
      return result;
    }

    RHI::ViewDesc depthDsvDesc{};
    depthDsvDesc.name = "VisibilityDepthDSV";
    depthDsvDesc.type = RHI::ViewType::DepthStencil;
    depthDsvDesc.bufferKind = RHI::BufferKind::Texture;
    depthDsvDesc.textureHandle = m_depth;
    depthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
    result = builder.create_view(depthDsvDesc, m_depthDsv);
    if (!result) {
      return result;
    }

    result = builder.read_buffer(m_renderObjectBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_transformBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_viewProjectionBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_visibleObjectCountBuffer);
    if (!result) {
      return result;
    }

    result = builder.get_buffer("MeshPool.Position", m_positionBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.Index", m_indexBuffer);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("IndirectCommandBuffer", m_indirectCommandBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("IndirectCommandCountBuffer",
                                m_indirectCommandCountBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("RenderObjectIndexBuffer",
                                m_renderObjectIndexBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "VisibilityBufferRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 6});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc vsDesc{};
    vsDesc.name = "VisibilityBufferVS";
    vsDesc.filePath = "Shaders/D3D12/VisibilityBuffer.hlsl";
    vsDesc.entryPoint = "vs_main";
    vsDesc.targetProfile = "vs_6_0";
    result = builder.create_shader_blob(vsDesc, m_vertexShader);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc psDesc{};
    psDesc.name = "VisibilityBufferPS";
    psDesc.filePath = "Shaders/D3D12/VisibilityBuffer.hlsl";
    psDesc.entryPoint = "ps_main";
    psDesc.targetProfile = "ps_6_0";
    result = builder.create_shader_blob(psDesc, m_pixelShader);
    if (!result) {
      return result;
    }

    RHI::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "VisibilityBufferPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.vsHandle = m_vertexShader;
    pipelineDesc.psHandle = m_pixelShader;
    pipelineDesc.inputElements = {
        {"POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float, 0, 0},
    };
    pipelineDesc.rasterizerState.cullMode = RHI::CullMode::Back;
    pipelineDesc.depthStencilState.depthEnable = true;
    pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
    pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
    pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
    pipelineDesc.blendMode = {RHI::BlendMode::None};
    pipelineDesc.rtvFormats = {RHI::ColorFormat::R32G32_UINT};
    return builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_texture(
        m_visibility, RHI::ResourceAccessType::Write,
        RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_texture(m_depth, RHI::ResourceAccessType::Write,
                                 RHI::ResourceState::DepthWrite,
                                 RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_renderObjectBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_transformBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_viewProjectionBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_visibleObjectCountBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_positionBuffer, RHI::ResourceAccessType::Read,
                                RHI::ResourceState::VertexBuffer,
                                RHI::ResourceState::VertexBuffer);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_indexBuffer, RHI::ResourceAccessType::Read,
                                RHI::ResourceState::IndexBuffer,
                                RHI::ResourceState::IndexBuffer);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_indirectCommandBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_indirectCommandCountBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    return builder.use_buffer(
        m_renderObjectIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const float clearIds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    commandContext->clear_render_target(m_visibilityRtv, clearIds);
    commandContext->clear_depth_stencil(m_depthDsv, 1.0f, 0);
    commandContext->set_render_targets(&m_visibilityRtv, 1, m_depthDsv);
    commandContext->set_viewport_scissor(context.width(), context.height());
    commandContext->set_graphics_pipeline(m_pipeline);
    commandContext->set_primitive_topology(
        RHI::PrimitiveTopologyType::Triangle);
    commandContext->set_32bit_constant(0, 0xffffffffu);
    commandContext->set_cbv(1, m_viewProjectionBuffer);
    commandContext->set_srv(2, m_renderObjectBuffer);
    commandContext->set_srv(3, m_transformBuffer);
    commandContext->set_srv(4, m_renderObjectIndexBuffer);
    commandContext->set_vertex_buffer(0, m_positionBuffer);
    commandContext->set_index_buffer(m_indexBuffer, RHI::IndexFormat::UInt32);

    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (frameState.objectCount == 0) {
      return;
    }

    commandContext->execute_indexed_indirect(m_indirectCommandBuffer,
                                             m_indirectCommandCountBuffer,
                                             m_maxIndirectCommandCount);
  }

private:
  const DrawFrameState &m_drawFrameState;
  const RenderPath &m_renderPath;
  const RenderDebugView &m_debugView;
  RHI::TextureHandle m_visibility{};
  RHI::TextureHandle m_depth{};
  RHI::ViewHandle m_visibilityRtv{};
  RHI::ViewHandle m_visibilitySrv{};
  RHI::ViewHandle m_depthDsv{};
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  uint32_t m_maxIndirectCommandCount = 0;
  RHI::BufferHandle m_positionBuffer{};
  RHI::BufferHandle m_indexBuffer{};
  RHI::BufferHandle m_indirectCommandBuffer{};
  RHI::BufferHandle m_indirectCommandCountBuffer{};
  RHI::BufferHandle m_renderObjectIndexBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_vertexShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class VisibilityBufferRangePass final : public RHI::FrameGraphPass {
public:
  VisibilityBufferRangePass(const RenderPath &renderPath,
                            const RenderDebugView &debugView,
                            RHI::BufferHandle renderObjectBuffer,
                            RHI::BufferHandle transformBuffer,
                            RHI::BufferHandle viewProjectionBuffer,
                            uint32_t maxRangeCommandCount)
      : m_renderPath(renderPath), m_debugView(debugView),
        m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_maxRangeCommandCount(maxRangeCommandCount) {}

  const char *name() const noexcept override { return "VisibilityBufferRange"; }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    (void)a_frameIndex;
    return m_renderPath == RenderPath::VisibilityBuffer &&
           m_debugView == RenderDebugView::Forward;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_texture("VisibilityBuffer", m_visibility);
    if (!result) {
      return result;
    }
    result = builder.get_view("VisibilityBufferRTV", m_visibilityRtv);
    if (!result) {
      return result;
    }
    result = builder.get_texture("VisibilityDepth", m_depth);
    if (!result) {
      return result;
    }
    result = builder.get_view("VisibilityDepthDSV", m_depthDsv);
    if (!result) {
      return result;
    }

    result = builder.read_buffer(m_renderObjectBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_transformBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_viewProjectionBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.Position", m_positionBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.Index", m_indexBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("RenderObjectIndexBuffer",
                                m_renderObjectIndexBuffer);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("GroupRangeCommandBuffer", m_rangeCommandBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GroupRangeCommandCountBuffer",
                                m_rangeCommandCountBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "VisibilityBufferRangeRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 6});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc vsDesc{};
    vsDesc.name = "VisibilityBufferRangeVS";
    vsDesc.filePath = "Shaders/D3D12/VisibilityBuffer.hlsl";
    vsDesc.entryPoint = "range_vs_main";
    vsDesc.targetProfile = "vs_6_0";
    result = builder.create_shader_blob(vsDesc, m_vertexShader);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc psDesc{};
    psDesc.name = "VisibilityBufferRangePS";
    psDesc.filePath = "Shaders/D3D12/VisibilityBuffer.hlsl";
    psDesc.entryPoint = "ps_main";
    psDesc.targetProfile = "ps_6_0";
    result = builder.create_shader_blob(psDesc, m_pixelShader);
    if (!result) {
      return result;
    }

    RHI::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "VisibilityBufferRangePipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.vsHandle = m_vertexShader;
    pipelineDesc.psHandle = m_pixelShader;
    pipelineDesc.inputElements = {
        {"POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float, 0, 0},
    };
    pipelineDesc.rasterizerState.cullMode = RHI::CullMode::Back;
    pipelineDesc.depthStencilState.depthEnable = true;
    pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
    pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
    pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
    pipelineDesc.blendMode = {RHI::BlendMode::None};
    pipelineDesc.rtvFormats = {RHI::ColorFormat::R32G32_UINT};
    return builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_texture(
        m_visibility, RHI::ResourceAccessType::Write,
        RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_texture(m_depth, RHI::ResourceAccessType::Write,
                                 RHI::ResourceState::DepthWrite,
                                 RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_renderObjectBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_transformBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_viewProjectionBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_positionBuffer, RHI::ResourceAccessType::Read,
                                RHI::ResourceState::VertexBuffer,
                                RHI::ResourceState::VertexBuffer);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_indexBuffer, RHI::ResourceAccessType::Read,
                                RHI::ResourceState::IndexBuffer,
                                RHI::ResourceState::IndexBuffer);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_renderObjectIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result =
        builder.use_buffer(m_rangeCommandBuffer, RHI::ResourceAccessType::Read,
                           RHI::ResourceState::IndirectArgument,
                           RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_rangeCommandCountBuffer,
                              RHI::ResourceAccessType::Read,
                              RHI::ResourceState::IndirectArgument,
                              RHI::ResourceState::IndirectArgument);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->set_render_targets(&m_visibilityRtv, 1, m_depthDsv);
    commandContext->set_viewport_scissor(context.width(), context.height());
    commandContext->set_graphics_pipeline(m_pipeline);
    commandContext->set_primitive_topology(
        RHI::PrimitiveTopologyType::Triangle);
    commandContext->set_32bit_constant(0, 0xffffffffu);
    commandContext->set_cbv(1, m_viewProjectionBuffer);
    commandContext->set_srv(2, m_renderObjectBuffer);
    commandContext->set_srv(3, m_transformBuffer);
    commandContext->set_srv(4, m_renderObjectIndexBuffer);
    commandContext->set_vertex_buffer(0, m_positionBuffer);
    commandContext->set_index_buffer(m_indexBuffer, RHI::IndexFormat::UInt32);
    commandContext->execute_indexed_indirect(m_rangeCommandBuffer,
                                             m_rangeCommandCountBuffer,
                                             m_maxRangeCommandCount);
  }

private:
  const RenderPath &m_renderPath;
  const RenderDebugView &m_debugView;
  RHI::TextureHandle m_visibility{};
  RHI::TextureHandle m_depth{};
  RHI::ViewHandle m_visibilityRtv{};
  RHI::ViewHandle m_depthDsv{};
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  uint32_t m_maxRangeCommandCount = 0;
  RHI::BufferHandle m_positionBuffer{};
  RHI::BufferHandle m_indexBuffer{};
  RHI::BufferHandle m_renderObjectIndexBuffer{};
  RHI::BufferHandle m_rangeCommandBuffer{};
  RHI::BufferHandle m_rangeCommandCountBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_vertexShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

} // namespace Cue::DrawSystem
