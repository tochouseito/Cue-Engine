#pragma once

#include <FrameGraph.h>

#include "DrawSystem/RenderDebugView.h"
#include "DrawSystem/RenderPath.h"
#include "DrawSystem/passes/MeshShaderVisibilityPass.h"
#include "GpuData/Batching.h"

#include <iterator>

namespace Cue::DrawSystem {

class GeneratedMeshletIndexDispatchArgsPass final
    : public RHI::FrameGraphPass {
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
    dispatchArgsDesc.name = "GeneratedMeshletIndex.DispatchArgs";
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

class GeneratedMeshletIndexBuildPass final : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "GeneratedMeshletIndexBuild";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("MeshShaderVisibility.VisibleMeshlets",
                                       m_visibleMeshletBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.Counters",
                                m_meshletCounterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletIndex.DispatchArgs",
                                m_dispatchArgsBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletBounds",
                                m_meshletBoundsBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletLocalIndex",
                                m_meshletLocalIndexBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc indexDesc{};
    indexDesc.name = "GeneratedMeshletIndex.IndexBuffer";
    indexDesc.type = RHI::BufferType::Index;
    indexDesc.defaultHeapCount = builder.buffer_count();
    indexDesc.initialState = RHI::ResourceState::UnorderedAccess;
    indexDesc.allowsUnorderedAccess = true;
    indexDesc.stride = sizeof(uint32_t);
    indexDesc.elementCount = MeshShaderVisibility::k_maxRangeIndexCount;
    indexDesc.size = indexDesc.stride * indexDesc.elementCount;
    indexDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(indexDesc, m_generatedIndexBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc offsetDesc{};
    offsetDesc.name = "GeneratedMeshletIndex.OutputOffsets";
    offsetDesc.type = RHI::BufferType::UnorderedAccess;
    offsetDesc.defaultHeapCount = builder.buffer_count();
    offsetDesc.initialState = RHI::ResourceState::UnorderedAccess;
    offsetDesc.stride = sizeof(uint32_t);
    offsetDesc.elementCount =
        MeshShaderVisibility::k_maxVisibleMeshletCount;
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
    result = builder.create_buffer(commandCountDesc, m_commandCountBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletIndexBuildRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1});
    for (uint32_t shaderRegister = 0u; shaderRegister < 4u;
         ++shaderRegister) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All,
           shaderRegister});
    }
    for (uint32_t shaderRegister = 0u; shaderRegister < 3u;
         ++shaderRegister) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All,
           shaderRegister});
    }
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "GeneratedMeshletIndexBuildCS";
    shaderDesc.filePath = "Shaders/D3D12/GeneratedMeshletIndexBuild.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletIndexBuildPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = use_read_buffer(builder, m_visibleMeshletBuffer,
                                    RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_meshletCounterBuffer,
                             RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_meshletBoundsBuffer,
                             RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_meshletLocalIndexBuffer,
                             RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_dispatchArgsBuffer,
                             RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_generatedIndexBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_outputOffsetBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_generatedCounterBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::ShaderResource);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const uint32_t clearValues[4] = {0u, 0u, 0u, 0u};
    commandContext->clear_unordered_access_uint(m_generatedCounterUav,
                                                clearValues);
    commandContext->uav_barrier(m_generatedCounterBuffer);
    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(
        0, MeshShaderVisibility::k_maxRangeIndexCount);
    commandContext->set_32bit_constant(
        1, MeshShaderVisibility::k_maxVisibleMeshletCount);
    commandContext->set_srv(2, m_visibleMeshletBuffer);
    commandContext->set_srv(3, m_meshletCounterBuffer);
    commandContext->set_srv(4, m_meshletBoundsBuffer);
    commandContext->set_srv(5, m_meshletLocalIndexBuffer);
    commandContext->set_uav(6, m_generatedIndexBuffer);
    commandContext->set_uav(7, m_outputOffsetBuffer);
    commandContext->set_uav(8, m_generatedCounterBuffer);
    commandContext->execute_dispatch_indirect(m_dispatchArgsBuffer);
  }

private:
  static Result use_read_buffer(RHI::FrameGraphBuilder &builder,
                                RHI::BufferHandle buffer,
                                RHI::ResourceState state) {
    return builder.use_buffer(buffer, RHI::ResourceAccessType::Read, state,
                              state);
  }

  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_meshletCounterBuffer{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_meshletLocalIndexBuffer{};
  RHI::BufferHandle m_generatedIndexBuffer{};
  RHI::BufferHandle m_outputOffsetBuffer{};
  RHI::BufferHandle m_generatedCounterBuffer{};
  RHI::BufferHandle m_commandBuffer{};
  RHI::BufferHandle m_commandCountBuffer{};
  RHI::ViewHandle m_generatedCounterUav{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class GeneratedMeshletIndexFinalizePass final : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "GeneratedMeshletIndexFinalize";
  }

  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("MeshShaderVisibility.VisibleMeshlets",
                                       m_visibleMeshletBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.Counters",
                                m_meshletCounterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletIndex.Counters",
                                m_generatedCounterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshShaderVisibility.CandidateChunks",
                                m_candidateChunkBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletIndex.DispatchArgs",
                                m_dispatchArgsBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("ObjectDrawModeBuffer",
                                m_objectDrawModeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletIndex.CommandBuffer",
                                m_commandBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletIndex.CommandCount",
                                m_commandCountBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletIndexFinalizeRootSignature";
    for (uint32_t shaderRegister = 0u; shaderRegister < 3u;
         ++shaderRegister) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::_32BitConstants,
           RHI::ShaderVisibility::All, shaderRegister});
    }
    for (uint32_t shaderRegister = 0u; shaderRegister < 4u;
         ++shaderRegister) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All,
           shaderRegister});
    }
    for (uint32_t shaderRegister = 0u; shaderRegister < 3u;
         ++shaderRegister) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All,
           shaderRegister});
    }
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "GeneratedMeshletIndexFinalizeCS";
    shaderDesc.filePath = "Shaders/D3D12/GeneratedMeshletIndexFinalize.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletIndexFinalizePipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = use_read_buffer(builder, m_visibleMeshletBuffer,
                                    RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_meshletCounterBuffer,
                             RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_generatedCounterBuffer,
                             RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_candidateChunkBuffer,
                             RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    result = use_read_buffer(builder, m_dispatchArgsBuffer,
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
    result = builder.use_buffer(m_commandBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::IndirectArgument);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_commandCountBuffer,
                              RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::IndirectArgument);
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
        2, MeshShaderVisibility::k_maxRangeIndexCount);
    commandContext->set_srv(3, m_visibleMeshletBuffer);
    commandContext->set_srv(4, m_meshletCounterBuffer);
    commandContext->set_srv(5, m_generatedCounterBuffer);
    commandContext->set_srv(6, m_candidateChunkBuffer);
    commandContext->set_uav(7, m_objectDrawModeBuffer);
    commandContext->set_uav(8, m_commandBuffer);
    commandContext->set_uav(9, m_commandCountBuffer);
    commandContext->execute_dispatch_indirect(m_dispatchArgsBuffer);
  }

private:
  static Result use_read_buffer(RHI::FrameGraphBuilder &builder,
                                RHI::BufferHandle buffer,
                                RHI::ResourceState state) {
    return builder.use_buffer(buffer, RHI::ResourceAccessType::Read, state,
                              state);
  }

  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_meshletCounterBuffer{};
  RHI::BufferHandle m_generatedCounterBuffer{};
  RHI::BufferHandle m_candidateChunkBuffer{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::BufferHandle m_objectDrawModeBuffer{};
  RHI::BufferHandle m_commandBuffer{};
  RHI::BufferHandle m_commandCountBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class GeneratedMeshletVisibilityBufferPass final
    : public RHI::FrameGraphPass {
public:
  GeneratedMeshletVisibilityBufferPass(
      const RenderPath &renderPath, const RenderDebugView &debugView,
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

    const char *bufferNames[] = {
        "MeshShaderVisibility.VisibleMeshlets",
        "MeshPool.MeshletBounds",
        "MeshPool.MeshletVertexIndex",
        "GeneratedMeshletIndex.OutputOffsets",
        "MeshPool.MeshRange",
        "MeshPool.Position",
        "GeneratedMeshletIndex.IndexBuffer",
        "GeneratedMeshletIndex.CommandBuffer",
        "GeneratedMeshletIndex.CommandCount"};
    RHI::BufferHandle *bufferHandles[] = {
        &m_visibleMeshletBuffer, &m_meshletBoundsBuffer,
        &m_meshletVertexIndexBuffer, &m_outputOffsetBuffer,
        &m_meshRangeBuffer, &m_positionBuffer, &m_generatedIndexBuffer,
        &m_commandBuffer, &m_commandCountBuffer};
    for (uint32_t index = 0u; index < std::size(bufferNames); ++index) {
      result = builder.get_buffer(bufferNames[index], *bufferHandles[index]);
      if (!result) {
        return result;
      }
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletVisibilityRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         1, 1, 0, 2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
    for (uint32_t shaderRegister = 10u; shaderRegister <= 15u;
         ++shaderRegister) {
      rootSignatureDesc.parameters.push_back(
          {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All,
           shaderRegister});
    }
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
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
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.vsHandle = m_vertexShader;
    pipelineDesc.psHandle = m_pixelShader;
    pipelineDesc.rasterizerState.cullMode = RHI::CullMode::Back;
    pipelineDesc.depthStencilState.depthEnable = true;
    pipelineDesc.depthStencilState.depthWriteMask = RHI::DepthWriteMask::All;
    pipelineDesc.depthStencilState.depthFunc = RHI::ComparisonFunc::LessEqual;
    pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
    pipelineDesc.blendMode = {RHI::BlendMode::None};
    pipelineDesc.rtvFormats = {RHI::ColorFormat::R32_UINT};
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

    const RHI::BufferHandle shaderBuffers[] = {
        m_renderObjectBuffer,   m_transformBuffer,
        m_viewProjectionBuffer, m_visibleMeshletBuffer,
        m_meshletBoundsBuffer,  m_meshletVertexIndexBuffer,
        m_meshRangeBuffer,      m_positionBuffer};
    for (RHI::BufferHandle buffer : shaderBuffers) {
      result = builder.use_buffer(buffer, RHI::ResourceAccessType::Read,
                                  RHI::ResourceState::ShaderResource,
                                  RHI::ResourceState::ShaderResource);
      if (!result) {
        return result;
      }
    }
    result = builder.use_buffer(m_outputOffsetBuffer,
                                RHI::ResourceAccessType::Read,
                                RHI::ResourceState::ShaderResource,
                                RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_generatedIndexBuffer,
                                RHI::ResourceAccessType::Read,
                                RHI::ResourceState::IndexBuffer,
                                RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_commandBuffer, RHI::ResourceAccessType::Read,
                                RHI::ResourceState::IndirectArgument,
                                RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_commandCountBuffer,
                              RHI::ResourceAccessType::Read,
                              RHI::ResourceState::IndirectArgument,
                              RHI::ResourceState::Common);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->set_render_targets(&m_visibilityRtv, 1u, m_depthDsv);
    commandContext->set_viewport_scissor(context.width(), context.height());
    commandContext->set_graphics_pipeline(m_pipeline);
    commandContext->set_primitive_topology(
        RHI::PrimitiveTopologyType::Triangle);
    commandContext->set_32bit_constant(0, 0u);
    commandContext->set_cbv(1, m_viewProjectionBuffer);
    commandContext->set_srv(2, m_renderObjectBuffer);
    commandContext->set_srv(3, m_transformBuffer);
    commandContext->set_srv(4, m_visibleMeshletBuffer);
    commandContext->set_srv(5, m_meshletBoundsBuffer);
    commandContext->set_srv(6, m_meshletVertexIndexBuffer);
    commandContext->set_srv(7, m_outputOffsetBuffer);
    commandContext->set_srv(8, m_meshRangeBuffer);
    commandContext->set_srv(9, m_positionBuffer);
    commandContext->set_index_buffer(m_generatedIndexBuffer,
                                     RHI::IndexFormat::UInt32);
    commandContext->execute_indexed_indirect(m_commandBuffer,
                                             m_commandCountBuffer, 1u);
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
  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_meshletVertexIndexBuffer{};
  RHI::BufferHandle m_outputOffsetBuffer{};
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_positionBuffer{};
  RHI::BufferHandle m_generatedIndexBuffer{};
  RHI::BufferHandle m_commandBuffer{};
  RHI::BufferHandle m_commandCountBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_vertexShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

} // namespace Cue::DrawSystem
