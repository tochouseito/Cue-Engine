#include "D3D12Backend.h"

namespace Cue::RHI
{
    std::unique_ptr<IBackend> create_backend()
    {
        return std::make_unique<DX12::D3D12Backend>();
    }
}

namespace Cue::RHI::DX12
{
    Result D3D12Backend::initialize(const BackendSetupInfo& a_info)
    {
        // バッファ数を保存
        m_bufferCount = a_info.bufferCount;

        // バックエンドが使うデバイスを先に確定しないと後続リソースを構築できません。
        m_renderDevice = std::make_unique<DX12::DX12RenderDevice>();
        Result r = m_renderDevice->initialize(a_info.enableDebugLayer);
        if (!r)
        {
            return Result::fail(
                r.code, Severity::Fatal,
                "Failed to initialize D3D12 render device.");
        }

        // デバイス確立後にヒープ容量を固定して、後段の割り当て責務を一箇所へ寄せます。
        m_descriptorAllocator = std::make_unique<DescriptorAllocator>(*m_renderDevice->get_d3d12_device());
        m_descriptorAllocator->initialize(
            a_info.textureCapacity,
            a_info.bufferCapacity,
            a_info.renderTargetCapacity,
            a_info.depthStencilCapacity);

        // バッファマネージャの初期化
        m_bufferManager = std::make_unique<DX12BufferManager>(*m_renderDevice);
        m_textureManager = std::make_unique<DX12TextureManager>(*m_renderDevice);
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
        m_swapChain->create(
            m_platform->get_window_handle(),
            a_info.width,
            a_info.height,
            a_info.bufferCount,
            *static_cast<DX12GpuCommandQueue*>(presentQueueContext));

        // 静的メッシュプールの初期化
        StaticMeshPoolDesc meshPoolDesc{};
        m_staticMeshPool = std::make_unique<DX12StaticMeshPool>(meshPoolDesc, *m_bufferManager, *m_viewManager, *m_commandPool, *m_queuePool);

        return Result::ok();
    }

    Result D3D12Backend::shutdown()
    {
        // graphics キューの完了を待ってからリソースを解放します。
        if (m_queuePool)
        {
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
        }

        if (m_swapChain)
        {
            m_swapChain->shutdown();
            m_swapChain.reset();
        }

        m_staticMeshPool.reset();
        m_viewManager.reset();
        m_textureManager.reset();
        m_bufferManager.reset();
        m_descriptorAllocator.reset();
        m_renderDevice.reset();

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
        desc.staticMeshPool = m_staticMeshPool.get();
        desc.commandPool = m_commandPool.get();
        desc.queuePool = m_queuePool.get();
        desc.width = m_swapChain->width();
        desc.height = m_swapChain->height();

        a_outFrameGraph = std::make_unique<FrameGraph>(desc, m_bufferCount);
        return Result::ok();
    }

    ID3D12CommandQueue* D3D12Backend::get_graphics_command_queue() const
    {
        queueContextLease queueContext{};
        Result r = m_queuePool->get_queue_context(CommandListType::Graphics, queueContext);
        if (!r)
        {
            return nullptr;
        }
        auto* d3d12QueuePtr = static_cast<DX12GpuCommandQueue*>(queueContext.get());
        ID3D12CommandQueue* commandQueue = d3d12QueuePtr->command_queue();
        r = m_queuePool->return_queue_context(queueContext);
        return r ? commandQueue : nullptr;
    }

    ImGuiFontSRVInfo D3D12Backend::get_font_srv_for_imgui() const
    {
        ImGuiFontSRVInfo result{};
        if (!m_descriptorAllocator)
        {
            CUE_ASSERT_MSG(false, "DescriptorAllocator is not initialized in D3D12Backend.");
        }
        else
        {
            TableID fontTable = m_descriptorAllocator->allocate(TableKind::Textures);
            result.srvDescHeap = m_descriptorAllocator->get_descriptor_heap(HeapType::CBV_SRV_UAV);
            result.cpuDescHandle = m_descriptorAllocator->get_cpu_handle_gpu_visible(fontTable);
            result.gpuDescHandle = m_descriptorAllocator->get_gpu_handle(fontTable);
        }
        return result;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12Backend::get_gpu_descriptor_handle(viewHandle a_viewHandle, uint32_t a_frameIndex, uint32_t a_bufferCount)
    {
        D3D12_GPU_DESCRIPTOR_HANDLE result{};
        if (!m_viewManager || !m_descriptorAllocator || !a_viewHandle.valid())
        {
            return result;
        }

        DX12ViewRecord* record = nullptr;
        if (!m_viewManager->try_get_record(a_viewHandle, &record) || record == nullptr || record->defaultTableIds.empty())
        {
            return result;
        }

        uint32_t descriptorIndex = 0;
        if (record->defaultTableIds.size() == 1)
        {
            descriptorIndex = 0;
        }
        else
        {
            if (record->defaultTableIds.size() != a_bufferCount || a_frameIndex >= record->defaultTableIds.size())
            {
                return result;
            }

            descriptorIndex = a_frameIndex;
        }

        return m_descriptorAllocator->get_gpu_handle(record->defaultTableIds[descriptorIndex]);
    }
}
