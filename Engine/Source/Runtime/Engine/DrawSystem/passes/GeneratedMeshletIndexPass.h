#pragma once

/// ****************************************************************************
/// 従来ラスターパイプライン向け GPU 生成 meshlet index 描画
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/RenderDebugView.h"
#include "DrawSystem/RenderPath.h"
#include "DrawSystem/passes/MeshShaderVisibilityPass.h"
#include "GpuData/Batching.h"

// === C++ includes ===
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>

namespace Cue::DrawSystem {
namespace GeneratedMeshletIndex {
static constexpr uint32_t k_maxVerticesPerMeshlet = 64u;
static constexpr uint32_t k_maxIndicesPerSegment = 384u;
static constexpr uint32_t k_indexCapacity =
    MeshShaderVisibility::k_maxRangeIndexCount;
static constexpr uint32_t k_segmentsPerPage =
    k_indexCapacity / k_maxIndicesPerSegment;
static constexpr uint32_t k_pageCount = 8u;
static constexpr uint32_t k_totalSegmentCapacity =
    k_segmentsPerPage * k_pageCount;
static constexpr uint32_t k_stateValueCount = 8u;

static_assert(k_segmentsPerPage * k_maxIndicesPerSegment <= k_indexCapacity);
static_assert(k_totalSegmentCapacity <=
              MeshShaderVisibility::k_maxVisibleMeshletCount);
static_assert(
    static_cast<uint64_t>(MeshShaderVisibility::k_maxVisibleMeshletCount) *
        k_maxVerticesPerMeshlet <=
    std::numeric_limits<uint32_t>::max());
} // namespace GeneratedMeshletIndex

class GeneratedMeshletIndexDispatchArgsPass final : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "GeneratedMeshletIndexDispatchArgs";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("MeshShaderVisibility.Counters",
                                       m_meshletCounterBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc dispatchArgsDesc{};
    dispatchArgsDesc.name = "GeneratedMeshletIndex.WorkDispatchArgs";
    dispatchArgsDesc.type = RHI::BufferType::UnorderedAccess;
    dispatchArgsDesc.defaultHeapCount = builder.buffer_count();
    dispatchArgsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    dispatchArgsDesc.stride = sizeof(MeshShaderVisibility::DispatchArgs);
    dispatchArgsDesc.elementCount = 1u;
    dispatchArgsDesc.size = dispatchArgsDesc.stride;
    dispatchArgsDesc.alignment = alignof(MeshShaderVisibility::DispatchArgs);
    result = builder.create_buffer(dispatchArgsDesc, m_dispatchArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletIndexDispatchArgsRootSignature";
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
    shaderDesc.name = "GeneratedMeshletIndexDispatchArgsCS";
    shaderDesc.filePath =
        "Shaders/D3D12/GeneratedMeshletIndexDispatchArgs.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletIndexDispatchArgsPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_meshletCounterBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::ShaderResource);
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
    commandContext->set_32bit_constant(
        1, MeshShaderVisibility::k_maxCandidateChunkCount);
    commandContext->set_srv(2, m_meshletCounterBuffer);
    commandContext->set_uav(3, m_dispatchArgsBuffer);
    commandContext->dispatch(1u, 1u, 1u);
  }

private:
  RHI::BufferHandle m_meshletCounterBuffer{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class GeneratedMeshletIndexClassifyPass final : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "GeneratedMeshletIndexClassify";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("MeshShaderVisibility.Counters",
                                       m_meshletCounterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.CandidateChunks",
                                m_candidateChunkBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletIndex.WorkDispatchArgs",
                                m_workDispatchArgsBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("ObjectDrawModeBuffer", m_objectDrawModeBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc pageArgsDesc{};
    pageArgsDesc.name = "GeneratedMeshletIndex.PageDispatchArgs";
    pageArgsDesc.type = RHI::BufferType::UnorderedAccess;
    pageArgsDesc.defaultHeapCount = builder.buffer_count();
    pageArgsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    pageArgsDesc.stride = sizeof(MeshShaderVisibility::DispatchArgs);
    pageArgsDesc.elementCount = GeneratedMeshletIndex::k_pageCount;
    pageArgsDesc.size = pageArgsDesc.stride * pageArgsDesc.elementCount;
    pageArgsDesc.alignment = alignof(MeshShaderVisibility::DispatchArgs);
    result = builder.create_buffer(pageArgsDesc, m_pageDispatchArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc stateDesc{};
    stateDesc.name = "GeneratedMeshletIndex.State";
    stateDesc.type = RHI::BufferType::Raw;
    stateDesc.defaultHeapCount = builder.buffer_count();
    stateDesc.readbackHeapCount = builder.buffer_count();
    stateDesc.initialState = RHI::ResourceState::UnorderedAccess;
    stateDesc.stride = sizeof(uint32_t);
    stateDesc.elementCount = GeneratedMeshletIndex::k_stateValueCount;
    stateDesc.size = stateDesc.stride * stateDesc.elementCount;
    stateDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(stateDesc, m_stateBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletIndexClassifyRootSignature";
    for (uint32_t shaderRegister = 0u; shaderRegister < 4u; ++shaderRegister) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
           shaderRegister});
    }
    for (uint32_t shaderRegister = 0u; shaderRegister < 2u; ++shaderRegister) {
      rootSignatureDesc.parameters.push_back({RHI::RootParameterType::SRV,
                                              RHI::ShaderVisibility::All,
                                              shaderRegister});
    }
    for (uint32_t shaderRegister = 0u; shaderRegister < 3u; ++shaderRegister) {
      rootSignatureDesc.parameters.push_back({RHI::RootParameterType::UAV,
                                              RHI::ShaderVisibility::All,
                                              shaderRegister});
    }
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "GeneratedMeshletIndexClassifyCS";
    shaderDesc.filePath = "Shaders/D3D12/GeneratedMeshletIndexClassify.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletIndexClassifyPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = use_read_buffer(builder, m_meshletCounterBuffer,
                                    RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_candidateChunkBuffer,
                             RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_workDispatchArgsBuffer,
                             RHI::ResourceState::IndirectArgument);
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
    result = builder.use_buffer(m_pageDispatchArgsBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_stateBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::UnorderedAccess);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->uav_barrier(m_objectDrawModeBuffer);
    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(
        0, MeshShaderVisibility::k_maxVisibleMeshletCount);
    commandContext->set_32bit_constant(
        1, MeshShaderVisibility::k_maxCandidateChunkCount);
    commandContext->set_32bit_constant(
        2, GeneratedMeshletIndex::k_segmentsPerPage);
    commandContext->set_32bit_constant(3, GeneratedMeshletIndex::k_pageCount);
    commandContext->set_srv(4, m_meshletCounterBuffer);
    commandContext->set_srv(5, m_candidateChunkBuffer);
    commandContext->set_uav(6, m_objectDrawModeBuffer);
    commandContext->set_uav(7, m_pageDispatchArgsBuffer);
    commandContext->set_uav(8, m_stateBuffer);
    commandContext->execute_dispatch_indirect(m_workDispatchArgsBuffer);
  }

private:
  static Result use_read_buffer(RHI::FrameGraphBuilder &builder,
                                RHI::BufferHandle buffer,
                                RHI::ResourceState state) {
    return builder.use_buffer(buffer, RHI::ResourceAccessType::Read, state,
                              state);
  }

  RHI::BufferHandle m_meshletCounterBuffer{};
  RHI::BufferHandle m_candidateChunkBuffer{};
  RHI::BufferHandle m_workDispatchArgsBuffer{};
  RHI::BufferHandle m_objectDrawModeBuffer{};
  RHI::BufferHandle m_pageDispatchArgsBuffer{};
  RHI::BufferHandle m_stateBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class GeneratedMeshletVisibilityBufferPass final : public RHI::FrameGraphPass {
public:
  GeneratedMeshletVisibilityBufferPass(const RenderPath &renderPath,
                                       const RenderDebugView &debugView,
                                       RHI::BufferHandle renderObjectBuffer,
                                       RHI::BufferHandle transformBuffer,
                                       RHI::BufferHandle viewProjectionBuffer)
      : m_renderPath(renderPath), m_debugView(debugView),
        m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer) {}

  const char *name() const noexcept override {
    return "GeneratedMeshletVisibilityBuffer";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  bool is_enabled(uint32_t a_frameIndex) const noexcept override {
    (void)a_frameIndex;
    return m_renderPath == RenderPath::VisibilityBuffer ||
           m_debugView != RenderDebugView::Forward;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = get_resources(builder);
    if (!result) {
      return result;
    }
    result = create_generated_buffers(builder);
    if (!result) {
      return result;
    }
    result = create_build_pipeline(builder);
    if (!result) {
      return result;
    }
    result = create_finalize_pipeline(builder);
    if (!result) {
      return result;
    }
    return create_graphics_pipeline(builder);
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

    const RHI::BufferHandle shaderBuffers[] = {
        m_renderObjectBuffer,      m_transformBuffer,
        m_viewProjectionBuffer,    m_visibleMeshletBuffer,
        m_meshletCounterBuffer,    m_meshletBoundsBuffer,
        m_meshletLocalIndexBuffer, m_meshletVertexIndexBuffer,
        m_meshRangeBuffer,         m_positionBuffer};
    for (RHI::BufferHandle buffer : shaderBuffers) {
      result =
          use_read_buffer(builder, buffer, RHI::ResourceState::ShaderResource);
      if (!result) {
        return result;
      }
    }
    result = use_read_buffer(builder, m_pageDispatchArgsBuffer,
                             RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }

    const RHI::BufferHandle generatedBuffers[] = {
        m_stateBuffer,        m_generatedIndexBuffer,
        m_outputOffsetBuffer, m_generatedCounterBuffer,
        m_commandBuffer,      m_commandCountBuffer};
    for (RHI::BufferHandle buffer : generatedBuffers) {
      result = builder.use_buffer(buffer, RHI::ResourceAccessType::ReadWrite,
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

    commandContext->set_render_targets(&m_visibilityRtv, 1u, m_depthDsv);
    commandContext->set_viewport_scissor(context.width(), context.height());

    for (uint32_t pageIndex = 0u;
         pageIndex < GeneratedMeshletIndex::k_pageCount; ++pageIndex) {
      build_page(*commandContext, pageIndex);
      finalize_page(*commandContext, pageIndex);
      transition_page_to_draw(*commandContext);
      draw_page(*commandContext);
      transition_page_to_build(*commandContext);
    }
  }

private:
  static Result use_read_buffer(RHI::FrameGraphBuilder &builder,
                                RHI::BufferHandle buffer,
                                RHI::ResourceState state) {
    return builder.use_buffer(buffer, RHI::ResourceAccessType::Read, state,
                              state);
  }

  Result get_resources(RHI::FrameGraphBuilder &builder) {
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

    const char *bufferNames[] = {"MeshShaderVisibility.VisibleMeshlets",
                                 "MeshShaderVisibility.Counters",
                                 "MeshPool.MeshletBounds",
                                 "MeshPool.MeshletLocalIndex",
                                 "MeshPool.MeshletVertexIndex",
                                 "MeshPool.MeshRange",
                                 "MeshPool.Position",
                                 "GeneratedMeshletIndex.PageDispatchArgs",
                                 "GeneratedMeshletIndex.State"};
    RHI::BufferHandle *bufferHandles[] = {&m_visibleMeshletBuffer,
                                          &m_meshletCounterBuffer,
                                          &m_meshletBoundsBuffer,
                                          &m_meshletLocalIndexBuffer,
                                          &m_meshletVertexIndexBuffer,
                                          &m_meshRangeBuffer,
                                          &m_positionBuffer,
                                          &m_pageDispatchArgsBuffer,
                                          &m_stateBuffer};
    for (uint32_t index = 0u;
         index < static_cast<uint32_t>(std::size(bufferNames)); ++index) {
      result = builder.get_buffer(bufferNames[index], *bufferHandles[index]);
      if (!result) {
        return result;
      }
    }
    return Result::ok();
  }

  Result create_generated_buffers(RHI::FrameGraphBuilder &builder) {
    RHI::BufferDesc indexDesc{};
    indexDesc.name = "GeneratedMeshletIndex.IndexBuffer";
    indexDesc.type = RHI::BufferType::Index;
    indexDesc.defaultHeapCount = builder.buffer_count();
    indexDesc.initialState = RHI::ResourceState::UnorderedAccess;
    indexDesc.allowsUnorderedAccess = true;
    indexDesc.stride = sizeof(uint32_t);
    indexDesc.elementCount = GeneratedMeshletIndex::k_indexCapacity;
    indexDesc.size = indexDesc.stride * indexDesc.elementCount;
    indexDesc.alignment = alignof(uint32_t);
    Result result = builder.create_buffer(indexDesc, m_generatedIndexBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc offsetDesc{};
    offsetDesc.name = "GeneratedMeshletIndex.OutputOffsets";
    offsetDesc.type = RHI::BufferType::UnorderedAccess;
    offsetDesc.defaultHeapCount = builder.buffer_count();
    offsetDesc.initialState = RHI::ResourceState::UnorderedAccess;
    offsetDesc.stride = sizeof(uint32_t);
    offsetDesc.elementCount = MeshShaderVisibility::k_maxVisibleMeshletCount;
    offsetDesc.size = offsetDesc.stride * offsetDesc.elementCount;
    offsetDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(offsetDesc, m_outputOffsetBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc counterDesc{};
    counterDesc.name = "GeneratedMeshletIndex.Counters";
    counterDesc.type = RHI::BufferType::Raw;
    counterDesc.defaultHeapCount = builder.buffer_count();
    counterDesc.initialState = RHI::ResourceState::UnorderedAccess;
    counterDesc.stride = sizeof(uint32_t);
    counterDesc.elementCount = 2u;
    counterDesc.size = counterDesc.stride * counterDesc.elementCount;
    counterDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(counterDesc, m_generatedCounterBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc counterUavDesc{};
    counterUavDesc.name = "GeneratedMeshletIndex.CountersUAV";
    counterUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    counterUavDesc.bufferKind = RHI::BufferKind::Buffer;
    counterUavDesc.bufferHandle = m_generatedCounterBuffer;
    counterUavDesc.numElements = counterDesc.elementCount;
    result = builder.create_view(counterUavDesc, m_generatedCounterUav);
    if (!result) {
      return result;
    }

    RHI::BufferDesc commandDesc{};
    commandDesc.name = "GeneratedMeshletIndex.CommandBuffer";
    commandDesc.type = RHI::BufferType::UnorderedAccess;
    commandDesc.defaultHeapCount = builder.buffer_count();
    commandDesc.initialState = RHI::ResourceState::UnorderedAccess;
    commandDesc.stride = sizeof(GpuData::IndirectCommand);
    commandDesc.elementCount = 1u;
    commandDesc.size = commandDesc.stride;
    commandDesc.alignment = alignof(GpuData::IndirectCommand);
    result = builder.create_buffer(commandDesc, m_commandBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc commandCountDesc{};
    commandCountDesc.name = "GeneratedMeshletIndex.CommandCount";
    commandCountDesc.type = RHI::BufferType::Raw;
    commandCountDesc.defaultHeapCount = builder.buffer_count();
    commandCountDesc.initialState = RHI::ResourceState::UnorderedAccess;
    commandCountDesc.stride = sizeof(uint32_t);
    commandCountDesc.elementCount = 1u;
    commandCountDesc.size = sizeof(uint32_t);
    commandCountDesc.alignment = alignof(uint32_t);
    return builder.create_buffer(commandCountDesc, m_commandCountBuffer);
  }

  Result create_build_pipeline(RHI::FrameGraphBuilder &builder) {
    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletIndexBuildRootSignature";
    for (uint32_t shaderRegister = 0u; shaderRegister < 4u; ++shaderRegister) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
           shaderRegister});
    }
    for (uint32_t shaderRegister = 0u; shaderRegister < 4u; ++shaderRegister) {
      rootSignatureDesc.parameters.push_back({RHI::RootParameterType::SRV,
                                              RHI::ShaderVisibility::All,
                                              shaderRegister});
    }
    for (uint32_t shaderRegister = 0u; shaderRegister < 4u; ++shaderRegister) {
      rootSignatureDesc.parameters.push_back({RHI::RootParameterType::UAV,
                                              RHI::ShaderVisibility::All,
                                              shaderRegister});
    }
    Result result =
        builder.create_root_signature(rootSignatureDesc, m_buildRootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "GeneratedMeshletIndexBuildCS";
    shaderDesc.filePath = "Shaders/D3D12/GeneratedMeshletIndexBuild.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_buildShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletIndexBuildPipeline";
    pipelineDesc.rootSignatureHandle = m_buildRootSignature;
    pipelineDesc.csHandle = m_buildShader;
    return builder.create_compute_pipeline(pipelineDesc, m_buildPipeline);
  }

  Result create_finalize_pipeline(RHI::FrameGraphBuilder &builder) {
    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletIndexFinalizeRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    for (uint32_t shaderRegister = 0u; shaderRegister < 3u; ++shaderRegister) {
      rootSignatureDesc.parameters.push_back({RHI::RootParameterType::UAV,
                                              RHI::ShaderVisibility::All,
                                              shaderRegister});
    }
    Result result = builder.create_root_signature(rootSignatureDesc,
                                                  m_finalizeRootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "GeneratedMeshletIndexFinalizeCS";
    shaderDesc.filePath = "Shaders/D3D12/GeneratedMeshletIndexFinalize.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_finalizeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletIndexFinalizePipeline";
    pipelineDesc.rootSignatureHandle = m_finalizeRootSignature;
    pipelineDesc.csHandle = m_finalizeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_finalizePipeline);
  }

  Result create_graphics_pipeline(RHI::FrameGraphBuilder &builder) {
    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletVisibilityRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All, 1,
         1, 0, 2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
    for (uint32_t shaderRegister = 10u; shaderRegister <= 15u;
         ++shaderRegister) {
      rootSignatureDesc.parameters.push_back({RHI::RootParameterType::SRV,
                                              RHI::ShaderVisibility::All,
                                              shaderRegister});
    }
    Result result = builder.create_root_signature(rootSignatureDesc,
                                                  m_graphicsRootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc vsDesc{};
    vsDesc.name = "GeneratedMeshletVisibilityVS";
    vsDesc.filePath = "Shaders/D3D12/VisibilityBuffer.hlsl";
    vsDesc.entryPoint = "generated_vs_main";
    vsDesc.targetProfile = "vs_6_0";
    result = builder.create_shader_blob(vsDesc, m_vertexShader);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc psDesc{};
    psDesc.name = "GeneratedMeshletVisibilityPS";
    psDesc.filePath = "Shaders/D3D12/VisibilityBuffer.hlsl";
    psDesc.entryPoint = "ps_main";
    psDesc.targetProfile = "ps_6_0";
    result = builder.create_shader_blob(psDesc, m_pixelShader);
    if (!result) {
      return result;
    }

    RHI::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletVisibilityPipeline";
    pipelineDesc.rootSignatureHandle = m_graphicsRootSignature;
    pipelineDesc.vsHandle = m_vertexShader;
    pipelineDesc.psHandle = m_pixelShader;
    pipelineDesc.rasterizerState.cullMode = RHI::CullMode::Back;
    pipelineDesc.depthStencilState.depthEnable = true;
    pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
    pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
    pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
    pipelineDesc.blendMode = {RHI::BlendMode::None};
    pipelineDesc.rtvFormats = {RHI::ColorFormat::R32_UINT};
    return builder.create_graphics_pipeline(pipelineDesc, m_graphicsPipeline);
  }

  void build_page(RHI::ICommandContext &commandContext,
                  uint32_t pageIndex) const {
    commandContext.begin_event("GeneratedMeshletIndexBuild");
    const uint32_t clearValues[4] = {0u, 0u, 0u, 0u};
    commandContext.clear_unordered_access_uint(m_generatedCounterUav,
                                               clearValues);
    commandContext.uav_barrier(m_generatedCounterBuffer);
    commandContext.set_compute_pipeline(m_buildPipeline);
    commandContext.set_32bit_constant(0,
                                      GeneratedMeshletIndex::k_indexCapacity);
    commandContext.set_32bit_constant(
        1, MeshShaderVisibility::k_maxVisibleMeshletCount);
    commandContext.set_32bit_constant(
        2, pageIndex * GeneratedMeshletIndex::k_segmentsPerPage);
    commandContext.set_32bit_constant(3,
                                      GeneratedMeshletIndex::k_segmentsPerPage);
    commandContext.set_srv(4, m_visibleMeshletBuffer);
    commandContext.set_srv(5, m_meshletCounterBuffer);
    commandContext.set_srv(6, m_meshletBoundsBuffer);
    commandContext.set_srv(7, m_meshletLocalIndexBuffer);
    commandContext.set_uav(8, m_generatedIndexBuffer);
    commandContext.set_uav(9, m_outputOffsetBuffer);
    commandContext.set_uav(10, m_generatedCounterBuffer);
    commandContext.set_uav(11, m_stateBuffer);
    commandContext.execute_dispatch_indirect(
        m_pageDispatchArgsBuffer,
        static_cast<uint64_t>(pageIndex) *
            sizeof(MeshShaderVisibility::DispatchArgs));
    commandContext.uav_barrier(m_generatedIndexBuffer);
    commandContext.uav_barrier(m_outputOffsetBuffer);
    commandContext.uav_barrier(m_generatedCounterBuffer);
    commandContext.end_event();
  }

  void finalize_page(RHI::ICommandContext &commandContext,
                     uint32_t pageIndex) const {
    commandContext.begin_event("GeneratedMeshletIndirectArgumentBuild");
    commandContext.set_compute_pipeline(m_finalizePipeline);
    commandContext.set_32bit_constant(0, pageIndex);
    commandContext.set_32bit_constant(1,
                                      GeneratedMeshletIndex::k_indexCapacity);
    commandContext.set_srv(2, m_generatedCounterBuffer);
    commandContext.set_uav(3, m_stateBuffer);
    commandContext.set_uav(4, m_commandBuffer);
    commandContext.set_uav(5, m_commandCountBuffer);
    commandContext.dispatch(1u, 1u, 1u);
    commandContext.uav_barrier(m_stateBuffer);
    commandContext.uav_barrier(m_commandBuffer);
    commandContext.uav_barrier(m_commandCountBuffer);
    commandContext.end_event();
  }

  void transition_page_to_draw(RHI::ICommandContext &commandContext) const {
    commandContext.resource_barrier(
        m_generatedIndexBuffer,
        {RHI::ResourceState::UnorderedAccess, RHI::ResourceState::IndexBuffer});
    commandContext.resource_barrier(m_outputOffsetBuffer,
                                    {RHI::ResourceState::UnorderedAccess,
                                     RHI::ResourceState::ShaderResource});
    commandContext.resource_barrier(m_commandBuffer,
                                    {RHI::ResourceState::UnorderedAccess,
                                     RHI::ResourceState::IndirectArgument});
    commandContext.resource_barrier(m_commandCountBuffer,
                                    {RHI::ResourceState::UnorderedAccess,
                                     RHI::ResourceState::IndirectArgument});
  }

  void draw_page(RHI::ICommandContext &commandContext) const {
    commandContext.begin_event("GeneratedMeshletDrawIndexed");
    commandContext.set_graphics_pipeline(m_graphicsPipeline);
    commandContext.set_primitive_topology(RHI::PrimitiveTopologyType::Triangle);
    commandContext.set_32bit_constant(0, 0u);
    commandContext.set_cbv(1, m_viewProjectionBuffer);
    commandContext.set_srv(2, m_renderObjectBuffer);
    commandContext.set_srv(3, m_transformBuffer);
    commandContext.set_srv(4, m_visibleMeshletBuffer);
    commandContext.set_srv(5, m_meshletBoundsBuffer);
    commandContext.set_srv(6, m_meshletVertexIndexBuffer);
    commandContext.set_srv(7, m_outputOffsetBuffer);
    commandContext.set_srv(8, m_meshRangeBuffer);
    commandContext.set_srv(9, m_positionBuffer);
    commandContext.set_index_buffer(m_generatedIndexBuffer,
                                    RHI::IndexFormat::UInt32);
    commandContext.execute_indexed_indirect(m_commandBuffer,
                                            m_commandCountBuffer, 1u);
    commandContext.end_event();
  }

  void transition_page_to_build(RHI::ICommandContext &commandContext) const {
    commandContext.resource_barrier(
        m_generatedIndexBuffer,
        {RHI::ResourceState::IndexBuffer, RHI::ResourceState::UnorderedAccess});
    commandContext.resource_barrier(m_outputOffsetBuffer,
                                    {RHI::ResourceState::ShaderResource,
                                     RHI::ResourceState::UnorderedAccess});
    commandContext.resource_barrier(m_commandBuffer,
                                    {RHI::ResourceState::IndirectArgument,
                                     RHI::ResourceState::UnorderedAccess});
    commandContext.resource_barrier(m_commandCountBuffer,
                                    {RHI::ResourceState::IndirectArgument,
                                     RHI::ResourceState::UnorderedAccess});
  }

  const RenderPath &m_renderPath;
  const RenderDebugView &m_debugView;
  RHI::TextureHandle m_visibility{};
  RHI::TextureHandle m_depth{};
  RHI::ViewHandle m_visibilityRtv{};
  RHI::ViewHandle m_depthDsv{};
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_meshletCounterBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_meshletLocalIndexBuffer{};
  RHI::BufferHandle m_meshletVertexIndexBuffer{};
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_positionBuffer{};
  RHI::BufferHandle m_pageDispatchArgsBuffer{};
  RHI::BufferHandle m_stateBuffer{};
  RHI::BufferHandle m_generatedIndexBuffer{};
  RHI::BufferHandle m_outputOffsetBuffer{};
  RHI::BufferHandle m_generatedCounterBuffer{};
  RHI::BufferHandle m_commandBuffer{};
  RHI::BufferHandle m_commandCountBuffer{};
  RHI::ViewHandle m_generatedCounterUav{};
  RHI::RootSignatureHandle m_buildRootSignature{};
  RHI::RootSignatureHandle m_finalizeRootSignature{};
  RHI::RootSignatureHandle m_graphicsRootSignature{};
  RHI::ShaderBlobHandle m_buildShader{};
  RHI::ShaderBlobHandle m_finalizeShader{};
  RHI::ShaderBlobHandle m_vertexShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_buildPipeline{};
  RHI::PipelineStateHandle m_finalizePipeline{};
  RHI::PipelineStateHandle m_graphicsPipeline{};
};

class GeneratedMeshletIndexStatsReadbackPass final
    : public RHI::FrameGraphPass {
public:
  explicit GeneratedMeshletIndexStatsReadbackPass(
      RHI::IBufferManager *bufferManager)
      : m_bufferManager(bufferManager) {}

  const char *name() const noexcept override {
    return "GeneratedMeshletIndexStatsReadback";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result =
        builder.get_buffer("GeneratedMeshletIndex.State", m_stateBuffer);
    if (!result) {
      return result;
    }
    if (m_bufferManager == nullptr) {
      return Result::fail(Code::InvalidState, Severity::Error,
                          "Generated meshlet stats require a buffer manager.");
    }
    return m_bufferManager->get_readback_buffer_view(m_stateBuffer,
                                                     m_readbackView);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    return builder.use_buffer(m_stateBuffer, RHI::ResourceAccessType::Read,
                              RHI::ResourceState::CopySource,
                              RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    update_stats_from_readback(context.frame_index());
    if (!should_copy_stats_to_readback()) {
      return;
    }

    RHI::BufferToReadbackCopyRegion copyRegion{};
    copyRegion.srcBufferHandle = m_stateBuffer;
    copyRegion.srcDefaultResourceIndex = context.frame_index();
    copyRegion.srcByteOffset = 0u;
    copyRegion.dstBufferHandle = m_stateBuffer;
    copyRegion.dstReadbackResourceIndex = context.frame_index();
    copyRegion.dstByteOffset = 0u;
    copyRegion.byteSize =
        GeneratedMeshletIndex::k_stateValueCount * sizeof(uint32_t);
    commandContext->copy_buffer_region_to_readback(copyRegion);
  }

private:
  static constexpr uint64_t k_readbackWarmupFrames = 8u;
  static constexpr uint64_t k_readbackIntervalFrames = 30u;

  bool should_copy_stats_to_readback() noexcept {
    const uint64_t frameIndex = m_readbackFrameCount++;
    return frameIndex < k_readbackWarmupFrames ||
           (frameIndex % k_readbackIntervalFrames) == 0u;
  }

  void update_stats_from_readback(uint32_t frameIndex) noexcept {
    if (frameIndex >= m_readbackView.mappedDatas.size()) {
      return;
    }
    const std::byte *mappedData = m_readbackView.mappedDatas[frameIndex];
    if (mappedData == nullptr ||
        (m_logFrameCount++ % k_readbackIntervalFrames) != 0u) {
      return;
    }

    const uint32_t *values = reinterpret_cast<const uint32_t *>(mappedData);
    const uint64_t generatedBytes =
        static_cast<uint64_t>(values[4u]) * sizeof(uint32_t);
    Core::IO::log(Core::IO::LogSink::console | Core::IO::LogSink::file,
                  "[GeneratedMeshletIndexStats] valid={} visibleSegments={} "
                  "candidateChunks={} activePages={} generatedIndices={} "
                  "generatedBytes={} drawCommands={} overflow={} fallback={}",
                  values[0u], values[1u], values[2u], values[3u], values[4u],
                  generatedBytes, values[5u], values[6u], values[7u]);
  }

  RHI::IBufferManager *m_bufferManager = nullptr;
  RHI::BufferHandle m_stateBuffer{};
  RHI::ReadbackBufferView m_readbackView{};
  uint64_t m_readbackFrameCount = 0u;
  uint64_t m_logFrameCount = 0u;
};

} // namespace Cue::DrawSystem
