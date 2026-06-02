#include "WinThread.h"
#include "ConvertHresult.h"
#include "ConvertUTF.h"

namespace Cue::PAL::Win
{
    namespace
    {
        static void set_thread_name_if_possible(HANDLE a_threadHandle, std::string_view a_name) noexcept
        {
            // - 空なら何もしない
            if (a_name.empty())
            {
                return;
            }

            // - 動的解決（SDK/Windows差でビルドが死ぬのを避ける）
            using Fn = HRESULT(WINAPI*)(HANDLE, PCWSTR);

            HMODULE kernelModule = ::GetModuleHandleW(L"kernel32.dll");
            if (kernelModule == nullptr)
            {
                return;
            }

            auto fn = reinterpret_cast<Fn>(::GetProcAddress(kernelModule, "SetThreadDescription"));
            if (fn == nullptr)
            {
                return;
            }

            // - UTF-8 -> UTF-16
            std::wstring w;
            const Result result = utf8_to_wide(a_name, &w);
            if (!result)
            {
                return;
            }
            if (w.empty())
            {
                return;
            }

            // - 失敗しても無視
            (void)fn(a_threadHandle, w.c_str());
        }

        static void apply_thread_options(HANDLE a_threadHandle, const Cue::Core::Threading::ThreadDesc& a_desc) noexcept
        {
            // - 優先度（0なら触らない）
            if (a_desc.priority != 0)
            {
                ::SetThreadPriority(a_threadHandle, a_desc.priority);
            }

            // - アフィニティ（0なら触らない）
            if (a_desc.affinityMask != 0)
            {
                ::SetThreadAffinityMask(a_threadHandle, static_cast<DWORD_PTR>(a_desc.affinityMask));
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
    WinThread::WinThread(WinThread&& a_other) noexcept
    {
        // ムーブ
        *this = std::move(a_other);
    }

    WinThread& WinThread::operator=(WinThread&& a_other) noexcept
    {
        // 自己代入防止
        if (this != &a_other)
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
            m_handle = a_other.m_handle;
            m_threadId = a_other.m_threadId;
            m_ctx = std::move(a_other.m_ctx);
            m_joinable = a_other.m_joinable;

            // 相手を無効化
            a_other.m_handle = nullptr;
            a_other.m_threadId = 0;
            a_other.m_joinable = false;
        }

        return *this;
    }

    Result WinThread::create(
        const Core::Threading::ThreadDesc& a_desc,
        Core::Threading::threadProc a_proc,
        void* a_user,
        WinThread& a_outThread) noexcept
    {
        // - 引数チェック
        if (a_proc == nullptr)
        {
            return Result::fail(
                Code::InvalidArgument, Severity::Error,
                "Thread procedure must not be null.");
        }

        // - outThread 初期化
        a_outThread = WinThread{};

        // - StartContext を nothrow でヒープ確保する
        a_outThread.m_ctx.reset(new (std::nothrow) StartContext{});
        if (!a_outThread.m_ctx)
        {
            return Result::fail(
                Code::OutOfMemory, Severity::Error,
                "Failed to allocate memory for thread start context.");
        }

        // - コンテキスト設定
        a_outThread.m_ctx->proc = a_proc;
        a_outThread.m_ctx->user = a_user;
        a_outThread.m_ctx->stopSource.reset();
        a_outThread.m_ctx->exitCode.store(0, std::memory_order_relaxed);

        // - スレッド生成（_beginthreadex）
        unsigned int tid = 0;
        const uintptr_t threadHandle = ::_beginthreadex(
            nullptr,
            static_cast<unsigned int>(a_desc.stackSizeBytes),
            &WinThread::thread_entry,
            a_outThread.m_ctx.get(),
            0,
            &tid);

        if (threadHandle == 0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "Failed to create thread.");
        }

        a_outThread.m_handle = reinterpret_cast<void*>(threadHandle);
        a_outThread.m_threadId = static_cast<uint32_t>(tid);
        a_outThread.m_joinable = true;

        // - スレッドオプション適用
        {
            HANDLE threadHandleValue = reinterpret_cast<HANDLE>(a_outThread.m_handle);
            set_thread_name_if_possible(threadHandleValue, a_desc.name);
            apply_thread_options(threadHandleValue, a_desc);
        }

        return Result::ok();
    }

    bool WinThread::joinable() const noexcept
    {
        // - join可能か
        return m_joinable;
    }

    Result WinThread::join() noexcept
    {
        // - join不可なら成功
        if (!m_joinable)
        {
            return Result::ok();
        }

        // - 待機
        HANDLE threadHandle = reinterpret_cast<HANDLE>(m_handle);
        const DWORD waitResult = ::WaitForSingleObject(threadHandle, INFINITE);
        if (waitResult != WAIT_OBJECT_0)
        {
            return Result::fail(
                convert_hresult_code(HRESULT_FROM_WIN32(::GetLastError())), Severity::Error,
                "WaitForSingleObject failed.");
        }

        // - join済みにする
        m_joinable = false;

        return Result::ok();
    }

    void WinThread::request_stop() noexcept
    {
        // - ctx が無ければ何もしない
        if (!m_ctx)
        {
            return;
        }

        // - 停止要求
        m_ctx->stopSource.request_stop();
    }

    Core::Threading::StopToken WinThread::stop_token() const noexcept
    {
        // - ctx が無ければ空トークン
        if (!m_ctx)
        {
            return Cue::Core::Threading::StopToken{};
        }

        // - トークンを返す
        return m_ctx->stopSource.token();
    }

    uint32_t WinThread::thread_id() const noexcept
    {
        // - スレッドID
        return m_threadId;
    }

    uint32_t WinThread::exit_code() const noexcept
    {
        // - ctx が無ければ0
        if (!m_ctx)
        {
            return 0;
        }

        // - 終了コード取得
        return m_ctx->exitCode.load(std::memory_order_relaxed);
    }

    unsigned __stdcall WinThread::thread_entry(void* a_context) noexcept
    {
        // - コンテキスト取得
        StartContext* context = static_cast<StartContext*>(a_context);
        if (context == nullptr)
        {
            ::_endthreadex(0);
            return 0;
        }

        // - StopToken を作ってユーザー処理を実行
        const Cue::Core::Threading::StopToken token = context->stopSource.token();
        const uint32_t code = context->proc(token, context->user);

        // - 終了コード保存
        context->exitCode.store(code, std::memory_order_relaxed);

        // - 終了
        ::_endthreadex(code);
        return code;
    }

    void WinThread::close_handle_no_wait() noexcept
    {
        // - ハンドルがあれば閉じる
        if (m_handle != nullptr)
        {
            ::CloseHandle(reinterpret_cast<HANDLE>(m_handle));
            m_handle = nullptr;
        }
    }
}
