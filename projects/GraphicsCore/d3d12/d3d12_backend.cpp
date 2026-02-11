#include "d3d12_backend.h"
#include <Windows.h>

#include "RenderDevice.h"

namespace Cue::GraphicsCore
{
    std::unique_ptr<Backend> create_backend()
    {
        // 1) 現在の既定backendとしてD3D12実装を返す
        return std::make_unique<DX12::D3D12Backend>();
    }
}

namespace Cue::GraphicsCore::DX12
{
    struct D3D12Backend::Impl
    {
        // 実装の詳細をここに記述
        Platform::Win::WinPlatform* m_winPlatform = nullptr;
        RenderDevice m_renderDevice;
    };

    D3D12Backend::D3D12Backend()
        : m_impl(std::make_unique<Impl>())
    {
    }
    D3D12Backend::~D3D12Backend()
    {
    }
    Core::Result D3D12Backend::initialize()
    {
        if (!m_impl->m_winPlatform)
        {
            return Core::Result::fail(
                Core::Facility::D3D12,
                Core::Code::InvalidState,
                Core::Severity::Error,
                0,
                "WinPlatform is not set in D3D12Backend.");
        }

        // 1) デバイス初期化失敗をそのまま返し、失敗点を保持する
        Core::Result r = m_impl->m_renderDevice.initialize(true);
        if (!r)
        {
            return r;
        }

        // 2) すべての初期化が成功したことを返す
        return Core::Result::ok();
    }
    Core::Result D3D12Backend::shutdown()
    {
        // 1) 現状は明示解放対象がないため成功を返す
        return Core::Result::ok();
    }
    void D3D12Backend::set_win_platform(Platform::IPlatform* platform)
    {
        m_impl->m_winPlatform = dynamic_cast<Platform::Win::WinPlatform*>(platform);
    }
} // namespace Cue::Graphics::DX12
