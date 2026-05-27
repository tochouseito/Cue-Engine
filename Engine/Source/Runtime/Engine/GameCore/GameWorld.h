#pragma once

// === Base includes ===
#include <Result.h>
#include <CueMath.h>

// === Engine includes ===
#include "Components.h"
#include "DebugDraw.h"
#include "GameObject.h"
#include "Navigation/Navigation.h"
#include <DrawSystem/DrawScene.h>
#include <DrawSystem/DrawFrameState.h>
#include "SceneAsset.h"
#include "SceneInstance.h"
#include "SceneSerializer.h"
#include <AnimationSystem/AnimationSystem.h>
#include "Systems/AudioSystem.h"
#include "Systems/CharacterControllerSystem.h"
#include "Systems/DemoEnemySystem.h"
#include "Systems/FirstPersonCameraControllerSystem.h"
#include "Systems/PhysicsBodySystem.h"
#include "Systems/PlayerControlSystem.h"
#include "Systems/TriggerVolumeSystem.h"
#include "Systems/UiLayoutSystem.h"
#include "Systems/UiWidgetSystem.h"
#include <DrawSystem/Systems/CameraSystem.h>
#include <DrawSystem/Systems/RenderableObjectSystem.h>
#include <DrawSystem/Systems/SkinnedRenderableObjectSystem.h>
#include <DrawSystem/Systems/SpriteSystem.h>
#include <DrawSystem/Systems/TextSystem.h>
#include <DrawSystem/DrawResources.h>
#include <Asset/AssetManager.h>
#include <DrawSystem/StaticMeshPoolTypes.h>
#include <LightingSystem/LightFrameState.h>
#include <LightingSystem/LightResources.h>
#include <LightingSystem/LightScene.h>
#include <LightingSystem/Systems/LightSystem.h>
#include <ShadowSystem/ShadowFrameState.h>
#include <ShadowSystem/ShadowResources.h>
#include <ShadowSystem/ShadowScene.h>
#include <ShadowSystem/Systems/ShadowSystem.h>
#include <ParticleSystem/ParticleFrameState.h>
#include <ParticleSystem/ParticleRangeAllocator.h>
#include <ParticleSystem/ParticleResources.h>
#include <ParticleSystem/ParticleScene.h>
#include <ParticleSystem/Systems/ParticleEmitterSystem.h>
#include <EffectSystem/Systems/EffectEmitterSystem.h>

// === PAL includes ===
#include <Input/InputManager.h>

// === C++ includes ===
#include <algorithm>
#include <array>
#include <memory>
#include <cctype>
#include <exception>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Cue::GameCore
{
    class GameWorld final
    {
    public:
        static constexpr uint32_t k_maxRenderObjectCount = 1000;
        static constexpr uint32_t k_maxSpriteCount = 1000;
        static constexpr uint32_t k_maxParticleEmitterCount =
            GpuData::k_maxParticleEmitterCount;
        static constexpr uint32_t k_maxParticleCount =
            GpuData::k_maxParticleCount;
        static constexpr uint32_t k_maxSkinPaletteCount =
            k_maxRenderObjectCount * 128u;
        static constexpr uint32_t k_maxMaterialCount = 1024;

        struct LoadSceneResult final
        {
            SceneId sceneId = k_invalidSceneId;
            std::vector<GameObject> objects{};
        };

        struct EntityRecord final
        {
            Generation generation = 0;
            bool isAlive = false;
            // 遅延削除キューへ同じ Entity を二重登録しないためのフラグ。
            bool isPendingDestroy = false;
            SceneId sourceSceneId = k_invalidSceneId;
            LocalObjectId sourceLocalObjectId = k_invalidLocalObjectId;
        };

        struct PendingSceneLoad final
        {
            SceneId sceneId = k_invalidSceneId;
            Core::IO::Path path{};
        };

        struct GameplayRaycastDesc final
        {
            Math::float3 origin = Math::float3::zero();
            Math::float3 direction = Math::float3(0.0f, 0.0f, 1.0f);
            EntityId ignoredEntity = k_invalidEntityId;
            float distance = 1000.0f;
        };

        struct GameplayRaycastHit final
        {
            EntityId entity = k_invalidEntityId;
            Math::float3 position = Math::float3::zero();
            Math::float3 normal = Math::float3::zero();
            float distance = 0.0f;
        };

        GameWorld() = default;

        [[nodiscard]] Result ecs(ECS::ECSManager*& a_outEcs) noexcept
        {
            a_outEcs = &m_ecs;
            return Result::ok();
        }

        [[nodiscard]] Result initialize(RHI::IBufferManager* a_bufferManager,
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
            MaterialHandle a_defaultMaterialHandle);
        [[nodiscard]] Result finalize_systems() noexcept;
        void set_asset_root_path(const Core::IO::Path& a_assetRootPath)
        {
            m_assetRootPath = a_assetRootPath.normalize();
        }

        [[nodiscard]] const Core::IO::Path& asset_root_path() const noexcept
        {
            return m_assetRootPath;
        }

        [[nodiscard]] Core::IO::IFileSystem* file_system() const noexcept
        {
            return m_fileSystem;
        }

        [[nodiscard]] Result simulate(float a_deltaTime);
        [[nodiscard]] Result editor_update(
            uint32_t a_bufferIndex,
            uint32_t a_renderWidth,
            uint32_t a_renderHeight,
            float a_deltaTime = 0.0f);
        [[nodiscard]] Result update(float a_deltaTime, uint32_t a_bufferIndex,
            uint32_t a_renderWidth, uint32_t a_renderHeight);
        [[nodiscard]] Result clone_from(const GameWorld& a_source);

        [[nodiscard]] Result add_object()
        {
            return add_object(make_spawn_position());
        }

        [[nodiscard]] Result add_object(GameObject& a_outObject)
        {
            return add_object(make_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_object(const Math::float3& a_position)
        {
            GameObject object{};
            return add_object(a_position, object);
        }

        [[nodiscard]] Result add_object(
            const Math::float3& a_position, GameObject& a_outObject);
        [[nodiscard]] Result add_game_object()
        {
            GameObject object{};
            return add_game_object(object);
        }

        [[nodiscard]] Result add_game_object(GameObject& a_outObject)
        {
            return add_game_object(make_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_game_object(const Math::float3& a_position)
        {
            GameObject object{};
            return add_game_object(a_position, object);
        }

        [[nodiscard]] Result add_game_object(
            const Math::float3& a_position, GameObject& a_outObject);
        [[nodiscard]] Result add_camera_object()
        {
            GameObject object{};
            return add_camera_object(object);
        }

        [[nodiscard]] Result add_camera_object(GameObject& a_outObject)
        {
            return add_camera_object(make_camera_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_camera_object(const Math::float3& a_position)
        {
            GameObject object{};
            return add_camera_object(a_position, object);
        }

        [[nodiscard]] Result add_camera_object(
            const Math::float3& a_position, GameObject& a_outObject);
        [[nodiscard]] Result add_directional_light_object()
        {
            GameObject object{};
            return add_directional_light_object(object);
        }

        [[nodiscard]] Result add_directional_light_object(GameObject& a_outObject)
        {
            return add_directional_light_object(
                make_light_spawn_position(),
                a_outObject);
        }

        [[nodiscard]] Result add_directional_light_object(
            const Math::float3& a_position)
        {
            GameObject object{};
            return add_directional_light_object(a_position, object);
        }

        [[nodiscard]] Result add_directional_light_object(
            const Math::float3& a_position,
            GameObject& a_outObject);
        [[nodiscard]] Result add_point_light_object()
        {
            GameObject object{};
            return add_point_light_object(object);
        }

        [[nodiscard]] Result add_point_light_object(GameObject& a_outObject)
        {
            return add_point_light_object(make_light_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_point_light_object(const Math::float3& a_position)
        {
            GameObject object{};
            return add_point_light_object(a_position, object);
        }

        [[nodiscard]] Result add_point_light_object(
            const Math::float3& a_position,
            GameObject& a_outObject);
        [[nodiscard]] Result add_spot_light_object()
        {
            GameObject object{};
            return add_spot_light_object(object);
        }

        [[nodiscard]] Result add_spot_light_object(GameObject& a_outObject)
        {
            return add_spot_light_object(make_light_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_spot_light_object(const Math::float3& a_position)
        {
            GameObject object{};
            return add_spot_light_object(a_position, object);
        }

        [[nodiscard]] Result add_spot_light_object(
            const Math::float3& a_position,
            GameObject& a_outObject);
        [[nodiscard]] Result add_sprite_object()
        {
            GameObject object{};
            return add_sprite_object(object);
        }

        [[nodiscard]] Result add_sprite_object(GameObject& a_outObject)
        {
            return add_sprite_object(make_sprite_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_sprite_object(const Math::float3& a_position)
        {
            GameObject object{};
            return add_sprite_object(a_position, object);
        }

        [[nodiscard]] Result add_sprite_object(
            const Math::float3& a_position, GameObject& a_outObject);

        [[nodiscard]] Result add_object_to_scene(SceneId a_sceneId)
        {
            return add_object_to_scene(a_sceneId, make_spawn_position());
        }

        [[nodiscard]] Result add_object_to_scene(
            SceneId a_sceneId, GameObject& a_outObject)
        {
            return add_object_to_scene(a_sceneId, make_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_object_to_scene(
            SceneId a_sceneId, const Math::float3& a_position)
        {
            GameObject object{};
            return add_object_to_scene(a_sceneId, a_position, object);
        }

        [[nodiscard]] Result add_object_to_scene(SceneId a_sceneId,
            const Math::float3& a_position, GameObject& a_outObject);
        [[nodiscard]] Result add_game_object_to_scene(SceneId a_sceneId)
        {
            return add_game_object_to_scene(a_sceneId, make_spawn_position());
        }

        [[nodiscard]] Result add_game_object_to_scene(
            SceneId a_sceneId, GameObject& a_outObject)
        {
            return add_game_object_to_scene(
                a_sceneId, make_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_game_object_to_scene(
            SceneId a_sceneId, const Math::float3& a_position)
        {
            GameObject object{};
            return add_game_object_to_scene(a_sceneId, a_position, object);
        }

        [[nodiscard]] Result add_game_object_to_scene(SceneId a_sceneId,
            const Math::float3& a_position, GameObject& a_outObject);
        [[nodiscard]] Result add_camera_object_to_scene(SceneId a_sceneId)
        {
            return add_camera_object_to_scene(
                a_sceneId, make_camera_spawn_position());
        }

        [[nodiscard]] Result add_camera_object_to_scene(
            SceneId a_sceneId, GameObject& a_outObject)
        {
            return add_camera_object_to_scene(
                a_sceneId, make_camera_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_camera_object_to_scene(
            SceneId a_sceneId, const Math::float3& a_position)
        {
            GameObject object{};
            return add_camera_object_to_scene(a_sceneId, a_position, object);
        }

        [[nodiscard]] Result add_camera_object_to_scene(SceneId a_sceneId,
            const Math::float3& a_position, GameObject& a_outObject);
        [[nodiscard]] Result add_directional_light_object_to_scene(SceneId a_sceneId)
        {
            return add_directional_light_object_to_scene(
                a_sceneId,
                make_light_spawn_position());
        }

        [[nodiscard]] Result add_directional_light_object_to_scene(
            SceneId a_sceneId,
            GameObject& a_outObject)
        {
            return add_directional_light_object_to_scene(
                a_sceneId,
                make_light_spawn_position(),
                a_outObject);
        }

        [[nodiscard]] Result add_directional_light_object_to_scene(
            SceneId a_sceneId,
            const Math::float3& a_position)
        {
            GameObject object{};
            return add_directional_light_object_to_scene(
                a_sceneId,
                a_position,
                object);
        }

        [[nodiscard]] Result add_directional_light_object_to_scene(
            SceneId a_sceneId,
            const Math::float3& a_position,
            GameObject& a_outObject);
        [[nodiscard]] Result add_point_light_object_to_scene(SceneId a_sceneId)
        {
            return add_point_light_object_to_scene(
                a_sceneId,
                make_light_spawn_position());
        }

        [[nodiscard]] Result add_point_light_object_to_scene(
            SceneId a_sceneId,
            GameObject& a_outObject)
        {
            return add_point_light_object_to_scene(
                a_sceneId,
                make_light_spawn_position(),
                a_outObject);
        }

        [[nodiscard]] Result add_point_light_object_to_scene(
            SceneId a_sceneId,
            const Math::float3& a_position)
        {
            GameObject object{};
            return add_point_light_object_to_scene(a_sceneId, a_position, object);
        }

        [[nodiscard]] Result add_point_light_object_to_scene(SceneId a_sceneId,
            const Math::float3& a_position, GameObject& a_outObject);
        [[nodiscard]] Result add_spot_light_object_to_scene(SceneId a_sceneId)
        {
            return add_spot_light_object_to_scene(
                a_sceneId,
                make_light_spawn_position());
        }

        [[nodiscard]] Result add_spot_light_object_to_scene(
            SceneId a_sceneId,
            GameObject& a_outObject)
        {
            return add_spot_light_object_to_scene(
                a_sceneId,
                make_light_spawn_position(),
                a_outObject);
        }

        [[nodiscard]] Result add_spot_light_object_to_scene(
            SceneId a_sceneId,
            const Math::float3& a_position)
        {
            GameObject object{};
            return add_spot_light_object_to_scene(a_sceneId, a_position, object);
        }

        [[nodiscard]] Result add_spot_light_object_to_scene(SceneId a_sceneId,
            const Math::float3& a_position, GameObject& a_outObject);
        [[nodiscard]] Result add_sprite_object_to_scene(SceneId a_sceneId)
        {
            return add_sprite_object_to_scene(a_sceneId, make_sprite_spawn_position());
        }

        [[nodiscard]] Result add_sprite_object_to_scene(
            SceneId a_sceneId, GameObject& a_outObject)
        {
            return add_sprite_object_to_scene(
                a_sceneId, make_sprite_spawn_position(), a_outObject);
        }

        [[nodiscard]] Result add_sprite_object_to_scene(
            SceneId a_sceneId, const Math::float3& a_position)
        {
            GameObject object{};
            return add_sprite_object_to_scene(a_sceneId, a_position, object);
        }

        [[nodiscard]] Result add_sprite_object_to_scene(SceneId a_sceneId,
            const Math::float3& a_position, GameObject& a_outObject);

        [[nodiscard]] Result remove_object(uint32_t a_objectId) noexcept;

        [[nodiscard]] Result get_render_object_entity(
            uint32_t a_objectId, EntityId& a_outEntityId) const noexcept
        {
            return try_get_static_mesh_entity(a_objectId, a_outEntityId)
                ? Result::ok()
                : Result::fail(Code::NotFound, Severity::Error,
                    "Static mesh object id was not found.");
        }

        [[nodiscard]] Result set_main_camera(EntityId a_cameraEntityId)
        {
            if (!contains_object(a_cameraEntityId) ||
                !has_component<ECS::CameraComponent>(a_cameraEntityId))
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "Camera object was not found.");
            }

            std::vector<EntityId> cameraEntities = collect_camera_entities();
            auto targetIt = std::find(
                cameraEntities.begin(), cameraEntities.end(), a_cameraEntityId);
            if (targetIt == cameraEntities.end())
            {
                return Result::fail(
                    Code::NotFound, Severity::Error, "Camera object was not found.");
            }

            for (uint32_t cameraIndex = 0;
                 cameraIndex < cameraEntities.size(); ++cameraIndex)
            {
                ECS::CameraComponent* camera =
                    get_component<ECS::CameraComponent>(cameraEntities[cameraIndex]);
                if (camera == nullptr)
                {
                    continue;
                }

                camera->isMain = (cameraEntities[cameraIndex] == a_cameraEntityId);
            }

            m_mainCameraIndex = static_cast<uint32_t>(
                std::distance(cameraEntities.begin(), targetIt));
            return Result::ok();
        }

        [[nodiscard]] Result get_parent(
            EntityId a_entityId,
            EntityId& a_outParent) const noexcept
        {
            a_outParent = k_invalidEntityId;
            if (!contains_object(a_entityId))
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
            }

            const BaseComponent* base = get_component<BaseComponent>(a_entityId);
            if (base == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "GameWorld BaseComponent is missing.");
            }

            a_outParent = base->parent;
            return Result::ok();
        }

        [[nodiscard]] Result set_parent(
            EntityId a_childEntityId,
            EntityId a_parentEntityId,
            bool a_keepsWorldTransform) noexcept
        {
            if (!contains_object(a_childEntityId) ||
                !contains_object(a_parentEntityId))
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
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

            ECS::TransformComponent* childTransform = nullptr;
            ECS::TransformComponent* parentTransform = nullptr;
            Result childTransformResult =
                get_component(a_childEntityId, childTransform);
            Result parentTransformResult =
                get_component(a_parentEntityId, parentTransform);
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
                if (!resolve_world_transform(
                        a_childEntityId, state, childWorld) ||
                    !resolve_world_transform(
                        a_parentEntityId, state, parentWorld))
                {
                    return Result::fail(Code::InvalidState, Severity::Error,
                        "GameWorld world transform could not be resolved.");
                }
            }

            BaseComponent* childBase = get_component<BaseComponent>(a_childEntityId);
            if (childBase == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "GameWorld BaseComponent is missing.");
            }
            childBase->parent = a_parentEntityId;

            if (a_keepsWorldTransform)
            {
                *childTransform =
                    make_local_transform(parentWorld, childWorld);
            }

            sync_world_transforms();
            return Result::ok();
        }

        [[nodiscard]] Result detach_parent(
            EntityId a_childEntityId,
            bool a_keepsWorldTransform) noexcept
        {
            if (!contains_object(a_childEntityId))
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
            }

            BaseComponent* childBase = get_component<BaseComponent>(a_childEntityId);
            ECS::TransformComponent* childTransform = nullptr;
            Result childTransformResult =
                get_component(a_childEntityId, childTransform);
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
                if (!resolve_world_transform(
                        a_childEntityId, state, childWorld))
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

        DrawSystem::DrawFrameState& draw_frame_state() noexcept
        {
            return m_drawFrameState;
        }

        const DrawSystem::DrawFrameState& draw_frame_state() const noexcept
        {
            return m_drawFrameState;
        }

        NavigationWorld& navigation_world() noexcept
        {
            return m_navigationWorld;
        }

        const NavigationWorld& navigation_world() const noexcept
        {
            return m_navigationWorld;
        }

        [[nodiscard]] Result load_navigation_mesh(
            const NavMeshAssetData& a_asset,
            NavMeshHandle& a_outHandle) noexcept
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

        [[nodiscard]] Result load_navigation_mesh_from_path(
            const Core::IO::Path& a_path,
            NavMeshHandle& a_outHandle) noexcept
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
            Result result =
                NavMeshAssetSerializer::load(*m_fileSystem, navMeshPath, navMeshAsset);
            if (!result)
            {
                return result;
            }

            return load_navigation_mesh(navMeshAsset, a_outHandle);
        }

        [[nodiscard]] Result set_active_navigation_mesh(
            NavMeshHandle a_handle) noexcept
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

        [[nodiscard]] Result set_active_navigation_mesh(
            NavMeshHandle a_handle,
            const NavMeshAssetData& a_asset) noexcept
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

        [[nodiscard]] NavMeshHandle active_navigation_mesh() const noexcept
        {
            return m_activeNavMesh;
        }

        [[nodiscard]] Result set_nav_agent_destination(
            EntityId a_entityId,
            const Math::float3& a_destination) noexcept
        {
            ECS::NavAgentComponent* agent = get_component<ECS::NavAgentComponent>(
                a_entityId);
            if (agent == nullptr)
            {
                return Result::fail(Code::NotFound, Severity::Warning,
                    "NavAgentComponent was not found.");
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

        [[nodiscard]] Result set_nav_agent_target(
            EntityId a_entityId,
            EntityId a_targetEntityId) noexcept
        {
            ECS::NavAgentComponent* agent = get_component<ECS::NavAgentComponent>(
                a_entityId);
            if (agent == nullptr)
            {
                return Result::fail(Code::NotFound, Severity::Warning,
                    "NavAgentComponent was not found.");
            }
            if (a_targetEntityId == k_invalidEntityId ||
                !contains_object(a_targetEntityId))
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

        [[nodiscard]] Result raycast(
            const GameplayRaycastDesc& a_desc,
            GameplayRaycastHit& a_outHit) const noexcept
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
                const ECS::RigidBodyComponent* ignoredRigidBody =
                    get_component<ECS::RigidBodyComponent>(
                        a_desc.ignoredEntity);
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

        [[nodiscard]] Result trigger_overlaps(
            EntityId a_entityId,
            std::vector<EntityId>& a_outEntities) const noexcept
        {
            a_outEntities.clear();
            const ECS::TriggerVolumeComponent* trigger =
                get_component<ECS::TriggerVolumeComponent>(a_entityId);
            if (trigger == nullptr)
            {
                return Result::fail(Code::NotFound, Severity::Warning,
                    "TriggerVolumeComponent was not found.");
            }

            a_outEntities = trigger->overlappingEntities;
            return Result::ok();
        }

        DebugDrawBuffer& debug_draw() noexcept
        {
            return m_debugDraw;
        }

        const DebugDrawBuffer& debug_draw() const noexcept
        {
            return m_debugDraw;
        }

        [[nodiscard]] Result build_navigation_debug_geometry(
            NavMeshDebugGeometry& a_outGeometry) noexcept
        {
            a_outGeometry = {};
            if (!m_activeNavMesh.valid())
            {
                return Result::fail(Code::InvalidState, Severity::Warning,
                    "Active navigation mesh is not set.");
            }

            Result result = m_navigationWorld.build_debug_geometry(
                m_activeNavMesh, a_outGeometry);
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

        [[nodiscard]] const DrawSystem::DrawResources* draw_resources() const noexcept
        {
            return m_drawResources.get();
        }

        [[nodiscard]] const LightingSystem::LightResources* light_resources()
            const noexcept
        {
            return m_lightResources.get();
        }

        [[nodiscard]] const ShadowSystem::ShadowResources* shadow_resources()
            const noexcept
        {
            return m_shadowResources.get();
        }

        [[nodiscard]] const ParticleSystem::ParticleResources* particle_resources()
            const noexcept
        {
            return m_particleResources.get();
        }

        LightingSystem::LightFrameState& light_frame_state() noexcept
        {
            return m_lightFrameState;
        }

        ParticleSystem::ParticleFrameState& particle_frame_state() noexcept
        {
            return m_particleFrameState;
        }

        const ParticleSystem::ParticleFrameState& particle_frame_state() const noexcept
        {
            return m_particleFrameState;
        }

        const LightingSystem::LightFrameState& light_frame_state() const noexcept
        {
            return m_lightFrameState;
        }

        ShadowSystem::ShadowFrameState& shadow_frame_state() noexcept
        {
            return m_shadowFrameState;
        }

        const ShadowSystem::ShadowFrameState& shadow_frame_state() const noexcept
        {
            return m_shadowFrameState;
        }

        void set_cpu_batching_enabled(bool a_enabled) noexcept
        {
            m_isCpuBatchingEnabled = a_enabled;
        }

        [[nodiscard]] bool is_cpu_batching_enabled() const noexcept
        {
            return m_isCpuBatchingEnabled;
        }

        [[nodiscard]] Result create_object(std::string_view a_name,
            std::string_view a_tag, bool a_isPersistent, GameObject& a_outObject)
        {
            a_outObject = {};
            return capture_result([this, &a_outObject, a_name, a_tag, a_isPersistent]()
                {
                    a_outObject = create_object(a_name, a_tag, a_isPersistent);
                });
        }

        [[nodiscard]] Result create_object(std::string_view a_name,
            GameObject& a_outObject)
        {
            return create_object(a_name, "Default", false, a_outObject);
        }

        [[nodiscard]] Result load_scene(
            const SceneAsset& a_asset, LoadSceneResult& a_outResult)
        {
            a_outResult = {};
            Result result = capture_result([this, &a_outResult, &a_asset]()
                {
                    a_outResult = load_scene(a_asset);
                });
            if (!result)
            {
                return result;
            }

            if (!a_asset.navigation_mesh_path().empty())
            {
                NavMeshHandle navMeshHandle{};
                result = load_navigation_mesh_from_path(
                    Core::IO::Path(a_asset.navigation_mesh_path()), navMeshHandle);
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

        [[nodiscard]] Result load_scene(SceneId a_sceneId,
            const SceneAsset& a_asset, LoadSceneResult& a_outResult)
        {
            a_outResult = {};
            if (a_sceneId == k_invalidSceneId)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "GameWorld scene id is invalid.");
            }

            Result result = capture_result(
                [this, &a_outResult, a_sceneId, &a_asset]()
                {
                    a_outResult = load_scene(a_sceneId, a_asset);
                });
            if (!result)
            {
                return result;
            }

            if (!a_asset.navigation_mesh_path().empty())
            {
                NavMeshHandle navMeshHandle{};
                result = load_navigation_mesh_from_path(
                    Core::IO::Path(a_asset.navigation_mesh_path()), navMeshHandle);
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

        [[nodiscard]] Result request_load_scene(
            std::string_view a_sceneName, SceneId& a_outSceneId)
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
                    m_pendingLoadedScenes.push_back(
                        PendingSceneLoad{ a_outSceneId, scenePath });
                });
        }

        [[nodiscard]] Result append_to_scene(SceneId a_sceneId,
            std::span<const ObjectDefinition> a_objects, LoadSceneResult& a_outResult)
        {
            a_outResult = {};
            return capture_result([this, &a_outResult, a_sceneId, a_objects]()
                {
                    a_outResult = append_to_scene(a_sceneId, a_objects);
                });
        }

        [[nodiscard]] Result append_to_scene(SceneId a_sceneId,
            const std::vector<ObjectDefinition>& a_objects, LoadSceneResult& a_outResult)
        {
            return append_to_scene(a_sceneId,
                std::span<const ObjectDefinition>(a_objects), a_outResult);
        }

        [[nodiscard]] Result append_object_to_scene(SceneId a_sceneId,
            const ObjectDefinition& a_object, GameObject& a_outObject)
        {
            a_outObject = {};
            return capture_result([this, &a_outObject, a_sceneId, &a_object]()
                {
                    a_outObject = append_object_to_scene(a_sceneId, a_object);
                });
        }

        [[nodiscard]] Result destroy_object(EntityId a_entityId) noexcept
        {
            if (!contains_object(a_entityId))
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
            }

            destroy_object_internal(a_entityId);
            return Result::ok();
        }

        [[nodiscard]] Result unload_scene(SceneId a_sceneId) noexcept
        {
            return unload_scene_internal(a_sceneId)
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld scene was not found.");
        }

        [[nodiscard]] Result request_unload_scene(SceneId a_sceneId) noexcept
        {
            if (a_sceneId == k_invalidSceneId)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "GameWorld scene id is invalid.");
            }

            const auto pendingIt = std::find_if(
                m_pendingLoadedScenes.begin(),
                m_pendingLoadedScenes.end(),
                [a_sceneId](const PendingSceneLoad& a_pending)
                {
                    return a_pending.sceneId == a_sceneId;
                });
            if (pendingIt != m_pendingLoadedScenes.end())
            {
                m_pendingLoadedScenes.erase(pendingIt);
                return Result::ok();
            }

            return unload_scene(a_sceneId);
        }

        [[nodiscard]] Result execute_deferred_deletions() noexcept
        {
            execute_deferred_deletions_internal();
            return Result::ok();
        }

        [[nodiscard]] Result find_object(EntityId a_entityId, GameObject& a_outObject) noexcept
        {
            a_outObject = find_object(a_entityId);
            return a_outObject.is_valid()
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
        }

        [[nodiscard]] Result contains_object(
            EntityId a_entityId, bool& a_outContains) const noexcept
        {
            a_outContains = contains_object(a_entityId);
            return Result::ok();
        }

        [[nodiscard]] Result contains_scene(
            SceneId a_sceneId, bool& a_outContains) const noexcept
        {
            a_outContains = contains_scene(a_sceneId);
            return Result::ok();
        }

        [[nodiscard]] Result get_object_tag(
            EntityId a_entityId, std::string& a_outTag) const
        {
            a_outTag = get_object_tag(a_entityId);
            return a_outTag.empty() && !contains_object(a_entityId)
                ? Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.")
                : Result::ok();
        }

        [[nodiscard]] Result get_object_name(
            EntityId a_entityId, std::string& a_outName) const
        {
            a_outName = get_object_name(a_entityId);
            return a_outName.empty() && !contains_object(a_entityId)
                ? Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.")
                : Result::ok();
        }

        [[nodiscard]] Result set_object_name(
            EntityId a_entityId, std::string_view a_name)
        {
            return capture_result([this, a_entityId, a_name]()
                {
                    BaseComponent* base = get_component<BaseComponent>(a_entityId);
                    if (base == nullptr)
                    {
                        throw std::runtime_error("GameWorld BaseComponent is missing.");
                    }

                    const std::string resolvedName =
                        make_unique_object_name(a_name, a_entityId);
                    if (base->name == resolvedName)
                    {
                        return;
                    }

                    remove_object_from_name_index(a_entityId, base->name);
                    base->name = resolvedName;
                    add_object_to_name_index(a_entityId, base->name);
                });
        }

        [[nodiscard]] Result set_object_tag(
            EntityId a_entityId, std::string_view a_tag)
        {
            return capture_result([this, a_entityId, a_tag]()
                {
                    BaseComponent* base = get_component<BaseComponent>(a_entityId);
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

        [[nodiscard]] Result is_object_active(
            EntityId a_entityId, bool& a_outIsActive) const noexcept
        {
            a_outIsActive = contains_object(a_entityId) &&
                m_ecs.is_entity_active(a_entityId);
            return contains_object(a_entityId)
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
        }

        [[nodiscard]] Result capture_deleted_object(
            EntityId a_entityId, DeletedObjectSnapshot& a_outSnapshot) const
        {
            a_outSnapshot = {};
            if (!contains_object(a_entityId))
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
            }

            const BaseComponent* base = get_component<BaseComponent>(a_entityId);
            const EntityRecord* record = try_get_entity_record(a_entityId);
            if (base == nullptr || record == nullptr || !record->isAlive)
            {
                return Result::fail(
                    Code::InvalidState, Severity::Error, "GameWorld object snapshot could not be captured.");
            }

            ObjectDefinition definition{};
            definition.localObjectId = record->sourceLocalObjectId;
            definition.isActive = m_ecs.is_entity_active(a_entityId);
            definition.isPersistent = base->isPersistent;
            definition.prototype = build_object_prototype(a_entityId, *base);

            if (base->parent != k_invalidEntityId)
            {
                const EntityRecord* parentRecord = try_get_entity_record(base->parent);
                if (parentRecord != nullptr &&
                    parentRecord->isAlive &&
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

        [[nodiscard]] Result restore_deleted_object(
            const DeletedObjectSnapshot& a_snapshot, EntityId& a_outObjectId)
        {
            a_outObjectId = k_invalidEntityId;

            GameObject object{};
            if (a_snapshot.sourceSceneId != k_invalidSceneId)
            {
                Result appendResult = append_object_to_scene(
                    a_snapshot.sourceSceneId, a_snapshot.definition, object);
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
                    return Result::fail(
                        Code::CreateFailed, Severity::Error, "GameWorld object restore failed.");
                }
            }

            a_outObjectId = object.entity_id();
            return Result::ok();
        }

        [[nodiscard]] Result set_object_active(
            EntityId a_entityId, bool a_isActive)
        {
            return capture_result([this, a_entityId, a_isActive]()
                {
                    BaseComponent* base = get_component<BaseComponent>(a_entityId);
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

        [[nodiscard]] Result is_object_persistent(
            EntityId a_entityId, bool& a_outIsPersistent) const noexcept
        {
            a_outIsPersistent = is_object_persistent(a_entityId);
            return contains_object(a_entityId)
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
        }

        [[nodiscard]] Result set_object_persistent(
            EntityId a_entityId, bool a_isPersistent)
        {
            return capture_result([this, a_entityId, a_isPersistent]()
                {
                    BaseComponent* base = get_component<BaseComponent>(a_entityId);
                    if (base == nullptr)
                    {
                        throw std::runtime_error("GameWorld BaseComponent is missing.");
                    }

                    if (base->isPersistent == a_isPersistent)
                    {
                        return;
                    }

                    base->isPersistent = a_isPersistent;
                    base->owningSceneId = a_isPersistent
                        ? k_invalidSceneId
                        : source_scene_id(a_entityId);
                });
        }

        [[nodiscard]] Result is_alive(EntityId a_entityId,
            Generation a_generation, bool& a_outIsAlive) const noexcept
        {
            a_outIsAlive = is_alive(a_entityId, a_generation);
            return Result::ok();
        }

        [[nodiscard]] Result source_scene_id(
            EntityId a_entityId, SceneId& a_outSceneId) const noexcept
        {
            a_outSceneId = source_scene_id(a_entityId);
            return contains_object(a_entityId)
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
        }

        [[nodiscard]] Result object_count(size_t& a_outCount) const noexcept
        {
            a_outCount = m_liveObjectCount;
            return Result::ok();
        }

        [[nodiscard]] Result set_rigid_body_linear_velocity(
            EntityId a_entityId,
            Math::float3 a_velocity) noexcept
        {
            ECS::RigidBodyComponent* rigidBody = nullptr;
            Result result = get_component<ECS::RigidBodyComponent>(
                a_entityId, rigidBody);
            if (!result || rigidBody == nullptr)
            {
                return result;
            }

            rigidBody->linearVelocity = a_velocity;
            if (m_physicsSystem == nullptr || !rigidBody->body.valid())
            {
                return Result::ok();
            }

            return m_physicsSystem->set_linear_velocity(
                rigidBody->body, a_velocity, Physics::BodyActivation::Activate);
        }

        [[nodiscard]] Result get_rigid_body_linear_velocity(
            EntityId a_entityId,
            Math::float3& a_outVelocity) const noexcept
        {
            a_outVelocity = Math::float3::zero();
            const ECS::RigidBodyComponent* rigidBody =
                get_component<ECS::RigidBodyComponent>(a_entityId);
            if (rigidBody == nullptr)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "RigidBodyComponent was not found.");
            }

            if (m_physicsSystem == nullptr || !rigidBody->body.valid())
            {
                a_outVelocity = rigidBody->linearVelocity;
                return Result::ok();
            }

            return m_physicsSystem->get_linear_velocity(
                rigidBody->body, a_outVelocity);
        }

        [[nodiscard]] Result add_rigid_body_force(
            EntityId a_entityId,
            Math::float3 a_force) noexcept
        {
            ECS::RigidBodyComponent* rigidBody = nullptr;
            Result result = get_component<ECS::RigidBodyComponent>(
                a_entityId, rigidBody);
            if (!result || rigidBody == nullptr)
            {
                return result;
            }
            if (m_physicsSystem == nullptr || !rigidBody->body.valid())
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "RigidBody physics body is not created.");
            }

            return m_physicsSystem->add_force(
                rigidBody->body, a_force, Physics::BodyActivation::Activate);
        }

        [[nodiscard]] Result add_rigid_body_impulse(
            EntityId a_entityId,
            Math::float3 a_impulse) noexcept
        {
            ECS::RigidBodyComponent* rigidBody = nullptr;
            Result result = get_component<ECS::RigidBodyComponent>(
                a_entityId, rigidBody);
            if (!result || rigidBody == nullptr)
            {
                return result;
            }
            if (m_physicsSystem == nullptr || !rigidBody->body.valid())
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "RigidBody physics body is not created.");
            }

            return m_physicsSystem->add_impulse(
                rigidBody->body, a_impulse, Physics::BodyActivation::Activate);
        }

        [[nodiscard]] Result set_character_move_velocity(
            EntityId a_entityId,
            Math::float3 a_velocity) noexcept
        {
            ECS::CharacterControllerComponent* controller = nullptr;
            Result result = get_component<ECS::CharacterControllerComponent>(
                a_entityId, controller);
            if (!result || controller == nullptr)
            {
                return result;
            }

            controller->moveVelocity = a_velocity;
            return Result::ok();
        }

        [[nodiscard]] Result request_character_jump(EntityId a_entityId) noexcept
        {
            ECS::CharacterControllerComponent* controller = nullptr;
            Result result = get_component<ECS::CharacterControllerComponent>(
                a_entityId, controller);
            if (!result || controller == nullptr)
            {
                return result;
            }

            controller->jumpRequested = true;
            return Result::ok();
        }

        [[nodiscard]] Result scene_count(size_t& a_outCount) const noexcept
        {
            a_outCount = m_scenes.size();
            return Result::ok();
        }

        [[nodiscard]] Result clear() noexcept
        {
            m_pendingLoadedScenes.clear();

            if (m_activeNavMesh.valid())
            {
                const Result navResult =
                    m_navigationWorld.unload_nav_mesh(m_activeNavMesh);
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

            // 公開 API を使って削除予約を積み、最後にまとめて flush する。
            std::vector<SceneId> sceneIds{};
            sceneIds.reserve(m_scenes.size());
            for (const auto& [sceneId, _] : m_scenes)
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

            for (EntityId entity = 0;
                entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
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

            // clear() 完了時点ではワールドが空になっていることを保証する。
            Result clearResult = execute_deferred_deletions();
            if (!clearResult)
            {
                return clearResult;
            }

            m_ownedSceneAssets.clear();
            return Result::ok();
        }

        template <typename T>
        [[nodiscard]] Result get_component(EntityId a_entityId, T*& a_outComponent) noexcept
        {
            a_outComponent = get_component<T>(a_entityId);
            return a_outComponent != nullptr
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld component was not found.");
        }

        template <typename T>
        [[nodiscard]] Result get_component(
            EntityId a_entityId, const T*& a_outComponent) const noexcept
        {
            a_outComponent = get_component<T>(a_entityId);
            return a_outComponent != nullptr
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld component was not found.");
        }

        template <typename T, typename... Args>
        [[nodiscard]] Result add_component(
            EntityId a_entityId, T*& a_outComponent, Args&&... a_args)
        {
            a_outComponent = nullptr;
            return capture_result([this, &a_outComponent, a_entityId, &a_args...]()
                {
                    a_outComponent =
                        &add_component<T>(a_entityId, std::forward<Args>(a_args)...);
                });
        }

        template <typename T>
        [[nodiscard]] Result has_component(
            EntityId a_entityId, bool& a_outHasComponent) const noexcept
        {
            a_outHasComponent = has_component<T>(a_entityId);
            return contains_object(a_entityId)
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
        }

        template <typename T>
        [[nodiscard]] Result remove_component(EntityId a_entityId) noexcept
        {
            if (!contains_object(a_entityId))
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
            }

            m_ecs.remove_component<T>(a_entityId);
            return Result::ok();
        }

        template <class F>
        [[nodiscard]] Result visit_object(EntityId a_entityId, F&& a_func)
        {
            GameObject object = find_object(a_entityId);
            if (!object.is_valid())
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
            }

            a_func(object.entity_id(), source_scene_id(a_entityId), object);
            return Result::ok();
        }

        template <class F>
        [[nodiscard]] Result for_each_object_in_scene(SceneId a_sceneId, F&& a_func)
        {
            auto sceneIt = m_scenes.find(a_sceneId);
            if (sceneIt == m_scenes.end())
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld scene was not found.");
            }

            const std::vector<EntityId> entities = sceneIt->second.entities;
            for (const EntityId entity : entities)
            {
                GameObject object = find_object(entity);
                if (!object.is_valid())
                {
                    continue;
                }

                a_func(object.entity_id(), a_sceneId, object);
            }

            return Result::ok();
        }

        template <class F>
        [[nodiscard]] Result for_each_object(F&& a_func)
        {
            for (EntityId entity = 0;
                entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
            {
                GameObject object = find_object(entity);
                if (!object.is_valid())
                {
                    continue;
                }

                a_func(object.entity_id(), source_scene_id(entity), object);
            }

            return Result::ok();
        }

        [[nodiscard]] Result find_objects_by_tag(
            std::string_view a_tag, std::vector<GameObject>& a_outObjects)
        {
            a_outObjects = find_objects_by_tag(a_tag);
            return Result::ok();
        }

        [[nodiscard]] Result find_objects_by_name(
            std::string_view a_name, std::vector<GameObject>& a_outObjects)
        {
            a_outObjects = find_objects_by_name(a_name);
            return Result::ok();
        }

        [[nodiscard]] Result find_object_by_name(
            std::string_view a_name, GameObject& a_outObject)
        {
            a_outObject = find_object_by_name(a_name);
            return a_outObject.is_valid()
                ? Result::ok()
                : Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
        }

        [[nodiscard]] Result destroy_object_by_name(std::string_view a_name) noexcept
        {
            const GameObject object = find_object_by_name(a_name);
            if (!object.is_valid())
            {
                return Result::fail(
                    Code::NotFound, Severity::Warning, "GameWorld object was not found.");
            }

            return destroy_object_internal(object.entity_id()), Result::ok();
        }

        [[nodiscard]] Result destroy_objects_by_name(
            std::string_view a_name, size_t& a_outCount) noexcept
        {
            const std::vector<GameObject> objects = find_objects_by_name(a_name);
            for (const GameObject& object : objects)
            {
                destroy_object_internal(object.entity_id());
            }

            a_outCount = objects.size();
            return Result::ok();
        }

        [[nodiscard]] Result find_objects_by_name_series(
            std::string_view a_baseName, std::vector<GameObject>& a_outObjects)
        {
            a_outObjects = find_objects_by_name_series(a_baseName);
            return Result::ok();
        }

        [[nodiscard]] Result destroy_objects_by_name_series(
            std::string_view a_baseName, size_t& a_outCount) noexcept
        {
            const std::vector<GameObject> objects =
                find_objects_by_name_series(a_baseName);
            for (const GameObject& object : objects)
            {
                destroy_object_internal(object.entity_id());
            }

            a_outCount = objects.size();
            return Result::ok();
        }

        [[nodiscard]] Result destroy_objects_by_tag(
            std::string_view a_tag, size_t& a_outCount) noexcept
        {
            const std::vector<GameObject> objects = find_objects_by_tag(a_tag);
            for (const GameObject& object : objects)
            {
                destroy_object_internal(object.entity_id());
            }

            a_outCount = objects.size();
            return Result::ok();
        }

    private:
        [[nodiscard]] Math::float3 make_spawn_position() const noexcept
        {
            const size_t objectIndex = count_active_static_mesh_objects();
            const uint32_t column = static_cast<uint32_t>(objectIndex % 3u);
            const uint32_t row = static_cast<uint32_t>(objectIndex / 3u);

            return Math::float3{
                (static_cast<float>(column) - 1.0f) * 2.0f,
                0.0f,
                static_cast<float>(row) * 2.5f
            };
        }

        [[nodiscard]] Math::float3 make_camera_spawn_position() const noexcept
        {
            return Math::float3(0.0f, 0.0f, -6.0f);
        }

        [[nodiscard]] Math::float3 make_sprite_spawn_position() const noexcept
        {
            if (!m_drawFrameState.frameStates.empty())
            {
                const DrawSystem::DrawFrameData& frameState =
                    m_drawFrameState.frameStates.front();
                return Math::float3{
                    static_cast<float>(frameState.renderWidth) * 0.5f,
                    static_cast<float>(frameState.renderHeight) * 0.5f,
                    0.0f
                };
            }

            return Math::float3(320.0f, 180.0f, 0.0f);
        }

        [[nodiscard]] Math::float3 make_light_spawn_position() const noexcept
        {
            return Math::float3(0.0f, 3.0f, -4.0f);
        }

        [[nodiscard]] static Math::float3 multiply_components(
            const Math::float3& a_left,
            const Math::float3& a_right) noexcept
        {
            return Math::float3(
                a_left.x * a_right.x,
                a_left.y * a_right.y,
                a_left.z * a_right.z);
        }

        [[nodiscard]] static Math::float3 divide_components_safe(
            const Math::float3& a_left,
            const Math::float3& a_right) noexcept
        {
            const auto divide = [](float a_value, float a_divisor) noexcept
            {
                return a_divisor != 0.0f ? a_value / a_divisor : a_value;
            };
            return Math::float3(
                divide(a_left.x, a_right.x),
                divide(a_left.y, a_right.y),
                divide(a_left.z, a_right.z));
        }

        [[nodiscard]] static Math::float3 rotate_vector(
            const Math::Quaternion& a_rotation,
            const Math::float3& a_value) noexcept
        {
            const Math::Quaternion rotation =
                Math::Quaternion::normalize(a_rotation);
            const Math::Quaternion vector(
                a_value.x, a_value.y, a_value.z, 0.0f);
            const Math::Quaternion result =
                rotation * vector * Math::Quaternion::inverse(rotation);
            return Math::float3(result.x, result.y, result.z);
        }

        [[nodiscard]] static ECS::WorldTransformComponent compose_world_transform(
            const ECS::WorldTransformComponent& a_parent,
            const ECS::TransformComponent& a_local) noexcept
        {
            ECS::WorldTransformComponent world{};
            world.scale = multiply_components(a_parent.scale, a_local.scale);
            world.rotation = Math::Quaternion::normalize(
                a_parent.rotation * a_local.rotation);
            const Math::float3 scaledLocalPosition =
                multiply_components(a_local.position, a_parent.scale);
            world.position =
                a_parent.position +
                rotate_vector(a_parent.rotation, scaledLocalPosition);
            return world;
        }

        [[nodiscard]] static ECS::TransformComponent make_local_transform(
            const ECS::WorldTransformComponent& a_parent,
            const ECS::WorldTransformComponent& a_world) noexcept
        {
            ECS::TransformComponent local{};
            const Math::Quaternion inverseParentRotation =
                Math::Quaternion::inverse(a_parent.rotation);
            local.position = divide_components_safe(
                rotate_vector(
                    inverseParentRotation,
                    a_world.position - a_parent.position),
                a_parent.scale);
            local.rotation = Math::Quaternion::normalize(
                inverseParentRotation * a_world.rotation);
            local.scale = divide_components_safe(a_world.scale, a_parent.scale);
            return local;
        }

        [[nodiscard]] bool is_descendant_of(
            EntityId a_entityId,
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

                const BaseComponent* base = get_component<BaseComponent>(current);
                current = base != nullptr ? base->parent : k_invalidEntityId;
            }

            return false;
        }

        [[nodiscard]] bool resolve_world_transform(
            EntityId a_entityId,
            std::vector<uint8_t>& a_state,
            ECS::WorldTransformComponent& a_outWorld) noexcept
        {
            if (!contains_object(a_entityId) ||
                static_cast<size_t>(a_entityId) >= a_state.size())
            {
                return false;
            }

            uint8_t& state = a_state[static_cast<size_t>(a_entityId)];
            if (state == 2u)
            {
                const ECS::WorldTransformComponent* world =
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

            ECS::TransformComponent* local =
                get_component<ECS::TransformComponent>(a_entityId);
            if (local == nullptr)
            {
                return false;
            }

            ECS::WorldTransformComponent* world =
                get_component<ECS::WorldTransformComponent>(a_entityId);
            if (world == nullptr)
            {
                Result addWorldResult =
                    add_component<ECS::WorldTransformComponent>(
                        a_entityId, world);
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
            const BaseComponent* base = get_component<BaseComponent>(a_entityId);
            if (base != nullptr &&
                base->parent != k_invalidEntityId &&
                contains_object(base->parent))
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

        void sync_world_transforms() noexcept
        {
            std::vector<uint8_t> state(m_entityRecords.size(), 0u);
            for (EntityId entity = 0;
                 entity < static_cast<EntityId>(m_entityRecords.size());
                 ++entity)
            {
                if (!contains_object(entity) ||
                    get_component<ECS::TransformComponent>(entity) == nullptr)
                {
                    continue;
                }

                ECS::WorldTransformComponent world{};
                (void)resolve_world_transform(entity, state, world);
            }
        }

        void sync_draw_frame_state(uint32_t a_bufferIndex, uint32_t a_renderWidth,
            uint32_t a_renderHeight) noexcept
        {
            if (a_bufferIndex >= m_drawFrameState.frameStates.size())
            {
                return;
            }

            DrawSystem::DrawFrameData& frameState = m_drawFrameState.frame_state(a_bufferIndex);
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
                ParticleSystem::ParticleFrameData& particleFrameState =
                    m_particleFrameState.frame_state(a_bufferIndex);
                particleFrameState.frame.emitterCount = 0;
                particleFrameState.frame.particleCount = 0;
            }
        }

        [[nodiscard]] Result upload_draw_scene(uint32_t a_bufferIndex);
        [[nodiscard]] Result upload_particle_scene(uint32_t a_bufferIndex);
        [[nodiscard]] Result upload_light_scene(uint32_t a_bufferIndex);
        [[nodiscard]] Result upload_shadow_scene(uint32_t a_bufferIndex);

        void animate_static_mesh_objects(float a_deltaTime)
        {
            std::vector<EntityId> entities = collect_active_static_mesh_entities();
            for (size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex)
            {
                ECS::TransformComponent* transform =
                    get_component<ECS::TransformComponent>(entities[entityIndex]);
                if (transform == nullptr)
                {
                    continue;
                }

                switch (entityIndex)
                {
                case 0:
                {
                    Math::float3 rotation =
                        Math::quaternion_to_euler_xyz(transform->rotation);
                    rotation.y += a_deltaTime * 1.25f;
                    transform->rotation =
                        Math::quaternion_from_euler_xyz(rotation);
                    break;
                }
                case 1:
                {
                    Math::float3 rotation =
                        Math::quaternion_to_euler_xyz(transform->rotation);
                    rotation.x += a_deltaTime * 0.75f;
                    transform->rotation =
                        Math::quaternion_from_euler_xyz(rotation);
                    break;
                }
                case 2:
                {
                    Math::float3 rotation =
                        Math::quaternion_to_euler_xyz(transform->rotation);
                    rotation.y -= a_deltaTime * 1.0f;
                    transform->rotation =
                        Math::quaternion_from_euler_xyz(rotation);
                    break;
                }
                default:
                {
                    Math::float3 rotation =
                        Math::quaternion_to_euler_xyz(transform->rotation);
                    rotation.y += a_deltaTime * 0.5f;
                    transform->rotation =
                        Math::quaternion_from_euler_xyz(rotation);
                    break;
                }
                }
            }
        }

        [[nodiscard]] std::vector<EntityId> collect_active_static_mesh_entities() const
        {
            std::vector<EntityId> entities{};
            entities.reserve(m_liveObjectCount);
            for (EntityId entity = 0;
                 entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
            {
                if (!contains_object(entity) || !m_ecs.is_entity_active(entity))
                {
                    continue;
                }

                const BaseComponent* base = get_component<BaseComponent>(entity);
                const ECS::TransformComponent* transform =
                    get_component<ECS::TransformComponent>(entity);
                const ECS::MeshFilterComponent* meshFilter =
                    get_component<ECS::MeshFilterComponent>(entity);
                const ECS::StaticMeshRendererComponent* renderer =
                    get_component<ECS::StaticMeshRendererComponent>(entity);
                if (base == nullptr || transform == nullptr || meshFilter == nullptr ||
                    renderer == nullptr)
                {
                    continue;
                }
                if (!base->isActiveSelf || !renderer->visible ||
                    meshFilter->meshId == ECS::k_invalidMeshId)
                {
                    continue;
                }

                entities.push_back(entity);
            }

            return entities;
        }

        [[nodiscard]] std::vector<EntityId> collect_camera_entities() const
        {
            std::vector<EntityId> entities{};
            entities.reserve(m_liveObjectCount);
            for (EntityId entity = 0;
                 entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
            {
                if (!contains_object(entity) ||
                    !has_component<ECS::CameraComponent>(entity))
                {
                    continue;
                }

                entities.push_back(entity);
            }

            return entities;
        }

        [[nodiscard]] size_t count_active_static_mesh_objects() const
        {
            return collect_active_static_mesh_entities().size();
        }

        [[nodiscard]] bool try_get_static_mesh_entity(uint32_t a_objectId,
            EntityId& a_outEntityId) const noexcept
        {
            a_outEntityId = k_invalidEntityId;
            for (EntityId entity = 0;
                 entity < static_cast<EntityId>(m_entityRecords.size()); ++entity)
            {
                if (!contains_object(entity) || !m_ecs.is_entity_active(entity))
                {
                    continue;
                }

                const ECS::RenderableInfoComponent* renderableInfo =
                    get_component<ECS::RenderableInfoComponent>(entity);
                if (renderableInfo == nullptr ||
                    renderableInfo->objectId != a_objectId)
                {
                    continue;
                }

                a_outEntityId = entity;
                return true;
            }

            return false;
        }

        template <typename F>
        [[nodiscard]] static Result capture_result(F&& a_func)
        {
            try
            {
                a_func();
                return Result::ok();
            }
            catch (const std::bad_alloc&)
            {
                return Result::fail(
                    Code::OutOfMemory, Severity::Error, "GameWorld out of memory.");
            }
            catch (const std::overflow_error& a_error)
            {
                return map_exception_message(a_error.what());
            }
            catch (const std::runtime_error& a_error)
            {
                return map_exception_message(a_error.what());
            }
            catch (const std::exception&)
            {
                return Result::fail(
                    Code::UnknownError, Severity::Error, "GameWorld unknown exception.");
            }
        }

        [[nodiscard]] static Result map_exception_message(
            std::string_view a_message) noexcept
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

        [[nodiscard]] GameObject create_object(std::string_view a_name,
            std::string_view a_tag = "Default", bool a_isPersistent = false)
        {
            // Scene に属さない単体の GameObject を生成する。
            const EntityId entity =
                create_entity_record(k_invalidSceneId, k_invalidLocalObjectId);
            initialize_base_component(entity, a_name, a_tag, k_invalidSceneId,
                k_invalidEntityId, true, a_isPersistent);
            return make_handle(entity);
        }

        [[nodiscard]] GameObject instantiate_object(const ObjectDefinition& a_object)
        {
            const EntityId entity =
                create_entity_record(k_invalidSceneId, k_invalidLocalObjectId);
            a_object.prototype.restore_components_into(entity, m_ecs);
            initialize_base_component(entity, a_object.name(), a_object.tag(),
                k_invalidSceneId, k_invalidEntityId, a_object.isActive,
                a_object.isPersistent);
            return make_handle(entity);
        }

        [[nodiscard]] LoadSceneResult load_scene(const SceneAsset& a_asset)
        {
            const SceneId sceneId = generate_scene_id();
            return load_scene(sceneId, a_asset);
        }

        [[nodiscard]] LoadSceneResult load_scene(
            SceneId a_sceneId, const SceneAsset& a_asset)
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

        [[nodiscard]] LoadSceneResult append_to_scene(SceneId a_sceneId,
            std::span<const ObjectDefinition> a_objects)
        {
            return instantiate_into_scene(a_sceneId, a_objects, nullptr);
        }

        [[nodiscard]] GameObject append_object_to_scene(SceneId a_sceneId,
            const ObjectDefinition& a_object)
        {
            const std::array<ObjectDefinition, 1> objects = { a_object };
            LoadSceneResult result = append_to_scene(a_sceneId, objects);
            if (result.objects.empty())
            {
                return {};
            }

            return result.objects.front();
        }

        void destroy_object_internal(EntityId a_entityId) noexcept
        {
            EntityRecord* record = try_get_entity_record(a_entityId);
            if (record == nullptr || !record->isAlive || record->isPendingDestroy)
            {
                return;
            }

            // 実際の削除は execute_deferred_deletions() が呼ばれるまで遅延させる。
            record->isPendingDestroy = true;
            m_pendingDestroyedEntities.push_back(a_entityId);
        }

        [[nodiscard]] bool unload_scene_internal(SceneId a_sceneId) noexcept
        {
            auto sceneIt = m_scenes.find(a_sceneId);
            if (sceneIt == m_scenes.end() || sceneIt->second.isPendingUnload)
            {
                return false;
            }

            // Scene の破棄も遅延させ、呼び出し側が flush のタイミングを制御できるようにする。
            sceneIt->second.isPendingUnload = true;
            m_pendingUnloadedScenes.push_back(a_sceneId);
            return true;
        }

        void execute_deferred_deletions_internal() noexcept
        {
            // Scene のアンロードでは非永続 Object がまとめて消えるため、先に Scene 側を処理する。
            std::vector<SceneId> pendingScenes{};
            pendingScenes.swap(m_pendingUnloadedScenes);
            for (const SceneId sceneId : pendingScenes)
            {
                (void)unload_scene_immediately(sceneId);
            }

            // 続いて、単体で予約されていた Object の削除を処理する。
            std::vector<EntityId> pendingEntities{};
            pendingEntities.swap(m_pendingDestroyedEntities);
            for (const EntityId entity : pendingEntities)
            {
                destroy_object_immediately(entity);
            }
        }

        [[nodiscard]] Result execute_deferred_scene_loads()
        {
            std::vector<PendingSceneLoad> pendingScenes{};
            pendingScenes.swap(m_pendingLoadedScenes);

            for (const PendingSceneLoad& pendingScene : pendingScenes)
            {
                auto sceneAsset = std::make_unique<SceneAsset>();
                SceneSerializer::LoadOptions loadOptions{};
                loadOptions.assetManager = m_assetManager;
                Result result = SceneSerializer::load_scene_asset(
                    *m_fileSystem,
                    pendingScene.path,
                    *sceneAsset,
                    loadOptions);
                if (!result)
                {
                    return result;
                }

                LoadSceneResult loadResult{};
                result = load_scene(
                    pendingScene.sceneId,
                    *sceneAsset,
                    loadResult);
                if (!result)
                {
                    return result;
                }

                m_ownedSceneAssets[pendingScene.sceneId] = std::move(sceneAsset);
            }

            return Result::ok();
        }

        [[nodiscard]] Result resolve_scene_path(
            std::string_view a_sceneName,
            Core::IO::Path& a_outPath) const noexcept
        {
            a_outPath = {};
            if (a_sceneName.empty())
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Scene name is empty.");
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
                sceneText.find('/') != std::string::npos ||
                sceneText.find('\\') != std::string::npos;
            Core::IO::Path scenePath(sceneText);
            if (scenePath.extension().empty())
            {
                sceneText += ".cuescene";
                scenePath = Core::IO::Path(sceneText);
            }

            if (!scenePath.is_absolute())
            {
                scenePath = hasDirectory
                    ? Core::IO::Path::join(m_assetRootPath, scenePath)
                    : Core::IO::Path::join(
                        Core::IO::Path::join(
                            m_assetRootPath,
                            Core::IO::Path("Scenes")),
                        scenePath);
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
                return Result::fail(Code::NotFound, Severity::Warning,
                    "Scene file was not found.");
            }

            a_outPath = scenePath;
            return Result::ok();
        }

        [[nodiscard]] GameObject find_object(EntityId a_entityId) noexcept
        {
            if (!contains_object(a_entityId))
            {
                return {};
            }

            return make_handle(a_entityId);
        }

        [[nodiscard]] bool contains_object(EntityId a_entityId) const noexcept
        {
            const EntityRecord* record = try_get_entity_record(a_entityId);
            return record != nullptr && record->isAlive;
        }

        [[nodiscard]] bool contains_scene(SceneId a_sceneId) const noexcept
        {
            return m_scenes.find(a_sceneId) != m_scenes.end();
        }

        void localize_script_entity_references(
            ECS::ScriptComponent& a_script,
            EntityId a_sourceEntityId) const noexcept
        {
            const SceneId sourceSceneId = source_scene_id(a_sourceEntityId);
            const auto localizeField =
                [this, sourceSceneId](ECS::ScriptFieldValue& a_field) noexcept
            {
                if (a_field.type != ECS::ScriptFieldType::EntityRef ||
                    a_field.entityValue == k_invalidEntityId)
                {
                    return;
                }

                const EntityRecord* record =
                    try_get_entity_record(a_field.entityValue);
                if (record == nullptr || !record->isAlive ||
                    record->sourceSceneId != sourceSceneId ||
                    record->sourceLocalObjectId == k_invalidLocalObjectId)
                {
                    return;
                }

                a_field.entityValue =
                    static_cast<EntityId>(record->sourceLocalObjectId);
            };

            for (ECS::ScriptFieldValue& field : a_script.serializedFieldValues)
            {
                localizeField(field);
            }
            for (ECS::ScriptFieldValue& field : a_script.transientFieldValues)
            {
                localizeField(field);
            }
        }

        [[nodiscard]] EntityId localize_entity_reference(
            EntityId a_entityValue,
            EntityId a_sourceEntityId) const noexcept
        {
            if (a_entityValue == k_invalidEntityId)
            {
                return a_entityValue;
            }

            const SceneId sourceSceneId = source_scene_id(a_sourceEntityId);
            const EntityRecord* record = try_get_entity_record(a_entityValue);
            if (record == nullptr || !record->isAlive ||
                record->sourceSceneId != sourceSceneId ||
                record->sourceLocalObjectId == k_invalidLocalObjectId)
            {
                return a_entityValue;
            }

            return static_cast<EntityId>(record->sourceLocalObjectId);
        }

        void resolve_script_entity_references(
            EntityId a_entityId,
            const SceneInstance& a_scene,
            const std::unordered_map<LocalObjectId, EntityId>& a_newLocalObjectToEntity)
            noexcept
        {
            ECS::ScriptComponent* script = get_component<ECS::ScriptComponent>(
                a_entityId);
            if (script == nullptr)
            {
                return;
            }

            const auto resolveField =
                [&a_scene, &a_newLocalObjectToEntity](
                    ECS::ScriptFieldValue& a_field) noexcept
            {
                if (a_field.type != ECS::ScriptFieldType::EntityRef ||
                    a_field.entityValue == k_invalidEntityId)
                {
                    return;
                }

                const LocalObjectId localObjectId =
                    static_cast<LocalObjectId>(a_field.entityValue);
                if (const auto newIt =
                    a_newLocalObjectToEntity.find(localObjectId);
                    newIt != a_newLocalObjectToEntity.end())
                {
                    a_field.entityValue = newIt->second;
                    return;
                }

                if (const auto sceneIt =
                    a_scene.localObjectToEntity.find(localObjectId);
                    sceneIt != a_scene.localObjectToEntity.end())
                {
                    a_field.entityValue = sceneIt->second;
                }
            };

            for (ECS::ScriptFieldValue& field : script->serializedFieldValues)
            {
                resolveField(field);
            }
            for (ECS::ScriptFieldValue& field : script->transientFieldValues)
            {
                resolveField(field);
            }
        }

        void resolve_component_entity_references(
            EntityId a_entityId,
            const SceneInstance& a_scene,
            const std::unordered_map<LocalObjectId, EntityId>& a_newLocalObjectToEntity)
            noexcept
        {
            const auto resolveEntity =
                [&a_scene, &a_newLocalObjectToEntity](
                    EntityId a_entityValue) noexcept -> EntityId
            {
                if (a_entityValue == k_invalidEntityId)
                {
                    return a_entityValue;
                }

                const LocalObjectId localObjectId =
                    static_cast<LocalObjectId>(a_entityValue);
                if (const auto newIt =
                    a_newLocalObjectToEntity.find(localObjectId);
                    newIt != a_newLocalObjectToEntity.end())
                {
                    return newIt->second;
                }

                if (const auto sceneIt =
                    a_scene.localObjectToEntity.find(localObjectId);
                    sceneIt != a_scene.localObjectToEntity.end())
                {
                    return sceneIt->second;
                }

                return a_entityValue;
            };

            if (ECS::FirstPersonCameraControllerComponent* controller =
                get_component<ECS::FirstPersonCameraControllerComponent>(
                    a_entityId);
                controller != nullptr)
            {
                controller->targetEntity =
                    resolveEntity(controller->targetEntity);
            }

            if (ECS::DemoEnemyComponent* demoEnemy =
                get_component<ECS::DemoEnemyComponent>(a_entityId);
                demoEnemy != nullptr)
            {
                demoEnemy->targetEntity =
                    resolveEntity(demoEnemy->targetEntity);
            }

            if (ECS::NavAgentComponent* navAgent =
                get_component<ECS::NavAgentComponent>(a_entityId);
                navAgent != nullptr && navAgent->hasTarget)
            {
                navAgent->targetEntity =
                    resolveEntity(navAgent->targetEntity);
            }
        }

        [[nodiscard]] std::string get_object_tag(EntityId a_entityId) const
        {
            const BaseComponent* base = get_component<BaseComponent>(a_entityId);
            if (base == nullptr)
            {
                return {};
            }

            return base->tag;
        }

        [[nodiscard]] std::string get_object_name(EntityId a_entityId) const
        {
            const BaseComponent* base = get_component<BaseComponent>(a_entityId);
            if (base == nullptr)
            {
                return {};
            }

            return base->name;
        }

        [[nodiscard]] GameObjectProto build_object_prototype(
            EntityId a_entityId, const BaseComponent& a_base) const
        {
            GameObjectProto prototype(std::string(a_base.name), std::string(a_base.tag));

            if (const ECS::TransformComponent* transform =
                get_component<ECS::TransformComponent>(a_entityId);
                transform != nullptr)
            {
                prototype.add_component(*transform);
            }

            if (const ECS::CameraComponent* camera =
                get_component<ECS::CameraComponent>(a_entityId);
                camera != nullptr)
            {
                prototype.add_component(*camera);
            }

            if (const ECS::CanvasComponent* canvas =
                get_component<ECS::CanvasComponent>(a_entityId);
                canvas != nullptr)
            {
                prototype.add_component(*canvas);
            }

            if (const ECS::UiRectTransformComponent* rect =
                get_component<ECS::UiRectTransformComponent>(a_entityId);
                rect != nullptr)
            {
                ECS::UiRectTransformComponent copiedRect = *rect;
                copiedRect.resolvedMin = Math::float2(0.0f, 0.0f);
                copiedRect.resolvedSize = Math::float2(0.0f, 0.0f);
                copiedRect.isResolved = false;
                prototype.add_component(copiedRect);
            }

            if (const ECS::UiLayoutGroupComponent* layout =
                get_component<ECS::UiLayoutGroupComponent>(a_entityId);
                layout != nullptr)
            {
                prototype.add_component(*layout);
            }

            if (const ECS::TextRendererComponent* text =
                get_component<ECS::TextRendererComponent>(a_entityId);
                text != nullptr)
            {
                prototype.add_component(*text);
            }

            if (const ECS::UiImageComponent* image =
                get_component<ECS::UiImageComponent>(a_entityId);
                image != nullptr)
            {
                prototype.add_component(*image);
            }

            if (const ECS::UiButtonComponent* button =
                get_component<ECS::UiButtonComponent>(a_entityId);
                button != nullptr)
            {
                ECS::UiButtonComponent copiedButton = *button;
                copiedButton.isHovered = false;
                copiedButton.isPressed = false;
                copiedButton.wasClicked = false;
                copiedButton.hasFocus = false;
                prototype.add_component(copiedButton);
            }

            if (const ECS::UiCheckboxComponent* checkbox =
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

            if (const ECS::UiSliderComponent* slider =
                get_component<ECS::UiSliderComponent>(a_entityId);
                slider != nullptr)
            {
                ECS::UiSliderComponent copiedSlider = *slider;
                copiedSlider.isHovered = false;
                copiedSlider.isDragging = false;
                copiedSlider.wasChanged = false;
                copiedSlider.hasFocus = false;
                prototype.add_component(copiedSlider);
            }

            if (const ECS::DirectionalLightComponent* directionalLight =
                get_component<ECS::DirectionalLightComponent>(a_entityId);
                directionalLight != nullptr)
            {
                prototype.add_component(*directionalLight);
            }

            if (const ECS::PointLightComponent* pointLight =
                get_component<ECS::PointLightComponent>(a_entityId);
                pointLight != nullptr)
            {
                prototype.add_component(*pointLight);
            }

            if (const ECS::SpotLightComponent* spotLight =
                get_component<ECS::SpotLightComponent>(a_entityId);
                spotLight != nullptr)
            {
                prototype.add_component(*spotLight);
            }

            if (const ECS::FirstPersonCameraControllerComponent* controller =
                get_component<ECS::FirstPersonCameraControllerComponent>(
                    a_entityId);
                controller != nullptr)
            {
                ECS::FirstPersonCameraControllerComponent copiedController =
                    *controller;
                copiedController.targetEntity = localize_entity_reference(
                    copiedController.targetEntity, a_entityId);
                prototype.add_component(copiedController);
            }

            if (const ECS::MeshFilterComponent* meshFilter =
                get_component<ECS::MeshFilterComponent>(a_entityId);
                meshFilter != nullptr)
            {
                prototype.add_component(*meshFilter);
            }

            if (const ECS::NavAgentComponent* navAgent =
                get_component<ECS::NavAgentComponent>(a_entityId);
                navAgent != nullptr)
            {
                ECS::NavAgentComponent copiedNavAgent = *navAgent;
                if (copiedNavAgent.hasTarget)
                {
                    copiedNavAgent.targetEntity = localize_entity_reference(
                        copiedNavAgent.targetEntity, a_entityId);
                }
                copiedNavAgent.pathPoints.clear();
                copiedNavAgent.pathIndex = 0;
                copiedNavAgent.desiredVelocity = Math::float3::zero();
                copiedNavAgent.hasPath = false;
                prototype.add_component(copiedNavAgent);
            }

            if (const ECS::DemoEnemyComponent* demoEnemy =
                get_component<ECS::DemoEnemyComponent>(a_entityId);
                demoEnemy != nullptr)
            {
                ECS::DemoEnemyComponent copiedDemoEnemy = *demoEnemy;
                copiedDemoEnemy.targetEntity = localize_entity_reference(
                    copiedDemoEnemy.targetEntity, a_entityId);
                prototype.add_component(copiedDemoEnemy);
            }

            if (const ECS::NavMeshBakeSourceComponent* navMeshBakeSource =
                get_component<ECS::NavMeshBakeSourceComponent>(a_entityId);
                navMeshBakeSource != nullptr)
            {
                prototype.add_component(*navMeshBakeSource);
            }

            if (const ECS::StaticMeshRendererComponent* renderer =
                get_component<ECS::StaticMeshRendererComponent>(a_entityId);
                renderer != nullptr)
            {
                prototype.add_component(*renderer);
            }

            if (const ECS::SkinnedMeshRendererComponent* renderer =
                get_component<ECS::SkinnedMeshRendererComponent>(a_entityId);
                renderer != nullptr)
            {
                prototype.add_component(*renderer);
            }

            if (const ECS::AnimationComponent* animation =
                get_component<ECS::AnimationComponent>(a_entityId);
                animation != nullptr)
            {
                prototype.add_component(*animation);
            }

            if (const ECS::SpriteRendererComponent* spriteRenderer =
                get_component<ECS::SpriteRendererComponent>(a_entityId);
                spriteRenderer != nullptr)
            {
                prototype.add_component(*spriteRenderer);
            }

            if (const ECS::ParticleEmitterComponent* particleEmitter =
                get_component<ECS::ParticleEmitterComponent>(a_entityId);
                particleEmitter != nullptr)
            {
                ECS::ParticleEmitterComponent copiedParticleEmitter =
                    *particleEmitter;
                copiedParticleEmitter.runtimeParticleBase =
                    (std::numeric_limits<uint32_t>::max)();
                copiedParticleEmitter.runtimeParticleCapacity = 0;
                copiedParticleEmitter.runtimeSpawnCursor = 0;
                copiedParticleEmitter.runtimeEmitAccumulator = 0.0f;
                prototype.add_component(copiedParticleEmitter);
            }

            if (const ECS::EffectEmitterComponent* effectEmitter =
                get_component<ECS::EffectEmitterComponent>(a_entityId);
                effectEmitter != nullptr)
            {
                ECS::EffectEmitterComponent copiedEffectEmitter =
                    *effectEmitter;
                copiedEffectEmitter.runtimeEmitters.clear();
                prototype.add_component(copiedEffectEmitter);
            }

            if (const ECS::AudioSourceComponent* audioSource =
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

            if (const ECS::RigidBodyComponent* rigidBody =
                get_component<ECS::RigidBodyComponent>(a_entityId);
                rigidBody != nullptr)
            {
                ECS::RigidBodyComponent copiedRigidBody = *rigidBody;
                copiedRigidBody.body = {};
                copiedRigidBody.isCreated = false;
                prototype.add_component(copiedRigidBody);
            }

            if (const ECS::ColliderComponent* collider =
                get_component<ECS::ColliderComponent>(a_entityId);
                collider != nullptr)
            {
                prototype.add_component(*collider);
            }

            if (const ECS::TriggerVolumeComponent* trigger =
                get_component<ECS::TriggerVolumeComponent>(a_entityId);
                trigger != nullptr)
            {
                ECS::TriggerVolumeComponent copiedTrigger = *trigger;
                copiedTrigger.overlappingEntities.clear();
                copiedTrigger.enteredEntities.clear();
                copiedTrigger.exitedEntities.clear();
                prototype.add_component(copiedTrigger);
            }

            if (const ECS::InteractableComponent* interactable =
                get_component<ECS::InteractableComponent>(a_entityId);
                interactable != nullptr)
            {
                prototype.add_component(*interactable);
            }

            if (const ECS::CharacterControllerComponent* characterController =
                get_component<ECS::CharacterControllerComponent>(a_entityId);
                characterController != nullptr)
            {
                ECS::CharacterControllerComponent copiedCharacterController =
                    *characterController;
                copiedCharacterController.isGrounded = false;
                copiedCharacterController.jumpRequested = false;
                prototype.add_component(copiedCharacterController);
            }

            if (const ECS::ScriptComponent* script =
                get_component<ECS::ScriptComponent>(a_entityId);
                script != nullptr)
            {
                ECS::ScriptComponent copiedScript = *script;
                localize_script_entity_references(copiedScript, a_entityId);
                prototype.add_component(copiedScript);
            }

            return prototype;
        }

        [[nodiscard]] bool is_object_persistent(EntityId a_entityId) const noexcept
        {
            const BaseComponent* base = get_component<BaseComponent>(a_entityId);
            if (base == nullptr)
            {
                return false;
            }

            return base->isPersistent;
        }

        [[nodiscard]] bool is_alive(EntityId a_entityId,
            Generation a_generation) const noexcept
        {
            const EntityRecord* record = try_get_entity_record(a_entityId);
            return record != nullptr && record->isAlive &&
                record->generation == a_generation;
        }

        [[nodiscard]] SceneId source_scene_id(EntityId a_entityId) const noexcept
        {
            const EntityRecord* record = try_get_entity_record(a_entityId);
            if (record == nullptr || !record->isAlive)
            {
                return k_invalidSceneId;
            }

            return record->sourceSceneId;
        }

        template <typename T>
        [[nodiscard]] T* get_component(EntityId a_entityId) noexcept
        {
            if (!contains_object(a_entityId))
            {
                return nullptr;
            }

            return m_ecs.get_component<T>(a_entityId);
        }

        template <typename T>
        [[nodiscard]] const T* get_component(EntityId a_entityId) const noexcept
        {
            return const_cast<GameWorld*>(this)->get_component<T>(a_entityId);
        }

        template <typename T, typename... Args>
        T& add_component(EntityId a_entityId, Args&&... a_args)
        {
            if (!contains_object(a_entityId))
            {
                throw std::runtime_error("GameWorld object is not alive.");
            }

            T* component = m_ecs.add_component<T>(a_entityId);
            if (component == nullptr)
            {
                throw std::runtime_error("GameWorld failed to add component.");
            }

            *component = T{ std::forward<Args>(a_args)... };
            return *component;
        }

        template <typename T>
        [[nodiscard]] bool has_component(EntityId a_entityId) const noexcept
        {
            return get_component<T>(a_entityId) != nullptr;
        }

        [[nodiscard]] std::vector<GameObject> find_objects_by_tag(
            std::string_view a_tag)
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
                [](const GameObject& a_left, const GameObject& a_right) {
                    return a_left.entity_id() < a_right.entity_id();
                });

            return objects;
        }

        [[nodiscard]] std::vector<GameObject> find_objects_by_name(
            std::string_view a_name)
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
                [](const GameObject& a_left, const GameObject& a_right) {
                    return a_left.entity_id() < a_right.entity_id();
                });

            return objects;
        }

        [[nodiscard]] GameObject find_object_by_name(std::string_view a_name)
        {
            std::vector<GameObject> objects = find_objects_by_name(a_name);
            if (objects.empty())
            {
                return {};
            }

            return objects.front();
        }

        [[nodiscard]] std::vector<GameObject> find_objects_by_name_series(
            std::string_view a_baseName)
        {
            const std::string normalizedBaseName =
                normalize_object_name(a_baseName);

            std::vector<GameObject> objects{};
            for (const auto& [name, entityIds] : m_nameIndex)
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
                [this, &normalizedBaseName](
                    const GameObject& a_left, const GameObject& a_right) {
                        std::uint32_t leftSeriesIndex = 0;
                        std::uint32_t rightSeriesIndex = 0;
                        const bool leftMatched = try_get_name_series_index(
                            get_object_name(a_left.entity_id()), normalizedBaseName,
                            leftSeriesIndex);
                        const bool rightMatched = try_get_name_series_index(
                            get_object_name(a_right.entity_id()), normalizedBaseName,
                            rightSeriesIndex);

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

        [[nodiscard]] SceneId generate_scene_id()
        {
            if (m_nextSceneId == 0)
            {
                throw std::overflow_error("GameWorld scene id overflow.");
            }

            const SceneId sceneId = m_nextSceneId;
            ++m_nextSceneId;
            return sceneId;
        }

        [[nodiscard]] EntityId create_entity_record(SceneId a_sourceSceneId,
            LocalObjectId a_localObjectId)
        {
            // ECS の Entity と GameWorld の管理情報を対応付ける。
            const EntityId entity = m_ecs.generate_entity();

            if (m_entityRecords.size() <= entity)
            {
                m_entityRecords.resize(entity + 1);
            }

            EntityRecord& record = m_entityRecords[entity];
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

        void initialize_base_component(EntityId a_entityId,
            std::string_view a_name, std::string_view a_tag,
            SceneId a_owningSceneId, EntityId a_parent, bool a_isActive,
            bool a_isPersistent)
        {
            BaseComponent* base = m_ecs.get_component<BaseComponent>(a_entityId);
            ECS::RenderableInfoComponent* renderableInfo =
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
                renderableInfo =
                    m_ecs.add_component<ECS::RenderableInfoComponent>(a_entityId);
            }

            if (base == nullptr || renderableInfo == nullptr)
            {
                throw std::runtime_error(
                    "GameWorld failed to initialize BaseComponent.");
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

        [[nodiscard]] LoadSceneResult instantiate_into_scene(SceneId a_sceneId,
            std::span<const ObjectDefinition> a_objects,
            const SceneAsset* a_asset)
        {
            // ObjectDefinition 群を実 Entity として生成し、Scene に紐付ける。
            auto sceneIt = m_scenes.find(a_sceneId);
            if (sceneIt == m_scenes.end())
            {
                throw std::runtime_error("GameWorld scene was not found.");
            }

            SceneInstance& scene = sceneIt->second;
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
                const ObjectDefinition* definition = nullptr;
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
                for (const ObjectDefinition& object : a_objects)
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

                        scene.nextLocalObjectId = (std::max)(scene.nextLocalObjectId,
                            localObjectId + 1);
                    }

                    const EntityId entity =
                        create_entity_record(a_sceneId, localObjectId);
                    createdEntities.push_back(entity);

                    object.prototype.restore_components_into(entity, m_ecs);

                    const SceneId owningSceneId = object.isPersistent
                        ? k_invalidSceneId
                        : a_sceneId;
                    initialize_base_component(entity, object.name(), object.tag(),
                        owningSceneId, k_invalidEntityId, object.isActive,
                        object.isPersistent);

                    scene.entities.push_back(entity);
                    scene.localObjectToEntity.emplace(localObjectId, entity);
                    newLocalObjectToEntity.emplace(localObjectId, entity);

                    pending.push_back({ &object, localObjectId, entity });
                    result.objects.push_back(make_handle(entity));
                }

                for (const PendingObjectInstantiation& entry : pending)
                {
                    resolve_script_entity_references(
                        entry.entityId, scene, newLocalObjectToEntity);
                    resolve_component_entity_references(
                        entry.entityId, scene, newLocalObjectToEntity);

                    if (!entry.definition->parentLocalObjectId.has_value())
                    {
                        continue;
                    }

                    const LocalObjectId parentLocalObjectId =
                        *entry.definition->parentLocalObjectId;
                    EntityId parentEntity = k_invalidEntityId;

                    if (const auto newIt =
                        newLocalObjectToEntity.find(parentLocalObjectId);
                        newIt != newLocalObjectToEntity.end())
                    {
                        parentEntity = newIt->second;
                    }
                    else if (const auto sceneLocalIt =
                        scene.localObjectToEntity.find(parentLocalObjectId);
                        sceneLocalIt != scene.localObjectToEntity.end())
                    {
                        parentEntity = sceneLocalIt->second;
                    }
                    else
                    {
                        throw std::runtime_error(
                            "GameWorld parentLocalObjectId could not be resolved.");
                    }

                    BaseComponent* base = get_component<BaseComponent>(entry.entityId);
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
                for (const EntityId entity : createdEntities)
                {
                    destroy_object_immediately(entity);
                }
                throw;
            }
        }

        void destroy_object_immediately(EntityId a_entityId) noexcept
        {
            EntityRecord* record = try_get_entity_record(a_entityId);
            if (record == nullptr || !record->isAlive)
            {
                return;
            }

            // flush 実行時と、例外時に即座に巻き戻す必要がある経路で使う。
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

        [[nodiscard]] bool unload_scene_immediately(SceneId a_sceneId) noexcept
        {
            auto sceneIt = m_scenes.find(a_sceneId);
            if (sceneIt == m_scenes.end())
            {
                return false;
            }

            // 遅延状態を解除し、ここで実際の Scene アンロードを行う。
            sceneIt->second.isPendingUnload = false;

            const std::vector<EntityId> entities = sceneIt->second.entities;
            for (const EntityId entity : entities)
            {
                if (!contains_object(entity))
                {
                    continue;
                }

                BaseComponent* base = get_component<BaseComponent>(entity);
                if (base != nullptr && base->isPersistent)
                {
                    if (base->parent != k_invalidEntityId &&
                        source_scene_id(base->parent) == a_sceneId)
                    {
                        base->parent = k_invalidEntityId;
                    }
                    base->owningSceneId = k_invalidSceneId;

                    if (EntityRecord* record = try_get_entity_record(entity))
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

        [[nodiscard]] bool unlink_object_from_scene(EntityId a_entityId) noexcept
        {
            EntityRecord* record = try_get_entity_record(a_entityId);
            if (record == nullptr || record->sourceSceneId == k_invalidSceneId)
            {
                return false;
            }

            auto sceneIt = m_scenes.find(record->sourceSceneId);
            if (sceneIt == m_scenes.end())
            {
                return false;
            }

            std::vector<EntityId>& entities = sceneIt->second.entities;
            const auto entityIt =
                std::find(entities.begin(), entities.end(), a_entityId);
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

        [[nodiscard]] GameObject make_handle(EntityId a_entityId) noexcept
        {
            EntityRecord* record = try_get_entity_record(a_entityId);
            if (record == nullptr || !record->isAlive)
            {
                return {};
            }

            return GameObject(this, a_entityId, record->generation);
        }

        [[nodiscard]] EntityRecord* try_get_entity_record(
            EntityId a_entityId) noexcept
        {
            if (a_entityId >= m_entityRecords.size())
            {
                return nullptr;
            }

            return &m_entityRecords[a_entityId];
        }

        [[nodiscard]] const EntityRecord* try_get_entity_record(
            EntityId a_entityId) const noexcept
        {
            if (a_entityId >= m_entityRecords.size())
            {
                return nullptr;
            }

            return &m_entityRecords[a_entityId];
        }

        void add_object_to_tag_index(EntityId a_entityId, const std::string& a_tag)
        {
            m_tagIndex[a_tag].insert(a_entityId);
        }

        void add_object_to_name_index(EntityId a_entityId, const std::string& a_name)
        {
            m_nameIndex[a_name].insert(a_entityId);
        }

        [[nodiscard]] std::string normalize_object_name(
            std::string_view a_name) const
        {
            if (a_name.empty())
            {
                return "GameObject";
            }

            return std::string(a_name);
        }

        [[nodiscard]] bool is_name_taken(std::string_view a_name,
            EntityId a_ignoredEntityId = k_invalidEntityId) const
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

        [[nodiscard]] std::string make_unique_object_name(
            std::string_view a_requestedName,
            EntityId a_ignoredEntityId = k_invalidEntityId) const
        {
            const std::string baseName = normalize_object_name(a_requestedName);
            if (!is_name_taken(baseName, a_ignoredEntityId))
            {
                return baseName;
            }

            std::uint32_t suffix = 1;
            while (true)
            {
                const std::string candidate =
                    baseName + "(" + std::to_string(suffix) + ")";
                if (!is_name_taken(candidate, a_ignoredEntityId))
                {
                    return candidate;
                }
                ++suffix;
            }
        }

        [[nodiscard]] bool try_get_name_series_index(const std::string& a_name,
            std::string_view a_baseName, std::uint32_t& a_outSeriesIndex) const
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

                seriesIndex =
                    (seriesIndex * 10u) + static_cast<std::uint32_t>(ch - '0');
            }

            a_outSeriesIndex = seriesIndex;
            return true;
        }

        void remove_object_from_tag_index(EntityId a_entityId,
            const std::string& a_tag)
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

        void remove_object_from_name_index(EntityId a_entityId,
            const std::string& a_name)
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

        [[nodiscard]] bool find_entity_by_body(
            Physics::RigidBodyHandle a_body,
            EntityId& a_outEntity) const noexcept
        {
            a_outEntity = k_invalidEntityId;
            if (!a_body.valid())
            {
                return false;
            }

            for (EntityId entity = 0;
                 entity < static_cast<EntityId>(m_entityRecords.size());
                 ++entity)
            {
                if (!contains_object(entity))
                {
                    continue;
                }

                const ECS::RigidBodyComponent* rigidBody =
                    get_component<ECS::RigidBodyComponent>(entity);
                if (rigidBody != nullptr && rigidBody->body == a_body)
                {
                    a_outEntity = entity;
                    return true;
                }
            }

            return false;
        }

        ECS::ECSManager m_ecs{};
        ECS::ECSManager::SystemPipeline m_editorPipeline{};
        ECS::ECSManager::SystemPipeline m_simulationPipeline{};
        NavigationWorld m_navigationWorld{};
        ECS::NavigationSystem* m_navigationSystem = nullptr;
        NavMeshHandle m_activeNavMesh{};
        NavMeshAssetData m_activeNavMeshAsset{};
        std::unique_ptr<DrawSystem::DrawResources> m_drawResources = nullptr;
        std::unique_ptr<LightingSystem::LightResources> m_lightResources = nullptr;
        std::unique_ptr<ShadowSystem::ShadowResources> m_shadowResources = nullptr;
        std::unique_ptr<ParticleSystem::ParticleResources> m_particleResources = nullptr;
        AssetManager* m_assetManager = nullptr;
        Core::IO::IFileSystem* m_fileSystem = nullptr;
        Audio::IBackend* m_audioBackend = nullptr;
        Physics::IPhysicsSystem* m_physicsSystem = nullptr;
        PAL::InputManager* m_inputManager = nullptr;
        Audio::AudioDeviceHandle m_audioDevice{};
        DebugDrawBuffer m_debugDraw{};
        Core::IO::Path m_assetRootPath{};
        bool m_isCpuBatchingEnabled = false;
        bool m_hasActiveNavMeshAsset = false;
        DrawSystem::FontAtlasManager m_fontAtlasManager{};
        DrawSystem::DrawScene m_drawScene{};
        DrawSystem::DrawFrameState m_drawFrameState{};
        ParticleSystem::ParticleScene m_particleScene{};
        ParticleSystem::ParticleFrameState m_particleFrameState{};
        ParticleSystem::ParticleRangeAllocator m_particleRangeAllocator{};
        uint32_t m_particleTrailFrameIndex = 0;
        LightingSystem::LightScene m_lightScene{};
        LightingSystem::LightFrameState m_lightFrameState{};
        ShadowSystem::ShadowScene m_shadowScene{};
        ShadowSystem::ShadowFrameState m_shadowFrameState{};
        MaterialHandle m_defaultMaterialHandle{};
        std::unordered_map<SceneId, SceneInstance> m_scenes{};
        std::unordered_map<SceneId, std::unique_ptr<SceneAsset>> m_ownedSceneAssets{};
        std::unordered_map<std::string, std::unordered_set<EntityId>> m_nameIndex{};
        std::unordered_map<std::string, std::unordered_set<EntityId>> m_tagIndex{};
        std::vector<EntityRecord> m_entityRecords{};
        // 公開 API の遅延削除要求を一時的に保持するキュー。
        std::vector<EntityId> m_pendingDestroyedEntities{};
        std::vector<PendingSceneLoad> m_pendingLoadedScenes{};
        std::vector<SceneId> m_pendingUnloadedScenes{};
        SceneId m_nextSceneId = 1;
        size_t m_liveObjectCount = 0;
        uint32_t m_mainCameraIndex = 0;
        uint32_t m_defaultStaticMeshId = ECS::k_invalidMeshId;
    };

    inline GameObject::GameObject(GameWorld* a_world, EntityId a_entityId,
        Generation a_generation) noexcept
        : m_world(a_world),
        m_entityId(a_entityId),
        m_generation(a_generation)
    {}

    inline bool GameObject::is_valid() const noexcept
    {
        if (m_world == nullptr)
        {
            return false;
        }

        bool isAlive = false;
        const Result result = m_world->is_alive(m_entityId, m_generation, isAlive);
        return result && isAlive;
    }

    inline Result GameObject::name(std::string& a_outName) const
    {
        if (!is_valid())
        {
            a_outName.clear();
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->get_object_name(m_entityId, a_outName);
    }

    inline Result GameObject::set_name(std::string_view a_name)
    {
        if (!is_valid())
        {
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->set_object_name(m_entityId, a_name);
    }

    inline Result GameObject::tag(std::string& a_outTag) const
    {
        if (!is_valid())
        {
            a_outTag.clear();
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->get_object_tag(m_entityId, a_outTag);
    }

    inline Result GameObject::set_tag(std::string_view a_tag)
    {
        if (!is_valid())
        {
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->set_object_tag(m_entityId, a_tag);
    }

    inline Result GameObject::is_active(bool& a_outIsActive) const
    {
        if (!is_valid())
        {
            a_outIsActive = false;
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->is_object_active(m_entityId, a_outIsActive);
    }

    inline Result GameObject::set_active(bool a_isActive)
    {
        if (!is_valid())
        {
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->set_object_active(m_entityId, a_isActive);
    }

    inline Result GameObject::is_persistent(bool& a_outIsPersistent) const
    {
        if (!is_valid())
        {
            a_outIsPersistent = false;
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->is_object_persistent(m_entityId, a_outIsPersistent);
    }

    inline Result GameObject::set_persistent(bool a_isPersistent)
    {
        if (!is_valid())
        {
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->set_object_persistent(m_entityId, a_isPersistent);
    }

    template <typename T>
    inline Result GameObject::get_component(T*& a_outComponent) noexcept
    {
        if (!is_valid())
        {
            a_outComponent = nullptr;
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->get_component<T>(m_entityId, a_outComponent);
    }

    template <typename T, typename... Args>
    inline Result GameObject::add_component(T*& a_outComponent, Args&&... a_args)
    {
        if (!is_valid())
        {
            a_outComponent = nullptr;
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->add_component<T>(
            m_entityId, a_outComponent, std::forward<Args>(a_args)...);
    }

    template <typename T>
    inline Result GameObject::has_component(bool& a_outHasComponent) const noexcept
    {
        if (!is_valid())
        {
            a_outHasComponent = false;
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->has_component<T>(m_entityId, a_outHasComponent);
    }

    template <typename T>
    inline Result GameObject::remove_component() noexcept
    {
        if (!is_valid())
        {
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->remove_component<T>(m_entityId);
    }

    inline Result GameObject::destroy() noexcept
    {
        if (!is_valid())
        {
            return Result::fail(
                Code::InvalidState, Severity::Warning, "GameObject is not valid.");
        }

        return m_world->destroy_object(m_entityId);
    }
}
