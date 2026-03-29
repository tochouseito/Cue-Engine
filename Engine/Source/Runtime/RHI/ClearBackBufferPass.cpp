#include "ClearBackBufferPass.h"

namespace Cue::RHI
{
    Result ClearBackBufferPass::setup(FrameGraphBuilder& builder)
    {
        // 1) present 用 graph が持つ imported backbuffer を RT view 化して、この pass の唯一の出力先にします。
        const FrameGraphResourceRef backBuffer = builder.backbuffer_texture();
        if (!backBuffer.valid())
        {
            return Result::fail(
                Code::NotFound,
                Severity::Error,
                "ClearBackBufferPass requires an imported swap chain backbuffer.");
        }

        ViewDesc viewDesc{};
        viewDesc.name = m_name;
        viewDesc.type = ViewType::RenderTarget;
        viewDesc.bufferKind = BufferKind::RenderTarget;
        viewDesc.colorFormat = m_colorFormat;
        viewDesc.mipSlice = 0;
        viewDesc.mipLevels = 1;
        viewDesc.firstArraySlice = 0;
        viewDesc.arraySize = 1;

        m_renderTargetView = builder.create_texture_view(backBuffer, viewDesc);
        if (!m_renderTargetView.valid())
        {
            return Result::fail(
                Code::CreateFailed,
                Severity::Error,
                "Failed to create render target view for ClearBackBufferPass.");
        }

        // 2) clear 自体が副作用を持つ最終出力なので、render target 書き込みとして登録します。
        builder.set_render_target(m_renderTargetView, LoadOp::Clear, StoreOp::Store, m_clearColor);
        return Result::ok();
    }

    Result ClearBackBufferPass::execute(FrameGraphPassContext& context)
    {
        // 1) 実行時は context に対して抽象 clear API を呼ぶだけにして、descriptor/state 解決は backend へ寄せます。
        if (context.type() != CommandListType::Graphics)
        {
            return Result::fail(
                Code::InvalidArgument,
                Severity::Error,
                "ClearBackBufferPass must execute on a graphics command context.");
        }

        return context.clear_render_target(m_renderTargetView, m_clearColor);
    }
}
