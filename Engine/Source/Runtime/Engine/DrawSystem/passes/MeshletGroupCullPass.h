#pragma once

/// ****************************************************************************
/// Meshlet group culling and range indirect command generation
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/RenderDebugView.h"
#include "DrawSystem/RenderPath.h"
#include "GpuData/Batching.h"

// === C++ includes ===
#include <cstring>

namespace Cue::DrawSystem {
class MeshletGroupCullPass final : public RHI::FrameGraphPass {
public:
  MeshletGroupCullPass(const DrawFrameState &drawFrameState,
                       const RenderPath &renderPath,
                       const RenderDebugView &debugView,
                       RHI::IBufferManager *bufferManager,
                       GpuData::MeshletGroupCullStatsGpu *statsOutput,
                       RHI::BufferHandle renderObjectBuffer,
                       RHI::BufferHandle transformBuffer,
                       RHI::BufferHandle viewProjectionBuffer,
                       RHI::BufferHandle visibleObjectCountBuffer,
                       uint32_t maxObjectCount, uint32_t maxRangeCommandCount)
      : m_drawFrameState(drawFrameState), m_renderPath(renderPath),
        m_debugView(debugView), m_bufferManager(bufferManager),
        m_statsOutput(statsOutput), m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_maxObjectCount(maxObjectCount),
        m_maxRangeCommandCount(maxRangeCommandCount) {}

  const char *name() const noexcept override { return "MeshletGroupCull"; }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    (void)a_frameIndex;
    return m_renderPath == RenderPath::VisibilityBuffer &&
           m_debugView == RenderDebugView::Forward;
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
    result =
        builder.get_buffer("MeshPool.MeshletBounds", m_meshletBoundsBuffer);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("MeshPool.MeshChunkRange", m_meshChunkRangeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletChunk", m_meshletChunkBuffer);
    if (!result) {
      return result;
    }

    result = builder.get_buffer("ObjectDrawModeBuffer", m_objectDrawModeBuffer);
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

    RHI::BufferDesc statsDesc{};
    statsDesc.name = "MeshletGroupCullStatsBuffer";
    statsDesc.type = RHI::BufferType::Raw;
    statsDesc.defaultHeapCount = 1u;
    statsDesc.uploadHeapCount = 0u;
    statsDesc.readbackHeapCount = builder.buffer_count();
    statsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    statsDesc.stride = sizeof(GpuData::MeshletGroupCullStatsGpu);
    statsDesc.elementCount = 1u;
    statsDesc.size = sizeof(GpuData::MeshletGroupCullStatsGpu);
    statsDesc.alignment = alignof(GpuData::MeshletGroupCullStatsGpu);
    result = builder.create_buffer(statsDesc, m_statsBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc statsUavDesc{};
    statsUavDesc.name = "MeshletGroupCullStatsBufferUAV";
    statsUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    statsUavDesc.bufferKind = RHI::BufferKind::Buffer;
    statsUavDesc.bufferHandle = m_statsBuffer;
    statsUavDesc.numElements = statsDesc.size / sizeof(uint32_t);
    result = builder.create_view(statsUavDesc, m_statsUav);
    if (!result) {
      return result;
    }

    if (m_bufferManager == nullptr) {
      return Result::fail(
          Code::InvalidState, Severity::Error,
          "MeshletGroupCullPass requires a buffer manager for stats readback.");
    }
    result = m_bufferManager->get_readback_buffer_view(m_statsBuffer,
                                                       m_statsReadbackView);
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
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 5});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 6});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 3});
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
    result = builder.use_buffer(
        m_meshChunkRangeBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_meshletChunkBuffer, RHI::ResourceAccessType::Read,
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
    result =
        builder.use_buffer(m_rangeCommandBuffer, RHI::ResourceAccessType::Write,
                           RHI::ResourceState::UnorderedAccess,
                           RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_rangeCommandCountBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_statsBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::UnorderedAccess);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    update_stats_from_readback(context.frame_index());

    const uint32_t clearValues[4] = {0, 0, 0, 0};
    commandContext->clear_unordered_access_uint(m_rangeCommandCountUav,
                                                clearValues);
    commandContext->clear_unordered_access_uint(m_statsUav, clearValues);

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
    commandContext->set_srv(8, m_meshChunkRangeBuffer);
    commandContext->set_srv(9, m_meshletChunkBuffer);
    commandContext->set_uav(10, m_objectDrawModeBuffer);
    commandContext->set_uav(11, m_rangeCommandBuffer);
    commandContext->set_uav(12, m_rangeCommandCountBuffer);
    commandContext->set_uav(13, m_statsBuffer);
    commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);

    commandContext->uav_barrier(m_statsBuffer);
    commandContext->resource_barrier(
        m_statsBuffer,
        RHI::ResourceBarrierDesc{RHI::ResourceState::UnorderedAccess,
                                 RHI::ResourceState::CopySource});

    RHI::BufferToReadbackCopyRegion statsCopyRegion{};
    statsCopyRegion.srcBufferHandle = m_statsBuffer;
    statsCopyRegion.srcDefaultResourceIndex = 0u;
    statsCopyRegion.srcByteOffset = 0u;
    statsCopyRegion.dstBufferHandle = m_statsBuffer;
    statsCopyRegion.dstReadbackResourceIndex = context.frame_index();
    statsCopyRegion.dstByteOffset = 0u;
    statsCopyRegion.byteSize = sizeof(GpuData::MeshletGroupCullStatsGpu);
    commandContext->copy_buffer_region_to_readback(statsCopyRegion);

    commandContext->resource_barrier(
        m_statsBuffer,
        RHI::ResourceBarrierDesc{RHI::ResourceState::CopySource,
                                 RHI::ResourceState::UnorderedAccess});
  }

private:
  void update_stats_from_readback(uint32_t frameIndex) noexcept {
    if (m_statsOutput == nullptr ||
        frameIndex >= m_statsReadbackView.mappedDatas.size()) {
      return;
    }

    const std::byte *mappedData = m_statsReadbackView.mappedDatas[frameIndex];
    if (mappedData == nullptr) {
      return;
    }

    GpuData::MeshletGroupCullStatsGpu stats{};
    std::memcpy(&stats, mappedData, sizeof(stats));
    *m_statsOutput = stats;
  }

  const DrawFrameState &m_drawFrameState;
  const RenderPath &m_renderPath;
  const RenderDebugView &m_debugView;
  RHI::IBufferManager *m_bufferManager = nullptr;
  GpuData::MeshletGroupCullStatsGpu *m_statsOutput = nullptr;
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  uint32_t m_maxObjectCount = 0;
  uint32_t m_maxRangeCommandCount = 0;
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_meshChunkRangeBuffer{};
  RHI::BufferHandle m_meshletChunkBuffer{};
  RHI::BufferHandle m_objectDrawModeBuffer{};
  RHI::BufferHandle m_rangeCommandBuffer{};
  RHI::BufferHandle m_rangeCommandCountBuffer{};
  RHI::BufferHandle m_statsBuffer{};
  RHI::ViewHandle m_rangeCommandCountUav{};
  RHI::ViewHandle m_statsUav{};
  RHI::ReadbackBufferView m_statsReadbackView{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};
} // namespace Cue::DrawSystem
