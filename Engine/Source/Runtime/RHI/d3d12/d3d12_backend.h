#pragma once

// === RHI include ===
#include <RHI.h>

// === PAL include ===
#include <win/win_platform.h>

// === DirectX 12 include ===
#include "stdafx.h"
#include "DX12RenderDevice.h"

namespace Cue::RHI::DX12
{
    class D3D12Backend final : public IBackend
    {
    public:
        D3D12Backend() = default;
        ~D3D12Backend() override = default;
        Result initialize(const backend_setup_info& info) override;
        Result shutdown() override;
        Result render(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph) override;
        Result present(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph) override;
        void set_win_platform(PAL::Win::WinPlatform* platform) noexcept { m_platform = platform; }
    private:
        PAL::Win::WinPlatform* m_platform = nullptr; // プラットフォーム
        std::unique_ptr<DX12RenderDevice> m_renderDevice = nullptr; // レンダーデバイス
    };
}
