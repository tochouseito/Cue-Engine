#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Threading/IThreadFactory.h>

// === C++ includes ===
#include <memory>
#include <new>

// === Windows API include ===
#include "stdafx.h"
#include "WinThread.h"

namespace Cue::PAL::Win
{
    class WinThreadFactory final : public Core::Threading::IThreadFactory
    {
    public:
        WinThreadFactory() noexcept = default;
        ~WinThreadFactory() override = default;

        // --- スレッド生成 ---
        Result create_thread(
            const Core::Threading::ThreadDesc& desc,
            Core::Threading::ThreadProc proc,
            void* user,
            std::unique_ptr<Core::Threading::IThread>& outThread) noexcept override;
    };
}
