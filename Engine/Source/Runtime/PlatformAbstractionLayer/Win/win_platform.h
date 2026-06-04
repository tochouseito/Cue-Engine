#pragma once

/// ********************************************************************************
/// プラットフォーム抽象化レイヤー - Windows 実装
/// ********************************************************************************

// === PAL includes ===
#include <PAL.h>

// === win_platform includes ===
#include "WinCommon.h"
#include "App/WinApp.h"
#include "IO/WinFileSystem.h"
#include "Threading/WinThread.h"
#include "Threading/WinThreadFactory.h"
#include "Time/WinQpcClock.h"
#include "Time/WinWaiter.h"
#include "DebugTool/CPUProfiler.h"

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
        /// @brief 作成済みウィンドウハンドルを返す
        HWND get_window_handle() const noexcept
        {
            return m_app ? m_app->get_window_handle() : nullptr;
        }
        void set_message_handler(WinApp::MessageHandler a_handler)
        {
            if (m_app)
            {
                m_app->set_message_handler(std::move(a_handler));
            }
        }
        Result get_process_memory_usage(ProcessMemoryUsage& a_out) noexcept override
        {
            if (m_cpuProfiler)
            {
                return m_cpuProfiler->get_process_memory_usage(a_out);
            }
            return Result::fail(
                Code::InitializeFailed, Severity::Error, "CPUProfiler is not initialized");
        }
        Result get_system_memory_usage(SystemMemoryUsage& a_out) noexcept override
        {
            if (m_cpuProfiler)
            {
                return m_cpuProfiler->get_system_memory_usage(a_out);
            }
            return Result::fail(
                Code::InitializeFailed, Severity::Error, "CPUProfiler is not initialized");
        }

    private:
        bool m_isComInitialized = false; // COM 初期化フラグ
        std::unique_ptr<WinApp> m_app = nullptr; // ウィンドウ管理
        std::unique_ptr<WinFileSystem> m_fileSystem = nullptr; // ファイルシステム
        std::unique_ptr<WinThreadFactory> m_threadFactory = nullptr; // スレッドファクトリ
        std::unique_ptr<WinQpcClock> m_clock = nullptr; // クロック
        std::unique_ptr<WinWaiter> m_waiter = nullptr; // ウェイタ
        std::unique_ptr<CPUProfiler> m_cpuProfiler = nullptr; // CPU プロファイラ
    };
}
