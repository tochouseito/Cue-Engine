#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Container/BatchedRegistry.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===

namespace Cue
{
    class GameWorld final
    {
        using EntityRegistry = Core::BatchedRegistry<ECS::Entity>;
    public:
        // Scene の追加
    private:
        std::unique_ptr<ECS::ECSManager> m_ecsManager = nullptr;
        EntityRegistry m_entityRegistry{};
        std::unordered_map<std::string, EntityRegistry::InsertGroupResult> m_namedEntityGroups{};
    };
}
