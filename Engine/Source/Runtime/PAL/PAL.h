#pragma once

// === C++ includes ===
#include <cstdint>
#include <string>
#include <vector>

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <Threading/IThread.h>
#include <Threading/IThreadFactory.h>
#include <Time/IClock.h>
#include <Time/IWaiter.h>
#include <Time/Timer.h>

// === PAL includes ===
#include "Input/InputManager.h"
#include "PlatformFactory.h"
#include "PlatformMessage.h"

namespace Cue::PAL
{
    /// @brief プラットフォーム初期化時の設定です。
    struct PlatformSetupInfo final
    {
        uint32_t width = 0; // ウィンドウ幅
        uint32_t height = 0; // ウィンドウ高さ
        const char* className = nullptr; // ウィンドウクラス名
        const char* title = nullptr; // ウィンドウタイトル
    };

    /// @brief プラットフォーム実装の共通インターフェースです。
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
        /// @brief 管理中のアプリケーションウィンドウがフォーカスされているか返します。
        [[nodiscard]] virtual bool is_window_focused() const noexcept = 0;
        /// @brief ファイルのドラッグアンドドロップ受け付けを切り替えます。
        virtual Result set_drag_drop_enabled(bool a_isEnabled) = 0;
        /// @brief ファイルのドラッグアンドドロップ受け付けが有効か返します。
        [[nodiscard]] virtual bool is_drag_drop_enabled() const noexcept = 0;
        /// @brief ドロップされたファイルパスを取り出します。
        [[nodiscard]] virtual bool consume_dropped_files(
            std::vector<std::string>& a_outPaths) noexcept = 0;
    public:
        // --- 取得 ---
        virtual Core::Threading::IThreadFactory& thread_factory() = 0;
        virtual Core::Time::IClock& clock() = 0;
        virtual Core::Time::IWaiter& waiter() = 0;
        virtual Core::IO::IFileSystem& file_system() = 0;
        virtual InputManager& input_manager() = 0;
    };
}
