#pragma once

// === RHI includes ===
#include "FrameGraph.h"

// === C++ includes ===
#include <array>
#include <filesystem>

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
            // スワップチェインのバックバッファをフレームグラフに宣言する。
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

            // コピー元になる finalColor を用意し、スワップチェインとは異なる色でクリアできるようにする。
            /*TextureDesc finalColorDesc{};
            finalColorDesc.name = "FinalColor";
            finalColorDesc.bufferCount = 1;
            finalColorDesc.kind = TextureKind::RenderTarget;
            finalColorDesc.width = builder.width();
            finalColorDesc.height = builder.height();
            finalColorDesc.format = ColorFormat::R8G8B8A8_UNORM;
            finalColorDesc.clearColor[0] = k_finalColorClearColor[0];
            finalColorDesc.clearColor[1] = k_finalColorClearColor[1];
            finalColorDesc.clearColor[2] = k_finalColorClearColor[2];
            finalColorDesc.clearColor[3] = k_finalColorClearColor[3];
            result = builder.create_texture(finalColorDesc, m_finalColorHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create final color texture for present pass.");
            }

            result = builder.render(&m_finalColorHandle, 1);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to declare final color render target for present pass.");
            }

            ViewDesc finalColorRtvDesc{};
            finalColorRtvDesc.name = "FinalColorRTV";
            finalColorRtvDesc.type = ViewType::RenderTarget;
            finalColorRtvDesc.bufferKind = BufferKind::Texture;
            finalColorRtvDesc.textureHandle = m_finalColorHandle;
            finalColorRtvDesc.colorFormat = ColorFormat::R8G8B8A8_UNORM;
            result = builder.create_view(finalColorRtvDesc, m_finalColorRtvHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create final color RTV for present pass.");
            }

            ViewDesc finalColorSrvDesc{};
            finalColorSrvDesc.name = "FinalColorSRV";
            finalColorSrvDesc.type = ViewType::ShaderResourceTexture2D;
            finalColorSrvDesc.bufferKind = BufferKind::Texture;
            finalColorSrvDesc.textureHandle = m_finalColorHandle;
            finalColorSrvDesc.colorFormat = ColorFormat::R8G8B8A8_UNORM;
            finalColorSrvDesc.mipLevels = 1;
            result = builder.create_view(finalColorSrvDesc, m_finalColorSrvHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create final color SRV for present pass.");
            }*/
            result = builder.get_texture("FinalColor", m_finalColorHandle);
            if(!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get final color texture handle for present pass.");
            }
            result = builder.get_view("FinalColorRTV", m_finalColorRtvHandle);
            if (!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get final color RTV view handle for present pass.");
            }
            result = builder.get_view("FinalColorSRV", m_finalColorSrvHandle);
            if(!result)
            {
                return Result::fail(
                    Code::GetFailed,
                    Severity::Error,
                    "Failed to get final color SRV view handle for present pass.");
            }

            RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "ScreenCopyRootSignature";
            rootSignatureDesc.parameters.push_back(RootParameterDesc{ RootParameterType::SRV, ShaderVisibility::Pixel, 0 });
            result = builder.create_root_signature(rootSignatureDesc, m_screenCopyRootSignatureHandle);
            if (!result)
            {
                return Result::fail(
                    result.code,
                    Severity::Error,
                    "Failed to create root signature for screen copy pass.");
            }

            const std::string shaderFilePath = find_screen_copy_shader_path();
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

        void execute(FrameGraphContext& context) override
        {
            ICommandContext* commandContext = context.commandContext();

            // finalColor をまず別色でクリアし、コピー結果を視認できるようにする。
            {
                ResourceBarrierDesc barrierDesc{};
                barrierDesc.before = ResourceState::Common;
                barrierDesc.after = ResourceState::RenderTarget;
                commandContext->resource_barrier(m_finalColorHandle, barrierDesc);
            }
            commandContext->clear_render_target(m_finalColorRtvHandle, k_finalColorClearColor.data());
            {
                ResourceBarrierDesc barrierDesc{};
                barrierDesc.before = ResourceState::RenderTarget;
                barrierDesc.after = ResourceState::ShaderResource;
                commandContext->resource_barrier(m_finalColorHandle, barrierDesc);
            }

            // スワップチェイン側も別色でクリアし、コピーが失敗すると色差で分かるようにする。
            {
                ResourceBarrierDesc barrierDesc{};
                barrierDesc.before = ResourceState::Present;
                barrierDesc.after = ResourceState::RenderTarget;
                commandContext->resource_barrier(m_backBufferHandle, barrierDesc);
            }
            commandContext->clear_render_target(m_backBufferRtvHandle, k_swapChainClearColor.data());
            commandContext->set_render_targets(&m_backBufferRtvHandle, 1, {});
            commandContext->set_viewport_scissor(context.width(), context.height());
            commandContext->set_graphics_pipeline(m_screenCopyPipelineHandle);
            commandContext->set_primitive_topology(PrimitiveTopologyType::Triangle);
            commandContext->set_graphics_descriptor_table(0, m_finalColorSrvHandle);
            commandContext->draw_instanced(3, 1, 0, 0);

            // バックバッファをプレゼント状態に戻す。
            {
                ResourceBarrierDesc barrierDesc{};
                barrierDesc.before = ResourceState::RenderTarget;
                barrierDesc.after = ResourceState::Present;
                commandContext->resource_barrier(m_backBufferHandle, barrierDesc);
            }
        }
    private:
        static std::string find_screen_copy_shader_path()
        {
            namespace fs = std::filesystem;

            fs::path current = fs::current_path();
            for (size_t i = 0; i < 8; ++i)
            {
                fs::path candidate = current / "Engine" / "Shaders" / "D3D12" / "ScreenCopy.hlsl";
                if (fs::exists(candidate))
                {
                    return candidate.string();
                }

                if (!current.has_parent_path())
                {
                    break;
                }
                current = current.parent_path();
            }

            return {};
        }
    private:
        static constexpr std::array<float, 4> k_finalColorClearColor = { 0.0f, 0.5f, 0.0f, 1.0f };
        static constexpr std::array<float, 4> k_swapChainClearColor = { 0.5f, 0.0f, 0.0f, 1.0f };

        TextureHandle m_backBufferHandle; // スワップチェインのバックバッファのハンドル
        ViewHandle m_backBufferRtvHandle; // スワップチェインのバックバッファの RTV ビューのハンドル
        TextureHandle m_finalColorHandle; // コピー元となる finalColor のハンドル
        ViewHandle m_finalColorRtvHandle; // finalColor の RTV ビューのハンドル
        ViewHandle m_finalColorSrvHandle; // finalColor の SRV ビューのハンドル
        RootSignatureHandle m_screenCopyRootSignatureHandle; // ScreenCopy 用ルートシグネチャのハンドル
        ShaderBlobHandle m_screenCopyVsHandle; // ScreenCopy の VS シェーダーハンドル
        ShaderBlobHandle m_screenCopyPsHandle; // ScreenCopy の PS シェーダーハンドル
        PipelineStateHandle m_screenCopyPipelineHandle; // ScreenCopy のグラフィックスパイプラインハンドル
    };
}
