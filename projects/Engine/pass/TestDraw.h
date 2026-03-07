#pragma once
#include <FrameGraph.h>

namespace Cue::GraphicsCore::Pass
{
    class TestDrawPass final : public FrameGraphPass
    {
    public:
        [[nodiscard]] const char* name() const override
        {
            return "TestDrawPass";
        }
        void setup(FrameGraphBuilder& builder) override
        {
        }
        void execute(FrameGraphContext& ctx) const override
        {}
    };
}
