#pragma once
#include <FrameGraph.h>
#include <Logger.h>
#include <StaticMeshBufferPool.h>
#include <filesystem>

namespace Cue::GraphicsCore::Pass
{
    namespace
    {
        [[nodiscard]] constexpr uint32_t align_constant_buffer_size(uint32_t byteSize) noexcept
        {
            return (byteSize + 255u) & ~255u;
        }

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

            // 3) Camera/Transform の CBV を VS から参照する root signature を宣言し、pass 単位で必要な binding を閉じる。
            RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "TestDraw.RootSignature";
            rootSignatureDesc.parameters =
            {
                RootParameterDesc{ RootParameterType::CBV, ShaderVisibility::Vertex, 0 },
                RootParameterDesc{ RootParameterType::CBV, ShaderVisibility::Vertex, 1 },
            };
            builder.use_root_signature(rootSignatureDesc);

            // 4) Camera/Transform の upload heap constant buffer を frame resource ごとに確保し、execute では CPU 書き込みだけにする。
            BufferDesc cameraBufferDesc{};
            cameraBufferDesc.name = "TestDraw.CameraConstants";
            cameraBufferDesc.type = BufferType::Constant;
            cameraBufferDesc.heapType = ResourceHeapType::Upload;
            cameraBufferDesc.size = align_constant_buffer_size(static_cast<uint32_t>(sizeof(CameraConstants)));
            m_cameraConstantBuffer = builder.create_buffer(cameraBufferDesc.name, cameraBufferDesc);
            builder.bind_cbv(0, m_cameraConstantBuffer, ShaderVisibility::Vertex);

            BufferDesc transformBufferDesc{};
            transformBufferDesc.name = "TestDraw.TransformConstants";
            transformBufferDesc.type = BufferType::Constant;
            transformBufferDesc.heapType = ResourceHeapType::Upload;
            transformBufferDesc.size = align_constant_buffer_size(static_cast<uint32_t>(sizeof(TransformConstants)));
            m_transformConstantBuffer = builder.create_buffer(transformBufferDesc.name, transformBufferDesc);
            builder.bind_cbv(1, m_transformConstantBuffer, ShaderVisibility::Vertex);

            // 5) SoA の position/uv/normal stream を受ける VS/PS をコンパイル対象として宣言する。
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

            // 6) 依存 handle は FrameGraph 側で補完される前提で PSO を宣言し、pass は draw call だけを持つようにする。
            GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "TestDraw.PSO";
            pipelineDesc.rasterizerState.cullMode = CullMode::Back;
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

            // 2) 描画前に現在の swapchain image を clear し、前フレームの色が残らないことを保証する。
            TextureHandle resolvedBackBuffer{};
            const Result resolveBackBufferResult = ctx.resolve_texture(m_backBuffer, resolvedBackBuffer);
            if (!resolveBackBufferResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to resolve back buffer.\n");
                return;
            }

            constexpr float clearColor[4] = { 0.07f, 0.11f, 0.18f, 1.0f };
            const Result clearResult = ctx.command_context().clear_render_target(resolvedBackBuffer, clearColor);
            if (!clearResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to clear back buffer.\n");
                return;
            }

            // 3) IA topology を command context 経由で設定し、pass 側から backend 依存コマンドを排除する。
            const Result setTopologyResult = ctx.command_context().set_primitive_topology_triangle_list();
            if (!setTopologyResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to set primitive topology.\n");
                return;
            }

            // 4) 固定 camera と回転 transform を組み立てて upload heap CB へ書き込み、draw 直前の CPU 更新だけで済ませる。
            const float aspectRatio = ctx.screen_height() == 0
                ? 1.0f
                : static_cast<float>(ctx.screen_width()) / static_cast<float>(ctx.screen_height());
            const Math::float4x4 projection = Math::perspective_fov_matrix(Math::pi / 3.0f, aspectRatio, 0.1f, 100.0f);

            const Math::float4x4 cameraWorld = Math::make_affine_matrix(
                Math::float3::one(),
                Math::float3::zero(),
                Math::float3(0.0f, 0.0f, -5.0f));
            const Math::float4x4 view = Math::float4x4::inverse(cameraWorld);

            CameraConstants cameraConstants{};
            cameraConstants.viewProjection = view * projection;
            const Result writeCameraResult = ctx.write_buffer(
                m_cameraConstantBuffer,
                0,
                &cameraConstants,
                static_cast<uint32_t>(sizeof(cameraConstants)));
            if (!writeCameraResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to update camera constants.\n");
                return;
            }

            const float rotationRadians = static_cast<float>(ctx.frame_no()) * 0.01f;
            TransformConstants transformConstants{};
            transformConstants.world = Math::make_affine_matrix(
                Math::float3::one(),
                Math::float3(rotationRadians, 0.0f, 0.0f),
                Math::float3::zero());
            const Result writeTransformResult = ctx.write_buffer(
                m_transformConstantBuffer,
                0,
                &transformConstants,
                static_cast<uint32_t>(sizeof(transformConstants)));
            if (!writeTransformResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to update transform constants.\n");
                return;
            }

            // 5) SoA の 3 stream を slot 0-2 に bind し、mesh pool 上の slice をそのまま描画に使う。
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

            // 6) 定数更新済みの mesh allocation を indexed draw し、pass の責務を 1 draw call に保つ。
            const Result drawResult = ctx.command_context().draw_indexed_instanced(meshAllocation.indexBuffer.indexCount, 1, 0, 0, 0);
            if (!drawResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[TestDrawPass] failed to issue draw_indexed_instanced.\n");
            }
        }

    private:
        struct CameraConstants final
        {
            Math::float4x4 viewProjection = Math::float4x4::identity();
        };

        struct TransformConstants final
        {
            Math::float4x4 world = Math::float4x4::identity();
        };

        GraphicsCore::TextureHandle m_backBuffer{};
        GraphicsCore::BufferHandle m_cameraConstantBuffer{};
        GraphicsCore::BufferHandle m_transformConstantBuffer{};
        std::string m_shaderFilePath{};
        StaticMeshAllocationHandle m_meshHandle{};
    };
}
