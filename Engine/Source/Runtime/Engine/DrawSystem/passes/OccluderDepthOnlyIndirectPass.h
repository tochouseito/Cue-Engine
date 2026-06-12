#pragma once

/// ****************************************************************************
/// Occluder-only indirect depth pass for early Hi-Z culling
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/RenderFeatureSettings.h"

namespace Cue::DrawSystem
{
class SceneDepthClearPass final : public RHI::FrameGraphPass
{
  public:
    SceneDepthClearPass() = default;

    const char *name() const noexcept override
    {
        return "SceneDepthClear";
    }
    RHI::CommandListType type() const noexcept override
    {
        return RHI::CommandListType::Graphics;
    }

    Result setup(RHI::FrameGraphBuilder &builder) override
    {
        RHI::TextureDesc depthDesc{};
        depthDesc.name = "SceneDepth";
        depthDesc.bufferCount = 1;
        depthDesc.kind = RHI::TextureKind::DepthStencil;
        depthDesc.width = builder.width();
        depthDesc.height = builder.height();
        depthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
        depthDesc.clearDepth = 1.0f;
        depthDesc.clearStencil = 0;
        Result result = builder.create_texture(depthDesc, m_depth);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc depthDsvDesc{};
        depthDsvDesc.name = "SceneDepthDSV";
        depthDsvDesc.type = RHI::ViewType::DepthStencil;
        depthDsvDesc.bufferKind = RHI::BufferKind::Texture;
        depthDsvDesc.textureHandle = m_depth;
        depthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
        return builder.create_view(depthDsvDesc, m_depthDsv);
    }

    Result describe_resources(RHI::FrameGraphBuilder &builder) override
    {
        return builder.use_texture(
            m_depth, RHI::ResourceAccessType::Write,
            RHI::ResourceState::DepthWrite, RHI::ResourceState::DepthWrite);
    }

    void execute(RHI::FrameGraphContext &context) override
    {
        RHI::ICommandContext *commandContext = context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        commandContext->clear_depth_stencil(m_depthDsv, 1.0f, 0);
    }

  private:
    RHI::TextureHandle m_depth{};
    RHI::ViewHandle m_depthDsv{};
};

class OccluderDepthOnlyIndirectPass final : public RHI::FrameGraphPass
{
  public:
    OccluderDepthOnlyIndirectPass(const DrawFrameState &drawFrameState,
                                  RHI::BufferHandle renderObjectBuffer,
                                  RHI::BufferHandle transformBuffer,
                                  RHI::BufferHandle viewProjectionBuffer,
                                  uint32_t maxIndirectCommandCount,
                                  const RenderFeatureSettings &featureSettings)
        : m_drawFrameState(drawFrameState),
          m_renderObjectBuffer(renderObjectBuffer),
          m_transformBuffer(transformBuffer),
          m_viewProjectionBuffer(viewProjectionBuffer),
          m_maxIndirectCommandCount(maxIndirectCommandCount),
          m_featureSettings(featureSettings)
    {
    }

    const char *name() const noexcept override
    {
        return "OccluderDepthOnlyIndirect";
    }
    RHI::CommandListType type() const noexcept override
    {
        return RHI::CommandListType::Graphics;
    }
    bool is_enabled(uint32_t a_frameIndex) const noexcept override
    {
        a_frameIndex;
        return m_featureSettings.hiZEnabled;
    }

    Result setup(RHI::FrameGraphBuilder &builder) override
    {
        m_depthWidth = (builder.width() + k_depthScale - 1u) / k_depthScale;
        m_depthHeight = (builder.height() + k_depthScale - 1u) / k_depthScale;
        if (m_depthWidth == 0u || m_depthHeight == 0u)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                                "Occluder depth dimensions must not be zero.");
        }

        RHI::TextureDesc depthDesc{};
        depthDesc.name = "OccluderDepth";
        depthDesc.bufferCount = 1;
        depthDesc.kind = RHI::TextureKind::DepthStencil;
        depthDesc.width = m_depthWidth;
        depthDesc.height = m_depthHeight;
        depthDesc.format = RHI::ColorFormat::D24_UNorm_S8_UInt;
        depthDesc.clearDepth = 1.0f;
        depthDesc.clearStencil = 0;
        Result result = builder.create_texture(depthDesc, m_depth);
        if (!result)
        {
            return result;
        }

        RHI::ViewDesc depthDsvDesc{};
        depthDsvDesc.name = "OccluderDepthDSV";
        depthDsvDesc.type = RHI::ViewType::DepthStencil;
        depthDsvDesc.bufferKind = RHI::BufferKind::Texture;
        depthDsvDesc.textureHandle = m_depth;
        depthDsvDesc.colorFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
        result = builder.create_view(depthDsvDesc, m_depthDsv);
        if (!result)
        {
            return result;
        }

        result = builder.read_buffer(m_renderObjectBuffer);
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
        result = builder.get_buffer("MeshPool.Position", m_positionBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.get_buffer("MeshPool.Index", m_indexBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.get_buffer("IndirectCommandBuffer",
                                    m_indirectCommandBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.get_buffer("IndirectCommandCountBuffer",
                                    m_indirectCommandCountBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.get_buffer("RenderObjectIndexBuffer",
                                    m_renderObjectIndexBuffer);
        if (!result)
        {
            return result;
        }

        RHI::RootSignatureDesc rootSignatureDesc{};
        rootSignatureDesc.name = "OccluderDepthOnlyIndirectRootSignature";
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 1});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 0});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 0});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 1});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 5});
        result =
            builder.create_root_signature(rootSignatureDesc, m_rootSignature);
        if (!result)
        {
            return result;
        }

        RHI::ShaderCompileDesc vsDesc{};
        vsDesc.name = "OccluderDepthOnlyIndirectVS";
        vsDesc.filePath = "Shaders/D3D12/OccluderDepthOnlyIndirect.hlsl";
        vsDesc.entryPoint = "vs_main";
        vsDesc.targetProfile = "vs_6_0";
        result = builder.create_shader_blob(vsDesc, m_vertexShader);
        if (!result)
        {
            return result;
        }

        RHI::ShaderCompileDesc psDesc{};
        psDesc.name = "OccluderDepthOnlyIndirectPS";
        psDesc.filePath = "Shaders/D3D12/OccluderDepthOnlyIndirect.hlsl";
        psDesc.entryPoint = "ps_main";
        psDesc.targetProfile = "ps_6_0";
        result = builder.create_shader_blob(psDesc, m_pixelShader);
        if (!result)
        {
            return result;
        }

        RHI::GraphicsPipelineStateDesc pipelineDesc{};
        pipelineDesc.name = "OccluderDepthOnlyIndirectPipeline";
        pipelineDesc.rootSignatureHandle = m_rootSignature;
        pipelineDesc.vsHandle = m_vertexShader;
        pipelineDesc.psHandle = m_pixelShader;
        pipelineDesc.inputElements = {
            {"POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float, 0, 0},
        };
        pipelineDesc.rasterizerState.cullMode = RHI::CullMode::Back;
        pipelineDesc.depthStencilState.depthEnable = true;
        pipelineDesc.depthStencilState.depthWriteMask =
            RHI::DepthWriteMask::All;
        pipelineDesc.depthStencilState.depthFunc =
            RHI::ComparisonFunc::LessEqual;
        pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
        pipelineDesc.blendMode = {};
        pipelineDesc.rtvFormats = {};
        return builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
    }

    Result describe_resources(RHI::FrameGraphBuilder &builder) override
    {
        Result result = builder.use_texture(
            m_depth, RHI::ResourceAccessType::Write,
            RHI::ResourceState::DepthWrite, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_renderObjectBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_transformBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_viewProjectionBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_positionBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::VertexBuffer, RHI::ResourceState::VertexBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_indexBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::IndexBuffer, RHI::ResourceState::IndexBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_indirectCommandBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::IndirectArgument, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_indirectCommandCountBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::IndirectArgument, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        return builder.use_buffer(
            m_renderObjectIndexBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
    }

    void execute(RHI::FrameGraphContext &context) override
    {
        RHI::ICommandContext *commandContext = context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        commandContext->clear_depth_stencil(m_depthDsv, 1.0f, 0);
        commandContext->set_render_targets(nullptr, 0, m_depthDsv);
        commandContext->set_viewport_scissor(m_depthWidth, m_depthHeight);
        commandContext->set_graphics_pipeline(m_pipeline);
        commandContext->set_primitive_topology(
            RHI::PrimitiveTopologyType::Triangle);
        commandContext->set_32bit_constant(0, 0xffffffffu);
        commandContext->set_cbv(1, m_viewProjectionBuffer);
        commandContext->set_srv(2, m_renderObjectBuffer);
        commandContext->set_srv(3, m_transformBuffer);
        commandContext->set_srv(4, m_renderObjectIndexBuffer);
        commandContext->set_vertex_buffer(0, m_positionBuffer);
        commandContext->set_index_buffer(m_indexBuffer,
                                         RHI::IndexFormat::UInt32);

        const DrawFrameData &frameState =
            m_drawFrameState.frame_state(context.frame_index());
        if (frameState.objectCount == 0)
        {
            return;
        }

        commandContext->execute_indexed_indirect(m_indirectCommandBuffer,
                                                 m_indirectCommandCountBuffer,
                                                 m_maxIndirectCommandCount);
    }

  private:
    static constexpr uint32_t k_depthScale = 2u;

    const DrawFrameState &m_drawFrameState;
    RHI::TextureHandle m_depth{};
    RHI::ViewHandle m_depthDsv{};
    RHI::BufferHandle m_renderObjectBuffer{};
    RHI::BufferHandle m_transformBuffer{};
    RHI::BufferHandle m_viewProjectionBuffer{};
    uint32_t m_maxIndirectCommandCount = 0;
    const RenderFeatureSettings &m_featureSettings;
    RHI::BufferHandle m_positionBuffer{};
    RHI::BufferHandle m_indexBuffer{};
    RHI::BufferHandle m_indirectCommandBuffer{};
    RHI::BufferHandle m_indirectCommandCountBuffer{};
    RHI::BufferHandle m_renderObjectIndexBuffer{};
    uint32_t m_depthWidth = 0;
    uint32_t m_depthHeight = 0;
    RHI::RootSignatureHandle m_rootSignature{};
    RHI::ShaderBlobHandle m_vertexShader{};
    RHI::ShaderBlobHandle m_pixelShader{};
    RHI::PipelineStateHandle m_pipeline{};
};
} // namespace Cue::DrawSystem
