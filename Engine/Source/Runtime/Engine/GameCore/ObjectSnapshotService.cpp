#include "ObjectSnapshotService.h"

// === Engine includes ===
#include "DrawSystem/RenderCameraSelection.h"
#include "GameWorld.h"
#include "ObjectSnapshot.h"
#include "TransformSystem.h"

// === C++ includes ===
#include <stdexcept>

namespace Cue::GameCore
{
    Result ObjectSnapshotService::capture(
        const GameWorld& a_world,
        const DrawSystem::RenderCameraSelection& a_cameraSelection,
        EntityId a_entityId,
        ObjectSnapshot& a_outSnapshot)
    {
        a_outSnapshot = {};

        const EntityRecord* record = a_world.try_get_entity_record(a_entityId);
        const BaseComponent* base =
            const_cast<GameWorld&>(a_world).m_ecsManager.get_component<BaseComponent>(a_entityId);
        const ECS::TransformComponent* transform =
            const_cast<GameWorld&>(a_world).m_ecsManager.get_component<ECS::TransformComponent>(a_entityId);
        if (record == nullptr || !record->isAlive || record->isPendingDestroy ||
            base == nullptr || transform == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                                "GameWorld object cannot be captured for undo.");
        }

        a_outSnapshot.entityId = a_entityId;
        a_outSnapshot.name = base->name;
        a_outSnapshot.tag = base->tag;
        a_outSnapshot.parentId = base->parent;
        a_outSnapshot.owningSceneId = base->owningSceneId;
        a_outSnapshot.sourceSceneId = record->sourceSceneId;
        a_outSnapshot.sourceLocalObjectId = record->sourceLocalObjectId;
        a_outSnapshot.isActive = base->isActiveSelf;
        a_outSnapshot.isPersistent = base->isPersistent;
        a_outSnapshot.isRenderCamera = a_cameraSelection.camera_entity() == a_entityId;
        a_outSnapshot.transform = *transform;

        const ECS::CameraComponent* camera =
            const_cast<GameWorld&>(a_world).m_ecsManager.get_component<ECS::CameraComponent>(a_entityId);
        if (camera != nullptr)
        {
            a_outSnapshot.camera = *camera;
            a_outSnapshot.hasCamera = true;
        }

        const ECS::MeshFilterComponent* meshFilter =
            const_cast<GameWorld&>(a_world).m_ecsManager.get_component<ECS::MeshFilterComponent>(a_entityId);
        if (meshFilter != nullptr)
        {
            a_outSnapshot.meshFilter = *meshFilter;
            a_outSnapshot.hasMeshFilter = true;
        }

        const ECS::StaticMeshRendererComponent* staticMeshRenderer =
            const_cast<GameWorld&>(a_world).m_ecsManager.get_component<ECS::StaticMeshRendererComponent>(a_entityId);
        if (staticMeshRenderer != nullptr)
        {
            a_outSnapshot.staticMeshRenderer = *staticMeshRenderer;
            a_outSnapshot.hasStaticMeshRenderer = true;
        }

        const ECS::ScriptComponent* script =
            const_cast<GameWorld&>(a_world).m_ecsManager.get_component<ECS::ScriptComponent>(a_entityId);
        if (script != nullptr)
        {
            a_outSnapshot.script = *script;
            a_outSnapshot.hasScript = true;
        }

        return Result::ok();
    }

    Result ObjectSnapshotService::restore(
        GameWorld& a_world,
        DrawSystem::RenderCameraSelection& a_cameraSelection,
        const ObjectSnapshot& a_snapshot,
        EntityId& a_outEntityId)
    {
        a_outEntityId = k_invalidEntityId;
        if (a_snapshot.entityId == k_invalidEntityId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                                "GameWorld undo snapshot does not have an entity ID.");
        }
        if (a_world.contains_object(a_snapshot.entityId))
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "GameWorld undo snapshot entity is already alive.");
        }

        const Result result = GameWorld::capture_result(
            [&]()
            {
                const EntityId entity = a_world.create_entity_record(
                    a_snapshot.sourceSceneId,
                    a_snapshot.sourceLocalObjectId);
                if (entity != a_snapshot.entityId)
                {
                    a_world.destroy_object_immediately(entity);
                    throw std::runtime_error("GameWorld undo could not restore the original entity ID.");
                }

                a_world.initialize_required_components(
                    entity,
                    a_snapshot.name,
                    a_snapshot.tag,
                    a_snapshot.owningSceneId,
                    a_snapshot.parentId,
                    a_snapshot.isActive,
                    a_snapshot.isPersistent);

                ECS::TransformComponent* transform =
                    a_world.m_ecsManager.get_component<ECS::TransformComponent>(entity);
                if (transform == nullptr)
                {
                    throw std::runtime_error("GameWorld undo transform component is missing.");
                }
                *transform = a_snapshot.transform;

                if (a_snapshot.hasCamera)
                {
                    ECS::CameraComponent* camera = a_world.m_ecsManager.add_component<ECS::CameraComponent>(entity);
                    if (camera == nullptr)
                    {
                        throw std::runtime_error("GameWorld undo camera component could not be restored.");
                    }
                    *camera = a_snapshot.camera;
                }
                if (a_snapshot.hasMeshFilter)
                {
                    ECS::MeshFilterComponent* meshFilter =
                        a_world.m_ecsManager.add_component<ECS::MeshFilterComponent>(entity);
                    if (meshFilter == nullptr)
                    {
                        throw std::runtime_error("GameWorld undo mesh filter component could not be restored.");
                    }
                    *meshFilter = a_snapshot.meshFilter;
                }
                if (a_snapshot.hasStaticMeshRenderer)
                {
                    ECS::StaticMeshRendererComponent* staticMeshRenderer =
                        a_world.m_ecsManager.add_component<ECS::StaticMeshRendererComponent>(entity);
                    if (staticMeshRenderer == nullptr)
                    {
                        throw std::runtime_error("GameWorld undo static mesh renderer component could not be restored.");
                    }
                    *staticMeshRenderer = a_snapshot.staticMeshRenderer;
                }
                if (a_snapshot.hasScript)
                {
                    ECS::ScriptComponent* script = a_world.m_ecsManager.add_component<ECS::ScriptComponent>(entity);
                    if (script == nullptr)
                    {
                        throw std::runtime_error("GameWorld undo script component could not be restored.");
                    }
                    *script = a_snapshot.script;
                }
                if (a_snapshot.isRenderCamera)
                {
                    a_cameraSelection.set_camera_entity(entity);
                }

                a_outEntityId = entity;
            });
        if (!result)
        {
            return result;
        }

        TransformSystem::sync_world_transforms(a_world);
        return Result::ok();
    }
} // namespace Cue::GameCore
