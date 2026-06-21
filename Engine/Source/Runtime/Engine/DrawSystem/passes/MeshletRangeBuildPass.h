#pragma once

/// ****************************************************************************
/// Meshlet range build pass
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/passes/StaticMeshBatchingPass.h"
#include "GpuData/MeshletRangeStats.h"

// === C++ includes ===
#include <cstring>

namespace Cue::DrawSystem {
class MeshletRangeBuildPass final : public RHI::FrameGraphPass {
public:
  MeshletRangeBuildPass(const DrawFrameState &drawFrameState,
                        RHI::IBufferManager *bufferManager,
                        RHI::BufferHandle renderObjectBuffer,
                        RHI::BufferHandle transformBuffer,
                        RHI::BufferHandle viewProjectionBuffer,
                        RHI::BufferHandle visibleObjectCountBuffer,
                        uint32_t maxObjectCount,
                        GpuData::MeshletRangeStatsGpu *statsOutput)
      : m_drawFrameState(drawFrameState),
        m_bufferManager(bufferManager),
        m_statsOutput(statsOutput),
        m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_maxObjectCount(maxObjectCount),
        m_maxRangeDrawCommandCount(maxObjectCount *
                                   StaticMeshBatching::k_maxRangesPerObject) {}

  const char *name() const noexcept override { return "MeshletRangeBuild"; }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    m_tileCountX = (builder.width() + m_tileSize - 1u) / m_tileSize;
    m_tileCountY = (builder.height() + m_tileSize - 1u) / m_tileSize;
    if (m_tileCountX == 0u || m_tileCountY == 0u) {
      return Result::fail(Code::InvalidArgument, Severity::Error,
                          "Meshlet range tile count must not be zero.");
    }

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
    result = builder.get_buffer("MeshletRefinedVisibilityBuffer",
                                m_refinedVisibilityBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("ObjectDrawPathBuffer", m_objectDrawPathBuffer);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("RangeDrawCommandBuffer", m_rangeDrawCommandBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("RangeDrawCountBuffer", m_rangeDrawCountBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("RangeOverflowBuffer", m_rangeOverflowBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("HiZDepthBuffer", m_hizDepthBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc statsBufferDesc{};
    statsBufferDesc.name = "MeshletRangeStatsBuffer";
    statsBufferDesc.type = RHI::BufferType::Raw;
    statsBufferDesc.defaultHeapCount = 1;
    statsBufferDesc.uploadHeapCount = 0;
    statsBufferDesc.readbackHeapCount = builder.buffer_count();
    statsBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
    statsBufferDesc.stride = sizeof(GpuData::MeshletRangeStatsGpu);
    statsBufferDesc.elementCount = 1u;
    statsBufferDesc.size = statsBufferDesc.stride * statsBufferDesc.elementCount;
    statsBufferDesc.alignment = alignof(GpuData::MeshletRangeStatsGpu);
    result = builder.create_buffer(statsBufferDesc, m_rangeStatsBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc statsUavDesc{};
    statsUavDesc.name = "MeshletRangeStatsBufferUAV";
    statsUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    statsUavDesc.bufferKind = RHI::BufferKind::Buffer;
    statsUavDesc.bufferHandle = m_rangeStatsBuffer;
    statsUavDesc.numElements = statsBufferDesc.size / sizeof(uint32_t);
    result = builder.create_view(statsUavDesc, m_rangeStatsUav);
    if (!result) {
      return result;
    }
    if (m_bufferManager == nullptr) {
      return Result::fail(Code::InvalidState, Severity::Error,
                          "MeshletRangeBuildPass requires a buffer manager.");
    }
    result =
        m_bufferManager->get_readback_buffer_view(m_rangeStatsBuffer,
                                                  m_rangeStatsReadbackView);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "MeshletRangeBuildRootSignature";
    for (uint32_t constantIndex = 0; constantIndex <= 6u; ++constantIndex) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
           constantIndex});
    }
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 7});
    for (uint32_t constantIndex = 8; constantIndex <= 10u; ++constantIndex) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
           constantIndex});
    }
    for (uint32_t srvIndex = 0; srvIndex <= 6u; ++srvIndex) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, srvIndex});
    }
    for (uint32_t uavIndex = 0; uavIndex <= 4u; ++uavIndex) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, uavIndex});
    }
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "MeshletRangeBuildCS";
    shaderDesc.filePath = "Shaders/D3D12/MeshletRangeBuild.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "MeshletRangeBuildPipeline";
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
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
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
        m_refinedVisibilityBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_hizDepthBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_objectDrawPathBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_rangeDrawCommandBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_rangeDrawCountBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_rangeOverflowBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_rangeStatsBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::UnorderedAccess);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    update_stats_from_readback(context.frame_index());

    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (frameState.objectCount == 0) {
      return;
    }

    const uint32_t clearValues[4] = {0u, 0u, 0u, 0u};
    commandContext->clear_unordered_access_uint(m_rangeStatsUav, clearValues);

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(0, m_maxObjectCount);
    commandContext->set_32bit_constant(1, m_minRangeMeshletCount);
    commandContext->set_32bit_constant(2, m_minRangeIndexCount);
    commandContext->set_32bit_constant(
        3, float_to_uint32(m_minRangeProjectedRadius));
    commandContext->set_32bit_constant(4, m_maxRangeDrawCommandCount);
    commandContext->set_32bit_constant(5, m_maxRangesPerObject);
    commandContext->set_32bit_constant(6, m_maxRangeGapIndices);
    commandContext->set_cbv(7, m_viewProjectionBuffer);
    commandContext->set_32bit_constant(8, m_tileCountX);
    commandContext->set_32bit_constant(9, m_tileCountY);
    commandContext->set_32bit_constant(10, m_tileSize);
    commandContext->set_srv(11, m_renderObjectBuffer);
    commandContext->set_srv(12, m_transformBuffer);
    commandContext->set_srv(13, m_visibleObjectCountBuffer);
    commandContext->set_srv(14, m_meshRangeBuffer);
    commandContext->set_srv(15, m_meshletBoundsBuffer);
    commandContext->set_srv(16, m_refinedVisibilityBuffer);
    commandContext->set_srv(17, m_hizDepthBuffer);
    commandContext->set_uav(18, m_objectDrawPathBuffer);
    commandContext->set_uav(19, m_rangeDrawCommandBuffer);
    commandContext->set_uav(20, m_rangeDrawCountBuffer);
    commandContext->set_uav(21, m_rangeOverflowBuffer);
    commandContext->set_uav(22, m_rangeStatsBuffer);
    commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);

    commandContext->resource_barrier(
        m_rangeStatsBuffer,
        RHI::ResourceBarrierDesc{RHI::ResourceState::UnorderedAccess,
                                 RHI::ResourceState::CopySource});

    RHI::BufferToReadbackCopyRegion statsCopyRegion{};
    statsCopyRegion.srcBufferHandle = m_rangeStatsBuffer;
    statsCopyRegion.srcDefaultResourceIndex = 0u;
    statsCopyRegion.srcByteOffset = 0u;
    statsCopyRegion.dstBufferHandle = m_rangeStatsBuffer;
    statsCopyRegion.dstReadbackResourceIndex = context.frame_index();
    statsCopyRegion.dstByteOffset = 0u;
    statsCopyRegion.byteSize = sizeof(GpuData::MeshletRangeStatsGpu);
    commandContext->copy_buffer_region_to_readback(statsCopyRegion);

    commandContext->resource_barrier(
        m_rangeStatsBuffer,
        RHI::ResourceBarrierDesc{RHI::ResourceState::CopySource,
                                 RHI::ResourceState::UnorderedAccess});
  }

private:
  void update_stats_from_readback(uint32_t frameIndex) noexcept {
    if (m_statsOutput == nullptr ||
        frameIndex >= m_rangeStatsReadbackView.mappedDatas.size()) {
      return;
    }

    const std::byte *mappedData =
        m_rangeStatsReadbackView.mappedDatas[frameIndex];
    if (mappedData == nullptr) {
      return;
    }

    GpuData::MeshletRangeStatsGpu stats{};
    std::memcpy(&stats, mappedData, sizeof(stats));
    *m_statsOutput = stats;
  }

  const DrawFrameState &m_drawFrameState;
  RHI::IBufferManager *m_bufferManager = nullptr;
  GpuData::MeshletRangeStatsGpu *m_statsOutput = nullptr;
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_refinedVisibilityBuffer{};
  RHI::BufferHandle m_hizDepthBuffer{};
  RHI::BufferHandle m_objectDrawPathBuffer{};
  RHI::BufferHandle m_rangeDrawCommandBuffer{};
  RHI::BufferHandle m_rangeDrawCountBuffer{};
  RHI::BufferHandle m_rangeOverflowBuffer{};
  RHI::BufferHandle m_rangeStatsBuffer{};
  RHI::ViewHandle m_rangeStatsUav{};
  RHI::ReadbackBufferView m_rangeStatsReadbackView{};
  uint32_t m_maxObjectCount = 0;
  uint32_t m_maxRangeDrawCommandCount = 0;
  uint32_t m_minRangeMeshletCount = 1u;
  uint32_t m_minRangeIndexCount = 3u;
  uint32_t m_maxRangesPerObject = StaticMeshBatching::k_maxRangesPerObject;
  uint32_t m_maxRangeGapIndices = 0u;
  uint32_t m_tileSize = 8u;
  uint32_t m_tileCountX = 0u;
  uint32_t m_tileCountY = 0u;
  float m_minRangeProjectedRadius = 0.0f;
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};

  static uint32_t float_to_uint32(float value) noexcept {
    uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }
};
} // namespace Cue::DrawSystem
