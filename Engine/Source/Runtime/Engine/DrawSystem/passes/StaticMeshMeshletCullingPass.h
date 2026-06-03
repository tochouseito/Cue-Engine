#pragma once

/// ****************************************************************************
/// Static mesh meshlet indirect command generation
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "GpuData/Batching.h"

namespace Cue::DrawSystem
{
    class StaticMeshMeshletCullingPass final : public RHI::FrameGraphPass
    {
    public:
        StaticMeshMeshletCullingPass(
            const DrawFrameState& drawFrameState,
            RHI::BufferHandle renderObjectBuffer,
            RHI::BufferHandle transformBuffer,
            RHI::BufferHandle viewProjectionBuffer,
            RHI::BufferHandle visibleObjectCountBuffer,
            uint32_t maxObjectCount,
            uint32_t maxIndirectCommandCount)
            : m_drawFrameState(drawFrameState)
            , m_renderObjectBuffer(renderObjectBuffer)
            , m_transformBuffer(transformBuffer)
            , m_viewProjectionBuffer(viewProjectionBuffer)
            , m_visibleObjectCountBuffer(visibleObjectCountBuffer)
            , m_maxObjectCount(maxObjectCount)
            , m_maxIndirectCommandCount(maxIndirectCommandCount)
        {}

        const char* name() const noexcept override
        {
            return "StaticMeshMeshletCulling";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.read_buffer(m_renderObjectBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_transformBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_viewProjectionBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("MeshPool.Meshlet", m_meshletBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_visibleObjectCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc commandBufferDesc{};
            commandBufferDesc.name = "IndirectCommandBuffer";
            commandBufferDesc.type = RHI::BufferType::UnorderedAccess;
            commandBufferDesc.defaultHeapCount = 1;
            commandBufferDesc.uploadHeapCount = 0;
            commandBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            commandBufferDesc.stride = sizeof(GpuData::IndirectCommand);
            commandBufferDesc.elementCount = m_maxIndirectCommandCount;
            commandBufferDesc.size =
                commandBufferDesc.stride * commandBufferDesc.elementCount;
            commandBufferDesc.alignment = alignof(GpuData::IndirectCommand);
            result = builder.create_buffer(commandBufferDesc, m_indirectCommandBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc countBufferDesc{};
            countBufferDesc.name = "IndirectCommandCountBuffer";
            countBufferDesc.type = RHI::BufferType::Raw;
            countBufferDesc.defaultHeapCount = 1;
            countBufferDesc.uploadHeapCount = 0;
            countBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            countBufferDesc.stride = sizeof(uint32_t);
            countBufferDesc.elementCount = 1;
            countBufferDesc.size = sizeof(uint32_t);
            countBufferDesc.alignment = alignof(uint32_t);
            result =
                builder.create_buffer(countBufferDesc, m_indirectCommandCountBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc countUavDesc{};
            countUavDesc.name = "IndirectCommandCountBufferUAV";
            countUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            countUavDesc.bufferKind = RHI::BufferKind::Buffer;
            countUavDesc.bufferHandle = m_indirectCommandCountBuffer;
            countUavDesc.numElements = countBufferDesc.size / sizeof(uint32_t);
            result = builder.create_view(countUavDesc, m_indirectCommandCountUav);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "StaticMeshMeshletCullingRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 4 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 1 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "StaticMeshMeshletCullingCS";
            shaderDesc.filePath = "Shaders/D3D12/StaticMeshMeshletCulling.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "StaticMeshMeshletCullingPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_renderObjectBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_transformBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_viewProjectionBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_meshRangeBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_meshletBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_visibleObjectCountBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_indirectCommandBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::IndirectArgument);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_indirectCommandCountBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::IndirectArgument);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const uint32_t clearValues[4] = { 0, 0, 0, 0 };
            commandContext->clear_unordered_access_uint(
                m_indirectCommandCountUav,
                clearValues);

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, m_maxIndirectCommandCount);
            commandContext->set_cbv(1, m_viewProjectionBuffer);
            commandContext->set_srv(2, m_renderObjectBuffer);
            commandContext->set_srv(3, m_transformBuffer);
            commandContext->set_srv(4, m_meshRangeBuffer);
            commandContext->set_srv(5, m_visibleObjectCountBuffer);
            commandContext->set_srv(6, m_meshletBuffer);
            commandContext->set_uav(7, m_indirectCommandBuffer);
            commandContext->set_uav(8, m_indirectCommandCountBuffer);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
        }

    private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_transformBuffer{};
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_meshRangeBuffer{};
        RHI::BufferHandle m_meshletBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        uint32_t m_maxObjectCount = 0;
        uint32_t m_maxIndirectCommandCount = 0;
        RHI::BufferHandle m_indirectCommandBuffer{};
        RHI::BufferHandle m_indirectCommandCountBuffer{};
        RHI::ViewHandle m_indirectCommandCountUav{};
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };
}
