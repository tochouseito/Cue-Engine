#pragma once

// === Base includes ===
#include <Result.h>

// === PAL includes ===
#include "PlatformFactory.h"
#include "PlatformMessage.h"

namespace Cue::PAL
{
    struct platform_setup_info final
    {
        uint32_t width = 0; // ウィンドウ幅
        uint32_t height = 0; // ウィンドウ高さ
        const char* className = nullptr; // ウィンドウクラス名
        const char* title = nullptr; // ウィンドウタイトル
    };

    class IPlatform
    {
    public:
        virtual ~IPlatform() = default;
        virtual Result initialize(const platform_setup_info& info) = 0;
        virtual Result start() = 0;
        virtual Result shutdown() = 0;
        virtual Result begin_frame() = 0;
        virtual Result end_frame() = 0;
        virtual PlatformMessage poll_message() = 0;
    };
}
