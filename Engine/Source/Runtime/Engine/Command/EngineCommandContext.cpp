#include "EngineCommandContext.h"

namespace Cue
{
    EngineCommandContext::EngineCommandContext(GameCore::GameWorld& a_gameWorld) noexcept
        : m_gameWorld(a_gameWorld)
    {
    }

    Result EngineCommandContext::destroy_object(GameCore::EntityId a_objectId)
    {
        Result result = m_gameWorld.destroy_object(a_objectId);
        if (result)
        {
            m_gameWorld.execute_deferred_deletions();
        }

        return result;
    }

    Result EngineCommandContext::get_object_name(GameCore::EntityId a_objectId, std::string& a_outName)
    {
        return m_gameWorld.get_object_name(a_objectId, a_outName);
    }

    Result EngineCommandContext::rename_object(GameCore::EntityId a_objectId, std::string_view a_name)
    {
        return m_gameWorld.set_object_name(a_objectId, a_name);
    }

    Result EngineCommandContext::get_parent(GameCore::EntityId a_objectId, GameCore::EntityId& a_outParentId)
    {
        return m_gameWorld.get_parent(a_objectId, a_outParentId);
    }

    Result EngineCommandContext::set_parent(
        GameCore::EntityId a_objectId,
        GameCore::EntityId a_parentId,
        bool a_keepsWorldTransform)
    {
        if (a_parentId == GameCore::k_invalidEntityId)
        {
            return m_gameWorld.detach_parent(a_objectId, a_keepsWorldTransform);
        }

        return m_gameWorld.set_parent(a_objectId, a_parentId, a_keepsWorldTransform);
    }
} // namespace Cue
