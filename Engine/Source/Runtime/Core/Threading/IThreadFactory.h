#pragma once

// === C++ includes ===
#include <memory>

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include "IThread.h"

namespace Cue::Core::Threading
{
    /// @brief スレッド生成を抽象化するファクトリです。
    class IThreadFactory
    {
    public:
        IThreadFactory() = default;
        virtual ~IThreadFactory() = default;
        // コピー禁止
        IThreadFactory(const IThreadFactory&) = delete;
        IThreadFactory& operator=(const IThreadFactory&) = delete;
        // ムーブ禁止
        IThreadFactory(IThreadFactory&&) = delete;
        IThreadFactory& operator=(IThreadFactory&&) = delete;

        /// @brief 新しいスレッドを生成します。
        virtual Result create_thread(
            const ThreadDesc& a_desc,
            threadProc a_proc,
            void* a_user,
            std::unique_ptr<IThread>& a_outThread) noexcept = 0;
    };
}
