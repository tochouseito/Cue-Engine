#include "D3D12Backend.h"

namespace Cue::RHI::DX12
{
    D3D12Backend::D3D12Backend()
    {
    }

    Result D3D12Backend::initialize(const RenderBackendSetupInfo& a_info)
    {
        // 引数の保存
        // ウィンドウサイズやバッファ数は SwapChain/FrameController と共有される基本設定なので、
        // バックエンド側でも問い合わせ可能な状態にしておく。
        m_width = a_info.width;
        m_height = a_info.height;
        m_bufferCount = a_info.bufferCount;

        // レンダーデバイスの初期化
        // DXGI factory と ID3D12Device は後続の manager がすべて依存するため最初に作る。
        m_renderDevice = std::make_unique<DX12RenderDevice>();
        m_renderDevice->initialize(a_info.enableDebugLayer);

        // デスクリプタアロケータの初期化
        // View/Texture/Buffer manager が使う CPU/GPU descriptor heap をまとめて確保する。
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
