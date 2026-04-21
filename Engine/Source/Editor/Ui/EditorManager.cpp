#include "EditorManager.h"

// === Base includes ===
#include <CueAssert.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Logger.h>
#include <IO/Path.h>

// === Engine includes ===
#include <GameCore/SceneSerializer.h>
#include <Script/MarionnetteObject.h>
#include <Engine/Source/Runtime/PAL/Win/ConvertUTF.h>

// === Win includes ===
#include <shellapi.h>

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
            case BuildStage::Reload:
                return "[Script][Reload]";
            case BuildStage::Attach:
                return "[Script][Attach]";
            case BuildStage::General:
            default:
                return "[Script]";
            }
        }

        [[nodiscard]] const char* to_stage_name(BuildStage a_stage) noexcept
        {
            switch (a_stage)
            {
            case BuildStage::Configure:
                return "Configure";
            case BuildStage::Build:
                return "Build";
            case BuildStage::Reload:
                return "Reload";
            case BuildStage::Attach:
                return "Attach";
            case BuildStage::General:
            default:
                return "General";
            }
        }

        [[nodiscard]] const char* to_severity_name(
            BuildMessageSeverity a_severity) noexcept
        {
            switch (a_severity)
            {
            case BuildMessageSeverity::Warning:
                return "Warning";
            case BuildMessageSeverity::Error:
                return "Error";
            case BuildMessageSeverity::Info:
            default:
                return "Info";
            }
        }

        [[nodiscard]] bool should_serialize_script_field(
            std::string_view a_scriptClassName,
            std::string_view a_fieldName,
            void* a_userData)
        {
            const Engine* engine = static_cast<const Engine*>(a_userData);
            if (engine == nullptr)
            {
                return true;
            }

            const MarionnetteClass* marionnetteClass =
                engine->find_marionnette_class(a_scriptClassName);
            if (marionnetteClass == nullptr)
            {
                // 未解決 class は保存データを落とさない。
                return true;
            }

            const MarionnetteProperty* property =
                marionnetteClass->find_property(a_fieldName);
            if (property == nullptr)
            {
                return false;
            }

            return has_any_flags(
                property->flags,
                MarionnettePropertyFlag_Serialize);
        }

        void push_build_message(
            BuildResult& a_result,
            BuildMessageSeverity a_severity,
            BuildStage a_stage,
            std::string a_text)
        {
            a_result.messages.push_back(BuildMessage{
                a_severity,
                a_stage,
                std::move(a_text)
            });
        }

        [[nodiscard]] bool has_stage_result(
            const BuildResult& a_result,
            BuildStage a_stage) noexcept
        {
            for (const BuildStageResult& stageResult : a_result.stageResults)
            {
                if (stageResult.stage == a_stage)
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] const BuildMessage* find_build_message(
            const BuildResult& a_result,
            BuildMessageSeverity a_severity) noexcept
        {
            for (const BuildMessage& message : a_result.messages)
            {
                if (message.severity == a_severity)
                {
                    return &message;
                }
            }

            return nullptr;
        }

        [[nodiscard]] const BuildStageResult* find_failed_stage_result(
            const BuildResult& a_result) noexcept
        {
            for (const BuildStageResult& stageResult : a_result.stageResults)
            {
                if (!stageResult.succeeded)
                {
                    return &stageResult;
                }
            }

            return nullptr;
        }

        [[nodiscard]] std::string make_output_excerpt(
            std::string_view a_output) noexcept
        {
            size_t lineBegin = 0;
            while (lineBegin < a_output.size())
            {
                const size_t lineEnd = a_output.find_first_of("\r\n", lineBegin);
                const size_t lineSize =
                    lineEnd == std::string_view::npos
                    ? (a_output.size() - lineBegin)
                    : (lineEnd - lineBegin);
                if (lineSize > 0)
                {
                    std::string excerpt(a_output.substr(lineBegin, lineSize));
                    if (excerpt.size() > 240)
                    {
                        excerpt.resize(240);
                        excerpt += "...";
                    }

                    return excerpt;
                }

                if (lineEnd == std::string_view::npos)
                {
                    break;
                }

                lineBegin = lineEnd + 1;
            }

            return {};
        }

        [[nodiscard]] std::string make_primary_build_message(
            const BuildResult& a_result) noexcept
        {
            if (const BuildMessage* errorMessage =
                find_build_message(a_result, BuildMessageSeverity::Error);
                errorMessage != nullptr && !errorMessage->text.empty())
            {
                return errorMessage->text;
            }

            if (const BuildMessage* warningMessage =
                find_build_message(a_result, BuildMessageSeverity::Warning);
                warningMessage != nullptr && !warningMessage->text.empty())
            {
                return warningMessage->text;
            }

            if (const BuildStageResult* failedStageResult =
                find_failed_stage_result(a_result);
                failedStageResult != nullptr)
            {
                const std::string excerpt =
                    make_output_excerpt(failedStageResult->output);
                if (!excerpt.empty())
                {
                    return excerpt;
                }
            }

            if (!a_result.summary.empty())
            {
                return a_result.summary;
            }

            return {};
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
            m_visualStudioBridge =
                std::make_unique<VisualStudioBridge>(*m_fileSystem);
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

        Result result = stop_play_mode();
        if (!result)
        {
            return result;
        }

        ProjectSettings projectSettings{};
        result = load_project_settings(
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

        GameCore::SceneSerializer::SaveOptions saveOptions{};
        saveOptions.shouldSerializeScriptField = &should_serialize_script_field;
        saveOptions.userData = m_engine;

        result = GameCore::SceneSerializer::save_scene_asset(
            sceneAsset,
            *m_fileSystem,
            Core::IO::Path(m_currentScenePath),
            saveOptions);
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

        m_lastScriptBuildResult = {};
        result = m_buildSystem->execute_script_build(
            request, m_lastScriptBuildResult);

        for (const BuildStageResult& stageResult : m_lastScriptBuildResult.stageResults)
        {
            log_build_output(
                to_stage_prefix(stageResult.stage),
                stageResult.output);
        }

        if (!result)
        {
            return result;
        }

        result = reload_script_module(m_lastScriptBuildResult);
        if (!m_lastScriptBuildResult.stageResults.empty())
        {
            const BuildStageResult& stageResult =
                m_lastScriptBuildResult.stageResults.back();
            if (stageResult.stage == BuildStage::Reload)
            {
                log_build_output(
                    to_stage_prefix(stageResult.stage),
                    stageResult.output);
            }
        }

        return result;
    }

    Result EditorManager::open_script_solution_in_visual_studio()
    {
        if (m_visualStudioBridge == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "VisualStudioBridge is not initialized.");
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            return result;
        }

        return m_visualStudioBridge->open_solution(scriptRoot, "win-x64");
    }

    Result EditorManager::attach_editor_debugger_in_visual_studio()
    {
        if (m_visualStudioBridge == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "VisualStudioBridge is not initialized.");
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            return result;
        }

        return m_visualStudioBridge->attach_debugger(
            scriptRoot, "win-x64", ::GetCurrentProcessId());
    }

    Result EditorManager::open_path_in_shell(
        const Core::IO::Path& a_path) const
    {
        if (a_path.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Open path is empty.");
        }

        std::wstring widePath{};
        Result result = PAL::Win::utf8_to_wide(a_path.utf8(), &widePath);
        if (!result)
        {
            return result;
        }

        const HINSTANCE executeResult = ::ShellExecuteW(
            nullptr,
            L"open",
            widePath.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(executeResult) <= 32)
        {
            return Result::fail(Code::CreateFailed, Severity::Error,
                "Shell でパスを開けませんでした。");
        }

        return Result::ok();
    }

    Result EditorManager::reload_script_module()
    {
        m_lastScriptBuildResult = {};
        Result result = reload_script_module(m_lastScriptBuildResult);
        if (!m_lastScriptBuildResult.stageResults.empty())
        {
            const BuildStageResult& stageResult =
                m_lastScriptBuildResult.stageResults.back();
            if (stageResult.stage == BuildStage::Reload)
            {
                log_build_output(
                    to_stage_prefix(stageResult.stage),
                    stageResult.output);
            }
        }

        return result;
    }

    Result EditorManager::create_script_template(
        const std::string& a_scriptName)
    {
        if (m_fileSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "FileSystem が初期化されていません。");
        }
        if (m_projectPath.empty())
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "プロジェクトが開かれていません。");
        }

        ProjectGenerator projectGenerator(*m_fileSystem);
        Result result = projectGenerator.create_script_template(
            m_projectPath,
            a_scriptName);
        if (!result)
        {
            return result;
        }

        BuildResult configureResult{};
        result = refresh_script_project_intellisense(configureResult);
        m_lastScriptBuildResult = std::move(configureResult);
        if (!m_lastScriptBuildResult.stageResults.empty())
        {
            const BuildStageResult& stageResult =
                m_lastScriptBuildResult.stageResults.back();
            if (stageResult.stage == BuildStage::Configure)
            {
                log_build_output(
                    to_stage_prefix(stageResult.stage),
                    stageResult.output);
            }
        }

        return result;
    }

    Result EditorManager::refresh_script_project_intellisense(
        BuildResult& a_outResult)
    {
        a_outResult = {};

        if (m_buildSystem == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "BuildSystem が初期化されていません。");
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
        return m_buildSystem->execute_script_configure(
            request,
            a_outResult);
    }

    Result EditorManager::reload_script_module(BuildResult& a_inOutBuildResult)
    {
        if (m_engine == nullptr)
        {
            const Result result = Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
            a_inOutBuildResult.stageResults.push_back(BuildStageResult{
                BuildStage::Reload,
                "Engine::load_script_module",
                std::string(result.message),
                {},
                1,
                false
            });
            a_inOutBuildResult.summary = std::string(result.message);
            a_inOutBuildResult.exitCode = 1;
            a_inOutBuildResult.succeeded = false;
            push_build_message(a_inOutBuildResult, BuildMessageSeverity::Error,
                BuildStage::Reload, std::string(result.message));
            return result;
        }

        Core::IO::Path scriptRoot{};
        Result result = resolve_script_root(scriptRoot);
        if (!result)
        {
            a_inOutBuildResult.stageResults.push_back(BuildStageResult{
                BuildStage::Reload,
                "Engine::load_script_module",
                std::string(result.message),
                {},
                1,
                false
            });
            a_inOutBuildResult.summary = std::string(result.message);
            a_inOutBuildResult.exitCode = 1;
            a_inOutBuildResult.succeeded = false;
            push_build_message(a_inOutBuildResult, BuildMessageSeverity::Error,
                BuildStage::Reload, std::string(result.message));
            return result;
        }

        result = m_engine->load_script_module(scriptRoot);
        const bool reloadSucceeded = static_cast<bool>(result);
        const bool hasBuildStage =
            has_stage_result(a_inOutBuildResult, BuildStage::Build) ||
            has_stage_result(a_inOutBuildResult, BuildStage::Configure);
        const std::string reloadOutput = reloadSucceeded
            ? std::string("GameScript の再読み込みに成功しました。")
            : std::string(result.message);

        a_inOutBuildResult.stageResults.push_back(BuildStageResult{
            BuildStage::Reload,
            "Engine::load_script_module",
            reloadOutput,
            {},
            reloadSucceeded ? 0u : 1u,
            reloadSucceeded
        });
        a_inOutBuildResult.exitCode = reloadSucceeded ? 0u : 1u;
        a_inOutBuildResult.succeeded =
            hasBuildStage ? (a_inOutBuildResult.succeeded && reloadSucceeded)
                          : reloadSucceeded;
        a_inOutBuildResult.summary = reloadSucceeded
            ? (hasBuildStage
                ? "GameScript のビルドと再読み込みに成功しました。"
                : "GameScript の再読み込みに成功しました。")
            : (hasBuildStage
                ? "GameScript のビルド後の再読み込みに失敗しました。"
                : "GameScript の再読み込みに失敗しました。");
        push_build_message(
            a_inOutBuildResult,
            reloadSucceeded ? BuildMessageSeverity::Info
                            : BuildMessageSeverity::Error,
            BuildStage::Reload,
            reloadOutput);

        if (reloadSucceeded)
        {
            const ScriptModuleHost::ScriptReloadReport& reloadReport =
                m_engine->last_script_reload_report();
            if (reloadReport.skippedStateCount > 0)
            {
                push_build_message(
                    a_inOutBuildResult,
                    BuildMessageSeverity::Warning,
                    BuildStage::Reload,
                    std::string("state restore skipped: ") +
                        std::to_string(reloadReport.skippedStateCount) +
                        " 件");
                for (const std::string& warning : reloadReport.warnings)
                {
                    push_build_message(
                        a_inOutBuildResult,
                        BuildMessageSeverity::Warning,
                        BuildStage::Reload,
                        warning);
                }
            }
        }

        return result;
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
                const std::string detail =
                    make_primary_build_message(m_lastScriptBuildResult);
                log_result("Failed to reload GameScript", result);
                set_status_message(
                    detail.empty()
                    ? "GameScript の再読み込みに失敗しました。"
                    : "GameScript の再読み込みに失敗しました: " + detail,
                    true);
                set_script_build_notification(
                    "GameScript Reload Failed",
                    detail.empty() ? std::string(result.message) : detail,
                    true,
                    true);
                m_showScriptBuildOutput = true;
            }
            else
            {
                const std::string detail =
                    make_primary_build_message(m_lastScriptBuildResult);
                set_status_message(
                    detail.empty()
                    ? "GameScript を再読み込みしました。"
                    : "GameScript を再読み込みしました: " + detail,
                    false);
                set_script_build_notification(
                    "GameScript Reload Succeeded",
                    detail.empty()
                    ? "GameScript の再読み込みに成功しました。"
                    : detail,
                    false,
                    false);
            }
            break;

        case PendingScriptAction::Build:
            result = build_script_module();
            if (!result)
            {
                const std::string detail =
                    make_primary_build_message(m_lastScriptBuildResult);
                log_result("Failed to build GameScript", result);
                set_status_message(
                    detail.empty()
                    ? "GameScript のビルドに失敗しました。"
                    : "GameScript のビルドに失敗しました: " + detail,
                    true);
                set_script_build_notification(
                    "GameScript Build Failed",
                    detail.empty() ? std::string(result.message) : detail,
                    true,
                    true);
                m_showScriptBuildOutput = true;
            }
            else
            {
                const std::string detail =
                    make_primary_build_message(m_lastScriptBuildResult);
                set_status_message(
                    detail.empty()
                    ? "GameScript をビルドして再読み込みしました。"
                    : "GameScript をビルドして再読み込みしました: " + detail,
                    false);
                set_script_build_notification(
                    "GameScript Build Succeeded",
                    detail.empty()
                    ? "GameScript のビルドと再読み込みに成功しました。"
                    : detail,
                    false,
                    false);
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

    void EditorManager::set_script_build_notification(
        std::string a_title,
        std::string a_message,
        bool a_isError,
        bool a_openPopup)
    {
        m_scriptBuildNotificationTitle = std::move(a_title);
        m_scriptBuildNotificationMessage = std::move(a_message);
        m_hasScriptBuildNotification = true;
        m_hasScriptBuildNotificationError = a_isError;
        m_openScriptBuildNotificationPopup = a_openPopup;
    }

    Result EditorManager::start_play_mode()
    {
        if (m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
        }

        return m_engine->start_play_mode();
    }

    Result EditorManager::stop_play_mode()
    {
        if (m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
        }

        return m_engine->stop_play_mode();
    }

    Result EditorManager::exit_play_mode()
    {
        if (m_engine == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Engine is not initialized.");
        }

        if (!m_engine->is_playing())
        {
            return Result::ok();
        }

        return m_engine->stop_play_mode();
    }

    void EditorManager::draw_script_build_output()
    {
        if (!m_showScriptBuildOutput)
        {
            return;
        }

        if (!ImGui::Begin("Script Build Output", &m_showScriptBuildOutput))
        {
            ImGui::End();
            return;
        }

        const bool hasBuildResult =
            !m_lastScriptBuildResult.summary.empty() ||
            !m_lastScriptBuildResult.stageResults.empty() ||
            !m_lastScriptBuildResult.messages.empty() ||
            !m_lastScriptBuildResult.artifacts.empty();

        if (!hasBuildResult)
        {
            ImGui::TextUnformatted(
                "まだ GameScript build は実行されていません。");
            ImGui::End();
            return;
        }

        const ImVec4 successColor = ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
        const ImVec4 errorColor = ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
        const ImVec4 warningColor = ImVec4(0.95f, 0.75f, 0.30f, 1.0f);

        ImGui::Text("Summary");
        ImGui::Separator();
        ImGui::Text("Result: ");
        ImGui::SameLine();
        ImGui::TextColored(
            m_lastScriptBuildResult.succeeded ? successColor : errorColor,
            m_lastScriptBuildResult.succeeded ? "Succeeded" : "Failed");
        const std::string primaryMessage =
            make_primary_build_message(m_lastScriptBuildResult);
        if (!m_lastScriptBuildResult.summary.empty())
        {
            ImGui::TextWrapped("%s", m_lastScriptBuildResult.summary.c_str());
        }
        if (!primaryMessage.empty() &&
            primaryMessage != m_lastScriptBuildResult.summary)
        {
            ImGui::TextColored(
                m_lastScriptBuildResult.succeeded ? successColor : errorColor,
                "Primary: %s",
                primaryMessage.c_str());
        }
        ImGui::Text("Exit Code: %u", m_lastScriptBuildResult.exitCode);
        ImGui::Text("Did Configure: %s",
            m_lastScriptBuildResult.didConfigure ? "Yes" : "No");

        if (ImGui::Button("Open Solution"))
        {
            const Result result = open_script_solution_in_visual_studio();
            if (!result)
            {
                log_result("Failed to open GameScript solution", result);
                set_status_message(
                    "GameScript solution を開けませんでした。", true);
            }
            else
            {
                set_status_message(
                    "GameScript solution を Visual Studio で開きました。",
                    false);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Attach Editor"))
        {
            const Result result = attach_editor_debugger_in_visual_studio();
            if (!result)
            {
                log_result("Failed to attach debugger", result);
                set_status_message(
                    "Visual Studio から Editor にアタッチできませんでした。",
                    true);
            }
            else
            {
                set_status_message(
                    "Visual Studio から Editor にアタッチしました。",
                    false);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Output"))
        {
            m_lastScriptBuildResult = {};
            ImGui::End();
            return;
        }

        const auto draw_path_row =
            [this](const char* a_label, const Core::IO::Path& a_path)
        {
            if (a_path.is_empty())
            {
                return;
            }

            ImGui::Text("%s", a_label);
            ImGui::SameLine();
            ImGui::PushItemWidth(-80.0f);
            std::string pathText = a_path.utf8();
            ImGui::InputText(
                (std::string("##") + a_label).c_str(),
                pathText.data(),
                pathText.size() + 1,
                ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button((std::string("Copy##") + a_label).c_str()))
            {
                ImGui::SetClipboardText(pathText.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button((std::string("Open##") + a_label).c_str()))
            {
                const Result result = open_path_in_shell(a_path);
                if (!result)
                {
                    log_result("Failed to open path", result);
                    set_status_message(
                        std::string(a_label) + " を開けませんでした。", true);
                }
                else
                {
                    set_status_message(
                        std::string(a_label) + " を開きました。", false);
                }
            }
        };

        draw_path_row("Configure Log", m_lastScriptBuildResult.configureLogPath);
        draw_path_row("Build Log", m_lastScriptBuildResult.buildLogPath);

        ImGui::Spacing();
        ImGui::Text("Stages");
        ImGui::Separator();
        if (m_lastScriptBuildResult.stageResults.empty())
        {
            ImGui::TextUnformatted("stage result はありません。");
        }
        else
        {
            for (size_t index = 0;
                 index < m_lastScriptBuildResult.stageResults.size();
                 ++index)
            {
                const BuildStageResult& stageResult =
                    m_lastScriptBuildResult.stageResults[index];
                const ImVec4 stageColor =
                    stageResult.succeeded ? successColor : errorColor;
                const std::string stageLabel =
                    "[" + std::string(to_stage_name(stageResult.stage)) + "] " +
                    (stageResult.succeeded ? "Succeeded" : "Failed") +
                    "##stage" + std::to_string(index);

                if (ImGui::TreeNodeEx(
                        stageLabel.c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Text("Stage: %s", to_stage_name(stageResult.stage));
                    ImGui::SameLine();
                    ImGui::TextColored(
                        stageColor,
                        "%s",
                        stageResult.succeeded ? "Succeeded" : "Failed");
                    ImGui::Text("Exit Code: %u", stageResult.exitCode);
                    if (!stageResult.command.empty())
                    {
                        ImGui::TextWrapped("Command: %s",
                            stageResult.command.c_str());
                    }
                    if (!stageResult.logPath.is_empty())
                    {
                        draw_path_row("Stage Log", stageResult.logPath);
                    }
                    if (!stageResult.output.empty())
                    {
                        ImGui::TextUnformatted("Output");
                        ImGui::BeginChild(
                            (std::string("StageOutput##") + std::to_string(index)).c_str(),
                            ImVec2(0.0f, 120.0f),
                            true);
                        ImGui::TextUnformatted(stageResult.output.c_str());
                        ImGui::EndChild();
                    }
                    ImGui::TreePop();
                }
            }
        }

        ImGui::Spacing();
        ImGui::Text("Messages");
        ImGui::Separator();
        if (m_lastScriptBuildResult.messages.empty())
        {
            ImGui::TextUnformatted("message はありません。");
        }
        else
        {
            ImGui::BeginChild("BuildMessages", ImVec2(0.0f, 140.0f), true);
            for (const BuildMessage& message : m_lastScriptBuildResult.messages)
            {
                ImVec4 color = successColor;
                if (message.severity == BuildMessageSeverity::Warning)
                {
                    color = warningColor;
                }
                else if (message.severity == BuildMessageSeverity::Error)
                {
                    color = errorColor;
                }

                ImGui::TextColored(
                    color,
                    "[%s][%s]",
                    to_stage_name(message.stage),
                    to_severity_name(message.severity));
                ImGui::SameLine();
                ImGui::TextWrapped("%s", message.text.c_str());
            }
            ImGui::EndChild();
        }

        ImGui::Spacing();
        ImGui::Text("Artifacts");
        ImGui::Separator();
        if (m_lastScriptBuildResult.artifacts.empty())
        {
            ImGui::TextUnformatted("artifact はありません。");
        }
        else
        {
            for (const BuildArtifact& artifact : m_lastScriptBuildResult.artifacts)
            {
                ImGui::Text("%s", artifact.name.c_str());
                draw_path_row("Path", artifact.path);
            }
        }

        ImGui::End();
    }

    void EditorManager::draw_script_build_notification_popup()
    {
        if (m_openScriptBuildNotificationPopup)
        {
            ImGui::OpenPopup("Script Build Notification");
            m_openScriptBuildNotificationPopup = false;
        }

        if (!ImGui::BeginPopupModal(
                "Script Build Notification",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        const ImVec4 messageColor = m_hasScriptBuildNotificationError
            ? ImVec4(0.95f, 0.35f, 0.35f, 1.0f)
            : ImVec4(0.35f, 0.85f, 0.45f, 1.0f);
        if (!m_scriptBuildNotificationTitle.empty())
        {
            ImGui::TextColored(
                messageColor,
                "%s",
                m_scriptBuildNotificationTitle.c_str());
            ImGui::Separator();
        }

        if (!m_scriptBuildNotificationMessage.empty())
        {
            ImGui::TextWrapped(
                "%s",
                m_scriptBuildNotificationMessage.c_str());
        }

        ImGui::Spacing();
        if (ImGui::Button("Build Output を開く"))
        {
            m_showScriptBuildOutput = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("閉じる"))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorManager::draw_create_script_popup()
    {
        if (m_openCreateScriptPopup)
        {
            ImGui::OpenPopup("Create Script");
            m_openCreateScriptPopup = false;
            m_focusCreateScriptNameInput = true;
        }

        if (!ImGui::BeginPopupModal(
                "Create Script",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextUnformatted("Assets/Scripts/ に新しい Script を作成します。");
        ImGui::Spacing();
        ImGui::TextUnformatted("Script 名");
        if (m_focusCreateScriptNameInput)
        {
            ImGui::SetKeyboardFocusHere();
            m_focusCreateScriptNameInput = false;
        }

        const bool submitted = ImGui::InputText(
            "##CreateScriptName",
            m_createScriptNameBuffer.data(),
            m_createScriptNameBuffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TextDisabled("例: TestCube");

        auto submit = [this]()
        {
            const std::string scriptName = m_createScriptNameBuffer.data();
            const Result result = create_script_template(scriptName);
            if (!result)
            {
                log_result("Failed to create script template", result);
                set_status_message(
                    result.message.empty()
                        ? "Script 作成に失敗しました。"
                        : std::string(result.message),
                    true);
                return;
            }

            m_createScriptNameBuffer.fill('\0');
            set_status_message(
                std::string("Script を作成しました: ") + scriptName,
                false);
            ImGui::CloseCurrentPopup();
        };

        ImGui::Spacing();
        if (submitted || ImGui::Button("作成"))
        {
            submit();
        }
        ImGui::SameLine();
        if (ImGui::Button("キャンセル"))
        {
            m_createScriptNameBuffer.fill('\0');
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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

            if (ImGui::BeginMenu("実行"))
            {
                const bool isPlaying =
                    m_engine != nullptr && m_engine->is_playing();
                const bool canStartPlay =
                    m_engine != nullptr && !m_projectPath.empty() &&
                    !m_isScriptActionActive && !isPlaying;
                const bool canStopPlay =
                    m_engine != nullptr && !m_isScriptActionActive && isPlaying;

                if (ImGui::MenuItem("Play", nullptr, false, canStartPlay))
                {
                    const Result result = start_play_mode();
                    if (!result)
                    {
                        log_result("Failed to start play mode", result);
                        set_status_message("Play 開始に失敗しました。", true);
                    }
                    else
                    {
                        set_status_message("Play を開始しました。", false);
                    }
                }

                if (ImGui::MenuItem("Stop", nullptr, false, canStopPlay))
                {
                    const Result result = stop_play_mode();
                    if (!result)
                    {
                        log_result("Failed to stop play mode", result);
                        set_status_message("Play 停止に失敗しました。", true);
                    }
                    else
                    {
                        set_status_message("Play を停止しました。", false);
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit", nullptr, false, canStopPlay))
                {
                    const Result result = exit_play_mode();
                    if (!result)
                    {
                        log_result("Failed to exit play mode", result);
                        set_status_message("Play 終了に失敗しました。", true);
                    }
                    else
                    {
                        set_status_message("Play を終了して editor に戻りました。", false);
                    }
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

                if (ImGui::MenuItem(
                        "GameScript を追加", nullptr, false,
                        !m_projectPath.empty() && !m_isScriptActionActive))
                {
                    m_createScriptNameBuffer.fill('\0');
                    m_openCreateScriptPopup = true;
                }

                ImGui::Separator();
                if (ImGui::MenuItem(
                        "GameScript solution を開く", nullptr, false,
                        m_visualStudioBridge != nullptr && !m_projectPath.empty()))
                {
                    const Result result = open_script_solution_in_visual_studio();
                    if (!result)
                    {
                        log_result("Failed to open GameScript solution", result);
                        set_status_message(
                            "GameScript solution を開けませんでした。", true);
                    }
                    else
                    {
                        set_status_message(
                            "GameScript solution を Visual Studio で開きました。",
                            false);
                    }
                }

                if (ImGui::MenuItem(
                        "Editor にデバッガをアタッチ", nullptr, false,
                        m_visualStudioBridge != nullptr && !m_projectPath.empty()))
                {
                    const Result result =
                        attach_editor_debugger_in_visual_studio();
                    if (!result)
                    {
                        log_result("Failed to attach debugger", result);
                        set_status_message(
                            "Visual Studio から Editor にアタッチできませんでした。",
                            true);
                    }
                    else
                    {
                        set_status_message(
                            "Visual Studio から Editor にアタッチしました。",
                            false);
                    }
                }

                ImGui::Separator();
                ImGui::MenuItem(
                    "Script Build Output",
                    nullptr,
                    &m_showScriptBuildOutput,
                    true);

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

            if (m_hasScriptBuildNotification &&
                !m_scriptBuildNotificationTitle.empty())
            {
                ImGui::Separator();
                if (m_hasScriptBuildNotificationError)
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
                }
                else
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text, IM_COL32(96, 220, 120, 255));
                }

                ImGui::TextUnformatted(
                    m_scriptBuildNotificationTitle.c_str());
                ImGui::PopStyleColor();

                if (!m_scriptBuildNotificationMessage.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextUnformatted(
                        m_scriptBuildNotificationMessage.c_str());
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Build Output"))
                {
                    m_showScriptBuildOutput = true;
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
        draw_create_script_popup();
        draw_script_build_notification_popup();
        draw_script_build_output();
        m_hierarchy->update();
        m_inspector->update();
    }
}
