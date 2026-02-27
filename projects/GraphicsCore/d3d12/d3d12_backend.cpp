#include "d3d12_backend.h"
#include <win/win_native.h>
#include "RenderDevice.h"
#include "DX12GpuCommand.h"
#include "DX12BufferManager.h"
#include "SwapChain.h"

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
        std::unique_ptr<CommandPool> m_commandPool = nullptr;
        std::unique_ptr<QueuePool> m_queuePool = nullptr;
        std::unique_ptr<DX12BufferManager> m_bufferManager = nullptr;
        std::unique_ptr<SwapChain> m_swapChain = nullptr;
    };

    D3D12Backend::D3D12Backend()
        : m_impl(std::make_unique<Impl>())
    {
    }
    D3D12Backend::~D3D12Backend()
    {
    }
    Result D3D12Backend::initialize()
    {
        if (!m_impl->m_winPlatform)
        {
            return Result::fail(
                Facility::D3D12,
                Code::InvalidState,
                Severity::Error,
                0,
                "WinPlatform is not set in D3D12Backend.");
        }

        // 1) デバイス初期化失敗をそのまま返し、失敗点を保持する
        Result r = m_impl->m_renderDevice.initialize(true);
        if (!r)
        {
            return r;
        }

        // 2) コマンドプールとキュープールを作成する
        m_impl->m_commandPool = std::make_unique<CommandPool>(m_impl->m_renderDevice.get_d3d12_device());
        m_impl->m_queuePool = std::make_unique<QueuePool>(m_impl->m_renderDevice.get_d3d12_device());

        // 3) バッファマネージャを作成する
        m_impl->m_bufferManager = std::make_unique<DX12BufferManager>(m_impl->m_renderDevice);

        // 4) スワップチェインを作成する
        m_impl->m_swapChain = std::make_unique<SwapChain>(
            m_impl->m_renderDevice,
            *m_impl->m_queuePool,
            m_impl->m_bufferManager->get_descriptor_allocator());
        m_impl->m_swapChain->initialize(
            m_impl->m_winPlatform->get_hwnd_
        )

        // 2) すべての初期化が成功したことを返す
        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        // 1) 現状は明示解放対象がないため成功を返す
        return Result::ok();
    }
    void D3D12Backend::set_win_platform(Platform::IPlatform* platform)
    {
        m_impl->m_winPlatform = dynamic_cast<Platform::Win::WinPlatform*>(platform);
    }
} // namespace Cue::Graphics::DX12
