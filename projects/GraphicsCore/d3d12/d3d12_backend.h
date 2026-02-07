#pragma once
#include <GraphicsCore.h>

namespace Cue::Graphics::DX12
{
    class D3D12Backend : public Backend
    {
    public:
        D3D12Backend();
        ~D3D12Backend() override;
        void initialize() override;
        void shutdown() override;
    };
} // namespace Cue::Graphics::DX12
