#pragma once

/// **********************************************************************
/// Command 定義
/// **********************************************************************

// === Base includes ===
#include <CueResult.h>

// === Core includes ===
#include <CQRS/CQRS.h>

// === GameCore includes ===

// === C++ includes ===
#include <cstdint>
#include <string>
#include <string_view>

namespace Cue
{
    class IGameCommandContext : public virtual Core::CQRS::ICommandContext
    {
    public:
        ~IGameCommandContext() override = default;


    };
}
