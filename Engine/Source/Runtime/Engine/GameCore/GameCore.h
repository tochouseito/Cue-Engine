#pragma once

// === Base includes ===
#include <Result.h>

// === ECS includes ===
#include <ECSManager.h>

// === C++ includes ===
#include <memory>

namespace Cue
{
    class GameCore final
    {
    public:
        GameCore() = default;
        ~GameCore() = default;

        Result initialize();

        Result add_object();
        Result remove_object(uint32_t objectId);
    private:
        std::unique_ptr<ECS::ECSManager> m_ecsManager = nullptr;
    };
}
