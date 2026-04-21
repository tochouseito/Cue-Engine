#include "VisualStudioBridge.h"

// === Core includes ===
#include <IO/IFileSystem.h>

// === Win includes ===
#include <Engine/Source/Runtime/PAL/Win/ConvertUTF.h>

// === C++ includes ===
#include <span>
#include <string_view>
#include <vector>

// === ThirdParty includes ===
#include <nlohmann/json.hpp>

namespace Cue::Editor
{
    namespace
    {
        using Json = nlohmann::json;

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

        [[nodiscard]] Result read_text_file(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            std::string& a_outText) noexcept
        {
            a_outText.clear();

            bool exists = false;
            Result result = a_fileSystem.exists(a_filePath, &exists);
            if (!result)
            {
                return result;
            }
            if (!exists)
            {
                return Result::ok();
            }

            std::vector<std::byte> data{};
            result = a_fileSystem.read_all(a_filePath, &data);
            if (!result)
            {
                return result;
            }

            a_outText.assign(
                reinterpret_cast<const char*>(data.data()),
                data.size());
            return Result::ok();
        }

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

        [[nodiscard]] Result execute_command(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_workingDirectory,
            std::string_view a_command,
            const Core::IO::Path& a_outputPath,
            uint32_t& a_outExitCode,
            std::string& a_outOutput) noexcept
        {
            a_outExitCode = 0;
            a_outOutput.clear();

            Result result = a_fileSystem.create_directories(a_outputPath.parent());
            if (!result)
            {
                return result;
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
                    "VisualStudioBridge プロセスの起動に失敗しました。");
            }

            const DWORD waitResult =
                ::WaitForSingleObject(processInfo.hProcess, INFINITE);
            if (waitResult != WAIT_OBJECT_0)
            {
                ::CloseHandle(processInfo.hThread);
                ::CloseHandle(processInfo.hProcess);
                return Result::fail(Code::InternalError, Severity::Error,
                    "VisualStudioBridge プロセスの完了待機に失敗しました。");
            }

            DWORD exitCode = 0;
            const BOOL gotExitCode =
                ::GetExitCodeProcess(processInfo.hProcess, &exitCode);

            ::CloseHandle(processInfo.hThread);
            ::CloseHandle(processInfo.hProcess);

            if (gotExitCode == FALSE)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "VisualStudioBridge 終了コードの取得に失敗しました。");
            }

            a_outExitCode = static_cast<uint32_t>(exitCode);
            return read_text_file(a_fileSystem, a_outputPath, a_outOutput);
        }

        [[nodiscard]] BuildStage parse_stage(std::string_view a_stage) noexcept
        {
            if (a_stage == "Configure")
            {
                return BuildStage::Configure;
            }
            if (a_stage == "Build")
            {
                return BuildStage::Build;
            }
            if (a_stage == "Reload")
            {
                return BuildStage::Reload;
            }
            if (a_stage == "Attach")
            {
                return BuildStage::Attach;
            }
            return BuildStage::General;
        }

        [[nodiscard]] BuildMessageSeverity parse_severity(
            std::string_view a_severity) noexcept
        {
            if (a_severity == "Warning")
            {
                return BuildMessageSeverity::Warning;
            }
            if (a_severity == "Error")
            {
                return BuildMessageSeverity::Error;
            }
            return BuildMessageSeverity::Info;
        }

        [[nodiscard]] Result parse_build_result_json(
            const std::string& a_text,
            BuildResult& a_outResult) noexcept
        {
            try
            {
                const Json root = Json::parse(a_text);

                a_outResult.succeeded = root.value("Succeeded", false);
                a_outResult.didConfigure = root.value("DidConfigure", false);
                a_outResult.exitCode = root.value("ExitCode", 0u);
                a_outResult.summary = root.value("Summary", std::string{});
                a_outResult.configureLogPath =
                    Core::IO::Path(root.value("ConfigureLogPath", std::string{}));
                a_outResult.buildLogPath =
                    Core::IO::Path(root.value("BuildLogPath", std::string{}));

                const Json stageResults = root.value("StageResults", Json::array());
                for (const Json& stageResultJson : stageResults)
                {
                    a_outResult.stageResults.push_back(BuildStageResult{
                        parse_stage(stageResultJson.value("Stage", std::string{})),
                        stageResultJson.value("Command", std::string{}),
                        stageResultJson.value("Output", std::string{}),
                        Core::IO::Path(stageResultJson.value("LogPath", std::string{})),
                        stageResultJson.value("ExitCode", 0u),
                        stageResultJson.value("Succeeded", false)
                    });
                }

                const Json messages = root.value("Messages", Json::array());
                for (const Json& messageJson : messages)
                {
                    a_outResult.messages.push_back(BuildMessage{
                        parse_severity(messageJson.value("Severity", std::string{})),
                        parse_stage(messageJson.value("Stage", std::string{})),
                        messageJson.value("Text", std::string{})
                    });
                }

                const Json artifacts = root.value("Artifacts", Json::array());
                for (const Json& artifactJson : artifacts)
                {
                    a_outResult.artifacts.push_back(BuildArtifact{
                        artifactJson.value("Name", std::string{}),
                        Core::IO::Path(artifactJson.value("Path", std::string{}))
                    });
                }

                return Result::ok();
            }
            catch (...)
            {
                return Result::fail(Code::GetFailed, Severity::Error,
                    "VisualStudioBridge の結果 JSON 解析に失敗しました。");
            }
        }

        [[nodiscard]] Result execute_tool_command(
            Core::IO::IFileSystem& a_fileSystem,
            std::string_view a_toolCommand,
            std::string_view a_toolArguments,
            const Core::IO::Path& a_logPath,
            std::string_view a_failureMessage,
            std::string& a_outOutput) noexcept
        {
            a_outOutput.clear();

            std::string dotnetPath{};
            Result result = resolve_executable_path(L"dotnet.exe", dotnetPath);
            if (!result)
            {
                return result;
            }

            const Core::IO::Path toolProjectPath = get_tool_project_path();
            std::string command =
                "\"" + dotnetPath + "\" run --project \"" + toolProjectPath.utf8() +
                "\" --configuration Debug -- " + std::string(a_toolCommand);
            if (!a_toolArguments.empty())
            {
                command += " ";
                command += a_toolArguments;
            }

            uint32_t exitCode = 0;
            result = execute_command(
                a_fileSystem,
                toolProjectPath.parent(),
                command,
                a_logPath,
                exitCode,
                a_outOutput);
            if (!result)
            {
                return result;
            }

            if (exitCode == 0)
            {
                return Result::ok();
            }

            return Result::fail(Code::InvalidState, Severity::Error,
                a_failureMessage);
        }
    }

    Result VisualStudioBridge::execute_script_build(
        const ScriptBuildRequest& a_request,
        BuildResult& a_outResult) const noexcept
    {
        a_outResult = {};

        const Core::IO::Path logRoot = Core::IO::Path::join(
            a_request.scriptRoot,
            Core::IO::Path("Intermediate/BuildSystem"));
        const Core::IO::Path resultPath = Core::IO::Path::join(
            logRoot,
            Core::IO::Path("VisualStudioBridgeResult.json"));
        const Core::IO::Path toolOutputPath = Core::IO::Path::join(
            logRoot,
            Core::IO::Path("VisualStudioBridge.log"));

        std::string dotnetPath{};
        Result result = resolve_executable_path(L"dotnet.exe", dotnetPath);
        if (!result)
        {
            a_outResult.summary = result.message;
            return result;
        }

        const Core::IO::Path toolProjectPath = get_tool_project_path();

        std::string command =
            "\"" + dotnetPath + "\" run --project \"" + toolProjectPath.utf8() +
            "\" --configuration Debug -- build-script" +
            " --script-root \"" + a_request.scriptRoot.utf8() + "\"" +
            " --configure-preset \"" + a_request.configurePreset + "\"" +
            " --configuration \"" +
            std::string(BuildSystem::to_configuration_name(a_request.configuration)) + "\"" +
            " --target \"" + a_request.target + "\"" +
            " --result-path \"" + resultPath.utf8() + "\"" +
            " --configure-log-path \"" +
            Core::IO::Path::join(logRoot, Core::IO::Path("Configure.log")).utf8() + "\"" +
            " --build-log-path \"" +
            Core::IO::Path::join(logRoot, Core::IO::Path("Build.log")).utf8() + "\"";

        uint32_t exitCode = 0;
        std::string toolOutput{};
        result = execute_command(
            m_fileSystem,
            toolProjectPath.parent(),
            command,
            toolOutputPath,
            exitCode,
            toolOutput);
        if (!result)
        {
            a_outResult.summary = result.message;
            a_outResult.messages.push_back(BuildMessage{
                BuildMessageSeverity::Error,
                BuildStage::General,
                a_outResult.summary
            });
            return result;
        }

        std::string resultJson{};
        result = read_text_file(m_fileSystem, resultPath, resultJson);
        if (!result)
        {
            a_outResult.summary = result.message;
            return result;
        }
        if (resultJson.empty())
        {
            a_outResult.summary =
                "VisualStudioBridge の結果 JSON が生成されませんでした。";
            a_outResult.exitCode = exitCode;
            a_outResult.messages.push_back(BuildMessage{
                BuildMessageSeverity::Error,
                BuildStage::General,
                a_outResult.summary
            });
            return Result::fail(Code::GetFailed, Severity::Error,
                "VisualStudioBridge result file was empty.");
        }

        result = parse_build_result_json(resultJson, a_outResult);
        if (!result)
        {
            a_outResult.exitCode = exitCode;
            return result;
        }

        if (!toolOutput.empty())
        {
            a_outResult.messages.push_back(BuildMessage{
                exitCode == 0 ? BuildMessageSeverity::Info : BuildMessageSeverity::Warning,
                BuildStage::General,
                toolOutput
            });
        }

        if (exitCode != 0 && a_outResult.succeeded)
        {
            a_outResult.succeeded = false;
        }

        return a_outResult.succeeded
            ? Result::ok()
            : Result::fail(Code::InvalidState, Severity::Error,
                a_outResult.summary.empty()
                    ? "VisualStudioBridge build failed."
                    : a_outResult.summary);
    }

    Result VisualStudioBridge::open_solution(
        const Core::IO::Path& a_scriptRoot,
        std::string_view a_configurePreset) const noexcept
    {
        const Core::IO::Path logPath = Core::IO::Path::join(
            a_scriptRoot,
            Core::IO::Path("Intermediate/BuildSystem/OpenSolution.log"));
        std::string toolOutput{};
        const Result result = execute_tool_command(
            m_fileSystem,
            "open-solution",
            std::string("--script-root \"") + a_scriptRoot.utf8() +
                "\" --configure-preset \"" + std::string(a_configurePreset) + "\"",
            logPath,
            "VisualStudioBridge solution open failed.",
            toolOutput);
        return result;
    }

    Result VisualStudioBridge::attach_debugger(
        const Core::IO::Path& a_scriptRoot,
        std::string_view a_configurePreset,
        uint32_t a_processId) const noexcept
    {
        const Core::IO::Path logPath = Core::IO::Path::join(
            a_scriptRoot,
            Core::IO::Path("Intermediate/BuildSystem/AttachDebugger.log"));
        std::string toolOutput{};
        const Result result = execute_tool_command(
            m_fileSystem,
            "attach-debugger",
            std::string("--script-root \"") + a_scriptRoot.utf8() +
                "\" --configure-preset \"" + std::string(a_configurePreset) +
                "\" --process-id \"" + std::to_string(a_processId) + "\"",
            logPath,
            "VisualStudioBridge debugger attach failed.",
            toolOutput);
        return result;
    }
}
