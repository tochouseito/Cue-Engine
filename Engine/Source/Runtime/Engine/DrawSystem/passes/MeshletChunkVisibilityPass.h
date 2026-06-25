#pragma once

/// ****************************************************************************
/// MeshletChunk persistent visibility bitset support
/// ****************************************************************************

// === RHI includes ===
#include <BufferManager.h>
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "GpuData/Batching.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace Cue::DrawSystem {
namespace MeshletChunkVisibility {
[[nodiscard]] constexpr uint32_t
word_count_for_chunks(uint32_t maxChunkCount) noexcept {
  return (maxChunkCount + 31u) / 32u;
}
} // namespace MeshletChunkVisibility

class MeshletChunkVisibilityResetPass final : public RHI::FrameGraphPass {
public:
  explicit MeshletChunkVisibilityResetPass(uint32_t maxChunkCount)
      : m_wordCount(
            MeshletChunkVisibility::word_count_for_chunks(maxChunkCount)) {}

  const char *name() const noexcept override {
    return "MeshletChunkVisibilityReset";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    if (m_wordCount == 0u) {
      m_wordCount = 1u;
    }

    for (uint32_t bufferIndex = 0; bufferIndex < 2u; ++bufferIndex) {
      RHI::BufferDesc desc{};
      desc.name =
          bufferIndex == 0u ? "ChunkVisibility.Bits0" : "ChunkVisibility.Bits1";
      desc.type = RHI::BufferType::Raw;
      desc.defaultHeapCount = 1;
      desc.uploadHeapCount = 0;
      desc.initialState = RHI::ResourceState::UnorderedAccess;
      desc.stride = sizeof(uint32_t);
      desc.elementCount = m_wordCount;
      desc.size = desc.stride * desc.elementCount;
      desc.alignment = alignof(uint32_t);
      Result result = builder.create_buffer(desc, m_bitBuffers[bufferIndex]);
      if (!result) {
        return result;
      }

      RHI::ViewDesc uavDesc{};
      uavDesc.name = bufferIndex == 0u ? "ChunkVisibility.Bits0UAV"
                                       : "ChunkVisibility.Bits1UAV";
      uavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
      uavDesc.bufferKind = RHI::BufferKind::Buffer;
      uavDesc.bufferHandle = m_bitBuffers[bufferIndex];
      uavDesc.firstElement = 0;
      uavDesc.numElements = m_wordCount;
      result = builder.create_view(uavDesc, m_bitUavs[bufferIndex]);
      if (!result) {
        return result;
      }
    }

    return Result::ok();
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    for (RHI::BufferHandle buffer : m_bitBuffers) {
      Result result = builder.use_buffer(buffer, RHI::ResourceAccessType::Write,
                                         RHI::ResourceState::UnorderedAccess,
                                         RHI::ResourceState::Common);
      if (!result) {
        return result;
      }
    }
    return Result::ok();
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const uint32_t clearValues[4] = {0u, 0u, 0u, 0u};
    if (!m_initialized) {
      for (RHI::ViewHandle uav : m_bitUavs) {
        commandContext->clear_unordered_access_uint(uav, clearValues);
      }
      m_initialized = true;
      return;
    }

    const uint32_t currentIndex = context.frame_index() & 1u;
    commandContext->clear_unordered_access_uint(m_bitUavs[currentIndex],
                                                clearValues);
  }

private:
  uint32_t m_wordCount = 1u;
  std::array<RHI::BufferHandle, 2> m_bitBuffers{};
  std::array<RHI::ViewHandle, 2> m_bitUavs{};
  bool m_initialized = false;
};

class ChunkHiZResourcesPass final : public RHI::FrameGraphPass {
public:
  explicit ChunkHiZResourcesPass(uint32_t bufferCount)
      : m_bufferCount(bufferCount) {}

  const char *name() const noexcept override { return "ChunkHiZResources"; }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    m_hizWidth = std::max(1u, builder.width() / 4u);
    m_hizHeight = std::max(1u, builder.height() / 4u);

    RHI::TextureDesc hizDesc{};
    hizDesc.name = "ChunkHiZ.Texture";
    hizDesc.kind = RHI::TextureKind::Default;
    hizDesc.width = m_hizWidth;
    hizDesc.height = m_hizHeight;
    hizDesc.format = RHI::ColorFormat::R32_UINT;
    hizDesc.allowUnorderedAccess = true;
    Result result = builder.create_texture(hizDesc, m_hizTexture);
    if (!result) {
      return result;
    }

    RHI::ViewDesc srvDesc{};
    srvDesc.name = "ChunkHiZ.SRV";
    srvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
    srvDesc.bufferKind = RHI::BufferKind::Texture;
    srvDesc.textureHandle = m_hizTexture;
    srvDesc.colorFormat = RHI::ColorFormat::R32_UINT;
    result = builder.create_view(srvDesc, m_hizSrv);
    if (!result) {
      return result;
    }

    RHI::ViewDesc uavDesc{};
    uavDesc.name = "ChunkHiZ.UAV";
    uavDesc.type = RHI::ViewType::UnorderedAccessTexture2D;
    uavDesc.bufferKind = RHI::BufferKind::Texture;
    uavDesc.textureHandle = m_hizTexture;
    uavDesc.colorFormat = RHI::ColorFormat::R32_UINT;
    result = builder.create_view(uavDesc, m_hizUav);
    if (!result) {
      return result;
    }

    RHI::BufferDesc statsDesc{};
    statsDesc.name = "ChunkOcclusionStatsBuffer";
    statsDesc.type = RHI::BufferType::Raw;
    statsDesc.defaultHeapCount = 1u;
    statsDesc.uploadHeapCount = 0u;
    statsDesc.readbackHeapCount = m_bufferCount;
    statsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    statsDesc.stride = sizeof(uint32_t);
    statsDesc.elementCount = 32u;
    statsDesc.size = statsDesc.stride * statsDesc.elementCount;
    statsDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(statsDesc, m_statsBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc statsUavDesc{};
    statsUavDesc.name = "ChunkOcclusionStatsUAV";
    statsUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    statsUavDesc.bufferKind = RHI::BufferKind::Buffer;
    statsUavDesc.bufferHandle = m_statsBuffer;
    statsUavDesc.numElements = statsDesc.size / sizeof(uint32_t);
    return builder.create_view(statsUavDesc, m_statsUav);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(m_statsBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::UnorderedAccess);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }
    const uint32_t clearValues[4] = {0u, 0u, 0u, 0u};
    commandContext->clear_unordered_access_uint(m_statsUav, clearValues);
  }

private:
  uint32_t m_bufferCount = 1u;
  uint32_t m_hizWidth = 1u;
  uint32_t m_hizHeight = 1u;
  RHI::TextureHandle m_hizTexture{};
  RHI::ViewHandle m_hizSrv{};
  RHI::ViewHandle m_hizUav{};
  RHI::BufferHandle m_statsBuffer{};
  RHI::ViewHandle m_statsUav{};
};

class ChunkOcclusionStatsReadbackPass final : public RHI::FrameGraphPass {
public:
  ChunkOcclusionStatsReadbackPass(
      RHI::IBufferManager *bufferManager,
      GpuData::MeshletChunkVisibilityStatsGpu *statsOutput)
      : m_bufferManager(bufferManager), m_statsOutput(statsOutput) {}

  const char *name() const noexcept override {
    return "ChunkOcclusionStatsReadback";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result =
        builder.get_buffer("ChunkOcclusionStatsBuffer", m_statsBuffer);
    if (!result) {
      return result;
    }
    if (m_bufferManager == nullptr) {
      return Result::fail(Code::InvalidState, Severity::Error,
                          "ChunkOcclusionStatsReadbackPass requires a buffer "
                          "manager.");
    }
    return m_bufferManager->get_readback_buffer_view(m_statsBuffer,
                                                     m_readbackView);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(m_statsBuffer, RHI::ResourceAccessType::Read,
                              RHI::ResourceState::CopySource,
                              RHI::ResourceState::UnorderedAccess);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    update_stats_from_readback(context.frame_index());

    RHI::BufferToReadbackCopyRegion copyRegion{};
    copyRegion.srcBufferHandle = m_statsBuffer;
    copyRegion.srcDefaultResourceIndex = 0u;
    copyRegion.srcByteOffset = 0u;
    copyRegion.dstBufferHandle = m_statsBuffer;
    copyRegion.dstReadbackResourceIndex = context.frame_index();
    copyRegion.dstByteOffset = 0u;
    copyRegion.byteSize = 96u;
    commandContext->copy_buffer_region_to_readback(copyRegion);
  }

private:
  void update_stats_from_readback(uint32_t frameIndex) noexcept {
    if (m_statsOutput == nullptr ||
        frameIndex >= m_readbackView.mappedDatas.size()) {
      return;
    }
    const std::byte *mappedData = m_readbackView.mappedDatas[frameIndex];
    if (mappedData == nullptr) {
      return;
    }

    const uint32_t *values = reinterpret_cast<const uint32_t *>(mappedData);
    m_statsOutput->occlusionEnabled = values[21u];
    m_statsOutput->occlusionTestedCount = values[22u];
    m_statsOutput->occlusionRejectedCount = values[23u];
  }

  RHI::IBufferManager *m_bufferManager = nullptr;
  GpuData::MeshletChunkVisibilityStatsGpu *m_statsOutput = nullptr;
  RHI::BufferHandle m_statsBuffer{};
  RHI::ReadbackBufferView m_readbackView{};
};

class ChunkHiZBuildPass final : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override { return "ChunkHiZBuild"; }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_texture("ChunkDepth.Texture", m_depthTexture);
    if (!result) {
      return result;
    }
    result = builder.get_view("ChunkDepth.SRV", m_depthSrv);
    if (!result) {
      return result;
    }
    result = builder.get_texture("ChunkHiZ.Texture", m_hizTexture);
    if (!result) {
      return result;
    }
    result = builder.get_view("ChunkHiZ.UAV", m_hizUav);
    if (!result) {
      return result;
    }

    m_sourceWidth = builder.width();
    m_sourceHeight = builder.height();
    m_hizWidth = std::max(1u, builder.width() / 4u);
    m_hizHeight = std::max(1u, builder.height() / 4u);

    RHI::RootSignatureDesc rootDesc{};
    rootDesc.name = "ChunkHiZBuildRootSignature";
    rootDesc.parameters.push_back({RHI::RootParameterType::DescriptorTableSRV,
                                   RHI::ShaderVisibility::All, 0});
    rootDesc.parameters.push_back({RHI::RootParameterType::DescriptorTableUAV,
                                   RHI::ShaderVisibility::All, 0});
    for (uint32_t i = 0; i < 4u; ++i) {
      rootDesc.parameters.push_back({RHI::RootParameterType::_32BitConstants,
                                     RHI::ShaderVisibility::All, i});
    }
    result = builder.create_root_signature(rootDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "ChunkHiZBuildCS";
    shaderDesc.filePath = "Shaders/D3D12/BuildChunkHiZ.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_shader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "ChunkHiZBuildPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_shader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_texture(
        m_depthTexture, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    return builder.use_texture(m_hizTexture, RHI::ResourceAccessType::Write,
                               RHI::ResourceState::UnorderedAccess,
                               RHI::ResourceState::ShaderResource);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }
    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_compute_descriptor_table(0, m_depthSrv);
    commandContext->set_compute_descriptor_table(1, m_hizUav);
    commandContext->set_32bit_constant(2, m_sourceWidth);
    commandContext->set_32bit_constant(3, m_sourceHeight);
    commandContext->set_32bit_constant(4, m_hizWidth);
    commandContext->set_32bit_constant(5, m_hizHeight);
    commandContext->dispatch((m_hizWidth + 7u) / 8u,
                             (m_hizHeight + 7u) / 8u, 1u);
  }

private:
  RHI::TextureHandle m_depthTexture{};
  RHI::ViewHandle m_depthSrv{};
  RHI::TextureHandle m_hizTexture{};
  RHI::ViewHandle m_hizUav{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_shader{};
  RHI::PipelineStateHandle m_pipeline{};
  uint32_t m_sourceWidth = 1u;
  uint32_t m_sourceHeight = 1u;
  uint32_t m_hizWidth = 1u;
  uint32_t m_hizHeight = 1u;
};

class BuildChunkDepthCommandsPass final : public RHI::FrameGraphPass {
public:
  BuildChunkDepthCommandsPass(
      const DrawFrameState &drawFrameState, RHI::IBufferManager *bufferManager,
      GpuData::MeshletChunkVisibilityStatsGpu *statsOutput,
      RHI::BufferHandle renderObjectBuffer, RHI::BufferHandle transformBuffer,
      RHI::BufferHandle viewProjectionBuffer,
      RHI::BufferHandle visibleObjectCountBuffer, uint32_t maxObjectCount,
      uint32_t maxCommandCount, uint32_t maxChunkCount)
      : m_drawFrameState(drawFrameState), m_bufferManager(bufferManager),
        m_statsOutput(statsOutput), m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_maxObjectCount(maxObjectCount), m_maxCommandCount(maxCommandCount),
        m_maxInstanceCount(maxCommandCount),
        m_visibilityWordCount(
            MeshletChunkVisibility::word_count_for_chunks(maxChunkCount)) {}

  const char *name() const noexcept override {
    return "BuildChunkDepthCommands";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    if (m_visibilityWordCount == 0u) {
      m_visibilityWordCount = 1u;
    }
    if (m_maxCommandCount == 0u) {
      m_maxCommandCount = 1u;
      m_maxInstanceCount = 1u;
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
    result =
        builder.get_buffer("MeshPool.MeshChunkRange", m_meshChunkRangeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletChunk", m_meshletChunkBuffer);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("ChunkVisibility.Bits0", m_visibilityBitBuffers[0]);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("ChunkVisibility.Bits1", m_visibilityBitBuffers[1]);
    if (!result) {
      return result;
    }

    RHI::BufferDesc commandDesc{};
    commandDesc.name = "ChunkDepthCommandBuffer";
    commandDesc.type = RHI::BufferType::UnorderedAccess;
    commandDesc.defaultHeapCount = 1;
    commandDesc.uploadHeapCount = 0;
    commandDesc.initialState = RHI::ResourceState::UnorderedAccess;
    commandDesc.stride = sizeof(GpuData::IndirectCommand);
    commandDesc.elementCount = m_maxCommandCount;
    commandDesc.size = commandDesc.stride * commandDesc.elementCount;
    commandDesc.alignment = alignof(GpuData::IndirectCommand);
    result = builder.create_buffer(commandDesc, m_commandBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc counterDesc{};
    counterDesc.name = "ChunkDepthCommandCountBuffer";
    counterDesc.type = RHI::BufferType::Raw;
    counterDesc.defaultHeapCount = 1;
    counterDesc.uploadHeapCount = 0;
    counterDesc.initialState = RHI::ResourceState::UnorderedAccess;
    counterDesc.stride = sizeof(uint32_t);
    counterDesc.elementCount = 2u;
    counterDesc.size = counterDesc.stride * counterDesc.elementCount;
    counterDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(counterDesc, m_counterBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc counterUavDesc{};
    counterUavDesc.name = "ChunkDepthCommandCountBufferUAV";
    counterUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    counterUavDesc.bufferKind = RHI::BufferKind::Buffer;
    counterUavDesc.bufferHandle = m_counterBuffer;
    counterUavDesc.numElements = counterDesc.size / sizeof(uint32_t);
    result = builder.create_view(counterUavDesc, m_counterUav);
    if (!result) {
      return result;
    }

    RHI::BufferDesc instanceDesc{};
    instanceDesc.name = "ChunkDepthInstanceList";
    instanceDesc.type = RHI::BufferType::UnorderedAccess;
    instanceDesc.defaultHeapCount = 1;
    instanceDesc.uploadHeapCount = 0;
    instanceDesc.initialState = RHI::ResourceState::UnorderedAccess;
    instanceDesc.stride = sizeof(uint32_t);
    instanceDesc.elementCount = m_maxInstanceCount;
    instanceDesc.size = instanceDesc.stride * instanceDesc.elementCount;
    instanceDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(instanceDesc, m_instanceListBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc statsDesc{};
    statsDesc.name = "ChunkDepthCommandStatsBuffer";
    statsDesc.type = RHI::BufferType::Raw;
    statsDesc.defaultHeapCount = 1;
    statsDesc.uploadHeapCount = 0;
    statsDesc.readbackHeapCount = builder.buffer_count();
    statsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    statsDesc.stride = sizeof(GpuData::MeshletChunkVisibilityStatsGpu);
    statsDesc.elementCount = 1u;
    statsDesc.size = statsDesc.stride * statsDesc.elementCount;
    statsDesc.alignment = alignof(GpuData::MeshletChunkVisibilityStatsGpu);
    result = builder.create_buffer(statsDesc, m_statsBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc statsUavDesc{};
    statsUavDesc.name = "ChunkDepthCommandStatsBufferUAV";
    statsUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    statsUavDesc.bufferKind = RHI::BufferKind::Buffer;
    statsUavDesc.bufferHandle = m_statsBuffer;
    statsUavDesc.numElements = statsDesc.size / sizeof(uint32_t);
    result = builder.create_view(statsUavDesc, m_statsUav);
    if (!result) {
      return result;
    }

    if (m_bufferManager == nullptr) {
      return Result::fail(Code::InvalidState, Severity::Error,
                          "BuildChunkDepthCommandsPass requires a buffer "
                          "manager for stats readback.");
    }
    result = m_bufferManager->get_readback_buffer_view(m_statsBuffer,
                                                       m_statsReadbackView);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc buildRootDesc{};
    buildRootDesc.name = "BuildChunkDepthCommandsRootSignature";
    for (uint32_t constantIndex = 0; constantIndex < 4u; ++constantIndex) {
      buildRootDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
           constantIndex});
    }
    buildRootDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 4});
    for (uint32_t srvIndex = 0; srvIndex < 5u; ++srvIndex) {
      buildRootDesc.parameters.push_back(
          {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, srvIndex});
    }
    for (uint32_t uavIndex = 0; uavIndex < 6u; ++uavIndex) {
      buildRootDesc.parameters.push_back(
          {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, uavIndex});
    }
    buildRootDesc.parameters.push_back({RHI::RootParameterType::_32BitConstants,
                                        RHI::ShaderVisibility::All, 5});
    buildRootDesc.parameters.push_back({RHI::RootParameterType::_32BitConstants,
                                        RHI::ShaderVisibility::All, 6});
    buildRootDesc.parameters.push_back({RHI::RootParameterType::_32BitConstants,
                                        RHI::ShaderVisibility::All, 7});
    buildRootDesc.parameters.push_back({RHI::RootParameterType::_32BitConstants,
                                        RHI::ShaderVisibility::All, 8});
    buildRootDesc.parameters.push_back({RHI::RootParameterType::_32BitConstants,
                                        RHI::ShaderVisibility::All, 9});
    buildRootDesc.parameters.push_back({RHI::RootParameterType::_32BitConstants,
                                        RHI::ShaderVisibility::All, 10});
    result = builder.create_root_signature(buildRootDesc, m_buildRootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc buildShaderDesc{};
    buildShaderDesc.name = "BuildChunkDepthCommandsCS";
    buildShaderDesc.filePath = "Shaders/D3D12/BuildChunkDepthCommands.hlsl";
    buildShaderDesc.entryPoint = "CSMain";
    buildShaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(buildShaderDesc, m_buildShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc buildPipelineDesc{};
    buildPipelineDesc.name = "BuildChunkDepthCommandsPipeline";
    buildPipelineDesc.rootSignatureHandle = m_buildRootSignature;
    buildPipelineDesc.csHandle = m_buildShader;
    result =
        builder.create_compute_pipeline(buildPipelineDesc, m_buildPipeline);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc statsRootDesc{};
    statsRootDesc.name = "ChunkDepthCommandStatsRootSignature";
    statsRootDesc.parameters.push_back({RHI::RootParameterType::_32BitConstants,
                                        RHI::ShaderVisibility::All, 0});
    for (uint32_t uavIndex = 0; uavIndex < 4u; ++uavIndex) {
      statsRootDesc.parameters.push_back(
          {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, uavIndex});
    }
    result = builder.create_root_signature(statsRootDesc, m_statsRootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc statsShaderDesc{};
    statsShaderDesc.name = "ChunkDepthCommandStatsCS";
    statsShaderDesc.filePath = "Shaders/D3D12/ChunkDepthCommandStats.hlsl";
    statsShaderDesc.entryPoint = "CSMain";
    statsShaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(statsShaderDesc, m_statsShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc statsPipelineDesc{};
    statsPipelineDesc.name = "ChunkDepthCommandStatsPipeline";
    statsPipelineDesc.rootSignatureHandle = m_statsRootSignature;
    statsPipelineDesc.csHandle = m_statsShader;
    return builder.create_compute_pipeline(statsPipelineDesc, m_statsPipeline);
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
    for (RHI::BufferHandle bitBuffer : m_visibilityBitBuffers) {
      result = builder.use_buffer(bitBuffer, RHI::ResourceAccessType::Write,
                                  RHI::ResourceState::UnorderedAccess,
                                  RHI::ResourceState::Common);
      if (!result) {
        return result;
      }
    }
    result = builder.use_buffer(m_commandBuffer, RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::UnorderedAccess);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_counterBuffer, RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::UnorderedAccess);
    if (!result) {
      return result;
    }
    result =
        builder.use_buffer(m_instanceListBuffer, RHI::ResourceAccessType::Write,
                           RHI::ResourceState::UnorderedAccess,
                           RHI::ResourceState::UnorderedAccess);
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

    const uint32_t clearValues[4] = {0u, 0u, 0u, 0u};
    commandContext->clear_unordered_access_uint(m_counterUav, clearValues);
    commandContext->clear_unordered_access_uint(m_statsUav, clearValues);

    const uint32_t currentIndex = context.frame_index() & 1u;
    const uint32_t previousIndex = currentIndex ^ 1u;
    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());

    commandContext->set_compute_pipeline(m_buildPipeline);
    commandContext->set_32bit_constant(0, frameState.objectCount);
    commandContext->set_32bit_constant(1, m_maxCommandCount);
    commandContext->set_32bit_constant(2, m_maxInstanceCount);
    commandContext->set_32bit_constant(3, m_seedVisibility ? 1u : 0u);
    commandContext->set_cbv(4, m_viewProjectionBuffer);
    commandContext->set_srv(5, m_renderObjectBuffer);
    commandContext->set_srv(6, m_transformBuffer);
    commandContext->set_srv(7, m_visibleObjectCountBuffer);
    commandContext->set_srv(8, m_meshChunkRangeBuffer);
    commandContext->set_srv(9, m_meshletChunkBuffer);
    commandContext->set_uav(10, m_commandBuffer);
    commandContext->set_uav(11, m_counterBuffer);
    commandContext->set_uav(12, m_instanceListBuffer);
    commandContext->set_uav(13, m_visibilityBitBuffers[currentIndex]);
    commandContext->set_uav(14, m_visibilityBitBuffers[previousIndex]);
    commandContext->set_uav(15, m_statsBuffer);
    commandContext->set_32bit_constant(
        16, float_to_uint32(k_occluderMinScreenRadiusPx));
    commandContext->set_32bit_constant(17,
                                       float_to_uint32(k_occluderMaxViewDepth));
    commandContext->set_32bit_constant(18, frameState.renderHeight);
    commandContext->set_32bit_constant(19, k_occluderMaxDepthBin);
    commandContext->set_32bit_constant(
        20, float_to_uint32(k_occluderMinObjectScreenRadiusPx));
    commandContext->set_32bit_constant(21, k_maxChunksPerObject);
    if (frameState.objectCount > 0u) {
      commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1u, 1u);
    }
    m_seedVisibility = false;

    // The stats shader reads UAV-written counters/bitsets in the same resource
    // state, so an explicit UAV barrier is sufficient here.
    commandContext->uav_barrier(m_counterBuffer);
    commandContext->uav_barrier(m_visibilityBitBuffers[currentIndex]);
    commandContext->uav_barrier(m_statsBuffer);

    commandContext->set_compute_pipeline(m_statsPipeline);
    commandContext->set_32bit_constant(0, m_visibilityWordCount);
    commandContext->set_uav(1, m_counterBuffer);
    commandContext->set_uav(2, m_visibilityBitBuffers[currentIndex]);
    commandContext->set_uav(3, m_visibilityBitBuffers[previousIndex]);
    commandContext->set_uav(4, m_statsBuffer);
    commandContext->dispatch((m_visibilityWordCount + 63u) / 64u, 1u, 1u);

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
    statsCopyRegion.byteSize = sizeof(GpuData::MeshletChunkVisibilityStatsGpu);
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

    GpuData::MeshletChunkVisibilityStatsGpu stats{};
    std::memcpy(&stats, mappedData, sizeof(stats));
    *m_statsOutput = stats;
  }

  const DrawFrameState &m_drawFrameState;
  RHI::IBufferManager *m_bufferManager = nullptr;
  GpuData::MeshletChunkVisibilityStatsGpu *m_statsOutput = nullptr;
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  RHI::BufferHandle m_meshChunkRangeBuffer{};
  RHI::BufferHandle m_meshletChunkBuffer{};
  std::array<RHI::BufferHandle, 2> m_visibilityBitBuffers{};
  RHI::BufferHandle m_commandBuffer{};
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_instanceListBuffer{};
  RHI::BufferHandle m_statsBuffer{};
  RHI::ViewHandle m_counterUav{};
  RHI::ViewHandle m_statsUav{};
  RHI::ReadbackBufferView m_statsReadbackView{};
  RHI::RootSignatureHandle m_buildRootSignature{};
  RHI::RootSignatureHandle m_statsRootSignature{};
  RHI::ShaderBlobHandle m_buildShader{};
  RHI::ShaderBlobHandle m_statsShader{};
  RHI::PipelineStateHandle m_buildPipeline{};
  RHI::PipelineStateHandle m_statsPipeline{};
  uint32_t m_maxObjectCount = 0;
  uint32_t m_maxCommandCount = 1u;
  uint32_t m_maxInstanceCount = 1u;
  uint32_t m_visibilityWordCount = 1u;
  bool m_seedVisibility = true;

  static constexpr float k_occluderMinScreenRadiusPx = 24.0f;
  static constexpr float k_occluderMinObjectScreenRadiusPx = 0.0f;
  static constexpr float k_occluderMaxViewDepth = 80.0f;
  static constexpr uint32_t k_occluderMaxDepthBin = 7u;
  static constexpr uint32_t k_maxChunksPerObject = 4u;

  static uint32_t float_to_uint32(float value) noexcept {
    uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }
};

class ChunkDepthOnlyDrawPass final : public RHI::FrameGraphPass {
public:
  ChunkDepthOnlyDrawPass(const DrawFrameState &drawFrameState,
                         RHI::BufferHandle renderObjectBuffer,
                         RHI::BufferHandle transformBuffer,
                         RHI::BufferHandle viewProjectionBuffer,
                         uint32_t maxCommandCount)
      : m_drawFrameState(drawFrameState), m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_maxCommandCount(maxCommandCount) {}

  const char *name() const noexcept override { return "ChunkDepthOnlyDraw"; }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    if (m_maxCommandCount == 0u) {
      m_maxCommandCount = 1u;
    }

    RHI::TextureDesc depthDesc{};
    depthDesc.name = "ChunkDepth.Texture";
    depthDesc.kind = RHI::TextureKind::DepthStencil;
    depthDesc.width = builder.width();
    depthDesc.height = builder.height();
    depthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
    depthDesc.clearDepth = 1.0f;
    depthDesc.clearStencil = 0;
    Result result = builder.create_texture(depthDesc, m_depth);
    if (!result) {
      return result;
    }

    RHI::ViewDesc depthDsvDesc{};
    depthDsvDesc.name = "ChunkDepth.DSV";
    depthDsvDesc.type = RHI::ViewType::DepthStencil;
    depthDsvDesc.bufferKind = RHI::BufferKind::Texture;
    depthDsvDesc.textureHandle = m_depth;
    depthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
    result = builder.create_view(depthDsvDesc, m_depthDsv);
    if (!result) {
      return result;
    }

    RHI::ViewDesc depthSrvDesc{};
    depthSrvDesc.name = "ChunkDepth.SRV";
    depthSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
    depthSrvDesc.bufferKind = RHI::BufferKind::Texture;
    depthSrvDesc.textureHandle = m_depth;
    depthSrvDesc.colorFormat = RHI::ColorFormat::R24_UNorm_X8_Typeless;
    result = builder.create_view(depthSrvDesc, m_depthSrv);
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
    result = builder.get_buffer("MeshPool.RangeIndex", m_rangeIndexBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("ChunkDepthCommandBuffer", m_commandBuffer);
    if (!result) {
      return result;
    }
    result =
        builder.get_buffer("ChunkDepthCommandCountBuffer", m_commandCountBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("ChunkDepthInstanceList", m_instanceListBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "ChunkDepthOnlyRootSignature";
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
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc vsDesc{};
    vsDesc.name = "ChunkDepthOnlyVS";
    vsDesc.filePath = "Shaders/D3D12/ChunkDepthOnly.hlsl";
    vsDesc.entryPoint = "vs_main";
    vsDesc.targetProfile = "vs_6_0";
    result = builder.create_shader_blob(vsDesc, m_vertexShader);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc psDesc{};
    psDesc.name = "ChunkDepthOnlyPS";
    psDesc.filePath = "Shaders/D3D12/ChunkDepthOnly.hlsl";
    psDesc.entryPoint = "ps_main";
    psDesc.targetProfile = "ps_6_0";
    result = builder.create_shader_blob(psDesc, m_pixelShader);
    if (!result) {
      return result;
    }

    RHI::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "ChunkDepthOnlyPipeline";
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
    return builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_texture(m_depth, RHI::ResourceAccessType::Write,
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
        m_positionBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::VertexBuffer, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_rangeIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndexBuffer, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_commandBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_commandCountBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    return builder.use_buffer(
        m_instanceListBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->clear_depth_stencil(m_depthDsv, 1.0f, 0);
    commandContext->set_render_targets(nullptr, 0, m_depthDsv);
    commandContext->set_viewport_scissor(context.width(), context.height());
    commandContext->set_graphics_pipeline(m_pipeline);
    commandContext->set_primitive_topology(
        RHI::PrimitiveTopologyType::Triangle);
    commandContext->set_32bit_constant(0, 0xffffffffu);
    commandContext->set_cbv(1, m_viewProjectionBuffer);
    commandContext->set_srv(2, m_renderObjectBuffer);
    commandContext->set_srv(3, m_transformBuffer);
    commandContext->set_srv(4, m_instanceListBuffer);
    commandContext->set_vertex_buffer(0, m_positionBuffer);
    commandContext->set_index_buffer(m_rangeIndexBuffer,
                                     RHI::IndexFormat::UInt32);

    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (frameState.objectCount == 0u) {
      return;
    }
    commandContext->execute_indexed_indirect(m_commandBuffer,
                                             m_commandCountBuffer,
                                             m_maxCommandCount);
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::TextureHandle m_depth{};
  RHI::ViewHandle m_depthDsv{};
  RHI::ViewHandle m_depthSrv{};
  RHI::BufferHandle m_positionBuffer{};
  RHI::BufferHandle m_rangeIndexBuffer{};
  RHI::BufferHandle m_commandBuffer{};
  RHI::BufferHandle m_commandCountBuffer{};
  RHI::BufferHandle m_instanceListBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_vertexShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_pipeline{};
  uint32_t m_maxCommandCount = 1u;
};
} // namespace Cue::DrawSystem
