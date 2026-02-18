#pragma once
#include <Result.h>
#include <memory>
#include <Threading/IThreadFactory.h>
#include <Time/IClock.h>
#include <Time/IWaiter.h>

namespace Cue::Platform
{
    class IPlatform
    {
    public:
        IPlatform() = default;
        virtual ~IPlatform() = default;
        virtual Result setup() = 0;
        virtual Result start() = 0;
        virtual void begin_frame() = 0;
        virtual void end_frame() = 0;
        virtual bool poll_message() = 0;
        virtual Result shutdown() = 0;

        virtual Core::Threading::IThreadFactory& get_thread_factory() = 0;
        virtual Core::Time::IClock& get_clock() = 0;
        virtual Core::Time::IWaiter& get_waiter() = 0;  
    };
} // namespace Cue
