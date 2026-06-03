#pragma once

/// ****************************************************************************
/// Build coarse Hi-Z depth buffer from the current scene depth
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === C++ includes ===
#include <vector>

namespace Cue::DrawSystem
{
    class InitializeHiZDepthPass final : public RHI::FrameGraphPass
    {
    public:
        InitializeHiZDepthPass() = default;

        const char* name() const noexcept override { return "InitializeHiZDepth"; }
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
                    "Hi-Z depth buffer tile count must not be zero.");
            }

            RHI::BufferDesc hizDepthBufferDesc{};
            hizDepthBufferDesc.name = "HiZDepthBuffer";
            hizDepthBufferDesc.type = RHI::BufferType::Raw;
            hizDepthBufferDesc.defaultHeapCount = builder.buffer_count();
            hizDepthBufferDesc.uploadHeapCount = 0;
            hizDepthBufferDesc.initialState = RHI::ResourceState::UnorderedAccess;
            hizDepthBufferDesc.stride = sizeof(uint32_t);
            hizDepthBufferDesc.elementCount = m_tileCountX * m_tileCountY;
            hizDepthBufferDesc.size =
                hizDepthBufferDesc.stride *
                hizDepthBufferDesc.elementCount;
            hizDepthBufferDesc.alignment = alignof(uint32_t);
            Result result =
                builder.create_buffer(hizDepthBufferDesc, m_hizDepthBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc hizUavDesc{};
            hizUavDesc.name = "HiZDepthBufferUAV";
            hizUavDesc.type = RHI::ViewType::UnorderedAccessRawBuffer;
            hizUavDesc.bufferKind = RHI::BufferKind::Buffer;
            hizUavDesc.bufferHandle = m_hizDepthBuffer;
            hizUavDesc.numElements = m_tileCountX * m_tileCountY;
            result = builder.create_view(hizUavDesc, m_hizDepthUav);
            if (!result)
            {
                return result;
            }

            m_initialized.assign(builder.buffer_count(), 0u);
            return Result::ok();
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            return builder.use_buffer(
                m_hizDepthBuffer,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::UnorderedAccess,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            if (context.frame_index() >= m_initialized.size() ||
                m_initialized[context.frame_index()] != 0u)
            {
                return;
            }

            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            const uint32_t clearValues[4] = { 0u, 0u, 0u, 0u };
            commandContext->clear_unordered_access_uint(
                m_hizDepthUav,
                clearValues);
            m_initialized[context.frame_index()] = 1u;
        }

    private:
        RHI::BufferHandle m_hizDepthBuffer{};
        RHI::ViewHandle m_hizDepthUav{};
        uint32_t m_tileSize = 16u;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        std::vector<uint8_t> m_initialized{};
    };

    class BuildHiZDepthPass final : public RHI::FrameGraphPass
    {
    public:
        BuildHiZDepthPass() = default;

        const char* name() const noexcept override { return "BuildHiZDepth"; }
        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Compute;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            m_depthWidth = builder.width();
            m_depthHeight = builder.height();
            m_tileCountX = (m_depthWidth + m_tileSize - 1u) / m_tileSize;
            m_tileCountY = (m_depthHeight + m_tileSize - 1u) / m_tileSize;
            if (m_depthWidth == 0u || m_depthHeight == 0u ||
                m_tileCountX == 0u || m_tileCountY == 0u)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Hi-Z depth pass dimensions must not be zero.");
            }

            Result result = builder.get_texture("SceneDepth", m_sceneDepth);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("HiZDepthBuffer", m_hizDepthBuffer);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc depthSrvDesc{};
            depthSrvDesc.name = "SceneDepthSRV";
            depthSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
            depthSrvDesc.bufferKind = RHI::BufferKind::Texture;
            depthSrvDesc.textureHandle = m_sceneDepth;
            depthSrvDesc.colorFormat = RHI::ColorFormat::R24_UNorm_X8_Typeless;
            depthSrvDesc.mipSlice = 0;
            depthSrvDesc.mipLevels = 1;
            result = builder.create_view(depthSrvDesc, m_sceneDepthSrv);
            if (!result)
            {
                return result;
            }

            result = builder.get_view("HiZDepthBufferUAV", m_hizDepthUav);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "BuildHiZDepthRootSignature";
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
                { RHI::RootParameterType::DescriptorTableSRV,
                    RHI::ShaderVisibility::All, 0 });
            rootSignatureDesc.parameters.push_back(
                { RHI::RootParameterType::UAV, RHI::ShaderVisibility::All, 0 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignature);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc shaderDesc{};
            shaderDesc.name = "BuildHiZDepthCS";
            shaderDesc.filePath = "Shaders/D3D12/BuildHiZDepth.hlsl";
            shaderDesc.entryPoint = "CSMain";
            shaderDesc.targetProfile = "cs_6_0";
            result = builder.create_shader_blob(shaderDesc, m_computeShader);
            if (!result)
            {
                return result;
            }

            RHI::ComputePipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "BuildHiZDepthPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignature;
            pipelineDesc.csHandle = m_computeShader;
            return builder.create_compute_pipeline(pipelineDesc, m_pipeline);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_sceneDepth,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }

            return builder.use_buffer(
                m_hizDepthBuffer,
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

            commandContext->set_compute_pipeline(m_pipeline);
            commandContext->set_32bit_constant(0, m_depthWidth);
            commandContext->set_32bit_constant(1, m_depthHeight);
            commandContext->set_32bit_constant(2, m_tileCountX);
            commandContext->set_32bit_constant(3, m_tileCountY);
            commandContext->set_32bit_constant(4, m_tileSize);
            commandContext->set_compute_descriptor_table(5, m_sceneDepthSrv);
            commandContext->set_uav(6, m_hizDepthBuffer);
            commandContext->dispatch(
                (m_tileCountX + 7u) / 8u,
                (m_tileCountY + 7u) / 8u,
                1u);
        }

    private:
        RHI::TextureHandle m_sceneDepth{};
        RHI::BufferHandle m_hizDepthBuffer{};
        RHI::ViewHandle m_sceneDepthSrv{};
        RHI::ViewHandle m_hizDepthUav{};
        uint32_t m_depthWidth = 0;
        uint32_t m_depthHeight = 0;
        uint32_t m_tileSize = 16u;
        uint32_t m_tileCountX = 0;
        uint32_t m_tileCountY = 0;
        RHI::RootSignatureHandle m_rootSignature{};
        RHI::ShaderBlobHandle m_computeShader{};
        RHI::PipelineStateHandle m_pipeline{};
    };
}
