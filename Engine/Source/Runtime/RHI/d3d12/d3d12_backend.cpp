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
    Result D3D12Backend::initialize(const backend_setup_info & info)
    {
        // レンダーデバイスの初期化
        m_renderDevice = std::make_unique<DX12::DX12RenderDevice>();
        Result r = m_renderDevice->initialize(info.enableDebugLayer);
        if (!r)
        {
            return Result::fail(
                r.code, Severity::Fatal,
                "Failed to initialize D3D12 render device.");
        }

        // デスクリプタアロケータの初期化
        m_descriptorAllocator = std::make_unique<DescriptorAllocator>(*m_renderDevice->get_d3d12_device());
        m_descriptorAllocator->initialize(
            info.textureCapacity,
            info.bufferCapacity,
            info.renderTargetCapacity,
            info.depthStencilCapacity);

        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        m_descriptorAllocator.reset();
        m_renderDevice.reset();

        return Result::ok();
    }
    Result D3D12Backend::render(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph)
    {
        frameNo;
        index;
        frameGraph;
        return Result();
    }
    Result D3D12Backend::present(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph)
    {
        frameNo;
        index;
        frameGraph;
        return Result();
    }
}
