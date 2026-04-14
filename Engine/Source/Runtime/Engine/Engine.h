#pragma once

// === Core includes ===
#include <CQRS/CQRS.h>

// === PAL includes ===
#include <PAL.h>
#include <PlatformRuntimeState.h>

// === RHI includes ===
#include <FrameGraph.h>
#include <RHI.h>

// === Engine includes ===
#include "Asset/AssetManager.h"
#include "FrameController.h"
#include "Commands.h"
#include "GameCore/GameWorld.h"

// === C++ includes ===
#include <functional>
#include <memory>
#include <vector>

namespace Cue
{
    class EngineCommandContext final : public IGameCommandContext
    {
    public:
        explicit EngineCommandContext(GameCore::GameWorld& a_gameWorld,
            GameCore::SceneId a_currentSceneId = GameCore::k_invalidSceneId) noexcept
            : m_gameWorld(a_gameWorld)
            , m_currentSceneId(a_currentSceneId)
        {
        }

        Result create_object(GameCore::EntityId& a_outObjectId) override
        {
            GameCore::GameObject object{};
            Result result = m_currentSceneId != GameCore::k_invalidSceneId
                ? m_gameWorld.add_object_to_scene(m_currentSceneId, object)
                : m_gameWorld.add_object(object);
            a_outObjectId = result ? object.entity_id() : GameCore::k_invalidEntityId;
            return result;
        }

        Result destroy_object(GameCore::EntityId a_objectId) override
        {
            return m_gameWorld.destroy_object(a_objectId);
        }

        Result resolve_render_object_entity(
            uint32_t a_objectId, GameCore::EntityId& a_outEntityId) override
        {
            return m_gameWorld.get_render_object_entity(a_objectId, a_outEntityId);
        }

        Result set_main_camera(uint32_t a_cameraIndex) override
        {
            return m_gameWorld.set_main_camera(a_cameraIndex);
        }

        Result get_object_name(
            GameCore::EntityId a_objectId, std::string& a_outName) override
        {
            return m_gameWorld.get_object_name(a_objectId, a_outName);
        }

        Result rename_object(
            GameCore::EntityId a_objectId, std::string_view a_name) override
        {
            return m_gameWorld.set_object_name(a_objectId, a_name);
        }

        Result capture_deleted_object(
            GameCore::EntityId a_objectId,
            GameCore::DeletedObjectSnapshot& a_outSnapshot) override
        {
            return m_gameWorld.capture_deleted_object(a_objectId, a_outSnapshot);
        }

        Result restore_deleted_object(
            const GameCore::DeletedObjectSnapshot& a_snapshot,
            GameCore::EntityId& a_outObjectId) override
        {
            return m_gameWorld.restore_deleted_object(a_snapshot, a_outObjectId);
        }

        Result add_component(GameCore::EntityId a_objectId,
            AddableComponentType a_componentType) override
        {
            switch (a_componentType)
            {
            case AddableComponentType::Camera:
                return add_component_internal<ECS::CameraComponent>(
                    a_objectId, "CameraComponent already exists.");

            case AddableComponentType::MeshFilter:
                return add_component_internal<ECS::MeshFilterComponent>(
                    a_objectId, "MeshFilterComponent already exists.");

            case AddableComponentType::StaticMeshRenderer:
                return add_component_internal<ECS::StaticMeshRendererComponent>(
                    a_objectId,
                    "StaticMeshRendererComponent already exists.");
            }

            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Unknown component type was requested.");
        }

        Result remove_component(GameCore::EntityId a_objectId,
            AddableComponentType a_componentType) override
        {
            switch (a_componentType)
            {
            case AddableComponentType::Camera:
                return remove_component_internal<ECS::CameraComponent>(
                    a_objectId, "CameraComponent was not found.");

            case AddableComponentType::MeshFilter:
                return remove_component_internal<ECS::MeshFilterComponent>(
                    a_objectId, "MeshFilterComponent was not found.");

            case AddableComponentType::StaticMeshRenderer:
                return remove_component_internal<ECS::StaticMeshRendererComponent>(
                    a_objectId,
                    "StaticMeshRendererComponent was not found.");
            }

            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Unknown component type was requested.");
        }

    private:
        template <typename T>
        Result add_component_internal(GameCore::EntityId a_objectId,
            const char* a_alreadyExistsMessage)
        {
            bool hasComponent = false;
            Result hasResult = m_gameWorld.has_component<T>(a_objectId, hasComponent);
            if (!hasResult)
            {
                return hasResult;
            }

            if (hasComponent)
            {
                return Result::fail(Code::InvalidState, Severity::Warning,
                    a_alreadyExistsMessage);
            }

            T* component = nullptr;
            return m_gameWorld.add_component<T>(a_objectId, component);
        }

        template <typename T>
        Result remove_component_internal(GameCore::EntityId a_objectId,
            const char* a_notFoundMessage)
        {
            bool hasComponent = false;
            Result hasResult = m_gameWorld.has_component<T>(a_objectId, hasComponent);
            if (!hasResult)
            {
                return hasResult;
            }

            if (!hasComponent)
            {
                return Result::fail(Code::NotFound, Severity::Warning,
                    a_notFoundMessage);
            }

            return m_gameWorld.remove_component<T>(a_objectId);
        }

        GameCore::GameWorld& m_gameWorld;
        GameCore::SceneId m_currentSceneId = GameCore::k_invalidSceneId;
    };

    /// @brief Engine 初期化時に必要な依存オブジェクトです。
    struct EngineSetupInfo final
    {
        PAL::IPlatform* platform = nullptr;
        RHI::IBackend* backend = nullptr;
        uint32_t maxFps = 60;

        std::unique_ptr<RHI::FrameGraphPass> editorPass = nullptr;
        Core::CQRS::Bridge* editorBridge = nullptr;
        Core::CQRS::Bridge* platformBridge = nullptr;
    };

    /// @brief Runtime 全体の統合窓口です。
    class Engine final
    {
    public:
        Engine() = default;
        // コピー禁止
        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;
        // ムーブ禁止
        Engine(Engine&&) = delete;
        Engine& operator=(Engine&&) = delete;
        ~Engine() = default;

        /// @brief 初期化
        Result initialize(EngineSetupInfo& a_info);

        /// @brief 終了
        void shutdown();

        /// @brief フレーム開始処理
        Result begin_frame();

        /// @brief フレーム終了処理
        Result end_frame();

        /// @brief ティック処理
        Result tick();

        FrameController& frame_controller() noexcept
        {
            return *m_frameController;
        }

        AssetManager& asset_manager() noexcept
        {
            return m_assetManager;
        }

        GameCore::GameWorld* game_world() noexcept
        {
            return m_gameWorld.get();
        }

        const GameCore::GameWorld* game_world() const noexcept
        {
            return m_gameWorld.get();
        }

        void set_editor_scene_id(GameCore::SceneId a_sceneId) noexcept
        {
            m_editorSceneId = a_sceneId;
        }

        [[nodiscard]] GameCore::SceneId editor_scene_id() const noexcept
        {
            return m_editorSceneId;
        }

    private:
        Result create_final_color_resources();
        Result destroy_final_color_resources();
        Result create_frame_graphs(std::unique_ptr<RHI::FrameGraphPass> a_editorPass);
        Result destroy_size_dependent_resources();
        Result apply_pending_resize();

        /// @brief 更新
        std::function<void(uint64_t, uint32_t)> update();
        /// @brief 描画
        std::function<void(uint64_t, uint32_t)> render();
        /// @brief present
        std::function<void(uint64_t, uint32_t)> present();

    private:
        PAL::IPlatform* m_platform = nullptr;
        RHI::IBackend* m_backend = nullptr;
        AssetManager m_assetManager{};
        std::unique_ptr<FrameController> m_frameController = nullptr;
        std::unique_ptr<RHI::FrameGraph> m_frameGraph = nullptr;
        std::unique_ptr<RHI::FrameGraph> m_presentFrameGraph = nullptr;
        std::unique_ptr<GameCore::GameWorld> m_gameWorld = nullptr;
        Core::CQRS::Bridge* m_editorBridge = nullptr;
        Core::CQRS::Bridge* m_platformBridge = nullptr;
        PAL::PlatformRuntimeState m_platformRuntimeState{};
        RHI::TextureHandle m_finalColorHandle{};
        RHI::ViewHandle m_finalColorRtvHandle{};
        RHI::ViewHandle m_finalColorSrvHandle{};
        uint32_t m_cubeIndexCount = 0;
        uint32_t m_defaultCubeMeshId = ECS::k_invalidMeshId;
        GameCore::SceneId m_editorSceneId = GameCore::k_invalidSceneId;
    };
} // namespace Cue
