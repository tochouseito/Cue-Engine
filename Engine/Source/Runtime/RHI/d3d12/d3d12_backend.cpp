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

        // バッファマネージャの初期化
        m_bufferManager = std::make_unique<DX12BufferManager>(*m_renderDevice);
        m_textureManager = std::make_unique<DX12TextureManager>(*m_renderDevice);
        m_viewManager = std::make_unique<DX12ViewManager>(*m_bufferManager, *m_textureManager, *m_descriptorAllocator);

        // スワップチェインの初期化
        m_swapChain = std::make_unique<SwapChain>(*m_renderDevice, *m_descriptorAllocator, *m_textureManager);
        QueueContextLease queueContext{};
        r = m_queuePool->get_queue_context(CommandListType::Graphics, queueContext);
        if (!r)
        {
            return Result::fail(
                Code::GetFailed,
                Severity::Fatal,
                "Failed to get graphics queue context for swap chain initialization.");
        }
        m_swapChain->create(
            m_platform->get_window_handle(),
            a_info.width,
            a_info.height,
            a_info.bufferCount,
            *static_cast<DX12GpuCommandQueue*>(queueContext.get()));

        // リソースアップローダの初期化
        m_resourceUploader = std::make_unique<ResourceUploader>(*m_bufferManager, *m_commandPool, *m_queuePool);

        // 静的メッシュプールの初期化
        StaticMeshPoolDesc meshPoolDesc{};
        m_staticMeshPool = std::make_unique<DX12StaticMeshPool>(meshPoolDesc, *m_bufferManager, *m_resourceUploader);

        return Result::ok();
    }

    Result D3D12Backend::shutdown()
    {
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
        a_index;
        a_frameGraph;
        return Result();
    }

    Result D3D12Backend::present(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph)
    {
        a_frameNo;
        a_index;
        a_frameGraph;
        return Result();
    }

    Result D3D12Backend::create_frame_graph(std::unique_ptr<FrameGraph>& a_outFrameGraph)
    {
        a_outFrameGraph;
        return Result();
    }
}
