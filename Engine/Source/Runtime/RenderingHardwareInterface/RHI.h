#pragma once

/// ************************************************************************************
/// グラフィックスAPIの抽象化レイヤー
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === RHI includes ===
#include "RHICommon.h"
#include "FrameGraph.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ViewManager.h"

// === C++ includes ===
#include <cstdint>

namespace Cue::RHI
{
    /// @brief レンダーバックエンドの初期化に必要な情報
    struct RenderBackendSetupInfo final
    {
        bool enableDebugLayer = false;
        uint32_t width{};
        uint32_t height{};
        uint32_t bufferCount = 3;
        uint32_t textureCapacity = 256;
        uint32_t bufferCapacity = 256;
        uint32_t renderTargetCapacity = 16;
        uint32_t depthStencilCapacity = 16;
    };

    /// @brief レンダーバックエンドのインターフェース
    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;

        /// @brief バックエンドを初期化する
        virtual Result initialize(const RenderBackendSetupInfo& a_info) = 0;

        /// @brief バックエンドを終了し
        virtual Result shutdown() = 0;

        /// @brief バックエンドで進行中の GPU 作業完了を待機する
        virtual Result wait_for_idle() = 0;

        /// @brief 指定フレームの描画を実行する
        virtual Result render(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph) = 0;

        /// @brief 指定フレームの提示処理を実行する
        virtual Result present(uint64_t a_frameNo, uint32_t a_index, bool vsync, FrameGraph& a_frameGraph) = 0;

        /// @brief FrameGraph の生成
        virtual Result create_frame_graph(const FrameGraphDesc& a_desc, std::unique_ptr<FrameGraph>& a_outFrameGraph) = 0;

        // --- バックエンドのシステムへのアクセス ---
        virtual IBufferManager* get_buffer_manager() = 0;
        virtual ITextureManager* get_texture_manager() = 0;
        virtual IViewManager* get_view_manager() = 0;
        virtual ICommandPool* get_command_pool() = 0;
        virtual IQueuePool* get_queue_pool() = 0;
        
        // --- パラメーターの取得 ---
        virtual uint32_t width() const noexcept = 0;
        virtual uint32_t height() const noexcept = 0;
        virtual const uint32_t& buffer_count() const noexcept = 0;
    };
}
