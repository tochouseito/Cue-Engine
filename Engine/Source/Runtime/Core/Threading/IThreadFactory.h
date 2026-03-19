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
        // コピー禁止
        IThreadFactory(const IThreadFactory&) = delete;
        IThreadFactory& operator=(const IThreadFactory&) = delete;
        // ムーブ禁止
        IThreadFactory(IThreadFactory&&) = delete;
        IThreadFactory& operator=(IThreadFactory&&) = delete;

        virtual Result create_thread(
            const ThreadDesc& desc,
            ThreadProc proc,
            void* user,
            std::unique_ptr<IThread>& outThread) noexcept = 0;
    };
}
