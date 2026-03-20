#pragma once

// === PAL include ===
#include <PAL.h>
#include <PlatformMessage.h>

// === C++ include ===
#include <memory>

// === Windows API include ===
#include "stdafx.h"
#include "App/WinApp.h"

namespace Cue::PAL::Win
{
    class WinPlatform final : public IPlatform
    {
    public:
        WinPlatform();
        ~WinPlatform() override;
        Result initialize(const platform_setup_info& info) override;
        Result start() override;
        Result shutdown() override;
        Result begin_frame() override;
        Result end_frame() override;
        PlatformMessage poll_message() override;
    private:
        bool m_isComInitialized = false; // COM 初期化フラグ
        std::unique_ptr<WinApp> m_app = nullptr; // Windows アプリ
    };
}
