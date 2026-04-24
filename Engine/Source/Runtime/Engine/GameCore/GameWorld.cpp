#include "GameWorld.h"

namespace Cue::GameCore
{
    namespace
    {
        [[nodiscard]] ObjectDefinition make_default_static_mesh_object_definition(
            const Math::float3& a_position, uint32_t a_meshId,
            MaterialHandle a_defaultMaterialHandle)
        {
            ObjectDefinition objectDefinition("StaticMeshObject");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::float3::zero();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::MeshFilterComponent meshFilter{};
            meshFilter.meshId = a_meshId;
            objectDefinition.prototype.add_component(meshFilter);

            ECS::StaticMeshRendererComponent renderer{};
            renderer.materialHandle = a_defaultMaterialHandle;
            renderer.visible = true;
            objectDefinition.prototype.add_component(renderer);

            return objectDefinition;
        }
    }

    [[nodiscard]] Result GameWorld::initialize(RHI::IBufferManager* a_bufferManager,
        RHI::IViewManager* a_viewManager,
        RHI::IStaticMeshPool* a_staticMeshPool,
        AssetManager* a_assetManager,
        uint32_t a_bufferCount,
        uint32_t a_renderWidth,
        uint32_t a_renderHeight,
        uint32_t a_defaultStaticMeshId,
        MaterialHandle a_defaultMaterialHandle)
    {
        if (a_bufferManager == nullptr || a_viewManager == nullptr ||
            a_staticMeshPool == nullptr || a_assetManager == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "GameWorld requires valid buffer, view, mesh, and asset managers.");
        }
        if (a_defaultStaticMeshId == ECS::k_invalidMeshId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "GameWorld default static mesh id is invalid.");
        }

        m_defaultStaticMeshId = a_defaultStaticMeshId;
        m_assetManager = a_assetManager;
        m_defaultMaterialHandle = a_defaultMaterialHandle;
        m_renderSceneState.resize(a_bufferCount);
        for (uint32_t bufferIndex = 0; bufferIndex < a_bufferCount; ++bufferIndex)
        {
            sync_render_scene_state(bufferIndex, a_renderWidth, a_renderHeight);
        }

        m_worldResources =
            std::make_unique<WorldResources>(a_bufferManager, a_viewManager);

        Result result = m_worldResources->create_renderable_info_buffer(
            k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_transform_buffer(k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_view_projection_buffer();
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_material_buffer(k_maxMaterialCount);
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_render_object_buffer(
            k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_worldResources->create_object_count_buffer();
        if (!result)
        {
            return result;
        }

        auto& renderableObjectSystem = m_ecs.add_system<ECS::RenderableObjectSystem>(
            m_worldResources->renderable_info_uploaders(),
            m_worldResources->transform_uploaders(),
            m_worldResources->material_uploaders(),
            m_worldResources->render_object_uploaders(),
            m_worldResources->visible_object_count_uploaders(),
            m_assetManager,
            a_staticMeshPool,
            m_defaultMaterialHandle,
            m_renderSceneState);
        auto& cameraSystem = m_ecs.add_system<ECS::CameraSystem>(
            m_worldResources->view_projection_uploaders(), m_renderSceneState);

        m_editorPipeline.add_system(&renderableObjectSystem);
        m_editorPipeline.add_system(&cameraSystem);
        m_editorPipeline.awake(m_ecs);
        m_editorPipeline.initialize(m_ecs);

        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::simulate(float a_deltaTime)
    {
        ECS::UpdateContext updateContext{};
        updateContext.deltaTime = a_deltaTime;
        m_simulationPipeline.update(m_ecs, updateContext);
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::finalize_systems() noexcept
    {
        m_simulationPipeline.finalize(m_ecs);
        m_editorPipeline.finalize(m_ecs);
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::editor_update(
        uint32_t a_bufferIndex,
        uint32_t a_renderWidth,
        uint32_t a_renderHeight)
    {
        execute_deferred_deletions_internal();

        sync_render_scene_state(a_bufferIndex, a_renderWidth, a_renderHeight);

        ECS::UpdateContext updateContext{};
        updateContext.bufferIndex = a_bufferIndex;
        m_editorPipeline.update(m_ecs, updateContext);
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::update(float a_deltaTime, uint32_t a_bufferIndex,
        uint32_t a_renderWidth, uint32_t a_renderHeight)
    {
        Result result = simulate(a_deltaTime);
        if (!result)
        {
            return result;
        }

        return editor_update(a_bufferIndex, a_renderWidth, a_renderHeight);
    }

    [[nodiscard]] Result GameWorld::clone_from(const GameWorld& a_source)
    {
        if (this == &a_source)
        {
            return Result::ok();
        }

        if (m_worldResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Clone target GameWorld is not initialized.");
        }

        if (a_source.m_worldResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Clone source GameWorld is not initialized.");
        }

        Result result = clear();
        if (!result)
        {
            return result;
        }

        m_mainCameraIndex = a_source.m_mainCameraIndex;
        m_defaultStaticMeshId = a_source.m_defaultStaticMeshId;
        m_nextSceneId = a_source.m_nextSceneId;
        m_isCpuBatchingEnabled = a_source.m_isCpuBatchingEnabled;

        std::vector<SceneId> sceneIds{};
        sceneIds.reserve(a_source.m_scenes.size());
        for (const auto& [sceneId, _] : a_source.m_scenes)
        {
            sceneIds.push_back(sceneId);
        }
        std::sort(sceneIds.begin(), sceneIds.end());

        for (const SceneId sceneId : sceneIds)
        {
            const auto sourceSceneIt = a_source.m_scenes.find(sceneId);
            if (sourceSceneIt == a_source.m_scenes.end())
            {
                continue;
            }

            const SceneInstance& sourceScene = sourceSceneIt->second;

            SceneInstance targetScene{};
            targetScene.sceneId = sourceScene.sceneId;
            targetScene.asset = sourceScene.asset;
            targetScene.isLoaded = sourceScene.isLoaded;
            targetScene.isActive = sourceScene.isActive;
            targetScene.isPendingUnload = false;
            targetScene.nextLocalObjectId = sourceScene.nextLocalObjectId;

            m_scenes.emplace(sceneId, std::move(targetScene));

            std::vector<ObjectDefinition> objectDefinitions{};
            objectDefinitions.reserve(sourceScene.entities.size());
            for (const EntityId entityId : sourceScene.entities)
            {
                if (!a_source.contains_object(entityId))
                {
                    continue;
                }

                const BaseComponent* base =
                    a_source.get_component<BaseComponent>(entityId);
                const EntityRecord* record =
                    a_source.try_get_entity_record(entityId);
                if (base == nullptr || record == nullptr || !record->isAlive)
                {
                    continue;
                }

                ObjectDefinition definition{};
                definition.localObjectId = record->sourceLocalObjectId;
                definition.isActive = base->isActiveSelf;
                definition.isPersistent = base->isPersistent;
                definition.prototype =
                    a_source.build_object_prototype(entityId, *base);

                if (base->parent != k_invalidEntityId &&
                    a_source.source_scene_id(base->parent) == sceneId)
                {
                    const EntityRecord* parentRecord =
                        a_source.try_get_entity_record(base->parent);
                    if (parentRecord != nullptr &&
                        parentRecord->isAlive &&
                        parentRecord->sourceLocalObjectId != k_invalidLocalObjectId)
                    {
                        definition.parentLocalObjectId =
                            parentRecord->sourceLocalObjectId;
                    }
                }

                objectDefinitions.push_back(std::move(definition));
            }

            try
            {
                (void)instantiate_into_scene(
                    sceneId,
                    std::span<const ObjectDefinition>(objectDefinitions),
                    sourceScene.asset);
            }
            catch (const std::exception& exception)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    exception.what());
            }
        }

        struct PendingLooseParent final
        {
            EntityId sourceEntityId = k_invalidEntityId;
            EntityId targetEntityId = k_invalidEntityId;
            EntityId sourceParentId = k_invalidEntityId;
        };

        std::unordered_map<EntityId, EntityId> looseEntityMap{};
        std::vector<PendingLooseParent> pendingLooseParents{};
        looseEntityMap.reserve(a_source.m_entityRecords.size());
        pendingLooseParents.reserve(a_source.m_entityRecords.size());

        for (EntityId entityId = 0;
             entityId < static_cast<EntityId>(a_source.m_entityRecords.size());
             ++entityId)
        {
            if (!a_source.contains_object(entityId) ||
                a_source.source_scene_id(entityId) != k_invalidSceneId)
            {
                continue;
            }

            const BaseComponent* base =
                a_source.get_component<BaseComponent>(entityId);
            if (base == nullptr)
            {
                continue;
            }

            ObjectDefinition definition{};
            definition.isActive = base->isActiveSelf;
            definition.isPersistent = base->isPersistent;
            definition.prototype =
                a_source.build_object_prototype(entityId, *base);

            const GameObject clonedObject = instantiate_object(definition);
            if (!clonedObject.is_valid())
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    "GameWorld loose object clone failed.");
            }

            looseEntityMap.emplace(entityId, clonedObject.entity_id());
            if (base->parent != k_invalidEntityId &&
                a_source.source_scene_id(base->parent) == k_invalidSceneId)
            {
                pendingLooseParents.push_back(PendingLooseParent{
                    entityId,
                    clonedObject.entity_id(),
                    base->parent
                });
            }
        }

        for (const PendingLooseParent& pendingParent : pendingLooseParents)
        {
            const auto parentIt = looseEntityMap.find(pendingParent.sourceParentId);
            if (parentIt == looseEntityMap.end())
            {
                continue;
            }

            BaseComponent* base = get_component<BaseComponent>(
                pendingParent.targetEntityId);
            if (base == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "GameWorld cloned BaseComponent is missing.");
            }

            base->parent = parentIt->second;
        }

        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_object(
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        if (m_defaultStaticMeshId == ECS::k_invalidMeshId)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "GameWorld default static mesh id is invalid.");
        }

        Result result = create_object("StaticMeshObject", a_outObject);
        if (!result)
        {
            return result;
        }

        ECS::TransformComponent* transform = nullptr;
        result =
            add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
        if (!result || transform == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add transform component for object.") : result;
        }

        ECS::MeshFilterComponent* meshFilter = nullptr;
        result =
            add_component<ECS::MeshFilterComponent>(a_outObject.entity_id(), meshFilter);
        if (!result || meshFilter == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add mesh filter component for object.") : result;
        }

        ECS::StaticMeshRendererComponent* renderer = nullptr;
        result = add_component<ECS::StaticMeshRendererComponent>(
            a_outObject.entity_id(), renderer);
        if (!result || renderer == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add static mesh renderer component for object.")
                : result;
        }

        transform->position = a_position;
        transform->rotation = Math::float3::zero();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        meshFilter->meshId = m_defaultStaticMeshId;
        renderer->materialHandle = m_defaultMaterialHandle;
        renderer->visible = true;
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_object_to_scene(SceneId a_sceneId,
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        if (a_sceneId == k_invalidSceneId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene id is invalid.");
        }
        if (m_defaultStaticMeshId == ECS::k_invalidMeshId)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "GameWorld default static mesh id is invalid.");
        }

        const ObjectDefinition objectDefinition =
            make_default_static_mesh_object_definition(
                a_position, m_defaultStaticMeshId, m_defaultMaterialHandle);
        return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
    }

    [[nodiscard]] Result GameWorld::remove_object(uint32_t a_objectId) noexcept
    {
        EntityId entityId = k_invalidEntityId;
        if (!try_get_static_mesh_entity(a_objectId, entityId))
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Static mesh object id was not found.");
        }

        return destroy_object(entityId);
    }
}
