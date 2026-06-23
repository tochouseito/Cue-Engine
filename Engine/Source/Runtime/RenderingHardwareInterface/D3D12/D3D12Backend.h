#pragma once

/// ************************************************************************************
/// D3D12バックエンドの実装
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === RHI includes ===
#include <FrameGraph.h>
#include <RHI.h>
#include <RHICommon.h>

// === D3D12 includes ===
#include "DX12BufferManager.h"
#include "DX12GpuCommand.h"
#include "DX12GpuProfiler.h"
#include "DX12PipelineManager.h"
#include "DX12RenderDevice.h"
#include "DX12TextureManager.h"
#include "DX12ViewManager.h"
#include "DescriptorAllocator.h"
#include "HLSLCompiler.h"
#include "ResourceLeakChecker.h"
#include "SwapChain.h"

namespace Cue::RHI::DX12
{
    /// @brief D3D12バックエンドの実装
    /// @details RHI の入口として D3D12 固有オブジェクトを所有し、
    ///          デバイス、シェーダーコンパイラ、デスクリプタ管理をまとめて初期化する。
    class D3D12Backend final : public IRenderBackend
    {
      public:
        D3D12Backend();
        ~D3D12Backend() override = default;
        Result initialize(const RenderBackendSetupInfo& a_info) override;
        Result shutdown() override;
        /// @brief バックエンドで進行中の GPU 作業完了を待機する
        Result wait_for_idle() override;

        /// @brief 指定フレームの描画処理を実行する
        Result render(uint64_t a_frameNo, uint32_t a_index,
                      FrameGraph& a_frameGraph) override;

        /// @brief 指定フレームの提示処理を実行する
        Result present(uint64_t a_frameNo, uint32_t a_index, bool vsync,
                       FrameGraph& a_frameGraph) override;
        Result create_frame_graph(
            const FrameGraphDesc& a_desc,
            std::unique_ptr<FrameGraph>& a_outFrameGraph) override;

        /// @brief ウィンドウリサイズ
        Result resize(uint32_t a_width, uint32_t a_height) override;

        // --- バックエンドのシステムへのアクセス ---
        IBufferManager* get_buffer_manager() override
        {
            return m_bufferManager.get();
        }
        ITextureManager* get_texture_manager() override
        {
            return m_textureManager.get();
        }
        IViewManager* get_view_manager() override
        {
            return m_viewManager.get();
        }
        ICommandPool* get_command_pool() override
        {
            return m_commandPool.get();
        }
        IQueuePool* get_queue_pool() override
        {
            return m_queuePool.get();
        }

        uint32_t width() const noexcept override
        {
            return m_width;
        }
        uint32_t height() const noexcept override
        {
            return m_height;
        }
        const uint32_t& buffer_count() const noexcept override
        {
            return m_bufferCount;
        }
        uint32_t current_back_buffer_index() const noexcept
        {
            return m_swapChain ? m_swapChain->get_current_back_buffer_index() : 0;
        }
        Result get_gpu_memory_usage(GpuMemoryUsage& outUsage) const override
        {
            return m_gpuProfiler->get_gpu_memory_usage(outUsage);
        }
        /// @brief 利用する Windows プラットフォームを設定する
        void set_win_platform(PAL::Win::WinPlatform* a_platform) noexcept
        {
            m_platform = a_platform;
        }

        ID3D12Device* imgui_device() const noexcept
        {
            return m_renderDevice ? m_renderDevice->get_d3d12_device()
                                  : nullptr;
        }
        ID3D12CommandQueue* imgui_command_queue() const noexcept
        {
            if (m_queuePool == nullptr)
            {
                return nullptr;
            }
            IQueueContext* queue = m_queuePool->get_present_queue_context();
            auto* dxQueue = static_cast<DX12GpuCommandQueue*>(queue);
            return dxQueue ? dxQueue->command_queue() : nullptr;
        }
        ID3D12DescriptorHeap* imgui_srv_descriptor_heap() const noexcept
        {
            return m_descriptorAllocator
                       ? m_descriptorAllocator->get_descriptor_heap(
                             HeapType::CBV_SRV_UAV)
                       : nullptr;
        }
        Result allocate_imgui_srv_descriptor(
            D3D12_CPU_DESCRIPTOR_HANDLE& a_outCpuHandle,
            D3D12_GPU_DESCRIPTOR_HANDLE& a_outGpuHandle)
        {
            if (!m_descriptorAllocator)
            {
                return Result::fail(
                    Code::InvalidState, Severity::Error,
                    "DescriptorAllocator is not initialized in D3D12Backend.");
            }

            return m_descriptorAllocator
                ->allocate_shader_visible_texture_descriptor(a_outCpuHandle,
                                                             a_outGpuHandle);
        }
        void free_imgui_srv_descriptor(D3D12_CPU_DESCRIPTOR_HANDLE a_cpuHandle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE a_gpuHandle)
        {
            if (m_descriptorAllocator)
            {
                m_descriptorAllocator->free_shader_visible_texture_descriptor(
                    a_cpuHandle, a_gpuHandle);
            }
        }
        D3D12_GPU_DESCRIPTOR_HANDLE get_gpu_descriptor_handle(ViewHandle a_viewHandle, uint32_t a_frameIndex, uint32_t a_bufferCount);
      private:
        // RenderBackendSetupInfo 由来の基本設定。RHI
        // 抽象層から参照されるため保持する。
        uint32_t m_width{};
        uint32_t m_height{};
        uint32_t m_bufferCount{};
        PAL::Win::WinPlatform* m_platform =
            nullptr; // Windows
                     // プラットフォームへのポインタ。スワップチェイン作成に必要。

        // D3D12 debug layer の live object 出力はバックエンド破棄時に行う。
        // 他の D3D12 オブジェクトより後に破棄されるよう、最初に宣言している。
        std::unique_ptr<ResourceLeakChecker> m_resourceLeakChecker =
            std::make_unique<ResourceLeakChecker>();

        // バックエンド共有サービス。各 manager はこれらの実体を参照して RHI
        // handle を D3D12 object へ解決する。
        std::unique_ptr<HLSLCompiler> m_hlslCompiler =
            nullptr; // HLSLコンパイラ
        std::unique_ptr<DX12RenderDevice> m_renderDevice =
            nullptr; // レンダーデバイス
        std::unique_ptr<DescriptorAllocator> m_descriptorAllocator =
            nullptr; // デスクリプタアロケータ
        std::unique_ptr<DX12CommandPool> m_commandPool =
            nullptr; // コマンドプール
        std::unique_ptr<DX12QueuePool> m_queuePool =
            nullptr; // コマンドキュープール
        std::unique_ptr<SwapChain> m_swapChain = nullptr; // スワップチェイン
        std::unique_ptr<DX12BufferManager> m_bufferManager =
            nullptr; // バッファマネージャ
        std::unique_ptr<DX12TextureManager> m_textureManager =
            nullptr; // テクスチャマネージャ
        std::unique_ptr<DX12ViewManager> m_viewManager =
            nullptr; // ビューマネージャ
        std::unique_ptr<DX12PipelineManager> m_pipelineManager =
            nullptr; // パイプラインマネージャ
        std::unique_ptr<DX12GpuProfiler> m_gpuProfiler =
            nullptr; // GPUプロファイラ
    };
} // namespace Cue::RHI::DX12
