#pragma once
#include <Result.h>

namespace Cue
{
    class IPlatform
    {
    public:
        IPlatform() = default;
        virtual ~IPlatform() = default;
        virtual Core::Result setup() = 0;
        virtual Core::Result start() = 0;
        virtual bool poll_message() = 0;
        virtual Core::Result shutdown() = 0;
    };
} // namespace Cue
