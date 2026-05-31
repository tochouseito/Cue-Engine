#include "D3D12Backend.h"

namespace Cue::RHI::DX12
{
    D3D12Backend::D3D12Backend()
    {
    }

    Result D3D12Backend::initialize(const RenderBackendSetupInfo& a_info)
    {
        // 引数の保存
        m_width = a_info.width;
        m_height = a_info.height;
        m_bufferCount = a_info.bufferCount;

        // レンダーデバイスの初期化
        m_renderDevice = std::make_unique<DX12RenderDevice>();
        m_renderDevice->initialize(a_info.enableDebugLayer);

        // デスクリプタアロケータの初期化
        m_descriptorAllocator = std::make_unique<DescriptorAllocator>(*m_renderDevice->get_d3d12_device());
        m_descriptorAllocator->initialize(
            a_info.textureCapacity, a_info.bufferCapacity,
            a_info.renderTargetCapacity, a_info.depthStencilCapacity);


        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        return Result::ok();
    }
}
