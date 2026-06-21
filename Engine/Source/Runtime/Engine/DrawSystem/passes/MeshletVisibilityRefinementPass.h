#pragma once

/// ****************************************************************************
/// Meshlet bounds based object visibility refinement
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"

// === C++ includes ===
#include <cstring>

namespace Cue::DrawSystem
{
    class MeshletVisibilityRefinementPass final : public RHI::FrameGraphPass
    {
    public:
        MeshletVisibilityRefinementPass(
            const DrawFrameState& drawFrameState,
            RHI::BufferHandle renderObjectBuffer,
            RHI::BufferHandle transformBuffer,
            RHI::BufferHandle viewProjectionBuffer,
            RHI::BufferHandle visibleObjectCountBuffer)
            : m_drawFrameState(drawFrameState)
            , m_renderObjectBuffer(renderObjectBuffer)
            , m_transformBuffer(transformBuffer)
            , m_viewProjectionBuffer(viewProjectionBuffer)
            , m_visibleObjectCountBuffer(visibleObjectCountBuffer)
        {}

        const char* name() const noexcept override
        {
            return "MeshletVisibilityRefinement";
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
                    "Meshlet visibility tile count must not be zero.");
            }

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
            result = builder.read_buffer(m_visibleObjectCountBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("MeshPool.MeshRange", m_meshRangeBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("MeshPool.MeshletBounds", m_meshletBoundsBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("HiZDepthBuffer", m_hizDepthBuffer);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer(
                "MeshletRefinedVisibilityBuffer",
                m_refinedVisibilityBuffer);
            if (!result)
            {
                return result;
            }
            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "MeshletVisibilityRefinementRootSignature";
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
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 4 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::_32BitConstants,
                    RHI::ShaderVisibility::All, 5 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 6 });
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
                { RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 5 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "MeshletVisibilityRefinementCS";
            shaderDesc.filePath = "Shaders/D3D12/MeshletVisibilityRefinement.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "MeshletVisibilityRefinementPipeline";
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
                m_visibleObjectCountBuffer,
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
                m_meshletBoundsBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_hizDepthBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            return builder.use_buffer(
                m_refinedVisibilityBuffer,
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
            commandContext->set_32bit_constant(4, m_minMeshletCount);
            commandContext->set_32bit_constant(
                5,
                float_to_uint32(m_minProjectedRadius));
            commandContext->set_cbv(6, m_viewProjectionBuffer);
            commandContext->set_srv(7, m_renderObjectBuffer);
            commandContext->set_srv(8, m_transformBuffer);
            commandContext->set_srv(9, m_visibleObjectCountBuffer);
            commandContext->set_srv(10, m_meshRangeBuffer);
            commandContext->set_srv(11, m_meshletBoundsBuffer);
            commandContext->set_srv(12, m_hizDepthBuffer);
            commandContext->set_uav(13, m_refinedVisibilityBuffer);
            commandContext->dispatch((frameState.objectCount + 63u) / 64u, 1, 1);
        }

    private:
        const DrawFrameState& m_drawFrameState;
        RHI::BufferHandle m_renderObjectBuffer{};
        RHI::BufferHandle m_transformBuffer{};
        RHI::BufferHandle m_viewProjectionBuffer{};
        RHI::BufferHandle m_visibleObjectCountBuffer{};
        RHI::BufferHandle m_meshRangeBuffer{};
        RHI::BufferHandle m_meshletBoundsBuffer{};
        RHI::BufferHandle m_hizDepthBuffer{};
        RHI::BufferHandle m_refinedVisibilityBuffer{};
        uint32_t m_tileSize = 8u;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        uint32_t m_minMeshletCount = 1u;
        float m_minProjectedRadius = 0.0f;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};

        static uint32_t float_to_uint32(float value) noexcept
        {
            uint32_t result = 0;
            std::memcpy(&result, &value, sizeof(result));
            return result;
        }
    };
}
