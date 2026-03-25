#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    struct BufferDesc
    {
        std::string_view name;
        BufferType type = BufferType::Unknown;
        uint32_t defaultHeapCount = 0; // デフォルトのヒープ数（バッファリングなしの場合は1）
        uint32_t uploadHeapCount = 0; // アップロードヒープの数（アップロードが必要な場合は1以上）
        ResourceState initialState = ResourceState::Common;
        uint32_t stride = 0; // StructuredBufferの要素サイズなど、リソースのインスタンスごとのサイズ
        uint32_t elementCount = 0; // StructuredBufferの要素数など、リソースのインスタンスごとの要素数
        uint32_t size = 0; // バッファ全体のサイズ（stride * インスタンス数など）
        uint32_t alignment = 0; // バッファのアライメント要件
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
