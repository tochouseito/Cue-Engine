#include "d3d12_backend.h"
#include <win/win_native.h>
#include "ResourceLeakChecker.h"
#include "DX12RenderDevice.h"
#include "DX12GpuCommand.h"
#include "DX12BufferManager.h"
#include "private/DX12TextureManager.h"
#include "private/DX12PipelineManager.h"
#include "private/DX12ViewManager.h"
#include "private/HLSLCompiler.h"
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
        HWND m_hWnd = nullptr;
        std::unique_ptr<ResourceLeakChecker> m_leakChecker = std::make_unique<ResourceLeakChecker>();
        std::unique_ptr<DX12RenderDevice> m_renderDevice = std::make_unique<DX12RenderDevice>();
        std::unique_ptr<DX12CommandPool> m_commandPool = nullptr;
        std::unique_ptr<DX12QueuePool> m_queuePool = nullptr;
        std::unique_ptr<HLSLCompiler> m_shaderCompiler = std::make_unique<HLSLCompiler>();
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr;
        std::unique_ptr<DX12BufferManager> m_bufferManager = nullptr;
        std::unique_ptr<DX12TextureManager> m_textureManager = nullptr;
        std::unique_ptr<DX12ViewManager> m_viewManager = nullptr;
        std::unique_ptr<DX12PipelineManager> m_pipelineManager = nullptr;
        std::unique_ptr<SwapChain> m_swapChain = nullptr;
        std::unique_ptr<StaticMeshBufferPool> m_staticMeshBufferPool = nullptr;
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
        m_setupInfo = info;

        // 1) デバイス初期化失敗をそのまま返し、失敗点を保持する
        Result r = m_impl->m_renderDevice->initialize(true);
        if (!r)
        {
            return r;
        }

        // 2) マネージャー類を初期化する
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
        m_impl->m_viewManager = std::make_unique<DX12ViewManager>(*m_impl->m_bufferManager, *m_impl->m_textureManager, *m_impl->m_descriptorAllocator);
        m_impl->m_pipelineManager = std::make_unique<DX12PipelineManager>(*m_impl->m_renderDevice, *m_impl->m_shaderCompiler, *m_impl->m_descriptorAllocator);

        // 3) コマンドプールとキュープールを初期化する
        m_impl->m_commandPool = std::make_unique<DX12CommandPool>(*m_impl->m_renderDevice);
        m_impl->m_commandPool->initialize();
        m_impl->m_queuePool = std::make_unique<DX12QueuePool>(*m_impl->m_renderDevice);
        m_impl->m_queuePool->initialize();
        m_impl->m_commandPool->bind_resources(
            *m_impl->m_bufferManager,
            *m_impl->m_textureManager,
            *m_impl->m_pipelineManager,
            *m_impl->m_viewManager,
            *m_impl->m_descriptorAllocator);

        // 4) 静的メッシュ用の巨大 VB/IB pool を backend 起動時に確保し、後段の asset upload 先を固定する。
        m_impl->m_staticMeshBufferPool = std::make_unique<StaticMeshBufferPool>();
        r = m_impl->m_staticMeshBufferPool->initialize(
            info.staticMeshBufferPoolDesc,
            *m_impl->m_bufferManager,
            *m_impl->m_commandPool,
            *m_impl->m_queuePool);
        if (!r)
        {
            return r;
        }

        // 5) スワップチェーンを初期化する
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
        m_impl->m_swapChain->import_back_buffers(*m_impl->m_textureManager);

        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        // 1) SwapChain back buffer を解放する前に graphics queue の最後の signal 完了を待ち、GPU 参照を確実に止める。
        if (m_impl->m_queuePool != nullptr)
        {
            QueueContextLease graphicsQueue;
            const Result acquireQueueResult = m_impl->m_queuePool->acquire_queue(CommandListType::Graphics, graphicsQueue);
            if (acquireQueueResult)
            {
                auto* dx12GraphicsQueue = dynamic_cast<DX12QueueContext*>(graphicsQueue.get());
                if (dx12GraphicsQueue != nullptr)
                {
                    dx12GraphicsQueue->wait_for_last_signal();
                }
            }
        }

        // 2) FrameGraph と SwapChain を止め、以後の render/present 経路を無効化する。
        m_impl->m_swapChain.reset();
        if (m_impl->m_staticMeshBufferPool != nullptr)
        {
            m_impl->m_staticMeshBufferPool->shutdown();
        }
        m_impl->m_staticMeshBufferPool.reset();

        // 3) Queue/Command を manager より先に破棄し、pooled context の destructor が allocator 生存中に走るようにする。
        m_impl->m_queuePool.reset();
        m_impl->m_commandPool.reset();

        // 4) CommandContext から参照される manager 群を後段で解放する。
        m_impl->m_pipelineManager.reset();
        m_impl->m_viewManager.reset();
        m_impl->m_textureManager.reset();
        m_impl->m_bufferManager.reset();
        m_impl->m_descriptorAllocator.reset();
        m_impl->m_shaderCompiler.reset();

        // 5) 最後に device と補助オブジェクトを解放する。
        m_impl->m_renderDevice.reset();
        m_impl->m_leakChecker.reset();
        return Result::ok();
    }
    Result D3D12Backend::render(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph)
    {
        // 1) render graph は offscreen 専用として実行し、SwapChain 直描きは present graph へ分離する。
        return frameGraph.execute(frameNo, index, 0, *m_impl->m_commandPool, *m_impl->m_queuePool);
    }
    Result D3D12Backend::present(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph)
    {
        // 1) current back buffer が確定した時点で present graph を実行し、SwapChain 直描き pass をここへ集約する。
        if (&frameGraph == nullptr)
        {
            return Result::fail(Facility::Graphics, Code::InvalidState, Severity::Error, 0, "Present frame graph is not initialized.");
        }

        const uint32_t backBufferIndex = m_impl->m_swapChain->current_back_buffer_index();
        const Result executeResult = frameGraph.execute(frameNo, index, backBufferIndex, *m_impl->m_commandPool, *m_impl->m_queuePool);
        if (!executeResult)
        {
            return executeResult;
        }

        const Result presentResult = m_impl->m_swapChain->present(false);
        if (!presentResult)
        {
            return presentResult;
        }

        // 3) shutdown 時に Present 後の back buffer 使用完了まで待てるよう、直後の graphics queue fence を更新する。
        QueueContextLease graphicsQueue;
        const Result acquireQueueResult = m_impl->m_queuePool->acquire_queue(CommandListType::Graphics, graphicsQueue);
        if (!acquireQueueResult)
        {
            return acquireQueueResult;
        }

        QueueSyncPoint presentSignalPoint{};
        return graphicsQueue->signal(presentSignalPoint);
    }
    Result D3D12Backend::create_frame_graph(std::unique_ptr<FrameGraph>& outFG)
    {
        outFG = std::make_unique<FrameGraph>(
            m_setupInfo.screenWidth,
            m_setupInfo.screenHeight,
            *m_impl->m_bufferManager,
            *m_impl->m_textureManager,
            *m_impl->m_viewManager,
            *m_impl->m_staticMeshBufferPool,
            *m_impl->m_pipelineManager,
            m_setupInfo.bufferCount);

        return Result::ok();
    }
    StaticMeshBufferPool* D3D12Backend::get_static_mesh_buffer_pool()
    {
        return m_impl->m_staticMeshBufferPool.get();
    }
    void D3D12Backend::set_win_platform(Platform::IPlatform* platform)
    {
        m_impl->m_winPlatform = dynamic_cast<Platform::Win::WinPlatform*>(platform);
    }
    ID3D12Device* D3D12Backend::get_device() const
    {
        return m_impl->m_renderDevice->get_d3d12_device();
    }
    ID3D12CommandQueue* D3D12Backend::get_graphics_command_queue() const
    {
        QueueContextLease graphicsQueue;
        const Result acquireQueueResult = m_impl->m_queuePool->acquire_queue(CommandListType::Graphics, graphicsQueue);
        if (!acquireQueueResult)
        {
            Assert::cue_assert(false, "Failed to acquire graphics queue in D3D12Backend.");
        }
        auto* dx12GraphicsQueue = dynamic_cast<DX12QueueContext*>(graphicsQueue.get());
        if (dx12GraphicsQueue == nullptr)
        {
            Assert::cue_assert(false, "Failed to cast to DX12QueueContext in D3D12Backend.");
            return nullptr;
        }
        auto* commandQueue = dx12GraphicsQueue->get_command_queue();
        return commandQueue;
    }
    DXGI_FORMAT D3D12Backend::get_rtv_format() const
    {
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    font_srv_for_imgui D3D12Backend::get_font_srv_for_imgui() const
    {
        font_srv_for_imgui result{};
        if (!m_impl->m_descriptorAllocator)
        {
            Assert::cue_assert(false, "DescriptorAllocator is not initialized in D3D12Backend.");
        }
        else
        {
            DescriptorAllocator::TableID fontTable = m_impl->m_descriptorAllocator->allocate(DescriptorAllocator::TableKind::Textures);
            result.srvDescHeap = m_impl->m_descriptorAllocator->get_descriptor_heap(HeapType::CBV_SRV_UAV);
            result.cpuDescHandle = m_impl->m_descriptorAllocator->get_cpu_handle_gpu_visible(fontTable);
            result.gpuDescHandle = m_impl->m_descriptorAllocator->get_gpu_handle(fontTable);
        }

        return result;
    }
} // namespace Cue::Graphics::DX12
