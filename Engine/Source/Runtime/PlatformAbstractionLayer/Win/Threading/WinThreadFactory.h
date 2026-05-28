#pragma once

/// *********************************************************************************
/// Windows スレッドファクトリ
/// *********************************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <Threading/IThreadFactory.h>

// === C++ includes ===
#include <memory>
#include <new>

// === Windows API includes ===
#include "WinThread.h"

namespace Cue::PAL::Win
{
    /// @brief Windows スレッド生成ファクトリ
    class WinThreadFactory final : public Core::Threading::IThreadFactory
    {
    public:
        WinThreadFactory() noexcept = default;
        ~WinThreadFactory() override = default;

        // --- スレッド生成 ---
        Result create_thread(
            const Core::Threading::ThreadDesc& a_desc,
            Core::Threading::threadProc a_proc,
            void* a_user,
            std::unique_ptr<Core::Threading::IThread>& a_outThread) noexcept override;
    };
}
