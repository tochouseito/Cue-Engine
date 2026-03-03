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
            return "BackBufferClearPass";
        }

        void setup(FrameGraphBuilder& builder) override
        {
            
        }

        void execute(FrameGraphContext& ctx) const override
        {
        }

    private:
        uint32_t m_bufferingCount = 1;
        GraphicsCore::BufferHandle m_sceneBuffer{};
        std::vector<GraphicsCore::TextureHandle> m_backBuffers{};
    };
}
