#pragma once

/// **********************************************************************
/// Engine API Command
/// **********************************************************************

// === Engine include ===
#include "Commands.h"

// === C++ includes ===
#include <string_view>

namespace Cue
{
    class EngineCommandContext final : public IGameCommandContext
    {
    public:
        explicit EngineCommandContext() noexcept;

    private:

    };
}
