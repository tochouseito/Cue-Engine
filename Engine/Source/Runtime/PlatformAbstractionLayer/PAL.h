#pragma once

/// ********************************************************************************
/// プラットフォーム抽象化レイヤー
/// ********************************************************************************

/// === C++ includes ===
#include <cstdint>

/// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <Threading/IThread.h>
#include <Threading/IThreadFactory.h>
#include <Time/IClock.h>
#include <Time/IWaiter.h>
#include <Time/Timer.h>

/// === PAL includes ===
#include "PlatformMessage.h"

namespace Cue::PAL
{
    /// @brief プラットフォーム共通初期化設定
    struct PlatformSetupInfo final
    {
        uint32_t width = 0; // ウィンドウ幅
        uint32_t height = 0; // ウィンドウ高さ
        const char* className = nullptr; // ウィンドウクラス名
        const char* title = nullptr; // ウィンドウタイトル
    };

    /// @brief プラットフォームインタフェース
    class IPlatform
    {
    public:
        virtual ~IPlatform() = default;

        /// @brief プラットフォーム実装を初期化します。
        /// @param a_info 初期化設定です。
        virtual Result initialize(const PlatformSetupInfo& a_info) = 0;
        /// @brief 実行開始処理を行います。
        virtual Result start() = 0;
        /// @brief 終了処理を行います。
        virtual Result shutdown() = 0;
        /// @brief フレーム開始処理を行います。
        virtual Result begin_frame() = 0;
        /// @brief フレーム終了処理を行います。
        virtual Result end_frame() = 0;
        /// @brief プラットフォームメッセージを 1 件取得します。
        virtual PlatformMessage poll_message() = 0;
    public:
        // --- 取得 ---
        virtual Core::Threading::IThreadFactory& thread_factory() = 0;
        virtual Core::Time::IClock& clock() = 0;
        virtual Core::Time::IWaiter& waiter() = 0;
        virtual Core::IO::IFileSystem& file_system() = 0;
    private:

    };
}
