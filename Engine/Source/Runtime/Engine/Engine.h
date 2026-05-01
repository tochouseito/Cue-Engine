#pragma once

// === Core includes ===
#include <CQRS/CQRS.h>
#include <Native/ScriptAbi.h>

// === PAL includes ===
#include <PAL.h>
#include <PlatformRuntimeState.h>

// === Audio includes ===
#include <Audio.h>

// === RHI includes ===
#include <FrameGraph.h>
#include <RHI.h>

// === Physics includes ===
#include <Physics.h>

// === Engine includes ===
#include "Asset/AssetManager.h"
#include "EngineCommandContext.h"
#include "FrameController.h"
#include "GameCore/GameWorld.h"
#include "GpuData/DebugSelection.h"
#include "GpuData/DebugPick.h"
#include "GpuData/ViewProjection.h"
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
        Audio::IBackend* audioBackend = nullptr;
        Physics::IPhysicsSystem* physicsSystem = nullptr;
        uint32_t maxFps = 60;
        Core::IO::Path errorTexturePath{};

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

        void set_asset_root_path(const Core::IO::Path& a_assetRootPath) noexcept
        {
            m_assetRootPath = a_assetRootPath.normalize();
            if (m_editorWorld != nullptr)
            {
                m_editorWorld->set_asset_root_path(m_assetRootPath);
            }
            if (m_playWorld != nullptr)
            {
                m_playWorld->set_asset_root_path(m_assetRootPath);
            }
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
            const Core::IO::Path& a_scriptRoot,
            ScriptModuleBuildConfiguration a_configuration) noexcept;
        [[nodiscard]] Result load_static_script_module(
            CueScriptAbiVersion(CUE_SCRIPT_CALL* a_getAbiVersion)(void),
            CueResult(CUE_SCRIPT_CALL* a_getExports)(CueScriptExports*)) noexcept;
        void unload_script_module() noexcept;
        [[nodiscard]] Result start_play_mode() noexcept;
        [[nodiscard]] Result stop_play_mode() noexcept;
        [[nodiscard]] bool is_playing() const noexcept;

        void set_debug_view_camera(
            const GpuData::ViewProjectionGpu& a_viewProjection) noexcept
        {
            m_debugViewProjection = a_viewProjection;
        }

        void set_debug_selection(
            const GpuData::DebugSelectionGpu& a_selection) noexcept
        {
            m_debugSelection = a_selection;
        }

        void set_debug_selected_object_id(uint32_t a_objectId) noexcept
        {
            m_debugSelectedObjectId = a_objectId;
        }

        void request_debug_pick(float a_normalizedX, float a_normalizedY) noexcept;
        void cancel_debug_pick() noexcept;
        [[nodiscard]] bool consume_debug_pick_result(
            GameCore::EntityId& a_outEntityId) noexcept;

        [[nodiscard]] RHI::FrameGraphExecutionStats
            render_frame_graph_stats() const noexcept
        {
            static const RHI::FrameGraphExecutionStats k_emptyStats{};
            return m_frameGraph != nullptr
                ? m_frameGraph->execution_stats_copy()
                : k_emptyStats;
        }

        [[nodiscard]] RHI::FrameGraphExecutionStats
            render_frame_graph_summary_stats() const noexcept
        {
            static const RHI::FrameGraphExecutionStats k_emptyStats{};
            return m_frameGraph != nullptr
                ? m_frameGraph->execution_stats_summary_copy()
                : k_emptyStats;
        }

        [[nodiscard]] RHI::FrameGraphExecutionStats
            present_frame_graph_stats() const noexcept
        {
            static const RHI::FrameGraphExecutionStats k_emptyStats{};
            return m_presentFrameGraph != nullptr
                ? m_presentFrameGraph->execution_stats_copy()
                : k_emptyStats;
        }

        [[nodiscard]] RHI::FrameGraphExecutionStats
            present_frame_graph_summary_stats() const noexcept
        {
            static const RHI::FrameGraphExecutionStats k_emptyStats{};
            return m_presentFrameGraph != nullptr
                ? m_presentFrameGraph->execution_stats_summary_copy()
                : k_emptyStats;
        }

        [[nodiscard]] Audio::IBackend* audio_backend() noexcept
        {
            return m_audioBackend;
        }

        [[nodiscard]] const Audio::IBackend* audio_backend() const noexcept
        {
            return m_audioBackend;
        }

        [[nodiscard]] Audio::AudioDeviceHandle default_audio_device() const noexcept
        {
            return m_audioDevice;
        }

    private:
        struct RenderTargetResources final
        {
            RHI::TextureHandle colorHandle{};
            RHI::ViewHandle colorRtvHandle{};
            RHI::ViewHandle colorSrvHandle{};
        };

        Result create_render_target_resources(
            std::string_view a_name,
            RHI::ColorFormat a_format,
            RenderTargetResources& a_outResources);
        Result destroy_render_target_resources(
            RenderTargetResources& a_resources);
        Result create_debug_pick_readback_buffer();
        Result destroy_debug_pick_readback_buffer();
        Result create_view_projection_buffer(
            std::string_view a_name,
            RHI::BufferHandle& a_outBufferHandle,
            std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>&
                a_outUploaders);
        Result create_debug_selection_buffer();
        Result destroy_debug_view_projection_buffer();
        Result destroy_debug_selection_buffer();
        Result upload_debug_view_projection(uint32_t a_bufferIndex);
        Result upload_debug_selection(uint32_t a_bufferIndex);
        void resolve_debug_pick_readback() noexcept;
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
        Audio::IBackend* m_audioBackend = nullptr;
        Physics::IPhysicsSystem* m_physicsSystem = nullptr;
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
        RenderTargetResources m_gameRenderTarget{};
        RenderTargetResources m_debugRenderTarget{};
        RenderTargetResources m_debugObjectIdTarget{};
        RenderTargetResources m_debugOutlineObjectIdTarget{};
        RHI::BufferHandle m_debugViewProjectionBufferHandle{};
        std::vector<RHI::SlotUploader<GpuData::ViewProjectionGpu>>
            m_debugViewProjectionUploaders{};
        GpuData::ViewProjectionGpu m_debugViewProjection{};
        RHI::BufferHandle m_debugSelectionBufferHandle{};
        std::vector<RHI::SlotUploader<GpuData::DebugSelectionGpu>>
            m_debugSelectionUploaders{};
        GpuData::DebugSelectionGpu m_debugSelection{};
        RHI::BufferHandle m_debugPickReadbackBufferHandle{};
        RHI::ReadbackBufferView m_debugPickReadbackView{};
        GpuData::DebugPickState m_debugPickState{};
        GameCore::EntityId m_debugPickResultEntityId = GameCore::k_invalidEntityId;
        bool m_hasDebugPickResult = false;
        uint32_t m_debugSelectedObjectId = 0;
        Audio::AudioDeviceHandle m_audioDevice{};
        Core::IO::Path m_assetRootPath{};
        MaterialHandle m_defaultMaterialHandle{};
        uint32_t m_cubeIndexCount = 0;
        uint32_t m_defaultCubeMeshId = ECS::k_invalidMeshId;
        GameCore::SceneId m_editorSceneId = GameCore::k_invalidSceneId;
        Core::IO::Path m_scriptRoot{};
    };
} // namespace Cue
