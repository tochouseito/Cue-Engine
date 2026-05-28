#pragma once

/// *********************************************************************************
/// Windows スレッド
/// *********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <Threading/IThread.h>
#include <Threading/StopToken.h>

// === C++ includes ===
#include <atomic>
#include <cstdint>
#include <memory>

// === Windows API includes ===
#include "stdafx.h"

namespace Cue::PAL::Win
{
    /// @brief Windows スレッド実装
    class WinThread final : public Core::Threading::IThread
    {
    public:
        WinThread() noexcept = default;
        ~WinThread() override;
        // ムーブを許可する
        WinThread(WinThread&& a_other) noexcept;
        WinThread& operator=(WinThread&& a_other) noexcept;

        // --- スレッド生成 ---
        static Result create(
            const Core::Threading::ThreadDesc& a_desc,
            Core::Threading::threadProc a_proc,
            void* a_user,
            WinThread& a_outThread) noexcept;

        // --- スレッド管理 ---
        bool joinable() const noexcept override;
        Result join() noexcept override;

        // --- 停止要求 ---
        void request_stop() noexcept override;
        Core::Threading::StopToken stop_token() const noexcept override;

        // --- スレッド情報 ---
        uint32_t thread_id() const noexcept override;
        uint32_t exit_code() const noexcept override;
    private:
        struct StartContext final
        {
            StartContext() noexcept = default;
            StartContext(const StartContext&) = delete;
            StartContext& operator=(const StartContext&) = delete;

            Core::Threading::threadProc proc = nullptr;
            void* user = nullptr;
            Core::Threading::StopSource stopSource{};
            std::atomic<uint32_t> exitCode{ 0 }; // スレッド終了コード
        };

        static unsigned __stdcall thread_entry(void* a_context) noexcept;

        void close_handle_no_wait() noexcept;
    private:
        void* m_handle = nullptr; // スレッドハンドル
        uint32_t m_threadId = 0; // スレッドID
        std::unique_ptr<StartContext> m_ctx{};
        bool m_joinable = false;
    };
}
