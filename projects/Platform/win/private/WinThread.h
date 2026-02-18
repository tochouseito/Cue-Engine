#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>

#include <Result.h>
#include <Threading/IThread.h>
#include <Threading/StopToken.h>

namespace Cue::Platform::Win
{
    class WinThread final : public Cue::Core::Threading::IThread
    {
    public:
        WinThread() noexcept = default;
        ~WinThread() noexcept override;

        WinThread(const WinThread&) = delete;
        WinThread& operator=(const WinThread&) = delete;

        WinThread(WinThread&& rhs) noexcept;
        WinThread& operator=(WinThread&& rhs) noexcept;

        static Cue::Result create(
            const Cue::Core::Threading::ThreadDesc& desc,
            Cue::Core::Threading::ThreadProc proc,
            void* user,
            WinThread& outThread) noexcept;

        bool joinable() const noexcept override;
        Cue::Result join() noexcept override;

        void request_stop() noexcept override;
        Cue::Core::Threading::StopToken stop_token() const noexcept override;

        uint32_t thread_id() const noexcept override;
        uint32_t exit_code() const noexcept override;

    private:
        struct StartContext final
        {
            Cue::Core::Threading::ThreadProc proc = nullptr;
            void* user = nullptr;

            Cue::Core::Threading::StopSource stopSource{};

            // 1) 競合を避けるため atomic にする（コピー不可だが、ヒープに置くので問題ない）
            std::atomic<uint32_t> exitCode{ 0 };

            StartContext() noexcept = default;

            StartContext(const StartContext&) = delete;
            StartContext& operator=(const StartContext&) = delete;
        };

        static unsigned __stdcall thread_entry(void* p) noexcept;

        void close_handle_no_wait() noexcept;

    private:
        void* m_handle = nullptr; // HANDLE を晒さない
        uint32_t m_threadId = 0;

        std::unique_ptr<StartContext> m_ctx{};
        bool m_joinable = false;
    };
}
