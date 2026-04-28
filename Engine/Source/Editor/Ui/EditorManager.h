#pragma once

// === Core includes ===
#include <CQRS/CQRS.h>

// === Win includes ===
#include <WinPlatform.h>

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Engine includes ===
#include <Engine.h>

// === Editor includes ===
#include "DebugCamera.h"
#include "BuildSystem.h"
#include "AssetBrowser.h"
#include "Statistics.h"
#include "GameView.h"
#include "DebugView.h"
#include "Hierarchy.h"
#include "Inspector.h"
#include "ProjectGenerator.h"
#include "VisualStudioBridge.h"

// === C++ includes ===
#include <array>
#include <string>

namespace Cue::Core::IO
{
    class IFileSystem;
}

namespace Cue::Editor
{
    class EditorManager final
    {
    public:
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
        ~EditorManager() = default;

        void initialize();
        void update();
        Result open_project(const std::string& a_projectPath);
        void set_loop_metrics_source(
            const EditorLoopMetrics* a_loopMetrics) noexcept;
    private:
        Result save_current_scene();
        Result reload_current_scene();
        Result unload_current_scene();
        Result build_script_module();
        Result build_game_release();
        Result reload_script_module();
        Result reload_script_module(BuildResult& a_inOutBuildResult);
        Result save_script_build_configuration(
            BuildConfiguration a_configuration);
        Result save_script_load_configuration(
            BuildConfiguration a_configuration);
        Result save_script_build_backend(BuildBackend a_backend);
        Result save_game_release_build_configuration(
            BuildConfiguration a_configuration);
        Result save_game_release_build_backend(BuildBackend a_backend);
        Result resolve_script_root(Core::IO::Path& a_outScriptRoot) const;
        Result open_script_solution_in_visual_studio();
        Result attach_editor_debugger_in_visual_studio();
        Result open_game_release_build_directory();
        Result create_script_template(const std::string& a_scriptName);
        Result open_path_in_shell(const Core::IO::Path& a_path) const;
        Result start_play_mode();
        Result stop_play_mode();
        Result exit_play_mode();
        Result refresh_script_project_intellisense(BuildResult& a_outResult);
        void draw_create_script_popup();
        void draw_script_build_output();
        void draw_script_build_notification_popup();
        void queue_script_action(PendingScriptAction a_action);
        void process_pending_script_action();
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
        void process_debug_pick_request();
        void sync_debug_selection();

        Core::CQRS::Bridge* m_bridge = nullptr;
        Core::IO::IFileSystem* m_fileSystem = nullptr;
        PAL::Win::WinPlatform* m_platform = nullptr;
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        Engine* m_engine = nullptr;
        std::unique_ptr<BuildSystem> m_buildSystem = nullptr;
        std::unique_ptr<VisualStudioBridge> m_visualStudioBridge = nullptr;
        std::unique_ptr<AssetBrowser> m_assetBrowser = nullptr;
        std::unique_ptr<Statistics> m_statistics = nullptr;
        std::unique_ptr<GameView> m_gameView = nullptr;
        std::unique_ptr<DebugView> m_debugView = nullptr;
        std::unique_ptr<Hierarchy> m_hierarchy = nullptr;
        std::unique_ptr<Inspector> m_inspector = nullptr;
        GameCore::EntityId m_selectedEntityId = GameCore::k_invalidEntityId;
        GameCore::SceneId m_currentSceneId = GameCore::k_invalidSceneId;
        GameCore::SceneAsset m_loadedSceneAsset{};
        std::string m_projectPath{};
        std::string m_currentScenePath{};
        std::string m_statusMessage{};
        BuildResult m_lastScriptBuildResult{};
        GameReleaseBuildResult m_lastGameReleaseBuildResult{};
        BuildConfiguration m_scriptBuildConfiguration =
            BuildConfiguration::Debug;
        BuildConfiguration m_scriptLoadConfiguration =
            BuildConfiguration::Debug;
        BuildBackend m_scriptBuildBackend = BuildBackend::CMake;
        BuildConfiguration m_gameReleaseBuildConfiguration =
            BuildConfiguration::Release;
        BuildBackend m_gameReleaseBuildBackend = BuildBackend::CMake;
        PendingScriptAction m_pendingScriptAction =
            PendingScriptAction::None;
        uint32_t m_pendingScriptActionDelayFrames = 0;
        std::string m_scriptBuildNotificationTitle{};
        std::string m_scriptBuildNotificationMessage{};
        bool m_showScriptBuildOutput = true;
        bool m_isScriptActionActive = false;
        bool m_hasStatusError = false;
        bool m_hasScriptBuildNotification = false;
        bool m_hasScriptBuildNotificationError = false;
        bool m_openScriptBuildNotificationPopup = false;
        bool m_openCreateScriptPopup = false;
        bool m_focusCreateScriptNameInput = false;
        std::array<char, 128> m_createScriptNameBuffer{};
        DebugCamera m_debugCamera{};
        EditorUpdateMetrics m_currentUpdateMetrics{};
        EditorUpdateMetrics m_lastUpdateMetrics{};
    };
}
