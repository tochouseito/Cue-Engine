#include "D3D12Backend.h"

namespace Cue::RHI::DX12
{
    Result D3D12Backend::initialize(const RenderBackendSetupInfo& a_info)
    {
        m_width = a_info.width;
        m_height = a_info.height;
        m_bufferCount = a_info.bufferCount;
        return Result::ok();
    }
    Result D3D12Backend::shutdown()
    {
        return Result::ok();
    }
}
