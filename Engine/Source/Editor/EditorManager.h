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
#include "BuildSystem.h"
#include "Statistics.h"
#include "DebugView.h"
#include "Hierarchy.h"
#include "Inspector.h"

// === C++ includes ===
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
    private:
        Result save_current_scene();
        Result reload_current_scene();
        Result unload_current_scene();
        Result build_script_module();
        Result reload_script_module();
        Result save_script_build_configuration(
            BuildConfiguration a_configuration);
        Result save_script_build_backend(BuildBackend a_backend);
        Result resolve_script_root(Core::IO::Path& a_outScriptRoot) const;
        void queue_script_action(PendingScriptAction a_action);
        void process_pending_script_action();
        Result drain_pending_editor_commands();
        void set_status_message(std::string a_message, bool a_isError);
        void undo_last_command();
        void redo_last_command();
        void handle_shortcuts();

        Core::CQRS::Bridge* m_bridge = nullptr;
        Core::IO::IFileSystem* m_fileSystem = nullptr;
        PAL::Win::WinPlatform* m_platform = nullptr;
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        Engine* m_engine = nullptr;
        std::unique_ptr<BuildSystem> m_buildSystem = nullptr;
        std::unique_ptr<Statistics> m_statistics = nullptr;
        std::unique_ptr<DebugView> m_debugView = nullptr;
        std::unique_ptr<Hierarchy> m_hierarchy = nullptr;
        std::unique_ptr<Inspector> m_inspector = nullptr;
        GameCore::EntityId m_selectedEntityId = GameCore::k_invalidEntityId;
        GameCore::SceneId m_currentSceneId = GameCore::k_invalidSceneId;
        GameCore::SceneAsset m_loadedSceneAsset{};
        std::string m_projectPath{};
        std::string m_currentScenePath{};
        std::string m_statusMessage{};
        BuildConfiguration m_scriptBuildConfiguration =
            BuildConfiguration::Debug;
        BuildBackend m_scriptBuildBackend = BuildBackend::CMake;
        PendingScriptAction m_pendingScriptAction =
            PendingScriptAction::None;
        uint32_t m_pendingScriptActionDelayFrames = 0;
        bool m_isScriptActionActive = false;
        bool m_hasStatusError = false;
    };
}
