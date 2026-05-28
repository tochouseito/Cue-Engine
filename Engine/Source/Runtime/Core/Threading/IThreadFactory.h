#pragma once

/// *********************************************************************************
/// スレッドファクトリインターフェース
/// *********************************************************************************

// === C++ includes ===
#include <memory>

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include "IThread.h"

namespace Cue::Core::Threading
{
    /// @brief スレッド生成を抽象化するファクトリ
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

        /// @brief 新しいスレッドを生成する
        virtual Result create_thread(
            const ThreadDesc& a_desc,
            threadProc a_proc,
            void* a_user,
            std::unique_ptr<IThread>& a_outThread) noexcept = 0;
    };
}
