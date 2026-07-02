#pragma once

#include <FrameGraph.h>

#include "DrawSystem/RenderDebugView.h"
#include "GpuData/ClusteredLighting.h"

#include <cmath>
#include <cstring>

namespace Cue::DrawSystem {

class VisibilityResolveDebugPass final : public RHI::FrameGraphPass {
public:
  VisibilityResolveDebugPass(const RenderDebugView &debugView,
                             RHI::BufferHandle renderObjectBuffer,
                             RHI::BufferHandle transformBuffer,
                             RHI::BufferHandle viewProjectionBuffer,
                             RHI::BufferHandle materialBuffer,
                             RHI::BufferHandle lightFrameBuffer,
                             RHI::BufferHandle directionalLightBuffer,
                             RHI::BufferHandle pointLightBuffer)
      : m_debugView(debugView), m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_materialBuffer(materialBuffer), m_lightFrameBuffer(lightFrameBuffer),
        m_directionalLightBuffer(directionalLightBuffer),
        m_pointLightBuffer(pointLightBuffer) {}

  const char *name() const noexcept override {
    return "VisibilityResolveDebug";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    (void)a_frameIndex;
    return m_debugView == RenderDebugView::VisibilityBarycentric ||
           m_debugView == RenderDebugView::VisibilityNormal ||
           m_debugView == RenderDebugView::VisibilityUv ||
           m_debugView == RenderDebugView::VisibilityLit ||
           m_debugView == RenderDebugView::VisibilityMaterial;
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

    result = builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.VisibilityTriangle",
                                m_visibilityTriangleBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("ClusterLightRangeBuffer",
                                m_clusterLightRangeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("ClusterLightIndexBuffer",
                                m_clusterLightIndexBuffer);
    if (!result) {
      return result;
    }

    m_screenWidth = builder.width();
    m_screenHeight = builder.height();
    m_clusterTileCountX = ClusteredLighting::k_clusterCountX;
    m_clusterTileCountY = ClusteredLighting::k_clusterCountY;
    m_clusterDepthSliceCount = ClusteredLighting::k_depthSliceCount;
    m_clusterNearZ = 0.01f;
    m_clusterFarZ = 100.0f;
    m_clusterInvLogFarNear = 1.0f / std::log(m_clusterFarZ / m_clusterNearZ);

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "VisibilityResolveDebugRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::DescriptorTableSRV,
         RHI::ShaderVisibility::Pixel, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::Pixel, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         3});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 5});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 8});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::Pixel, 4});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 9});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 10});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         5});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         6});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         7});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         8});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         9});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::Pixel,
         10});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 11});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 12});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 13});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc vsDesc{};
    vsDesc.name = "VisibilityResolveDebugVS";
    vsDesc.filePath = "Shaders/D3D12/VisibilityResolveDebug.hlsl";
    vsDesc.entryPoint = "vs_main";
    vsDesc.targetProfile = "vs_6_0";
    result = builder.create_shader_blob(vsDesc, m_vertexShader);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc psDesc{};
    psDesc.name = "VisibilityResolveDebugPS";
    psDesc.filePath = "Shaders/D3D12/VisibilityResolveDebug.hlsl";
    psDesc.entryPoint = "ps_main";
    psDesc.targetProfile = "ps_6_0";
    result = builder.create_shader_blob(psDesc, m_pixelShader);
    if (!result) {
      return result;
    }

    RHI::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "VisibilityResolveDebugPipeline";
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
    result = builder.use_texture(m_color, RHI::ResourceAccessType::Write,
                                 RHI::ResourceState::RenderTarget,
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
        m_meshRangeBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
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
        m_visibilityTriangleBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_lightFrameBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_directionalLightBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_pointLightBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_clusterLightRangeBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_clusterLightIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(
        m_viewProjectionBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
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
    commandContext->set_cbv(1, m_viewProjectionBuffer);
    commandContext->set_32bit_constant(2, m_screenWidth);
    commandContext->set_32bit_constant(3, m_screenHeight);
    commandContext->set_32bit_constant(4, static_cast<uint32_t>(m_debugView));
    commandContext->set_srv(5, m_renderObjectBuffer);
    commandContext->set_srv(6, m_meshRangeBuffer);
    commandContext->set_srv(7, m_transformBuffer);
    commandContext->set_cbv(9, m_lightFrameBuffer);
    commandContext->set_srv(10, m_directionalLightBuffer);
    commandContext->set_srv(11, m_pointLightBuffer);
    commandContext->set_32bit_constant(12, m_clusterTileCountX);
    commandContext->set_32bit_constant(13, m_clusterTileCountY);
    commandContext->set_32bit_constant(14, m_clusterDepthSliceCount);
    commandContext->set_32bit_constant(15, float_to_uint32(m_clusterNearZ));
    commandContext->set_32bit_constant(16, float_to_uint32(m_clusterFarZ));
    commandContext->set_32bit_constant(17,
                                       float_to_uint32(m_clusterInvLogFarNear));
    commandContext->set_srv(18, m_clusterLightRangeBuffer);
    commandContext->set_srv(19, m_clusterLightIndexBuffer);
    commandContext->set_srv(20, m_visibilityTriangleBuffer);
    commandContext->draw_instanced(3, 1, 0, 0);
  }

private:
  const RenderDebugView &m_debugView;
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_materialBuffer{};
  RHI::BufferHandle m_lightFrameBuffer{};
  RHI::BufferHandle m_directionalLightBuffer{};
  RHI::BufferHandle m_pointLightBuffer{};
  RHI::TextureHandle m_color{};
  RHI::TextureHandle m_visibility{};
  RHI::ViewHandle m_colorRtv{};
  RHI::ViewHandle m_visibilitySrv{};
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_visibilityTriangleBuffer{};
  RHI::BufferHandle m_clusterLightRangeBuffer{};
  RHI::BufferHandle m_clusterLightIndexBuffer{};
  uint32_t m_screenWidth = 1;
  uint32_t m_screenHeight = 1;
  uint32_t m_clusterTileCountX = 1;
  uint32_t m_clusterTileCountY = 1;
  uint32_t m_clusterDepthSliceCount = 1;
  float m_clusterNearZ = 0.01f;
  float m_clusterFarZ = 100.0f;
  float m_clusterInvLogFarNear = 1.0f;
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_vertexShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_pipeline{};

  static uint32_t float_to_uint32(float value) noexcept {
    uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }
};

} // namespace Cue::DrawSystem
