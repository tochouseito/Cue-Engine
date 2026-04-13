#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Container/BatchedRegistry.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include "GameScene.h"

namespace Cue
{
    class GameWorld final
    {
        using EntityRegistry = Core::BatchedRegistry<ECS::Entity>;
    public:
        // Scene の追加
    private:
        EntityRegistry m_entityRegistry{};
        std::unordered_map<std::string, EntityRegistry::InsertGroupResult> m_namedEntityGroups{};
    };
}
