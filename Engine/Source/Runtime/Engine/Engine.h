#pragma once

// === PAL includes ===
#include <PAL.h>

// === RHI includes ===
#include <RHI.h>

namespace Cue
{
    /// @brief Engine 初期化時に必要な依存オブジェクトです。
    struct EngineSetupInfo final
    {
        PAL::IPlatform* platform = nullptr;
        RHI::IBackend* backend = nullptr;
    };

    /// @brief Runtime 全体の統合窓口です。
    class Engine final
    {
    public:
        Engine() = default;
        ~Engine() = default;
    };
}
