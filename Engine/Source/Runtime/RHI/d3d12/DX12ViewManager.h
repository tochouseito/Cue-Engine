#pragma once

// === RHI Includes ===
#include <ViewManager.h>

namespace Cue::RHI::DX12
{
    class DX12ViewManager final : public IViewManager
    {
    public:
        DX12ViewManager() = default;
        ~DX12ViewManager() override = default;
        Result create_view(const ViewDesc& desc, ViewHandle& out) override;
        Result destroy_view(ViewHandle handle) override;
    };
}
