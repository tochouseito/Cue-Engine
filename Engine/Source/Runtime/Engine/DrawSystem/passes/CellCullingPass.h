#pragma once

/// ****************************************************************************
/// Cell culling and visible-cell object culling
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"

namespace Cue::DrawSystem {
class CellCullingPass final : public RHI::FrameGraphPass {
public:
  CellCullingPass(const DrawFrameState &drawFrameState,
                  RHI::BufferHandle renderCellBuffer,
                  RHI::BufferHandle viewProjectionBuffer, uint32_t maxCellCount)
      : m_drawFrameState(drawFrameState), m_renderCellBuffer(renderCellBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_maxCellCount(maxCellCount) {}

  const char *name() const noexcept override { return "CellCulling"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    m_tileCountX = (builder.width() + m_tileSize - 1u) / m_tileSize;
    m_tileCountY = (builder.height() + m_tileSize - 1u) / m_tileSize;
    if (m_tileCountX == 0u || m_tileCountY == 0u) {
      return Result::fail(Code::InvalidArgument, Severity::Error,
                          "Cell culling tile count must not be zero.");
    }

    Result result = builder.read_buffer(m_renderCellBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_viewProjectionBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("HiZDepthBuffer", m_hizDepthBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc visibleCellIndexBufferDesc{};
    visibleCellIndexBufferDesc.name = "VisibleCellIndexBuffer";
    visibleCellIndexBufferDesc.type = RHI::BufferType::UnorderedAccess;
    visibleCellIndexBufferDesc.defaultHeapCount = 1;
    visibleCellIndexBufferDesc.uploadHeapCount = 0;
    visibleCellIndexBufferDesc.initialState =
        RHI::ResourceState::UnorderedAccess;
    visibleCellIndexBufferDesc.stride = sizeof(uint32_t);
    visibleCellIndexBufferDesc.elementCount = m_maxCellCount;
    visibleCellIndexBufferDesc.size = visibleCellIndexBufferDesc.stride *
                                      visibleCellIndexBufferDesc.elementCount;
    visibleCellIndexBufferDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(visibleCellIndexBufferDesc,
                                   m_visibleCellIndexBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc visibleCellCountBufferDesc{};
    visibleCellCountBufferDesc.name = "VisibleCellCountBuffer";
    visibleCellCountBufferDesc.type = RHI::BufferType::Raw;
    visibleCellCountBufferDesc.defaultHeapCount = 1;
    visibleCellCountBufferDesc.uploadHeapCount = 0;
    visibleCellCountBufferDesc.initialState =
        RHI::ResourceState::UnorderedAccess;
    visibleCellCountBufferDesc.stride = sizeof(uint32_t);
    visibleCellCountBufferDesc.elementCount = 1;
    visibleCellCountBufferDesc.size = sizeof(uint32_t);
    visibleCellCountBufferDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(visibleCellCountBufferDesc,
                                   m_visibleCellCountBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc visibleCellCountUavDesc{};
    visibleCellCountUavDesc.name = "VisibleCellCountBufferUAV";
    visibleCellCountUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    visibleCellCountUavDesc.bufferKind = RHI::BufferKind::Buffer;
    visibleCellCountUavDesc.bufferHandle = m_visibleCellCountBuffer;
    visibleCellCountUavDesc.numElements = 1;
    result =
        builder.create_view(visibleCellCountUavDesc, m_visibleCellCountUav);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "CellCullingRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         3});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 4});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "CellCullingCS";
    shaderDesc.filePath = "Shaders/D3D12/CellCulling.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "CellCullingPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_renderCellBuffer, RHI::ResourceAccessType::Read,
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
    result = builder.use_buffer(m_hizDepthBuffer, RHI::ResourceAccessType::Read,
                                RHI::ResourceState::ShaderResource,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_visibleCellIndexBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_visibleCellCountBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::ShaderResource);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const uint32_t clearValues[4] = {0, 0, 0, 0};
    commandContext->clear_unordered_access_uint(m_visibleCellCountUav,
                                                clearValues);

    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (frameState.cellCount == 0) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(0, frameState.cellCount);
    commandContext->set_32bit_constant(1, m_tileCountX);
    commandContext->set_32bit_constant(2, m_tileCountY);
    commandContext->set_32bit_constant(3, m_tileSize);
    commandContext->set_cbv(4, m_viewProjectionBuffer);
    commandContext->set_srv(5, m_renderCellBuffer);
    commandContext->set_srv(6, m_hizDepthBuffer);
    commandContext->set_uav(7, m_visibleCellIndexBuffer);
    commandContext->set_uav(8, m_visibleCellCountBuffer);
    commandContext->dispatch((frameState.cellCount + 63u) / 64u, 1, 1);
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderCellBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_hizDepthBuffer{};
  RHI::BufferHandle m_visibleCellIndexBuffer{};
  RHI::BufferHandle m_visibleCellCountBuffer{};
  RHI::ViewHandle m_visibleCellCountUav{};
  uint32_t m_maxCellCount = 0;
  uint32_t m_tileSize = 16u;
  uint32_t m_tileCountX = 0;
  uint32_t m_tileCountY = 0;
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class ObjectCullingPass final : public RHI::FrameGraphPass {
public:
  ObjectCullingPass(const DrawFrameState &drawFrameState,
                    RHI::BufferHandle renderableInfoBuffer,
                    RHI::BufferHandle renderCellBuffer,
                    RHI::BufferHandle viewProjectionBuffer,
                    RHI::BufferHandle renderObjectBuffer,
                    RHI::BufferHandle visibleObjectCountBuffer,
                    RHI::ViewHandle visibleObjectCountUav,
                    uint32_t maxCellCount, uint32_t cellObjectCapacity,
                    uint32_t maxObjectCount)
      : m_drawFrameState(drawFrameState),
        m_renderableInfoBuffer(renderableInfoBuffer),
        m_renderCellBuffer(renderCellBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_renderObjectBuffer(renderObjectBuffer),
        m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_visibleObjectCountUav(visibleObjectCountUav),
        m_maxCellCount(maxCellCount), m_cellObjectCapacity(cellObjectCapacity),
        m_maxObjectCount(maxObjectCount) {}

  const char *name() const noexcept override { return "ObjectCulling"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    m_tileCountX = (builder.width() + m_tileSize - 1u) / m_tileSize;
    m_tileCountY = (builder.height() + m_tileSize - 1u) / m_tileSize;
    if (m_tileCountX == 0u || m_tileCountY == 0u) {
      return Result::fail(Code::InvalidArgument, Severity::Error,
                          "Object culling tile count must not be zero.");
    }

    Result result = builder.read_buffer(m_renderableInfoBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_renderCellBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_viewProjectionBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_renderObjectBuffer);
    if (!result) {
      return result;
    }
    result = builder.read_buffer(m_visibleObjectCountBuffer);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("VisibleCellIndexBuffer", m_visibleCellIndexBuffer);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("VisibleCellCountBuffer", m_visibleCellCountBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("HiZDepthBuffer", m_hizDepthBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "ObjectCullingRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         3});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 4});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         5});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 4});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "ObjectCullingCS";
    shaderDesc.filePath = "Shaders/D3D12/ObjectCulling.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "ObjectCullingPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_renderableInfoBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_renderCellBuffer, RHI::ResourceAccessType::Read,
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
        m_visibleCellIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_visibleCellCountBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_hizDepthBuffer, RHI::ResourceAccessType::Read,
                                RHI::ResourceState::ShaderResource,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result =
        builder.use_buffer(m_renderObjectBuffer, RHI::ResourceAccessType::Write,
                           RHI::ResourceState::UnorderedAccess,
                           RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_visibleObjectCountBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::ShaderResource);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const uint32_t clearValues[4] = {0, 0, 0, 0};
    commandContext->clear_unordered_access_uint(m_visibleObjectCountUav,
                                                clearValues);

    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (frameState.cellCount == 0 || frameState.objectCount == 0) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(0, m_cellObjectCapacity);
    commandContext->set_32bit_constant(1, m_tileCountX);
    commandContext->set_32bit_constant(2, m_tileCountY);
    commandContext->set_32bit_constant(3, m_tileSize);
    commandContext->set_cbv(4, m_viewProjectionBuffer);
    commandContext->set_32bit_constant(5, m_maxObjectCount);
    commandContext->set_srv(6, m_renderableInfoBuffer);
    commandContext->set_srv(7, m_renderCellBuffer);
    commandContext->set_srv(8, m_visibleCellIndexBuffer);
    commandContext->set_srv(9, m_visibleCellCountBuffer);
    commandContext->set_srv(10, m_hizDepthBuffer);
    commandContext->set_uav(11, m_renderObjectBuffer);
    commandContext->set_uav(12, m_visibleObjectCountBuffer);
    commandContext->dispatch(
        (frameState.cellCount * m_cellObjectCapacity + 63u) / 64u, 1, 1);
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderableInfoBuffer{};
  RHI::BufferHandle m_renderCellBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  RHI::BufferHandle m_visibleCellIndexBuffer{};
  RHI::BufferHandle m_visibleCellCountBuffer{};
  RHI::BufferHandle m_hizDepthBuffer{};
  RHI::ViewHandle m_visibleObjectCountUav{};
  uint32_t m_maxCellCount = 0;
  uint32_t m_cellObjectCapacity = 1;
  uint32_t m_maxObjectCount = 0;
  uint32_t m_tileSize = 16u;
  uint32_t m_tileCountX = 0;
  uint32_t m_tileCountY = 0;
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};
} // namespace Cue::DrawSystem
