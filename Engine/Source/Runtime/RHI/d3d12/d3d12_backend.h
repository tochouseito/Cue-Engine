#pragma once

// === RHI include ===
#include <RHI.h>

namespace Cue::RHI::D3D12
{
    class D3D12Backend final : public IBackend
    {
    public:
        D3D12Backend();
        ~D3D12Backend() override;
        Result initialize(const backend_setup_info& info) override;
        Result shutdown() override;
        Result render(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph) override;
        Result present(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph) override;
    };
}
