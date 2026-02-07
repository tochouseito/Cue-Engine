#pragma once
#include <GraphicsCore.h>
#include "BackendFactory.h"
#include <win_platform.h>
#include <memory>

namespace Cue::Graphics::DX12
{
    class D3D12Backend : public Backend
    {
    public:
        D3D12Backend();
        ~D3D12Backend() override;
        Core::Result initialize() override;
        Core::Result shutdown() override;
        void set_win_platform(Platform::Win::WinPlatform* platform);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Cue::Graphics::DX12
