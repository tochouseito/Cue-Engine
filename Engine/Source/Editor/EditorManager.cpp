#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Logger.h>
#include <IO/Path.h>

// === Engine includes ===
#include <GameCore/SceneSerializer.h>

// === C++ includes ===
#include <span>
#include <vector>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

namespace Cue::Editor
{
    namespace
    {
        struct ProjectSettings final
        {
            std::string startupScene{};
            std::string scriptRoot{};
            BuildConfiguration scriptBuildConfiguration =
                BuildConfiguration::Debug;
            BuildBackend scriptBuildBackend = BuildBackend::CMake;
        };

        void log_result(std::string_view a_prefix, const Result& a_result)
        {
            Core::IO::log(Core::IO::LogSink::debugConsole,
                "{}: {} (code: {}, severity: {}) at {}:{} in function {}",
                a_prefix, a_result.message, Cue::to_string(a_result.code),
                Cue::to_string(a_result.severity), a_result.file, a_result.line,
                a_result.function);
        }

        void log_build_output(std::string_view a_prefix, std::string_view a_output)
        {
            if (a_output.empty())
            {
                return;
            }

            Core::IO::log(Core::IO::LogSink::debugConsole,
                "{}:\n{}", a_prefix, a_output);
        }

        [[nodiscard]] const char* to_stage_prefix(BuildStage a_stage) noexcept
        {
            switch (a_stage)
            {
            case BuildStage::Configure:
                return "[Script][Configure]";
            case BuildStage::Build:
                return "[Script][Build]";
            case BuildStage::Attach:
                return "[Script][Attach]";
            case BuildStage::General:
            default:
                return "[Script]";
            }
        }

        [[nodiscard]] Result parse_build_configuration(
            std::string_view a_text,
            BuildConfiguration& a_outConfiguration) noexcept
        {
            if (a_text == "Debug")
            {
                a_outConfiguration = BuildConfiguration::Debug;
                return Result::ok();
            }

            if (a_text == "RelWithDebInfo")
            {
                a_outConfiguration = BuildConfiguration::RelWithDebInfo;
                return Result::ok();
            }

            if (a_text == "Release")
            {
                a_outConfiguration = BuildConfiguration::Release;
                return Result::ok();
            }

            return Result::fail(Code::InvalidArgument, Severity::Error,
                "scriptBuildConfiguration が不正です。");
        }

        [[nodiscard]] Result parse_build_backend(
            std::string_view a_text,
            BuildBackend& a_outBackend) noexcept
        {
            if (a_text == "CMake")
            {
                a_outBackend = BuildBackend::CMake;
                return Result::ok();
            }

            if (a_text == "VisualStudio")
            {
                a_outBackend = BuildBackend::VisualStudio;
                return Result::ok();
            }

            return Result::fail(Code::InvalidArgument, Severity::Error,
                "scriptBuildBackend が不正です。");
        }

        [[nodiscard]] const char* to_build_backend_name(
            BuildBackend a_backend) noexcept
        {
            switch (a_backend)
            {
            case BuildBackend::CMake:
                return "CMake";
            case BuildBackend::VisualStudio:
                return "VisualStudio";
            }

            return "CMake";
        }

        [[nodiscard]] Result save_project_settings(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_projectPath,
            const ProjectSettings& a_settings) noexcept
        {
            const Core::IO::Path projectFilePath = Core::IO::Path::join(
                a_projectPath, Core::IO::Path("cueproject.json"));
            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(projectFilePath, &fileData);
            if (!result)
            {
                return result;
            }

            try
            {
                const std::string text(
                    reinterpret_cast<const char*>(fileData.data()),
                    fileData.size());
                nlohmann::json root = nlohmann::json::parse(text);
                root["scriptBuildConfiguration"] =
                    BuildSystem::to_configuration_name(
                        a_settings.scriptBuildConfiguration);
                root["scriptBuildBackend"] =
                    to_build_backend_name(a_settings.scriptBuildBackend);

                std::string updatedText = root.dump(4);
                updatedText.push_back('\n');
                const std::span<const char> textSpan(
                    updatedText.data(), updatedText.size());
                const std::span<const std::byte> byteSpan =
                    std::as_bytes(textSpan);
                return a_fileSystem.write_all(projectFilePath, byteSpan, false);
            }
            catch (...)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "cueproject.json could not be updated.");
            }
        }

        [[nodiscard]] Result load_project_settings(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_projectPath,
            ProjectSettings& a_outSettings) noexcept
        {
            const Core::IO::Path projectFilePath = Core::IO::Path::join(
                a_projectPath, Core::IO::Path("cueproject.json"));
            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(projectFilePath, &fileData);
            if (!result)
            {
                return result;
            }

            try
            {
                const std::string text(
                    reinterpret_cast<const char*>(fileData.data()),
                    fileData.size());
                const nlohmann::json root = nlohmann::json::parse(text);

                a_outSettings.startupScene =
                    root.at("startupScene").get<std::string>();
                if (a_outSettings.startupScene.empty())
                {
                    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Project startup scene is empty.");
                }

                a_outSettings.scriptRoot =
                    root.value("scriptRoot", std::string("."));
                if (a_outSettings.scriptRoot.empty())
                {
                    a_outSettings.scriptRoot = ".";
                }

                const std::string buildConfigurationText =
                    root.value("scriptBuildConfiguration", std::string("Debug"));
                result = parse_build_configuration(
                    buildConfigurationText,
                    a_outSettings.scriptBuildConfiguration);
                if (!result)
                {
                    return result;
                }

                const std::string buildBackendText =
                    root.value("scriptBuildBackend", std::string("CMake"));
                result = parse_build_backend(
                    buildBackendText,
                    a_outSettings.scriptBuildBackend);
                if (!result)
                {
                    return result;
                }

                return Result::ok();
            }
            catch (...)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "cueproject.json could not be parsed.");
            }
        }
    }

    void EditorManager::initialize()
    {
        if (m_fileSystem != nullptr)
        {
            m_buildSystem = std::make_unique<BuildSystem>(*m_fileSystem);
        }
        m_statistics = std::make_unique<Statistics>(m_engine->frame_controller());
        m_debugView = std::make_unique<DebugView>(m_backend, m_bridge);
        m_hierarchy = std::make_unique<Hierarchy>(
            m_bridge, m_engine->game_world(), &m_selectedEntityId);
        m_inspector = std::make_unique<Inspector>(
            m_bridge, m_engine->game_world(), &m_selectedEntityId, m_engine);
    }

    Result EditorManager::open_project(const std::string& a_projectPath)
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }

        ProjectSettings projectSettings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(a_projectPath), projectSettings);
        if (!result)
        {
            set_status_message(
                "cueproject.json の読み込みに失敗しました。", true);
            return result;
        }

        Core::IO::Path scenePath(projectSettings.startupScene);
        if (!scenePath.is_absolute())
        {
            scenePath = Core::IO::Path::join(Core::IO::Path(a_projectPath), scenePath);
        }

        Core::IO::Path scriptRootPath(projectSettings.scriptRoot);
        if (!scriptRootPath.is_absolute())
        {
            scriptRootPath =
                Core::IO::Path::join(Core::IO::Path(a_projectPath), scriptRootPath);
        }

        m_projectPath = a_projectPath;
        m_currentScenePath = scenePath.utf8();
        m_scriptBuildConfiguration = projectSettings.scriptBuildConfiguration;
        m_scriptBuildBackend = projectSettings.scriptBuildBackend;

        const Result scriptLoadResult = m_engine->load_script_module(scriptRootPath);
        if (!scriptLoadResult && scriptLoadResult.code != Code::NotFound)
        {
            set_status_message("GameScript.dll の読み込みに失敗しました。", true);
            return scriptLoadResult;
        }

        result = reload_current_scene();
        if (!result)
        {
            set_status_message("スタートアップシーンの読み込みに失敗しました。",
                true);
            return result;
        }

        if (!scriptLoadResult && scriptLoadResult.code == Code::NotFound)
        {
            set_status_message(
                "プロジェクトを開きました。GameScript.dll はまだ見つかっていません。",
                true);
        }
        else
        {
            set_status_message("プロジェクトを開きました。", false);
        }
        return Result::ok();
    }

    Result EditorManager::save_current_scene()
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }
        if (m_currentSceneId == GameCore::k_invalidSceneId ||
            m_currentScenePath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "There is no loaded scene to save.");
        }

        Result result = drain_pending_editor_commands();
        if (!result)
        {
            return result;
        }

        result = m_engine->game_world()->execute_deferred_deletions();
        if (!result)
        {
            return result;
        }

        const std::string sceneName = !m_loadedSceneAsset.name().empty()
            ? m_loadedSceneAsset.name()
            : Core::IO::Path(m_currentScenePath).stem();
        GameCore::SceneAsset sceneAsset(sceneName);
        Result captureResult = Result::ok();
        result = m_engine->game_world()->for_each_object_in_scene(
            m_currentSceneId,
            [this, &sceneAsset, &captureResult](GameCore::EntityId a_entityId,
                GameCore::SceneId, GameCore::GameObject&)
            {
                if (!captureResult)
                {
                    return;
                }

                GameCore::DeletedObjectSnapshot snapshot{};
                captureResult = m_engine->game_world()->capture_deleted_object(
                    a_entityId, snapshot);
                if (!captureResult)
                {
                    return;
                }

                sceneAsset.add_object(std::move(snapshot.definition));
            });
        if (!result)
        {
            return result;
        }
        if (!captureResult)
        {
            return captureResult;
        }

        result = GameCore::SceneSerializer::save_scene_asset(
            sceneAsset, *m_fileSystem, Core::IO::Path(m_currentScenePath));
        if (!result)
        {
            return result;
        }

        m_loadedSceneAsset = std::move(sceneAsset);
        set_status_message("シーンを保存しました。", false);
        return Result::ok();
    }

    Result EditorManager::resolve_script_root(
        Core::IO::Path& a_outScriptRoot) const
    {
        a_outScriptRoot = {};

        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings projectSettings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), projectSettings);
        if (!result)
        {
            return result;
        }

        a_outScriptRoot = Core::IO::Path(projectSettings.scriptRoot);
        if (!a_outScriptRoot.is_absolute())
        {
            a_outScriptRoot = Core::IO::Path::join(
                Core::IO::Path(m_projectPath), a_outScriptRoot);
        }

        return Result::ok();
    }

    Result EditorManager::build_script_module()
    {
        if (m_buildSystem == nullptr || m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "BuildSystem dependencies are not initialized.");
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            return result;
        }

        const ScriptBuildRequest request{
            scriptRoot,
            "win-x64",
            m_scriptBuildConfiguration,
            "GameScript",
            m_scriptBuildBackend
        };

        ScriptBuildValidation validation{};
        result = m_buildSystem->validate_script_build_environment(
            request,
            validation);
        if (!result)
        {
            return result;
        }

        BuildResult buildResult{};
        result = m_buildSystem->execute_script_build(request, buildResult);

        for (const BuildStageResult& stageResult : buildResult.stageResults)
        {
            log_build_output(
                to_stage_prefix(stageResult.stage),
                stageResult.output);
        }

        if (!result)
        {
            return result;
        }

        return reload_script_module();
    }

    Result EditorManager::reload_script_module()
    {
        if (m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            return result;
        }

        result = m_engine->load_script_module(scriptRoot);
        if (!result)
        {
            return result;
        }

        return Result::ok();
    }

    Result EditorManager::save_script_build_configuration(
        BuildConfiguration a_configuration)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        settings.scriptBuildConfiguration = a_configuration;
        result = save_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        m_scriptBuildConfiguration = a_configuration;
        return Result::ok();
    }

    Result EditorManager::save_script_build_backend(
        BuildBackend a_backend)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager file system is not initialized.");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Project is not opened.");
        }

        ProjectSettings settings{};
        Result result = load_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        settings.scriptBuildBackend = a_backend;
        result = save_project_settings(
            *m_fileSystem, Core::IO::Path(m_projectPath), settings);
        if (!result)
        {
            return result;
        }

        m_scriptBuildBackend = a_backend;
        return Result::ok();
    }

    void EditorManager::queue_script_action(PendingScriptAction a_action)
    {
        if (a_action == PendingScriptAction::None)
        {
            return;
        }

        m_pendingScriptAction = a_action;
        m_pendingScriptActionDelayFrames = 1;
        m_isScriptActionActive = true;

        switch (a_action)
        {
        case PendingScriptAction::Reload:
            set_status_message("GameScript を再読み込みしています...", false);
            break;

        case PendingScriptAction::Build:
            set_status_message("GameScript をビルドしています...", false);
            break;

        case PendingScriptAction::None:
            break;
        }
    }

    void EditorManager::process_pending_script_action()
    {
        if (m_pendingScriptAction == PendingScriptAction::None)
        {
            m_isScriptActionActive = false;
            return;
        }

        if (m_pendingScriptActionDelayFrames > 0)
        {
            --m_pendingScriptActionDelayFrames;
            return;
        }

        const PendingScriptAction action = m_pendingScriptAction;
        m_pendingScriptAction = PendingScriptAction::None;

        Result result = Result::ok();
        switch (action)
        {
        case PendingScriptAction::Reload:
            result = reload_script_module();
            if (!result)
            {
                log_result("Failed to reload GameScript", result);
                set_status_message("GameScript の再読み込みに失敗しました。",
                    true);
            }
            else
            {
                set_status_message("GameScript を再読み込みしました。",
                    false);
            }
            break;

        case PendingScriptAction::Build:
            result = build_script_module();
            if (!result)
            {
                log_result("Failed to build GameScript", result);
                set_status_message("GameScript のビルドに失敗しました。", true);
            }
            else
            {
                set_status_message(
                    "GameScript をビルドして再読み込みしました。", false);
            }
            break;

        case PendingScriptAction::None:
            break;
        }

        m_isScriptActionActive = false;
    }

    Result EditorManager::reload_current_scene()
    {
        if (m_fileSystem == nullptr || m_engine == nullptr ||
            m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }
        if (m_currentScenePath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Warning,
                "There is no scene path to load.");
        }

        Result result = drain_pending_editor_commands();
        if (!result)
        {
            return result;
        }

        result = unload_current_scene();
        if (!result)
        {
            return result;
        }

        GameCore::SceneAsset sceneAsset{};
        result = GameCore::SceneSerializer::load_scene_asset(
            *m_fileSystem, Core::IO::Path(m_currentScenePath), sceneAsset);
        if (!result)
        {
            return result;
        }

        m_loadedSceneAsset = std::move(sceneAsset);

        GameCore::GameWorld::LoadSceneResult loadResult{};
        result = m_engine->game_world()->load_scene(m_loadedSceneAsset, loadResult);
        if (!result)
        {
            return result;
        }

        m_currentSceneId = loadResult.sceneId;
        m_engine->set_editor_scene_id(m_currentSceneId);
        m_selectedEntityId = GameCore::k_invalidEntityId;
        set_status_message("シーンを読み込みました。", false);
        return Result::ok();
    }

    Result EditorManager::unload_current_scene()
    {
        if (m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }

        m_engine->set_editor_scene_id(GameCore::k_invalidSceneId);
        if (m_currentSceneId == GameCore::k_invalidSceneId)
        {
            return Result::ok();
        }

        const GameCore::SceneId sceneId = m_currentSceneId;
        Result result = m_engine->game_world()->unload_scene(sceneId);
        if (!result)
        {
            return result;
        }

        result = m_engine->game_world()->execute_deferred_deletions();
        if (!result)
        {
            return result;
        }

        m_currentSceneId = GameCore::k_invalidSceneId;
        m_loadedSceneAsset = {};
        m_selectedEntityId = GameCore::k_invalidEntityId;
        return Result::ok();
    }

    Result EditorManager::drain_pending_editor_commands()
    {
        if (m_bridge == nullptr)
        {
            return Result::ok();
        }
        if (m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "EditorManager dependencies are not initialized.");
        }

        EngineCommandContext commandContext(
            *m_engine->game_world(), m_engine->editor_scene_id());
        return m_bridge->drain_commands(commandContext);
    }

    void EditorManager::set_status_message(std::string a_message, bool a_isError)
    {
        m_statusMessage = std::move(a_message);
        m_hasStatusError = a_isError;
    }

    void EditorManager::undo_last_command()
    {
        if (m_bridge == nullptr || m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return;
        }

        EngineCommandContext commandContext(
            *m_engine->game_world(), m_engine->editor_scene_id());
        Result result = m_bridge->undo_last_command(commandContext);
        if (!result && result.code != Code::InvalidState)
        {
            CUE_ASSERTF(false,
                "Failed to undo command: %s (code: %s, severity: %s) at %s:%u in function %s",
                result.message.data(), Cue::to_string(result.code),
                Cue::to_string(result.severity), result.file,
                result.line, result.function);
        }
    }

    void EditorManager::redo_last_command()
    {
        if (m_bridge == nullptr || m_engine == nullptr || m_engine->game_world() == nullptr)
        {
            return;
        }

        EngineCommandContext commandContext(
            *m_engine->game_world(), m_engine->editor_scene_id());
        Result result = m_bridge->redo_last_command(commandContext);
        if (!result && result.code != Code::InvalidState)
        {
            CUE_ASSERTF(false,
                "Failed to redo command: %s (code: %s, severity: %s) at %s:%u in function %s",
                result.message.data(), Cue::to_string(result.code),
                Cue::to_string(result.severity), result.file,
                result.line, result.function);
        }
    }

    void EditorManager::handle_shortcuts()
    {
        if (m_bridge == nullptr)
        {
            return;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput)
        {
            return;
        }

        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            undo_last_command();
        }

        if (io.KeyCtrl &&
            (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
        {
            redo_last_command();
        }

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            const Result result = save_current_scene();
            if (!result)
            {
                log_result("Failed to save scene", result);
                set_status_message("シーン保存に失敗しました。", true);
            }
        }
    }

    void EditorManager::update()
    {
        process_pending_script_action();

        // ビューポート全体をカバーするドックスペースを作成
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);

        ImGuiWindowFlags window_flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGui::Begin("DockSpace Window", nullptr, window_flags);
        ImGui::PopStyleVar(2);

        static bool showMetricsWindow = false;
        static bool showDemoWindow = false;
        static bool showStyleEditor = false;

        handle_shortcuts();

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("ファイル"))
            {
                const bool canOperateScene =
                    m_currentSceneId != GameCore::k_invalidSceneId &&
                    !m_currentScenePath.empty();

                if (ImGui::MenuItem("シーンを保存", "Ctrl+S", false, canOperateScene))
                {
                    const Result result = save_current_scene();
                    if (!result)
                    {
                        log_result("Failed to save scene", result);
                        set_status_message("シーン保存に失敗しました。", true);
                    }
                }

                if (ImGui::MenuItem("シーンを再読み込み", nullptr, false, canOperateScene))
                {
                    const Result result = reload_current_scene();
                    if (!result)
                    {
                        log_result("Failed to reload scene", result);
                        set_status_message("シーン再読み込みに失敗しました。", true);
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("編集"))
            {
                const bool canUndo = m_bridge != nullptr && m_bridge->can_undo();
                const bool canRedo = m_bridge != nullptr && m_bridge->can_redo();

                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo))
                {
                    undo_last_command();
                }

                if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo))
                {
                    redo_last_command();
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("ビルド"))
            {
                const bool canEditBuildSettings = !m_isScriptActionActive;
                const bool canBuildScript =
                    m_buildSystem != nullptr && !m_projectPath.empty() &&
                    !m_isScriptActionActive;
                const bool canReloadScript =
                    m_engine != nullptr && !m_projectPath.empty() &&
                    !m_isScriptActionActive;

                if (ImGui::BeginMenu("GameScript 構成", canEditBuildSettings))
                {
                    const auto draw_configuration_item =
                        [this](const char* a_label, BuildConfiguration a_configuration)
                    {
                        const bool isSelected =
                            m_scriptBuildConfiguration == a_configuration;
                        if (ImGui::MenuItem(a_label, nullptr, isSelected, true) &&
                            !isSelected)
                        {
                            const Result result =
                                save_script_build_configuration(a_configuration);
                            if (!result)
                            {
                                log_result(
                                    "Failed to save GameScript build configuration",
                                    result);
                                set_status_message(
                                    "GameScript のビルド構成保存に失敗しました。", true);
                            }
                            else
                            {
                                set_status_message(
                                    std::string("GameScript 構成を ") + a_label +
                                        " に変更しました。",
                                    false);
                            }
                        }
                    };

                    draw_configuration_item("Debug", BuildConfiguration::Debug);
                    draw_configuration_item("RelWithDebInfo",
                        BuildConfiguration::RelWithDebInfo);
                    draw_configuration_item("Release",
                        BuildConfiguration::Release);

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("GameScript backend", canEditBuildSettings))
                {
                    const auto draw_backend_item =
                        [this](const char* a_label, BuildBackend a_backend)
                    {
                        const bool isSelected =
                            m_scriptBuildBackend == a_backend;
                        if (ImGui::MenuItem(a_label, nullptr, isSelected, true) &&
                            !isSelected)
                        {
                            const Result result =
                                save_script_build_backend(a_backend);
                            if (!result)
                            {
                                log_result(
                                    "Failed to save GameScript build backend",
                                    result);
                                set_status_message(
                                    "GameScript の build backend 保存に失敗しました。", true);
                            }
                            else
                            {
                                set_status_message(
                                    std::string("GameScript backend を ") + a_label +
                                        " に変更しました。",
                                    false);
                            }
                        }
                    };

                    draw_backend_item("CMake", BuildBackend::CMake);
                    draw_backend_item("VisualStudio", BuildBackend::VisualStudio);

                    ImGui::EndMenu();
                }

                if (ImGui::MenuItem(
                        "GameScript を再読み込み", nullptr, false, canReloadScript))
                {
                    queue_script_action(PendingScriptAction::Reload);
                }

                if (ImGui::MenuItem(
                        "GameScript をビルド", nullptr, false, canBuildScript))
                {
                    queue_script_action(PendingScriptAction::Build);
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Test"))
            {
                if (ImGui::MenuItem("Show Metrics Window"))
                {
                    showMetricsWindow = !showMetricsWindow;
                }

                if (ImGui::MenuItem("Show Demo Window"))
                {
                    showDemoWindow = !showDemoWindow;
                }

                if (ImGui::MenuItem("Show Style Editor"))
                {
                    showStyleEditor = !showStyleEditor;
                }

                ImGui::EndMenu();
            }

            if (!m_statusMessage.empty())
            {
                ImGui::Separator();
                if (m_hasStatusError)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
                }
                ImGui::TextUnformatted(m_statusMessage.c_str());
                if (m_hasStatusError)
                {
                    ImGui::PopStyleColor();
                }
            }

            ImGui::EndMenuBar();
        }

        ImGuiID dockspace_id = ImGui::GetID("MyDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
        ImGui::End();

        if (showMetricsWindow)
        {
            ImGui::ShowMetricsWindow(&showMetricsWindow);
        }
        if (showDemoWindow)
        {
            ImGui::ShowDemoWindow(&showDemoWindow);
        }
        if (showStyleEditor)
        {
            ImGui::ShowStyleEditor();
        }

        m_statistics->update();
        m_debugView->update();
        m_hierarchy->update();
        m_inspector->update();
    }
}
