#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === C++ includes ===
#include <vector>

namespace Cue
{
    class GameScene final
    {
    public:
    private:
        std::vector<ECS::Entity> m_entities{};
    };
}
