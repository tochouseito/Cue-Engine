#include "win_pch.h"
#include "WinThreadFactory.h"

namespace Cue::Platform::Win
{
    Cue::Result WinThreadFactory::create_thread(const Cue::Core::Threading::ThreadDesc& desc, Cue::Core::Threading::ThreadProc proc, void* user, std::unique_ptr<Cue::Core::Threading::IThread>& outThread) noexcept
    {
        // 1) WinThread を確保（例外禁止なので nothrow）
        auto th = std::unique_ptr<WinThread>(new (std::nothrow) WinThread{});
        if (!th)
        {
            return Cue::Result::fail(
                Cue::Facility::Platform,
                Cue::Code::OutOfMemory,
                Cue::Severity::Error,
                0,
                "Failed to allocate WinThread.");
        }

        // 2) 実スレッド生成
        const auto r = WinThread::create(desc, proc, user, *th);
        if (!r)
        {
            return r;
        }

        // 3) baseへ移譲
        outThread = std::move(th);
        return Cue::Result::ok();
    }
}
