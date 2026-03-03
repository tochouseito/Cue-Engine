#include "d3d12_backend.h"
#include <win/win_native.h>
#include "ResourceLeakChecker.h"
#include "DX12RenderDevice.h"
#include "DX12GpuCommand.h"
#include "DX12BufferManager.h"
#include "private/DX12TextureManager.h"
#include "private/DX12PipelineManager.h"
#include "private/HLSLCompiler.h"
#include "SwapChain.h"

#include <passes/BackBufferClear.h>

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
        HWND m_hWnd = nullptr;
        std::unique_ptr<ResourceLeakChecker> m_leakChecker = std::make_unique<ResourceLeakChecker>();
        std::unique_ptr<DX12RenderDevice> m_renderDevice = std::make_unique<DX12RenderDevice>();
        std::unique_ptr<DX12CommandPool> m_commandPool = nullptr;
        std::unique_ptr<DX12QueuePool> m_queuePool = nullptr;
        std::unique_ptr<HLSLCompiler> m_shaderCompiler = std::make_unique<HLSLCompiler>();
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr;
        std::unique_ptr<DX12BufferManager> m_bufferManager = nullptr;
        std::unique_ptr<DX12TextureManager> m_textureManager = nullptr;
        std::unique_ptr<DX12PipelineManager> m_pipelineManager = nullptr;
        std::unique_ptr<SwapChain> m_swapChain = nullptr;
    };

    D3D12Backend::D3D12Backend()
        : m_impl(std::make_unique<Impl>())
    {
    }
    D3D12Backend::~D3D12Backend()
    {
    }
    Result D3D12Backend::initialize(const backend_setup_info& info)
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

        m_impl->m_hWnd = reinterpret_cast<HWND>(m_impl->m_winPlatform->get_native_window_handle());

        // 1) デバイス初期化失敗をそのまま返し、失敗点を保持する
        Result r = m_impl->m_renderDevice->initialize(true);
        if (!r)
        {
            return r;
        }

        // 2) コマンドプールとキュープールを初期化する
        m_impl->m_commandPool = std::make_unique<DX12CommandPool>(*m_impl->m_renderDevice);
        m_impl->m_commandPool->initialize();
        m_impl->m_queuePool = std::make_unique<DX12QueuePool>(*m_impl->m_renderDevice);
        m_impl->m_queuePool->initialize();

        // 3) マネージャー類を初期化する
        m_impl->m_descriptorAllocator = std::make_unique<DescriptorAllocator>(*m_impl->m_renderDevice);
        r = m_impl->m_descriptorAllocator->initialize(
            /*texCap=*/256,
            /*bufCap=*/256,
            /*rtCap=*/32,
            /*dsCap=*/2);
        if (!r)
        {
            return r;
        }
        m_impl->m_bufferManager = std::make_unique<DX12BufferManager>(*m_impl->m_renderDevice);
        m_impl->m_textureManager = std::make_unique<DX12TextureManager>(*m_impl->m_renderDevice);
        m_impl->m_pipelineManager = std::make_unique<DX12PipelineManager>(*m_impl->m_renderDevice, *m_impl->m_shaderCompiler, *m_impl->m_descriptorAllocator);

        // 4) スワップチェーンを初期化する
        m_impl->m_swapChain = std::make_unique<SwapChain>(*m_impl->m_renderDevice, *m_impl->m_descriptorAllocator);
        QueueContextLease graphicsQueue;
        r = m_impl->m_queuePool->acquire_queue(CommandListType::Graphics, graphicsQueue);
        DX12QueueContext& graphicsQueueRef = static_cast<DX12QueueContext&>(*graphicsQueue);
        r = m_impl->m_swapChain->create(
            m_impl->m_hWnd,
            m_impl->m_winPlatform->window_width(),
            m_impl->m_winPlatform->window_height(),
            info.bufferCount,
            graphicsQueueRef);

        //
        std::vector<TextureHandle> m_backBufferHandles(info.bufferCount);
        m_impl->m_swapChain->import_back_buffers(*m_impl->m_textureManager, m_backBufferHandles);

        m_frameGraph = std::make_unique<FrameGraph>(
            *m_impl->m_bufferManager,
            *m_impl->m_textureManager,
            *m_impl->m_pipelineManager,
            info.bufferCount);
        Pass::BackBufferClearPass* backBufferClearPass = m_frameGraph->add_pass<Pass::BackBufferClearPass>(info.bufferCount);
        (void)backBufferClearPass;

        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        return Result::ok();
    }
    Result D3D12Backend::build_frame_graph()
    {
        return m_frameGraph->build();
    }
    Result D3D12Backend::render(uint64_t frameNo, uint32_t index)
    {
       return m_frameGraph->execute(frameNo, index, *m_impl->m_commandPool, *m_impl->m_queuePool);
    }
    Result D3D12Backend::present(uint64_t frameNo, uint32_t index)
    {
        (void)frameNo; // 現状は未使用
        (void)index;   // 現状は未使用
        return m_impl->m_swapChain->present(1, 0);
    }
    void D3D12Backend::set_win_platform(Platform::IPlatform* platform)
    {
        m_impl->m_winPlatform = dynamic_cast<Platform::Win::WinPlatform*>(platform);
    }
} // namespace Cue::Graphics::DX12
