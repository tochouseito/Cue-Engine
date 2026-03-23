#pragma once

// === RHI includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    // バックエンド初期化情報
    struct backend_setup_info final
    {
        bool enableDebugLayer = false;
        uint32_t width{};
        uint32_t height{};
        uint32_t textureCapacity = 256;
        uint32_t bufferCapacity = 256;
        uint32_t renderTargetCapacity = 16;
        uint32_t depthStencilCapacity = 16;
    };

    // レンダリングバックエンドのインターフェース
    class IBackend
    {
    public:
        virtual ~IBackend() = default;
        virtual Result initialize(const backend_setup_info& info) = 0;
        virtual Result shutdown() = 0;
        virtual Result render(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph) = 0;
        virtual Result present(uint64_t frameNo, uint32_t index, FrameGraph& frameGraph) = 0;
    };
}
