#pragma once
#include <FrameGraph.h>
#include <Logger.h>
#include <filesystem>

namespace Cue::GraphicsCore::Pass
{
    namespace
    {
        [[nodiscard]] std::string resolve_screen_copy_shader_path()
        {
            namespace fs = std::filesystem;

            // 1) ソースツリー基準の絶対経路を最優先し、実行ディレクトリ差で shader 解決が壊れるのを防ぐ。
            const fs::path passFilePath = fs::path(__FILE__).lexically_normal();
            const fs::path sourceTreePath = passFilePath.parent_path().parent_path() / "shader" / "ScreenCopy.hlsl";
            std::error_code errorCode{};
            if (fs::exists(sourceTreePath, errorCode))
            {
                return sourceTreePath.lexically_normal().string();
            }

            // 2) 実行ディレクトリを repo root にした開発実行にも対応する。
            const fs::path repoRelativePath = fs::path("projects/Engine/shader/ScreenCopy.hlsl");
            errorCode = {};
            if (fs::exists(repoRelativePath, errorCode))
            {
                return repoRelativePath.lexically_normal().string();
            }

            // 3) 生成物ディレクトリから起動した場合の相対経路も許可し、Editor/App の起動位置差を吸収する。
            const fs::path generatedOutputRelativePath = fs::path("../../../projects/Engine/shader/ScreenCopy.hlsl");
            errorCode = {};
            if (fs::exists(generatedOutputRelativePath, errorCode))
            {
                return generatedOutputRelativePath.lexically_normal().string();
            }

            // 4) 最後は repo 相対を返し、コンパイラ側のエラーメッセージに期待パスを残す。
            return repoRelativePath.string();
        }
    }

    class PresentToSwapChainPass final : public FrameGraphPass
    {
    public:
        [[nodiscard]] const char* name() const override
        {
            return "PresentToSwapChainPass";
        }

        void setup(FrameGraphBuilder& builder) override
        {
            // 1) render graph が生成した FinalColor を frame resource 単位の外部 texture として受け取る。
            TextureDesc finalColorDesc{};
            finalColorDesc.name = "FinalColor";
            finalColorDesc.instanceSource = ResourceInstanceSource::FrameResourceIndex;
            m_finalColor = builder.import_texture(finalColorDesc.name, finalColorDesc, ResourceState::ShaderResource);
            builder.read(m_finalColor);

            // 2) swapchain back buffer を render target として宣言し、present 直前に必ず Present 状態へ戻す。
            TextureDesc backBufferDesc{};
            backBufferDesc.name = "SwapChain.BackBuffer";
            backBufferDesc.instanceSource = ResourceInstanceSource::SwapchainImageIndex;
            m_backBuffer = builder.import_texture(backBufferDesc.name, backBufferDesc, ResourceState::Present);
            builder.render(m_backBuffer, ResourceState::Present);

            // 3) pixel shader から FinalColor を読むだけの root signature を宣言し、descriptor 解決を FrameGraph へ委譲する。
            RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "PresentToSwapChain.RootSignature";
            rootSignatureDesc.parameters =
            {
                RootParameterDesc{ RootParameterType::SRV, ShaderVisibility::Pixel, 0 },
            };
            builder.use_root_signature(rootSignatureDesc);
            builder.bind_srv(0, m_finalColor, ShaderVisibility::Pixel);

            // 4) フルスクリーン三角形でコピーする shader を VS/PS として宣言し、present pass は draw call だけを持つ。
            m_shaderFilePath = resolve_screen_copy_shader_path();

            ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "PresentToSwapChain.VS";
            vertexShaderDesc.filePath = m_shaderFilePath;
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            builder.compile_shader(vertexShaderDesc);

            ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "PresentToSwapChain.PS";
            pixelShaderDesc.filePath = m_shaderFilePath;
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            builder.compile_shader(pixelShaderDesc);

            // 5) input layout なしのフルスクリーン描画 PSO を宣言し、back face cull で三角形が消えないようにする。
            GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "PresentToSwapChain.PSO";
            pipelineDesc.rasterizerState.cullMode = CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask = DepthWriteMask::Zero;
            pipelineDesc.primitiveTopologyType = PrimitiveTopologyType::Triangle;
            pipelineDesc.rtvFormats = { ColorFormat::R8G8B8A8_UNORM };
            builder.use_pipeline(pipelineDesc);
        }

        void execute(FrameGraphContext& ctx) const override
        {
            // 1) PSO/RootSignature が解決済みであることを確認し、present graph の build 漏れを即座に検出する。
            if (!ctx.pipeline_handle().valid() || !ctx.root_signature_handle().valid())
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[PresentToSwapChainPass] pipeline artifacts are not resolved.\n");
                return;
            }

            // 2) フルスクリーン三角形の topology を設定し、vertex buffer なしで ScreenCopy.hlsl を実行する。
            const Result setTopologyResult = ctx.command_context().set_primitive_topology_triangle_list();
            if (!setTopologyResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[PresentToSwapChainPass] failed to set primitive topology.\n");
                return;
            }

            // 3) 3 頂点だけ描き、FinalColor 全体を swapchain back buffer へコピーする。
            const Result drawResult = ctx.command_context().draw_instanced(3, 1, 0, 0);
            if (!drawResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[PresentToSwapChainPass] failed to draw fullscreen triangle.\n");
            }
        }

    private:
        GraphicsCore::TextureHandle m_finalColor{};
        GraphicsCore::TextureHandle m_backBuffer{};
        std::string m_shaderFilePath{};
    };
}
