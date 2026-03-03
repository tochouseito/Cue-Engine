#pragma once
#include "FrameGraph.h"

#include <array>
#include <functional>
#include <string>

namespace Cue::GraphicsCore::Pass
{
    class BackBufferClearPass final : public FrameGraphPass
    {
    public:
        explicit BackBufferClearPass(uint32_t bufferingCount) noexcept
            : m_bufferingCount((std::max)(bufferingCount, 1u))
        {}

        [[nodiscard]] const char* name() const override
        {
            return "BackBufferPass";
        }

        void setup(FrameGraphBuilder& builder) override
        {
            // setup() only declares resources and bindings.
            // This pass imports the back buffer and records the pipeline layout it expects.
            GraphicsCore::BufferDesc sceneBufferDesc{};
            sceneBufferDesc.name = "BackBuffer.Scene";
            sceneBufferDesc.bufferingCount = 0;
            m_sceneBuffer = builder.create_buffer("BackBuffer.Scene", sceneBufferDesc);
            builder.read(m_sceneBuffer);

            m_backBuffers.clear();
            m_backBuffers.reserve(m_bufferingCount);
            for (uint32_t bufferIndex = 0; bufferIndex < m_bufferingCount; ++bufferIndex)
            {
                const std::string backBufferName = "SwapChain.BackBuffer." + std::to_string(bufferIndex);
                GraphicsCore::TextureDesc backBufferDesc{};
                m_backBuffers.push_back(builder.import_texture(backBufferName, backBufferDesc, GraphicsCore::ResourceState::Present));
                builder.render(m_backBuffers.back(), GraphicsCore::ResourceState::Present);
            }

            GraphicsCore::RootSignatureDesc rootSignatureDesc{};
            rootSignatureDesc.name = "BackBuffer.RootSignature";
            builder.use_root_signature(rootSignatureDesc);

            builder.compile_shader(GraphicsCore::ShaderCompileDesc{
                .name = "BackBuffer.VS",
                .filePath = "backbuffer.hlsl",
                .entryPoint = "vs_main",
                .targetProfile = "vs_6_0",
                .enableDebugInfo = true
                });
            builder.compile_shader(GraphicsCore::ShaderCompileDesc{
                .name = "BackBuffer.PS",
                .filePath = "backbuffer.hlsl",
                .entryPoint = "ps_main",
                .targetProfile = "ps_6_0",
                .enableDebugInfo = true
                });

            GraphicsCore::GraphicsPipelineStateDesc pipelineDesc{};
            pipelineDesc.name = "BackBuffer.Pipeline";
            builder.use_pipeline(pipelineDesc);

            builder.bind_cbv(0, m_sceneBuffer, GraphicsCore::ShaderVisibility::Vertex);
            for (uint32_t bufferIndex = 0; bufferIndex < static_cast<uint32_t>(m_backBuffers.size()); ++bufferIndex)
            {
                builder.bind_srv_at(0, bufferIndex, m_backBuffers[bufferIndex], GraphicsCore::ShaderVisibility::Pixel);
            }
        }

        void execute(FrameGraphContext& ctx) const override
        {
            // execute() resolves the logical back buffer to the backend handle for this frame.
            if (m_backBuffers.empty())
            {
                return;
            }

            const uint32_t bufferIndex = (std::min)(ctx.buffer_index(), static_cast<uint32_t>(m_backBuffers.size() - 1));
            GraphicsCore::TextureHandle physicalBackBuffer{};
            const Result result = ctx.resolve_texture(m_backBuffers[bufferIndex], physicalBackBuffer);
            if (!result)
            {
                return;
            }
            (void)ctx;
            (void)physicalBackBuffer;
        }

    private:
        uint32_t m_bufferingCount = 1;
        GraphicsCore::BufferHandle m_sceneBuffer{};
        std::vector<GraphicsCore::TextureHandle> m_backBuffers{};
    };
}
