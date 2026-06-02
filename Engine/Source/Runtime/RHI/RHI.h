// RHI の役割と公開要素を定義する

#pragma once

// === RHI includes ===
#include "RHICommon.h"
#include "FrameGraph.h"
#include "BufferManager.h"
#include "TextureManager.h"
#include "ViewManager.h"

namespace Cue::RHI
{
    /// @brief バックエンド初期化時に必要な設定
    struct BackendSetupInfo final
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

    /// @brief レンダリングバックエンドの共通インターフェース
    class IBackend
    {
    public:
        virtual ~IBackend() = default;

        /// @brief バックエンドを初期化する
        virtual Result initialize(const BackendSetupInfo& a_info) = 0;

        /// @brief バックエンドを終了し
        virtual Result shutdown() = 0;

        /// @brief バックエンドで進行中の GPU 作業完了を待機する
        virtual Result wait_for_idle() = 0;

        /// @brief 指定フレームの描画を実行する
        virtual Result render(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph) = 0;

        /// @brief 指定フレームの提示処理を実行する
        virtual Result present(uint64_t a_frameNo, uint32_t a_index,bool vsync, FrameGraph& a_frameGraph) = 0;

        /// @brief サイズ依存のバックエンド資源をリサイズし
        virtual Result resize(uint32_t a_width, uint32_t a_height) = 0;

        /// @brief FrameGraph の生成
        virtual Result create_frame_graph(const FrameGraphDesc& a_desc, std::unique_ptr<FrameGraph>& a_outFrameGraph) = 0;

        // --- バックエンドのシステムへのアクセス ---
        virtual IBufferManager* get_buffer_manager() = 0;
        virtual ITextureManager* get_texture_manager() = 0;
        virtual IViewManager* get_view_manager() = 0;
        virtual ICommandPool* get_command_pool() = 0;
        virtual IQueuePool* get_queue_pool() = 0;
        virtual uint32_t width() const noexcept = 0;
        virtual uint32_t height() const noexcept = 0;

        // --- パラメーターの取得 ---
        virtual const uint32_t& buffer_count() const noexcept = 0;
    };
}
