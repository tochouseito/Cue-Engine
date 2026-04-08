#pragma once

// === RHI includes ===
#include <RHI.h>

// === PAL includes ===
#include <WinPlatform.h>

// === DirectX 12 includes ===
#include "stdafx.h"
#include "ResourceLeakChecker.h"
#include "HLSLCompiler.h"
#include "DX12RenderDevice.h"
#include "DescriptorAllocator.h"
#include "DX12GpuCommand.h"
#include "SwapChain.h"
#include "DX12BufferManager.h"
#include "DX12TextureManager.h"
#include "DX12ViewManager.h"
#include "DX12PipelineManager.h"
#include "DX12StaticMeshPool.h"

namespace Cue::RHI::DX12
{
    struct ImGuiFontSRVInfo final
    {
        ID3D12DescriptorHeap* srvDescHeap = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle = {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle = {};
    };

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
        Result present(uint64_t a_frameNo, uint32_t a_index, bool vsync, FrameGraph& a_frameGraph) override;

        /// @brief FrameGraph を生成します。
        Result create_frame_graph(const FrameGraphDesc& a_desc, std::unique_ptr<FrameGraph>& a_outFrameGraph) override;

        /// @brief 利用する Windows プラットフォームを設定します。
        void set_win_platform(PAL::Win::WinPlatform* a_platform) noexcept { m_platform = a_platform; }

        // --- バックエンドのシステムへのアクセス ---
        IBufferManager* get_buffer_manager() override { return m_bufferManager.get(); }
        ITextureManager* get_texture_manager() override { return m_textureManager.get(); }
        IViewManager* get_view_manager() override { return m_viewManager.get(); }
        IStaticMeshPool* get_static_mesh_pool() override { return m_staticMeshPool.get(); }
        uint32_t width() const noexcept override { return m_swapChain ? m_swapChain->width() : 0; }
        uint32_t height() const noexcept override { return m_swapChain ? m_swapChain->height() : 0; }

        // --- パラメーターの取得 ---
        const uint32_t& buffer_count() const noexcept override
        {
            return m_bufferCount;
        }
        uint32_t current_back_buffer_index() const noexcept
        {
            return m_swapChain ? m_swapChain->get_current_back_buffer_index() : 0;
        }

        // --- ImGui 用 ---
        ID3D12Device* get_device() const { return m_renderDevice->get_d3d12_device(); }
        ID3D12CommandQueue* get_graphics_command_queue() const;
        ImGuiFontSRVInfo get_font_srv_for_imgui() const;
        D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_descriptor_handle(ViewHandle a_viewHandle, uint32_t a_frameIndex, uint32_t a_bufferCount);
    private:
        PAL::Win::WinPlatform* m_platform = nullptr; // プラットフォーム
        std::unique_ptr<ResourceLeakChecker> m_leakChecker = std::make_unique<ResourceLeakChecker>(); // リソースリークチェッカー
        std::unique_ptr<HLSLCompiler> m_hlslCompiler = std::make_unique<HLSLCompiler>(); // HLSL コンパイラ
        std::unique_ptr<DX12RenderDevice> m_renderDevice = nullptr; // レンダーデバイス
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator = nullptr; // デスクリプタアロケータ
        std::unique_ptr<DX12CommandPool> m_commandPool = nullptr; // コマンドプール
        std::unique_ptr<DX12QueuePool> m_queuePool = nullptr; // コマンドキュープール 
        std::unique_ptr<SwapChain> m_swapChain = nullptr; // スワップチェイン
        std::unique_ptr<DX12BufferManager> m_bufferManager = nullptr; // バッファマネージャ
        std::unique_ptr<DX12TextureManager> m_textureManager = nullptr; // テクスチャマネージャ
        std::unique_ptr<DX12ViewManager> m_viewManager = nullptr; // ビューマネージャ
        std::unique_ptr<DX12PipelineManager> m_pipelineManager = nullptr; // パイプラインマネージャ
        std::unique_ptr<DX12StaticMeshPool> m_staticMeshPool = nullptr; // 静的メッシュプール
        uint32_t m_bufferCount = 0; // バッファ数
    };
}
