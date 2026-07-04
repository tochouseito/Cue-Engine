#pragma once

/// **********************************************************************
/// Engine API Command
/// **********************************************************************

// === Engine include ===
#include "Commands.h"
#include <GameCore/GameWorld.h>

// === C++ includes ===
#include <string_view>

namespace Cue
{
    class EngineCommandContext final : public IGameCommandContext
    {
    public:
        explicit EngineCommandContext(GameCore::GameWorld& a_gameWorld) noexcept;

        Result destroy_object(GameCore::EntityId a_objectId) override;
        Result get_object_name(GameCore::EntityId a_objectId, std::string& a_outName) override;
        Result rename_object(GameCore::EntityId a_objectId, std::string_view a_name) override;
        Result get_parent(GameCore::EntityId a_objectId, GameCore::EntityId& a_outParentId) override;
        Result set_parent(
            GameCore::EntityId a_objectId,
            GameCore::EntityId a_parentId,
            bool a_keepsWorldTransform) override;

    private:
        GameCore::GameWorld& m_gameWorld;
    };
}
