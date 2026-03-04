#pragma once
#include <GraphicsCore.h>
#include "BackendFactory.h"
#include <win_platform.h>
#include <memory>

// DirectX12
#include "private/stdafx.h"

namespace Cue::GraphicsCore::DX12
{
    struct font_srv_for_imgui final
    {
        ID3D12DescriptorHeap* srvDescHeap = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle = {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle = {};
    };

    class D3D12Backend : public Backend
    {
    public:
        D3D12Backend();
        ~D3D12Backend() override;
        Result initialize(const backend_setup_info& info) override;
        Result shutdown() override;
        Result build_frame_graph() override;
        Result render(uint64_t frameNo, uint32_t index) override;
        Result present(uint64_t frameNo, uint32_t index) override;
        IViewManager* get_view_manager() const override;
        FrameGraph* get_present_frame_graph() const override;
        void set_win_platform(Platform::IPlatform* platform);

        // ImGui用
        ID3D12Device* get_device() const;
        DXGI_FORMAT get_rtv_format() const;
        font_srv_for_imgui get_font_srv_for_imgui() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Cue::GraphicsCore::DX12
