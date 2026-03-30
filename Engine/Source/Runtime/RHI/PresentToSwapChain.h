#pragma once

// === RHI includes ===
#include "FrameGraph.h"

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
            // 1) スワップチェインのバックバッファをフレームグラフに宣言する。
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
            return Result::ok();
        }

        void execute(FrameGraphContext& context) override
        {
            ICommandContext* commandContext = context.commandContext();

            // バックバッファをレンダーターゲットとしてクリアする。
            {
                ResourceBarrierDesc barrierDesc{};
                barrierDesc.before = ResourceState::Present;
                barrierDesc.after = ResourceState::RenderTarget;
                commandContext->resource_barrier(m_backBufferHandle, barrierDesc);
            }

            // レンダーターゲットのクリア
            const float clearColor[4] = { 0.5f, 0.0f, 0.0f, 1.0f };
            commandContext->clear_render_target(m_backBufferRtvHandle, clearColor);

            // バックバッファをプレゼント状態に戻す。
            {
                ResourceBarrierDesc barrierDesc{};
                barrierDesc.before = ResourceState::RenderTarget;
                barrierDesc.after = ResourceState::Present;
                commandContext->resource_barrier(m_backBufferHandle, barrierDesc);
            }
        }
    private:
        TextureHandle m_backBufferHandle; // スワップチェインのバックバッファのハンドル
        ViewHandle m_backBufferRtvHandle; // スワップチェインのバックバッファの RTV ビューのハンドル
    };
}
