#include "d3d12_backend.h"

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

        // コマンドプールの初期化
        m_commandPool = std::make_unique<DX12CommandPool>(*m_renderDevice);

        // コマンドキュープールの初期化
        m_queuePool = std::make_unique<DX12QueuePool>(*m_renderDevice);

        // スワップチェインの初期化
        m_swapChain = std::make_unique<SwapChain>(*m_renderDevice, *m_descriptorAllocator);
        QueueContextLease queueContext{};
        r = m_queuePool->get_queue_context(CommandListType::Graphics, queueContext);
        if (!r)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Fatal,
                "Failed to get graphics queue context for swap chain initialization.");
        }
        r = m_swapChain->create(
            m_platform->get_window_handle(),
            a_info.width,
            a_info.height,
            a_info.bufferCount,
            *static_cast<DX12GpuCommandQueue*>(queueContext.get()));
        if (!r)
        {
            (void)m_queuePool->return_queue_context(queueContext);
            return Result::fail(
                r.code,
                Severity::Fatal,
                "Failed to create swap chain.");
        }
        r = m_queuePool->return_queue_context(queueContext);
        if (!r)
        {
            return Result::fail(
                r.code,
                Severity::Fatal,
                "Failed to return graphics queue context after swap chain initialization.");
        }

        // バッファマネージャの初期化
        m_bufferManager = std::make_unique<DX12BufferManager>(*m_renderDevice);
        m_textureManager = std::make_unique<DX12TextureManager>(*m_renderDevice);
        m_pipelineManager = std::make_unique<DX12PipelineManager>(*m_renderDevice, *m_hlslCompiler);

        // SwapChain の backbuffer を imported texture として登録して、FrameGraph から external resource として扱えるようにする。
        TextureDesc swapChainDesc{};
        swapChainDesc.name = "SwapChainBackBuffer";
        swapChainDesc.type = TextureType::RenderTarget;
        swapChainDesc.defaultHeapCount = a_info.bufferCount;
        swapChainDesc.initialState = ResourceState::Present;
        swapChainDesc.width = a_info.width;
        swapChainDesc.height = a_info.height;
        swapChainDesc.depthOrArraySize = 1;
        swapChainDesc.mipLevels = 1;
        swapChainDesc.colorFormat = ColorFormat::R8G8B8A8_UNORM;
        r = m_textureManager->import_texture(
            swapChainDesc,
            m_swapChain->back_buffer_resources(),
            D3D12_RESOURCE_STATE_PRESENT,
            m_swapChainTextureHandle);
        if (!r)
        {
            return r;
        }

        m_viewManager = std::make_unique<DX12ViewManager>(*m_bufferManager, *m_textureManager, *m_descriptorAllocator);

        // リソースアップローダの初期化
        m_resourceUploader = std::make_unique<ResourceUploader>(*m_bufferManager, *m_commandPool, *m_queuePool);

        // 静的メッシュプールの初期化
        StaticMeshPoolDesc meshPoolDesc{};
        m_staticMeshPool = std::make_unique<DX12StaticMeshPool>(meshPoolDesc, *m_bufferManager, *m_resourceUploader);

        return Result::ok();
    }

    Result D3D12Backend::shutdown()
    {
        // 1) queue 完了を待ってから swapchain/backbuffer を解放し、GPU 実行中の最終 release を防ぎます。
        auto wait_for_queue_idle = [this](CommandListType a_type) -> Result
            {
                QueueContextLease queueContext{};
                Result result = m_queuePool->get_queue_context(a_type, queueContext);
                if (!result)
                {
                    return result;
                }

                result = queueContext->wait();
                Result returnResult = m_queuePool->return_queue_context(queueContext);
                if (!result)
                {
                    return result;
                }
                return returnResult;
            };

        if (m_queuePool)
        {
            Result result = wait_for_queue_idle(CommandListType::Graphics);
            if (!result)
            {
                return result;
            }

            result = wait_for_queue_idle(CommandListType::Compute);
            if (!result)
            {
                return result;
            }

            result = wait_for_queue_idle(CommandListType::Copy);
            if (!result)
            {
                return result;
            }
        }

        // 2) imported swapchain texture の wrapper を先に registry から外して、後段 reset で二重管理を残しません。
        if (m_textureManager && m_swapChainTextureHandle.valid())
        {
            Result result = m_textureManager->destroy_texture(m_swapChainTextureHandle);
            if (!result)
            {
                return result;
            }
            m_swapChainTextureHandle = {};
        }

        // 3) 依存の深い順に解放して、swapchain resource より先に参照側を全て落とします。
        m_staticMeshPool.reset();
        m_resourceUploader.reset();
        m_viewManager.reset();
        m_pipelineManager.reset();
        m_textureManager.reset();
        m_bufferManager.reset();
        m_swapChain.reset();
        m_queuePool.reset();
        m_commandPool.reset();
        m_descriptorAllocator.reset();
        m_renderDevice.reset();

        return Result::ok();
    }

    Result D3D12Backend::render(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph)
    {
        a_frameNo;
        // 1) FrameController が割り当てた buffered index をそのまま使い、render/present の実体解決を同じ規則へ揃えます。
        return a_frameGraph.execute(a_index);
    }

    Result D3D12Backend::present(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph)
    {
        a_frameNo;
        // 1) swapchain backbuffer だけは DXGI の current index を優先し、FrameController の汎用 index と切り分けます。
        const uint32_t backBufferIndex = m_swapChain ? m_swapChain->current_back_buffer_index() : a_index;
        Result result = a_frameGraph.execute(backBufferIndex);
        if (!result)
        {
            return result;
        }

        result = m_swapChain->present(false);
        if (!result)
        {
            return result;
        }

        // 2) flip present 自体も graphics queue 上で進むため、終了前に fence 完了まで待って backbuffer 解放と競合させません。
        QueueContextLease queueContext{};
        result = m_queuePool->get_queue_context(CommandListType::Graphics, queueContext);
        if (!result)
        {
            return result;
        }

        result = queueContext->signal();
        if (!result)
        {
            (void)m_queuePool->return_queue_context(queueContext);
            return result;
        }

        result = queueContext->wait();
        Result returnResult = m_queuePool->return_queue_context(queueContext);
        if (!result)
        {
            return result;
        }
        return returnResult;
    }

    Result D3D12Backend::create_frame_graph(std::unique_ptr<FrameGraph>& a_outFrameGraph)
    {
        a_outFrameGraph = std::make_unique<FrameGraph>(
            m_bufferManager.get(),
            m_textureManager.get(),
            m_viewManager.get(),
            m_pipelineManager.get(),
            m_staticMeshPool.get(),
            m_commandPool.get(),
            m_queuePool.get(),
            m_swapChainTextureHandle);
        return Result::ok();
    }
}
