#include "GameWorld.h"

namespace Cue::GameCore
{
    namespace
    {
        [[nodiscard]] DrawSystem::CpuShadowCaster make_cpu_shadow_caster(
            const GpuData::ObjectTransformGpu& a_transform) noexcept
        {
            const Math::float4x4& world = a_transform.worldMatrix;
            const Math::float3 basisX(
                world.values[0][0],
                world.values[0][1],
                world.values[0][2]);
            const Math::float3 basisY(
                world.values[1][0],
                world.values[1][1],
                world.values[1][2]);
            const Math::float3 basisZ(
                world.values[2][0],
                world.values[2][1],
                world.values[2][2]);
            const float maxExtent = (std::max)(
                basisX.length(),
                (std::max)(basisY.length(), basisZ.length()));

            DrawSystem::CpuShadowCaster caster{};
            caster.center = Math::float3(
                world.values[3][0],
                world.values[3][1],
                world.values[3][2]);
            caster.radius = maxExtent * 1.7320508f;
            return caster;
        }

        [[nodiscard]] Math::float3 transform_position(
            const GpuData::ObjectTransformGpu& a_transform) noexcept
        {
            const Math::float4x4& world = a_transform.worldMatrix;
            return Math::float3(
                world.values[3][0],
                world.values[3][1],
                world.values[3][2]);
        }

        [[nodiscard]] Math::float3 camera_position(
            const GpuData::ViewProjectionGpu& a_viewProjection) noexcept
        {
            const Math::float4x4 world =
                Math::float4x4::inverse(a_viewProjection.view);
            return Math::float3(
                world.values[3][0],
                world.values[3][1],
                world.values[3][2]);
        }

        [[nodiscard]] float distance_squared(
            const Math::float3& a_left,
            const Math::float3& a_right) noexcept
        {
            const Math::float3 diff = a_left - a_right;
            return diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
        }

        [[nodiscard]] ObjectDefinition make_default_static_mesh_object_definition(
            const Math::float3& a_position,
            uint32_t a_meshId)
        {
            ObjectDefinition objectDefinition("StaticMeshObject");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::Quaternion::identity();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::MeshFilterComponent meshFilter{};
            meshFilter.modelName = "Cube";
            meshFilter.meshId = a_meshId;
            objectDefinition.prototype.add_component(meshFilter);

            ECS::StaticMeshRendererComponent renderer{};
            renderer.visible = true;
            renderer.castsShadow = true;
            renderer.receivesShadow = true;
            objectDefinition.prototype.add_component(renderer);

            return objectDefinition;
        }

        [[nodiscard]] ObjectDefinition make_default_game_object_definition(
            const Math::float3& a_position)
        {
            ObjectDefinition objectDefinition("GameObject");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::Quaternion::identity();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            return objectDefinition;
        }

        [[nodiscard]] ObjectDefinition make_default_camera_object_definition(
            const Math::float3& a_position)
        {
            ObjectDefinition objectDefinition("Camera");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::Quaternion::identity();
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
            transform.rotation = Math::Quaternion::identity();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::SpriteRendererComponent renderer{};
            renderer.materialHandle = a_defaultMaterialHandle;
            renderer.isVisible = true;
            objectDefinition.prototype.add_component(renderer);

            return objectDefinition;
        }

        [[nodiscard]] ObjectDefinition make_default_directional_light_definition(
            const Math::float3& a_position)
        {
            ObjectDefinition objectDefinition("DirectionalLight");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::quaternion_from_euler_xyz(
                Math::float3(0.6f, -0.5f, 0.0f));
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::DirectionalLightComponent light{};
            objectDefinition.prototype.add_component(light);

            return objectDefinition;
        }

        [[nodiscard]] ObjectDefinition make_default_point_light_definition(
            const Math::float3& a_position)
        {
            ObjectDefinition objectDefinition("PointLight");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::Quaternion::identity();
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::PointLightComponent light{};
            objectDefinition.prototype.add_component(light);

            return objectDefinition;
        }

        [[nodiscard]] ObjectDefinition make_default_spot_light_definition(
            const Math::float3& a_position)
        {
            ObjectDefinition objectDefinition("SpotLight");

            ECS::TransformComponent transform{};
            transform.position = a_position;
            transform.rotation = Math::quaternion_from_euler_xyz(
                Math::float3(0.35f, 0.0f, 0.0f));
            transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
            objectDefinition.prototype.add_component(transform);

            ECS::SpotLightComponent light{};
            objectDefinition.prototype.add_component(light);

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
        m_particleFrameState.resize(a_bufferCount);
        m_particleScene.resize(a_bufferCount);
        m_lightFrameState.resize(a_bufferCount);
        m_lightScene.resize(a_bufferCount);
        m_shadowFrameState.resize(a_bufferCount);
        m_shadowScene.resize(a_bufferCount);
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

        result = m_drawResources->create_skin_palette_buffer(
            k_maxSkinPaletteCount);
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

        m_particleResources =
            std::make_unique<ParticleSystem::ParticleResources>(
                a_bufferManager, a_viewManager, a_bufferCount);

        result = m_particleResources->create_frame_buffer();
        if (!result)
        {
            return result;
        }

        result = m_particleResources->create_emitter_buffer(
            k_maxParticleEmitterCount);
        if (!result)
        {
            return result;
        }

        result = m_particleResources->create_particle_buffer(k_maxParticleCount);
        if (!result)
        {
            return result;
        }

        m_lightResources =
            std::make_unique<LightingSystem::LightResources>(
                a_bufferManager, a_viewManager, a_bufferCount);

        result = m_lightResources->create_frame_buffer();
        if (!result)
        {
            return result;
        }

        result = m_lightResources->create_directional_light_buffer(
            GpuData::k_maxDirectionalLightCount);
        if (!result)
        {
            return result;
        }

        result = m_lightResources->create_point_light_buffer(
            GpuData::k_maxPointLightCount);
        if (!result)
        {
            return result;
        }

        result = m_lightResources->create_spot_light_buffer(
            GpuData::k_maxSpotLightCount);
        if (!result)
        {
            return result;
        }

        m_shadowResources =
            std::make_unique<ShadowSystem::ShadowResources>(
                a_bufferManager, a_viewManager, a_bufferCount);

        result = m_shadowResources->create_directional_shadow_frame_buffer();
        if (!result)
        {
            return result;
        }

        result = m_shadowResources->create_point_shadow_face_buffer();
        if (!result)
        {
            return result;
        }

        result = m_shadowResources->create_spot_shadow_frame_buffer();
        if (!result)
        {
            return result;
        }

        result = m_fontAtlasManager.initialize(m_assetManager);
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
        auto& skinnedRenderableObjectSystem =
            m_ecs.add_system<ECS::SkinnedRenderableObjectSystem>(
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
        auto& textSystem = m_ecs.add_system<ECS::TextSystem>(
            m_fontAtlasManager,
            m_drawFrameState,
            m_drawScene);
        auto& particleEmitterSystem =
            m_ecs.add_system<ECS::ParticleEmitterSystem>(
                m_assetManager,
                m_defaultMaterialHandle,
                m_particleScene);
        auto& uiLayoutSystem =
            m_ecs.add_system<ECS::UiLayoutSystem>(m_drawFrameState);
        auto& uiWidgetSystem = m_ecs.add_system<ECS::UiWidgetSystem>(
            m_inputManager,
            m_assetManager,
            m_defaultMaterialHandle,
            m_drawFrameState,
            m_drawScene);
        auto& cameraSystem = m_ecs.add_system<ECS::CameraSystem>(
            m_drawFrameState, m_drawScene);
        auto& lightSystem = m_ecs.add_system<ECS::LightSystem>(m_lightScene);
        auto& shadowSystem = m_ecs.add_system<ECS::ShadowSystem>(m_shadowScene);
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
        auto& animationSystem =
            m_ecs.add_system<ECS::AnimationSystem>(m_assetManager);

        m_editorPipeline.add_system(&animationSystem);
        m_editorPipeline.add_system(&renderableObjectSystem);
        m_editorPipeline.add_system(&skinnedRenderableObjectSystem);
        m_editorPipeline.add_system(&uiLayoutSystem);
        m_editorPipeline.add_system(&uiWidgetSystem);
        m_editorPipeline.add_system(&textSystem);
        m_editorPipeline.add_system(&spriteSystem);
        m_editorPipeline.add_system(&particleEmitterSystem);
        m_editorPipeline.add_system(&cameraSystem);
        m_editorPipeline.add_system(&lightSystem);
        m_editorPipeline.add_system(&shadowSystem);
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
        sync_world_transforms();
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
        uint32_t a_renderHeight,
        float a_deltaTime)
    {
        execute_deferred_deletions_internal();

        Result sceneLoadResult = execute_deferred_scene_loads();
        if (!sceneLoadResult)
        {
            return sceneLoadResult;
        }

        sync_world_transforms();
        sync_draw_frame_state(a_bufferIndex, a_renderWidth, a_renderHeight);
        if (a_bufferIndex < m_particleFrameState.frameStates.size())
        {
            ParticleSystem::ParticleFrameData& particleFrame =
                m_particleFrameState.frame_state(a_bufferIndex);
            particleFrame.frame.deltaTime = a_deltaTime;
            particleFrame.frame.time += a_deltaTime;
        }
        m_drawScene.begin_frame(a_bufferIndex);
        m_particleScene.begin_frame(a_bufferIndex);
        m_lightScene.begin_frame(a_bufferIndex);
        m_shadowScene.begin_frame(a_bufferIndex);

        ECS::UpdateContext updateContext{};
        updateContext.bufferIndex = a_bufferIndex;
        updateContext.deltaTime = a_deltaTime;
        m_editorPipeline.update(m_ecs, updateContext);
        Result result = upload_draw_scene(a_bufferIndex);
        if (!result)
        {
            return result;
        }

        result = upload_particle_scene(a_bufferIndex);
        if (!result)
        {
            return result;
        }

        result = upload_light_scene(a_bufferIndex);
        if (!result)
        {
            return result;
        }

        return upload_shadow_scene(a_bufferIndex);
    }

    [[nodiscard]] Result GameWorld::update(float a_deltaTime, uint32_t a_bufferIndex,
        uint32_t a_renderWidth, uint32_t a_renderHeight)
    {
        Result result = simulate(a_deltaTime);
        if (!result)
        {
            return result;
        }

        return editor_update(
            a_bufferIndex,
            a_renderWidth,
            a_renderHeight,
            a_deltaTime);
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
        transform->rotation = Math::Quaternion::identity();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        meshFilter->meshId = m_defaultStaticMeshId;
        renderer->visible = true;
        renderer->castsShadow = true;
        renderer->receivesShadow = true;
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_game_object(
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        Result result = create_object("GameObject", a_outObject);
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
                "Failed to add transform component for game object.") : result;
        }

        transform->position = a_position;
        transform->rotation = Math::Quaternion::identity();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
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
        transform->rotation = Math::Quaternion::identity();
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
        transform->rotation = Math::Quaternion::identity();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        renderer->materialHandle = m_defaultMaterialHandle;
        renderer->isVisible = true;
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_directional_light_object(
        const Math::float3& a_position,
        GameObject& a_outObject)
    {
        a_outObject = {};

        Result result = create_object("DirectionalLight", a_outObject);
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
                "Failed to add transform component for directional light.")
                : result;
        }

        ECS::DirectionalLightComponent* light = nullptr;
        result = add_component<ECS::DirectionalLightComponent>(
            a_outObject.entity_id(), light);
        if (!result || light == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add directional light component.") : result;
        }

        transform->position = a_position;
        transform->rotation = Math::quaternion_from_euler_xyz(
            Math::float3(0.6f, -0.5f, 0.0f));
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        *light = ECS::DirectionalLightComponent{};
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_point_light_object(
        const Math::float3& a_position,
        GameObject& a_outObject)
    {
        a_outObject = {};

        Result result = create_object("PointLight", a_outObject);
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
                "Failed to add transform component for point light.") : result;
        }

        ECS::PointLightComponent* light = nullptr;
        result = add_component<ECS::PointLightComponent>(
            a_outObject.entity_id(), light);
        if (!result || light == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add point light component.") : result;
        }

        transform->position = a_position;
        transform->rotation = Math::Quaternion::identity();
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        *light = ECS::PointLightComponent{};
        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::add_spot_light_object(
        const Math::float3& a_position,
        GameObject& a_outObject)
    {
        a_outObject = {};

        Result result = create_object("SpotLight", a_outObject);
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
                "Failed to add transform component for spot light.") : result;
        }

        ECS::SpotLightComponent* light = nullptr;
        result = add_component<ECS::SpotLightComponent>(
            a_outObject.entity_id(), light);
        if (!result || light == nullptr)
        {
            destroy_object_immediately(a_outObject.entity_id());
            return result ? Result::fail(Code::CreateFailed, Severity::Error,
                "Failed to add spot light component.") : result;
        }

        transform->position = a_position;
        transform->rotation = Math::quaternion_from_euler_xyz(
            Math::float3(0.35f, 0.0f, 0.0f));
        transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
        *light = ECS::SpotLightComponent{};
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
                a_position, m_defaultStaticMeshId);
        return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
    }

    [[nodiscard]] Result GameWorld::add_game_object_to_scene(SceneId a_sceneId,
        const Math::float3& a_position, GameObject& a_outObject)
    {
        a_outObject = {};
        if (a_sceneId == k_invalidSceneId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene id is invalid.");
        }

        const ObjectDefinition objectDefinition =
            make_default_game_object_definition(a_position);
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

    [[nodiscard]] Result GameWorld::add_directional_light_object_to_scene(
        SceneId a_sceneId,
        const Math::float3& a_position,
        GameObject& a_outObject)
    {
        a_outObject = {};
        if (a_sceneId == k_invalidSceneId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene id is invalid.");
        }

        const ObjectDefinition objectDefinition =
            make_default_directional_light_definition(a_position);
        return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
    }

    [[nodiscard]] Result GameWorld::add_point_light_object_to_scene(
        SceneId a_sceneId,
        const Math::float3& a_position,
        GameObject& a_outObject)
    {
        a_outObject = {};
        if (a_sceneId == k_invalidSceneId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene id is invalid.");
        }

        const ObjectDefinition objectDefinition =
            make_default_point_light_definition(a_position);
        return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
    }

    [[nodiscard]] Result GameWorld::add_spot_light_object_to_scene(
        SceneId a_sceneId,
        const Math::float3& a_position,
        GameObject& a_outObject)
    {
        a_outObject = {};
        if (a_sceneId == k_invalidSceneId)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Scene id is invalid.");
        }

        const ObjectDefinition objectDefinition =
            make_default_spot_light_definition(a_position);
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

        const uint32_t staticMeshCount =
            static_cast<uint32_t>(sceneFrame.staticMeshVisibilityItems.size());
        frameState.objectCount = staticMeshCount;
        frameState.spriteCount = 0;
        frameState.cpuIndexedDraws.clear();
        frameState.transparentCpuIndexedDraws.clear();
        frameState.cpuShadowCasters.clear();
        frameState.cpuShadowCasters.reserve(
            sceneFrame.staticMeshSurfaceItems.size());
        for (const DrawSystem::StaticMeshSurfaceItem& item :
            sceneFrame.staticMeshSurfaceItems)
        {
            frameState.cpuShadowCasters.push_back(
                make_cpu_shadow_caster(item.transform));
        }

        const DrawSystem::CameraDrawItem* sortCamera = nullptr;
        for (const DrawSystem::CameraDrawItem& item : sceneFrame.cameraItems)
        {
            if (item.isMain)
            {
                sortCamera = &item;
                break;
            }
            if (sortCamera == nullptr)
            {
                sortCamera = &item;
            }
        }

        const Math::float3 currentCameraPosition =
            sortCamera != nullptr
            ? camera_position(sortCamera->viewProjection)
            : Math::float3::zero();

        bool hasTransparentStaticMesh = false;
        for (const DrawSystem::StaticMeshSurfaceItem& item :
            sceneFrame.staticMeshSurfaceItems)
        {
            if (item.renderQueue == DrawSystem::StaticMeshRenderQueue::Transparent)
            {
                hasTransparentStaticMesh = true;
                break;
            }
        }
        if (hasTransparentStaticMesh)
        {
            frameState.useCpuBatching = true;
        }

        if (frameState.useCpuBatching)
        {
            frameState.cpuIndexedDraws.reserve(
                sceneFrame.staticMeshBatchItems.size());
            frameState.transparentCpuIndexedDraws.reserve(
                sceneFrame.staticMeshBatchItems.size());
            for (uint32_t index = 0; index < staticMeshCount; ++index)
            {
                const DrawSystem::StaticMeshBatchItem& item =
                    sceneFrame.staticMeshBatchItems[index];
                if (!item.hasCpuIndexedDraw)
                {
                    continue;
                }

                DrawSystem::CpuIndexedDraw draw = item.cpuIndexedDraw;
                draw.sortDepth = distance_squared(
                    transform_position(sceneFrame.staticMeshSurfaceItems[index].transform),
                    currentCameraPosition);

                if (sceneFrame.staticMeshSurfaceItems[index].renderQueue ==
                    DrawSystem::StaticMeshRenderQueue::Transparent)
                {
                    frameState.transparentCpuIndexedDraws.push_back(draw);
                    continue;
                }

                frameState.cpuIndexedDraws.push_back(draw);
            }

            std::stable_sort(
                frameState.transparentCpuIndexedDraws.begin(),
                frameState.transparentCpuIndexedDraws.end(),
                [](const DrawSystem::CpuIndexedDraw& a_left,
                    const DrawSystem::CpuIndexedDraw& a_right)
                {
                    return a_left.sortDepth > a_right.sortDepth;
                });
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

        auto& skinPaletteUploaders =
            m_drawResources->skin_palette_uploaders();
        if (!skinPaletteUploaders.empty())
        {
            const uint32_t uploaderIndex =
                resolve_uploader_index(
                    static_cast<uint32_t>(skinPaletteUploaders.size()));
            if (uploaderIndex < skinPaletteUploaders.size())
            {
                auto& uploader = skinPaletteUploaders[uploaderIndex];
                uploader.begin_frame();
                uint32_t paletteIndex = 0;
                for (const DrawSystem::StaticMeshSurfaceItem& item :
                     sceneFrame.staticMeshSurfaceItems)
                {
                    for (const GpuData::SkinPaletteGpu& palette :
                         item.skinPalette)
                    {
                        if (!uploader.push(paletteIndex, palette))
                        {
                            return Result::fail(Code::InvalidState,
                                Severity::Error,
                                "Failed to queue skin palette upload.");
                        }
                        ++paletteIndex;
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit skin palette upload.");
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

    [[nodiscard]] Result GameWorld::upload_particle_scene(uint32_t a_bufferIndex)
    {
        if (m_particleResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Particle resources are not initialized.");
        }

        if (a_bufferIndex >= m_particleFrameState.frameStates.size())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Particle frame index is out of range.");
        }

        auto resolve_uploader_index = [a_bufferIndex](uint32_t a_count) -> uint32_t
        {
            if (a_count <= 1)
            {
                return 0;
            }

            return a_bufferIndex;
        };

        ParticleSystem::ParticleSceneFrame& sceneFrame =
            m_particleScene.frame(a_bufferIndex);
        ParticleSystem::ParticleFrameData& frameState =
            m_particleFrameState.frame_state(a_bufferIndex);
        frameState.frame.emitterCount = (std::min)(
            static_cast<uint32_t>(sceneFrame.emitters.size()),
            k_maxParticleEmitterCount);
        uint32_t particleCount = 0;
        for (uint32_t emitterIndex = 0; emitterIndex < frameState.frame.emitterCount;
             ++emitterIndex)
        {
            const GpuData::ParticleEmitterGpu& emitter =
                sceneFrame.emitters[emitterIndex].emitter;
            particleCount = (std::max)(
                particleCount,
                emitter.particleBase + emitter.particleCapacity);
        }
        frameState.frame.particleCount =
            (std::min)(particleCount, k_maxParticleCount);

        auto& frameUploaders = m_particleResources->frame_uploaders();
        if (!frameUploaders.empty())
        {
            const uint32_t uploaderIndex = resolve_uploader_index(
                static_cast<uint32_t>(frameUploaders.size()));
            if (uploaderIndex < frameUploaders.size())
            {
                auto& uploader = frameUploaders[uploaderIndex];
                uploader.begin_frame();
                if (!uploader.push(0, frameState.frame))
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to queue particle frame upload.");
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit particle frame upload.");
                }
            }
        }

        auto& emitterUploaders = m_particleResources->emitter_uploaders();
        if (!emitterUploaders.empty())
        {
            const uint32_t uploaderIndex = resolve_uploader_index(
                static_cast<uint32_t>(emitterUploaders.size()));
            if (uploaderIndex < emitterUploaders.size())
            {
                auto& uploader = emitterUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0; index < frameState.frame.emitterCount;
                     ++index)
                {
                    if (!uploader.push(index, sceneFrame.emitters[index].emitter))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue particle emitter upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit particle emitter upload.");
                }
            }
        }

        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::upload_light_scene(uint32_t a_bufferIndex)
    {
        if (m_lightResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Light resources are not initialized.");
        }

        if (a_bufferIndex >= m_lightFrameState.frameStates.size())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Light frame index is out of range.");
        }

        auto resolve_uploader_index = [a_bufferIndex](uint32_t a_count) -> uint32_t
        {
            if (a_count <= 1)
            {
                return 0;
            }

            return a_bufferIndex;
        };

        LightingSystem::LightSceneFrame& sceneFrame =
            m_lightScene.frame(a_bufferIndex);
        LightingSystem::LightFrameData& frameState =
            m_lightFrameState.frame_state(a_bufferIndex);

        frameState.frame.directionalLightCount =
            (std::min)(static_cast<uint32_t>(sceneFrame.directionalLights.size()),
                GpuData::k_maxDirectionalLightCount);
        frameState.frame.pointLightCount =
            (std::min)(static_cast<uint32_t>(sceneFrame.pointLights.size()),
                GpuData::k_maxPointLightCount);
        frameState.frame.spotLightCount =
            (std::min)(static_cast<uint32_t>(sceneFrame.spotLights.size()),
                GpuData::k_maxSpotLightCount);

        auto& frameUploaders = m_lightResources->frame_uploaders();
        if (!frameUploaders.empty())
        {
            const uint32_t uploaderIndex = resolve_uploader_index(
                static_cast<uint32_t>(frameUploaders.size()));
            if (uploaderIndex < frameUploaders.size())
            {
                auto& uploader = frameUploaders[uploaderIndex];
                uploader.begin_frame();
                if (!uploader.push(0, frameState.frame))
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to queue light frame upload.");
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit light frame upload.");
                }
            }
        }

        auto& directionalUploaders =
            m_lightResources->directional_light_uploaders();
        if (!directionalUploaders.empty())
        {
            const uint32_t uploaderIndex = resolve_uploader_index(
                static_cast<uint32_t>(directionalUploaders.size()));
            if (uploaderIndex < directionalUploaders.size())
            {
                auto& uploader = directionalUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0;
                    index < frameState.frame.directionalLightCount;
                    ++index)
                {
                    if (!uploader.push(
                        index,
                        sceneFrame.directionalLights[index].light))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue directional light upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit directional light upload.");
                }
            }
        }

        auto& pointUploaders = m_lightResources->point_light_uploaders();
        if (!pointUploaders.empty())
        {
            const uint32_t uploaderIndex = resolve_uploader_index(
                static_cast<uint32_t>(pointUploaders.size()));
            if (uploaderIndex < pointUploaders.size())
            {
                auto& uploader = pointUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0;
                    index < frameState.frame.pointLightCount;
                    ++index)
                {
                    if (!uploader.push(index, sceneFrame.pointLights[index].light))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue point light upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit point light upload.");
                }
            }
        }

        auto& spotUploaders = m_lightResources->spot_light_uploaders();
        if (!spotUploaders.empty())
        {
            const uint32_t uploaderIndex = resolve_uploader_index(
                static_cast<uint32_t>(spotUploaders.size()));
            if (uploaderIndex < spotUploaders.size())
            {
                auto& uploader = spotUploaders[uploaderIndex];
                uploader.begin_frame();
                for (uint32_t index = 0;
                    index < frameState.frame.spotLightCount;
                    ++index)
                {
                    if (!uploader.push(index, sceneFrame.spotLights[index].light))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
                            "Failed to queue spot light upload.");
                    }
                }
                if (!uploader.commit())
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "Failed to commit spot light upload.");
                }
            }
        }

        return Result::ok();
    }

    [[nodiscard]] Result GameWorld::upload_shadow_scene(uint32_t a_bufferIndex)
    {
        if (m_shadowResources == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Shadow resources are not initialized.");
        }

        if (a_bufferIndex >= m_shadowFrameState.frameStates.size())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Shadow frame index is out of range.");
        }

        ShadowSystem::ShadowSceneFrame& sceneFrame =
            m_shadowScene.frame(a_bufferIndex);
        ShadowSystem::ShadowFrameData& frameState =
            m_shadowFrameState.frame_state(a_bufferIndex);
        frameState.directionalShadow = sceneFrame.hasDirectionalShadow
            ? sceneFrame.directionalShadow.shadow
            : GpuData::DirectionalShadowFrameGpu{};
        frameState.pointShadowFaces = sceneFrame.hasPointShadow
            ? sceneFrame.pointShadow.faces
            : std::array<GpuData::PointShadowFaceGpu,
                GpuData::k_pointShadowFaceCount>{};
        frameState.spotShadows = {};
        const uint32_t spotShadowCount = (std::min)(
            static_cast<uint32_t>(sceneFrame.spotShadows.size()),
            GpuData::k_maxSpotShadowCount);
        for (uint32_t shadowIndex = 0; shadowIndex < spotShadowCount;
             ++shadowIndex)
        {
            frameState.spotShadows[shadowIndex] =
                sceneFrame.spotShadows[shadowIndex].shadow;
        }

        auto resolve_uploader_index = [a_bufferIndex](uint32_t a_count) -> uint32_t
        {
            if (a_count <= 1)
            {
                return 0;
            }

            return a_bufferIndex;
        };

        auto& uploaders = m_shadowResources->spot_shadow_frame_uploaders();
        if (uploaders.empty())
        {
            return Result::ok();
        }

        const uint32_t uploaderIndex = resolve_uploader_index(
            static_cast<uint32_t>(uploaders.size()));
        if (uploaderIndex >= uploaders.size())
        {
            return Result::ok();
        }

        auto& uploader = uploaders[uploaderIndex];
        uploader.begin_frame();
        for (uint32_t shadowIndex = 0; shadowIndex < GpuData::k_maxSpotShadowCount;
             ++shadowIndex)
        {
            if (!uploader.push(shadowIndex, frameState.spotShadows[shadowIndex]))
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Failed to queue spot shadow frame upload.");
            }
        }
        if (!uploader.commit())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Failed to commit spot shadow frame upload.");
        }

        auto& directionalUploaders =
            m_shadowResources->directional_shadow_frame_uploaders();
        if (directionalUploaders.empty())
        {
            return Result::ok();
        }

        const uint32_t directionalUploaderIndex = resolve_uploader_index(
            static_cast<uint32_t>(directionalUploaders.size()));
        if (directionalUploaderIndex >= directionalUploaders.size())
        {
            return Result::ok();
        }

        auto& directionalUploader =
            directionalUploaders[directionalUploaderIndex];
        directionalUploader.begin_frame();
        if (!directionalUploader.push(0, frameState.directionalShadow))
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Failed to queue directional shadow frame upload.");
        }
        if (!directionalUploader.commit())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Failed to commit directional shadow frame upload.");
        }

        auto& pointUploaders = m_shadowResources->point_shadow_face_uploaders();
        if (pointUploaders.empty())
        {
            return Result::ok();
        }

        const uint32_t pointUploaderIndex = resolve_uploader_index(
            static_cast<uint32_t>(pointUploaders.size()));
        if (pointUploaderIndex >= pointUploaders.size())
        {
            return Result::ok();
        }

        auto& pointUploader = pointUploaders[pointUploaderIndex];
        pointUploader.begin_frame();
        for (uint32_t faceIndex = 0; faceIndex < GpuData::k_pointShadowFaceCount;
             ++faceIndex)
        {
            if (!pointUploader.push(faceIndex, frameState.pointShadowFaces[faceIndex]))
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "Failed to queue point shadow face upload.");
            }
        }
        if (!pointUploader.commit())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Failed to commit point shadow face upload.");
        }

        return Result::ok();
    }
}
