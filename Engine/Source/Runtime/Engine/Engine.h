#pragma once

// === PAL include ===
#include <PAL.h>

// === RHI include ===
#include <RHI.h>

namespace Cue
{
    struct engine_setup_info final
    {
        PAL::IPlatform* platform = nullptr;
        RHI::IBackend* backend = nullptr;
    };

    class Engine final
    {
    public:
        Engine()
        {

        }
        ~Engine()
        {

        }
    };
}
