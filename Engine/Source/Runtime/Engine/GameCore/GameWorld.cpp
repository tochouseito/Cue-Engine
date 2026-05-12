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
            meshFilter.modelName = "Cube";
            meshFilter.meshId = a_meshId;
            objectDefinition.prototype.add_component(meshFilter);

            ECS::StaticMeshRendererComponent renderer{};
            renderer.materialHandle = a_defaultMaterialHandle;
            renderer.visible = true;
            objectDefinition.prototype.add_component(renderer);

            return objectDefinition;
        }

        [[nodiscard]] ObjectDefinition make_default_camera_object_definition(
            const Math::float3& a_position)
        {
            ObjectDefinition objectDefinition("Camera");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::float3::zero();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::CameraComponent camera{};
            objectDefinition.prototype.add_component(camera);

            return objectDefinition;
        }

        [[nodiscard]] ObjectDefinition make_default_sprite_object_definition(
            const Math::float3& a_position,
            MaterialHandle a_defaultMaterialHandle)
        {
            ObjectDefinition objectDefinition("SpriteObject");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::float3::zero();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::SpriteRendererComponent renderer{};
            renderer.materialHandle = a_defaultMaterialHandle;
            renderer.isVisible = true;
            objectDefinition.prototype.add_component(renderer);

            return objectDefinition;
        }
    }

    [[nodiscard]] Result GameWorld::initialize(RHI::IBufferManager* a_bufferManager,
        RHI::IViewManager* a_viewManager,
        DrawSystem::IStaticMeshPool* a_staticMeshPool,
        AssetManager* a_assetManager,
        Core::IO::IFileSystem* a_fileSystem,
        Audio::IBackend* a_audioBackend,
        Audio::AudioDeviceHandle a_audioDevice,
        Physics::IPhysicsSystem* a_physicsSystem,
        PAL::InputManager* a_inputManager,
        uint32_t a_bufferCount,
        uint32_t a_renderWidth,
        uint32_t a_renderHeight,
        uint32_t a_defaultStaticMeshId,
        MaterialHandle a_defaultMaterialHandle)
    {
        if (a_bufferManager == nullptr || a_viewManager == nullptr ||
            a_staticMeshPool == nullptr || a_assetManager == nullptr ||
            a_fileSystem == nullptr || a_audioBackend == nullptr)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "GameWorld requires valid buffer, view, mesh, asset, file, and audio managers.");
        }
        if (a_defaultStaticMeshId == ECS::k_invalidMeshId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "GameWorld default static mesh id is invalid.");
        }
        if (a_bufferCount == 0)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "GameWorld buffer count must be greater than 0.");
        }

        m_defaultStaticMeshId = a_defaultStaticMeshId;
        m_assetManager = a_assetManager;
        m_fileSystem = a_fileSystem;
        m_audioBackend = a_audioBackend;
        m_physicsSystem = a_physicsSystem;
        m_inputManager = a_inputManager;
        m_audioDevice = a_audioDevice;
        m_defaultMaterialHandle = a_defaultMaterialHandle;
        m_drawFrameState.resize(a_bufferCount);
        m_drawScene.resize(a_bufferCount);
        for (uint32_t bufferIndex = 0; bufferIndex < a_bufferCount; ++bufferIndex)
        {
            sync_draw_frame_state(bufferIndex, a_renderWidth, a_renderHeight);
        }

        m_drawResources =
            std::make_unique<DrawSystem::DrawResources>(
                a_bufferManager, a_viewManager, a_bufferCount);

        Result result = m_drawResources->create_renderable_info_buffer(
            k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_drawResources->create_transform_buffer(k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_drawResources->create_view_projection_buffer();
        if (!result)
        {
            return result;
        }

        result = m_drawResources->create_material_buffer(k_maxMaterialCount);
        if (!result)
        {
            return result;
        }

        result = m_drawResources->create_render_object_buffer(
            k_maxRenderObjectCount);
        if (!result)
        {
            return result;
        }

        result = m_drawResources->create_object_count_buffer();
        if (!result)
        {
            return result;
        }

        result = m_drawResources->create_sprite_instance_buffer(k_maxSpriteCount);
        if (!result)
        {
            return result;
        }

        auto& renderableObjectSystem = m_ecs.add_system<ECS::RenderableObjectSystem>(
            m_assetManager,
            a_staticMeshPool,
            m_defaultMaterialHandle,
            m_drawFrameState,
            m_drawScene);
        auto& spriteSystem = m_ecs.add_system<ECS::SpriteSystem>(
            m_assetManager,
            m_defaultMaterialHandle,
            m_drawFrameState,
            m_drawScene);
        auto& cameraSystem = m_ecs.add_system<ECS::CameraSystem>(
            m_drawFrameState, m_drawScene);
        auto& firstPersonCameraControllerSystem =
            m_ecs.add_system<ECS::FirstPersonCameraControllerSystem>(
                m_inputManager);
        auto& playerControlSystem =
            m_ecs.add_system<ECS::PlayerControlSystem>(m_inputManager);
        auto& audioSystem = m_ecs.add_system<ECS::AudioSystem>(
            m_fileSystem, m_audioBackend, m_audioDevice, m_assetRootPath);
        auto& characterControllerSystem =
            m_ecs.add_system<ECS::CharacterControllerSystem>(a_physicsSystem);
        auto& physicsBodySystem = m_ecs.add_system<ECS::PhysicsBodySystem>(
            a_physicsSystem, m_assetManager);
        result = m_navigationWorld.set_backend(
            std::make_unique<RecastNavigationBackend>());
        if (!result)
        {
            return result;
        }
        auto& navigationSystem = m_ecs.add_system<ECS::NavigationSystem>(
            &m_navigationWorld);
        m_navigationSystem = &navigationSystem;
        auto& navAgentMotorSystem = m_ecs.add_system<ECS::NavAgentMotorSystem>();
        auto& triggerVolumeSystem = m_ecs.add_system<ECS::TriggerVolumeSystem>();
        auto& demoEnemySystem =
            m_ecs.add_system<ECS::DemoEnemySystem>(&m_debugDraw);

        m_editorPipeline.add_system(&renderableObjectSystem);
        m_editorPipeline.add_system(&spriteSystem);
        m_editorPipeline.add_system(&cameraSystem);
        m_editorPipeline.add_system(&audioSystem);
        m_simulationPipeline.add_system(&firstPersonCameraControllerSystem);
        m_simulationPipeline.add_system(&playerControlSystem);
        m_simulationPipeline.add_system(&demoEnemySystem);
        m_simulationPipeline.add_system(&navigationSystem);
        m_simulationPipeline.add_system(&navAgentMotorSystem);
        m_simulationPipeline.add_system(&characterControllerSystem);
        m_simulationPipeline.add_system(&physicsBodySystem);
        m_simulationPipeline.add_system(&triggerVolumeSystem);
        m_simulationPipeline.add_system(&audioSystem);
        m_editorPipeline.awake(m_ecs);
        m_editorPipeline.initialize(m_ecs);
        m_simulationPipeline.awake(m_ecs);
        m_simulationPipeline.initialize(m_ecs);

        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::simulate(float a_deltaTime)
    {
        m_debugDraw.update(a_deltaTime);
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

        Result sceneLoadResult = execute_deferred_scene_loads();
        if (!sceneLoadResult)
        {
            return sceneLoadResult;
        }

        sync_draw_frame_state(a_bufferIndex, a_renderWidth, a_renderHeight);
        m_drawScene.begin_frame(a_bufferIndex);

        ECS::UpdateContext updateContext{};
        updateContext.bufferIndex = a_bufferIndex;
        m_editorPipeline.update(m_ecs, updateContext);
        return upload_draw_scene(a_bufferIndex);
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

        if (m_drawResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Clone target GameWorld is not initialized.");
        }

        if (a_source.m_drawResources == nullptr)
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

        if (a_source.m_hasActiveNavMeshAsset)
        {
            NavMeshHandle navMeshHandle{};
            result = load_navigation_mesh(
                a_source.m_activeNavMeshAsset, navMeshHandle);
            if (!result)
            {
                return result;
            }
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

    [[nodiscard]] Result GameWorld::add_camera_object(
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};

        Result result = create_object("Camera", a_outObject);
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
                "Failed to add transform component for camera object.") : result;
        }

        ECS::CameraComponent* camera = nullptr;
        result =
            add_component<ECS::CameraComponent>(a_outObject.entity_id(), camera);
        if (!result || camera == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add camera component for camera object.") : result;
        }

        transform->position = a_position;
        transform->rotation = Math::float3::zero();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        *camera = ECS::CameraComponent{};
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_sprite_object(
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};

        Result result = create_object("SpriteObject", a_outObject);
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
                "Failed to add transform component for sprite object.") : result;
        }

        ECS::SpriteRendererComponent* renderer = nullptr;
        result = add_component<ECS::SpriteRendererComponent>(
            a_outObject.entity_id(), renderer);
        if (!result || renderer == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add sprite renderer component for object.")
                : result;
        }

        transform->position = a_position;
        transform->rotation = Math::float3::zero();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        renderer->materialHandle = m_defaultMaterialHandle;
        renderer->isVisible = true;
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

    [[nodiscard]] Result GameWorld::add_camera_object_to_scene(SceneId a_sceneId,
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        if (a_sceneId == k_invalidSceneId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene id is invalid.");
        }

        const ObjectDefinition objectDefinition =
            make_default_camera_object_definition(a_position);
        return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
    }

    [[nodiscard]] Result GameWorld::add_sprite_object_to_scene(SceneId a_sceneId,
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        if (a_sceneId == k_invalidSceneId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene id is invalid.");
        }

        const ObjectDefinition objectDefinition =
            make_default_sprite_object_definition(
                a_position, m_defaultMaterialHandle);
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

    [[nodiscard]] Result GameWorld::upload_draw_scene(uint32_t a_bufferIndex)
    {
        if (m_drawResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Draw resources are not initialized.");
        }

        if (a_bufferIndex >= m_drawFrameState.frameStates.size())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Draw frame index is out of range.");
        }

        auto resolve_uploader_index = [a_bufferIndex](uint32_t a_count) -> uint32_t
        {
            if (a_count <= 1)
            {
                return 0;
            }

            return a_bufferIndex;
        };

        DrawSystem::DrawSceneFrame& sceneFrame = m_drawScene.frame(a_bufferIndex);
        DrawSystem::DrawFrameData& frameState =
            m_drawFrameState.frame_state(a_bufferIndex);
        if (sceneFrame.staticMeshVisibilityItems.size() !=
                sceneFrame.staticMeshSurfaceItems.size() ||
            sceneFrame.staticMeshVisibilityItems.size() !=
                sceneFrame.staticMeshBatchItems.size())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Static mesh draw scene item counts are inconsistent.");
        }

        frameState.objectCount =
            static_cast<uint32_t>(sceneFrame.staticMeshVisibilityItems.size());
        frameState.spriteCount = 0;
        frameState.cpuIndexedDraws.clear();

        if (frameState.useCpuBatching)
        {
            frameState.cpuIndexedDraws.reserve(
                sceneFrame.staticMeshBatchItems.size());
            for (const DrawSystem::StaticMeshBatchItem& item :
                sceneFrame.staticMeshBatchItems)
            {
                if (item.hasCpuIndexedDraw)
                {
                    frameState.cpuIndexedDraws.push_back(item.cpuIndexedDraw);
                }
            }
        }

        auto& renderableInfoUploaders =
            m_drawResources->renderable_info_uploaders();
        if (!renderableInfoUploaders.empty())
        {
            const uint32_t uploaderIndex =
                resolve_uploader_index(
                    static_cast<uint32_t>(renderableInfoUploaders.size()));
            if (uploaderIndex < renderableInfoUploaders.size())
            {
                auto& uploader = renderableInfoUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0;
                    index < sceneFrame.staticMeshVisibilityItems.size();
                    ++index)
                {
                    if (!uploader.push(
                        index,
                        sceneFrame.staticMeshVisibilityItems[index].renderableInfo))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue renderable info upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit renderable info upload.");
                }
            }
        }

        auto& transformUploaders = m_drawResources->transform_uploaders();
        if (!transformUploaders.empty())
        {
            const uint32_t uploaderIndex =
                resolve_uploader_index(
                    static_cast<uint32_t>(transformUploaders.size()));
            if (uploaderIndex < transformUploaders.size())
            {
                auto& uploader = transformUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0;
                    index < sceneFrame.staticMeshSurfaceItems.size();
                    ++index)
                {
                    if (!uploader.push(
                        index,
                        sceneFrame.staticMeshSurfaceItems[index].transform))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue transform upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit transform upload.");
                }
            }
        }

        auto& materialUploaders = m_drawResources->material_uploaders();
        if (!materialUploaders.empty())
        {
            const uint32_t uploaderIndex =
                resolve_uploader_index(
                    static_cast<uint32_t>(materialUploaders.size()));
            if (uploaderIndex < materialUploaders.size())
            {
                auto& uploader = materialUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0;
                    index < sceneFrame.staticMeshSurfaceItems.size();
                    ++index)
                {
                    if (!sceneFrame.staticMeshSurfaceItems[index].hasMaterial)
                    {
                        continue;
                    }

                    if (!uploader.push(
                        index,
                        sceneFrame.staticMeshSurfaceItems[index].material))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue material upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit material upload.");
                }
            }
        }

        auto& renderObjectUploaders = m_drawResources->render_object_uploaders();
        if (!renderObjectUploaders.empty())
        {
            const uint32_t uploaderIndex =
                resolve_uploader_index(
                    static_cast<uint32_t>(renderObjectUploaders.size()));
            if (uploaderIndex < renderObjectUploaders.size())
            {
                auto& uploader = renderObjectUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0;
                    index < sceneFrame.staticMeshVisibilityItems.size();
                    ++index)
                {
                    if (!uploader.push(
                        index,
                        sceneFrame.staticMeshVisibilityItems[index].renderObject))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue render object upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit render object upload.");
                }
            }
        }

        if (frameState.useCpuBatching)
        {
            auto& visibleObjectCountUploaders =
                m_drawResources->visible_object_count_uploaders();
            if (!visibleObjectCountUploaders.empty())
            {
                const uint32_t uploaderIndex =
                    resolve_uploader_index(
                        static_cast<uint32_t>(visibleObjectCountUploaders.size()));
                if (uploaderIndex < visibleObjectCountUploaders.size())
                {
                    auto& uploader = visibleObjectCountUploaders[uploaderIndex];
                    uploader.begin_frame();
                    if (!uploader.push(0, frameState.objectCount))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue visible object count upload.");
                    }
                    if (!uploader.commit())
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to commit visible object count upload.");
                    }
                }
            }
        }

        std::stable_sort(
            sceneFrame.spriteItems.begin(),
            sceneFrame.spriteItems.end(),
            [](const DrawSystem::SpriteDrawItem& a_left,
                const DrawSystem::SpriteDrawItem& a_right)
            {
                if (a_left.layer != a_right.layer)
                {
                    return a_left.layer < a_right.layer;
                }
                if (a_left.order != a_right.order)
                {
                    return a_left.order < a_right.order;
                }
                return a_left.entity < a_right.entity;
            });

        auto& spriteInstanceUploaders = m_drawResources->sprite_instance_uploaders();
        if (!spriteInstanceUploaders.empty())
        {
            const uint32_t uploaderIndex =
                resolve_uploader_index(
                    static_cast<uint32_t>(spriteInstanceUploaders.size()));
            if (uploaderIndex < spriteInstanceUploaders.size())
            {
                auto& uploader = spriteInstanceUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0; index < sceneFrame.spriteItems.size();
                    ++index)
                {
                    if (!uploader.push(index, sceneFrame.spriteItems[index].instance))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue sprite instance upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit sprite instance upload.");
                }
            }
        }
        frameState.spriteCount =
            static_cast<uint32_t>(sceneFrame.spriteItems.size());

        auto& viewProjectionUploaders =
            m_drawResources->view_projection_uploaders();
        if (!viewProjectionUploaders.empty())
        {
            const uint32_t uploaderIndex =
                resolve_uploader_index(
                    static_cast<uint32_t>(viewProjectionUploaders.size()));
            if (uploaderIndex < viewProjectionUploaders.size())
            {
                const DrawSystem::CameraDrawItem* selectedCamera = nullptr;
                for (const DrawSystem::CameraDrawItem& item :
                    sceneFrame.cameraItems)
                {
                    if (item.isMain)
                    {
                        selectedCamera = &item;
                        break;
                    }
                    if (selectedCamera == nullptr)
                    {
                        selectedCamera = &item;
                    }
                }

                auto& uploader = viewProjectionUploaders[uploaderIndex];
                uploader.begin_frame();
                if (selectedCamera != nullptr &&
                    !uploader.push(0, selectedCamera->viewProjection))
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to queue view projection upload.");
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit view projection upload.");
                }
            }
        }

        return Result::ok();
    }
}
