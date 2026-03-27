#pragma once

// === RHI includes ===
#include <RHI.h>

// === PAL includes ===
#include <win/win_platform.h>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "ResourceLeakChecker.h"
#include "HLSLCompiler.h"
#include "DX12RenderDevice.h"
#include "DescriptorAllocator.h"
#include "DX12BufferManager.h"
#include "DX12TextureManager.h"
#include "DX12ViewManager.h"

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

        /// @brief FrameGraph を生成します。
        Result create_frame_graph(std::unique_ptr<FrameGraph>& a_outFrameGraph) override;

        /// @brief 利用する Windows プラットフォームを設定します。
        void set_win_platform(PAL::Win::WinPlatform* a_platform) noexcept { m_platform = a_platform; }

        // --- バックエンドのシステムへのアクセス ---
        IBufferManager* get_buffer_manager() override { return m_bufferManager.get(); }
        ITextureManager* get_texture_manager() override { return m_textureManager.get(); }
        IViewManager* get_view_manager() override { return m_viewManager.get(); }

    private:
        PAL::Win::WinPlatform* m_platform = nullptr; // プラットフォーム
        std::unique_ptr<ResourceLeakChecker> m_leakChecker = std::make_unique<ResourceLeakChecker>(); // リソースリークチェッカー
        std::unique_ptr<HLSLCompiler> m_hlslCompiler = std::make_unique<HLSLCompiler>(); // HLSL コンパイラ
        std::unique_ptr<DX12RenderDevice> m_renderDevice = nullptr; // レンダーデバイス
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr; // デスクリプタアロケータ
        std::unique_ptr<DX12BufferManager> m_bufferManager = nullptr; // バッファマネージャ
        std::unique_ptr<DX12TextureManager> m_textureManager = nullptr; // テクスチャマネージャ
        std::unique_ptr<DX12ViewManager> m_viewManager = nullptr; // ビューマネージャ
    };
}
