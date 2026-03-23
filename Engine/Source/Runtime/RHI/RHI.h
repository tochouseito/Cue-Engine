#pragma once

// === RHI includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    /// <summary>
    /// バックエンド初期化時に必要な設定です。
    /// </summary>
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

    /// <summary>
    /// レンダリングバックエンドの共通インターフェースです。
    /// </summary>
    class IBackend
    {
    public:
        virtual ~IBackend() = default;

        /// <summary>
        /// バックエンドを初期化します。
        /// </summary>
        virtual Result initialize(const BackendSetupInfo& a_info) = 0;

        /// <summary>
        /// バックエンドを終了します。
        /// </summary>
        virtual Result shutdown() = 0;

        /// <summary>
        /// 指定フレームの描画を実行します。
        /// </summary>
        virtual Result render(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph) = 0;

        /// <summary>
        /// 指定フレームの提示処理を実行します。
        /// </summary>
        virtual Result present(uint64_t a_frameNo, uint32_t a_index, FrameGraph& a_frameGraph) = 0;
    };
}
