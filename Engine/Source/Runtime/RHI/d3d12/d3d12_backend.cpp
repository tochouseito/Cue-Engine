#include "d3d12_backend.h"

namespace Cue::RHI
{
    std::unique_ptr<IBackend> create_backend()
    {
        return std::make_unique<D3D12::D3D12Backend>();
    }
}

namespace Cue::RHI::D3D12
{
    D3D12Backend::D3D12Backend()
    {}
    D3D12Backend::~D3D12Backend()
    {}
    Result D3D12Backend::initialize(const backend_setup_info & info)
    {
        info;
        return Result();
    }
    Result D3D12Backend::shutdown()
    {
        return Result();
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
