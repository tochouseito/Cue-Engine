#pragma once

/// ****************************************************************************
/// Generated meshlet depth stream experiment
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"

namespace Cue::DrawSystem {
namespace GeneratedMeshletDepth {
static constexpr uint32_t k_maxGeneratedVertexCount = 16u * 1024u * 1024u;
static constexpr uint32_t k_maxVisibleMeshletCount = 4u * 1024u * 1024u;

struct VisibleMeshlet final {
  uint32_t objectIndex = 0;
  uint32_t meshId = 0;
  uint32_t meshletIndex = 0;
  uint32_t padding = 0;
};

struct GeneratedTriVertex final {
  uint32_t objectIndex = 0;
  uint32_t sourceVertexIndex = 0;
};

struct DrawInstancedArgs final {
  uint32_t vertexCountPerInstance = 0;
  uint32_t instanceCount = 0;
  uint32_t startVertexLocation = 0;
  uint32_t startInstanceLocation = 0;
};

struct DispatchArgs final {
  uint32_t threadGroupCountX = 0;
  uint32_t threadGroupCountY = 0;
  uint32_t threadGroupCountZ = 0;
};
} // namespace GeneratedMeshletDepth

class GeneratedMeshletDepthResetPass final : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "GeneratedMeshletDepthReset";
  }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    RHI::BufferDesc vertexDesc{};
    vertexDesc.name = "GeneratedMeshletDepth.VertexStream";
    vertexDesc.type = RHI::BufferType::UnorderedAccess;
    vertexDesc.defaultHeapCount = 1;
    vertexDesc.uploadHeapCount = 0;
    vertexDesc.initialState = RHI::ResourceState::UnorderedAccess;
    vertexDesc.stride = sizeof(GeneratedMeshletDepth::GeneratedTriVertex);
    vertexDesc.elementCount =
        GeneratedMeshletDepth::k_maxGeneratedVertexCount;
    vertexDesc.size = static_cast<uint64_t>(vertexDesc.stride) *
                      static_cast<uint64_t>(vertexDesc.elementCount);
    vertexDesc.alignment = alignof(GeneratedMeshletDepth::GeneratedTriVertex);
    Result result = builder.create_buffer(vertexDesc, m_vertexStreamBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc visibleDesc{};
    visibleDesc.name = "GeneratedMeshletDepth.VisibleMeshlets";
    visibleDesc.type = RHI::BufferType::UnorderedAccess;
    visibleDesc.defaultHeapCount = 1;
    visibleDesc.uploadHeapCount = 0;
    visibleDesc.initialState = RHI::ResourceState::UnorderedAccess;
    visibleDesc.stride = sizeof(GeneratedMeshletDepth::VisibleMeshlet);
    visibleDesc.elementCount =
        GeneratedMeshletDepth::k_maxVisibleMeshletCount;
    visibleDesc.size = static_cast<uint64_t>(visibleDesc.stride) *
                       static_cast<uint64_t>(visibleDesc.elementCount);
    visibleDesc.alignment = alignof(GeneratedMeshletDepth::VisibleMeshlet);
    result = builder.create_buffer(visibleDesc, m_visibleMeshletBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc counterDesc{};
    counterDesc.name = "GeneratedMeshletDepth.VertexCounter";
    counterDesc.type = RHI::BufferType::Raw;
    counterDesc.defaultHeapCount = 1;
    counterDesc.uploadHeapCount = 0;
    counterDesc.initialState = RHI::ResourceState::UnorderedAccess;
    counterDesc.stride = sizeof(uint32_t);
    counterDesc.elementCount = 2;
    counterDesc.size = sizeof(uint32_t) * counterDesc.elementCount;
    counterDesc.alignment = alignof(uint32_t);
    result = builder.create_buffer(counterDesc, m_counterBuffer);
    if (!result) {
      return result;
    }

    RHI::ViewDesc counterUavDesc{};
    counterUavDesc.name = "GeneratedMeshletDepth.VertexCounterUAV";
    counterUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
    counterUavDesc.bufferKind = RHI::BufferKind::Buffer;
    counterUavDesc.bufferHandle = m_counterBuffer;
    counterUavDesc.numElements = counterDesc.size / sizeof(uint32_t);
    result = builder.create_view(counterUavDesc, m_counterUav);
    if (!result) {
      return result;
    }

    RHI::BufferDesc drawArgsDesc{};
    drawArgsDesc.name = "GeneratedMeshletDepth.DrawArgs";
    drawArgsDesc.type = RHI::BufferType::UnorderedAccess;
    drawArgsDesc.defaultHeapCount = 1;
    drawArgsDesc.uploadHeapCount = 0;
    drawArgsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    drawArgsDesc.stride = sizeof(GeneratedMeshletDepth::DrawInstancedArgs);
    drawArgsDesc.elementCount = 1;
    drawArgsDesc.size = drawArgsDesc.stride;
    drawArgsDesc.alignment = alignof(GeneratedMeshletDepth::DrawInstancedArgs);
    result = builder.create_buffer(drawArgsDesc, m_drawArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::BufferDesc dispatchArgsDesc{};
    dispatchArgsDesc.name = "GeneratedMeshletDepth.DispatchArgs";
    dispatchArgsDesc.type = RHI::BufferType::UnorderedAccess;
    dispatchArgsDesc.defaultHeapCount = 1;
    dispatchArgsDesc.uploadHeapCount = 0;
    dispatchArgsDesc.initialState = RHI::ResourceState::UnorderedAccess;
    dispatchArgsDesc.stride = sizeof(GeneratedMeshletDepth::DispatchArgs);
    dispatchArgsDesc.elementCount = 1;
    dispatchArgsDesc.size = dispatchArgsDesc.stride;
    dispatchArgsDesc.alignment = alignof(GeneratedMeshletDepth::DispatchArgs);
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
  }

private:
  RHI::BufferHandle m_vertexStreamBuffer{};
  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_drawArgsBuffer{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::ViewHandle m_counterUav{};
};

class GeneratedMeshletDepthCullPass final : public RHI::FrameGraphPass {
public:
  GeneratedMeshletDepthCullPass(
      const DrawFrameState &drawFrameState,
      RHI::BufferHandle renderObjectBuffer,
      RHI::BufferHandle transformBuffer,
      RHI::BufferHandle viewProjectionBuffer,
      RHI::BufferHandle visibleObjectCountBuffer,
      uint32_t maxObjectCount)
      : m_drawFrameState(drawFrameState),
        m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer),
        m_visibleObjectCountBuffer(visibleObjectCountBuffer),
        m_maxObjectCount(maxObjectCount) {}

  const char *name() const noexcept override {
    return "GeneratedMeshletDepthCull";
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
    result = builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.MeshletBounds", m_meshletBoundsBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletDepth.VisibleMeshlets",
                                m_visibleMeshletBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletDepth.VertexCounter",
                                m_counterBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletDepthCullRootSignature";
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
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "GeneratedMeshletDepthCullCS";
    shaderDesc.filePath = "Shaders/D3D12/GeneratedMeshletDepthCull.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletDepthCullPipeline";
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
    result = builder.use_buffer(m_visibleMeshletBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_counterBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::ShaderResource);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    const DrawFrameData &frameState =
        m_drawFrameState.frame_state(context.frame_index());
    if (frameState.objectCount == 0) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(0, frameState.objectCount);
    commandContext->set_32bit_constant(
        1, GeneratedMeshletDepth::k_maxVisibleMeshletCount);
    commandContext->set_cbv(2, m_viewProjectionBuffer);
    commandContext->set_srv(3, m_renderObjectBuffer);
    commandContext->set_srv(4, m_transformBuffer);
    commandContext->set_srv(5, m_visibleObjectCountBuffer);
    commandContext->set_srv(6, m_meshRangeBuffer);
    commandContext->set_srv(7, m_meshletBoundsBuffer);
    commandContext->set_uav(8, m_visibleMeshletBuffer);
    commandContext->set_uav(9, m_counterBuffer);
    commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
  }

private:
  const DrawFrameState &m_drawFrameState;
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::BufferHandle m_visibleObjectCountBuffer{};
  uint32_t m_maxObjectCount = 0;
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_counterBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class GeneratedMeshletDepthDispatchArgsPass final
    : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "GeneratedMeshletDepthDispatchArgs";
  }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("GeneratedMeshletDepth.VertexCounter",
                                       m_counterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletDepth.DispatchArgs",
                                m_dispatchArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletDepthDispatchArgsRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "GeneratedMeshletDepthDispatchArgsCS";
    shaderDesc.filePath =
        "Shaders/D3D12/GeneratedMeshletDepthDispatchArgs.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletDepthDispatchArgsPipeline";
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
    commandContext->set_srv(0, m_counterBuffer);
    commandContext->set_uav(1, m_dispatchArgsBuffer);
    commandContext->dispatch(1, 1, 1);
  }

private:
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class GeneratedMeshletDepthStreamBuildPass final
    : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "GeneratedMeshletDepthStreamBuild";
  }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("GeneratedMeshletDepth.VisibleMeshlets",
                                       m_visibleMeshletBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletDepth.VertexCounter",
                                m_counterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletDepth.VertexStream",
                                m_vertexStreamBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletDepth.DispatchArgs",
                                m_dispatchArgsBuffer);
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

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletDepthStreamBuildRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::_32BitConstants, RHI::ShaderVisibility::All,
         0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
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
    shaderDesc.name = "GeneratedMeshletDepthStreamBuildCS";
    shaderDesc.filePath =
        "Shaders/D3D12/GeneratedMeshletDepthStreamBuild.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletDepthStreamBuildPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.csHandle = m_computeShader;
    return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
  }

  Result describe_resources(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.use_buffer(
        m_visibleMeshletBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_dispatchArgsBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument, RHI::ResourceState::Common);
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
        m_rangeIndexBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(m_vertexStreamBuffer,
                                RHI::ResourceAccessType::Write,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource);
    if (!result) {
      return result;
    }
    return builder.use_buffer(m_counterBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::ShaderResource);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_32bit_constant(
        0, GeneratedMeshletDepth::k_maxGeneratedVertexCount);
    commandContext->set_srv(1, m_visibleMeshletBuffer);
    commandContext->set_srv(2, m_meshRangeBuffer);
    commandContext->set_srv(3, m_meshletBoundsBuffer);
    commandContext->set_srv(4, m_rangeIndexBuffer);
    commandContext->set_uav(5, m_vertexStreamBuffer);
    commandContext->set_uav(6, m_counterBuffer);
    commandContext->execute_dispatch_indirect(m_dispatchArgsBuffer);
  }

private:
  RHI::BufferHandle m_visibleMeshletBuffer{};
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_vertexStreamBuffer{};
  RHI::BufferHandle m_dispatchArgsBuffer{};
  RHI::BufferHandle m_meshRangeBuffer{};
  RHI::BufferHandle m_meshletBoundsBuffer{};
  RHI::BufferHandle m_rangeIndexBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class GeneratedMeshletDepthArgsPass final : public RHI::FrameGraphPass {
public:
  const char *name() const noexcept override {
    return "GeneratedMeshletDepthArgs";
  }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Compute;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    Result result = builder.get_buffer("GeneratedMeshletDepth.VertexCounter",
                                       m_counterBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletDepth.DrawArgs",
                                m_drawArgsBuffer);
    if (!result) {
      return result;
    }

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletDepthArgsRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc shaderDesc{};
    shaderDesc.name = "GeneratedMeshletDepthArgsCS";
    shaderDesc.filePath = "Shaders/D3D12/GeneratedMeshletDepthArgs.hlsl";
    shaderDesc.entryPoint = "CSMain";
    shaderDesc.targetProfile = "cs_6_0";
    result = builder.create_shader_blob(shaderDesc, m_computeShader);
    if (!result) {
      return result;
    }

    RHI::ComputePipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletDepthArgsPipeline";
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
    return builder.use_buffer(m_drawArgsBuffer, RHI::ResourceAccessType::Write,
                              RHI::ResourceState::UnorderedAccess,
                              RHI::ResourceState::IndirectArgument);
  }

  void execute(RHI::FrameGraphContext &context) override {
    RHI::ICommandContext *commandContext = context.commandContext();
    if (commandContext == nullptr) {
      return;
    }

    commandContext->set_compute_pipeline(m_pipeline);
    commandContext->set_srv(0, m_counterBuffer);
    commandContext->set_uav(1, m_drawArgsBuffer);
    commandContext->dispatch(1, 1, 1);
  }

private:
  RHI::BufferHandle m_counterBuffer{};
  RHI::BufferHandle m_drawArgsBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_computeShader{};
  RHI::PipelineStateHandle m_pipeline{};
};

class GeneratedMeshletDepthDrawPass final : public RHI::FrameGraphPass {
public:
  GeneratedMeshletDepthDrawPass(RHI::BufferHandle renderObjectBuffer,
                                RHI::BufferHandle transformBuffer,
                                RHI::BufferHandle viewProjectionBuffer)
      : m_renderObjectBuffer(renderObjectBuffer),
        m_transformBuffer(transformBuffer),
        m_viewProjectionBuffer(viewProjectionBuffer) {}

  const char *name() const noexcept override {
    return "GeneratedMeshletDepthDraw";
  }
  RHI::CommandListType type() const noexcept override {
    return RHI::CommandListType::Graphics;
  }

  Result setup(RHI::FrameGraphBuilder &builder) override {
    RHI::TextureDesc depthDesc{};
    depthDesc.name = "GeneratedMeshletDepth.Texture";
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
    depthDsvDesc.name = "GeneratedMeshletDepth.DSV";
    depthDsvDesc.type = RHI::ViewType::DepthStencil;
    depthDsvDesc.bufferKind = RHI::BufferKind::Texture;
    depthDsvDesc.textureHandle = m_depth;
    depthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
    result = builder.create_view(depthDsvDesc, m_depthDsv);
    if (!result) {
      return result;
    }

    result = builder.get_buffer("GeneratedMeshletDepth.VertexStream",
                                m_vertexStreamBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("GeneratedMeshletDepth.DrawArgs",
                                m_drawArgsBuffer);
    if (!result) {
      return result;
    }
    result = builder.get_buffer("MeshPool.Position", m_positionBuffer);
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

    RHI::RootSignatureDesc rootSignatureDesc{};
    rootSignatureDesc.name = "GeneratedMeshletDepthRootSignature";
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2});
    rootSignatureDesc.parameters.push_back(
        {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3});
    result = builder.create_root_signature(rootSignatureDesc, m_rootSignature);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc vsDesc{};
    vsDesc.name = "GeneratedMeshletDepthVS";
    vsDesc.filePath = "Shaders/D3D12/GeneratedMeshletDepth.hlsl";
    vsDesc.entryPoint = "vs_main";
    vsDesc.targetProfile = "vs_6_0";
    result = builder.create_shader_blob(vsDesc, m_vertexShader);
    if (!result) {
      return result;
    }

    RHI::ShaderCompileDesc psDesc{};
    psDesc.name = "GeneratedMeshletDepthPS";
    psDesc.filePath = "Shaders/D3D12/GeneratedMeshletDepth.hlsl";
    psDesc.entryPoint = "ps_main";
    psDesc.targetProfile = "ps_6_0";
    result = builder.create_shader_blob(psDesc, m_pixelShader);
    if (!result) {
      return result;
    }

    RHI::GraphicsPipelineStateDesc pipelineDesc{};
    pipelineDesc.name = "GeneratedMeshletDepthPipeline";
    pipelineDesc.rootSignatureHandle = m_rootSignature;
    pipelineDesc.vsHandle = m_vertexShader;
    pipelineDesc.psHandle = m_pixelShader;
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
        m_vertexStreamBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_drawArgsBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::IndirectArgument, RHI::ResourceState::Common);
    if (!result) {
      return result;
    }
    result = builder.use_buffer(
        m_positionBuffer, RHI::ResourceAccessType::Read,
        RHI::ResourceState::ShaderResource, RHI::ResourceState::VertexBuffer);
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
    return builder.use_buffer(
        m_viewProjectionBuffer, RHI::ResourceAccessType::Read,
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
    commandContext->set_cbv(0, m_viewProjectionBuffer);
    commandContext->set_srv(1, m_vertexStreamBuffer);
    commandContext->set_srv(2, m_positionBuffer);
    commandContext->set_srv(3, m_renderObjectBuffer);
    commandContext->set_srv(4, m_transformBuffer);
    commandContext->execute_instanced_indirect(m_drawArgsBuffer, 1);
  }

private:
  RHI::BufferHandle m_renderObjectBuffer{};
  RHI::BufferHandle m_transformBuffer{};
  RHI::BufferHandle m_viewProjectionBuffer{};
  RHI::TextureHandle m_depth{};
  RHI::ViewHandle m_depthDsv{};
  RHI::BufferHandle m_vertexStreamBuffer{};
  RHI::BufferHandle m_drawArgsBuffer{};
  RHI::BufferHandle m_positionBuffer{};
  RHI::RootSignatureHandle m_rootSignature{};
  RHI::ShaderBlobHandle m_vertexShader{};
  RHI::ShaderBlobHandle m_pixelShader{};
  RHI::PipelineStateHandle m_pipeline{};
};
} // namespace Cue::DrawSystem
