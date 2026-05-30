#include "D3D12Backend.h"

namespace Cue::RHI::DX12
{
    D3D12Backend::D3D12Backend()
    {
        m_renderDevice = std::make_unique<DX12RenderDevice>();
    }

    Result D3D12Backend::initialize(const RenderBackendSetupInfo& a_info)
    {
        m_width = a_info.width;
        m_height = a_info.height;
        m_bufferCount = a_info.bufferCount;

        m_renderDevice->initialize(a_info.enableDebugLayer);

        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        return Result::ok();
    }
}
