#include "win_pch.h"
#include "WinThread.h"

namespace Cue::Platform::Win
{
    namespace
    {
        static Cue::Result make_ok() noexcept
        {
            // 1) 成功
            return Cue::Result::ok();
        }

        static Cue::Result make_fail_invalid(std::string_view msg) noexcept
        {
            // 1) 引数不正
            return Cue::Result::fail(
                Cue::Facility::Platform,
                Cue::Code::InvalidArg,
                Cue::Severity::Error,
                0,
                msg);
        }

        static Cue::Result make_fail_oom(std::string_view msg) noexcept
        {
            // 1) OOM
            return Cue::Result::fail(
                Cue::Facility::Platform,
                Cue::Code::OutOfMemory,
                Cue::Severity::Error,
                0,
                msg);
        }

        static Cue::Result make_fail_win32(DWORD e, std::string_view msg) noexcept
        {
            // 1) Win32エラー
            return Cue::Result::fail(
                Cue::Facility::Platform,
                Cue::Code::IoError,
                Cue::Severity::Error,
                static_cast<uint32_t>(e),
                msg);
        }

        static std::wstring utf8_to_wide(std::string_view s) noexcept
        {
            // 1) 空なら空
            if (s.empty())
            {
                return {};
            }

            // 2) 必要長取得
            const int len = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            if (len <= 0)
            {
                return {};
            }

            // 3) 変換
            std::wstring w;
            w.resize(static_cast<size_t>(len));
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
            return w;
        }

        static void set_thread_name_if_possible(HANDLE hThread, std::string_view name) noexcept
        {
            // 1) 空なら何もしない
            if (name.empty())
            {
                return;
            }

            // 2) 動的解決（SDK/Windows差でビルドが死ぬのを避ける）
            using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);

            HMODULE hKernel = ::GetModuleHandleW(L"kernel32.dll");
            if (hKernel == nullptr)
            {
                return;
            }

            auto fn = reinterpret_cast<Fn>(::GetProcAddress(hKernel, "SetThreadDescription"));
            if (fn == nullptr)
            {
                return;
            }

            // 3) UTF-8 -> UTF-16
            const std::wstring w = utf8_to_wide(name);
            if (w.empty())
            {
                return;
            }

            // 4) 失敗しても無視
            (void)fn(hThread, w.c_str());
        }

        static void apply_thread_options(HANDLE hThread, const Cue::Core::Threading::ThreadDesc& desc) noexcept
        {
            // 1) 優先度（0なら触らない）
            if (desc.priority != 0)
            {
                ::SetThreadPriority(hThread, desc.priority);
            }

            // 2) アフィニティ（0なら触らない）
            if (desc.affinityMask != 0)
            {
                ::SetThreadAffinityMask(hThread, static_cast<DWORD_PTR>(desc.affinityMask));
            }
        }
    }


    WinThread::~WinThread() noexcept
    {
        // 1) joinされず破棄されるのは危険なので、停止要求→join
        if (m_joinable)
        {
            request_stop();
            join();
        }

        // 2) ハンドルを閉じる
        close_handle_no_wait();

        // 3) コンテキスト破棄
        m_ctx.reset();
    }

    WinThread::WinThread(WinThread&& rhs) noexcept
    {
        // 1) ムーブ
        *this = std::move(rhs);
    }

    WinThread& WinThread::operator=(WinThread && rhs) noexcept
    {
        // 1) 自己代入防止
        if (this != &rhs)
        {
            // 2) 自分の後始末
            if (m_joinable)
            {
                request_stop();
                join();
            }
            close_handle_no_wait();
            m_ctx.reset();

            // 3) 所有権を移す（StartContextはポインタだけ移す）
            m_handle = rhs.m_handle;
            m_threadId = rhs.m_threadId;
            m_ctx = std::move(rhs.m_ctx);
            m_joinable = rhs.m_joinable;

            // 4) 相手を無効化
            rhs.m_handle = nullptr;
            rhs.m_threadId = 0;
            rhs.m_joinable = false;
        }

        return *this;
    }

    Cue::Result WinThread::create(const Cue::Core::Threading::ThreadDesc& desc, Cue::Core::Threading::ThreadProc proc, void* user, WinThread& outThread) noexcept
    {
        // 1) 引数チェック
        if (proc == nullptr)
        {
            return make_fail_invalid("ThreadProc is null.");
        }

        // 2) outThread 初期化
        outThread = WinThread{};

        // 3) StartContext をヒープ確保（例外禁止なので nothrow）
        outThread.m_ctx.reset(new (std::nothrow) StartContext{});
        if (!outThread.m_ctx)
        {
            return make_fail_oom("Failed to allocate StartContext.");
        }

        // 4) コンテキスト設定
        outThread.m_ctx->proc = proc;
        outThread.m_ctx->user = user;
        outThread.m_ctx->stopSource.reset();
        outThread.m_ctx->exitCode.store(0, std::memory_order_relaxed);

        // 5) スレッド生成（_beginthreadex）
        unsigned int tid = 0;
        const uintptr_t h = ::_beginthreadex(
            nullptr,
            static_cast<unsigned int>(desc.stackSizeBytes),
            &WinThread::thread_entry,
            outThread.m_ctx.get(),
            0,
            &tid);

        if (h == 0)
        {
            return make_fail_win32(::GetLastError(), "_beginthreadex failed.");
        }

        outThread.m_handle = reinterpret_cast<void*>(h);
        outThread.m_threadId = static_cast<uint32_t>(tid);
        outThread.m_joinable = true;

        // 6) スレッドオプション適用
        {
            HANDLE hh = reinterpret_cast<HANDLE>(outThread.m_handle);
            set_thread_name_if_possible(hh, desc.name);
            apply_thread_options(hh, desc);
        }

        return make_ok();
    }

    bool WinThread::joinable() const noexcept
    {
        // 1) join可能か
        return m_joinable;
    }

    Cue::Result WinThread::join() noexcept
    {
        // 1) join不可なら成功
        if (!m_joinable)
        {
            return make_ok();
        }

        // 2) 待機
        HANDLE hh = reinterpret_cast<HANDLE>(m_handle);
        const DWORD r = ::WaitForSingleObject(hh, INFINITE);
        if (r != WAIT_OBJECT_0)
        {
            return make_fail_win32(::GetLastError(), "WaitForSingleObject failed.");
        }

        // 3) join済みにする
        m_joinable = false;

        return make_ok();
    }

    void WinThread::request_stop() noexcept
    {
        // 1) ctx が無ければ何もしない
        if (!m_ctx)
        {
            return;
        }

        // 2) 停止要求
        m_ctx->stopSource.request_stop();
    }

    Cue::Core::Threading::StopToken WinThread::stop_token() const noexcept
    {
        // 1) ctx が無ければ空トークン
        if (!m_ctx)
        {
            return Cue::Core::Threading::StopToken{};
        }

        // 2) トークンを返す
        return m_ctx->stopSource.token();
    }

    uint32_t WinThread::thread_id() const noexcept
    {
        // 1) スレッドID
        return m_threadId;
    }

    uint32_t WinThread::exit_code() const noexcept
    {
        // 1) ctx が無ければ0
        if (!m_ctx)
        {
            return 0;
        }

        // 2) 終了コード取得
        return m_ctx->exitCode.load(std::memory_order_relaxed);
    }

    unsigned __stdcall WinThread::thread_entry(void* p) noexcept
    {
        // 1) コンテキスト取得
        StartContext* ctx = static_cast<StartContext*>(p);
        if (ctx == nullptr)
        {
            ::_endthreadex(0);
            return 0;
        }

        // 2) StopToken を作ってユーザー処理を実行
        const Cue::Core::Threading::StopToken token = ctx->stopSource.token();
        const uint32_t code = ctx->proc(token, ctx->user);

        // 3) 終了コード保存
        ctx->exitCode.store(code, std::memory_order_relaxed);

        // 4) 終了
        ::_endthreadex(code);
        return code;
    }

    void WinThread::close_handle_no_wait() noexcept
    {
        // 1) ハンドルがあれば閉じる
        if (m_handle != nullptr)
        {
            ::CloseHandle(reinterpret_cast<HANDLE>(m_handle));
            m_handle = nullptr;
        }
    }

}
