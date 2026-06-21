#pragma once

/// ****************************************************************************
/// Generate visible render object list
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"

namespace Cue::DrawSystem
{
    class GenerateVisibleListPass final : public RHI::FrameGraphPass
    {
    public:
        GenerateVisibleListPass(const DrawFrameState& drawFrameState,
            RHI::BufferHandle renderableInfoBuffer,
            RHI::BufferHandle viewProjectionBuffer,
            RHI::BufferHandle renderObjectBuffer,
            RHI::BufferHandle visibleObjectCountBuffer,
            RHI::ViewHandle visibleObjectCountUav)
            : m_drawFrameState(drawFrameState)
            , m_renderableInfoBuffer(renderableInfoBuffer)
            , m_viewProjectionBuffer(viewProjectionBuffer)
            , m_renderObjectBuffer(renderObjectBuffer)
            , m_visibleObjectCountBuffer(visibleObjectCountBuffer)
            , m_visibleObjectCountUav(visibleObjectCountUav)
        {}

        const char* name() const noexcept override { return "GenerateVisibleList"; }
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
                    "Hi-Z tile count must not be zero.");
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
            result = builder.read_buffer(m_renderObjectBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.read_buffer(m_visibleObjectCountBuffer);
            if (!result)
            {
                return result;
            }

            result = builder.get_buffer("HiZDepthBuffer", m_hizDepthBuffer);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "GenerateVisibleListRootSignature";
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 1 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 2 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 3 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 4 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1 });
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
            shaderDesc.name = "GenerateVisibleObjectListCS";
            shaderDesc.filePath = "Shaders/D3D12/GenerateVisibleObjectList.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "GenerateVisibleListPipeline";
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

            result = builder.use_buffer(
                m_renderObjectBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            result = builder.use_buffer(
                m_visibleObjectCountBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_hizDepthBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
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
                m_visibleObjectCountUav, clearValues);

            const DrawFrameData& frameState =
                m_drawFrameState.frame_state(context.frame_index());
            if (frameState.objectCount == 0)
            {
                return;
            }

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, frameState.objectCount);
            commandContext->set_cbv(1, m_viewProjectionBuffer);
            commandContext->set_32bit_constant(2, m_tileCountX);
            commandContext->set_32bit_constant(3, m_tileCountY);
            commandContext->set_32bit_constant(4, m_tileSize);
            commandContext->set_srv(5, m_renderableInfoBuffer);
            commandContext->set_srv(6, m_hizDepthBuffer);
            commandContext->set_uav(7, m_renderObjectBuffer);
            commandContext->set_uav(8, m_visibleObjectCountBuffer);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
        }

    private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderableInfoBuffer{};
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::BufferHandle m_hizDepthBuffer{};
        RHI::ViewHandle m_visibleObjectCountUav{};
        uint32_t m_tileSize = 8u;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };
}
