#pragma once
#include <GraphicsCore.h>
#include "BackendFactory.h"
#include <win_platform.h>
#include <memory>

namespace Cue::GraphicsCore::DX12
{
    class D3D12Backend : public Backend
    {
    public:
        D3D12Backend();
        ~D3D12Backend() override;
        Result initialize(const backend_setup_info& info) override;
        Result shutdown() override;
        Result build_frame_graph() override;
        Result render() override;
        void set_win_platform(Platform::IPlatform* platform);
        [[nodiscard]] Result create_frame_graph_runtime(std::unique_ptr<IFrameGraphRuntime>* outRuntime) override;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Cue::GraphicsCore::DX12
