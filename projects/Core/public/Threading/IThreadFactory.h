#pragma once
#include <memory>
#include <Result.h>
#include "IThread.h"

namespace Cue::Core::Threading
{
    class IThreadFactory
    {
    public:
        virtual ~IThreadFactory() = default;

        virtual Result create_thread(
            const ThreadDesc& desc,
            ThreadProc proc,
            void* user,
            std::unique_ptr<IThread>& outThread) noexcept = 0;
    };
}
