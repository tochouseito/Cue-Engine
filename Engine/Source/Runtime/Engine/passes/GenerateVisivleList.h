#pragma once

// === RHI includes ===
#include <FrameGraph.h>

namespace Cue
{
    class GenerateVisibleListPass final : public RHI::FrameGraphPass
    {
    public:
        const char* name() const noexcept override
        {
            return "GenerateVisibleList";
        }

        RHI::CommandListType type() const noexcept override
        {
            return RHI::CommandListType::Graphics;
        }

        Result setup(RHI::FrameGraphBuilder& builder) override
        {
            // 必要なバッファをフレームグラフに宣言する。
            Result result = builder.get_buffer("ObjectInfoBuffer", m_objectInfoBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("RenderObjectBuffer", m_renderObjectBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_buffer("VisibleObjectCountBuffer", m_visibleObjectCountBufferHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("ObjectInfoBufferSRV", m_objectInfoBufferSrvHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("RenderObjectBufferUAV", m_renderObjectBufferUavHandle);
            if (!result)
            {
                return result;
            }
            result = builder.get_view("VisibleObjectCountBufferUAV", m_visibleObjectCountBufferUavHandle);
            if (!result)
            {
                return result;
            }
            return Result::ok();
        }

        void execute(RHI::FrameGraphContext& context) override
        {}
    private:
        RHI::BufferHandle m_objectInfoBufferHandle{};
        RHI::BufferHandle m_renderObjectBufferHandle{};
        RHI::BufferHandle m_visibleObjectCountBufferHandle{};
        RHI::ViewHandle m_objectInfoBufferSrvHandle{};
        RHI::ViewHandle m_renderObjectBufferUavHandle{};
        RHI::ViewHandle m_visibleObjectCountBufferUavHandle{};
    };
}
