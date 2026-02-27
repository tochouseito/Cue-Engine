#pragma once
#include <Result.h>
#include "FrameGraph.h"

namespace Cue::GraphicsCore
{
    struct backend_setup_info final
    {
        uint32_t bufferCount = 2;
    };

    class Backend
    {
    public:
        Backend() = default;
        virtual ~Backend() = default;
        virtual Result initialize(const backend_setup_info& info) = 0;
        virtual Result shutdown() = 0;
    private:
    };
} // namespace Cue::GraphicsCore
