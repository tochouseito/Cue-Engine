#include "SceneWorldMapper.h"

// === Engine includes ===
#include "GameWorld.h"
#include "SceneAsset.h"
#include "TransformSystem.h"

// === C++ includes ===
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Cue::GameCore
{
    Result SceneWorldMapper::load_into(
        GameWorld& a_world,
        const SceneAsset& a_scene,
        EntityId& a_outFirstCameraEntity)
    {
        a_outFirstCameraEntity = k_invalidEntityId;
        Result result = a_world.clear();
        if (!result)
        {
            return result;
        }

        const auto clearAfterFailure = [&a_world](const Result& a_failure) -> Result
        {
            const Result clearResult = a_world.clear();
            return clearResult ? a_failure : clearResult;
        };

        const SceneId sceneId = a_world.m_nextSceneId;
        ++a_world.m_nextSceneId;
        if (a_world.m_nextSceneId == k_invalidSceneId)
        {
            a_world.m_nextSceneId = 1;
        }

        std::unordered_map<LocalObjectId, EntityId> entityMap{};
        result = GameWorld::capture_result(
            [&]()
            {
                for (const SceneObject& object : a_scene.objects)
                {
                    if (object.localId != k_invalidLocalObjectId && entityMap.contains(object.localId))
                    {
                        throw std::runtime_error("Scene local object id is duplicated.");
                    }

                    const SceneId owningSceneId = object.isPersistent ? k_invalidSceneId : sceneId;
                    const EntityId entity = a_world.create_entity_record(sceneId, object.localId);
                    a_world.initialize_required_components(
                        entity, object.name, object.tag, owningSceneId, k_invalidEntityId,
                        object.isActive, object.isPersistent);

                    ECS::TransformComponent* transform =
                        a_world.m_ecsManager.get_component<ECS::TransformComponent>(entity);
                    ECS::WorldTransformComponent* worldTransform =
                        a_world.m_ecsManager.get_component<ECS::WorldTransformComponent>(entity);
                    if (transform == nullptr || worldTransform == nullptr)
                    {
                        throw std::runtime_error("Scene object transform components are missing.");
                    }

                    if (object.hasTransform)
                    {
                        transform->position = object.transform.position;
                        transform->rotation = object.transform.rotation;
                        transform->scale = object.transform.scale;
                        worldTransform->position = object.transform.position;
                        worldTransform->rotation = object.transform.rotation;
                        worldTransform->scale = object.transform.scale;
                    }

                    if (object.hasCamera)
                    {
                        ECS::CameraComponent* camera =
                            a_world.m_ecsManager.add_component<ECS::CameraComponent>(entity);
                        if (camera == nullptr)
                        {
                            throw std::runtime_error("Scene object camera component could not be created.");
                        }
                        camera->fovY = object.camera.fovY;
                        camera->aspectRatio = object.camera.aspectRatio;
                        camera->nearZ = object.camera.nearZ;
                        camera->farZ = object.camera.farZ;

                        if (a_outFirstCameraEntity == k_invalidEntityId)
                        {
                            a_outFirstCameraEntity = entity;
                        }
                    }

                    if (object.hasRenderable)
                    {
                        ECS::MeshFilterComponent* meshFilter =
                            a_world.m_ecsManager.add_component<ECS::MeshFilterComponent>(entity);
                        ECS::StaticMeshRendererComponent* renderer =
                            a_world.m_ecsManager.add_component<ECS::StaticMeshRendererComponent>(entity);
                        if (meshFilter == nullptr || renderer == nullptr)
                        {
                            throw std::runtime_error("Scene object renderable components could not be created.");
                        }
                        meshFilter->modelName = object.renderable.modelName;
                        meshFilter->meshId = object.renderable.meshId;
                        renderer->materialId = object.renderable.materialId;
                        renderer->propertyBlock = object.renderable.propertyBlock;
                        renderer->renderQueue = object.renderable.renderQueue;
                        renderer->shadowCasterMode = object.renderable.shadowCasterMode;
                        renderer->visible = object.renderable.visible;
                        renderer->castsShadow = object.renderable.castsShadow;
                        renderer->receivesShadow = object.renderable.receivesShadow;
                    }

                    if (object.hasScript)
                    {
                        ECS::ScriptComponent* script =
                            a_world.m_ecsManager.add_component<ECS::ScriptComponent>(entity);
                        if (script == nullptr)
                        {
                            throw std::runtime_error("Scene object script component could not be created.");
                        }
                        *script = object.script;
                    }

                    if (object.localId != k_invalidLocalObjectId)
                    {
                        entityMap.emplace(object.localId, entity);
                    }
                }
            });
        if (!result)
        {
            return clearAfterFailure(result);
        }

        for (const SceneObject& object : a_scene.objects)
        {
            if (object.parentLocalId == k_invalidLocalObjectId)
            {
                continue;
            }

            const auto childIterator = entityMap.find(object.localId);
            const auto parentIterator = entityMap.find(object.parentLocalId);
            if (childIterator == entityMap.end() || parentIterator == entityMap.end())
            {
                return clearAfterFailure(Result::fail(
                    Code::InvalidArgument, Severity::Error,
                    "Scene parent reference could not be resolved."));
            }

            result = TransformSystem::set_parent(
                a_world, childIterator->second, parentIterator->second, false);
            if (!result)
            {
                return clearAfterFailure(result);
            }
        }

        for (const SceneObject& object : a_scene.objects)
        {
            if (!object.hasScript)
            {
                continue;
            }

            const auto owner = entityMap.find(object.localId);
            if (owner == entityMap.end())
            {
                return clearAfterFailure(Result::fail(
                    Code::InvalidArgument, Severity::Error,
                    "Scene script owner could not be resolved."));
            }

            ECS::ScriptComponent* script =
                a_world.m_ecsManager.get_component<ECS::ScriptComponent>(
                    owner->second);
            if (script == nullptr)
            {
                return clearAfterFailure(Result::fail(
                    Code::InvalidState, Severity::Error,
                    "Scene script component could not be resolved."));
            }

            const auto resolveReference =
                [&a_world, &entityMap](
                    ECS::ScriptEntityReference& a_reference) -> bool
            {
                const LocalObjectId localId =
                    static_cast<LocalObjectId>(a_reference.entityId);
                if (localId == k_invalidLocalObjectId)
                {
                    a_reference = {};
                    return true;
                }

                const auto target = entityMap.find(localId);
                if (target == entityMap.end())
                {
                    return false;
                }

                const EntityRecord* record =
                    a_world.try_get_entity_record(target->second);
                if (record == nullptr || !record->isAlive)
                {
                    return false;
                }

                a_reference.entityId = target->second;
                a_reference.generation = record->generation;
                return true;
            };

            const auto resolveFields =
                [&resolveReference](
                    std::vector<ECS::ScriptFieldValue>& a_fields) -> bool
            {
                for (ECS::ScriptFieldValue& field : a_fields)
                {
                    if (ECS::ScriptEntityReference* entityReference =
                            std::get_if<ECS::ScriptEntityReference>(
                                &field.value))
                    {
                        if (!resolveReference(*entityReference))
                        {
                            return false;
                        }
                    }
                    else if (ECS::ScriptReference* scriptReference =
                                 std::get_if<ECS::ScriptReference>(
                                     &field.value))
                    {
                        if (!resolveReference(scriptReference->entity))
                        {
                            return false;
                        }
                    }
                }
                return true;
            };

            if (!resolveFields(script->serializedFieldValues) ||
                !resolveFields(script->transientFieldValues))
            {
                return clearAfterFailure(Result::fail(
                    Code::InvalidArgument, Severity::Error,
                    "Scene script reference could not be resolved."));
            }
        }

        TransformSystem::sync_world_transforms(a_world);
        return Result::ok();
    }

    Result SceneWorldMapper::make_asset(
        const GameWorld& a_world,
        std::string_view a_name,
        SceneAsset& a_outScene)
    {
        a_outScene = {};
        return GameWorld::capture_result(
            [&]()
            {
                SceneAsset scene{};
                scene.name = a_name;
                scene.objects.reserve(a_world.m_liveObjectCount);

                std::vector<EntityId> entities{};
                entities.reserve(a_world.m_liveObjectCount);
                std::unordered_map<EntityId, LocalObjectId> localIds{};
                localIds.reserve(a_world.m_liveObjectCount);

                ECS::ECSManager& ecs = const_cast<ECS::ECSManager&>(a_world.m_ecsManager);
                for (EntityId entity = 0;
                     entity < static_cast<EntityId>(a_world.m_entityRecords.size()); ++entity)
                {
                    if (!a_world.contains_object(entity))
                    {
                        continue;
                    }

                    const BaseComponent* base = ecs.get_component<BaseComponent>(entity);
                    if (base == nullptr)
                    {
                        throw std::runtime_error("GameWorld scene export requires BaseComponent.");
                    }

                    SceneObject object{};
                    object.localId = static_cast<LocalObjectId>(entities.size() + 1u);
                    if (object.localId == k_invalidLocalObjectId)
                    {
                        throw std::runtime_error("GameWorld scene export exhausted local object ids.");
                    }
                    object.name = base->name;
                    object.tag = base->tag;
                    object.isActive = base->isActiveSelf;
                    object.isPersistent = base->isPersistent;

                    const ECS::TransformComponent* transform =
                        ecs.get_component<ECS::TransformComponent>(entity);
                    if (transform != nullptr)
                    {
                        object.transform.position = transform->position;
                        object.transform.rotation = transform->rotation;
                        object.transform.scale = transform->scale;
                        object.hasTransform = true;
                    }

                    const ECS::CameraComponent* camera = ecs.get_component<ECS::CameraComponent>(entity);
                    if (camera != nullptr)
                    {
                        object.camera.fovY = camera->fovY;
                        object.camera.aspectRatio = camera->aspectRatio;
                        object.camera.nearZ = camera->nearZ;
                        object.camera.farZ = camera->farZ;
                        object.hasCamera = true;
                    }

                    const ECS::MeshFilterComponent* meshFilter =
                        ecs.get_component<ECS::MeshFilterComponent>(entity);
                    const ECS::StaticMeshRendererComponent* renderer =
                        ecs.get_component<ECS::StaticMeshRendererComponent>(entity);
                    if (meshFilter != nullptr || renderer != nullptr)
                    {
                        if (meshFilter != nullptr)
                        {
                            object.renderable.modelName = meshFilter->modelName;
                            object.renderable.meshId = meshFilter->meshId;
                        }
                        if (renderer != nullptr)
                        {
                            object.renderable.materialId = renderer->materialId;
                            object.renderable.propertyBlock = renderer->propertyBlock;
                            object.renderable.renderQueue = renderer->renderQueue;
                            object.renderable.shadowCasterMode = renderer->shadowCasterMode;
                            object.renderable.visible = renderer->visible;
                            object.renderable.castsShadow = renderer->castsShadow;
                            object.renderable.receivesShadow = renderer->receivesShadow;
                        }
                        object.hasRenderable = true;
                    }

                    const ECS::ScriptComponent* script = ecs.get_component<ECS::ScriptComponent>(entity);
                    if (script != nullptr)
                    {
                        object.script = *script;
                        object.hasScript = true;
                    }

                    localIds.emplace(entity, object.localId);
                    entities.push_back(entity);
                    scene.objects.push_back(std::move(object));
                }

                const auto localizeReference =
                    [&a_world, &localIds](
                        ECS::ScriptEntityReference& a_reference)
                {
                    const EntityRecord* record =
                        a_world.try_get_entity_record(a_reference.entityId);
                    const auto localId = localIds.find(a_reference.entityId);
                    if (record == nullptr || !record->isAlive ||
                        record->generation != a_reference.generation ||
                        localId == localIds.end())
                    {
                        a_reference = {};
                        return;
                    }

                    a_reference.entityId =
                        static_cast<EntityId>(localId->second);
                    a_reference.generation = 0u;
                };
                const auto localizeFields =
                    [&localizeReference](
                        std::vector<ECS::ScriptFieldValue>& a_fields)
                {
                    for (ECS::ScriptFieldValue& field : a_fields)
                    {
                        if (ECS::ScriptEntityReference* entityReference =
                                std::get_if<ECS::ScriptEntityReference>(
                                    &field.value))
                        {
                            localizeReference(*entityReference);
                        }
                        else if (ECS::ScriptReference* scriptReference =
                                     std::get_if<ECS::ScriptReference>(
                                         &field.value))
                        {
                            localizeReference(scriptReference->entity);
                        }
                    }
                };

                for (size_t objectIndex = 0; objectIndex < entities.size(); ++objectIndex)
                {
                    SceneObject& object = scene.objects[objectIndex];
                    if (object.hasScript)
                    {
                        localizeFields(object.script.serializedFieldValues);
                        localizeFields(object.script.transientFieldValues);
                    }

                    const BaseComponent* base =
                        ecs.get_component<BaseComponent>(entities[objectIndex]);
                    if (base == nullptr || base->parent == k_invalidEntityId)
                    {
                        continue;
                    }

                    const auto parent = localIds.find(base->parent);
                    if (parent != localIds.end())
                    {
                        object.parentLocalId = parent->second;
                    }
                }

                a_outScene = std::move(scene);
            });
    }
} // namespace Cue::GameCore
