#pragma once

// === Core includes ===
#include <CQRS/CQRS.h>

// === PAL includes ===
#include <PAL.h>
#include <PlatformMessage.h>

// === C++ includes ===
#include <memory>

// === Windows API includes ===
#include "App/WinApp.h"
#include "IO/WinFileSystem.h"
#include "Threading/WinThread.h"
#include "Threading/WinThreadFactory.h"
#include "Time/WinQpcClock.h"
#include "Time/WinWaiter.h"
#include "ConvertHresult.h"
#include "ConvertUTF.h"
#include "stdafx.h"

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

        /// @brief 作成済みウィンドウハンドルを返します。
        HWND get_window_handle() const noexcept
        {
            return m_app ? m_app->get_window_handle() : nullptr;
        }

        [[nodiscard]] uint64_t register_message_handler(WinApp::messageHandler a_handler);
        /// @brief メッセージハンドラ解除
        bool unregister_message_handler(uint64_t handlerId);
        /// @brief Platform 用 command bridge を設定します。
        void set_platform_bridge(Core::CQRS::Bridge* a_bridge) noexcept;
    public:
        // --- 取得 --- 
        Core::Threading::IThreadFactory& thread_factory() override
        {
            return *m_threadFactory.get();
        }
        Core::Time::IClock& clock() override
        {
            return *m_clock.get();
        }
        Core::Time::IWaiter& waiter() override
        {
            return *m_waiter.get();
        }
        Core::IO::IFileSystem& file_system() override
        {
            return *m_fileSystem.get();
        }
    private:
        bool m_isComInitialized = false; // COM 初期化フラグ
        std::unique_ptr<WinApp> m_app = nullptr; // Windows アプリ
        std::unique_ptr<WinFileSystem> m_fileSystem = nullptr; // ファイルシステム
        std::unique_ptr<WinThreadFactory> m_threadFactory = nullptr; // スレッドファクトリ
        std::unique_ptr<WinQpcClock> m_clock = nullptr; // クロック
        std::unique_ptr<WinWaiter> m_waiter = nullptr; // ウェイタ
    };
}
