#include "WinThreadFactory.h"

namespace Cue::PAL::Win
{
    Result WinThreadFactory::create_thread(
        const Core::Threading::ThreadDesc& a_desc,
        Core::Threading::threadProc a_proc,
        void* a_user,
        std::unique_ptr<Core::Threading::IThread>& a_outThread) noexcept
    {
        // WinThread を nothrow で確保する
        auto thread = std::unique_ptr<WinThread>(new (std::nothrow) WinThread{});
        if (!thread)
        {
            return Result::fail(
                Code::OutOfMemory, Severity::Error,
                "Failed to allocate memory for WinThread.");
        }

        // 実スレッド生成
        const auto result = WinThread::create(a_desc, a_proc, a_user, *thread);
        if (!result)
        {
            return result;
        }

        // baseへ移譲
        a_outThread = std::move(thread);
        return Result::ok();
    }
}
