#pragma once

/// ****************************************************************************
/// Static mesh forward pass
/// ****************************************************************************

// === RHI includes ===
#include <FrameGraph.h>

// === Engine includes ===
#include "DrawSystem/DrawFrameState.h"
#include "DrawSystem/passes/ClusteredLightingPass.h"

// === C++ includes ===
#include <cmath>
#include <cstring>

namespace Cue::DrawSystem
{
class StaticMeshForwardPass final : public RHI::FrameGraphPass
{
  public:
    StaticMeshForwardPass(const DrawFrameState &drawFrameState,
                          RHI::BufferHandle renderObjectBuffer,
                          RHI::BufferHandle transformBuffer,
                          RHI::BufferHandle viewProjectionBuffer,
                          RHI::BufferHandle visibleObjectCountBuffer,
                          RHI::BufferHandle materialBuffer,
                          RHI::BufferHandle lightFrameBuffer,
                          RHI::BufferHandle directionalLightBuffer,
                          RHI::BufferHandle pointLightBuffer,
                          uint32_t maxIndirectCommandCount)
        : m_drawFrameState(drawFrameState),
          m_renderObjectBuffer(renderObjectBuffer),
          m_transformBuffer(transformBuffer),
          m_viewProjectionBuffer(viewProjectionBuffer),
          m_visibleObjectCountBuffer(visibleObjectCountBuffer),
          m_materialBuffer(materialBuffer),
          m_lightFrameBuffer(lightFrameBuffer),
          m_directionalLightBuffer(directionalLightBuffer),
          m_pointLightBuffer(pointLightBuffer),
          m_maxIndirectCommandCount(maxIndirectCommandCount)
    {
    }

    const char *name() const noexcept override
    {
        return "StaticMeshForward";
    }
    RHI::CommandListType type() const noexcept override
    {
        return RHI::CommandListType::Graphics;
    }

    Result setup(RHI::FrameGraphBuilder &builder) override
    {
        Result result = builder.get_texture("FinalColor", m_color);
        if (!result)
        {
            return result;
        }
        result = builder.render(&m_color, 1);
        if (!result)
        {
            return result;
        }
        result = builder.get_view("FinalColorRTV", m_colorRtv);
        if (!result)
        {
            return result;
        }

        result = builder.get_texture("SceneDepth", m_depth);
        if (!result)
        {
            return result;
        }
        result = builder.get_view("SceneDepthDSV", m_depthDsv);
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
        result = builder.read_buffer(m_visibleObjectCountBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.read_buffer(m_materialBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.read_buffer(m_lightFrameBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.read_buffer(m_directionalLightBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.read_buffer(m_pointLightBuffer);
        if (!result)
        {
            return result;
        }

        result = builder.get_buffer("MeshPool.Position", m_positionBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.get_buffer("MeshPool.Uv", m_uvBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.get_buffer("MeshPool.Normal", m_normalBuffer);
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
        result = builder.get_buffer("ClusterLightRangeBuffer",
                                    m_clusterLightRangeBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.get_buffer("ClusterLightIndexBuffer",
                                    m_clusterLightIndexBuffer);
        if (!result)
        {
            return result;
        }

        m_screenWidth = builder.width();
        m_screenHeight = builder.height();

        // Pixel shader が screen position と view-space z から cluster id を
        // 求めるための固定 grid 設定。near/far は projection 行列から毎 pixel
        // 復元せず、root constants として渡す。
        m_clusterTileCountX = ClusteredLighting::k_clusterCountX;
        m_clusterTileCountY = ClusteredLighting::k_clusterCountY;
        m_clusterDepthSliceCount = ClusteredLighting::k_depthSliceCount;
        m_clusterNearZ = 0.01f;
        m_clusterFarZ = 100.0f;
        m_clusterInvLogFarNear =
            1.0f / std::log(m_clusterFarZ / m_clusterNearZ);

        RHI::RootSignatureDesc rootSignatureDesc{};
        rootSignatureDesc.name = "StaticMeshForwardRootSignature";
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
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 2});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 3});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::CBV, RHI::ShaderVisibility::All, 2});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 4});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 5});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 6});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 3});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 4});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 5});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 6});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 7});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 8});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 9});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::_32BitConstants,
             RHI::ShaderVisibility::All, 10});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 7});
        rootSignatureDesc.parameters.push_back(
            {RHI::RootParameterType::SRV, RHI::ShaderVisibility::All, 8});
        result =
            builder.create_root_signature(rootSignatureDesc, m_rootSignature);
        if (!result)
        {
            return result;
        }

        RHI::ShaderCompileDesc vsDesc{};
        vsDesc.name = "StaticMeshForwardVS";
        vsDesc.filePath = "Shaders/D3D12/StaticMeshForward.hlsl";
        vsDesc.entryPoint = "vs_main";
        vsDesc.targetProfile = "vs_6_0";
        result = builder.create_shader_blob(vsDesc, m_vertexShader);
        if (!result)
        {
            return result;
        }

        RHI::ShaderCompileDesc psDesc{};
        psDesc.name = "StaticMeshForwardPS";
        psDesc.filePath = "Shaders/D3D12/StaticMeshForward.hlsl";
        psDesc.entryPoint = "ps_main";
        psDesc.targetProfile = "ps_6_0";
        result = builder.create_shader_blob(psDesc, m_pixelShader);
        if (!result)
        {
            return result;
        }

        RHI::GraphicsPipelineStateDesc pipelineDesc{};
        pipelineDesc.name = "StaticMeshForwardPipeline";
        pipelineDesc.rootSignatureHandle = m_rootSignature;
        pipelineDesc.vsHandle = m_vertexShader;
        pipelineDesc.psHandle = m_pixelShader;
        pipelineDesc.inputElements = {
            {"POSITION", 0, RHI::InputElementFormat::R32G32B32A32_Float, 0, 0},
            {"TEXCOORD", 0, RHI::InputElementFormat::R32G32_Float, 1, 0},
            {"NORMAL", 0, RHI::InputElementFormat::R32G32B32_Float, 2, 0},
        };
        pipelineDesc.rasterizerState.cullMode = RHI::CullMode::Back;
        pipelineDesc.depthStencilState.depthEnable = true;
        pipelineDesc.depthStencilState.depthWriteMask =
            RHI::DepthWriteMask::All;
        pipelineDesc.depthStencilState.depthFunc =
            RHI::ComparisonFunc::LessEqual;
        pipelineDesc.dsvFormat = RHI::ColorFormat::D24_UNorm_S8_UInt;
        pipelineDesc.blendMode = {RHI::BlendMode::None};
        pipelineDesc.rtvFormats = {RHI::ColorFormat::R8G8B8A8_UNORM};
        return builder.create_graphics_pipeline(pipelineDesc, m_pipeline);
    }

    Result describe_resources(RHI::FrameGraphBuilder &builder) override
    {
        Result result = builder.use_texture(
            m_color, RHI::ResourceAccessType::Write,
            RHI::ResourceState::RenderTarget, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_texture(m_depth, RHI::ResourceAccessType::Write,
                                     RHI::ResourceState::DepthWrite,
                                     RHI::ResourceState::Common);
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
            m_visibleObjectCountBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result =
            builder.use_buffer(m_materialBuffer, RHI::ResourceAccessType::Read,
                               RHI::ResourceState::ShaderResource,
                               RHI::ResourceState::ShaderResource);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_lightFrameBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_directionalLightBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_pointLightBuffer, RHI::ResourceAccessType::Read,
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
        result = builder.use_buffer(m_uvBuffer, RHI::ResourceAccessType::Read,
                                    RHI::ResourceState::VertexBuffer,
                                    RHI::ResourceState::VertexBuffer);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_normalBuffer, RHI::ResourceAccessType::Read,
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
        result = builder.use_buffer(
            m_renderObjectIndexBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource, RHI::ResourceState::Common);
        if (!result)
        {
            return result;
        }
        result = builder.use_buffer(
            m_clusterLightRangeBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource,
            RHI::ResourceState::ShaderResource);
        if (!result)
        {
            return result;
        }
        return builder.use_buffer(
            m_clusterLightIndexBuffer, RHI::ResourceAccessType::Read,
            RHI::ResourceState::ShaderResource,
            RHI::ResourceState::ShaderResource);
    }

    void execute(RHI::FrameGraphContext &context) override
    {
        RHI::ICommandContext *commandContext = context.commandContext();
        if (commandContext == nullptr)
        {
            return;
        }

        static constexpr Math::float4 k_clearColor =
            Math::float4::from_rgba8(0, 0, 0, 255);
        commandContext->clear_render_target(m_colorRtv, k_clearColor.data());
        commandContext->clear_depth_stencil(m_depthDsv, 1.0f, 0);
        commandContext->set_render_targets(&m_colorRtv, 1, m_depthDsv);
        commandContext->set_viewport_scissor(context.width(), context.height());
        commandContext->set_graphics_pipeline(m_pipeline);
        commandContext->set_primitive_topology(
            RHI::PrimitiveTopologyType::Triangle);
        commandContext->set_32bit_constant(0, 0xffffffffu);
        commandContext->set_cbv(1, m_viewProjectionBuffer);
        commandContext->set_srv(2, m_renderObjectBuffer);
        commandContext->set_srv(3, m_transformBuffer);
        commandContext->set_srv(4, m_visibleObjectCountBuffer);
        commandContext->set_srv(5, m_materialBuffer);
        commandContext->set_cbv(6, m_lightFrameBuffer);
        commandContext->set_srv(7, m_directionalLightBuffer);
        commandContext->set_srv(8, m_pointLightBuffer);
        commandContext->set_srv(9, m_renderObjectIndexBuffer);
        commandContext->set_32bit_constant(10, m_screenWidth);
        commandContext->set_32bit_constant(11, m_screenHeight);
        commandContext->set_32bit_constant(12, m_clusterTileCountX);
        commandContext->set_32bit_constant(13, m_clusterTileCountY);
        commandContext->set_32bit_constant(14, m_clusterDepthSliceCount);

        // float root constant は RHI API 上 uint32_t として渡す。
        // shader 側では同じ bit pattern を float cbuffer として読む。
        commandContext->set_32bit_constant(15, float_to_uint32(m_clusterNearZ));
        commandContext->set_32bit_constant(16, float_to_uint32(m_clusterFarZ));
        commandContext->set_32bit_constant(
            17, float_to_uint32(m_clusterInvLogFarNear));
        commandContext->set_srv(18, m_clusterLightRangeBuffer);
        commandContext->set_srv(19, m_clusterLightIndexBuffer);
        commandContext->set_vertex_buffer(0, m_positionBuffer);
        commandContext->set_vertex_buffer(1, m_uvBuffer);
        commandContext->set_vertex_buffer(2, m_normalBuffer);
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
    const DrawFrameState &m_drawFrameState;
    RHI::TextureHandle m_color{};
    RHI::TextureHandle m_depth{};
    RHI::ViewHandle m_colorRtv{};
    RHI::ViewHandle m_depthDsv{};
    RHI::BufferHandle m_renderObjectBuffer{};
    RHI::BufferHandle m_transformBuffer{};
    RHI::BufferHandle m_viewProjectionBuffer{};
    RHI::BufferHandle m_visibleObjectCountBuffer{};
    RHI::BufferHandle m_materialBuffer{};
    RHI::BufferHandle m_lightFrameBuffer{};
    RHI::BufferHandle m_directionalLightBuffer{};
    RHI::BufferHandle m_pointLightBuffer{};
    uint32_t m_maxIndirectCommandCount = 0;
    RHI::BufferHandle m_positionBuffer{};
    RHI::BufferHandle m_uvBuffer{};
    RHI::BufferHandle m_normalBuffer{};
    RHI::BufferHandle m_indexBuffer{};
    RHI::BufferHandle m_indirectCommandBuffer{};
    RHI::BufferHandle m_indirectCommandCountBuffer{};
    RHI::BufferHandle m_renderObjectIndexBuffer{};
    RHI::BufferHandle m_clusterLightRangeBuffer{};
    RHI::BufferHandle m_clusterLightIndexBuffer{};
    uint32_t m_screenWidth = 0;
    uint32_t m_screenHeight = 0;
    uint32_t m_clusterTileCountX = 0;
    uint32_t m_clusterTileCountY = 0;
    uint32_t m_clusterDepthSliceCount = 0;
    float m_clusterNearZ = 0.01f;
    float m_clusterFarZ = 100.0f;
    float m_clusterInvLogFarNear = 1.0f;
    RHI::RootSignatureHandle m_rootSignature{};
    RHI::ShaderBlobHandle m_vertexShader{};
    RHI::ShaderBlobHandle m_pixelShader{};
    RHI::PipelineStateHandle m_pipeline{};

    static uint32_t float_to_uint32(float value) noexcept
    {
        uint32_t result = 0;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }
};
} // namespace Cue::DrawSystem
