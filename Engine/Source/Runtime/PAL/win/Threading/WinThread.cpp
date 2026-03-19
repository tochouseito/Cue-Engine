#include "WinThread.h"
#include "ConvertHresult.h"
#include "ConvertUTF.h"

namespace Cue::PAL::Win
{
    namespace
    {
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
            std::wstring w;
            const Result r = utf8_to_wide(name, &w);
            if (!r)
            {
                return;
            }
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

    WinThread::~WinThread()
    {
        // 停止要求を出してから join する
        if (m_joinable)
        {
            request_stop();
            join();
        }

        // ハンドルを閉じる
        close_handle_no_wait();

        // コンテキスト破棄
        m_ctx.reset();
    }
    WinThread::WinThread(WinThread&& other) noexcept
    {
        // ムーブ
        *this = std::move(other);
    }
    WinThread& WinThread::operator=(WinThread && other) noexcept
    {
        // 自己代入防止
        if (this != &other)
        {
            // 自分の後始末
            if (m_joinable)
            {
                request_stop();
                join();
            }
            close_handle_no_wait();
            m_ctx.reset();

            // 所有権を移す（StartContextはポインタだけ移す）
            m_handle = other.m_handle;
            m_threadId = other.m_threadId;
            m_ctx = std::move(other.m_ctx);
            m_joinable = other.m_joinable;

            // 相手を無効化
            other.m_handle = nullptr;
            other.m_threadId = 0;
            other.m_joinable = false;
        }

        return *this;
    }
    Result WinThread::create(const Core::Threading::ThreadDesc& desc, Core::Threading::ThreadProc proc, void* user, WinThread& out_thread) noexcept
    {
        // 1) 引数チェック
        if (proc == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Thread procedure must not be null.");
        }

        // 2) outThread 初期化
        out_thread = WinThread{};

        // 3) StartContext を nothrow でヒープ確保する
        out_thread.m_ctx.reset(new (std::nothrow) StartContext{});
        if (!out_thread.m_ctx)
        {
            return Result::fail(
                Code::OutOfMemory, Severity::Error,
                "Failed to allocate memory for thread start context.");
        }

        // 4) コンテキスト設定
        out_thread.m_ctx->proc = proc;
        out_thread.m_ctx->user = user;
        out_thread.m_ctx->stopSource.reset();
        out_thread.m_ctx->exitCode.store(0, std::memory_order_relaxed);

        // 5) スレッド生成（_beginthreadex）
        unsigned int tid = 0;
        const uintptr_t h = ::_beginthreadex(
            nullptr,
            static_cast<unsigned int>(desc.stackSizeBytes),
            &WinThread::thread_entry,
            out_thread.m_ctx.get(),
            0,
            &tid);

        if (h == 0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to create thread.");
        }

        out_thread.m_handle = reinterpret_cast<void*>(h);
        out_thread.m_threadId = static_cast<uint32_t>(tid);
        out_thread.m_joinable = true;

        // 6) スレッドオプション適用
        {
            HANDLE hh = reinterpret_cast<HANDLE>(out_thread.m_handle);
            set_thread_name_if_possible(hh, desc.name);
            apply_thread_options(hh, desc);
        }

        return Result::ok();
    }
    bool WinThread::joinable() const noexcept
    {
        // 1) join可能か
        return m_joinable;
    }
    Result WinThread::join() noexcept
    {
        // 1) join不可なら成功
        if (!m_joinable)
        {
            return Result::ok();
        }

        // 2) 待機
        HANDLE hh = reinterpret_cast<HANDLE>(m_handle);
        const DWORD r = ::WaitForSingleObject(hh, INFINITE);
        if (r != WAIT_OBJECT_0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "WaitForSingleObject failed.");
        }

        // 3) join済みにする
        m_joinable = false;

        return Result::ok();
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
    Core::Threading::StopToken WinThread::stop_token() const noexcept
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
