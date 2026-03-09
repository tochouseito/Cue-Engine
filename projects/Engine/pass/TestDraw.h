#pragma once
#include <FrameGraph.h>
#include <Logger.h>
#include <StaticMeshBufferPool.h>
#include <filesystem>

namespace Cue::GraphicsCore::Pass
{
    namespace
    {
        [[nodiscard]] std::string resolve_test_draw_shader_path()
        {
            namespace fs = std::filesystem;

            // 1) ソースツリー基準の絶対経路を最優先し、実行ディレクトリ差で shader 解決が壊れるのを防ぐ。
            const fs::path passFilePath = fs::path(__FILE__).lexically_normal();
            const fs::path sourceTreePath = passFilePath.parent_path().parent_path() / "shader" / "TestDraw.hlsl";
            std::error_code errorCode{};
            if (fs::exists(sourceTreePath, errorCode))
            {
                return sourceTreePath.lexically_normal().string();
            }

            // 2) 実行ディレクトリを repo root にした開発実行にも対応する。
            const fs::path repoRelativePath = fs::path("projects/Engine/shader/TestDraw.hlsl");
            errorCode = {};
            if (fs::exists(repoRelativePath, errorCode))
            {
                return repoRelativePath.lexically_normal().string();
            }

            // 3) 生成物ディレクトリから起動した場合の相対経路も許可し、Editor/App の起動位置差を吸収する。
            const fs::path generatedOutputRelativePath = fs::path("../../../projects/Engine/shader/TestDraw.hlsl");
            errorCode = {};
            if (fs::exists(generatedOutputRelativePath, errorCode))
            {
                return generatedOutputRelativePath.lexically_normal().string();
            }

            // 4) 最後は repo 相対を返し、コンパイラ側のエラーメッセージに期待パスを残す。
            return repoRelativePath.string();
        }
    }

    class TestDrawPass final : public FrameGraphPass
    {
    public:
        explicit TestDrawPass(StaticMeshAllocationHandle meshHandle)
            : m_meshHandle(meshHandle)
        {
        }

        [[nodiscard]] const char* name() const override
        {
            return "TestDrawPass";
        }

        void setup(FrameGraphBuilder& builder) override
        {
            // 1) SwapChain back buffer を render target として宣言し、この pass が Present 直前に色を書き込むことを明示する。
            TextureDesc backBufferDesc{};
            backBufferDesc.name = "SwapChain.BackBuffer";
            backBufferDesc.instanceSource = ResourceInstanceSource::SwapchainImageIndex;
            m_backBuffer = builder.import_texture(backBufferDesc.name, backBufferDesc, ResourceState::Present);
            builder.render(m_backBuffer, ResourceState::Present);

            // 2) シェーダパスを先に確定し、VS/PS で同一ファイルを使う契約を固定する。
            m_shaderFilePath = resolve_test_draw_shader_path();

            // 3) Descriptor を使わない最小 root signature を宣言し、PSO 生成時の依存を pass 内で閉じる。
            RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "TestDraw.RootSignature";
            builder.use_root_signature(rootSignatureDesc);

            // 4) SoA の position/uv/normal stream を受ける VS/PS をコンパイル対象として宣言する。
            ShaderCompileDesc vertexShaderDesc{};
            vertexShaderDesc.name = "TestDraw.VS";
            vertexShaderDesc.filePath = m_shaderFilePath;
            vertexShaderDesc.entryPoint = "vs_main";
            vertexShaderDesc.targetProfile = "vs_6_0";
            builder.compile_shader(vertexShaderDesc);

            ShaderCompileDesc pixelShaderDesc{};
            pixelShaderDesc.name = "TestDraw.PS";
            pixelShaderDesc.filePath = m_shaderFilePath;
            pixelShaderDesc.entryPoint = "ps_main";
            pixelShaderDesc.targetProfile = "ps_6_0";
            builder.compile_shader(pixelShaderDesc);

            // 5) 依存 handle は FrameGraph 側で補完される前提で PSO を宣言し、pass は draw call だけを持つようにする。
            GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "TestDraw.PSO";
            pipelineDesc.rasterizerState.cullMode = CullMode::None;
            pipelineDesc.depthStencilState.depthEnable = false;
            pipelineDesc.depthStencilState.depthWriteMask = DepthWriteMask::Zero;
            pipelineDesc.primitiveTopologyType = PrimitiveTopologyType::Triangle;
            pipelineDesc.rtvFormats = { ColorFormat::R8G8B8A8_UNORM };
            pipelineDesc.inputElements =
            {
                InputElementDesc{ "POSITION", 0, InputElementFormat::R32G32B32A32_Float, 0, 0 },
                InputElementDesc{ "TEXCOORD", 0, InputElementFormat::R32G32_Float, 1, 0 },
                InputElementDesc{ "NORMAL", 0, InputElementFormat::R32G32B32_Float, 2, 0 },
            };
            builder.use_pipeline(pipelineDesc);
        }

        void execute(FrameGraphContext& ctx) const override
        {
            // 1) execute 時点で PSO/RootSignature が解決済みであることを確認し、設定漏れを即座に検出する。
            if (!ctx.pipeline_handle().valid() || !ctx.root_signature_handle().valid())
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] pipeline artifacts are not resolved.\n");
                return;
            }

            // 2) IA topology を command context 経由で設定し、pass 側から backend 依存コマンドを排除する。
            const Result setTopologyResult = ctx.command_context().set_primitive_topology_triangle_list();
            if (!setTopologyResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to set primitive topology.\n");
                return;
            }

            // 3) SoA の 3 stream を slot 0-2 に bind し、mesh pool 上の slice をそのまま描画に使う。
            StaticMeshAllocation meshAllocation{};
            const Result getAllocationResult = ctx.static_mesh_buffer_pool().get_allocation(m_meshHandle, meshAllocation);
            if (!getAllocationResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to resolve static mesh allocation.\n");
                return;
            }

            const VertexBufferBindDesc vertexBuffers[] =
            {
                VertexBufferBindDesc{ meshAllocation.positionBuffer.buffer, meshAllocation.positionBuffer.byteOffset, meshAllocation.positionBuffer.byteSize, meshAllocation.positionBuffer.stride },
                VertexBufferBindDesc{ meshAllocation.uvBuffer.buffer, meshAllocation.uvBuffer.byteOffset, meshAllocation.uvBuffer.byteSize, meshAllocation.uvBuffer.stride },
                VertexBufferBindDesc{ meshAllocation.normalBuffer.buffer, meshAllocation.normalBuffer.byteOffset, meshAllocation.normalBuffer.byteSize, meshAllocation.normalBuffer.stride },
            };
            const Result bindVertexBuffersResult = ctx.command_context().set_vertex_buffers(0, vertexBuffers, 3);
            if (!bindVertexBuffersResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to bind vertex buffers.\n");
                return;
            }

            const IndexBufferBindDesc indexBuffer{
                meshAllocation.indexBuffer.buffer,
                meshAllocation.indexBuffer.byteOffset,
                meshAllocation.indexBuffer.byteSize,
                meshAllocation.indexBuffer.format };
            const Result bindIndexBufferResult = ctx.command_context().set_index_buffer(indexBuffer);
            if (!bindIndexBufferResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to bind index buffer.\n");
                return;
            }

            const Result drawResult = ctx.command_context().draw_indexed_instanced(meshAllocation.indexBuffer.indexCount, 1, 0, 0, 0);
            if (!drawResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to issue draw_indexed_instanced.\n");
            }
        }

    private:
        GraphicsCore::TextureHandle m_backBuffer{};
        std::string m_shaderFilePath{};
        StaticMeshAllocationHandle m_meshHandle{};
    };
}
