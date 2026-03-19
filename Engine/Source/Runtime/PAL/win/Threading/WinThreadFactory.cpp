#include "WinThreadFactory.h"

namespace Cue::PAL::Win
{
    Result WinThreadFactory::create_thread(const Core::Threading::ThreadDesc& desc, Core::Threading::ThreadProc proc, void* user, std::unique_ptr<Core::Threading::IThread>& outThread) noexcept
    {
        // WinThread を nothrow で確保する
        auto th = std::unique_ptr<WinThread>(new (std::nothrow) WinThread{});
        if (!th)
        {
            return Result::fail(
                Code::OutOfMemory, Severity::Error,
                "Failed to allocate memory for WinThread.");
        }

        // 実スレッド生成
        const auto r = WinThread::create(desc, proc, user, *th);
        if (!r)
        {
            return r;
        }

        // baseへ移譲
        outThread = std::move(th);
        return Result::ok();
    }
}
