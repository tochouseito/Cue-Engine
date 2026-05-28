#pragma once

/// ********************************************************************************
/// プラットフォーム抽象化レイヤー - Windows 実装
/// ********************************************************************************

// === PAL includes ===
#include <PAL.h>

// === win_platform includes ===
#include "stdafx.h"
#include "App/WinApp.h"

// === C++ includes ===
#include <memory>

namespace Cue::PAL::Win
{
    /// @brief Windows 向けプラットフォーム実装です。
    class WinPlatform final : public IPlatform
    {
    public:
        WinPlatform();
        ~WinPlatform() override;

        /// @brief プラットフォーム実装を初期化します。
        Result initialize(const PlatformSetupInfo& a_info) override;
        /// @brief ウィンドウ表示を開始します。
        Result start() override;
        /// @brief 終了処理を行います。
        Result shutdown() override;
        /// @brief フレーム開始処理を行います。
        Result begin_frame() override;
        /// @brief フレーム終了処理を行います。
        Result end_frame() override;
        /// @brief Windows メッセージを 1 件取得します。
        PlatformMessage poll_message() override;
    private:
        bool m_isComInitialized = false; // COM 初期化フラグ
        std::unique_ptr<WinApp> m_app = nullptr; // ウィンドウ管理
    };
}
