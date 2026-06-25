// EditorManager の役割と公開要素を定義する

#pragma once

// === Core includes ===
#include <CQRS/CQRS.h>
#include <Threading/JobSystem.h>

// === Win includes ===
#include <WinPlatform.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Engine includes ===
#include <Engine.h>
#include <EffectSystem/EffectAsset.h>

// === Editor includes ===
#include "DebugCamera.h"
#include "BuildSystem.h"
#include "AssetBrowser.h"
#include "Statistics.h"
#include "GameView.h"
#include "DebugView.h"
#include "EffectPreviewView.h"
#include "Hierarchy.h"
#include "Icon.h"
#include "Inspector.h"
#include "ProjectGenerator.h"
#include "VisualStudioBridge.h"

// === C++ includes ===
#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    class EditorManager final
    {
    public:
        struct SceneReloadOperation final
        {
            std::shared_future<void> future{};
            std::atomic<uint32_t> completed{ 0 };
            std::atomic<uint32_t> total{ 1 };
            std::atomic<bool> isFinished{ false };
            std::mutex mutex{};
            std::string title = "シーンを再読み込み中";
            std::string detail = "準備中...";
            std::string errorMessage{};
            std::vector<Core::IO::Path> texturePaths{};
            std::vector<Core::IO::Path> modelPaths{};
            std::vector<Core::IO::Path> materialPaths{};
            std::vector<Core::IO::Path> effectPaths{};
            Code resultCode = Code::OK;
            Severity resultSeverity = Severity::Info;
            bool succeeded = false;
            bool hasApplied = false;
        };

        enum class PendingScriptAction : uint8_t
        {
            None,
            Reload,
            Build,
        };

        EditorManager(Core::CQRS::Bridge* bridge,
            Core::IO::IFileSystem* fileSystem,
            PAL::Win::WinPlatform* platform,
            RHI::DX12::D3D12Backend* backend,
            Engine* engine)
            : m_bridge(bridge)
            , m_fileSystem(fileSystem)
            , m_platform(platform)
            , m_backend(backend)
            , m_engine(engine)
        {
        }
        ~EditorManager();

        void initialize();
        void update();
        Result open_project(const std::string& a_projectPath);
        void set_loop_metrics_source(
            const EditorLoopMetrics* a_loopMetrics) noexcept;
    private:
        struct LoadedSceneEntry final
        {
            std::unique_ptr<GameCore::SceneAsset> asset = nullptr;
            std::string name{};
            std::string path{};
            GameCore::SceneId sceneId = GameCore::k_invalidSceneId;
        };

        enum class Workspace : uint8_t
        {
            Scene,
            EffectEditor,
        };

        enum class EffectEditorSelection : uint8_t
        {
            Emitter,
            Sprite,
            Ribbon,
        };

        Result save_current_scene();
        Result save_scene(GameCore::SceneId a_sceneId);
        Result create_scene_asset(const std::string& a_sceneName);
        Result reload_current_scene();
        Result start_background_scene_reload();
        void update_background_scene_reload();
        Result apply_background_scene_reload(SceneReloadOperation& a_operation);
        Result unload_scene(GameCore::SceneId a_sceneId);
        Result unload_current_scene();
        Result load_scene_to_world(
            const Core::IO::Path& a_scenePath,
            bool a_isPrimaryScene);
        Result set_current_scene(GameCore::SceneId a_sceneId);
        Result collect_project_scene_paths(
            std::vector<Core::IO::Path>& a_outScenePaths) const;
        Result bake_current_scene_navigation();
        Result build_script_module();
        Result build_game_release();
        Result reload_script_module();
        Result reload_script_module(BuildResult& a_inOutBuildResult);
        Result save_script_build_configuration(
            BuildConfiguration a_configuration);
        Result save_game_release_build_configuration(
            BuildConfiguration a_configuration);
        Result save_game_release_build_backend(BuildBackend a_backend);
        Result save_game_release_app_settings(
            const std::string& a_executableName,
            const std::string& a_windowTitle,
            const std::string& a_iconPath);
        Result load_game_release_app_settings_to_buffers();
        Result resolve_script_root(Core::IO::Path& a_outScriptRoot) const;
        Result open_script_solution_in_visual_studio();
        Result attach_editor_debugger_in_visual_studio();
        Result open_game_release_build_directory();
        Result create_material_asset();
        Result create_effect_editor_asset();
        Result load_effect_editor_asset(const Core::IO::Path& a_effectPath);
        Result save_effect_editor_asset();
        Result sync_effect_editor_preview();
        Result place_effect_editor_in_scene();
        void destroy_effect_editor_preview();
        void refresh_effect_editor_buffers() noexcept;
        Result handle_dropped_asset_files();
        Result import_external_asset_file(
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationDirectory);
        Result import_copied_asset_file(const Core::IO::Path& a_assetPath);
        Result make_asset_import_destination(
            const Core::IO::Path& a_sourcePath,
            const Core::IO::Path& a_destinationDirectory,
            Core::IO::Path& a_outDestinationPath) const;
        Result create_script_template(const std::string& a_scriptName);
        Result open_path_in_shell(const Core::IO::Path& a_path) const;
        Result start_play_mode();
        Result stop_play_mode();
        Result exit_play_mode();
        Result refresh_script_project_intellisense(BuildResult& a_outResult);
        void draw_create_scene_popup();
        void draw_create_script_popup();
        void draw_script_build_output();
        void draw_navigation_debug_window();
        void draw_workspace_tabs();
        void draw_effect_editor_workspace();
        void draw_status_bar();
        void draw_play_controls();
        void draw_script_build_configuration_combo();
        void draw_script_build_notification_popup();
        void draw_game_release_app_settings_popup();
        void draw_background_progress_window();
        void queue_script_action(PendingScriptAction a_action);
        void process_pending_script_action();
        void update_auto_script_build();
        [[nodiscard]] bool try_get_script_source_version(
            uint64_t& a_outVersion) const;
        Result drain_pending_editor_commands();
        void set_status_message(std::string a_message, bool a_isError);
        void set_script_build_notification(
            std::string a_title,
            std::string a_message,
            bool a_isError,
            bool a_openPopup);
        void undo_last_command();
        void redo_last_command();
        void handle_shortcuts();
        void draw_add_menu_items();
        void draw_scene_menu_items();
        void draw_view_menu_items();
        void draw_display_menu_items();
        void draw_main_camera_menu();
        void draw_skybox_menu();
        void show_and_focus_window(
            const char* a_windowName,
            bool* a_showWindow = nullptr);
        void focus_pending_window();
        [[nodiscard]] GameCore::SceneId selected_add_scene_id() const noexcept;
        [[nodiscard]] std::vector<Hierarchy::SceneEntry>
            collect_hierarchy_scenes() const;
        [[nodiscard]] LoadedSceneEntry* find_loaded_scene(
            GameCore::SceneId a_sceneId) noexcept;
        [[nodiscard]] const LoadedSceneEntry* find_loaded_scene(
            GameCore::SceneId a_sceneId) const noexcept;
        [[nodiscard]] bool is_scene_path_loaded(
            const Core::IO::Path& a_scenePath) const noexcept;
        void process_debug_pick_request();
        [[nodiscard]] bool draw_debug_transform_gizmo(
            const ImVec2& a_viewportMin,
            const ImVec2& a_viewportMax,
            ImDrawList* a_drawList);
        [[nodiscard]] bool draw_debug_overlay(
            const ImVec2& a_viewportMin,
            const ImVec2& a_viewportMax,
            ImDrawList* a_drawList);
        [[nodiscard]] bool draw_spot_shadow_map_preview(
            const ImVec2& a_viewportMin,
            const ImVec2& a_viewportMax,
            ImDrawList* a_drawList);
        [[nodiscard]] bool pick_debug_non_rendered_object(
            const DebugView::PickRequest& a_request,
            GameCore::EntityId& a_outEntityId) const;
        [[nodiscard]] Core::IO::Path current_asset_drop_folder() const noexcept;
        [[nodiscard]] std::string make_asset_relative_name(
            const Core::IO::Path& a_assetPath) const;
        void sync_debug_selection();
        [[nodiscard]] EffectSystem::EffectEmitterDesc*
            selected_effect_emitter() noexcept;
        [[nodiscard]] const EffectSystem::EffectEmitterDesc*
            selected_effect_emitter() const noexcept;
        [[nodiscard]] EffectSystem::EffectSpritePrimitiveDesc*
            selected_effect_sprite() noexcept;
        [[nodiscard]] EffectSystem::EffectRibbonPrimitiveDesc*
            selected_effect_ribbon() noexcept;

        Core::CQRS::Bridge* m_bridge = nullptr;
        Core::IO::IFileSystem* m_fileSystem = nullptr;
        PAL::Win::WinPlatform* m_platform = nullptr;
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        Engine* m_engine = nullptr;
        std::unique_ptr<BuildSystem> m_buildSystem = nullptr;
        std::unique_ptr<Core::Threading::JobSystem> m_jobSystem = nullptr;
        std::unique_ptr<VisualStudioBridge> m_visualStudioBridge = nullptr;
        std::unique_ptr<AssetBrowser> m_assetBrowser = nullptr;
        std::unique_ptr<Statistics> m_statistics = nullptr;
        std::unique_ptr<GameView> m_gameView = nullptr;
        std::unique_ptr<DebugView> m_debugView = nullptr;
        std::unique_ptr<EffectPreviewView> m_effectPreviewView = nullptr;
        std::unique_ptr<Hierarchy> m_hierarchy = nullptr;
        std::unique_ptr<Inspector> m_inspector = nullptr;
        GameCore::EntityId m_selectedEntityId = GameCore::k_invalidEntityId;
        GameCore::SceneId m_selectedSceneId = GameCore::k_invalidSceneId;
        GameCore::SceneId m_currentSceneId = GameCore::k_invalidSceneId;
        GameCore::SceneAsset m_loadedSceneAsset{};
        std::vector<LoadedSceneEntry> m_loadedEditorScenes{};
        Core::IO::Path m_assetRootPath{};
        Core::IO::Path m_selectedAssetPath{};
        Core::IO::Path m_effectEditorPath{};
        std::string m_projectPath{};
        std::string m_currentScenePath{};
        std::string m_statusMessage{};
        BuildResult m_lastScriptBuildResult{};
        GameReleaseBuildResult m_lastGameReleaseBuildResult{};
        std::shared_ptr<SceneReloadOperation> m_sceneReloadOperation = nullptr;
        BuildConfiguration m_scriptBuildConfiguration =
            BuildConfiguration::Debug;
        BuildConfiguration m_gameReleaseBuildConfiguration =
            BuildConfiguration::Release;
        BuildBackend m_gameReleaseBuildBackend = BuildBackend::CMake;
        PendingScriptAction m_pendingScriptAction =
            PendingScriptAction::None;
        Workspace m_currentWorkspace = Workspace::Scene;
        EffectEditorSelection m_effectEditorSelection =
            EffectEditorSelection::Emitter;
        EffectSystem::EffectAsset m_effectEditorAsset{};
        EffectHandle m_effectEditorHandle{};
        GameCore::EntityId m_effectPreviewEntityId =
            GameCore::k_invalidEntityId;
        uint32_t m_selectedEffectEmitterIndex = 0;
        uint32_t m_selectedEffectSpriteIndex = 0;
        uint32_t m_selectedEffectRibbonIndex = 0;
        uint32_t m_pendingScriptActionDelayFrames = 0;
        uint32_t m_autoScriptBuildScanDelayFrames = 0;
        uint32_t m_autoScriptBuildDebounceFrames = 0;
        float m_effectPreviewSpeed = 1.0f;
        float m_effectPreviewScrubTime = 0.0f;
        std::string m_scriptBuildNotificationTitle{};
        std::string m_scriptBuildNotificationMessage{};
        bool m_showScriptBuildOutput = true;
        bool m_showNavigationDebugWindow = false;
        bool m_showSpotShadowMapPreview = true;
        bool m_isScriptActionActive = false;
        bool m_hasStatusError = false;
        bool m_hasScriptBuildNotification = false;
        bool m_hasScriptBuildNotificationError = false;
        bool m_openScriptBuildNotificationPopup = false;
        bool m_openCreateScenePopup = false;
        bool m_openCreateScriptPopup = false;
        bool m_openGameReleaseAppSettingsPopup = false;
        bool m_focusCreateSceneNameInput = false;
        bool m_focusCreateScriptNameInput = false;
        bool m_hasScriptSourceSnapshot = false;
        bool m_hasPendingAutoScriptBuild = false;
        bool m_hasEffectEditorAsset = false;
        bool m_effectEditorDirty = false;
        bool m_effectPreviewPlaying = true;
        std::array<char, 128> m_createSceneNameBuffer{};
        std::array<char, 128> m_createScriptNameBuffer{};
        std::array<char, 128> m_effectNameBuffer{};
        std::array<char, 128> m_effectEmitterNameBuffer{};
        std::array<char, 128> m_effectMaterialNameBuffer{};
        std::array<char, 128> m_effectMeshNameBuffer{};
        std::array<char, 128> m_gameReleaseExecutableNameBuffer{};
        std::array<char, 128> m_gameReleaseWindowTitleBuffer{};
        std::array<char, 260> m_gameReleaseIconPathBuffer{};
        std::string m_pendingFocusWindowName{};
        uint64_t m_scriptSourceVersion = 0;
        DebugView::PickRequest m_pendingDebugPickFallback{};
        DebugCamera m_debugCamera{};
        DebugCamera m_effectPreviewCamera{};
        ECS::TransformComponent m_debugGizmoStartTransform{};
        GameCore::EntityId m_debugGizmoEntityId =
            GameCore::k_invalidEntityId;
        EditorUpdateMetrics m_currentUpdateMetrics{};
        EditorUpdateMetrics m_lastUpdateMetrics{};
        uint32_t m_debugGizmoOperation = 0;
        uint32_t m_debugGizmoMode = 0;
        uint32_t m_debugGizmoPickBlockFrames = 0;
        bool m_isDebugGizmoEditing = false;
        bool m_hasPendingDebugPickFallback = false;
    };
}
