#pragma once
#include "FrameGraph.h"

namespace Cue::GraphicsCore::Pass
{
    class BackBufferClearPass final : public FrameGraphPass
    {
    public:
        [[nodiscard]] const char* name() const override
        {
            return "BackBufferClearPass";
        }

        void setup(FrameGraphBuilder& builder) override
        {
            // 1) SwapChain back buffer 群は 1 つの logical texture として宣言し、実行時の swapchain image index で解決する。
            TextureDesc desc{};
            desc.name = "SwapChain.BackBuffer";
            desc.instanceSource = ResourceInstanceSource::SwapchainImageIndex;
            m_backBuffer = builder.import_texture(desc.name, desc, ResourceState::Present);
            builder.render(m_backBuffer, ResourceState::Present);
        }

        void execute(FrameGraphContext& ctx) const override
        {
            // 1) 現在の swapchain image に解決された物理 handle を使って clear する。
            TextureHandle resolvedBackBuffer{};
            const Result resolveResult = ctx.resolve_texture(m_backBuffer, resolvedBackBuffer);
            if (!resolveResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[BackBufferClearPass] failed to resolve back buffer for swapchain image {}\n", ctx.swapchain_image_index());
                return;
            }

            constexpr float clearColor[4] = { 0.07f, 0.11f, 0.18f, 1.0f };
            const Result clearResult = ctx.command_context().clear_render_target(resolvedBackBuffer, clearColor);
            if (!clearResult)
            {
                Core::Logger::log(Core::LogSink::debugConsole, "[BackBufferClearPass] failed to clear back buffer for swapchain image {}\n", ctx.swapchain_image_index());
            }
        }

    private:
        GraphicsCore::TextureHandle m_backBuffer{};
    };
}
