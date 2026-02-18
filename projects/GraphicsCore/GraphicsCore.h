#pragma once
#include <Result.h>

namespace Cue::GraphicsCore
{
    class Backend
    {
    public:
        Backend() = default;
        virtual ~Backend() = default;
        virtual Result initialize() = 0;
        virtual Result shutdown() = 0;
    };
} // namespace Cue::GraphicsCore
