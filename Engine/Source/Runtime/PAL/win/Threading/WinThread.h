#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Threading/IThread.h>
#include <Threading/StopToken.h>

// === C++ includes ===
#include <cstdint>
#include <memory>

// === Windows API include ===
#include "stdafx.h"

namespace Cue::PAL::Win
{
    class WinThread final : public Core::Threading::IThread
    {
    public:
        WinThread() noexcept = default;
        ~WinThread() override;
        // ムーブを許可する
        WinThread(WinThread&& other) noexcept;
        WinThread& operator=(WinThread&& other) noexcept;

        // --- Thread creation ---
        static Result create(
            const Core::Threading::ThreadDesc& desc,
            Core::Threading::ThreadProc proc,
            void* user,
            WinThread& out_thread) noexcept;

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

            Core::Threading::ThreadProc proc = nullptr;
            void* user = nullptr;
            Core::Threading::StopSource stopSource{};
            std::atomic<uint32_t> exitCode{ 0 }; // スレッド終了コード
        };

        static unsigned __stdcall thread_entry(void* p) noexcept;

        void close_handle_no_wait() noexcept;
    private:
        void* m_handle = nullptr; // スレッドハンドル
        uint32_t m_threadId = 0; // スレッドID
    };
}
