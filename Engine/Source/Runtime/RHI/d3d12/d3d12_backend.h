#pragma once

// === RHI includes ===
#include <RHI.h>

// === PAL includes ===
#include <win/win_platform.h>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "DX12RenderDevice.h"
#include "DescriptorAllocator.h"

namespace Cue::RHI::DX12
{
    class D3D12Backend final : public IBackend
    {
    public:
        D3D12Backend() = default;
        ~D3D12Backend() override = default;

        /// @brief D3D12 バックエンドを初期化します。
        Result initialize(const BackendSetupInfo& a_info) override;

        /// @brief D3D12 バックエンドを終了します。
        Result shutdown() override;

        /// @brief 指定フレームの描画処理を実行します。
        Result render(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph) override;

        /// @brief 指定フレームの提示処理を実行します。
        Result present(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph) override;

        /// @brief 利用する Windows プラットフォームを設定します。
        void set_win_platform(PAL::Win::WinPlatform* a_platform) noexcept { m_platform = a_platform; }

    private:
        PAL::Win::WinPlatform* m_platform = nullptr; // プラットフォーム
        std::unique_ptr<DX12RenderDevice> m_renderDevice = nullptr; // レンダーデバイス
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr; // デスクリプタアロケータ
    };
}
