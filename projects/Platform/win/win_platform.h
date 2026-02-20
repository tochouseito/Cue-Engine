#pragma once
#include <Platform.h>
#include <memory>
#include "PlatformFactory.h"

namespace Cue::Platform::Win
{
    class WinPlatform : public IPlatform
    {
    public:
        WinPlatform();
        ~WinPlatform() override;

        Result setup() override;
        Result start() override;
        void begin_frame() override {}
        void end_frame() override {}
        bool poll_message() override;
        Result shutdown() override;

        Core::Threading::IThreadFactory& get_thread_factory() override;
        Core::Time::IClock& get_clock() override;
        Core::Time::IWaiter& get_waiter() override;
        Core::IO::IFileSystem& get_file_system() override;
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
