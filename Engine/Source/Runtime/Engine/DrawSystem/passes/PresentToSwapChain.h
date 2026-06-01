#pragma once

/// ************************************************************************************
/// スワップチェインへの Present パス
/// ************************************************************************************

// === RHI includes ===
#include "FrameGraph.h"

// === C++ includes ===
#include <array>

namespace Cue::RHI
{
    class PresentToSwapChainPass final : public FrameGraphPass
    {
    public:
        const char* name() const noexcept override
        {
            return "PresentToSwapChain";
        }

        CommandListType type() const noexcept override
        {
            return CommandListType::Graphics;
        }

        Result setup(FrameGraphBuilder& builder) override
        {
            // スワップチェインのバックバッファをフレームグラフに宣言する
            Result result = builder.get_texture("BackBuffer", m_backBufferHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get back buffer texture handle for present pass.");
            }
            result = builder.render(&m_backBufferHandle, 1);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to declare back buffer as render target for present pass.");
            }
            result = builder.get_view("BackBufferRTV", m_backBufferRtvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get back buffer RTV view handle for present pass.");
            }

            result = builder.get_texture("FinalColor", m_sourceColorHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get final color texture handle for present pass.");
            }
            result = builder.get_view("FinalColorRTV", m_sourceColorRtvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get final color RTV view handle for present pass.");
            }
            result = builder.get_view("FinalColorSRV", m_sourceColorSrvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get final color SRV view handle for present pass.");
            }

            RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ScreenCopyRootSignature";
            rootSignatureDesc.parameters.push_back(RootParameterDesc{ RootParameterType::DescriptorTableSRV, ShaderVisibility::Pixel, 0 });
            result = builder.create_root_signature(rootSignatureDesc, m_screenCopyRootSignatureHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create root signature for screen copy pass.");
            }

            const std::string shaderFilePath = "Shaders/D3D12/ScreenCopy.hlsl";
            if (shaderFilePath.empty())
            {
                return Result::fail(
                    Code::NotFound,
                    Severity::Error,
                    "Failed to locate ScreenCopy.hlsl for present pass.");
            }

            ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "ScreenCopyVS";
            vertexShaderDesc.filePath = shaderFilePath;
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            result = builder.create_shader_blob(vertexShaderDesc, m_screenCopyVsHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to compile screen copy vertex shader.");
            }

            ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "ScreenCopyPS";
            pixelShaderDesc.filePath = shaderFilePath;
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            result = builder.create_shader_blob(pixelShaderDesc, m_screenCopyPsHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to compile screen copy pixel shader.");
            }

            GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "ScreenCopyPipeline";
            pipelineDesc.rootSignatureHandle = m_screenCopyRootSignatureHandle;
            pipelineDesc.vsHandle = m_screenCopyVsHandle;
            pipelineDesc.psHandle = m_screenCopyPsHandle;
            pipelineDesc.rasterizerState.cullMode = CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask = DepthWriteMask::Zero;
            pipelineDesc.blendMode = { BlendMode::None };
            pipelineDesc.rtvFormats = { ColorFormat::R8G8B8A8_UNORM };
            result = builder.create_graphics_pipeline(pipelineDesc, m_screenCopyPipelineHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create graphics pipeline for screen copy pass.");
            }

            return Result::ok();
        }

        Result describe_resources(FrameGraphBuilder& builder) override
        {
            Result result = builder.use_texture(
                m_backBufferHandle,
                ResourceAccessType::Write,
                ResourceState::RenderTarget,
                ResourceState::Present);
            if (!result)
            {
                return result;
            }

            return builder.use_texture(
                m_sourceColorHandle,
                ResourceAccessType::Read,
                ResourceState::ShaderResource,
                ResourceState::Common);
        }

        void execute(FrameGraphContext& context) override
        {
            ICommandContext* commandContext = context.commandContext();

            // スワップチェイン側も別色でクリアし、コピーが失敗すると色差で分かるようにする
            commandContext->clear_render_target(
                m_backBufferRtvHandle,
                k_swapChainClearColor.data());
            //commandContext->set_render_targets(&m_backBufferRtvHandle, 1, {});
            //commandContext->set_viewport_scissor(context.width(), context.height());
            //commandContext->set_graphics_pipeline(m_screenCopyPipelineHandle);
            //commandContext->set_primitive_topology(PrimitiveTopologyType::Triangle);
            //commandContext->set_graphics_descriptor_table(0, m_sourceColorSrvHandle);
            //commandContext->draw_instanced(3, 1, 0, 0);

        }
    private:
        static constexpr Math::float4 k_swapChainClearColor = Math::float4::from_rgba8(63, 63, 63);

        TextureHandle m_backBufferHandle; // スワップチェインのバックバッファのハンドル
        ViewHandle m_backBufferRtvHandle; // スワップチェインのバックバッファの RTV ビューのハンドル
        TextureHandle m_sourceColorHandle; // コピー元となる color のハンドル
        ViewHandle m_sourceColorRtvHandle; // color の RTV ビューのハンドル
        ViewHandle m_sourceColorSrvHandle; // color の SRV ビューのハンドル
        RootSignatureHandle m_screenCopyRootSignatureHandle; // ScreenCopy 用ルートシグネチャのハンドル
        ShaderBlobHandle m_screenCopyVsHandle; // ScreenCopy の VS シェーダーハンドル
        ShaderBlobHandle m_screenCopyPsHandle; // ScreenCopy の PS シェーダーハンドル
        PipelineStateHandle m_screenCopyPipelineHandle; // ScreenCopy のグラフィックスパイプラインハンドル
    };
}
