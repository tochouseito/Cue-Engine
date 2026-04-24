#include "VisualStudioBuildRunner.h"

// === Core includes ===
#include <IO/IFileSystem.h>

// === Win includes ===
#include <Engine/Source/Runtime/PAL/Win/ConvertUTF.h>

// === C++ includes ===
#include <string_view>
#include <vector>

namespace Cue::Editor
{
    namespace
    {
        [[nodiscard]] Result resolve_executable_path(
            std::wstring_view a_fileName,
            std::string& a_outPath) noexcept
        {
            a_outPath.clear();

            wchar_t buffer[MAX_PATH]{};
            const DWORD length = ::SearchPathW(
                nullptr,
                a_fileName.data(),
                nullptr,
                static_cast<DWORD>(std::size(buffer)),
                buffer,
                nullptr);
            if (length == 0)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "dotnet.exe が見つかりません。PATH を確認してください。");
            }

            return PAL::Win::wide_to_utf8(
                std::wstring_view(buffer, length), &a_outPath);
        }

        [[nodiscard]] Result resolve_cmake_executable_path(
            std::string& a_outPath) noexcept
        {
            a_outPath.clear();

            wchar_t buffer[MAX_PATH]{};
            const DWORD length = ::SearchPathW(
                nullptr,
                L"cmake.exe",
                nullptr,
                static_cast<DWORD>(std::size(buffer)),
                buffer,
                nullptr);
            if (length == 0)
            {
                return Result::fail(Code::NotFound, Severity::Error,
                    "cmake.exe が見つかりません。PATH を確認してください。");
            }

            return PAL::Win::wide_to_utf8(
                std::wstring_view(buffer, length), &a_outPath);
        }

        [[nodiscard]] Core::IO::Path get_tool_project_path() noexcept
        {
#ifdef CUE_PROJECT_ROOT_PATH
            return Core::IO::Path(
                std::string(CUE_PROJECT_ROOT_PATH) +
                "/Tools/VisualStudioBridge/VisualStudioBridge.csproj");
#else
            return Core::IO::Path("Tools/VisualStudioBridge/VisualStudioBridge.csproj");
#endif
        }

        [[nodiscard]] bool is_solution_file(const Core::IO::Path& a_path) noexcept
        {
            const std::string extension = a_path.extension();
            return extension == ".sln" || extension == ".slnx";
        }

        void push_message(
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

        void push_message(
            GameReleaseBuildResult& a_result,
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

        [[nodiscard]] std::string make_cmake_target_arguments(
            std::string_view a_primaryTarget,
            std::string_view a_secondaryTarget)
        {
            std::string result{};

            if (!a_primaryTarget.empty())
            {
                result += " ";
                result += a_primaryTarget;
            }

            if (!a_secondaryTarget.empty())
            {
                result += " ";
                result += a_secondaryTarget;
            }

            return result;
        }

        [[nodiscard]] Result execute_command(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_workingDirectory,
            std::string_view a_command,
            BuildStage a_stage,
            const Core::IO::Path& a_outputPath,
            BuildStageResult& a_outResult) noexcept
        {
            a_outResult = {};
            a_outResult.stage = a_stage;
            a_outResult.command = std::string(a_command);
            a_outResult.logPath = a_outputPath;

            Result result = a_fileSystem.create_directories(a_outputPath.parent());
            if (!result)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    "ビルドログ用ディレクトリの作成に失敗しました。");
            }

            bool removed = false;
            result = a_fileSystem.remove(a_outputPath, &removed);
            if (!result && result.code != Code::NotFound)
            {
                return result;
            }

            std::wstring wideWorkingDirectory{};
            result = PAL::Win::utf8_to_wide(a_workingDirectory.utf8(),
                &wideWorkingDirectory);
            if (!result)
            {
                return result;
            }

            std::wstring wideCommand{};
            result = PAL::Win::utf8_to_wide(std::string(a_command), &wideCommand);
            if (!result)
            {
                return result;
            }

            std::wstring wideOutputPath{};
            result = PAL::Win::utf8_to_wide(a_outputPath.utf8(), &wideOutputPath);
            if (!result)
            {
                return result;
            }

            std::wstring commandLine =
                L"cmd.exe /d /c \"" + wideCommand + L" > \"" + wideOutputPath +
                L"\" 2>&1\"";
            std::vector<wchar_t> commandBuffer(
                commandLine.begin(), commandLine.end());
            commandBuffer.push_back(L'\0');

            STARTUPINFOW startupInfo{};
            startupInfo.cb = sizeof(startupInfo);

            PROCESS_INFORMATION processInfo{};
            const BOOL created = ::CreateProcessW(
                nullptr,
                commandBuffer.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_NO_WINDOW,
                nullptr,
                wideWorkingDirectory.c_str(),
                &startupInfo,
                &processInfo);
            if (created == FALSE)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    "ビルドプロセスの起動に失敗しました。");
            }

            const DWORD waitResult =
                ::WaitForSingleObject(processInfo.hProcess, INFINITE);
            if (waitResult != WAIT_OBJECT_0)
            {
                ::CloseHandle(processInfo.hThread);
                ::CloseHandle(processInfo.hProcess);
                return Result::fail(Code::InternalError, Severity::Error,
                    "ビルドプロセスの完了待機に失敗しました。");
            }

            DWORD exitCode = 0;
            const BOOL gotExitCode =
                ::GetExitCodeProcess(processInfo.hProcess, &exitCode);

            ::CloseHandle(processInfo.hThread);
            ::CloseHandle(processInfo.hProcess);

            if (gotExitCode == FALSE)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "ビルドプロセスの終了コード取得に失敗しました。");
            }

            a_outResult.exitCode = static_cast<uint32_t>(exitCode);

            std::string output{};
            bool exists = false;
            result = a_fileSystem.exists(a_outputPath, &exists);
            if (!result)
            {
                return result;
            }
            if (exists)
            {
                std::vector<std::byte> data{};
                result = a_fileSystem.read_all(a_outputPath, &data);
                if (!result)
                {
                    return result;
                }

                output.assign(
                    reinterpret_cast<const char*>(data.data()),
                    data.size());
            }

            a_outResult.output = std::move(output);
            a_outResult.succeeded = exitCode == 0;
            if (!a_outResult.succeeded)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "ビルドコマンドの実行に失敗しました。");
            }

            return Result::ok();
        }
    }

    Result VisualStudioBuildRunner::plan_script_build(
        const ScriptBuildRequest& a_request,
        ScriptBuildPlan& a_outPlan) const noexcept
    {
        a_outPlan = {};

        ScriptBuildValidation validation{};
        Result result =
            validate_script_build_environment(a_request, validation);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path configureDirectory = Core::IO::Path::join(
            a_request.scriptRoot,
            Core::IO::Path("out/build/" + a_request.configurePreset));
        const Core::IO::Path buildDirectory = Core::IO::Path::join(
            configureDirectory,
            Core::IO::Path(a_request.target));

        bool configureDirectoryExists = false;
        result = m_fileSystem.exists(configureDirectory, &configureDirectoryExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "VisualStudio configure ディレクトリの確認に失敗しました。");
        }

        bool hasSolution = false;
        if (configureDirectoryExists)
        {
            std::vector<Core::IO::Path> entries{};
            result = m_fileSystem.list_directory(configureDirectory, &entries);
            if (!result)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "VisualStudio solution の確認に失敗しました。");
            }

            for (const Core::IO::Path& entry : entries)
            {
                if (is_solution_file(entry))
                {
                    hasSolution = true;
                    break;
                }
            }
        }

        a_outPlan.scriptRoot = a_request.scriptRoot;
        a_outPlan.presetsPath = validation.presetsPath;
        a_outPlan.configureDirectory = configureDirectory;
        a_outPlan.buildDirectory = buildDirectory;
        a_outPlan.requiresConfigure = !hasSolution;
        a_outPlan.configureCommand =
            "cmake --preset " + a_request.configurePreset + " --fresh";
        a_outPlan.buildCommand =
            "MSBuild <solution> /t:" + a_request.target +
            " /p:Configuration=" +
            std::string(BuildSystem::to_configuration_name(a_request.configuration));
        return Result::ok();
    }

    Result VisualStudioBuildRunner::validate_script_build_environment(
        const ScriptBuildRequest& a_request,
        ScriptBuildValidation& a_outValidation) const noexcept
    {
        a_outValidation = {};

        if (a_request.scriptRoot.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script root が指定されていません。");
        }

        bool scriptRootExists = false;
        Result result = m_fileSystem.exists(a_request.scriptRoot, &scriptRootExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Script root の確認に失敗しました。");
        }

        if (!scriptRootExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Script root が存在しません。");
        }

        Core::IO::FileStat scriptRootStat{};
        result = m_fileSystem.stat(a_request.scriptRoot, &scriptRootStat);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Script root の情報取得に失敗しました。");
        }

        if (scriptRootStat.type != Core::IO::FileType::directory)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Script root はディレクトリである必要があります。");
        }

        const Core::IO::Path presetsPath = Core::IO::Path::join(
            a_request.scriptRoot, Core::IO::Path("CMakePresets.json"));
        bool presetsExists = false;
        result = m_fileSystem.exists(presetsPath, &presetsExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "CMakePresets.json の確認に失敗しました。");
        }

        if (!presetsExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "CMakePresets.json が見つかりません。");
        }

        std::string dotnetPath{};
        result = resolve_executable_path(L"dotnet.exe", dotnetPath);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path toolProjectPath = get_tool_project_path();
        bool toolProjectExists = false;
        result = m_fileSystem.exists(toolProjectPath, &toolProjectExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "VisualStudioBridge.csproj の確認に失敗しました。");
        }

        if (!toolProjectExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "VisualStudioBridge.csproj が見つかりません。");
        }

        a_outValidation.scriptRoot = a_request.scriptRoot;
        a_outValidation.presetsPath = presetsPath;
        return Result::ok();
    }

    Result VisualStudioBuildRunner::execute_script_build(
        const ScriptBuildRequest& a_request,
        BuildResult& a_outResult) const noexcept
    {
        a_outResult = {};

        Result result = plan_script_build(a_request, a_outResult.plan);
        if (!result)
        {
            a_outResult.summary = result.message;
            push_message(a_outResult, BuildMessageSeverity::Error, BuildStage::General,
                std::string(result.message));
            return result;
        }

        const ScriptBuildPlan plan = a_outResult.plan;
        result = m_bridge.execute_script_build(a_request, a_outResult);
        a_outResult.plan = plan;
        if (!result && a_outResult.summary.empty())
        {
            a_outResult.summary = result.message;
            push_message(a_outResult, BuildMessageSeverity::Error, BuildStage::General,
                std::string(result.message));
        }

        return result;
    }

    Result VisualStudioBuildRunner::execute_script_configure(
        const ScriptBuildRequest& a_request,
        BuildResult& a_outResult) const noexcept
    {
        a_outResult = {};

        Result result = plan_script_build(a_request, a_outResult.plan);
        if (!result)
        {
            a_outResult.summary = result.message;
            push_message(a_outResult, BuildMessageSeverity::Error, BuildStage::General,
                std::string(result.message));
            return result;
        }

        const Core::IO::Path logRoot = Core::IO::Path::join(
            a_request.scriptRoot,
            Core::IO::Path("Intermediate/BuildSystem"));
        a_outResult.configureLogPath =
            Core::IO::Path::join(logRoot, Core::IO::Path("Configure.log"));

        BuildStageResult configureStage{};
        result = execute_command(
            m_fileSystem,
            a_request.scriptRoot,
            a_outResult.plan.configureCommand,
            BuildStage::Configure,
            a_outResult.configureLogPath,
            configureStage);
        a_outResult.didConfigure = true;
        a_outResult.stageResults.push_back(std::move(configureStage));
        a_outResult.exitCode = a_outResult.stageResults.back().exitCode;
        if (!result)
        {
            a_outResult.summary = "CMake configure に失敗しました。";
            push_message(a_outResult, BuildMessageSeverity::Error,
                BuildStage::Configure, a_outResult.summary);
            return result;
        }

        a_outResult.succeeded = true;
        a_outResult.summary = "CMake configure が成功しました。";
        push_message(a_outResult, BuildMessageSeverity::Info,
            BuildStage::Configure, a_outResult.summary);
        return Result::ok();
    }

    Result VisualStudioBuildRunner::plan_game_release_build(
        const GameReleaseBuildRequest& a_request,
        GameReleaseBuildPlan& a_outPlan) const noexcept
    {
        a_outPlan = {};

        GameReleaseBuildValidation validation{};
        Result result =
            validate_game_release_build_environment(a_request, validation);
        if (!result)
        {
            return result;
        }

        const Core::IO::Path configureDirectory = Core::IO::Path::join(
            a_request.projectRoot,
            Core::IO::Path("out/build/" + a_request.configurePreset));
        const Core::IO::Path buildDirectory = Core::IO::Path::join(
            configureDirectory,
            Core::IO::Path(a_request.appTarget.empty()
                ? a_request.gameTarget
                : a_request.appTarget));

        bool configureDirectoryExists = false;
        result = m_fileSystem.exists(configureDirectory, &configureDirectoryExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "VisualStudio configure ディレクトリの確認に失敗しました。");
        }

        bool hasSolution = false;
        if (configureDirectoryExists)
        {
            std::vector<Core::IO::Path> entries{};
            result = m_fileSystem.list_directory(configureDirectory, &entries);
            if (!result)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "VisualStudio solution の確認に失敗しました。");
            }

            for (const Core::IO::Path& entry : entries)
            {
                if (is_solution_file(entry))
                {
                    hasSolution = true;
                    break;
                }
            }
        }

        a_outPlan.projectRoot = a_request.projectRoot;
        a_outPlan.presetsPath = validation.presetsPath;
        a_outPlan.configureDirectory = configureDirectory;
        a_outPlan.buildDirectory = buildDirectory;
        a_outPlan.requiresConfigure = !hasSolution;
        a_outPlan.configureCommand =
            "cmake --preset " + a_request.configurePreset + " --fresh";
        a_outPlan.buildCommand =
            "cmake --build out/build/" + a_request.configurePreset +
            " --config " + BuildSystem::to_configuration_name(a_request.configuration) +
            " --target" +
            make_cmake_target_arguments(a_request.gameTarget, a_request.appTarget);
        return Result::ok();
    }

    Result VisualStudioBuildRunner::validate_game_release_build_environment(
        const GameReleaseBuildRequest& a_request,
        GameReleaseBuildValidation& a_outValidation) const noexcept
    {
        a_outValidation = {};

        if (a_request.projectRoot.is_empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Project root が指定されていません。");
        }

        bool projectRootExists = false;
        Result result = m_fileSystem.exists(a_request.projectRoot, &projectRootExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Project root の確認に失敗しました。");
        }

        if (!projectRootExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Project root が存在しません。");
        }

        Core::IO::FileStat projectRootStat{};
        result = m_fileSystem.stat(a_request.projectRoot, &projectRootStat);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "Project root の情報取得に失敗しました。");
        }

        if (projectRootStat.type != Core::IO::FileType::directory)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Project root はディレクトリである必要があります。");
        }

        const Core::IO::Path presetsPath = Core::IO::Path::join(
            a_request.projectRoot, Core::IO::Path("CMakePresets.json"));
        bool presetsExists = false;
        result = m_fileSystem.exists(presetsPath, &presetsExists);
        if (!result)
        {
            return Result::fail(Code::GetFailed, Severity::Error,
                "CMakePresets.json の確認に失敗しました。");
        }

        if (!presetsExists)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "CMakePresets.json が見つかりません。");
        }

        std::string cmakePath{};
        result = resolve_cmake_executable_path(cmakePath);
        if (!result)
        {
            return result;
        }

        a_outValidation.projectRoot = a_request.projectRoot;
        a_outValidation.presetsPath = presetsPath;
        a_outValidation.cmakePath = std::move(cmakePath);
        return Result::ok();
    }

    Result VisualStudioBuildRunner::execute_game_release_configure(
        const GameReleaseBuildRequest& a_request,
        GameReleaseBuildResult& a_outResult) const noexcept
    {
        a_outResult = {};

        Result result = plan_game_release_build(a_request, a_outResult.plan);
        if (!result)
        {
            a_outResult.summary = result.message;
            push_message(a_outResult, BuildMessageSeverity::Error, BuildStage::General,
                std::string(result.message));
            return result;
        }

        const Core::IO::Path logRoot = Core::IO::Path::join(
            a_request.projectRoot,
            Core::IO::Path("Intermediate/BuildSystem/GameRelease"));
        a_outResult.configureLogPath =
            Core::IO::Path::join(logRoot, Core::IO::Path("Configure.log"));

        BuildStageResult configureStage{};
        result = execute_command(
            m_fileSystem,
            a_request.projectRoot,
            a_outResult.plan.configureCommand,
            BuildStage::Configure,
            a_outResult.configureLogPath,
            configureStage);
        a_outResult.didConfigure = true;
        a_outResult.stageResults.push_back(std::move(configureStage));
        a_outResult.exitCode = a_outResult.stageResults.back().exitCode;
        if (!result)
        {
            a_outResult.summary = "Game Release 用 configure に失敗しました。";
            push_message(a_outResult, BuildMessageSeverity::Error,
                BuildStage::Configure, a_outResult.summary);
            return result;
        }

        a_outResult.succeeded = true;
        a_outResult.summary = "Game Release 用 configure が成功しました。";
        push_message(a_outResult, BuildMessageSeverity::Info,
            BuildStage::Configure, a_outResult.summary);
        return Result::ok();
    }

    Result VisualStudioBuildRunner::execute_game_release_build(
        const GameReleaseBuildRequest& a_request,
        GameReleaseBuildResult& a_outResult) const noexcept
    {
        a_outResult = {};

        Result result = plan_game_release_build(a_request, a_outResult.plan);
        if (!result)
        {
            a_outResult.summary = result.message;
            push_message(a_outResult, BuildMessageSeverity::Error, BuildStage::General,
                std::string(result.message));
            return result;
        }

        const Core::IO::Path logRoot = Core::IO::Path::join(
            a_request.projectRoot,
            Core::IO::Path("Intermediate/BuildSystem/GameRelease"));
        a_outResult.configureLogPath =
            Core::IO::Path::join(logRoot, Core::IO::Path("Configure.log"));
        a_outResult.buildLogPath =
            Core::IO::Path::join(logRoot, Core::IO::Path("Build.log"));

        if (a_outResult.plan.requiresConfigure)
        {
            BuildStageResult configureStage{};
            result = execute_command(
                m_fileSystem,
                a_request.projectRoot,
                a_outResult.plan.configureCommand,
                BuildStage::Configure,
                a_outResult.configureLogPath,
                configureStage);
            a_outResult.didConfigure = true;
            a_outResult.stageResults.push_back(std::move(configureStage));
            if (!result)
            {
                a_outResult.exitCode = a_outResult.stageResults.back().exitCode;
                a_outResult.summary = "Game Release 用 configure に失敗しました。";
                push_message(a_outResult, BuildMessageSeverity::Error,
                    BuildStage::Configure, a_outResult.summary);
                return result;
            }
        }

        BuildStageResult buildStage{};
        result = execute_command(
            m_fileSystem,
            a_request.projectRoot,
            a_outResult.plan.buildCommand,
            BuildStage::Build,
            a_outResult.buildLogPath,
            buildStage);
        a_outResult.stageResults.push_back(std::move(buildStage));
        a_outResult.exitCode = a_outResult.stageResults.back().exitCode;
        if (!result)
        {
            a_outResult.summary = "Game Release 用 build に失敗しました。";
            push_message(a_outResult, BuildMessageSeverity::Error,
                BuildStage::Build, a_outResult.summary);
            return result;
        }

        a_outResult.succeeded = true;
        a_outResult.summary = "Game Release 用 build が成功しました。";
        push_message(a_outResult, BuildMessageSeverity::Info,
            BuildStage::Build, a_outResult.summary);
        return Result::ok();
    }
}
