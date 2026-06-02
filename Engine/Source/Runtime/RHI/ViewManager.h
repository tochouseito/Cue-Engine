// ViewManager の役割と公開要素を定義する

#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    struct ViewDesc
    {
        std::string name = {};
        ViewType type = ViewType::ShaderResourceBuffer;
        BufferKind bufferKind = BufferKind::Buffer;
        uint32_t resourceIndex = 0;

        BufferHandle bufferHandle = {};
        TextureHandle textureHandle = {};

        uint64_t byteOffset = 0;
        uint32_t byteSize = 0;
        uint32_t firstElement = 0;
        uint32_t numElements = 0;
        uint32_t structureByteStride = 0;

        ColorFormat colorFormat = ColorFormat::R8G8B8A8_UNORM;
        uint32_t mipSlice = 0;
        uint32_t mipLevels = 1;
    };

    class IViewManager
    {
    public:
        IViewManager() = default;
        // コピー禁止
        IViewManager(const IViewManager&) = delete;
        IViewManager& operator=(const IViewManager&) = delete;
        // ムーブ禁止
        IViewManager(IViewManager&&) = delete;
        IViewManager& operator=(IViewManager&&) = delete;
        virtual ~IViewManager() = default;

        // --- ビューの生成と破棄 ---
        virtual Result create_view(const ViewDesc& desc, ViewHandle& out) = 0;
        virtual Result destroy_view(ViewHandle handle) = 0;

        // --- 名前からビューのハンドルの取得 ---
        virtual Result get_view(std::string_view name, ViewHandle& out) = 0;
    };
}
