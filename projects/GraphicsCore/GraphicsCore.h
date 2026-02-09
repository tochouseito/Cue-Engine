#pragma once
#include <Result.h>

namespace Cue::GraphicsCore
{
    class Backend
    {
    public:
        Backend() = default;
        virtual ~Backend() = default;
        virtual Core::Result initialize() = 0;
        virtual Core::Result shutdown() = 0;
    };
} // namespace Cue::GraphicsCore
