#pragma once

// === RHI includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    /// @brief バックエンド初期化時に必要な設定です。
    struct BackendSetupInfo final
    {
        bool enableDebugLayer = false;
        uint32_t width{};
        uint32_t height{};
        uint32_t textureCapacity = 256;
        uint32_t bufferCapacity = 256;
        uint32_t renderTargetCapacity = 16;
        uint32_t depthStencilCapacity = 16;
    };

    /// @brief レンダリングバックエンドの共通インターフェースです。
    class IBackend
    {
    public:
        virtual ~IBackend() = default;

        /// @brief バックエンドを初期化します。
        virtual Result initialize(const BackendSetupInfo& a_info) = 0;

        /// @brief バックエンドを終了します。
        virtual Result shutdown() = 0;

        /// @brief 指定フレームの描画を実行します。
        virtual Result render(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph) = 0;

        /// @brief 指定フレームの提示処理を実行します。
        virtual Result present(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph) = 0;

        /// @brief FrameGraph の生成
        virtual Result create_frame_graph(std::unique_ptr<FrameGraph>& a_outFrameGraph) = 0;

        // --- バックエンドのシステムへのアクセス ---
        virtual IBufferManager* get_buffer_manager() = 0;
    };
}
