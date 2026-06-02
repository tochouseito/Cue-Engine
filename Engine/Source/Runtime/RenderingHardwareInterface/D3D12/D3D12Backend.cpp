#include "D3D12Backend.h"

namespace Cue::RHI::DX12
{
    D3D12Backend::D3D12Backend()
    {
    }

    Result D3D12Backend::initialize(const RenderBackendSetupInfo& a_info)
    {
        // 引数の保存
        // ウィンドウサイズやバッファ数は SwapChain/FrameController と共有される基本設定なので、
        // バックエンド側でも問い合わせ可能な状態にしておく。
        m_width = a_info.width;
        m_height = a_info.height;
        m_bufferCount = a_info.bufferCount;

        // レンダーデバイスの初期化
        // DXGI factory と ID3D12Device は後続の manager がすべて依存するため最初に作る。
        m_renderDevice = std::make_unique<DX12RenderDevice>();
        m_renderDevice->initialize(a_info.enableDebugLayer);

        // GPU Profiler の初期化
        m_gpuProfiler = std::make_unique<DX12GpuProfiler>(*m_renderDevice);

        // デスクリプタアロケータの初期化
        // View/Texture/Buffer manager が使う CPU/GPU descriptor heap をまとめて確保する。
        m_descriptorAllocator = std::make_unique<DescriptorAllocator>(*m_renderDevice->get_d3d12_device());
        m_descriptorAllocator->initialize(
            a_info.textureCapacity, a_info.bufferCapacity,
            a_info.renderTargetCapacity, a_info.depthStencilCapacity);


        // バッファマネージャの初期化
        m_bufferManager = std::make_unique<DX12BufferManager>(*m_renderDevice);
        m_textureManager = std::make_unique<DX12TextureManager>(*m_renderDevice, *m_descriptorAllocator);
        m_viewManager = std::make_unique<DX12ViewManager>(*m_bufferManager, *m_textureManager, *m_descriptorAllocator);
        m_pipelineManager = std::make_unique<DX12PipelineManager>(*m_renderDevice, *m_hlslCompiler);

        // コマンドプールの初期化
        m_commandPool = std::make_unique<DX12CommandPool>(*m_renderDevice, *m_descriptorAllocator, *m_bufferManager, *m_textureManager, *m_viewManager, *m_pipelineManager);

        // コマンドキュープールの初期化
        m_queuePool = std::make_unique<DX12QueuePool>(*m_renderDevice);

        // スワップチェインの初期化
        m_swapChain = std::make_unique<SwapChain>(*m_renderDevice, *m_textureManager, *m_viewManager);
        queueContextPtr presentQueueContext = m_queuePool->get_present_queue_context();
        if (!presentQueueContext)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Fatal,
                "Failed to get present graphics queue context for swap chain initialization.");
        }
        // スワップチェインの作成にはウィンドウハンドルと present 用グラフィックスキューが必要。
        m_swapChain->create(
            m_platform->get_window_handle(),
            a_info.width,
            a_info.height,
            a_info.bufferCount,
            *static_cast<DX12GpuCommandQueue*>(presentQueueContext));

        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        return Result::ok();
    }
    Result D3D12Backend::wait_for_idle()
    {
        if (!m_queuePool)
        {
            return Result::ok();
        }

        Result result = m_queuePool->wait_for_graphics_queue();
        if (!result)
        {
            return result;
        }

        queueContextPtr presentQueueContext = m_queuePool->get_present_queue_context();
        if (presentQueueContext != nullptr)
        {
            result = presentQueueContext->signal();
            if (!result)
            {
                return result;
            }

            result = presentQueueContext->wait();
            if (!result)
            {
                return result;
            }
        }

        return Result::ok();
    }

    Result D3D12Backend::render(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph)
    {
        a_frameNo;
        return a_frameGraph.execute(a_index);
    }

    Result D3D12Backend::present(uint64_t a_frameNo, uint32_t a_index, bool vsync, FrameGraph& a_frameGraph)
    {
        a_frameNo;
        a_index;
        Result result = a_frameGraph.execute(m_swapChain->get_current_back_buffer_index());
        if (!result)
        {
            return result;
        }
        return m_swapChain->present(vsync);
    }
    Result D3D12Backend::create_frame_graph(const FrameGraphDesc& a_desc, std::unique_ptr<FrameGraph>& a_outFrameGraph)
    {
        FrameGraphDesc desc = a_desc;
        desc.bufferManager = m_bufferManager.get();
        desc.textureManager = m_textureManager.get();
        desc.pipelineManager = m_pipelineManager.get();
        desc.viewManager = m_viewManager.get();
        desc.commandPool = m_commandPool.get();
        desc.queuePool = m_queuePool.get();
        desc.width = m_swapChain->width();
        desc.height = m_swapChain->height();

        a_outFrameGraph = std::make_unique<FrameGraph>(desc, m_bufferCount);
        return Result::ok();
    }
}
