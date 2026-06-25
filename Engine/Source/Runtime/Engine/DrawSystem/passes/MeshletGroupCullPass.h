#pragma once

/// ****************************************************************************
/// Meshlet group culling and range indirect command generation
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "GpuData/Batching.h"

namespace Cue::DrawSystem {
class MeshletGroupCullPass final : public RHI::FrameGraphPass {
public:
  MeshletGroupCullPass(const DrawFrameState &drawFrameState,
                       RHI::BufferHandle renderObjectBuffer,
                       RHI::BufferHandle transformBuffer,
                       RHI::BufferHandle viewProjectionBuffer,
                       RHI::BufferHandle visibleObjectCountBuffer,
                       uint32_t maxObjectCount,
                       uint32_t maxRangeCommandCount)
      : m_drawFrameState(drawFrameState),
        m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_maxObjectCount(maxObjectCount),
        m_maxRangeCommandCount(maxRangeCommandCount) {}

  const char *name() const noexcept override { return "MeshletGroupCull"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.read_buffer(m_renderObjectBuffer);
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
    result = builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletBounds", m_meshletBoundsBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc drawModeDesc{};
    drawModeDesc.name = "ObjectDrawModeBuffer";
    drawModeDesc.type = RHI::BufferType::UnorderedAccess;
    drawModeDesc.defaultHeapCount = 1;
    drawModeDesc.uploadHeapCount = 0;
    drawModeDesc.initialState = RHI::ResourceState::UnorderedAccess;
    drawModeDesc.stride = sizeof(uint32_t);
    drawModeDesc.elementCount = m_maxObjectCount;
    drawModeDesc.size = drawModeDesc.stride * drawModeDesc.elementCount;
    drawModeDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(drawModeDesc, m_objectDrawModeBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc commandDesc{};
    commandDesc.name = "GroupRangeCommandBuffer";
    commandDesc.type = RHI::BufferType::UnorderedAccess;
    commandDesc.defaultHeapCount = 1;
    commandDesc.uploadHeapCount = 0;
    commandDesc.initialState = RHI::ResourceState::UnorderedAccess;
    commandDesc.stride = sizeof(GpuData::IndirectCommand);
    commandDesc.elementCount = m_maxRangeCommandCount;
    commandDesc.size = commandDesc.stride * commandDesc.elementCount;
    commandDesc.alignment = alignof(GpuData::IndirectCommand);
    result = builder.create_buffer(commandDesc, m_rangeCommandBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc countDesc{};
    countDesc.name = "GroupRangeCommandCountBuffer";
    countDesc.type = RHI::BufferType::Raw;
    countDesc.defaultHeapCount = 1;
    countDesc.uploadHeapCount = 0;
    countDesc.initialState = RHI::ResourceState::UnorderedAccess;
    countDesc.stride = sizeof(uint32_t);
    countDesc.elementCount = 1;
    countDesc.size = sizeof(uint32_t);
    countDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(countDesc, m_rangeCommandCountBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc countUavDesc{};
    countUavDesc.name = "GroupRangeCommandCountBufferUAV";
    countUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    countUavDesc.bufferKind = RHI::BufferKind::Buffer;
    countUavDesc.bufferHandle = m_rangeCommandCountBuffer;
    countUavDesc.numElements = countDesc.size / sizeof(uint32_t);
    result = builder.create_view(countUavDesc, m_rangeCommandCountUav);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "MeshletGroupCullRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 2});
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
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 2});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "MeshletGroupCullCS";
    shaderDesc.filePath = "Shaders/D3D12/MeshletGroupCull.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "MeshletGroupCullPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_renderObjectBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_transformBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
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
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
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
        m_meshletBoundsBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_objectDrawModeBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_rangeCommandBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_rangeCommandCountBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::IndirectArgument);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const uint32_t clearValues[4] = {0, 0, 0, 0};
    commandContext->clear_unordered_access_uint(m_rangeCommandCountUav,
                                                clearValues);

    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (frameState.objectCount == 0) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(0, frameState.objectCount);
    commandContext->set_32bit_constant(1, m_maxRangeCommandCount);
    commandContext->set_cbv(2, m_viewProjectionBuffer);
    commandContext->set_srv(3, m_renderObjectBuffer);
    commandContext->set_srv(4, m_transformBuffer);
    commandContext->set_srv(5, m_visibleObjectCountBuffer);
    commandContext->set_srv(6, m_meshRangeBuffer);
    commandContext->set_srv(7, m_meshletBoundsBuffer);
    commandContext->set_uav(8, m_objectDrawModeBuffer);
    commandContext->set_uav(9, m_rangeCommandBuffer);
    commandContext->set_uav(10, m_rangeCommandCountBuffer);
    commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  uint32_t m_maxObjectCount = 0;
  uint32_t m_maxRangeCommandCount = 0;
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_objectDrawModeBuffer{};
  RHI::BufferHandle m_rangeCommandBuffer{};
  RHI::BufferHandle m_rangeCommandCountBuffer{};
  RHI::ViewHandle m_rangeCommandCountUav{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};
} // namespace Cue::DrawSystem
