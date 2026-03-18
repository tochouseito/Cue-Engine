#pragma once

// === Base includes ===
#include <Result.h>

// === PAL includes ===
#include "PlatformFactory.h"

namespace Cue::PAL
{
    struct platform_setup_info final
    {

    };

    enum class PlatformMessage : uint8_t
    {
        None = 0,
        Quit,
    };

    class IPlatform
    {
    public:
        virtual ~IPlatform() = default;
        virtual Result initialize(const platform_setup_info& info) = 0;
        virtual Result start() = 0;
        virtual Result shutdown() = 0;
        virtual Result begin_frame() = 0;
        virtual Result end_frame() = 0;
        virtual PlatformMessage poll_message() = 0;
    };
}
