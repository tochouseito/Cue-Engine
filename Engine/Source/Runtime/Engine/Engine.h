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
#include "EngineCommandContext.h"
#include "FrameController.h"
#include "GameCore/GameWorld.h"
#include "Script/ScriptModuleHost.h"

// === C++ includes ===
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace Cue
{
    struct MarionnetteClass;

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

        GameCore::GameWorld* editor_world() noexcept
        {
            return m_editorWorld.get();
        }

        const GameCore::GameWorld* editor_world() const noexcept
        {
            return m_editorWorld.get();
        }

        GameCore::GameWorld* play_world() noexcept
        {
            return m_playWorld.get();
        }

        const GameCore::GameWorld* play_world() const noexcept
        {
            return m_playWorld.get();
        }

        GameCore::GameWorld* active_world() noexcept
        {
            return m_activeWorld;
        }

        const GameCore::GameWorld* active_world() const noexcept
        {
            return m_activeWorld;
        }

        GameCore::GameWorld* game_world() noexcept
        {
            return editor_world();
        }

        const GameCore::GameWorld* game_world() const noexcept
        {
            return editor_world();
        }

        void set_editor_scene_id(GameCore::SceneId a_sceneId) noexcept
        {
            m_editorSceneId = a_sceneId;
        }

        [[nodiscard]] GameCore::SceneId editor_scene_id() const noexcept
        {
            return m_editorSceneId;
        }

        [[nodiscard]] const std::vector<std::string>&
            registered_script_classes() const noexcept
        {
            static const std::vector<std::string> k_emptyClasses{};
            return m_scriptModuleHost != nullptr
                ? m_scriptModuleHost->registered_script_classes()
                : k_emptyClasses;
        }

        [[nodiscard]] bool has_registered_script_class(
            std::string_view a_className) const noexcept
        {
            return m_scriptModuleHost != nullptr &&
                m_scriptModuleHost->has_registered_script_class(a_className);
        }

        [[nodiscard]] const std::vector<ECS::ScriptFieldValue>&
            script_field_defaults(std::string_view a_className) const noexcept
        {
            static const std::vector<ECS::ScriptFieldValue> k_emptyFieldValues{};
            return m_scriptModuleHost != nullptr
                ? m_scriptModuleHost->script_field_defaults(a_className)
                : k_emptyFieldValues;
        }

        [[nodiscard]] const MarionnetteClass* find_marionnette_class(
            std::string_view a_className) const noexcept
        {
            return m_scriptModuleHost != nullptr
                ? m_scriptModuleHost->find_marionnette_class(a_className)
                : nullptr;
        }

        [[nodiscard]] const ScriptModuleHost::ScriptReloadReport&
            last_script_reload_report() const noexcept
        {
            static const ScriptModuleHost::ScriptReloadReport k_empty{};
            return m_scriptModuleHost != nullptr
                ? m_scriptModuleHost->last_reload_report()
                : k_empty;
        }

        [[nodiscard]] Result load_script_module(
            const Core::IO::Path& a_scriptRoot) noexcept;
        void unload_script_module() noexcept;
        [[nodiscard]] Result start_play_mode() noexcept;
        [[nodiscard]] Result stop_play_mode() noexcept;
        [[nodiscard]] bool is_playing() const noexcept;

    private:
        Result create_final_color_resources();
        Result destroy_final_color_resources();
        Result create_frame_graphs(std::unique_ptr<RHI::FrameGraphPass> a_editorPass);
        Result recreate_render_frame_graph();
        Result sync_active_world_buffers();
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
        std::unique_ptr<GameCore::GameWorld> m_editorWorld = nullptr;
        std::unique_ptr<GameCore::GameWorld> m_playWorld = nullptr;
        GameCore::GameWorld* m_activeWorld = nullptr;
        std::unique_ptr<ScriptModuleHost> m_scriptModuleHost = nullptr;
        Core::CQRS::Bridge* m_editorBridge = nullptr;
        Core::CQRS::Bridge* m_platformBridge = nullptr;
        PAL::PlatformRuntimeState m_platformRuntimeState{};
        RHI::TextureHandle m_finalColorHandle{};
        RHI::ViewHandle m_finalColorRtvHandle{};
        RHI::ViewHandle m_finalColorSrvHandle{};
        uint32_t m_cubeIndexCount = 0;
        uint32_t m_defaultCubeMeshId = ECS::k_invalidMeshId;
        GameCore::SceneId m_editorSceneId = GameCore::k_invalidSceneId;
        Core::IO::Path m_scriptRoot{};
    };
} // namespace Cue
