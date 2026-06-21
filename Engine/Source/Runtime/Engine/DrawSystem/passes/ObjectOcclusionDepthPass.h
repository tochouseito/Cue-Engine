#pragma once

/// ****************************************************************************
/// Object-space occlusion depth prepass
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"

namespace Cue::DrawSystem
{
    class ObjectOcclusionDepthPass final : public RHI::FrameGraphPass
    {
    public:
        ObjectOcclusionDepthPass(
            const DrawFrameState& drawFrameState,
            RHI::BufferHandle renderableInfoBuffer,
            RHI::BufferHandle viewProjectionBuffer)
            : m_drawFrameState(drawFrameState)
            , m_renderableInfoBuffer(renderableInfoBuffer)
            , m_viewProjectionBuffer(viewProjectionBuffer)
        {}

        const char* name() const noexcept override
        {
            return "ObjectOcclusionDepth";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            m_tileCountX = (builder.width() + m_tileSize - 1u) / m_tileSize;
            m_tileCountY = (builder.height() + m_tileSize - 1u) / m_tileSize;
            if (m_tileCountX == 0u || m_tileCountY == 0u)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Object occlusion tile count must not be zero.");
            }

            Result result = builder.read_buffer(m_renderableInfoBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_viewProjectionBuffer);
            if (!result)
            {
                return result;
            }

            RHI::BufferDesc depthBufferDesc{};
            depthBufferDesc.name = "ObjectOcclusionDepthBuffer";
            depthBufferDesc.type = RHI::BufferType::Raw;
            depthBufferDesc.defaultHeapCount = 1;
            depthBufferDesc.uploadHeapCount = 0;
            depthBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            depthBufferDesc.stride = sizeof(uint32_t);
            depthBufferDesc.elementCount = m_tileCountX * m_tileCountY;
            depthBufferDesc.size =
                depthBufferDesc.stride * depthBufferDesc.elementCount;
            depthBufferDesc.alignment = alignof(uint32_t);
            result = builder.create_buffer(depthBufferDesc, m_occlusionDepthBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc depthUavDesc{};
            depthUavDesc.name = "ObjectOcclusionDepthBufferUAV";
            depthUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            depthUavDesc.bufferKind = RHI::BufferKind::Buffer;
            depthUavDesc.bufferHandle = m_occlusionDepthBuffer;
            depthUavDesc.numElements = depthBufferDesc.elementCount;
            result = builder.create_view(depthUavDesc, m_occlusionDepthUav);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ObjectOcclusionDepthRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 4 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "ObjectOcclusionDepthCS";
            shaderDesc.filePath = "Shaders/D3D12/ObjectOcclusionDepthPrepass.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ObjectOcclusionDepthPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_buffer(
                m_renderableInfoBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_viewProjectionBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_occlusionDepthBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const uint32_t clearValues[4] = {
                0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu
            };
            commandContext->clear_unordered_access_uint(
                m_occlusionDepthUav,
                clearValues);

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, frameState.objectCount);
            commandContext->set_32bit_constant(1, m_tileCountX);
            commandContext->set_32bit_constant(2, m_tileCountY);
            commandContext->set_32bit_constant(3, m_tileSize);
            commandContext->set_cbv(4, m_viewProjectionBuffer);
            commandContext->set_srv(5, m_renderableInfoBuffer);
            commandContext->set_uav(6, m_occlusionDepthBuffer);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
        }

    private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderableInfoBuffer{};
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_occlusionDepthBuffer{};
        RHI::ViewHandle m_occlusionDepthUav{};
        uint32_t m_tileSize = 8u;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };
}
