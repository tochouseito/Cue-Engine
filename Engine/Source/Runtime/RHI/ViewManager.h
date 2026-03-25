#pragma once

// === RHI Includes ===
#include "RHICommon.h"

namespace Cue::RHI
{
    struct ViewDesc
    {
        std::string_view name;
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
    };
}
