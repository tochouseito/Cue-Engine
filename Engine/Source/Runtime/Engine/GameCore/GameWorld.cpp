// GameWorld
// の重いテンプレート外処理を分離し、シーン実行状態の所有範囲を明確にする

#include "GameWorld.h"

namespace Cue::GameCore
{
namespace
{
[[nodiscard]] DrawSystem::CpuShadowCaster make_cpu_shadow_caster(
    const GpuData::ObjectTransformGpu &a_transform) noexcept
{
    const Math::float4x4 &world = a_transform.worldMatrix;
    const Math::float3 basisX(world.values[0][0], world.values[0][1], world.values[0][2]);
    const Math::float3 basisY(world.values[1][0], world.values[1][1], world.values[1][2]);
    const Math::float3 basisZ(world.values[2][0], world.values[2][1], world.values[2][2]);
    const float maxExtent =
        (std::max)(basisX.length(), (std::max)(basisY.length(), basisZ.length()));

    DrawSystem::CpuShadowCaster caster{};
    caster.center = Math::float3(world.values[3][0], world.values[3][1], world.values[3][2]);
    caster.radius = maxExtent * 1.7320508f;
    return caster;
}

[[nodiscard]] Math::float3 transform_position(
    const GpuData::ObjectTransformGpu &a_transform) noexcept
{
    const Math::float4x4 &world = a_transform.worldMatrix;
    return Math::float3(world.values[3][0], world.values[3][1], world.values[3][2]);
}

[[nodiscard]] Math::float3 camera_position(
    const GpuData::ViewProjectionGpu &a_viewProjection) noexcept
{
    const Math::float4x4 world = Math::float4x4::inverse(a_viewProjection.view);
    return Math::float3(world.values[3][0], world.values[3][1], world.values[3][2]);
}

[[nodiscard]] float distance_squared(const Math::float3 &a_left,
                                     const Math::float3 &a_right) noexcept
{
    const Math::float3 diff = a_left - a_right;
    return diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
}

[[nodiscard]] ObjectDefinition make_default_static_mesh_object_definition(
    const Math::float3 &a_position, uint32_t a_meshId)
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

[[nodiscard]] ObjectDefinition make_default_game_object_definition(const Math::float3 &a_position)
{
    ObjectDefinition objectDefinition("GameObject");

    ECS::TransformComponent transform{};
    transform.position = a_position;
    transform.rotation = Math::Quaternion::identity();
    transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
    objectDefinition.prototype.add_component(transform);

    return objectDefinition;
}

[[nodiscard]] ObjectDefinition make_default_camera_object_definition(const Math::float3 &a_position)
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
    const Math::float3 &a_position, MaterialHandle a_defaultMaterialHandle)
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
    const Math::float3 &a_position)
{
    ObjectDefinition objectDefinition("DirectionalLight");

    ECS::TransformComponent transform{};
    transform.position = a_position;
    transform.rotation = Math::quaternion_from_euler_xyz(Math::float3(0.6f, -0.5f, 0.0f));
    transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
    objectDefinition.prototype.add_component(transform);

    ECS::DirectionalLightComponent light{};
    objectDefinition.prototype.add_component(light);

    return objectDefinition;
}

[[nodiscard]] ObjectDefinition make_default_point_light_definition(const Math::float3 &a_position)
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

[[nodiscard]] ObjectDefinition make_default_spot_light_definition(const Math::float3 &a_position)
{
    ObjectDefinition objectDefinition("SpotLight");

    ECS::TransformComponent transform{};
    transform.position = a_position;
    transform.rotation = Math::quaternion_from_euler_xyz(Math::float3(0.35f, 0.0f, 0.0f));
    transform.scale = Math::float3(1.0f, 1.0f, 1.0f);
    objectDefinition.prototype.add_component(transform);

    ECS::SpotLightComponent light{};
    objectDefinition.prototype.add_component(light);

    return objectDefinition;
}
} // namespace

GameWorld::GameWorld() = default;

[[nodiscard]] Result GameWorld::initialize(
    RHI::IBufferManager *a_bufferManager, RHI::IViewManager *a_viewManager,
    DrawSystem::IStaticMeshPool *a_staticMeshPool, AssetManager *a_assetManager,
    Core::IO::IFileSystem *a_fileSystem, Audio::IBackend *a_audioBackend,
    Audio::AudioDeviceHandle a_audioDevice, Physics::IPhysicsSystem *a_physicsSystem,
    PAL::InputManager *a_inputManager, uint32_t a_bufferCount, uint32_t a_renderWidth,
    uint32_t a_renderHeight, uint32_t a_defaultStaticMeshId, MaterialHandle a_defaultMaterialHandle)
{
    if (a_bufferManager == nullptr || a_viewManager == nullptr || a_staticMeshPool == nullptr ||
        a_assetManager == nullptr || a_fileSystem == nullptr || a_audioBackend == nullptr)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "GameWorld requires valid buffer, view, mesh, asset, "
                            "file, and audio managers.");
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

    // 各 System から共有参照される非所有ポインタを先に固定する
    m_defaultStaticMeshId = a_defaultStaticMeshId;
    m_assetManager = a_assetManager;
    m_fileSystem = a_fileSystem;
    m_audioBackend = a_audioBackend;
    m_physicsSystem = a_physicsSystem;
    m_inputManager = a_inputManager;
    m_audioDevice = a_audioDevice;
    m_defaultMaterialHandle = a_defaultMaterialHandle;

    // Buffer 数に依存する Frame/Scene 状態は Resource 作成前に確保する
    m_drawFrameState.resize(a_bufferCount);
    m_drawScene.resize(a_bufferCount);
    m_particleFrameState.resize(a_bufferCount);
    m_particleScene.resize(a_bufferCount);
    m_effectPrimitiveFrameState.resize(a_bufferCount);
    m_effectPrimitiveScene.resize(a_bufferCount);
    m_lightFrameState.resize(a_bufferCount);
    m_lightScene.resize(a_bufferCount);
    m_shadowFrameState.resize(a_bufferCount);
    m_shadowScene.resize(a_bufferCount);
    for (uint32_t bufferIndex = 0; bufferIndex < a_bufferCount; ++bufferIndex)
    {
        sync_draw_frame_state(bufferIndex, a_renderWidth, a_renderHeight);
    }

    // DrawSystem が参照する GPU buffer 群を用途ごとに確保する
    m_drawResources =
        std::make_unique<DrawSystem::DrawResources>(a_bufferManager, a_viewManager, a_bufferCount);

    Result result = m_drawResources->create_renderable_info_buffer(k_maxRenderObjectCount);
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

    result = m_drawResources->create_skin_palette_buffer(k_maxSkinPaletteCount);
    if (!result)
    {
        return result;
    }

    result = m_drawResources->create_render_object_buffer(k_maxRenderObjectCount);
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

    m_particleResources = std::make_unique<ParticleSystem::ParticleResources>(
        a_bufferManager, a_viewManager, a_bufferCount);

    result = m_particleResources->create_frame_buffer();
    if (!result)
    {
        return result;
    }

    result = m_particleResources->create_emitter_buffer(k_maxParticleEmitterCount);
    if (!result)
    {
        return result;
    }

    result = m_particleResources->create_particle_buffer(k_maxParticleCount);
    if (!result)
    {
        return result;
    }

    result = m_particleResources->create_trail_buffer(k_maxParticleCount,
                                                      GpuData::k_maxParticleTrailSegmentCount);
    if (!result)
    {
        return result;
    }

    m_effectPrimitiveResources =
        std::make_unique<EffectSystem::EffectPrimitiveResources>(a_bufferManager, a_bufferCount);

    result = m_effectPrimitiveResources->create_frame_buffer();
    if (!result)
    {
        return result;
    }

    result = m_effectPrimitiveResources->create_sprite_buffer(GpuData::k_maxEffectSpriteCount);
    if (!result)
    {
        return result;
    }

    result = m_effectPrimitiveResources->create_ribbon_buffer(GpuData::k_maxEffectRibbonCount);
    if (!result)
    {
        return result;
    }

    m_lightResources = std::make_unique<LightingSystem::LightResources>(
        a_bufferManager, a_viewManager, a_bufferCount);

    result = m_lightResources->create_frame_buffer();
    if (!result)
    {
        return result;
    }

    result = m_lightResources->create_directional_light_buffer(GpuData::k_maxDirectionalLightCount);
    if (!result)
    {
        return result;
    }

    result = m_lightResources->create_point_light_buffer(GpuData::k_maxPointLightCount);
    if (!result)
    {
        return result;
    }

    result = m_lightResources->create_spot_light_buffer(GpuData::k_maxSpotLightCount);
    if (!result)
    {
        return result;
    }

    m_shadowResources = std::make_unique<ShadowSystem::ShadowResources>(
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

    auto &renderableObjectSystem = m_ecs.add_system<ECS::RenderableObjectSystem>(
        m_assetManager, a_staticMeshPool, m_defaultMaterialHandle, m_drawFrameState, m_drawScene);
    auto &skinnedRenderableObjectSystem = m_ecs.add_system<ECS::SkinnedRenderableObjectSystem>(
        m_assetManager, a_staticMeshPool, m_defaultMaterialHandle, m_drawFrameState, m_drawScene);
    auto &spriteSystem = m_ecs.add_system<ECS::SpriteSystem>(
        m_assetManager, m_defaultMaterialHandle, m_drawFrameState, m_drawScene);
    auto &textSystem =
        m_ecs.add_system<ECS::TextSystem>(m_fontAtlasManager, m_drawFrameState, m_drawScene);
    auto &particleEmitterSystem = m_ecs.add_system<ECS::ParticleEmitterSystem>(
        m_assetManager, m_defaultMaterialHandle, m_particleRangeAllocator, m_particleScene);
    auto &effectEmitterSystem = m_ecs.add_system<ECS::EffectEmitterSystem>(
        m_assetManager, m_defaultMaterialHandle, m_particleRangeAllocator, m_particleScene,
        m_effectPrimitiveScene);
    auto &uiLayoutSystem = m_ecs.add_system<ECS::UiLayoutSystem>(m_drawFrameState);
    auto &uiWidgetSystem = m_ecs.add_system<ECS::UiWidgetSystem>(
        m_inputManager, m_assetManager, m_defaultMaterialHandle, m_drawFrameState, m_drawScene);
    auto &cameraSystem = m_ecs.add_system<ECS::CameraSystem>(m_drawFrameState, m_drawScene);
    auto &lightSystem = m_ecs.add_system<ECS::LightSystem>(m_lightScene);
    auto &shadowSystem = m_ecs.add_system<ECS::ShadowSystem>(m_shadowScene);
    auto &firstPersonCameraControllerSystem =
        m_ecs.add_system<ECS::FirstPersonCameraControllerSystem>(m_inputManager);
    auto &playerControlSystem = m_ecs.add_system<ECS::PlayerControlSystem>(m_inputManager);
    auto &audioSystem = m_ecs.add_system<ECS::AudioSystem>(m_fileSystem, m_audioBackend,
                                                           m_audioDevice, m_assetRootPath);
    auto &characterControllerSystem =
        m_ecs.add_system<ECS::CharacterControllerSystem>(a_physicsSystem);
    auto &physicsBodySystem =
        m_ecs.add_system<ECS::PhysicsBodySystem>(a_physicsSystem, m_assetManager);
    result = m_navigationWorld.set_backend(std::make_unique<RecastNavigationBackend>());
    if (!result)
    {
        return result;
    }
    auto &navigationSystem = m_ecs.add_system<ECS::NavigationSystem>(&m_navigationWorld);
    m_navigationSystem = &navigationSystem;
    auto &navAgentMotorSystem = m_ecs.add_system<ECS::NavAgentMotorSystem>();
    auto &triggerVolumeSystem = m_ecs.add_system<ECS::TriggerVolumeSystem>();
    auto &demoEnemySystem = m_ecs.add_system<ECS::DemoEnemySystem>(&m_debugDraw);
    auto &animationSystem = m_ecs.add_system<ECS::AnimationSystem>(m_assetManager);

    m_editorPipeline.add_system(&animationSystem);
    m_editorPipeline.add_system(&cameraSystem);
    m_editorPipeline.add_system(&renderableObjectSystem);
    m_editorPipeline.add_system(&skinnedRenderableObjectSystem);
    m_editorPipeline.add_system(&uiLayoutSystem);
    m_editorPipeline.add_system(&uiWidgetSystem);
    m_editorPipeline.add_system(&textSystem);
    m_editorPipeline.add_system(&spriteSystem);
    m_editorPipeline.add_system(&effectEmitterSystem);
    m_editorPipeline.add_system(&particleEmitterSystem);
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

[[nodiscard]] Result GameWorld::editor_update(uint32_t a_bufferIndex, uint32_t a_renderWidth,
                                              uint32_t a_renderHeight, float a_deltaTime)
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
        ParticleSystem::ParticleFrameData &particleFrame =
            m_particleFrameState.frame_state(a_bufferIndex);
        particleFrame.frame.deltaTime = a_deltaTime;
        particleFrame.frame.time += a_deltaTime;
    }
    if (a_bufferIndex < m_effectPrimitiveFrameState.frameStates.size())
    {
        EffectSystem::EffectPrimitiveFrameData &effectFrame =
            m_effectPrimitiveFrameState.frame_state(a_bufferIndex);
        effectFrame.frame.deltaTime = a_deltaTime;
        effectFrame.frame.time += a_deltaTime;
    }
    m_drawScene.begin_frame(a_bufferIndex);
    m_particleScene.begin_frame(a_bufferIndex);
    m_effectPrimitiveScene.begin_frame(a_bufferIndex);
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

    result = upload_effect_primitive_scene(a_bufferIndex);
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

    return editor_update(a_bufferIndex, a_renderWidth, a_renderHeight, a_deltaTime);
}

[[nodiscard]] Result GameWorld::clone_from(const GameWorld &a_source)
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
    for (const auto &[sceneId, _] : a_source.m_scenes)
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

        const SceneInstance &sourceScene = sourceSceneIt->second;

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

            const BaseComponent *base = a_source.get_component<BaseComponent>(entityId);
            const EntityRecord *record = a_source.try_get_entity_record(entityId);
            if (base == nullptr || record == nullptr || !record->isAlive)
            {
                continue;
            }

            ObjectDefinition definition{};
            definition.localObjectId = record->sourceLocalObjectId;
            definition.isActive = base->isActiveSelf;
            definition.isPersistent = base->isPersistent;
            definition.prototype = a_source.build_object_prototype(entityId, *base);

            if (base->parent != k_invalidEntityId &&
                a_source.source_scene_id(base->parent) == sceneId)
            {
                const EntityRecord *parentRecord = a_source.try_get_entity_record(base->parent);
                if (parentRecord != nullptr && parentRecord->isAlive &&
                    parentRecord->sourceLocalObjectId != k_invalidLocalObjectId)
                {
                    definition.parentLocalObjectId = parentRecord->sourceLocalObjectId;
                }
            }

            objectDefinitions.push_back(std::move(definition));
        }

        try
        {
            (void)instantiate_into_scene(
                sceneId, std::span<const ObjectDefinition>(objectDefinitions), sourceScene.asset);
        }
        catch (const std::exception &exception)
        {
            return Result::fail(Code::CreateFailed, Severity::Error, exception.what());
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

    for (EntityId entityId = 0; entityId < static_cast<EntityId>(a_source.m_entityRecords.size());
         ++entityId)
    {
        if (!a_source.contains_object(entityId) ||
            a_source.source_scene_id(entityId) != k_invalidSceneId)
        {
            continue;
        }

        const BaseComponent *base = a_source.get_component<BaseComponent>(entityId);
        if (base == nullptr)
        {
            continue;
        }

        ObjectDefinition definition{};
        definition.isActive = base->isActiveSelf;
        definition.isPersistent = base->isPersistent;
        definition.prototype = a_source.build_object_prototype(entityId, *base);

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
            pendingLooseParents.push_back(
                PendingLooseParent{entityId, clonedObject.entity_id(), base->parent});
        }
    }

    for (const PendingLooseParent &pendingParent : pendingLooseParents)
    {
        const auto parentIt = looseEntityMap.find(pendingParent.sourceParentId);
        if (parentIt == looseEntityMap.end())
        {
            continue;
        }

        BaseComponent *base = get_component<BaseComponent>(pendingParent.targetEntityId);
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
        result = load_navigation_mesh(a_source.m_activeNavMeshAsset, navMeshHandle);
        if (!result)
        {
            return result;
        }
    }

    return Result::ok();
}

[[nodiscard]] Result GameWorld::add_object(const Math::float3 &a_position, GameObject &a_outObject)
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

    ECS::TransformComponent *transform = nullptr;
    result = add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
    if (!result || transform == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add transform component for object.")
                      : result;
    }

    ECS::MeshFilterComponent *meshFilter = nullptr;
    result = add_component<ECS::MeshFilterComponent>(a_outObject.entity_id(), meshFilter);
    if (!result || meshFilter == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add mesh filter component for object.")
                      : result;
    }

    ECS::StaticMeshRendererComponent *renderer = nullptr;
    result = add_component<ECS::StaticMeshRendererComponent>(a_outObject.entity_id(), renderer);
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

[[nodiscard]] Result GameWorld::add_game_object(const Math::float3 &a_position,
                                                GameObject &a_outObject)
{
    a_outObject = {};
    Result result = create_object("GameObject", a_outObject);
    if (!result)
    {
        return result;
    }

    ECS::TransformComponent *transform = nullptr;
    result = add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
    if (!result || transform == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add transform component for game object.")
                      : result;
    }

    transform->position = a_position;
    transform->rotation = Math::Quaternion::identity();
    transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::add_camera_object(const Math::float3 &a_position,
                                                  GameObject &a_outObject)
{
    a_outObject = {};

    Result result = create_object("Camera", a_outObject);
    if (!result)
    {
        return result;
    }

    ECS::TransformComponent *transform = nullptr;
    result = add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
    if (!result || transform == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add transform component for camera object.")
                      : result;
    }

    ECS::CameraComponent *camera = nullptr;
    result = add_component<ECS::CameraComponent>(a_outObject.entity_id(), camera);
    if (!result || camera == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add camera component for camera object.")
                      : result;
    }

    transform->position = a_position;
    transform->rotation = Math::Quaternion::identity();
    transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
    *camera = ECS::CameraComponent{};
    return Result::ok();
}

[[nodiscard]] Result GameWorld::add_sprite_object(const Math::float3 &a_position,
                                                  GameObject &a_outObject)
{
    a_outObject = {};

    Result result = create_object("SpriteObject", a_outObject);
    if (!result)
    {
        return result;
    }

    ECS::TransformComponent *transform = nullptr;
    result = add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
    if (!result || transform == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add transform component for sprite object.")
                      : result;
    }

    ECS::SpriteRendererComponent *renderer = nullptr;
    result = add_component<ECS::SpriteRendererComponent>(a_outObject.entity_id(), renderer);
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

[[nodiscard]] Result GameWorld::add_directional_light_object(const Math::float3 &a_position,
                                                             GameObject &a_outObject)
{
    a_outObject = {};

    Result result = create_object("DirectionalLight", a_outObject);
    if (!result)
    {
        return result;
    }

    ECS::TransformComponent *transform = nullptr;
    result = add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
    if (!result || transform == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add transform component for directional light.")
                      : result;
    }

    ECS::DirectionalLightComponent *light = nullptr;
    result = add_component<ECS::DirectionalLightComponent>(a_outObject.entity_id(), light);
    if (!result || light == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add directional light component.")
                      : result;
    }

    transform->position = a_position;
    transform->rotation = Math::quaternion_from_euler_xyz(Math::float3(0.6f, -0.5f, 0.0f));
    transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
    *light = ECS::DirectionalLightComponent{};
    return Result::ok();
}

[[nodiscard]] Result GameWorld::add_point_light_object(const Math::float3 &a_position,
                                                       GameObject &a_outObject)
{
    a_outObject = {};

    Result result = create_object("PointLight", a_outObject);
    if (!result)
    {
        return result;
    }

    ECS::TransformComponent *transform = nullptr;
    result = add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
    if (!result || transform == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add transform component for point light.")
                      : result;
    }

    ECS::PointLightComponent *light = nullptr;
    result = add_component<ECS::PointLightComponent>(a_outObject.entity_id(), light);
    if (!result || light == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add point light component.")
                      : result;
    }

    transform->position = a_position;
    transform->rotation = Math::Quaternion::identity();
    transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
    *light = ECS::PointLightComponent{};
    return Result::ok();
}

[[nodiscard]] Result GameWorld::add_spot_light_object(const Math::float3 &a_position,
                                                      GameObject &a_outObject)
{
    a_outObject = {};

    Result result = create_object("SpotLight", a_outObject);
    if (!result)
    {
        return result;
    }

    ECS::TransformComponent *transform = nullptr;
    result = add_component<ECS::TransformComponent>(a_outObject.entity_id(), transform);
    if (!result || transform == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add transform component for spot light.")
                      : result;
    }

    ECS::SpotLightComponent *light = nullptr;
    result = add_component<ECS::SpotLightComponent>(a_outObject.entity_id(), light);
    if (!result || light == nullptr)
    {
        destroy_object_immediately(a_outObject.entity_id());
        return result ? Result::fail(Code::CreateFailed, Severity::Error,
                                     "Failed to add spot light component.")
                      : result;
    }

    transform->position = a_position;
    transform->rotation = Math::quaternion_from_euler_xyz(Math::float3(0.35f, 0.0f, 0.0f));
    transform->scale = Math::float3(1.0f, 1.0f, 1.0f);
    *light = ECS::SpotLightComponent{};
    return Result::ok();
}

[[nodiscard]] Result GameWorld::add_object_to_scene(SceneId a_sceneId,
                                                    const Math::float3 &a_position,
                                                    GameObject &a_outObject)
{
    a_outObject = {};
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Scene id is invalid.");
    }
    if (m_defaultStaticMeshId == ECS::k_invalidMeshId)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld default static mesh id is invalid.");
    }

    const ObjectDefinition objectDefinition =
        make_default_static_mesh_object_definition(a_position, m_defaultStaticMeshId);
    return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
}

[[nodiscard]] Result GameWorld::add_game_object_to_scene(SceneId a_sceneId,
                                                         const Math::float3 &a_position,
                                                         GameObject &a_outObject)
{
    a_outObject = {};
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Scene id is invalid.");
    }

    const ObjectDefinition objectDefinition = make_default_game_object_definition(a_position);
    return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
}

[[nodiscard]] Result GameWorld::add_camera_object_to_scene(SceneId a_sceneId,
                                                           const Math::float3 &a_position,
                                                           GameObject &a_outObject)
{
    a_outObject = {};
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Scene id is invalid.");
    }

    const ObjectDefinition objectDefinition = make_default_camera_object_definition(a_position);
    return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
}

[[nodiscard]] Result GameWorld::add_sprite_object_to_scene(SceneId a_sceneId,
                                                           const Math::float3 &a_position,
                                                           GameObject &a_outObject)
{
    a_outObject = {};
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Scene id is invalid.");
    }

    const ObjectDefinition objectDefinition =
        make_default_sprite_object_definition(a_position, m_defaultMaterialHandle);
    return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
}

[[nodiscard]] Result GameWorld::add_directional_light_object_to_scene(
    SceneId a_sceneId, const Math::float3 &a_position, GameObject &a_outObject)
{
    a_outObject = {};
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Scene id is invalid.");
    }

    const ObjectDefinition objectDefinition = make_default_directional_light_definition(a_position);
    return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
}

[[nodiscard]] Result GameWorld::add_point_light_object_to_scene(SceneId a_sceneId,
                                                                const Math::float3 &a_position,
                                                                GameObject &a_outObject)
{
    a_outObject = {};
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Scene id is invalid.");
    }

    const ObjectDefinition objectDefinition = make_default_point_light_definition(a_position);
    return append_object_to_scene(a_sceneId, objectDefinition, a_outObject);
}

[[nodiscard]] Result GameWorld::add_spot_light_object_to_scene(SceneId a_sceneId,
                                                               const Math::float3 &a_position,
                                                               GameObject &a_outObject)
{
    a_outObject = {};
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Scene id is invalid.");
    }

    const ObjectDefinition objectDefinition = make_default_spot_light_definition(a_position);
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

    DrawSystem::DrawSceneFrame &sceneFrame = m_drawScene.frame(a_bufferIndex);
    DrawSystem::DrawFrameData &frameState = m_drawFrameState.frame_state(a_bufferIndex);
    if (sceneFrame.staticMeshVisibilityItems.size() != sceneFrame.staticMeshSurfaceItems.size() ||
        sceneFrame.staticMeshVisibilityItems.size() != sceneFrame.staticMeshBatchItems.size())
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
    frameState.cpuShadowCasters.reserve(sceneFrame.staticMeshSurfaceItems.size());
    for (const DrawSystem::StaticMeshSurfaceItem &item : sceneFrame.staticMeshSurfaceItems)
    {
        frameState.cpuShadowCasters.push_back(make_cpu_shadow_caster(item.transform));
    }

    const DrawSystem::CameraDrawItem *sortCamera = nullptr;
    for (const DrawSystem::CameraDrawItem &item : sceneFrame.cameraItems)
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
        sortCamera != nullptr ? camera_position(sortCamera->viewProjection) : Math::float3::zero();

    bool hasTransparentStaticMesh = false;
    for (const DrawSystem::StaticMeshSurfaceItem &item : sceneFrame.staticMeshSurfaceItems)
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
        frameState.cpuIndexedDraws.reserve(sceneFrame.staticMeshBatchItems.size());
        frameState.transparentCpuIndexedDraws.reserve(sceneFrame.staticMeshBatchItems.size());
        for (uint32_t index = 0; index < staticMeshCount; ++index)
        {
            const DrawSystem::StaticMeshBatchItem &item = sceneFrame.staticMeshBatchItems[index];
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
            [](const DrawSystem::CpuIndexedDraw &a_left, const DrawSystem::CpuIndexedDraw &a_right)
            { return a_left.sortDepth > a_right.sortDepth; });
    }

    auto &renderableInfoUploaders = m_drawResources->renderable_info_uploaders();
    if (!renderableInfoUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(renderableInfoUploaders.size()));
        if (uploaderIndex < renderableInfoUploaders.size())
        {
            auto &uploader = renderableInfoUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < sceneFrame.staticMeshVisibilityItems.size(); ++index)
            {
                if (!uploader.push(index,
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

    auto &transformUploaders = m_drawResources->transform_uploaders();
    if (!transformUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(transformUploaders.size()));
        if (uploaderIndex < transformUploaders.size())
        {
            auto &uploader = transformUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < sceneFrame.staticMeshSurfaceItems.size(); ++index)
            {
                if (!uploader.push(index, sceneFrame.staticMeshSurfaceItems[index].transform))
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

    auto &materialUploaders = m_drawResources->material_uploaders();
    if (!materialUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(materialUploaders.size()));
        if (uploaderIndex < materialUploaders.size())
        {
            auto &uploader = materialUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < sceneFrame.staticMeshSurfaceItems.size(); ++index)
            {
                if (!sceneFrame.staticMeshSurfaceItems[index].hasMaterial)
                {
                    continue;
                }

                if (!uploader.push(index, sceneFrame.staticMeshSurfaceItems[index].material))
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

    auto &skinPaletteUploaders = m_drawResources->skin_palette_uploaders();
    if (!skinPaletteUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(skinPaletteUploaders.size()));
        if (uploaderIndex < skinPaletteUploaders.size())
        {
            auto &uploader = skinPaletteUploaders[uploaderIndex];
            uploader.begin_frame();
            uint32_t paletteIndex = 0;
            for (const DrawSystem::StaticMeshSurfaceItem &item : sceneFrame.staticMeshSurfaceItems)
            {
                for (const GpuData::SkinPaletteGpu &palette : item.skinPalette)
                {
                    if (!uploader.push(paletteIndex, palette))
                    {
                        return Result::fail(Code::InvalidState, Severity::Error,
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

    auto &renderObjectUploaders = m_drawResources->render_object_uploaders();
    if (!renderObjectUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(renderObjectUploaders.size()));
        if (uploaderIndex < renderObjectUploaders.size())
        {
            auto &uploader = renderObjectUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < sceneFrame.staticMeshVisibilityItems.size(); ++index)
            {
                if (!uploader.push(index, sceneFrame.staticMeshVisibilityItems[index].renderObject))
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
        auto &visibleObjectCountUploaders = m_drawResources->visible_object_count_uploaders();
        if (!visibleObjectCountUploaders.empty())
        {
            const uint32_t uploaderIndex =
                resolve_uploader_index(static_cast<uint32_t>(visibleObjectCountUploaders.size()));
            if (uploaderIndex < visibleObjectCountUploaders.size())
            {
                auto &uploader = visibleObjectCountUploaders[uploaderIndex];
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
        sceneFrame.spriteItems.begin(), sceneFrame.spriteItems.end(),
        [](const DrawSystem::SpriteDrawItem &a_left, const DrawSystem::SpriteDrawItem &a_right)
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

    auto &spriteInstanceUploaders = m_drawResources->sprite_instance_uploaders();
    if (!spriteInstanceUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(spriteInstanceUploaders.size()));
        if (uploaderIndex < spriteInstanceUploaders.size())
        {
            auto &uploader = spriteInstanceUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < sceneFrame.spriteItems.size(); ++index)
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
    frameState.spriteCount = static_cast<uint32_t>(sceneFrame.spriteItems.size());

    auto &viewProjectionUploaders = m_drawResources->view_projection_uploaders();
    if (!viewProjectionUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(viewProjectionUploaders.size()));
        if (uploaderIndex < viewProjectionUploaders.size())
        {
            const DrawSystem::CameraDrawItem *selectedCamera = nullptr;
            for (const DrawSystem::CameraDrawItem &item : sceneFrame.cameraItems)
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

            auto &uploader = viewProjectionUploaders[uploaderIndex];
            uploader.begin_frame();
            if (selectedCamera != nullptr && !uploader.push(0, selectedCamera->viewProjection))
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

    ParticleSystem::ParticleSceneFrame &sceneFrame = m_particleScene.frame(a_bufferIndex);
    ParticleSystem::ParticleFrameData &frameState = m_particleFrameState.frame_state(a_bufferIndex);
    frameState.frame.emitterCount =
        (std::min)(static_cast<uint32_t>(sceneFrame.emitters.size()), k_maxParticleEmitterCount);
    uint32_t particleCount = 0;
    for (uint32_t emitterIndex = 0; emitterIndex < frameState.frame.emitterCount; ++emitterIndex)
    {
        const GpuData::ParticleEmitterGpu &emitter = sceneFrame.emitters[emitterIndex].emitter;
        particleCount = (std::max)(particleCount, emitter.particleBase + emitter.particleCapacity);
    }
    frameState.frame.particleCount = (std::min)(particleCount, k_maxParticleCount);
    frameState.frame.trailFrameIndex =
        m_particleTrailFrameIndex % GpuData::k_maxParticleTrailSegmentCount;
    frameState.frame.maxTrailSegmentCount = GpuData::k_maxParticleTrailSegmentCount;
    m_particleTrailFrameIndex =
        (m_particleTrailFrameIndex + 1u) % GpuData::k_maxParticleTrailSegmentCount;

    auto &frameUploaders = m_particleResources->frame_uploaders();
    if (!frameUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(frameUploaders.size()));
        if (uploaderIndex < frameUploaders.size())
        {
            auto &uploader = frameUploaders[uploaderIndex];
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

    auto &emitterUploaders = m_particleResources->emitter_uploaders();
    if (!emitterUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(emitterUploaders.size()));
        if (uploaderIndex < emitterUploaders.size())
        {
            auto &uploader = emitterUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < frameState.frame.emitterCount; ++index)
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

[[nodiscard]] Result GameWorld::upload_effect_primitive_scene(uint32_t a_bufferIndex)
{
    if (m_effectPrimitiveResources == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "Effect primitive resources are not initialized.");
    }

    if (a_bufferIndex >= m_effectPrimitiveFrameState.frameStates.size())
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "Effect primitive frame index is out of range.");
    }

    auto resolve_uploader_index = [a_bufferIndex](uint32_t a_count) -> uint32_t
    {
        if (a_count <= 1)
        {
            return 0;
        }

        return a_bufferIndex;
    };

    EffectSystem::EffectPrimitiveSceneFrame &sceneFrame =
        m_effectPrimitiveScene.frame(a_bufferIndex);
    EffectSystem::EffectPrimitiveFrameData &frameState =
        m_effectPrimitiveFrameState.frame_state(a_bufferIndex);
    frameState.frame.spriteCount = (std::min)(static_cast<uint32_t>(sceneFrame.sprites.size()),
                                              GpuData::k_maxEffectSpriteCount);
    frameState.frame.ribbonCount = (std::min)(static_cast<uint32_t>(sceneFrame.ribbons.size()),
                                              GpuData::k_maxEffectRibbonCount);

    auto &frameUploaders = m_effectPrimitiveResources->frame_uploaders();
    if (!frameUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(frameUploaders.size()));
        if (uploaderIndex < frameUploaders.size())
        {
            auto &uploader = frameUploaders[uploaderIndex];
            uploader.begin_frame();
            if (!uploader.push(0, frameState.frame))
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                                    "Failed to queue effect primitive frame upload.");
            }
            if (!uploader.commit())
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                                    "Failed to commit effect primitive frame upload.");
            }
        }
    }

    auto &spriteUploaders = m_effectPrimitiveResources->sprite_uploaders();
    if (!spriteUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(spriteUploaders.size()));
        if (uploaderIndex < spriteUploaders.size())
        {
            auto &uploader = spriteUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < frameState.frame.spriteCount; ++index)
            {
                if (!uploader.push(index, sceneFrame.sprites[index].sprite))
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                                        "Failed to queue effect sprite upload.");
                }
            }
            if (!uploader.commit())
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                                    "Failed to commit effect sprite upload.");
            }
        }
    }

    auto &ribbonUploaders = m_effectPrimitiveResources->ribbon_uploaders();
    if (!ribbonUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(ribbonUploaders.size()));
        if (uploaderIndex < ribbonUploaders.size())
        {
            auto &uploader = ribbonUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < frameState.frame.ribbonCount; ++index)
            {
                if (!uploader.push(index, sceneFrame.ribbons[index].ribbon))
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                                        "Failed to queue effect ribbon upload.");
                }
            }
            if (!uploader.commit())
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                                    "Failed to commit effect ribbon upload.");
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

    LightingSystem::LightSceneFrame &sceneFrame = m_lightScene.frame(a_bufferIndex);
    LightingSystem::LightFrameData &frameState = m_lightFrameState.frame_state(a_bufferIndex);

    frameState.frame.directionalLightCount =
        (std::min)(static_cast<uint32_t>(sceneFrame.directionalLights.size()),
                   GpuData::k_maxDirectionalLightCount);
    frameState.frame.pointLightCount =
        (std::min)(static_cast<uint32_t>(sceneFrame.pointLights.size()),
                   GpuData::k_maxPointLightCount);
    frameState.frame.spotLightCount =
        (std::min)(static_cast<uint32_t>(sceneFrame.spotLights.size()),
                   GpuData::k_maxSpotLightCount);

    auto &frameUploaders = m_lightResources->frame_uploaders();
    if (!frameUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(frameUploaders.size()));
        if (uploaderIndex < frameUploaders.size())
        {
            auto &uploader = frameUploaders[uploaderIndex];
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

    auto &directionalUploaders = m_lightResources->directional_light_uploaders();
    if (!directionalUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(directionalUploaders.size()));
        if (uploaderIndex < directionalUploaders.size())
        {
            auto &uploader = directionalUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < frameState.frame.directionalLightCount; ++index)
            {
                if (!uploader.push(index, sceneFrame.directionalLights[index].light))
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

    auto &pointUploaders = m_lightResources->point_light_uploaders();
    if (!pointUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(pointUploaders.size()));
        if (uploaderIndex < pointUploaders.size())
        {
            auto &uploader = pointUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < frameState.frame.pointLightCount; ++index)
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

    auto &spotUploaders = m_lightResources->spot_light_uploaders();
    if (!spotUploaders.empty())
    {
        const uint32_t uploaderIndex =
            resolve_uploader_index(static_cast<uint32_t>(spotUploaders.size()));
        if (uploaderIndex < spotUploaders.size())
        {
            auto &uploader = spotUploaders[uploaderIndex];
            uploader.begin_frame();
            for (uint32_t index = 0; index < frameState.frame.spotLightCount; ++index)
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

    ShadowSystem::ShadowSceneFrame &sceneFrame = m_shadowScene.frame(a_bufferIndex);
    ShadowSystem::ShadowFrameData &frameState = m_shadowFrameState.frame_state(a_bufferIndex);
    frameState.directionalShadow = sceneFrame.hasDirectionalShadow
                                       ? sceneFrame.directionalShadow.shadow
                                       : GpuData::DirectionalShadowFrameGpu{};
    frameState.pointShadowFaces =
        sceneFrame.hasPointShadow
            ? sceneFrame.pointShadow.faces
            : std::array<GpuData::PointShadowFaceGpu, GpuData::k_pointShadowFaceCount>{};
    frameState.spotShadows = {};
    const uint32_t spotShadowCount =
        (std::min)(static_cast<uint32_t>(sceneFrame.spotShadows.size()),
                   GpuData::k_maxSpotShadowCount);
    for (uint32_t shadowIndex = 0; shadowIndex < spotShadowCount; ++shadowIndex)
    {
        frameState.spotShadows[shadowIndex] = sceneFrame.spotShadows[shadowIndex].shadow;
    }

    auto resolve_uploader_index = [a_bufferIndex](uint32_t a_count) -> uint32_t
    {
        if (a_count <= 1)
        {
            return 0;
        }

        return a_bufferIndex;
    };

    auto &uploaders = m_shadowResources->spot_shadow_frame_uploaders();
    if (uploaders.empty())
    {
        return Result::ok();
    }

    const uint32_t uploaderIndex = resolve_uploader_index(static_cast<uint32_t>(uploaders.size()));
    if (uploaderIndex >= uploaders.size())
    {
        return Result::ok();
    }

    auto &uploader = uploaders[uploaderIndex];
    uploader.begin_frame();
    for (uint32_t shadowIndex = 0; shadowIndex < GpuData::k_maxSpotShadowCount; ++shadowIndex)
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

    auto &directionalUploaders = m_shadowResources->directional_shadow_frame_uploaders();
    if (directionalUploaders.empty())
    {
        return Result::ok();
    }

    const uint32_t directionalUploaderIndex =
        resolve_uploader_index(static_cast<uint32_t>(directionalUploaders.size()));
    if (directionalUploaderIndex >= directionalUploaders.size())
    {
        return Result::ok();
    }

    auto &directionalUploader = directionalUploaders[directionalUploaderIndex];
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

    auto &pointUploaders = m_shadowResources->point_shadow_face_uploaders();
    if (pointUploaders.empty())
    {
        return Result::ok();
    }

    const uint32_t pointUploaderIndex =
        resolve_uploader_index(static_cast<uint32_t>(pointUploaders.size()));
    if (pointUploaderIndex >= pointUploaders.size())
    {
        return Result::ok();
    }

    auto &pointUploader = pointUploaders[pointUploaderIndex];
    pointUploader.begin_frame();
    for (uint32_t faceIndex = 0; faceIndex < GpuData::k_pointShadowFaceCount; ++faceIndex)
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

[[nodiscard]] Result GameWorld::ecs(ECS::ECSManager *&a_outEcs) noexcept
{
    a_outEcs = &m_ecs;
    return Result::ok();
}

void GameWorld::set_asset_root_path(const Core::IO::Path &a_assetRootPath)
{
    m_assetRootPath = a_assetRootPath.normalize();
}

[[nodiscard]] const Core::IO::Path &GameWorld::asset_root_path() const noexcept
{
    return m_assetRootPath;
}

[[nodiscard]] Core::IO::IFileSystem *GameWorld::file_system() const noexcept
{
    return m_fileSystem;
}

[[nodiscard]] Result GameWorld::add_object()
{
    return add_object(make_spawn_position());
}

[[nodiscard]] Result GameWorld::add_object(GameObject &a_outObject)
{
    return add_object(make_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_object(const Math::float3 &a_position)
{
    GameObject object{};
    return add_object(a_position, object);
}

[[nodiscard]] Result GameWorld::add_game_object()
{
    GameObject object{};
    return add_game_object(object);
}

[[nodiscard]] Result GameWorld::add_game_object(GameObject &a_outObject)
{
    return add_game_object(make_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_game_object(const Math::float3 &a_position)
{
    GameObject object{};
    return add_game_object(a_position, object);
}

[[nodiscard]] Result GameWorld::add_camera_object()
{
    GameObject object{};
    return add_camera_object(object);
}

[[nodiscard]] Result GameWorld::add_camera_object(GameObject &a_outObject)
{
    return add_camera_object(make_camera_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_camera_object(const Math::float3 &a_position)
{
    GameObject object{};
    return add_camera_object(a_position, object);
}

[[nodiscard]] Result GameWorld::add_directional_light_object()
{
    GameObject object{};
    return add_directional_light_object(object);
}

[[nodiscard]] Result GameWorld::add_directional_light_object(GameObject &a_outObject)
{
    return add_directional_light_object(make_light_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_directional_light_object(const Math::float3 &a_position)
{
    GameObject object{};
    return add_directional_light_object(a_position, object);
}

[[nodiscard]] Result GameWorld::add_point_light_object()
{
    GameObject object{};
    return add_point_light_object(object);
}

[[nodiscard]] Result GameWorld::add_point_light_object(GameObject &a_outObject)
{
    return add_point_light_object(make_light_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_point_light_object(const Math::float3 &a_position)
{
    GameObject object{};
    return add_point_light_object(a_position, object);
}

[[nodiscard]] Result GameWorld::add_spot_light_object()
{
    GameObject object{};
    return add_spot_light_object(object);
}

[[nodiscard]] Result GameWorld::add_spot_light_object(GameObject &a_outObject)
{
    return add_spot_light_object(make_light_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_spot_light_object(const Math::float3 &a_position)
{
    GameObject object{};
    return add_spot_light_object(a_position, object);
}

[[nodiscard]] Result GameWorld::add_sprite_object()
{
    GameObject object{};
    return add_sprite_object(object);
}

[[nodiscard]] Result GameWorld::add_sprite_object(GameObject &a_outObject)
{
    return add_sprite_object(make_sprite_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_sprite_object(const Math::float3 &a_position)
{
    GameObject object{};
    return add_sprite_object(a_position, object);
}

[[nodiscard]] Result GameWorld::add_object_to_scene(SceneId a_sceneId)
{
    return add_object_to_scene(a_sceneId, make_spawn_position());
}

[[nodiscard]] Result GameWorld::add_object_to_scene(SceneId a_sceneId, GameObject &a_outObject)
{
    return add_object_to_scene(a_sceneId, make_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_object_to_scene(SceneId a_sceneId,
                                                    const Math::float3 &a_position)
{
    GameObject object{};
    return add_object_to_scene(a_sceneId, a_position, object);
}

[[nodiscard]] Result GameWorld::add_game_object_to_scene(SceneId a_sceneId)
{
    return add_game_object_to_scene(a_sceneId, make_spawn_position());
}

[[nodiscard]] Result GameWorld::add_game_object_to_scene(SceneId a_sceneId, GameObject &a_outObject)
{
    return add_game_object_to_scene(a_sceneId, make_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_game_object_to_scene(SceneId a_sceneId,
                                                         const Math::float3 &a_position)
{
    GameObject object{};
    return add_game_object_to_scene(a_sceneId, a_position, object);
}

[[nodiscard]] Result GameWorld::add_camera_object_to_scene(SceneId a_sceneId)
{
    return add_camera_object_to_scene(a_sceneId, make_camera_spawn_position());
}

[[nodiscard]] Result GameWorld::add_camera_object_to_scene(SceneId a_sceneId,
                                                           GameObject &a_outObject)
{
    return add_camera_object_to_scene(a_sceneId, make_camera_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_camera_object_to_scene(SceneId a_sceneId,
                                                           const Math::float3 &a_position)
{
    GameObject object{};
    return add_camera_object_to_scene(a_sceneId, a_position, object);
}

[[nodiscard]] Result GameWorld::add_directional_light_object_to_scene(SceneId a_sceneId)
{
    return add_directional_light_object_to_scene(a_sceneId, make_light_spawn_position());
}

[[nodiscard]] Result GameWorld::add_directional_light_object_to_scene(SceneId a_sceneId,
                                                                      GameObject &a_outObject)
{
    return add_directional_light_object_to_scene(a_sceneId, make_light_spawn_position(),
                                                 a_outObject);
}

[[nodiscard]] Result GameWorld::add_directional_light_object_to_scene(
    SceneId a_sceneId, const Math::float3 &a_position)
{
    GameObject object{};
    return add_directional_light_object_to_scene(a_sceneId, a_position, object);
}

[[nodiscard]] Result GameWorld::add_point_light_object_to_scene(SceneId a_sceneId)
{
    return add_point_light_object_to_scene(a_sceneId, make_light_spawn_position());
}

[[nodiscard]] Result GameWorld::add_point_light_object_to_scene(SceneId a_sceneId,
                                                                GameObject &a_outObject)
{
    return add_point_light_object_to_scene(a_sceneId, make_light_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_point_light_object_to_scene(SceneId a_sceneId,
                                                                const Math::float3 &a_position)
{
    GameObject object{};
    return add_point_light_object_to_scene(a_sceneId, a_position, object);
}

[[nodiscard]] Result GameWorld::add_spot_light_object_to_scene(SceneId a_sceneId)
{
    return add_spot_light_object_to_scene(a_sceneId, make_light_spawn_position());
}

[[nodiscard]] Result GameWorld::add_spot_light_object_to_scene(SceneId a_sceneId,
                                                               GameObject &a_outObject)
{
    return add_spot_light_object_to_scene(a_sceneId, make_light_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_spot_light_object_to_scene(SceneId a_sceneId,
                                                               const Math::float3 &a_position)
{
    GameObject object{};
    return add_spot_light_object_to_scene(a_sceneId, a_position, object);
}

[[nodiscard]] Result GameWorld::add_sprite_object_to_scene(SceneId a_sceneId)
{
    return add_sprite_object_to_scene(a_sceneId, make_sprite_spawn_position());
}

[[nodiscard]] Result GameWorld::add_sprite_object_to_scene(SceneId a_sceneId,
                                                           GameObject &a_outObject)
{
    return add_sprite_object_to_scene(a_sceneId, make_sprite_spawn_position(), a_outObject);
}

[[nodiscard]] Result GameWorld::add_sprite_object_to_scene(SceneId a_sceneId,
                                                           const Math::float3 &a_position)
{
    GameObject object{};
    return add_sprite_object_to_scene(a_sceneId, a_position, object);
}

[[nodiscard]] Result GameWorld::get_render_object_entity(uint32_t a_objectId,
                                                         EntityId &a_outEntityId) const noexcept
{
    return try_get_static_mesh_entity(a_objectId, a_outEntityId)
               ? Result::ok()
               : Result::fail(Code::NotFound, Severity::Error,
                              "Static mesh object id was not found.");
}

[[nodiscard]] Result GameWorld::set_main_camera(EntityId a_cameraEntityId)
{
    if (!contains_object(a_cameraEntityId) ||
        !has_component<ECS::CameraComponent>(a_cameraEntityId))
    {
        return Result::fail(Code::NotFound, Severity::Error, "Camera object was not found.");
    }

    std::vector<EntityId> cameraEntities = collect_camera_entities();
    auto targetIt = std::find(cameraEntities.begin(), cameraEntities.end(), a_cameraEntityId);
    if (targetIt == cameraEntities.end())
    {
        return Result::fail(Code::NotFound, Severity::Error, "Camera object was not found.");
    }

    for (uint32_t cameraIndex = 0; cameraIndex < cameraEntities.size(); ++cameraIndex)
    {
        ECS::CameraComponent *camera =
            get_component<ECS::CameraComponent>(cameraEntities[cameraIndex]);
        if (camera == nullptr)
        {
            continue;
        }

        camera->isMain = (cameraEntities[cameraIndex] == a_cameraEntityId);
    }

    m_mainCameraIndex = static_cast<uint32_t>(std::distance(cameraEntities.begin(), targetIt));
    return Result::ok();
}

[[nodiscard]] Result GameWorld::get_parent(EntityId a_entityId,
                                           EntityId &a_outParent) const noexcept
{
    a_outParent = k_invalidEntityId;
    if (!contains_object(a_entityId))
    {
        return Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
    }

    const BaseComponent *base = get_component<BaseComponent>(a_entityId);
    if (base == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld BaseComponent is missing.");
    }

    a_outParent = base->parent;
    return Result::ok();
}

[[nodiscard]] Result GameWorld::set_parent(EntityId a_childEntityId, EntityId a_parentEntityId,
                                           bool a_keepsWorldTransform) noexcept
{
    if (!contains_object(a_childEntityId) || !contains_object(a_parentEntityId))
    {
        return Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
    }
    if (a_childEntityId == a_parentEntityId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "GameWorld parent cannot be the child itself.");
    }
    if (is_descendant_of(a_parentEntityId, a_childEntityId))
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "GameWorld parent cycle was rejected.");
    }

    ECS::TransformComponent *childTransform = nullptr;
    ECS::TransformComponent *parentTransform = nullptr;
    Result childTransformResult = get_component(a_childEntityId, childTransform);
    Result parentTransformResult = get_component(a_parentEntityId, parentTransform);
    if (!childTransformResult || childTransform == nullptr)
    {
        return childTransformResult;
    }
    if (!parentTransformResult || parentTransform == nullptr)
    {
        return parentTransformResult;
    }

    ECS::WorldTransformComponent childWorld{};
    ECS::WorldTransformComponent parentWorld{};
    if (a_keepsWorldTransform)
    {
        std::vector<uint8_t> state(m_entityRecords.size(), 0u);
        if (!resolve_world_transform(a_childEntityId, state, childWorld) ||
            !resolve_world_transform(a_parentEntityId, state, parentWorld))
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "GameWorld world transform could not be resolved.");
        }
    }

    BaseComponent *childBase = get_component<BaseComponent>(a_childEntityId);
    if (childBase == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld BaseComponent is missing.");
    }
    childBase->parent = a_parentEntityId;

    if (a_keepsWorldTransform)
    {
        *childTransform = make_local_transform(parentWorld, childWorld);
    }

    sync_world_transforms();
    return Result::ok();
}

[[nodiscard]] Result GameWorld::detach_parent(EntityId a_childEntityId,
                                              bool a_keepsWorldTransform) noexcept
{
    if (!contains_object(a_childEntityId))
    {
        return Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
    }

    BaseComponent *childBase = get_component<BaseComponent>(a_childEntityId);
    ECS::TransformComponent *childTransform = nullptr;
    Result childTransformResult = get_component(a_childEntityId, childTransform);
    if (childBase == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld BaseComponent is missing.");
    }
    if (!childTransformResult || childTransform == nullptr)
    {
        return childTransformResult;
    }

    ECS::WorldTransformComponent childWorld{};
    if (a_keepsWorldTransform)
    {
        std::vector<uint8_t> state(m_entityRecords.size(), 0u);
        if (!resolve_world_transform(a_childEntityId, state, childWorld))
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "GameWorld world transform could not be resolved.");
        }
    }

    childBase->parent = k_invalidEntityId;
    if (a_keepsWorldTransform)
    {
        childTransform->position = childWorld.position;
        childTransform->rotation = childWorld.rotation;
        childTransform->scale = childWorld.scale;
    }

    sync_world_transforms();
    return Result::ok();
}

DrawSystem::DrawFrameState &GameWorld::draw_frame_state() noexcept
{
    return m_drawFrameState;
}

const DrawSystem::DrawFrameState &GameWorld::draw_frame_state() const noexcept
{
    return m_drawFrameState;
}

NavigationWorld &GameWorld::navigation_world() noexcept
{
    return m_navigationWorld;
}

const NavigationWorld &GameWorld::navigation_world() const noexcept
{
    return m_navigationWorld;
}

[[nodiscard]] Result GameWorld::load_navigation_mesh(const NavMeshAssetData &a_asset,
                                                     NavMeshHandle &a_outHandle) noexcept
{
    Result result = m_navigationWorld.load_nav_mesh(a_asset, a_outHandle);
    if (!result)
    {
        return result;
    }

    result = set_active_navigation_mesh(a_outHandle, a_asset);
    if (!result)
    {
        (void)m_navigationWorld.unload_nav_mesh(a_outHandle);
        a_outHandle = {};
        return result;
    }

    return Result::ok();
}

[[nodiscard]] Result GameWorld::load_navigation_mesh_from_path(const Core::IO::Path &a_path,
                                                               NavMeshHandle &a_outHandle) noexcept
{
    a_outHandle = {};
    if (m_fileSystem == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld file system is not initialized.");
    }

    Core::IO::Path navMeshPath = a_path;
    if (!navMeshPath.is_absolute())
    {
        if (m_assetRootPath.is_empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                                "GameWorld asset root path is not initialized.");
        }
        navMeshPath = Core::IO::Path::join(m_assetRootPath, navMeshPath);
    }

    NavMeshAssetData navMeshAsset{};
    Result result = NavMeshAssetSerializer::load(*m_fileSystem, navMeshPath, navMeshAsset);
    if (!result)
    {
        return result;
    }

    return load_navigation_mesh(navMeshAsset, a_outHandle);
}

[[nodiscard]] Result GameWorld::set_active_navigation_mesh(NavMeshHandle a_handle) noexcept
{
    if (!a_handle.valid())
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "Navigation mesh handle is invalid.");
    }

    m_activeNavMesh = a_handle;
    m_activeNavMeshAsset = {};
    m_hasActiveNavMeshAsset = false;
    if (m_navigationSystem != nullptr)
    {
        m_navigationSystem->set_nav_mesh(a_handle);
    }
    return Result::ok();
}

[[nodiscard]] Result GameWorld::set_active_navigation_mesh(NavMeshHandle a_handle,
                                                           const NavMeshAssetData &a_asset) noexcept
{
    Result result = set_active_navigation_mesh(a_handle);
    if (!result)
    {
        return result;
    }

    m_activeNavMeshAsset = a_asset;
    m_hasActiveNavMeshAsset = true;
    return Result::ok();
}

[[nodiscard]] NavMeshHandle GameWorld::active_navigation_mesh() const noexcept
{
    return m_activeNavMesh;
}

[[nodiscard]] Result GameWorld::set_nav_agent_destination(
    EntityId a_entityId, const Math::float3 &a_destination) noexcept
{
    ECS::NavAgentComponent *agent = get_component<ECS::NavAgentComponent>(a_entityId);
    if (agent == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Warning, "NavAgentComponent was not found.");
    }

    agent->destination = a_destination;
    agent->targetEntity = 0;
    agent->hasTarget = false;
    agent->hasDestination = true;
    agent->hasArrived = false;
    agent->hasPath = false;
    agent->hasPathFailed = false;
    agent->isOnNavMesh = false;
    agent->pathPoints.clear();
    agent->pathIndex = 0;
    return Result::ok();
}

[[nodiscard]] Result GameWorld::set_nav_agent_target(EntityId a_entityId,
                                                     EntityId a_targetEntityId) noexcept
{
    ECS::NavAgentComponent *agent = get_component<ECS::NavAgentComponent>(a_entityId);
    if (agent == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Warning, "NavAgentComponent was not found.");
    }
    if (a_targetEntityId == k_invalidEntityId || !contains_object(a_targetEntityId))
    {
        return Result::fail(Code::InvalidArgument, Severity::Warning,
                            "NavAgent target entity is invalid.");
    }

    agent->targetEntity = a_targetEntityId;
    agent->hasTarget = true;
    agent->hasDestination = true;
    agent->hasArrived = false;
    agent->hasPath = false;
    agent->hasPathFailed = false;
    agent->pathPoints.clear();
    agent->pathIndex = 0;
    return Result::ok();
}

[[nodiscard]] Result GameWorld::raycast(const GameplayRaycastDesc &a_desc,
                                        GameplayRaycastHit &a_outHit) const noexcept
{
    a_outHit = {};
    if (m_physicsSystem == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld physics system is not initialized.");
    }

    Physics::RaycastDesc raycast{};
    raycast.origin = a_desc.origin;
    raycast.direction = a_desc.direction;
    raycast.distance = a_desc.distance;
    if (a_desc.ignoredEntity != k_invalidEntityId)
    {
        const ECS::RigidBodyComponent *ignoredRigidBody =
            get_component<ECS::RigidBodyComponent>(a_desc.ignoredEntity);
        if (ignoredRigidBody != nullptr)
        {
            raycast.ignoredBody = ignoredRigidBody->body;
        }
    }

    Physics::RaycastHit hit{};
    Result result = m_physicsSystem->raycast(raycast, hit);
    if (!result)
    {
        return result;
    }

    EntityId entity = k_invalidEntityId;
    if (!find_entity_by_body(hit.body, entity))
    {
        return Result::fail(Code::NotFound, Severity::Warning,
                            "Raycast hit body is not owned by GameWorld.");
    }

    a_outHit.entity = entity;
    a_outHit.position = hit.position;
    a_outHit.normal = hit.normal;
    a_outHit.distance = hit.distance;
    return Result::ok();
}

[[nodiscard]] Result GameWorld::trigger_overlaps(
    EntityId a_entityId, std::vector<EntityId> &a_outEntities) const noexcept
{
    a_outEntities.clear();
    const ECS::TriggerVolumeComponent *trigger =
        get_component<ECS::TriggerVolumeComponent>(a_entityId);
    if (trigger == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Warning,
                            "TriggerVolumeComponent was not found.");
    }

    a_outEntities = trigger->overlappingEntities;
    return Result::ok();
}

DebugDrawBuffer &GameWorld::debug_draw() noexcept
{
    return m_debugDraw;
}

const DebugDrawBuffer &GameWorld::debug_draw() const noexcept
{
    return m_debugDraw;
}

[[nodiscard]] Result GameWorld::build_navigation_debug_geometry(
    NavMeshDebugGeometry &a_outGeometry) noexcept
{
    a_outGeometry = {};
    if (!m_activeNavMesh.valid())
    {
        return Result::fail(Code::InvalidState, Severity::Warning,
                            "Active navigation mesh is not set.");
    }

    Result result = m_navigationWorld.build_debug_geometry(m_activeNavMesh, a_outGeometry);
    if (!result)
    {
        return result;
    }

    if (m_navigationSystem != nullptr)
    {
        m_navigationSystem->append_agent_debug_geometry(a_outGeometry);
    }
    return Result::ok();
}

[[nodiscard]] const DrawSystem::DrawResources *GameWorld::draw_resources() const noexcept
{
    return m_drawResources.get();
}

[[nodiscard]] const LightingSystem::LightResources *GameWorld::light_resources() const noexcept
{
    return m_lightResources.get();
}

[[nodiscard]] const ShadowSystem::ShadowResources *GameWorld::shadow_resources() const noexcept
{
    return m_shadowResources.get();
}

[[nodiscard]] const ParticleSystem::ParticleResources *GameWorld::particle_resources()
    const noexcept
{
    return m_particleResources.get();
}

[[nodiscard]] const EffectSystem::EffectPrimitiveResources *GameWorld::effect_primitive_resources()
    const noexcept
{
    return m_effectPrimitiveResources.get();
}

LightingSystem::LightFrameState &GameWorld::light_frame_state() noexcept
{
    return m_lightFrameState;
}

ParticleSystem::ParticleFrameState &GameWorld::particle_frame_state() noexcept
{
    return m_particleFrameState;
}

const ParticleSystem::ParticleFrameState &GameWorld::particle_frame_state() const noexcept
{
    return m_particleFrameState;
}

EffectSystem::EffectPrimitiveFrameState &GameWorld::effect_primitive_frame_state() noexcept
{
    return m_effectPrimitiveFrameState;
}

const EffectSystem::EffectPrimitiveFrameState &GameWorld::effect_primitive_frame_state()
    const noexcept
{
    return m_effectPrimitiveFrameState;
}

const LightingSystem::LightFrameState &GameWorld::light_frame_state() const noexcept
{
    return m_lightFrameState;
}

ShadowSystem::ShadowFrameState &GameWorld::shadow_frame_state() noexcept
{
    return m_shadowFrameState;
}

const ShadowSystem::ShadowFrameState &GameWorld::shadow_frame_state() const noexcept
{
    return m_shadowFrameState;
}

void GameWorld::set_cpu_batching_enabled(bool a_enabled) noexcept
{
    m_isCpuBatchingEnabled = a_enabled;
}

[[nodiscard]] bool GameWorld::is_cpu_batching_enabled() const noexcept
{
    return m_isCpuBatchingEnabled;
}

[[nodiscard]] Result GameWorld::create_object(std::string_view a_name, std::string_view a_tag,
                                              bool a_isPersistent, GameObject &a_outObject)
{
    a_outObject = {};
    return capture_result([this, &a_outObject, a_name, a_tag, a_isPersistent]()
                          { a_outObject = create_object(a_name, a_tag, a_isPersistent); });
}

[[nodiscard]] Result GameWorld::create_object(std::string_view a_name, GameObject &a_outObject)
{
    return create_object(a_name, "Default", false, a_outObject);
}

[[nodiscard]] Result GameWorld::load_scene(const SceneAsset &a_asset, LoadSceneResult &a_outResult)
{
    a_outResult = {};
    Result result =
        capture_result([this, &a_outResult, &a_asset]() { a_outResult = load_scene(a_asset); });
    if (!result)
    {
        return result;
    }

    if (!a_asset.navigation_mesh_path().empty())
    {
        NavMeshHandle navMeshHandle{};
        result = load_navigation_mesh_from_path(Core::IO::Path(a_asset.navigation_mesh_path()),
                                                navMeshHandle);
        if (!result)
        {
            (void)unload_scene(a_outResult.sceneId);
            (void)execute_deferred_deletions();
            a_outResult = {};
            return result;
        }
    }

    return Result::ok();
}

[[nodiscard]] Result GameWorld::load_scene(SceneId a_sceneId, const SceneAsset &a_asset,
                                           LoadSceneResult &a_outResult)
{
    a_outResult = {};
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "GameWorld scene id is invalid.");
    }

    Result result = capture_result([this, &a_outResult, a_sceneId, &a_asset]()
                                   { a_outResult = load_scene(a_sceneId, a_asset); });
    if (!result)
    {
        return result;
    }

    if (!a_asset.navigation_mesh_path().empty())
    {
        NavMeshHandle navMeshHandle{};
        result = load_navigation_mesh_from_path(Core::IO::Path(a_asset.navigation_mesh_path()),
                                                navMeshHandle);
        if (!result)
        {
            (void)unload_scene(a_outResult.sceneId);
            (void)execute_deferred_deletions();
            a_outResult = {};
            return result;
        }
    }

    return Result::ok();
}

[[nodiscard]] Result GameWorld::request_load_scene(std::string_view a_sceneName,
                                                   SceneId &a_outSceneId)
{
    a_outSceneId = k_invalidSceneId;

    Core::IO::Path scenePath{};
    Result result = resolve_scene_path(a_sceneName, scenePath);
    if (!result)
    {
        return result;
    }

    return capture_result(
        [this, &a_outSceneId, &scenePath]()
        {
            a_outSceneId = generate_scene_id();
            m_pendingLoadedScenes.push_back(PendingSceneLoad{a_outSceneId, scenePath});
        });
}

[[nodiscard]] Result GameWorld::append_to_scene(SceneId a_sceneId,
                                                std::span<const ObjectDefinition> a_objects,
                                                LoadSceneResult &a_outResult)
{
    a_outResult = {};
    return capture_result([this, &a_outResult, a_sceneId, a_objects]()
                          { a_outResult = append_to_scene(a_sceneId, a_objects); });
}

[[nodiscard]] Result GameWorld::append_to_scene(SceneId a_sceneId,
                                                const std::vector<ObjectDefinition> &a_objects,
                                                LoadSceneResult &a_outResult)
{
    return append_to_scene(a_sceneId, std::span<const ObjectDefinition>(a_objects), a_outResult);
}

[[nodiscard]] Result GameWorld::append_object_to_scene(SceneId a_sceneId,
                                                       const ObjectDefinition &a_object,
                                                       GameObject &a_outObject)
{
    a_outObject = {};
    return capture_result([this, &a_outObject, a_sceneId, &a_object]()
                          { a_outObject = append_object_to_scene(a_sceneId, a_object); });
}

[[nodiscard]] Result GameWorld::destroy_object(EntityId a_entityId) noexcept
{
    if (!contains_object(a_entityId))
    {
        return Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
    }

    destroy_object_internal(a_entityId);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::unload_scene(SceneId a_sceneId) noexcept
{
    return unload_scene_internal(a_sceneId)
               ? Result::ok()
               : Result::fail(Code::NotFound, Severity::Warning, "GameWorld scene was not found.");
}

[[nodiscard]] Result GameWorld::request_unload_scene(SceneId a_sceneId) noexcept
{
    if (a_sceneId == k_invalidSceneId)
    {
        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "GameWorld scene id is invalid.");
    }

    const auto pendingIt = std::find_if(m_pendingLoadedScenes.begin(), m_pendingLoadedScenes.end(),
                                        [a_sceneId](const PendingSceneLoad &a_pending)
                                        { return a_pending.sceneId == a_sceneId; });
    if (pendingIt != m_pendingLoadedScenes.end())
    {
        m_pendingLoadedScenes.erase(pendingIt);
        return Result::ok();
    }

    return unload_scene(a_sceneId);
}

[[nodiscard]] Result GameWorld::execute_deferred_deletions() noexcept
{
    execute_deferred_deletions_internal();
    return Result::ok();
}

[[nodiscard]] Result GameWorld::find_object(EntityId a_entityId, GameObject &a_outObject) noexcept
{
    a_outObject = find_object(a_entityId);
    return a_outObject.is_valid()
               ? Result::ok()
               : Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
}

[[nodiscard]] Result GameWorld::contains_object(EntityId a_entityId,
                                                bool &a_outContains) const noexcept
{
    a_outContains = contains_object(a_entityId);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::contains_scene(SceneId a_sceneId,
                                               bool &a_outContains) const noexcept
{
    a_outContains = contains_scene(a_sceneId);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::get_object_tag(EntityId a_entityId, std::string &a_outTag) const
{
    a_outTag = get_object_tag(a_entityId);
    return a_outTag.empty() && !contains_object(a_entityId)
               ? Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.")
               : Result::ok();
}

[[nodiscard]] Result GameWorld::get_object_name(EntityId a_entityId, std::string &a_outName) const
{
    a_outName = get_object_name(a_entityId);
    return a_outName.empty() && !contains_object(a_entityId)
               ? Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.")
               : Result::ok();
}

[[nodiscard]] Result GameWorld::set_object_name(EntityId a_entityId, std::string_view a_name)
{
    return capture_result(
        [this, a_entityId, a_name]()
        {
            BaseComponent *base = get_component<BaseComponent>(a_entityId);
            if (base == nullptr)
            {
                throw std::runtime_error("GameWorld BaseComponent is missing.");
            }

            const std::string resolvedName = make_unique_object_name(a_name, a_entityId);
            if (base->name == resolvedName)
            {
                return;
            }

            remove_object_from_name_index(a_entityId, base->name);
            base->name = resolvedName;
            add_object_to_name_index(a_entityId, base->name);
        });
}

[[nodiscard]] Result GameWorld::set_object_tag(EntityId a_entityId, std::string_view a_tag)
{
    return capture_result(
        [this, a_entityId, a_tag]()
        {
            BaseComponent *base = get_component<BaseComponent>(a_entityId);
            if (base == nullptr)
            {
                throw std::runtime_error("GameWorld BaseComponent is missing.");
            }

            if (base->tag == a_tag)
            {
                return;
            }

            remove_object_from_tag_index(a_entityId, base->tag);
            base->tag = std::string(a_tag);
            add_object_to_tag_index(a_entityId, base->tag);
        });
}

[[nodiscard]] Result GameWorld::is_object_active(EntityId a_entityId,
                                                 bool &a_outIsActive) const noexcept
{
    a_outIsActive = contains_object(a_entityId) && m_ecs.is_entity_active(a_entityId);
    return contains_object(a_entityId)
               ? Result::ok()
               : Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
}

[[nodiscard]] Result GameWorld::capture_deleted_object(EntityId a_entityId,
                                                       DeletedObjectSnapshot &a_outSnapshot) const
{
    a_outSnapshot = {};
    if (!contains_object(a_entityId))
    {
        return Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
    }

    const BaseComponent *base = get_component<BaseComponent>(a_entityId);
    const EntityRecord *record = try_get_entity_record(a_entityId);
    if (base == nullptr || record == nullptr || !record->isAlive)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld object snapshot could not be captured.");
    }

    ObjectDefinition definition{};
    definition.localObjectId = record->sourceLocalObjectId;
    definition.isActive = m_ecs.is_entity_active(a_entityId);
    definition.isPersistent = base->isPersistent;
    definition.prototype = build_object_prototype(a_entityId, *base);

    if (base->parent != k_invalidEntityId)
    {
        const EntityRecord *parentRecord = try_get_entity_record(base->parent);
        if (parentRecord != nullptr && parentRecord->isAlive &&
            parentRecord->sourceSceneId == record->sourceSceneId &&
            parentRecord->sourceLocalObjectId != k_invalidLocalObjectId)
        {
            definition.parentLocalObjectId = parentRecord->sourceLocalObjectId;
        }
    }

    a_outSnapshot.sourceSceneId = record->sourceSceneId;
    a_outSnapshot.definition = std::move(definition);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::restore_deleted_object(const DeletedObjectSnapshot &a_snapshot,
                                                       EntityId &a_outObjectId)
{
    a_outObjectId = k_invalidEntityId;

    GameObject object{};
    if (a_snapshot.sourceSceneId != k_invalidSceneId)
    {
        Result appendResult =
            append_object_to_scene(a_snapshot.sourceSceneId, a_snapshot.definition, object);
        if (!appendResult)
        {
            return appendResult;
        }
    }
    else
    {
        object = instantiate_object(a_snapshot.definition);
        if (!object.is_valid())
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                                "GameWorld object restore failed.");
        }
    }

    a_outObjectId = object.entity_id();
    return Result::ok();
}

[[nodiscard]] Result GameWorld::set_object_active(EntityId a_entityId, bool a_isActive)
{
    return capture_result(
        [this, a_entityId, a_isActive]()
        {
            BaseComponent *base = get_component<BaseComponent>(a_entityId);
            if (base == nullptr)
            {
                throw std::runtime_error("GameWorld BaseComponent is missing.");
            }

            if (base->isActiveSelf == a_isActive &&
                m_ecs.is_entity_active(a_entityId) == a_isActive)
            {
                return;
            }

            base->isActiveSelf = a_isActive;
            m_ecs.set_entity_active(a_entityId, a_isActive);
        });
}

[[nodiscard]] Result GameWorld::is_object_persistent(EntityId a_entityId,
                                                     bool &a_outIsPersistent) const noexcept
{
    a_outIsPersistent = is_object_persistent(a_entityId);
    return contains_object(a_entityId)
               ? Result::ok()
               : Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
}

[[nodiscard]] Result GameWorld::set_object_persistent(EntityId a_entityId, bool a_isPersistent)
{
    return capture_result(
        [this, a_entityId, a_isPersistent]()
        {
            BaseComponent *base = get_component<BaseComponent>(a_entityId);
            if (base == nullptr)
            {
                throw std::runtime_error("GameWorld BaseComponent is missing.");
            }

            if (base->isPersistent == a_isPersistent)
            {
                return;
            }

            base->isPersistent = a_isPersistent;
            base->owningSceneId = a_isPersistent ? k_invalidSceneId : source_scene_id(a_entityId);
        });
}

[[nodiscard]] Result GameWorld::is_alive(EntityId a_entityId, Generation a_generation,
                                         bool &a_outIsAlive) const noexcept
{
    a_outIsAlive = is_alive(a_entityId, a_generation);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::source_scene_id(EntityId a_entityId,
                                                SceneId &a_outSceneId) const noexcept
{
    a_outSceneId = source_scene_id(a_entityId);
    return contains_object(a_entityId)
               ? Result::ok()
               : Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
}

[[nodiscard]] Result GameWorld::object_count(size_t &a_outCount) const noexcept
{
    a_outCount = m_liveObjectCount;
    return Result::ok();
}

[[nodiscard]] Result GameWorld::set_rigid_body_linear_velocity(EntityId a_entityId,
                                                               Math::float3 a_velocity) noexcept
{
    ECS::RigidBodyComponent *rigidBody = nullptr;
    Result result = get_component<ECS::RigidBodyComponent>(a_entityId, rigidBody);
    if (!result || rigidBody == nullptr)
    {
        return result;
    }

    rigidBody->linearVelocity = a_velocity;
    if (m_physicsSystem == nullptr || !rigidBody->body.valid())
    {
        return Result::ok();
    }

    return m_physicsSystem->set_linear_velocity(rigidBody->body, a_velocity,
                                                Physics::BodyActivation::Activate);
}

[[nodiscard]] Result GameWorld::get_rigid_body_linear_velocity(
    EntityId a_entityId, Math::float3 &a_outVelocity) const noexcept
{
    a_outVelocity = Math::float3::zero();
    const ECS::RigidBodyComponent *rigidBody = get_component<ECS::RigidBodyComponent>(a_entityId);
    if (rigidBody == nullptr)
    {
        return Result::fail(Code::NotFound, Severity::Error, "RigidBodyComponent was not found.");
    }

    if (m_physicsSystem == nullptr || !rigidBody->body.valid())
    {
        a_outVelocity = rigidBody->linearVelocity;
        return Result::ok();
    }

    return m_physicsSystem->get_linear_velocity(rigidBody->body, a_outVelocity);
}

[[nodiscard]] Result GameWorld::add_rigid_body_force(EntityId a_entityId,
                                                     Math::float3 a_force) noexcept
{
    ECS::RigidBodyComponent *rigidBody = nullptr;
    Result result = get_component<ECS::RigidBodyComponent>(a_entityId, rigidBody);
    if (!result || rigidBody == nullptr)
    {
        return result;
    }
    if (m_physicsSystem == nullptr || !rigidBody->body.valid())
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "RigidBody physics body is not created.");
    }

    return m_physicsSystem->add_force(rigidBody->body, a_force, Physics::BodyActivation::Activate);
}

[[nodiscard]] Result GameWorld::add_rigid_body_impulse(EntityId a_entityId,
                                                       Math::float3 a_impulse) noexcept
{
    ECS::RigidBodyComponent *rigidBody = nullptr;
    Result result = get_component<ECS::RigidBodyComponent>(a_entityId, rigidBody);
    if (!result || rigidBody == nullptr)
    {
        return result;
    }
    if (m_physicsSystem == nullptr || !rigidBody->body.valid())
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "RigidBody physics body is not created.");
    }

    return m_physicsSystem->add_impulse(rigidBody->body, a_impulse,
                                        Physics::BodyActivation::Activate);
}

[[nodiscard]] Result GameWorld::set_character_move_velocity(EntityId a_entityId,
                                                            Math::float3 a_velocity) noexcept
{
    ECS::CharacterControllerComponent *controller = nullptr;
    Result result = get_component<ECS::CharacterControllerComponent>(a_entityId, controller);
    if (!result || controller == nullptr)
    {
        return result;
    }

    controller->moveVelocity = a_velocity;
    return Result::ok();
}

[[nodiscard]] Result GameWorld::request_character_jump(EntityId a_entityId) noexcept
{
    ECS::CharacterControllerComponent *controller = nullptr;
    Result result = get_component<ECS::CharacterControllerComponent>(a_entityId, controller);
    if (!result || controller == nullptr)
    {
        return result;
    }

    controller->jumpRequested = true;
    return Result::ok();
}

[[nodiscard]] Result GameWorld::scene_count(size_t &a_outCount) const noexcept
{
    a_outCount = m_scenes.size();
    return Result::ok();
}

[[nodiscard]] Result GameWorld::clear() noexcept
{
    // まだ実体化していない Scene は破棄対象の Entity を持たないため予約だけ消す
    m_pendingLoadedScenes.clear();

    if (m_activeNavMesh.valid())
    {
        const Result navResult = m_navigationWorld.unload_nav_mesh(m_activeNavMesh);
        if (!navResult)
        {
            return navResult;
        }
    }

    m_activeNavMesh = {};
    m_activeNavMeshAsset = {};
    m_hasActiveNavMeshAsset = false;
    if (m_navigationSystem != nullptr)
    {
        m_navigationSystem->set_nav_mesh(m_activeNavMesh);
    }

    // 公開 API を使って削除予約を積み、最後にまとめて flush する
    std::vector<SceneId> sceneIds{};
    sceneIds.reserve(m_scenes.size());
    for (const auto &[sceneId, _] : m_scenes)
    {
        sceneIds.push_back(sceneId);
    }

    for (const SceneId sceneId : sceneIds)
    {
        const Result unloadResult = unload_scene(sceneId);
        if (!unloadResult)
        {
            return unloadResult;
        }
    }

    for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
    {
        if (contains_object(entity))
        {
            const Result destroyResult = destroy_object(entity);
            if (!destroyResult)
            {
                return destroyResult;
            }
        }
    }

    // clear() 完了時点ではワールドが空になっていることを保証する
    Result clearResult = execute_deferred_deletions();
    if (!clearResult)
    {
        return clearResult;
    }

    m_ownedSceneAssets.clear();
    return Result::ok();
}

[[nodiscard]] Result GameWorld::find_objects_by_tag(std::string_view a_tag,
                                                    std::vector<GameObject> &a_outObjects)
{
    a_outObjects = find_objects_by_tag(a_tag);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::find_objects_by_name(std::string_view a_name,
                                                     std::vector<GameObject> &a_outObjects)
{
    a_outObjects = find_objects_by_name(a_name);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::find_object_by_name(std::string_view a_name,
                                                    GameObject &a_outObject)
{
    a_outObject = find_object_by_name(a_name);
    return a_outObject.is_valid()
               ? Result::ok()
               : Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
}

[[nodiscard]] Result GameWorld::destroy_object_by_name(std::string_view a_name) noexcept
{
    const GameObject object = find_object_by_name(a_name);
    if (!object.is_valid())
    {
        return Result::fail(Code::NotFound, Severity::Warning, "GameWorld object was not found.");
    }

    return destroy_object_internal(object.entity_id()), Result::ok();
}

[[nodiscard]] Result GameWorld::destroy_objects_by_name(std::string_view a_name,
                                                        size_t &a_outCount) noexcept
{
    const std::vector<GameObject> objects = find_objects_by_name(a_name);
    for (const GameObject &object : objects)
    {
        destroy_object_internal(object.entity_id());
    }

    a_outCount = objects.size();
    return Result::ok();
}

[[nodiscard]] Result GameWorld::find_objects_by_name_series(std::string_view a_baseName,
                                                            std::vector<GameObject> &a_outObjects)
{
    a_outObjects = find_objects_by_name_series(a_baseName);
    return Result::ok();
}

[[nodiscard]] Result GameWorld::destroy_objects_by_name_series(std::string_view a_baseName,
                                                               size_t &a_outCount) noexcept
{
    const std::vector<GameObject> objects = find_objects_by_name_series(a_baseName);
    for (const GameObject &object : objects)
    {
        destroy_object_internal(object.entity_id());
    }

    a_outCount = objects.size();
    return Result::ok();
}

[[nodiscard]] Result GameWorld::destroy_objects_by_tag(std::string_view a_tag,
                                                       size_t &a_outCount) noexcept
{
    const std::vector<GameObject> objects = find_objects_by_tag(a_tag);
    for (const GameObject &object : objects)
    {
        destroy_object_internal(object.entity_id());
    }

    a_outCount = objects.size();
    return Result::ok();
}

[[nodiscard]] Math::float3 GameWorld::make_spawn_position() const noexcept
{
    const size_t objectIndex = count_active_static_mesh_objects();
    const uint32_t column = static_cast<uint32_t>(objectIndex % 3u);
    const uint32_t row = static_cast<uint32_t>(objectIndex / 3u);

    return Math::float3{(static_cast<float>(column) - 1.0f) * 2.0f, 0.0f,
                        static_cast<float>(row) * 2.5f};
}

[[nodiscard]] Math::float3 GameWorld::make_camera_spawn_position() const noexcept
{
    return Math::float3(0.0f, 0.0f, -6.0f);
}

[[nodiscard]] Math::float3 GameWorld::make_sprite_spawn_position() const noexcept
{
    if (!m_drawFrameState.frameStates.empty())
    {
        const DrawSystem::DrawFrameData &frameState = m_drawFrameState.frameStates.front();
        return Math::float3{static_cast<float>(frameState.renderWidth) * 0.5f,
                            static_cast<float>(frameState.renderHeight) * 0.5f, 0.0f};
    }

    return Math::float3(320.0f, 180.0f, 0.0f);
}

[[nodiscard]] Math::float3 GameWorld::make_light_spawn_position() const noexcept
{
    return Math::float3(0.0f, 3.0f, -4.0f);
}

[[nodiscard]] Math::float3 GameWorld::multiply_components(const Math::float3 &a_left,
                                                          const Math::float3 &a_right) noexcept
{
    return Math::float3(a_left.x * a_right.x, a_left.y * a_right.y, a_left.z * a_right.z);
}

[[nodiscard]] Math::float3 GameWorld::divide_components_safe(const Math::float3 &a_left,
                                                             const Math::float3 &a_right) noexcept
{
    const auto divide = [](float a_value, float a_divisor) noexcept
    { return a_divisor != 0.0f ? a_value / a_divisor : a_value; };
    return Math::float3(divide(a_left.x, a_right.x), divide(a_left.y, a_right.y),
                        divide(a_left.z, a_right.z));
}

[[nodiscard]] Math::float3 GameWorld::rotate_vector(const Math::Quaternion &a_rotation,
                                                    const Math::float3 &a_value) noexcept
{
    const Math::Quaternion rotation = Math::Quaternion::normalize(a_rotation);
    const Math::Quaternion vector(a_value.x, a_value.y, a_value.z, 0.0f);
    const Math::Quaternion result = rotation * vector * Math::Quaternion::inverse(rotation);
    return Math::float3(result.x, result.y, result.z);
}

[[nodiscard]] ECS::WorldTransformComponent GameWorld::compose_world_transform(
    const ECS::WorldTransformComponent &a_parent, const ECS::TransformComponent &a_local) noexcept
{
    ECS::WorldTransformComponent world{};
    world.scale = multiply_components(a_parent.scale, a_local.scale);
    world.rotation = Math::Quaternion::normalize(a_parent.rotation * a_local.rotation);
    const Math::float3 scaledLocalPosition = multiply_components(a_local.position, a_parent.scale);
    world.position = a_parent.position + rotate_vector(a_parent.rotation, scaledLocalPosition);
    return world;
}

[[nodiscard]] ECS::TransformComponent GameWorld::make_local_transform(
    const ECS::WorldTransformComponent &a_parent,
    const ECS::WorldTransformComponent &a_world) noexcept
{
    ECS::TransformComponent local{};
    const Math::Quaternion inverseParentRotation = Math::Quaternion::inverse(a_parent.rotation);
    local.position = divide_components_safe(
        rotate_vector(inverseParentRotation, a_world.position - a_parent.position), a_parent.scale);
    local.rotation = Math::Quaternion::normalize(inverseParentRotation * a_world.rotation);
    local.scale = divide_components_safe(a_world.scale, a_parent.scale);
    return local;
}

[[nodiscard]] bool GameWorld::is_descendant_of(EntityId a_entityId,
                                               EntityId a_potentialAncestorId) const noexcept
{
    EntityId current = a_entityId;
    std::unordered_set<EntityId> visited{};
    while (current != k_invalidEntityId)
    {
        if (current == a_potentialAncestorId)
        {
            return true;
        }
        if (!visited.insert(current).second)
        {
            return true;
        }

        const BaseComponent *base = get_component<BaseComponent>(current);
        current = base != nullptr ? base->parent : k_invalidEntityId;
    }

    return false;
}

[[nodiscard]] bool GameWorld::resolve_world_transform(
    EntityId a_entityId, std::vector<uint8_t> &a_state,
    ECS::WorldTransformComponent &a_outWorld) noexcept
{
    if (!contains_object(a_entityId) || static_cast<size_t>(a_entityId) >= a_state.size())
    {
        return false;
    }

    uint8_t &state = a_state[static_cast<size_t>(a_entityId)];
    if (state == 2u)
    {
        const ECS::WorldTransformComponent *world =
            get_component<ECS::WorldTransformComponent>(a_entityId);
        if (world == nullptr)
        {
            return false;
        }
        a_outWorld = *world;
        return true;
    }
    if (state == 1u)
    {
        return false;
    }

    ECS::TransformComponent *local = get_component<ECS::TransformComponent>(a_entityId);
    if (local == nullptr)
    {
        return false;
    }

    ECS::WorldTransformComponent *world = get_component<ECS::WorldTransformComponent>(a_entityId);
    if (world == nullptr)
    {
        Result addWorldResult = add_component<ECS::WorldTransformComponent>(a_entityId, world);
        if (!addWorldResult || world == nullptr)
        {
            return false;
        }
    }

    if (world == nullptr)
    {
        return false;
    }

    state = 1u;
    const BaseComponent *base = get_component<BaseComponent>(a_entityId);
    if (base != nullptr && base->parent != k_invalidEntityId && contains_object(base->parent))
    {
        ECS::WorldTransformComponent parentWorld{};
        if (!resolve_world_transform(base->parent, a_state, parentWorld))
        {
            return false;
        }
        *world = compose_world_transform(parentWorld, *local);
    }
    else
    {
        world->position = local->position;
        world->rotation = local->rotation;
        world->scale = local->scale;
    }

    state = 2u;
    a_outWorld = *world;
    return true;
}

void GameWorld::sync_world_transforms() noexcept
{
    std::vector<uint8_t> state(m_entityRecords.size(), 0u);
    for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
    {
        if (!contains_object(entity) || get_component<ECS::TransformComponent>(entity) == nullptr)
        {
            continue;
        }

        ECS::WorldTransformComponent world{};
        (void)resolve_world_transform(entity, state, world);
    }
}

void GameWorld::sync_draw_frame_state(uint32_t a_bufferIndex, uint32_t a_renderWidth,
                                      uint32_t a_renderHeight) noexcept
{
    if (a_bufferIndex >= m_drawFrameState.frameStates.size())
    {
        return;
    }

    DrawSystem::DrawFrameData &frameState = m_drawFrameState.frame_state(a_bufferIndex);
    frameState.objectCount = 0;
    frameState.spriteCount = 0;
    frameState.cpuIndexedDraws.clear();
    frameState.transparentCpuIndexedDraws.clear();
    frameState.cpuShadowCasters.clear();
    frameState.renderWidth = a_renderWidth;
    frameState.renderHeight = a_renderHeight;
    frameState.useCpuBatching = m_isCpuBatchingEnabled;

    if (a_bufferIndex < m_particleFrameState.frameStates.size())
    {
        ParticleSystem::ParticleFrameData &particleFrameState =
            m_particleFrameState.frame_state(a_bufferIndex);
        particleFrameState.frame.emitterCount = 0;
        particleFrameState.frame.particleCount = 0;
    }

    if (a_bufferIndex < m_effectPrimitiveFrameState.frameStates.size())
    {
        EffectSystem::EffectPrimitiveFrameData &effectFrameState =
            m_effectPrimitiveFrameState.frame_state(a_bufferIndex);
        effectFrameState.frame.spriteCount = 0;
        effectFrameState.frame.ribbonCount = 0;
    }
}

void GameWorld::animate_static_mesh_objects(float a_deltaTime)
{
    std::vector<EntityId> entities = collect_active_static_mesh_entities();
    for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
    {
        ECS::TransformComponent *transform =
            get_component<ECS::TransformComponent>(entities[entityIndex]);
        if (transform == nullptr)
        {
            continue;
        }

        switch (entityIndex)
        {
        case 0:
        {
            Math::float3 rotation = Math::quaternion_to_euler_xyz(transform->rotation);
            rotation.y += a_deltaTime * 1.25f;
            transform->rotation = Math::quaternion_from_euler_xyz(rotation);
            break;
        }
        case 1:
        {
            Math::float3 rotation = Math::quaternion_to_euler_xyz(transform->rotation);
            rotation.x += a_deltaTime * 0.75f;
            transform->rotation = Math::quaternion_from_euler_xyz(rotation);
            break;
        }
        case 2:
        {
            Math::float3 rotation = Math::quaternion_to_euler_xyz(transform->rotation);
            rotation.y -= a_deltaTime * 1.0f;
            transform->rotation = Math::quaternion_from_euler_xyz(rotation);
            break;
        }
        default:
        {
            Math::float3 rotation = Math::quaternion_to_euler_xyz(transform->rotation);
            rotation.y += a_deltaTime * 0.5f;
            transform->rotation = Math::quaternion_from_euler_xyz(rotation);
            break;
        }
        }
    }
}

[[nodiscard]] std::vector<EntityId> GameWorld::collect_active_static_mesh_entities() const
{
    std::vector<EntityId> entities{};
    entities.reserve(m_liveObjectCount);
    for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
    {
        if (!contains_object(entity) || !m_ecs.is_entity_active(entity))
        {
            continue;
        }

        const BaseComponent *base = get_component<BaseComponent>(entity);
        const ECS::TransformComponent *transform = get_component<ECS::TransformComponent>(entity);
        const ECS::MeshFilterComponent *meshFilter =
            get_component<ECS::MeshFilterComponent>(entity);
        const ECS::StaticMeshRendererComponent *renderer =
            get_component<ECS::StaticMeshRendererComponent>(entity);
        if (base == nullptr || transform == nullptr || meshFilter == nullptr || renderer == nullptr)
        {
            continue;
        }
        if (!base->isActiveSelf || !renderer->visible || meshFilter->meshId == ECS::k_invalidMeshId)
        {
            continue;
        }

        entities.push_back(entity);
    }

    return entities;
}

[[nodiscard]] std::vector<EntityId> GameWorld::collect_camera_entities() const
{
    std::vector<EntityId> entities{};
    entities.reserve(m_liveObjectCount);
    for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
    {
        if (!contains_object(entity) || !has_component<ECS::CameraComponent>(entity))
        {
            continue;
        }

        entities.push_back(entity);
    }

    return entities;
}

[[nodiscard]] size_t GameWorld::count_active_static_mesh_objects() const
{
    return collect_active_static_mesh_entities().size();
}

[[nodiscard]] bool GameWorld::try_get_static_mesh_entity(uint32_t a_objectId,
                                                         EntityId &a_outEntityId) const noexcept
{
    a_outEntityId = k_invalidEntityId;
    for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
    {
        if (!contains_object(entity) || !m_ecs.is_entity_active(entity))
        {
            continue;
        }

        const ECS::RenderableInfoComponent *renderableInfo =
            get_component<ECS::RenderableInfoComponent>(entity);
        if (renderableInfo == nullptr || renderableInfo->objectId != a_objectId)
        {
            continue;
        }

        a_outEntityId = entity;
        return true;
    }

    return false;
}

[[nodiscard]] Result GameWorld::map_exception_message(std::string_view a_message) noexcept
{
    if (a_message == "GameWorld BaseComponent is missing." ||
        a_message == "GameWorld BaseComponent is missing while resolving parent.")
    {
        return Result::fail(Code::InternalError, Severity::Error, a_message);
    }
    if (a_message == "GameWorld object is not alive.")
    {
        return Result::fail(Code::InvalidState, Severity::Warning, a_message);
    }
    if (a_message == "GameWorld failed to add component." ||
        a_message == "GameWorld failed to initialize BaseComponent.")
    {
        return Result::fail(Code::CreateFailed, Severity::Error, a_message);
    }
    if (a_message == "GameWorld scene id overflow.")
    {
        return Result::fail(Code::InvalidState, Severity::Error, a_message);
    }
    if (a_message == "GameWorld internal error: entity slot is already alive." ||
        a_message == "GameWorld parentLocalObjectId could not be resolved.")
    {
        return Result::fail(Code::InternalError, Severity::Error, a_message);
    }
    if (a_message == "GameWorld scene was not found.")
    {
        return Result::fail(Code::NotFound, Severity::Warning, a_message);
    }
    if (a_message == "GameWorld scene is pending unload.")
    {
        return Result::fail(Code::InvalidState, Severity::Warning, a_message);
    }
    if (a_message == "GameWorld localObjectId is duplicated in scene.")
    {
        return Result::fail(Code::InvalidArgument, Severity::Warning, a_message);
    }
    if (a_message == "GameWorld scene id is invalid." ||
        a_message == "GameWorld scene id is duplicated.")
    {
        return Result::fail(Code::InvalidArgument, Severity::Warning, a_message);
    }

    return Result::fail(Code::UnknownError, Severity::Error, a_message);
}

[[nodiscard]] GameObject GameWorld::create_object(std::string_view a_name, std::string_view a_tag,
                                                  bool a_isPersistent)
{
    // Scene に属さない単体の GameObject を生成する
    const EntityId entity = create_entity_record(k_invalidSceneId, k_invalidLocalObjectId);
    initialize_base_component(entity, a_name, a_tag, k_invalidSceneId, k_invalidEntityId, true,
                              a_isPersistent);
    return make_handle(entity);
}

[[nodiscard]] GameObject GameWorld::instantiate_object(const ObjectDefinition &a_object)
{
    const EntityId entity = create_entity_record(k_invalidSceneId, k_invalidLocalObjectId);
    a_object.prototype.restore_components_into(entity, m_ecs);
    initialize_base_component(entity, a_object.name(), a_object.tag(), k_invalidSceneId,
                              k_invalidEntityId, a_object.isActive, a_object.isPersistent);
    return make_handle(entity);
}

[[nodiscard]] GameWorld::LoadSceneResult GameWorld::load_scene(const SceneAsset &a_asset)
{
    // SceneId の採番は GameWorld が一元管理し、SceneAsset には実行時 ID を持たせない
    const SceneId sceneId = generate_scene_id();
    return load_scene(sceneId, a_asset);
}

[[nodiscard]] GameWorld::LoadSceneResult GameWorld::load_scene(SceneId a_sceneId,
                                                               const SceneAsset &a_asset)
{
    if (a_sceneId == k_invalidSceneId)
    {
        throw std::runtime_error("GameWorld scene id is invalid.");
    }
    if (m_scenes.contains(a_sceneId))
    {
        throw std::runtime_error("GameWorld scene id is duplicated.");
    }
    if (a_sceneId >= m_nextSceneId)
    {
        m_nextSceneId = a_sceneId + 1;
        if (m_nextSceneId == 0)
        {
            throw std::overflow_error("GameWorld scene id overflow.");
        }
    }

    SceneInstance scene{};
    scene.sceneId = a_sceneId;
    scene.asset = &a_asset;
    scene.isLoaded = true;
    scene.isActive = true;

    // SceneInstance を先に登録し、Object 実体化中の参照解決で同じ Scene を参照できるようにする
    m_scenes.emplace(a_sceneId, std::move(scene));

    try
    {
        return instantiate_into_scene(a_sceneId, a_asset.objects(), &a_asset);
    }
    catch (...)
    {
        auto it = m_scenes.find(a_sceneId);
        if (it != m_scenes.end())
        {
            const std::vector<EntityId> created = it->second.entities;
            for (const EntityId entity : created)
            {
                destroy_object_immediately(entity);
            }
            m_scenes.erase(it);
        }
        throw;
    }
}

[[nodiscard]] GameWorld::LoadSceneResult GameWorld::append_to_scene(
    SceneId a_sceneId, std::span<const ObjectDefinition> a_objects)
{
    return instantiate_into_scene(a_sceneId, a_objects, nullptr);
}

[[nodiscard]] GameObject GameWorld::append_object_to_scene(SceneId a_sceneId,
                                                           const ObjectDefinition &a_object)
{
    const std::array<ObjectDefinition, 1> objects = {a_object};
    LoadSceneResult result = append_to_scene(a_sceneId, objects);
    if (result.objects.empty())
    {
        return {};
    }

    return result.objects.front();
}

void GameWorld::destroy_object_internal(EntityId a_entityId) noexcept
{
    EntityRecord *record = try_get_entity_record(a_entityId);
    if (record == nullptr || !record->isAlive || record->isPendingDestroy)
    {
        return;
    }

    // 実際の削除は execute_deferred_deletions() が呼ばれるまで遅延させる
    record->isPendingDestroy = true;
    m_pendingDestroyedEntities.push_back(a_entityId);
}

[[nodiscard]] bool GameWorld::unload_scene_internal(SceneId a_sceneId) noexcept
{
    auto sceneIt = m_scenes.find(a_sceneId);
    if (sceneIt == m_scenes.end() || sceneIt->second.isPendingUnload)
    {
        return false;
    }

    // Scene の破棄も遅延させ、呼び出し側が flush
    // のタイミングを制御できるようにする
    sceneIt->second.isPendingUnload = true;
    m_pendingUnloadedScenes.push_back(a_sceneId);
    return true;
}

void GameWorld::execute_deferred_deletions_internal() noexcept
{
    // Scene のアンロードでは非永続 Object がまとめて消えるため、先に Scene
    // 側を処理する
    std::vector<SceneId> pendingScenes{};
    pendingScenes.swap(m_pendingUnloadedScenes);
    for (const SceneId sceneId : pendingScenes)
    {
        (void)unload_scene_immediately(sceneId);
    }

    // 続いて、単体で予約されていた Object の削除を処理する
    std::vector<EntityId> pendingEntities{};
    pendingEntities.swap(m_pendingDestroyedEntities);
    for (const EntityId entity : pendingEntities)
    {
        destroy_object_immediately(entity);
    }
}

[[nodiscard]] Result GameWorld::execute_deferred_scene_loads()
{
    std::vector<PendingSceneLoad> pendingScenes{};
    pendingScenes.swap(m_pendingLoadedScenes);

    for (const PendingSceneLoad &pendingScene : pendingScenes)
    {
        // ファイル由来 SceneAsset は SceneInstance.asset の参照先として World が所有する
        auto sceneAsset = std::make_unique<SceneAsset>();
        SceneSerializer::LoadOptions loadOptions{};
        loadOptions.assetManager = m_assetManager;
        Result result = SceneSerializer::load_scene_asset(*m_fileSystem, pendingScene.path,
                                                          *sceneAsset, loadOptions);
        if (!result)
        {
            return result;
        }

        LoadSceneResult loadResult{};
        result = load_scene(pendingScene.sceneId, *sceneAsset, loadResult);
        if (!result)
        {
            return result;
        }

        m_ownedSceneAssets[pendingScene.sceneId] = std::move(sceneAsset);
    }

    return Result::ok();
}

[[nodiscard]] Result GameWorld::resolve_scene_path(std::string_view a_sceneName,
                                                   Core::IO::Path &a_outPath) const noexcept
{
    a_outPath = {};
    if (a_sceneName.empty())
    {
        return Result::fail(Code::InvalidArgument, Severity::Error, "Scene name is empty.");
    }
    if (m_fileSystem == nullptr)
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld file system is not initialized.");
    }
    if (m_assetRootPath.is_empty())
    {
        return Result::fail(Code::InvalidState, Severity::Error,
                            "GameWorld asset root path is not configured.");
    }

    std::string sceneText(a_sceneName);
    const bool hasDirectory =
        sceneText.find('/') != std::string::npos || sceneText.find('\\') != std::string::npos;
    Core::IO::Path scenePath(sceneText);
    if (scenePath.extension().empty())
    {
        sceneText += ".cuescene";
        scenePath = Core::IO::Path(sceneText);
    }

    if (!scenePath.is_absolute())
    {
        scenePath =
            hasDirectory
                ? Core::IO::Path::join(m_assetRootPath, scenePath)
                : Core::IO::Path::join(
                      Core::IO::Path::join(m_assetRootPath, Core::IO::Path("Scenes")), scenePath);
    }

    scenePath = scenePath.normalize();
    bool exists = false;
    Result result = m_fileSystem->exists(scenePath, &exists);
    if (!result)
    {
        return result;
    }
    if (!exists)
    {
        return Result::fail(Code::NotFound, Severity::Warning, "Scene file was not found.");
    }

    a_outPath = scenePath;
    return Result::ok();
}

[[nodiscard]] GameObject GameWorld::find_object(EntityId a_entityId) noexcept
{
    if (!contains_object(a_entityId))
    {
        return {};
    }

    return make_handle(a_entityId);
}

[[nodiscard]] bool GameWorld::contains_object(EntityId a_entityId) const noexcept
{
    const EntityRecord *record = try_get_entity_record(a_entityId);
    return record != nullptr && record->isAlive;
}

[[nodiscard]] bool GameWorld::contains_scene(SceneId a_sceneId) const noexcept
{
    return m_scenes.find(a_sceneId) != m_scenes.end();
}

void GameWorld::localize_script_entity_references(ECS::ScriptComponent &a_script,
                                                  EntityId a_sourceEntityId) const noexcept
{
    const SceneId sourceSceneId = source_scene_id(a_sourceEntityId);
    const auto localizeField = [this, sourceSceneId](ECS::ScriptFieldValue &a_field) noexcept
    {
        if (a_field.type != ECS::ScriptFieldType::EntityRef ||
            a_field.entityValue == k_invalidEntityId)
        {
            return;
        }

        const EntityRecord *record = try_get_entity_record(a_field.entityValue);
        if (record == nullptr || !record->isAlive || record->sourceSceneId != sourceSceneId ||
            record->sourceLocalObjectId == k_invalidLocalObjectId)
        {
            return;
        }

        a_field.entityValue = static_cast<EntityId>(record->sourceLocalObjectId);
    };

    for (ECS::ScriptFieldValue &field : a_script.serializedFieldValues)
    {
        localizeField(field);
    }
    for (ECS::ScriptFieldValue &field : a_script.transientFieldValues)
    {
        localizeField(field);
    }
}

[[nodiscard]] EntityId GameWorld::localize_entity_reference(
    EntityId a_entityValue, EntityId a_sourceEntityId) const noexcept
{
    if (a_entityValue == k_invalidEntityId)
    {
        return a_entityValue;
    }

    const SceneId sourceSceneId = source_scene_id(a_sourceEntityId);
    const EntityRecord *record = try_get_entity_record(a_entityValue);
    if (record == nullptr || !record->isAlive || record->sourceSceneId != sourceSceneId ||
        record->sourceLocalObjectId == k_invalidLocalObjectId)
    {
        return a_entityValue;
    }

    return static_cast<EntityId>(record->sourceLocalObjectId);
}

void GameWorld::resolve_script_entity_references(
    EntityId a_entityId, const SceneInstance &a_scene,
    const std::unordered_map<LocalObjectId, EntityId> &a_newLocalObjectToEntity) noexcept
{
    ECS::ScriptComponent *script = get_component<ECS::ScriptComponent>(a_entityId);
    if (script == nullptr)
    {
        return;
    }

    const auto resolveField =
        [&a_scene, &a_newLocalObjectToEntity](ECS::ScriptFieldValue &a_field) noexcept
    {
        if (a_field.type != ECS::ScriptFieldType::EntityRef ||
            a_field.entityValue == k_invalidEntityId)
        {
            return;
        }

        const LocalObjectId localObjectId = static_cast<LocalObjectId>(a_field.entityValue);
        if (const auto newIt = a_newLocalObjectToEntity.find(localObjectId);
            newIt != a_newLocalObjectToEntity.end())
        {
            a_field.entityValue = newIt->second;
            return;
        }

        if (const auto sceneIt = a_scene.localObjectToEntity.find(localObjectId);
            sceneIt != a_scene.localObjectToEntity.end())
        {
            a_field.entityValue = sceneIt->second;
        }
    };

    for (ECS::ScriptFieldValue &field : script->serializedFieldValues)
    {
        resolveField(field);
    }
    for (ECS::ScriptFieldValue &field : script->transientFieldValues)
    {
        resolveField(field);
    }
}

void GameWorld::resolve_component_entity_references(
    EntityId a_entityId, const SceneInstance &a_scene,
    const std::unordered_map<LocalObjectId, EntityId> &a_newLocalObjectToEntity) noexcept
{
    const auto resolveEntity =
        [&a_scene, &a_newLocalObjectToEntity](EntityId a_entityValue) noexcept -> EntityId
    {
        if (a_entityValue == k_invalidEntityId)
        {
            return a_entityValue;
        }

        const LocalObjectId localObjectId = static_cast<LocalObjectId>(a_entityValue);
        if (const auto newIt = a_newLocalObjectToEntity.find(localObjectId);
            newIt != a_newLocalObjectToEntity.end())
        {
            return newIt->second;
        }

        if (const auto sceneIt = a_scene.localObjectToEntity.find(localObjectId);
            sceneIt != a_scene.localObjectToEntity.end())
        {
            return sceneIt->second;
        }

        return a_entityValue;
    };

    if (ECS::FirstPersonCameraControllerComponent *controller =
            get_component<ECS::FirstPersonCameraControllerComponent>(a_entityId);
        controller != nullptr)
    {
        controller->targetEntity = resolveEntity(controller->targetEntity);
    }

    if (ECS::DemoEnemyComponent *demoEnemy = get_component<ECS::DemoEnemyComponent>(a_entityId);
        demoEnemy != nullptr)
    {
        demoEnemy->targetEntity = resolveEntity(demoEnemy->targetEntity);
    }

    if (ECS::NavAgentComponent *navAgent = get_component<ECS::NavAgentComponent>(a_entityId);
        navAgent != nullptr && navAgent->hasTarget)
    {
        navAgent->targetEntity = resolveEntity(navAgent->targetEntity);
    }
}

[[nodiscard]] std::string GameWorld::get_object_tag(EntityId a_entityId) const
{
    const BaseComponent *base = get_component<BaseComponent>(a_entityId);
    if (base == nullptr)
    {
        return {};
    }

    return base->tag;
}

[[nodiscard]] std::string GameWorld::get_object_name(EntityId a_entityId) const
{
    const BaseComponent *base = get_component<BaseComponent>(a_entityId);
    if (base == nullptr)
    {
        return {};
    }

    return base->name;
}

[[nodiscard]] GameObjectProto GameWorld::build_object_prototype(EntityId a_entityId,
                                                                const BaseComponent &a_base) const
{
    GameObjectProto prototype(std::string(a_base.name), std::string(a_base.tag));

    if (const ECS::TransformComponent *transform =
            get_component<ECS::TransformComponent>(a_entityId);
        transform != nullptr)
    {
        prototype.add_component(*transform);
    }

    if (const ECS::CameraComponent *camera = get_component<ECS::CameraComponent>(a_entityId);
        camera != nullptr)
    {
        prototype.add_component(*camera);
    }

    if (const ECS::CanvasComponent *canvas = get_component<ECS::CanvasComponent>(a_entityId);
        canvas != nullptr)
    {
        prototype.add_component(*canvas);
    }

    if (const ECS::UiRectTransformComponent *rect =
            get_component<ECS::UiRectTransformComponent>(a_entityId);
        rect != nullptr)
    {
        ECS::UiRectTransformComponent copiedRect = *rect;
        copiedRect.resolvedMin = Math::float2(0.0f, 0.0f);
        copiedRect.resolvedSize = Math::float2(0.0f, 0.0f);
        copiedRect.isResolved = false;
        prototype.add_component(copiedRect);
    }

    if (const ECS::UiLayoutGroupComponent *layout =
            get_component<ECS::UiLayoutGroupComponent>(a_entityId);
        layout != nullptr)
    {
        prototype.add_component(*layout);
    }

    if (const ECS::TextRendererComponent *text =
            get_component<ECS::TextRendererComponent>(a_entityId);
        text != nullptr)
    {
        prototype.add_component(*text);
    }

    if (const ECS::UiImageComponent *image = get_component<ECS::UiImageComponent>(a_entityId);
        image != nullptr)
    {
        prototype.add_component(*image);
    }

    if (const ECS::UiButtonComponent *button = get_component<ECS::UiButtonComponent>(a_entityId);
        button != nullptr)
    {
        ECS::UiButtonComponent copiedButton = *button;
        copiedButton.isHovered = false;
        copiedButton.isPressed = false;
        copiedButton.wasClicked = false;
        copiedButton.hasFocus = false;
        prototype.add_component(copiedButton);
    }

    if (const ECS::UiCheckboxComponent *checkbox =
            get_component<ECS::UiCheckboxComponent>(a_entityId);
        checkbox != nullptr)
    {
        ECS::UiCheckboxComponent copiedCheckbox = *checkbox;
        copiedCheckbox.isHovered = false;
        copiedCheckbox.isPressed = false;
        copiedCheckbox.wasChanged = false;
        copiedCheckbox.hasFocus = false;
        prototype.add_component(copiedCheckbox);
    }

    if (const ECS::UiSliderComponent *slider = get_component<ECS::UiSliderComponent>(a_entityId);
        slider != nullptr)
    {
        ECS::UiSliderComponent copiedSlider = *slider;
        copiedSlider.isHovered = false;
        copiedSlider.isDragging = false;
        copiedSlider.wasChanged = false;
        copiedSlider.hasFocus = false;
        prototype.add_component(copiedSlider);
    }

    if (const ECS::DirectionalLightComponent *directionalLight =
            get_component<ECS::DirectionalLightComponent>(a_entityId);
        directionalLight != nullptr)
    {
        prototype.add_component(*directionalLight);
    }

    if (const ECS::PointLightComponent *pointLight =
            get_component<ECS::PointLightComponent>(a_entityId);
        pointLight != nullptr)
    {
        prototype.add_component(*pointLight);
    }

    if (const ECS::SpotLightComponent *spotLight =
            get_component<ECS::SpotLightComponent>(a_entityId);
        spotLight != nullptr)
    {
        prototype.add_component(*spotLight);
    }

    if (const ECS::FirstPersonCameraControllerComponent *controller =
            get_component<ECS::FirstPersonCameraControllerComponent>(a_entityId);
        controller != nullptr)
    {
        ECS::FirstPersonCameraControllerComponent copiedController = *controller;
        copiedController.targetEntity =
            localize_entity_reference(copiedController.targetEntity, a_entityId);
        prototype.add_component(copiedController);
    }

    if (const ECS::MeshFilterComponent *meshFilter =
            get_component<ECS::MeshFilterComponent>(a_entityId);
        meshFilter != nullptr)
    {
        prototype.add_component(*meshFilter);
    }

    if (const ECS::NavAgentComponent *navAgent = get_component<ECS::NavAgentComponent>(a_entityId);
        navAgent != nullptr)
    {
        ECS::NavAgentComponent copiedNavAgent = *navAgent;
        if (copiedNavAgent.hasTarget)
        {
            copiedNavAgent.targetEntity =
                localize_entity_reference(copiedNavAgent.targetEntity, a_entityId);
        }
        copiedNavAgent.pathPoints.clear();
        copiedNavAgent.pathIndex = 0;
        copiedNavAgent.desiredVelocity = Math::float3::zero();
        copiedNavAgent.hasPath = false;
        prototype.add_component(copiedNavAgent);
    }

    if (const ECS::DemoEnemyComponent *demoEnemy =
            get_component<ECS::DemoEnemyComponent>(a_entityId);
        demoEnemy != nullptr)
    {
        ECS::DemoEnemyComponent copiedDemoEnemy = *demoEnemy;
        copiedDemoEnemy.targetEntity =
            localize_entity_reference(copiedDemoEnemy.targetEntity, a_entityId);
        prototype.add_component(copiedDemoEnemy);
    }

    if (const ECS::NavMeshBakeSourceComponent *navMeshBakeSource =
            get_component<ECS::NavMeshBakeSourceComponent>(a_entityId);
        navMeshBakeSource != nullptr)
    {
        prototype.add_component(*navMeshBakeSource);
    }

    if (const ECS::StaticMeshRendererComponent *renderer =
            get_component<ECS::StaticMeshRendererComponent>(a_entityId);
        renderer != nullptr)
    {
        prototype.add_component(*renderer);
    }

    if (const ECS::SkinnedMeshRendererComponent *renderer =
            get_component<ECS::SkinnedMeshRendererComponent>(a_entityId);
        renderer != nullptr)
    {
        prototype.add_component(*renderer);
    }

    if (const ECS::AnimationComponent *animation =
            get_component<ECS::AnimationComponent>(a_entityId);
        animation != nullptr)
    {
        prototype.add_component(*animation);
    }

    if (const ECS::SpriteRendererComponent *spriteRenderer =
            get_component<ECS::SpriteRendererComponent>(a_entityId);
        spriteRenderer != nullptr)
    {
        prototype.add_component(*spriteRenderer);
    }

    if (const ECS::ParticleEmitterComponent *particleEmitter =
            get_component<ECS::ParticleEmitterComponent>(a_entityId);
        particleEmitter != nullptr)
    {
        ECS::ParticleEmitterComponent copiedParticleEmitter = *particleEmitter;
        copiedParticleEmitter.runtimeParticleBase = (std::numeric_limits<uint32_t>::max)();
        copiedParticleEmitter.runtimeParticleCapacity = 0;
        copiedParticleEmitter.runtimeSpawnCursor = 0;
        copiedParticleEmitter.runtimeEmitAccumulator = 0.0f;
        prototype.add_component(copiedParticleEmitter);
    }

    if (const ECS::EffectEmitterComponent *effectEmitter =
            get_component<ECS::EffectEmitterComponent>(a_entityId);
        effectEmitter != nullptr)
    {
        ECS::EffectEmitterComponent copiedEffectEmitter = *effectEmitter;
        copiedEffectEmitter.runtimeEmitters.clear();
        prototype.add_component(copiedEffectEmitter);
    }

    if (const ECS::AudioSourceComponent *audioSource =
            get_component<ECS::AudioSourceComponent>(a_entityId);
        audioSource != nullptr)
    {
        ECS::AudioSourceComponent copiedAudioSource = *audioSource;
        copiedAudioSource.sourceHandle = {};
        copiedAudioSource.isPlaying = false;
        copiedAudioSource.playRequested = false;
        copiedAudioSource.stopRequested = false;
        copiedAudioSource.hasStarted = false;
        prototype.add_component(copiedAudioSource);
    }

    if (const ECS::RigidBodyComponent *rigidBody =
            get_component<ECS::RigidBodyComponent>(a_entityId);
        rigidBody != nullptr)
    {
        ECS::RigidBodyComponent copiedRigidBody = *rigidBody;
        copiedRigidBody.body = {};
        copiedRigidBody.isCreated = false;
        prototype.add_component(copiedRigidBody);
    }

    if (const ECS::ColliderComponent *collider = get_component<ECS::ColliderComponent>(a_entityId);
        collider != nullptr)
    {
        prototype.add_component(*collider);
    }

    if (const ECS::TriggerVolumeComponent *trigger =
            get_component<ECS::TriggerVolumeComponent>(a_entityId);
        trigger != nullptr)
    {
        ECS::TriggerVolumeComponent copiedTrigger = *trigger;
        copiedTrigger.overlappingEntities.clear();
        copiedTrigger.enteredEntities.clear();
        copiedTrigger.exitedEntities.clear();
        prototype.add_component(copiedTrigger);
    }

    if (const ECS::InteractableComponent *interactable =
            get_component<ECS::InteractableComponent>(a_entityId);
        interactable != nullptr)
    {
        prototype.add_component(*interactable);
    }

    if (const ECS::CharacterControllerComponent *characterController =
            get_component<ECS::CharacterControllerComponent>(a_entityId);
        characterController != nullptr)
    {
        ECS::CharacterControllerComponent copiedCharacterController = *characterController;
        copiedCharacterController.isGrounded = false;
        copiedCharacterController.jumpRequested = false;
        prototype.add_component(copiedCharacterController);
    }

    if (const ECS::ScriptComponent *script = get_component<ECS::ScriptComponent>(a_entityId);
        script != nullptr)
    {
        ECS::ScriptComponent copiedScript = *script;
        localize_script_entity_references(copiedScript, a_entityId);
        prototype.add_component(copiedScript);
    }

    return prototype;
}

[[nodiscard]] bool GameWorld::is_object_persistent(EntityId a_entityId) const noexcept
{
    const BaseComponent *base = get_component<BaseComponent>(a_entityId);
    if (base == nullptr)
    {
        return false;
    }

    return base->isPersistent;
}

[[nodiscard]] bool GameWorld::is_alive(EntityId a_entityId, Generation a_generation) const noexcept
{
    const EntityRecord *record = try_get_entity_record(a_entityId);
    return record != nullptr && record->isAlive && record->generation == a_generation;
}

[[nodiscard]] SceneId GameWorld::source_scene_id(EntityId a_entityId) const noexcept
{
    const EntityRecord *record = try_get_entity_record(a_entityId);
    if (record == nullptr || !record->isAlive)
    {
        return k_invalidSceneId;
    }

    return record->sourceSceneId;
}

[[nodiscard]] std::vector<GameObject> GameWorld::find_objects_by_tag(std::string_view a_tag)
{
    const auto it = m_tagIndex.find(std::string(a_tag));
    if (it == m_tagIndex.end())
    {
        return {};
    }

    std::vector<GameObject> objects{};
    objects.reserve(it->second.size());
    for (const EntityId entity : it->second)
    {
        if (!contains_object(entity))
        {
            continue;
        }

        objects.push_back(make_handle(entity));
    }

    std::sort(objects.begin(), objects.end(),
              [](const GameObject &a_left, const GameObject &a_right)
              { return a_left.entity_id() < a_right.entity_id(); });

    return objects;
}

[[nodiscard]] std::vector<GameObject> GameWorld::find_objects_by_name(std::string_view a_name)
{
    const auto it = m_nameIndex.find(std::string(a_name));
    if (it == m_nameIndex.end())
    {
        return {};
    }

    std::vector<GameObject> objects{};
    objects.reserve(it->second.size());
    for (const EntityId entity : it->second)
    {
        if (!contains_object(entity))
        {
            continue;
        }

        objects.push_back(make_handle(entity));
    }

    std::sort(objects.begin(), objects.end(),
              [](const GameObject &a_left, const GameObject &a_right)
              { return a_left.entity_id() < a_right.entity_id(); });

    return objects;
}

[[nodiscard]] GameObject GameWorld::find_object_by_name(std::string_view a_name)
{
    std::vector<GameObject> objects = find_objects_by_name(a_name);
    if (objects.empty())
    {
        return {};
    }

    return objects.front();
}

[[nodiscard]] std::vector<GameObject> GameWorld::find_objects_by_name_series(
    std::string_view a_baseName)
{
    const std::string normalizedBaseName = normalize_object_name(a_baseName);

    std::vector<GameObject> objects{};
    for (const auto &[name, entityIds] : m_nameIndex)
    {
        std::uint32_t seriesIndex = 0;
        if (!try_get_name_series_index(name, normalizedBaseName, seriesIndex))
        {
            continue;
        }

        for (const EntityId entity : entityIds)
        {
            if (!contains_object(entity))
            {
                continue;
            }

            objects.push_back(make_handle(entity));
        }
    }

    std::sort(objects.begin(), objects.end(),
              [this, &normalizedBaseName](const GameObject &a_left, const GameObject &a_right)
              {
                  std::uint32_t leftSeriesIndex = 0;
                  std::uint32_t rightSeriesIndex = 0;
                  const bool leftMatched = try_get_name_series_index(
                      get_object_name(a_left.entity_id()), normalizedBaseName, leftSeriesIndex);
                  const bool rightMatched = try_get_name_series_index(
                      get_object_name(a_right.entity_id()), normalizedBaseName, rightSeriesIndex);

                  if (leftMatched != rightMatched)
                  {
                      return leftMatched;
                  }
                  if (leftSeriesIndex != rightSeriesIndex)
                  {
                      return leftSeriesIndex < rightSeriesIndex;
                  }

                  return a_left.entity_id() < a_right.entity_id();
              });

    return objects;
}

[[nodiscard]] SceneId GameWorld::generate_scene_id()
{
    if (m_nextSceneId == 0)
    {
        throw std::overflow_error("GameWorld scene id overflow.");
    }

    const SceneId sceneId = m_nextSceneId;
    ++m_nextSceneId;
    return sceneId;
}

[[nodiscard]] EntityId GameWorld::create_entity_record(SceneId a_sourceSceneId,
                                                       LocalObjectId a_localObjectId)
{
    // ECS の Entity と GameWorld の管理情報を対応付ける
    const EntityId entity = m_ecs.generate_entity();

    if (m_entityRecords.size() <= entity)
    {
        m_entityRecords.resize(entity + 1);
    }

    EntityRecord &record = m_entityRecords[entity];
    if (record.isAlive)
    {
        throw std::runtime_error("GameWorld internal error: entity slot is already alive.");
    }

    if (record.generation == 0)
    {
        record.generation = 1;
    }

    record.isAlive = true;
    record.isPendingDestroy = false;
    record.sourceSceneId = a_sourceSceneId;
    record.sourceLocalObjectId = a_localObjectId;
    ++m_liveObjectCount;

    return entity;
}

void GameWorld::initialize_base_component(EntityId a_entityId, std::string_view a_name,
                                          std::string_view a_tag, SceneId a_owningSceneId,
                                          EntityId a_parent, bool a_isActive, bool a_isPersistent)
{
    BaseComponent *base = m_ecs.get_component<BaseComponent>(a_entityId);
    ECS::RenderableInfoComponent *renderableInfo =
        m_ecs.get_component<ECS::RenderableInfoComponent>(a_entityId);
    std::string previousName{};
    std::string previousTag{};
    const bool hadBaseComponent = base != nullptr;
    if (hadBaseComponent)
    {
        previousName = base->name;
        previousTag = base->tag;
    }

    if (base == nullptr)
    {
        base = m_ecs.add_component<BaseComponent>(a_entityId);
    }
    if (renderableInfo == nullptr)
    {
        renderableInfo = m_ecs.add_component<ECS::RenderableInfoComponent>(a_entityId);
    }

    if (base == nullptr || renderableInfo == nullptr)
    {
        throw std::runtime_error("GameWorld failed to initialize BaseComponent.");
    }

    base->name = make_unique_object_name(a_name, a_entityId);
    base->tag = std::string(a_tag);
    base->owningSceneId = a_owningSceneId;
    base->parent = a_parent;
    base->isActiveSelf = a_isActive;
    base->isPersistent = a_isPersistent;
    renderableInfo->objectId = ECS::k_invalidRenderableId;
    renderableInfo->transformId = ECS::k_invalidRenderableId;

    if (hadBaseComponent && previousName != base->name)
    {
        remove_object_from_name_index(a_entityId, previousName);
    }

    if (hadBaseComponent && previousTag != base->tag)
    {
        remove_object_from_tag_index(a_entityId, previousTag);
    }

    add_object_to_name_index(a_entityId, base->name);
    add_object_to_tag_index(a_entityId, base->tag);
    m_ecs.set_entity_active(a_entityId, a_isActive);
}

[[nodiscard]] GameWorld::LoadSceneResult GameWorld::instantiate_into_scene(
    SceneId a_sceneId, std::span<const ObjectDefinition> a_objects, const SceneAsset *a_asset)
{
    // ObjectDefinition 群を実 Entity として生成し、Scene に紐付ける
    auto sceneIt = m_scenes.find(a_sceneId);
    if (sceneIt == m_scenes.end())
    {
        throw std::runtime_error("GameWorld scene was not found.");
    }

    SceneInstance &scene = sceneIt->second;
    if (scene.isPendingUnload)
    {
        throw std::runtime_error("GameWorld scene is pending unload.");
    }

    if (a_asset != nullptr)
    {
        scene.asset = a_asset;
    }

    LoadSceneResult result{};
    result.sceneId = a_sceneId;
    result.objects.reserve(a_objects.size());

    struct PendingObjectInstantiation final
    {
        const ObjectDefinition *definition = nullptr;
        LocalObjectId localObjectId = k_invalidLocalObjectId;
        EntityId entityId = k_invalidEntityId;
    };

    std::vector<PendingObjectInstantiation> pending{};
    pending.reserve(a_objects.size());
    std::unordered_map<LocalObjectId, EntityId> newLocalObjectToEntity{};
    newLocalObjectToEntity.reserve(a_objects.size());
    std::vector<EntityId> createdEntities{};
    createdEntities.reserve(a_objects.size());

    try
    {
        // 先に全 Object を Entity 化し、Scene 内 LocalObjectId の対応表を確定する
        for (const ObjectDefinition &object : a_objects)
        {
            LocalObjectId localObjectId = object.localObjectId;
            if (localObjectId == k_invalidLocalObjectId)
            {
                localObjectId = scene.nextLocalObjectId++;
            }
            else
            {
                if (scene.localObjectToEntity.contains(localObjectId) ||
                    newLocalObjectToEntity.contains(localObjectId))
                {
                    throw std::runtime_error("GameWorld localObjectId is duplicated in scene.");
                }

                scene.nextLocalObjectId = (std::max)(scene.nextLocalObjectId, localObjectId + 1);
            }

            const EntityId entity = create_entity_record(a_sceneId, localObjectId);
            createdEntities.push_back(entity);

            object.prototype.restore_components_into(entity, m_ecs);

            const SceneId owningSceneId = object.isPersistent ? k_invalidSceneId : a_sceneId;
            initialize_base_component(entity, object.name(), object.tag(), owningSceneId,
                                      k_invalidEntityId, object.isActive, object.isPersistent);

            scene.entities.push_back(entity);
            scene.localObjectToEntity.emplace(localObjectId, entity);
            newLocalObjectToEntity.emplace(localObjectId, entity);

            pending.push_back({&object, localObjectId, entity});
            result.objects.push_back(make_handle(entity));
        }

        // Entity 参照と親子関係は、全 Object の EntityId が確定してから解決する
        for (const PendingObjectInstantiation &entry : pending)
        {
            resolve_script_entity_references(entry.entityId, scene, newLocalObjectToEntity);
            resolve_component_entity_references(entry.entityId, scene, newLocalObjectToEntity);

            if (!entry.definition->parentLocalObjectId.has_value())
            {
                continue;
            }

            const LocalObjectId parentLocalObjectId = *entry.definition->parentLocalObjectId;
            EntityId parentEntity = k_invalidEntityId;

            if (const auto newIt = newLocalObjectToEntity.find(parentLocalObjectId);
                newIt != newLocalObjectToEntity.end())
            {
                parentEntity = newIt->second;
            }
            else if (const auto sceneLocalIt = scene.localObjectToEntity.find(parentLocalObjectId);
                     sceneLocalIt != scene.localObjectToEntity.end())
            {
                parentEntity = sceneLocalIt->second;
            }
            else
            {
                throw std::runtime_error("GameWorld parentLocalObjectId could not be resolved.");
            }

            BaseComponent *base = get_component<BaseComponent>(entry.entityId);
            if (base == nullptr)
            {
                throw std::runtime_error(
                    "GameWorld BaseComponent is missing while resolving parent.");
            }

            base->parent = parentEntity;
        }

        return result;
    }
    catch (...)
    {
        // 途中で失敗した場合は、作成済み Entity を即時破棄して Scene 状態を巻き戻す
        for (const EntityId entity : createdEntities)
        {
            destroy_object_immediately(entity);
        }
        throw;
    }
}

void GameWorld::destroy_object_immediately(EntityId a_entityId) noexcept
{
    EntityRecord *record = try_get_entity_record(a_entityId);
    if (record == nullptr || !record->isAlive)
    {
        return;
    }

    // flush 実行時と、例外時に即座に巻き戻す必要がある経路で使う
    record->isPendingDestroy = false;

    const bool unlinked = unlink_object_from_scene(a_entityId);
    (void)unlinked;
    remove_object_from_name_index(a_entityId, get_object_name(a_entityId));
    remove_object_from_tag_index(a_entityId, get_object_tag(a_entityId));

    record->isAlive = false;
    record->sourceSceneId = k_invalidSceneId;
    record->sourceLocalObjectId = k_invalidLocalObjectId;
    ++record->generation;
    if (record->generation == 0)
    {
        record->generation = 1;
    }

    if (m_liveObjectCount > 0)
    {
        --m_liveObjectCount;
    }

    m_ecs.remove_entity(a_entityId);
}

[[nodiscard]] bool GameWorld::unload_scene_immediately(SceneId a_sceneId) noexcept
{
    auto sceneIt = m_scenes.find(a_sceneId);
    if (sceneIt == m_scenes.end())
    {
        return false;
    }

    // 遅延状態を解除し、ここで実際の Scene アンロードを行う
    sceneIt->second.isPendingUnload = false;

    const std::vector<EntityId> entities = sceneIt->second.entities;
    for (const EntityId entity : entities)
    {
        if (!contains_object(entity))
        {
            continue;
        }

        BaseComponent *base = get_component<BaseComponent>(entity);
        if (base != nullptr && base->isPersistent)
        {
            if (base->parent != k_invalidEntityId && source_scene_id(base->parent) == a_sceneId)
            {
                base->parent = k_invalidEntityId;
            }
            base->owningSceneId = k_invalidSceneId;

            if (EntityRecord *record = try_get_entity_record(entity))
            {
                record->sourceSceneId = k_invalidSceneId;
                record->sourceLocalObjectId = k_invalidLocalObjectId;
            }
            continue;
        }

        destroy_object_immediately(entity);
    }

    m_scenes.erase(sceneIt);
    m_ownedSceneAssets.erase(a_sceneId);
    return true;
}

[[nodiscard]] bool GameWorld::unlink_object_from_scene(EntityId a_entityId) noexcept
{
    EntityRecord *record = try_get_entity_record(a_entityId);
    if (record == nullptr || record->sourceSceneId == k_invalidSceneId)
    {
        return false;
    }

    auto sceneIt = m_scenes.find(record->sourceSceneId);
    if (sceneIt == m_scenes.end())
    {
        return false;
    }

    std::vector<EntityId> &entities = sceneIt->second.entities;
    const auto entityIt = std::find(entities.begin(), entities.end(), a_entityId);
    if (entityIt != entities.end())
    {
        entities.erase(entityIt);
    }

    if (record->sourceLocalObjectId != k_invalidLocalObjectId)
    {
        sceneIt->second.localObjectToEntity.erase(record->sourceLocalObjectId);
    }

    return true;
}

[[nodiscard]] GameObject GameWorld::make_handle(EntityId a_entityId) noexcept
{
    EntityRecord *record = try_get_entity_record(a_entityId);
    if (record == nullptr || !record->isAlive)
    {
        return {};
    }

    return GameObject(this, a_entityId, record->generation);
}

[[nodiscard]] GameWorld::EntityRecord *GameWorld::try_get_entity_record(
    EntityId a_entityId) noexcept
{
    if (a_entityId >= m_entityRecords.size())
    {
        return nullptr;
    }

    return &m_entityRecords[a_entityId];
}

[[nodiscard]] const GameWorld::EntityRecord *GameWorld::try_get_entity_record(
    EntityId a_entityId) const noexcept
{
    if (a_entityId >= m_entityRecords.size())
    {
        return nullptr;
    }

    return &m_entityRecords[a_entityId];
}

void GameWorld::add_object_to_tag_index(EntityId a_entityId, const std::string &a_tag)
{
    m_tagIndex[a_tag].insert(a_entityId);
}

void GameWorld::add_object_to_name_index(EntityId a_entityId, const std::string &a_name)
{
    m_nameIndex[a_name].insert(a_entityId);
}

[[nodiscard]] std::string GameWorld::normalize_object_name(std::string_view a_name) const
{
    if (a_name.empty())
    {
        return "GameObject";
    }

    return std::string(a_name);
}

[[nodiscard]] bool GameWorld::is_name_taken(std::string_view a_name,
                                            EntityId a_ignoredEntityId) const
{
    const auto it = m_nameIndex.find(std::string(a_name));
    if (it == m_nameIndex.end())
    {
        return false;
    }

    for (const EntityId entity : it->second)
    {
        if (entity == a_ignoredEntityId)
        {
            continue;
        }
        if (contains_object(entity))
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] std::string GameWorld::make_unique_object_name(std::string_view a_requestedName,
                                                             EntityId a_ignoredEntityId) const
{
    const std::string baseName = normalize_object_name(a_requestedName);
    if (!is_name_taken(baseName, a_ignoredEntityId))
    {
        return baseName;
    }

    std::uint32_t suffix = 1;
    while (true)
    {
        const std::string candidate = baseName + "(" + std::to_string(suffix) + ")";
        if (!is_name_taken(candidate, a_ignoredEntityId))
        {
            return candidate;
        }
        ++suffix;
    }
}

[[nodiscard]] bool GameWorld::try_get_name_series_index(const std::string &a_name,
                                                        std::string_view a_baseName,
                                                        std::uint32_t &a_outSeriesIndex) const
{
    if (a_name == a_baseName)
    {
        a_outSeriesIndex = 0;
        return true;
    }

    if (!a_name.starts_with(a_baseName) || a_name.size() <= a_baseName.size() + 2)
    {
        return false;
    }

    if (a_name[a_baseName.size()] != '(' || a_name.back() != ')')
    {
        return false;
    }

    const size_t digitsBegin = a_baseName.size() + 1;
    const size_t digitsCount = a_name.size() - digitsBegin - 1;
    if (digitsCount == 0)
    {
        return false;
    }

    std::uint32_t seriesIndex = 0;
    for (size_t i = digitsBegin; i < a_name.size() - 1; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(a_name[i]);
        if (!std::isdigit(ch))
        {
            return false;
        }

        seriesIndex = (seriesIndex * 10u) + static_cast<std::uint32_t>(ch - '0');
    }

    a_outSeriesIndex = seriesIndex;
    return true;
}

void GameWorld::remove_object_from_tag_index(EntityId a_entityId, const std::string &a_tag)
{
    const auto it = m_tagIndex.find(a_tag);
    if (it == m_tagIndex.end())
    {
        return;
    }

    it->second.erase(a_entityId);
    if (it->second.empty())
    {
        m_tagIndex.erase(it);
    }
}

void GameWorld::remove_object_from_name_index(EntityId a_entityId, const std::string &a_name)
{
    const auto it = m_nameIndex.find(a_name);
    if (it == m_nameIndex.end())
    {
        return;
    }

    it->second.erase(a_entityId);
    if (it->second.empty())
    {
        m_nameIndex.erase(it);
    }
}

[[nodiscard]] bool GameWorld::find_entity_by_body(Physics::RigidBodyHandle a_body,
                                                  EntityId &a_outEntity) const noexcept
{
    a_outEntity = k_invalidEntityId;
    if (!a_body.valid())
    {
        return false;
    }

    for (EntityId entity = 0; entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
    {
        if (!contains_object(entity))
        {
            continue;
        }

        const ECS::RigidBodyComponent *rigidBody = get_component<ECS::RigidBodyComponent>(entity);
        if (rigidBody != nullptr && rigidBody->body == a_body)
        {
            a_outEntity = entity;
            return true;
        }
    }

    return false;
}

GameObject::GameObject(GameWorld *a_world, EntityId a_entityId, Generation a_generation) noexcept
    : m_world(a_world), m_entityId(a_entityId), m_generation(a_generation)
{
}

bool GameObject::is_valid() const noexcept
{
    if (m_world == nullptr)
    {
        return false;
    }

    bool isAlive = false;
    const Result result = m_world->is_alive(m_entityId, m_generation, isAlive);
    return result && isAlive;
}

Result GameObject::name(std::string &a_outName) const
{
    if (!is_valid())
    {
        a_outName.clear();
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->get_object_name(m_entityId, a_outName);
}

Result GameObject::set_name(std::string_view a_name)
{
    if (!is_valid())
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->set_object_name(m_entityId, a_name);
}

Result GameObject::tag(std::string &a_outTag) const
{
    if (!is_valid())
    {
        a_outTag.clear();
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->get_object_tag(m_entityId, a_outTag);
}

Result GameObject::set_tag(std::string_view a_tag)
{
    if (!is_valid())
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->set_object_tag(m_entityId, a_tag);
}

Result GameObject::is_active(bool &a_outIsActive) const
{
    if (!is_valid())
    {
        a_outIsActive = false;
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->is_object_active(m_entityId, a_outIsActive);
}

Result GameObject::set_active(bool a_isActive)
{
    if (!is_valid())
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->set_object_active(m_entityId, a_isActive);
}

Result GameObject::is_persistent(bool &a_outIsPersistent) const
{
    if (!is_valid())
    {
        a_outIsPersistent = false;
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->is_object_persistent(m_entityId, a_outIsPersistent);
}

Result GameObject::set_persistent(bool a_isPersistent)
{
    if (!is_valid())
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->set_object_persistent(m_entityId, a_isPersistent);
}

Result GameObject::destroy() noexcept
{
    if (!is_valid())
    {
        return Result::fail(Code::InvalidState, Severity::Warning, "GameObject is not valid.");
    }

    return m_world->destroy_object(m_entityId);
}

} // namespace Cue::GameCore
