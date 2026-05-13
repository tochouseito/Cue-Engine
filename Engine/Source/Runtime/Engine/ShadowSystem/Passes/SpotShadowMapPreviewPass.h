#pragma once

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include <ShadowSystem/GpuData/ShadowData.h>
#include <ShadowSystem/ShadowBindings.h>

namespace Cue::ShadowSystem
{
    class SpotShadowMapPreviewPass final : public RHI::FrameGraphPass
    {
    public:
        explicit SpotShadowMapPreviewPass(
            const ShadowBindings& a_shadowBindings) noexcept
            : m_shadowBindings(a_shadowBindings)
        {}

        const char* name() const noexcept override
        {
            return "SpotShadowMapPreview";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.get_texture("SpotShadowMap", m_shadowMapHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("SpotShadowMapSRV", m_shadowMapSrvHandle);
            if (!result)
            {
                return result;
            }

            RHI::TextureDesc previewDesc{};
            previewDesc.name = "SpotShadowMapPreview";
            previewDesc.bufferCount = 1;
            previewDesc.kind = RHI::TextureKind::RenderTarget;
            previewDesc.width = GpuData::k_spotShadowMapSize;
            previewDesc.height = GpuData::k_spotShadowMapSize;
            previewDesc.format = RHI::ColorFormat::R8G8B8A8_UNORM;
            previewDesc.clearColor[0] = 0.02f;
            previewDesc.clearColor[1] = 0.02f;
            previewDesc.clearColor[2] = 0.02f;
            previewDesc.clearColor[3] = 1.0f;
            result = builder.create_texture(previewDesc, m_previewHandle);
            if (!result)
            {
                return result;
            }
            result = builder.render(&m_previewHandle, 1);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc previewRtvDesc{};
            previewRtvDesc.name = "SpotShadowMapPreviewRTV";
            previewRtvDesc.type = RHI::ViewType::RenderTarget;
            previewRtvDesc.bufferKind = RHI::BufferKind::Texture;
            previewRtvDesc.textureHandle = m_previewHandle;
            previewRtvDesc.colorFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
            result = builder.create_view(previewRtvDesc, m_previewRtvHandle);
            if (!result)
            {
                return result;
            }

            RHI::ViewDesc previewSrvDesc{};
            previewSrvDesc.name = "SpotShadowMapPreviewSRV";
            previewSrvDesc.type = RHI::ViewType::ShaderResourceTexture2D;
            previewSrvDesc.bufferKind = RHI::BufferKind::Texture;
            previewSrvDesc.textureHandle = m_previewHandle;
            previewSrvDesc.colorFormat = RHI::ColorFormat::R8G8B8A8_UNORM;
            result = builder.create_view(previewSrvDesc, m_previewSrvHandle);
            if (!result)
            {
                return result;
            }

            RHI::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "SpotShadowMapPreviewRootSignature";
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::DescriptorTableSRV,
                RHI::ShaderVisibility::Pixel,
                0,
                1,
                0 });
            rootSignatureDesc.parameters.push_back(RHI::RootParameterDesc{
                RHI::RootParameterType::SRV, RHI::ShaderVisibility::Pixel, 1 });
            result =
                builder.create_root_signature(rootSignatureDesc, m_rootSignatureHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "SpotShadowMapPreviewVS";
            vertexShaderDesc.filePath = "Shaders/D3D12/SpotShadowMapPreview.hlsl";
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_vertexShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "SpotShadowMapPreviewPS";
            pixelShaderDesc.filePath = "Shaders/D3D12/SpotShadowMapPreview.hlsl";
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_pixelShaderHandle);
            if (!result)
            {
                return result;
            }

            RHI::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "SpotShadowMapPreviewPipeline";
            pipelineDesc.rootSignatureHandle = m_rootSignatureHandle;
            pipelineDesc.vsHandle = m_vertexShaderHandle;
            pipelineDesc.psHandle = m_pixelShaderHandle;
            pipelineDesc.rasterizerState.cullMode = RHI::CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask =
                RHI::DepthWriteMask::Zero;
            pipelineDesc.blendMode = { RHI::BlendMode::None };
            pipelineDesc.rtvFormats = { RHI::ColorFormat::R8G8B8A8_UNORM };
            return builder.create_graphics_pipeline(pipelineDesc, m_pipelineHandle);
        }

        Result describe_resources(RHI::FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_shadowMapHandle,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::ShaderResource);
            if (!result)
            {
                return result;
            }
            result = builder.use_buffer(
                m_shadowBindings.spotShadowFrameBuffer,
                RHI::ResourceAccessType::Read,
                RHI::ResourceState::ShaderResource,
                RHI::ResourceState::Common);
            if (!result)
            {
                return result;
            }
            return builder.use_texture(
                m_previewHandle,
                RHI::ResourceAccessType::Write,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
        }

        void execute(RHI::FrameGraphContext& context) override
        {
            RHI::ICommandContext* commandContext = context.commandContext();
            if (commandContext == nullptr)
            {
                return;
            }

            static constexpr Math::float4 k_clearColor =
                Math::float4(0.02f, 0.02f, 0.02f, 1.0f);
            commandContext->clear_render_target(
                m_previewRtvHandle, k_clearColor.data());
            commandContext->set_render_targets(&m_previewRtvHandle, 1, {});
            commandContext->set_viewport_scissor(
                GpuData::k_spotShadowMapSize,
                GpuData::k_spotShadowMapSize);
            commandContext->set_graphics_pipeline(m_pipelineHandle);
            commandContext->set_primitive_topology(
                RHI::PrimitiveTopologyType::Triangle);
            commandContext->set_graphics_descriptor_table(0, m_shadowMapSrvHandle);
            commandContext->set_srv(1, m_shadowBindings.spotShadowFrameBuffer);
            commandContext->draw_instanced(3, 1, 0, 0);
        }

    private:
        ShadowBindings m_shadowBindings{};
        RHI::TextureHandle m_shadowMapHandle{};
        RHI::TextureHandle m_previewHandle{};
        RHI::ViewHandle m_shadowMapSrvHandle{};
        RHI::ViewHandle m_previewRtvHandle{};
        RHI::ViewHandle m_previewSrvHandle{};
        RHI::RootSignatureHandle m_rootSignatureHandle{};
        RHI::ShaderBlobHandle m_vertexShaderHandle{};
        RHI::ShaderBlobHandle m_pixelShaderHandle{};
        RHI::PipelineStateHandle m_pipelineHandle{};
    };
}
