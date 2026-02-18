#pragma once
#include <GraphicsCore.h>
#include "BackendFactory.h"
#include <win_platform.h>
#include <memory>

namespace Cue::GraphicsCore::DX12
{
    class D3D12Backend : public Backend
    {
    public:
        D3D12Backend();
        ~D3D12Backend() override;
        Result initialize() override;
        Result shutdown() override;
        void set_win_platform(Platform::IPlatform* platform);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Cue::GraphicsCore::DX12
