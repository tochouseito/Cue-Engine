#pragma once

#include <memory>
#include <new>

#include <Result.h>
#include <Threading/IThreadFactory.h>
#include "WinThread.h"

namespace Cue::Platform::Win
{
    class WinThreadFactory final : public Cue::Core::Threading::IThreadFactory
    {
    public:
        Cue::Result create_thread(
            const Cue::Core::Threading::ThreadDesc& desc,
            Cue::Core::Threading::ThreadProc proc,
            void* user,
            std::unique_ptr<Cue::Core::Threading::IThread>& outThread) noexcept override;
    };
}
