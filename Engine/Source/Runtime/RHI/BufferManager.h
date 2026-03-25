#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    struct BufferDesc
    {
        std::string_view name;
    };

    class IBufferManager
    {
    public:
        IBufferManager() = default;
        // コピー禁止
        IBufferManager(const IBufferManager&) = delete;
        IBufferManager& operator=(const IBufferManager&) = delete;
        // ムーブ禁止
        IBufferManager(IBufferManager&&) = delete;
        IBufferManager& operator=(IBufferManager&&) = delete;
        virtual ~IBufferManager() = default;

        // --- バッファの生成と破棄 ---
        virtual Result create_buffer(const BufferDesc& desc, BufferHandle& out) = 0;
        virtual Result destroy_buffer(BufferHandle handle) = 0;
    };
}
