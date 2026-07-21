#pragma once

#include <algorithm>
#include <array>
#include <cstring>

#include <FrameGraph.h>
#include <IO/Logger.h>

#include "DrawSystem/MeshPool.h"
#include "DrawSystem/RenderDebugView.h"
#include "DrawSystem/RenderPath.h"
#include "GpuData/Batching.h"
#include "GpuData/Transform.h"
#include "GpuData/ViewProjection.h"

namespace Cue::DrawSystem {

namespace MeshShaderVisibility {
static constexpr uint32_t k_maxVisibleMeshletCount = 1024u * 1024u;
static constexpr uint32_t k_maxCandidateChunkCount = 256u * 1024u;
static constexpr uint32_t k_maxMeshCount = 4u * 1024u;
static constexpr uint32_t k_maxMeshletCount = 1024u * 1024u;
static constexpr uint32_t k_maxRangeIndexCount = 16u * 1024u * 1024u;
static constexpr uint32_t k_maxMeshletLocalIndexCount = k_maxRangeIndexCount;
static constexpr uint32_t k_maxMeshletLocalIndexPackedUintCount =
    (k_maxMeshletLocalIndexCount + 3u) / 4u;
static constexpr uint32_t k_maxMeshletVertexIndexCount =
    k_maxRangeIndexCount + k_maxMeshletCount * 2u;
static constexpr uint32_t k_maxPositionCount = 8u * 1024u * 1024u;
static constexpr uint32_t k_meshletsPerAmplificationGroup = 64u;

struct VisibleMeshlet final {
    uint32_t objectIndex = 0;
    uint32_t meshId = 0;
    uint32_t meshletIndex = 0;
    uint32_t segmentStartIndex = 0;
};

struct CandidateChunk final {
  uint32_t objectIndex = 0;
  uint32_t meshId = 0;
  uint32_t firstMeshlet = 0;
  uint32_t meshletCountAndSegmentCount = 0;
};

struct DispatchArgs final {
  uint32_t threadGroupCountX = 0;
  uint32_t threadGroupCountY = 0;
  uint32_t threadGroupCountZ = 0;
};
} // namespace MeshShaderVisibility

class MeshShaderVisibilityResetPass final : public RHI::FrameGraphPass {
public:
  explicit MeshShaderVisibilityResetPass(uint32_t bufferCount = 1u)
      : m_bufferCount(bufferCount) {}

  const char *name() const noexcept override {
    return "MeshShaderVisibilityReset";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    RHI::BufferDesc visibleDesc{};
    visibleDesc.name = "MeshShaderVisibility.VisibleMeshlets";
    visibleDesc.type = RHI::BufferType::UnorderedAccess;
    visibleDesc.defaultHeapCount = builder.buffer_count();
    visibleDesc.uploadHeapCount = 0;
    visibleDesc.initialState = RHI::ResourceState::UnorderedAccess;
    visibleDesc.stride = sizeof(MeshShaderVisibility::VisibleMeshlet);
    visibleDesc.elementCount =
        MeshShaderVisibility::k_maxVisibleMeshletCount;
    visibleDesc.size = static_cast<uint64_t>(visibleDesc.stride) *
                       static_cast<uint64_t>(visibleDesc.elementCount);
    visibleDesc.alignment = alignof(MeshShaderVisibility::VisibleMeshlet);
    Result result = builder.create_buffer(visibleDesc, m_visibleMeshletBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc visibleSrvDesc{};
    visibleSrvDesc.name = "MeshShaderVisibility.VisibleMeshletsSRV";
    visibleSrvDesc.type = RHI::ViewType::ShaderResourceRawBuffer;
    visibleSrvDesc.bufferKind = RHI::BufferKind::Buffer;
    visibleSrvDesc.bufferHandle = m_visibleMeshletBuffer;
    visibleSrvDesc.firstElement = 0;
    visibleSrvDesc.numElements =
        static_cast<uint32_t>(visibleDesc.size / sizeof(uint32_t));
    visibleSrvDesc.structureByteStride = 0;
    result = builder.create_view(visibleSrvDesc, m_visibleMeshletSrv);
    if (!result) {
      return result;
    }

    RHI::ViewDesc visibleStructuredSrvDesc{};
    visibleStructuredSrvDesc.name =
        "MeshShaderVisibility.VisibleMeshletsStructuredSRV";
    visibleStructuredSrvDesc.type = RHI::ViewType::ShaderResourceBuffer;
    visibleStructuredSrvDesc.bufferKind = RHI::BufferKind::Buffer;
    visibleStructuredSrvDesc.bufferHandle = m_visibleMeshletBuffer;
    visibleStructuredSrvDesc.firstElement = 0;
    visibleStructuredSrvDesc.numElements = visibleDesc.elementCount;
    visibleStructuredSrvDesc.structureByteStride = visibleDesc.stride;
    result = builder.create_view(visibleStructuredSrvDesc,
                                 m_visibleMeshletStructuredSrv);
    if (!result) {
      return result;
    }

    RHI::BufferDesc candidateDesc{};
    candidateDesc.name = "MeshShaderVisibility.CandidateChunks";
    candidateDesc.type = RHI::BufferType::UnorderedAccess;
    candidateDesc.defaultHeapCount = builder.buffer_count();
    candidateDesc.uploadHeapCount = 0;
    candidateDesc.initialState = RHI::ResourceState::UnorderedAccess;
    candidateDesc.stride = sizeof(MeshShaderVisibility::CandidateChunk);
    candidateDesc.elementCount =
        MeshShaderVisibility::k_maxCandidateChunkCount;
    candidateDesc.size = static_cast<uint64_t>(candidateDesc.stride) *
                         static_cast<uint64_t>(candidateDesc.elementCount);
    candidateDesc.alignment = alignof(MeshShaderVisibility::CandidateChunk);
    result = builder.create_buffer(candidateDesc, m_candidateChunkBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc counterDesc{};
    counterDesc.name = "MeshShaderVisibility.Counters";
    counterDesc.type = RHI::BufferType::Raw;
    counterDesc.defaultHeapCount = builder.buffer_count();
    counterDesc.uploadHeapCount = 0;
    counterDesc.readbackHeapCount = m_bufferCount;
    counterDesc.initialState = RHI::ResourceState::UnorderedAccess;
    counterDesc.stride = sizeof(uint32_t);
    counterDesc.elementCount = 8;
    counterDesc.size = sizeof(uint32_t) * counterDesc.elementCount;
    counterDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(counterDesc, m_counterBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc counterUavDesc{};
    counterUavDesc.name = "MeshShaderVisibility.CountersUAV";
    counterUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    counterUavDesc.bufferKind = RHI::BufferKind::Buffer;
    counterUavDesc.bufferHandle = m_counterBuffer;
    counterUavDesc.numElements = counterDesc.size / sizeof(uint32_t);
    result = builder.create_view(counterUavDesc, m_counterUav);
    if (!result) {
      return result;
    }

    RHI::ViewDesc counterSrvDesc{};
    counterSrvDesc.name = "MeshShaderVisibility.CountersSRV";
    counterSrvDesc.type = RHI::ViewType::ShaderResourceRawBuffer;
    counterSrvDesc.bufferKind = RHI::BufferKind::Buffer;
    counterSrvDesc.bufferHandle = m_counterBuffer;
    counterSrvDesc.numElements = counterDesc.size / sizeof(uint32_t);
    result = builder.create_view(counterSrvDesc, m_counterSrv);
    if (!result) {
      return result;
    }

    RHI::BufferDesc cullArgsDesc{};
    cullArgsDesc.name = "MeshShaderVisibility.CullDispatchArgs";
    cullArgsDesc.type = RHI::BufferType::UnorderedAccess;
    cullArgsDesc.defaultHeapCount = builder.buffer_count();
    cullArgsDesc.uploadHeapCount = 0;
    cullArgsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    cullArgsDesc.stride = sizeof(MeshShaderVisibility::DispatchArgs);
    cullArgsDesc.elementCount = 1;
    cullArgsDesc.size = cullArgsDesc.stride;
    cullArgsDesc.alignment = alignof(MeshShaderVisibility::DispatchArgs);
    result = builder.create_buffer(cullArgsDesc, m_cullDispatchArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc meshletCullArgsDesc{};
    meshletCullArgsDesc.name = "MeshShaderVisibility.MeshletCullDispatchArgs";
    meshletCullArgsDesc.type = RHI::BufferType::UnorderedAccess;
    meshletCullArgsDesc.defaultHeapCount = builder.buffer_count();
    meshletCullArgsDesc.uploadHeapCount = 0;
    meshletCullArgsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    meshletCullArgsDesc.stride = sizeof(MeshShaderVisibility::DispatchArgs);
    meshletCullArgsDesc.elementCount = 1;
    meshletCullArgsDesc.size = meshletCullArgsDesc.stride;
    meshletCullArgsDesc.alignment = alignof(MeshShaderVisibility::DispatchArgs);
    result =
        builder.create_buffer(meshletCullArgsDesc, m_meshletCullDispatchArgs);
    if (!result) {
      return result;
    }

    RHI::BufferDesc dispatchArgsDesc{};
    dispatchArgsDesc.name = "MeshShaderVisibility.DispatchArgs";
    dispatchArgsDesc.type = RHI::BufferType::UnorderedAccess;
    dispatchArgsDesc.defaultHeapCount = builder.buffer_count();
    dispatchArgsDesc.uploadHeapCount = 0;
    dispatchArgsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    dispatchArgsDesc.stride = sizeof(MeshShaderVisibility::DispatchArgs);
    dispatchArgsDesc.elementCount = 1;
    dispatchArgsDesc.size = dispatchArgsDesc.stride;
    dispatchArgsDesc.alignment = alignof(MeshShaderVisibility::DispatchArgs);
    return builder.create_buffer(dispatchArgsDesc, m_dispatchArgsBuffer);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(m_counterBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::UnorderedAccess);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const uint32_t clearValues[4] = {0, 0, 0, 0};
    commandContext->clear_unordered_access_uint(m_counterUav, clearValues);
    commandContext->uav_barrier(m_counterBuffer);
  }

private:
  uint32_t m_bufferCount = 1u;
  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_candidateChunkBuffer{};
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_cullDispatchArgsBuffer{};
  RHI::BufferHandle m_meshletCullDispatchArgs{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::ViewHandle m_visibleMeshletSrv{};
  RHI::ViewHandle m_visibleMeshletStructuredSrv{};
  RHI::ViewHandle m_counterUav{};
  RHI::ViewHandle m_counterSrv{};
};

class MeshShaderVisibilityCullDispatchArgsPass final
    : public RHI::FrameGraphPass {
public:
  MeshShaderVisibilityCullDispatchArgsPass(
      RHI::BufferHandle visibleObjectCountBuffer, uint32_t maxObjectCount)
      : m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_maxObjectCount(maxObjectCount) {}

  const char *name() const noexcept override {
    return "MeshShaderVisibilityCullDispatchArgs";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.read_buffer(m_visibleObjectCountBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.CullDispatchArgs",
                                m_cullDispatchArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name =
        "MeshShaderVisibilityCullDispatchArgsRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "MeshShaderVisibilityCullDispatchArgsCS";
    shaderDesc.filePath =
        "Shaders/D3D12/MeshShaderVisibilityCullDispatchArgs.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "MeshShaderVisibilityCullDispatchArgsPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_visibleObjectCountBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_cullDispatchArgsBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::IndirectArgument);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(0, m_maxObjectCount);
    commandContext->set_srv(1, m_visibleObjectCountBuffer);
    commandContext->set_uav(2, m_cullDispatchArgsBuffer);
    commandContext->dispatch(1, 1, 1);
  }

private:
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  RHI::BufferHandle m_cullDispatchArgsBuffer{};
  uint32_t m_maxObjectCount = 0;
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class MeshShaderVisibilityChunkCullPass final : public RHI::FrameGraphPass {
public:
  MeshShaderVisibilityChunkCullPass(RHI::BufferHandle renderObjectBuffer,
                                    RHI::BufferHandle transformBuffer,
                                    RHI::BufferHandle viewProjectionBuffer,
                                    RHI::BufferHandle visibleObjectCountBuffer,
                                    uint32_t maxObjectCount,
                                    uint32_t maxMeshletChunkCount,
                                    bool enableChunkFrustumCull,
                                    bool enableAdvancedLod,
                                    bool enableChunkHiZOcclusion)
      : m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_maxObjectCount(maxObjectCount),
        m_maxMeshletChunkCount(maxMeshletChunkCount),
        m_enableChunkFrustumCull(enableChunkFrustumCull),
        m_enableAdvancedLod(enableAdvancedLod),
        m_enableChunkHiZOcclusion(enableChunkHiZOcclusion) {}

  const char *name() const noexcept override {
    return "MeshShaderVisibilityChunkCull";
  }

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
    result =
        builder.get_buffer("MeshPool.MeshChunkRange", m_meshChunkRangeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletChunk", m_meshletChunkBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.CandidateChunks",
                                m_candidateChunkBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.Counters",
                                m_counterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.CullDispatchArgs",
                                m_cullDispatchArgsBuffer);
    if (!result) {
      return result;
    }
    for (uint32_t hizIndex = 0u; hizIndex < 2u; ++hizIndex) {
      const bool first = hizIndex == 0u;
      result = builder.get_texture(first ? "ChunkHiZ.Texture0"
                                         : "ChunkHiZ.Texture1",
                                   m_hizTextures[hizIndex]);
      if (!result) {
        return result;
      }
      result = builder.get_view(first ? "ChunkHiZ.SRV0" : "ChunkHiZ.SRV1",
                                m_hizSrvs[hizIndex]);
      if (!result) {
        return result;
      }
    }
    m_hizWidth = std::max(1u, builder.width() / 4u);
    m_hizHeight = std::max(1u, builder.height() / 4u);

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "MeshShaderVisibilityChunkCullRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         3});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         4});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         5});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         6});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         7});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         8});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         9});
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
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::DescriptorTableSRV,
         RHI::ShaderVisibility::All, 6});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         10});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         11});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         12});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "MeshShaderVisibilityChunkCullCS";
    shaderDesc.filePath = "Shaders/D3D12/MeshShaderVisibilityChunkCull.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "MeshShaderVisibilityChunkCullPipeline";
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
    result = builder.use_buffer(
        m_meshRangeBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_cullDispatchArgsBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument,
        RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_candidateChunkBuffer, RHI::ResourceAccessType::Write,
        RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_counterBuffer, RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    for (RHI::TextureHandle hizTexture : m_hizTextures) {
      result = builder.use_texture(hizTexture, RHI::ResourceAccessType::Read,
                                   RHI::ResourceState::ShaderResource,
                                   RHI::ResourceState::ShaderResource);
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

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(0, m_maxObjectCount);
    commandContext->set_32bit_constant(
        1, MeshShaderVisibility::k_maxCandidateChunkCount);
    commandContext->set_cbv(2, m_viewProjectionBuffer);
    commandContext->set_32bit_constant(3, MeshShaderVisibility::k_maxMeshCount);
    commandContext->set_32bit_constant(4, m_maxMeshletChunkCount);
    commandContext->set_32bit_constant(5, m_enableChunkFrustumCull ? 1u : 0u);
    commandContext->set_32bit_constant(6, m_enableAdvancedLod ? 1u : 0u);
    commandContext->set_32bit_constant(7, context.height());
    commandContext->set_32bit_constant(8, float_to_uint32(k_lod1RadiusPx));
    commandContext->set_32bit_constant(9, float_to_uint32(k_lod2RadiusPx));
    commandContext->set_srv(10, m_renderObjectBuffer);
    commandContext->set_srv(11, m_transformBuffer);
    commandContext->set_srv(12, m_visibleObjectCountBuffer);
    commandContext->set_srv(13, m_meshChunkRangeBuffer);
    commandContext->set_srv(14, m_meshletChunkBuffer);
    commandContext->set_srv(15, m_meshRangeBuffer);
    commandContext->set_uav(16, m_candidateChunkBuffer);
    commandContext->set_uav(17, m_counterBuffer);
    const uint32_t readHiZIndex = (context.frame_index() + 1u) & 1u;
    commandContext->set_compute_descriptor_table(18,
                                                 m_hizSrvs[readHiZIndex]);
    commandContext->set_32bit_constant(19, m_hizWidth);
    commandContext->set_32bit_constant(20, m_hizHeight);
    commandContext->set_32bit_constant(
        21, m_enableChunkHiZOcclusion && m_hasPreviousHiZ ? 1u : 0u);
    commandContext->execute_dispatch_indirect(m_cullDispatchArgsBuffer);
    m_hasPreviousHiZ = true;
  }

private:
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  uint32_t m_maxObjectCount = 0;
  uint32_t m_maxMeshletChunkCount = 0;
  bool m_enableChunkFrustumCull = true;
  bool m_enableAdvancedLod = true;
  bool m_enableChunkHiZOcclusion = true;
  bool m_hasPreviousHiZ = false;
  RHI::BufferHandle m_meshChunkRangeBuffer{};
  RHI::BufferHandle m_meshletChunkBuffer{};
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_candidateChunkBuffer{};
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_cullDispatchArgsBuffer{};
  std::array<RHI::TextureHandle, 2> m_hizTextures{};
  std::array<RHI::ViewHandle, 2> m_hizSrvs{};
  uint32_t m_hizWidth = 1u;
  uint32_t m_hizHeight = 1u;
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};

  static constexpr float k_lod1RadiusPx = 42.0f;
  static constexpr float k_lod2RadiusPx = 18.0f;

  static uint32_t float_to_uint32(float value) noexcept {
    uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }
};

class MeshShaderVisibilityMeshletCullDispatchArgsPass final
    : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "MeshShaderVisibilityMeshletCullDispatchArgs";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("MeshShaderVisibility.Counters",
                                       m_counterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.MeshletCullDispatchArgs",
                                m_meshletCullDispatchArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name =
        "MeshShaderVisibilityMeshletCullDispatchArgsRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "MeshShaderVisibilityMeshletCullDispatchArgsCS";
    shaderDesc.filePath =
        "Shaders/D3D12/MeshShaderVisibilityMeshletCullDispatchArgs.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name =
        "MeshShaderVisibilityMeshletCullDispatchArgsPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_counterBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_meshletCullDispatchArgsBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::IndirectArgument);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(
        0, MeshShaderVisibility::k_maxCandidateChunkCount);
    commandContext->set_srv(1, m_counterBuffer);
    commandContext->set_uav(2, m_meshletCullDispatchArgsBuffer);
    commandContext->dispatch(1, 1, 1);
  }

private:
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_meshletCullDispatchArgsBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class MeshShaderVisibilityCullPass final : public RHI::FrameGraphPass {
public:
  MeshShaderVisibilityCullPass(RHI::BufferHandle renderObjectBuffer,
                               RHI::BufferHandle transformBuffer,
                               RHI::BufferHandle viewProjectionBuffer,
                               bool enableHiZOcclusion)
      : m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_enableHiZOcclusion(enableHiZOcclusion) {}

  const char *name() const noexcept override {
    return "MeshShaderVisibilityCull";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("MeshShaderVisibility.CandidateChunks",
                                       m_candidateChunkBuffer);
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
    result = builder.get_buffer("MeshShaderVisibility.VisibleMeshlets",
                                m_visibleMeshletBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.Counters",
                                m_counterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.MeshletCullDispatchArgs",
                                m_meshletCullDispatchArgsBuffer);
    if (!result) {
      return result;
    }
    for (uint32_t hizIndex = 0u; hizIndex < 2u; ++hizIndex) {
      const bool first = hizIndex == 0u;
      result = builder.get_texture(first ? "ChunkHiZ.Texture0"
                                         : "ChunkHiZ.Texture1",
                                   m_hizTextures[hizIndex]);
      if (!result) {
        return result;
      }
      result = builder.get_view(first ? "ChunkHiZ.SRV0" : "ChunkHiZ.SRV1",
                                m_hizSrvs[hizIndex]);
      if (!result) {
        return result;
      }
    }
    m_hizWidth = std::max(1u, builder.width() / 4u);
    m_hizHeight = std::max(1u, builder.height() / 4u);

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "MeshShaderVisibilityCullRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 4});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::DescriptorTableSRV,
         RHI::ShaderVisibility::All, 5});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         3});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         4});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "MeshShaderVisibilityCullCS";
    shaderDesc.filePath = "Shaders/D3D12/MeshShaderVisibilityCull.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "MeshShaderVisibilityCullPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_candidateChunkBuffer, RHI::ResourceAccessType::Read,
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
        m_meshletCullDispatchArgsBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument,
        RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_visibleMeshletBuffer, RHI::ResourceAccessType::Write,
        RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_counterBuffer, RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    for (RHI::TextureHandle hizTexture : m_hizTextures) {
      result = builder.use_texture(hizTexture, RHI::ResourceAccessType::Read,
                                   RHI::ResourceState::ShaderResource,
                                   RHI::ResourceState::ShaderResource);
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

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(
        0, MeshShaderVisibility::k_maxVisibleMeshletCount);
    commandContext->set_srv(1, m_candidateChunkBuffer);
    commandContext->set_srv(2, m_meshRangeBuffer);
    commandContext->set_srv(3, m_meshletBoundsBuffer);
    commandContext->set_cbv(4, m_viewProjectionBuffer);
    commandContext->set_srv(5, m_renderObjectBuffer);
    commandContext->set_srv(6, m_transformBuffer);
    commandContext->set_uav(7, m_visibleMeshletBuffer);
    commandContext->set_uav(8, m_counterBuffer);
    const uint32_t readHiZIndex = (context.frame_index() + 1u) & 1u;
    commandContext->set_compute_descriptor_table(9, m_hizSrvs[readHiZIndex]);
    commandContext->set_32bit_constant(10, m_hizWidth);
    commandContext->set_32bit_constant(11, m_hizHeight);
    commandContext->set_32bit_constant(
        12, m_enableHiZOcclusion && m_hasPreviousHiZ ? 1u : 0u);
    commandContext->execute_dispatch_indirect(m_meshletCullDispatchArgsBuffer);
    m_hasPreviousHiZ = true;
  }

private:
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_candidateChunkBuffer{};
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_meshletCullDispatchArgsBuffer{};
  std::array<RHI::TextureHandle, 2> m_hizTextures{};
  std::array<RHI::ViewHandle, 2> m_hizSrvs{};
  uint32_t m_hizWidth = 1u;
  uint32_t m_hizHeight = 1u;
  bool m_enableHiZOcclusion = true;
  bool m_hasPreviousHiZ = false;
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class MeshShaderVisibilityDispatchArgsPass final : public RHI::FrameGraphPass {
public:
  explicit MeshShaderVisibilityDispatchArgsPass(uint32_t meshletsPerGroup)
      : m_meshletsPerGroup(meshletsPerGroup == 0u ? 1u : meshletsPerGroup) {}

  const char *name() const noexcept override {
    return "MeshShaderVisibilityDispatchArgs";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("MeshShaderVisibility.Counters",
                                       m_counterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.DispatchArgs",
                                m_dispatchArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "MeshShaderVisibilityDispatchArgsRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "MeshShaderVisibilityDispatchArgsCS";
    shaderDesc.filePath =
        "Shaders/D3D12/MeshShaderVisibilityDispatchArgs.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "MeshShaderVisibilityDispatchArgsPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_counterBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::UnorderedAccess);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_dispatchArgsBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::IndirectArgument);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(
        0, MeshShaderVisibility::k_maxVisibleMeshletCount);
    commandContext->set_32bit_constant(1, m_meshletsPerGroup);
    commandContext->set_srv(2, m_counterBuffer);
    commandContext->set_uav(3, m_dispatchArgsBuffer);
    commandContext->dispatch(1, 1, 1);
  }

private:
  uint32_t m_meshletsPerGroup = 1u;
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class MeshShaderVisibilityStatsReadbackPass final
    : public RHI::FrameGraphPass {
public:
  explicit MeshShaderVisibilityStatsReadbackPass(
      RHI::IBufferManager *bufferManager, bool enableStatsReadback = true)
      : m_bufferManager(bufferManager),
        m_enableStatsReadback(enableStatsReadback) {}

  const char *name() const noexcept override {
    return "MeshShaderVisibilityStatsReadback";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("MeshShaderVisibility.Counters",
                                       m_counterBuffer);
    if (!result) {
      return result;
    }
    if (m_enableStatsReadback && m_bufferManager == nullptr) {
      return Result::fail(Code::InvalidState, Severity::Error,
                          "MeshShaderVisibilityStatsReadbackPass requires a "
                          "buffer manager.");
    }
    if (!m_enableStatsReadback) {
      return Result::ok();
    }
    return m_bufferManager->get_readback_buffer_view(m_counterBuffer,
                                                     m_readbackView);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(m_counterBuffer, RHI::ResourceAccessType::Read,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::UnorderedAccess);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    if (m_enableStatsReadback) {
      update_stats_from_readback(context.frame_index());
    }
    if (!m_enableStatsReadback || !should_copy_stats_to_readback()) {
      return;
    }

    commandContext->resource_barrier(
        m_counterBuffer,
        RHI::ResourceBarrierDesc{RHI::ResourceState::UnorderedAccess,
                                 RHI::ResourceState::CopySource});

    RHI::BufferToReadbackCopyRegion copyRegion{};
    copyRegion.srcBufferHandle = m_counterBuffer;
    copyRegion.srcDefaultResourceIndex = context.frame_index();
    copyRegion.srcByteOffset = 0u;
    copyRegion.dstBufferHandle = m_counterBuffer;
    copyRegion.dstReadbackResourceIndex = context.frame_index();
    copyRegion.dstByteOffset = 0u;
    copyRegion.byteSize = 32u;
    commandContext->copy_buffer_region_to_readback(copyRegion);

    commandContext->resource_barrier(
        m_counterBuffer,
        RHI::ResourceBarrierDesc{RHI::ResourceState::CopySource,
                                 RHI::ResourceState::UnorderedAccess});
  }

private:
  static constexpr uint64_t kStatsReadbackWarmupFrames = 8u;
  static constexpr uint64_t kStatsReadbackIntervalFrames = 30u;

  bool should_copy_stats_to_readback() noexcept {
    const uint64_t frameIndex = m_statsReadbackFrameCount++;
    return frameIndex < kStatsReadbackWarmupFrames ||
           (frameIndex % kStatsReadbackIntervalFrames) == 0u;
  }

  void update_stats_from_readback(uint32_t frameIndex) noexcept {
    if (frameIndex >= m_readbackView.mappedDatas.size()) {
      return;
    }
    const std::byte *mappedData = m_readbackView.mappedDatas[frameIndex];
    if (mappedData == nullptr) {
      return;
    }

    const uint32_t *values = reinterpret_cast<const uint32_t *>(mappedData);
    if ((m_logFrameCount++ % kStatsReadbackIntervalFrames) != 0u) {
      return;
    }

    const uint32_t visibleSegmentCount = values[0u];
    const uint32_t candidateChunkCount = values[1u];
    const uint32_t testedMeshletCount = values[2u];
    const uint32_t frustumRejectedMeshletCount = values[3u];
    const uint32_t coneRejectedMeshletCount = values[4u];
    const uint32_t occlusionTestedMeshletCount = values[5u];
    const uint32_t occlusionRejectedMeshletCount = values[6u];
    const uint32_t visibleMeshletCount = values[7u];
    const float occlusionRejectedPercent =
        occlusionTestedMeshletCount == 0u
            ? 0.0f
            : (100.0f * static_cast<float>(occlusionRejectedMeshletCount)) /
                  static_cast<float>(occlusionTestedMeshletCount);
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "[MeshShaderVisibilityStats] candidateChunks={} "
                  "testedMeshlets={} visibleMeshlets={} visibleSegments={} "
                  "frustumRejected={} coneRejected={} occlusionTested={} "
                  "occlusionRejected={} occlusionRejectedPct={:.2f}",
                  candidateChunkCount, testedMeshletCount, visibleMeshletCount,
                  visibleSegmentCount, frustumRejectedMeshletCount,
                  coneRejectedMeshletCount, occlusionTestedMeshletCount,
                  occlusionRejectedMeshletCount, occlusionRejectedPercent);
  }

  RHI::IBufferManager *m_bufferManager = nullptr;
  RHI::BufferHandle m_counterBuffer{};
  RHI::ReadbackBufferView m_readbackView{};
  uint64_t m_statsReadbackFrameCount = 0u;
  uint64_t m_logFrameCount = 0u;
  bool m_enableStatsReadback = true;
};

class MeshShaderVisibilityPass final : public RHI::FrameGraphPass {
public:
  MeshShaderVisibilityPass(const RenderPath &renderPath,
                           const RenderDebugView &debugView,
                           RHI::BufferHandle renderObjectBuffer,
                           RHI::BufferHandle transformBuffer,
                           RHI::BufferHandle viewProjectionBuffer,
                           uint32_t maxObjectCount,
                           bool drawEnabled = true,
                           bool useSegmentLocalRemap = false,
                           bool useAmplificationShader = false,
                           bool allowAmplificationShaderFallback = false,
                           bool useMinimalAmplificationShader = false,
                           bool usePayloadProbeAmplificationShader = false,
                           bool useLiteralPayloadProbeAmplificationShader =
                               false,
                           bool useFixedMeshShaderProbe = false,
                           bool depthOnly = false,
                           bool useExternalDepth = false,
                           bool enableAsHiZOcclusion = false,
                           bool forceDepthOnlyPixelShader = false)
      : m_renderPath(renderPath), m_debugView(debugView),
        m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_maxObjectCount(maxObjectCount),
        m_drawEnabled(drawEnabled),
        m_useSegmentLocalRemap(useSegmentLocalRemap),
        m_useAmplificationShader(useAmplificationShader),
        m_allowAmplificationShaderFallback(allowAmplificationShaderFallback),
        m_useMinimalAmplificationShader(useMinimalAmplificationShader),
        m_usePayloadProbeAmplificationShader(
            usePayloadProbeAmplificationShader),
        m_useLiteralPayloadProbeAmplificationShader(
            useLiteralPayloadProbeAmplificationShader),
        m_useFixedMeshShaderProbe(useFixedMeshShaderProbe),
        m_depthOnly(depthOnly), m_useExternalDepth(useExternalDepth),
        m_enableAsHiZOcclusion(enableAsHiZOcclusion),
        m_forceDepthOnlyPixelShader(forceDepthOnlyPixelShader) {}

  const char *name() const noexcept override {
    return m_depthOnly ? "MeshShaderDepthPrepass" : "MeshShaderVisibility";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    (void)a_frameIndex;
    if (m_depthOnly) {
      return m_renderPath == RenderPath::VisibilityBuffer ||
             m_debugView != RenderDebugView::Forward;
    }
    return m_renderPath == RenderPath::VisibilityBuffer ||
           m_debugView != RenderDebugView::Forward;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = Result::ok();
    {
      RHI::TextureDesc visibilityDesc{};
      visibilityDesc.name =
          m_depthOnly ? "MeshShaderDepthPrepass.DummyRT" : "VisibilityBuffer";
      visibilityDesc.kind = RHI::TextureKind::RenderTarget;
      visibilityDesc.width = builder.width();
      visibilityDesc.height = builder.height();
      visibilityDesc.format = RHI::ColorFormat::R32_UINT;
      visibilityDesc.clearColor[0] = 0.0f;
      visibilityDesc.clearColor[1] = 0.0f;
      visibilityDesc.clearColor[2] = 0.0f;
      visibilityDesc.clearColor[3] = 0.0f;
      result = builder.create_texture(visibilityDesc, m_visibility);
      if (!result) {
        return result;
      }

      if (!m_depthOnly) {
        result = builder.render(&m_visibility, 1);
        if (!result) {
          return result;
        }
      }

      RHI::ViewDesc visibilityRtvDesc{};
      visibilityRtvDesc.name =
          m_depthOnly ? "MeshShaderDepthPrepass.DummyRTV"
                      : "VisibilityBufferRTV";
      visibilityRtvDesc.type = RHI::ViewType::RenderTarget;
      visibilityRtvDesc.bufferKind = RHI::BufferKind::Texture;
      visibilityRtvDesc.textureHandle = m_visibility;
      visibilityRtvDesc.colorFormat = RHI::ColorFormat::R32_UINT;
      result = builder.create_view(visibilityRtvDesc, m_visibilityRtv);
      if (!result) {
        return result;
      }

      if (!m_depthOnly) {
        RHI::ViewDesc visibilitySrvDesc{};
        visibilitySrvDesc.name = "VisibilityBufferSRV";
        visibilitySrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
        visibilitySrvDesc.bufferKind = RHI::BufferKind::Texture;
        visibilitySrvDesc.textureHandle = m_visibility;
        visibilitySrvDesc.colorFormat = RHI::ColorFormat::R32_UINT;
        visibilitySrvDesc.mipLevels = 1;
        result = builder.create_view(visibilitySrvDesc, m_visibilitySrv);
        if (!result) {
          return result;
        }
      }
    }

    if (m_useExternalDepth) {
      result = builder.get_texture("VisibilityDepth", m_depth);
      if (!result) {
        return result;
      }
      result = builder.get_view("VisibilityDepthDSV", m_depthDsv);
      if (!result) {
        return result;
      }
      result = builder.get_view("VisibilityDepthSRV", m_depthSrv);
      if (!result) {
        return result;
      }
    } else {
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

      RHI::ViewDesc depthSrvDesc{};
      depthSrvDesc.name = "VisibilityDepthSRV";
      depthSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
      depthSrvDesc.bufferKind = RHI::BufferKind::Texture;
      depthSrvDesc.textureHandle = m_depth;
      depthSrvDesc.colorFormat = RHI::ColorFormat::R24_UNorm_X8_Typeless;
      result = builder.create_view(depthSrvDesc, m_depthSrv);
      if (!result) {
        return result;
      }
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
    result = builder.get_buffer("MeshShaderVisibility.VisibleMeshlets",
                                m_visibleMeshletBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_view("MeshShaderVisibility.VisibleMeshletsSRV",
                              m_visibleMeshletSrv);
    if (!result) {
      return result;
    }
    result = builder.get_view(
        "MeshShaderVisibility.VisibleMeshletsStructuredSRV",
        m_visibleMeshletStructuredSrv);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.DispatchArgs",
                                m_dispatchArgsBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.Counters",
                                m_counterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_view("MeshShaderVisibility.CountersSRV",
                              m_counterSrv);
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
    result = builder.get_buffer("MeshPool.RangeIndex", m_rangeIndexBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletLocalIndex",
                                m_meshletLocalIndexBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletVertexIndex",
                                m_meshletVertexIndexBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.Position", m_positionBuffer);
    if (!result) {
      return result;
    }

    auto createStructuredSrv =
        [&](const char *name, RHI::BufferHandle buffer, uint32_t elementCount,
            uint32_t stride, RHI::ViewHandle &outView) -> Result {
      RHI::ViewDesc desc{};
      desc.name = name;
      desc.type = RHI::ViewType::ShaderResourceBuffer;
      desc.bufferKind = RHI::BufferKind::Buffer;
      desc.bufferHandle = buffer;
      desc.firstElement = 0;
      desc.numElements = elementCount;
      desc.structureByteStride = stride;
      return builder.create_view(desc, outView);
    };
    auto createRawSrv = [&](const char *name, RHI::BufferHandle buffer,
                            uint32_t uintCount,
                            RHI::ViewHandle &outView) -> Result {
      RHI::ViewDesc desc{};
      desc.name = name;
      desc.type = RHI::ViewType::ShaderResourceRawBuffer;
      desc.bufferKind = RHI::BufferKind::Buffer;
      desc.bufferHandle = buffer;
      desc.firstElement = 0;
      desc.numElements = uintCount;
      desc.structureByteStride = 0;
      return builder.create_view(desc, outView);
    };

    RHI::ViewDesc viewProjectionCbvDesc{};
    viewProjectionCbvDesc.name = "MeshShaderVisibility.ViewProjectionCBV";
    viewProjectionCbvDesc.type = RHI::ViewType::ConstantBuffer;
    viewProjectionCbvDesc.bufferKind = RHI::BufferKind::Buffer;
    viewProjectionCbvDesc.bufferHandle = m_viewProjectionBuffer;
    viewProjectionCbvDesc.byteOffset = 0;
    viewProjectionCbvDesc.byteSize = sizeof(GpuData::ViewProjectionGpu);
    result = builder.create_view(viewProjectionCbvDesc, m_viewProjectionCbv);
    if (!result) {
      return result;
    }

    result = createStructuredSrv(
        "MeshShaderVisibility.RenderObjectsSRV", m_renderObjectBuffer,
        m_maxObjectCount, sizeof(GpuData::RenderObject), m_renderObjectSrv);
    if (!result) {
      return result;
    }
    result = createStructuredSrv(
        "MeshShaderVisibility.TransformsSRV", m_transformBuffer,
        m_maxObjectCount, sizeof(GpuData::ObjectTransformGpu), m_transformSrv);
    if (!result) {
      return result;
    }
    result = createStructuredSrv(
        "MeshShaderVisibility.MeshRangesSRV", m_meshRangeBuffer,
        MeshShaderVisibility::k_maxMeshCount, sizeof(MeshRange),
        m_meshRangeSrv);
    if (!result) {
      return result;
    }
    result = createStructuredSrv(
        "MeshShaderVisibility.MeshletBoundsSRV", m_meshletBoundsBuffer,
        MeshShaderVisibility::k_maxMeshletCount,
        sizeof(Core::Native::MeshletBounds), m_meshletBoundsSrv);
    if (!result) {
      return result;
    }
    result = createRawSrv("MeshShaderVisibility.RangeIndicesSRV",
                          m_rangeIndexBuffer,
                          MeshShaderVisibility::k_maxRangeIndexCount,
                          m_rangeIndexSrv);
    if (!result) {
      return result;
    }
    result = createRawSrv(
        "MeshShaderVisibility.MeshletLocalIndicesSRV",
        m_meshletLocalIndexBuffer,
        MeshShaderVisibility::k_maxMeshletLocalIndexPackedUintCount,
        m_meshletLocalIndexSrv);
    if (!result) {
      return result;
    }
    result = createRawSrv(
        "MeshShaderVisibility.MeshletVertexIndicesSRV",
        m_meshletVertexIndexBuffer,
        MeshShaderVisibility::k_maxMeshletVertexIndexCount,
        m_meshletVertexIndexSrv);
    if (!result) {
      return result;
    }
    result = createStructuredSrv(
        "MeshShaderVisibility.PositionsSRV", m_positionBuffer,
        MeshShaderVisibility::k_maxPositionCount, sizeof(Math::float4),
        m_positionSrv);
    if (!result) {
      return result;
    }
    for (uint32_t hizIndex = 0u; hizIndex < 2u; ++hizIndex) {
      const bool first = hizIndex == 0u;
      result = builder.get_texture(first ? "ChunkHiZ.Texture0"
                                         : "ChunkHiZ.Texture1",
                                   m_hizTextures[hizIndex]);
      if (!result) {
        return result;
      }
      result = builder.get_view(first ? "ChunkHiZ.SRV0" : "ChunkHiZ.SRV1",
                                m_hizSrvs[hizIndex]);
      if (!result) {
        return result;
      }
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "MeshShaderVisibilityRootSignature";
    const bool useAmplificationProbeRootSignature =
        false;
    if (useAmplificationProbeRootSignature) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::Amplification, 0});
    } else {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableCBV,
           RHI::ShaderVisibility::All, 0});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 0});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 1});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 2});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 3});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 4});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 5});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 6});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 7});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::All, 8});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::Amplification, 9});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::DescriptorTableSRV,
           RHI::ShaderVisibility::Amplification, 10});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants,
           RHI::ShaderVisibility::Amplification, 1});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants,
           RHI::ShaderVisibility::Amplification, 2});
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants,
           RHI::ShaderVisibility::Amplification, 3});
    }
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    if (m_useAmplificationShader) {
      RHI::ShaderCompileDesc asDesc{};
      asDesc.name = "MeshShaderVisibilityAS";
      asDesc.filePath = m_useMinimalAmplificationShader
                            ? "Shaders/D3D12/MeshShaderVisibilityASMinimal.hlsl"
                        : m_usePayloadProbeAmplificationShader ||
                                  m_useLiteralPayloadProbeAmplificationShader
                            ? "Shaders/D3D12/MeshShaderVisibilityASPayloadProbe.hlsl"
                        : m_useFixedMeshShaderProbe
                            ? "Shaders/D3D12/MeshShaderVisibilityASFixedProbe.hlsl"
                            : "Shaders/D3D12/MeshShaderVisibilityAS.hlsl";
      asDesc.entryPoint =
          m_useLiteralPayloadProbeAmplificationShader
              ? "as_main_literal_payload_probe"
          : m_usePayloadProbeAmplificationShader
              ? "as_main_payload_probe"
              : "as_main";
      asDesc.targetProfile = "as_6_5";
      result = builder.create_shader_blob(asDesc, m_amplificationShader);
      if (!result) {
        return result;
      }
    }

    RHI::ShaderCompileDesc msDesc{};
    msDesc.name = "MeshShaderVisibilityMS";
    msDesc.filePath =
        m_useAmplificationShader
            ? (m_useMinimalAmplificationShader
                   ? "Shaders/D3D12/MeshShaderVisibilityASMinimal.hlsl"
               : m_usePayloadProbeAmplificationShader ||
                     m_useLiteralPayloadProbeAmplificationShader
                   ? "Shaders/D3D12/MeshShaderVisibilityASPayloadProbe.hlsl"
               : m_useFixedMeshShaderProbe
                   ? "Shaders/D3D12/MeshShaderVisibilityASFixedProbe.hlsl"
                   : "Shaders/D3D12/MeshShaderVisibilityAS.hlsl")
            : "Shaders/D3D12/MeshShaderVisibility.hlsl";
    msDesc.entryPoint =
        m_useAmplificationShader
            ? (m_useMinimalAmplificationShader
                   ? "ms_main"
               : m_usePayloadProbeAmplificationShader ||
                     m_useLiteralPayloadProbeAmplificationShader
                   ? "ms_main_payload_probe"
               : m_useFixedMeshShaderProbe
                   ? "ms_main"
               : m_useSegmentLocalRemap
                   ? "ms_main_as_segment_remap"
                   : "ms_main_as")
            : (m_useSegmentLocalRemap ? "ms_main_segment_remap" : "ms_main");
    msDesc.targetProfile = "ms_6_5";
    result = builder.create_shader_blob(msDesc, m_meshShader);
    if (!result) {
      return result;
    }

    {
      RHI::ShaderCompileDesc psDesc{};
      psDesc.name = "MeshShaderVisibilityPS";
      psDesc.filePath =
          m_useMinimalAmplificationShader
              ? "Shaders/D3D12/MeshShaderVisibilityASMinimal.hlsl"
          : m_usePayloadProbeAmplificationShader ||
                m_useLiteralPayloadProbeAmplificationShader
              ? "Shaders/D3D12/MeshShaderVisibilityASPayloadProbe.hlsl"
          : m_useFixedMeshShaderProbe
              ? "Shaders/D3D12/MeshShaderVisibilityASFixedProbe.hlsl"
              : "Shaders/D3D12/MeshShaderVisibility.hlsl";
      psDesc.entryPoint =
          m_depthOnly || m_forceDepthOnlyPixelShader ? "ps_depth_only"
          : m_usePayloadProbeAmplificationShader ||
                  m_useLiteralPayloadProbeAmplificationShader
              ? "ps_main_payload_probe"
          : m_useFixedMeshShaderProbe
              ? "ps_main"
              : "ps_main";
      psDesc.targetProfile = "ps_6_5";
      result = builder.create_shader_blob(psDesc, m_pixelShader);
      if (!result) {
        return result;
      }
    }

    RHI::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "MeshShaderVisibilityPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.asHandle = m_amplificationShader;
    pipelineDesc.msHandle = m_meshShader;
    pipelineDesc.psHandle = m_pixelShader;
    pipelineDesc.rasterizerState.cullMode = RHI::CullMode::Back;
    pipelineDesc.depthStencilState.depthEnable = true;
    pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
    pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
    pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
    pipelineDesc.blendMode = {RHI::BlendMode::None};
    pipelineDesc.rtvFormats = {RHI::ColorFormat::R32_UINT};
    result = builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
    if (!result && m_useAmplificationShader &&
        !m_allowAmplificationShaderFallback) {
      Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                    "[MeshShader] AS pipeline creation failed; strict mode "
                    "keeps the initialization failure.");
      return result;
    }
    if (!result && m_useAmplificationShader &&
        m_allowAmplificationShaderFallback) {
      Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                    "[MeshShader] AS pipeline creation failed; falling back "
                    "to direct MS pipeline.");
      RHI::ShaderCompileDesc fallbackMsDesc{};
      fallbackMsDesc.name = "MeshShaderVisibilityFallbackMS";
      fallbackMsDesc.filePath = "Shaders/D3D12/MeshShaderVisibility.hlsl";
      fallbackMsDesc.entryPoint = "ms_main";
      fallbackMsDesc.targetProfile = "ms_6_5";
      result = builder.create_shader_blob(fallbackMsDesc, m_meshShader);
      if (!result) {
        return result;
      }

      pipelineDesc.name = "MeshShaderVisibilityFallbackPipeline";
      pipelineDesc.asHandle = {};
      pipelineDesc.msHandle = m_meshShader;
      result = builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
    }
    if (!result) {
      return result;
    }
    return result;
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = Result::ok();
    {
      result = builder.use_texture(
          m_visibility, RHI::ResourceAccessType::Write,
          RHI::ResourceState::RenderTarget,
          m_depthOnly ? RHI::ResourceState::Common
                      : RHI::ResourceState::ShaderResource);
      if (!result) {
        return result;
      }
    }
    result = builder.use_texture(m_depth, RHI::ResourceAccessType::Write,
                                 RHI::ResourceState::DepthWrite,
                                 RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
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
        m_visibleMeshletBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_counterBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::UnorderedAccess);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_dispatchArgsBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument,
        RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_meshRangeBuffer, RHI::ResourceAccessType::Read,
                                RHI::ResourceState::ShaderResource,
                                RHI::ResourceState::ShaderResource);
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
        m_rangeIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_meshletLocalIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_meshletVertexIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_positionBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    for (RHI::TextureHandle hizTexture : m_hizTextures) {
      result = builder.use_texture(hizTexture, RHI::ResourceAccessType::Read,
                                   RHI::ResourceState::ShaderResource,
                                   RHI::ResourceState::ShaderResource);
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

    if (m_depthOnly) {
      const float clearIds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      commandContext->clear_render_target(m_visibilityRtv, clearIds);
      commandContext->clear_depth_stencil(m_depthDsv, 1.0f, 0);
      commandContext->set_render_targets(&m_visibilityRtv, 1, m_depthDsv);
    } else {
      const float clearIds[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      commandContext->clear_render_target(m_visibilityRtv, clearIds);
      if (!m_useExternalDepth) {
        commandContext->clear_depth_stencil(m_depthDsv, 1.0f, 0);
      }
      commandContext->set_render_targets(&m_visibilityRtv, 1, m_depthDsv);
    }
    commandContext->set_viewport_scissor(context.width(), context.height());
    commandContext->set_graphics_pipeline(m_pipeline);
    if (m_usePayloadProbeAmplificationShader ||
        m_useLiteralPayloadProbeAmplificationShader) {
      commandContext->set_graphics_descriptor_table(1, m_visibleMeshletSrv);
      if (m_drawEnabled) {
        commandContext->execute_dispatch_mesh_indirect(m_dispatchArgsBuffer);
      }
      return;
    }
    commandContext->set_graphics_descriptor_table(0, m_viewProjectionCbv);
    commandContext->set_graphics_descriptor_table(1, m_renderObjectSrv);
    commandContext->set_graphics_descriptor_table(2, m_transformSrv);
    commandContext->set_graphics_descriptor_table(
        3, m_useAmplificationShader ? m_visibleMeshletSrv
                                    : m_visibleMeshletStructuredSrv);
    commandContext->set_graphics_descriptor_table(4, m_meshRangeSrv);
    commandContext->set_graphics_descriptor_table(5, m_meshletBoundsSrv);
    commandContext->set_graphics_descriptor_table(6, m_rangeIndexSrv);
    commandContext->set_graphics_descriptor_table(7, m_meshletLocalIndexSrv);
    commandContext->set_graphics_descriptor_table(8, m_meshletVertexIndexSrv);
    commandContext->set_graphics_descriptor_table(9, m_positionSrv);
    if (m_useAmplificationShader) {
      commandContext->set_graphics_descriptor_table(10, m_counterSrv);
      const uint32_t readHiZIndex = (context.frame_index() + 1u) & 1u;
      commandContext->set_graphics_descriptor_table(11,
                                                    m_hizSrvs[readHiZIndex]);
      commandContext->set_32bit_constant(12,
                                         std::max(1u, context.width() / 4u));
      commandContext->set_32bit_constant(13,
                                         std::max(1u, context.height() / 4u));
      commandContext->set_32bit_constant(
          14, m_enableAsHiZOcclusion && m_hasPreviousHiZ ? 1u : 0u);
    }
    if (m_drawEnabled) {
      commandContext->execute_dispatch_mesh_indirect(m_dispatchArgsBuffer);
    }
    if (m_useAmplificationShader) {
      m_hasPreviousHiZ = true;
    }
  }

private:
  const RenderPath &m_renderPath;
  const RenderDebugView &m_debugView;
  uint32_t m_maxObjectCount = 0;
  bool m_drawEnabled = true;
  bool m_useSegmentLocalRemap = false;
  bool m_useAmplificationShader = false;
  bool m_allowAmplificationShaderFallback = false;
  bool m_useMinimalAmplificationShader = false;
  bool m_usePayloadProbeAmplificationShader = false;
  bool m_useLiteralPayloadProbeAmplificationShader = false;
  bool m_useFixedMeshShaderProbe = false;
  bool m_depthOnly = false;
  bool m_useExternalDepth = false;
  bool m_enableAsHiZOcclusion = false;
  bool m_forceDepthOnlyPixelShader = false;
  bool m_hasPreviousHiZ = false;
  RHI::TextureHandle m_visibility{};
  RHI::TextureHandle m_depth{};
  RHI::ViewHandle m_visibilityRtv{};
  RHI::ViewHandle m_visibilitySrv{};
  RHI::ViewHandle m_depthDsv{};
  RHI::ViewHandle m_depthSrv{};
  RHI::ViewHandle m_viewProjectionCbv{};
  RHI::ViewHandle m_renderObjectSrv{};
  RHI::ViewHandle m_transformSrv{};
  RHI::ViewHandle m_visibleMeshletSrv{};
  RHI::ViewHandle m_visibleMeshletStructuredSrv{};
  RHI::ViewHandle m_meshRangeSrv{};
  RHI::ViewHandle m_meshletBoundsSrv{};
  RHI::ViewHandle m_rangeIndexSrv{};
  RHI::ViewHandle m_meshletLocalIndexSrv{};
  RHI::ViewHandle m_meshletVertexIndexSrv{};
  RHI::ViewHandle m_positionSrv{};
  RHI::ViewHandle m_counterSrv{};
  std::array<RHI::TextureHandle, 2> m_hizTextures{};
  std::array<RHI::ViewHandle, 2> m_hizSrvs{};
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_rangeIndexBuffer{};
  RHI::BufferHandle m_meshletLocalIndexBuffer{};
  RHI::BufferHandle m_meshletVertexIndexBuffer{};
  RHI::BufferHandle m_positionBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_amplificationShader{};
  RHI::ShaderBlobHandle m_meshShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

} // namespace Cue::DrawSystem
