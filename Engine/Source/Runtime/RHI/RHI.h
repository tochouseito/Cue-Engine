#pragma once

// === Base includes ===
#include <Result.h>

// === RHI includes ===
#include "BackendFactory.h"
#include "FrameGraph.h"

// === C++ includes ===
#include <cstdint>

namespace Cue::RHI
{
    // バックエンド初期化情報
    struct backend_setup_info final
    {
        bool enableDebugLayer = false;
        uint32_t width{};
        uint32_t height{};
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
