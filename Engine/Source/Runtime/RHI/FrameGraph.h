#pragma once

// === RHI includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    class FrameGraphPass;

    struct FrameGraphDesc final
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    /// @brief フレーム単位の描画依存関係を保持するプレースホルダーです。
    class FrameGraph final
    {
    public:
        FrameGraph() = default;
        // コピー禁止
        FrameGraph(const FrameGraph&) = delete;
        FrameGraph& operator=(const FrameGraph&) = delete;
        // ムーブ禁止
        FrameGraph(FrameGraph&&) = delete;
        FrameGraph& operator=(FrameGraph&&) = delete;
        ~FrameGraph() = default;

        /// @brief パスの追加

        /// @brief 描画依存関係を構築します。
        Result build();

        /// @brief パスの実行
        Result execute();
    };
}
